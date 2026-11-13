#include "modes.h"

#include <vector>

#include "app.h"
#include "config.h"
#include "content.h"
#include "display.h"
#include "feeds.h"
#include "log.h"
#include "net.h"
#include "popup_rules.h"
#include "settings.h"
#include "text_layout.h"
#include "units.h"

namespace modes {

// ---- State ---------------------------------------------------------------------------------
static String activeMode;
static uint32_t lastTick = 0;
static bool started = false;       // rotating modes: the first item has been shown
static uint32_t holdMs = 0;        // how long the current item stays after it has landed
static int currentItem = -1;
static int lineStop = -1;
static bool showingName = false;

static bool inQuiet = false;
static size_t pageIndex = 0;       // weather / bart / printers: which page is showing

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


// The current text has fully landed (every line of it) and been readable for at least `ms`.
// All "seconds" settings of the rotating modes count from here, not from when the flaps started.
static bool shownFor(uint32_t ms) {
  uint32_t since = display::idleSinceMs();
  return since != 0 && millis() - since >= ms;
}

// Cycle through pages, each shown for `seconds` (times its number of lines if it wraps).
// Called every tick with fresh pages, so changing data (minutes, temperature) updates in place.
static void runPages(const std::vector<String>& pages, int seconds) {
  if (pages.empty()) return;
  if (pageIndex >= pages.size()) pageIndex = 0;
  if (started && shownFor((uint32_t)seconds * 1000UL)) pageIndex = (pageIndex + 1) % pages.size();
  started = true;
  display::show(pages[pageIndex]);  // every tick: changing data (minutes, temperature) updates in place
}

// "a b" if it fits on the display, otherwise on two lines
static String fit(const String& a, const String& b) {
  return (int)(a.length() + 1 + b.length()) <= units::count() ? a + " " + b : a + "\n" + b;
}

// ---- Modes ---------------------------------------------------------------------------------
static void runClock(const Settings& s) {
  struct tm t;
  if (!net::localTime(t)) return display::show("NO TIME");
  display::show(layout::clockText(t.tm_hour, t.tm_min, t.tm_mday, t.tm_wday, s.clockHours.c_str(), s.clockLeadingZero,
                                  s.clockDay.c_str(), units::count())
                    .c_str());
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
  if (started && !shownFor((uint32_t)s.trainSeconds * 1000UL)) return;
  started = true;
  if (s.trainType == "random") {
    std::vector<String> custom = splitList(s.trainStations);
    int count = custom.empty() ? content::RANDOM_STATION_COUNT : (int)custom.size();
    currentItem = pickOther(count, currentItem);
    display::show(custom.empty() ? String(content::RANDOM_STATIONS[currentItem]) : custom[currentItem]);
    return;
  }
  const content::TrainLine* line = findLine(s.trainLine);
  if (!line) line = content::TRAIN_LINES[0];  // unknown / no line chosen yet
  lineStop = (lineStop + 1) % line->count;
  display::show(line->stops[lineStop]);
}

static void runRandomPhrase(const Settings& s) {
  std::vector<String> phrases = splitList(s.phrases);
  if (phrases.empty()) return display::show("NO PHRASES");
  if (started && !shownFor(holdMs)) return;  // min..max seconds after the whole phrase has landed
  started = true;
  holdMs = randomDelayMs(s.phraseMinSeconds, s.phraseMaxSeconds);
  currentItem = pickOther(phrases.size(), currentItem);
  display::show(phrases[currentItem]);
}

static void runFamousNumbers(const Settings& s) {
  // Custom list: number,name,number,name,...
  std::vector<String> parts = splitList(s.numbers);
  int count = parts.size() >= 2 ? (int)parts.size() / 2 : content::NUMBER_COUNT;
  auto number = [&](int i) { return parts.size() >= 2 ? parts[i * 2] : String(content::NUMBERS[i][0]); };
  auto name = [&](int i) { return parts.size() >= 2 ? parts[i * 2 + 1] : String(content::NUMBERS[i][1]); };

  // number -> (name seconds after it landed) name -> (min..max seconds after that landed) next number
  if (started && currentItem >= 0 && currentItem < count) {
    if (!showingName) {
      if (!shownFor((uint32_t)s.numberNameSeconds * 1000UL)) return;
      showingName = true;
      holdMs = randomDelayMs(s.numberMinSeconds, s.numberMaxSeconds);
      display::show(name(currentItem));
      return;
    }
    if (!shownFor(holdMs)) return;
  }
  started = true;
  currentItem = pickOther(count, currentItem);
  showingName = false;
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

// ---- Quiet hours ---------------------------------------------------------------------------
static bool quietNow(const Settings& s) {
  if (!s.quietEnabled) return false;
  struct tm t;
  if (!net::localTime(t)) return false;
  int now = t.tm_hour * 60 + t.tm_min;
  return layout::inWindow(now, layout::parseHHMM(s.quietFrom.c_str()), layout::parseHHMM(s.quietTo.c_str()));
}

// ---- Pop-ups -------------------------------------------------------------------------------
// A pop-up holds the display for its duration; the base mode (settings.mode) shows otherwise.
// Who may interrupt whom: popup_rules.h (high > normal > low, equal never interrupts).
struct Popup {
  bool active = false;
  int source = -1;
  int priority = 0;
  uint32_t durationMs = 0;
  uint32_t shownAt = 0;  // when it had fully landed (0 = still flapping in); the duration counts from here
  String text;  // fixed text ("PRINTY DONE"), or empty ...
  String mode;  // ... and a mode to run instead ("weather", "bart", ...)
};
static Popup current;
static uint32_t lastPopupEnd = 0;
static int timerSlot[SRC_COUNT];  // minute-of-day slot already handled per source (-1 = none)
static String lastTrainKey;

static void resetRenderState() {
  started = false;
  holdMs = 0;
  currentItem = -1;
  lineStop = -1;
  showingName = false;
  pageIndex = 0;
}

void restart() {
  activeMode = "";  // loop() re-initializes on the next tick
}

static bool showPopup(int source, const String& text, const String& mode, uint32_t durationMs, int priority) {
  const char* id = source >= 0 && source < SRC_COUNT ? SOURCE_IDS[source] : "?";
  const String& what = text.length() ? text : mode;
  if (app::displayPaused || app::otaActive || inQuiet) {
    LOGI("popup", "%s \"%s\" not shown (%s)", id, what.c_str(), inQuiet ? "quiet hours" : "display busy");
    return false;
  }
  if (!popup::admits(priority, current.active, current.priority, millis() - lastPopupEnd)) {
    LOGD("popup", "%s \"%s\" (%s) ignored: %s", id, what.c_str(), popup::priorityName(priority),
         current.active ? ("\"" + (current.text.length() ? current.text : current.mode) + "\" is showing").c_str()
                        : "a pop-up just ended");
    return false;
  }
  current.active = true;
  current.source = source;
  current.priority = priority;
  current.durationMs = durationMs;
  current.shownAt = 0;
  current.text = text;
  current.mode = mode;
  resetRenderState();
  LOGI("popup", "%s \"%s\" (%s) for %lus", id, what.c_str(), popup::priorityName(priority),
       (unsigned long)(durationMs / 1000));
  if (text.length()) display::show(text, true);
  return true;
}

bool notify(const String& text, uint32_t durationMs, int priority) {
  const SourceRule& rule = settings::current().sources[SRC_NOTIFY];
  if (!rule.enabled) {
    LOGI("popup", "notify \"%s\" not shown (notifications are off)", text.c_str());
    return false;
  }
  return showPopup(SRC_NOTIFY, text, "", durationMs ? durationMs : rule.showSeconds * 1000UL,
                   priority >= 0 ? priority : rule.priority);
}

void printerProgress(const String& text, uint32_t durationMs, int priority) {
  if (settings::current().sources[SRC_PRINTERS].enabled) showPopup(SRC_PRINTERS, text, "", durationMs, priority);
}

// Pending repeats of "PRINTY DONE": one per printer
struct DoneRepeat {
  String printer;
  String text;
  popup::Repeat timing;
};
static std::vector<DoneRepeat> doneRepeats;

void printerCleared(const String& printer) {
  for (size_t i = 0; i < doneRepeats.size(); i++) {
    if (doneRepeats[i].printer == printer) {
      LOGD("popup", "%s: no more repeats of \"%s\" (it started something new)", printer.c_str(), doneRepeats[i].text.c_str());
      doneRepeats.erase(doneRepeats.begin() + i);
      return;
    }
  }
}

void printerEvent(const String& printer, const String& text, bool repeat) {
  const Settings& s = settings::current();
  const SourceRule& rule = s.sources[SRC_PRINTERS];
  if (!rule.enabled) return;
  showPopup(SRC_PRINTERS, text, "", rule.showSeconds * 1000UL, rule.priority);
  printerCleared(printer);  // a new event replaces older repeats of the same printer
  if (repeat && s.printerDoneRepeatCount > 0) {
    DoneRepeat entry;
    entry.printer = printer;
    entry.text = text;
    entry.timing.remaining = s.printerDoneRepeatCount;
    entry.timing.intervalMs = (uint32_t)s.printerDoneRepeatMinutes * 60000UL;
    entry.timing.nextAt = millis() + entry.timing.intervalMs;
    doneRepeats.push_back(entry);
    LOGI("popup", "%s: \"%s\" will repeat %d more time(s), every %d min", printer.c_str(), text.c_str(),
         s.printerDoneRepeatCount, s.printerDoneRepeatMinutes);
  }
}

// Called every tick: show a repeat when it's due and may be shown (not in quiet hours, not paused,
// not while something equally important is up). A repeat that can't be shown yet just waits.
static void checkDoneRepeats(const Settings& s) {
  const SourceRule& rule = s.sources[SRC_PRINTERS];
  for (size_t i = 0; i < doneRepeats.size();) {
    DoneRepeat& r = doneRepeats[i];
    if (!rule.enabled || s.printerDoneRepeatCount == 0) {  // switched off meanwhile
      doneRepeats.erase(doneRepeats.begin() + i);
      continue;
    }
    bool canShow = !(app::displayPaused || app::otaActive || inQuiet) &&
                   popup::admits(rule.priority, current.active, current.priority, millis() - lastPopupEnd);
    if (popup::repeatDue(r.timing, millis(), canShow) &&
        showPopup(SRC_PRINTERS, r.text, "", rule.showSeconds * 1000UL, rule.priority)) {
      popup::repeatShown(r.timing, millis());
      LOGI("popup", "%s: repeat of \"%s\", %d more", r.printer.c_str(), r.text.c_str(), r.timing.remaining);
      if (r.timing.remaining <= 0) {
        doneRepeats.erase(doneRepeats.begin() + i);
        continue;
      }
    }
    i++;
  }
}

// Scheduled messages: show when due; repeating ones move their time forward and stay in the list
static void checkScheduled() {
  if (!net::timeValid()) return;
  Settings& s = settings::current();
  int64_t now = time(nullptr);
  for (size_t i = 0; i < s.scheduled.size(); i++) {
    if (s.scheduled[i].at > now) continue;
    ScheduledMessage m = s.scheduled[i];
    Settings updated = s;
    if (m.indefinitely) {
      LOGI("modes", "scheduled message \"%s\" - now the text-mode text", m.text.c_str());
      updated.scheduled.erase(updated.scheduled.begin() + i);
      updated.mode = "text";
      updated.text = m.text;
      settings::commit(updated);
      restart();
      return;
    }
    const SourceRule& rule = s.sources[SRC_SCHEDULED];
    bool late = now - m.at > 600;  // missed by more than 10 min (device was off): don't pop up now
    if (!late && rule.enabled) {
      showPopup(SRC_SCHEDULED, m.text, "", (m.showSeconds ? m.showSeconds : rule.showSeconds) * 1000UL, rule.priority);
    }
    ScheduledMessage& stored = updated.scheduled[i];
    if (m.repeatMinutes > 0 && m.repeatCount > 0) {
      while (stored.at <= now && stored.repeatCount > 0) {  // catch up past missed repeats
        stored.at += (int64_t)m.repeatMinutes * 60;
        stored.repeatCount--;
      }
      if (stored.at <= now) updated.scheduled.erase(updated.scheduled.begin() + i);
    } else {
      updated.scheduled.erase(updated.scheduled.begin() + i);
    }
    settings::commit(updated);
    return;  // one per tick
  }
}

// Timer pop-ups: "every N minutes" on clock-aligned slots (every 15 = :00 :15 :30 :45), inside the
// source's time window and weekdays. If something more important is showing, it keeps trying for
// up to 5 minutes into the slot.
static void checkTimers(const Settings& s) {
  struct tm t;
  if (!net::localTime(t)) return;
  int minute = t.tm_hour * 60 + t.tm_min;
  for (int i = 0; i < SRC_COUNT; i++) {
    const SourceRule& r = s.sources[i];
    if (!r.enabled || r.everyMinutes <= 0 || i == SRC_NOTIFY || i == SRC_SCHEDULED) continue;
    int slot = minute - (minute % r.everyMinutes);
    if (timerSlot[i] == slot || minute - slot > 5) continue;
    if (!popup::dayMatches(r.days, t.tm_wday) || !popup::windowMatches(r.from.c_str(), r.to.c_str(), minute)) {
      timerSlot[i] = slot;
      continue;
    }
    if (current.active && current.source == i) {
      timerSlot[i] = slot;  // already showing
      continue;
    }
    if (showPopup(i, "", SOURCE_IDS[i], r.showSeconds * 1000UL, r.priority)) timerSlot[i] = slot;
  }
}

// BART: pop up when the next train is exactly `trainMinutes` away (once per train)
static void checkTrains(const Settings& s) {
  const SourceRule& r = s.sources[SRC_BART];
  if (!r.enabled || r.trainMinutes <= 0) return;
  feeds::Bart b = feeds::bart();
  if (!b.valid) return;
  for (const feeds::Departure& d : b.departures) {
    if (d.minutes != r.trainMinutes) continue;
    String key = d.destination + "@" + String((long)(time(nullptr) / 60 + d.minutes));
    if (key == lastTrainKey) return;
    lastTrainKey = key;
    struct tm t;
    if (net::localTime(t) && (!popup::dayMatches(r.days, t.tm_wday) ||
                              !popup::windowMatches(r.from.c_str(), r.to.c_str(), t.tm_hour * 60 + t.tm_min))) {
      return;
    }
    showPopup(SRC_BART, "", "bart", r.showSeconds * 1000UL, r.priority);
    return;
  }
}

static void renderMode(const String& mode, const Settings& s) {
  if (mode == "text") display::show(s.text);
  else if (mode == "clock") runClock(s);
  else if (mode == "date") runDate(s);
  else if (mode == "countdown") runCountdown(s);
  else if (mode == "trainstation") runTrainStation(s);
  else if (mode == "randomphrase") runRandomPhrase(s);
  else if (mode == "famousnumbers") runFamousNumbers(s);
  else if (mode == "weather") runWeather(s);
  else if (mode == "bart") runBart(s);
  else if (mode == "printers") runPrinters(s);
}

// ---- Loop ----------------------------------------------------------------------------------
void begin() {
  for (int i = 0; i < SRC_COUNT; i++) timerSlot[i] = -1;
  lastPopupEnd = millis();
  restart();
}

void loop() {
  if (app::displayPaused || app::otaActive) return;
  if (millis() - lastTick < 250) return;
  lastTick = millis();

  const Settings& s = settings::current();

  // Quiet hours: freeze (or blank) the display
  bool quiet = quietNow(s);
  static bool quietKnown = false;  // first check after boot: no blanking, no morning re-home
  bool booting = !quietKnown && net::timeValid();
  if (booting) quietKnown = true;
  if (quiet != inQuiet) {
    inQuiet = quiet;
    LOGI("modes", quiet ? "quiet hours started (until %s)" : "quiet hours ended", s.quietTo.c_str());
    if (quiet) {
      current.active = false;
      // Only blank when quiet hours START while running - never because the ESP32 rebooted
      // (an update at night must not move the flaps)
      if (s.quietBlank && !booting) display::show("", true);
      if (s.quietPowerSave) {
        app::lowPower = true;
        net::setPowerSave(true);
        units::sleepAll();  // only 2.3+ units; the coils of all units are off when idle anyway
      }
    } else {
      if (app::lowPower) {
        app::lowPower = false;
        net::setPowerSave(false);
      }
      feeds::refreshWeather();  // the morning's data, ready before the first weather pop-up
      if (s.quietRehome) units::rehomeAll(unitproto::rpmFromSpeed(s.speed));  // morning check; wakes sleepers
      restart();
    }
  }
  if (inQuiet) return;

  checkScheduled();
  checkDoneRepeats(s);
  checkTimers(s);
  checkTrains(s);

  // A pop-up is up: show it until its time is over (and its last update has landed)
  if (current.active) {
    if (current.shownAt != 0 && millis() - current.shownAt >= current.durationMs && display::idle()) {
      LOGD("popup", "%s ended", SOURCE_IDS[current.source]);
      current.active = false;
      lastPopupEnd = millis();
      restart();
    } else {
      if (current.text.length()) display::show(current.text);
      else renderMode(current.mode, s);
      if (current.shownAt == 0 && display::idle()) current.shownAt = millis();  // landed (or nothing to change)
      return;
    }
  }

  // Base mode
  if (activeMode != s.mode) {
    LOGI("modes", "mode: %s", s.mode.c_str());
    activeMode = s.mode;
    resetRenderState();
    if (s.mode == "text") display::show(s.text, true);
  }
  renderMode(s.mode, s);
}

String status() {
  const Settings& s = settings::current();
  if (app::otaActive) return "OTA update";
  if (app::displayPaused) return "paused (console)";
  if (inQuiet) return "quiet hours until " + s.quietTo;
  if (current.active) {
    long left = current.shownAt ? (long)(current.durationMs - (millis() - current.shownAt)) / 1000
                                : (long)(current.durationMs / 1000);
    return String("pop-up: ") + SOURCE_IDS[current.source] + " (" +
           (current.text.length() ? current.text : current.mode) + ", " + String(left < 0 ? 0 : left) + "s left)";
  }
  return s.mode;
}

}  // namespace modes
