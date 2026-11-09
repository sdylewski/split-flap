#include "units.h"

#include <Wire.h>

#include "log.h"
#include "settings.h"

namespace units {

static Unit table[MAX_UNITS];
static uint32_t lastRecovery = 0;
static uint32_t recoveries = 0;
static bool redrawWanted = false;

bool takeRedrawRequest() {
  bool wanted = redrawWanted;
  redrawWanted = false;
  return wanted;
}

static void startWire() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  Wire.setTimeOut(I2C_TIMEOUT_MS);
}

// A unit that was cut off mid-transfer (e.g. the ESP32 reset while talking to it) can hold SDA low
// forever, which blocks every unit. Clocking SCL lets it finish its byte; then a STOP frees the bus.
bool recoverBus(const char* why) {
  lastRecovery = millis();
  recoveries++;
  Wire.end();
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  delayMicroseconds(50);
  bool sdaLow = digitalRead(PIN_I2C_SDA) == LOW;
  bool sclLow = digitalRead(PIN_I2C_SCL) == LOW;
  logx::write(sdaLow || sclLow ? logx::Level::Warn : logx::Level::Debug, "units", "I2C bus check (%s): SDA %s, SCL %s",
              why, sdaLow ? "LOW (stuck)" : "high", sclLow ? "LOW (stuck)" : "high");
  if (sclLow) {
    LOGE("units", "SCL is held low - a unit is hung or SCL is shorted; power-cycle the display");
  }
  int pulses = 0;
  if (sdaLow && !sclLow) {
    pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
    while (pulses < 18 && digitalRead(PIN_I2C_SDA) == LOW) {
      digitalWrite(PIN_I2C_SCL, LOW);
      delayMicroseconds(10);
      digitalWrite(PIN_I2C_SCL, HIGH);
      delayMicroseconds(10);
      pulses++;
    }
    // STOP condition: SDA low -> high while SCL is high
    pinMode(PIN_I2C_SDA, OUTPUT_OPEN_DRAIN);
    digitalWrite(PIN_I2C_SDA, LOW);
    delayMicroseconds(10);
    digitalWrite(PIN_I2C_SCL, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_I2C_SDA, HIGH);
    delayMicroseconds(10);
  }
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  delayMicroseconds(50);
  bool ok = digitalRead(PIN_I2C_SDA) == HIGH && digitalRead(PIN_I2C_SCL) == HIGH;
  startWire();
  if (sdaLow) LOGW("units", "sent %d clock pulses: bus %s", pulses, ok ? "free again" : "STILL STUCK");
  return ok;
}

uint32_t busRecoveries() { return recoveries; }

void begin() {
  recoverBus("startup check");
  recoveries = 0;
  LOGI("units", "I2C on SDA %d / SCL %d at %d kHz", PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ / 1000);
}

int count() {
  int n = settings::current().unitCount;
  return n < 1 ? 1 : (n > MAX_UNITS ? MAX_UNITS : n);
}

Unit& get(int address) { return table[address < 0 || address >= MAX_UNITS ? 0 : address]; }

String versionName(uint8_t version) {
  if (version == unitproto::VERSION_UNKNOWN) return "<1.3.0";
  return String(version / 10) + "." + String(version % 10) + ".0";
}

String i2cErrorName(uint8_t error) {
  switch (error) {
    case 0: return "ok";
    case 1: return "data too long";
    case 2: return "no answer (address NACK)";
    case 3: return "data NACK";
    case 4: return "bus error";
    case 5: return "timeout";
    default: return "error " + String(error);
  }
}

static void noteError(int address, uint8_t error, const char* what) {
  Unit& u = get(address);
  u.i2cErrors++;
  if (u.lastI2cError != error || u.present) {
    LOGW("units", "unit %d %s failed: %s", address, what, i2cErrorName(error).c_str());
  }
  u.lastI2cError = error;
}

bool send(int address, const uint8_t* data, size_t length) {
  Wire.beginTransmission(address);
  Wire.write(data, length);
  uint8_t error = Wire.endTransmission();
  if (error != 0) {
    noteError(address, error, "write");
    return false;
  }
  return true;
}

bool writeLetter(int address, int letter, uint8_t rpm) {
  uint8_t data[2] = {(uint8_t)letter, rpm};
  bool ok = send(address, data, 2);
  if (ok) get(address).lastLetter = letter;
  return ok;
}

