#include "led.h"

#include <Arduino.h>
#include <Ticker.h>

#include "config.h"

namespace led {

static Ticker ticker;
static volatile Code code = OFF;
static volatile uint32_t lastAlive = 0;
static volatile uint16_t tick = 0;

static void write(bool on) {
  digitalWrite(PIN_STATUS_LED, (on == (STATUS_LED_ACTIVE_HIGH == 1)) ? HIGH : LOW);
}

static void onTick() {  // every 100 ms, timer task
  uint16_t t = tick++;
  Code c = code;
  if (c != OTA && c != OFF && millis() - lastAlive > 5000) {
    write((t / 10) % 2 == 0);  // stuck
    return;
  }
  switch (c) {
    case OFF: write(false); break;
    case RUNNING: write(t % 30 == 0); break;
    case WIFI_LOST: write(t % 2 == 0); break;
    case OTA: write(true); break;
    default: {  // N blinks (200 ms on, 300 ms off), then a 1.5 s pause
      uint16_t period = c * 5 + 15;
      uint16_t p = t % period;
      write(p < c * 5 && (p % 5) < 2);
    }
  }
}

void begin() {
  pinMode(PIN_STATUS_LED, OUTPUT);
  write(false);
  lastAlive = millis();
  ticker.attach_ms(100, onTick);
}

void set(Code c) {
  if (code != c) {
    code = c;
    tick = 0;
  }
}

Code get() { return code; }

void alive() { lastAlive = millis(); }

}  // namespace led
