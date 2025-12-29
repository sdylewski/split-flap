//Train Station Mode - Multiple display modes for train stations and lines
void checkTrainStation() {
  if (deviceMode == DEVICE_MODE_TRAIN_STATION) {
    static unsigned long lastStationChange = 0;
    static int currentStationIndex = 0;
    static int currentLineIndex = 0;
    
    // Get delay in seconds (default 30)
    unsigned long delaySeconds = atol(trainStationDelaySeconds.c_str());
    if (delaySeconds < 5) delaySeconds = 5; // Minimum 5 seconds
    if (delaySeconds > 3600) delaySeconds = 3600; // Maximum 1 hour
    
    unsigned long delayMillis = delaySeconds * 1000;
    unsigned long currentTime = millis();
    
    // Initialize on first run or if delay has passed
    if (lastStationChange == 0 || (currentTime - lastStationChange >= delayMillis)) {
      String newStation;
      
      if (trainStationType == "random") {
        // Random stations mode
        const char* trainStations[] = {
          "GRAND CENT",  // Grand Central Terminal, NYC
          "PENN STN.",   // Penn Station, NYC
          "UNION STN.",  // Union Station, DC
          "KING'S X",    // King's Cross, London
          "PADDINGTON",  // Paddington, London
          "GARE DU N.",  // Gare du Nord, Paris
          "GARE DE L.",  // Gare de Lyon, Paris
          "CENTRAL ST.", // Central Station, various cities
          "TERMINI",     // Roma Termini
          "AMSTERDAM",   // Amsterdam Centraal
          "FRANKFURT",   // Frankfurt Hauptbahnhof
          "MUNICH HB.",  // Munich Hauptbahnhof
          "ZURICH HB.",  // Zurich Hauptbahnhof
          "VIENNA HB.",  // Vienna Hauptbahnhof
          "MADRID AT.",  // Madrid Atocha
          "BARCELONA",   // Barcelona Sants
          "MILAN CEN.",  // Milan Centrale
          "ST PANCRAS",  // St Pancras, London
          "LIVERPOOL",   // Liverpool Street, London
          "CHICAGO UN.", // Chicago Union Station
          "BOSTON SB.",  // Boston South Station
          "PHILADELPH.", // Philadelphia 30th St
          "SEATTLE KI.", // Seattle King Street
          "PORTLAND",    // Portland Union Station
          "DENVER UN.",  // Denver Union Station
        };
        const int stationCount = sizeof(trainStations) / sizeof(trainStations[0]);
        
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
        newStation = String(trainStations[currentStationIndex]);
        
      } else if (trainStationType == "line") {
        // Train line mode - show stops in order
        if (trainStationLine == "eurostar") {
          const char* stops[] = {
            "LONDON ST",  // London St Pancras
            "ASHFORD",    // Ashford International
            "PARIS NORD", // Paris Gare du Nord
            "BRUSSELS",   // Brussels Midi
            "AMSTERDAM",  // Amsterdam Centraal
          };
          const int stopCount = sizeof(stops) / sizeof(stops[0]);
          currentLineIndex = (currentLineIndex + 1) % stopCount;
          newStation = String(stops[currentLineIndex]);
        } else if (trainStationLine == "thameslink") {
          const char* stops[] = {
            "BEDFORD",    // Bedford
            "LUTON",      // Luton
            "ST ALBANS",  // St Albans
            "LONDON BL",  // London Blackfriars
            "LONDON BR",  // London Bridge
            "BRIGHTON",   // Brighton
          };
          const int stopCount = sizeof(stops) / sizeof(stops[0]);
          currentLineIndex = (currentLineIndex + 1) % stopCount;
          newStation = String(stops[currentLineIndex]);
        } else if (trainStationLine == "acela") {
          const char* stops[] = {
            "BOSTON SB.", // Boston South Station
            "PROVIDENCE", // Providence
            "NEW HAVEN",  // New Haven
            "NYC PENN",   // NYC Penn Station
            "PHILADELPH.", // Philadelphia
            "BALTIMORE",  // Baltimore
            "DC UNION",   // DC Union Station
          };
          const int stopCount = sizeof(stops) / sizeof(stops[0]);
          currentLineIndex = (currentLineIndex + 1) % stopCount;
          newStation = String(stops[currentLineIndex]);
        } else if (trainStationLine == "california") {
          const char* stops[] = {
            "SACRAMENTO", // Sacramento
            "DAVIS",      // Davis
            "BERKELEY",   // Berkeley
            "OAKLAND",    // Oakland
            "SAN JOSE",   // San Jose
            "SALINAS",    // Salinas
            "SAN LUIS",   // San Luis Obispo
            "SANTA BAR",  // Santa Barbara
            "LOS ANGEL",  // Los Angeles
            "ANAHEIM",    // Anaheim
            "SAN DIEGO",  // San Diego
          };
          const int stopCount = sizeof(stops) / sizeof(stops[0]);
          currentLineIndex = (currentLineIndex + 1) % stopCount;
          newStation = String(stops[currentLineIndex]);
        } else {
          // Default to Eurostar if line not specified
          const char* stops[] = {"LONDON ST", "PARIS NORD", "BRUSSELS"};
          const int stopCount = sizeof(stops) / sizeof(stops[0]);
          currentLineIndex = (currentLineIndex + 1) % stopCount;
          newStation = String(stops[currentLineIndex]);
        }
        
      } else if (trainStationType == "bart") {
        // BART line mode - show stops in order
        if (trainStationLine == "bart-red") {
          const char* stops[] = {
            "RICHMOND",   // Richmond
            "EL CERRITO", // El Cerrito del Norte
            "BERKELEY",   // Berkeley
            "OAKLAND 19", // Oakland 19th St
            "OAKLAND 12", // Oakland 12th St
            "SF MONTGOM", // SF Montgomery
            "SF POWELL",  // SF Powell
            "SF CIVIC",   // SF Civic Center
            "SF 16TH",    // SF 16th St Mission
            "SF 24TH",    // SF 24th St Mission
            "DALY CITY",  // Daly City
            "SF AIRPORT", // SF Airport
            "MILLBRAE",   // Millbrae
          };
          const int stopCount = sizeof(stops) / sizeof(stops[0]);
          currentLineIndex = (currentLineIndex + 1) % stopCount;
          newStation = String(stops[currentLineIndex]);
        } else if (trainStationLine == "bart-yellow") {
          const char* stops[] = {
            "ANTIOCH",    // Antioch
            "PITTSBURG",  // Pittsburg/Bay Point
            "CONCORD",    // Concord
            "PLEASANT",   // Pleasant Hill
            "WALNUT CR",  // Walnut Creek
            "LAFAYETTE",  // Lafayette
            "ORINDA",     // Orinda
            "ROCKRIDGE",  // Rockridge
            "OAKLAND 19", // Oakland 19th St
            "OAKLAND 12", // Oakland 12th St
            "SF MONTGOM", // SF Montgomery
            "SF POWELL",  // SF Powell
            "SF CIVIC",   // SF Civic Center
            "SF 16TH",    // SF 16th St Mission
            "SF 24TH",    // SF 24th St Mission
            "GLEN PARK",  // Glen Park
            "BALBOA PK",  // Balboa Park
            "DALY CITY",  // Daly City
            "SF AIRPORT", // SF Airport
            "S SAN MATE", // South San Francisco
            "SAN BRUNO",  // San Bruno
            "MILLBRAE",   // Millbrae
          };
          const int stopCount = sizeof(stops) / sizeof(stops[0]);
          currentLineIndex = (currentLineIndex + 1) % stopCount;
          newStation = String(stops[currentLineIndex]);
        } else if (trainStationLine == "bart-blue") {
          const char* stops[] = {
            "DUBLIN",     // Dublin/Pleasanton
            "CASTRO VL",  // Castro Valley
            "HAYWARD",    // Hayward
            "SAN LEANDRO", // San Leandro
            "COLMISEUM",  // Coliseum
            "OAKLAND 12", // Oakland 12th St
            "SF MONTGOM", // SF Montgomery
            "SF POWELL",  // SF Powell
            "SF CIVIC",   // SF Civic Center
            "SF 16TH",    // SF 16th St Mission
            "SF 24TH",    // SF 24th St Mission
            "DALY CITY",  // Daly City
          };
          const int stopCount = sizeof(stops) / sizeof(stops[0]);
          currentLineIndex = (currentLineIndex + 1) % stopCount;
          newStation = String(stops[currentLineIndex]);
        } else if (trainStationLine == "bart-green") {
          const char* stops[] = {
            "FREMONT",    // Fremont
            "UNION CITY", // Union City
            "SOUTH HAY",  // South Hayward
            "HAYWARD",    // Hayward
            "SAN LEANDRO", // San Leandro
            "COLMISEUM",  // Coliseum
            "OAKLAND 12", // Oakland 12th St
            "LAKE MERRIT", // Lake Merritt
            "OAKLAND 19", // Oakland 19th St
            "ROCKRIDGE",  // Rockridge
            "BERKELEY",   // Berkeley
            "EL CERRITO", // El Cerrito del Norte
            "RICHMOND",   // Richmond
          };
          const int stopCount = sizeof(stops) / sizeof(stops[0]);
          currentLineIndex = (currentLineIndex + 1) % stopCount;
          newStation = String(stops[currentLineIndex]);
        } else if (trainStationLine == "bart-orange") {
          const char* stops[] = {
            "RICHMOND",   // Richmond
            "EL CERRITO", // El Cerrito del Norte
            "BERKELEY",   // Berkeley
            "OAKLAND 19", // Oakland 19th St
            "OAKLAND 12", // Oakland 12th St
            "LAKE MERRIT", // Lake Merritt
            "FRUITVALE",  // Fruitvale
            "COLMISEUM",  // Coliseum
            "SAN LEANDRO", // San Leandro
            "BAY FAIR",   // Bay Fair
            "CASTRO VL",  // Castro Valley
            "DUBLIN",     // Dublin/Pleasanton
          };
          const int stopCount = sizeof(stops) / sizeof(stops[0]);
          currentLineIndex = (currentLineIndex + 1) % stopCount;
          newStation = String(stops[currentLineIndex]);
        } else {
          // Default to Red line if BART line not specified
          const char* stops[] = {"RICHMOND", "OAKLAND 12", "SF MONTGOM", "DALY CITY"};
          const int stopCount = sizeof(stops) / sizeof(stops[0]);
          currentLineIndex = (currentLineIndex + 1) % stopCount;
          newStation = String(stops[currentLineIndex]);
        }
        
      } else {
        // Default to random mode if type not recognized
        const char* trainStations[] = {"GRAND CENT", "PENN STN.", "UNION STN."};
        const int stationCount = sizeof(trainStations) / sizeof(trainStations[0]);
        currentStationIndex = (currentStationIndex + 1) % stationCount;
        newStation = String(trainStations[currentStationIndex]);
      }
      
      // Only update if different from current text
      if (inputText != newStation) {
        SerialPrintln("Train Station Mode: Changing to " + newStation);
        inputText = newStation;
        lastStationChange = currentTime;
      }
    }
  }
}