bool writeLetterAndOffset(int address, int letter, uint8_t rpm, uint16_t offset) {
  uint8_t data[4] = {(uint8_t)letter, rpm, (uint8_t)(offset & 0xFF), (uint8_t)(offset >> 8)};
  bool ok = send(address, data, 4);
  if (ok) get(address).lastLetter = letter;
  return ok;
}

bool readStatus(int address, bool withPosition) {
  Unit& u = get(address);
  uint8_t raw[unitproto::STATUS_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF};
  // Units before 2.0.0 restart their I2C hardware when woken from power-down sleep - in the middle of
  // the request that woke them. A multi-byte reply can then leave SDA stuck low (whole bus dead until
  // power-cycled). The original master only ever read 1 byte, so do the same for them, except when
  // the position is needed (calibration, while the unit is awake).
  // Full read when: asked, version unknown, v2, or the unit is (re)appearing - it may have been reflashed
  bool full = withPosition || u.version == unitproto::VERSION_UNKNOWN || u.isV2() || !u.present;
  if (full && !u.isV2()) {
    // Wake it with a 1-byte read first; it then stays awake for 2 s, so the full read is safe
    Wire.requestFrom((uint8_t)address, (size_t)1);
    while (Wire.available()) Wire.read();
    delay(5);
  }
  size_t want = full ? unitproto::STATUS_SIZE : 1;
  size_t received = Wire.requestFrom((uint8_t)address, want);
  for (size_t i = 0; i < received && i < sizeof(raw); i++) raw[i] = Wire.read();
  while (Wire.available()) Wire.read();
  if (received == 0) {
    if (u.present) {
      LOGW("units", "unit %d stopped answering", address);
    }
    noteError(address, 2, "status read");
    u.present = false;
    return false;
  }
  if (!u.present) {
    redrawWanted = true;  // it may have restarted and lost its letter
    LOGI("units", "unit %d answering: %s, firmware %s", address, unitproto::statusName((int8_t)raw[0]),
         versionName(raw[3]).c_str());
  }
  int8_t previous = u.status;
  u.present = true;
  u.lastSeenMs = millis();
  u.status = (int8_t)raw[0];
  if (full) {
    u.version = raw[3];
    if (u.version != unitproto::VERSION_UNKNOWN) u.position = raw[1] | (raw[2] << 8);
  }
  if (u.status == unitproto::STATUS_CAL_ERROR && previous != unitproto::STATUS_CAL_ERROR) {
    LOGE("units", "unit %d: magnet not found - check the hall sensor / magnet", address);
  }
  return true;
}

bool readInfo(int address) {
  Unit& u = get(address);
  if (!u.isV2()) return false;
  uint8_t command = unitproto::CMD_INFO;
  if (!send(address, &command, 1)) return false;
  delayMicroseconds(300);
  // 2.0.0 sends 30 bytes; 2.1.0 adds stepsPerRev (32)
  size_t size = u.version >= unitproto::V21_FIRMWARE ? unitproto::INFO_SIZE : unitproto::INFO_SIZE_V20;
  uint8_t raw[unitproto::INFO_SIZE] = {0};
  size_t received = Wire.requestFrom((uint8_t)address, size);
  for (size_t i = 0; i < received && i < sizeof(raw); i++) raw[i] = Wire.read();
  while (Wire.available()) Wire.read();
  if (received != size) {
    noteError(address, 4, "info read");
    return false;
  }
  memcpy(&u.info, raw, sizeof(u.info));
  if (size < unitproto::INFO_SIZE) u.info.stepsPerRev = 2038;  // what 2.0.0 assumes
  u.infoValid = true;
  // Showing something other than what we sent (restarted, watchdog, slipped)? Redraw.
  if (u.lastLetter >= 0 && u.info.letter != u.lastLetter && u.status == unitproto::STATUS_READY) {
    LOGW("units", "unit %d shows '%c' but should show '%c' - redrawing", address,
         unitproto::LETTERS[u.info.letter % unitproto::FLAP_COUNT], unitproto::LETTERS[u.lastLetter]);
    redrawWanted = true;
  }
  return true;
}

