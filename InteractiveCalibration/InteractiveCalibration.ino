/*********
  Interactive Flap Calibration Tool
  Version: 1.0.21
  Changes: Updated homing to stop at marker edge (no backup) - matches Unit.ino v1.2.0 behavior
           - Removed backup-to-center logic to match production firmware
           - Both calibration tool and Unit.ino now use same reference point (edge) for offset calculation
           - Prevents mismatch between calibration offset and application offset
  
  Version History:
  - 1.0.21: Updated to stop at marker edge (no backup) to match Unit.ino v1.2.0
  - 1.0.20: Updated homing speed to match Unit.ino (speed 10 instead of 5)
  - 1.0.2: Improved homing function with debug output and proper logic matching Unit.ino behavior
  - 1.0.1: Fixed serial initialization to match EEPROM_Write_Offset.ino pattern (removed delay, removed F() macros)
  - 1.0.0: Initial version - Interactive calibration tool for finding optimal flap offset
  
  This script helps calibrate the flap offset by interactively measuring
  the actual step positions where flaps flip. It homes the unit, then guides
  the user through a calibration process to find the optimal offset value.
  
  Usage:
  1. Upload this sketch to an Arduino Nano
  2. Open Serial Monitor at 9600 baud
  3. Follow the on-screen instructions
  4. The calculated offset will be saved to EEPROM
*********/

#include <Arduino.h>
#include <Stepper.h>
#include <EEPROM.h>

// Stepper motor pins (same as Unit.ino)
#define STEPPERPIN1 11
#define STEPPERPIN2 10
#define STEPPERPIN3 9
#define STEPPERPIN4 8
#define STEPS 2038 // 28BYJ-48 stepper, number of steps per full rotation
#define HALLPIN 7  // Pin of hall sensor
#define AMOUNTFLAPS 45

// Constants
#define BAUDRATE 9600
#define ROTATIONDIRECTION -1 // -1 for reverse direction
#define STEPS_PER_FLAP ((float)STEPS / (float)AMOUNTFLAPS) // ~45.29 steps per flap

// Stepper setup
Stepper stepper(STEPS, STEPPERPIN1, STEPPERPIN3, STEPPERPIN2, STEPPERPIN4);

// Motor control state
bool lastInd1 = false;
bool lastInd2 = false;
bool lastInd3 = false;
bool lastInd4 = false;
int currentlyrotating = 0; // Motor state (not used in this script but needed for compatibility)

// Calibration state
int currentFlapIndex = 0; // Current flap we're trying to measure (0 = blank, 1 = A, etc.)
int firstMeasurementFlapIndex = -1; // Which flap index the first measurement corresponds to
int totalStepsFromHome = 0; // Total steps moved from home position
long measuredPositions[20]; // Store up to 20 measured positions
int measurementCount = 0;
bool calibrationActive = false;

// Hall sensor diagnostics (stored during homing for final summary)
int hallTriggerWidth = -1; // Trigger width in steps (-1 = not measured)
int hallMaxConsecutiveTriggered = -1; // Max consecutive triggered steps
float hallTriggerPercent = -1.0; // Trigger width as % of steps per flap
const char* hallDiagnosticStatus = ""; // Diagnostic status message

// Speed settings
int fastSpeed = 12;  // Fast speed for moving to approximate position
int mediumSpeed = 5; // Medium speed for initial part of slow region
int slowSpeed = 1;   // Slow speed for fine positioning (reduced from 3 to 1 for easier timing)

// Serial input
const byte numChars = 32;
char receivedChars[numChars];
boolean newData = false;

// Flap characters (same as Unit.ino)
const char letters[] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'Ä', 'Ö', 'Ü', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', '.', '-', '?', '!'};

// Forward declarations
void homeUnit();

void setup() {
  Serial.begin(BAUDRATE);
  delay(500); // Give serial time to stabilize
  Serial.println("init");
  Serial.flush();
  delay(100);
  
  pinMode(HALLPIN, INPUT); // Hall sensor pin (no pull-up, sensor provides signal)
  delay(50);
  
  Serial.println("========================================");
  delay(50);
  Serial.println("Interactive Flap Calibration Tool");
  delay(50);
  Serial.println("Firmware Version: 1.0.13");
  delay(50);
  Serial.println("========================================");
  Serial.println();
  delay(100);
  
  Serial.print("Steps per flap (calculated): ");
  Serial.println(STEPS_PER_FLAP, 2);
  Serial.println();
  delay(100);
  
  // Initialize stepper (make sure it's ready)
  Serial.println("Initializing stepper motor...");
  delay(100);
  stepper.setSpeed(10); // Set initial speed (matches Unit.ino homing speed)
  delay(200);
  
  // Home the unit first (start fresh calibration)
  Serial.println("Starting homing sequence...");
  delay(100);
  homeUnit();
  Serial.println("Homing complete!");
  Serial.println();
  delay(100);
  
  // Start calibration
  startCalibration();
}

