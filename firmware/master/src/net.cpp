#include "net.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include "app.h"
#include "config.h"
#include "led.h"
#include "log.h"
#include "settings.h"

namespace net {

static volatile bool isConnected = false;
static volatile bool gotIpEvent = false;
static volatile int disconnectReason = 0;
static volatile bool disconnectEvent = false;
static uint32_t dropCount = 0;
static uint32_t lastBegin = 0;
static uint32_t upSince = 0;
static bool servicesStarted = false;
static bool timeLogged = false;
static bool scanning = false;
static bool roamScan = false;      // scan started to look for a stronger node while connected
static uint32_t lastRoamCheck = 0;

const char* reasonName(int reason) {
  switch (reason) {
    case 0: return "none";
    case 2: return "auth expired";
    case 3: return "auth leave";
    case 4: return "assoc expired";
    case 8: return "assoc leave";
    case 15: return "4-way handshake timeout (wrong password?)";
    case 200: return "beacon timeout (weak signal?)";
    case 201: return "network not found";
    case 202: return "auth failed (wrong password?)";
    case 203: return "assoc failed";
    case 204: return "handshake timeout";
    case 205: return "connection failed";
    default: return "other";
  }
}

static void onEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  // Runs in the WiFi event task: only set flags, log from loop()
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      isConnected = true;
      gotIpEvent = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      isConnected = false;
      disconnectReason = info.wifi_sta_disconnected.reason;
      disconnectEvent = true;
      break;
    default:
      break;
  }
}

// Mesh networks have several access points with the same name. Scan first and connect to the
// strongest one by BSSID (the core's "sort by signal" option doesn't reliably do this).
static void startScan(bool roaming) {
  scanning = true;
  roamScan = roaming;
  lastBegin = millis();
  WiFi.scanNetworks(true /* async */, false, false, 300);
}

struct BestAp {
  int index = -1;
  int rssi = -1000;
  int count = 0;
};

static BestAp bestAp() {
  BestAp best;
  int n = WiFi.scanComplete();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != SECRET_WIFI_SSID) continue;
    best.count++;
    if (WiFi.RSSI(i) > best.rssi) {
      best.rssi = WiFi.RSSI(i);
      best.index = i;
    }
  }
  return best;
}

static void connectBest() {
  BestAp best = bestAp();
  lastBegin = millis();
  WiFi.disconnect();
  if (best.index >= 0) {
    uint8_t bssid[6];
    memcpy(bssid, WiFi.BSSID(best.index), 6);
    int channel = WiFi.channel(best.index);
    LOGI("wifi", "found %d access point(s) for \"%s\" - joining the strongest: %s, %d dBm, channel %d", best.count,
         SECRET_WIFI_SSID, WiFi.BSSIDstr(best.index).c_str(), best.rssi, channel);
    WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD, channel, bssid);
  } else {
    LOGW("wifi", "\"%s\" not seen in the scan - trying anyway", SECRET_WIFI_SSID);
    WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD);
  }
  WiFi.scanDelete();
}

static void startWifi() { startScan(false); }

void setPowerSave(bool on) {
  WiFi.setSleep(on ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
  setCpuFrequencyMhz(on ? 80 : 240);
  LOGI("power", "%s: WiFi %s, CPU %d MHz", on ? "power saving" : "full speed", on ? "modem sleep" : "always on",
       (int)getCpuFrequencyMhz());
}

void setTimezone(const String& posixTz) {
  configTzTime(posixTz.c_str(), NTP_SERVER1, NTP_SERVER2);
  LOGI("time", "time zone %s", posixTz.c_str());
}

static void startServices() {
  servicesStarted = true;

  ArduinoOTA.setHostname(HOSTNAME);
  if (strlen(SECRET_OTA_PASSWORD) > 0) ArduinoOTA.setPassword(SECRET_OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    app::otaActive = true;
    led::set(led::OTA);
    LittleFS.end();
    LOGW("ota", "update started (%s) - display paused", ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "web files");
  });
  ArduinoOTA.onEnd([]() { LOGW("ota", "update finished - rebooting"); });
  ArduinoOTA.onError([](ota_error_t error) {
    app::otaActive = false;
    led::set(led::RUNNING);
    LOGE("ota", "update failed, error %d", (int)error);
  });
  ArduinoOTA.begin();  // also starts mDNS as HOSTNAME.local

  MDNS.addService("http", "tcp", 80);
  MDNS.addService("telnet", "tcp", TELNET_PORT);
  LOGI("net", "http://%s.local  telnet %s.local  OTA \"%s\"", HOSTNAME, HOSTNAME, HOSTNAME);
}

