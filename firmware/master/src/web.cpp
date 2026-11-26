#include "web.h"

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "app.h"
#include "config.h"
#include "content.h"
#include "console.h"
#include "crash.h"
#include "display.h"
#include "feeds.h"
#include "log.h"
#include "modes.h"
#include "popup_rules.h"
#include "net.h"
#include "settings.h"
#include "units.h"

namespace web {

static AsyncWebServer server(80);
static AsyncWebSocket socket("/ws");

// Memory guard. The ESP32 aborts (and reboots) if the network stack can't get memory, which happened
// when a browser opened many connections at once. So: a request that would arrive when memory is low,
// or when too many are being handled, gets "503 busy, retry" - a few bytes - instead of the whole
// display going down.
static constexpr uint32_t REQUEST_MIN_HEAP = 36000;   // bytes free (TLS downloads wait for much more)
static constexpr uint32_t REQUEST_MIN_BLOCK = 12000;  // largest free block
static constexpr int REQUEST_MAX_IN_FLIGHT = 10;
static volatile int inFlight = 0;
static volatile uint32_t inFlightFullSince = 0;
static volatile uint32_t rejectedRequests = 0;

uint32_t rejected() { return rejectedRequests; }
bool busy() { return inFlight > 0; }

static void sendJson(AsyncWebServerRequest* request, int code, JsonDocument& doc) {
  String body;
  body.reserve(measureJson(doc) + 1);  // exact size: growing a String by doubling fragments the heap
  serializeJson(doc, body);
  request->send(code, "application/json", body);
}

static void sendError(AsyncWebServerRequest* request, int code, const String& message) {
  JsonDocument doc;
  doc["error"] = message;
  sendJson(request, code, doc);
}

static void sendOk(AsyncWebServerRequest* request) { request->send(200, "application/json", "{\"ok\":true}"); }

// Validate a settings patch against the current settings (for the HTTP answer), then apply the
// same patch on the main loop (so two quick requests can't overwrite each other).
static void applySettingsPatch(AsyncWebServerRequest* request, JsonVariant& json) {
  if (!json.is<JsonObject>()) return sendError(request, 400, "expected a JSON object");
  Settings checked;
  String error;
  if (!settings::merge(settings::snapshot(), json.as<JsonObjectConst>(), checked, error)) {
    return sendError(request, 400, error);
  }
  String patch;
  serializeJson(json, patch);
  app::post([patch]() {
    JsonDocument doc;
    deserializeJson(doc, patch);
    Settings updated;
    String error;
    const Settings& before = settings::current();
    if (!settings::merge(before, doc.as<JsonObjectConst>(), updated, error)) {
      LOGW("web", "settings change rejected: %s", error.c_str());
      return;
    }
    bool timezoneChanged = updated.timezone != before.timezone;
    bool powerChanged = updated.wifiSleep != before.wifiSleep || updated.cpuMhz != before.cpuMhz ||
                        updated.txPower != before.txPower || updated.wifiFallback != before.wifiFallback;
    // Editor state only (the Numbers panel autosaves its lists): save, but don't restart the
    // mode - in famous-numbers mode a restart would flip to a new number on every keystroke pause
    bool editorOnly = true;
    for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
      if (kv.key() != "numberList" && kv.key() != "numberLists") editorOnly = false;
    }
    settings::commit(updated);
    if (editorOnly) {
      LOGD("web", "numbers editor saved");
      return;
    }
    if (timezoneChanged) net::setTimezone(updated.timezone);
    if (powerChanged) net::applyPower();
    app::displayPaused = false;
    modes::restart();
    LOGI("web", "settings changed: %s", patch.c_str());
  });
  sendOk(request);
}

// Siri / dictation gives "Hello." or "Don't panic!": drop the closing punctuation and apostrophes
static String cleanDictation(String text) {
  text.trim();
  text.replace("\u2019", "");  // curly apostrophe
  text.replace("'", "");
  text.replace("\"", "");
  text.replace("\u201c", "");
  text.replace("\u201d", "");
  while (text.length() && (text.endsWith(".") || text.endsWith("!") || text.endsWith("?") || text.endsWith(","))) {
    text.remove(text.length() - 1);
  }
  return text;
}

