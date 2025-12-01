# Software Configuration & Usage

This guide covers everything required to configure the firmware, calibrate the
units, and operate/debug the split-flap display.

## 1. Firmware Components

| Component | Location | Purpose |
| --- | --- | --- |
| ESPMaster (ESP-01S) | `ESPMaster/` | Web server, WiFi, scheduler, diagnostics |
| Unit firmware (Nano) | `Unit/Unit.ino` | Drives each flap drum |
| Interactive Calibration | `InteractiveCalibration/InteractiveCalibration.ino` | **Recommended:** Guided calibration tool with automatic offset calculation |
| EEPROM offset tool | `EEPROM_Write_Offset/EEPROM_Write_Offset.ino` | **Backup:** Basic utility to manually set per-unit zero position |

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
4. **Calibration tools** (see Calibration Workflow section below)
   - **Recommended:** Use `InteractiveCalibration/InteractiveCalibration.ino` for guided calibration
   - **Backup:** Use `EEPROM_Write_Offset/EEPROM_Write_Offset.ino` if you need to manually enter a known offset value

## 4. Over-The-Air (OTA) Updates

OTA allows you to update the ESPMaster firmware wirelessly over WiFi without physically connecting the ESP-01S to your computer. This is convenient for deployed displays or when the ESP-01S is difficult to access.

### 4.1 Enabling OTA

1. **Enable OTA in code:**
   - In `ESPMaster.ino`, set `#define OTA_ENABLE true`
   - Optionally change the OTA password (default is `"1234"`):
     ```cpp
     const char* otaPassword = "1234";  // Change this to your desired password
     ```
   - Upload the firmware normally (via USB) with OTA enabled

2. **Activate OTA mode:**
   - Once the ESP-01S is running and connected to WiFi, open the web interface
   - Click the "OTA Update" link (only visible when `OTA_ENABLE` is `true`)
   - Confirm the action - this puts the ESP-01S into OTA mode
   - A confirmation page will appear

### 4.2 Uploading via OTA

1. **In Arduino IDE:**
   - Open `ESPMaster/ESPMaster.ino`
   - Make your code changes
   - Go to *Tools → Port* and look for a new port named **"Split-Flap-OTA at [IP address]"**
     - Example: "Split-Flap-OTA at 192.168.0.111"
     - This appears as a network port, not a USB serial port
     - If you don't see it, try refreshing the port list or ensure OTA mode was activated
   - Select this OTA port
   - Click *Upload* as normal - the firmware will be uploaded over WiFi
   - Watch the progress in the Arduino IDE status bar

2. **After upload:**
   - The ESP-01S will automatically reboot
   - Wait a few seconds, then refresh the web interface
   - The device should be running the new firmware

### 4.3 OTA Limitations & Notes

- **LittleFS updates:** OTA can update the firmware sketch, but **cannot update LittleFS files** (web assets). To update `index.html`, `script.js`, or `style.css`, you must use the USB LittleFS upload method.
- **First-time setup:** OTA must be enabled and uploaded via USB at least once before it can be used wirelessly.
- **WiFi required:** OTA only works when the ESP-01S is connected to WiFi. If WiFi is down, you must use USB.
- **Password protection:** The default password is `"1234"`. Change it in the code for security.
- **Stability:** OTA works best with strong WiFi signal. Poor signal can cause upload failures or corruption.

### 4.4 Troubleshooting OTA

- **OTA port not appearing:**
  - Ensure OTA mode was activated via the web interface (click "OTA Update" link)
  - Check that the ESP-01S is on the same WiFi network as your computer
  - Try refreshing the Arduino IDE port list (close and reopen Tools → Port menu)
  - Verify OTA is enabled in code (`#define OTA_ENABLE true`)
- **Upload fails:**
  - Check WiFi signal strength (RSSI should be better than -80 dBm via web UI)
  - Ensure no firewall is blocking the connection
  - Try activating OTA mode again via web interface
  - Move closer to the router for better signal
  - As a fallback, use USB upload
- **Device unresponsive after OTA:**
  - Power cycle the ESP-01S - sometimes a manual reset is needed after OTA updates
  - Check serial monitor for error messages

## 5. Configuration Options

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

## 6. Calibration Workflow

### 6.1 Set Zero Position Offset

