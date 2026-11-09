#include "commands.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_system.h>

#include "app.h"
#include "calibration.h"
#include "config.h"
#include "console.h"
#include "crash.h"
#include "display.h"
#include "log.h"
#include "modes.h"
#include "net.h"
#include "settings.h"
#include "units.h"

using console::print;
using console::cprintf;
using console::println;

// Console input -> flap index ("blank"/"space" for the blank flap), -1 if not on the flaps.
// Also used by calibration.cpp.
int letterFromInput(const String& input) {
  String lower = input;
  lower.toLowerCase();
  if (input == " " || lower == "blank" || lower == "space" || lower == "_") return 0;
  String t = input;
  t.trim();
  if (t.length() != 1) return -1;
  return unitproto::letterIndex(t[0]);
}

namespace commands {

// ---- Parsing helpers -----------------------------------------------------------------------
struct Args {
  String command;
  String rest;  // everything after the command
  String a1;    // first word of rest
  String a2;    // everything after a1
};

static Args parse(const String& line) {
  Args args;
  int space = line.indexOf(' ');
  args.command = space < 0 ? line : line.substring(0, space);
  args.command.toLowerCase();
  args.rest = space < 0 ? "" : line.substring(space + 1);
  args.rest.trim();
  int space2 = args.rest.indexOf(' ');
  args.a1 = space2 < 0 ? args.rest : args.rest.substring(0, space2);
  args.a2 = space2 < 0 ? "" : args.rest.substring(space2 + 1);
  args.a2.trim();
  return args;
}

static bool parseAddress(const String& text, int& address) {
  if (text.length() == 0 || text.length() > 2) return false;
  for (char c : text) {
    if (!isDigit(c)) return false;
  }
  address = text.toInt();
  return address >= 0 && address < MAX_UNITS;
}

static bool parseNumber(const String& text, long& value) {
  if (text.length() == 0) return false;
  for (size_t i = 0; i < text.length(); i++) {
    if (!isDigit(text[i]) && !(i == 0 && text[i] == '-')) return false;
  }
  value = text.toInt();
  return true;
}

// Apply a settings patch from the console: same validation as the web API
static bool applyPatch(JsonDocument& patch) {
  Settings updated;
  String error;
  if (!settings::merge(settings::current(), patch.as<JsonObjectConst>(), updated, error)) {
    println("Error: " + error);
    return false;
  }
  settings::commit(updated);
  modes::restart();
  return true;
}

// ---- Commands ------------------------------------------------------------------------------
static void help() {
  println(F("Display"));
  println(F("  show <text>           show text now (pauses the normal mode until 'resume')"));
  println(F("  text <text>           switch to text mode with this text (saved)"));
  println(F("  mode <name>           text clock date countdown trainstation randomphrase famousnumbers"));
  println(F("                        weather bart printers"));
  println(F("  notify <s> <text>     show text for s seconds, then back to the current mode"));
  println(F("  unit <addr> <char>    one character on one unit ('blank' for the blank flap)"));
  println(F("  ident                 every unit shows its address: 0123456789 left to right"));
  println(F("  pause / resume        stop / restart the normal mode"));
  println(F("Units"));
  println(F("  status [all]          status + firmware of each unit (all = addresses 0-15)"));
  println(F("  units                 diagnostics: voltage, magnet syncs, errors, uptime (v2 units)"));
  println(F("  events [addr]         fetch the units' event logs now"));
  println(F("  scan                  list I2C addresses that answer"));
  println(F("  cal <addr> [flips]    interactive calibration: press Enter whenever a flap drops"));
  println(F("  offset <addr> <n>     set a unit's offset (steps from magnet to blank flap) and re-home"));
  println(F("  home <addr>           go to the magnet edge with no offset (sensor check)"));
  println(F("  rehome <addr|all>     full turn to the magnet + offset (fixes a unit that's off)"));
  println(F("  unitreboot <addr>     restart a unit (v2 units)"));
  println(F("  i2creset              free a stuck I2C bus (also automatic when no unit answers)"));
  println(F("  param <addr|all> [name value | reset]   unit settings stored on the unit (2.1+)"));
  println(F("Settings & system"));
  println(F("  settings              print all settings (JSON)"));
  println(F("  set <key> <value>     change a setting, e.g.  set speed 60   set quietEnabled true"));
  println(F("  info                  firmware, memory, flash, uptime, reset reason"));
  println(F("  wifi / time           network details / clock and time zone"));
  println(F("  log <level|off>       stream the log here (serial: sets the serial log level)"));
  println(F("  crash [clear]         last crash report (task, address, backtrace) / erase it"));
  println(F("  reboot                restart the ESP32"));
  println(F("  quit                  close telnet"));
}

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "CRASH (panic)";
    case ESP_RST_INT_WDT: return "CRASH (interrupt watchdog)";
    case ESP_RST_TASK_WDT: return "CRASH (task watchdog)";
    case ESP_RST_WDT: return "CRASH (watchdog)";
    case ESP_RST_BROWNOUT: return "BROWN-OUT (power dipped)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "other";
  }
}