static void postMessage(AsyncWebServerRequest* request, JsonVariant& json) {
  String text = json["text"] | "";
  int64_t at = json["at"] | (long long)0;
  bool indefinitely = json["indefinitely"] | false;
  int seconds = json["seconds"] | 0;
  String priorityName = json["priority"] | "";
  if (json["dictated"] | false) text = cleanDictation(text);
  if (text.length() > 200) return sendError(request, 400, "text is too long (max 200)");
  if (seconds < 0 || seconds > 86400) return sendError(request, 400, "seconds must be 0..86400");
  if (seconds > 0 && at == 0) {
    // Temporary: show it, then go back to whatever mode is running (notifications)
    int priority = priorityName.length() ? popup::parsePriority(priorityName.c_str()) : -1;
    app::post([text, seconds, priority]() { modes::notify(text, (uint32_t)seconds * 1000UL, priority); });
    return sendOk(request);
  }
  if (at == 0) {
    app::post([text]() {
      Settings updated = settings::current();
      updated.mode = "text";
      updated.text = text;
      settings::commit(updated);
      app::displayPaused = false;
      modes::restart();
      LOGI("web", "message: \"%s\"", text.c_str());
    });
    return sendOk(request);
  }
  ScheduledMessage m;
  m.text = text;
  m.at = at;
  m.indefinitely = indefinitely;
  m.showSeconds = json["showSeconds"] | 0;
  m.repeatMinutes = json["repeatMinutes"] | 0;
  m.repeatCount = json["repeatCount"] | 0;
  if (m.showSeconds < 0 || m.showSeconds > 7200 || m.repeatMinutes < 0 || m.repeatMinutes > 10080 ||
      m.repeatCount < 0 || m.repeatCount > 500) {
    return sendError(request, 400, "showSeconds 0..7200, repeatMinutes 0..10080, repeatCount 0..500");
  }
  Settings check = settings::snapshot();
  String error;
  if (!settings::addScheduled(check, m, error)) return sendError(request, 400, error);
  app::post([m]() {
    Settings updated = settings::current();
    String error;
    if (settings::addScheduled(updated, m, error)) {
      settings::commit(updated);
      LOGI("web", "scheduled \"%s\" at %lld", m.text.c_str(), (long long)m.at);
    }
  });
  sendOk(request);
}

