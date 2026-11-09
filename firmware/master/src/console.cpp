#include "console.h"

#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>

#include <deque>
#include <vector>

#include "app.h"
#include "commands.h"
#include "config.h"
#include "log.h"

namespace console {

enum class Kind { Serial, Telnet, Web, Detached };

struct Session {
  Kind kind;
  uint32_t webId = 0;
  bool open = false;
  String input;
  bool lastWasCR = false;
  uint8_t iac = 0;
  std::deque<String> lines;
  bool streamLog = false;
  logx::Level logLevel = logx::Level::Info;
  uint32_t logSeq = 0;
  String out;
};

static constexpr int MAX_WEB = 4;
static Session serialSession{Kind::Serial};
static Session telnetSession{Kind::Telnet};
static Session webSessions[MAX_WEB];
static Session detachedSession{Kind::Detached};
static Session* current = nullptr;

static WiFiServer telnetServer(TELNET_PORT);
static WiFiClient telnetClient;
static bool telnetStarted = false;

// Web input arrives on the web server task
struct WebLine {
  uint32_t id;
  String text;
  int event;  // 0 = line, 1 = connected, 2 = disconnected
};
static SemaphoreHandle_t webLock = nullptr;
static std::deque<WebLine> webInbox;
static void (*webSend)(uint32_t, const String&) = nullptr;

// ---- Output --------------------------------------------------------------------------------
static void writeTo(Session& s, const String& text) {
  switch (s.kind) {
    case Kind::Serial: Serial.print(text); break;
    case Kind::Telnet:
      if (telnetClient && telnetClient.connected()) telnetClient.print(text);
      break;
    case Kind::Web:
      if (s.out.length() < 16000) s.out += text;
      break;
    case Kind::Detached: {
      // Commands started from the web API: output becomes log lines
      s.out += text;
      int nl;
      while ((nl = s.out.indexOf('\n')) >= 0) {
        String line = s.out.substring(0, nl);
        line.trim();
        if (line.length() > 0) LOGI("cmd", "%s", line.c_str());
        s.out = s.out.substring(nl + 1);
      }
      break;
    }
  }
}

void print(const String& text) {
  if (current) writeTo(*current, text);
}

void println(const String& text) { print(text + "\r\n"); }

void cprintf(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  print(buffer);
}

static void prompt(Session& s) { writeTo(s, app::displayPaused ? "flappy (paused)> " : "flappy> "); }

// ---- Input ---------------------------------------------------------------------------------
static void feed(Session& s, uint8_t c, bool echo) {
  if (s.kind == Kind::Telnet) {  // skip telnet option negotiation (IAC ...)
    if (s.iac == 1) { s.iac = (c >= 0xFB && c <= 0xFE) ? 2 : 0; return; }
    if (s.iac == 2) { s.iac = 0; return; }
    if (c == 0xFF) { s.iac = 1; return; }
  }
  if (c == '\r' || (c == '\n' && !s.lastWasCR)) {
    s.lastWasCR = (c == '\r');
    if (s.lines.size() < 8) s.lines.push_back(s.input);
    s.input = "";
    if (echo) writeTo(s, "\r\n");
    return;
  }
  if (c == '\n' || c == 0) {
    s.lastWasCR = false;
    return;
  }
  s.lastWasCR = false;
  if (c == 8 || c == 127) {
    if (s.input.length() > 0) {
      s.input.remove(s.input.length() - 1);
      if (echo) writeTo(s, "\b \b");
    }
  } else if (c >= 32 && s.input.length() < 200) {
    s.input += (char)c;
    if (echo) {
      char ch[2] = {(char)c, 0};
      writeTo(s, ch);
    }
  }
}

static Session* webSession(uint32_t id) {
  for (Session& s : webSessions) {
    if (s.open && s.webId == id) return &s;
  }
  return nullptr;
}

static void greet(Session& s) {
  writeTo(s, String("\r\nSplit-flap console - firmware ") + FIRMWARE_VERSION + ". Type 'help'.\r\n");
  prompt(s);
}

static void appendLog(const logx::Entry& entry, void* context) {
  Session* s = (Session*)context;
  if (entry.level >= s->logLevel) writeTo(*s, logx::format(entry) + "\r\n");
}

static void streamLog(Session& s) {
  if (!s.open || !s.streamLog) {
    s.logSeq = logx::lastSeq();
    return;
  }
  s.logSeq = logx::readSince(s.logSeq, 20, appendLog, &s);
}

void poll() {
  // Serial
  while (Serial.available()) feed(serialSession, Serial.read(), true);

  // Telnet: one client at a time, a new one replaces the old one
  if (telnetStarted && telnetServer.hasClient()) {
    WiFiClient incoming = telnetServer.accept();
    if (telnetClient && telnetClient.connected()) {
      telnetClient.print("\r\nAnother console connected - bye!\r\n");
      telnetClient.stop();
    }
    telnetClient = incoming;
    telnetClient.setNoDelay(true);
    telnetSession.open = true;
    telnetSession.lines.clear();
    telnetSession.input = "";
    telnetSession.streamLog = false;
    LOGI("console", "telnet connected from %s", telnetClient.remoteIP().toString().c_str());
    greet(telnetSession);
  }
  if (telnetSession.open && !(telnetClient && telnetClient.connected())) {
    telnetSession.open = false;
    LOGI("console", "telnet disconnected");
  }
  if (telnetSession.open) {
    while (telnetClient.available()) feed(telnetSession, telnetClient.read(), false);
    streamLog(telnetSession);
  }

  // Web
  // Take the web input only if there is some (no allocation on every call)
  static std::deque<WebLine> inbox;
  inbox.clear();
  xSemaphoreTake(webLock, portMAX_DELAY);
  if (!webInbox.empty()) inbox.swap(webInbox);
  xSemaphoreGive(webLock);
  for (WebLine& w : inbox) {
    if (w.event == 1) {
      for (Session& s : webSessions) {
        if (!s.open) {
          s = Session{Kind::Web};
          s.open = true;
          s.webId = w.id;
          s.logSeq = logx::lastSeq();
          greet(s);
          break;
        }
      }
    } else if (w.event == 2) {
      Session* s = webSession(w.id);
      if (s) s->open = false;
    } else {
      Session* s = webSession(w.id);
      if (s && s->lines.size() < 8) s->lines.push_back(w.text);
    }
  }
  for (Session& s : webSessions) {
    if (!s.open) continue;
    streamLog(s);
    if (s.out.length() > 0 && webSend) {
      webSend(s.webId, s.out);
      s.out = "";
    }
  }
}

bool sessionGone() {
  if (!current) return true;
  if (current->kind == Kind::Serial || current->kind == Kind::Detached) return false;
  return !current->open;
}

bool lineWaiting() { return current && !current->lines.empty(); }

bool readLine(String& line, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    app::services();
    if (sessionGone()) return false;
    if (!current->lines.empty()) {
      line = current->lines.front();
      current->lines.pop_front();
      line.trim();
      return true;
    }
    delay(5);
  }
  return false;
}

