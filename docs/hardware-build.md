# Hardware Build Guide

This document walks through the mechanical and electrical assembly of the
split-flap display—from PCB soldering to final enclosure assembly. Follow the
sections in order for the smoothest build.

## 1. CAD Changes vs Original Design

This fork includes a few small but important mechanical tweaks to the original
CAD to improve assembly, alignment and robustness:

- **Drum**
  - Increased magnet hole diameter from **2.1 mm → 2.2 mm** for easier insertion
    of 2 mm magnets.
  - Added more material around the magnet cavity and shifted it slightly inward
    to better align the magnet with the hall sensor path and reduce cracking.
  - Added small through-hole indicators for the **first flap position** on the
    drum to make flap ordering more obvious during assembly.
  - Added a simple **drum removal feature**: an M3 screw + nut can be used as a
    jack to push the motor shaft out of the press-fit drum without damage.
- **Frame**
  - Corrected oversized PCB mounting holes so the screws clamp the board
    securely.
  - Widened the cable channel by ~1 mm to give wiring more space and reduce
    pinching when closing the case.


## 2. PCB Preparation

Use the Gerber files in `PCB/` and have one board fabricated per unit. Populate
*all* footprints for the first unit (it hosts the ESP-01S) and only the required
5 V circuitry for subsequent units.

### 1.1 Mandatory components (all units)

- ULN2003A Darlington driver (U4)
- L7805 5 V regulator (U3) plus its capacitors
- JST-XH motor, hall sensor and daisy-chain connectors
- Arduino Nano female headers
- DIP switch (SW1) for unit addressing

### 1.2 First unit extras

The first PCB also hosts the hub electronics:

- ESP-01S module + 3.3 V regulator (AMS1117-3.3)
- Level shifting (BSS138 + resistors) for I²C
- Pull-ups for SDA/SCL

Populate every component on the back side for unit #0. Subsequent boards only
need the 5 V path (leave 3.3 V parts unpopulated).

### 1.3 Assembly tips

- Install heat-set inserts before final soldering to avoid reheating populated
  boards.
- Keep the ESP-01S socket accessible; you will swap the module during firmware
  work.
- Double-check polarity on electrolytics and regulators.

## 3. Wiring & Cable Looms

Prepare wiring harnesses before mechanical assembly:

- **Daisy-chain cables:** 9 × cables with 4 × 70 mm wires (XH-4P on both ends)
- **Power lead:** one cable with XH-4P on one end, free leads to the rocker
  switch and DC jack on the other
- **Hall sensors:** 10 × sensors with 3 × 220 mm wires, terminated with XH-3Y

Label cables per unit to avoid confusion later.

## 4. Unit Assembly

Each unit consists of:

1. FrameUnit print with inserts installed
2. Stepper motor mounted using M3 hardware
3. Hall sensor positioned near the magnet path
4. PCB secured with screws; connect stepper + hall harnesses
5. Drum + flaps installed (see Section 4 of `printing.md`)

### Tips

- Press two M3 nuts diagonally into the frame back; use temporary screws to
  pull them tight.
- Route wiring through the provided channels (see legacy Figures 11–13).
- **Hall sensor mounting:** The hall sensor mounting holes in this version have
  **9 mm spacing** (prior versions used 10.9 mm spacing). Ensure your hall sensor
  PCB matches this spacing before mounting.
- Confirm the hall sensor LED only lights when a magnet is nearby.

## 5. Middle Frame & Case

### 4.1 MiddleFrame

1. Insert eight M3 heat-set inserts per the diagram.
2. Join Left, Center and Right pieces with the corresponding screws/nuts.
3. Mount each completed unit to the frame; daisy-chain the connectors as you go.

### 4.2 Front Cover

1. Insert six M3 heat-set inserts.
2. Bolt the three cover sections together.
3. Test-fit over the units to ensure cable clearance.

### 4.3 Back Cover

1. Join the three back sections.
2. Mount the rocker switch and DC socket, wiring them to the power harness.
3. Terminate with an XH-4Y plug to mate with the first unit.

## 6. Final Assembly Checklist

- [ ] All units securely mounted and wired in order (addresses 0…n-1)
- [ ] Hall sensors aligned; magnets glued with consistent polarity and marked
- [ ] Flaps rotate freely without rubbing
- [ ] Power path tested (12 V supply, polarity confirmed)
- [ ] ESP-01S sits firmly on the first PCB with antenna clearances
- [ ] Enclosure closes without pinching cables


