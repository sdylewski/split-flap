// Split-flap display - master controller (ESP32)
//
// Modules (each has a header with the details):
//   log        one logger -> serial, telnet, web
//   settings   settings.json on LittleFS
//   net        WiFi, mDNS (flappy.local), OTA, NTP
//   units      I2C driver for the flap units
//   display    text -> lines -> units (non-blocking)
//   modes      clock, date, countdown, ... + quiet hours + scheduled messages
//   console    serial / telnet / web console, commands.cpp + calibration.cpp
//   web        web UI + JSON API
//   led        status blink codes
#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "app.h"
#include "config.h"
#include "console.h"
#include "crash.h"
#include "display.h"
#include "feeds.h"
#include "led.h"
#include "log.h"
#include "modes.h"
#include "net.h"
#include "settings.h"
#include "units.h"
#include "web.h"

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "CRASH (panic) - see the backtrace before this boot";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "CRASH (watchdog)";
    case ESP_RST_BROWNOUT: return "BROWN-OUT - the 3.3/5 V supply dipped";
    default: return "other";
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  // Task watchdog 10 s (default 5 s): a slow but working moment (TLS maths, flash writes) must not reboot us
  esp_task_wdt_config_t watchdog = {.timeout_ms = 10000, .idle_core_mask = 1, .trigger_panic = true};
  esp_task_wdt_reconfigure(&watchdog);
  logx::begin();
  app::begin();
  led::begin();
  randomSeed(esp_random());
  LOGI("boot", "split-flap master %s (built %s %s), reset reason: %s", FIRMWARE_VERSION, __DATE__, __TIME__,
       resetReason());

  crash::begin();
  settings::begin();
  units::begin();
  display::begin();
  modes::begin();
  net::begin();
  console::begin();
  web::begin();
  feeds::begin();
  LOGI("boot", "ready - %d units configured, mode %s", units::count(), settings::current().mode.c_str());
}

// Slow housekeeping: status for the web UI, unit diagnostics, LED
static void housekeeping() {
  static uint32_t lastState = 0, lastPoll = 0, lastInfo = 0;
  uint32_t now = millis();

  // Keep unit status fresh while nothing is moving (display.cpp polls while it is).
  // Not in low-power mode: polling would wake units from deep sleep.
  if (display::idle() && !app::displayPaused && !app::lowPower && now - lastPoll > 2000) {
    lastPoll = now;
    units::pollAll(true);
  }

  // v2 units: diagnostics + event log every 15 s, only while the display is idle
  if (display::idle() && !app::lowPower && now - lastInfo > 15000) {
    lastInfo = now;
    for (int a = 0; a < units::count(); a++) {
      units::Unit& u = units::get(a);
      if (!u.present || !u.isV2()) continue;
      units::readInfo(a);
      units::pullEvents(a);
    }
  }

  if (display::idle() && !app::displayPaused && !app::otaActive && units::takeRedrawRequest()) {
    LOGI("display", "redrawing (a unit restarted or lost its letter)");
    display::redraw();
  }

  if (now - lastState > (app::lowPower ? 5000UL : 1000UL)) {
    lastState = now;
    app::publishState(web::buildState());
    if (!app::otaActive && net::connected() && net::timeValid()) {
      led::set(units::problemCount() > 0 ? led::UNIT_PROBLEM : led::RUNNING);
    }
  }
}

void loop() {
  app::services();   // OTA, consoles, LED heartbeat
  app::runPosted();  // work handed over by the web server
  console::loop();   // one console command, if one was typed
  modes::loop();     // what should be on the display
  display::loop();   // drive the units
  housekeeping();
  delay(2);
}