**Important:** Disconnect the unit from all other units and the I2C bus. Only connect power and the USB serial cable. Serial communication can be unreliable when multiple units are connected due to power draw, I2C bus interference, or electrical noise.

#### Option A: Interactive Calibration (Recommended)

The Interactive Calibration tool provides a guided, step-by-step process that automatically calculates the optimal offset using statistical methods. This is the recommended method for accurate calibration.

**Steps:**

1. **Upload the calibration tool:**
   - Open `InteractiveCalibration/InteractiveCalibration.ino` in Arduino IDE
   - Select *Arduino Nano* board (use *ATmega328P (Old Bootloader)* if uploads fail)
   - Upload the sketch to the unit

2. **Open Serial Monitor:**
   - Set baud rate to **9600**
   - Set line ending to **"Newline"** or **"Both NL & CR"**
   - **Note:** You may need to press the reset button on the Arduino after upload for serial output to work correctly

3. **Follow the interactive prompts:**
   - The tool will automatically home the unit
   - It will ask you to type the letter you see (to determine starting position)
   - For each of 5 measurements, it will rotate slowly - press Enter when the next letter just flips
   - The tool calculates the optimal offset using linear regression from all measurements
   - The offset is automatically written to EEPROM when complete

4. **Complete the calibration:**
   - After the 5 measurements, the tool displays the calculated offset
   - The offset is saved to EEPROM automatically
   - You can now upload `Unit.ino` - the calibration is complete

**For detailed instructions, see:** [`docs/interactive-calibration.md`](./interactive-calibration.md)

#### Option B: Manual Entry (Backup Method)

The `EEPROM_Write_Offset.ino` tool is a simple backup method if you already know the offset value or want to manually enter it. This method requires you to determine the correct offset value yourself through trial and error.

**Steps:**

1. **Upload the utility:**
   - Open `EEPROM_Write_Offset/EEPROM_Write_Offset.ino` in Arduino IDE
   - Upload to the unit

2. **Open Serial Monitor:**
   - Set baud rate to **9600**
   - You may need to restart the serial monitor or press reset to see output

3. **Enter offset value:**
   - Type a number (typically around 50 steps) and press Enter
   - Test the unit with `Unit.ino` to see if the blank flap aligns correctly
   - Repeat with different values until satisfied

4. **Upload Unit.ino:**
   - Once the offset is correct, upload `Unit.ino` to use the calibrated unit

**Note:** This method is less accurate than Interactive Calibration and requires manual testing to find the correct value.
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

## 7. Web Interface & Usage

- **Text mode:** send ad-hoc messages; long messages auto-split or honour `\n`
- **Countdown / date / clock modes:** switch via the web UI
- **Scheduling:** queue messages with timestamps or “show indefinitely”
- **WiFi strength indicator:** view RSSI + qualitative label on the settings
  panel
- **Log viewer:** shows the last 50 serial log entries and can copy to clipboard

## 8. Debugging Tools

### 8.1 Web UI diagnostics

- **Error status panel:** shows network/JavaScript errors at the top of the page
- **Page load debug log:** records lifecycle events (DOMContentLoaded, AJAX
  statuses, JSON parsing errors)
- **Serial log viewer:** mirrors the circular buffer from the ESP firmware with
  hide/show toggle and “Copy Full Log” button
- **Startup debug page:** accessible when `DEBUG_ENABLE` is true; shows the full
  boot log before redirecting to the main UI

### 8.2 LED indicator (ESP-01S only)

- Continuous blinking = critical error (e.g., WiFi connection failed)
- Requires `ESP01S_LED_ENABLE` to remain `true` and an ESP-01S module (LED on
  GPIO2, active LOW)

### 8.3 Serial logging

Enable `SERIAL_ENABLE` for verbose firmware logs. Useful during calibration or
when diagnosing I²C issues (`ServiceFlapFunctions.ino` reports error codes,
timeouts, sleeping units, etc.).

## 9. Common Issues & Fixes

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

## 10. Additional Resources

- [`docs/README.md`](./README.md) – documentation hub
- [`docs/hardware-build.md`](./hardware-build.md) – mechanical assembly steps
- [`docs/printing.md`](./printing.md) – BOM and printing instructions
- [`docs/SplitFlapInstructions.md`](./SplitFlapInstructions.md) – legacy detailed instructions

With the firmware configured and calibrated, you can rely on the web interface
for day-to-day control, scheduling and diagnostics. Happy flipping!