static void info() {
  cprintf("Firmware %s, built %s %s\r\n", FIRMWARE_VERSION, __DATE__, __TIME__);
  cprintf("Uptime %lus, reset reason: %s\r\n", millis() / 1000, resetReason());
  cprintf("CPU %d MHz, heap free %u (lowest %u, largest block %u)\r\n", (int)ESP.getCpuFreqMHz(),
          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  cprintf("Flash %u KB, firmware %u KB, free for OTA %u KB, LittleFS %u/%u KB\r\n",
          (unsigned)(ESP.getFlashChipSize() / 1024), (unsigned)(ESP.getSketchSize() / 1024),
          (unsigned)(ESP.getFreeSketchSpace() / 1024), (unsigned)(LittleFS.usedBytes() / 1024),
         (unsigned)(LittleFS.totalBytes() / 1024));
  cprintf("Mode %s, showing \"%s\", %d units\r\n", modes::status().c_str(), display::line().c_str(), units::count());
}

static void wifi() {
  cprintf("%s, ip %s, rssi %d dBm, channel %d, bssid %s\r\n", net::connected() ? "connected" : "NOT connected",
         net::ip().c_str(), net::rssi(), (int)WiFi.channel(), WiFi.BSSIDstr().c_str());
  cprintf("Up for %lus, %lu drop(s) since boot, last disconnect reason %d (%s)\r\n",
         net::connectedSinceMs() ? (millis() - net::connectedSinceMs()) / 1000 : 0UL, (unsigned long)net::reconnects(),
         net::lastDisconnectReason(), net::reasonName(net::lastDisconnectReason()));
}

static void timeCommand() {
  struct tm t;
  if (!net::localTime(t)) {
    println("Time not set yet (waiting for NTP)");
    return;
  }
  char buffer[48];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &t);
  cprintf("%s  (%s: %s)\r\n", buffer, settings::current().timezoneName.c_str(), settings::current().timezone.c_str());
}

static void status(bool all) {
  int last = all ? MAX_UNITS : units::count();
  for (int a = 0; a < last; a++) {
    bool ok = units::readStatus(a, true);
    const units::Unit& u = units::get(a);
    if (!ok) {
      if (a < units::count()) cprintf("  unit %2d: NO ANSWER (%s)\r\n", a, units::i2cErrorName(u.lastI2cError).c_str());
      continue;
    }
    cprintf("  unit %2d: %-32s firmware %-6s%s\r\n", a, unitproto::statusName(u.status),
           units::versionName(u.version).c_str(), a >= units::count() ? "  (beyond unitCount!)" : "");
    app::services();
  }
}

// AVR reset flags. Optiboot restarts itself through the watchdog after a reset-pin/USB reset, so
// "reset pin + watchdog" is a normal reset; "watchdog" alone means the unit really hung.
static const char* resetCauseName(uint8_t cause) {
  if (cause & 0x04) return "BROWN-OUT";
  if (cause & 0x02) return "reset pin";
  if (cause & 0x01) return "power on";
  if (cause & 0x08) return "WATCHDOG";
  return "?";
}