void loop() {
  if (calibrationActive) {
    // Check for serial input
    recvWithEndMarker();
    if (newData) {
      processUserInput();
      newData = false;
    }
  }
}

void homeUnit() {
  // Simple homing: rotate until Hall sensor detects magnet (matches Unit.ino logic)
  // Use same speed as Unit.ino (stepperSpeed = 10)
  // Don't apply offset - start fresh for calibration
  stepper.setSpeed(10);
  delay(100);
  
  // Check initial Hall sensor state
  int initialHallValue = digitalRead(HALLPIN);
  Serial.print("Initial Hall sensor: ");
  Serial.print(initialHallValue);
  Serial.println(" (1=no magnet, 0=magnet)");
  delay(100);
  
  // Move away from marker if already on it (same as Unit.ino/scientress version)
  if (initialHallValue == 0) {
    Serial.println("At marker, moving 50 steps away...");
    delay(100);
    for (int i = 0; i < 50; i++) {
      stepper.step(ROTATIONDIRECTION * 1); // Normal rotation direction (same as scientress)
    }
    delay(200);
  }
  
  // Rotate until marker found (Hall sensor goes from 1 to 0) - same direction as scientress version
  Serial.println("Searching for marker...");
  Serial.flush();
  delay(100);
  int steps = 0;
  int maxSteps = STEPS * 3; // 3 full rotations max (same as Unit.ino)
  
  Serial.print("Max steps: ");
  Serial.println(maxSteps);
  Serial.flush();
  delay(100);
  
  // Start motor (ensure it's ready)
  startMotor();
  delay(50);
  
  while (steps < maxSteps) {
    int currentHallValue = digitalRead(HALLPIN);
    
    if (currentHallValue == 1) {
      // No magnet detected yet - keep searching (same direction as scientress version)
      stepper.step(ROTATIONDIRECTION * 1); // Normal rotation direction
      steps++;
      
      // Log progress every 500 steps or first few steps
      if (steps % 500 == 0 || steps <= 10) {
        Serial.print("Step ");
        Serial.print(steps);
        Serial.print("/");
        Serial.print(maxSteps);
        Serial.print(" - Hall: ");
        Serial.println(currentHallValue);
        Serial.flush();
        delay(50);
      }
    } else if (currentHallValue == 0 && steps > 0) {
      // Magnet detected! (and we've moved at least 1 step to ensure we found it)
      Serial.print("*** Marker found at step ");
      Serial.print(steps);
      Serial.println(" - Stopped at leading edge (start of magnet)");
      Serial.println("(Staying at leading edge - no forward movement to measure trigger width)");
      Serial.flush();
      
      // STOP HERE - this is the marker edge position (leading edge, start of magnet)
      // We do NOT move forward to measure trigger width - we stop immediately at the leading edge
      // This matches Unit.ino behavior - both stop at the leading edge for consistent offset calculation
      
      // Reset totalStepsFromHome to 0 at the leading edge position (this is our reference point)
      totalStepsFromHome = 0;
      
      stopMotor();
      delay(100);
      break;
    }
    
    // Safety check
    if (steps >= maxSteps) {
      Serial.println("ERROR: Timeout - marker not found after 3 rotations!");
      Serial.flush();
      stopMotor();
      delay(100);
      break;
    }
  }
  
  // Make sure motor is stopped
  stopMotor();
  
  // Don't apply offset - start fresh for calibration
  
  // Found home - reset position counter
  totalStepsFromHome = 0;
  delay(500);
}

void startCalibration() {
  calibrationActive = true;
  currentFlapIndex = 0;
  measurementCount = 0;
  totalStepsFromHome = 0;
  
  Serial.println(F("=== Calibration Started ==="));
  Serial.println();
  Serial.println(F("Instructions:"));
  Serial.println(F("1. The unit will try to go to a letter"));
  Serial.println(F("2. Tell me what letter you see (type the letter and press Enter)"));
  Serial.println(F("3. The unit will rotate slowly - press Enter when the next letter just flips"));
  Serial.println(F("4. Repeat for 5 measurements total (will ignore first 2 and outliers)"));
  Serial.println(F("5. Calibration will finish automatically after 5 measurements"));
  Serial.println();
  
  // Try to go to first letter (blank/space)
  goToFlapIndex(0);
  askUserWhatTheySee();
}

