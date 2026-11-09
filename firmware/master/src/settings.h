// All runtime settings, stored as one JSON file (/settings.json on LittleFS).
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
  int64_t at = 0;             // unix time to show it
  bool indefinitely = false;  // true = becomes the text-mode text; false = shown for SCHEDULED_SHOW_MS
};

struct Settings {
  // Display
  String mode = "text";       // text | clock | date | countdown | trainstation | randomphrase | famousnumbers
  String text = "";           // text mode message (multi-line: words wrap, \n forces a break)
  String alignment = "left";  // left | center | right
  int speed = 80;             // 1..100 (mapped to 1..12 rpm)
  int unitCount = 10;         // number of units, I2C addresses 0..unitCount-1
  int lineSeconds = 5;        // multi-line messages: seconds each line stays up

  // Time
  String timezone = "PST8PDT,M3.2.0,M11.1.0";  // POSIX TZ string
  String timezoneName = "America/Los_Angeles"; // label for the UI
  bool clock24h = false;
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

  // Scheduled messages
  std::vector<ScheduledMessage> scheduled;

  // Diagnostics
  String logLevel = "info";     // serial log level: debug | info | warn | error | off
};

namespace settings {

void begin();                 // mount LittleFS, load /settings.json (defaults if missing/corrupt)
Settings& current();          // main loop only
Settings snapshot();          // copy for other tasks (web handlers)
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
