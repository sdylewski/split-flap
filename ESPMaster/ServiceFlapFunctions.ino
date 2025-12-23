//Shows a new message on the display
void showText(String message) {  
  showText(message, 0);
}

void showText(String message, int delayMillis) {  
  if (lastWrittenText != message || alignmentUpdated) { 
    String messageDisplay = message == "" ? "<Blank>" : message;
    String alignmentUpdatedDisplay = alignmentUpdated ? "Yes" : "No";

    SerialPrintln("Showing new Message");
    SerialPrintln("New Message: " + messageDisplay);
    SerialPrintln("Alignment Updated: " + alignmentUpdatedDisplay);
  
    LList<String> messageLines = processSentenceToLines(message);

    if (messageLines.size() > 1) {
      SerialPrintln("Showing a split down message");
    
      //Iterate over all the message lines we've got
      for (int linesIndex = 0; linesIndex < messageLines.size(); linesIndex++) {
        String line = messageLines[linesIndex];

        SerialPrint("-- Message Line: ");
        SerialPrintln(line);

        showMessage(line, convertSpeed(flapSpeed));
    
        //If the lines index isn't the last, wait for motors to fully stop, then delay before showing next word
        // Use non-blocking delay with yield to allow web server to process
        if (linesIndex < messageLines.size() - 1) {  // Only delay if not the last line
          // First, ensure all motors have stopped moving
          SerialPrintln("Waiting for all motors to stop before delay...");
          unsigned long waitStart = millis();
          unsigned long waitTimeout = 30000; // 30 second timeout
          while (isDisplayMoving() && (millis() - waitStart < waitTimeout)) {
            yield(); // Allow web server to process requests
            delay(100);
          }
          
          if (millis() - waitStart >= waitTimeout) {
            SerialPrintln("WARNING: Motor stop wait timeout - continuing anyway");
          } else {
            SerialPrintln("All motors stopped. Starting delay before next word...");
          }
          
          // Now delay to give time to read the word
          unsigned long delayStart = millis();
          while (millis() - delayStart < 5000) {  // 5 second delay for better readability
            yield(); // Allow web server to process requests
            delay(100);
          }
        }
      }  
    }
    else {
      SerialPrintln("Showing a simple message: " + message);
      
      showMessage(message, convertSpeed(flapSpeed));
    }  

    //If the device wasn't previously in text mode, delay for a short time so can read!
    // Use non-blocking delay with yield
    if (delayMillis != 0) {
      SerialPrintln("Pausing for a small duration. Delay: " + String(delayMillis));
      unsigned long delayStart = millis();
      while (millis() - delayStart < delayMillis) {
        yield(); // Allow web server to process requests
        delay(100);
      }
    }

    //Save what we last did
    lastWrittenText = message;

    //Alignment definitely has not changed now
    alignmentUpdated = false;
    
    SerialPrintln("Done showing message");
  }
}

