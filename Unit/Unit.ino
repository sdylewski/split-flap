/*********
  Split Flap Arduino Nano Unit
  Version: 1.2.0
  Changes: Forward-only rotation - all backward movements replaced with forward wrap-around to prevent mechanical issues
  
  Version History:
  - 1.2.0: Forward-only rotation - replaced all backward movements with forward wrap-around, updated calibration to use forward-only positioning
  - 1.1.1: Fixed calibration to move away from marker if already at it on startup (prevents double offset), added trigger width measurement and center positioning
  - 1.1.0: Added expanded I2C status codes (0=ready, 1=busy, 2=cal_searching, 3=cal_offset, 4=cal_error, -1=sleeping)
  
  Major Changes from Scientress Fork:
  - Expanded I2C status reporting: Units now report detailed calibration states (searching, applying offset, errors)
  - Improved serial debugging: Added delay after Serial.begin() to prevent garbled characters, added timestamps to all serial output
  - Version tracking: Added firmware version number for easier debugging and verification
  - Simplified calibration code: Removed excessive debug logging to match original working version
  - Fixed calibration startup: Handles case where unit reboots while already at marker position
*********/

//#define SERIAL_ENABLE // uncomment for serial debug communication
//#define TEST_ENABLE   // uncomment for Test mode. Rotates through a few character to make sure unit is working. These characters should be displayed in the correct order: " ", "Z", "A", "U", "N", "?", "0", "1", "2", "9"

#include <Arduino.h>
#include <Wire.h>
#include <Stepper.h>
#include <EEPROM.h>
#include <avr/sleep.h>

// Pins of I2C adress switch
#define ADRESSSW1 6
#define ADRESSSW2 5
#define ADRESSSW3 4
#define ADRESSSW4 3

//constants stepper
#define STEPPERPIN1 11
#define STEPPERPIN2 10
#define STEPPERPIN3 9
#define STEPPERPIN4 8
#define STEPS 2038 //28BYJ-48 stepper, number of steps
#define HALLPIN 7 //Pin of hall sensor
#define AMOUNTFLAPS 45

//constants others
#define BAUDRATE 9600
#define ROTATIONDIRECTION -1 //-1 for reverse direction
#define OVERHEATINGTIMEOUT 2 //timeout in seconds to avoid overheating of stepper. After starting rotation, the counter will start. Stepper won't move again until timeout is passed
unsigned long lastRotation = 0;

#ifdef SERIAL_ENABLE
//Serial input stuff
const byte numChars = 32;
char receivedChars[numChars]; // an array to store the received data
boolean newData = false;
#endif

//globals
int displayedLetter = 0; //currently shown letter
int desiredLetter = 0; //letter to be shown
const char letters[] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'Ä', 'Ö', 'Ü', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', '.', '-', '?', '!'};
Stepper stepper(STEPS, STEPPERPIN1, STEPPERPIN3, STEPPERPIN2, STEPPERPIN4); //stepper setup
bool lastInd1 = false; //store last status of phase
bool lastInd2 = false; //store last status of phase
bool lastInd3 = false; //store last status of phase
bool lastInd4 = false; //store last status of phase
float missedSteps = 0; //cummulate steps <1, to compensate via additional step when reaching >1
int currentlyrotating = 0; // Status codes: 0=ready, 1=busy/moving, 2=calibrating(searching), 3=calibrating(offset), 4=calibration_error, -1=sleeping
int stepperSpeed = 10; //current speed of stepper, value only for first homing
int eeAddress = 0;   //EEPROM address for calibration offset
uint16_t calOffset;       //Offset for calibration in steps, stored in EEPROM, gets read in setup
int receivedNumber = 0;
int i2cAddress;

//sleep globals
const unsigned long WAIT_TIME = 2000;    //wait time before sleep routine gets executed again in milliseconds
unsigned long previousMillis = 0;       //stores last time sleep was interrupted

