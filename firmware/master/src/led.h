// Status LED blink codes (GPIO2). Driven by a hardware timer, so it keeps blinking even if the
// main loop is stuck - the pattern then tells you where.
//
//   1 blink, pause        connecting to WiFi
//   2 blinks, pause       WiFi OK, waiting for the time (NTP)
//   3 blinks, pause       running, but a unit is missing or reports an error (see 'units')
//   short blip every 3 s  running normally
//   fast flicker          WiFi lost
//   1 s on / 1 s off      main loop stuck for more than 5 s
//   solid on              OTA update in progress
#pragma once
#include <stdint.h>

namespace led {

enum Code : uint8_t { OFF = 0, CONNECTING = 1, NO_TIME = 2, UNIT_PROBLEM = 3, RUNNING = 20, WIFI_LOST = 21, OTA = 22 };

void begin();
void set(Code code);
Code get();
void alive();  // called from the main loop; if it stops for > 5 s the LED shows "stuck"

}  // namespace led
