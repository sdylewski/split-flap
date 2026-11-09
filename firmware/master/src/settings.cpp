#include "settings.h"

#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"
#include "log.h"
#include "text_layout.h"

namespace settings {

static const char* FILE_PATH = "/settings.json";
static const char* MODES[] = {"text", "clock", "date", "countdown", "trainstation", "randomphrase", "famousnumbers"};

static Settings live;
static Settings published;
static SemaphoreHandle_t lock = nullptr;

bool isValidMode(const String& mode) {
  for (const char* m : MODES) {
    if (mode == m) return true;
  }
  return false;
}

static void publish() {
  xSemaphoreTake(lock, portMAX_DELAY);
  published = live;
  xSemaphoreGive(lock);
}

Settings& current() { return live; }

Settings snapshot() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Settings copy = published;
  xSemaphoreGive(lock);
  return copy;
}

void toJson(const Settings& s, JsonObject o, bool includeScheduled) {
  o["mode"] = s.mode;
  o["text"] = s.text;
  o["alignment"] = s.alignment;
  o["speed"] = s.speed;
  o["unitCount"] = s.unitCount;
  o["lineSeconds"] = s.lineSeconds;
  o["timezone"] = s.timezone;
  o["timezoneName"] = s.timezoneName;
  o["clock24h"] = s.clock24h;
  o["dateFormat"] = s.dateFormat;
  o["countdownTo"] = s.countdownTo;
  o["trainType"] = s.trainType;
  o["trainLine"] = s.trainLine;
  o["trainSeconds"] = s.trainSeconds;
  o["trainStations"] = s.trainStations;
  o["phrases"] = s.phrases;
  o["phraseMinSeconds"] = s.phraseMinSeconds;
  o["phraseMaxSeconds"] = s.phraseMaxSeconds;
  o["numbers"] = s.numbers;
  o["numberMinSeconds"] = s.numberMinSeconds;
  o["numberMaxSeconds"] = s.numberMaxSeconds;
  o["numberNameSeconds"] = s.numberNameSeconds;
  o["quietEnabled"] = s.quietEnabled;
  o["quietFrom"] = s.quietFrom;
  o["quietTo"] = s.quietTo;
  o["quietBlank"] = s.quietBlank;
  o["logLevel"] = s.logLevel;
  if (includeScheduled) {
    JsonArray list = o["scheduled"].to<JsonArray>();
    for (const ScheduledMessage& m : s.scheduled) {
      JsonObject item = list.add<JsonObject>();
      item["text"] = m.text;
      item["at"] = m.at;
      item["indefinitely"] = m.indefinitely;
    }
  }
}

// ---- Validation helpers ------------------------------------------------------------------
static bool takeInt(JsonObjectConst p, const char* key, int& field, int min, int max, String& error) {
  JsonVariantConst v = p[key];
  if (v.isNull()) return true;
  if (!v.is<long>() && !v.is<double>()) {
    error = String(key) + " must be a number";
    return false;
  }
  long value = v.as<long>();
  if (value < min || value > max) {
    error = String(key) + " must be " + min + ".." + max;
    return false;
  }
  field = (int)value;
  return true;
}

static bool takeInt64(JsonObjectConst p, const char* key, int64_t& field, String& error) {
  JsonVariantConst v = p[key];
  if (v.isNull()) return true;
  if (!v.is<long long>() && !v.is<double>()) {
    error = String(key) + " must be a number";
    return false;
  }
  field = v.as<long long>();
  return true;
}

static bool takeBool(JsonObjectConst p, const char* key, bool& field, String& error) {
  JsonVariantConst v = p[key];
  if (v.isNull()) return true;
  if (!v.is<bool>()) {
    error = String(key) + " must be true or false";
    return false;
  }
  field = v.as<bool>();
  return true;
}

static bool takeString(JsonObjectConst p, const char* key, String& field, size_t maxLength, String& error) {
  JsonVariantConst v = p[key];
  if (v.isNull()) return true;
  if (!v.is<const char*>()) {
    error = String(key) + " must be text";
    return false;
  }
  String value = v.as<const char*>();
  if (value.length() > maxLength) {
    error = String(key) + " is too long (max " + maxLength + ")";
    return false;
  }
  field = value;
  return true;
}

static bool oneOf(const String& value, std::initializer_list<const char*> options) {
  for (const char* o : options) {
    if (value == o) return true;
  }
  return false;
}