void begin() {
  WiFi.onEvent(onEvent);
  WiFi.persistent(false);  // credentials come from secrets.h; don't wear out flash
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // modem sleep adds 100-300 ms to every request
  WiFi.setAutoReconnect(true);
#if NET_STATIC_IP
  if (!WiFi.config(IPAddress(NET_IP), IPAddress(NET_GATEWAY), IPAddress(NET_SUBNET), IPAddress(NET_DNS1),
                   IPAddress(NET_DNS2))) {
    LOGE("wifi", "static IP configuration failed");
  }
#endif
  setTimezone(settings::current().timezone);
  LOGI("wifi", "connecting to \"%s\"...", SECRET_WIFI_SSID);
  led::set(led::CONNECTING);
  startWifi();
}

void loop() {
  if (gotIpEvent) {
    gotIpEvent = false;
    upSince = millis();
    LOGI("wifi", "connected: ip %s, rssi %d dBm, channel %d, bssid %s", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(),
         (int)WiFi.channel(), WiFi.BSSIDstr().c_str());
    if (!servicesStarted) startServices();
  }
  if (disconnectEvent) {
    disconnectEvent = false;
    if (upSince != 0) {
      dropCount++;
      LOGW("wifi", "disconnected after %lus: reason %d (%s)", (unsigned long)((millis() - upSince) / 1000),
           disconnectReason, reasonName(disconnectReason));
      upSince = 0;
    } else {
      LOGD("wifi", "connect attempt failed: reason %d (%s)", disconnectReason, reasonName(disconnectReason));
    }
  }

  if (scanning) {
    int result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) return;
    scanning = false;
    if (roamScan) {
      BestAp best = bestAp();
      int current = WiFi.RSSI();
      if (best.index >= 0 && best.rssi >= current + 10 && WiFi.BSSIDstr(best.index) != WiFi.BSSIDstr()) {
        LOGI("wifi", "roaming: %d dBm here, %d dBm at %s", current, best.rssi, WiFi.BSSIDstr(best.index).c_str());
        upSince = 0;  // a deliberate reconnect, not a drop
        connectBest();
      } else {
        WiFi.scanDelete();
      }
    } else {
      connectBest();
    }
  }

  // Weak signal: every 10 minutes look for a much stronger node of the same network
  if (isConnected && !scanning && millis() - lastRoamCheck > 600000) {
    lastRoamCheck = millis();
    if (WiFi.RSSI() < -70) startScan(true);
  }

  // The core gives up after some failures (e.g. "auth expired" on WPA2/WPA3 mesh routers): retry.
  if (!isConnected && !scanning && millis() - lastBegin > 10000) {
    LOGI("wifi", "still not connected (last reason %d: %s) - retrying", disconnectReason, reasonName(disconnectReason));
    startWifi();
  }

  if (!timeLogged && timeValid()) {
    timeLogged = true;
    struct tm t;
    localTime(t);
    char buffer[40];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &t);
    LOGI("time", "clock set: %s", buffer);
  }

  if (!app::otaActive) {
    if (!isConnected) led::set(servicesStarted ? led::WIFI_LOST : led::CONNECTING);
    else if (!timeValid()) led::set(led::NO_TIME);
  }

  if (servicesStarted) ArduinoOTA.handle();
}

bool connected() { return isConnected; }
bool timeValid() { return time(nullptr) > 1700000000; }

bool localTime(struct tm& out) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  localtime_r(&now, &out);
  return true;
}

int rssi() { return isConnected ? WiFi.RSSI() : 0; }
String ip() { return WiFi.localIP().toString(); }
uint32_t reconnects() { return dropCount; }
int lastDisconnectReason() { return disconnectReason; }
uint32_t connectedSinceMs() { return upSince; }

}  // namespace net
