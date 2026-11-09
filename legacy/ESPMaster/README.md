# ESPMaster Scott Version - Modifications

This is a modified version of the ESPMaster code with improvements for better web server responsiveness and debugging capabilities.

## Major Changes from the Base v2.3.0 Release

- Added LED error indication that continuously blinks when critical errors are detected (e.g., WiFi connection failed).
- Replaced blocking calls (notably `waitForSync()` and `delay()`) with non-blocking loops that `yield()` so the HTTP server stays responsive.
- Introduced per-phase timeouts (WiFi, NTP sync, flap updates, filesystem waits) to prevent indefinite hangs.
- Expanded debug output, including a status string that traces the current setup phase over serial and mirroring of those messages to the web UI.
- Added a WiFi strength indicator (RSSI + qualitative label) on the main interface so you can spot reception issues without attaching a serial cable.
- Hardened `ServiceFlapFunctions` with cooperative timing so lengthy display updates do not starve the rest of the system.

## Key Changes

### 1. LED Error Indication
The onboard blue LED on the ESP-01S will continuously blink when a critical error is detected (e.g., WiFi connection failed). This provides a simple visual indicator for troubleshooting.

**ESP-01S Only**
- This feature only works with the **ESP-01S** module.
- The ESP-01S has its onboard blue LED on GPIO 2 (not on TX/RX lines), which allows it to be used for error indication while keeping the TX/RX lines dedicated to I2C communication with the split-flap units.
- The original ESP-01 has its LED on GPIO 1 (TX), which conflicts with I2C communication, so this LED feature is not supported on ESP-01.

### 2. Non-Blocking NTP Sync
- Replaced blocking `waitForSync()` with a non-blocking timeout-based approach
- 30-second timeout prevents indefinite hanging
- Calls `yield()` during sync to allow web server to process requests
- System continues even if NTP sync fails

### 3. Web Server Responsiveness Improvements
- Added `yield()` calls in all blocking `while` loops in `ServiceFlapFunctions.ino`
- Replaced blocking `delay()` calls with non-blocking timing using `millis()`
- Added 30-second timeouts to prevent infinite hangs when waiting for display
- Web server can now process requests even while display is updating

### 4. Enhanced Debugging
- Added `DEBUG:` status messages throughout setup
- Debug status string tracks current initialization phase
- More verbose logging for troubleshooting
- **Debug Mode** (`DEBUG_ENABLE`): When enabled, provides comprehensive debugging features:
  - **Startup Debug Page**: Shows initialization log during boot (accessible before main UI loads)
  - **Error Status Panel**: Displays JavaScript errors, network errors, and page load issues at the top of the main page
  - **Page Load Debug Log**: Shows debug messages during page initialization (collapsible viewer)
  - **Serial Debug Log**: Real-time serial output at the bottom of the main page with hide/show toggle
- All debug features can be enabled/disabled via `/enable-debug-mode` and `/exit-debug-mode` endpoints

### 5. WiFi Strength Indicator
- `getCurrentSettingValues()` now samples RSSI multiple times and returns both the raw dBm value and connection state.
- The web UI renders this as a color-coded "Excellent/Very Good/…/Very Weak" status line next to the WiFi details.
- Use this to diagnose poor placement or antenna issues; values worse than about -80 dBm usually cause missed OTA packets and unreliable message pushes.

## Hardware Requirements

- **ESP-01S Module**: Required for LED error indication feature
- The onboard blue LED on GPIO 2 is used automatically (no external wiring needed)

## Version

- **Version**: 2.3.0-Scott
- **Based on**: split-flap-scientress ESPMaster v2.3.0

## Files Modified

1. **ESPMaster.ino**
   - Added LED error indication function
   - Non-blocking NTP sync with timeout
   - Enhanced debug logging

2. **ServiceFlapFunctions.ino**
   - Added `yield()` calls in all blocking loops
   - Non-blocking delays with `yield()`
   - Timeout protection for display wait loops

## Benefits

- **Web interface remains responsive** even when display is updating
- **Visual error indication** via LED continuous blink on critical errors
- **No more indefinite hangs** from NTP sync or display waits
- **Better error detection** through LED indication and comprehensive debug messages

## Troubleshooting

If the web page still doesn't load:
1. Check if the LED is continuously blinking (indicates a critical error like WiFi connection failure)
2. Enable `SERIAL_ENABLE` to see detailed debug messages
3. Check the debug log on the web interface (if accessible) or serial output
4. Verify WiFi connection and credentials

If LED doesn't work:
- Ensure you're using an ESP-01S module (not the original ESP-01)
- The LED feature is automatically enabled when `ESP01S_LED_ENABLE` is set to `true` in the code

