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
  
  Version: 1.1.22
  Changes: Added I2C setup scan system for comprehensive bus testing
           - Created ServiceI2CSetupScan.ino that scans all 16 possible I2C addresses (0-15)
           - Tests both read and write operations for each found device
           - Reports which addresses are working, read-only, write-only, or not working
           - Compares found devices against expected units (0 to UNITS_AMOUNT-1)
           - Identifies unexpected devices (misconfigured address switches)
           - Added I2C_SETUP_SCAN_ENABLE flag (default: true) for startup scanning
           - Added /i2c-setup-scan endpoint for on-demand testing
           - Perfect for testing individual units before connecting them all
           - Each unit keeps its configured ID number (no renumbering needed)
  
  Version: 1.1.21
  Changes: Added optional I2C diagnostic testing system
           - Created ServiceI2CDiagnostics.ino with comprehensive read/write/speed tests
           - Added I2C_DIAGNOSTIC_ENABLE flag (default: false) for startup testing
           - Added /i2c-diagnostics endpoint for on-demand testing (works even if flag is false)
           - Can test specific unit (?unit=7) or all units
           - Tests: address detection, read operations, write operations, error rate
           - Safe to enable/disable without affecting normal operation
  
  Major Changes from Scientress Fork:
  - Expanded I2C status codes: ESPMaster now interprets detailed unit calibration states for better debugging
  - Enhanced calibration wait loop: Improved timeout handling, stuck unit detection, and progress logging
  - Improved error handling: Better I2C error reporting and unit status tracking during calibration
  - Version tracking: Added firmware version number for easier debugging and verification
  - ESP-01S support: Better handling of ESP-01S vs ESP-01 differences (LED, I2C initialization)
  - Serial/I2C conflict handling: Proper initialization to avoid GPIO 1 conflicts on ESP-01
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
#define OTA_ENABLE          true    //Option to enable OTA functionality
#define UNITS_AMOUNT        9       //Amount of connected units !IMPORTANT TO BE SET CORRECTLY!
#define SERIAL_BAUDRATE     57600  //Serial debugging BAUD rate
#define WIFI_USE_DIRECT     true   //Option to either direct connect to a WiFi Network or setup a AP to configure WiFi. Setting to false will setup as a AP.
#define ESP01S_LED_ENABLE   true   //Option to enable LED error indication on ESP-01S (set to false if not using ESP-01S or LED)
#define DEBUG_ENABLE        true  //Enable debug features: startup debug page, error status panel, and serial debug log at bottom of page
#define PAGE_LOAD_DEBUG_ENABLE false  //Enable page load debug blocks: browser errors panel and page load debug log at top of page (set to true for troubleshooting page loading issues)
#define I2C_DIAGNOSTIC_ENABLE false  //Enable I2C diagnostic testing (DISABLED by default - blocks web server startup; use /i2c-diagnostics endpoint instead)
#define I2C_SETUP_SCAN_ENABLE false  //Enable I2C setup scan at startup (DISABLED by default - blocks web server startup; use /i2c-setup-scan endpoint instead)
#define CLOCK_FORMAT_24H true  //Default clock format: true = 24-hour (HH:MM), false = 12-hour (hh:mma)

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
const char* otaPassword = "1234";

//Change this to your timezone, use the TZ database name
//https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
const char* timezoneString = "America/Los_Angeles";

//If you want to have a different date or clock format change these two
//Complete table with every char: https://github.com/ropg/ezTime#getting-date-and-time
const char* dateFormat = "d.m.Y"; //Examples: d.m.Y -> 11.09.2021, D M y -> SAT SEP 21
const char* clockFormat24H = "H:i"; //24-hour format: H:i -> 21:19
const char* clockFormat12H = "h:ia"; //12-hour format: h:ia -> 09:19pm
// Note: clockFormat is now dynamically set based on user preference (stored in clockFormat24Hour variable)

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
// Store unit status from initial scan for static display on main page
bool staticFoundUnits[16] = {false}; // Track which addresses are found (from initial scan)
int staticUnitStatus[16] = {-2}; // Track status of each address (-2 = not found, -1 = sleeping, 0 = ready, 1 = busy)
bool staticUnitStatusValid = false; // Whether we have valid static unit status data
unsigned long previousMillis = 0;

//Search for parameter in HTTP POST request
const char* PARAM_ALIGNMENT = "alignment";
const char* PARAM_FLAP_SPEED = "flapSpeed";
const char* PARAM_DEVICEMODE = "deviceMode";
const char* PARAM_INPUT_TEXT = "inputText";
const char* PARAM_TRAIN_STATION_DELAY = "trainStationDelay";
const char* PARAM_RANDOM_PHRASE_LIST = "randomPhraseList";
const char* PARAM_RANDOM_PHRASE_MIN_DELAY = "randomPhraseMinDelay";
const char* PARAM_RANDOM_PHRASE_MAX_DELAY = "randomPhraseMaxDelay";
const char* PARAM_CLOCK_FORMAT_24H = "clockFormat24H";
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
const char* DEVICE_MODE_TRAIN_STATION = "trainstation";
const char* DEVICE_MODE_RANDOM_PHRASE = "randomphrase";

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
const char* trainStationDelayPath = "/trainstationdelay.txt";
const char* randomPhraseListPath = "/randomphraselist.txt";
const char* randomPhraseMinDelayPath = "/randomphasemindelay.txt";
const char* randomPhraseMaxDelayPath = "/randomphasemaxdelay.txt";
const char* clockFormat24HourPath = "/clockformat24h.txt";