//test calibration settings
#if defined(SERIAL_ENABLE) || defined(TEST_ENABLE)
int calLetters[10] = {0, 26, 1, 21, 14, 43, 30, 31, 32, 39};
//int calLetters[10] = {0, 6, 1, 5, 0};

void run_test() {
  stepperSpeed = 12;
  for (int i = 0; i < 10; i++) {
    int currentCalLetter = calLetters[i];
    rotateToLetter(currentCalLetter);
    delay(2000);
  }
}
#endif

#ifdef SERIAL_ENABLE
// Helper function to print timestamp before serial messages
void SerialPrintTimestamp() {
  Serial.print(F("["));
  Serial.print(millis());
  Serial.print(F("ms] "));
}
#endif

//setup
void setup() {
  // i2c adress switch
  pinMode(ADRESSSW1, INPUT_PULLUP);
  pinMode(ADRESSSW2, INPUT_PULLUP);
  pinMode(ADRESSSW3, INPUT_PULLUP);
  pinMode(ADRESSSW4, INPUT_PULLUP);

  //hall sensor
  pinMode(HALLPIN, INPUT);

  i2cAddress = getaddress(); //get I2C Address and save in variable

#ifdef SERIAL_ENABLE
  //initialize serial
  Serial.begin(BAUDRATE);
  delay(100); // Wait for serial port to initialize (helps prevent garbled characters)
  SerialPrintTimestamp();
  Serial.println("starting unit");
  SerialPrintTimestamp();
  Serial.print("Firmware Version: ");
  Serial.println("1.2.0");
  SerialPrintTimestamp();
  Serial.print("I2CAddress: ");
  Serial.println(i2cAddress);
#endif

  //I2C function assignment
  Wire.begin(i2cAddress); //i2c address of this unit
  Wire.onReceive(receiveLetter);//call-function for transfered letter via i2c
  Wire.onRequest(requestEvent); //call-funtion if master requests unit state

  getOffset();     //get calibration offset from EEPROM
  calibrate(true); //home stepper after startup

#ifdef TEST_ENABLE
  // Run test on startup
  run_test();
#endif
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= WAIT_TIME) {
#ifdef SERIAL_ENABLE
    // Only log sleep/wake if unit is busy (unusual state) or first few cycles for debugging
    static int sleepCycleCount = 0;
    if (currentlyrotating != 0 || sleepCycleCount < 3) {
      SerialPrintTimestamp();
      Serial.print(F("Going to sleep (currentlyrotating="));
      Serial.print(currentlyrotating);
      Serial.println(F(")"));
    }
    sleepCycleCount++;
#endif
    byte old_ADCSRA = ADCSRA;
    // disable ADC
    ADCSRA = 0;
#ifdef SERIAL_ENABLE
    set_sleep_mode (SLEEP_MODE_IDLE);
#else
    set_sleep_mode (SLEEP_MODE_PWR_DOWN);
#endif
    sleep_enable();
#ifndef SERIAL_ENABLE
    digitalWrite (LED_BUILTIN, LOW); // shuts off LED when starting to sleep, for debugging
#endif
    sleep_cpu ();
#ifndef SERIAL_ENABLE
    digitalWrite (LED_BUILTIN, HIGH); // turns on LED when waking up, for debugging
#endif
    sleep_disable();
    previousMillis = currentMillis; //reset sleep counter
    ADCSRA = old_ADCSRA;

#ifdef SERIAL_ENABLE
    // Only log wake/I2C reinit if unit is busy or first few cycles
    if (currentlyrotating != 0 || sleepCycleCount <= 3) {
      SerialPrintTimestamp();
      Serial.println(F("Woke up from sleep"));
      SerialPrintTimestamp();
      Serial.println(F("I2C reinitialized after sleep"));
    }
#endif
    // release TWI bus
    TWCR = bit(TWEN) | bit(TWIE) | bit(TWEA) | bit(TWINT);

    // turn it back on again
    Wire.begin (i2cAddress);
  }  // end of time to sleep

  //check if new letter was received through i2c
  if (displayedLetter != receivedNumber)
  {
    /*
      #ifdef SERIAL_ENABLE
      SerialPrintTimestamp();
      Serial.print("Value over serial received: ");
      Serial.print(receivedNumber);
      Serial.print(" Letter: ");
      Serial.print(letters[receivedNumber]);
      Serial.println();
      #endif
    */
    //rotate to new letter
    rotateToLetter(receivedNumber);
  }