void goToFlapIndex(int targetIndex) {
  if (targetIndex < 0 || targetIndex >= AMOUNTFLAPS) {
    Serial.println(F("Invalid flap index!"));
    return;
  }
  
  // Calculate how many steps we need to move
  int stepsToMove = targetIndex - currentFlapIndex;
  if (stepsToMove < 0) {
    stepsToMove += AMOUNTFLAPS; // Wrap around
  }
  
  // Move using calculated steps per flap
  int steps = (int)(stepsToMove * STEPS_PER_FLAP);
  
  Serial.print(F("Moving to flap index "));
  Serial.print(targetIndex);
  Serial.print(F(" ("));
  Serial.print(letters[targetIndex]);
  Serial.print(F(") - moving "));
  Serial.print(steps);
  Serial.println(F(" steps..."));
  
  stepper.setSpeed(fastSpeed);
  startMotor();
  
  for (int i = 0; i < steps; i++) {
    stepper.step(ROTATIONDIRECTION * 1); // Normal rotation direction (same as scientress)
    totalStepsFromHome += 1;
  }
  
  stopMotor();
  delay(300);
  currentFlapIndex = targetIndex;
}

void goToFlapIndexFastThenSlow(int targetIndex, int slowRegionSteps) {
  if (targetIndex < 0 || targetIndex >= AMOUNTFLAPS) {
    Serial.println(F("Invalid flap index!"));
    return;
  }
  
  // Calculate how many steps we need to move
  int stepsToMove = targetIndex - currentFlapIndex;
  if (stepsToMove < 0) {
    stepsToMove += AMOUNTFLAPS; // Wrap around
  }
  
  // Calculate total steps and slow region
  int totalSteps = (int)(stepsToMove * STEPS_PER_FLAP);
  int fastSteps = totalSteps - slowRegionSteps;
  if (fastSteps < 0) fastSteps = 0;
  
  Serial.print(F("Moving to flap index "));
  Serial.print(targetIndex);
  Serial.print(F(" ("));
  Serial.print(letters[targetIndex]);
  Serial.print(F(") - fast: "));
  Serial.print(fastSteps);
  Serial.print(F(" steps, slow: "));
  Serial.print(slowRegionSteps);
  Serial.println(F(" steps..."));
  
  // Fast movement
  if (fastSteps > 0) {
    stepper.setSpeed(fastSpeed);
    startMotor();
    for (int i = 0; i < fastSteps; i++) {
      stepper.step(ROTATIONDIRECTION * 1); // Normal rotation direction (same as scientress)
      totalStepsFromHome += 1;
    }
    stopMotor();
    delay(200);
  }
  
  // Slow movement - continues until user presses Enter
  if (slowRegionSteps > 0) {
    stepper.setSpeed(slowSpeed);
    startMotor();
    Serial.println("Moving slowly - press Enter IMMEDIATELY when the next letter flips!");
    delay(200);
    
    // Move slowly, checking for user input after each step
    // Start with medium speed for first part, then slow down as we approach target
    int stepsMoved = 0;
    newData = false; // Reset input flag
    int mediumSpeedSteps = slowRegionSteps / 3; // First third at medium speed
    
    while (stepsMoved < slowRegionSteps * 2) { // Allow up to 2x the slow region in case user is slow
      // Check for serial input (user pressing Enter or Q to quit)
      recvWithEndMarker();
      if (newData) {
        // Check if user wants to quit
        String input = String(receivedChars);
        input.trim();
        input.toUpperCase();
        if (input.length() > 0 && input.charAt(0) == 'Q') {
          Serial.println("Q detected - finishing calibration!");
          delay(100);
          stopMotor();
          finishCalibration();
          return;
        }
        // User pressed Enter - stop immediately and record position
        Serial.println("Enter detected - stopping movement!");
        delay(100);
        stopMotor();
        // Clear the input buffer and flag so it doesn't get processed again
        newData = false;
        // Record the position immediately
        recordFlapPosition();
        return; // Exit function - recordFlapPosition() will handle moving to next letter
      }
      
      // Adjust speed based on position in slow region
      // First part: medium speed (faster), then slow down
      if (stepsMoved < mediumSpeedSteps) {
        stepper.setSpeed(mediumSpeed); // Medium speed for first part
        delay(50); // 50ms delay = medium speed
      } else {
        stepper.setSpeed(slowSpeed); // Slow speed for precision
        delay(100); // 100ms delay = very slow
      }
      
      // Move one step
      stepper.step(ROTATIONDIRECTION * 1); // Normal rotation direction (same as scientress)
      totalStepsFromHome += 1;
      stepsMoved++;
    }
    
    stopMotor();
    
    if (stepsMoved >= slowRegionSteps * 2) {
      Serial.println("Slow movement completed (timeout).");
      delay(300);
    }
  }
  
  currentFlapIndex = targetIndex;
}

