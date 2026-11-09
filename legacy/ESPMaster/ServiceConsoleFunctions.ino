// Telnet console: `nc flappy.local 23` (or `telnet flappy.local`). Type "help".
// With SERIAL_ENABLE (default on ESP32) the same console also works over USB serial
// (Serial Monitor at SERIAL_BAUDRATE, line ending "Newline" or "Both NL & CR").
// Replies go to whichever side sent the command.
//
// Lets you test and calibrate units over WiFi without unplugging anything.
// Input is collected in consolePoll() (safe to call from anywhere via serviceNetwork());
// commands only run from consoleLoop() in the main loop, so they never re-enter display code.
#if CONSOLE_ENABLE == true

// ---- Unit I2C extension commands (need Unit.ino >= 1.3.0) --------------------------------
// Normal letter writes are [letter, speed] or [letter, speed, offsetLo, offsetHi].
// Letter indexes are < 45, so first bytes >= 0xF0 are free for calibration commands.
#define UNIT_CMD_HOME 0xF1  // [cmd, speed]                go to magnet edge, no offset, position = 0
#define UNIT_CMD_MOVE 0xF2  // [cmd, speed, lo, hi]        move N steps forward
#define UNIT_CMD_JOG  0xF3  // [cmd, msPerStep]            creep forward until STOP
#define UNIT_CMD_STOP 0xF4  // [cmd]                       stop jogging
#define UNIT_STATUS_SIZE 4  // status reply: [status, posLo, posHi, firmwareVersion]
#define UNIT_MIN_CAL_FIRMWARE 13  // 1.3.0
#define UNIT_STEPS_PER_REV 2038   // must match STEPS in Unit.ino

WiFiServer consoleServer(23);
WiFiClient consoleClient;
bool consoleStarted = false;
String consoleInput = "";
String consolePendingLine = "";
bool consoleLineReady = false;
bool consoleLastWasCR = false;
uint8_t consoleIacState = 0;
String consoleLogQueue = "";
#define CONSOLE_SRC_TELNET 0
#define CONSOLE_SRC_SERIAL 1
uint8_t consoleSource = CONSOLE_SRC_TELNET; //where the current command came from (replies go there)
String consoleSerialInput = "";
bool consoleSerialLastWasCR = false;

// (UnitStatus struct lives in Classes.h so Arduino's auto-generated prototypes can see it)

// ---- Output helpers ----------------------------------------------------------------------
bool consoleTelnetConnected() {
  return consoleClient && consoleClient.connected();
}

bool consoleConnected() {
#if SERIAL_ENABLE == true
  if (consoleSource == CONSOLE_SRC_SERIAL) return true;
#endif
  return consoleTelnetConnected();
}

void cprint(const String& text) {
#if SERIAL_ENABLE == true
  if (consoleSource == CONSOLE_SRC_SERIAL) { Serial.print(text); return; }
#endif
  if (consoleTelnetConnected()) consoleClient.print(text);
}

void cprintln(const String& text) {
  cprint(text);
  cprint("\r\n");
}

void cprintf(const char* format, ...) {
  char buffer[192];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  cprint(String(buffer));
}

void consolePrompt() {
  cprint(consolePaused ? "flappy (paused)> " : "flappy> ");
}

// Called from anywhere (including web/OTA callbacks): only queue, the socket write happens in consolePoll()
void consoleNotice(const String& text) {
  if (consoleLogQueue.length() < 1024) consoleLogQueue += "\r\n*** " + text + "\r\n";
}

void consoleLogLine(const String& line) {
  if (!consoleLogEnabled) return;
  if (consoleLogQueue.length() < 2048) consoleLogQueue += line + "\r\n";
}

// ---- Input -------------------------------------------------------------------------------
void consoleBegin() {
  consoleServer.begin();
  consoleServer.setNoDelay(true);
  consoleStarted = true;
#if SERIAL_ENABLE == true
  Serial.print("\r\nConsole ready on serial and telnet port 23 - type 'help' (serial: 'log off' to quiet the log)\r\n");
#endif
}

