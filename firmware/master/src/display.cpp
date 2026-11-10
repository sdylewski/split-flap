#include "display.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "app.h"
#include "config.h"
#include "log.h"
#include "settings.h"
#include "text_layout.h"
#include "units.h"

namespace display {

enum class State { Idle, WaitBefore, Write, WaitStart, WaitMoving, Hold };

static State state = State::Idle;
static uint32_t idleAt = 1;  // millis() when the display last became idle (0 = busy)
static String requested;
static String requestedAlign;
static std::vector<std::string> lines;
static size_t index = 0;
static String onFlaps;
static uint32_t stateSince = 0;
static uint32_t lastPoll = 0;
static uint32_t lastWrite = 0;
static uint32_t settleMs = 0;

static void enter(State next) {
  state = next;
  stateSince = millis();
  lastPoll = 0;
}

void begin() {
  idleAt = millis() | 1;
  int n = units::count();
  onFlaps = String(std::string(n, ' ').c_str());
}

void show(const String& text, bool force, const char* alignment) {
  String align = alignment ? String(alignment) : settings::current().alignment;
  if (!force && text == requested && align == requestedAlign) return;
  requested = text;
  requestedAlign = align;
  lines = layout::splitLines(text.c_str(), units::count());
  index = 0;
  LOGI("display", "show \"%s\" (%u line%s, %s)", text.c_str(), (unsigned)lines.size(), lines.size() == 1 ? "" : "s",
       align.c_str());
  enter(State::WaitBefore);
}

// Only if something was shown since boot: right after a reboot during quiet hours nothing has been
// requested yet, and "redrawing" would blank the display.
void redraw() {
  if (lines.empty()) return;
  show(requested, true, requestedAlign.c_str());
}

static bool pollDue() {
  if (millis() - lastPoll < DISPLAY_POLL_MS) return false;
  lastPoll = millis();
  return true;
}

// Writing a line. Every unit gets a start time (ms after the line begins):
//   off        all at 0
//   start      k * waveMs                         (units start one after another)
//   arrive     units LAND waveMs apart: start = arrival - travel time, so a unit with a long way
//              to go starts early and a unit with a short hop starts late
//   sequential each unit starts waveMs after the previous one landed
// Travel time = flaps to go x time per flap. The drum only turns forward, so going from 'Z' to
// 'A' is most of a turn. One flap takes 60 s / (45 flaps x rpm).
struct Start {
  int unit;
  uint32_t at;
};
static std::string writeText;
static std::vector<Start> writePlan;
static size_t writeNext = 0;
static int writeFailed = 0;
static uint8_t writeRpm = 0;

static uint32_t travelMs(int address, int toLetter, uint8_t rpm) {
  const units::Unit& u = units::get(address);
  int from = u.lastLetter >= 0 ? u.lastLetter : (u.infoValid ? u.info.letter : -1);
  int flaps = from < 0 ? unitproto::FLAP_COUNT / 2 : (toLetter - from + unitproto::FLAP_COUNT) % unitproto::FLAP_COUNT;
  return (uint32_t)flaps * 60000UL / ((uint32_t)unitproto::FLAP_COUNT * (rpm ? rpm : 1));
}

static void prepareWrite() {
  const Settings& s = settings::current();
  int n = units::count();
  writeText = layout::align(lines[index], n, layout::parseAlign(requestedAlign.c_str()));
  int replaced = layout::sanitize(writeText);
  if (replaced > 0) LOGW("display", "%d character(s) not on the flaps - shown as blank", replaced);
  writeRpm = unitproto::rpmFromSpeed(s.speed);
  writeNext = 0;
  writeFailed = 0;

  // Order the units go in
  std::vector<int> order;
  for (int a = 0; a < n; a++) order.push_back(a);
  if (s.waveOrder == "right") {
    std::reverse(order.begin(), order.end());
  } else if (s.waveOrder == "center") {  // from the middle outwards
    std::stable_sort(order.begin(), order.end(),
                     [n](int x, int y) { return std::abs(2 * x - (n - 1)) < std::abs(2 * y - (n - 1)); });
  } else if (s.waveOrder == "random") {
    for (int i = n - 1; i > 0; i--) std::swap(order[i], order[random(0, i + 1)]);
  }

  // Start times
  writePlan.clear();
  const uint32_t gap = (uint32_t)s.waveMs;
  if (s.waveStyle == "arrive") {
    std::vector<int32_t> starts;
    int32_t earliest = 0;
    for (size_t k = 0; k < order.size(); k++) {
      int u = order[k];
      int32_t start = (int32_t)(k * gap) - (int32_t)travelMs(u, unitproto::letterIndex(writeText[u]), writeRpm);
      starts.push_back(start);
      if (start < earliest) earliest = start;
    }
    for (size_t k = 0; k < order.size(); k++) writePlan.push_back({order[k], (uint32_t)(starts[k] - earliest)});
  } else if (s.waveStyle == "sequential") {
    uint32_t t = 0;
    for (int u : order) {
      writePlan.push_back({u, t});
      uint32_t travel = travelMs(u, unitproto::letterIndex(writeText[u]), writeRpm);
      if (travel > 0) t += travel + gap;  // units that don't move don't take a turn
    }
  } else if (s.waveStyle == "start") {
    for (size_t k = 0; k < order.size(); k++) writePlan.push_back({order[k], (uint32_t)(k * gap)});
  } else {
    for (int u : order) writePlan.push_back({u, 0});
  }
  std::stable_sort(writePlan.begin(), writePlan.end(), [](const Start& x, const Start& y) { return x.at < y.at; });

  if (lines.size() > 1) {
    LOGD("display", "line %u/%u \"%s\"", (unsigned)index + 1, (unsigned)lines.size(), writeText.c_str());
  }
  if (s.waveStyle != "off") {
    LOGD("display", "wave %s, %s, %d ms: last unit starts at +%lu ms", s.waveStyle.c_str(), s.waveOrder.c_str(),
         s.waveMs, (unsigned long)writePlan.back().at);
  }
}

// Returns true when every unit has been written
static bool writeStep(uint32_t sinceStart) {
  while (writeNext < writePlan.size() && sinceStart >= writePlan[writeNext].at) {
    int a = writePlan[writeNext++].unit;
    if (!units::writeLetter(a, unitproto::letterIndex(writeText[a]), writeRpm)) writeFailed++;
  }
  if (writeNext < writePlan.size()) return false;
  lastWrite = millis();
  onFlaps = writeText.c_str();
  if (writeFailed > 0) LOGW("display", "%d unit(s) did not take the update", writeFailed);
  return true;
}

void loop() {
  if (app::displayPaused || app::otaActive) return;

  switch (state) {
    case State::Idle:
      break;

    case State::WaitBefore:  // let a previous update finish first
      if (!pollDue()) break;
      if (units::pollAll() && millis() - stateSince < DISPLAY_MOVE_TIMEOUT_MS) break;
      if (millis() - lastWrite < DISPLAY_WRITE_SPACING_MS) break;  // units ignore moves < 2 s apart
      prepareWrite();
      enter(State::Write);
      break;

    case State::Write:
      if (writeStep(millis() - stateSince)) enter(State::WaitStart);
      break;

    case State::WaitStart:  // give the units a moment to start before asking if they're done
      if (millis() - stateSince >= DISPLAY_FIRST_POLL_MS) enter(State::WaitMoving);
      break;

    case State::WaitMoving: {
      if (!pollDue()) break;
      bool busy = units::pollAll();
      uint32_t elapsed = millis() - lastWrite;
      if (busy && elapsed < DISPLAY_MOVE_TIMEOUT_MS) break;
      if (busy) {
        String stuck;
        for (int a = 0; a < units::count(); a++) {
          if (unitproto::isBusy(units::get(a).status)) stuck += " " + String(a);
        }
        LOGW("display", "units still moving after %lus:%s - continuing", (unsigned long)(elapsed / 1000),
             stuck.c_str());
      }
      settleMs = elapsed;
      LOGD("display", "settled in %lu ms", (unsigned long)elapsed);
      if (index + 1 < lines.size()) {
        enter(State::Hold);
      } else {
        enter(State::Idle);
        idleAt = millis() | 1;  // never 0: 0 means busy
      }
      break;
    }

    case State::Hold:
      if (millis() - stateSince >= (uint32_t)settings::current().lineSeconds * 1000) {
        index++;
        enter(State::WaitBefore);
      }
      break;
  }
}

bool idle() { return state == State::Idle; }
uint32_t idleSinceMs() { return state == State::Idle ? idleAt : 0; }
String text() { return requested; }
String line() { return onFlaps; }
int lineIndex() { return (int)index; }
int lineCount() { return (int)lines.size(); }
uint32_t lastMoveMs() { return settleMs; }

}  // namespace display
