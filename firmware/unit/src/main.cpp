// Split-flap display - flap unit firmware 2.3.0 (Arduino Nano / ATmega328P)
//
// 2.3.0: deep sleep for quiet hours (CMD_SLEEP): the CPU powers down until the next command.
// 2.2.0: no false "magnet missed" when a move ends a few steps before the magnet (units with a
//        small offset showing the last flap): the hall state is kept between moves.
// 2.1.0: learns its motor's steps per turn (2038 or 2048 depending on the 28BYJ-48's gearing) from
//        the distance between magnet passes, stores it in EEPROM and uses it for all flap positions.
//        All tuning values are parameters in EEPROM (see Param in unit_protocol.h), changed from the
//        master's console with "param <addr> <name> <value>" - no reflashing.
//
// What changed from 1.3.0:
//  - Non-blocking motor driver: the unit answers I2C instantly even while moving, and the status
//    it reports is always the truth (it says "moving" as soon as a letter arrives).
//  - Absolute positions: every flap's step position is computed from the magnet edge
//    (offset + flap * 2038/45), so rounding errors can't add up.
//  - Re-sync at the magnet: every time the drum passes the magnet the position is corrected and
//    the error is recorded. Slipping shows up as a number instead of a wrong letter.
//  - Diagnostics for the master (protocol v2, see shared/unit_protocol.h): supply voltage, reset
//    cause, boot count, magnet sync errors and misses, and an event log.
//  - Self-healing I2C: if the I2C hardware gets stuck it's reset; a watchdog restarts a hung unit
//    (Optiboot builds).
//  - Own coil sequencing: the Arduino Stepper library glitches once per turn with 2038 steps.
//  - Same EEPROM layout for the offset as 1.3.0, so calibrations survive the update.
//
// Hardware: 28BYJ-48 stepper on D8-D11 (ULN2003), hall sensor on D7 (LOW at the magnet),
// I2C address from DIP switches on D3-D6, onboard LED on D13 = motor running.
#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/twi.h>

#include "unit_protocol.h"

using namespace unitproto;

#define FIRMWARE_VERSION 23  // 2.3.0
#define SERIAL_BAUD 115200

// ---- Pins ----------------------------------------------------------------------------------
static const uint8_t PIN_ADDR[4] = {6, 5, 4, 3};  // DIP SW1 (bit 3) .. SW4 (bit 0), active LOW
static const uint8_t PIN_COIL[4] = {11, 9, 10, 8};  // order matches the old Stepper(2038, 11, 9, 10, 8)
static const uint8_t PIN_HALL = 7;                  // LOW = magnet
static const uint8_t PIN_LED = LED_BUILTIN;

// ---- Tuning: parameters in EEPROM (see Param in unit_protocol.h), changed with the master's
// console ("param <addr> <name> <value>") or the serial bench command "p <id> <value>".
static uint16_t params[P_COUNT];
static inline uint16_t param(Param id) { return params[id]; }

// ---- EEPROM (offset at 0 is the same as firmware 1.3.0) ------------------------------------
static const int EE_OFFSET = 0;  // uint16
static const int EE_BOOTS = 2;   // uint16
static const int EE_PARAMS = 16; // uint16 x P_COUNT (2.1.0); 0xFFFF = default

// ---- State ---------------------------------------------------------------------------------
enum class Motion : uint8_t { Idle, HomeAway, HomeSearch, HomeOffset, ToTarget, CalMove, CalJog };

static uint8_t address;
static uint16_t offset = 0;
static bool offsetUnset = false;
static uint16_t boots = 0;
static uint8_t resetCause = 0;
static uint16_t stepsPerRev = DEFAULT_STEPS_PER_REV;

// Learning steps per turn: distance between consecutive magnet edges seen while tracking
static bool edgeTracking = false;          // the previous magnet edge was seen in normal motion
static uint16_t revSamples[3] = {0, 0, 0};
static uint8_t revSampleCount = 0;