void askUserWhatTheySee() {
  Serial.println();
  Serial.print(F(">>> What letter do you see? (type letter and press Enter): "));
}

void askUserToPressEnter() {
  Serial.println();
  Serial.print(F(">>> Rotating slowly... Press Enter when the next letter just flips: "));
}

void processUserInput() {
  String input = String(receivedChars);
  
  // Check for space character BEFORE trimming (trim removes spaces)
  // If input is just a space, treat it as the blank/space flap (index 0)
  bool isSpace = false;
  if (input.length() == 1 && input.charAt(0) == ' ') {
    isSpace = true;
  }
  
  input.trim();
  input.toUpperCase();
  
  if (input.length() == 0 && !isSpace) {
    // Empty input (just Enter)
    if (measurementCount == 0) {
      // First measurement - empty input is not valid, need to know what letter they see
      Serial.println();
      Serial.println(F("WARNING: Empty input detected!"));
      Serial.println(F("For the first measurement, you must tell me what letter you see."));
      Serial.println(F("Please type the letter (e.g., '!', ' ' (space), or 'A') and press Enter."));
      Serial.println();
      askUserWhatTheySee();
      return;
    } else {
      // Subsequent measurements - empty input means flap just flipped
      recordFlapPosition();
      return;
    }
  }
  
  if (input.length() > 0 && input.charAt(0) == 'Q') {
    // User wants to quit and save
    finishCalibration();
    return;
  }
  
  // User is telling us what letter they see
  int seenIndex;
  if (isSpace) {
    // Space character was entered - use index 0 (blank/space flap)
    seenIndex = 0;
  } else {
    seenIndex = findLetterIndex(input.charAt(0));
  }
  
  // Validate input
  if (seenIndex == -1) {
    Serial.println(F("Invalid letter! Please try again."));
    askUserWhatTheySee();
    return;
  }
  
  // Special validation for first measurement (measurementCount == 0)
  if (measurementCount == 0) {
    // For first measurement, we expect to be near home position
    // Expected values: '!' (index 44), ' ' (index 0), or 'A' (index 1)
    // These are the most likely flaps to see right after homing
    bool isValidFirstInput = (seenIndex == 44 || seenIndex == 0 || seenIndex == 1);
    
    if (!isValidFirstInput) {
      Serial.println();
      Serial.println(F("WARNING: Unexpected first letter!"));
      Serial.print(F("You entered '"));
      Serial.print(letters[seenIndex]);
      Serial.println(F("'"));
      Serial.println(F("After homing, you should typically see one of:"));
      Serial.println(F("  - '!' (exclamation mark, index 44)"));
      Serial.println(F("  - ' ' (space/blank, index 0)"));
      Serial.println(F("  - 'A' (index 1)"));
      Serial.println();
      Serial.println(F("If you're sure this is correct, you can continue."));
      Serial.println(F("Otherwise, please restart calibration or check your unit."));
      Serial.println();
      delay(2000); // Give user time to read the warning
    }
  }
  
  // Calculate offset from expected position
  int expectedIndex = currentFlapIndex;
  int offset = seenIndex - expectedIndex;
  if (offset > AMOUNTFLAPS / 2) offset -= AMOUNTFLAPS;
  if (offset < -AMOUNTFLAPS / 2) offset += AMOUNTFLAPS;
  
  Serial.print(F("You see '"));
  Serial.print(letters[seenIndex]);
  Serial.print(F("' but expected '"));
  Serial.print(letters[expectedIndex]);
  Serial.print(F("' - offset: "));
  Serial.print(offset);
  Serial.print(F(" flaps ("));
  Serial.print((int)(offset * STEPS_PER_FLAP));
  Serial.println(F(" steps)"));
  
  // Record the current position for the flap the user sees (first measurement only)
  if (measurementCount == 0) {
    // First measurement - record current position and which flap it is
    firstMeasurementFlapIndex = seenIndex;
    measuredPositions[measurementCount] = totalStepsFromHome;
    measurementCount++;
    Serial.print(F("Recorded initial position: "));
    Serial.print(totalStepsFromHome);
    Serial.print(F(" steps for '"));
    Serial.print(letters[seenIndex]);
    Serial.print(F("' (flap index "));
    Serial.print(seenIndex);
    Serial.println(F(")"));
    Serial.print(F("NOTE: This is the flap at the marker edge position (home/reference point)"));
    Serial.println();
    
    // Update current flap index to what user actually sees
    currentFlapIndex = seenIndex;
    
    // Now move to next letter slowly for precise measurement
    int nextIndex = (currentFlapIndex + 1) % AMOUNTFLAPS;
    
    // Determine slow region size based on measurement count
    int slowRegionSteps = (int)(STEPS_PER_FLAP * 1.5); // Large slow region for first measurements
    
    Serial.println();
    Serial.print(F("Moving to next letter '"));
    Serial.print(letters[nextIndex]);
    Serial.print(F("' (flap index "));
    Serial.print(nextIndex);
    Serial.println(F(")..."));
    
    goToFlapIndexFastThenSlow(nextIndex, slowRegionSteps);
    // After movement, ask user to press Enter when next letter flips
    if (!newData) {
      askUserToPressEnter();
    }
  }
  // Note: After first measurement, all subsequent measurements are handled by recordFlapPosition()
  // which is called when user presses Enter (empty input)
}