static void unitsTable() {
  println(F("addr fw     status       letter  vcc(min)    syncs miss  err(max) boots steps reset     up     i2c-err"));
  for (int a = 0; a < units::count(); a++) {
    bool ok = units::readStatus(a, true);
    units::Unit& u = units::get(a);
    if (!ok) {
      cprintf("%3d  NO ANSWER - %s (%lu I2C errors)\r\n", a, units::i2cErrorName(u.lastI2cError).c_str(),
             (unsigned long)u.i2cErrors);
      continue;
    }
    if (!u.isV2() || !units::readInfo(a)) {
      cprintf("%3d  %-6s %-12s  (no diagnostics before 2.0.0)                                    %lu\r\n", a,
             units::versionName(u.version).c_str(), unitproto::statusName(u.status), (unsigned long)u.i2cErrors);
      continue;
    }
    const unitproto::Info& i = u.info;
    char letter = i.letter < unitproto::FLAP_COUNT ? unitproto::LETTERS[i.letter] : '?';
    cprintf("%3d  %-6s %-12.12s  '%c'   %4u(%4u)  %5u %4u  %+4d(%3u) %5u %5d %-9s %5lum  %lu%s\r\n", a,
           units::versionName(u.version).c_str(), unitproto::statusName(u.status), letter, i.vcc, i.vccMin, i.syncs,
           i.misses, i.lastSyncError, i.maxSyncError, i.boots, u.stepsPerRev(), resetCauseName(i.resetCause),
           (unsigned long)(i.uptime / 60), (unsigned long)u.i2cErrors,
           (i.flags & unitproto::INFO_FLAG_NO_OFFSET) ? "  NO OFFSET SET" : "");
    app::services();
  }
  println(F("vcc = supply mV now (lowest while moving). syncs = magnet passes, err = steps corrected at the"));
  println(F("magnet (last, max). A growing err or miss count means the drum slips or the sensor is flaky."));
}

static void scan() {
  String found;
  for (int a = 0; a < 128; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) found += " " + String(a);
    if (a % 16 == 0) app::services();
  }
  println(found.length() ? "Answering addresses:" + found : "No I2C devices answered");
}

static void showNow(const String& text) {
  app::displayPaused = false;
  display::show(text, true, "left");
  // Run the display until it's written, then pause the normal mode so it stays up
  uint32_t start = millis();
  while (!display::idle() && millis() - start < 40000) {
    display::loop();
    app::services();
    delay(5);
  }
  app::displayPaused = true;
}

static void ident() {
  String text;
  for (int a = 0; a < units::count(); a++) text += a < 10 ? (char)('0' + a) : (char)('A' + a - 10);
  showNow(text);
  cprintf("Each unit shows its I2C address. Left to right you should read %s.\r\n", text.c_str());
  println("If not, a DIP switch doesn't match the unit's position. 'resume' when done.");
}

static void rehome(int address) {
  units::Unit& u = units::get(address);
  uint8_t rpm = unitproto::rpmFromSpeed(settings::current().speed);
  if (u.isV2()) {
    uint8_t cmd[2] = {unitproto::CMD_REHOME, rpm};
    units::send(address, cmd, 2);
  } else {
    // Old firmware re-homes on every full turn: go to the last flap, then back to blank
    units::writeLetter(address, unitproto::FLAP_COUNT - 1, rpm);
    app::wait(unitproto::MOVE_GUARD_MS + 300);
    calibration::waitReady(address, 20000);
    units::writeLetter(address, 0, rpm);
  }
}

