// One logger for everything. Lines go into a ring buffer (thread-safe) and straight to USB serial.
// Consoles (telnet, web) and the web log view read the ring buffer by sequence number.
//
//   LOGI("wifi", "connected, ip %s", ip);   ->   "18:02:11.204 I wifi: connected, ip 192.168.0.111"
#pragma once
#include <Arduino.h>

namespace logx {

enum class Level : uint8_t { Debug = 0, Info = 1, Warn = 2, Error = 3, Off = 4 };

constexpr size_t TEXT_MAX = 160;
constexpr size_t RING_ENTRIES = 200;

struct Entry {
  uint32_t seq;
  uint32_t ms;     // millis() when logged
  time_t wall;     // wall clock (0 if time isn't set yet)
  uint16_t wallMs;
  Level level;
  char tag[12];
  char text[TEXT_MAX];
};

void begin();
void write(Level level, const char* tag, const char* format, ...) __attribute__((format(printf, 3, 4)));

// Format an entry as one line (no newline): "18:02:11.204 I wifi: text" (or uptime if no clock yet)
String format(const Entry& entry);

// Copy entries with seq > afterSeq (oldest first), at most `max`. Returns the last seq copied
// (or afterSeq if nothing new). Safe from any task.
uint32_t readSince(uint32_t afterSeq, size_t max, void (*fn)(const Entry&, void*), void* context);
uint32_t lastSeq();

Level serialLevel();
void setSerialLevel(Level level);
char levelChar(Level level);
bool parseLevel(const String& name, Level& out);  // "debug" | "info" | "warn" | "error" | "off"

}  // namespace logx

#define LOGD(tag, ...) logx::write(logx::Level::Debug, tag, __VA_ARGS__)
#define LOGI(tag, ...) logx::write(logx::Level::Info, tag, __VA_ARGS__)
#define LOGW(tag, ...) logx::write(logx::Level::Warn, tag, __VA_ARGS__)
#define LOGE(tag, ...) logx::write(logx::Level::Error, tag, __VA_ARGS__)
