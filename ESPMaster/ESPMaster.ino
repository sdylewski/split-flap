/* ####################################################################################################################### */
/* # ____  ____  _     ___ _____   _____ _        _    ____    _____ ____  ____    __  __    _    ____ _____ _____ ____  # */
/* #/ ___||  _ \| |   |_ _|_   _| |  ___| |      / \  |  _ \  | ____/ ___||  _ \  |  \/  |  / \  / ___|_   _| ____|  _ \ # */
/* #\___ \| |_) | |    | |  | |   | |_  | |     / _ \ | |_) | |  _| \___ \| |_) | | |\/| | / _ \ \___ \ | | |  _| | |_) |# */
/* # ___) |  __/| |___ | |  | |   |  _| | |___ / ___ \|  __/  | |___ ___) |  __/  | |  | |/ ___ \ ___) || | | |___|  _ < # */
/* #|____/|_|   |_____|___| |_|   |_|   |_____/_/   \_|_|     |_____|____/|_|     |_|  |_/_/   \_|____/ |_| |_____|_| \_\# */
/* ####################################################################################################################### */
/*
  This project project is done for fun as part of: https://github.com/JonnyBooker/split-flap
  None of this would be possible without the brilliant work of David Königsmann: https://github.com/Dave19171/split-flap

  Licensed under GNU: https://github.com/JonnyBooker/split-flap/blob/master/LICENSE
  
  Modified by Scott - Added LED debug codes and non-blocking operations for better web server responsiveness
*/

/* .--------------------------------------------------------------------------------. */
/* |  ___           __ _                    _    _       ___       __ _             | */
/* | / __|___ _ _  / _(_)__ _ _  _ _ _ __ _| |__| |___  |   \ ___ / _(_)_ _  ___ ___| */
/* || (__/ _ | ' \|  _| / _` | || | '_/ _` | '_ | / -_) | |) / -_|  _| | ' \/ -_(_-<| */
/* | \___\___|_||_|_| |_\__, |\_,_|_| \__,_|_.__|_\___| |___/\___|_| |_|_||_\___/__/| */
/* |                    |___/                                                       | */
/* '--------------------------------------------------------------------------------' */
/*
  These define statements can be changed as you desire for changing the functionality and
  behaviour of your device.
*/
#define SERIAL_ENABLE       false   //Option to enable serial debug messages
#define UNIT_CALLS_DISABLE  false   //Option to disable the call to the units so can just debug the ESP with no connections
#define OTA_ENABLE          false    //Option to enable OTA functionality
#define UNITS_AMOUNT        4       //Amount of connected units !IMPORTANT TO BE SET CORRECTLY!
#define SERIAL_BAUDRATE     57600  //Serial debugging BAUD rate
#define WIFI_USE_DIRECT     true   //Option to either direct connect to a WiFi Network or setup a AP to configure WiFi. Setting to false will setup as a AP.
#define ESP01S_LED_ENABLE   true   //Option to enable LED error indication on ESP-01S (set to false if not using ESP-01S or LED)
#define DEBUG_ENABLE true  //Enable debug features: startup debug page, error status panel, and serial debug log at bottom of page
#define PAGE_LOAD_DEBUG_ENABLE false  //Enable page load debug blocks: browser errors panel and page load debug log at top of page (set to true for troubleshooting page loading issues)
#define SHOW_UNIT_NUMBERS_ON_STARTUP false  //Display each unit's number (0, 1, 2, etc.) for 1 second on bootup to help identify units

/*
  EXPERIMENTAL: Try to use your Router when possible to set a Static IP address for your device to avoid conflicts with other devices
  on your network. This will try and setup your device with a static IP address of your chosing. See below for more details.
*/
#define WIFI_STATIC_IP      true
//#define FAE_MOD                   //Option for the modified PCB that includes an ESP-12F module

// LED Debug Pin Configuration for ESP-01S
// ESP-01S has an onboard blue LED on GPIO 2 (not on TX/RX lines), which is active LOW.
// This allows the LED to be used for error indication while keeping TX/RX lines
// dedicated to I2C communication with the split-flap units.
// Note: This feature only works with ESP-01S. The original ESP-01 has its LED on GPIO 1 (TX),
// which conflicts with I2C communication, so this LED feature is not supported on ESP-01.
#define LED_DEBUG_PIN 2  // GPIO 2 = onboard blue LED on ESP-01S
#define LED_ON LOW       // ESP-01S onboard LED is active LOW
#define LED_OFF HIGH

/*
  LED ERROR INDICATION:
  The LED will continuously blink when an error is detected (e.g., WiFi connection failed).
  This provides a simple visual indicator for critical errors.
*/

/* .--------------------------------------------------------. */
/* | ___         _               ___       __ _             | */
/* |/ __|_  _ __|_| ___ _ __   |   \ ___ / _(_)_ _  ___ ___| */
/* |\__ | || (_-|  _/ -_| '  \  | |) / -_|  _| | ' \/ -_(_-<| */
/* ||___/\_, /__/\__\___|_|_|_| |___/\___|_| |_|_||_\___/__/| */
/* |     |__/                                               | */
/* '--------------------------------------------------------' */
/*
  These are important to maintain normal system behaviour. Only change if you know 
  what your doing.
*/
#define ANSWER_SIZE         1       //Size of unit's request answer
#define FLAP_AMOUNT         45      //Amount of Flaps in each unit
#define MIN_SPEED           1       //Min Speed
#define MAX_SPEED           12      //Max Speed

/* .-----------------------------------. */
/* | _    _ _                 _        | */
/* || |  (_| |__ _ _ __ _ _ _(_)___ ___| */
/* || |__| | '_ | '_/ _` | '_| / -_(_-<| */
/* ||____|_|_.__|_| \__,_|_| |_\___/__/| */
/* '-----------------------------------' */
/*
  External library dependencies, not much more to say!
*/

//WiFi Setup Library if we use that mode
//Specifically put here in this order to avoid conflict with other libraries
#if WIFI_USE_DIRECT == false
//Needed in order to be compatible with WiFiManager: https://github.com/me-no-dev/ESPAsyncWebServer/issues/418#issuecomment-667976368
#define WEBSERVER_H
#include <WiFiManager.h>
#endif

//OTA Libary if we are into that kind of thing
#if OTA_ENABLE == true
#include <ArduinoOTA.h>
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <ESP8266WiFi.h>
#include <ezTime.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include "Classes.h"
#include "LittleFS.h"

/* .------------------------------------------------------------------------------------. */
/* |  ___           __ _                    _    _       ___     _   _   _              | */
/* | / __|___ _ _  / _(_)__ _ _  _ _ _ __ _| |__| |___  / __|___| |_| |_(_)_ _  __ _ ___| */
/* || (__/ _ | ' \|  _| / _` | || | '_/ _` | '_ | / -_) \__ / -_|  _|  _| | ' \/ _` (_-<| */
/* | \___\___|_||_|_| |_\__, |\_,_|_| \__,_|_.__|_\___| |___\___|\__|\__|_|_||_\__, /__/| */
/* |                    |___/                                                  |___/    | */
/* '------------------------------------------------------------------------------------' */
/*
  Settings you can feel free to change to customise how your display works.
*/
//Used if connecting via "WIFI_USE_DIRECT" of "true" - Otherwise, leave blank
const char* wifiDirectSsid = "Metolla at 8720";
const char* wifiDirectPassword = "brotheradso!";

//Change if you want to have an Over The Air (OTA) Password for updates
const char* otaPassword = "0424";

//Change this to your timezone, use the TZ database name
//https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
const char* timezoneString = "America/Los_Angeles";

//If you want to have a different date or clock format change these two
//Complete table with every char: https://github.com/ropg/ezTime#getting-date-and-time
const char* dateFormat = "d.m.Y"; //Examples: d.m.Y -> 11.09.2021, D M y -> SAT SEP 21
const char* clockFormat = "H:i"; //Examples: H:i -> 21:19, h:ia -> 09:19PM

//How long to show a message for when a scheduled message is shown for
const int scheduledMessageDisplayTimeMillis = 7500;

#if WIFI_STATIC_IP == true
//Static IP address for your device. Try take care to not conflict with something else on your network otherwise
//it is likely to not work
IPAddress wifiDeviceStaticIp(192, 168, 0, 111);

//Your router details
IPAddress wifiRouterGateway(192, 168, 0, 1);
IPAddress wifiSubnet(255, 255, 0, 0);

//DNS Entry. Default: Google DNS
IPAddress wifiPrimaryDns(8, 8, 8, 8);
#endif

