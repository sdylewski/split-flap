# Legacy firmware

These are the previous Arduino IDE sketches, kept for reference. The current firmware is in
[`firmware/`](../firmware) (see [docs/software.md](../docs/software.md)).

| Folder | What it was |
|---|---|
| `ESPMaster/` | ESP-01S master (ESP8266, Arduino IDE). Also builds for ESP32, but without the new unit diagnostics |
| `Unit/` | Unit firmware 1.3.0. Known issue: can lock the I2C bus when woken from sleep - use unit firmware 2.x |
| `EEPROM_Write_Offset/` | USB tool to write a unit's offset (now: console `offset <addr> <steps>`) |
| `InteractiveCalibration/` | USB calibration tool (now: console `cal <addr>`) |
