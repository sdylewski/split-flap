#include "settings.h"

#include <LittleFS.h>
#include <Preferences.h>

#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"
#include "log.h"
#include "popup_rules.h"
#include "text_layout.h"

Settings::Settings() {
  // Event pop-ups on by default (they only fire when something happens), timers off
  sources[SRC_PRINTERS] = {true, 2, 300, 0, "", "", 0x7F, 0};
  sources[SRC_NOTIFY] = {true, 2, 60, 0, "", "", 0x7F, 0};
  sources[SRC_SCHEDULED] = {true, 1, 8, 0, "", "", 0x7F, 0};
}

namespace settings {

static const char* FILE_PATH = "/settings.json";
static const char* MODES[] = {"text",          "clock",   "date", "countdown", "trainstation", "randomphrase",
                              "famousnumbers", "weather", "bart", "printers"};

static Settings live;
static Settings published;
static SemaphoreHandle_t lock = nullptr;
static volatile uint32_t rev = 1;

bool isValidMode(const String& mode) {
  for (const char* m : MODES) {
    if (mode == m) return true;
  }
  return false;
}

static void publish() {
  xSemaphoreTake(lock, portMAX_DELAY);
  published = live;
  rev++;
  xSemaphoreGive(lock);
}

uint32_t revision() { return rev; }

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
  o["clockHours"] = s.clockHours;
  o["clockDay"] = s.clockDay;
  o["clockLeadingZero"] = s.clockLeadingZero;
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
  o["numberList"] = s.numberList;
  JsonObject lists = o["numberLists"].to<JsonObject>();
  for (const auto& list : s.numberLists) lists[list.first] = list.second;
  o["numberMinSeconds"] = s.numberMinSeconds;
  o["numberMaxSeconds"] = s.numberMaxSeconds;
  o["numberNameSeconds"] = s.numberNameSeconds;
  o["quietEnabled"] = s.quietEnabled;
  o["quietFrom"] = s.quietFrom;
  o["quietTo"] = s.quietTo;
  o["quietBlank"] = s.quietBlank;
  o["logLevel"] = s.logLevel;
  o["waveStyle"] = s.waveStyle;
  o["waveMs"] = s.waveMs;
  o["quietPowerSave"] = s.quietPowerSave;
  o["quietRehome"] = s.quietRehome;
  o["waveOrder"] = s.waveOrder;
  o["weatherLat"] = s.weatherLat;
  o["weatherLon"] = s.weatherLon;
  o["weatherPlace"] = s.weatherPlace;
  o["weatherFahrenheit"] = s.weatherFahrenheit;
  o["weatherSeconds"] = s.weatherSeconds;
  o["bartStation"] = s.bartStation;
  o["bartDirection"] = s.bartDirection;
  o["bartKey"] = s.bartKey;
  o["bartSeconds"] = s.bartSeconds;
  JsonObject sources = o["sources"].to<JsonObject>();
  for (int i = 0; i < SRC_COUNT; i++) {
    const SourceRule& r = s.sources[i];
    JsonObject item = sources[SOURCE_IDS[i]].to<JsonObject>();
    item["enabled"] = r.enabled;
    item["priority"] = popup::priorityName(r.priority);
    item["showSeconds"] = r.showSeconds;
    item["everyMinutes"] = r.everyMinutes;
    item["from"] = r.from;
    item["to"] = r.to;
    item["days"] = r.days;
    if (i == SRC_BART) item["trainMinutes"] = r.trainMinutes;
  }
  o["printerDoneRepeatCount"] = s.printerDoneRepeatCount;
  o["printerDoneRepeatMinutes"] = s.printerDoneRepeatMinutes;
  o["printerProgressStep"] = s.printerProgressStep;
  o["printerProgressSeconds"] = s.printerProgressSeconds;
  o["printerProgressPriority"] = s.printerProgressPriority;
  JsonArray printers = o["printers"].to<JsonArray>();
  for (const PrinterConfig& p : s.printers) {
    JsonObject item = printers.add<JsonObject>();
    item["name"] = p.name;
    item["host"] = p.host;
  }
  if (includeScheduled) {
    JsonArray list = o["scheduled"].to<JsonArray>();
    for (const ScheduledMessage& m : s.scheduled) {
      JsonObject item = list.add<JsonObject>();
      item["text"] = m.text;
      item["at"] = m.at;
      item["indefinitely"] = m.indefinitely;
      item["showSeconds"] = m.showSeconds;
      item["repeatMinutes"] = m.repeatMinutes;
      item["repeatCount"] = m.repeatCount;
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

static bool takeFloat(JsonObjectConst p, const char* key, float& field, float min, float max, String& error) {
  JsonVariantConst v = p[key];
  if (v.isNull()) return true;
  if (!v.is<double>() && !v.is<long>()) {
    error = String(key) + " must be a number";
    return false;
  }
  float value = v.as<float>();
  if (value < min || value > max) {
    error = String(key) + " is out of range";
    return false;
  }
  field = value;
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
            takeString(p, "clockHours", s.clockHours, 8, error) && takeString(p, "clockDay", s.clockDay, 10, error) &&
            takeBool(p, "clockLeadingZero", s.clockLeadingZero, error) && takeString(p, "dateFormat", s.dateFormat, 30, error) &&
            takeInt64(p, "countdownTo", s.countdownTo, error) && takeString(p, "trainType", s.trainType, 10, error) &&
            takeString(p, "trainLine", s.trainLine, 30, error) &&
            takeInt(p, "trainSeconds", s.trainSeconds, 5, 3600, error) &&
            takeString(p, "trainStations", s.trainStations, 1500, error) &&
            takeString(p, "phrases", s.phrases, 3000, error) &&
            takeInt(p, "phraseMinSeconds", s.phraseMinSeconds, 5, 3600, error) &&
            takeInt(p, "phraseMaxSeconds", s.phraseMaxSeconds, 5, 3600, error) &&
            takeString(p, "numbers", s.numbers, 3000, error) && takeString(p, "numberList", s.numberList, 40, error) &&
            takeInt(p, "numberMinSeconds", s.numberMinSeconds, 5, 3600, error) &&
            takeInt(p, "numberMaxSeconds", s.numberMaxSeconds, 5, 3600, error) &&
            takeInt(p, "numberNameSeconds", s.numberNameSeconds, 1, 60, error) &&
            takeBool(p, "quietEnabled", s.quietEnabled, error) && takeString(p, "quietFrom", s.quietFrom, 5, error) &&
            takeString(p, "quietTo", s.quietTo, 5, error) && takeBool(p, "quietBlank", s.quietBlank, error) &&
            takeString(p, "logLevel", s.logLevel, 10, error) && takeInt(p, "waveMs", s.waveMs, 0, 5000, error) &&
            takeString(p, "waveStyle", s.waveStyle, 12, error) && takeBool(p, "quietPowerSave", s.quietPowerSave, error) &&
            takeBool(p, "quietRehome", s.quietRehome, error) &&
            takeInt(p, "printerDoneRepeatCount", s.printerDoneRepeatCount, 0, 50, error) &&
            takeInt(p, "printerDoneRepeatMinutes", s.printerDoneRepeatMinutes, 1, 720, error) &&
            takeInt(p, "printerProgressStep", s.printerProgressStep, 0, 50, error) &&
            takeInt(p, "printerProgressSeconds", s.printerProgressSeconds, 3, 600, error) &&
            takeString(p, "printerProgressPriority", s.printerProgressPriority, 6, error) &&
            takeString(p, "waveOrder", s.waveOrder, 10, error) &&
            takeFloat(p, "weatherLat", s.weatherLat, -90, 90, error) &&
            takeFloat(p, "weatherLon", s.weatherLon, -180, 180, error) &&
            takeString(p, "weatherPlace", s.weatherPlace, 60, error) &&
            takeBool(p, "weatherFahrenheit", s.weatherFahrenheit, error) &&
            takeInt(p, "weatherSeconds", s.weatherSeconds, 3, 120, error) &&
            takeString(p, "bartStation", s.bartStation, 4, error) &&
            takeString(p, "bartDirection", s.bartDirection, 1, error) &&
            takeString(p, "bartKey", s.bartKey, 40, error) && takeInt(p, "bartSeconds", s.bartSeconds, 3, 120, error) &&
            takeBool(p, "printerNotify", s.sources[SRC_PRINTERS].enabled, error) &&  // pre-pop-up settings
            takeInt(p, "printerNotifySeconds", s.sources[SRC_PRINTERS].showSeconds, 10, 7200, error);
  if (!ok) return false;

  if (!isValidMode(s.mode)) return error = "unknown mode '" + s.mode + "'", false;
  if (!oneOf(s.alignment, {"left", "center", "right"})) return error = "alignment must be left, center or right", false;
  if (s.trainType == "bart") s.trainType = "line";  // BART lines are ordinary lines now (older settings)
  if (!oneOf(s.trainType, {"random", "line"})) return error = "trainType must be random or line", false;
  if (layout::parseHHMM(s.quietFrom.c_str()) < 0) return error = "quietFrom must be HH:MM", false;
  if (layout::parseHHMM(s.quietTo.c_str()) < 0) return error = "quietTo must be HH:MM", false;
  if (s.timezone.length() < 3) return error = "timezone must be a POSIX TZ string like PST8PDT,M3.2.0,M11.1.0", false;
  logx::Level level;
  if (!logx::parseLevel(s.logLevel, level)) return error = "logLevel must be debug, info, warn, error or off", false;
  if (!oneOf(s.waveOrder, {"left", "right", "center", "random"})) return error = "waveOrder must be left, right, center or random", false;
  // Older settings had clock24h (true/false; the 12-hour clock always showed AM/PM)
  if (!p["clock24h"].isNull() && p["clockHours"].isNull()) s.clockHours = (p["clock24h"] | false) ? "24" : "12ampm";
  if (!oneOf(s.clockHours, {"24", "12", "12ampm"})) return error = "clockHours must be 24, 12 or 12ampm", false;
  if (!oneOf(s.clockDay, {"none", "number", "ordinal", "weekday"})) return error = "clockDay must be none, number, ordinal or weekday", false;
  if (!oneOf(s.printerProgressPriority, {"low", "normal", "high"})) return error = "printerProgressPriority must be low, normal or high", false;
  if (!oneOf(s.waveStyle, {"off", "arrive", "start", "sequential"})) return error = "waveStyle must be off, arrive, start or sequential", false;
  s.bartStation.toUpperCase();
  s.bartDirection.toLowerCase();
  if (s.bartStation.length() != 4) return error = "bartStation must be a 4-letter station code (e.g. MONT)", false;
  if (!oneOf(s.bartDirection, {"", "n", "s"})) return error = "bartDirection must be empty, n or s", false;

  JsonVariantConst numberLists = p["numberLists"];
  if (!numberLists.isNull()) {
    if (!numberLists.is<JsonObjectConst>()) return error = "numberLists must be an object", false;
    s.numberLists.clear();
    for (JsonPairConst kv : numberLists.as<JsonObjectConst>()) {
      String name = kv.key().c_str();
      String text = kv.value() | "";
      if (name.length() == 0 || name.length() > 40 || text.length() > 1500) {
        return error = "numberLists: names up to 40 characters, lists up to 1500", false;
      }
      if (s.numberLists.size() >= 12) return error = "numberLists: at most 12 edited lists", false;
      s.numberLists.push_back({name, text});
    }
  }

  JsonVariantConst sourceList = p["sources"];
  if (!sourceList.isNull()) {
    if (!sourceList.is<JsonObjectConst>()) return error = "sources must be an object", false;
    for (JsonPairConst kv : sourceList.as<JsonObjectConst>()) {
      int id = -1;
      for (int i = 0; i < SRC_COUNT; i++) {
        if (kv.key() == SOURCE_IDS[i]) id = i;
      }
      if (id < 0) return error = String("unknown source '") + kv.key().c_str() + "'", false;
      JsonObjectConst r = kv.value().as<JsonObjectConst>();
      SourceRule& rule = s.sources[id];
      String priority = rule.priority >= 2 ? "high" : (rule.priority <= 0 ? "low" : "normal");
      String prefix = String("sources.") + SOURCE_IDS[id] + ".";
      bool ok = takeBool(r, "enabled", rule.enabled, error) && takeString(r, "priority", priority, 6, error) &&
                takeInt(r, "showSeconds", rule.showSeconds, 3, 7200, error) &&
                takeInt(r, "everyMinutes", rule.everyMinutes, 0, 1440, error) &&
                takeString(r, "from", rule.from, 5, error) && takeString(r, "to", rule.to, 5, error) &&
                takeInt(r, "days", rule.days, 0, 0x7F, error) && takeInt(r, "trainMinutes", rule.trainMinutes, 0, 60, error);
      if (!ok) return error = prefix + error, false;
      if (!oneOf(priority, {"low", "normal", "high"})) return error = prefix + "priority must be low, normal or high", false;
      rule.priority = popup::parsePriority(priority.c_str());
      if ((rule.from.length() && layout::parseHHMM(rule.from.c_str()) < 0) ||
          (rule.to.length() && layout::parseHHMM(rule.to.c_str()) < 0)) {
        return error = prefix + "from/to must be HH:MM (or empty for all day)", false;
      }
    }
  }

  JsonVariantConst printerList = p["printers"];
  if (!printerList.isNull()) {
    if (!printerList.is<JsonArrayConst>()) return error = "printers must be a list", false;
    s.printers.clear();
    for (JsonObjectConst item : printerList.as<JsonArrayConst>()) {
      PrinterConfig printer;
      printer.name = item["name"] | "";
      printer.host = item["host"] | "";
      printer.name.toUpperCase();
      if (printer.name.length() == 0 || printer.name.length() > 10 || printer.host.length() == 0 ||
          printer.host.length() > 60) {
        return error = "each printer needs a name (1-10 characters) and a host", false;
      }
      if (s.printers.size() >= 4) return error = "at most 4 printers", false;
      s.printers.push_back(printer);
    }
  }
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
      m.showSeconds = item["showSeconds"] | 0;
      m.repeatMinutes = item["repeatMinutes"] | 0;
      m.repeatCount = item["repeatCount"] | 0;
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
// Settings live in NVS (the ESP32's key/value flash area), NOT in LittleFS: uploading the web
// files (uploadfs) replaces the whole LittleFS image and would wipe them. An older
// /settings.json in LittleFS is migrated once.
static Preferences prefs;
static const char* NVS_NAMESPACE = "flappy";
static const char* NVS_KEY = "settings";

bool save() {
  JsonDocument doc;
  toJson(live, doc.to<JsonObject>(), true);
  String json;
  serializeJson(doc, json);
  size_t written = prefs.putBytes(NVS_KEY, json.c_str(), json.length());
  if (written != json.length()) {
    LOGE("settings", "saving to NVS failed (%u of %u bytes)", (unsigned)written, (unsigned)json.length());
    return false;
  }
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

static bool loadJson(const String& json, const char* source) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  String error;
  Settings loaded;
  if (err) {
    LOGE("settings", "%s is corrupt (%s) - using defaults", source, err.c_str());
    return false;
  }
  if (!merge(Settings(), doc.as<JsonObjectConst>(), loaded, error)) {
    LOGE("settings", "invalid value in %s: %s - using defaults", source, error.c_str());
    return false;
  }
  live = loaded;
  LOGI("settings", "loaded from %s: mode %s, %d units, %u scheduled, %u printer(s)", source, live.mode.c_str(),
       live.unitCount, (unsigned)live.scheduled.size(), (unsigned)live.printers.size());
  return true;
}

void begin() {
  if (!lock) lock = xSemaphoreCreateMutex();
  prefs.begin(NVS_NAMESPACE, false);
  if (!LittleFS.begin(true)) {  // web files; true = format if it can't be mounted
    LOGE("settings", "LittleFS mount failed - web files missing");
  } else {
    LOGI("settings", "LittleFS (web files): %u of %u KB used", (unsigned)(LittleFS.usedBytes() / 1024),
         (unsigned)(LittleFS.totalBytes() / 1024));
  }

  size_t length = prefs.getBytesLength(NVS_KEY);
  if (length > 0) {
    std::unique_ptr<char[]> buffer(new char[length + 1]);
    prefs.getBytes(NVS_KEY, buffer.get(), length);
    buffer[length] = 0;
    loadJson(String(buffer.get()), "NVS");
  } else if (LittleFS.exists(FILE_PATH)) {
    File file = LittleFS.open(FILE_PATH, "r");
    String json = file.readString();
    file.close();
    if (loadJson(json, FILE_PATH)) {
      save();
      LittleFS.remove(FILE_PATH);
      LOGI("settings", "moved settings from %s to NVS", FILE_PATH);
    }
  } else {
    LOGW("settings", "no saved settings - using defaults");
    save();
  }
  logx::Level level;
  if (logx::parseLevel(live.logLevel, level)) logx::setSerialLevel(level);
  publish();
}

}  // namespace settings
