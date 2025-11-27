# Split-Flap Documentation

> This project builds on outstanding prior work:
> - Original split-flap design by [David Königsmann](https://github.com/Dave19171/split-flap)
> - Additional software updates by [JonnyBooker](https://github.com/JonnyBooker/split-flap)
> - Firmware/UI enhancements by [scientress](https://github.com/scientress/split-flap)

**What’s new in this fork**

- Arduino IDE 2.x support (LittleFS workflow + updated library guidance)
- Expanded debugging (startup log page, error status panel, page-load log,
  copyable serial log, ESP-01S LED indicator)
- Stability fixes for the web UI, `/settings` endpoint and I²C diagnostics
- Consolidated documentation under `docs/` with separate printing, hardware and
  software guides

Welcome to the documentation hub for the split-flap display. The goal of this
folder is to provide a single place for build instructions, firmware guidance
and reference material. If you are new to the project, follow the quick start
below and then dive into the topic that is most relevant for you.

## Quick Start

1. **Print & source parts** – make sure you have all printed parts, screws and
   electronic components ready. See [`printing.md`](./printing.md).
2. **Assemble the hardware** – solder the PCBs, build the units, wire
   everything and close the enclosure. See [`hardware-build.md`](./hardware-build.md).
3. **Configure the software** – flash the ESPMaster firmware, calibrate each
   unit, set DIP switches and verify/debug the system. See
   [`software.md`](./software.md).

### BOM & Purchasing Notes

- The recommended controller is the **ESP-01S** (not the original ESP-01). The
  ESP-01S has better WiFi performance, and routes its onboard blue LED to GPIO2 (so the firmware can drive the
  error indicator without tying up TX/RX) 
- For a consolidated list of screws, inserts, printed parts and electronics,
  refer to [`printing.md`](./printing.md).


## Repository Layout

| Path | Description |
| --- | --- |
| `docs/` | Documentation hub (this folder) |
| `ESPMaster/` | ESP-01S firmware and web assets |
| `Unit/` | Arduino Nano firmware for each flap unit |
| `EEPROM_Write_Offset/` | Utility sketch to calibrate per-unit offsets |
| `PCB/` | Schematics, Gerbers and pick-and-place files |
| `Images/` | Reference photos used by the docs |

## Document Map

| Document | Purpose |
| --- | --- |
| [`printing.md`](./printing.md) | BOM, screw list, printed part manifest and flap printing tips |
| [`hardware-build.md`](./hardware-build.md) | PCB soldering, unit assembly and enclosure build |
| [`software.md`](./software.md) | Firmware setup, calibration, DIP switches, debugging and usage |
| [`SplitFlapInstructions.md`](./SplitFlapInstructions.md) | Legacy PDF content converted to Markdown for archival/reference |

## Feature Highlights

- Web UI with scheduling, OTA updates and diagnostics
- Non-blocking firmware that keeps the web server responsive
- Built-in WiFi setup portal and optional static IP mode
- Detailed debug tooling: startup page, error log, serial log viewer and LED
  error indicator (ESP-01S only)

## Need Help?

- Hardware or mechanical questions → start with [`printing.md`](./printing.md)
  and [`hardware-build.md`](./hardware-build.md).
- Firmware configuration or troubleshooting → see [`software.md`](./software.md).
- Historical instructions → consult [`SplitFlapInstructions.md`](./SplitFlapInstructions.md).

Spotted an issue or have an improvement? Feel free to open an issue or pull
request on the repository. Contributions are welcome!