/* .------------------------------------------------------------. */
/* | ___         _               ___     _   _   _              | */
/* |/ __|_  _ __|_| ___ _ __   / __|___| |_| |_(_)_ _  __ _ ___| */
/* |\__ | || (_-|  _/ -_| '  \  \__ / -_|  _|  _| | ' \/ _` (_-<| */
/* ||___/\_, /__/\__\___|_|_|_| |___\___|\__|\__|_|_||_\__, /__/| */
/* |     |__/                                          |___/    | */
/* '------------------------------------------------------------' */
/*
  Used for normal running of the system so changing things here might make things 
  behave a little strange.
*/
//The current version of code to display on the UI
const char* espVersion = "3.0.0";

//All the letters on the units that we have to be displayed. You can change these if it so pleases at your own risk
const char letters[] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '$', '&', '#', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', '.', '-', '?', '!'};
int displayState[UNITS_AMOUNT];
int connectedUnitCount = 0; // Number of units actually detected on I2C bus
unsigned long previousMillis = 0;

//Search for parameter in HTTP POST request
const char* PARAM_ALIGNMENT = "alignment";
const char* PARAM_FLAP_SPEED = "flapSpeed";
const char* PARAM_DEVICEMODE = "deviceMode";
const char* PARAM_INPUT_TEXT = "inputText";
const char* PARAM_SCHEDULE_ENABLED = "scheduleEnabled";
const char* PARAM_SCHEDULE_DATE_TIME = "scheduledDateTimeUnix";
const char* PARAM_SCHEDULE_SHOW_INDEFINITELY = "scheduleShowIndefinitely";
const char* PARAM_COUNTDOWN_DATE = "countdownDateTimeUnix";
const char* PARAM_ID = "id";

//Device Modes
const char* DEVICE_MODE_TEXT = "text";
const char* DEVICE_MODE_CLOCK = "clock";
const char* DEVICE_MODE_DATE = "date";
const char* DEVICE_MODE_COUNTDOWN = "countdown";

//Alignment options
const char* ALIGNMENT_MODE_LEFT = "left";
const char* ALIGNMENT_MODE_CENTER = "center";
const char* ALIGNMENT_MODE_RIGHT = "right";

//File paths to save input values permanently
const char* alignmentPath = "/alignment.txt";
const char* flapSpeedPath = "/flapspeed.txt";
const char* deviceModePath = "/devicemode.txt";
const char* countdownPath = "/countdown.txt";
const char* scheduledMessagesPath = "/scheduled-messages.txt";
const char* debugModePath = "/debugmode.txt";

//Variables for storing things for checking and use in normal running
String alignment = "";
String flapSpeed = "";
String inputText = "";
String deviceMode = "";
String countdownToDateUnix = "";
String lastWrittenText = "";
String lastReceivedMessageDateTime = "";
bool alignmentUpdated = false;
bool isPendingReboot = false;
bool isPendingUnitsReset = false;
bool isWifiConfigured = false;
bool showDebugPage = true; // Default to showing debug page on first boot
volatile bool webRequestActive = false; // Flag to skip display updates during web requests
LList<ScheduledMessage> scheduledMessages;
Timezone timezone; 

//Create AsyncWebServer object on port 80
AsyncWebServer webServer(80);

//Used for creating a Access Point to allow WiFi setup
#if WIFI_USE_DIRECT == false
WiFiManager wifiManager;
bool isPendingWifiReset = false;
#endif

//Used to denote that the system has gone into OTA mode
#if OTA_ENABLE == true
bool isInOtaMode = false;
#endif

// LED Debugging variables
String debugStatus = "";

// Serial log buffer for web interface (circular buffer, stores last 100 messages)
#define SERIAL_LOG_SIZE 100
struct SerialLogEntry {
  String message;
  unsigned long timestamp;
};
SerialLogEntry serialLog[SERIAL_LOG_SIZE];
int serialLogIndex = 0;
int serialLogCount = 0;
String currentSerialLine = ""; // Buffer for building complete lines

// Function to add message to serial log buffer
void addToSerialLog(String message) {
  if (message.length() == 0) return; // Don't log empty messages
  
  // Add timestamp
  unsigned long timestamp = millis();
  
  // Store in circular buffer
  serialLog[serialLogIndex].message = message;
  serialLog[serialLogIndex].timestamp = timestamp;
  
  // Update indices
  serialLogIndex = (serialLogIndex + 1) % SERIAL_LOG_SIZE;
  if (serialLogCount < SERIAL_LOG_SIZE) {
    serialLogCount++;
  }
}

/* .-----------------------------------------------. */
/* | ___          _          ___     _             | */
/* ||   \ _____ _(_)__ ___  / __|___| |_ _  _ _ __ | */
/* || |) / -_\ V | / _/ -_) \__ / -_|  _| || | '_ \| */
/* ||___/\___|\_/|_\__\___| |___\___|\__|\_,_| .__/| */
/* |                                         |_|   | */
/* '-----------------------------------------------' */

// LED Error Indication Functions
// Simple continuous blink pattern to indicate errors

void ledOn() {
#if ESP01S_LED_ENABLE == true
  digitalWrite(LED_DEBUG_PIN, LED_ON);
#endif
}

void ledOff() {
#if ESP01S_LED_ENABLE == true
  digitalWrite(LED_DEBUG_PIN, LED_OFF);
#endif
}

void continuousBlink(int duration) {
#if ESP01S_LED_ENABLE == true
  // Blink continuously for the specified duration (in milliseconds)
  unsigned long startTime = millis();
  bool ledState = false;
  while ((millis() - startTime) < duration) {
    if (ledState) {
      ledOn();
    } else {
      ledOff();
    }
    ledState = !ledState;
    delay(100); // 100ms on, 100ms off = 5 Hz blink rate
  }
  ledOff(); // Ensure LED is off when done
#endif
}

