//Random Phrase Mode - Cycles through user-defined phrases with random timing
void checkRandomPhrase() {
  if (deviceMode == DEVICE_MODE_RANDOM_PHRASE) {
    static unsigned long lastPhraseChange = 0;
    static unsigned long currentDelayMillis = 0;
    static int currentPhraseIndex = -1;
    
    // Parse phrase list (comma-separated)
    if (randomPhraseList.length() == 0) {
      // No phrases defined, show placeholder
      if (inputText != "NO PHRASES") {
        inputText = "NO PHRASES";
        SerialPrintln("Random Phrase Mode: No phrases defined");
      }
      return;
    }
    
    // Split comma-separated phrases
    // Simple parsing - split by comma
    const int MAX_PHRASES = 50;
    String phrases[MAX_PHRASES];
    int phraseCount = 0;
    
    String remaining = randomPhraseList;
    remaining.trim();
    
    int commaPos = 0;
    while (commaPos >= 0 && phraseCount < MAX_PHRASES) {
      commaPos = remaining.indexOf(',');
      if (commaPos >= 0) {
        phrases[phraseCount] = remaining.substring(0, commaPos);
        phrases[phraseCount].trim();
        remaining = remaining.substring(commaPos + 1);
        remaining.trim();
      } else {
        // Last phrase
        phrases[phraseCount] = remaining;
        phrases[phraseCount].trim();
      }
      
      // Only add non-empty phrases
      if (phrases[phraseCount].length() > 0) {
        phraseCount++;
      }
    }
    
    if (phraseCount == 0) {
      if (inputText != "NO PHRASES") {
        inputText = "NO PHRASES";
        SerialPrintln("Random Phrase Mode: No valid phrases found");
      }
      return;
    }
    
    // Get min and max delays
    unsigned long minDelaySeconds = atol(randomPhraseMinDelaySeconds.c_str());
    unsigned long maxDelaySeconds = atol(randomPhraseMaxDelaySeconds.c_str());
    
    // Validate and constrain delays
    if (minDelaySeconds < 5) minDelaySeconds = 5;
    if (minDelaySeconds > 3600) minDelaySeconds = 3600;
    if (maxDelaySeconds < 5) maxDelaySeconds = 5;
    if (maxDelaySeconds > 3600) maxDelaySeconds = 3600;
    if (maxDelaySeconds < minDelaySeconds) maxDelaySeconds = minDelaySeconds;
    
    unsigned long minDelayMillis = minDelaySeconds * 1000;
    unsigned long maxDelayMillis = maxDelaySeconds * 1000;
    
    unsigned long currentTime = millis();
    
    // Initialize delay on first run
    if (currentDelayMillis == 0) {
      // Random delay between min and max
      if (maxDelayMillis > minDelayMillis) {
        currentDelayMillis = minDelayMillis + random(0, maxDelayMillis - minDelayMillis + 1);
      } else {
        currentDelayMillis = minDelayMillis;
      }
      lastPhraseChange = currentTime;
    }
    
    // Check if it's time to change phrases
    if (currentTime - lastPhraseChange >= currentDelayMillis) {
      // Select a random phrase (different from current if possible)
      int newPhraseIndex;
      if (phraseCount > 1) {
        do {
          newPhraseIndex = random(0, phraseCount);
        } while (newPhraseIndex == currentPhraseIndex);
      } else {
        newPhraseIndex = 0;
      }
      
      currentPhraseIndex = newPhraseIndex;
      String newPhrase = phrases[currentPhraseIndex];
      
      // Truncate if too long (max 75 characters for display)
      if (newPhrase.length() > 75) {
        newPhrase = newPhrase.substring(0, 75);
      }
      
      // Only update if different from current text
      if (inputText != newPhrase) {
        SerialPrintln("Random Phrase Mode: Changing to \"" + newPhrase + "\"");
        inputText = newPhrase;
        lastPhraseChange = currentTime;
        
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