// ---- Session-level commands (log, quit) handled here; the rest in commands.cpp -------------
static bool sessionCommand(Session& s, const String& line) {
  String lower = line;
  lower.toLowerCase();
  if (lower == "quit" || lower == "exit") {
    if (s.kind == Kind::Telnet) {
      writeTo(s, "Bye!\r\n");
      telnetClient.stop();
      s.open = false;
    } else {
      writeTo(s, "(quit only closes telnet)\r\n");
    }
    return true;
  }
  if (lower == "log" || lower.startsWith("log ")) {
    String arg = lower.length() > 4 ? lower.substring(4) : "";
    arg.trim();
    logx::Level level;
    if (arg.length() == 0) {
      writeTo(s, "usage: log on|off|debug|info|warn|error\r\n");
    } else if (s.kind == Kind::Serial) {
      if (logx::parseLevel(arg, level)) {
        logx::setSerialLevel(level);
        writeTo(s, String("Serial log level: ") + (level == logx::Level::Off ? "off" : arg.c_str()) + "\r\n");
      }
    } else if (arg == "off") {
      s.streamLog = false;
      writeTo(s, "Log streaming off\r\n");
    } else if (logx::parseLevel(arg, level)) {
      s.streamLog = true;
      s.logLevel = level;
      s.logSeq = logx::lastSeq() > 30 ? logx::lastSeq() - 30 : 0;  // include some recent history
      writeTo(s, "Log streaming on (" + arg + ")\r\n");
    } else {
      writeTo(s, "usage: log on|off|debug|info|warn|error\r\n");
    }
    return true;
  }
  return false;
}

static void run(Session& s) {
  if (s.lines.empty()) return;
  String line = s.lines.front();
  s.lines.pop_front();
  line.trim();
  if (line.length() > 0) {
    current = &s;
    if (!sessionCommand(s, line)) {
      if (s.kind != Kind::Detached) LOGD("console", "> %s", line.c_str());
      commands::execute(line);
    }
    current = nullptr;
  }
  if (s.kind != Kind::Detached && (s.kind == Kind::Serial || s.open)) prompt(s);
}

void loop() {
  run(serialSession);
  if (telnetSession.open) run(telnetSession);
  for (Session& s : webSessions) {
    if (s.open) run(s);
  }
  run(detachedSession);
}

void runDetached(const String& line) {
  if (detachedSession.lines.size() < 8) detachedSession.lines.push_back(line);
}

// ---- Setup / web hooks ---------------------------------------------------------------------
void begin() {
  webLock = xSemaphoreCreateMutex();
  serialSession.open = true;
  detachedSession.open = true;
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  telnetStarted = true;
  LOGI("console", "console on serial, telnet port %d and the web page - type 'help'", TELNET_PORT);
}

static void pushWeb(uint32_t id, const String& text, int event) {
  if (!webLock) return;
  xSemaphoreTake(webLock, portMAX_DELAY);
  if (webInbox.size() < 32) webInbox.push_back({id, text, event});
  xSemaphoreGive(webLock);
}

void webConnected(uint32_t id) { pushWeb(id, "", 1); }
void webDisconnected(uint32_t id) { pushWeb(id, "", 2); }
void webInput(uint32_t id, const String& text) {
  // The browser sends whole lines (possibly several)
  int start = 0;
  while (start <= (int)text.length()) {
    int nl = text.indexOf('\n', start);
    if (nl < 0) nl = text.length();
    pushWeb(id, text.substring(start, nl), 0);
    start = nl + 1;
  }
}
void setWebSender(void (*send)(uint32_t, const String&)) { webSend = send; }

}  // namespace console
