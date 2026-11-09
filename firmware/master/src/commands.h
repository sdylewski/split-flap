// Console commands (help, status, units, cal, ...). See docs/console.md.
#pragma once
#include <Arduino.h>

namespace commands {

void execute(const String& line);

}  // namespace commands
