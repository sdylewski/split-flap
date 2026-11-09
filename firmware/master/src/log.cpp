#include "log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

namespace logx {

static Entry ring[RING_ENTRIES];
static uint32_t nextSeq = 1;
static SemaphoreHandle_t lock = nullptr;
static Level serialMin = Level::Info;

void begin() {
  if (!lock) lock = xSemaphoreCreateMutex();
}

char levelChar(Level level) {
  switch (level) {
    case Level::Debug: return 'D';
    case Level::Info: return 'I';
    case Level::Warn: return 'W';
    case Level::Error: return 'E';
    default: return '?';
  }
}

bool parseLevel(const String& name, Level& out) {
  String n = name;
  n.toLowerCase();
  if (n == "debug" || n == "d") out = Level::Debug;
  else if (n == "info" || n == "i" || n == "on") out = Level::Info;
  else if (n == "warn" || n == "w") out = Level::Warn;
  else if (n == "error" || n == "e") out = Level::Error;
  else if (n == "off") out = Level::Off;
  else return false;
  return true;
}

String format(const Entry& e) {
  char head[32];
  if (e.wall > 1700000000) {
    struct tm t;
    localtime_r(&e.wall, &t);
    snprintf(head, sizeof(head), "%02d:%02d:%02d.%03u", t.tm_hour, t.tm_min, t.tm_sec, (unsigned)e.wallMs);
  } else {
    snprintf(head, sizeof(head), "+%lu.%03u", (unsigned long)(e.ms / 1000), (unsigned)(e.ms % 1000));
  }
  String line;
  line.reserve(strlen(head) + strlen(e.tag) + strlen(e.text) + 6);
  line += head;
  line += ' ';
  line += levelChar(e.level);
  line += ' ';
  line += e.tag;
  line += ": ";
  line += e.text;
  return line;
}

void write(Level level, const char* tag, const char* fmt, ...) {
  if (!lock) begin();
  Entry entry;
  entry.ms = millis();
  struct timeval now;
  gettimeofday(&now, nullptr);
  entry.wall = now.tv_sec;
  entry.wallMs = now.tv_usec / 1000;
  entry.level = level;
  strlcpy(entry.tag, tag, sizeof(entry.tag));
  va_list args;
  va_start(args, fmt);
  vsnprintf(entry.text, sizeof(entry.text), fmt, args);
  va_end(args);

  xSemaphoreTake(lock, portMAX_DELAY);
  entry.seq = nextSeq++;
  ring[entry.seq % RING_ENTRIES] = entry;
  xSemaphoreGive(lock);

  if (level >= serialMin) {
    Serial.println(format(entry));
  }
}

uint32_t lastSeq() {
  if (!lock) return 0;
  xSemaphoreTake(lock, portMAX_DELAY);
  uint32_t seq = nextSeq - 1;
  xSemaphoreGive(lock);
  return seq;
}

uint32_t readSince(uint32_t afterSeq, size_t max, void (*fn)(const Entry&, void*), void* context) {
  if (!lock) return afterSeq;
  uint32_t last = lastSeq();
  if (afterSeq >= last) return afterSeq;
  uint32_t first = afterSeq + 1;
  if (last - first + 1 > RING_ENTRIES) first = last - RING_ENTRIES + 1;  // the oldest ones were overwritten
  uint32_t copied = afterSeq;
  for (uint32_t seq = first; seq <= last && max > 0; seq++, max--) {
    Entry entry;
    xSemaphoreTake(lock, portMAX_DELAY);
    entry = ring[seq % RING_ENTRIES];
    xSemaphoreGive(lock);
    if (entry.seq != seq) continue;  // overwritten while we were reading
    fn(entry, context);
    copied = seq;
  }
  return copied;
}

Level serialLevel() { return serialMin; }
void setSerialLevel(Level level) { serialMin = level; }

}  // namespace logx
