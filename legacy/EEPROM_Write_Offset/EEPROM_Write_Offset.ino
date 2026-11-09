//Code to write calibration offset into arduino EEPROM
#include <EEPROM.h>

int eeAddress = 0;   //Location we want the data to be put.
int calOffsetGet;  //Variable already in EEPROM.

//Serial input stuff
const byte numChars = 32;
char receivedChars[numChars]; // an array to store the received data
boolean newData = false;

void setup() {
  Serial.begin(9600);
  Serial.println("init");
  getData();
}

void loop() {
  recvWithEndMarker();
  showNewData();
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
    }
    else {
      receivedChars[ndx] = '\0'; // terminate the string
      ndx = 0;
      newData = true;
    }
  }
}

void showNewData() {
  if (newData == true) {
    Serial.print("This just in ... ");
    Serial.println(receivedChars);
    int calOffsetSet = atoi(receivedChars);
    Serial.print("Calibration offset to write: ");
    Serial.println(calOffsetSet);
    
    // Write to EEPROM
    EEPROM.put(eeAddress, calOffsetSet);
    EEPROM.commit();
    
    // Verify by reading back
    EEPROM.get(eeAddress, calOffsetGet);
    Serial.print("Calibration offset read back: ");
    Serial.println(calOffsetGet);
    
    if (calOffsetGet == calOffsetSet) {
      Serial.println("SUCCESS: Offset written and verified!");
    } else {
      Serial.println("ERROR: Offset verification failed!");
    }
    
    newData = false;
  }
}

void getData() {
  EEPROM.get(eeAddress, calOffsetGet);
  Serial.print("Current calibration offset in EEPROM: ");
  Serial.println(calOffsetGet);
  Serial.println("Enter new calibration offset value (integer) and press Enter:");
}

