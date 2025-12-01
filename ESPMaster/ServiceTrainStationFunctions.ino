//Train Station Mode - Cycles through famous train stations
void checkTrainStation() {
  if (deviceMode == DEVICE_MODE_TRAIN_STATION) {
    static unsigned long lastStationChange = 0;
    static int currentStationIndex = 0;
    
    // List of famous train stations (Europe and US)
    // Limited to 10 characters max per station name to fit on display
    const char* trainStations[] = {
      "GRAND CENT",  // Grand Central Terminal, NYC
      "PENN STN",    // Penn Station, NYC
      "UNION STN",   // Union Station, DC
      "KING'S X",    // King's Cross, London
      "PADDINGTON",  // Paddington, London
      "GARE DU N",   // Gare du Nord, Paris
      "GARE DE L",   // Gare de Lyon, Paris
      "CENTRAL ST",  // Central Station, various cities
      "TERMINI",     // Roma Termini
      "AMSTERDAM",   // Amsterdam Centraal
      "FRANKFURT",   // Frankfurt Hauptbahnhof
      "MUNICH HB",   // Munich Hauptbahnhof
      "ZURICH HB",   // Zurich Hauptbahnhof
      "VIENNA HB",   // Vienna Hauptbahnhof
      "MADRID AT",   // Madrid Atocha
      "BARCELONA",   // Barcelona Sants
      "MILAN CEN",   // Milan Centrale
      "ST PANCRAS",  // St Pancras, London
      "LIVERPOOL",   // Liverpool Street, London
      "CHICAGO UN",  // Chicago Union Station
      "BOSTON SB",   // Boston South Station
      "PHILADELPH",  // Philadelphia 30th St
      "SEATTLE KI",  // Seattle King Street
      "PORTLAND",    // Portland Union Station
      "DENVER UN",   // Denver Union Station
    };
    const int stationCount = sizeof(trainStations) / sizeof(trainStations[0]);
    
    // Get delay in seconds (default 30)
    unsigned long delaySeconds = atol(trainStationDelaySeconds.c_str());
    if (delaySeconds < 5) delaySeconds = 5; // Minimum 5 seconds
    if (delaySeconds > 3600) delaySeconds = 3600; // Maximum 1 hour
    
    unsigned long delayMillis = delaySeconds * 1000;
    
    // Check if it's time to change stations
    unsigned long currentTime = millis();
    
    // Initialize on first run or if delay has passed
    if (lastStationChange == 0 || (currentTime - lastStationChange >= delayMillis)) {
      // Select a random station (different from current)
      int newStationIndex;
      if (stationCount > 1) {
        do {
          newStationIndex = random(0, stationCount);
        } while (newStationIndex == currentStationIndex);
      } else {
        newStationIndex = 0;
      }
      
      currentStationIndex = newStationIndex;
      String newStation = String(trainStations[currentStationIndex]);
      
      // Only update if different from current text
      if (inputText != newStation) {
        SerialPrintln("Train Station Mode: Changing to " + newStation);
        inputText = newStation;
        lastStationChange = currentTime;
      }
    }
  }
}

