#include "app.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <deque>

#include "console.h"
#include "led.h"
#include "net.h"

namespace app {

bool displayPaused = false;
bool otaActive = false;
bool lowPower = false;

static SemaphoreHandle_t lock = nullptr;
static std::deque<std::function<void()>> jobs;
static String state = "{}";

void begin() {
  if (!lock) lock = xSemaphoreCreateMutex();
}

void post(std::function<void()> job) {
  xSemaphoreTake(lock, portMAX_DELAY);
  if (jobs.size() < 32) jobs.push_back(std::move(job));
  xSemaphoreGive(lock);
}

void runPosted() {
  while (true) {
    std::function<void()> job;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!jobs.empty()) {
      job = std::move(jobs.front());
      jobs.pop_front();
    }
    xSemaphoreGive(lock);
    if (!job) return;
    job();
  }
}

void services() {
  led::alive();
  net::loop();
  console::poll();
}

void wait(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    services();
    delay(5);
  }
}

void publishState(const String& json) {
  xSemaphoreTake(lock, portMAX_DELAY);
  state = json;
  xSemaphoreGive(lock);
}

String stateJson() {
  xSemaphoreTake(lock, portMAX_DELAY);
  String copy = state;
  xSemaphoreGive(lock);
  return copy;
}

}  // namespace app
