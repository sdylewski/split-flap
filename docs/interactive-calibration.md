# Interactive Calibration Tool

The Interactive Calibration tool (`InteractiveCalibration/InteractiveCalibration.ino`) is an improved alternative to the basic `EEPROM_Write_Offset.ino` utility. It guides you through a step-by-step calibration process to find the optimal offset value for each unit.

## Overview

This tool automatically:
- Homes the unit to find the zero position (magnet/Hall sensor)
- Guides you through measuring 5 flap positions
- Calculates the optimal offset using statistical methods
- Writes the offset to EEPROM automatically

The offset calculation uses the formula: **O + D/2**, where:
- **O** = Distance from home (zero point) to where the blank/space flap flips
- **D** = Average steps per flap (distance between consecutive flaps)
- **D/2** = Half the steps per flap (to center in the middle of the blank flap)

## Prerequisites

- **Important:** Disconnect the unit from all other units and the I2C bus. Only connect power and the USB serial cable. Serial communication can be unreliable when multiple units are connected due to power draw, I2C bus interference, or electrical noise.
- Arduino Nano with Unit.ino firmware (or fresh Arduino)
- USB cable for serial communication
- Serial Monitor access

## Setup

1. **Open the sketch:**
   - Navigate to `InteractiveCalibration/InteractiveCalibration.ino` in Arduino IDE
   - This is a standalone sketch (not in the same folder as `EEPROM_Write_Offset.ino`)

2. **Upload the firmware:**
   - Select *Arduino Nano* board (use *ATmega328P (Old Bootloader)* if uploads fail)
   - Select the correct COM port
   - Upload the sketch

3. **Open Serial Monitor:**
   - Set baud rate to **9600**
   - Set line ending to **"Newline"** or **"Both NL & CR"**
   - **Note:** You may need to press the reset button on the Arduino after upload for serial output to work correctly

## Usage

### Step 1: Initial Setup

After upload and reset, you should see:
```
init
========================================
Interactive Flap Calibration Tool
Firmware Version: 1.0.7
========================================

Steps per flap (calculated): 45.29

Initializing stepper motor...
Starting homing sequence...
```

The unit will automatically home itself (find the magnet/Hall sensor position).

### Step 2: First Measurement

After homing completes, you'll see:
```
>>> What letter do you see? (type letter and press Enter):
```

**Type the letter you see** (e.g., `!`, `A`, `?`, etc.) and press Enter. This tells the tool which flap the unit is currently at.

**Important:** The first measurement can be at any flap - the tool will calculate the offset correctly regardless of where you start.

### Step 3: Subsequent Measurements

After the first measurement, the tool will:
1. Move slowly to the next letter
2. Display: `Moving slowly - press Enter IMMEDIATELY when the next letter flips!`
3. Continue moving very slowly (one step every 100ms)
4. **Press Enter as soon as you see the next letter flip**

The tool will automatically:
- Record the position
- Move to the next letter
- Repeat until 5 measurements are complete

### Step 4: Automatic Completion

After 5 measurements, the tool will:
- Calculate the optimal offset
- Filter out outliers
- Display detailed statistics
- Write the offset to EEPROM
- Show confirmation: `Value written to EEPROM: XX`

### Early Exit

You can type `Q` and press Enter at any time to finish early (after at least 3 measurements).

## How It Works

### Offset Calculation

The tool uses a two-part calculation:

1. **O (Offset to blank flap):**
   - Calculates the distance from home (zero point) to where the blank/space flap (index 0) flips
   - Accounts for which flap you started on
   - Always calculates forward distance (wrapping around if needed) to ensure positive offset

2. **D (Steps per flap):**
   - Measures the distance between consecutive flap flips
   - Uses median and filters outliers (more than 20 steps from median)
   - Averages the valid measurements

3. **Final offset = O + D/2:**
   - Centers the unit in the middle of the blank flap range
   - This is what `Unit.ino` expects when it applies the offset after finding the magnet

### Outlier Filtering