#ifdef SERIAL_ENABLE
  // check if we got calibration data via serial
  recvWithEndMarker();
#endif
}

int translateLettertoInt(char letterchar) {
  // automatically convert lower case to upper case
  if ('a' <= letterchar && letterchar <= 'z') {
    letterchar -= 32;
  }
  for (int flapIndex = 0; flapIndex < sizeof(letters); flapIndex++) {
    if (letterchar == letters[flapIndex]) {
      return flapIndex;
    }
  }

  return -1;
}

//rotate to letter
void rotateToLetter(int toLetter) {
  if (lastRotation == 0 || (millis() - lastRotation > OVERHEATINGTIMEOUT * 1000)) {
    lastRotation = millis();
    //get letter position
    int posLetter = -1;
    posLetter = toLetter;
    int posCurrentLetter = -1;
    posCurrentLetter = displayedLetter;
    //int amountLetters = sizeof(letters) / sizeof(String);
#ifdef SERIAL_ENABLE
    SerialPrintTimestamp();
    Serial.print(F("go to letter: "));
    Serial.println(letters[toLetter]);
#endif
    //go to letter, but only if available (>-1)
    if (posLetter > -1) { //check if letter exists
      bool motorStarted = false;
      //check if letter is on higher index, then no full rotaion is needed
      if (posLetter >= posCurrentLetter) {
#ifdef SERIAL_ENABLE
        SerialPrintTimestamp();
        Serial.println("direct");
#endif
        //go directly to next letter, get steps from current letter to target letter
        int diffPosition = posLetter - posCurrentLetter;
        startMotor();
        motorStarted = true;
        stepper.setSpeed(stepperSpeed);
        //doing the rotation letterwise
        for (int i = 0; i < diffPosition; i++) {
          float preciseStep = (float)STEPS / (float)AMOUNTFLAPS;
          int roundedStep = (int)preciseStep;
          missedSteps = missedSteps + ((float)preciseStep - (float)roundedStep);
          if (missedSteps > 1) {
            roundedStep = roundedStep + 1;
            missedSteps--;
          }
          stepper.step(ROTATIONDIRECTION * roundedStep);
        }
      }
      else {
        //full rotation is needed, good time for a calibration
#ifdef SERIAL_ENABLE
        SerialPrintTimestamp();
        Serial.println(F("full rotation incl. calibration"));
#endif
        startMotor(); // Ensure motor is started before calibration
        motorStarted = true;
        int calResult = calibrate(false); //calibrate revolver and do not stop motor
        if (calResult < 0) {
          // Calibration failed - stop motor and reset status to prevent getting stuck
          stopMotor();
          return; // Exit early if calibration failed
        }
        stepper.setSpeed(stepperSpeed);
        for (int i = 0; i < posLetter; i++) {
          float preciseStep = (float)STEPS / (float)AMOUNTFLAPS;
          int roundedStep = (int)preciseStep;
          missedSteps = missedSteps + (float)preciseStep - (float)roundedStep;
          if (missedSteps > 1) {
            roundedStep = roundedStep + 1;
            missedSteps--;
          }
          stepper.step(ROTATIONDIRECTION * roundedStep);
        }
      }
      //store new position
      displayedLetter = toLetter;
      //rotation is done, stop the motor (always ensure motor is stopped)
      if (motorStarted) {
        delay(100); //important to stop rotation before shutting of the motor to avoid rotation after switching off current
        stopMotor();
      }
    }
    else {
#ifdef SERIAL_ENABLE
      SerialPrintTimestamp();
      Serial.println("letter unknown, go to space");
#endif
      desiredLetter = 0;
    }
  }
}

