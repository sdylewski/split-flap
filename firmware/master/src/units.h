// I2C driver for the flap units. Main loop only (one task owns the bus).
#pragma once
#include <Arduino.h>

#include "config.h"
#include "unit_protocol.h"

namespace units {

struct Unit {
  bool present = false;       // answered the last status read
  int8_t status = unitproto::STATUS_READY;
  uint8_t version = unitproto::VERSION_UNKNOWN;
  uint16_t position = 0;
  int8_t lastLetter = -1;     // last letter we sent (-1 = unknown)
  uint32_t lastSeenMs = 0;
  uint32_t i2cErrors = 0;     // failed reads/writes since boot
  uint8_t lastI2cError = 0;   // Wire error code: 2 = address NACK, 3 = data NACK, 5 = timeout
  bool infoValid = false;
  unitproto::Info info;       // v2 units: diagnostics block (refreshed by readInfo)

  bool isV2() const { return version != unitproto::VERSION_UNKNOWN && version >= unitproto::V2_FIRMWARE; }
  // Steps per turn this unit uses (learned by 2.1.0+; nominal otherwise)
  int stepsPerRev() const {
    return infoValid && info.stepsPerRev >= unitproto::MIN_STEPS_PER_REV ? info.stepsPerRev : unitproto::STEPS_PER_REV;
  }
  bool canCalibrate() const {
    return version != unitproto::VERSION_UNKNOWN && version >= unitproto::MIN_CAL_FIRMWARE;
  }
};

void begin();
int count();               // configured number of units (settings.unitCount)
Unit& get(int address);

// Raw commands. Return true if the unit acknowledged.
bool send(int address, const uint8_t* data, size_t length);
bool writeLetter(int address, int letter, uint8_t rpm);
bool writeLetterAndOffset(int address, int letter, uint8_t rpm, uint16_t offset);

// Reads. Update get(address) and return true if the unit answered.
bool readStatus(int address, bool withPosition = false);  // withPosition: also refresh the step position
bool readInfo(int address);  // v2 only
// v2 only: pull queued events and log them. Returns how many.
int pullEvents(int address);

// Unit parameters (2.1.0+, stored in the unit's EEPROM). Return false if the unit didn't answer
// or rejected the value.
bool getParam(int address, uint8_t id, uint16_t& value);
bool setParam(int address, uint8_t id, uint16_t value);
bool resetParams(int address);

// Quiet hours: put units (2.3+) into deep sleep / wake everyone with a re-home (full turn)
void sleepAll();
void rehomeAll(uint8_t rpm);

// Free a stuck bus (a unit holding SDA low). Done at startup and automatically when no unit answers.
bool recoverBus(const char* why);
// True once after a unit restarted / reappeared / shows the wrong letter: the display should redraw.
bool takeRedrawRequest();
uint32_t busRecoveries();

// Read the status of every configured unit. Returns true if any of them is still moving.
bool pollAll(bool idlePoll = false);  // idlePoll: background check - skips idle pre-2.0 units
// Units that should be there but didn't answer, or report an error (for the LED / web UI).
int problemCount();

String versionName(uint8_t version);
String i2cErrorName(uint8_t error);

}  // namespace units