The tool filters outliers to improve accuracy:
- **Step differences:** Filters measurements more than 20 steps from the median
- **First measurement:** Always used (never filtered) since it establishes the starting point
- **Result:** Only truly bad measurements are excluded

### Example Calculation

If you start at "!" (index 44) and measure:
- Measurement 1: "!" at position 0 steps
- Measurement 2: " " (blank) at position 50 steps
- Measurement 3: "A" at position 95 steps
- Measurement 4: "B" at position 140 steps
- Measurement 5: "C" at position 185 steps

Calculation:
- **D** (steps per flap) = Average of (50-0, 95-50, 140-95, 185-140) = 45 steps
- **O** (to blank flap) = 0 + (1 flap × 45 steps) = 45 steps (from "!" to " ")
- **Final offset** = 45 + (45/2) = 45 + 22 = **67 steps**

## Troubleshooting

### Serial Output is Garbled

- Close Serial Monitor completely
- Upload the code again
- Wait 2-3 seconds after upload
- Open Serial Monitor at 9600 baud
- Press the reset button on the Arduino if needed

### Unit Doesn't Home

- Check Hall sensor wiring (pin 7)
- Verify magnet is properly aligned
- Check that Hall sensor LED lights up when near magnet
- Ensure motor can rotate freely (not physically stuck)

### Can't Press Enter in Time

- The tool moves very slowly (100ms per step)
- It continues moving until you press Enter
- You don't need to time it exactly - just press Enter when you see the letter flip
- If you miss it, the tool will timeout after 2× the slow region

### Offset Seems Wrong

- Make sure you're pressing Enter at the exact moment the letter flips
- Try running calibration again - the first 2 measurements are often less accurate
- Check that all 5 measurements were used (not filtered as outliers)
- Verify the calculated steps per flap is reasonable (~45 steps)

### "Q" Doesn't Work

- Make sure line ending is set to "Newline" in Serial Monitor
- Type "Q" and press Enter (not just Enter)
- You need at least 3 measurements before it will finish

## After Calibration

1. **Verify the offset:**
   - The tool displays: `Value written to EEPROM: XX`
   - Note this value for reference

2. **Upload Unit.ino:**
   - The offset is now saved in EEPROM
   - Upload `Unit/Unit.ino` firmware normally
   - The unit will use the saved offset automatically

3. **Test the unit:**
   - Connect to ESPMaster
   - Send a message via web UI
   - Verify the blank/space character displays correctly

## Comparison with EEPROM_Write_Offset.ino

| Feature | EEPROM_Write_Offset | Interactive Calibration |
|---------|---------------------|------------------------|
| Manual entry | Yes, type offset value | No, measures automatically |
| User guidance | Minimal | Step-by-step instructions |
| Measurements | Single value | 5 measurements, filtered |
| Outlier handling | None | Automatic filtering |
| Accuracy | Depends on user | More accurate (averaged) |
| Time required | Fast (if you know offset) | ~5-10 minutes |
| Best for | Quick adjustments | Initial calibration |

## Technical Details

- **Baud rate:** 9600
- **Measurements:** 5 total (automatically stops)
- **Outlier threshold:** 20 steps from median
- **Slow movement speed:** 1 step per 100ms (~10 steps/second)
- **EEPROM address:** 0 (same as EEPROM_Write_Offset.ino)
- **Data type:** uint16_t (0-65535 steps)

## Version History

- **1.0.7:** Fixed offset calculation to account for starting flap position, always calculates forward distance to blank flap
- **1.0.6:** Fixed offset calculation to be blank flap position + half steps per flap
- **1.0.5:** Fixed offset calculation to correctly measure steps per flap and blank flap position
- **1.0.4:** Limited to 5 measurements, ignore first 2 if outliers, filter outliers
- **1.0.3:** Changed slow movement to continue until user presses Enter
- **1.0.2:** Improved homing function with better logic and debug output
- **1.0.1:** Fixed serial initialization issues
- **1.0.0:** Initial version

