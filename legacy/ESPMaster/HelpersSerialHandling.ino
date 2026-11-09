// Debug logging back-end.
//
// Code calls SerialPrint / SerialPrintln / SerialPrintf (macros defined in ESPMaster.ino).
// With LOG_ENABLE false those macros expand to nothing, so this file is only used when
// LOG_ENABLE is true. Output then goes to:
//   - Serial (only if SERIAL_ENABLE - on an ESP-01 that disables I2C!), unless "log off" was typed there
//   - the web UI log buffer (/log)
//   - the telnet console, if a client is connected and typed "log on"
#if LOG_ENABLE == true

static void logEmitLine(const String& line) {
  addToSerialLog(line);
#if CONSOLE_ENABLE == true
  consoleLogLine(line);
#endif
}

template <typename T>
void logPrint(T value) {
#if SERIAL_ENABLE == true
  if (serialLogEnabled) Serial.print(value);
#endif
  currentSerialLine += String(value);
}

void logPrint(IPAddress value) {
#if SERIAL_ENABLE == true
  if (serialLogEnabled) Serial.print(value);
#endif
  currentSerialLine += value.toString();
}

template <typename T>
void logPrintf(const char* message, T value) {
#if SERIAL_ENABLE == true
  if (serialLogEnabled) Serial.printf(message, value);
#endif
  char buffer[160];
  snprintf(buffer, sizeof(buffer), message, value);
  logEmitLine(String(buffer));
}

template <typename T>
void logPrintln(T value) {
#if SERIAL_ENABLE == true
  if (serialLogEnabled) Serial.println(value);
#endif
  currentSerialLine += String(value);
  logEmitLine(currentSerialLine);
  currentSerialLine = "";
}

void logPrintln(IPAddress value) {
#if SERIAL_ENABLE == true
  if (serialLogEnabled) Serial.println(value);
#endif
  currentSerialLine += value.toString();
  logEmitLine(currentSerialLine);
  currentSerialLine = "";
}

void logPrintln() {
#if SERIAL_ENABLE == true
  if (serialLogEnabled) Serial.println();
#endif
  logEmitLine(currentSerialLine);
  currentSerialLine = "";
}

#endif