static void onSocket(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg,
                     uint8_t* data, size_t length) {
  if (type == WS_EVT_CONNECT) {
    console::webConnected(client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    console::webDisconnected(client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == length && info->opcode == WS_TEXT) {
      console::webInput(client->id(), String((const char*)data, length));
    }
  }
}

static void webSend(uint32_t clientId, const String& text) { socket.text(clientId, text); }

struct LogCollect {
  JsonArray lines;
};

static void collectLog(const logx::Entry& entry, void* context) {
  LogCollect* c = (LogCollect*)context;
  JsonObject line = c->lines.add<JsonObject>();
  line["seq"] = entry.seq;
  line["level"] = String(logx::levelChar(entry.level));
  line["text"] = logx::format(entry);
}

void begin() {
  socket.onEvent(onSocket);
  server.addHandler(&socket);

  server.addMiddleware([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
    uint32_t now = millis();
    // A counter that stays full for 15 s has lost track (a missed disconnect): start over
    if (inFlight >= REQUEST_MAX_IN_FLIGHT) {
      if (inFlightFullSince == 0) inFlightFullSince = now;
      else if (now - inFlightFullSince > 15000) {
        inFlight = 0;
        inFlightFullSince = 0;
      }
    } else {
      inFlightFullSince = 0;
    }
    if (ESP.getFreeHeap() < REQUEST_MIN_HEAP || ESP.getMaxAllocHeap() < REQUEST_MIN_BLOCK ||
        inFlight >= REQUEST_MAX_IN_FLIGHT) {
      static uint32_t lastLog = 0;
      rejectedRequests++;
      if (now - lastLog > 10000) {
        lastLog = now;
        LOGW("web", "busy: refusing %s (%u free, largest %u, %d in flight, %lu refused so far)", request->url().c_str(),
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (int)inFlight, (unsigned long)rejectedRequests);
      }
      AsyncWebServerResponse* busy = request->beginResponse(503, "text/plain", "Busy - try again in a moment");
      busy->addHeader("Retry-After", "2");
      busy->addHeader("Cache-Control", "no-store");
      request->send(busy);
      return;
    }
    inFlight++;
    request->onDisconnect([]() {
      if (inFlight > 0) inFlight--;
    });
    next();
  });
  console::setWebSender(webSend);

  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "application/json", app::stateJson());
  });

  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    settings::toJson(settings::snapshot(), doc.to<JsonObject>(), true);
    sendJson(request, 200, doc);
  });
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/settings", applySettingsPatch));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/message", postMessage));

  server.on("/api/scheduled", HTTP_DELETE, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("at")) return sendError(request, 400, "missing ?at=");
    int64_t at = atoll(request->getParam("at")->value().c_str());
    app::post([at]() {
      Settings updated = settings::current();
      if (settings::removeScheduled(updated, at)) settings::commit(updated);
    });
    sendOk(request);
  });

  server.addHandler(new AsyncCallbackJsonWebHandler("/api/command", [](AsyncWebServerRequest* request, JsonVariant& json) {
    String line = json["line"] | "";
    if (line.length() == 0 || line.length() > 200) return sendError(request, 400, "expected {\"line\":\"...\"}");
    LOGI("web", "command: %s", line.c_str());
    console::runDetached(line);
    sendOk(request);
  }));

  // Built-in station lists (content.h), for the web UI's preview. Constant data: safe from this task.
  server.on("/api/trainlines", HTTP_GET, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray lines = doc["lines"].to<JsonArray>();
    for (uint8_t i = 0; i < content::TRAIN_LINE_COUNT; i++) {
      const content::TrainLine* line = content::TRAIN_LINES[i];
      JsonObject o = lines.add<JsonObject>();
      String id = line->id;
      id.replace('_', '-');  // settings use "bart-red"
      o["id"] = id;
      o["name"] = line->name;
      JsonArray stops = o["stops"].to<JsonArray>();
      for (uint8_t s = 0; s < line->count; s++) stops.add(line->stops[s]);
    }
    JsonArray random = doc["random"].to<JsonArray>();
    for (uint8_t i = 0; i < content::RANDOM_STATION_COUNT; i++) random.add(content::RANDOM_STATIONS[i]);
    sendJson(request, 200, doc);
  });

  // The Weather panel's "Download weather now" button
  server.on("/api/weather/refresh", HTTP_POST, [](AsyncWebServerRequest* request) {
    feeds::refreshWeather();
    LOGI("web", "weather refresh requested");
    sendOk(request);
  });

  server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    uint32_t since = request->hasParam("since") ? strtoul(request->getParam("since")->value().c_str(), nullptr, 10) : 0;
    uint32_t newest = logx::lastSeq();
    if (since == 0 && newest > 30) since = newest - 30;  // first call: the newest 30 lines (the page polls for the rest)
    JsonDocument doc;
    LogCollect collect{doc["lines"].to<JsonArray>()};
    uint32_t last = logx::readSince(since, 40, collectLog, &collect);
    doc["last"] = last;
    sendJson(request, 200, doc);
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
    LOGW("web", "reboot requested");
    sendOk(request);
    app::post([]() {
      app::wait(300);
      ESP.restart();
    });
  });

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");

  server.onNotFound([](AsyncWebServerRequest* request) {
    if (request->url().startsWith("/api/")) return sendError(request, 404, "unknown endpoint " + request->url());
    if (!LittleFS.exists("/index.html") && !LittleFS.exists("/index.html.gz")) {
      return request->send(503, "text/plain",
                           "Web files missing: upload them with  pio run -t uploadfs  (firmware/master)");
    }
    request->send(404, "text/plain", "Not found");
  });

  server.begin();
  LOGI("web", "web server on port 80");
}

