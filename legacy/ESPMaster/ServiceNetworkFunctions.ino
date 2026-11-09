// mDNS (flappy.local), always-on OTA, and the background "network service" pump.
//
// serviceNetwork() must be called often: from loop() and from inside any long wait
// (display waits, calibration), so OTA uploads and the telnet console stay responsive
// even while the display is busy.

void startNetworkServices() {
#if OTA_ENABLE == true
  ArduinoOTA.setHostname(MDNS_HOSTNAME);
  if (strlen(otaPassword) > 0) {
    ArduinoOTA.setPassword(otaPassword);
  }

  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    statusLedSet(LED_CODE_OTA);
    LittleFS.end();
    SerialPrintln(ArduinoOTA.getCommand() == U_FLASH ? "OTA: updating sketch" : "OTA: updating filesystem");
#if CONSOLE_ENABLE == true
    consoleNotice("OTA update started - the device will reboot when it finishes");
#endif
  });

  ArduinoOTA.onEnd([]() {
    SerialPrintln("OTA: finished - rebooting");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    statusLedSet(LED_CODE_RUNNING);
    SerialPrint("OTA error: ");
    SerialPrintln((int)error);
#if CONSOLE_ENABLE == true
    consoleNotice("OTA update failed (error " + String((int)error) + ")");
#endif
  });

  //Also starts mDNS with our hostname and advertises the Arduino OTA service,
  //which is what makes "flappy at x.x.x.x" show up under Tools > Port in the IDE
#if defined(ESP32)
  ArduinoOTA.begin(); //mDNS is on by default on ESP32
#else
  ArduinoOTA.begin(true);
#endif
  isInOtaMode = true;
#else
  MDNS.begin(MDNS_HOSTNAME);
#endif

  MDNS.addService("http", "tcp", 80);
#if CONSOLE_ENABLE == true
  MDNS.addService("telnet", "tcp", 23);
  consoleBegin();
#endif

  SerialPrint("Network services started: http://");
  SerialPrint(MDNS_HOSTNAME);
  SerialPrintln(".local");
}

void serviceNetwork() {
  statusLedAlive();

#if OTA_ENABLE == true
  if (isInOtaMode) {
    ArduinoOTA.handle(); //on ESP8266 this also runs MDNS.update()
  }
#elif !defined(ESP32)
  MDNS.update(); //ESP32 runs mDNS in its own task
#endif

#if CONSOLE_ENABLE == true
  consolePoll();
#endif
}
