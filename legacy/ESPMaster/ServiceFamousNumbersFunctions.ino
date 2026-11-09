//Famous Numbers Mode - Displays famous scientific constants and numbers
void checkFamousNumbers() {
  if (deviceMode == DEVICE_MODE_FAMOUS_NUMBERS) {
    static unsigned long lastNumberChange = 0;
    static unsigned long currentDelayMillis = 0;
    static unsigned long nameDisplayTime = 0;
    static int currentNumberIndex = -1;
    static bool showingName = false;
    
    // Default list of famous numbers with their names
    struct FamousNumber {
      String number;
      String name;
    };
    
    const int MAX_NUMBERS = 50;
    FamousNumber famousNumbers[MAX_NUMBERS];
    int numberCount = 0;
    
    // Parse famous numbers list if provided (format: "number1,name1,number2,name2,...")
    if (famousNumbersList.length() > 0) {
      String remaining = famousNumbersList;
      remaining.trim();
      
      int commaPos = 0;
      bool expectingNumber = true;
      String currentNumber = "";
      
      while (commaPos >= 0 && numberCount < MAX_NUMBERS) {
        commaPos = remaining.indexOf(',');
        if (commaPos >= 0) {
          String part = remaining.substring(0, commaPos);
          part.trim();
          remaining = remaining.substring(commaPos + 1);
          remaining.trim();
          
          if (expectingNumber) {
            currentNumber = part;
            expectingNumber = false;
          } else {
            // This is the name
            if (currentNumber.length() > 0 && part.length() > 0) {
              famousNumbers[numberCount].number = currentNumber;
              famousNumbers[numberCount].name = part;
              numberCount++;
            }
            expectingNumber = true;
            currentNumber = "";
          }
        } else {
          // Last part
          String part = remaining;
          part.trim();
          if (part.length() > 0) {
            if (expectingNumber) {
              // Last item is a number without a name - use number as name
              famousNumbers[numberCount].number = part;
              famousNumbers[numberCount].name = part;
              numberCount++;
            } else {
              // Last item is a name
              if (currentNumber.length() > 0) {
                famousNumbers[numberCount].number = currentNumber;
                famousNumbers[numberCount].name = part;
                numberCount++;
              }
            }
          }
          break;
        }
      }
    }
    
    // If no editable list or parsing failed, use defaults
    if (numberCount == 0) {
      const char* defaultNumbers[] = {"3.14159", "PI", "2.71828", "EULER E", "1.41421", "SQRT 2", "1.73205", "SQRT 3",
        "6.022 E 23", "AVOGADRO", "6.626 E-34", "PLANCK H", "1.055 E-34", "PLANCK HB", "9.80665", "GRAVITY G",
        "2.9979 E 8", "LIGHT C", "1.602 E-19", "CHARGE E", "9.109 E-31", "ELECTRON", "1.673 E-27", "PROTON",
        "8.314 J/MOL", "GAS CONST", "1.380 E-23", "BOLTZMANN", "6.674 E-11", "GRAV CONST", "1.61803", "GOLDEN",
        "0.57721", "EULER GAM", "2.68545", "KHINCHIN"};
      numberCount = sizeof(defaultNumbers) / sizeof(defaultNumbers[0]) / 2; // Divide by 2 since each entry has 2 parts
      for (int i = 0; i < numberCount; i++) {
        famousNumbers[i].number = String(defaultNumbers[i * 2]);
        famousNumbers[i].name = String(defaultNumbers[i * 2 + 1]);
      }
    }
    
    // Get min and max delays
    unsigned long minDelaySeconds = atol(famousNumbersMinDelaySeconds.c_str());
    unsigned long maxDelaySeconds = atol(famousNumbersMaxDelaySeconds.c_str());
    
    // Validate and constrain delays
    if (minDelaySeconds < 5) minDelaySeconds = 5;
    if (minDelaySeconds > 3600) minDelaySeconds = 3600;
    if (maxDelaySeconds < 5) maxDelaySeconds = 5;
    if (maxDelaySeconds > 3600) maxDelaySeconds = 3600;
    if (maxDelaySeconds < minDelaySeconds) maxDelaySeconds = minDelaySeconds;
    
    unsigned long minDelayMillis = minDelaySeconds * 1000;
    unsigned long maxDelayMillis = maxDelaySeconds * 1000;
    
    // Get name display delay
    unsigned long nameDelaySeconds = atol(famousNumbersNameDelaySeconds.c_str());
    if (nameDelaySeconds < 1) nameDelaySeconds = 1;
    if (nameDelaySeconds > 60) nameDelaySeconds = 60;
    unsigned long nameDelayMillis = nameDelaySeconds * 1000;
    
    unsigned long currentTime = millis();
    
    // Initialize delay on first run
    if (currentDelayMillis == 0) {
      // Random delay between min and max
      if (maxDelayMillis > minDelayMillis) {
        currentDelayMillis = minDelayMillis + random(0, maxDelayMillis - minDelayMillis + 1);
      } else {
        currentDelayMillis = minDelayMillis;
      }
      lastNumberChange = currentTime;
      showingName = false;
    }
    
    // Check if we should show the name (after showing number for nameDelayMillis)
    if (!showingName && nameDisplayTime > 0 && currentNumberIndex >= 0 && (currentTime - nameDisplayTime >= nameDelayMillis)) {
      // Time to show the name
      String name = famousNumbers[currentNumberIndex].name;
      if (inputText != name) {
        SerialPrintln("Famous Numbers Mode: Showing name: " + name);
        inputText = name;
        showingName = true;
      }
    }
    
    // Check if it's time to change to a new number
    if (currentTime - lastNumberChange >= currentDelayMillis) {
      // Select a random number (different from current if possible)
      int newNumberIndex;
      if (numberCount > 1) {
        do {
          newNumberIndex = random(0, numberCount);
        } while (newNumberIndex == currentNumberIndex);
      } else {
        newNumberIndex = 0;
      }
      
      currentNumberIndex = newNumberIndex;
      String newNumber = famousNumbers[currentNumberIndex].number;
      
      // Show the number first
      if (inputText != newNumber) {
        SerialPrintln("Famous Numbers Mode: Changing to " + newNumber);
        inputText = newNumber;
        lastNumberChange = currentTime;
        nameDisplayTime = currentTime; // Start timer for name display
        showingName = false;
        
        // Set new random delay for next change
        if (maxDelayMillis > minDelayMillis) {
          currentDelayMillis = minDelayMillis + random(0, maxDelayMillis - minDelayMillis + 1);
        } else {
          currentDelayMillis = minDelayMillis;
        }
      }
    }
  }
}
