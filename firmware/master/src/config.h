// Build-time configuration. Things you'd change at runtime (mode, text, time zone, quiet hours, ...)
// live in settings (web UI / settings.json), not here.
#pragma once

#define FIRMWARE_VERSION "4.0.0"

// ---- Hardware -----------------------------------------------------------------------------
// The ESP32 board is jumpered into the display's ESP-01 header U2:
//   U2 pin 8 (TX)  = I2C SDA -> GPIO21      U2 pin 4 (RX) = I2C SCL -> GPIO22      U2 pin 1 = GND
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22
#define I2C_CLOCK_HZ 50000      // slow and reliable on a long bus with 10+ units
#define I2C_TIMEOUT_MS 50

#define PIN_STATUS_LED 2        // onboard LED on most ESP32 dev boards (active HIGH)
#define STATUS_LED_ACTIVE_HIGH 1

#define MAX_UNITS 16            // I2C addresses 0..15 (DIP switches); the actual count is a setting

// ---- Network ------------------------------------------------------------------------------
#define HOSTNAME "flappy"       // http://flappy.local, telnet flappy.local, OTA "flappy"

// Fixed address. Set NET_STATIC_IP to 0 to use DHCP instead (flappy.local works either way).
#define NET_STATIC_IP 1
#define NET_IP 192, 168, 0, 111
#define NET_GATEWAY 192, 168, 0, 1
#define NET_SUBNET 255, 255, 255, 0
#define NET_DNS1 192, 168, 0, 1
#define NET_DNS2 8, 8, 8, 8

#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"

#define TELNET_PORT 23
#define SERIAL_BAUD 115200

// ---- Display timing -----------------------------------------------------------------------
#define DISPLAY_POLL_MS 200             // how often to ask units "are you done?" while moving
#define DISPLAY_FIRST_POLL_MS 300       // give units time to start moving before the first poll
#define DISPLAY_MOVE_TIMEOUT_MS 30000   // give up waiting for a stuck unit after this long
#define DISPLAY_WRITE_SPACING_MS 2200   // units ignore moves < 2 s apart: never send faster than this
#define SCHEDULED_SHOW_MS 7500          // how long a one-off scheduled message stays up

// WiFi credentials: secrets.h (git-ignored). Copy secrets.h.example to create it.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing firmware/master/src/secrets.h - copy secrets.h.example and fill in your WiFi details"
#endif
