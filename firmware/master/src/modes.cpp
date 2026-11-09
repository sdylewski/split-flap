#include "modes.h"

#include <vector>

#include "app.h"
#include "config.h"
#include "content.h"
#include "display.h"
#include "feeds.h"
#include "log.h"
#include "net.h"
#include "settings.h"
#include "text_layout.h"
#include "units.h"

namespace modes {

// ---- State ---------------------------------------------------------------------------------
static String activeMode;
static uint32_t lastTick = 0;
static uint32_t nextChangeMs = 0;  // when the rotating modes pick the next item
static int currentItem = -1;
static int lineStop = -1;
static bool showingName = false;
static uint32_t nameAtMs = 0;

static bool inQuiet = false;
static String overlayText;         // one-off scheduled message / notification
static uint32_t overlayUntil = 0;
static size_t pageIndex = 0;       // weather / bart / printers: which page is showing
static uint32_t pageUntil = 0;

// ---- Helpers -------------------------------------------------------------------------------
static std::vector<String> splitList(const String& list) {
  std::vector<String> items;
  int start = 0;
  while (start <= (int)list.length()) {
    int comma = list.indexOf(',', start);
    if (comma < 0) comma = list.length();
    String item = list.substring(start, comma);
    item.trim();
    if (item.length() > 0) items.push_back(item);
    start = comma + 1;
  }
  return items;
}

static uint32_t randomDelayMs(int minSeconds, int maxSeconds) {
  if (maxSeconds < minSeconds) maxSeconds = minSeconds;
  return (uint32_t)random(minSeconds, maxSeconds + 1) * 1000UL;
}

static int pickOther(int count, int current) {
  if (count <= 1) return 0;
  int next;
  do {
    next = random(0, count);
  } while (next == current);
  return next;
}

static bool timeDue(uint32_t at) { return (int32_t)(millis() - at) >= 0; }

void restart() {
  activeMode = "";  // loop() re-initializes on the next tick
}

// Cycle through pages, each shown for `seconds` (times its number of lines if it wraps).
// Called every tick with fresh pages, so changing data (minutes, temperature) updates in place.
static void runPages(const std::vector<String>& pages, int seconds) {
  if (pages.empty()) return;
  if (pageIndex >= pages.size()) pageIndex = 0;
  if (pageUntil == 0 || timeDue(pageUntil)) {
    if (pageUntil != 0) pageIndex = (pageIndex + 1) % pages.size();
    size_t lines = layout::splitLines(pages[pageIndex].c_str(), units::count()).size();
    pageUntil = millis() + (uint32_t)seconds * 1000UL * (lines < 1 ? 1 : lines);
  }
  display::show(pages[pageIndex]);
}

// "a b" if it fits on the display, otherwise on two lines
static String fit(const String& a, const String& b) {
  return (int)(a.length() + 1 + b.length()) <= units::count() ? a + " " + b : a + "\n" + b;
}

// ---- Modes ---------------------------------------------------------------------------------
static void runClock(const Settings& s) {
  struct tm t;
  if (!net::localTime(t)) return display::show("NO TIME");
  char buffer[16];
  strftime(buffer, sizeof(buffer), s.clock24h ? "%H:%M" : "%I:%M%p", &t);
  display::show(buffer);
}

static void runDate(const Settings& s) {
  struct tm t;
  if (!net::localTime(t)) return display::show("NO TIME");
  char buffer[40];
  strftime(buffer, sizeof(buffer), s.dateFormat.c_str(), &t);
  display::show(buffer);
}

static void runCountdown(const Settings& s) {
  if (!net::timeValid()) return display::show("NO TIME");
  if (s.countdownTo <= 0) return display::show("NO DATE");
  long days = layout::daysUntil(s.countdownTo, time(nullptr));
  String text = String(days) + (days == 1 ? " DAY" : " DAYS");
  if ((int)text.length() > units::count()) text = String(days);
  display::show(text);
}

static const content::TrainLine* findLine(const String& id) {
  String key = id;
  key.replace('-', '_');
  for (uint8_t i = 0; i < content::TRAIN_LINE_COUNT; i++) {
    if (key == content::TRAIN_LINES[i]->id) return content::TRAIN_LINES[i];
  }
  return nullptr;
}

static void runTrainStation(const Settings& s) {
  if (!timeDue(nextChangeMs)) return;
  nextChangeMs = millis() + (uint32_t)s.trainSeconds * 1000UL;
  if (s.trainType == "random") {
    std::vector<String> custom = splitList(s.trainStations);
    int count = custom.empty() ? content::RANDOM_STATION_COUNT : (int)custom.size();
    currentItem = pickOther(count, currentItem);
    display::show(custom.empty() ? String(content::RANDOM_STATIONS[currentItem]) : custom[currentItem]);
    return;
  }
  const content::TrainLine* line = findLine(s.trainLine);
  if (!line) line = s.trainType == "bart" ? findLine("bart-red") : findLine("eurostar");
  lineStop = (lineStop + 1) % line->count;
  display::show(line->stops[lineStop]);
}

static void runRandomPhrase(const Settings& s) {
  std::vector<String> phrases = splitList(s.phrases);
  if (phrases.empty()) return display::show("NO PHRASES");
  if (!timeDue(nextChangeMs)) return;
  nextChangeMs = millis() + randomDelayMs(s.phraseMinSeconds, s.phraseMaxSeconds);
  currentItem = pickOther(phrases.size(), currentItem);
  display::show(phrases[currentItem]);
}

static void runFamousNumbers(const Settings& s) {
  // Custom list: number,name,number,name,...
  std::vector<String> parts = splitList(s.numbers);
  int count = parts.size() >= 2 ? (int)parts.size() / 2 : content::NUMBER_COUNT;
  auto number = [&](int i) { return parts.size() >= 2 ? parts[i * 2] : String(content::NUMBERS[i][0]); };
  auto name = [&](int i) { return parts.size() >= 2 ? parts[i * 2 + 1] : String(content::NUMBERS[i][1]); };

  if (currentItem >= 0 && currentItem < count && !showingName && timeDue(nameAtMs)) {
    showingName = true;
    display::show(name(currentItem));
  }
  if (!timeDue(nextChangeMs)) return;
  nextChangeMs = millis() + randomDelayMs(s.numberMinSeconds, s.numberMaxSeconds);
  currentItem = pickOther(count, currentItem);
  showingName = false;
  nameAtMs = millis() + (uint32_t)s.numberNameSeconds * 1000UL;
  display::show(number(currentItem));
}

static void runWeather(const Settings& s) {
  feeds::Weather w = feeds::weather();
  if (!w.valid) return display::show(w.error.length() ? "NO WEATHER" : "WEATHER...");
  String unit = s.weatherFahrenheit ? "F" : "C";
  std::vector<String> pages;
  pages.push_back(String((int)lroundf(w.now)) + unit + " " + feeds::weatherWord(w.codeNow));
  pages.push_back("HI" + String((int)lroundf(w.high)) + " LO" + String((int)lroundf(w.low)));
  pages.push_back(w.rainChance >= 10 ? "RAIN " + String(w.rainChance) : String("NO RAIN"));
  runPages(pages, s.weatherSeconds);
}

static void runBart(const Settings& s) {
  feeds::Bart b = feeds::bart();
  if (!b.valid) return display::show(b.error.length() ? "BART ERROR" : "BART...");
  if (b.departures.empty()) return display::show("NO TRAINS");
  std::vector<String> pages;
  for (size_t i = 0; i < b.departures.size() && i < 4; i++) {
    const feeds::Departure& d = b.departures[i];
    String minutes = d.minutes == 0 ? "NOW" : (d.minutes < 10 ? String(d.minutes) + " MIN" : String(d.minutes) + "MIN");
    pages.push_back(d.destination + " " + minutes);  // "DALY 5 MIN", "ANTC 13MIN", "RICH NOW"
  }
  runPages(pages, s.bartSeconds);
}

static void runPrinters(const Settings& s) {
  std::vector<feeds::Printer> list = feeds::printers();
  if (s.printers.empty()) return display::show("NO PRINTER");
  if (list.empty()) return display::show("PRINTERS..");
  std::vector<String> pages;
  for (const feeds::Printer& p : list) {
    if (!p.reachable) {
      pages.push_back(fit(p.name, "OFF"));
    } else if (p.state == "printing" || p.state == "paused") {
      pages.push_back(fit(p.name, p.state == "paused" ? "PAUSE" : String((int)(p.progress * 100))));
      if (p.minutesLeft >= 0) {
        char left[12];
        snprintf(left, sizeof(left), "LEFT %d:%02d", p.minutesLeft / 60, p.minutesLeft % 60);
        pages.push_back(left);
      }
    } else if (p.state == "complete") {
      pages.push_back(fit(p.name, "DONE"));
    } else if (p.state == "error") {
      pages.push_back(fit(p.name, "ERROR"));
    } else {
      pages.push_back(fit(p.name, "IDLE"));
    }
  }
  runPages(pages, s.lineSeconds);
}

// ---- Overlays ------------------------------------------------------------------------------
static bool quietNow(const Settings& s) {
  if (!s.quietEnabled) return false;
  struct tm t;
  if (!net::localTime(t)) return false;
  int now = t.tm_hour * 60 + t.tm_min;
  return layout::inWindow(now, layout::parseHHMM(s.quietFrom.c_str()), layout::parseHHMM(s.quietTo.c_str()));
}

static void checkScheduled() {
  if (!net::timeValid()) return;
  Settings& s = settings::current();
  int64_t now = time(nullptr);
  for (size_t i = 0; i < s.scheduled.size(); i++) {
    if (s.scheduled[i].at > now) continue;
    ScheduledMessage m = s.scheduled[i];
    Settings updated = s;
    updated.scheduled.erase(updated.scheduled.begin() + i);
    if (m.indefinitely) {
      LOGI("modes", "scheduled message \"%s\" - now the text-mode text", m.text.c_str());
      updated.mode = "text";
      updated.text = m.text;
      settings::commit(updated);
      restart();
    } else {
      LOGI("modes", "scheduled message \"%s\" - showing for %us", m.text.c_str(), SCHEDULED_SHOW_MS / 1000);
      settings::commit(updated);
      overlayText = m.text;
      overlayUntil = millis() + SCHEDULED_SHOW_MS;
      display::show(m.text, true);
    }
    return;  // one per tick
  }
}

// ---- Loop ----------------------------------------------------------------------------------
void begin() { restart(); }

void loop() {
  if (app::displayPaused || app::otaActive) return;
  if (millis() - lastTick < 250) return;
  lastTick = millis();

  const Settings& s = settings::current();

  // Quiet hours: freeze (or blank) the display
  bool quiet = quietNow(s);
  if (quiet != inQuiet) {
    inQuiet = quiet;
    LOGI("modes", quiet ? "quiet hours started (until %s)" : "quiet hours ended", s.quietTo.c_str());
    if (quiet && s.quietBlank) display::show("", true);
    if (!quiet) restart();
  }
  if (inQuiet) return;

  checkScheduled();
  if (overlayUntil != 0) {
    if (!timeDue(overlayUntil) || !display::idle()) return;
    overlayUntil = 0;
    overlayText = "";
    restart();
  }

  if (activeMode != s.mode) {
    LOGI("modes", "mode: %s", s.mode.c_str());
    activeMode = s.mode;
    nextChangeMs = millis();
    currentItem = -1;
    lineStop = -1;
    showingName = false;
    nameAtMs = 0;
    pageIndex = 0;
    pageUntil = 0;
    if (s.mode == "text") display::show(s.text, true);
  }

  if (s.mode == "text") display::show(s.text);
  else if (s.mode == "clock") runClock(s);
  else if (s.mode == "date") runDate(s);
  else if (s.mode == "countdown") runCountdown(s);
  else if (s.mode == "trainstation") runTrainStation(s);
  else if (s.mode == "randomphrase") runRandomPhrase(s);
  else if (s.mode == "famousnumbers") runFamousNumbers(s);
  else if (s.mode == "weather") runWeather(s);
  else if (s.mode == "bart") runBart(s);
  else if (s.mode == "printers") runPrinters(s);
}

void notify(const String& text, uint32_t durationMs) {
  if (app::displayPaused || app::otaActive || inQuiet) {
    LOGI("modes", "notification \"%s\" not shown (%s)", text.c_str(), inQuiet ? "quiet hours" : "display busy");
    return;
  }
  LOGI("modes", "notification \"%s\" for %lus", text.c_str(), (unsigned long)(durationMs / 1000));
  overlayText = text;
  overlayUntil = millis() + durationMs;
  display::show(text, true);
}

String status() {
  const Settings& s = settings::current();
  if (app::otaActive) return "OTA update";
  if (app::displayPaused) return "paused (console)";
  if (inQuiet) return "quiet hours until " + s.quietTo;
  if (overlayUntil != 0) return "message: " + overlayText;
  return s.mode;
}

}  // namespace modes