//Pushes message to units
void showMessage(String message, int flapSpeed) {
  //Format string per alignment choice
  if (alignment == ALIGNMENT_MODE_LEFT) {
    message = leftString(message);
  } 
  else if (alignment == ALIGNMENT_MODE_RIGHT) {
    message = rightString(message);
  } 
  else if (alignment == ALIGNMENT_MODE_CENTER) {
    message = centerString(message);
  }

  SerialPrint("Showing Aligned Message: \"");
  SerialPrint(message);
  SerialPrintln("\"");

#if UNIT_CALLS_DISABLE == true
  SerialPrintln("Unit Calls are disabled for debugging. Will delay to simulate calls...");
  unsigned long delayStart = millis();
  while (millis() - delayStart < 2000) {
    yield();
    delay(100);
  }
#else
  //Wait while display is still moving - ADD YIELD TO PREVENT HANGING
  SerialPrintln("Unit calls are enabled. Will display message");
  unsigned long waitStart = millis();
  unsigned long waitTimeout = 5000; // Reduced to 5 seconds - don't wait too long
  int checkCount = 0;
  while (isDisplayMoving() && (millis() - waitStart < waitTimeout)) {
    checkCount++;
    if (checkCount % 2 == 0) { // Only log every other check to reduce noise
      SerialPrintln("Waiting for display to stop");
    }
    yield(); // CRITICAL: Allow web server to process requests
    delay(200); // Reduced from 500ms to 200ms for faster checking
  }
  
  if (millis() - waitStart >= waitTimeout) {
    SerialPrintln("WARNING: Display wait timeout - continuing anyway");
  }

  for (int unitIndex = 0; unitIndex < UNITS_AMOUNT; unitIndex++) {
    char currentLetter = message[unitIndex];
    int currentLetterPosition = translateLettertoInt(currentLetter);
    
    SerialPrint("Unit Nr.: ");
    SerialPrint(unitIndex);
    SerialPrint(" Letter: ");
    SerialPrint(message[unitIndex]);
    SerialPrint(" Letter position: ");
    SerialPrintln(currentLetterPosition);

    //only write to unit if char exists in letter array
    if (currentLetterPosition != -1) {
      writeToUnit(unitIndex, currentLetterPosition, flapSpeed);
    }
    
    // Small yield between units to keep web server responsive
    yield();
  }

  //Wait for the display to stop moving before exit - ADD YIELD TO PREVENT HANGING
  waitStart = millis();
  waitTimeout = 5000; // Reduced to 5 seconds
  checkCount = 0;
  while (isDisplayMoving() && (millis() - waitStart < waitTimeout)) {
    checkCount++;
    if (checkCount % 5 == 0) { // Only log every 5th check to reduce noise
      SerialPrintln("Waiting for display to stop now message is display");
    }
    yield(); // CRITICAL: Allow web server to process requests
    delay(200); // Reduced from 100ms to 200ms (check less frequently)
  }
  
  if (millis() - waitStart >= waitTimeout) {
    SerialPrintln("WARNING: Display wait timeout - continuing anyway");
  }
#endif
}

//Translates char to letter position
int translateLettertoInt(char letterchar) {
  for (int flapIndex = 0; flapIndex < FLAP_AMOUNT; flapIndex++) {
    if (letterchar == letters[flapIndex]) {
      return flapIndex;
    }
  }

  return -1;
}

//Write letter position and speed in rpm to single unit
void writeToUnit(int address, int letter, int flapSpeed) {
  int sendArray[2] = {letter, flapSpeed}; //Array with values to send to unit

  Wire.beginTransmission(address);

  //Write values to send to slave in buffer
  for (unsigned int index = 0; index < sizeof sendArray / sizeof sendArray[0]; index++) {
    SerialPrint("sendArray: ");
    SerialPrintln(sendArray[index]);

    Wire.write(sendArray[index]);
  }
  byte error = Wire.endTransmission(); //send values to unit
  
  // Add delay to allow bus to settle and unit to process command
  if (error == 0) {
    delay(2); // 2ms delay after successful transmission
  } else {
    delay(5); // Longer delay on error to let bus recover
  }
  
  yield(); // Allow web server to process
}

//Checks if unit in display is currently moving
bool isDisplayMoving() {
  //Request all units moving state and write to array
  for (int unitIndex = 0; unitIndex < UNITS_AMOUNT; unitIndex++) {
    displayState[unitIndex] = checkIfMoving(unitIndex);
    
    // Add delay between I2C requests to prevent bus congestion
    // Only delay if not the last unit (optimization)
    if (unitIndex < UNITS_AMOUNT - 1) {
      delay(2); // Reduced from 5ms to 2ms for faster checking
      yield(); // Allow web server to process
    }
    
    // Only return true if unit is actually moving (status == 1)
    // Don't treat sleeping/not responding units as "moving" - they're stopped
    if (displayState[unitIndex] == 1) {
      SerialPrint("Unit ");
      SerialPrint(unitIndex);
      SerialPrintln(" is busy (moving)");
      return true;
    } 
    // If unit is sleeping/not responding (-1), treat as stopped (not moving)
    // This prevents waiting forever when a unit is offline
    else if (displayState[unitIndex] == -1) {
      SerialPrint("Unit ");
      SerialPrint(unitIndex);
      SerialPrintln(" is sleeping/not responding (treating as stopped)");
      // Continue checking other units - don't return true
    }
  }

  SerialPrintln("Display is standing still");
  return false;
}