//Variables for storing things for checking and use in normal running
String alignment = "";
String flapSpeed = "";
String inputText = "";
String deviceMode = "";
String countdownToDateUnix = "";
String trainStationDelaySeconds = "30"; // Default 30 seconds
String randomPhraseList = ""; // Comma-separated list of phrases
String randomPhraseMinDelaySeconds = "10"; // Default 10 seconds
String randomPhraseMaxDelaySeconds = "60"; // Default 60 seconds
String clockFormat24Hour = ""; // "true" for 24-hour format, "false" for 12-hour format (defaults to CLOCK_FORMAT_24H)
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

// Forward declaration for I2C diagnostics (defined in ServiceI2CDiagnostics.ino)
// Function is always available, but only runs if I2C_DIAGNOSTIC_ENABLE is true or called via endpoint
void runI2CDiagnostics(int unitAddress);

// Forward declaration for I2C setup scan (defined in ServiceI2CSetupScan.ino)
// Scans all 16 possible addresses and tests read/write operations
void runI2CSetupScan();

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

// I2C Diagnostic state (for async execution)
bool i2cDiagnosticPending = false;
int i2cDiagnosticUnitAddress = -1;
unsigned long i2cDiagnosticStartTime = 0;

// Serial log buffer for web interface (circular buffer, stores last 200 messages)
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
  // Seed random number generator for train station mode
  // Use millis() for seeding (will vary based on boot timing)
  randomSeed(millis());
#if SERIAL_ENABLE == true
  //Setup so we can see serial messages
  //NOTE: On ESP-01, GPIO 1 is shared between TX (serial) and SDA (I2C), so I2C is disabled when serial is enabled
  //For debugging with multiple units, disable SERIAL_ENABLE and use unit.ino serial debugging instead
  Serial.begin(SERIAL_BAUDRATE);
#elif !defined(FAE_MOD)
  //For ESP01/ESP01S - I2C on GPIO 1 (SDA) and GPIO 3 (SCL)
  //Note: ESP-01S is recommended over ESP-01 due to better I2C performance
  //On ESP-01, GPIO 1 is also TX, which conflicts with serial, so I2C is only enabled when serial is disabled
  Wire.begin(1, 3); 
  Wire.setClock(50000); // Set I2C speed to 50kHz (slower = more reliable with long wires and many devices)
  // Default is usually 100kHz, but 50kHz is more reliable for long buses with 10+ units
  
  //De-activate I2C if debugging the ESP, otherwise serial does not work
  //Wire.begin(D1, D2); //For NodeMCU testing only SDA=D1 and SCL=D2
#endif

#ifdef FAE_MOD
  //Fae Mod
  Wire.begin(4, 5);
  Wire.setClock(50000); // Set I2C speed to 50kHz for reliability
#endif

  // Initialize LED for error indication
#if ESP01S_LED_ENABLE == true
  pinMode(LED_DEBUG_PIN, OUTPUT);
  ledOff();
#endif

  // I2C bus scan is now disabled at startup to prevent blocking web server
  // Use the "Scan I2C Bus" button on the main page to run scanI2CBus() on-demand
  delay(500); // Give I2C bus time to stabilize
  SerialPrintln("");
  SerialPrintln("=== I2C Bus Scan Skipped at Startup ===");
  SerialPrintln("Use the 'Scan I2C Bus' button on the main page to scan units on-demand");
  SerialPrintln("");
  
  // I2C diagnostic testing (if enabled)
#if I2C_DIAGNOSTIC_ENABLE == true
  SerialPrintln("I2C Diagnostic Testing is ENABLED");
  runI2CDiagnostics(-1); // Test all units (-1 = all units)
#else
  SerialPrintln("I2C Diagnostic Testing is DISABLED (set I2C_DIAGNOSTIC_ENABLE to true to enable, or use /i2c-diagnostics endpoint)");