void consoleDiscardInput() {
  consoleLineReady = false;
  consolePendingLine = "";
  consoleInput = "";
  consoleSerialInput = "";
  while (consoleTelnetConnected() && consoleClient.available()) consoleClient.read();
#if SERIAL_ENABLE == true
  while (Serial.available()) Serial.read();
#endif
}

// Line editing shared by telnet and serial. Returns true when a line is complete.
bool consoleFeedChar(uint8_t c, String& input, bool& lastWasCR, uint8_t source) {
  if (c == '\r' || (c == '\n' && !lastWasCR)) {
    consolePendingLine = input;
    input = "";
    consoleLineReady = true;
    consoleSource = source;
    lastWasCR = (c == '\r');
    return true;
  }
  if (c == '\n' || c == 0) {
    lastWasCR = false;
    return false;
  }
  lastWasCR = false;
  if ((c == 8 || c == 127) && input.length() > 0) {
    input.remove(input.length() - 1);
  } else if (c >= 32 && input.length() < 120) {
    input += (char)c;
  }
  return false;
}

void consolePoll() {
  if (!consoleStarted) return;

  if (consoleServer.hasClient()) {
    WiFiClient incoming = consoleServer.accept();
    if (consoleTelnetConnected()) {
      consoleClient.print("\r\nAnother console connected - bye!\r\n");
      consoleClient.stop();
    }
    consoleClient = incoming;
    consoleClient.setNoDelay(true);
    consoleSource = CONSOLE_SRC_TELNET;
    consoleDiscardInput();
    consoleLogEnabled = false;
    cprintf("\r\nSplit-flap console - ESPMaster %s. Type 'help'.\r\n", espVersion);
    consolePrompt();
  }

  if (consoleTelnetConnected()) {
    if (consoleLogQueue.length() > 0) {
      consoleClient.print(consoleLogQueue);
      consoleLogQueue = "";
    }

    //Only read up to one complete line; the rest waits in the socket until it is consumed
    while (!consoleLineReady && consoleClient.available()) {
      uint8_t c = consoleClient.read();

      //Ignore telnet option negotiation (IAC sequences)
      if (consoleIacState == 1) { consoleIacState = (c >= 0xFB && c <= 0xFE) ? 2 : 0; continue; }
      if (consoleIacState == 2) { consoleIacState = 0; continue; }
      if (c == 0xFF) { consoleIacState = 1; continue; }

      consoleFeedChar(c, consoleInput, consoleLastWasCR, CONSOLE_SRC_TELNET);
    }
  } else {
    consoleLogQueue = ""; //nobody to show it to
  }

#if SERIAL_ENABLE == true
  while (!consoleLineReady && Serial.available()) {
    uint8_t c = Serial.read();
    bool done = consoleFeedChar(c, consoleSerialInput, consoleSerialLastWasCR, CONSOLE_SRC_SERIAL);
    //Echo, so terminal programs (screen, minicom) show what you type
    if (done) Serial.print("\r\n");
    else if (c == 8 || c == 127) Serial.print("\b \b");
    else if (c >= 32) Serial.write(c);
  }
#endif
}

// Wait for one line of input while keeping OTA/mDNS alive. Returns false on timeout or disconnect.
bool consoleReadLine(String& line, unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    serviceNetwork();
    if (!consoleConnected()) return false;
    if (consoleLineReady) {
      line = consolePendingLine;
      consoleLineReady = false;
      return true;
    }
    delay(5);
  }
  return false;
}

// Like delay(), but keeps the network serviced
void consoleWait(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    serviceNetwork();
    delay(5);
  }
}

// ---- Unit helpers ------------------------------------------------------------------------
uint8_t unitSend(int address, const uint8_t* data, size_t length) {
  Wire.beginTransmission(address);
  Wire.write(data, length);
  return Wire.endTransmission();
}

UnitStatus unitReadStatus(int address) {
  UnitStatus result = {false, -1, 0, 255};
  uint8_t received = Wire.requestFrom(address, UNIT_STATUS_SIZE);
  if (received >= 1) {
    uint8_t raw[UNIT_STATUS_SIZE] = {255, 255, 255, 255};
    for (int i = 0; i < received && i < UNIT_STATUS_SIZE; i++) raw[i] = Wire.read();
    while (Wire.available()) Wire.read();
    result.responded = true;
    result.status = (int8_t)raw[0];
    result.version = raw[3];
    if (result.version != 255) result.position = raw[1] | (raw[2] << 8);
  }
  return result;
}

