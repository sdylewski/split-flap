# Software Guide

The display runs two firmwares, both built with [PlatformIO](https://platformio.org):

| Folder | Runs on | What it does |
|---|---|---|
| `firmware/master/` | ESP32 dev board, wired into the ESP-01 header ([esp32.md](./esp32.md)) | WiFi, web UI, modes, scheduling, console, drives the units over I2C |
| `firmware/unit/` | Arduino Nano in each flap unit | Moves the drum to the requested flap, homes on the magnet, reports diagnostics |
| `firmware/shared/unit_protocol.h` | both | The I2C protocol, so the two sides can't drift apart |

The old Arduino IDE sketches (ESP-01 master, unit 1.3.0, calibration tools) are in
[`legacy/`](../legacy) for reference.

## 1. Tools

Install PlatformIO, either:

- the **PlatformIO IDE** extension for VS Code (open `firmware/master` or `firmware/unit` as a
  folder), or
- the command line: `pipx install --python python3.14 platformio`. PlatformIO's ESP32 platform
  supports Python 3.10 to 3.14, not newer.

Everything else (ESP32 core, compilers, libraries) is downloaded on the first build. The exact
versions are pinned in each `platformio.ini`, so builds are repeatable.

## 2. Master (ESP32)

### 2.1 WiFi details

```sh
cd firmware/master
cp src/secrets.h.example src/secrets.h    # git-ignored - put your WiFi name/password in it
```

Network settings that rarely change live in `src/config.h`: fixed IP `192.168.0.111` (set
`NET_STATIC_IP 0` for DHCP), hostname `flappy`, I2C pins, timings.

### 2.2 First flash over USB

```sh
pio run -t upload        # firmware
pio run -t uploadfs      # web UI (web/ is gzipped automatically into the file system)
pio device monitor       # log + console, 115200 baud
```

Most ESP32 boards switch to flash mode by themselves. If an upload hangs at `Connecting...`,
hold the board's BOOT button until it starts writing.

### 2.3 Later updates over WiFi (OTA)

```sh
export FLAPPY_OTA_PASSWORD='the SECRET_OTA_PASSWORD from secrets.h'
pio run -e ota -t upload        # about a minute
pio run -e ota -t uploadfs      # web UI
```

The display pauses during an update (LED solid on) and reboots when it's done.

### 2.4 Web UI

`http://flappy.local` (or `http://192.168.0.111`). The tabs:

- **Display** – mode (text, clock, date, countdown, train station, phrases, numbers),
  alignment, speed. The board at the top mirrors the real flaps.
- **Schedule** – messages at a set time (one-off for 7.5 s, or "keep showing"), quiet hours.
- **Units** – status and diagnostics per unit, re-home and calibrate buttons.
- **Console** – the same command console as telnet/serial, in the browser.
- **System** – firmware, memory, WiFi, time zone, unit count, log level, live log, reboot.

Edit the UI in `firmware/master/web/`; `pio run -t uploadfs` (or `-e ota`) publishes it.

### 2.5 Settings

All settings are stored in the ESP32's NVS (non-volatile storage) - firmware and web-file
uploads don't touch them - and are changed through the web UI,
the console (`set <key> <value>`) or the API (`POST /api/settings`). Invalid values are
rejected with a message saying what's wrong. The main keys:

| Key | Default | Meaning |
|---|---|---|
| `mode` | `text` | `text` `clock` `date` `countdown` `trainstation` `randomphrase` `famousnumbers` `weather` `bart` `printers` |
| `text` | | Text-mode message. Long text wraps onto several lines, `\n` forces a break |
| `alignment` | `left` | `left` `center` `right` |
| `speed` | 80 | 1-100 (sent to the units as 1-12 rpm) |
| `unitCount` | 10 | Units at I2C addresses 0 .. unitCount-1 |
| `lineSeconds` | 5 | Multi-line text: seconds per line |
| `timezone` | `PST8PDT,M3.2.0,M11.1.0` | POSIX TZ string (the web UI has a list) |
| `clock24h` / `dateFormat` | false / `%a %b %d` | Clock and date format (strftime) |
| `quietEnabled` `quietFrom` `quietTo` `quietBlank` | off, 22:00, 08:00 | No display changes during these hours |
| `quietPowerSave` | true | During quiet hours: WiFi modem sleep, CPU at 80 MHz, no unit polling, units (2.3+) in deep sleep |
| `quietRehome` | true | When quiet hours end, every unit re-homes (a full turn) before the mode resumes |
| `waveMs` / `waveOrder` | 0 / `left` | Wave reveal: ms between units (0 = off), `left` `right` `center` `random` |
| weather, BART, printers | | See [integrations.md](./integrations.md) |
| `logLevel` | `info` | Serial log level: `debug` `info` `warn` `error` `off` |

### 2.6 API

| Request | Does |
|---|---|
| `GET /api/state` | Live status: display, units (with diagnostics), WiFi, time |
| `GET /api/settings` / `POST /api/settings` | Read / change settings (JSON, any subset of keys) |
| `POST /api/message` | `{"text":"HI"}` show now; `"seconds": 60` show temporarily; `"at": <unix time>` (+ `"indefinitely": true`) to schedule; `"dictated": true` cleans up Siri text |
| `DELETE /api/scheduled?at=<unix time>` | Remove a scheduled message |
| `POST /api/command` | `{"line":"rehome all"}` run a console command (output goes to the log) |
| `GET /api/log?since=<seq>` | Log lines after a sequence number |
| `POST /api/reboot` | Restart |
| `ws://flappy.local/ws` | Console: send a line, receive output |

Example: `curl -d '{"text":"HELLO"}' -H 'Content-Type: application/json' http://flappy.local/api/message`

## 3. Units (Arduino Nano)

### 3.1 Flash a unit

Plug the unit's Nano into USB (the display can stay powered) and run:

```sh
firmware/unit/flash.sh
```

It tries the new bootloader (Optiboot) first and falls back to the old one. Nanos with the
new bootloader also get a watchdog that restarts a hung unit; the old bootloader can't handle
watchdog resets, so that build leaves it off.

- The **calibration offset** stored in the Nano's EEPROM is kept across updates (same location
  as firmware 1.3.0).
