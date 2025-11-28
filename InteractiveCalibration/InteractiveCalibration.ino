/*********
  Interactive Flap Calibration Tool
  Version: 1.0.1
  Changes: Removed delay after Serial.begin() and F() macros to match working EEPROM_Write_Offset pattern
           Fixed serial communication issues (garbled characters)
  
  Version History:
  - 1.0.1: Fixed serial initialization to match EEPROM_Write_Offset.ino pattern (removed delay, removed F() macros)
  - 1.0.0: Initial version - Interactive calibration tool for finding optimal flap offset
  
  This script helps calibrate the flap offset by interactively measuring
  the actual step positions where flaps flip. It homes the unit, then guides
  the user through a calibration process to find the optimal offset value.
  
  Usage:
  1. Upload this sketch to an Arduino Nano
  2. Open Serial Monitor at 57600 baud
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
#define BAUDRATE 57600
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
int totalStepsFromHome = 0; // Total steps moved from home position
long measuredPositions[20]; // Store up to 20 measured positions
int measurementCount = 0;
bool calibrationActive = false;

// Speed settings
int fastSpeed = 12;  // Fast speed for moving to approximate position
int slowSpeed = 3;   // Slow speed for fine positioning

// Serial input
const byte numChars = 32;
char receivedChars[numChars];
boolean newData = false;

// Flap characters (same as Unit.ino)
const char letters[] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'Ä', 'Ö', 'Ü', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', '.', '-', '?', '!'};

void setup() {
  Serial.begin(BAUDRATE);
  // No delay - match EEPROM_Write_Offset.ino pattern that works
  Serial.println("init"); // Send immediate message like EEPROM_Write_Offset does
  
  pinMode(HALLPIN, INPUT);
  
  Serial.println("========================================");
  Serial.println("Interactive Flap Calibration Tool");
  Serial.println("Firmware Version: 1.0.1");
  Serial.println("========================================");
  Serial.println();
  Serial.print("Steps per flap (calculated): ");
  Serial.println(STEPS_PER_FLAP, 2);
  Serial.println();
  
  // Home the unit first
  Serial.println("Homing unit...");
  homeUnit();
  Serial.println("Homing complete!");
  Serial.println();
  
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
  // Simple homing: rotate until Hall sensor detects magnet
  stepper.setSpeed(5);
  startMotor();
  
  // Move away from marker if already on it
  if (digitalRead(HALLPIN) == 0) {
    for (int i = 0; i < 50; i++) {
      stepper.step(ROTATIONDIRECTION * 1);
    }
  }
  
  // Rotate until marker found
  int steps = 0;
  while (digitalRead(HALLPIN) == 1 && steps < STEPS * 3) {
    stepper.step(ROTATIONDIRECTION * 1);
    steps++;
  }
  
  // Found home - reset position counter
  totalStepsFromHome = 0;
  stopMotor();
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
  Serial.println(F("4. Repeat for several letters to get accurate calibration"));
  Serial.println(F("5. Type 'Q' and press Enter to finish and save offset"));
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
    stepper.step(ROTATIONDIRECTION * 1);
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
      stepper.step(ROTATIONDIRECTION * 1);
      totalStepsFromHome += 1;
    }
    stopMotor();
    delay(200);
  }
  
  // Slow movement
  if (slowRegionSteps > 0) {
    stepper.setSpeed(slowSpeed);
    startMotor();
    for (int i = 0; i < slowRegionSteps; i++) {
      stepper.step(ROTATIONDIRECTION * 1);
      totalStepsFromHome += 1;
    }
    stopMotor();
    delay(300);
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
  input.trim();
  input.toUpperCase();
  
  if (input.length() == 0) {
    // Empty input (just Enter) - user is indicating flap just flipped
    recordFlapPosition();
    return;
  }
  
  if (input.charAt(0) == 'Q') {
    // User wants to quit and save
    finishCalibration();
    return;
  }
  
  // User is telling us what letter they see
  int seenIndex = findLetterIndex(input.charAt(0));
  if (seenIndex == -1) {
    Serial.println(F("Invalid letter! Please try again."));
    askUserWhatTheySee();
    return;
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
  
  // Record the current position for the flap the user sees
  if (measurementCount == 0) {
    // First measurement - record current position
    measuredPositions[measurementCount] = totalStepsFromHome;
    measurementCount++;
    Serial.print(F("Recorded initial position: "));
    Serial.print(totalStepsFromHome);
    Serial.print(F(" steps for '"));
    Serial.print(letters[seenIndex]);
    Serial.println(F("'"));
  }
  
  // Update current flap index to what user actually sees
  currentFlapIndex = seenIndex;
  
  // Now move to next letter slowly for precise measurement
  int nextIndex = (currentFlapIndex + 1) % AMOUNTFLAPS;
  
  // Determine slow region size based on measurement count
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
  Serial.println(F("'..."));
  
  goToFlapIndexFastThenSlow(nextIndex, slowRegionSteps);
  askUserToPressEnter();
}

void recordFlapPosition() {
  if (measurementCount >= 20) {
    Serial.println(F("Maximum measurements reached!"));
    finishCalibration();
    return;
  }
  
  // Record the position where this flap just flipped
  measuredPositions[measurementCount] = totalStepsFromHome;
  measurementCount++;
  
  Serial.println();
  Serial.print(F("*** Recorded position: "));
  Serial.print(totalStepsFromHome);
  Serial.print(F(" steps from home (measurement #"));
  Serial.print(measurementCount);
  Serial.println(F(")"));
  
  // Calculate current average offset
  if (measurementCount >= 2) {
    long sum = 0;
    for (int i = 0; i < measurementCount; i++) {
      // Calculate expected position for each measurement (flap 0, 1, 2, etc.)
      long expectedPos = (long)(i * STEPS_PER_FLAP);
      long offset = measuredPositions[i] - expectedPos;
      sum += offset;
    }
    long avgOffset = sum / measurementCount;
    
    Serial.print(F("Current average offset: "));
    Serial.print(avgOffset);
    Serial.print(F(" steps ("));
    Serial.print((float)avgOffset / STEPS_PER_FLAP, 2);
    Serial.println(F(" flaps)"));
  }
  
  // Move to next letter
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
  Serial.println(F("'..."));
  
  goToFlapIndexFastThenSlow(nextIndex, slowRegionSteps);
  askUserToPressEnter();
}

void finishCalibration() {
  calibrationActive = false;
  
  Serial.println();
  Serial.println(F("=== Finishing Calibration ==="));
  
  if (measurementCount < 2) {
    Serial.println(F("Not enough measurements! Need at least 2. Calibration cancelled."));
    return;
  }
  
  // Calculate average offset
  long sum = 0;
  for (int i = 0; i < measurementCount; i++) {
    long expectedPos = (long)(i * STEPS_PER_FLAP);
    long offset = measuredPositions[i] - expectedPos;
    sum += offset;
  }
  long avgOffset = sum / measurementCount;
  
  Serial.print(F("Measurements taken: "));
  Serial.println(measurementCount);
  Serial.print(F("Calculated average offset: "));
  Serial.print(avgOffset);
  Serial.println(F(" steps"));
  
  // Show individual measurements
  Serial.println(F("Individual measurements:"));
  for (int i = 0; i < measurementCount; i++) {
    long expectedPos = (long)(i * STEPS_PER_FLAP);
    long offset = measuredPositions[i] - expectedPos;
    Serial.print(F("  Flap "));
    Serial.print(i);
    Serial.print(F(": expected "));
    Serial.print(expectedPos);
    Serial.print(F(", measured "));
    Serial.print(measuredPositions[i]);
    Serial.print(F(", offset "));
    Serial.println(offset);
  }
  
  // Save to EEPROM
  int eeAddress = 0;
  uint16_t offsetToSave = (uint16_t)avgOffset;
  EEPROM.put(eeAddress, offsetToSave);
  
  Serial.println();
  Serial.print(F("Offset saved to EEPROM: "));
  Serial.println(offsetToSave);
  Serial.println(F("Calibration complete!"));
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