void recordFlapPosition() {
  // Limit to 5 measurements total
  if (measurementCount >= 5) {
    Serial.println(F("5 measurements completed! Finishing calibration..."));
    finishCalibration();
    return;
  }
  
  // Record the position where this flap just flipped
  measuredPositions[measurementCount] = totalStepsFromHome;
  measurementCount++;
  
  Serial.println();
  Serial.println(); // Extra blank line for better formatting
  Serial.print(F("*** Recorded position: "));
  Serial.print(totalStepsFromHome);
  Serial.print(F(" steps from home (measurement #"));
  Serial.print(measurementCount);
  Serial.println(F(")"));
  
  // Show progress (measurement X of 5)
  Serial.print(F("Progress: Measurement "));
  Serial.print(measurementCount);
  Serial.println(F(" of 5"));
  
  // Check if we have enough measurements - finish calibration
  if (measurementCount >= 5) {
    Serial.println();
    Serial.println(F("5 measurements completed! Finishing calibration..."));
    finishCalibration();
    return;
  }
  
  // Update currentFlapIndex to the flap we just measured (the one that just flipped)
  // This is the next flap after the previous currentFlapIndex
  currentFlapIndex = (currentFlapIndex + 1) % AMOUNTFLAPS;
  
  // Show current calculations if we have enough measurements
  if (measurementCount >= 2) {
    // Calculate current steps per flap
    long sumDiffs = 0;
    for (int i = 0; i < measurementCount - 1; i++) {
      sumDiffs += (measuredPositions[i + 1] - measuredPositions[i]);
    }
    long avgStepsPerFlap = sumDiffs / (measurementCount - 1);
    
    Serial.print(F("Current steps per flap: "));
    Serial.print(avgStepsPerFlap);
    Serial.print(F(" steps"));
    if (measurementCount > 2) {
      Serial.print(F(" (from "));
      Serial.print(measurementCount - 1);
      Serial.print(F(" differences)"));
    }
    Serial.println();
    
    // Show current offset (position of blank flap)
    Serial.print(F("Current offset (blank flap position): "));
    Serial.print(measuredPositions[0]);
    Serial.println(F(" steps from home"));
  }
  
  // Move to next letter (one after the one we just measured)
  int nextIndex = (currentFlapIndex + 1) % AMOUNTFLAPS;
  
  // Determine slow region size based on number of measurements
  int slowRegionSteps;
  if (measurementCount < 2) {
    slowRegionSteps = (int)(STEPS_PER_FLAP * 1.5); // Large slow region for first measurements
  } else if (measurementCount < 5) {
    slowRegionSteps = (int)(STEPS_PER_FLAP * 0.8); // Medium slow region
  } else {
    slowRegionSteps = (int)(STEPS_PER_FLAP * 0.5); // Small slow region for fine tuning
  }
  
  Serial.println();
  Serial.print(F("Moving to next letter '"));
  Serial.print(letters[nextIndex]);
  Serial.print(F("' (flap index "));
  Serial.print(nextIndex);
  Serial.println(F(")..."));
  
  goToFlapIndexFastThenSlow(nextIndex, slowRegionSteps);
  // Don't call askUserToPressEnter() here - if Enter was pressed during movement,
  // recordFlapPosition() was already called and will handle the next step
  if (!newData) {
    askUserToPressEnter();
  }
}

