// Command console, available on three "sessions" at once:
//   - USB serial       (pio device monitor, 115200)
//   - telnet           (nc flappy.local 23)
//   - the web page     (Console tab, over a WebSocket)
// Commands run one at a time on the main loop. Output goes to the session that typed the command.
#pragma once
#include <Arduino.h>

namespace console {

void begin();
void poll();   // collect input, stream log lines, flush web output (call often - app::services())
void loop();   // run one complete command (main loop only)

// Output to the session running the current command
void print(const String& text);
void println(const String& text = "");
void cprintf(const char* format, ...) __attribute__((format(printf, 1, 2)));

// Wait for the next input line from the session running the current command.
// Returns false on timeout or if that session went away.
bool readLine(String& line, uint32_t timeoutMs);
bool lineWaiting();       // a line is ready (for commands that poll while doing something)
bool sessionGone();       // the session running the current command disconnected

// Web console (called by web.cpp from the web server task)
void webConnected(uint32_t clientId);
void webDisconnected(uint32_t clientId);
void webInput(uint32_t clientId, const String& text);
void setWebSender(void (*send)(uint32_t clientId, const String& text));

// Run a command line as if typed (from the web API); output goes to the log.
void runDetached(const String& line);

}  // namespace console
