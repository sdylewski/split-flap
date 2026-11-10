# Split-Flap

[![Firmware](https://github.com/sdylewski/split-flap/actions/workflows/firmware.yml/badge.svg)](https://github.com/sdylewski/split-flap/actions/workflows/firmware.yml)

![Split Flap Display](./Images/Split-Flap.jpg)

A ten-character split-flap display you can print and build yourself. This fork keeps the
mechanical design, PCB and 3D-printed parts of the projects it builds on (see
[Credits](#credits)) and **rewrites the firmware**: an **ESP32** master with a new web UI, a
console, live data (weather, BART, 3D printers), pop-ups, and unit firmware that tracks its own
position and reports how it's doing.

## What's different in this fork

- **ESP32 master** wired into the original ESP-01 socket, instead of the ESP-01S. Serial and I2C no
  longer share pins, so you get a live log and a console while the display runs.
- **New web UI** at `http://flappy.local`: text, clock (24-hour, 12-hour, AM/PM, day in front),
  date, countdown, **weather**, **real-time BART departures**, **3D printer progress**, train
  station lines for BART, New York, Chicago, Paris and Copenhagen, random phrases and famous
  numbers (editable preset lists), scheduled and repeating messages, quiet hours.
- **Pop-ups**: keep the clock up all day and let "PRINTY DONE", a BART train or a birthday message
  take over for a while - each with its own priority, duration, timer, hours and weekdays.
- **Wave reveal**: flaps that start together, one after another, or timed so the letters *land* a
  chosen interval apart.
- **Home Assistant and Siri** ("Hey Siri, Flappy") through a small JSON API.
- **One console** on USB serial, telnet and the web page - including **calibrating units in place**
  (press Enter when a flap drops; no unscrewing the boards).
- **Unit firmware 2.x**: re-syncs at every magnet pass, learns its motor's steps per turn,
  reports supply voltage, slipping and missed magnets, fixes its own I2C hangs, keeps its tuning
  as parameters you change without reflashing, and sleeps deeply in quiet hours.
- **OTA updates**, settings that survive updates, crash reports over WiFi, and
  **PlatformIO builds with pinned versions, host tests and CI**.

The full comparison - including what was *removed* (ESP-01S support in the new firmware, the WiFi
setup portal) and what has and hasn't been tested on hardware - is in
**[docs/whats-different.md](./docs/whats-different.md)**.

## The web UI

![The Display tab in clock mode: the board mirrors the real flaps](./docs/screenshots/display-clock.png)

![The Pop-ups tab: what may interrupt the base mode, and when](./docs/screenshots/popups.png)

More screenshots: [docs/screenshots](./docs/screenshots) (weather, BART, train lines, printers,
famous numbers, schedule, units, console, system, and a phone-width view).

## Build it

Starting from scratch: print the parts, build the units, then follow
**[docs/build-guide.md](./docs/build-guide.md)** to wire the ESP32, flash the master and the units,
and calibrate. Converting an existing build works the same way.

## Documentation

All documentation lives under [`docs/`](./docs):

| Topic | Description |
|---|---|
| [`docs/whats-different.md`](./docs/whats-different.md) | What this fork changes, removes and has tested |
| [`docs/build-guide.md`](./docs/build-guide.md) | **Start here for the firmware:** wiring, flashing, calibrating |
| [`docs/printing.md`](./docs/printing.md) | BOM, screw list, printed parts, flap tips |
| [`docs/hardware-build.md`](./docs/hardware-build.md) | PCB soldering, unit assembly, enclosure |
| [`docs/esp32.md`](./docs/esp32.md) | The ESP32 master: wiring to the ESP-01 header, power |
| [`docs/software.md`](./docs/software.md) | Both firmwares in detail: web UI, settings, API, troubleshooting |
| [`docs/console.md`](./docs/console.md) | Console commands, calibration, unit parameters, reading the diagnostics |
| [`docs/integrations.md`](./docs/integrations.md) | Weather, BART, 3D printers, pop-ups, wave reveal, Home Assistant, Siri |
| [`docs/SplitFlapInstructions.md`](./docs/SplitFlapInstructions.md) | Original assembly instructions (PDF converted to Markdown) |

## Repository Structure

```
.
├── firmware/
│   ├── master/      # ESP32 master (PlatformIO): src/, web/ (UI), test/
│   ├── unit/        # Arduino Nano unit firmware (PlatformIO), flash.sh
│   └── shared/      # unit_protocol.h - the I2C protocol both sides use
├── docs/            # Documentation
├── PCB/             # Schematics, Gerbers, pick-and-place
├── 3dModels/        # Printable parts
├── legacy/          # Previous Arduino IDE firmware (ESP-01 master, unit 1.3.0, USB calibration tools)
└── Images/, Instructions/
```

## Credits

This fork stands on a lot of good work:

- The original split-flap design - mechanics, PCB and firmware - by
  [David Königsmann](https://github.com/Dave19171/split-flap)
- Additional software updates by [JonnyBooker](https://github.com/JonnyBooker/split-flap)
- Firmware and UI enhancements by [scientress](https://github.com/scientress/split-flap)

Licensed under the GNU General Public License v3 - see [`LICENSE`](./LICENSE).

Have improvements or fixes? Contributions are welcome - open an issue or a pull request.
