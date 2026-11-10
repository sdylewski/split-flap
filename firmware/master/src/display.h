// What's on the flaps. Non-blocking: show() records a request, loop() drives the units.
//
//   request -> wait until units are idle -> write all units -> wait until they stop
//           -> (multi-line text) hold the line for settings.lineSeconds -> next line ...
#pragma once
#include <Arduino.h>

namespace display {

void begin();
void loop();

// Show text (wrapped into lines, aligned per settings). The same text again is ignored unless
// `force` is set. `alignment` overrides the setting ("left" | "center" | "right").
void show(const String& text, bool force = false, const char* alignment = nullptr);

// Re-send the current text (after the console paused the display, or a unit was reset).
void redraw();

bool idle();                   // nothing left to write or wait for
uint32_t idleSinceMs();        // millis() when the last text fully landed (0 while busy)
String text();                 // text requested last
String line();                 // the line currently on the flaps (exactly unitCount characters)
int lineIndex();
int lineCount();
uint32_t lastMoveMs();         // how long the last update took to settle

}  // namespace display
