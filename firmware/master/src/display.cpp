#include "display.h"

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

void redraw() { show(requested, true, requestedAlign.c_str()); }

static bool pollDue() {
  if (millis() - lastPoll < DISPLAY_POLL_MS) return false;
  lastPoll = millis();
  return true;
}

static void writeLine() {
  int n = units::count();
  std::string text = layout::align(lines[index], n, layout::parseAlign(requestedAlign.c_str()));
  int replaced = layout::sanitize(text);
  if (replaced > 0) LOGW("display", "%d character(s) not on the flaps - shown as blank", replaced);

  uint8_t rpm = unitproto::rpmFromSpeed(settings::current().speed);
  int failed = 0;
  for (int a = 0; a < n; a++) {
    if (!units::writeLetter(a, unitproto::letterIndex(text[a]), rpm)) failed++;
  }
  lastWrite = millis();
  onFlaps = text.c_str();
  if (lines.size() > 1) {
    LOGD("display", "line %u/%u \"%s\"", (unsigned)index + 1, (unsigned)lines.size(), text.c_str());
  }
  if (failed > 0) LOGW("display", "%d unit(s) did not take the update", failed);
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
      enter(State::Write);
      break;

    case State::Write:
      writeLine();
      enter(State::WaitStart);
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
String text() { return requested; }
String line() { return onFlaps; }
int lineIndex() { return (int)index; }
int lineCount() { return (int)lines.size(); }
uint32_t lastMoveMs() { return settleMs; }

}  // namespace display
