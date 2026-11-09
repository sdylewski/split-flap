// Glue between the modules.
//
// Two tasks matter:
//   - the main loop (Arduino loopTask): owns settings, the display, the units, the console
//   - the web server task (AsyncTCP): must never touch those directly
// Web handlers call app::post(job) and the job runs on the main loop.
#pragma once
#include <Arduino.h>

#include <functional>

namespace app {

void begin();

// Queue work for the main loop. Safe from any task.
void post(std::function<void()> job);

// Run queued jobs. Called from loop() only (not from inside long console waits, so a job never
// interrupts a running console command half-way).
void runPosted();

// Keep the background services alive (OTA, consoles, LED heartbeat). Call this from any long
// wait on the main loop: display waits, console calibration, etc.
void services();

// Wait `ms` while running services().
void wait(uint32_t ms);

// True while the console is testing units ("pause"): modes don't touch the display.
extern bool displayPaused;
// True while an OTA update is being written.
extern bool otaActive;
// True during quiet hours with power saving on: slower housekeeping, no unit polling.
extern bool lowPower;

// Cached JSON for GET /api/state, rebuilt by the main loop (cheap for the web task to copy).
void publishState(const String& json);
String stateJson();

}  // namespace app