void setup() {
#if SERIAL_ENABLE == true
  //Setup so we can see serial messages
  Serial.begin(SERIAL_BAUDRATE);
#elif !defined(FAE_MOD)
  //For ESP01 only
  Wire.begin(1, 3); 
  
  //De-activate I2C if debugging the ESP, otherwise serial does not work
  //Wire.begin(D1, D2); //For NodeMCU testing only SDA=D1 and SCL=D2
#endif

#ifdef FAE_MOD
  //Fae Mod
  Wire.begin(4, 5);
#endif

  // Initialize LED for error indication
#if ESP01S_LED_ENABLE == true
  pinMode(LED_DEBUG_PIN, OUTPUT);
  ledOff();
#endif

  // I2C bus scan on startup
  delay(500); // Give I2C bus time to stabilize
  SerialPrintln("");
  SerialPrintln("=== I2C Bus Scan on Startup ===");
  scanI2CBus();
  SerialPrintln("");

  SerialPrintln("");
  SerialPrintln("#######################################################");
  SerialPrintln("..............Split Flap Display Starting..............");
  SerialPrintln("#######################################################");
  debugStatus = "Starting";
  SerialPrintln("DEBUG: Status = " + debugStatus);

  //Load and read all the things
  debugStatus = "WiFi Init";
  SerialPrintln("DEBUG: Status = " + debugStatus);
  initWiFi();
  
  //Helpful if want to force reset WiFi settings for testing
  //wifiManager.resetSettings();

  if (isWifiConfigured && !isPendingReboot) {
    debugStatus = "WiFi Connected";
    SerialPrintln("DEBUG: Status = " + debugStatus);
    
    //ezTime initialization - NON-BLOCKING with timeout
    debugStatus = "NTP Sync Starting";
    SerialPrintln("DEBUG: Status = " + debugStatus);
    
    // Set sync interval but don't block
    setInterval(60); // Sync every 60 seconds
    setDebug(INFO); // Set to INFO level for debugging
    
    // Try to sync with timeout (non-blocking)
    unsigned long ntpStartTime = millis();
    unsigned long ntpTimeout = 30000; // 30 second timeout
    
    SerialPrintln("DEBUG: Starting NTP sync (non-blocking, 30s timeout)");
    
    // Wait for sync with timeout and yield
    while (timeStatus() == timeNotSet && (millis() - ntpStartTime) < ntpTimeout) {
      events(); // Process ezTime events
      yield();  // Allow other tasks to run
      delay(100);
    }
    
    if (timeStatus() == timeSet) {
      debugStatus = "NTP Sync Success";
      SerialPrintln("DEBUG: Status = " + debugStatus);
      SerialPrintln("DEBUG: NTP sync successful!");
    } else {
      debugStatus = "NTP Sync Failed";
      SerialPrintln("DEBUG: Status = " + debugStatus);
      SerialPrintln("DEBUG: WARNING - NTP sync failed or timed out, continuing anyway");
    }
    
    timezone.setLocation(timezoneString);
    SerialPrintln("DEBUG: Timezone set to: " + String(timezoneString));
    
    //Load various variables
    debugStatus = "File System Init";
    SerialPrintln("DEBUG: Status = " + debugStatus);
    initialiseFileSystem();
    loadValuesFromFileSystem();
    
    // Load debug mode setting
#if DEBUG_ENABLE == true
    String debugModeSetting = readFile(LittleFS, debugModePath, "true");
    showDebugPage = (debugModeSetting == "true");
    SerialPrintln("DEBUG: Debug mode: " + String(showDebugPage ? "enabled" : "disabled") + " (controls startup page, error status, page load log, and serial log)");
#endif

#if OTA_ENABLE == true
    SerialPrintln("OTA is enabled! Yay!");
#endif

    //Web Server Endpoint configuration
    debugStatus = "Web Server Setup";
    SerialPrintln("DEBUG: Status = " + debugStatus);
    
    webServer.serveStatic("/", LittleFS, "/");
    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      unsigned long pageRequestStart = millis();
      SerialPrintln("DEBUG: Request Home Page Received");
      
#if DEBUG_ENABLE == true
      // If debug mode is enabled and we're still in debug mode, show debug page
      if (showDebugPage) {
        // Generate debug HTML page with live log
        IPAddress ip = WiFi.localIP();
        String html = "<!DOCTYPE html><html><head>";
        html += "<title>Split Flap - Debug Mode</title>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<style>";
        html += "body { font-family: Arial, sans-serif; margin: 20px; background: #1e1e1e; color: #d4d4d4; }";
        html += "h1 { color: #4ec9b0; }";
        html += ".log-container { background: #252526; border: 1px solid #3e3e42; padding: 15px; font-family: 'Courier New', monospace; font-size: 0.9em; white-space: pre-wrap; }";
        html += ".log-entry { margin: 2px 0; }";
        html += ".timestamp { color: #808080; margin-right: 10px; }";
        html += ".debug { color: #4ec9b0; }";
        html += ".error { color: #f48771; }";
        html += ".warning { color: #dcdcaa; }";
        html += "button { background: #0e639c; color: white; border: none; padding: 10px 20px; font-size: 16px; cursor: pointer; border-radius: 4px; margin-bottom: 20px; }";
        html += "button:hover { background: #1177bb; }";
        html += ".status { color: #4ec9b0; font-weight: bold; margin: 10px 0; }";
        html += "</style>";
        html += "</head><body>";
        html += "<h1>Split Flap - Debug Mode</h1>";
        html += "<button onclick='continueToNormal()'>Continue to Normal Mode</button>";
        html += "<div class='status'>Current Status: " + debugStatus + "</div>";
        html += "<p>Debug mode is enabled. This page shows the initialization log. When you continue to normal mode, you'll also see error status panel, page load debug log, and serial debug log features.</p>";
        html += "<div class='log-container' id='logContainer'>";
        html += "<div style='color: #888;'>Loading log...</div>";
        html += "</div>";
        html += "<script>";
        html += "function loadLog() {";
        html += "  var xhr = new XMLHttpRequest();";
        html += "  xhr.onreadystatechange = function() {";
        html += "    if (this.readyState == 4) {";
        html += "      var container = document.getElementById('logContainer');";
        html += "      if (this.status == 200) {";
        html += "        try {";
        html += "          var data = JSON.parse(this.responseText);";
        html += "          var html = '';";
        html += "          if (data.logs && data.logs.length > 0) {";
        html += "            for (var i = 0; i < data.logs.length; i++) {";
        html += "              var entry = data.logs[i];";
        html += "              var msg = entry.message || '';";
        html += "              var timestamp = (entry.timestamp / 1000).toFixed(1) + 's';";
        html += "              var colorClass = '';";
        html += "              if (msg.indexOf('DEBUG:') >= 0) colorClass = 'debug';";
        html += "              else if (msg.indexOf('ERROR') >= 0 || msg.indexOf('Error') >= 0) colorClass = 'error';";
        html += "              else if (msg.indexOf('WARNING') >= 0 || msg.indexOf('Warning') >= 0) colorClass = 'warning';";
        html += "              html += '<div class=\"log-entry\"><span class=\"timestamp\">[' + timestamp + ']</span><span class=\"' + colorClass + '\">' + escapeHtml(msg) + '</span></div>';";
        html += "            }";
        html += "          } else {";
        html += "            html = '<div style=\"color: #888;\">No log messages yet. (Count: ' + (data.count || 0) + ')</div>';";
        html += "          }";
        html += "          container.innerHTML = html;";
        html += "        } catch (e) {";
        html += "          container.innerHTML = '<div style=\"color: #f48771;\">Error parsing log: ' + e.message + '<br>Response: ' + escapeHtml(this.responseText.substring(0, 200)) + '</div>';";
        html += "        }";
        html += "      } else {";
        html += "        container.innerHTML = '<div style=\"color: #f48771;\">Error loading log: Status ' + this.status + '</div>';";
        html += "      }";
        html += "    }";
        html += "  };";
        html += "  xhr.onerror = function() {";
        html += "    var container = document.getElementById('logContainer');";
        html += "    container.innerHTML = '<div style=\"color: #f48771;\">Network error loading log</div>';";
        html += "  };";
        html += "  xhr.open('GET', '/log', true);";
        html += "  xhr.send();";
        html += "}";
        html += "function escapeHtml(text) {";
        html += "  var map = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '\"': '&quot;', \"'\": '&#039;' };";
        html += "  return text.replace(/[&<>\"']/g, function(m) { return map[m]; });";
        html += "}";
        html += "function continueToNormal() {";
        html += "  var xhr = new XMLHttpRequest();";
        html += "  xhr.onreadystatechange = function() {";
        html += "    if (this.readyState == 4 && this.status == 200) {";
        html += "      window.location.href = '/';";
        html += "    }";
        html += "  };";
        html += "  xhr.open('GET', '/exit-debug-mode', true);";
        html += "  xhr.send();";
        html += "}";
        html += "loadLog();";
        html += "setInterval(loadLog, 1000);"; // Auto-refresh every second
        html += "</script>";
        html += "</body></html>";
        
        request->send(200, "text/html", html);
        return;
      }
#endif
      // Normal mode - serve regular index.html
      unsigned long beforeSend = millis();
      SerialPrintln("DEBUG: Serving index.html (took " + String(beforeSend - pageRequestStart) + "ms to get here)");
      request->send(LittleFS, "/index.html", "text/html");
      SerialPrintln("DEBUG: index.html sent (total: " + String(millis() - pageRequestStart) + "ms)");
    });

    webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest * request) {
      unsigned long settingsStart = millis();
      SerialPrintln("DEBUG: Request for Settings Received at " + String(settingsStart) + "ms");
      
      // Set flag to prevent display updates during web request
      webRequestActive = true;
      
      // Build minimal response immediately (fast path)
      JsonDocument minimalDoc;
      minimalDoc["timezoneOffset"] = timezone.getOffset();
      minimalDoc["unitCount"] = UNITS_AMOUNT;
      minimalDoc["connectedUnitCount"] = connectedUnitCount;
      minimalDoc["alignment"] = alignment;
      minimalDoc["flapSpeed"] = flapSpeed;
      minimalDoc["deviceMode"] = deviceMode;
      minimalDoc["version"] = espVersion;
      minimalDoc["lastTimeReceivedMessageDateTime"] = lastReceivedMessageDateTime;
      minimalDoc["lastWrittenText"] = lastWrittenText;
      minimalDoc["countdownToDateUnix"] = atol(countdownToDateUnix.c_str());
      minimalDoc["wifiStatus"] = WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected";
      minimalDoc["wifiRssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
      minimalDoc["wifiIp"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
      minimalDoc["scheduledMessages"] = JsonArray();
      minimalDoc["wifiSettingsResettable"] = true;
      minimalDoc["otaEnabled"] = false;
      
      // Try to get scheduled messages quickly (with timeout)
      unsigned long beforeScheduled = millis();
      int scheduledCount = scheduledMessages.size();
      if (scheduledCount > 0 && (millis() - settingsStart) < 2000) { // Only if we have time
        for(int i = 0; i < scheduledCount && (millis() - beforeScheduled) < 1000; i++) {
          ScheduledMessage msg = scheduledMessages[i];
          minimalDoc["scheduledMessages"][i]["scheduledDateTimeUnix"] = msg.ScheduledDateTimeUnix;
          minimalDoc["scheduledMessages"][i]["message"] = msg.Message;
          minimalDoc["scheduledMessages"][i]["showIndefinitely"] = msg.ShowIndefinitely;
          yield();
        }
      }
      
      String minimalJson;
      serializeJson(minimalDoc, minimalJson);
      
      unsigned long totalTime = millis() - settingsStart;
      SerialPrintln("DEBUG: Settings response ready in " + String(totalTime) + "ms, JSON size: " + String(minimalJson.length()) + " bytes");
      
      request->send(200, "application/json", minimalJson);
      minimalJson = String();
      
      SerialPrintln("DEBUG: Settings response sent (total: " + String(millis() - settingsStart) + "ms)");
      
      // Clear flag after request completes
      webRequestActive = false;
    });
    
    webServer.on("/health", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("DEBUG: Request for Health Check Received");
      request->send(200, "text/plain", "Healthy");
    });
    
    // Simple test endpoint that responds immediately
    webServer.on("/test", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("DEBUG: Test endpoint called");
      request->send(200, "application/json", "{\"status\":\"ok\",\"time\":" + String(millis()) + "}");
    });
    
    webServer.on("/log", HTTP_GET, [](AsyncWebServerRequest * request) {
      // Use larger JSON document to handle many log entries (100 entries * ~200 bytes each = ~20KB)
      StaticJsonDocument<25000> document;
      document["count"] = serialLogCount;
      
      int startIndex = serialLogCount < SERIAL_LOG_SIZE ? 0 : serialLogIndex;
      int entriesToReturn = serialLogCount < SERIAL_LOG_SIZE ? serialLogCount : SERIAL_LOG_SIZE;
      
      // Limit to last 50 entries to prevent JSON buffer overflow
      int maxEntries = entriesToReturn > 50 ? 50 : entriesToReturn;
      int actualStart = entriesToReturn > 50 ? (startIndex + entriesToReturn - 50) % SERIAL_LOG_SIZE : startIndex;
      
      for (int i = 0; i < maxEntries; i++) {
        int idx = (actualStart + i) % SERIAL_LOG_SIZE;
        String msg = serialLog[idx].message;
        
        // Truncate very long messages to prevent JSON issues (max 500 chars)
        if (msg.length() > 500) {
          msg = msg.substring(0, 497) + "...";
        }
        
        document["logs"][i]["message"] = msg;
        document["logs"][i]["timestamp"] = serialLog[idx].timestamp;
      }
      
      String jsonString;
      serializeJson(document, jsonString);
      
      // Check if serialization succeeded
      if (jsonString.length() == 0) {
        SerialPrintln("ERROR: JSON serialization failed for /log endpoint");
        request->send(500, "application/json", "{\"error\":\"Serialization failed\"}");
        return;
      }
      
      request->send(200, "application/json", jsonString);
    });
    
    // I2C bus scanner endpoint for diagnostics
    webServer.on("/i2c-scan", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("DEBUG: I2C scan requested");
      JsonDocument doc;
      doc["scanTime"] = millis();
      
      int foundCount = 0;
      for (int address = 0; address < 16; address++) {
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();
        
        if (error == 0) {
          doc["devices"][foundCount]["address"] = address;
          
          // Try to read status
          Wire.requestFrom(address, 1, 1);
          if (Wire.available()) {
            int status = Wire.read();
            doc["devices"][foundCount]["status"] = status;
            if (status == 0) {
              doc["devices"][foundCount]["statusText"] = "ready";
            } else if (status == 1) {
              doc["devices"][foundCount]["statusText"] = "busy";
            } else {
              doc["devices"][foundCount]["statusText"] = "unknown";
            }
          } else {
            doc["devices"][foundCount]["status"] = -1;
            doc["devices"][foundCount]["statusText"] = "no response";
          }
          foundCount++;
        }
        
        yield();
        delay(10);
      }
      
      doc["foundCount"] = foundCount;
      doc["expectedCount"] = UNITS_AMOUNT;
      
      String jsonString;
      serializeJson(doc, jsonString);
      request->send(200, "application/json", jsonString);
    });
    
    // Detailed unit status endpoint - check a specific unit multiple times
    webServer.on("/unit-status", HTTP_GET, [](AsyncWebServerRequest * request) {
      int unitAddress = -1;
      if (request->hasParam("unit")) {
        unitAddress = request->getParam("unit")->value().toInt();
      }
      
      if (unitAddress < 0 || unitAddress >= UNITS_AMOUNT) {
        request->send(400, "application/json", "{\"error\":\"Invalid unit address. Use ?unit=0 to " + String(UNITS_AMOUNT - 1) + "\"}");
        return;
      }
      
      SerialPrint("DEBUG: Detailed status check requested for unit ");
      SerialPrintln(unitAddress);
      
      JsonDocument doc;
      doc["unit"] = unitAddress;
      doc["checkTime"] = millis();
      
      // Check I2C connection first (quick check)
      Wire.beginTransmission(unitAddress);
      byte i2cError = Wire.endTransmission();
      doc["i2cError"] = i2cError;
      
      if (i2cError == 0) {
        doc["i2cConnected"] = true;
        
        // Check status fewer times (3 instead of 5) with shorter delays to avoid timeout
        // Also add timeout protection for each check
        int statusReadings[3];
        unsigned long statusCheckStart = millis();
        const unsigned long MAX_STATUS_CHECK_TIME = 5000; // 5 second max for all checks
        
        for (int i = 0; i < 3; i++) {
          unsigned long checkStart = millis();
          
          // Try to read status with timeout protection
          statusReadings[i] = -2; // Default to "no response"
          
          // Quick I2C request with minimal blocking
          Wire.requestFrom(unitAddress, ANSWER_SIZE, 1);
          
          // Wait for response but with timeout
          unsigned long waitStart = millis();
          while (!Wire.available() && (millis() - waitStart < 500)) {
            yield(); // Allow web server to process
            delay(10);
          }
          
          if (Wire.available()) {
            statusReadings[i] = Wire.read();
          } else {
            // Timeout - unit not responding quickly
            SerialPrint("WARNING: Unit ");
            SerialPrint(unitAddress);
            SerialPrintln(" status check timed out");
            statusReadings[i] = -2; // No response
          }
          
          // Check if we're taking too long overall
          if (millis() - statusCheckStart > MAX_STATUS_CHECK_TIME) {
            SerialPrintln("WARNING: Status check taking too long, stopping early");
            // Fill remaining with -2 (no response)
            for (int j = i + 1; j < 3; j++) {
              statusReadings[j] = -2;
            }
            break;
          }
          
          yield(); // Allow web server to process
          if (i < 2) { // Don't delay after last reading
            delay(100); // Reduced from 200ms to 100ms
          }
        }
        
        // Add readings to JSON
        for (int i = 0; i < 3; i++) {
          doc["statusReadings"][i] = statusReadings[i];
        }
        
        // Analyze readings (ignore -2 "no response" values in analysis)
        bool allSame = true;
        int firstValidStatus = -3;
        int validStatusCount = 0;
        
        for (int i = 0; i < 3; i++) {
          if (statusReadings[i] != -2) { // -2 means no response/timeout
            if (firstValidStatus == -3) {
              firstValidStatus = statusReadings[i];
            }
            validStatusCount++;
            if (statusReadings[i] != firstValidStatus) {
              allSame = false;
            }
          }
        }
        
        doc["allReadingsSame"] = allSame;
        doc["validStatusCount"] = validStatusCount;
        
        if (validStatusCount == 0) {
          doc["consistentStatus"] = -2;
          doc["statusText"] = "no response";
          doc["diagnosis"] = "Unit is not responding to I2C status requests. Unit may be stuck, sleeping, or having communication issues.";
          doc["suggestedFix"] = "Check I2C wiring, power, and DIP switch settings. Try resetting the unit or power cycling.";
        } else {
          doc["consistentStatus"] = firstValidStatus;
          
          if (allSame && firstValidStatus == 1) {
            doc["diagnosis"] = "Unit is stuck in BUSY/MOVING state. The motor may be physically stuck, Hall sensor not detecting home, or calibration failed.";
            doc["suggestedFix"] = "Try 'Reset/Home Unit' button to force calibration. If that doesn't work, check: motor can rotate freely, Hall sensor wiring, magnet alignment, and calibration offset.";
          } else if (allSame && firstValidStatus == -1) {
            doc["diagnosis"] = "Unit is sleeping or not responding to I2C requests.";
            doc["suggestedFix"] = "Unit should wake up automatically when sent a command. Try 'Reset/Home Unit' button.";
          } else if (allSame && firstValidStatus == 0) {
            doc["diagnosis"] = "Unit is ready and not moving.";
          } else {
            doc["diagnosis"] = "Unit status is inconsistent - may be transitioning between states or having communication issues.";
          }
          
          // Status text
          if (firstValidStatus == 0) {
            doc["statusText"] = "ready";
          } else if (firstValidStatus == 1) {
            doc["statusText"] = "busy/moving";
          } else if (firstValidStatus == -1) {
            doc["statusText"] = "sleeping";
          } else {
            doc["statusText"] = "unknown";
          }
        }
      } else {
        doc["i2cConnected"] = false;
        doc["diagnosis"] = "Unit not found on I2C bus. Check DIP switch settings, wiring, and power.";
        if (i2cError == 2) {
          doc["i2cErrorText"] = "Address NACK (device not found)";
        } else if (i2cError == 3) {
          doc["i2cErrorText"] = "Data NACK";
        } else if (i2cError == 4) {
          doc["i2cErrorText"] = "Unknown I2C error";
        } else if (i2cError == 5) {
          doc["i2cErrorText"] = "Timeout";
        }
      }
      
      String jsonString;
      serializeJson(doc, jsonString);
      request->send(200, "application/json", jsonString);
    });
    
    // Force unit to home/calibrate by sending it to position 0
    webServer.on("/unit-reset", HTTP_GET, [](AsyncWebServerRequest * request) {
      int unitAddress = -1;
      if (request->hasParam("unit")) {
        unitAddress = request->getParam("unit")->value().toInt();
      }
      
      if (unitAddress < 0 || unitAddress >= UNITS_AMOUNT) {
        request->send(400, "application/json", "{\"error\":\"Invalid unit address. Use ?unit=0 to " + String(UNITS_AMOUNT - 1) + "\"}");
        return;
      }
      
      SerialPrint("DEBUG: Reset/home command requested for unit ");
      SerialPrintln(unitAddress);
      
      // Check I2C connection first
      Wire.beginTransmission(unitAddress);
      byte i2cError = Wire.endTransmission();
      
      JsonDocument doc;
      doc["unit"] = unitAddress;
      doc["action"] = "reset";
      
      if (i2cError != 0) {
        doc["error"] = "Unit not found on I2C bus";
        doc["i2cError"] = i2cError;
        String jsonString;
        serializeJson(doc, jsonString);
        request->send(200, "application/json", jsonString);
        return;
      }
      
      // Send unit to position 0 (space) with default speed
      // This should trigger calibration if the unit needs a full rotation
      writeToUnit(unitAddress, 0, 80); // Position 0 = space, speed 80
      
      // Verify the command was sent by checking I2C error
      Wire.beginTransmission(unitAddress);
      byte verifyError = Wire.endTransmission();
      
      if (verifyError == 0) {
        doc["message"] = "Reset command sent successfully. Unit should now move to position 0 (space) and calibrate.";
        doc["note"] = "This may take 10-30 seconds. Check unit status after a few seconds to see progress.";
        doc["success"] = true;
      } else {
        doc["message"] = "Command sent but unit may not be responding properly.";
        doc["i2cError"] = verifyError;
        doc["success"] = false;
      }
      
      String jsonString;
      serializeJson(doc, jsonString);
      request->send(200, "application/json", jsonString);
    });
    
