// Pop-up arbitration rules (pure logic, no Arduino - tested on the host with `pio test -e native`).
//
//   high    interrupts anything that's showing (except another high pop-up)
//   normal  interrupts the base mode; ignored while another pop-up is showing
//   low     only when the base mode has been up for at least LOW_GAP_MS
// A pop-up that isn't admitted is dropped, not queued.
#pragma once
#include <stdint.h>

#include <string>

#include "text_layout.h"

namespace popup {

enum Priority : int { PRIO_LOW = 0, PRIO_NORMAL = 1, PRIO_HIGH = 2 };
constexpr uint32_t LOW_GAP_MS = 60000;

inline int parsePriority(const std::string& name) {
  if (name == "high") return PRIO_HIGH;
  if (name == "low") return PRIO_LOW;
  return PRIO_NORMAL;
}

inline const char* priorityName(int priority) {
  return priority >= PRIO_HIGH ? "high" : (priority <= PRIO_LOW ? "low" : "normal");
}

// May a new pop-up with `priority` take the display now?
inline bool admits(int priority, bool popupShowing, int showingPriority, uint32_t msSinceLastPopup) {
  if (popupShowing) return priority >= PRIO_HIGH && showingPriority < PRIO_HIGH;
  if (priority >= PRIO_NORMAL) return true;
  return msSinceLastPopup >= LOW_GAP_MS;
}

// days: bit 0 = Sunday .. bit 6 = Saturday; weekday: 0 = Sunday (struct tm tm_wday)
inline bool dayMatches(int days, int weekday) { return (days >> weekday) & 1; }

// Time window "HH:MM".."HH:MM"; empty (or equal) = all day. End is exclusive, may wrap midnight.
inline bool windowMatches(const std::string& from, const std::string& to, int nowMinutes) {
  if (from.empty() || to.empty()) return true;
  int f = layout::parseHHMM(from), t = layout::parseHHMM(to);
  if (f < 0 || t < 0 || f == t) return true;
  return layout::inWindow(nowMinutes, f, t);
}

}  // namespace popup