static Motion motion = Motion::Idle;
static bool homed = false;
static bool applyOffsetWhenHomed = true;  // false for the master's calibration HOME
static uint16_t position = 0;             // steps after the magnet edge
static uint16_t target = 0;               // target position (ToTarget / HomeOffset)
static uint16_t stepsLeft = 0;            // HomeAway / CalMove
static uint16_t searched = 0;             // homing: steps searched so far
static uint16_t sinceMagnet = 0;          // steps since the last magnet edge (miss detection)
static uint8_t phase = 0;                 // coil phase 0..3
static bool coilsOn = false;
static bool lastHall = false;            // hall state at the last step (kept between moves)
static uint16_t stepsThisMove = 0;
static uint32_t stepInterval = 5000;      // us
static uint32_t lastStepUs = 0;
static uint32_t motorStopMs = 0;          // when the last move finished (coils off after HOLD_MS)
static uint32_t lastMoveStart = 0;
static bool moveStartedOnce = false;

static uint8_t letter = 0;                // flap showing
static uint8_t wantedLetter = 0;          // flap requested
static bool letterPending = false;
static uint8_t rpm = 10;
static volatile int8_t status = STATUS_CAL_SEARCHING;

// Calibration mode (master "cal")
static bool calMode = false;
static uint16_t calPosition = 0;
static uint32_t lastCalCommand = 0;
static uint32_t jogStartMs = 0;
static uint8_t jogMs = 80;                // ms per step while jogging

// Diagnostics
static Info info;
static uint16_t vccMv = 0;
static uint16_t vccMinMoving = 0xFFFF;
static bool lowVoltageLogged = false;
static uint32_t lastVccMs = 0;

struct EventRecord {
  uint8_t code;
  int16_t arg;
};
static EventRecord events[EVENT_SLOTS];
static uint8_t eventCount = 0;

// I2C: everything the interrupt hands to loop()
static volatile uint8_t rxBuf[8];
static volatile uint8_t rxLen = 0;
static volatile bool rxReady = false;
static volatile uint8_t replyMode = 0;  // 0 = status, CMD_INFO, CMD_EVENTS, CMD_GET_PARAM
static volatile uint8_t replyParam = 0;
static volatile uint16_t i2cWrites = 0;
static volatile uint32_t lastI2cMs = 0;
static volatile bool stopJog = false;   // CMD_STOP takes effect immediately, even mid-move
static volatile bool sleepWanted = false;  // CMD_SLEEP: power down whenever idle, until a real command

// ---- Helpers -------------------------------------------------------------------------------
static void logEvent(uint8_t code, int16_t arg) {
  if (eventCount < EVENT_SLOTS) {
    events[eventCount++] = {code, arg};
  } else {  // keep the newest
    memmove(events, events + 1, sizeof(EventRecord) * (EVENT_SLOTS - 1));
    events[EVENT_SLOTS - 1] = {code, arg};
  }
  Serial.print(F("[event] "));
  Serial.print(code);
  Serial.print(' ');
  Serial.println(arg);
}

static uint16_t flapPosition(uint8_t flap) {
  return (uint16_t)((offset + ((uint32_t)flap * stepsPerRev + FLAP_COUNT / 2) / FLAP_COUNT) % stepsPerRev);
}

static void setSpeed(uint8_t newRpm) {
  if (newRpm < 1) newRpm = 1;
  if (newRpm > param(P_MAX_RPM)) newRpm = param(P_MAX_RPM);
  rpm = newRpm;
  stepInterval = 60000000UL / ((uint32_t)stepsPerRev * rpm);
}

static void readAddress() {
  for (uint8_t pin : PIN_ADDR) pinMode(pin, INPUT_PULLUP);
  delay(2);
  address = (!digitalRead(PIN_ADDR[3])) | (!digitalRead(PIN_ADDR[2]) << 1) | (!digitalRead(PIN_ADDR[1]) << 2) |
            (!digitalRead(PIN_ADDR[0]) << 3);
}

// ---- Parameters ----------------------------------------------------------------------------
static void applyParam(uint8_t id) {
  if (id == P_STEPS_PER_REV) {
    stepsPerRev = params[P_STEPS_PER_REV];
    if (position >= stepsPerRev) position %= stepsPerRev;
    revSampleCount = 0;
    setSpeed(rpm);
  } else if (id == P_MAX_RPM) {
    setSpeed(rpm);
  } else if (id == P_LED && !params[P_LED]) {
    digitalWrite(PIN_LED, LOW);
  } else if (id == P_HALL_ACTIVE_LOW || id == P_REVERSE) {
    homed = false;  // the position can't be trusted any more: home on the next move
    lastHall = digitalRead(PIN_HALL) == (params[P_HALL_ACTIVE_LOW] ? LOW : HIGH);
  }
}

