// I2C protocol between the master (ESP32) and the flap units (Arduino Nano, Unit firmware 1.3.0+).
// Shared by both firmwares so the two sides can't drift apart.
#pragma once
#include <stdint.h>

namespace unitproto {

// Flap order on the drum. Index 0 is the blank flap, which is also the "home" position.
static const char LETTERS[] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
                               'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '$', '&', '#',
                               '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', '.', '-', '?', '!'};
constexpr int FLAP_COUNT = 45;
// 28BYJ-48 steppers come with two gearings: 63.68:1 (2038 steps per turn, the datasheet value) and
// exactly 64:1 (2048). This display's motors measured 2048. Unit firmware 2.1+ measures its own
// value between magnet passes and stores it; until then it assumes DEFAULT_STEPS_PER_REV.
constexpr int DEFAULT_STEPS_PER_REV = 2048;
constexpr int MIN_STEPS_PER_REV = 2000;
constexpr int MAX_STEPS_PER_REV = 2100;
constexpr int STEPS_PER_REV = DEFAULT_STEPS_PER_REV;  // nominal, for when the unit's own value isn't known
constexpr int MIN_RPM = 1;
constexpr int MAX_RPM = 12;
constexpr uint32_t MOVE_GUARD_MS = 2000; // unit ignores a new move for 2 s after starting one (overheating guard)

// ---- Master -> unit (write) ----------------------------------------------------------------
//   [letter, rpm]                          show a letter (letter < FLAP_COUNT)
//   [letter, rpm, offsetLo, offsetHi]      show a letter and store a new calibration offset (EEPROM)
//   [CMD_HOME, rpm]                        calibration: go to the magnet edge, no offset, position = 0
//   [CMD_MOVE, rpm, stepsLo, stepsHi]      calibration: move N steps forward
//   [CMD_JOG, msPerStep]                   calibration: creep forward until CMD_STOP
//   [CMD_STOP]                             calibration: stop creeping
// Any normal letter command leaves calibration mode.
constexpr uint8_t CMD_HOME = 0xF1;
constexpr uint8_t CMD_MOVE = 0xF2;
constexpr uint8_t CMD_JOG = 0xF3;
constexpr uint8_t CMD_STOP = 0xF4;

// ---- Protocol v2 (Unit firmware 2.0.0+) --------------------------------------------------
//   [CMD_INFO]                    the next read returns the Info block (INFO_SIZE bytes)
//   [CMD_EVENTS]                  the next read returns queued events (EVENTS_SIZE bytes) and clears them
//   [CMD_SET_OFFSET, lo, hi]      store a calibration offset without moving (used on the next re-home)
//   [CMD_REHOME, rpm]             re-home now (full turn to the magnet + offset), then show the blank flap
//   [CMD_REBOOT]                  restart the unit (watchdog)
//   [CMD_CLEAR_STATS]             zero the counters in the Info block
constexpr uint8_t CMD_INFO = 0xF5;
constexpr uint8_t CMD_EVENTS = 0xF6;
constexpr uint8_t CMD_SET_OFFSET = 0xF7;
constexpr uint8_t CMD_REHOME = 0xF8;
constexpr uint8_t CMD_REBOOT = 0xF9;
constexpr uint8_t CMD_CLEAR_STATS = 0xFA;
// Parameters (2.1.0+): settings stored in the unit's EEPROM, changed without reflashing
//   [CMD_GET_PARAM, id]           the next read returns [id, valueLo, valueHi, ok]
//   [CMD_SET_PARAM, id, lo, hi]   check the range, store in EEPROM, use it right away
//   [CMD_RESET_PARAMS]            every parameter back to its default (the offset is kept)
constexpr uint8_t CMD_GET_PARAM = 0xFB;
constexpr uint8_t CMD_SET_PARAM = 0xFC;
constexpr uint8_t CMD_RESET_PARAMS = 0xFD;
constexpr uint8_t PARAM_REPLY_SIZE = 4;
// 0xFE-0xFF reserved (e.g. a future "enter I2C bootloader" command)
constexpr uint8_t V2_FIRMWARE = 20;   // 2.0.0
constexpr uint8_t V21_FIRMWARE = 21;  // 2.1.0: learned steps per turn (Info.stepsPerRev)

