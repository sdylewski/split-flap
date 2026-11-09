// I2C Diagnostic Testing - Optional comprehensive I2C bus and unit communication tests
// Enable with #define I2C_DIAGNOSTIC_ENABLE true in ESPMaster.ino for startup testing
// Can also be run on-demand via /i2c-diagnostics endpoint (works even if flag is false)
// This file is always included but only runs when explicitly called

// Run comprehensive I2C diagnostics for a specific unit or all units
// unitAddress: -1 = test all units, 0-15 = test specific unit
void runI2CDiagnostics(int unitAddress) {
  SerialPrintln("");
  SerialPrintln("=== I2C Diagnostic Testing ===");
  
  int unitsToTest[16];
  int unitCount = 0;
  
  if (unitAddress >= 0 && unitAddress < 16) {
    // Test specific unit
    unitsToTest[0] = unitAddress;
    unitCount = 1;
    SerialPrint("Testing unit ");
    SerialPrintln(unitAddress);
  } else {
    // Test all expected units
    for (int i = 0; i < UNITS_AMOUNT; i++) {
      unitsToTest[unitCount] = i;
      unitCount++;
    }
    SerialPrint("Testing all ");
    SerialPrint(UNITS_AMOUNT);
    SerialPrintln(" units");
  }
  
  SerialPrintln("");
  
  const int TEST_ITERATIONS = 10; // Number of read/write tests per unit
  int totalTests = 0;
  int passedTests = 0;
  int failedTests = 0;
  
  for (int u = 0; u < unitCount; u++) {
    int address = unitsToTest[u];
    
    SerialPrint("--- Unit ");
    SerialPrint(address);
    SerialPrintln(" ---");
    
    // Test 1: Address detection
    SerialPrint("  Address detection: ");
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    delay(2);
    
    if (error == 0) {
      SerialPrintln("✓ PASS");
      passedTests++;
    } else {
      SerialPrint("✗ FAIL (error code: ");
      SerialPrint(error);
      SerialPrintln(")");
      failedTests++;
    }
    totalTests++;
    yield();
    
    if (error != 0) {
      // Skip remaining tests if address not found
      SerialPrintln("  Skipping remaining tests (address not responding)");
      continue;
    }
    
    // Test 2: Read status (multiple times)
    SerialPrint("  Status read test (");
    SerialPrint(TEST_ITERATIONS);
    SerialPrint(" reads): ");
    int successfulReads = 0;
    unsigned long readStartTime = millis();
    
    for (int i = 0; i < TEST_ITERATIONS; i++) {
      Wire.requestFrom(address, 1, 1);
      delay(2);
      if (Wire.available() > 0) {
        int status = Wire.read();
        successfulReads++;
      }
      yield();
      delay(5); // Small delay between reads
    }
    
    unsigned long readTime = millis() - readStartTime;
    float readsPerSecond = (successfulReads * 1000.0) / readTime;
    
    if (successfulReads == TEST_ITERATIONS) {
      SerialPrint("✓ PASS (");
      SerialPrint(successfulReads);
      SerialPrint("/");
      SerialPrint(TEST_ITERATIONS);
      SerialPrint(" successful, ");
      SerialPrint(String(readsPerSecond, 1));
      SerialPrintln(" reads/sec)");
      passedTests++;
    } else {
      SerialPrint("✗ FAIL (");
      SerialPrint(successfulReads);
      SerialPrint("/");
      SerialPrint(TEST_ITERATIONS);
      SerialPrintln(" successful)");
      failedTests++;
    }
    totalTests++;
    
    // Test 3: Write test (send command)
    SerialPrint("  Write test (");
    SerialPrint(TEST_ITERATIONS);
    SerialPrint(" writes): ");
    int successfulWrites = 0;
    byte lastWriteError = 0;
    unsigned long writeStartTime = millis();
    
    for (int i = 0; i < TEST_ITERATIONS; i++) {
      Wire.beginTransmission(address);
      Wire.write(0); // Send space character
      Wire.write(10); // Send speed 10
      byte writeError = Wire.endTransmission();
      delay(2);
      
      if (writeError == 0) {
        successfulWrites++;
      } else {
        lastWriteError = writeError; // Store last error for reporting
      }
      yield();
      delay(5); // Small delay between writes
    }
    
    unsigned long writeTime = millis() - writeStartTime;
    float writesPerSecond = (successfulWrites * 1000.0) / writeTime;
    
    if (successfulWrites == TEST_ITERATIONS) {
      SerialPrint("✓ PASS (");
      SerialPrint(successfulWrites);
      SerialPrint("/");
      SerialPrint(TEST_ITERATIONS);
      SerialPrint(" successful, ");
      SerialPrint(String(writesPerSecond, 1));
      SerialPrintln(" writes/sec)");
      passedTests++;
    } else {
      SerialPrint("✗ FAIL (");
      SerialPrint(successfulWrites);
      SerialPrint("/");
      SerialPrint(TEST_ITERATIONS);
      SerialPrint(" successful, last error code: ");
      SerialPrintln(lastWriteError);
      failedTests++;
    }
    totalTests++;
    
    // Test 4: Error rate check (rapid read/write)
    SerialPrint("  Error rate test (50 operations): ");
    int errorCount = 0;
    const int RAPID_TESTS = 50;
    
    for (int i = 0; i < RAPID_TESTS; i++) {
      // Alternate between read and write
      if (i % 2 == 0) {
        // Write
        Wire.beginTransmission(address);
        Wire.write(0);
        Wire.write(10);
        byte err = Wire.endTransmission();
        delay(1);
        if (err != 0) errorCount++;
      } else {
        // Read
        Wire.requestFrom(address, 1, 1);
        delay(1);
        if (Wire.available() == 0) errorCount++;
        else Wire.read();
      }
      yield();
    }
    
    float errorRate = (errorCount * 100.0) / RAPID_TESTS;
    
    if (errorCount == 0) {
      SerialPrint("✓ PASS (0% error rate)");
      passedTests++;
    } else {
      SerialPrint("✗ FAIL (");
      SerialPrint(String(errorRate, 1));
      SerialPrint("% error rate, ");
      SerialPrint(errorCount);
      SerialPrint("/");
      SerialPrint(RAPID_TESTS);
      SerialPrintln(" errors)");
      failedTests++;
    }
    totalTests++;
    
    SerialPrintln("");
    yield();
  }
  
  // Summary
  SerialPrintln("=== I2C Diagnostic Summary ===");
  SerialPrint("Total tests: ");
  SerialPrintln(totalTests);
  SerialPrint("Passed: ");
  SerialPrint(passedTests);
  SerialPrint(" (");
  SerialPrint(String((float)passedTests / totalTests * 100.0, 1));
  SerialPrintln("%)");
  SerialPrint("Failed: ");
  SerialPrint(failedTests);
  SerialPrint(" (");
  SerialPrint(String((float)failedTests / totalTests * 100.0, 1));
  SerialPrintln("%)");
  
  if (failedTests == 0) {
    SerialPrintln("✓ All I2C diagnostic tests PASSED");
  } else {
    SerialPrintln("⚠ Some I2C diagnostic tests FAILED");
  }
  
  SerialPrintln("");
}

