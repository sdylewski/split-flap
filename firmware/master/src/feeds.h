// Live data from the internet / the local network, fetched by a background task so a slow
// server never stalls the display:
//   - weather       Open-Meteo (no account), every 10 minutes while the weather mode is on
//   - BART          api.bart.gov departures, every 30 s while the bart mode is on
//   - 3D printers   Klipper/Moonraker on the LAN, every 15 s (if any are configured)
// Modes read the latest results; a print finishing (or failing) posts a notification.
#pragma once
#include <Arduino.h>

#include <vector>

namespace feeds {

struct Weather {
  bool valid = false;
  uint32_t fetchedMs = 0;     // when the data was downloaded
  uint32_t attemptMs = 0;     // when the last download was tried (success or failure)
  uint32_t attempts = 0;      // downloads tried since boot
  float now = 0;          // current temperature
  int codeNow = 0;        // WMO weather code
  float high = 0, low = 0;
  int rainChance = 0;     // % today
  int codeToday = 0;
  String error;
};

struct Departure {
  String destination;     // 4-letter code, e.g. "DALY"
  int minutes;            // 0 = leaving
  char direction;         // 'n' / 's'
};

struct Bart {
  bool valid = false;
  uint32_t fetchedMs = 0;
  String station;
  std::vector<Departure> departures;  // soonest first
  String error;
};

struct Printer {
  String name;
  String host;
  bool reachable = false;
  String state;           // standby | printing | paused | complete | cancelled | error
  float progress = 0;     // 0..1
  int minutesLeft = -1;   // estimate, -1 = unknown
  String file;
};

void begin();
Weather weather();

// Download the weather as soon as possible (the web button, boot, wake-up from quiet hours).
// Safe from any task; does nothing if a download is already pending.
void refreshWeather();
Bart bart();
std::vector<Printer> printers();

// Short description of a WMO weather code that fits on the flaps ("CLEAR", "RAIN", ...)
const char* weatherWord(int code);

}  // namespace feeds