void receiveLetter(int numBytes) {
  int receiveArray[4]; //array for received bytes
  uint16_t newCalOffset = 0;

  for (int i = 0; i < numBytes && i < 4; i++) {
    receiveArray[i] = Wire.read();
  }
  //Write received bytes to correct variables
  receivedNumber = receiveArray[0];
  stepperSpeed = receiveArray[1];
  if (numBytes >= 4) {
    newCalOffset = receiveArray[2] + (receiveArray[3] << 8);
    if (newCalOffset != calOffset) {
      calOffset = newCalOffset;
      writeToEEPROM(calOffset);
    }
  }
}

void requestEvent() {
#ifdef SERIAL_ENABLE
  SerialPrintTimestamp();
  Serial.print(F("Status request received - sending: "));
  Serial.print(currentlyrotating);
  Serial.print(F(" (0=ready, 1=busy, 2=cal_searching, 3=cal_offset, 4=cal_error, -1=sleeping)"));
  Serial.print(F(" | displayedLetter: "));
  Serial.print(displayedLetter);
  Serial.print(F(" | receivedNumber: "));
  Serial.println(receivedNumber);
#endif
  Wire.write(currentlyrotating); //send unit status to master
}

//returns the adress of the unit as int from 0-15
int getaddress() {
  int address = !digitalRead(ADRESSSW4) + (!digitalRead(ADRESSSW3) * 2) + (!digitalRead(ADRESSSW2) * 4) + (!digitalRead(ADRESSSW1) * 8);
  return address;
}

//gets magnet sensor offset from EEPROM in steps
void getOffset() {
  EEPROM.get(eeAddress, calOffset);
  if (calOffset < 0) {
    calOffset = 0;
  }
#ifdef SERIAL_ENABLE
  SerialPrintTimestamp();
  Serial.print(F("CalOffset from EEPROM: "));
  Serial.print(calOffset);
  Serial.print(F(" (raw value from EEPROM)"));
  Serial.println();
  SerialPrintTimestamp();
  Serial.print(F("EEPROM address: "));
  Serial.print(eeAddress);
  Serial.println();
  SerialPrintTimestamp();
  Serial.print(F("Offset will be applied as: "));
  Serial.print(ROTATIONDIRECTION);
  Serial.print(F(" * "));
  Serial.print(calOffset);
  Serial.print(F(" = "));
  Serial.print(ROTATIONDIRECTION * calOffset);
  Serial.print(F(" steps ("));
  Serial.print(ROTATIONDIRECTION * calOffset > 0 ? F("forward") : F("backward"));
  Serial.println(F(")"));
#endif
}

