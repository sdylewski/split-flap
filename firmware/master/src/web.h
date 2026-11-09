// Web UI + JSON API + WebSocket console. Handlers run on the web server task: they only read
// snapshots and hand work to the main loop with app::post().
//
//   GET    /api/state            live status (display, units, wifi, time)
//   GET    /api/settings         all settings
//   POST   /api/settings         JSON patch, e.g. {"mode":"clock"} -> {"ok":true} or 400 {"error":"..."}
//   POST   /api/message          {"text":"HI"} show now (text mode), or + "at":unix[, "indefinitely":true] to schedule
//   DELETE /api/scheduled?at=N   remove a scheduled message
//   POST   /api/command          {"line":"rehome all"} run a console command (output goes to the log)
//   GET    /api/log?since=N      log lines after sequence N
//   POST   /api/reboot
//   WS     /ws                   console: send a line, receive output
#pragma once
#include <Arduino.h>

namespace web {

void begin();
String buildState();  // main loop: build the /api/state JSON

}  // namespace web
