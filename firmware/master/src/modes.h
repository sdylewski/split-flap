// Display modes (text, clock, date, countdown, train station, random phrase, famous numbers)
// plus the two overlays that can take over: quiet hours and scheduled messages.
#pragma once
#include <Arduino.h>

namespace modes {

void begin();
void loop();        // main loop: decides what to show, display:: does the showing
void restart();     // settings changed: start the current mode from scratch
String status();    // human readable: "clock", "quiet hours until 08:00", ...

// Show a message for a while, then go back to the current mode (printer done, Home Assistant,
// Siri, ...). Ignored during quiet hours and while the console has the display paused.
void notify(const String& text, uint32_t durationMs);

}  // namespace modes
