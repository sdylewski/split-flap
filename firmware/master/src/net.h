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
// Apply the power settings (WiFi sleep, CPU speed, transmit power) plus quiet-hours power saving
// (app::lowPower). Call after the settings or app::lowPower change; net::loop() also keeps it current
// (automatic transmit power, the fall-back after repeated drops).
void applyPower();

struct PowerStatus {
  int cpuMhz;
  const char* wifiSleep;     // what the radio is really doing: "off" | "min" | "max"
  float txDbm;               // transmit power in use
  bool fallback;             // power saving suspended because WiFi kept dropping
  int fallbackMinutesLeft;
  int meshNodes;             // access points with our network name in the last scan (0 = no scan yet)
  String bssid;              // access point we're connected to
  int channel;
  float chipTempC;           // NAN if the chip has no sensor
};
PowerStatus powerStatus();

// Diagnostics
int rssi();
String ip();
uint32_t reconnects();          // times the connection dropped since boot
int lastDisconnectReason();     // ESP-IDF wifi_err_reason_t, 0 = none
const char* reasonName(int reason);
uint32_t connectedSinceMs();    // millis() when the current connection came up

}  // namespace net