// Info block, little endian. Read after CMD_INFO.
struct __attribute__((packed)) Info {
  uint8_t version;       // firmware version (20 = 2.0.0)
  int8_t status;         // Status
  uint8_t flags;         // INFO_FLAG_*
  uint8_t resetCause;    // AVR MCUSR at boot: 1 power-on, 2 reset pin, 4 brown-out, 8 watchdog
  uint8_t letter;        // flap showing now
  uint8_t target;        // flap it is moving to
  uint16_t position;     // steps after the magnet edge (0..STEPS_PER_REV-1)
  uint16_t offset;       // calibration offset (steps from the magnet edge to the blank flap)
  uint32_t uptime;       // seconds since boot
  uint16_t vcc;          // supply voltage now, mV
  uint16_t vccMin;       // lowest supply voltage seen while the motor was running, mV
  uint16_t moves;        // moves since boot
  uint16_t syncs;        // times the magnet was passed and the position re-synced
  uint16_t misses;       // magnet not found where expected (slipping, sensor or magnet problem)
  int8_t lastSyncError;  // steps the position was off at the last magnet pass (+ = drum behind)
  uint8_t maxSyncError;  // largest |sync error| since boot
  uint16_t i2cWrites;    // commands received since boot
  uint16_t boots;        // boot counter (EEPROM)
  uint16_t stepsPerRev;  // 2.1.0+: steps per turn the unit uses (learned from magnet passes)
};
constexpr uint8_t INFO_SIZE = sizeof(Info);   // 32 bytes = the AVR Wire buffer, don't grow it
constexpr uint8_t INFO_SIZE_V20 = 30;         // 2.0.0 had no stepsPerRev
static_assert(sizeof(Info) == 32, "Info layout changed - update both firmwares");

constexpr uint8_t INFO_FLAG_HOMED = 0x01;      // position is known
constexpr uint8_t INFO_FLAG_CAL_MODE = 0x02;   // master calibration in progress
constexpr uint8_t INFO_FLAG_JOGGING = 0x04;
constexpr uint8_t INFO_FLAG_AT_MAGNET = 0x08;  // hall sensor sees the magnet right now
constexpr uint8_t INFO_FLAG_GUARD = 0x10;      // a move is waiting for the overheating guard
constexpr uint8_t INFO_FLAG_NO_OFFSET = 0x20;  // EEPROM offset was never written
constexpr uint8_t INFO_FLAG_WATCHDOG = 0x40;   // watchdog enabled (new bootloader build)

// Events, read after CMD_EVENTS: [count, (code, argLo, argHi) x count]. The unit keeps the last 8.
constexpr uint8_t EVENT_SLOTS = 8;
constexpr uint8_t EVENTS_SIZE = 1 + EVENT_SLOTS * 3;
enum Event : uint8_t {
  EV_NONE = 0,
  EV_BOOT = 1,          // arg = reset cause
  EV_HOMED = 2,         // arg = steps it took to find the magnet
  EV_HOME_FAILED = 3,   // arg = steps searched
  EV_SYNC_ERROR = 4,    // arg = signed sync error (only logged when |error| > 3 steps)
  EV_MAGNET_MISSED = 5, // arg = steps past the expected magnet position
  EV_OFFSET_SAVED = 6,  // arg = new offset
  EV_CAL_START = 7,
  EV_CAL_END = 8,
  EV_LOW_VOLTAGE = 9,   // arg = mV (motor running below 4.5 V)
  EV_BAD_COMMAND = 10,  // arg = first byte
  EV_I2C_RESET = 11,    // arg = TWI status; the unit's I2C hardware was stuck and got reset
  EV_STEPS_LEARNED = 12,// arg = measured steps per turn, now used and saved
  EV_PARAM_SET = 13,    // arg = parameter id
  EV_PARAMS_RESET = 14,
};