//doing a calibration of the revolver using the hall sensor
int calibrate(bool initialCalibration) {
#ifdef SERIAL_ENABLE
  SerialPrintTimestamp();
  Serial.println(F("calibrate revolver"));
  unsigned long calStartTime = millis();
  int lastLogStep = -1000; // Track last logged step to avoid spamming
#endif
  currentlyrotating = 2; // Status: calibrating (searching for marker)
  bool reachedMarker = false;
  stepper.setSpeed(stepperSpeed);
  int i = 0;
  while (!reachedMarker) {
    int currentHallValue = digitalRead(HALLPIN);
    
#ifdef SERIAL_ENABLE
    // Log progress every 500 steps or at key moments
    if (i - lastLogStep >= 500 || i == 0 || i == 50 || (i % 1000 == 0)) {
      unsigned long elapsed = millis() - calStartTime;
      SerialPrintTimestamp();
      Serial.print(F("Step: "));
      Serial.print(i);
      Serial.print(F(" / "));
      Serial.print(3 * STEPS);
      Serial.print(F(" | Hall: "));
      Serial.print(currentHallValue);
      Serial.print(F(" (1=no magnet, 0=magnet) | Time: "));
      Serial.print(elapsed);
      Serial.println(F("ms"));
      lastLogStep = i;
    }
    // Also log when Hall sensor changes state (important for debugging)
    static int lastHallValue = -1;
    if (currentHallValue != lastHallValue) {
      SerialPrintTimestamp();
      Serial.print(F("Hall sensor changed: "));
      Serial.print(lastHallValue);
      Serial.print(F(" -> "));
      Serial.print(currentHallValue);
      Serial.print(F(" at step "));
      Serial.print(i);
      Serial.println();
      lastHallValue = currentHallValue;
    }
#endif
    
    if (i == 0) {
      // At start of calibration - check initial Hall sensor state
      if (currentHallValue == 0) {
        // Already at marker position at startup - move away first to ensure we find it properly
        // This prevents double-applying offset if unit reboots while already homed
#ifdef SERIAL_ENABLE
        SerialPrintTimestamp();
        Serial.println(F("Already at marker at startup, moving 50 steps away to re-find it"));
#endif
        i = 50;
        stepper.step(ROTATIONDIRECTION * 50); //move 50 steps in normal rotation direction
        // Continue loop - next iteration will check Hall sensor again after moving
        i++; // Increment so we don't hit this check again
        continue; // Re-check Hall sensor after moving away
      }
      // If Hall == 1 at startup, that's normal - continue with normal search below
    }
    
    if (currentHallValue == 1) {
      //not reached yet - still searching for marker
      currentlyrotating = 2; // Keep status as "calibrating (searching)"
      stepper.step(ROTATIONDIRECTION * 1); // Normal rotation direction only
    }
    else if (currentHallValue == 0 && i > 0) {
      //reached marker LEADING EDGE (and we've moved at least 1 step, so we found it properly)
      // The i > 0 check ensures we didn't just start at the marker (prevents double offset)
      // STOP HERE IMMEDIATELY - this is the marker edge position (leading edge, start of magnet)
      // This matches the calibration tool which calculates offset from the leading edge
      // We do NOT move forward to measure trigger width - we stop immediately at the leading edge
      
#ifdef SERIAL_ENABLE
      unsigned long markerFoundTime = millis() - calStartTime;
      SerialPrintTimestamp();
      Serial.print(F("*** MARKER FOUND *** at step: "));
      Serial.print(i);
      Serial.print(F(" | Time: "));
      Serial.print(markerFoundTime);
      Serial.println(F("ms - Stopped at leading edge (start of magnet)"));
      SerialPrintTimestamp();
      Serial.println(F("(Staying at leading edge - no forward movement to measure trigger width)"));
#endif
      
      reachedMarker = true;
      currentlyrotating = 3; // Status: calibrating (applying offset)
      
#ifdef SERIAL_ENABLE
      SerialPrintTimestamp();
      Serial.print(F("Marker edge found at step: "));
      Serial.println(i);
      SerialPrintTimestamp();
      Serial.print(F("CalOffset value from EEPROM: "));
      Serial.print(calOffset);
      Serial.println(F(" steps"));
      
      if (calOffset == 0) {
        SerialPrintTimestamp();
        Serial.println(F("WARNING: CalOffset is ZERO! Offset will not be applied."));
        SerialPrintTimestamp();
        Serial.println(F("This means either:"));
        SerialPrintTimestamp();
        Serial.println(F("  1. EEPROM was never written (calibration not done)"));
        SerialPrintTimestamp();
        Serial.println(F("  2. EEPROM was cleared"));
        SerialPrintTimestamp();
        Serial.println(F("  3. EEPROM read failed"));
        SerialPrintTimestamp();
        Serial.println(F("Unit will stay at marker edge position (not at blank flap center)"));
      } else {
        SerialPrintTimestamp();
        Serial.print(F("ROTATIONDIRECTION: "));
        Serial.println(ROTATIONDIRECTION);
        SerialPrintTimestamp();
        Serial.print(F("Calculated step value: "));
        Serial.print(ROTATIONDIRECTION);
        Serial.print(F(" * "));
        Serial.print(calOffset);
        Serial.print(F(" = "));
        long stepValue = (long)ROTATIONDIRECTION * (long)calOffset;
        Serial.print(stepValue);
        Serial.println(F(" steps"));
        SerialPrintTimestamp();
        Serial.print(F("Applying offset: moving "));
        Serial.print(abs(stepValue));
        Serial.print(F(" steps "));
        Serial.print(stepValue > 0 ? F("forward") : F("backward"));
        Serial.println();
      }
#endif
      
      // Apply offset: calOffset is steps in ROTATIONDIRECTION, just like scientress version
      // Apply directly - no conversion needed, no wrap-around
      // ROTATIONDIRECTION = -1, so ROTATIONDIRECTION * calOffset moves in ROTATIONDIRECTION
      long markerStep = i; // Save marker position before offset
      long stepValue = 0;
      if (calOffset > 0) {
        stepValue = (long)ROTATIONDIRECTION * (long)calOffset;
        stepper.step(stepValue); // Normal rotation direction only
        i = i + stepValue;
      } else {
        // Offset is zero - don't move, stay at marker
        stepValue = 0;
        // i stays at marker position
      }
      // Wrap around if negative (shouldn't happen, but handle it)
      if (i < 0) {
#ifdef SERIAL_ENABLE
        SerialPrintTimestamp();
        Serial.print(F("WARNING: Step counter went negative ("));
        Serial.print(i);
        Serial.println(F("), wrapping around"));
#endif
        i = i + STEPS;
      }
      // Wrap around if beyond one rotation
      if (i >= STEPS) {
#ifdef SERIAL_ENABLE
        SerialPrintTimestamp();
        Serial.print(F("WARNING: Step counter exceeded STEPS ("));
        Serial.print(i);
        Serial.println(F("), wrapping around"));
#endif
        i = i - STEPS;
      }
      
      displayedLetter = 0;
      missedSteps = 0;
      
#ifdef SERIAL_ENABLE
      unsigned long totalTime = millis() - calStartTime;
      SerialPrintTimestamp();
      Serial.println(F("=== OFFSET APPLICATION SUMMARY ==="));
      SerialPrintTimestamp();
      Serial.print(F("Marker edge found at step: "));
      Serial.println(markerStep);
      SerialPrintTimestamp();
      Serial.print(F("Offset value: "));
      Serial.print(calOffset);
      Serial.println(F(" steps"));
      SerialPrintTimestamp();
      Serial.print(F("Step value applied: "));
      Serial.print(stepValue);
      Serial.print(F(" steps ("));
      Serial.print(stepValue > 0 ? F("forward") : F("backward"));
      Serial.println(F(")"));
      SerialPrintTimestamp();
      Serial.print(F("Final step position after offset: "));
      Serial.print(i);
      Serial.print(F(" / "));
      Serial.print(STEPS);
      Serial.println();
      SerialPrintTimestamp();
      Serial.print(F("Total calibration time: "));
      Serial.print(totalTime);
      Serial.println(F("ms"));
      SerialPrintTimestamp();
      Serial.print(F("Expected position: CENTER of blank flap (index 0, '"));
      Serial.print(letters[0]);
      Serial.println(F("')"));
      SerialPrintTimestamp();
      Serial.print(F("Calculation: marker at step "));
      Serial.print(markerStep);
      Serial.print(F(" + offset step value "));
      Serial.print(stepValue);
      Serial.print(F(" = final step "));
      Serial.print(i);
      Serial.println();
      SerialPrintTimestamp();
      Serial.println(F("=== CALIBRATION COMPLETE ==="));
#endif
      //Only stop motor for initial calibration
      if (initialCalibration) {
        stopMotor();
      } else {
        // If not initial calibration, we're still moving, so set status to busy
        currentlyrotating = 1;
      }
      return i;
    }
    if (i > 3 * STEPS) {
      //seems that there is a problem with the marker or the sensor. turn of the motor to avoid overheating.
      displayedLetter = 0;
      desiredLetter = 0;
      reachedMarker = true;
      currentlyrotating = 4; // Status: calibration error/timeout
#ifdef SERIAL_ENABLE
      unsigned long elapsed = millis() - calStartTime;
      SerialPrintTimestamp();
      Serial.print(F("*** CALIBRATION TIMEOUT *** after "));
      Serial.print(elapsed);
      Serial.print(F("ms and "));
      Serial.print(i);
      Serial.println(F(" steps (3 full rotations)"));
      SerialPrintTimestamp();
      Serial.print(F("Final Hall sensor reading: "));
      Serial.println(digitalRead(HALLPIN));
      SerialPrintTimestamp();
      Serial.println(F("calibration revolver failed"));
#endif
      stopMotor();
      return -1;
    }
    i++;
  }
  return i;
}