//Checks if single unit is moving
int checkIfMoving(int address) {
  const int MAX_RETRIES = 3;
  int retryCount = 0;
  int active = -1;
  
  while (retryCount < MAX_RETRIES) {
    Wire.requestFrom(address, ANSWER_SIZE, 1);
    
    if (Wire.available() > 0) {
      active = Wire.read();
      break; // Success, exit retry loop
    }
    
    // No response - check I2C error
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
      // Device responded but no data - might be waking from sleep
      delay(10); // Wait for unit to wake up
      retryCount++;
      continue;
    }
    
    // Log error only on last retry to reduce noise
    if (retryCount == MAX_RETRIES - 1) {
      SerialPrint("ERROR: Unit ");
      SerialPrint(address);
      SerialPrintln(" - No response after retries");
      SerialPrint("  I2C error code: ");
      SerialPrintln(error);
      
      if (error == 2) {
        SerialPrintln("  -> Address received NACK (device not found or sleeping)");
      } else if (error == 3) {
        SerialPrintln("  -> Data received NACK");
      } else if (error == 4) {
        SerialPrintln("  -> Unknown I2C error");
      } else if (error == 5) {
        SerialPrintln("  -> Timeout");
      }
    }
    
    retryCount++;
    if (retryCount < MAX_RETRIES) {
      delay(10); // Wait before retry
      yield(); // Allow web server to process
    }
  }
  
  if (active == -1) {
    return -1; // Failed after retries
  }
  
  // More detailed logging for debugging calibration issues
  static unsigned long lastStatusTime[16] = {0};
  static int lastStatus[16] = {-2};
  static unsigned long statusStartTime[16] = {0};
  static int readCount[16] = {0}; // Track how many times we've successfully read from each unit
  
  unsigned long currentTime = millis();
  readCount[address]++; // Increment successful read counter
  
  // Track how long unit has been in current state
  if (lastStatus[address] != active) {
    // Status changed
    if (lastStatus[address] == 1 && active == 0) {
      // Unit just finished moving
      unsigned long duration = currentTime - statusStartTime[address];
      SerialPrint("Unit ");
      SerialPrint(address);
      SerialPrint(" finished moving after ");
      SerialPrint(duration / 1000);
      SerialPrintln(" seconds");
    } else if (active == 1) {
      // Unit just started moving
      statusStartTime[address] = currentTime;
      SerialPrint("Unit ");
      SerialPrint(address);
      SerialPrintln(" started moving (status = 1)");
    }
    lastStatus[address] = active;
    lastStatusTime[address] = currentTime;
  } else if (active == 1) {
    // Unit still moving - log periodically
    unsigned long duration = currentTime - statusStartTime[address];
    if (duration > 5000 && (currentTime - lastStatusTime[address] > 5000)) {
      // Log every 5 seconds if unit has been moving for more than 5 seconds
      SerialPrint("Unit ");
      SerialPrint(address);
      SerialPrint(" still moving (status = 1) for ");
      SerialPrint(duration / 1000);
      SerialPrintln(" seconds");
      lastStatusTime[address] = currentTime;
    }
  }
  
  SerialPrint("Unit ");
  SerialPrint(address);
  SerialPrint(" status: ");
  SerialPrint(active);
  SerialPrint(" (");
  if (active == 0) {
    SerialPrint("READY");
  } else if (active == 1) {
    SerialPrint("BUSY/MOVING");
  } else if (active == 2) {
    SerialPrint("CALIBRATING (searching for marker)");
  } else if (active == 3) {
    SerialPrint("CALIBRATING (applying offset)");
  } else if (active == 4) {
    SerialPrint("CALIBRATION ERROR/TIMEOUT");
  } else if (active == -1) {
    SerialPrint("SLEEPING");
  } else {
    SerialPrint("UNKNOWN");
  }
  SerialPrint(")");
  
  // Add I2C diagnostic: show read count to verify we're reading fresh values
  // Only show this periodically to avoid log spam
  static unsigned long lastDiagnosticTime[16] = {0};
  if (currentTime - lastDiagnosticTime[address] > 10000) { // Every 10 seconds
    SerialPrint(" [I2C reads: ");
    SerialPrint(readCount[address]);
    SerialPrint("]");
    lastDiagnosticTime[address] = currentTime;
  }
  SerialPrintln();

  if (active == -1) {
    SerialPrint("WARNING: Unit ");
    SerialPrint(address);
    SerialPrintln(" returned -1 (sleeping), attempting wake-up...");
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    if (error != 0) {
      SerialPrint("  Wake-up failed, I2C error: ");
      SerialPrintln(error);
    }
  }
  
  return active;
}