// ---- Unit parameters -----------------------------------------------------------------------
#ifdef __AVR__
#include <avr/pgmspace.h>
#define UNITPROTO_PROGMEM PROGMEM
#else
#define UNITPROTO_PROGMEM
#endif

enum Param : uint8_t {
  P_STEPS_PER_REV,     // steps per drum turn (learned automatically when P_LEARN_STEPS = 1)
  P_LEARN_STEPS,       // 1 = measure steps per turn between magnet passes and update P_STEPS_PER_REV
  P_MOVE_GUARD_MS,     // minimum time between the starts of two moves (overheating guard)
  P_HOLD_MS,           // keep the coils on this long after a move (stops coasting)
  P_MAGNET_TOLERANCE,  // steps past the expected magnet before it counts as "missed"
  P_HOME_BACKOFF,      // homing that starts on the magnet first moves this far off it
  P_HOME_TURNS,        // give up homing after this many turns without finding the magnet
  P_HOME_RPM,          // speed for homing at power-up
  P_MAX_RPM,           // speed limit, whatever the master asks for
  P_REVERSE,           // 1 = turn the motor the other way (motor wired/mounted mirrored)
  P_HALL_ACTIVE_LOW,   // 1 = hall sensor output goes LOW at the magnet (A3144 etc.), 0 = HIGH
  P_SYNC,              // 1 = correct the position at every magnet pass
  P_SYNC_LOG_STEPS,    // log a "position corrected" event when the error is bigger than this
  P_LOW_VCC_MV,        // log a low-voltage event below this supply voltage while moving
  P_CAL_TIMEOUT_S,     // leave calibration mode after this long without commands
  P_JOG_TIMEOUT_S,     // stop creeping (calibration) after this long
  P_LED,               // 1 = onboard LED lights while the motor runs
  P_COUNT
};

struct ParamLimits {
  uint16_t def, min, max;
};

static const ParamLimits PARAM_LIMITS[P_COUNT] UNITPROTO_PROGMEM = {
    {2048, 2000, 2100},  // P_STEPS_PER_REV
    {1, 0, 1},           // P_LEARN_STEPS
    {2000, 0, 10000},    // P_MOVE_GUARD_MS
    {100, 0, 1000},      // P_HOLD_MS
    {60, 10, 500},       // P_MAGNET_TOLERANCE
    {50, 0, 500},        // P_HOME_BACKOFF
    {3, 1, 5},           // P_HOME_TURNS
    {10, 1, 15},         // P_HOME_RPM
    {15, 1, 20},         // P_MAX_RPM
    {0, 0, 1},           // P_REVERSE
    {1, 0, 1},           // P_HALL_ACTIVE_LOW
    {1, 0, 1},           // P_SYNC
    {3, 0, 100},         // P_SYNC_LOG_STEPS
    {4500, 3000, 5500},  // P_LOW_VCC_MV
    {300, 30, 3600},     // P_CAL_TIMEOUT_S
    {60, 5, 600},        // P_JOG_TIMEOUT_S
    {1, 0, 1},           // P_LED
};

inline ParamLimits paramLimits(uint8_t id) {
#ifdef __AVR__
  ParamLimits l;
  memcpy_P(&l, &PARAM_LIMITS[id], sizeof(l));
  return l;
#else
  return PARAM_LIMITS[id];
#endif
}

#ifndef __AVR__  // names and help only on the master (saves the Nano's RAM)
static const char* const PARAM_NAMES[P_COUNT] = {
    "stepsPerRev", "learnSteps", "moveGuardMs", "holdMs",    "magnetTolerance", "homeBackoff",
    "homeTurns",   "homeRpm",    "maxRpm",      "reverse",   "hallActiveLow",   "sync",
    "syncLogSteps", "lowVccMv",  "calTimeoutS", "jogTimeoutS", "led"};