String buildState() {
  JsonDocument doc;
  const Settings& s = settings::current();
  doc["version"] = FIRMWARE_VERSION;
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["heapMin"] = ESP.getMinFreeHeap();          // lowest free memory since boot
  doc["heapBlock"] = ESP.getMaxAllocHeap();       // largest free block
  doc["refused"] = rejected();                    // web requests answered "busy" to protect memory
  if (crash::present()) doc["lastCrash"] = crash::summary();

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["connected"] = net::connected();
  wifi["rssi"] = net::rssi();
  wifi["ip"] = net::ip();
  wifi["drops"] = net::reconnects();

  {
    net::PowerStatus p = net::powerStatus();
    JsonObject o = doc["power"].to<JsonObject>();
    o["cpuMhz"] = p.cpuMhz;
    o["wifiSleep"] = p.wifiSleep;
    o["txDbm"] = p.txDbm;
    o["quiet"] = app::lowPower;
    o["fallback"] = p.fallback;
    o["fallbackMinutes"] = p.fallbackMinutesLeft;
    o["meshNodes"] = p.meshNodes;
    o["bssid"] = p.bssid;
    o["channel"] = p.channel;
    if (!isnan(p.chipTempC) && p.chipTempC > -40 && p.chipTempC < 150) o["chipTempC"] = p.chipTempC;
  }

  JsonObject clock = doc["time"].to<JsonObject>();
  struct tm t;
  clock["valid"] = net::localTime(t);
  if (clock["valid"]) {
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &t);
    clock["local"] = buffer;
    clock["unix"] = (long long)time(nullptr);
  }
  clock["zone"] = s.timezoneName;

  JsonObject d = doc["display"].to<JsonObject>();
  d["mode"] = s.mode;
  d["status"] = modes::status();
  d["text"] = display::text();
  d["line"] = display::line();
  d["lineIndex"] = display::lineIndex();
  d["lineCount"] = display::lineCount();
  d["idle"] = display::idle();
  d["paused"] = app::displayPaused;
  d["lastMoveMs"] = display::lastMoveMs();

  feeds::Weather w = feeds::weather();
  {
    JsonObject o = doc["weather"].to<JsonObject>();
    o["valid"] = w.valid;
    o["attempts"] = w.attempts;
    o["error"] = w.error;  // the last attempt's error ("" if it worked)
    if (w.attemptMs) o["attemptAgeS"] = (millis() - w.attemptMs) / 1000;
    if (w.valid) {
      o["now"] = w.now;
      o["word"] = feeds::weatherWord(w.codeNow);
      o["high"] = w.high;
      o["low"] = w.low;
      o["rain"] = w.rainChance;
      o["ageS"] = (millis() - w.fetchedMs) / 1000;
    }
  }
  feeds::Bart b = feeds::bart();
  if (b.valid || b.error.length()) {
    JsonObject o = doc["bart"].to<JsonObject>();
    o["station"] = b.station;
    o["error"] = b.error;
    JsonArray deps = o["departures"].to<JsonArray>();
    for (const feeds::Departure& d : b.departures) {
      JsonObject dep = deps.add<JsonObject>();
      dep["to"] = d.destination;
      dep["min"] = d.minutes;
      dep["dir"] = String(d.direction);
    }
  }
  JsonArray printerList = doc["printers"].to<JsonArray>();
  for (const feeds::Printer& p : feeds::printers()) {
    JsonObject o = printerList.add<JsonObject>();
    o["name"] = p.name;
    o["reachable"] = p.reachable;
    o["state"] = p.state;
    o["progress"] = p.progress;
    o["minutesLeft"] = p.minutesLeft;
    o["file"] = p.file;
  }

  JsonArray list = doc["units"].to<JsonArray>();
  for (int a = 0; a < units::count(); a++) {
    const units::Unit& u = units::get(a);
    JsonObject o = list.add<JsonObject>();
    o["address"] = a;
    o["present"] = u.present;
    o["status"] = u.status;
    o["statusName"] = unitproto::statusName(u.status);
    o["firmware"] = units::versionName(u.version);
    o["i2cErrors"] = u.i2cErrors;
    if (u.infoValid && u.isV2()) {
      const unitproto::Info& i = u.info;
      JsonObject info = o["info"].to<JsonObject>();
      info["vcc"] = i.vcc;
      info["vccMin"] = i.vccMin;
      info["syncs"] = i.syncs;
      info["misses"] = i.misses;
      info["lastSyncError"] = i.lastSyncError;
      info["maxSyncError"] = i.maxSyncError;
      info["boots"] = i.boots;
      info["resetCause"] = i.resetCause;
      info["uptime"] = i.uptime;
      info["moves"] = i.moves;
      info["offset"] = i.offset;
      info["stepsPerRev"] = u.stepsPerRev();
      info["noOffset"] = (i.flags & unitproto::INFO_FLAG_NO_OFFSET) != 0;
    }
  }
  String json;
  json.reserve(measureJson(doc) + 1);
  serializeJson(doc, json);
  return json;
}

}  // namespace web
