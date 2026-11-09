// Crash reports: after a crash (panic, watchdog) the ESP32 saves a core dump to flash. At boot
// this reads its summary - crashed task, program counter, backtrace - and logs it, so a crash can
// be diagnosed without a USB cable. Decode the addresses with the firmware.elf of the build
// that crashed:  xtensa-esp32-elf-addr2line -pfiaC -e firmware.elf 0x400d1234 ...
#pragma once
#include <Arduino.h>

namespace crash {

void begin();         // read + log the last crash (if there is one)
bool present();
String summary();     // one line: task, pc, reason, backtrace
void clear();         // erase the stored core dump

}  // namespace crash