const char* unitStatusName(int status) {
  switch (status) {
    case 0: return "ready";
    case 1: return "moving";
    case 2: return "calibrating (searching for magnet)";
    case 3: return "calibrating (applying offset)";
    case 4: return "CALIBRATION ERROR (magnet not found)";
    default: return "unknown";
  }
}

String unitVersionName(int version) {
  if (version == 255) return "< 1.3.0";
  return String(version / 10) + "." + String(version % 10) + ".0";
}

// Wait until a unit reports ready (or error). Returns the last status, or -1 if it never answered.
int unitWaitReady(int address, unsigned long timeoutMs) {
  unsigned long start = millis();
  consoleWait(60);
  int last = -1;
  while (millis() - start < timeoutMs) {
    UnitStatus status = unitReadStatus(address);
    if (status.responded) {
      last = status.status;
      if (last == 0 || last == 4) return last;
    }
    consoleWait(100);
  }
  return last;
}

int consoleFlapSpeed() {
  return convertSpeed(flapSpeed.length() > 0 ? flapSpeed : "80");
}

int letterIndexFromInput(String text) {
  String lower = text;
  lower.toLowerCase();
  lower.trim();
  if (text == " " || lower == "blank" || lower == "space" || lower == "_") return 0;
  if (lower.length() == 0) return -1;
  char c = text.charAt(0) == ' ' ? text.charAt(1) : text.charAt(0);
  if (c >= 'a' && c <= 'z') c -= 32;
  return translateLettertoInt(c);
}

String letterName(int index) {
  if (index == 0) return "blank";
  return "'" + String(letters[index]) + "'";
}

bool parseUnitAddress(const String& text, int& address) {
  if (text.length() == 0) return false;
  for (unsigned int i = 0; i < text.length(); i++) if (!isDigit(text.charAt(i))) return false;
  address = text.toInt();
  return address >= 0 && address < 16;
}

// ---- Commands ----------------------------------------------------------------------------
void consoleHelp() {
  cprint(F(
    "Display\r\n"
    "  show <text>          show text (left aligned), pauses the normal mode\r\n"
    "  unit <addr> <char>   show one character on one unit (use 'blank' for the blank flap)\r\n"
    "  ident                each unit shows its own address: 0123456789 left to right if wired right\r\n"
    "  pause / resume       stop / restart the normal display mode (clock, text, ...)\r\n"
    "Units\r\n"
    "  status [all]         status + firmware of each unit (all = scan addresses 0-15)\r\n"
    "  scan                 list I2C addresses that answer\r\n"
    "  cal <addr> [flips]   interactive calibration: press Enter whenever a flap flips (default 5)\r\n"
    "  offset <addr> <n>    set a unit's calibration offset (steps after the magnet) and re-home\r\n"
    "  home <addr>          move a unit to the magnet edge (no offset) - for checking the sensor\r\n"
    "System\r\n"
    "  info                 IP, memory, flash size, uptime, WiFi signal\r\n"
    "  log on|off           debug log on this console (telnet: off by default, serial: on)\r\n"
    "  reboot               restart the ESP\r\n"
    "  quit                 close this connection (telnet)\r\n"));
}