- The **I2C address** comes from the unit's DIP switches. The leftmost unit must be 0. Check
  the order with the console's `ident` command.
- With USB connected, `pio device monitor` (in `firmware/unit`) shows the unit's own log and
  has bench commands: a letter to show it, `o <n>` set offset, `h` re-home, `i` info, `t` test.

### 3.2 What the unit firmware does

- Homes on the magnet's leading edge at power-up, then moves `offset` steps to the blank flap.
- Computes every flap position from the magnet edge (`offset + flap × steps-per-turn / 45`),
  so rounding errors don't add up.
- **Re-syncs at every magnet pass** and records how far off the count was ("sync error").
- **Learns its motor's steps per turn** (firmware 2.1+). 28BYJ-48 motors come as 2038 or 2048
  steps per turn; this display's measured 2048. After three consistent magnet-to-magnet
  measurements the unit stores the value and uses it.
- Reports diagnostics: supply voltage (and the lowest while the motor ran), boot count, magnet
  syncs/errors/misses, I2C traffic, and an event log the master pulls into its own log.
- Resets its own I2C hardware if it ever gets stuck mid-transfer (the 1.3.0 firmware could
  lock the whole bus).
- Switches the motor coils off 100 ms after every move (`holdMs`): an energized 28BYJ-48 draws
  about 200 mA. During quiet hours (2.3+) the CPU goes into deep sleep until the master wakes it.