// Returns false if the id or value is out of range. `announce` logs an EV_PARAM_SET event.
static bool setParam(uint8_t id, uint16_t value, bool announce) {
  if (id >= P_COUNT) return false;
  ParamLimits l = paramLimits(id);
  if (value < l.min || value > l.max) return false;
  if (params[id] != value) {
    params[id] = value;
    EEPROM.put(EE_PARAMS + id * 2, value);
    applyParam(id);
    if (announce) logEvent(EV_PARAM_SET, id);
  }
  return true;
}

static void loadParams() {
  for (uint8_t id = 0; id < P_COUNT; id++) {
    ParamLimits l = paramLimits(id);
    uint16_t value;
    EEPROM.get(EE_PARAMS + id * 2, value);
    params[id] = (value < l.min || value > l.max) ? l.def : value;  // 0xFFFF (never written) -> default
  }
  stepsPerRev = params[P_STEPS_PER_REV];
}

static void resetParams() {
  for (uint8_t id = 0; id < P_COUNT; id++) {
    uint16_t blank = 0xFFFF;
    EEPROM.put(EE_PARAMS + id * 2, blank);
  }
  loadParams();
  for (uint8_t id = 0; id < P_COUNT; id++) applyParam(id);
  logEvent(EV_PARAMS_RESET, 0);
}

// ---- Motor ---------------------------------------------------------------------------------
static void writeCoils() {
  static const uint8_t PATTERN[4] = {0b1010, 0b0110, 0b0101, 0b1001};  // pin1..pin4 (Stepper library 4-wire)
  uint8_t p = PATTERN[phase];
  for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_COIL[i], (p >> (3 - i)) & 1);
}

static void coilsOff() {
  for (uint8_t pin : PIN_COIL) digitalWrite(pin, LOW);
  coilsOn = false;
  digitalWrite(PIN_LED, LOW);
}

static void coilsEnergize() {
  if (!coilsOn) {
    writeCoils();
    coilsOn = true;
    if (param(P_LED)) digitalWrite(PIN_LED, HIGH);
    lastStepUs = micros();
  }
}

static bool hallActive() { return digitalRead(PIN_HALL) == (param(P_HALL_ACTIVE_LOW) ? LOW : HIGH); }

// One step forward (the drum only ever turns one way)
static void stepForward() {
  // The original turned the Stepper library "backwards" (ROTATIONDIRECTION -1): phase counts down
  phase = param(P_REVERSE) ? (phase + 1) & 3 : (phase + 3) & 3;
  writeCoils();
  position = position + 1 >= stepsPerRev ? 0 : position + 1;
  if (sinceMagnet < 0xFFFF) sinceMagnet++;
  if (calMode) calPosition++;
}

static void startMotion(Motion m) {
  motion = m;
  coilsEnergize();
  stepsThisMove = 0;
  // lastHall is NOT re-read here: if the magnet "appeared" while standing still (the gears settle a
  // few steps when the coils switch off), the first step sees it as an edge instead of missing it.
  if (status == STATUS_READY || status == STATUS_CAL_ERROR) status = STATUS_MOVING;
}

static void finishMotion() {
  motion = Motion::Idle;
  motorStopMs = millis();
  status = STATUS_READY;
}

static void startHoming(bool withOffset) {
  applyOffsetWhenHomed = withOffset;
  homed = false;
  edgeTracking = false;
  searched = 0;
  status = STATUS_CAL_SEARCHING;
  if (hallActive() && param(P_HOME_BACKOFF) > 0) {
    // Starting on the magnet: back off first so we find its leading edge properly
    stepsLeft = param(P_HOME_BACKOFF);
    startMotion(Motion::HomeAway);
  } else {
    startMotion(Motion::HomeSearch);
  }
}

static void startMoveTo(uint8_t flap) {
  wantedLetter = flap;
  if (!homed) {
    startHoming(true);  // continues to the letter once homed
    return;
  }
  target = flapPosition(flap);
  if (target == position) {
    letter = flap;
    status = STATUS_READY;
    return;
  }
  info.moves++;
  startMotion(Motion::ToTarget);
}