#if DEBUG_ENABLE == true
    webServer.on("/exit-debug-mode", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("DEBUG: Request to exit debug mode received");
      showDebugPage = false;
      writeFile(LittleFS, debugModePath, "false");
      request->send(200, "text/plain", "OK - Debug mode disabled. Startup debug page, error status, and debug logs will be hidden.");
    });
    
    webServer.on("/enable-debug-mode", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("DEBUG: Request to enable debug mode received");
      showDebugPage = true;
      writeFile(LittleFS, debugModePath, "true");
      request->send(200, "text/plain", "OK - Debug mode enabled. This enables: startup debug page, error status panel, page load debug log, and serial debug log. Refresh page to see changes.");
    });
#endif
    
    webServer.on("/reboot", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request to Reboot Received");
      
      //Create HTML page to explain the system is rebooting
      IPAddress ip = WiFi.localIP();
      
      String html = "<div style='text-align:center'>";
      html += "<font face='arial'><h1>Split Flap - Rebooting</h1>";
      html += "<p>Reboot is pending now...<p>";
      html += "<p>This can take anywhere between 10-20 seconds<p>";
      html += "<p>You can go to the main home page after this time by clicking the button below or going to '/'.</p>";
      html += "<p><a href=\"http://" + ip.toString() + "\">Home</a></p>";
      html += "</font>";
      html += "</div>";
      
      request->send(200, "text/html", html);
      isPendingReboot = true;
    });
    
    webServer.on("/reset-units", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request to Reset Units Received");
      
      //This will be picked up in the loop
      isPendingUnitsReset = true;
      
      request->redirect("/?is-resetting-units=true");
    });

    webServer.on("/scheduled-message/remove", HTTP_DELETE, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request to Remove Scheduled Message Received");
      
      if (request->hasParam(PARAM_ID)) {
        bool removedScheduledMessage = false;
        String idValue = request->getParam(PARAM_ID)->value();

        if (isNumber(idValue)) {
          long parsedIdValue = atol(idValue.c_str());
          bool removed = removeScheduledMessage(parsedIdValue);
          
          if (removed) {
            request->send(202, "text/plain", "Removed");
          }
          else {
            request->send(400, "text/plain", "Unable to find message with ID specified. Id: " + idValue);
          }
        }
        else {
          SerialPrintln("Invalid Delete Scheduled Message ID Received");
          request->send(400, "text/plain", "Invalid ID value");
        }
      } 
      else {
          SerialPrintln("Delete Scheduled Message Received with no ID");
          request->send(400, "text/plain", "No ID specified");
      }
    });

    webServer.on("/", HTTP_POST, [](AsyncWebServerRequest * request) {
      SerialPrintln("DEBUG: Request Post of Form Received");    

      bool submissionError = false;
      
      bool newMessageScheduleEnabledValue, newMessageScheduleShowIndefinitely = false;
      long newMessageScheduleDateTimeUnixValue = -1;
      String newAlignmentValue, newDeviceModeValue, newFlapSpeedValue, newInputTextValue, newCountdownToDateUnixValue = "";
      bool hasNewInputTextValue = false;
      
      int params = request->params();
      for (int paramIndex = 0; paramIndex < params; paramIndex++) {
        AsyncWebParameter* p = request->getParam(paramIndex);
        if (p->isPost()) {
          //HTTP POST alignment value
          if (p->name() == PARAM_ALIGNMENT) {
            String receivedValue = p->value();
            if (receivedValue == ALIGNMENT_MODE_LEFT || receivedValue == ALIGNMENT_MODE_CENTER || receivedValue == ALIGNMENT_MODE_RIGHT) {
              newAlignmentValue = receivedValue;
            }
            else {
              SerialPrintln("Alignment provided was not valid. Value: " + receivedValue); 
              submissionError = true;
            }
          }

          //HTTP POST device mode value
          if (p->name() == PARAM_DEVICEMODE) {
            String receivedValue = p->value();
            if (receivedValue == DEVICE_MODE_TEXT || receivedValue == DEVICE_MODE_CLOCK || receivedValue == DEVICE_MODE_DATE || receivedValue == DEVICE_MODE_COUNTDOWN) {
              newDeviceModeValue = receivedValue;          
            }
            else {
              SerialPrintln("Device Mode provided was not valid. Invalid Value: " + receivedValue); 
              submissionError = true;
            }
          }

          //HTTP POST Flap Speed Slider value
          if (p->name() == PARAM_FLAP_SPEED) {
            newFlapSpeedValue = p->value().c_str();
          }

          //HTTP POST inputText value
          if (p->name() == PARAM_INPUT_TEXT) {
            newInputTextValue = p->value().c_str();
            hasNewInputTextValue = true;
          }

          //HTTP POST Schedule Enabled
          if (p->name() == PARAM_SCHEDULE_ENABLED) {
            String newMessageScheduleEnabledString = p->value().c_str();
            newMessageScheduleEnabledValue = newMessageScheduleEnabledString == "on" ?
              true : 
              false;
          }
          
          //HTTP POST Schedule Show Indefinitely
          if (p->name() == PARAM_SCHEDULE_SHOW_INDEFINITELY) {
            String newMessageScheduleShowIndefinitelyString = p->value().c_str();
            newMessageScheduleShowIndefinitely = newMessageScheduleShowIndefinitelyString == "on" ?
              true : 
              false;
          }

          //HTTP POST Schedule Seconds
          if (p->name() == PARAM_SCHEDULE_DATE_TIME) {
            String receivedValue = p->value().c_str();
            if (isNumber(receivedValue)) {
              newMessageScheduleDateTimeUnixValue = atol(receivedValue.c_str());
            }
            else {
              SerialPrintln("Schedule date time provided was not valid. Invalid Value: " + receivedValue); 
              submissionError = true;
            }
          }

          //HTTP POST Countdown Seconds
          if (p->name() == PARAM_COUNTDOWN_DATE) {
            String receivedValue = p->value().c_str();
            if (isNumber(receivedValue)) {
              newCountdownToDateUnixValue = receivedValue;
            }
            else {
              SerialPrintln("Countdown date provided was not valid. Invalid Value: " + receivedValue); 
              submissionError = true;
            }
          }
        }
      }    

      //If there was an error, report back to check what has been input
      if (submissionError) {
        SerialPrintln("Finished Processing Request with Error");
        request->redirect("/?invalid-submission=" + true);
      }
      else {
        SerialPrintln("Finished Processing Request Successfully");

        lastReceivedMessageDateTime = timezone.dateTime("d M y H:i:s");

        //Only if a new alignment value
        if (newAlignmentValue != "" && alignment != newAlignmentValue) {
          alignment = newAlignmentValue;
          alignmentUpdated = true;

          writeFile(LittleFS, alignmentPath, alignment.c_str());
          SerialPrintln("Alignment Updated: " + alignment);
        }

        //Only if a new flap speed value
        if (newFlapSpeedValue != "" && flapSpeed != newFlapSpeedValue) {
          flapSpeed = newFlapSpeedValue;

          writeFile(LittleFS, flapSpeedPath, flapSpeed.c_str());
          SerialPrintln("Flap Speed Updated: " + flapSpeed);
        }

        //Only if countdown date has changed
        if (newCountdownToDateUnixValue != "" && countdownToDateUnix != newCountdownToDateUnixValue) {
          countdownToDateUnix = newCountdownToDateUnixValue;

          writeFile(LittleFS, countdownPath, countdownToDateUnix.c_str());
          SerialPrintln("Countdown Date Time Unix Updated: " + countdownToDateUnix);
        }

        //If its a new scheduled message, add it to the backlog and proceed, don't want to change device mode
        //Else, we do want to change the device mode and clear out the input text
        if (newMessageScheduleEnabledValue) {
          addAndPersistScheduledMessage(newInputTextValue, newMessageScheduleDateTimeUnixValue, newMessageScheduleShowIndefinitely);
          SerialPrintln("New Scheduled Message added");
        }
        else {
          //Only if device mode has changed
          if (newDeviceModeValue != "" && deviceMode != newDeviceModeValue) {
            deviceMode = newDeviceModeValue;

            writeFile(LittleFS, deviceModePath, deviceMode.c_str());
            SerialPrintln("Device Mode Set: " + deviceMode);
          }

          //Only if we are showing text
          if (hasNewInputTextValue && deviceMode == DEVICE_MODE_TEXT) {
            inputText = newInputTextValue;
          }
        }

        //Redirect so that we don't have the "re-submit form" problem in browser for refresh
        request->redirect("/");
      }
    });

