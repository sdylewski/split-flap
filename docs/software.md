# Software Configuration & Usage

This guide covers everything required to configure the firmware, calibrate the
units, and operate/debug the split-flap display.

## 1. Firmware Components

| Component | Location | Purpose |
| --- | --- | --- |
| ESPMaster (ESP-01S) | `ESPMaster/` | Web server, WiFi, scheduler, diagnostics |
| Unit firmware (Nano) | `Unit/Unit.ino` | Drives each flap drum |
| EEPROM offset tool | `EEPROM_Write_Offset/EEPROM_Write_Offset.ino` | Calibrates per-unit zero position |

## 2. Toolchain & Libraries

1. Install the ESP8266 board package in Arduino IDE  
   [Guide](https://randomnerdtutorials.com/how-to-install-esp8266-board-arduino-ide/)
2. Install the following libraries via Library Manager (versions work on IDE 1.x
   **and** 2.x):
   - ArduinoJson 7.0.4 (any 7.x)
   - ESPAsyncWebSrv 1.2.7 (+ ESPAsyncTCP / AsyncTCP dependencies)
   - NTPClient 3.2.1
   - ezTime 0.8.3
   - LinkedList 1.3.3
   - WiFiManager 2.0.17
3. Install the LittleFS upload plugin:
   - **IDE 2.x:**
     1. Download the latest `.vsix` file from
        https://github.com/earlephilhower/arduino-littlefs-upload/releases
     2. Copy the `.vsix` file to the Arduino IDE plugins folder:
        - **macOS/Linux:** `~/.arduinoIDE/plugins/`
        - **Windows:** `C:\Users\<your_username>\.arduinoIDE\plugins\`
        - Create the `plugins` folder if it doesn't exist
     3. Restart Arduino IDE 2.x completely
     4. The plugin will appear in the command palette (see upload steps below)
   - **IDE 1.x:** install the legacy plugin and use *Tools → ESP8266 LittleFS
     Data Upload*.

> **Important:** Before uploading LittleFS, always close the Serial Monitor. The uploader needs exclusive access to the serial port. If you see "Resource busy" or "could not open port" errors, close the Serial Monitor and any other programs using the port, then retry.

> **Note:** The codebase works unmodified on Arduino IDE 2.x. If you encounter
> compilation issues, update libraries to their latest minor versions.

## 3. Upload Sequence

1. **LittleFS assets**
   - Open `ESPMaster/ESPMaster.ino` in Arduino IDE
   - **Important:** Close the Serial Monitor if it's open (the serial port must be free)
   - Select *Generic ESP8266 Module* from the board menu
   - Select the correct COM port for your ESP-01S programmer
   - **IDE 2.x:** Press `⌘ + Shift + P` (macOS) or `Ctrl + Shift + P` (Windows/Linux) to open the command palette, then type "Upload LittleFS" and select it
   - **IDE 1.x:** Use *Tools → ESP8266 LittleFS Data Upload* from the menu
   - Wait for the upload to complete (you'll see "LittleFS Image Uploaded" in the output)
   - **Important:** After LittleFS upload completes, unplug and replug the ESP-01S programmer to reset the device before proceeding to firmware upload
2. **ESPMaster firmware**
   - After LittleFS completes and you've power-cycled the ESP-01S, ensure it's in programming mode (GPIO0 LOW)
   - Use *Upload* to flash the sketch itself
   - If upload fails with "device disconnected" errors, try lowering the upload speed to 115200 baud in *Tools → Upload Speed*
3. **Unit firmware**
   - For each Arduino Nano, open `Unit/Unit.ino`
   - Select *Arduino Nano* (use *ATmega328P (Old Bootloader)* if uploads fail)
   - Flash the sketch
4. **EEPROM offset utility**
   - Upload `EEPROM_Write_Offset.ino` to each unit when calibrating (see below)

## 4. Configuration Options

At the top of `ESPMaster.ino`, configurable `#define` blocks control behaviour:

- `WIFI_USE_DIRECT` – set `true` for infrastructure WiFi, `false` for AP setup
- `WIFI_STATIC_IP` – enable to assign a static IP (update the IP/DNS settings
  directly below)
- `ESP01S_LED_ENABLE` – toggles the ESP-01S onboard LED error indicator
- `SERIAL_ENABLE`, `OTA_ENABLE`, `UNIT_CALLS_DISABLE` – enable serial logging,
  OTA updates and ESP-only testing respectively
- `DEBUG_ENABLE` – exposes the startup debug page, error status panel, page-load
  log and serial log in the web UI

Other important settings:

- `timezoneString` – set to a TZ database name (e.g. `America/Los_Angeles`)
- Clock/date formatting – see https://github.com/ropg/ezTime#datetime
- WiFi credentials for direct mode: update `wifiDirectSsid` and
  `wifiDirectPassword`

## 5. Calibration Workflow

1. **Set Zero Position Offset**
   - Upload `EEPROM_Write_Offset.ino` to a unit
   - Open Serial Monitor @ 115200 baud
   - Note the current offset, enter new values until the blank flap aligns
     reliably (typically ~100 steps)
   - Reflash `Unit.ino` when satisfied
2. **Configure Unit Addresses**
   - DIP switch (SW1) bits: SW1=bit3 (value 8)…SW4=bit0 (value 1)
   - Switch **up = ON = 1**, **down = OFF = 0**
   - Address = SW4×1 + SW3×2 + SW2×4 + SW1×8
   - Units must be sequential starting at 0
   - Verify via the `/i2c-scan` endpoint in the ESP web UI
3. **Integration Test**
   - Power the full chain
   - Use the web UI text mode or schedule messages
   - Confirm all units respond (watch serial/I²C diagnostics)

## 6. Web Interface & Usage

- **Text mode:** send ad-hoc messages; long messages auto-split or honour `\n`
- **Countdown / date / clock modes:** switch via the web UI
- **Scheduling:** queue messages with timestamps or “show indefinitely”
- **WiFi strength indicator:** view RSSI + qualitative label on the settings
  panel
- **Log viewer:** shows the last 50 serial log entries and can copy to clipboard

## 7. Debugging Tools

### 7.1 Web UI diagnostics

- **Error status panel:** shows network/JavaScript errors at the top of the page
- **Page load debug log:** records lifecycle events (DOMContentLoaded, AJAX
  statuses, JSON parsing errors)
- **Serial log viewer:** mirrors the circular buffer from the ESP firmware with
  hide/show toggle and “Copy Full Log” button
- **Startup debug page:** accessible when `DEBUG_ENABLE` is true; shows the full
  boot log before redirecting to the main UI

### 7.2 LED indicator (ESP-01S only)

- Continuous blinking = critical error (e.g., WiFi connection failed)
- Requires `ESP01S_LED_ENABLE` to remain `true` and an ESP-01S module (LED on
  GPIO2, active LOW)

### 7.3 Serial logging

Enable `SERIAL_ENABLE` for verbose firmware logs. Useful during calibration or
when diagnosing I²C issues (`ServiceFlapFunctions.ino` reports error codes,
timeouts, sleeping units, etc.).

## 8. Common Issues & Fixes

| Symptom | Recommendation |
| --- | --- |
| LittleFS upload fails: "Resource busy" or "could not open port" | Close the Serial Monitor and any other programs using the serial port, then retry. On macOS, you may need to quit and restart Arduino IDE if the port remains locked. |
| Upload fails: "device reports readiness to read but returned no data" or "device disconnected" | **ESP-01/ESP-01S programming mode:** Ensure GPIO0 is pulled LOW and the device is reset into programming mode. Many ESP-01 programmers have a button or switch for this. Try: 1) Lower upload baud rate to 115200 or 57600 in Tools menu, 2) Press and hold the programming button (if available) during upload, 3) Power cycle the ESP-01S and immediately start upload, 4) Check USB cable/programmer connection. |
| Web UI hangs on load | Ensure LittleFS assets were uploaded; check `/settings` endpoint logs |
| `/settings` times out | Confirm `webRequestActive` is not held true (long-running display updates); inspect serial log |
| Units unresponsive / `I2C error 2` | Verify DIP switch addresses, wiring continuity, and use `/i2c-scan` |
| Unit stuck in "busy" state (status 1) | 1) Use `/i2c-scan` endpoint to check all unit addresses, 2) Verify DIP switch on the stuck unit (no conflicts with other units), 3) Check if unit's stepper motor is physically stuck, 4) Power cycle the stuck unit, 5) Re-upload `Unit.ino` firmware to the stuck unit, 6) Check I2C wiring to that specific unit |
| Incorrect character after homing | Re-run EEPROM offset calibration for that unit |
| WiFi unstable | Try static IP or extend antenna (per community notes); verify RSSI via UI |
| LED indicator off | Confirm module is ESP-01S (original ESP-01 LED conflicts with I²C) |

## 9. Additional Resources

- [`docs/README.md`](./README.md) – documentation hub
- [`docs/hardware-build.md`](./hardware-build.md) – mechanical assembly steps
- [`docs/printing.md`](./printing.md) – BOM and printing instructions
- [`docs/SplitFlapInstructions.md`](./SplitFlapInstructions.md) – legacy detailed instructions

With the firmware configured and calibrated, you can rely on the web interface
for day-to-day control, scheduling and diagnostics. Happy flipping!

