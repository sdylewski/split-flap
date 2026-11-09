// All runtime settings, stored as one JSON document in NVS (survives firmware and web-file uploads).
//
// Threading: the main loop owns `settings::current()`. Web handlers (another task) never touch it:
// they take a `snapshot()`, validate a JSON patch onto that copy with `merge()`, and hand the result
// to the main loop with `app::post()`, which calls `commit()`.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#include <vector>

struct ScheduledMessage {
  String text;
  int64_t at = 0;             // unix time to show it (the first time)
  bool indefinitely = false;  // true = becomes the text-mode text; false = a pop-up
  int showSeconds = 0;        // pop-up length, 0 = the "scheduled" source's default
  int repeatMinutes = 0;      // show it again every N minutes ...
  int repeatCount = 0;        // ... this many more times (e.g. every 60 min, 8 times)
};

// Pop-up sources: things that take over the display for a while on top of the base mode.
enum Source : uint8_t {
  SRC_PRINTERS,      // print done / failed (event) + progress (timer)
  SRC_NOTIFY,        // messages from Home Assistant, Siri, the API ("seconds")
  SRC_SCHEDULED,     // scheduled messages
  SRC_WEATHER,
  SRC_BART,          // timer, and/or "a train is N minutes away" (trainMinutes)
  SRC_DATE,
  SRC_COUNTDOWN,
  SRC_PHRASES,       // random phrase
  SRC_NUMBERS,       // famous numbers
  SRC_TRAINS,        // train station board
  SRC_COUNT
};
// Ids in settings / the API, and the mode each source shows (empty = event text only)
static const char* const SOURCE_IDS[SRC_COUNT] = {"printers", "notify",   "scheduled",    "weather",       "bart",
                                                  "date",     "countdown", "randomphrase", "famousnumbers", "trainstation"};

struct SourceRule {
  bool enabled = false;
  int priority = 1;           // popup::LOW / NORMAL / HIGH
  int showSeconds = 60;       // how long it holds the display
  int everyMinutes = 0;       // timer: show every N minutes (0 = only on events)
  String from = "";           // timer window "HH:MM" .. "HH:MM" (empty = all day)
  String to = "";
  int days = 0x7F;            // timer weekdays: bit 0 = Sunday .. bit 6 = Saturday
  int trainMinutes = 0;       // BART only: pop up when a train is this many minutes away (0 = off)
};

struct PrinterConfig {
  String name;  // shown on the display, e.g. "PRINTY"
  String host;  // Klipper/Moonraker host: "printy.local" or an IP (port 7125 is added)
};

struct Settings {
  // Display
  String mode = "text";       // text | clock | date | countdown | trainstation | randomphrase | famousnumbers
                              // | weather | bart | printers
  String text = "";           // text mode message (multi-line: words wrap, \n forces a break)
  String alignment = "left";  // left | center | right
  int speed = 80;             // 1..100 (mapped to 1..12 rpm)
  int unitCount = 10;         // number of units, I2C addresses 0..unitCount-1
  int lineSeconds = 5;        // multi-line messages: seconds each line stays up

  // Time
  String timezone = "PST8PDT,M3.2.0,M11.1.0";  // POSIX TZ string
  String timezoneName = "America/Los_Angeles"; // label for the UI
  bool clock24h = false;
  String clockDay = "none";     // none | number (day of month: "05  06:55") | weekday ("MON 06:55")
  bool clockLeadingZero = true; // "06:55" vs "6:55"
  String dateFormat = "%a %b %d";              // strftime, e.g. "SUN NOV 08"

  // Countdown mode
  int64_t countdownTo = 0;  // unix time

  // Train station mode
  String trainType = "random";  // random | line | bart
  String trainLine = "";
  int trainSeconds = 30;
  String trainStations = "";    // comma separated; empty = built-in list

  // Random phrase mode
  String phrases = "";          // comma separated
  int phraseMinSeconds = 10;
  int phraseMaxSeconds = 60;

  // Famous numbers mode
  String numbers = "";          // "3.14159,PI,2.71828,EULER E" (number,name pairs); empty = built-in
  int numberMinSeconds = 10;
  int numberMaxSeconds = 60;
  int numberNameSeconds = 5;

  // Quiet hours: no display changes between quietFrom and quietTo
  bool quietEnabled = false;
  String quietFrom = "22:00";
  String quietTo = "08:00";
  bool quietBlank = false;      // true = blank the display at the start of quiet hours
  bool quietPowerSave = true;   // quiet hours: WiFi power save, 80 MHz CPU, units in deep sleep (2.3+)
  bool quietRehome = true;      // when quiet hours end: every unit re-homes (a full turn) before showing

  // Scheduled messages
  std::vector<ScheduledMessage> scheduled;

  // Wave reveal
  //   off        all units start together
  //   arrive     letters LAND waveMs apart: units with further to go start earlier
  //   start      units START waveMs apart (and land whenever)
  //   sequential each unit starts waveMs after the previous one has landed
  String waveStyle = "off";
  int waveMs = 300;             // spacing between units
  String waveOrder = "left";    // left | right | center | random

  // Weather mode (Open-Meteo, no account needed)
  float weatherLat = 37.7749f;
  float weatherLon = -122.4194f;
  String weatherPlace = "San Francisco";
  bool weatherFahrenheit = true;
  int weatherSeconds = 8;       // seconds per page (now / high-low / rain)

  // BART mode (api.bart.gov)
  String bartStation = "MONT";  // station abbreviation, see https://api.bart.gov/docs/overview/abbrev.aspx
  String bartDirection = "";    // "" = both, "n" = north, "s" = south
  String bartKey = "MW9S-E7SL-26DU-VV8V";  // BART's public key; register your own at https://api.bart.gov/api/register.aspx
  int bartSeconds = 6;          // seconds per departure

  // 3D printers (Klipper / Moonraker): progress mode + "done" pop-ups (SRC_PRINTERS)
  std::vector<PrinterConfig> printers;

  // Pop-up rules per source (defaults set in settings.cpp)
  SourceRule sources[SRC_COUNT];
  Settings();

  // Diagnostics
  String logLevel = "info";     // serial log level: debug | info | warn | error | off
};

namespace settings {

void begin();                 // load from NVS (defaults if missing/corrupt); also mounts LittleFS
Settings& current();          // main loop only
Settings snapshot();          // copy for other tasks (web handlers)
uint32_t revision();          // changes whenever the settings change (cheap check before snapshot())
void commit(const Settings& updated);  // main loop only: replace, save, publish
bool save();

// Validate `patch` (any subset of keys) on top of `base` into `out`. Returns false + error message
// on the first invalid value. Unknown keys are ignored. Thread-safe (pure function).
bool merge(const Settings& base, JsonObjectConst patch, Settings& out, String& error);

void toJson(const Settings& s, JsonObject out, bool includeScheduled = true);
bool isValidMode(const String& mode);

// Scheduled messages (main loop)
bool addScheduled(Settings& s, const ScheduledMessage& message, String& error);
bool removeScheduled(Settings& s, int64_t at);

}  // namespace settings
