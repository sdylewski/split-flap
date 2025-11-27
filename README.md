-# Split-Flap
+# Split-Flap
+
([![Build ESP Master Sketch](https://github.com/JonnyBooker/split-flap/actions/workflows/build-esp-master.yml/badge.svg)](https://github.com/JonnyBooker/split-flap/actions/workflows/build-esp-master.yml) [![Build EEPROM Write Sketch](https://github.com/JonnyBooker/split-flap/actions/workflows/build-eeprom-write.yml/badge.svg)](https://github.com/JonnyBooker/split-flap/actions/workflows/build-eeprom-write.yml) [![Build Arduino Unit Sketch](https://github.com/JonnyBooker/split-flap/actions/workflows/build-unit.yml/badge.svg)](https://github.com/JonnyBooker/split-flap/actions/workflows/build-unit.yml) )
+
+![Split Flap Display](./Images/Split-Flap.jpg)
+
+This fork builds on the brilliant [original project](https://github.com/Dave19171/split-flap) by
+[David Königsmann](https://github.com/Dave19171) and adds a modern web UI, OTA updates,
+message scheduling, a WiFi setup portal, diagnostics and numerous firmware
+improvements while keeping the 3D-printable mechanical design.
+
+## Feature Highlights
+
+- Multi-mode web UI (text, countdown, date, clock) with scheduling and OTA
+- Non-blocking ESPMaster firmware that keeps HTTP requests responsive
+- Built-in WiFi setup portal (AP mode) plus optional static IP/direct mode
+- LED error indicator (ESP-01S), page-load debug logs and copyable serial logs
+- Message splitting for long strings, newline support and REST endpoints
+
+## Documentation
+
+All project documentation now lives under [`docs/`](./docs). Start here:
+
+| Topic | Description |
+| --- | --- |
+| [`docs/README.md`](./docs/README.md) | Docs hub + quick start |
+| [`docs/printing.md`](./docs/printing.md) | BOM, screw list, printed parts, flap tips |
+| [`docs/hardware-build.md`](./docs/hardware-build.md) | PCB soldering, unit assembly, enclosure |
+| [`docs/software.md`](./docs/software.md) | Firmware setup, calibration, debugging, usage |
+| [`docs/SplitFlapInstructions.md`](./docs/SplitFlapInstructions.md) | Legacy PDF converted to Markdown |
+
+## Repository Structure
+
+```
+.
+├── docs/                 # Documentation hub
+├── ESPMaster/            # ESP-01S firmware + web assets
+├── Unit/                 # Arduino Nano unit firmware
+├── EEPROM_Write_Offset/  # Utility sketch for per-unit offsets
+├── PCB/                  # Schematics, Gerbers, pick-and-place
+├── Images/               # Reference photos used in docs
+└── Instructions/         # Original PDF (kept for reference)
+```
+
+## Getting Started
+
+1. Review [`docs/printing.md`](./docs/printing.md) to print and source parts
+2. Follow [`docs/hardware-build.md`](./docs/hardware-build.md) to assemble the
+   PCBs, units and enclosure
+3. Configure firmware, calibrate offsets and explore the web UI using
+   [`docs/software.md`](./docs/software.md)
+
+Have improvements or fixes? Contributions are welcome—open an issue or a pull
+request and help the display flip even better!