int pullEvents(int address) {
  Unit& u = get(address);
  if (!u.isV2()) return 0;
  uint8_t command = unitproto::CMD_EVENTS;
  if (!send(address, &command, 1)) return 0;
  delayMicroseconds(300);
  uint8_t raw[unitproto::EVENTS_SIZE];
  size_t received = Wire.requestFrom((uint8_t)address, (size_t)unitproto::EVENTS_SIZE);
  for (size_t i = 0; i < received && i < sizeof(raw); i++) raw[i] = Wire.read();
  while (Wire.available()) Wire.read();
  if (received < 1) return 0;
  int n = raw[0] > unitproto::EVENT_SLOTS ? unitproto::EVENT_SLOTS : raw[0];
  for (int i = 0; i < n && 1 + i * 3 + 2 < (int)received; i++) {
    uint8_t code = raw[1 + i * 3];
    int16_t arg = (int16_t)(raw[2 + i * 3] | (raw[3 + i * 3] << 8));
    if (code == unitproto::EV_BOOT) redrawWanted = true;
    bool bad = code == unitproto::EV_HOME_FAILED || code == unitproto::EV_MAGNET_MISSED ||
               code == unitproto::EV_LOW_VOLTAGE;
    logx::write(bad ? logx::Level::Warn : logx::Level::Info, "unit", "unit %d: %s (%d)", address, unitproto::eventName(code), arg);
  }
  return n;
}

bool getParam(int address, uint8_t id, uint16_t& value) {
  if (get(address).version < unitproto::V21_FIRMWARE || get(address).version == unitproto::VERSION_UNKNOWN) return false;
  uint8_t cmd[2] = {unitproto::CMD_GET_PARAM, id};
  if (!send(address, cmd, 2)) return false;
  delayMicroseconds(300);
  uint8_t raw[unitproto::PARAM_REPLY_SIZE] = {0};
  size_t received = Wire.requestFrom((uint8_t)address, (size_t)unitproto::PARAM_REPLY_SIZE);
  for (size_t i = 0; i < received && i < sizeof(raw); i++) raw[i] = Wire.read();
  while (Wire.available()) Wire.read();
  if (received != unitproto::PARAM_REPLY_SIZE || raw[0] != id || !raw[3]) return false;
  value = raw[1] | (raw[2] << 8);
  return true;
}

bool setParam(int address, uint8_t id, uint16_t value) {
  uint8_t cmd[4] = {unitproto::CMD_SET_PARAM, id, (uint8_t)(value & 0xFF), (uint8_t)(value >> 8)};
  if (!send(address, cmd, 4)) return false;
  delay(20);  // EEPROM write on the unit
  uint16_t check;
  return getParam(address, id, check) && check == value;  // read back to confirm
}

bool resetParams(int address) {
  uint8_t cmd = unitproto::CMD_RESET_PARAMS;
  bool ok = send(address, &cmd, 1);
  delay(50);
  return ok;
}

void sleepAll() {
  int sent = 0;
  for (int a = 0; a < count(); a++) {
    Unit& u = get(a);
    if (!u.present || u.version == unitproto::VERSION_UNKNOWN || u.version < unitproto::SLEEP_FIRMWARE) continue;
    uint8_t cmd = unitproto::CMD_SLEEP;
    if (send(a, &cmd, 1)) sent++;
  }
  LOGI("units", "%d unit(s) sent to deep sleep", sent);
}

void rehomeAll(uint8_t rpm) {
  for (int a = 0; a < count(); a++) {
    uint8_t cmd[2] = {unitproto::CMD_REHOME, rpm};
    if (get(a).isV2()) send(a, cmd, 2);
    get(a).lastLetter = 0;  // they all end on the blank flap
  }
  LOGI("units", "all units re-homing");
}

bool pollAll(bool idlePoll) {
  bool busy = false;
  int answered = 0;
  for (int a = 0; a < count(); a++) {
    // Pre-2.0 units: every request can wake them from power-down sleep, and each wake-up risks the
    // 1.3.0 bug that locks the bus. Like the original master, leave idle ones alone.
    if (idlePoll && get(a).present && !get(a).isV2() && get(a).version != unitproto::VERSION_UNKNOWN) {
      answered++;
      continue;
    }
    if (readStatus(a)) {
      answered++;
      if (unitproto::isBusy(get(a).status)) busy = true;
    }
  }
  // Nobody answers: much more likely a stuck bus than every unit dead at once
  if (answered == 0 && count() > 1 && millis() - lastRecovery > 10000) recoverBus("no unit answers");
  return busy;
}

int problemCount() {
  int problems = 0;
  for (int a = 0; a < count(); a++) {
    const Unit& u = get(a);
    if (!u.present || u.status == unitproto::STATUS_CAL_ERROR) problems++;
  }
  return problems;
}

}  // namespace units