void consoleInfo() {
  cprintf("ESPMaster %s, hostname %s.local, IP %s, RSSI %d dBm\r\n", espVersion, MDNS_HOSTNAME,
          WiFi.localIP().toString().c_str(), WiFi.RSSI());
#if defined(ESP32)
  const char* reasons[] = {"unknown", "power on", "external pin", "software", "crash", "interrupt watchdog",
                           "task watchdog", "other watchdog", "deep sleep", "brownout", "SDIO"};
  int reason = (int)esp_reset_reason();
  cprintf("Uptime %lus, ESP32 %d MHz, reset reason: %s\r\n", millis() / 1000, ESP.getCpuFreqMHz(),
          reason >= 0 && reason <= 10 ? reasons[reason] : "other");
  cprintf("Heap free %u bytes, largest block %u, lowest ever %u\r\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
  cprintf("Flash chip %u KB, sketch %u KB, free for OTA %u KB\r\n",
          ESP.getFlashChipSize() / 1024, ESP.getSketchSize() / 1024, ESP.getFreeSketchSpace() / 1024);
#else
  cprintf("Uptime %lus, CPU %d MHz, reset reason: %s\r\n", millis() / 1000, ESP.getCpuFreqMHz(), ESP.getResetReason().c_str());
  cprintf("Heap free %u bytes, largest block %u, fragmentation %u%%\r\n", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(), ESP.getHeapFragmentation());
  cprintf("Flash chip %u KB (IDE setting %u KB), sketch %u KB, free for OTA %u KB\r\n",
          ESP.getFlashChipRealSize() / 1024, ESP.getFlashChipSize() / 1024, ESP.getSketchSize() / 1024, ESP.getFreeSketchSpace() / 1024);
#endif
  cprintf("Mode %s, text \"%s\", speed %s, units %d, logging %s\r\n", deviceMode.c_str(), lastWrittenText.c_str(),
          flapSpeed.c_str(), UNITS_AMOUNT, LOG_ENABLE ? "compiled in" : "compiled out");
}

void consoleStatus(bool all) {
  int last = all ? 16 : UNITS_AMOUNT;
  for (int address = 0; address < last; address++) {
    UnitStatus status = unitReadStatus(address);
    if (!status.responded) {
      if (!all || address < UNITS_AMOUNT) cprintf("  unit %2d: no answer\r\n", address);
      continue;
    }
    cprintf("  unit %2d: %-36s firmware %s%s\r\n", address, unitStatusName(status.status),
            unitVersionName(status.version).c_str(), address >= UNITS_AMOUNT ? "  (beyond UNITS_AMOUNT!)" : "");
    serviceNetwork();
  }
}

void consoleScan() {
  String found = "";
  for (int address = 0; address < 128; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) found += " " + String(address);
    if (address % 16 == 0) serviceNetwork();
  }
  cprintln(found.length() > 0 ? "Answering addresses:" + found : "No I2C devices answered");
}

void consoleShow(String text) {
  consolePaused = true;
  text.toUpperCase();
  int speed = consoleFlapSpeed();
  for (int unit = 0; unit < UNITS_AMOUNT; unit++) {
    char c = unit < (int)text.length() ? text.charAt(unit) : ' ';
    int index = translateLettertoInt(c);
    if (index < 0) {
      cprintf("  '%c' is not on the flaps - unit %d shows blank\r\n", c, unit);
      index = 0;
    }
    writeToUnit(unit, index, speed);
  }
  lastWrittenText = ""; //so "resume" redraws the normal mode
}

void consoleIdent() {
  String text = "";
  for (int unit = 0; unit < UNITS_AMOUNT; unit++) {
    text += unit < 10 ? (char)('0' + unit) : (char)('A' + unit - 10);
  }
  consoleShow(text);
  cprintf("Each unit shows its I2C address. Reading left to right you should see %s.\r\n", text.c_str());
  cprintln("If it reads backwards or out of order, the DIP switches don't match the physical order.");
}

// Write a new offset and make the unit re-home with it. Works with old unit firmware too:
// a 4-byte letter command stores the offset, then moving to a lower letter forces a full
// rotation, which is where the unit re-homes and applies the offset.
bool consoleApplyOffset(int address, uint16_t offset) {
  int speed = consoleFlapSpeed();
  uint8_t store[4] = {1, (uint8_t)speed, (uint8_t)(offset & 0xFF), (uint8_t)(offset >> 8)};
  if (unitSend(address, store, 4) != 0) {
    cprintf("Unit %d did not acknowledge\r\n", address);
    return false;
  }
  consoleWait(2200); //unit ignores moves within 2 s of the previous one (overheating guard)
  unitWaitReady(address, 20000);
  uint8_t blank[2] = {0, (uint8_t)speed};
  unitSend(address, blank, 2);
  cprintf("Offset %u stored on unit %d - re-homing (full turn)...\r\n", offset, address);
  consoleWait(2500); //unit waits 2 s between moves (overheating guard) before it starts
  int status = unitWaitReady(address, 30000);
  if (status == 4) {
    cprintln("Unit reports a calibration error - magnet not found.");
    return false;
  }
  cprintf("Done. Unit %d should show the blank flap. Try: unit %d A, unit %d 9\r\n", address, address, address);
  return true;
}