// param <addr|all>                  list the unit's parameters
// param <addr|all> <name> <value>   change one (stored in the unit's EEPROM)
// param <addr|all> reset            all back to defaults
static void paramCommand(const Args& args) {
  int first, last, address;
  if (args.a1 == "all") {
    first = 0;
    last = units::count() - 1;
  } else if (parseAddress(args.a1, address)) {
    first = last = address;
  } else {
    println("usage: param <addr|all> [<name> <value> | reset]");
    return;
  }
  String name = args.a2;
  String valueText;
  int space = name.indexOf(' ');
  if (space > 0) {
    valueText = name.substring(space + 1);
    valueText.trim();
    name = name.substring(0, space);
  }

  for (int a = first; a <= last; a++) {
    units::readStatus(a, true);
    units::Unit& u = units::get(a);
    if (!u.present || u.version == unitproto::VERSION_UNKNOWN || u.version < unitproto::V21_FIRMWARE) {
      cprintf("unit %d: needs firmware 2.1.0+ (has %s)\r\n", a, units::versionName(u.version).c_str());
      continue;
    }
    if (name.length() == 0) {  // list
      cprintf("unit %d parameters:\r\n", a);
      for (uint8_t id = 0; id < unitproto::P_COUNT; id++) {
        uint16_t value;
        unitproto::ParamLimits l = unitproto::paramLimits(id);
        if (!units::getParam(a, id, value)) {
          cprintf("  %-16s (no answer)\r\n", unitproto::PARAM_NAMES[id]);
          continue;
        }
        cprintf("  %-16s %5u%s  (default %u, %u..%u)  %s\r\n", unitproto::PARAM_NAMES[id], value,
                value == l.def ? " " : "*", l.def, l.min, l.max, unitproto::PARAM_HELP[id]);
        app::services();
      }
      continue;
    }
    if (name == "reset") {
      cprintf("unit %d: %s\r\n", a, units::resetParams(a) ? "parameters reset to defaults" : "no answer");
      continue;
    }
    int id = unitproto::paramFromName(name.c_str());
    long value;
    if (id < 0) {
      println("Unknown parameter '" + name + "'. 'param " + String(a) + "' lists them.");
      return;
    }
    unitproto::ParamLimits l = unitproto::paramLimits(id);
    if (!parseNumber(valueText, value) || value < l.min || value > l.max) {
      cprintf("%s must be %u..%u\r\n", unitproto::PARAM_NAMES[id], l.min, l.max);
      return;
    }
    bool ok = units::setParam(a, id, (uint16_t)value);
    cprintf("unit %d: %s = %ld %s\r\n", a, unitproto::PARAM_NAMES[id], value, ok ? "(saved)" : "FAILED");
    if (ok) LOGI("units", "unit %d: parameter %s = %ld", a, unitproto::PARAM_NAMES[id], value);
  }
  if (name.length() > 0 && name != "reset") return;
  if (name.length() == 0) println("* = changed from the default. Change with: param <addr|all> <name> <value>");
}

