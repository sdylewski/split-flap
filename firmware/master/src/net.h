// WiFi (with retries - needed on WPA2/WPA3 mesh networks), mDNS (flappy.local), OTA and NTP time.
#pragma once
#include <Arduino.h>
#include <time.h>

namespace net {

void begin();   // start WiFi (non-blocking)
void loop();    // reconnects, starts mDNS/OTA once connected, runs OTA

bool connected();
bool timeValid();               // NTP has set the clock
bool localTime(struct tm& out); // false if the time isn't known yet
void setTimezone(const String& posixTz);
// Night: WiFi modem sleep + 80 MHz CPU (slower replies, much less power). Day: full speed.
void setPowerSave(bool on);

// Diagnostics
int rssi();
String ip();
uint32_t reconnects();          // times the connection dropped since boot
int lastDisconnectReason();     // ESP-IDF wifi_err_reason_t, 0 = none
const char* reasonName(int reason);
uint32_t connectedSinceMs();    // millis() when the current connection came up

}  // namespace net
