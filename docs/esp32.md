# The ESP32 master

The display's master controller is an original ESP32 dev board (ESP32-WROOM-32: DevKitC,
"ESP32 Dev Module", mini D1 ESP32, ...) wired into the ESP-01 header where the ESP-01S used to
sit. Compared with the ESP-01S it gives you:

- **USB serial at the same time as I2C.** On the ESP-01, I2C uses the TX/RX pins, so there's no
  serial. On the ESP32, I2C is on GPIO21/22, so you get a live log and a serial console.
- **A fast web page.** Two 240 MHz cores and about 4x the RAM, with WiFi on its own core.
- **Uploads over plain USB**, and over WiFi (OTA) once it's running.

The firmware is in `firmware/master` - see [software.md](./software.md) for building and
flashing.

## Wiring

Connect the ESP32 board to the display's ESP-01 header **U2** (2x4 pins) with jumper wires.
These pin numbers come from the PCB files (`PCB/KiCad_Export/SplitFlap.kicad_pcb`):

| U2 pin | ESP-01S signal | Split-flap use | ESP32 board |
|---|---|---|---|
| **8** | TXD (GPIO1) | **I2C SDA** | **GPIO21** (`ESP32_I2C_SDA`) |
| **4** | RXD (GPIO3) | **I2C SCL** | **GPIO22** (`ESP32_I2C_SCL`) |
| **1** | GND | ground | **GND** (always connect) |
| 5 | VCC | +3.3 V | 3V3, *or leave open* (see "Power") |
| 7 | CH_PD / EN | tied to +3.3 V | not connected |
| 6 | RST | – | not connected |
| 3 | IO0 | – | not connected |
| 2 | IO2 | – | not connected |

Pin 1 is the **square pad**. The header is two rows of four:

```
  1 GND      2 IO2   3 IO0   4 RX = SCL
  8 TX = SDA 7 EN    6 RST   5 VCC 3.3V
```

Pins 4 and 8 each go through a BSS138 level shifter (Q1, Q2) to the 5 V I2C bus of the
units. The ESP side of the shifters is 3.3 V, which is what the ESP32 needs.

### Power

The board's 3.3 V comes from an AMS1117-3.3 (800 mA). That's enough for an ESP32
(about 250 mA on average, with short WiFi peaks near 500 mA). There are two ways to power it:

1. **Recommended:** feed the display's **5 V** into the ESP32 board's **5V/VIN** pin, and
   connect only GND, SDA and SCL to the header. The ESP32 board's own regulator powers it,
   and the display's 3.3 V regulator stays lightly loaded.
2. Feed the header's **3.3 V** into the ESP32 board's **3V3** pin and leave 5V/VIN open.

You can plug in USB while either option is connected.

The board's own BSS138 shifters already convert between 3.3 V and 5 V, so no extra
level shifting is needed.

## Firmware

Build and flash with PlatformIO (details in [software.md](./software.md#2-master-esp32)):

```sh
cd firmware/master
pio run -t upload && pio run -t uploadfs    # first time, over USB
pio run -e ota -t upload                    # later, over WiFi
pio device monitor                          # log + console over USB
```

The board settings (ESP32 Dev Module, "Minimal SPIFFS" partitions: two 1.9 MB app slots for OTA
plus 128 KB for the web files) are in `platformio.ini`; nothing to pick by hand. If an upload
hangs at `Connecting...`, hold the board's **BOOT** button until writing starts.

## Heat

The ESP32 runs warmer than the ESP-01S, especially in a closed enclosure. Add ventilation, then use
the web UI's **System -> Power and heat** card (CPU speed, WiFi sleep, transmit power). See
[upgrade-esp01-to-esp32.md](./upgrade-esp01-to-esp32.md#power-and-heat).

## LED

Most ESP32 dev boards have an LED on GPIO2. It shows the status blink codes
([software.md](./software.md#52-status-led-esp32-gpio2)). Boards without that LED (for example
DevKitC v4) work the same, just without blinking.

## The ESP-01S

The old ESP-01S master firmware is in [`legacy/ESPMaster`](../legacy/ESPMaster) (Arduino IDE,
Generic ESP8266 Module, 4MB FS:1MB). It doesn't support the new unit diagnostics.