void finishCalibration() {
  calibrationActive = false;
  
  Serial.println();
  Serial.println(F("=== Finishing Calibration ==="));
  
  if (measurementCount < 3) {
    Serial.println(F("Not enough measurements! Need at least 3. Calibration cancelled."));
    return;
  }
  
  // Validate that marker edge is at blank flap (index 0), '!' (index 44), or '?' (index 43)
  // These are valid because the blank flap is close (0-2 flaps backward), so wrap-around is fine
  // Any flap at 'A' (index 1) through '-' (index 42) should give an error
  if (firstMeasurementFlapIndex == -1) {
    Serial.println(F("ERROR: First measurement flap index not recorded!"));
    return;
  }
  
  const int BLANK_FLAP_INDEX = 0; // Blank/space flap is at index 0
  
  bool isValidPosition = (firstMeasurementFlapIndex == 0 || firstMeasurementFlapIndex == 43 || firstMeasurementFlapIndex == 44);
  if (!isValidPosition) {
    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("ERROR: Marker position invalid!"));
    Serial.println(F("========================================"));
    Serial.println();
    Serial.print(F("Marker edge is at flap index "));
    Serial.print(firstMeasurementFlapIndex);
    Serial.print(F(" ('"));
    Serial.print(letters[firstMeasurementFlapIndex]);
    Serial.println(F("')"));
    Serial.print(F("Blank flap is at index 0 ('"));
    Serial.print(letters[0]);
    Serial.println(F("')"));
    Serial.println();
    Serial.println(F("The marker edge must be at one of these positions:"));
    Serial.println(F("  - Blank flap (' ', index 0)"));
    Serial.println(F("  - '?' (index 43)"));
    Serial.println(F("  - '!' (index 44)"));
    Serial.println();
    Serial.println(F("Any flap from 'A' (index 1) through '-' (index 42) is invalid."));
    Serial.println();
    Serial.println(F("SOLUTION:"));
    Serial.println(F("1. Physically move the hall sensor/magnet so the marker edge"));
    Serial.println(F("   appears at the blank flap (' '), '?', or '!'"));
    Serial.println(F("2. Re-run calibration after repositioning"));
    Serial.println();
    Serial.println(F("Calibration cancelled."));
    Serial.println(F("========================================"));
    return; // Exit without saving
  }
  
  // Calculate offset using ACTUAL MEASURED positions, not theoretical calculations
  // Find which measurement corresponds to the blank flap (index 0)
  int blankFlapMeasurementIndex = -1;
  for (int i = 0; i < measurementCount; i++) {
    int measurementFlapIndex = (firstMeasurementFlapIndex + i) % AMOUNTFLAPS;
    if (measurementFlapIndex == BLANK_FLAP_INDEX) {
      blankFlapMeasurementIndex = i;
      break;
    }
  }
  
  long finalOffset;
  if (blankFlapMeasurementIndex >= 0) {
    // We have a measurement for the blank flap - use the actual measured position
    long stepsToBlankEdge = measuredPositions[blankFlapMeasurementIndex] - measuredPositions[0];
    // Calculate half flap from actual measured steps per flap
    long avgStepsPerFlap = 0;
    if (measurementCount > 1) {
      long sumDiffs = 0;
      for (int i = 0; i < measurementCount - 1; i++) {
        sumDiffs += (measuredPositions[i + 1] - measuredPositions[i]);
      }
      avgStepsPerFlap = sumDiffs / (measurementCount - 1);
    } else {
      avgStepsPerFlap = (long)(STEPS_PER_FLAP + 0.5); // Fallback to theoretical if only one measurement
    }
    long halfFlap = avgStepsPerFlap / 2;
    finalOffset = stepsToBlankEdge + halfFlap;
  } else {
    // Fallback: calculate from flap indices if blank flap wasn't measured
    // This shouldn't happen if validation is working correctly
    int directBackward = firstMeasurementFlapIndex - BLANK_FLAP_INDEX;
    int wrapAroundBackward = (AMOUNTFLAPS - firstMeasurementFlapIndex + BLANK_FLAP_INDEX) % AMOUNTFLAPS;
    int flapsBackwardToBlank = (directBackward <= wrapAroundBackward) ? directBackward : wrapAroundBackward;
    long stepsPerFlap = (long)(STEPS_PER_FLAP + 0.5);
    long halfFlap = stepsPerFlap / 2;
    finalOffset = ((long)flapsBackwardToBlank * stepsPerFlap) + halfFlap;
  }
  
  // Warn if offset is very small (marker is at end of blank flap range)
  long minOffsetWarning = avgStepsPerFlap > 0 ? avgStepsPerFlap : (long)(STEPS_PER_FLAP + 0.5);
  if (finalOffset < minOffsetWarning) {
    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("WARNING: Very small offset detected!"));
    Serial.println(F("========================================"));
    Serial.println();
    Serial.print(F("Calculated offset: "));
    Serial.print(finalOffset);
    Serial.println(F(" steps"));
    Serial.print(F("This is less than one flap's worth ("));
    Serial.print(minOffsetWarning);
    Serial.println(F(" steps)"));
    Serial.println();
    Serial.println(F("This means the marker edge is positioned at or very near the end"));
    Serial.println(F("of the blank flap range. The calibration will proceed, but you may"));
    Serial.println(F("want to consider moving the hall sensor/magnet slightly earlier to"));
    Serial.println(F("provide more margin."));
    Serial.println();
    Serial.println(F("Calibration will continue..."));
    Serial.println(F("========================================"));
    Serial.println();
  }
  
  // Validate offset is within one rotation (should always be true now that we reject wrap-around cases)
  if (finalOffset >= STEPS || finalOffset < 0) {
    Serial.println();
    Serial.println(F("ERROR: Calculated offset is out of range!"));
    Serial.print(F("Offset: "));
    Serial.print(finalOffset);
    Serial.print(F(" steps (should be 0-"));
    Serial.print(STEPS - 1);
    Serial.println(F(")"));
    Serial.println(F("This should not happen - please report this error."));
    Serial.println(F("Calibration cancelled."));
    return; // Exit without saving
  }
  
  // Display offset calculation summary
  Serial.println();
  Serial.println(F("--- Offset Calculation Summary ---"));
  Serial.print(F("Marker edge position: flap index "));
  Serial.print(firstMeasurementFlapIndex);
  Serial.print(F(" ('"));
  Serial.print(letters[firstMeasurementFlapIndex]);
  Serial.println(F("')"));
  
  if (blankFlapMeasurementIndex >= 0) {
    // Using actual measured position
    Serial.print(F("Blank flap measured at: "));
    Serial.print(measuredPositions[blankFlapMeasurementIndex]);
    Serial.println(F(" steps from marker"));
    Serial.print(F("Steps to blank flap edge: "));
    Serial.print(stepsToBlankEdge);
    Serial.println(F(" steps (measured)"));
    Serial.print(F("Average steps per flap: "));
    Serial.print(avgStepsPerFlap);
    Serial.println(F(" steps (from measurements)"));
    Serial.print(F("Half flap (to center): "));
    Serial.print(halfFlap);
    Serial.println(F(" steps"));
    Serial.print(F("Final offset: "));
    Serial.print(finalOffset);
    Serial.println(F(" steps"));
  } else {
    // Fallback calculation
    Serial.println(F("(Using fallback calculation - blank flap not measured)"));
    Serial.print(F("Final offset: "));
    Serial.print(finalOffset);
    Serial.println(F(" steps"));
  }
  Serial.println();
  Serial.println(F("--- Offset Interpretation ---"));
  Serial.print(F("Marker edge: 0 steps (home/reference point)"));
  Serial.println();
  Serial.print(F("Blank flap center: "));
  Serial.print(finalOffset);
  Serial.println(F(" steps from marker edge"));
  Serial.print(F("(Marker edge + (ROTATIONDIRECTION * offset) = center of blank flap)"));
  Serial.println();
  Serial.println();
  
  // Show individual measurements
  Serial.println(F("Individual measurements:"));
  Serial.flush(); // Ensure all output is sent before continuing
  delay(50);
  
  for (int i = 0; i < measurementCount; i++) {
    int measurementFlapIndex = (firstMeasurementFlapIndex + i) % AMOUNTFLAPS;
    Serial.print(F("  Measurement "));
    Serial.print(i + 1);
    Serial.print(F(": flap '"));
    Serial.print(letters[measurementFlapIndex]);
    Serial.print(F("' (index "));
    Serial.print(measurementFlapIndex);
    Serial.print(F(") at position "));
    Serial.print(measuredPositions[i]);
    Serial.print(F(" steps from home"));
    if (i == 0) {
      Serial.print(F(" (marker edge position)"));
    }
    Serial.println();
    
    // Flush periodically to prevent buffer overflow
    if (i % 2 == 0) {
      Serial.flush();
      delay(10);
    }
  }
  
  Serial.flush(); // Ensure all output is sent before continuing
  delay(50);
  
  // Save to EEPROM (same address and method as EEPROM_Write_Offset.ino, but using uint16_t to match Unit.ino)
  int eeAddress = 0;   // Location we want the data to be put (same as EEPROM_Write_Offset.ino)
  uint16_t offsetToSave = (uint16_t)finalOffset;  // Use uint16_t to match Unit.ino which reads it as uint16_t
  
  // Ensure it fits in uint16_t (0-65535)
  if (offsetToSave > 65535) {
    Serial.println(F("WARNING: Offset too large for uint16_t! Clamping to 65535."));
    offsetToSave = 65535;
  }
  
  EEPROM.put(eeAddress, offsetToSave);  // Same method as EEPROM_Write_Offset.ino (writes to same address)
  
  Serial.flush(); // Ensure all output is sent before continuing
  delay(100);
  
  Serial.println();
  Serial.println(F("=== EEPROM Write ==="));
  Serial.print(F("Value written to EEPROM: "));
  Serial.println(offsetToSave);
  Serial.println();
  
  Serial.flush(); // Ensure all output is sent before continuing
  delay(100);
  
  // Final Summary Block
  Serial.println(F("========================================"));
  Serial.println(F("=== CALIBRATION SUMMARY ==="));
  Serial.println(F("========================================"));
  Serial.println();
  
  Serial.flush(); // Ensure all output is sent before continuing
  delay(100);
  
  Serial.println(F("--- Calibration Results ---"));
  Serial.print(F("Final Offset: "));
  Serial.print(finalOffset);
  Serial.println(F(" steps"));
  Serial.print(F("Steps per Flap: "));
  Serial.print(stepsPerFlap);
  Serial.println(F(" steps"));
  Serial.print(F("Measurements Used: "));
  Serial.print(measurementCount);
  Serial.println(F(" of 5"));
  Serial.println();
  
  Serial.println(F("--- Hall Sensor Diagnostics ---"));
  if (hallTriggerWidth >= 0) {
    Serial.print(F("Trigger Width: "));
    Serial.print(hallTriggerWidth);
    Serial.print(F(" steps ("));
    Serial.print(hallTriggerPercent, 1);
    Serial.println(F("% of flap)"));
    Serial.print(F("Max Consecutive Triggered: "));
    Serial.print(hallMaxConsecutiveTriggered);
    Serial.println(F(" steps"));
    if (hallDiagnosticStatus[0] != '\0') {
      Serial.print(F("Status: "));
      Serial.println(hallDiagnosticStatus);
    }
  } else {
    Serial.println(F("Hall sensor diagnostics not available"));
  }
  Serial.println();
  
  Serial.println(F("--- Next Steps ---"));
  Serial.println(F("1. Calibration offset saved to EEPROM"));
  Serial.println(F("2. Upload Unit.ino firmware to use this offset"));
  Serial.println(F("3. Test the unit to verify calibration accuracy"));
  if (hallTriggerWidth >= 0 && hallTriggerWidth < 5) {
    Serial.println(F("4. NOTE: Narrow trigger region detected - check magnet alignment"));
  }
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("Calibration complete!"));
  Serial.println(F("========================================"));
  
  Serial.flush(); // Ensure all output is sent
  delay(100);
}

