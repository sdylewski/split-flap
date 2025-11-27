# Printing & Bill of Materials

This guide consolidates everything required to print, source and prepare the
mechanical parts of the split-flap display. Print and procurement planning up
front will save a lot of time during the hardware build.

## Filament & General Materials

Quantities below assume a 10-unit display (the PCB supports up to 16 units).

- **Black filament:** ~2.6 kg PLA (frame, covers, structural pieces)
- **White filament:** ~250 g PLA (cladding, accents)
- **Magnets:** 10 × 2 mm × 1 mm neodymium discs
- **Wire:** 22 AWG silicone (power), 26 AWG stranded (signal, two colours)
- **Switches & power:**
  - Rocker switch (panel mount)
  - DC barrel jack (<9 mm hole)
  - 12 V, ≥24 W PSU
- **Hall sensors:** 10 × KY-003 5 V momentary hall sensor modules
- **Stepper motors:** 10 × 28BYJ-48 **12 V** steppers (do not use 5 V variants)
- **Heat-set inserts:** 14 × M3, 20 × M2x3.6x4. If you are not using the referenced Ruthex hardware, match their dimensions (M2 inserts are 4 mm long × 3.6 mm
  diameter) so they grip properly in the printed parts.

> Tip: Links to example parts are in the legacy instructions and may change
> over time. Double-check dimensions before ordering.

## Screw & Insert Reference

| Screw / Insert | Total | UnitFrame | Stepper | HallSensor | PCB | FlapDrum | MiddleFrame | FrontCover | BackCover |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M4 Nut | 4 | – | – | – | – | – | 4 | – | – |
| M3 Nut | 32 | – | – | – | – | – | 20 | 8 | 4 |
| M4×8 mm pan head | 20 | 20 | – | – | – | – | – | – | – |
| M4×20 mm pan head | 4 | – | – | – | – | – | 4 | – | – |
| M3×16 socket cap | 34 | – | – | – | – | – | 20 | 6 | 8 |
| M3×8 mm pan head | 12 | – | – | – | – | – | 8 | 4 | – |
| M3×6 mm | 20 | – | – | – | 20 | – | – | – | – |
| M2×8 mm pan head | 20 | – | – | – | 20 | – | – | – | – |
| M2×4 mm | 20 | – | – | – | 20 | – | – | – | – |
| M3 heat insert | 14 | – | – | – | – | – | 8 | 6 | – |
| M2 heat insert | 20 | – | – | – | 20 | – | – | – | – |

## PCB Bill of Materials

All passives are 0805 packages. You need one PCB per flap unit.

| ID | Item | Designators | Qty | LCSC / Notes |
| --- | --- | --- | --- | --- |
| 1 | ESP-01S | U2 | 1 | ESP-01S much better than ESP01 |
| 2 | 10 kΩ resistor | R1–R4 | 4 | LCSC C269742 |
| 3 | BSS138 MOSFET | Q1–Q2 | 2 | LCSC C78284 |
| 4 | 330 nF capacitor | C1 | 1 | LCSC C527609 |
| 5 | Arduino Nano | U1 | 1 | Use genuine or quality clone |
| 6 | 100 nF capacitor | C2 | 1 | LCSC C521242 |
| 7 | AMS1117-3.3 | U9 | 1 | LCSC C6186 |
| 8 | JST-XH-5A | U10 | 1 | LCSC C161872 |
| 9 | L7805CV regulator | U3 | 1 | LCSC C111887 |
| 10 | ULN2003A | U4 | 1 | LCSC C107221 |
| 11 | 22 µF capacitor | C5 | 1 | LCSC C503893 |
| 12 | JST-XH-3A | U6 | 1 | LCSC C144394 |
| 13 | JST-XH-4A | U7–U8 | 2 | LCSC C161871 |
| 14 | DSWB04LHGET DIP switch | SW1 | 1 | LCSC C964138 |

Get an assortment of JST-XH housings and the matching crimp tool:

- 10 × XH-3A
- 20 × XH-4A
- 10 × XH-5A
- 10 × XH-3Y
- 19 × XH-4Y

Single-row female headers (2.54 mm pitch) are also required:

- 20 × 15-pin strips
- 1 × 4-pin, 2-row header

## Printed Parts Manifest

Download the latest `.3mf` or STL bundle from Printables:
https://www.prusaprinters.org/prints/69464-split-flap-display

Print the following:

- 10 × FlapDrumInner + FlapDrumOuter
- 10 × FrameUnit
- 1 × MiddleFrameLeft / Center / Right
- 1 × FrontCoverLeft / Center / Right
- 1 × BackCoverLeft / Center / Right


## Flap Printing

Flaps are the trickiest part—test a few before committing to all 45 × units.

- Each flap is 1 mm thick; font is *Expressway Condensed Bold*.
- Colour changes happen mid-layer via `M600` commands in the provided `.3mf`
  files.
- Need 10 copies of every flap (two-sided).

**Character sets**

- *Standard set:* `␠ A–Z Ä Ö Ü 0–9 : . - ? !`
- *Noum set (no Umlauts):* `␠ A–Z $ & # 0–9 : . - ? !`

> Pro tip: Open the generated G-code and remove the first `M600` line to avoid a
> filament change at print start.

## Power Consumption Reference

- 13 mA @ 12 V (0.15 W) per unit when idle
- 186 mA @ 12 V (2.22 W) per unit while flipping
- 90 mA @ 12 V (1.08 W) for the ESP-01S hub
- Full 10-unit build with ESP-01S: 220 mA idle / 1.73 A active at 12 V

Plan your PSU accordingly (≥25 W recommended).