// Magnet leading edge while moving normally: correct the position, record the error
static void learnStepsPerRev(uint16_t measured) {
  if (measured < MIN_STEPS_PER_REV || measured > MAX_STEPS_PER_REV) return;
  revSamples[revSampleCount % 3] = measured;
  revSampleCount++;
  if (revSampleCount < 3) return;
  uint16_t lo = revSamples[0], hi = revSamples[0];
  uint32_t sum = 0;
  for (uint16_t v : revSamples) {
    if (v < lo) lo = v;
    if (v > hi) hi = v;
    sum += v;
  }
  if (hi - lo > 3) return;  // not consistent yet
  uint16_t learned = (uint16_t)((sum + 1) / 3);
  if (learned == stepsPerRev) return;
  setParam(P_STEPS_PER_REV, learned, false);
  logEvent(EV_STEPS_LEARNED, stepsPerRev);
}

static void onMagnetEdge() {
  if (edgeTracking && param(P_LEARN_STEPS)) learnStepsPerRev(sinceMagnet);
  edgeTracking = true;
  int16_t error = position <= stepsPerRev / 2 ? (int16_t)position : (int16_t)position - stepsPerRev;
  info.syncs++;
  info.lastSyncError = error > 127 ? 127 : (error < -127 ? -127 : error);
  uint8_t magnitude = error < 0 ? -error : error;
  if (magnitude > info.maxSyncError) info.maxSyncError = magnitude;
  if (magnitude > param(P_SYNC_LOG_STEPS)) logEvent(EV_SYNC_ERROR, error);
  if (param(P_SYNC)) position = 0;  // the target is absolute (steps after the magnet), so it stays valid
  sinceMagnet = 0;
}

static void motorTask() {
  if (motion == Motion::Idle) {
    if (coilsOn && millis() - motorStopMs >= param(P_HOLD_MS)) coilsOff();
    return;
  }
  if (motion == Motion::CalJog && stopJog) {
    stopJog = false;
    finishMotion();
    return;
  }
  uint32_t now = micros();
  uint32_t interval = motion == Motion::CalJog ? (uint32_t)jogMs * 1000UL : stepInterval;
  if (now - lastStepUs < interval) return;
  lastStepUs = now;

  stepForward();
  if (stepsThisMove < 0xFFFF) stepsThisMove++;
  bool hall = hallActive();
  bool edge = hall && !lastHall;
  lastHall = hall;

  switch (motion) {
    case Motion::HomeAway:
      if (--stepsLeft == 0) motion = Motion::HomeSearch;
      break;

    case Motion::HomeSearch:
      searched++;
      if (edge) {  // leading edge of the magnet (not "already on it" - that's what HomeAway is for)
        position = 0;
        sinceMagnet = 0;
        homed = true;
        edgeTracking = true;  // the next magnet pass measures a full turn
        logEvent(EV_HOMED, searched);
        if (calMode || !applyOffsetWhenHomed) {
          calPosition = 0;
          finishMotion();
        } else {
          status = STATUS_CAL_OFFSET;
          target = flapPosition(wantedLetter);
          if (target == 0) {
            letter = wantedLetter;
            finishMotion();
          } else {
            motion = Motion::HomeOffset;
          }
        }
      } else if (searched >= (uint32_t)param(P_HOME_TURNS) * stepsPerRev + 100) {
        logEvent(EV_HOME_FAILED, searched);
        info.misses++;
        motion = Motion::Idle;
        motorStopMs = millis();
        letter = 0;
        status = STATUS_CAL_ERROR;  // stays until the next command (which tries again)
      }
      break;

    case Motion::HomeOffset:
    case Motion::ToTarget:
      if (edge && homed && !calMode) {
        if (stepsThisMove <= 2) {
          // Magnet came up while standing still: it's been passed, but a reading taken at rest
          // isn't precise enough to correct the position or to measure a turn with.
          sinceMagnet = 0;
          edgeTracking = false;
        } else {
          onMagnetEdge();
        }
      }
      if (homed && sinceMagnet > stepsPerRev + param(P_MAGNET_TOLERANCE)) {
        logEvent(EV_MAGNET_MISSED, sinceMagnet - stepsPerRev);
        edgeTracking = false;
        info.misses++;
        sinceMagnet = 0;
        homed = false;  // dead reckoning for this move; the next move homes properly
      }
      if (position == target) {
        letter = wantedLetter;
        finishMotion();
      }
      break;

    case Motion::CalMove:
      if (--stepsLeft == 0 || stopJog) {
        stopJog = false;
        finishMotion();
      }
      break;

    case Motion::CalJog:
    case Motion::Idle:
      break;
  }

  if (coilsOn && vccMv > 0 && vccMv < vccMinMoving) vccMinMoving = vccMv;
}