#if OTA_ENABLE == true
    webServer.on("/ota", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request to start OTA mode received");
      
      //Create HTML page to explain OTA
      IPAddress ip = WiFi.localIP();
      
      String html = "<div style='text-align:center'>";
      html += "<font face='arial'><h1>Split Flap - OTA Update Mode</h1>";
      html += "<p>OTA mode has been started. You can now update your module via WiFI. Open your Arduino IDE and select the new port in \"Tools\" menu and upload the your sketch as normal!<p>";
      html += "<p>Open your Arduino IDE and select the new port in \"Tools\" menu and upload the your sketch as normal!</p>";
      html += "<p>After you have carried out your update, the system will automatically be rebooted. You can go to the main home page after this time by clicking the button below or going to '/'.</p>";
      html += "<p>You can take the system out of this mode by clicking the button to reboot below or going to '/reboot'.</p>";
      html += "<p><a href=\"http://" + ip.toString() + "\")\">Home</a></p>";
      html += "<p><a href=\"http://" + ip.toString() + "/reboot\")\">Reboot</a></p>";
      html += "</font>";
      html += "</div>";

      request->send(200, "text/html", html);
  
      if (!isInOtaMode) {
        SerialPrintln("Setting OTA Hostname");
        ArduinoOTA.setHostname("Split-Flap-OTA");

        //If there is a password set, disabled by default for ease
        if (otaPassword != "") {
          ArduinoOTA.setPassword(otaPassword);
        }
        
        SerialPrintln("Starting OTA Mode");
        ArduinoOTA.begin();
        delay(100);
      
        ArduinoOTA.onStart([]() {
          LittleFS.end();
          if (ArduinoOTA.getCommand() == U_FLASH) {
            SerialPrintln("Start updating sketch");
          } 
          else {
            SerialPrintln("Start updating filesystem");
          }  
        });
        
        ArduinoOTA.onEnd([]() {
          SerialPrintln("Finished OTA Update - Rebooting");
          isPendingReboot = true;
        });
        
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
          SerialPrintf("OTA Progress: %u%%\r", (progress / (total / 100)));
        });
        
        ArduinoOTA.onError([](ota_error_t error) {
          SerialPrintf("Error[%u]: ", error);

          if (error == OTA_AUTH_ERROR) {
            SerialPrintln("Finished OTA Update - Rebooting");
          }
          else if (error == OTA_BEGIN_ERROR) {
            SerialPrintln("OTA Begin Failed");
          }
          else if (error == OTA_CONNECT_ERROR) {
            SerialPrintln("OTA Connect Failed");
          }
          else if (error == OTA_RECEIVE_ERROR) {
            SerialPrintln("OTA Receive Failed");
          }
          else if (error == OTA_END_ERROR) {
            SerialPrintln("OTA End Failed");
          }
        });
        
        //Put in OTA Mode
        isInOtaMode = true;
      }
      else {
        SerialPrintln("Already in OTA Mode");
      }
    });