//switching off the motor driver
void stopMotor() {
  lastInd1 = digitalRead(STEPPERPIN1);
  lastInd2 = digitalRead(STEPPERPIN2);
  lastInd3 = digitalRead(STEPPERPIN3);
  lastInd4 = digitalRead(STEPPERPIN4);

  digitalWrite(STEPPERPIN1, LOW);
  digitalWrite(STEPPERPIN2, LOW);
  digitalWrite(STEPPERPIN3, LOW);
  digitalWrite(STEPPERPIN4, LOW);
#ifdef SERIAL_ENABLE
  SerialPrintTimestamp();
  Serial.println(F("Motor Stop"));
#endif
  currentlyrotating = 0; //set active state to not active
  delay(100);
}

void startMotor() {
#ifdef SERIAL_ENABLE
  SerialPrintTimestamp();
  Serial.println(F("Motor Start"));
#endif
  currentlyrotating = 1; //set active state to active
  digitalWrite(STEPPERPIN1, lastInd1);
  digitalWrite(STEPPERPIN2, lastInd2);
  digitalWrite(STEPPERPIN3, lastInd3);
  digitalWrite(STEPPERPIN4, lastInd4);
}


void writeToEEPROM(uint16_t offsetValue) {
  //One simple call, with the address first and the object second.
  EEPROM.put(eeAddress, offsetValue);

#ifdef SERIAL_ENABLE
  SerialPrintTimestamp();
  Serial.print(F("Value written to EEPROM: "));
  Serial.print(calOffset);
  Serial.println();
#endif
}

#ifdef SERIAL_ENABLE
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
    }
    else {
      receivedChars[ndx] = '\0'; // terminate the string
      ndx = 0;
      newData = true;
    }
  }

  if (newData == true) {
    newData = false;
    if (receivedChars[0] != 0x00 && receivedChars[1] == 0x00) {
      auto new_letter = translateLettertoInt(receivedChars[0]);
      if (new_letter == -1) {
        SerialPrintTimestamp();
        Serial.println(F("Letter not found in index"));
      } else {
        stepperSpeed = 12;
        receivedNumber = new_letter;
      }
    } else if (strncmp(receivedChars, "test", numChars) == 0) {
      SerialPrintTimestamp();
      Serial.println(F("Starting calibration test"));
      calibrate(true);
      run_test();
    } else {
      // convert to int
      calOffset = String(receivedChars).toInt();
      // check if the integer conversion failed
      if (calOffset == 0 && receivedChars[0] != '0') {
        SerialPrintTimestamp();
        Serial.println(F("Invalid Command"));
      } else {
        // save to eeprom
        writeToEEPROM(calOffset);
        calibrate(true);
      }
    }
  }
}
#endif