static const char* const PARAM_HELP[P_COUNT] = {
    "steps per drum turn (learned when learnSteps=1)",
    "1 = learn steps per turn from magnet passes",
    "min ms between move starts (overheating guard)",
    "ms the coils stay on after a move",
    "steps past the expected magnet = missed",
    "steps to move off the magnet before homing",
    "turns to search for the magnet before giving up",
    "rpm for homing at power-up",
    "speed limit (rpm)",
    "1 = reverse the motor direction",
    "1 = hall output LOW at the magnet, 0 = HIGH",
    "1 = correct the position at every magnet pass",
    "log corrections bigger than this many steps",
    "low-voltage warning threshold (mV, while moving)",
    "leave calibration mode after this many s idle",
    "stop creeping after this many s",
    "1 = LED on while the motor runs"};

inline int paramFromName(const char* name) {
  for (int i = 0; i < P_COUNT; i++) {
    const char* a = name;
    const char* b = PARAM_NAMES[i];
    while (*a && *b && ((*a | 32) == (*b | 32))) a++, b++;  // case-insensitive
    if (!*a && !*b) return i;
  }
  return -1;
}
#endif

inline const char* eventName(uint8_t code) {
  switch (code) {
    case EV_BOOT: return "boot";
    case EV_HOMED: return "homed";
    case EV_HOME_FAILED: return "HOMING FAILED - magnet not found";
    case EV_SYNC_ERROR: return "position corrected at magnet";
    case EV_MAGNET_MISSED: return "MAGNET MISSED";
    case EV_OFFSET_SAVED: return "offset saved";
    case EV_CAL_START: return "calibration started";
    case EV_CAL_END: return "calibration ended";
    case EV_LOW_VOLTAGE: return "LOW VOLTAGE while moving";
    case EV_BAD_COMMAND: return "unknown command";
    case EV_I2C_RESET: return "I2C hardware was stuck - reset it";
    case EV_STEPS_LEARNED: return "learned steps per turn";
    case EV_PARAM_SET: return "parameter changed";
    case EV_PARAMS_RESET: return "parameters reset to defaults";
    default: return "?";
  }
}

// ---- Unit -> master (read) -----------------------------------------------------------------
//   [status, positionLo, positionHi, firmwareVersion]
// Units older than 1.3.0 send only the status byte (the rest reads as 0xFF).
constexpr uint8_t STATUS_SIZE = 4;
constexpr uint8_t MIN_CAL_FIRMWARE = 13; // 1.3.0: needed for CMD_HOME..CMD_STOP
constexpr uint8_t VERSION_UNKNOWN = 255;

enum Status : int8_t {
  STATUS_SLEEPING = -1,     // old firmware: answered from sleep
  STATUS_READY = 0,
  STATUS_MOVING = 1,
  STATUS_CAL_SEARCHING = 2, // full turn: looking for the magnet
  STATUS_CAL_OFFSET = 3,    // full turn: moving from the magnet to the blank flap
  STATUS_CAL_ERROR = 4,     // magnet not found
};

inline bool isBusy(int status) {
  return status == STATUS_MOVING || status == STATUS_CAL_SEARCHING || status == STATUS_CAL_OFFSET;
}

inline const char* statusName(int status) {
  switch (status) {
    case STATUS_SLEEPING: return "ready (sleeping)";
    case STATUS_READY: return "ready";
    case STATUS_MOVING: return "moving";
    case STATUS_CAL_SEARCHING: return "homing (searching for magnet)";
    case STATUS_CAL_OFFSET: return "homing (applying offset)";
    case STATUS_CAL_ERROR: return "ERROR: magnet not found";
    default: return "unknown";
  }
}

// Returns the flap index for a character (case-insensitive), or -1 if it isn't on the drum.
inline int letterIndex(char c) {
  if (c >= 'a' && c <= 'z') c -= 32;
  for (int i = 0; i < FLAP_COUNT; i++) {
    if (LETTERS[i] == c) return i;
  }
  return -1;
}

// Web/settings speed 1..100 -> rpm the unit understands (same mapping as the original firmware)
inline uint8_t rpmFromSpeed(int speed) {
  if (speed < 1) speed = 1;
  if (speed > 100) speed = 100;
  return (uint8_t)(MIN_RPM + (long)(speed - 1) * (MAX_RPM - MIN_RPM) / 99);
}

}  // namespace unitproto
