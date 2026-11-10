# What's Different in This Fork

The mechanical design, PCB and 3D-printed parts are unchanged from the projects this fork builds on
(see the credits in the [README](../README.md)). **The firmware is a rewrite**, and the master
controller moves from an ESP-01S to an ESP32. This page lists what changed, what was dropped, and
what has and hasn't been tested on real hardware.

The previous firmware (ESP-01S master, unit 1.3.0, the USB calibration tools) is kept in
[`legacy/`](../legacy). To move an existing build to this firmware, follow the
[build guide](./build-guide.md).

## At a glance

| | Previous firmware | This fork |
|---|---|---|
| Master controller | ESP-01S (ESP8266, 80 KB RAM, one UART shared with I2C) | Original ESP32 dev board in the ESP-01 header; serial and I2C no longer share pins |
| Build system | Arduino IDE sketches | [PlatformIO](https://platformio.org) with every version pinned; host unit tests; CI |
| Master code | One ~2,250-line `.ino` plus helpers | 16 modules, each with a header explaining it |
| Unit firmware | 1.3.0 | 2.3.0 (same I2C address scheme and EEPROM offset, extended protocol) |
| WiFi setup | Access-point setup portal, or credentials in the sketch | Credentials in a git-ignored `secrets.h` (no setup portal) |
| Debugging | Serial only (and serial disables I2C on the ESP-01) | Log with levels over USB serial, telnet and the web page; console; crash reports; unit diagnostics |

## Master

**Platform**
- ESP32 with Arduino core 3.3.12, built with PlatformIO. Libraries are pinned in `platformio.ini`;
  CI builds both firmwares and runs the host tests on every push.
- Updates over WiFi (OTA, password protected), reachable as `flappy.local`. Settings live in the
  ESP32's NVS, so uploading firmware or web files never resets them.
- Crash reports: after a crash the ESP32 keeps a core dump; the `crash` console command prints the
  task, address and backtrace, decodable with the build's `firmware.elf`.
- Several bugs of the ESP32 platform were hit and worked around while building this (an HTTP
  client call that crashes when a time-sync lookup is pending, an incompatible AsyncTCP, a
  mesh-WiFi connection problem). They are documented in the commit history.

**Display**
- Non-blocking: the display is a state machine that never stalls the web server or the console.
- **Wave reveal**: units can start together, one after another, one at a time, or - the "arrive"
  style - timed so the letters *land* a chosen interval apart (it works out each unit's travel time
  from its current flap, target and speed).
- Every "seconds" setting (per phrase, per station, per page, pop-up length) counts from when the
  flaps have **stopped**, not from when they started moving.

**Modes**
- Kept: text (multi-line, wrapped), clock, date, countdown, random phrases, famous numbers, train
  stations, scheduled messages, quiet hours.
- Clock: 24-hour / 12-hour / 12-hour with AM/PM, leading zeros, and a day in front (`28`, `28TH`,
  `MON`).
- Train stations: pick a city and a line (BART, New York 7/1/A, Chicago CTA Red/Blue, Paris
  Métro 1/14, Copenhagen M1/M2, plus Eurostar, Thameslink, Acela and the California coast), with an
  expandable list of the stations on the line.
- Famous numbers: an editable two-column table with preset lists (general, mathematics, quantum
  mechanics, thermodynamics, electromagnetism, astronomy, chemistry, random). Edits are remembered
  per list.
- New live-data modes: **weather** (Open-Meteo), **BART real-time departures** (BART API),
  **3D printer** progress (Klipper/Moonraker).

**Pop-ups**
A base mode (say the clock) plus pop-up sources that take over for a while: printers (`PRINTY DONE`),
messages (Home Assistant, Siri, the API), scheduled messages, and timed pop-ups of weather, BART and
the other modes. Each source has an on switch, a priority (high interrupts anything, normal only the
base mode, low only after a quiet minute), a show time, and an optional timer with hours and
weekdays. Scheduled messages can repeat ("every hour for 8 hours"). A BART pop-up can fire when a
train is N minutes away. See [integrations.md](./integrations.md).

**Integrations**
- JSON API and a WebSocket console; examples for **Home Assistant** (`rest_command`) and a
  **Siri Shortcut** ("Hey Siri, Flappy") in [integrations.md](./integrations.md).

**Power**
- Quiet hours can switch the ESP32 into WiFi power save at 80 MHz, stop polling the units and send
  newer units into deep sleep; when they end, the units re-home. The web page polls every 5 s and
  not at all while its tab is hidden.

**Console and tools**
- One command console on USB serial, telnet (`nc flappy.local 23`) and the web page: status,
  `ident`, `scan`, `units` (diagnostics), `cal` (calibration in place), `offset`, `rehome`,
  `param`, `set`, `notify`, `crash`, `log`, and more ([console.md](./console.md)).
- Interactive calibration over WiFi: press Enter when a flap drops - no USB cable, no unscrewing
  the boards. It replaces the separate USB calibration sketches.

**Removed or replaced** (so you aren't surprised)
- ESP-01S / ESP8266 support in the new firmware (the old firmware stays in `legacy/`).
- The WiFi access-point setup portal (WiFiManager): credentials are set at build time in
  `secrets.h`.
- The startup debug page and the browser-side page-load log; the web page's Console and Log views
  replace them.
- The I2C diagnostics web endpoints: `scan`, `status` and `units` in the console replace them.
- The ezTime/NTPClient libraries: the ESP32's built-in SNTP with a POSIX time-zone string is used.

## Units

Firmware 2.x is a rewrite of the Nano sketch. The I2C protocol for showing a letter and the EEPROM
location of the calibration offset are unchanged, so existing calibrations carry over.

- **Non-blocking motor driver** with its own coil sequencing (no `Stepper` library); the unit
  answers I2C while it moves, and reports "moving" the moment a command arrives.
- **Absolute flap positions** measured from the magnet, and a **re-sync at every magnet pass** that
  records how far off the count was - slipping shows up as a number instead of a wrong letter.
- **Learns its motor's steps per turn** (28BYJ-48 motors come as 2038 or 2048; this build's
  measured 2048) from the distance between magnet passes.
- **Diagnostics** over I2C: supply voltage (and the lowest while moving), reset cause, boot count,
  magnet syncs, misses, errors, and an event log the master copies into its own log.
- **Parameters in EEPROM**: speed limits, timings, sensor polarity, direction, the LED, ... change
  them from the console with `param` - no reflashing ([console.md](./console.md#unit-parameters-param)).
- **Reliability**: the unit resets its own I2C hardware if it gets stuck, and a watchdog restarts a
  hung unit (Optiboot bootloaders). Unit 1.3.0 could lock the whole I2C bus when woken from sleep
  by a multi-byte read; the master reads such units one byte at a time, and 2.x doesn't have the
  problem.
- **Deep sleep during quiet hours** (2.3): the CPU powers down until the master sends a command.
- A bench console on the unit's own USB serial port (`help`).

## Status: what has been tested

Built and run on one display: ESP32 master, 10 units, a three-node mesh WiFi network.

| Verified on the hardware | Tested only off the hardware, or not yet |
|---|---|
| All 45 flaps displayed, matching each unit's own readout; position error at the magnet within ±1 step in the test runs (units 2.1+) | Unit deep sleep (firmware 2.3, built but not yet running on the units) |
| Text, clock, date, countdown, train station, phrases and famous-numbers modes; OTA; crash reports | The "arrive" wave timing and the pop-up handover on the flaps (the rules have host tests) |
| Weather, BART and printer data fetched and parsed on the ESP32 | Weather, BART and printer pages as shown on the flaps; a print finishing on a real printer (`PRINTY DONE`) |
| Console on serial, telnet and web; calibration; unit parameters | Home Assistant and Siri (the API side is tested; the setups are documented, not run) |
| Quiet hours, including that an update during quiet hours doesn't move the flaps | Other ESP32 variants (S2, S3, C3, C6) - only the original ESP32 is supported |

The text and timing logic has 18 host tests (`pio test -e native`), run in CI.

## Where to go next

- [Build guide](./build-guide.md) - wire the ESP32, flash everything, calibrate
- [software.md](./software.md) - the firmware in detail, settings, API, troubleshooting
- [integrations.md](./integrations.md) - weather, BART, printers, pop-ups, Home Assistant, Siri
- [console.md](./console.md) - every console command