bool merge(const Settings& base, JsonObjectConst p, Settings& s, String& error) {
  s = base;
  bool ok = takeString(p, "mode", s.mode, 20, error) && takeString(p, "text", s.text, 200, error) &&
            takeString(p, "alignment", s.alignment, 10, error) && takeInt(p, "speed", s.speed, 1, 100, error) &&
            takeInt(p, "unitCount", s.unitCount, 1, MAX_UNITS, error) &&
            takeInt(p, "lineSeconds", s.lineSeconds, 1, 60, error) &&
            takeString(p, "timezone", s.timezone, 60, error) && takeString(p, "timezoneName", s.timezoneName, 60, error) &&
            takeBool(p, "clock24h", s.clock24h, error) && takeString(p, "dateFormat", s.dateFormat, 30, error) &&
            takeInt64(p, "countdownTo", s.countdownTo, error) && takeString(p, "trainType", s.trainType, 10, error) &&
            takeString(p, "trainLine", s.trainLine, 30, error) &&
            takeInt(p, "trainSeconds", s.trainSeconds, 5, 3600, error) &&
            takeString(p, "trainStations", s.trainStations, 1500, error) &&
            takeString(p, "phrases", s.phrases, 3000, error) &&
            takeInt(p, "phraseMinSeconds", s.phraseMinSeconds, 5, 3600, error) &&
            takeInt(p, "phraseMaxSeconds", s.phraseMaxSeconds, 5, 3600, error) &&
            takeString(p, "numbers", s.numbers, 3000, error) &&
            takeInt(p, "numberMinSeconds", s.numberMinSeconds, 5, 3600, error) &&
            takeInt(p, "numberMaxSeconds", s.numberMaxSeconds, 5, 3600, error) &&
            takeInt(p, "numberNameSeconds", s.numberNameSeconds, 1, 60, error) &&
            takeBool(p, "quietEnabled", s.quietEnabled, error) && takeString(p, "quietFrom", s.quietFrom, 5, error) &&
            takeString(p, "quietTo", s.quietTo, 5, error) && takeBool(p, "quietBlank", s.quietBlank, error) &&
            takeString(p, "logLevel", s.logLevel, 10, error);
  if (!ok) return false;

  if (!isValidMode(s.mode)) return error = "unknown mode '" + s.mode + "'", false;
  if (!oneOf(s.alignment, {"left", "center", "right"})) return error = "alignment must be left, center or right", false;
  if (!oneOf(s.trainType, {"random", "line", "bart"})) return error = "trainType must be random, line or bart", false;
  if (layout::parseHHMM(s.quietFrom.c_str()) < 0) return error = "quietFrom must be HH:MM", false;
  if (layout::parseHHMM(s.quietTo.c_str()) < 0) return error = "quietTo must be HH:MM", false;
  if (s.timezone.length() < 3) return error = "timezone must be a POSIX TZ string like PST8PDT,M3.2.0,M11.1.0", false;
  logx::Level level;
  if (!logx::parseLevel(s.logLevel, level)) return error = "logLevel must be debug, info, warn, error or off", false;
  if (s.phraseMaxSeconds < s.phraseMinSeconds) s.phraseMaxSeconds = s.phraseMinSeconds;
  if (s.numberMaxSeconds < s.numberMinSeconds) s.numberMaxSeconds = s.numberMinSeconds;

  JsonVariantConst list = p["scheduled"];
  if (!list.isNull()) {
    if (!list.is<JsonArrayConst>()) return error = "scheduled must be a list", false;
    s.scheduled.clear();
    for (JsonObjectConst item : list.as<JsonArrayConst>()) {
      ScheduledMessage m;
      m.text = item["text"] | "";
      m.at = item["at"] | (long long)0;
      m.indefinitely = item["indefinitely"] | false;
      if (m.at > 0 && m.text.length() <= 200) s.scheduled.push_back(m);
    }
  }
  return true;
}

bool addScheduled(Settings& s, const ScheduledMessage& message, String& error) {
  if (message.text.length() == 0 || message.text.length() > 200) return error = "text must be 1..200 characters", false;
  if (message.at < 1700000000) return error = "at must be a unix time", false;
  if (s.scheduled.size() >= 50) return error = "too many scheduled messages (50)", false;
  removeScheduled(s, message.at);  // same time = replace
  s.scheduled.push_back(message);
  return true;
}

bool removeScheduled(Settings& s, int64_t at) {
  for (size_t i = 0; i < s.scheduled.size(); i++) {
    if (s.scheduled[i].at == at) {
      s.scheduled.erase(s.scheduled.begin() + i);
      return true;
    }
  }
  return false;
}

// ---- Storage -----------------------------------------------------------------------------
bool save() {
  JsonDocument doc;
  toJson(live, doc.to<JsonObject>(), true);
  File file = LittleFS.open("/settings.tmp", "w");
  if (!file) {
    LOGE("settings", "can't write /settings.tmp");
    return false;
  }
  size_t written = serializeJson(doc, file);
  file.close();
  if (written == 0) {
    LOGE("settings", "writing settings failed");
    return false;
  }
  LittleFS.remove(FILE_PATH);
  LittleFS.rename("/settings.tmp", FILE_PATH);  // a power cut mid-write can't corrupt the real file
  LOGD("settings", "saved (%u bytes)", (unsigned)written);
  return true;
}

void commit(const Settings& updated) {
  live = updated;
  save();
  publish();
  logx::Level level;
  if (logx::parseLevel(live.logLevel, level)) logx::setSerialLevel(level);
}

void begin() {
  if (!lock) lock = xSemaphoreCreateMutex();
  if (!LittleFS.begin(true)) {  // true = format if it can't be mounted
    LOGE("settings", "LittleFS mount failed - running with defaults");
    publish();
    return;
  }
  LOGI("settings", "LittleFS: %u of %u KB used", (unsigned)(LittleFS.usedBytes() / 1024),
       (unsigned)(LittleFS.totalBytes() / 1024));

  File file = LittleFS.open(FILE_PATH, "r");
  if (!file) {
    LOGW("settings", "%s not found - using defaults", FILE_PATH);
    save();
  } else {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    String error;
    Settings loaded;
    if (err) {
      LOGE("settings", "%s is corrupt (%s) - using defaults", FILE_PATH, err.c_str());
    } else if (!merge(Settings(), doc.as<JsonObjectConst>(), loaded, error)) {
      LOGE("settings", "invalid value in %s: %s - using defaults", FILE_PATH, error.c_str());
    } else {
      live = loaded;
      LOGI("settings", "loaded: mode %s, %d units, %u scheduled", live.mode.c_str(), live.unitCount,
           (unsigned)live.scheduled.size());
    }
  }
  logx::Level level;
  if (logx::parseLevel(live.logLevel, level)) logx::setSerialLevel(level);
  publish();
}

}  // namespace settings