// ---- Supply voltage (internal 1.1 V bandgap vs AVcc) ----------------------------------------
static void vccBegin() {
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);  // measure 1.1 V against AVcc
  ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);
}

static void vccTask() {
  // Non-blocking: start a conversion, pick it up on a later loop
  static bool converting = false;
  if (converting) {
    if (ADCSRA & _BV(ADSC)) return;
    converting = false;
    uint16_t adc = ADC;
    if (adc > 0) vccMv = (uint16_t)(1125300UL / adc);
    if (coilsOn && vccMv < param(P_LOW_VCC_MV) && !lowVoltageLogged) {
      lowVoltageLogged = true;
      logEvent(EV_LOW_VOLTAGE, vccMv);
    }
    return;
  }
  if (millis() - lastVccMs >= (coilsOn ? 50UL : 2000UL)) {
    lastVccMs = millis();
    ADCSRA |= _BV(ADSC);
    converting = true;
  }
}

// ---- I2C (interrupt context: copy and get out) ---------------------------------------------
static void onReceive(int /*count*/) {
  uint8_t n = 0;
  while (Wire.available()) {
    uint8_t b = Wire.read();
    if (n < sizeof(rxBuf)) rxBuf[n++] = b;
  }
  if (n == 0) return;
  i2cWrites++;
  lastI2cMs = millis();
  uint8_t first = rxBuf[0];
  if (first == CMD_INFO || first == CMD_EVENTS) {
    replyMode = first;
    return;
  }
  if (first == CMD_GET_PARAM) {
    replyMode = first;
    replyParam = n >= 2 ? rxBuf[1] : 0xFF;
    return;
  }
  if (first == CMD_SLEEP) {
    sleepWanted = true;
    return;
  }
  sleepWanted = false;  // any other command wakes it for good (status reads don't come through here)
  if (first == CMD_STOP) stopJog = true;
  // Report "busy" right away so the master never sees a stale "ready" for a move that's queued
  if (first < FLAP_COUNT || first == CMD_HOME || first == CMD_MOVE || first == CMD_REHOME) {
    if (status != STATUS_CAL_SEARCHING && status != STATUS_CAL_OFFSET) status = STATUS_MOVING;
  }
  rxLen = n;
  rxReady = true;
}

static void onRequest() {
  lastI2cMs = millis();
  uint8_t mode = replyMode;
  replyMode = 0;
  if (mode == CMD_INFO) {
    info.version = FIRMWARE_VERSION;
    info.status = status;
    info.flags = (homed ? INFO_FLAG_HOMED : 0) | (calMode ? INFO_FLAG_CAL_MODE : 0) |
                 (motion == Motion::CalJog ? INFO_FLAG_JOGGING : 0) | (hallActive() ? INFO_FLAG_AT_MAGNET : 0) |
                 (letterPending ? INFO_FLAG_GUARD : 0) | (offsetUnset ? INFO_FLAG_NO_OFFSET : 0) |
                 (WATCHDOG_ENABLE ? INFO_FLAG_WATCHDOG : 0);
    info.resetCause = resetCause;
    info.letter = letter;
    info.target = wantedLetter;
    info.position = position;
    info.offset = offset;
    info.uptime = millis() / 1000;
    info.vcc = vccMv;
    info.vccMin = vccMinMoving == 0xFFFF ? 0 : vccMinMoving;
    info.i2cWrites = i2cWrites;
    info.boots = boots;
    info.stepsPerRev = stepsPerRev;
    Wire.write((const uint8_t*)&info, sizeof(info));
  } else if (mode == CMD_GET_PARAM) {
    uint8_t id = replyParam;
    uint16_t value = id < P_COUNT ? params[id] : 0;
    uint8_t reply[PARAM_REPLY_SIZE] = {id, (uint8_t)(value & 0xFF), (uint8_t)(value >> 8), (uint8_t)(id < P_COUNT)};
    Wire.write(reply, sizeof(reply));
  } else if (mode == CMD_EVENTS) {
    uint8_t out[EVENTS_SIZE] = {0};
    out[0] = eventCount;
    for (uint8_t i = 0; i < eventCount; i++) {
      out[1 + i * 3] = events[i].code;
      out[2 + i * 3] = events[i].arg & 0xFF;
      out[3 + i * 3] = (uint16_t)events[i].arg >> 8;
    }
    eventCount = 0;
    Wire.write(out, sizeof(out));
  } else {
    uint16_t p = calMode ? calPosition : position;
    uint8_t reply[STATUS_SIZE] = {(uint8_t)status, (uint8_t)(p & 0xFF), (uint8_t)(p >> 8), FIRMWARE_VERSION};
    Wire.write(reply, sizeof(reply));  // one write: older Wire versions keep only the last one
  }
}