#endif

#if WIFI_USE_DIRECT == false
    webServer.on("/reset-wifi", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request to Reset WiFi Received");
      
      IPAddress ip = WiFi.localIP();
      
      String html = "<div style='text-align:center'>";
      html += "<font face='arial'><h1>Split Flap - Resetting WiFi</h1>";
      html += "<p>WiFi Settings have been erased. Device will now reboot...<p>";
      html += "<p>You will now be able to connect to this device in AP mode to configure the WiFi once more<p>";
      html += "<p>You can go to the main home page after this time by clicking the button below or going to '/'.</p>";
      html += "<p><a href=\"http://" + ip.toString() + "\">Home</a></p>";
      html += "</font>";
      html += "</div>";
      
      request->send(200, "text/html", html);
      isPendingWifiReset = true;
    });
#endif   

    delay(250);
    webServer.begin();
    
#if SHOW_UNIT_NUMBERS_ON_STARTUP == true && UNIT_CALLS_DISABLE == false
    // Display each unit's number on bootup to help identify units
    SerialPrintln("DEBUG: Displaying unit numbers on startup...");
    bool unitsSent = false;
    for (int unitIndex = 0; unitIndex < UNITS_AMOUNT; unitIndex++) {
      // Convert unit number to letter position: '0' is at index 30, '1' at 31, etc.
      int digitPosition = 30 + unitIndex; // '0' = 30, '1' = 31, ..., '9' = 39
      if (unitIndex <= 9) { // Only display if unit number is 0-9 (single digit)
        SerialPrint("DEBUG: Sending unit ");
        SerialPrint(unitIndex);
        SerialPrintln(" to display its number");
        writeToUnit(unitIndex, digitPosition, 10); // Use speed 10 for quick display
        unitsSent = true;
        yield(); // Allow web server to process
        delay(50); // Small delay between units
      }
    }
    
    // Only wait if we actually sent commands to units
    if (unitsSent) {
      // Wait for units to finish displaying their numbers, then hold for 1 second
      SerialPrintln("DEBUG: Waiting for units to display numbers...");
      unsigned long waitStart = millis();
      unsigned long waitTimeout = 3000; // 3 second max wait (reduced from 5s)
      int waitIterations = 0;
      while (isDisplayMoving() && (millis() - waitStart < waitTimeout)) {
        yield();
        delay(100);
        waitIterations++;
        // Safety: if we've been waiting a while and units aren't responding, break
        if (waitIterations > 10) {
          // Check if we're stuck because units aren't responding
          bool allUnitsStuck = true;
          for (int i = 0; i < UNITS_AMOUNT; i++) {
            if (displayState[i] == 0) { // At least one unit is ready
              allUnitsStuck = false;
              break;
            }
          }
          if (allUnitsStuck) {
            SerialPrintln("DEBUG: Units not responding, skipping wait");
            break;
          }
        }
      }
      
      if (millis() - waitStart >= waitTimeout) {
        SerialPrintln("DEBUG: Timeout waiting for units, continuing anyway");
      }
      
      // Hold the display for 1 second so numbers are visible
      SerialPrintln("DEBUG: Unit numbers displayed, holding for 1 second...");
      delay(1000);
      yield();
    }
