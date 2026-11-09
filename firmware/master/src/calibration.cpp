#include "calibration.h"

#include <math.h>

#include "app.h"
#include "console.h"
#include "log.h"
#include "settings.h"
#include "units.h"

using console::print;
using console::cprintf;
using console::println;

int letterFromInput(const String& input);  // commands.cpp

namespace calibration {

using namespace unitproto;

static uint8_t rpm() { return rpmFromSpeed(settings::current().speed); }

int waitReady(int address, uint32_t timeoutMs) {
  uint32_t start = millis();
  app::wait(60);
  int last = -1;
  while (millis() - start < timeoutMs) {
    if (units::readStatus(address)) {
      last = units::get(address).status;
      if (last == STATUS_READY || last == STATUS_SLEEPING || last == STATUS_CAL_ERROR) return last;
    }
    app::wait(100);
  }
  return last;
}

static String letterName(int index) {
  if (index == 0) return "blank";
  return String("'") + LETTERS[index] + "'";
}

static bool checkUnit(int address, bool needCal) {
  if (!units::readStatus(address)) {
    cprintf("Unit %d does not answer (%s)\r\n", address, units::i2cErrorName(units::get(address).lastI2cError).c_str());
    return false;
  }
  if (needCal && !units::get(address).canCalibrate()) {
    cprintf("Unit %d runs firmware %s - this needs 1.3.0+. 'offset' still works.\r\n", address,
           units::versionName(units::get(address).version).c_str());
    return false;
  }
  return true;
}

bool applyOffset(int address, uint16_t offset) {
  if (!checkUnit(address, false)) return false;
  units::Unit& u = units::get(address);
  if (u.isV2()) {
    uint8_t store[3] = {CMD_SET_OFFSET, (uint8_t)(offset & 0xFF), (uint8_t)(offset >> 8)};
    uint8_t rehome[2] = {CMD_REHOME, rpm()};
    if (!units::send(address, store, 3) || !units::send(address, rehome, 2)) {
      println("Unit did not acknowledge.");
      return false;
    }
    cprintf("Offset %u stored on unit %d - re-homing...\r\n", offset, address);
    app::wait(300);
  } else {
    // Old firmware: a 4-byte letter command stores the offset; moving to a lower letter forces a
    // full turn, which is where the unit re-homes and applies it.
    if (!units::writeLetterAndOffset(address, 1, rpm(), offset)) {
      println("Unit did not acknowledge.");
      return false;
    }
    app::wait(MOVE_GUARD_MS + 200);
    waitReady(address, 20000);
    units::writeLetter(address, 0, rpm());
    cprintf("Offset %u stored on unit %d - re-homing (full turn)...\r\n", offset, address);
    app::wait(MOVE_GUARD_MS + 500);
  }
  int status = waitReady(address, 30000);
  if (status == STATUS_CAL_ERROR) {
    println("The unit reports a homing error - magnet not found.");
    return false;
  }
  LOGI("cal", "unit %d: offset set to %u", address, offset);
  cprintf("Done - unit %d should show the blank flap. Try: unit %d A, unit %d 9\r\n", address, address, address);
  return true;
}

void home(int address) {
  if (!checkUnit(address, true)) return;
  app::displayPaused = true;
  uint8_t cmd[2] = {CMD_HOME, 10};
  units::send(address, cmd, 2);
  cprintf("Unit %d homing...\r\n", address);
  if (waitReady(address, 30000) == STATUS_READY) {
    println("At the magnet edge (no offset applied). Any letter command leaves this mode.");
  } else {
    println("Homing failed - check the magnet and hall sensor.");
  }
}

void interactive(int address, int flipsWanted) {
  if (!checkUnit(address, true)) return;
  if (flipsWanted < 2) flipsWanted = 2;
  if (flipsWanted > 10) flipsWanted = 10;

  units::readInfo(address);
  const int stepsPerRev = units::get(address).stepsPerRev();
  const float nominal = (float)stepsPerRev / FLAP_COUNT;
  app::displayPaused = true;
  uint8_t stopCmd[1] = {CMD_STOP};
  uint8_t exitCmd[2] = {0, rpm()};  // any letter command leaves calibration mode
  LOGI("cal", "unit %d: calibration started", address);

  cprintf("Calibrating unit %d. Homing to the magnet...\r\n", address);
  uint8_t homeCmd[2] = {CMD_HOME, 10};
  units::send(address, homeCmd, 2);
  if (waitReady(address, 30000) != STATUS_READY) {
    println("Homing failed - magnet not found. Check the hall sensor and magnet.");
    units::send(address, exitCmd, 2);
    return;
  }

  int seen = -1;
  String line;
  while (seen < 0) {
    print("Which flap is showing now? Type it + Enter ('blank' for the blank flap, x = abort): ");
    if (!console::readLine(line, 180000) || line == "x" || line == "X") {
      println("Aborted.");
      units::send(address, exitCmd, 2);
      return;
    }
    seen = letterFromInput(line);
    if (seen < 0) println("That character isn't on the flaps, try again.");
  }

  long flips[10];
  int count = 0;
  long lastPosition = 0;
  println("The unit now creeps forward. Press Enter the moment each new flap drops.");
  println("(q + Enter = finish early, x + Enter = abort)");

  for (int k = 1; k <= flipsWanted; k++) {
    int target = (seen + k) % FLAP_COUNT;

    // After the first flip we know roughly where the next one is: go most of the way quickly
    if (count >= 1) {
      float perFlap = count >= 2 ? (float)(flips[count - 1] - flips[0]) / (count - 1) : nominal;
      int ahead = (int)(perFlap - 15);
      if (ahead > 0) {
        uint8_t move[4] = {CMD_MOVE, 8, (uint8_t)(ahead & 0xFF), (uint8_t)(ahead >> 8)};
        units::send(address, move, 4);
        waitReady(address, 15000);
      }
    }

    while (console::lineWaiting()) console::readLine(line, 0);  // ignore Enters typed while moving
    cprintf("  waiting for %s ... ", letterName(target).c_str());
    uint8_t jog[2] = {CMD_JOG, 80};  // one step every 80 ms
    units::send(address, jog, 2);

    bool gotFlip = false;
    uint32_t lastPoll = millis();
    uint32_t start = millis();
    while (true) {
      app::services();
      if (console::sessionGone()) {
        units::send(address, stopCmd, 1);
        units::send(address, exitCmd, 2);
        LOGW("cal", "unit %d: console went away - calibration aborted", address);
        return;
      }
      if (console::lineWaiting()) {
        console::readLine(line, 0);
        units::send(address, stopCmd, 1);
        app::wait(40);
        units::readStatus(address, true);
        long position = units::get(address).position;
        if (line == "x" || line == "X") {
          println("aborted.");
          units::send(address, exitCmd, 2);
          return;
        }
        if (line == "q" || line == "Q") {
          println("finishing.");
          break;
        }
        flips[count++] = position;
        lastPosition = position;
        cprintf("dropped at step %ld\r\n", position);
        gotFlip = true;
        break;
      }
      if (millis() - lastPoll > 250) {
        lastPoll = millis();
        if (units::readStatus(address, true) && units::get(address).position - lastPosition > (long)(nominal * 2.5f)) {
          units::send(address, stopCmd, 1);
          println("\r\nNo Enter for over two flaps - stopping. Run cal again.");
          units::send(address, exitCmd, 2);
          return;
        }
      }
      if (millis() - start > 120000) {
        units::send(address, stopCmd, 1);
        println("\r\nTimed out.");
        units::send(address, exitCmd, 2);
        return;
      }
      delay(5);
    }
    if (!gotFlip) break;
  }

  if (count < 1) {
    println("No drops recorded - nothing to calculate.");
    units::send(address, exitCmd, 2);
    return;
  }

  // Least squares: drop k (1..n) happens at  a + b*k  steps after the magnet edge
  float b = nominal;
  float a;
  if (count >= 2) {
    float sk = 0, sp = 0, skk = 0, skp = 0;
    for (int i = 0; i < count; i++) {
      float k = i + 1;
      sk += k;
      sp += flips[i];
      skk += k * k;
      skp += k * flips[i];
    }
    b = (count * skp - sk * sp) / (count * skk - sk * sk);
    a = (sp - b * sk) / count;
  } else {
    a = flips[0] - b;
  }

  // The blank flap (index 0) drops k0 flips after the one showing at the magnet
  int k0 = (FLAP_COUNT - seen) % FLAP_COUNT;
  float blankDrop = a + b * k0;
  long offset = lroundf(blankDrop + b / 2.0f);  // middle of the blank flap's window
  while (offset < 0) offset += stepsPerRev;
  while (offset >= stepsPerRev) offset -= stepsPerRev;

  println("");
  cprintf("Measured %d drops: %.2f steps per flap (nominal %.2f)\r\n", count, b, nominal);
  if (count >= 3 && fabsf(b - nominal) > 0.6f) {
    println("WARNING: far from what the unit uses. Run cal again; a v2.1 unit learns its real steps");
    println("         per turn by itself after a few full turns (see 'units').");
  }
  cprintf("Blank flap drops at step %.0f -> new offset %ld (blank drop + half a flap)\r\n", blankDrop, offset);
  LOGI("cal", "unit %d: %d drops, %.2f steps/flap, suggested offset %ld", address, count, b, offset);

  print("Save this offset to the unit? (y/n): ");
  if (console::readLine(line, 180000) && (line == "y" || line == "Y")) {
    applyOffset(address, (uint16_t)offset);
  } else {
    println("Not saved. Unit re-homing with its old offset.");
    units::send(address, exitCmd, 2);
  }
  println("The display stays paused - 'resume' when you're done.");
}

}  // namespace calibration