static void startI2c() {
  Wire.begin(address);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
}

// If the TWI hardware sits in the middle of a transfer for a long time (master reset mid-transfer,
// noise), it can hold SDA low and block the whole bus. Reset it.
static void i2cWatchdog() {
  static uint32_t stuckSince = 0;
  uint8_t state = TW_STATUS;  // TW_NO_INFO (0xF8) = no transfer in progress
  bool stuck = state == TW_BUS_ERROR || (state != TW_NO_INFO && millis() - lastI2cMs > 100);
  if (!stuck) {
    stuckSince = 0;
    return;
  }
  if (stuckSince == 0) stuckSince = millis();
  if (millis() - stuckSince < 50) return;
  stuckSince = 0;
  TWCR = 0;  // TWI off: releases SDA and SCL
  delayMicroseconds(20);
  startI2c();
  lastI2cMs = millis();
  logEvent(EV_I2C_RESET, state);
}

// ---- Commands (main loop) ------------------------------------------------------------------
static void saveOffset(uint16_t value) {
  if (value >= stepsPerRev) return;
  if (value != offset || offsetUnset) {
    offset = value;
    offsetUnset = false;
    EEPROM.put(EE_OFFSET, offset);
    logEvent(EV_OFFSET_SAVED, offset);
  }
}

static void runUntilIdle() {
  while (motion != Motion::Idle) {
#if WATCHDOG_ENABLE
    wdt_reset();
#endif
    motorTask();
  }
}

static void reboot() {
#if WATCHDOG_ENABLE
  wdt_enable(WDTO_15MS);
  while (true) {
  }
#else
  asm volatile("jmp 0");
#endif
}

static void handleCommand() {
  uint8_t buf[8];
  uint8_t n;
  noInterrupts();
  n = rxLen;
  for (uint8_t i = 0; i < n; i++) buf[i] = rxBuf[i];
  rxReady = false;
  interrupts();

  uint8_t first = buf[0];
  if (first < FLAP_COUNT) {
    if (n >= 2 && buf[1] > 0) setSpeed(buf[1]);
    if (n >= 4) saveOffset(buf[2] | (buf[3] << 8));
    if (calMode) {  // any letter ends calibration
      calMode = false;
      motion = Motion::Idle;
      logEvent(EV_CAL_END, 0);
    }
    wantedLetter = first;
    letterPending = true;  // started by loop() once the overheating guard allows it
    return;
  }

  switch (first) {
    case CMD_HOME:
      calMode = true;
      lastCalCommand = millis();
      if (n >= 2 && buf[1] > 0) setSpeed(buf[1]);
      logEvent(EV_CAL_START, 0);
      startHoming(false);
      break;
    case CMD_MOVE:
      calMode = true;
      lastCalCommand = millis();
      if (n >= 2 && buf[1] > 0) setSpeed(buf[1]);
      stepsLeft = n >= 4 ? (buf[2] | (buf[3] << 8)) : 0;
      if (stepsLeft > 0) startMotion(Motion::CalMove);
      else status = STATUS_READY;
      break;
    case CMD_JOG:
      calMode = true;
      lastCalCommand = millis();
      jogMs = n >= 2 && buf[1] > 0 ? buf[1] : 80;
      stopJog = false;
      jogStartMs = millis();
      startMotion(Motion::CalJog);
      break;
    case CMD_STOP:
      lastCalCommand = millis();
      if (motion == Motion::CalJog || motion == Motion::CalMove) finishMotion();
      stopJog = false;
      if (status == STATUS_MOVING && motion == Motion::Idle) status = STATUS_READY;
      break;
    case CMD_SET_OFFSET:
      if (n >= 3) saveOffset(buf[1] | (buf[2] << 8));
      break;
    case CMD_REHOME:
      if (n >= 2 && buf[1] > 0) setSpeed(buf[1]);
      calMode = false;
      wantedLetter = 0;
      startHoming(true);
      break;
    case CMD_REBOOT:
      Serial.println(F("reboot requested"));
      Serial.flush();
      reboot();
      break;
    case CMD_SET_PARAM:
      if (n < 4 || !setParam(buf[1], buf[2] | (buf[3] << 8), true)) logEvent(EV_BAD_COMMAND, first);
      break;
    case CMD_RESET_PARAMS:
      resetParams();
      break;
    case CMD_CLEAR_STATS:
      info.moves = info.syncs = info.misses = 0;
      info.lastSyncError = 0;
      info.maxSyncError = 0;
      vccMinMoving = 0xFFFF;
      break;
    default:
      logEvent(EV_BAD_COMMAND, first);
      if (motion == Motion::Idle) status = STATUS_READY;
      break;
  }
}

