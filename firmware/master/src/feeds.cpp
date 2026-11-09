#include "feeds.h"

#include <ArduinoJson.h>
#include <lwip/netdb.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>

#include "app.h"
#include "config.h"
#include "log.h"
#include "modes.h"
#include "net.h"
#include "settings.h"

namespace feeds {

static SemaphoreHandle_t lock = nullptr;
static Weather weatherData;
static Bart bartData;
static std::vector<Printer> printerData;

const char* weatherWord(int code) {
  // WMO weather interpretation codes (https://open-meteo.com/en/docs)
  // At most 6 letters so "57F CLOUDY" fits on 10 flaps
  if (code == 0) return "CLEAR";
  if (code <= 2) return "PARTLY";
  if (code == 3) return "CLOUDY";
  if (code == 45 || code == 48) return "FOG";
  if (code >= 51 && code <= 57) return "MIST";
  if (code >= 61 && code <= 67) return "RAIN";
  if (code >= 71 && code <= 77) return "SNOW";
  if (code >= 80 && code <= 82) return "SHOWER";
  if (code >= 85 && code <= 86) return "SNOW";
  if (code >= 95) return "STORM";
  return "?";
}

// ---- HTTP ----------------------------------------------------------------------------------
// A minimal HTTP/1.0 GET. Deliberately NOT HTTPClient: HTTPClient resolves names with the core's
// NetworkManager::hostByName(), which calls lwIP's dns_clear_cache() without the TCP/IP core lock
// on its first use. If the NTP client has a DNS lookup pending at that moment, lwIP asserts
// ("Required to lock TCPIP core functionality") and the ESP32 reboots. Here names are resolved
// with lwip_getaddrinfo() (thread-safe) and we connect by IP. HTTPS still verifies the server
// certificate (hostname + Mozilla root bundle built into the core).

// "printy.local" -> IP. lwip_getaddrinfo runs inside the network stack's own thread (thread-safe)
// and, with CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES, answers .local names by multicast DNS.
// (ESPmDNS's MDNS.queryHost() from this task tripped lwIP's thread-safety check and crashed.)
// Cached for 10 minutes.
static String resolveHost(const String& host) {
  struct Cached {
    String host;
    String ip;
    uint32_t at;
  };
  static std::vector<Cached> cache;
  IPAddress literal;
  if (literal.fromString(host)) return host;
  for (Cached& c : cache) {
    if (c.host == host && millis() - c.at < 600000) return c.ip;
  }
  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* result = nullptr;
  if (lwip_getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) return "";
  String ip = IPAddress(((struct sockaddr_in*)result->ai_addr)->sin_addr.s_addr).toString();
  lwip_freeaddrinfo(result);
  for (Cached& c : cache) {
    if (c.host == host) {
      c.ip = ip;
      c.at = millis();
      return ip;
    }
  }
  cache.push_back({host, ip, (uint32_t)millis()});
  return ip;
}

static void logResources(const char* what, uint32_t startedMs) {
  LOGD("feeds", "%s took %lu ms; stack left %u bytes, heap free %u (largest %u)", what,
       (unsigned long)(millis() - startedMs), (unsigned)uxTaskGetStackHighWaterMark(nullptr),
       (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

struct Url {
  bool https = false;
  String host;
  uint16_t port = 80;
  String path = "/";
};

static bool parseUrl(const String& url, Url& u) {
  int start;
  if (url.startsWith("https://")) {
    u.https = true;
    u.port = 443;
    start = 8;
  } else if (url.startsWith("http://")) {
    start = 7;
  } else {
    return false;
  }
  int slash = url.indexOf('/', start);
  String hostPort = slash < 0 ? url.substring(start) : url.substring(start, slash);
  u.path = slash < 0 ? "/" : url.substring(slash);
  int colon = hostPort.indexOf(':');
  u.host = colon < 0 ? hostPort : hostPort.substring(0, colon);
  if (colon >= 0) u.port = hostPort.substring(colon + 1).toInt();
  return u.host.length() > 0;
}

// Decodes "Transfer-Encoding: chunked" on the fly for ArduinoJson (HTTP/1.1 servers use it)
class ChunkedStream : public Stream {
 public:
  ChunkedStream(Client& c, bool chunked) : client(c), chunked(chunked) {}
  int available() override { return client.available(); }
  int read() override {
    if (!chunked) return client.read();
    if (left == 0) {
      if (done) return -1;
      String size = client.readStringUntil('\n');  // "1a2f\r" (after the previous chunk's CRLF)
      size.trim();
      if (size.length() == 0) size = client.readStringUntil('\n');
      left = strtoul(size.c_str(), nullptr, 16);
      if (left == 0) {
        done = true;
        return -1;
      }
    }
    int c = client.read();
    if (c >= 0) left--;
    return c;
  }
  int peek() override { return -1; }  // not used by ArduinoJson
  size_t write(uint8_t) override { return 0; }

 private:
  Client& client;
  bool chunked;
  unsigned long left = 0;
  bool done = false;
};

static bool getJson(const String& url, JsonDocument& doc, JsonDocument* filter, String& error) {
  Url u;
  if (!parseUrl(url, u)) return error = "bad URL", false;
  String ipText = resolveHost(u.host);
  IPAddress ip;
  if (ipText.length() == 0 || !ip.fromString(ipText)) return error = "can't resolve " + u.host, false;

  NetworkClientSecure secure;
  NetworkClient plain;
  Client* client;
  if (u.https) {
    // A TLS connection needs ~16 KB contiguous heap for its buffers. Skip a refresh rather than
    // run the whole system out of memory.
    if (ESP.getMaxAllocHeap() < 24000 || ESP.getFreeHeap() < 50000) {
      return error = "not enough memory for HTTPS (" + String(ESP.getFreeHeap()) + " free)", false;
    }
    secure.useBuiltinCACertBundle();
    secure.setHandshakeTimeout(10);
    secure.setTimeout(8000);
    if (!secure.connect(ip, u.port, u.host.c_str(), nullptr, nullptr, nullptr)) {
      char tls[100] = "";
      secure.lastError(tls, sizeof(tls));
      return error = String("TLS connect to ") + u.host + " failed: " + tls, false;
    }
    client = &secure;
  } else {
    plain.setTimeout(8000);
    if (!plain.connect(ip, u.port, 5000)) return error = "can't connect to " + u.host, false;
    client = &plain;
  }

  // HTTP/1.1: api.bart.gov answers HTTP/1.0 with "403 Forbidden"
  client->print(String("GET ") + u.path + " HTTP/1.1\r\nHost: " + u.host +
                "\r\nUser-Agent: flappy-split-flap/" FIRMWARE_VERSION "\r\nAccept: application/json\r\n"
                "Connection: close\r\n\r\n");
  String status = client->readStringUntil('\n');  // "HTTP/1.1 200 OK"
  int space = status.indexOf(' ');
  int code = space > 0 ? status.substring(space + 1, space + 4).toInt() : 0;
  if (code != 200) {
    client->stop();
    return error = code ? "HTTP " + String(code) : "no answer from " + u.host, false;
  }
  bool chunked = false;
  while (client->connected() || client->available()) {  // headers
    String header = client->readStringUntil('\n');
    if (header == "\r" || header.length() == 0) break;
    header.toLowerCase();
    if (header.startsWith("transfer-encoding:") && header.indexOf("chunked") > 0) chunked = true;
  }
  ChunkedStream body(*client, chunked);
  DeserializationError err = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
                                    : deserializeJson(doc, body);
  client->stop();
  if (err) return error = String("bad JSON: ") + err.c_str(), false;
  return true;
}

// ---- Weather -------------------------------------------------------------------------------
static void fetchWeather(const Settings& s) {
  uint32_t started = millis();
  char url[320];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code"
           "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code"
           "&temperature_unit=%s&timezone=auto&forecast_days=1",
           s.weatherLat, s.weatherLon, s.weatherFahrenheit ? "fahrenheit" : "celsius");
  JsonDocument doc;
  Weather w;
  if (!getJson(url, doc, nullptr, w.error)) {
    LOGW("feeds", "weather: %s", w.error.c_str());
  } else {
    w.valid = true;
    w.now = doc["current"]["temperature_2m"] | 0.0f;
    w.codeNow = doc["current"]["weather_code"] | 0;
    w.high = doc["daily"]["temperature_2m_max"][0] | 0.0f;
    w.low = doc["daily"]["temperature_2m_min"][0] | 0.0f;
    w.rainChance = doc["daily"]["precipitation_probability_max"][0] | 0;
    w.codeToday = doc["daily"]["weather_code"][0] | 0;
    LOGI("feeds", "weather %s: %.0f, %s, high %.0f low %.0f, rain %d%%", s.weatherPlace.c_str(), w.now,
         weatherWord(w.codeNow), w.high, w.low, w.rainChance);
  }
  w.fetchedMs = millis();
  xSemaphoreTake(lock, portMAX_DELAY);
  if (w.valid || !weatherData.valid) weatherData = w;
  else weatherData.error = w.error;  // keep the last good data, note the error
  if (w.valid) weatherData.error = "";
  xSemaphoreGive(lock);
  logResources("weather", started);
}

// ---- BART ----------------------------------------------------------------------------------
static void fetchBart(const Settings& s) {
  uint32_t started = millis();
  String url = "https://api.bart.gov/api/etd.aspx?cmd=etd&json=y&orig=" + s.bartStation + "&key=" + s.bartKey;
  if (s.bartDirection.length()) url += "&dir=" + s.bartDirection;
  JsonDocument filter;
  JsonObject f = filter["root"]["station"][0]["etd"][0].to<JsonObject>();
  f["abbreviation"] = true;
  f["estimate"][0]["minutes"] = true;
  f["estimate"][0]["direction"] = true;
  JsonDocument doc;
  Bart b;
  b.station = s.bartStation;
  if (!getJson(url, doc, &filter, b.error)) {
    LOGW("feeds", "bart: %s", b.error.c_str());
  } else {
    b.valid = true;
    for (JsonObjectConst etd : doc["root"]["station"][0]["etd"].as<JsonArrayConst>()) {
      String abbr = etd["abbreviation"] | "";
      for (JsonObjectConst est : etd["estimate"].as<JsonArrayConst>()) {
        String minutes = est["minutes"] | "";
        String dir = est["direction"] | "";
        Departure d;
        d.destination = abbr;
        d.minutes = minutes == "Leaving" ? 0 : minutes.toInt();
        d.direction = dir.length() ? (char)tolower(dir[0]) : '?';
        b.departures.push_back(d);
      }
    }
    std::sort(b.departures.begin(), b.departures.end(),
              [](const Departure& x, const Departure& y) { return x.minutes < y.minutes; });
    if (b.departures.size() > 8) b.departures.resize(8);
    LOGD("feeds", "bart %s: %u departures", b.station.c_str(), (unsigned)b.departures.size());
  }
  b.fetchedMs = millis();
  xSemaphoreTake(lock, portMAX_DELAY);
  bartData = b;
  xSemaphoreGive(lock);
  logResources("bart", started);
}

// ---- Printers ------------------------------------------------------------------------------
static void notify(const String& text, int seconds) {
  app::post([text, seconds]() { modes::notify(text, (uint32_t)seconds * 1000UL); });
}

static void fetchPrinters(const Settings& s) {
  uint32_t started = millis();
  std::vector<Printer> previous = printers();
  std::vector<Printer> result;
  for (const PrinterConfig& cfg : s.printers) {
    Printer p;
    p.name = cfg.name;
    p.host = cfg.host;
    {
      String url = "http://" + cfg.host + (cfg.host.indexOf(':') < 0 ? ":7125" : "") +
                   "/printer/objects/query?print_stats=state,filename,print_duration&display_status=progress";
      JsonDocument doc;
      String error;
      if (!getJson(url, doc, nullptr, error)) LOGD("feeds", "printer %s: %s", cfg.name.c_str(), error.c_str());
      else {
        JsonObjectConst st = doc["result"]["status"];
        p.reachable = true;
        p.state = st["print_stats"]["state"] | "unknown";
        p.file = st["print_stats"]["filename"] | "";
        p.progress = st["display_status"]["progress"] | 0.0f;
        float elapsed = st["print_stats"]["print_duration"] | 0.0f;
        if (p.state == "printing" && p.progress > 0.02f) {
          p.minutesLeft = (int)((elapsed / p.progress - elapsed) / 60.0f + 0.5f);
        }
      }
    }
    // A print that was running just finished (or failed): put it on the display
    for (const Printer& old : previous) {
      if (old.name != p.name || !old.reachable || !p.reachable || old.state == p.state) continue;
      LOGI("feeds", "printer %s: %s -> %s", p.name.c_str(), old.state.c_str(), p.state.c_str());
      if (s.printerNotify && old.state == "printing") {
        if (p.state == "complete") notify(p.name + " DONE", s.printerNotifySeconds);
        else if (p.state == "error") notify(p.name + " ERROR", s.printerNotifySeconds);
        else if (p.state == "cancelled") notify(p.name + " STOPPED", 60);
      }
    }
    result.push_back(p);
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  printerData = result;
  xSemaphoreGive(lock);
  logResources("printers", started);
}

// ---- Task ----------------------------------------------------------------------------------
static void task(void*) {
  uint32_t lastWeather = 0, lastBart = 0, lastPrinters = 0;
  String weatherKey, bartKey;
  Settings s;
  uint32_t seenRevision = 0;
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!net::connected() || app::otaActive) continue;
    if (settings::revision() != seenRevision) {  // copy only when something changed (less heap churn)
      seenRevision = settings::revision();
      s = settings::snapshot();
    }
    uint32_t now = millis();

    // Fetch right away when the location / station changes, then on a timer
    String wk = String(s.weatherLat, 4) + String(s.weatherLon, 4) + (s.weatherFahrenheit ? "F" : "C");
    uint32_t weatherEvery = weather().valid && weather().error.length() == 0 ? 600000 : 30000;  // retry errors sooner
    if (s.mode == "weather" && (wk != weatherKey || lastWeather == 0 || now - lastWeather > weatherEvery)) {
      weatherKey = wk;
      lastWeather = now;
      fetchWeather(s);
    }
    String bk = s.bartStation + s.bartDirection;
    if (s.mode == "bart" && (bk != bartKey || lastBart == 0 || now - lastBart > 30000)) {
      bartKey = bk;
      lastBart = now;
      fetchBart(s);
    }
    if (!s.printers.empty() && (lastPrinters == 0 || now - lastPrinters > 15000)) {
      lastPrinters = now;
      fetchPrinters(s);
    }
  }
}

void begin() {
  lock = xSemaphoreCreateMutex();
  // TLS handshakes need a lot of stack; the log reports how much is left after each fetch
  xTaskCreatePinnedToCore(task, "feeds", 20480, nullptr, 1, nullptr, 1);
  LOGI("feeds", "background fetcher started (weather, BART, printers)");
}

Weather weather() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Weather copy = weatherData;
  xSemaphoreGive(lock);
  return copy;
}

Bart bart() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Bart copy = bartData;
  xSemaphoreGive(lock);
  return copy;
}

std::vector<Printer> printers() {
  xSemaphoreTake(lock, portMAX_DELAY);
  std::vector<Printer> copy = printerData;
  xSemaphoreGive(lock);
  return copy;
}

}  // namespace feeds