void consoleHome(int address) {
  UnitStatus info = unitReadStatus(address);
  if (!info.responded) { cprintf("Unit %d does not answer\r\n", address); return; }
  if (info.version == 255 || info.version < UNIT_MIN_CAL_FIRMWARE) {
    cprintf("Unit %d runs firmware %s - 'home' and 'cal' need Unit.ino 1.3.0+\r\n", address, unitVersionName(info.version).c_str());
    return;
  }
  consolePaused = true;
  uint8_t home[2] = {UNIT_CMD_HOME, 10};
  unitSend(address, home, 2);
  cprintf("Unit %d homing...\r\n", address);
  int status = unitWaitReady(address, 30000);
  if (status == 0) cprintln("At the magnet edge (no offset applied). Send any letter to leave this mode.");
  else cprintln("Homing failed - is the magnet/hall sensor OK?");
}

// Interactive calibration, same idea as InteractiveCalibration.ino but over WiFi:
// home to the magnet edge, creep forward, user presses Enter each time a flap flips.
// A straight-line fit of the flip positions gives the steps where the blank flap flips;
// the offset is that plus half a flap, so the blank sits in the middle of its window.
void consoleCalibrate(int address, int flipsWanted) {
  UnitStatus info = unitReadStatus(address);
  if (!info.responded) { cprintf("Unit %d does not answer\r\n", address); return; }
  if (info.version == 255 || info.version < UNIT_MIN_CAL_FIRMWARE) {
    cprintf("Unit %d runs firmware %s - cal needs Unit.ino 1.3.0+. You can still use 'offset'.\r\n",
            address, unitVersionName(info.version).c_str());
    return;
  }
  if (flipsWanted < 2) flipsWanted = 2;
  if (flipsWanted > 10) flipsWanted = 10;

  const float theoreticalStepsPerFlap = (float)UNIT_STEPS_PER_REV / FLAP_AMOUNT;
  consolePaused = true;
  uint8_t stopCmd[1] = {UNIT_CMD_STOP};
  uint8_t exitCmd[2] = {0, (uint8_t)consoleFlapSpeed()}; //any letter command leaves calibration mode

  cprintf("Calibrating unit %d. Homing to the magnet...\r\n", address);
  uint8_t home[2] = {UNIT_CMD_HOME, 10};
  unitSend(address, home, 2);
  if (unitWaitReady(address, 30000) != 0) {
    cprintln("Homing failed - magnet not found. Check the hall sensor and magnet.");
    unitSend(address, exitCmd, 2);
    return;
  }

  int seenIndex = -1;
  String line;
  while (seenIndex < 0) {
    cprint("Which flap is showing now? Type it and press Enter ('blank' for the blank flap, x = abort): ");
    if (!consoleReadLine(line, 180000) || line == "x" || line == "X") {
      cprintln("Aborted.");
      unitSend(address, exitCmd, 2);
      return;
    }
    seenIndex = letterIndexFromInput(line);
    if (seenIndex < 0) cprintln("That character is not on the flaps, try again.");
  }

  long flips[10];
  int flipCount = 0;
  long lastPosition = 0;

  cprintln("The unit will now creep forward. Press Enter the moment each new flap drops.");
  cprintln("(q + Enter = finish early, x + Enter = abort)");

  for (int k = 1; k <= flipsWanted; k++) {
    int target = (seenIndex + k) % FLAP_AMOUNT;

    //After the first flip we know roughly where the next one is: go most of the way quickly
    if (flipCount >= 1) {
      float perFlap = flipCount >= 2 ? (float)(flips[flipCount - 1] - flips[0]) / (flipCount - 1) : theoreticalStepsPerFlap;
      int ahead = (int)(perFlap - 15);
      if (ahead > 0) {
        uint8_t move[4] = {UNIT_CMD_MOVE, 8, (uint8_t)(ahead & 0xFF), (uint8_t)(ahead >> 8)};
        unitSend(address, move, 4);
        unitWaitReady(address, 15000);
      }
    }

    consoleDiscardInput(); //ignore Enters typed while it was moving
    cprintf("  waiting for %s ... ", letterName(target).c_str());
    uint8_t jog[2] = {UNIT_CMD_JOG, 80}; //one step every 80 ms
    unitSend(address, jog, 2);

    bool gotFlip = false;
    unsigned long lastPoll = millis();
    unsigned long start = millis();
    while (true) {
      serviceNetwork();
      if (!consoleConnected()) { unitSend(address, stopCmd, 1); unitSend(address, exitCmd, 2); return; }

      if (consoleLineReady) {
        line = consolePendingLine;
        consoleLineReady = false;
        unitSend(address, stopCmd, 1);
        consoleWait(40);
        UnitStatus now = unitReadStatus(address);
        if (line == "x" || line == "X") {
          cprintln("aborted.");
          unitSend(address, exitCmd, 2);
          return;
        }
        if (line == "q" || line == "Q") {
          cprintln("finishing.");
          break;
        }
        flips[flipCount++] = now.position;
        lastPosition = now.position;
        cprintf("flipped at step %d\r\n", now.position);
        gotFlip = true;
        break;
      }

      if (millis() - lastPoll > 250) {
        lastPoll = millis();
        UnitStatus now = unitReadStatus(address);
        //More than ~2.5 flaps without an Enter: the user missed it
        if (now.responded && now.position - lastPosition > (long)(theoreticalStepsPerFlap * 2.5f)) {
          unitSend(address, stopCmd, 1);
          cprintln("\r\nNo Enter for over two flaps - stopping. Run cal again.");
          unitSend(address, exitCmd, 2);
          return;
        }
      }
      if (millis() - start > 120000) {
        unitSend(address, stopCmd, 1);
        cprintln("\r\nTimed out.");
        unitSend(address, exitCmd, 2);
        return;
      }
      delay(5);
    }
    if (!gotFlip) break;
  }

  if (flipCount < 1) {
    cprintln("No flips recorded - nothing to calculate.");
    unitSend(address, exitCmd, 2);
    return;
  }

  //Least-squares line: flip k (1..n) happens at  a + b*k  steps after the magnet edge
  float b = theoreticalStepsPerFlap;
  float a;
  if (flipCount >= 2) {
    float sumK = 0, sumP = 0, sumKK = 0, sumKP = 0;
    for (int i = 0; i < flipCount; i++) {
      float k = i + 1;
      sumK += k; sumP += flips[i]; sumKK += k * k; sumKP += k * flips[i];
    }
    b = (flipCount * sumKP - sumK * sumP) / (flipCount * sumKK - sumK * sumK);
    a = (sumP - b * sumK) / flipCount;
  } else {
    a = flips[0] - b;
  }

  //The blank flap is index 0, i.e. k0 flips after the one that was showing at the magnet
  int k0 = (FLAP_AMOUNT - seenIndex) % FLAP_AMOUNT;
  float blankFlip = a + b * k0;
  long offset = lroundf(blankFlip + b / 2.0f);
  while (offset < 0) offset += UNIT_STEPS_PER_REV;
  while (offset >= UNIT_STEPS_PER_REV) offset -= UNIT_STEPS_PER_REV;

  cprintln("");
  cprintf("Measured %d flips: %.2f steps per flap (firmware assumes %.2f)\r\n", flipCount, b, theoreticalStepsPerFlap);
  if (flipCount >= 3 && fabsf(b - theoreticalStepsPerFlap) > 0.6f) {
    cprintln("WARNING: that differs a lot from the firmware's value. Letters late in the alphabet and the");
    cprintln("         digits will drift. Re-run cal; if it repeats, the motor's steps/rev differs from 2038.");
  }
  cprintf("Blank flap drops at step %.0f -> new offset %ld (blank flip + half a flap)\r\n", blankFlip, offset);

  cprint("Save this offset to the unit? (y/n): ");
  if (consoleReadLine(line, 180000) && (line == "y" || line == "Y")) {
    consoleApplyOffset(address, (uint16_t)offset);
  } else {
    cprintln("Not saved. Unit re-homing with its old offset.");
    unitSend(address, exitCmd, 2);
  }
  cprintln("The display stays paused - type 'resume' when you're done.");
}

