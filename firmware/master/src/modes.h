// Display modes (text, clock, date, countdown, train station, random phrase, famous numbers)
// plus the two overlays that can take over: quiet hours and scheduled messages.
#pragma once
#include <Arduino.h>

namespace modes {

void begin();
void loop();        // main loop: decides what to show, display:: does the showing
void restart();     // settings changed: start the current mode from scratch
String status();    // human readable: "clock", "quiet hours until 08:00", ...

// Pop-ups on top of the base mode (rules: popup_rules.h, settings: Settings::sources).
// notify(): messages from Home Assistant / Siri / the API / the console. durationMs 0 and
// priority -1 = the "notify" source's defaults. Returns false if it wasn't shown.
bool notify(const String& text, uint32_t durationMs = 0, int priority = -1);
// A print finished / failed ("PRINTY DONE")
void printerEvent(const String& text);
// "PRINTY 40" progress update: own duration and priority, but follows the printers source's on switch
void printerProgress(const String& text, uint32_t durationMs, int priority);

}  // namespace modes
