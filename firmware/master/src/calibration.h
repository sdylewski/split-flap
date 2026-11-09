// Unit calibration from the console (works over serial, telnet and the web console).
#pragma once
#include <Arduino.h>

namespace calibration {

// Interactive: home to the magnet, creep forward, the user presses Enter whenever a flap drops.
// A straight-line fit of the drop positions gives the offset. Needs unit firmware 1.3.0+.
void interactive(int address, int flips);

// Store an offset and re-home the unit with it. Works with every unit firmware.
bool applyOffset(int address, uint16_t offset);

// Move to the magnet edge without the offset (sensor check). Needs 1.3.0+.
void home(int address);

// Wait until the unit is ready (or reports an error). Returns its last status, -1 if it never answered.
int waitReady(int address, uint32_t timeoutMs);

}  // namespace calibration