void execute(const String& line) {
  Args args = parse(line);
  const String& c = args.command;
  int address;
  long value;

  if (c == "help" || c == "?") help();
  else if (c == "info") info();
  else if (c == "wifi") wifi();
  else if (c == "time") timeCommand();
  else if (c == "status") status(args.a1 == "all");
  else if (c == "units") unitsTable();
  else if (c == "scan") scan();
  else if (c == "events") {
    int total = 0;
    for (int a = 0; a < units::count(); a++) {
      if (args.a1.length() && (!parseAddress(args.a1, address) || address != a)) continue;
      total += units::pullEvents(a);
    }
    cprintf("%d event(s) - they're in the log ('log info' to see them here)\r\n", total);
  } else if (c == "show") {
    showNow(args.rest);
    println("Shown. The normal mode is paused - 'resume' when done.");
  } else if (c == "text") {
    JsonDocument patch;
    patch["mode"] = "text";
    patch["text"] = args.rest;
    if (applyPatch(patch)) {
      app::displayPaused = false;
      println("Text mode: " + args.rest);
    }
  } else if (c == "mode") {
    JsonDocument patch;
    patch["mode"] = args.a1;
    if (applyPatch(patch)) {
      app::displayPaused = false;
      println("Mode: " + args.a1);
    }
  } else if (c == "unit") {
    int letter = letterFromInput(args.a2);
    if (!parseAddress(args.a1, address) || letter < 0) {
      println("usage: unit <addr> <char>   (char must be on the flaps; 'blank' for the blank flap)");
      return;
    }
    app::displayPaused = true;
    units::writeLetter(address, letter, unitproto::rpmFromSpeed(settings::current().speed));
  } else if (c == "ident") ident();
  else if (c == "pause") {
    app::displayPaused = true;
    println("Normal mode paused.");
  } else if (c == "resume") {
    app::displayPaused = false;
    modes::restart();  // redraws the current mode
    println("Normal mode resumed.");
  } else if (c == "cal") {
    if (!parseAddress(args.a1, address)) return println("usage: cal <addr> [flips]");
    calibration::interactive(address, args.a2.length() ? args.a2.toInt() : 5);
  } else if (c == "offset") {
    if (!parseAddress(args.a1, address) || !parseNumber(args.a2, value) || value < 0 ||
        value >= units::get(address).stepsPerRev()) {
      return println("usage: offset <addr> <steps from the magnet edge, 0 .. steps per turn - 1>");
    }
    app::displayPaused = true;
    calibration::applyOffset(address, (uint16_t)value);
  } else if (c == "home") {
    if (!parseAddress(args.a1, address)) return println("usage: home <addr>");
    calibration::home(address);
  } else if (c == "rehome") {
    if (args.a1 == "all") {
      for (int a = 0; a < units::count(); a++) rehome(a);
      println("All units re-homing.");
    } else if (parseAddress(args.a1, address)) {
      rehome(address);
      cprintf("Unit %d re-homing.\r\n", address);
    } else {
      println("usage: rehome <addr|all>");
    }
  } else if (c == "crash") {
    if (args.a1 == "clear") {
      crash::clear();
      println("Crash report erased.");
    } else {
      println(crash::present() ? crash::summary() : String("No crash stored since the last 'crash clear'."));
    }
  } else if (c == "notify") {
    long seconds;
    if (!parseNumber(args.a1, seconds) || seconds < 1 || args.a2.length() == 0) {
      return println("usage: notify <seconds> <text>   (shows it, then back to the current mode)");
    }
    modes::notify(args.a2, (uint32_t)seconds * 1000UL);
  } else if (c == "param" || c == "params") {
    paramCommand(args);
  } else if (c == "i2creset") {
    println(units::recoverBus("console") ? "Bus is free (see the log for details)." : "Bus still stuck - power-cycle the display.");
  } else if (c == "unitreboot") {
    if (!parseAddress(args.a1, address)) return println("usage: unitreboot <addr>");
    if (!units::get(address).isV2()) return println("Needs unit firmware 2.0.0+");
    uint8_t cmd = unitproto::CMD_REBOOT;
    units::send(address, &cmd, 1);
    cprintf("Unit %d restarting (it re-homes by itself).\r\n", address);
  } else if (c == "settings" || c == "get") {
    JsonDocument doc;
    settings::toJson(settings::current(), doc.to<JsonObject>(), true);
    String out;
    serializeJsonPretty(doc, out);
    out.replace("\n", "\r\n");
    println(out);
  } else if (c == "set") {
    if (args.a1.length() == 0) return println("usage: set <key> <value>");
    JsonDocument patch;
    JsonDocument parsed;
    // Numbers / true / false as JSON, anything else as text
    if (deserializeJson(parsed, args.a2) == DeserializationError::Ok && !parsed.is<JsonObject>() &&
        !parsed.is<JsonArray>()) {
      patch[args.a1] = parsed.as<JsonVariant>();
    } else {
      patch[args.a1] = args.a2;
    }
    if (applyPatch(patch)) cprintf("%s = %s\r\n", args.a1.c_str(), args.a2.c_str());
  } else if (c == "reboot") {
    println("Rebooting...");
    app::wait(200);
    ESP.restart();
  } else {
    println("Unknown command '" + c + "' - type 'help'");
  }
}

}  // namespace commands