// ---- Serial bench console ------------------------------------------------------------------
static void printInfo() {
  Serial.print(F("unit ")); Serial.print(address);
  Serial.print(F(" fw 2.3.0  status ")); Serial.print(status);
  Serial.print(F("  letter '")); Serial.print(LETTERS[letter]);
  Serial.print(F("'  position ")); Serial.print(position);
  Serial.print(homed ? F(" (homed)") : F(" (NOT homed)"));
  Serial.print(F("  offset ")); Serial.print(offset); if (offsetUnset) Serial.print(F(" (never set)"));
  Serial.print(F("\n  vcc ")); Serial.print(vccMv); Serial.print(F(" mV (min moving "));
  Serial.print(vccMinMoving == 0xFFFF ? 0 : vccMinMoving);
  Serial.print(F(")  syncs ")); Serial.print(info.syncs);
  Serial.print(F("  last err ")); Serial.print(info.lastSyncError);
  Serial.print(F("  max err ")); Serial.print(info.maxSyncError);
  Serial.print(F("  misses ")); Serial.print(info.misses);
  Serial.print(F("  boots ")); Serial.print(boots);
  Serial.print(F("  steps/turn ")); Serial.print(stepsPerRev);
  Serial.print(F("  reset cause 0x")); Serial.print(resetCause, HEX);
  Serial.print(F("  hall ")); Serial.println(hallActive() ? F("MAGNET") : F("-"));
}

static void serialTask() {
  static char line[24];
  static uint8_t length = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (length < sizeof(line) - 1) line[length++] = c;
      continue;
    }
    if (length == 0) continue;
    line[length] = 0;
    length = 0;
    if (strcmp(line, "help") == 0) {
      Serial.println(F("<char> show it   o <n> offset   h re-home   i info   p params   p <id> <v> set   t test   r reboot"));
    } else if (strcmp(line, "i") == 0) {
      printInfo();
    } else if (strcmp(line, "p") == 0) {
      for (uint8_t id = 0; id < P_COUNT; id++) {
        Serial.print(F("  p ")); Serial.print(id); Serial.print(F(" = ")); Serial.println(params[id]);
      }
      Serial.println(F("  (names: see the master's 'param' command; set with: p <id> <value>)"));
    } else if (line[0] == 'p' && line[1] == ' ') {
      char* rest;
      long id = strtol(line + 2, &rest, 10);
      long value = strtol(rest, nullptr, 10);
      Serial.println(setParam(id, value, true) ? F("ok") : F("out of range"));
    } else if (strcmp(line, "h") == 0) {
      wantedLetter = 0;
      startHoming(true);
    } else if (strcmp(line, "r") == 0) {
      reboot();
    } else if (strcmp(line, "t") == 0) {
      Serial.println(F("test: blank Z A U N ? 0 1 2 9 (send any letter to stop)"));
      static const char TEST[] = " ZAUN?0129";
      for (const char* p = TEST; *p; p++) {
        startMoveTo(letterIndex(*p));
        runUntilIdle();
        for (uint8_t i = 0; i < 15; i++) {
#if WATCHDOG_ENABLE
          wdt_reset();
#endif
          delay(100);
        }
      }
    } else if (line[0] == 'o' && line[1] == ' ') {
      long value = atol(line + 2);
      if (value >= 0 && value < stepsPerRev) {
        saveOffset(value);
        wantedLetter = 0;
        startHoming(true);
      }
    } else if (line[1] == 0 && letterIndex(line[0]) >= 0) {
      wantedLetter = letterIndex(line[0]);
      letterPending = true;
    } else {
      Serial.println(F("? (type help)"));
    }
  }
}

