// Status LED blink codes on the ESP-01S blue LED (GPIO 2, active LOW).
//
// The LED is driven from a hardware timer (Ticker), NOT from loop(), so it keeps blinking
// even if the code is stuck somewhere. If startup hangs, the LED keeps repeating the
// blink count of the step it is stuck in.
//
//   Pattern (repeats)                    Meaning
//   1 blink, pause                       Connecting to WiFi
//   2 blinks, pause                      WiFi OK, syncing time (NTP) / looking up time zone
//   3 blinks, pause                      Mounting file system, loading settings
//   4 blinks, pause                      Starting OTA, mDNS (flappy.local) and telnet console
//   5 blinks, pause                      Setting up the web server
//   short blip every 3 s                 Running normally (heartbeat)
//   fast flicker (5x per second)         WiFi failed / offline
//   slow 1 s on / 1 s off                Running, but the main loop has stalled for > 5 s
//   solid on                             OTA update in progress
//
// See docs/software.md ("Status LED") for the same table.

//LED_CODE_* values are defined in ESPMaster.ino (needed before setup())

#define LED_TICK_MS         100
#define LED_STALL_MS        5000

#if ESP01S_LED_ENABLE == true
#include <Ticker.h>

Ticker statusLedTicker;
volatile uint8_t statusLedCode = LED_CODE_OFF;
volatile uint32_t statusLedLastAlive = 0;
volatile uint16_t statusLedTick = 0;

static inline void statusLedWrite(bool on) {
  digitalWrite(LED_DEBUG_PIN, on ? LED_ON : LED_OFF);
}

// Runs every LED_TICK_MS from the system timer
void statusLedOnTick() {
  uint16_t t = statusLedTick++;
  uint8_t code = statusLedCode;

  if (code == LED_CODE_OFF) {
    statusLedWrite(false);
  }
  else if (code < LED_CODE_RUNNING) {
    //N blinks (200 ms on, 300 ms off), then a 1.5 s pause
    uint16_t period = code * 5 + 15;
    uint16_t p = t % period;
    statusLedWrite(p < code * 5 && (p % 5) < 2);
  }
  else if (code == LED_CODE_RUNNING) {
    if (millis() - statusLedLastAlive > LED_STALL_MS) {
      statusLedWrite((t / 10) % 2 == 0);   //stalled: 1 s on / 1 s off
    } else {
      statusLedWrite(t % 30 == 0);         //heartbeat blip every 3 s
    }
  }
  else if (code == LED_CODE_WIFI_FAIL) {
    statusLedWrite(t % 2 == 0);
  }
  else if (code == LED_CODE_OTA) {
    statusLedWrite(true);
  }
}
#endif

void statusLedBegin() {
#if ESP01S_LED_ENABLE == true
  pinMode(LED_DEBUG_PIN, OUTPUT);
  statusLedWrite(false);
  statusLedLastAlive = millis();
  statusLedTicker.attach_ms(LED_TICK_MS, statusLedOnTick);
#endif
}

void statusLedSet(uint8_t code) {
#if ESP01S_LED_ENABLE == true
  if (statusLedCode != code) {
    statusLedCode = code;
    statusLedTick = 0; //start the new pattern from the beginning
  }
  statusLedLastAlive = millis();
#endif
}

// Call from loop() / long waits: proves the main code is still running
void statusLedAlive() {
#if ESP01S_LED_ENABLE == true
  statusLedLastAlive = millis();
#endif
}