int findLetterIndex(char letter) {
  for (int i = 0; i < AMOUNTFLAPS; i++) {
    if (letters[i] == letter) {
      return i;
    }
  }
  return -1;
}

void startMotor() {
  lastInd1 = digitalRead(STEPPERPIN1);
  lastInd2 = digitalRead(STEPPERPIN2);
  lastInd3 = digitalRead(STEPPERPIN3);
  lastInd4 = digitalRead(STEPPERPIN4);
  currentlyrotating = 1;
  digitalWrite(STEPPERPIN1, lastInd1);
  digitalWrite(STEPPERPIN2, lastInd2);
  digitalWrite(STEPPERPIN3, lastInd3);
  digitalWrite(STEPPERPIN4, lastInd4);
}

void stopMotor() {
  lastInd1 = digitalRead(STEPPERPIN1);
  lastInd2 = digitalRead(STEPPERPIN2);
  lastInd3 = digitalRead(STEPPERPIN3);
  lastInd4 = digitalRead(STEPPERPIN4);
  
  digitalWrite(STEPPERPIN1, LOW);
  digitalWrite(STEPPERPIN2, LOW);
  digitalWrite(STEPPERPIN3, LOW);
  digitalWrite(STEPPERPIN4, LOW);
  currentlyrotating = 0;
  delay(100);
}

void recvWithEndMarker() {
  static byte ndx = 0;
  char endMarker = '\n';
  char rc;
  
  while (Serial.available() > 0 && newData == false) {
    rc = Serial.read();
    
    if (rc != endMarker) {
      receivedChars[ndx] = rc;
      ndx++;
      if (ndx >= numChars) {
        ndx = numChars - 1;
      }
    } else {
      receivedChars[ndx] = '\0';
      ndx = 0;
      newData = true;
    }
  }
}