#endif
    
    // Wait for all units to finish their initial calibration (they calibrate on startup)
    // Units can take 5-30 seconds to calibrate depending on Hall sensor position
    SerialPrintln("DEBUG: Waiting for units to finish initial calibration...");
    unsigned long calibrationWaitStart = millis();
    unsigned long calibrationWaitTimeout = 35000; // 35 second max wait for calibration
    int calibrationCheckCount = 0;
    
    while (millis() - calibrationWaitStart < calibrationWaitTimeout) {
      bool allUnitsReady = true;
      int readyCount = 0;
      
      // Check status of all units
      for (int unitIndex = 0; unitIndex < UNITS_AMOUNT; unitIndex++) {
        int status = checkIfMoving(unitIndex);
        displayState[unitIndex] = status;
        
        if (status == 1) {
          // Unit is still busy (calibrating)
          allUnitsReady = false;
        } else if (status == 0) {
          // Unit is ready
          readyCount++;
        }
        // Ignore -1 (sleeping) as units may sleep during calibration
      }
      
      if (allUnitsReady) {
        SerialPrint("DEBUG: All units finished calibration (");
        SerialPrint(readyCount);
        SerialPrintln(" units ready)");
        break;
      }
      
      calibrationCheckCount++;
      if (calibrationCheckCount % 10 == 0) {
        // Log progress every 10 checks (every ~2 seconds)
        SerialPrint("DEBUG: Waiting for units to calibrate... (");
        SerialPrint(readyCount);
        SerialPrint("/");
        SerialPrint(UNITS_AMOUNT);
        SerialPrintln(" ready)");
      }
      
      yield();
      delay(200); // Check every 200ms
    }
    
    if (millis() - calibrationWaitStart >= calibrationWaitTimeout) {
      SerialPrintln("DEBUG: WARNING - Calibration wait timeout. Some units may still be calibrating.");
      SerialPrintln("DEBUG: System will continue, but first command may timeout if units aren't ready.");
    }
    
    debugStatus = "Ready";
    SerialPrintln("DEBUG: Status = " + debugStatus);
    SerialPrintln("Split Flap Ready!");
    SerialPrintln("#######################################################");
  }
  else {
    if (isPendingReboot) {
      SerialPrintln("Reboot is pending to be able to continue device function. Hold please...");
      SerialPrintln("#######################################################");
    }
    else {
      SerialPrintln("Unable to connect to WiFi... Not starting web server");
      SerialPrintln("Please hard restart your device to try connect again");
      SerialPrintln("#######################################################");
      // Continuous blink to indicate error
      continuousBlink(10000); // Blink for 10 seconds
    }
  }
}

/* .----------------------------------------------------. */
/* | ___                _             _                 | */
/* || _ \_  _ _ _  _ _ (_)_ _  __ _  | |   ___ ___ _ __ | */
/* ||   | || | ' \| ' \| | ' \/ _` | | |__/ _ / _ | '_ \| */
/* ||_|_\\_,_|_||_|_||_|_|_||_\__, | |____\___\___| .__/| */
/* |                          |___/               |_|   | */
/* '----------------------------------------------------' */
void loop() {
  //Reboot in here as if we restart within a request handler, no response is returned
  if (isPendingReboot) {
    SerialPrintln("Rebooting Now... Fairwell!");
    SerialPrintln("#######################################################");
    delay(100);

    ESP.restart();
    return;
  }

#if WIFI_USE_DIRECT == false
  //Clear off the WiFi Manager Settings
  if (isPendingWifiReset) {
    SerialPrintln("Removing WiFi settings");
    wifiManager.resetSettings();
    delay(100);

    isPendingReboot = true;
    return;
  }
#endif

  // Monitor WiFi connection and attempt reconnection if lost
  if (isWifiConfigured) {
    static unsigned long lastWiFiCheck = 0;
    static int reconnectAttempts = 0;
    const unsigned long wifiCheckInterval = 10000; // Check every 10 seconds
    
    if (millis() - lastWiFiCheck > wifiCheckInterval) {
      lastWiFiCheck = millis();
      
      if (WiFi.status() != WL_CONNECTED) {
        SerialPrintln("DEBUG: WiFi connection lost! Status: " + String(WiFi.status()));
        reconnectAttempts++;
        
        if (reconnectAttempts <= 3) {
          SerialPrintln("DEBUG: Attempting to reconnect (attempt " + String(reconnectAttempts) + "/3)...");
          WiFi.disconnect();
          delay(100);
          yield();
          
          // Reconnect with stored credentials
          WiFi.begin(wifiDirectSsid, wifiDirectPassword);
          
          int reconnectTimeout = 10; // 10 second timeout for reconnection
          int reconnectCount = 0;
          while (WiFi.status() != WL_CONNECTED && reconnectCount < reconnectTimeout) {
            delay(500);
            yield();
            delay(500);
            yield();
            reconnectCount++;
          }
          
          if (WiFi.status() == WL_CONNECTED) {
            SerialPrintln("DEBUG: WiFi reconnected successfully! IP: " + WiFi.localIP().toString());
            reconnectAttempts = 0;
          } else {
            SerialPrintln("DEBUG: Reconnection attempt " + String(reconnectAttempts) + " failed");
            if (reconnectAttempts >= 3) {
              SerialPrintln("DEBUG: Max reconnection attempts reached. Marking WiFi as not configured.");
              isWifiConfigured = false;
            }
          }
        }
      } else {
        // Connection is good, reset reconnect counter
        if (reconnectAttempts > 0) {
          reconnectAttempts = 0;
        }
      }
    }
  }
  
  //Do nothing if WiFi is not configured
  if (!isWifiConfigured) {
    //Show there is an error via text on display
    deviceMode = DEVICE_MODE_TEXT;
    alignment = ALIGNMENT_MODE_CENTER;
    flapSpeed = "80";

    showText("OFFLINE");
    delay(100);
    return;
  }

  if (isPendingUnitsReset) {
    SerialPrintln("Reseting Units now...");

    //Blank out the message
    String blankOutText1 = createRepeatingString('-');
    showText(blankOutText1);
    delay(2000);

    //Do just enough to do a full iteration which triggers the re-calibration
    String blankOutText2 = createRepeatingString('.');
    showText(blankOutText2);

    //We did a reset!
    isPendingUnitsReset = false;

    SerialPrintln("Done Units Reset!");
  }
  
#if OTA_ENABLE == true
  //If System is in OTA, try handle!
  if(isInOtaMode) {
    ArduinoOTA.handle();
    delay(1);
  }
#endif

  //ezTime library sync
  events(); 
  
  //Process every second
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= 1000) {
    previousMillis = currentMillis;

    checkScheduledMessages();
    checkCountdown();

    // Skip display updates if a web request is active to prevent blocking
    if (!webRequestActive) {
      //Mode Selection
      if (deviceMode == DEVICE_MODE_TEXT || deviceMode == DEVICE_MODE_COUNTDOWN) { 
        showText(inputText);
      } 
      else if (deviceMode == DEVICE_MODE_DATE) {
        showText(timezone.dateTime(dateFormat));
      } 
      else if (deviceMode == DEVICE_MODE_CLOCK) {
        showText(timezone.dateTime(clockFormat));
      }
    } else {
      // Web request is active, skip display update to keep web server responsive
      yield(); // Give web server time to process
    }
  }
}