#endif
  SerialPrintln("");
  
  SerialPrintln("#######################################################");
  SerialPrintln("..............Split Flap Display Starting..............");
  SerialPrintln("#######################################################");
  SerialPrintln("Firmware Version: 1.1.5");
  SerialPrintln("");
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
        html += "<div style='margin-bottom: 15px;'>";
        html += "<button onclick='continueToNormal()' style='margin-right: 10px;'>Continue to Normal Mode</button>";
        html += "<button onclick='copyLog()' style='background: #4ec9b0;'>Copy Log</button>";
        html += "</div>";
        html += "<div class='status'>Current Status: " + debugStatus + "</div>";
        html += "<p>Debug mode is enabled. Click below to continue to the main page.</p>";
        html += "<script>";
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
      minimalDoc["trainStationDelay"] = atol(trainStationDelaySeconds.c_str());
      minimalDoc["randomPhraseList"] = randomPhraseList;
      minimalDoc["randomPhraseMinDelay"] = atol(randomPhraseMinDelaySeconds.c_str());
      minimalDoc["randomPhraseMaxDelay"] = atol(randomPhraseMaxDelaySeconds.c_str());
      minimalDoc["clockFormat24H"] = (clockFormat24Hour == "true");
      minimalDoc["wifiStatus"] = WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected";
      minimalDoc["wifiRssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
      minimalDoc["wifiIp"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
      minimalDoc["scheduledMessages"] = JsonArray();
      minimalDoc["wifiSettingsResettable"] = true;
#if OTA_ENABLE == true
      minimalDoc["otaEnabled"] = true;
#else
      minimalDoc["otaEnabled"] = false;
#endif
      
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
      SerialPrintln("DEBUG: /log endpoint requested");
      
      // Use JSON document to handle log entries
      // Reduced to 30 entries max with shorter messages to prevent truncation
      // With 30 entries max, ~150 bytes per entry (message + timestamp), need ~5KB minimum
      // Using 12KB buffer to be safe with escaped characters and overhead
      StaticJsonDocument<12000> document;
      document["count"] = serialLogCount;
      
      // Initialize logs array even if empty
      JsonArray logsArray = document["logs"].to<JsonArray>();
      
      if (serialLogCount > 0) {
        int startIndex = serialLogCount < SERIAL_LOG_SIZE ? 0 : serialLogIndex;
        int entriesToReturn = serialLogCount < SERIAL_LOG_SIZE ? serialLogCount : SERIAL_LOG_SIZE;
        
        // Limit to last 30 entries to prevent JSON buffer overflow and memory issues
        int maxEntries = entriesToReturn > 30 ? 30 : entriesToReturn;
        int actualStart = entriesToReturn > 30 ? (startIndex + entriesToReturn - 30) % SERIAL_LOG_SIZE : startIndex;
        
        // Try to add entries, but check if we're running out of space
        for (int i = 0; i < maxEntries; i++) {
          int idx = (actualStart + i) % SERIAL_LOG_SIZE;
          String msg = serialLog[idx].message;
          
          // Truncate very long messages to prevent JSON issues (max 180 chars to leave room for JSON escaping overhead)
          // This ensures we don't exceed buffer even with many escaped characters
          if (msg.length() > 180) {
            msg = msg.substring(0, 177) + "...";
          }
          
          // ArduinoJson automatically escapes special characters in strings, so no manual escaping needed
          JsonObject logEntry = logsArray.add<JsonObject>();
          logEntry["message"] = msg;
          logEntry["timestamp"] = serialLog[idx].timestamp;
          
          // Check if document is getting too full (rough estimate - stop at 10KB to leave room)
          if (document.memoryUsage() > 10000) {
            // Stop adding entries if we're getting close to buffer limit
            SerialPrintln("WARNING: Log JSON buffer getting full, stopping at entry " + String(i + 1));
            break;
          }
        }
      }
      
      String jsonString;
      jsonString.reserve(12000); // Reserve memory to prevent fragmentation
      
      // Measure size before serialization
      size_t initialSize = jsonString.length();
      size_t bytesWritten = serializeJson(document, jsonString);
      size_t finalSize = jsonString.length();
      
      // Check if serialization succeeded
      if (jsonString.length() == 0) {
        SerialPrintln("ERROR: JSON serialization failed for /log endpoint");
        request->send(500, "application/json", "{\"error\":\"Serialization failed\"}");
        return;
      }
      
      // Check if serialization was truncated by comparing bytes written to string length
      // If bytesWritten is 0, it means serialization failed
      // If the string is very close to buffer size, it might be truncated
      if (bytesWritten == 0 || jsonString.length() > 11500) {
        SerialPrintln("ERROR: JSON serialization truncated - buffer too small. Size: " + String(jsonString.length()) + ", bytesWritten: " + String(bytesWritten));
        // Try sending a smaller response with error message
        request->send(500, "application/json", "{\"error\":\"Buffer overflow\",\"count\":" + String(serialLogCount) + ",\"size\":" + String(jsonString.length()) + "}");
        return;
      }
      
      // Verify JSON is complete by checking it ends with }]
      if (!jsonString.endsWith("]}") && !jsonString.endsWith("}")) {
        SerialPrintln("WARNING: JSON response may be incomplete. Length: " + String(jsonString.length()));
      }
      
      request->send(200, "application/json", jsonString);
      // Don't clear jsonString immediately - let AsyncWebServer handle it
      // The string will be cleaned up automatically after the response is sent
    });
    
    // Static unit status endpoint - returns cached status from initial scan (no I2C calls)
    webServer.on("/unit-status-static", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("DEBUG: Static unit status requested");
      
      // Use StaticJsonDocument with proper size (16 units, ~100 bytes per unit)
      StaticJsonDocument<2000> doc;
      doc["expectedUnits"] = UNITS_AMOUNT;
      doc["isValid"] = staticUnitStatusValid;
      
      JsonArray unitsArray = doc["units"].to<JsonArray>();
      
      for (int address = 0; address < 16; address++) {
        JsonObject unit = unitsArray.add<JsonObject>();
        unit["address"] = address;
        unit["isExpected"] = (address < UNITS_AMOUNT);
        unit["connected"] = staticFoundUnits[address];
        unit["status"] = staticUnitStatus[address];
        
        // Map status codes to text
        if (staticFoundUnits[address]) {
          int status = staticUnitStatus[address];
          if (status == 0) {
            unit["statusText"] = "Ready";
          } else if (status == 1) {
            unit["statusText"] = "Busy";
          } else if (status == 2) {
            unit["statusText"] = "Calibrating";
          } else if (status == 3) {
            unit["statusText"] = "Calibrating";
          } else if (status == 4) {
            unit["statusText"] = "Error";
          } else if (status == -1) {
            unit["statusText"] = "Sleeping";
          } else if (status == -3) {
            unit["statusText"] = "No Response";
          } else {
            unit["statusText"] = "Unknown";
          }
        } else {
          unit["statusText"] = "Not Connected";
        }
      }
      
      String jsonString;
      jsonString.reserve(2000);
      serializeJson(doc, jsonString);
      
      if (jsonString.length() == 0) {
        SerialPrintln("ERROR: JSON serialization failed for /unit-status-static endpoint");
        request->send(500, "application/json", "{\"error\":\"Serialization failed\"}");
        return;
      }
      
      request->send(200, "application/json", jsonString);
    });
    
    // Dynamic unit status endpoint - performs live I2C scan (for debug page)
    webServer.on("/unit-status", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("DEBUG: Dynamic unit status requested (live scan)");
      
      // Use StaticJsonDocument with proper size (16 units, ~150 bytes per unit)
      StaticJsonDocument<3000> doc;
      doc["scanTime"] = millis();
      doc["expectedUnits"] = UNITS_AMOUNT;
      
      JsonArray unitsArray = doc["units"].to<JsonArray>();
      
      for (int address = 0; address < 16; address++) {
        JsonObject unit = unitsArray.add<JsonObject>();
        unit["address"] = address;
        unit["isExpected"] = (address < UNITS_AMOUNT);
        
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();
        delay(1); // Reduced delay - allow bus to settle but faster
        
        if (error == 0) {
          unit["connected"] = true;
          
          // Try to read status
          Wire.requestFrom(address, 1, 1);
          delay(1); // Reduced delay
          if (Wire.available()) {
            int status = Wire.read();
            unit["status"] = status;
            
            // Map status codes to text
            if (status == 0) {
              unit["statusText"] = "Ready";
            } else if (status == 1) {
              unit["statusText"] = "Busy/Moving";
            } else if (status == 2) {
              unit["statusText"] = "Calibrating (Searching)";
            } else if (status == 3) {
              unit["statusText"] = "Calibrating (Offset)";
            } else if (status == 4) {
              unit["statusText"] = "Calibration Error";
            } else if (status == -1) {
              unit["statusText"] = "Sleeping";
            } else {
              unit["statusText"] = "Unknown (" + String(status) + ")";
            }
          } else {
            unit["status"] = -3;
            unit["statusText"] = "No Response";
          }
        } else if (error == 2) {
          // NACK - device not found
          unit["connected"] = false;
          unit["status"] = -2;
          unit["statusText"] = "Not Connected";
        } else {
          // Other I2C error
          unit["connected"] = false;
          unit["status"] = error;
          unit["statusText"] = "I2C Error (" + String(error) + ")";
        }
        
        yield(); // Allow web server to process
        delay(2); // Reduced delay between addresses
      }
      
      String jsonString;
      jsonString.reserve(3000);
      serializeJson(doc, jsonString);
      
      if (jsonString.length() == 0) {
        SerialPrintln("ERROR: JSON serialization failed for /unit-status endpoint");
        request->send(500, "application/json", "{\"error\":\"Serialization failed\"}");
        return;
      }
      
      request->send(200, "application/json", jsonString);
    });
    
    // I2C bus scanner endpoint for diagnostics
    webServer.on("/i2c-scan", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("DEBUG: I2C scan requested");
      
      // Use StaticJsonDocument with proper size (16 devices max, ~200 bytes per device)
      StaticJsonDocument<4000> doc;
      doc["scanTime"] = millis();
      
      int foundCount = 0;
      for (int address = 0; address < 16; address++) {
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();
        
        // Add small delay after endTransmission to allow bus to settle
        delay(2);
        
        if (error == 0) {
          doc["devices"][foundCount]["address"] = address;
          
          // Try to read status
          Wire.requestFrom(address, 1, 1);
          delay(2); // Delay after requestFrom
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
        
        yield(); // Allow web server to process
        delay(10);
      }
      
      doc["foundCount"] = foundCount;
      doc["expectedCount"] = UNITS_AMOUNT;
      
      String jsonString;
      jsonString.reserve(4000); // Reserve memory
      serializeJson(doc, jsonString);
      
      // Check if serialization succeeded
      if (jsonString.length() == 0) {
        SerialPrintln("ERROR: JSON serialization failed for /i2c-scan endpoint");
        request->send(500, "application/json", "{\"error\":\"Serialization failed\"}");
        return;
      }
      
      request->send(200, "application/json", jsonString);
      SerialPrintln("DEBUG: I2C scan completed, found " + String(foundCount) + " devices");
    });
    
    // I2C setup scan endpoint (scans all 16 addresses, tests read/write)
    webServer.on("/i2c-setup-scan", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("DEBUG: I2C setup scan requested");
      
      // Run setup scan (this will output to serial log)
      runI2CSetupScan();
      
      // Return simple response (detailed results are in serial log)
      request->send(200, "text/plain", "I2C setup scan completed. Check serial log for detailed results.");
    });
    
    // I2C diagnostic testing endpoint (comprehensive read/write/speed tests)
    webServer.on("/i2c-diagnostics", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("DEBUG: I2C diagnostics requested");
      
      // Check for optional unit parameter
      int unitAddress = -1; // -1 means test all units
      if (request->hasParam("unit")) {
        unitAddress = request->getParam("unit")->value().toInt();
        if (unitAddress < 0 || unitAddress >= 16) {
          unitAddress = -1; // Invalid, test all
        }
      }
      
      // Check if JSON format requested
      bool returnJson = request->hasParam("format") && request->getParam("format")->value() == "json";
      
      // Queue diagnostics to run in loop() - don't block the handler
      i2cDiagnosticPending = true;
      i2cDiagnosticUnitAddress = unitAddress;
      i2cDiagnosticStartTime = millis();
      
      // Respond immediately - handler returns right away
      if (returnJson) {
        StaticJsonDocument<200> doc;
        doc["status"] = "started";
        doc["unit"] = unitAddress;
        doc["message"] = "Diagnostics queued. Poll /log endpoint for results.";
        
        String jsonString;
        serializeJson(doc, jsonString);
        request->send(200, "application/json", jsonString);
      } else {
        String response = "I2C diagnostics queued. Check serial log for detailed results.";
        if (unitAddress >= 0) {
          response = "I2C diagnostics queued for unit " + String(unitAddress) + ". Check serial log for detailed results.";
        }
        request->send(200, "text/plain", response);
      }
      
      // Handler returns immediately - diagnostics will run in loop()
    });
    
    // I2C Bus Scan endpoint (on-demand scan)
    webServer.on("/i2c-scan", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("=== I2C Bus Scan Requested ===");
      scanI2CBus(); // This stores results in staticFoundUnits[] and staticUnitStatus[] arrays
      SerialPrintln("=== I2C Bus Scan Complete ===");
      request->send(200, "text/plain", "OK - I2C bus scan complete. Check serial log for results.");
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
            if (receivedValue == DEVICE_MODE_TEXT || receivedValue == DEVICE_MODE_CLOCK || receivedValue == DEVICE_MODE_DATE || receivedValue == DEVICE_MODE_COUNTDOWN || receivedValue == DEVICE_MODE_TRAIN_STATION || receivedValue == DEVICE_MODE_RANDOM_PHRASE) {
              newDeviceModeValue = receivedValue;          
            }
            else {
              SerialPrintln("Device Mode provided was not valid. Invalid Value: " + receivedValue); 
              submissionError = true;
            }
          }
          
          //HTTP POST train station delay value
          if (p->name() == PARAM_TRAIN_STATION_DELAY) {
            String receivedDelay = p->value();
            long delayValue = atol(receivedDelay.c_str());
            if (delayValue >= 5 && delayValue <= 3600) { // 5 seconds to 1 hour
              trainStationDelaySeconds = receivedDelay;
            } else {
              SerialPrintln("Train Station Delay out of range (5-3600 seconds). Using default 30.");
            }
          }
          
          //HTTP POST random phrase list
          if (p->name() == PARAM_RANDOM_PHRASE_LIST) {
            randomPhraseList = p->value();
            // Limit length to prevent memory issues (max 2000 characters)
            if (randomPhraseList.length() > 2000) {
              randomPhraseList = randomPhraseList.substring(0, 2000);
              SerialPrintln("Random phrase list truncated to 2000 characters");
            }
          }
          
          //HTTP POST random phrase min delay
          if (p->name() == PARAM_RANDOM_PHRASE_MIN_DELAY) {
            String receivedDelay = p->value();
            long delayValue = atol(receivedDelay.c_str());
            if (delayValue >= 5 && delayValue <= 3600) { // 5 seconds to 1 hour
              randomPhraseMinDelaySeconds = receivedDelay;
            } else {
              SerialPrintln("Random Phrase Min Delay out of range (5-3600 seconds). Using default 10.");
            }
          }
          
          //HTTP POST random phrase max delay
          if (p->name() == PARAM_RANDOM_PHRASE_MAX_DELAY) {
            String receivedDelay = p->value();
            long delayValue = atol(receivedDelay.c_str());
            if (delayValue >= 5 && delayValue <= 3600) { // 5 seconds to 1 hour
              randomPhraseMaxDelaySeconds = receivedDelay;
            } else {
              SerialPrintln("Random Phrase Max Delay out of range (5-3600 seconds). Using default 60.");
            }
          }

          //HTTP POST Clock Format (24H or 12H)
          if (p->name() == PARAM_CLOCK_FORMAT_24H) {
            String receivedValue = p->value();
            if (receivedValue == "true" || receivedValue == "false") {
              clockFormat24Hour = receivedValue;
            } else {
              SerialPrintln("Clock format value invalid. Using default.");
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
        
        //Save train station delay if provided
        if (trainStationDelaySeconds != "") {
          writeFile(LittleFS, trainStationDelayPath, trainStationDelaySeconds.c_str());
          SerialPrintln("Train Station Delay Updated: " + trainStationDelaySeconds + " seconds");
        }
        
        //Save random phrase settings
        writeFile(LittleFS, randomPhraseListPath, randomPhraseList.c_str());
        SerialPrintln("Random Phrase List Updated (" + String(randomPhraseList.length()) + " characters)");
        
        if (randomPhraseMinDelaySeconds != "") {
          writeFile(LittleFS, randomPhraseMinDelayPath, randomPhraseMinDelaySeconds.c_str());
          SerialPrintln("Random Phrase Min Delay Updated: " + randomPhraseMinDelaySeconds + " seconds");
        }
        
        if (randomPhraseMaxDelaySeconds != "") {
          writeFile(LittleFS, randomPhraseMaxDelayPath, randomPhraseMaxDelaySeconds.c_str());
          SerialPrintln("Random Phrase Max Delay Updated: " + randomPhraseMaxDelaySeconds + " seconds");
        }
        
        if (clockFormat24Hour != "") {
          writeFile(LittleFS, clockFormat24HourPath, clockFormat24Hour.c_str());
          SerialPrintln("Clock Format Updated: " + String(clockFormat24Hour == "true" ? "24-hour" : "12-hour"));
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
      html += "<p><a href=\"http://" + ip.toString() + "\">Home</a></p>";
      html += "<p><a href=\"http://" + ip.toString() + "/reboot\">Reboot</a></p>";
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
        
        // Register handlers BEFORE calling begin() - this is required for proper initialization
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
          // Calculate percentage safely to avoid division by zero
          if (total > 0) {
            unsigned int percent = (progress * 100) / total;
            SerialPrint("OTA Progress: ");
            SerialPrint(percent);
            SerialPrintln("%");
          }
        });
        
        ArduinoOTA.onError([](ota_error_t error) {
          SerialPrint("Error[");
          SerialPrint(error);
          SerialPrint("]: ");

          if (error == OTA_AUTH_ERROR) {
            SerialPrintln("OTA Authentication Failed - Check password");
          }
          else if (error == OTA_BEGIN_ERROR) {
            SerialPrintln("OTA Begin Failed - Not enough space or invalid partition");
          }
          else if (error == OTA_CONNECT_ERROR) {
            SerialPrintln("OTA Connect Failed - Network connection lost");
          }
          else if (error == OTA_RECEIVE_ERROR) {
            SerialPrintln("OTA Receive Failed - Data transfer error");
          }
          else if (error == OTA_END_ERROR) {
            SerialPrintln("OTA End Failed - Update validation failed");
          }
          else {
            SerialPrintln("Unknown OTA error");
          }
        });
        
        SerialPrintln("Starting OTA Mode");
        ArduinoOTA.begin();
        delay(100);
        
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
    
    // Give web server time to initialize before blocking operations
    debugStatus = "Web Server Started";
    SerialPrintln("DEBUG: Status = " + debugStatus);
    SerialPrintln("DEBUG: Web server started - pages should be accessible now");
    delay(500); // Allow web server to fully initialize
    
    // Skip calibration wait at startup to allow web server to respond immediately
    // Units will finish calibrating in the background - web server is accessible right away
    SerialPrintln("DEBUG: Skipping calibration wait - web server is ready immediately");
    SerialPrintln("DEBUG: Units will finish calibrating in background");
    SerialPrintln("DEBUG: Web pages are accessible now");
    
    // Send initial speed command to all units (quick, non-blocking)
    int defaultFlapSpeed = convertSpeed(flapSpeed.length() > 0 ? flapSpeed : "80");
    SerialPrintln("DEBUG: Sending speed commands to all units...");
    for (int unitIndex = 0; unitIndex < UNITS_AMOUNT; unitIndex++) {
      writeToUnit(unitIndex, 0, defaultFlapSpeed); // Send space (0) with current speed
      yield();
      delay(5); // Small delay between units
    }
    SerialPrintln("DEBUG: Speed commands sent to all units");
    
    // Calibration wait loop REMOVED - web server needs to respond immediately
    // Units will finish calibrating in background - web server is accessible right away
    /*
    unsigned long calibrationWaitStart = millis();
    unsigned long calibrationWaitTimeout = 1000;
    int calibrationCheckCount = 0;
    int consecutiveReadyChecks = 0;
    const int REQUIRED_CONSECUTIVE_READY = 3;
    unsigned long stuckUnitStartTime[UNITS_AMOUNT];
    bool unitStuckTracked[UNITS_AMOUNT] = {false};
    const unsigned long STUCK_UNIT_TIMEOUT = 50000;
    
    while (millis() - calibrationWaitStart < calibrationWaitTimeout) {
      bool allUnitsReady = true;
      int readyCount = 0;
      
      // Periodically resend speed commands to units that are still calibrating
      // This helps units with I2C communication issues get speed updates
      static unsigned long lastSpeedUpdate = 0;
      if (millis() - lastSpeedUpdate > 5000) { // Every 5 seconds
        for (int unitIndex = 0; unitIndex < UNITS_AMOUNT; unitIndex++) {
          int status = checkIfMoving(unitIndex);
          if (status == 2 || status == 3) { // Unit is still calibrating
            writeToUnit(unitIndex, 0, defaultFlapSpeed); // Resend speed command
            yield();
            delay(5);
          }
        }
        lastSpeedUpdate = millis();
      }
      
      // Check status of all units
      bool hasStuckUnit = false;
      for (int unitIndex = 0; unitIndex < UNITS_AMOUNT; unitIndex++) {
        int status = checkIfMoving(unitIndex);
        displayState[unitIndex] = status;
        
        if (status == 1 || status == 2 || status == 3) {
          // Unit is busy (1=moving, 2=calibrating searching, 3=calibrating offset)
          // Track if this unit has been stuck for a long time
          if (!unitStuckTracked[unitIndex]) {
            stuckUnitStartTime[unitIndex] = millis();
            unitStuckTracked[unitIndex] = true;
            if (status == 2) {
              SerialPrint("DEBUG: Unit ");
              SerialPrint(unitIndex);
              SerialPrintln(" started calibration - searching for marker (status = 2)");
            } else if (status == 3) {
              SerialPrint("DEBUG: Unit ");
              SerialPrint(unitIndex);
              SerialPrintln(" applying calibration offset (status = 3)");
            } else {
              SerialPrint("DEBUG: Unit ");
              SerialPrint(unitIndex);
              SerialPrintln(" started moving (status = 1)");
            }
          } else {
            unsigned long stuckDuration = millis() - stuckUnitStartTime[unitIndex];
            // Check if this unit has been stuck for too long
            if (stuckDuration > STUCK_UNIT_TIMEOUT) {
              SerialPrint("DEBUG: WARNING - Unit ");
              SerialPrint(unitIndex);
              SerialPrint(" has been reporting BUSY (status=");
              SerialPrint(status);
              SerialPrint(") for ");
              SerialPrint(stuckDuration / 1000);
              SerialPrintln(" seconds.");
              if (status == 2) {
                SerialPrintln("DEBUG: Unit is still searching for marker - may be hitting timeout (3 full rotations).");
              } else if (status == 4) {
                SerialPrintln("DEBUG: Unit reported calibration error/timeout.");
              }
              SerialPrintln("DEBUG: The unit will eventually timeout and set status to 0. System will proceed.");
              hasStuckUnit = true;
              // Don't reset allUnitsReady - allow system to proceed if most units are ready
            } else if (stuckDuration > 30000 && stuckDuration % 10000 < 200) {
              // Log every 10 seconds after 30 seconds
              SerialPrint("DEBUG: Unit ");
              SerialPrint(unitIndex);
              SerialPrint(" still busy (status=");
              SerialPrint(status);
              SerialPrint(") after ");
              SerialPrint(stuckDuration / 1000);
              if (status == 2) {
                SerialPrintln(" seconds (searching for marker)");
              } else if (status == 3) {
                SerialPrintln(" seconds (applying offset)");
              } else {
                SerialPrintln(" seconds (moving)");
              }
            }
          }
          allUnitsReady = false;
          consecutiveReadyChecks = 0; // Reset counter if any unit is busy
        } else if (status == 4) {
          // Calibration error - treat as busy but log the error
          SerialPrint("DEBUG: Unit ");
          SerialPrint(unitIndex);
          SerialPrintln(" reported calibration error/timeout (status = 4)");
          allUnitsReady = false;
          consecutiveReadyChecks = 0;
        } else if (status == 0) {
          // Unit is ready - clear stuck tracking and log if it was stuck
          if (unitStuckTracked[unitIndex]) {
            unsigned long stuckDuration = millis() - stuckUnitStartTime[unitIndex];
            SerialPrint("DEBUG: Unit ");
            SerialPrint(unitIndex);
            SerialPrint(" finished calibration after ");
            SerialPrint(stuckDuration / 1000);
            SerialPrintln(" seconds");
            unitStuckTracked[unitIndex] = false;
          }
          readyCount++;
        } else {
          // Unit is sleeping or not responding - clear stuck tracking
          if (unitStuckTracked[unitIndex]) {
            unitStuckTracked[unitIndex] = false;
          }
        }
      }
      
      // If we have a stuck unit that's been stuck for a long time, and most other units are ready,
      // allow the system to proceed (the stuck unit will eventually timeout and work)
      if (hasStuckUnit && readyCount >= (UNITS_AMOUNT - 1)) {
        SerialPrintln("DEBUG: Most units are ready. Stuck unit will continue calibrating in background.");
        SerialPrintln("DEBUG: System will proceed - stuck unit will timeout and be ready shortly.");
        break; // Exit wait loop
      }
      
      if (allUnitsReady) {
        consecutiveReadyChecks++;
        // Require multiple consecutive ready checks to ensure units are truly done
        // This handles the case where a unit finishes physically but status hasn't updated yet
        if (consecutiveReadyChecks >= REQUIRED_CONSECUTIVE_READY) {
          SerialPrint("DEBUG: All units finished calibration (");
          SerialPrint(readyCount);
          SerialPrint("/");
          SerialPrint(UNITS_AMOUNT);
          SerialPrintln(" units ready, confirmed over multiple checks)");
          break;
        }
      } else {
        consecutiveReadyChecks = 0; // Reset if any unit becomes busy again
      }
      
      calibrationCheckCount++;
      if (calibrationCheckCount % 10 == 0) {
        // Log progress every 10 checks (every ~2 seconds)
        SerialPrint("DEBUG: Waiting for units to calibrate... (");
        SerialPrint(readyCount);
        SerialPrint("/");
        SerialPrint(UNITS_AMOUNT);
        SerialPrint(" ready");
        if (consecutiveReadyChecks > 0) {
          SerialPrint(", ");
          SerialPrint(consecutiveReadyChecks);
          SerialPrint(" consecutive ready checks");
        }
        SerialPrintln(")");
      }
      
      yield(); // Allow web server to process requests
      delay(100); // Check every 100ms (faster checks, more yield() calls for web server)
    }
    
    if (millis() - calibrationWaitStart >= calibrationWaitTimeout) {
      SerialPrintln("DEBUG: WARNING - Calibration wait timeout. Some units may still be calibrating.");
      SerialPrintln("DEBUG: System will continue, but first command may timeout if units aren't ready.");
      
      // Log final status of all units
      SerialPrintln("DEBUG: Final unit status:");
      for (int unitIndex = 0; unitIndex < UNITS_AMOUNT; unitIndex++) {
        int status = checkIfMoving(unitIndex);
        SerialPrint("  Unit ");
        SerialPrint(unitIndex);
        SerialPrint(": ");
        if (status == 0) {
          SerialPrintln("READY");
        } else if (status == 1) {
          SerialPrintln("BUSY (still calibrating?)");
        } else {
          SerialPrintln("NOT RESPONDING");
        }
      }
    }
    */
    
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
  
  // Run pending I2C diagnostics (queued from web endpoint)
  if (i2cDiagnosticPending) {
    // Small delay to ensure web response was sent
    if (millis() - i2cDiagnosticStartTime > 100) {
      SerialPrintln("DEBUG: Running queued I2C diagnostics");
      i2cDiagnosticPending = false; // Clear flag before running (in case it takes a while)
      runI2CDiagnostics(i2cDiagnosticUnitAddress);
      SerialPrintln("DEBUG: I2C diagnostics completed");
    }
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
    yield(); // Allow web server and other tasks to process
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
    checkTrainStation();
    checkRandomPhrase();

    // Skip display updates if a web request is active to prevent blocking
    if (!webRequestActive) {
    //Mode Selection
      if (deviceMode == DEVICE_MODE_TEXT || deviceMode == DEVICE_MODE_COUNTDOWN || deviceMode == DEVICE_MODE_TRAIN_STATION || deviceMode == DEVICE_MODE_RANDOM_PHRASE) { 
      showText(inputText);
    } 
    else if (deviceMode == DEVICE_MODE_DATE) {
      showText(timezone.dateTime(dateFormat));
    } 
    else if (deviceMode == DEVICE_MODE_CLOCK) {
      // Use 24-hour or 12-hour format based on user preference
      String format = (clockFormat24Hour == "false") ? clockFormat12H : clockFormat24H;
      showText(timezone.dateTime(format.c_str()));
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
    
    // Add small delay after endTransmission to allow bus to settle
    delay(2);
    
    if (error == 0) {
      foundUnits[address] = true;
      foundCount++;
      
      SerialPrint("✓ Found device at address ");
      SerialPrint(address);
      
      // Try to read status from this address (only for addresses 0-15 which are our units)
      if (address < 16) {
        Wire.requestFrom(address, 1, 1);
        delay(2); // Delay after requestFrom
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
  
  // Store unit status in global arrays for static display on main page
  for (int i = 0; i < 16; i++) {
    staticFoundUnits[i] = foundUnits[i];
    staticUnitStatus[i] = unitStatus[i];
  }
  staticUnitStatusValid = true; // Mark data as valid
  
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
