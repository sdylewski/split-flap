// I2C Setup Scan - Comprehensive bus scan and test for all possible addresses
// Scans all 16 possible I2C addresses (0-15) and tests read/write operations
// Useful for:
//   - Testing individual units before connecting them all
//   - Finding units with misconfigured address switches
//   - Verifying I2C bus health
// Enable with #define I2C_SETUP_SCAN_ENABLE true in ESPMaster.ino for startup scanning
// Can also be run on-demand via /i2c-setup-scan endpoint
// 
// !!!!! USE Command Shift P to load little FS 
// 
// Structure to store unit test results
struct UnitTestResult {
  bool found;
  bool readWorks;
  bool writeWorks;
  int statusCode;
  int readErrors;
  int writeErrors;
};

// Run comprehensive I2C setup scan - tests all 16 possible addresses
void runI2CSetupScan() {
  SerialPrintln("");
  SerialPrintln("========================================");
  SerialPrintln("I2C Setup Scan - Testing All Addresses");
  SerialPrintln("========================================");
  SerialPrintln("Scanning all 16 possible I2C addresses (0-15)...");
  SerialPrintln("This will test both read and write operations for each found device.");
  SerialPrintln("");
  
  UnitTestResult results[16];
  
  // Initialize results
  for (int i = 0; i < 16; i++) {
    results[i].found = false;
    results[i].readWorks = false;
    results[i].writeWorks = false;
    results[i].statusCode = -2; // -2 = not found
    results[i].readErrors = 0;
    results[i].writeErrors = 0;
  }
  
  // Step 1: Scan all addresses to find devices
  SerialPrintln("--- Step 1: Address Detection ---");
  int foundCount = 0;
  
  for (int address = 0; address < 16; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    delay(2); // Allow bus to settle
    
    if (error == 0) {
      results[address].found = true;
      foundCount++;
      SerialPrint("  ✓ Address ");
      SerialPrint(address);
      SerialPrintln(": Device found");
    } else if (error == 2) {
      // NACK - no device (normal for unused addresses)
    } else {
      // Other error
      SerialPrint("  ⚠ Address ");
      SerialPrint(address);
      SerialPrint(": I2C error code ");
      SerialPrintln(error);
    }
    
    yield();
    delay(5);
  }
  
  SerialPrintln("");
  SerialPrint("Found ");
  SerialPrint(foundCount);
  SerialPrintln(" device(s) on I2C bus");
  SerialPrintln("");
  
  if (foundCount == 0) {
    SerialPrintln("⚠ No devices found on I2C bus!");
    SerialPrintln("  - Check I2C wiring (SDA, SCL)");
    SerialPrintln("  - Verify power to units");
    SerialPrintln("  - Check pull-up resistors");
    SerialPrintln("");
    return;
  }
  
  // Step 2: Test read operations for each found device
  SerialPrintln("--- Step 2: Read Operation Tests ---");
  const int READ_TEST_COUNT = 5;
  
  for (int address = 0; address < 16; address++) {
    if (!results[address].found) continue;
    
    SerialPrint("  Testing address ");
    SerialPrint(address);
    SerialPrint(": ");
    
    int successfulReads = 0;
    for (int i = 0; i < READ_TEST_COUNT; i++) {
      Wire.requestFrom(address, 1, 1);
      delay(2);
      
      if (Wire.available() > 0) {
        int status = Wire.read();
        successfulReads++;
        if (i == 0) {
          results[address].statusCode = status; // Store first status
        }
      } else {
        results[address].readErrors++;
      }
      
      yield();
      delay(5);
    }
    
    if (successfulReads == READ_TEST_COUNT) {
      results[address].readWorks = true;
      SerialPrint("✓ READ OK");
      if (results[address].statusCode >= 0) {
        SerialPrint(" (Status: ");
        SerialPrint(results[address].statusCode);
        SerialPrint(")");
      }
      SerialPrintln("");
    } else {
      SerialPrint("✗ READ FAILED (");
      SerialPrint(successfulReads);
      SerialPrint("/");
      SerialPrint(READ_TEST_COUNT);
      SerialPrintln(" successful)");
    }
    
    yield();
    delay(10);
  }
  
  SerialPrintln("");
  
  // Step 3: Test write operations for each found device
  SerialPrintln("--- Step 3: Write Operation Tests ---");
  const int WRITE_TEST_COUNT = 5;
  
  for (int address = 0; address < 16; address++) {
    if (!results[address].found) continue;
    
    SerialPrint("  Testing address ");
    SerialPrint(address);
    SerialPrint(": ");
    
    int successfulWrites = 0;
    for (int i = 0; i < WRITE_TEST_COUNT; i++) {
      Wire.beginTransmission(address);
      Wire.write(0); // Send space character (index 0)
      Wire.write(10); // Send speed 10
      byte error = Wire.endTransmission();
      delay(2);
      
      if (error == 0) {
        successfulWrites++;
      } else {
        results[address].writeErrors++;
      }
      
      yield();
      delay(5);
    }
    
    if (successfulWrites == WRITE_TEST_COUNT) {
      results[address].writeWorks = true;
      SerialPrintln("✓ WRITE OK");
    } else {
      SerialPrint("✗ WRITE FAILED (");
      SerialPrint(successfulWrites);
      SerialPrint("/");
      SerialPrint(WRITE_TEST_COUNT);
      SerialPrintln(" successful)");
    }
    
    yield();
    delay(10);
  }
  
  SerialPrintln("");
  
  // Step 4: Summary Report
  SerialPrintln("========================================");
  SerialPrintln("I2C Setup Scan Summary");
  SerialPrintln("========================================");
  SerialPrintln("");
  
  int fullyWorking = 0;
  int readOnly = 0;
  int writeOnly = 0;
  int notWorking = 0;
  
  SerialPrintln("Address | Found | Read | Write | Status | Result");
  SerialPrintln("--------|-------|------|-------|--------|--------");
  
  for (int address = 0; address < 16; address++) {
    if (!results[address].found) continue;
    
    SerialPrint("   ");
    if (address < 10) SerialPrint(" ");
    SerialPrint(address);
    SerialPrint("   |   ✓   | ");
    
    if (results[address].readWorks) {
      SerialPrint(" ✓  | ");
      readOnly++; // Counted here, will be adjusted below
    } else {
      SerialPrint(" ✗  | ");
    }
    
    if (results[address].writeWorks) {
      SerialPrint(" ✓  | ");
      writeOnly++; // Counted here, will be adjusted below
    } else {
      SerialPrint(" ✗  | ");
    }
    
    // Status code
    if (results[address].statusCode == -2) {
      SerialPrint("  -  | ");
    } else {
      SerialPrint("  ");
      SerialPrint(results[address].statusCode);
      SerialPrint("  | ");
    }
    
    // Result
    if (results[address].readWorks && results[address].writeWorks) {
      SerialPrintln("✓ FULLY WORKING");
      fullyWorking++;
      readOnly--; // Adjust counts
      writeOnly--;
    } else if (results[address].readWorks) {
      SerialPrintln("⚠ READ ONLY");
    } else if (results[address].writeWorks) {
      SerialPrintln("⚠ WRITE ONLY");
    } else {
      SerialPrintln("✗ NOT WORKING");
      notWorking++;
      readOnly--; // Adjust counts
      writeOnly--;
    }
  }
  
  SerialPrintln("");
  SerialPrintln("--- Statistics ---");
  SerialPrint("Total devices found: ");
  SerialPrintln(foundCount);
  SerialPrint("Fully working (read + write): ");
  SerialPrintln(fullyWorking);
  SerialPrint("Read only: ");
  SerialPrintln(readOnly);
  SerialPrint("Write only: ");
  SerialPrintln(writeOnly);
  SerialPrint("Not working: ");
  SerialPrintln(notWorking);
  SerialPrintln("");
  
  // Check against expected units
  SerialPrintln("--- Expected Units Check ---");
  SerialPrint("Expected units (0 to ");
  SerialPrint(UNITS_AMOUNT - 1);
  SerialPrintln("):");
  
  int expectedFound = 0;
  int unexpectedFound = 0;
  
  for (int i = 0; i < UNITS_AMOUNT; i++) {
    if (results[i].found) {
      expectedFound++;
      if (results[i].readWorks && results[i].writeWorks) {
        SerialPrint("  ✓ Unit ");
        SerialPrint(i);
        SerialPrintln(": Found and working");
      } else {
        SerialPrint("  ⚠ Unit ");
        SerialPrint(i);
        SerialPrintln(": Found but not fully working");
      }
    } else {
      SerialPrint("  ✗ Unit ");
      SerialPrint(i);
      SerialPrintln(": NOT FOUND");
    }
  }
  
  // Check for unexpected addresses
  for (int i = UNITS_AMOUNT; i < 16; i++) {
    if (results[i].found) {
      unexpectedFound++;
      SerialPrint("  ⚠ Unexpected device at address ");
      SerialPrint(i);
      SerialPrintln(" (outside expected range)");
    }
  }
  
  SerialPrintln("");
  SerialPrint("Expected units found: ");
  SerialPrint(expectedFound);
  SerialPrint("/");
  SerialPrintln(UNITS_AMOUNT);
  
  if (unexpectedFound > 0) {
    SerialPrint("Unexpected devices found: ");
    SerialPrintln(unexpectedFound);
    SerialPrintln("  → Check address switch configuration on these units");
  }
  
  SerialPrintln("");
  SerialPrintln("========================================");
  SerialPrintln("Setup scan complete!");
  SerialPrintln("========================================");
  SerialPrintln("");
}