- Keeps its tuning (speed limits, timings, sensor polarity, direction, LED, ...) as
  **parameters in EEPROM** (2.1+), changed from the console with `param` - no reflashing.
  See [console.md](./console.md#unit-parameters-param).

## 4. Calibration

Each unit needs an **offset**: the number of steps from the magnet's leading edge to the
middle of the blank flap. Calibrate from any console (web, telnet `nc flappy.local 23`,
or serial):

```
cal 3
```

The unit homes to the magnet, asks which flap is showing, then creeps forward; press
**Enter** each time a flap drops. It fits a line through the drop positions, suggests an offset
and asks before saving. Details: [console.md](./console.md#calibrating-a-unit-cal).

Quick nudge without `cal`: if a unit shows the flap *before* the right one, its offset is too
small; if it shows the one *after*, too big. One flap is about 45 steps: `offset 3 520`.

## 5. Diagnostics and troubleshooting

### 5.1 Where to look

- **Log**: System tab (live), `log info` in any console, or USB serial. Each line has a
  level (D/I/W/E) and a source (`wifi`, `units`, `display`, `unit` = events from a unit, ...).
- **`units`** console command or the Units tab: per-unit firmware, status, supply voltage,
  magnet syncs and sync error, misses, boots, steps per turn, I2C errors.
- **`info`**, **`wifi`**, **`time`**: firmware, memory, reset reason (crash/brown-out show up
  here), WiFi signal and disconnect reasons, clock.
- **`crash`**: after a crash the ESP32 keeps a core dump in flash; `crash` shows the crashed task,
  address, reason and backtrace (also logged at boot and in `/api/state`). Decode the addresses
  with the `firmware.elf` of that build:
  `~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-addr2line -pfiaC -e .pio/build/esp32/firmware.elf 0x400d1234 ...`.
  `crash clear` erases it.

### 5.2 Status LED (ESP32 GPIO2)

| Pattern | Meaning |
|---|---|
| 1 blink, pause | Connecting to WiFi |
| 2 blinks, pause | WiFi OK, waiting for the time (NTP) |
| 3 blinks, pause | Running, but a unit is missing or reports an error |
| Short blip every 3 s | Running normally |
| Fast flicker | WiFi lost |
| 1 s on / 1 s off | Main loop stuck for more than 5 s |
| Solid on | OTA update in progress |

### 5.3 Common problems

| Symptom | Likely cause / fix |
|---|---|
| No unit answers, log says `SDA LOW (stuck)` | A unit is holding the I2C bus. The master tries to free it automatically; if the log says "STILL STUCK", power-cycle the display. Units on firmware 2.0+ reset their own I2C hardware, so this should not happen anymore. |
| One unit missing | DIP address wrong or duplicate, loose I2C cable, or no power. `scan` lists every address that answers. |
| A unit shows the neighbouring flap | Offset off: `cal <addr>`, or nudge with `offset`. |
| Sync error keeps growing or `miss` counts up | The drum slips, or the hall sensor/magnet gap is marginal. Check the magnet and the sensor distance. |
| Supply voltage below ~4.6 V while moving | Weak 5 V supply or thin wiring to that unit. |
| WiFi "auth failed" or "auth expired" in the log | Normal for a few attempts on WPA2/WPA3 mesh routers; the master retries and picks the strongest access point. If it never connects, check `secrets.h`. |
| `http://flappy.local` says "Web files missing" | Run `pio run -t uploadfs`. |
| `pio` says "Python version must be ..." | PlatformIO's ESP32 platform needs Python 3.10-3.14. Reinstall with `pipx install --python python3.14 platformio`. |

## 6. Development

```sh
cd firmware/master && pio test -e native   # host tests for the text/line logic and protocol
```

GitHub Actions (`.github/workflows/firmware.yml`) builds both firmwares, the web files and runs
the host tests on every push that touches `firmware/`.

Code map (master, `firmware/master/src/`): `main.cpp` wiring · `log` · `settings` · `net`
(WiFi/mDNS/OTA/NTP) · `units` (I2C driver) · `display` (text → lines → units) · `modes`
(+ quiet hours, scheduled messages) · `console` + `commands` + `calibration` · `web` · `led`.
Each header explains its module.
