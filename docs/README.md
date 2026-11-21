# Split-Flap Documentation

> This project builds on outstanding prior work:
> - Original split-flap design by [David Königsmann](https://github.com/Dave19171/split-flap)
> - Additional software updates by [JonnyBooker](https://github.com/JonnyBooker/split-flap)
> - Firmware/UI enhancements by [scientress](https://github.com/scientress/split-flap)

**What's new in this fork** (the full list: [`whats-different.md`](./whats-different.md))

- ESP32 master (wired into the ESP-01 header) with a new web UI, JSON API and OTA updates
- Weather, real-time BART, 3D printer status, train lines for several cities, pop-ups with
  priorities, a wave reveal, Home Assistant and Siri
- A console on the web page, telnet and USB serial, with in-place unit calibration
- Unit firmware 2.x: magnet re-sync, learned steps per turn, diagnostics, parameters in EEPROM
- Power and heat controls (CPU speed, WiFi sleep, automatic transmit power for mesh networks)
- PlatformIO builds with pinned versions, host tests and GitHub Actions CI

Welcome to the documentation hub for the split-flap display. The goal of this
folder is to provide a single place for build instructions, firmware guidance
and reference material. If you are new to the project, follow the quick start
below and then dive into the topic that is most relevant for you.

## Quick Start

1. **Print & source parts** – make sure you have all printed parts, screws and
   electronic components ready. See [`printing.md`](./printing.md).
2. **Assemble the hardware** – solder the PCBs, build the units, wire
   everything and close the enclosure. See [`hardware-build.md`](./hardware-build.md).
   *Already have an ESP-01S display?* Read [`upgrade-esp01-to-esp32.md`](./upgrade-esp01-to-esp32.md)
   first: the hardware stays, only the master and the firmware change.
3. **Wire and flash** – connect the ESP32 to the ESP-01 header, flash the master and the units,
   set the DIP switches and calibrate each unit. The step-by-step guide is
   [`build-guide.md`](./build-guide.md); background in [`esp32.md`](./esp32.md),
   [`software.md`](./software.md) and [`console.md`](./console.md).

### BOM & Purchasing Notes

- The master controller is an **original ESP32 dev board** (ESP32-WROOM-32), jumpered into
  the ESP-01 header; see [`esp32.md`](./esp32.md). The old ESP-01S firmware is kept in
  `legacy/`.
- For a consolidated list of screws, inserts, printed parts and electronics,
  refer to [`printing.md`](./printing.md).


## Repository Layout

| Path | Description |
| --- | --- |
| `docs/` | Documentation hub (this folder) |
| `firmware/master/` | ESP32 master firmware and web UI (PlatformIO) |
| `firmware/unit/` | Arduino Nano firmware for each flap unit (PlatformIO) |
| `firmware/shared/` | The I2C protocol shared by both |
| `legacy/` | Previous Arduino IDE firmware and USB calibration tools |
| `PCB/` | Schematics, Gerbers and pick-and-place files |
| `Images/` | Reference photos used by the docs |

## Document Map

| Document | Purpose |
| --- | --- |
| [`printing.md`](./printing.md) | BOM, screw list, printed part manifest and flap printing tips |
| [`hardware-build.md`](./hardware-build.md) | PCB soldering, unit assembly and enclosure build |
| [`whats-different.md`](./whats-different.md) | What this fork changes, removes and has tested |
| [`build-guide.md`](./build-guide.md) | Step by step: wire the ESP32, flash master and units, calibrate |
| [`esp32.md`](./esp32.md) | Wiring the ESP32 master into the ESP-01 header, power |
| [`software.md`](./software.md) | Building and flashing, web UI, settings, API, LED codes, troubleshooting |
| [`console.md`](./console.md) | Console commands, calibration (`cal`), reading the unit diagnostics |
| [`integrations.md`](./integrations.md) | Weather, BART departures, 3D printers, wave reveal, Home Assistant, Siri |
| [`interactive-calibration.md`](./interactive-calibration.md) | Legacy USB calibration tool |
| [`SplitFlapInstructions.md`](./SplitFlapInstructions.md) | Legacy PDF content converted to Markdown for archival/reference |

## Feature Highlights

- Web UI with modes, scheduling, quiet hours, unit diagnostics, a console and a live log
- OTA updates and a JSON API
- Units that re-sync at the magnet, learn their steps per turn and report problems
- Self-healing I2C bus and status LED blink codes

## Need Help?

- Hardware or mechanical questions → start with [`printing.md`](./printing.md)
  and [`hardware-build.md`](./hardware-build.md).
- Firmware configuration or troubleshooting → see [`software.md`](./software.md) and
  [`console.md`](./console.md).
- Historical instructions → consult [`SplitFlapInstructions.md`](./SplitFlapInstructions.md).

Spotted an issue or have an improvement? Feel free to open an issue or pull
request on the repository. Contributions are welcome!