void consoleExecute(String line) {
  line.trim();
  if (line.length() == 0) return;

  int space = line.indexOf(' ');
  String command = space < 0 ? line : line.substring(0, space);
  String rest = space < 0 ? "" : line.substring(space + 1);
  command.toLowerCase();
  rest.trim();
  String arg1 = rest;
  String arg2 = "";
  int space2 = rest.indexOf(' ');
  if (space2 >= 0) {
    arg1 = rest.substring(0, space2);
    arg2 = rest.substring(space2 + 1);
  }

  int address;
  if (command == "help" || command == "?") {
    consoleHelp();
  } else if (command == "info") {
    consoleInfo();
  } else if (command == "status") {
    consoleStatus(arg1 == "all");
  } else if (command == "scan") {
    consoleScan();
  } else if (command == "show") {
    consoleShow(rest);
  } else if (command == "ident") {
    consoleIdent();
  } else if (command == "unit") {
    if (!parseUnitAddress(arg1, address) || arg2.length() == 0) { cprintln("usage: unit <addr> <char>"); return; }
    int index = letterIndexFromInput(arg2);
    if (index < 0) { cprintln("That character is not on the flaps"); return; }
    consolePaused = true;
    writeToUnit(address, index, consoleFlapSpeed());
    lastWrittenText = "";
  } else if (command == "pause") {
    consolePaused = true;
  } else if (command == "resume") {
    consolePaused = false;
    lastWrittenText = ""; //force the normal mode to redraw
  } else if (command == "offset") {
    long value = arg2.toInt();
    if (!parseUnitAddress(arg1, address) || arg2.length() == 0 || !isDigit(arg2.charAt(0)) || value >= UNIT_STEPS_PER_REV) {
      cprintln("usage: offset <addr> <steps 0-2037>");
      return;
    }
    consolePaused = true;
    consoleApplyOffset(address, (uint16_t)value);
  } else if (command == "cal") {
    if (!parseUnitAddress(arg1, address)) { cprintln("usage: cal <addr> [flips]"); return; }
    consoleCalibrate(address, arg2.length() > 0 ? arg2.toInt() : 5);
  } else if (command == "home") {
    if (!parseUnitAddress(arg1, address)) { cprintln("usage: home <addr>"); return; }
    consoleHome(address);
  } else if (command == "log") {
#if LOG_ENABLE == true
    if (consoleSource == CONSOLE_SRC_SERIAL) {
      serialLogEnabled = (arg1 == "on");
      cprintln(serialLogEnabled ? "Serial log on" : "Serial log off");
    } else {
      consoleLogEnabled = (arg1 == "on");
      cprintln(consoleLogEnabled ? "Log streaming on" : "Log streaming off");
    }
#else
    cprintln("Logging is compiled out. Set LOG_ENABLE true in ESPMaster.ino and re-upload.");
#endif
  } else if (command == "reboot") {
    cprintln("Rebooting...");
    if (consoleTelnetConnected()) consoleClient.flush();
    isPendingReboot = true;
  } else if (command == "quit" || command == "exit") {
    if (consoleSource == CONSOLE_SRC_SERIAL) { cprintln("(quit only closes telnet sessions)"); return; }
    cprintln("Bye!");
    consoleClient.stop();
  } else {
    cprintln("Unknown command - type 'help'");
  }
}

// Called from loop(): run one complete command line, if any
void consoleLoop() {
  if (!consoleLineReady) return;
  String line = consolePendingLine;
  consoleLineReady = false;
  consoleExecute(line);
  if (consoleConnected()) consolePrompt();
}

#endif