// Scan I2C bus and report all found devices
void scanI2CBus() {
  SerialPrintln("========================================");
  SerialPrintln("I2C Bus Scan - Checking addresses 0-15");
  SerialPrintln("========================================");
  
  int foundCount = 0;
  bool foundUnits[16] = {false}; // Track which addresses are found
  int unitStatus[16] = {-2}; // Track status of each address (-2 = not found, -1 = sleeping, 0 = ready, 1 = busy)
  
  // Scan all possible I2C addresses (0-15)
  for (int address = 0; address < 16; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
      foundUnits[address] = true;
      foundCount++;
      
      SerialPrint("✓ Found device at address ");
      SerialPrint(address);
      
      // Try to read status from this address (only for addresses 0-15 which are our units)
      if (address < 16) {
        Wire.requestFrom(address, 1, 1);
        if (Wire.available()) {
          int status = Wire.read();
          unitStatus[address] = status;
          
          SerialPrint(" - Status: ");
          SerialPrint(status);
          
          if (status == 0) {
            SerialPrintln(" (READY)");
          } else if (status == 1) {
            SerialPrintln(" (BUSY/MOVING)");
          } else if (status == -1) {
            SerialPrintln(" (SLEEPING)");
          } else {
            SerialPrintln(" (UNKNOWN)");
          }
        } else {
          SerialPrintln(" - No status response");
          unitStatus[address] = -3; // No response
        }
      } else {
        SerialPrintln(" - Unknown device type");
      }
    } else if (error == 2) {
      // NACK on address - device not found (normal for unused addresses)
      // Don't log this to reduce noise
    } else if (error == 4) {
      SerialPrint("⚠ Unknown I2C error at address ");
      SerialPrintln(address);
    }
    
    yield();
    delay(10);
  }
  
  // Count how many expected units (0 to UNITS_AMOUNT-1) are actually connected
  int connectedExpectedUnits = 0;
  for (int i = 0; i < UNITS_AMOUNT; i++) {
    if (foundUnits[i]) {
      connectedExpectedUnits++;
    }
  }
  connectedUnitCount = connectedExpectedUnits; // Store for web UI
  
  // Summary report
  SerialPrintln("");
  SerialPrintln("--- Scan Summary ---");
  SerialPrint("Expected units: ");
  SerialPrint(UNITS_AMOUNT);
  SerialPrint(" | Connected units: ");
  SerialPrintln(connectedExpectedUnits);
  SerialPrint("Total devices found on I2C bus: ");
  SerialPrintln(foundCount);
  SerialPrintln("");
  
  // Report which units are connected vs missing
  SerialPrintln("Unit Status:");
  bool allUnitsFound = true;
  for (int i = 0; i < UNITS_AMOUNT; i++) {
    SerialPrint("  Unit ");
    SerialPrint(i);
    SerialPrint(" (address ");
    SerialPrint(i);
    SerialPrint("): ");
    
    if (foundUnits[i]) {
      SerialPrint("✓ CONNECTED");
      if (unitStatus[i] == 0) {
        SerialPrintln(" - Ready");
      } else if (unitStatus[i] == 1) {
        SerialPrintln(" - Busy");
      } else if (unitStatus[i] == -1) {
        SerialPrintln(" - Sleeping");
      } else if (unitStatus[i] == -3) {
        SerialPrintln(" - No status response");
      } else {
        SerialPrint(" - Status code: ");
        SerialPrintln(unitStatus[i]);
      }
    } else {
      SerialPrintln("✗ MISSING (not responding on I2C)");
      allUnitsFound = false;
    }
  }
  
  // Report any extra devices found at addresses beyond expected units
  if (foundCount > UNITS_AMOUNT) {
    SerialPrintln("");
    SerialPrintln("Additional devices found (beyond expected units):");
    for (int i = UNITS_AMOUNT; i < 16; i++) {
      if (foundUnits[i]) {
        SerialPrint("  Address ");
        SerialPrint(i);
        SerialPrintln(" - Unknown device");
      }
    }
  }
  
  SerialPrintln("");
  if (allUnitsFound) {
    SerialPrintln("✓ All expected units are connected!");
  } else {
    SerialPrint("⚠ WARNING: Some units are missing! Check:");
    SerialPrintln("");
    SerialPrintln("  1. DIP switch settings on missing units");
    SerialPrintln("  2. I2C wiring (SDA/SCL connections)");
    SerialPrintln("  3. Power connections to missing units");
    SerialPrintln("  4. Unit firmware (verify address is correct)");
  }
  
  SerialPrintln("========================================");
}

//Gets all the currently stored calues from memory in a JSON object
String getCurrentSettingValues() {
  unsigned long funcStart = millis();
  const unsigned long MAX_FUNCTION_TIME = 5000; // 5 second max for this function (reduced from 10s)
  SerialPrintln("DEBUG: getCurrentSettingValues() - Starting");
  
  // Yield immediately to allow web server to process
  yield();
  
  JsonDocument document;
  unsigned long afterDoc = millis();
  SerialPrintln("DEBUG: getCurrentSettingValues() - JsonDocument created (" + String(afterDoc - funcStart) + "ms)");

  document["timezoneOffset"] = timezone.getOffset();
  document["unitCount"] = UNITS_AMOUNT;
  document["connectedUnitCount"] = connectedUnitCount;
  document["alignment"] = alignment;
  document["flapSpeed"] = flapSpeed;
  document["deviceMode"] = deviceMode;
  document["version"] = espVersion;
#if PAGE_LOAD_DEBUG_ENABLE == true
  document["pageLoadDebugEnabled"] = true;
#else
  document["pageLoadDebugEnabled"] = false;
#endif
  document["lastTimeReceivedMessageDateTime"] = lastReceivedMessageDateTime;
  document["lastWrittenText"] = lastWrittenText;
  document["countdownToDateUnix"] = atol(countdownToDateUnix.c_str());
  
  // WiFi status information - read RSSI quickly (reduced readings for faster response)
  if (WiFi.status() == WL_CONNECTED) {
    document["wifiStatus"] = "Connected";
    // Read RSSI 2 times for faster response (was 5, but adds delay)
    long rssiSum = 0;
    int rssiReadings = 2;
    for (int i = 0; i < rssiReadings; i++) {
      rssiSum += WiFi.RSSI();
      if (i < rssiReadings - 1) {
        delay(5); // Reduced from 10ms
        yield();
      }
    }
    document["wifiRssi"] = rssiSum / rssiReadings;
    document["wifiIp"] = WiFi.localIP().toString();
  } else {
    document["wifiStatus"] = "Disconnected";
    document["wifiRssi"] = 0;
    document["wifiIp"] = "";
  }
  unsigned long afterBasicFields = millis();
  SerialPrintln("DEBUG: getCurrentSettingValues() - Basic fields set (" + String(afterBasicFields - afterDoc) + "ms)");

  int scheduledCount = scheduledMessages.size();
  SerialPrintln("DEBUG: getCurrentSettingValues() - Processing " + String(scheduledCount) + " scheduled messages");
  unsigned long beforeScheduled = millis();
  
  // Process scheduled messages with yields every few items to keep web server responsive
  // Also check for timeout to prevent hanging
  for(int scheduledMessageIndex = 0; scheduledMessageIndex < scheduledMessages.size(); scheduledMessageIndex++) {
    // Check for timeout - if we're taking too long, skip remaining messages
    if (millis() - funcStart > MAX_FUNCTION_TIME) {
      SerialPrintln("DEBUG: WARNING - getCurrentSettingValues() timeout, skipping remaining scheduled messages");
      break;
    }
    
    ScheduledMessage scheduledMessage = scheduledMessages[scheduledMessageIndex];
    
    document["scheduledMessages"][scheduledMessageIndex]["scheduledDateTimeUnix"] = scheduledMessage.ScheduledDateTimeUnix;
    document["scheduledMessages"][scheduledMessageIndex]["message"] = scheduledMessage.Message;
    document["scheduledMessages"][scheduledMessageIndex]["showIndefinitely"] = scheduledMessage.ShowIndefinitely;
    
    // Yield every 5 messages to keep web server responsive, or on every message if < 10 total
    if (scheduledMessageIndex % 5 == 0 || scheduledCount < 10) {
      yield(); // Allow web server to process requests during loop
    }
  }
  unsigned long afterScheduled = millis();
  SerialPrintln("DEBUG: getCurrentSettingValues() - Scheduled messages processed (" + String(afterScheduled - beforeScheduled) + "ms)");

#if OTA_ENABLE == true
  document["otaEnabled"] = true;
  document["isInOtaMode"] = isInOtaMode;
#else
  document["otaEnabled"] = false;
#endif

#if WIFI_USE_DIRECT == false
  document["wifiSettingsResettable"] = true;
#else
  document["wifiSettingsResettable"] = false;
#endif
  
  unsigned long beforeSerialize = millis();
  
  // Check timeout before serialization
  if (millis() - funcStart > MAX_FUNCTION_TIME) {
    SerialPrintln("DEBUG: WARNING - getCurrentSettingValues() timeout before serialization, returning minimal response");
    JsonDocument minimalDoc;
    minimalDoc["timezoneOffset"] = timezone.getOffset();
    minimalDoc["unitCount"] = UNITS_AMOUNT;
    minimalDoc["alignment"] = alignment;
    minimalDoc["flapSpeed"] = flapSpeed;
    minimalDoc["deviceMode"] = deviceMode;
    minimalDoc["version"] = espVersion;
#if PAGE_LOAD_DEBUG_ENABLE == true
    minimalDoc["pageLoadDebugEnabled"] = true;
#else
    minimalDoc["pageLoadDebugEnabled"] = false;
#endif
    minimalDoc["wifiStatus"] = WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected";
    minimalDoc["wifiRssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    minimalDoc["wifiIp"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
    minimalDoc["scheduledMessages"] = JsonArray(); // Empty array
    String minimalJson;
    serializeJson(minimalDoc, minimalJson);
    return minimalJson;
  }
  
  yield(); // Yield before potentially long serialization
  String jsonString;
  serializeJson(document, jsonString);
  yield(); // Yield after serialization to allow web server to process
  unsigned long afterSerialize = millis();
  SerialPrintln("DEBUG: getCurrentSettingValues() - JSON serialized (" + String(afterSerialize - beforeSerialize) + "ms, " + String(jsonString.length()) + " bytes)");
  SerialPrintln("DEBUG: getCurrentSettingValues() - Total time: " + String(afterSerialize - funcStart) + "ms");

  return jsonString;
}