// Power-down until an I2C address match wakes us (the TWI hardware stretches the clock until
// we're up - about 1 ms). Coils are already off; the ADC and watchdog are paused too.
// millis() stops while asleep, so "uptime" only counts awake time during quiet hours.
static void deepSleep() {
  Serial.flush();
  uint8_t adc = ADCSRA;
  ADCSRA &= ~_BV(ADEN);
#if WATCHDOG_ENABLE
  wdt_disable();
#endif
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  if (sleepWanted && !rxReady) {
    sleep_enable();
    sei();
    sleep_cpu();  // ... an I2C transfer for our address wakes us here
    sleep_disable();
  }
  sei();
  ADCSRA = adc;
#if WATCHDOG_ENABLE
  wdt_enable(WDTO_2S);
#endif
}

// ---- Setup / loop --------------------------------------------------------------------------
#if OPTIBOOT
// Optiboot clears MCUSR but passes its old value in r2. Grab it before anything else runs.
static uint8_t bootR2 __attribute__((section(".noinit")));
void captureResetCause() __attribute__((naked, used, section(".init0")));
void captureResetCause() { __asm__ __volatile__("sts %0, r2\n" : : "m"(bootR2) :); }
#endif

void setup() {
  resetCause = MCUSR;
#if OPTIBOOT
  if (!(bootR2 & 0xF0)) resetCause |= bootR2;  // only the low four MCUSR bits exist: 0xFF etc. is garbage
#endif
  MCUSR = 0;
  wdt_disable();

  for (uint8_t pin : PIN_COIL) pinMode(pin, OUTPUT);
  coilsOff();
  pinMode(PIN_HALL, INPUT);
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(SERIAL_BAUD);
  readAddress();

  EEPROM.get(EE_OFFSET, offset);
  loadParams();
  lastHall = digitalRead(PIN_HALL) == (params[P_HALL_ACTIVE_LOW] ? LOW : HIGH);
  if (offset >= stepsPerRev) {  // 0xFFFF = never written
    offset = 0;
    offsetUnset = true;
  }
  EEPROM.get(EE_BOOTS, boots);
  if (boots == 0xFFFF) boots = 0;
  boots++;
  EEPROM.put(EE_BOOTS, boots);

  memset(&info, 0, sizeof(info));
  vccBegin();
  setSpeed(param(P_HOME_RPM));
  startI2c();

  Serial.print(F("\nsplit-flap unit 2.3.0, I2C address "));
  Serial.print(address);
  Serial.print(F(", offset "));
  Serial.print(offset);
  Serial.print(F(", boot #"));
  Serial.println(boots);
  logEvent(EV_BOOT, resetCause);

  wantedLetter = 0;
  startHoming(true);  // find the magnet and show the blank flap

#if WATCHDOG_ENABLE
  wdt_enable(WDTO_2S);
#endif
}

void loop() {
#if WATCHDOG_ENABLE
  wdt_reset();
#endif
  if (rxReady) handleCommand();

  // Start a queued letter once the overheating guard allows (and nothing else is running)
  if (letterPending && motion == Motion::Idle && !calMode) {
    if (!moveStartedOnce || millis() - lastMoveStart >= param(P_MOVE_GUARD_MS)) {
      letterPending = false;
      lowVoltageLogged = false;
      lastMoveStart = millis();
      moveStartedOnce = true;
      startMoveTo(wantedLetter);
    } else {
      status = STATUS_MOVING;  // queued: tell the master we're not done
    }
  }

  motorTask();
  vccTask();
  i2cWatchdog();

  if (calMode) {
    if (motion == Motion::CalJog && millis() - jogStartMs > param(P_JOG_TIMEOUT_S) * 1000UL) finishMotion();
    if (millis() - lastCalCommand > param(P_CAL_TIMEOUT_S) * 1000UL) {
      calMode = false;  // the master went away: back to normal
      logEvent(EV_CAL_END, 1);
      wantedLetter = 0;
      startHoming(true);
    }
  }

  serialTask();

  // Quiet hours: deep sleep once the motor is idle and the coils are off
  if (sleepWanted && motion == Motion::Idle && !coilsOn && !rxReady && !letterPending && !calMode) {
    deepSleep();
    return;
  }

  // Nothing to do: idle sleep until the next interrupt (timer tick or I2C). millis() keeps running.
  if (motion == Motion::Idle && !rxReady && !letterPending) {
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_mode();
  }
}
