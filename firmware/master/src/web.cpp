#include "web.h"

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "app.h"
#include "config.h"
#include "console.h"
#include "display.h"
#include "log.h"
#include "modes.h"
#include "net.h"
#include "settings.h"
#include "units.h"

namespace web {

static AsyncWebServer server(80);
static AsyncWebSocket socket("/ws");

static void sendJson(AsyncWebServerRequest* request, int code, JsonDocument& doc) {
  String body;
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
    settings::commit(updated);
    if (timezoneChanged) net::setTimezone(updated.timezone);
    app::displayPaused = false;
    modes::restart();
    LOGI("web", "settings changed: %s", patch.c_str());
  });
  sendOk(request);
}

static void postMessage(AsyncWebServerRequest* request, JsonVariant& json) {
  String text = json["text"] | "";
  int64_t at = json["at"] | (long long)0;
  bool indefinitely = json["indefinitely"] | false;
  if (text.length() > 200) return sendError(request, 400, "text is too long (max 200)");
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
  ScheduledMessage m{text, at, indefinitely};
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

  server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    uint32_t since = request->hasParam("since") ? strtoul(request->getParam("since")->value().c_str(), nullptr, 10) : 0;
    JsonDocument doc;
    LogCollect collect{doc["lines"].to<JsonArray>()};
    uint32_t last = logx::readSince(since, 100, collectLog, &collect);
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

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["connected"] = net::connected();
  wifi["rssi"] = net::rssi();
  wifi["ip"] = net::ip();
  wifi["drops"] = net::reconnects();

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
  serializeJson(doc, json);
  return json;
}

}  // namespace web
