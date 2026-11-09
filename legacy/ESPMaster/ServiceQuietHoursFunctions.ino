//Quiet Hours / Lockout Mode - Prevents flap movement during specified hours
//Returns true if flaps should be disabled (quiet hours active), false otherwise
bool isQuietHoursActive() {
  if (quietHoursEnabled != "true") {
    return false; // Quiet hours disabled
  }
  
  // Get current time
  if (timeStatus() != timeSet) {
    // Time not set yet, allow movement
    return false;
  }
  
  // Get current hour and minute
  int currentHour = timezone.hour();
  int currentMinute = timezone.minute();
  int currentTimeMinutes = currentHour * 60 + currentMinute;
  
  // Parse quiet hours from time (format: "HH:MM")
  int fromHour = 0, fromMinute = 0;
  int toHour = 0, toMinute = 0;
  
  // Parse "from" time
  int colonPos = quietHoursFrom.indexOf(':');
  if (colonPos > 0) {
    fromHour = atoi(quietHoursFrom.substring(0, colonPos).c_str());
    fromMinute = atoi(quietHoursFrom.substring(colonPos + 1).c_str());
  }
  int fromTimeMinutes = fromHour * 60 + fromMinute;
  
  // Parse "to" time
  colonPos = quietHoursTo.indexOf(':');
  if (colonPos > 0) {
    toHour = atoi(quietHoursTo.substring(0, colonPos).c_str());
    toMinute = atoi(quietHoursTo.substring(colonPos + 1).c_str());
  }
  int toTimeMinutes = toHour * 60 + toMinute;
  
  // Check if we're in quiet hours
  // Handle wrap-around case (e.g., 22:00 to 08:00 spans midnight)
  if (fromTimeMinutes > toTimeMinutes) {
    // Quiet hours span midnight (e.g., 22:00 to 08:00)
    // Active if current time is >= fromTime OR <= toTime
    if (currentTimeMinutes >= fromTimeMinutes || currentTimeMinutes <= toTimeMinutes) {
      return true;
    }
  } else {
    // Quiet hours within same day (e.g., 14:00 to 16:00)
    // Active if current time is >= fromTime AND <= toTime
    if (currentTimeMinutes >= fromTimeMinutes && currentTimeMinutes <= toTimeMinutes) {
      return true;
    }
  }
  
  return false; // Not in quiet hours
}

