#include "net.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_wifi.h>

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
static int meshNodes = 0;               // access points with our network name in the last scan

// Power saving that makes WiFi drop is worse than none: after 3 drops in 10 minutes go back to full
// WiFi power for 30 minutes (settings.wifiFallback)
static const int FALLBACK_DROPS = 3;
static const uint32_t FALLBACK_WINDOW_MS = 10UL * 60000;
static const uint32_t FALLBACK_HOLD_MS = 30UL * 60000;
static uint32_t recentDrops[FALLBACK_DROPS] = {0};
static uint32_t fallbackUntil = 0;      // millis() when full power ends; 0 = not in fall-back
static wifi_power_t txTier = WIFI_POWER_19_5dBm;
static uint32_t lastTxCheck = 0;

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
  meshNodes = best.count;
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

// ---- Power ----------------------------------------------------------------------------------
// Automatic transmit power: strong signal (next to the router) -> turn the radio down; weak signal
// (moved next to a far extender) -> full power. The signal we measure is the router's, so it is only
// a guide for ours: tiers stay conservative (never below 8.5 dBm) and step UP at once but DOWN only
// with 4 dB of margin, so it doesn't flap.
static wifi_power_t autoTxTier(int rssi) {
  static const struct { int rssi; wifi_power_t tier; } steps[] = {
      {-50, WIFI_POWER_8_5dBm}, {-60, WIFI_POWER_11dBm}, {-67, WIFI_POWER_15dBm}};
  wifi_power_t wanted = WIFI_POWER_19_5dBm;
  for (const auto& step : steps) {
    int margin = txTier <= step.tier ? 0 : 4;  // currently at or below this tier: no extra margin to stay
    if (rssi >= step.rssi + margin) {
      wanted = step.tier;
      break;
    }
  }
  return wanted;
}

static void updateTxPower(bool force) {
  const Settings& s = settings::current();
  wifi_power_t wanted = WIFI_POWER_19_5dBm;
  if (fallbackUntil == 0) {
    if (s.txPower == "low") wanted = WIFI_POWER_11dBm;
    else if (s.txPower == "auto" && isConnected) wanted = autoTxTier(WiFi.RSSI());
  }
  if (wanted != txTier || force) {
    txTier = wanted;
    WiFi.setTxPower(txTier);
    LOGD("power", "WiFi transmit power %.1f dBm", txTier / 4.0f);
  }
}

void applyPower() {
  const Settings& s = settings::current();
  bool fallback = fallbackUntil != 0;

  String sleep = s.wifiSleep;
  if (app::lowPower && sleep == "off") sleep = "min";  // quiet hours: at least modem sleep
  if (fallback) sleep = "off";
  WiFi.setSleep(sleep == "min" ? WIFI_PS_MIN_MODEM : sleep == "max" ? WIFI_PS_MAX_MODEM : WIFI_PS_NONE);

  int mhz = app::lowPower ? min(s.cpuMhz, 80) : s.cpuMhz;
  if ((int)getCpuFrequencyMhz() != mhz) setCpuFrequencyMhz(mhz);
  updateTxPower(true);

  LOGI("power", "WiFi sleep %s, CPU %d MHz, transmit %.1f dBm%s%s", sleep.c_str(), (int)getCpuFrequencyMhz(),
       txTier / 4.0f, app::lowPower ? " (quiet hours)" : "", fallback ? " (fall-back: WiFi kept dropping)" : "");
}

static bool savingWifiPower() {
  const Settings& s = settings::current();
  return s.wifiSleep != "off" || s.txPower != "max" || app::lowPower;
}

// A drop happened: if that's the 3rd within 10 minutes and we're saving WiFi power, stop saving it
static void noteDrop() {
  uint32_t now = millis();
  for (int i = 0; i < FALLBACK_DROPS - 1; i++) recentDrops[i] = recentDrops[i + 1];
  recentDrops[FALLBACK_DROPS - 1] = now;
  bool burst = recentDrops[0] != 0 && now - recentDrops[0] < FALLBACK_WINDOW_MS;
  if (burst && fallbackUntil == 0 && settings::current().wifiFallback && savingWifiPower()) {
    fallbackUntil = now + FALLBACK_HOLD_MS;
    LOGW("power", "WiFi dropped %d times in 10 minutes - full WiFi power for 30 minutes", FALLBACK_DROPS);
    applyPower();
  }
}

PowerStatus powerStatus() {
  PowerStatus st;
  st.cpuMhz = (int)getCpuFrequencyMhz();
  wifi_ps_type_t ps = WIFI_PS_NONE;
  esp_wifi_get_ps(&ps);
  st.wifiSleep = ps == WIFI_PS_MIN_MODEM ? "min" : ps == WIFI_PS_MAX_MODEM ? "max" : "off";
  int8_t quarterDbm = 0;
  esp_wifi_get_max_tx_power(&quarterDbm);
  st.txDbm = quarterDbm / 4.0f;
  st.fallback = fallbackUntil != 0;
  st.fallbackMinutesLeft = st.fallback ? (int)((fallbackUntil - millis()) / 60000) + 1 : 0;
  st.meshNodes = meshNodes;
  st.bssid = isConnected ? WiFi.BSSIDstr() : String();
  st.channel = isConnected ? (int)WiFi.channel() : 0;
  st.chipTempC = temperatureRead();  // NAN on chips without a usable sensor
  return st;
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
  WiFi.setAutoReconnect(true);
#if NET_STATIC_IP
  if (!WiFi.config(IPAddress(NET_IP), IPAddress(NET_GATEWAY), IPAddress(NET_SUBNET), IPAddress(NET_DNS1),
                   IPAddress(NET_DNS2))) {
    LOGE("wifi", "static IP configuration failed");
  }
#endif
  applyPower();
  setTimezone(settings::current().timezone);
  LOGI("wifi", "connecting to \"%s\"...", SECRET_WIFI_SSID);
  led::set(led::CONNECTING);
  startWifi();
}

void loop() {
  if (gotIpEvent) {
    gotIpEvent = false;
    upSince = millis();
    updateTxPower(true);
    LOGI("wifi", "connected: ip %s, rssi %d dBm, channel %d, bssid %s", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(),
         (int)WiFi.channel(), WiFi.BSSIDstr().c_str());
    if (!servicesStarted) startServices();
  }
  if (disconnectEvent) {
    disconnectEvent = false;
    if (upSince != 0) {
      dropCount++;
      noteDrop();
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

  // Full power ends after the fall-back period; automatic transmit power follows the signal
  if (fallbackUntil != 0 && (int32_t)(millis() - fallbackUntil) >= 0) {
    fallbackUntil = 0;
    memset(recentDrops, 0, sizeof(recentDrops));
    LOGI("power", "fall-back over - power saving back on");
    applyPower();
  }
  if (millis() - lastTxCheck > 10000) {
    lastTxCheck = millis();
    if (settings::current().txPower == "auto") updateTxPower(false);
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
