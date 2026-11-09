// Flappy web UI. Talks to the JSON API (see firmware/master/src/web.h) and the /ws console.
"use strict";

const $ = (id) => document.getElementById(id);
const $$ = (selector) => Array.from(document.querySelectorAll(selector));

const TRAIN_LINES = {
  line: [["eurostar", "Eurostar"], ["thameslink", "Thameslink"], ["acela", "Acela"], ["california", "California coast"]],
  bart: [["bart-red", "BART Red"], ["bart-yellow", "BART Yellow"], ["bart-blue", "BART Blue"], ["bart-green", "BART Green"], ["bart-orange", "BART Orange"]],
};

// BART stations (from api.bart.gov stn.aspx?cmd=stns)
const BART_STATIONS = [["12TH", "12th St./Oakland City Center"], ["16TH", "16th St./Mission"], ["19TH", "19th St./Oakland"], ["24TH", "24th St./Mission"], ["ANTC", "Antioch"], ["ASHB", "Ashby"], ["BALB", "Balboa Park"], ["BAYF", "Bay Fair"], ["BERY", "Berryessa/North San Jose"], ["CAST", "Castro Valley"], ["CIVC", "Civic Center/UN Plaza"], ["COLS", "Coliseum"], ["COLM", "Colma"], ["CONC", "Concord"], ["DALY", "Daly City"], ["DBRK", "Downtown Berkeley"], ["DUBL", "Dublin/Pleasanton"], ["PLZA", "El Cerrito Plaza"], ["DELN", "El Cerrito del Norte"], ["EMBR", "Embarcadero"], ["FRMT", "Fremont"], ["FTVL", "Fruitvale"], ["GLEN", "Glen Park"], ["HAYW", "Hayward"], ["LAFY", "Lafayette"], ["LAKE", "Lake Merritt"], ["MCAR", "MacArthur"], ["MLBR", "Millbrae"], ["MLPT", "Milpitas"], ["MONT", "Montgomery St."], ["NBRK", "North Berkeley"], ["NCON", "North Concord/Martinez"], ["OAKL", "Oakland International Airport (OAK)"], ["ORIN", "Orinda"], ["PCTR", "Pittsburg Center"], ["PITT", "Pittsburg/Bay Point"], ["PHIL", "Pleasant Hill/Contra Costa Centre"], ["POWL", "Powell St."], ["RICH", "Richmond"], ["ROCK", "Rockridge"], ["SBRN", "San Bruno"], ["SFIA", "San Francisco International Airport (SFO)"], ["SANL", "San Leandro"], ["SHAY", "South Hayward"], ["SSAN", "South San Francisco"], ["UCTY", "Union City"], ["WCRK", "Walnut Creek"], ["WARM", "Warm Springs/South Fremont"], ["WDUB", "West Dublin/Pleasanton"], ["WOAK", "West Oakland"]];

const TIMEZONES = [
  ["America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"],
  ["America/Denver", "MST7MDT,M3.2.0,M11.1.0"],
  ["America/Phoenix", "MST7"],
  ["America/Chicago", "CST6CDT,M3.2.0,M11.1.0"],
  ["America/New_York", "EST5EDT,M3.2.0,M11.1.0"],
  ["America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"],
  ["Pacific/Honolulu", "HST10"],
  ["Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"],
  ["Europe/Berlin (CET)", "CET-1CEST,M3.5.0,M10.5.0/3"],
  ["Europe/Helsinki (EET)", "EET-2EEST,M3.5.0/3,M10.5.0/4"],
  ["Asia/Tokyo", "JST-9"],
  ["Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"],
  ["UTC", "UTC0"],
];

let settings = null;
let weatherPick = null;  // {lat, lon, place} chosen in the city search
let selectedMode = "text";
let selectedAlign = "left";
let lastState = null;

// ---- API -----------------------------------------------------------------------------------
async function api(path, options = {}) {
  const init = { method: options.method || "GET", headers: {} };
  if (options.body !== undefined) {
    init.method = options.method || "POST";
    init.headers["Content-Type"] = "application/json";
    init.body = JSON.stringify(options.body);
  }
  const response = await fetch(path, init);
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.error || `${response.status} ${response.statusText}`);
  return data;
}

function flash(element, text, ok = true) {
  element.textContent = text;
  element.className = "status " + (ok ? "good" : "bad");
  clearTimeout(element._timer);
  element._timer = setTimeout(() => { element.textContent = ""; }, ok ? 3000 : 8000);
}

async function save(patch, statusElement) {
  try {
    await api("/api/settings", { body: patch });
    flash(statusElement, "Saved");
    setTimeout(loadSettings, 400);
  } catch (error) {
    flash(statusElement, error.message, false);
  }
}

function command(line) {
  return api("/api/command", { body: { line } }).catch((error) => alert(error.message));
}

// ---- Tabs ----------------------------------------------------------------------------------
function showTab(name) {
  $$("#tabs button").forEach((b) => b.classList.toggle("active", b.dataset.tab === name));
  $$(".tab").forEach((t) => t.classList.toggle("active", t.id === "tab-" + name));
  try { localStorage.setItem("tab", name); } catch (e) { /* private mode */ }
  if (name === "console") { connectConsole(); $("consoleLine").focus(); }
}
$$("#tabs button").forEach((b) => b.addEventListener("click", () => showTab(b.dataset.tab)));

// ---- Settings -> form ----------------------------------------------------------------------
function setMode(mode) {
  selectedMode = mode;
  $$("#modeButtons button").forEach((b) => b.classList.toggle("active", b.dataset.mode === mode));
  $$(".mode-panel").forEach((p) => p.classList.toggle("active", p.dataset.panel === mode));
}
$$("#modeButtons button").forEach((b) => b.addEventListener("click", () => setMode(b.dataset.mode)));

function setAlign(align) {
  selectedAlign = align;
  $$("#alignButtons button").forEach((b) => b.classList.toggle("active", b.dataset.align === align));
}
$$("#alignButtons button").forEach((b) => b.addEventListener("click", () => setAlign(b.dataset.align)));

function fillTrainLines() {
  const lines = TRAIN_LINES[$("trainType").value] || [];
  $("trainLine").innerHTML = lines.map(([id, name]) => `<option value="${id}">${name}</option>`).join("");
  $("trainLine").disabled = lines.length === 0;
  if (settings && lines.some(([id]) => id === settings.trainLine)) $("trainLine").value = settings.trainLine;
}
$("trainType").addEventListener("change", fillTrainLines);

function toDateInput(unix) {
  if (!unix) return "";
  const d = new Date(unix * 1000);
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

function fillForm(s) {
  setMode(s.mode);
  setAlign(s.alignment);
  $("text").value = s.text;
  $("clock24h").checked = s.clock24h;
  $("dateFormat").value = s.dateFormat;
  $("countdownDate").value = toDateInput(s.countdownTo);
  $("trainType").value = s.trainType;
  fillTrainLines();
  $("trainSeconds").value = s.trainSeconds;
  $("trainStations").value = s.trainStations;
  $("phrases").value = s.phrases;
  $("phraseMinSeconds").value = s.phraseMinSeconds;
  $("phraseMaxSeconds").value = s.phraseMaxSeconds;
  $("numbers").value = s.numbers;
  $("numberMinSeconds").value = s.numberMinSeconds;
  $("numberMaxSeconds").value = s.numberMaxSeconds;
  $("numberNameSeconds").value = s.numberNameSeconds;
  $("speed").value = s.speed;
  $("speedValue").textContent = s.speed;
  $("lineSeconds").value = s.lineSeconds;
  $("quietEnabled").checked = s.quietEnabled;
  $("quietFrom").value = s.quietFrom;
  $("quietTo").value = s.quietTo;
  $("quietBlank").checked = s.quietBlank;
  $("unitCount").value = s.unitCount;
  $("logLevel").value = s.logLevel;
  $("timezone").value = s.timezone;
  // Weather
  weatherPick = null;
  $("weatherWhere").textContent = `Now: ${s.weatherPlace} (${s.weatherLat.toFixed(3)}, ${s.weatherLon.toFixed(3)})`;
  $("weatherFahrenheit").checked = s.weatherFahrenheit;
  $("weatherSeconds").value = s.weatherSeconds;
  // BART
  $("bartStation").value = s.bartStation;
  $("bartDirection").value = s.bartDirection;
  $("bartSeconds").value = s.bartSeconds;
  $("bartKey").value = s.bartKey;
  // Printers
  renderPrinters(s.printers || []);
  $("printerNotify").checked = s.printerNotify;
  $("printerNotifySeconds").value = s.printerNotifySeconds;
  // Wave
  $("waveOrder").value = s.waveMs > 0 ? s.waveOrder : "off";
  $("waveMs").value = s.waveMs > 0 ? s.waveMs : 150;
  $("waveMsValue").textContent = `${$("waveMs").value} ms`;
  const known = TIMEZONES.find(([name, tz]) => tz === s.timezone);
  $("timezoneName").value = known ? known[1] : "custom";
  renderSchedule(s.scheduled || []);
}

async function loadSettings() {
  try {
    settings = await api("/api/settings");
    fillForm(settings);
  } catch (error) {
    console.warn("settings", error);
  }
}

$("speed").addEventListener("input", () => { $("speedValue").textContent = $("speed").value; });
$("text").addEventListener("input", () => {
  const n = $("text").value.length;
  const units = settings ? settings.unitCount : 10;
  $("textHint").textContent = n > units
    ? `${n} characters - longer than ${units}, so it shows as several lines, ${$("lineSeconds").value || 5} s each.`
    : "Words wrap onto several lines; a new line in the box forces a break.";
});

// ---- Display tab: apply --------------------------------------------------------------------
$("btnApply").addEventListener("click", () => {
  const n = (id) => Number($(id).value);
  const wave = $("waveOrder").value;
  const patch = { mode: selectedMode, alignment: selectedAlign, speed: n("speed"), lineSeconds: n("lineSeconds"),
    waveMs: wave === "off" ? 0 : n("waveMs"), waveOrder: wave === "off" ? "left" : wave };
  switch (selectedMode) {
    case "text": patch.text = $("text").value.toUpperCase(); break;
    case "clock": patch.clock24h = $("clock24h").checked; break;
    case "date": patch.dateFormat = $("dateFormat").value; break;
    case "countdown": {
      const value = $("countdownDate").value;
      if (!value) return flash($("applyStatus"), "Pick a date first", false);
      const [y, m, d] = value.split("-").map(Number);
      patch.countdownTo = Math.floor(new Date(y, m - 1, d).getTime() / 1000);
      break;
    }
    case "trainstation":
      Object.assign(patch, { trainType: $("trainType").value, trainLine: $("trainLine").value || "",
        trainSeconds: n("trainSeconds"), trainStations: $("trainStations").value.toUpperCase() });
      break;
    case "randomphrase":
      Object.assign(patch, { phrases: $("phrases").value.toUpperCase(), phraseMinSeconds: n("phraseMinSeconds"),
        phraseMaxSeconds: n("phraseMaxSeconds") });
      break;
    case "weather":
      Object.assign(patch, { weatherFahrenheit: $("weatherFahrenheit").checked, weatherSeconds: n("weatherSeconds") });
      if (weatherPick) Object.assign(patch, { weatherLat: weatherPick.lat, weatherLon: weatherPick.lon, weatherPlace: weatherPick.place });
      break;
    case "bart":
      Object.assign(patch, { bartStation: $("bartStation").value, bartDirection: $("bartDirection").value,
        bartSeconds: n("bartSeconds"), bartKey: $("bartKey").value.trim() });
      break;
    case "printers":
      Object.assign(patch, { printerNotify: $("printerNotify").checked, printerNotifySeconds: n("printerNotifySeconds") });
      break;
    case "famousnumbers":
      Object.assign(patch, { numbers: $("numbers").value.toUpperCase(), numberMinSeconds: n("numberMinSeconds"),
        numberMaxSeconds: n("numberMaxSeconds"), numberNameSeconds: n("numberNameSeconds") });
      break;
  }
  save(patch, $("applyStatus"));
});

// ---- Weather / BART / printers panels --------------------------------------------------------
$("bartStation").innerHTML = BART_STATIONS.map(([code, name]) => `<option value="${code}">${name} (${code})</option>`).join("");
$("waveMs").addEventListener("input", () => { $("waveMsValue").textContent = `${$("waveMs").value} ms`; });

let searchTimer = null;
$("weatherSearch").addEventListener("input", () => {
  clearTimeout(searchTimer);
  const name = $("weatherSearch").value.trim();
  if (name.length < 2) return;
  searchTimer = setTimeout(async () => {
    try {
      // Open-Meteo's free geocoding, called from the browser (the ESP32 isn't involved)
      const r = await fetch(`https://geocoding-api.open-meteo.com/v1/search?count=8&name=${encodeURIComponent(name)}`);
      const data = await r.json();
      const results = data.results || [];
      $("weatherResults").innerHTML = results.length
        ? results.map((p, i) => `<option value="${i}">${p.name}, ${p.admin1 || ""} ${p.country_code || ""}</option>`).join("")
        : '<option value="">No match</option>';
      $("weatherResults")._results = results;
      if (results.length) pickPlace(0);
    } catch (e) {
      $("weatherWhere").textContent = "City search needs internet access from this browser.";
    }
  }, 400);
});
$("weatherResults").addEventListener("change", () => pickPlace(Number($("weatherResults").value)));
function pickPlace(i) {
  const p = ($("weatherResults")._results || [])[i];
  if (!p) return;
  weatherPick = { lat: p.latitude, lon: p.longitude, place: p.name };
  $("weatherWhere").textContent = `Will use: ${p.name} (${p.latitude.toFixed(3)}, ${p.longitude.toFixed(3)}) - press "Show on display"`;
}

function renderPrinters(list) {
  const ul = $("printerList");
  ul.innerHTML = list.length ? "" : '<li class="empty">No printers yet</li>';
  list.forEach((p, i) => {
    const li = document.createElement("li");
    li.innerHTML = "<div><strong></strong><div class='when'></div></div>";
    li.querySelector("strong").textContent = p.name;
    li.querySelector(".when").textContent = p.host;
    const remove = document.createElement("button");
    remove.textContent = "Remove";
    remove.addEventListener("click", () => {
      const printers = settings.printers.filter((_, j) => j !== i);
      save({ printers }, $("applyStatus"));
    });
    li.appendChild(remove);
    ul.appendChild(li);
  });
}
$("btnAddPrinter").addEventListener("click", () => {
  const name = $("printerName").value.trim().toUpperCase();
  const host = $("printerHost").value.trim();
  if (!name || !host) return flash($("applyStatus"), "Enter a name and a host", false);
  save({ printers: [...(settings.printers || []), { name, host }] }, $("applyStatus"));
  $("printerName").value = "";
  $("printerHost").value = "";
});

// ---- Schedule tab --------------------------------------------------------------------------
function renderSchedule(list) {
  const ul = $("schedList");
  if (!list.length) {
    ul.innerHTML = '<li class="empty">Nothing scheduled</li>';
    return;
  }
  ul.innerHTML = "";
  list.slice().sort((a, b) => a.at - b.at).forEach((m) => {
    const li = document.createElement("li");
    const when = new Date(m.at * 1000).toLocaleString();
    li.innerHTML = `<div><strong></strong><div class="when">${when}${m.indefinitely ? " · keeps showing" : ""}</div></div>`;
    li.querySelector("strong").textContent = m.text;
    const remove = document.createElement("button");
    remove.textContent = "Remove";
    remove.addEventListener("click", async () => {
      await api(`/api/scheduled?at=${m.at}`, { method: "DELETE" }).catch((e) => alert(e.message));
      setTimeout(loadSettings, 400);
    });
    li.appendChild(remove);
    ul.appendChild(li);
  });
}

$("btnSchedule").addEventListener("click", async () => {
  const text = $("schedText").value.trim().toUpperCase();
  const when = $("schedAt").value;
  if (!text || !when) return flash($("schedStatus"), "Enter a message and a time", false);
  const at = Math.floor(new Date(when).getTime() / 1000);
  try {
    await api("/api/message", { body: { text, at, indefinitely: $("schedForever").checked } });
    flash($("schedStatus"), "Scheduled");
    $("schedText").value = "";
    setTimeout(loadSettings, 400);
  } catch (error) {
    flash($("schedStatus"), error.message, false);
  }
});

$("btnQuiet").addEventListener("click", () => save({
  quietEnabled: $("quietEnabled").checked, quietFrom: $("quietFrom").value, quietTo: $("quietTo").value,
  quietBlank: $("quietBlank").checked,
}, $("quietStatus")));

// ---- System tab ----------------------------------------------------------------------------
$("timezoneName").innerHTML = TIMEZONES.map(([name, tz]) => `<option value="${tz}">${name}</option>`).join("") +
  '<option value="custom">Other (type the POSIX string)</option>';
$("timezoneName").addEventListener("change", () => {
  if ($("timezoneName").value !== "custom") $("timezone").value = $("timezoneName").value;
});
$("btnSystem").addEventListener("click", () => {
  const option = $("timezoneName").selectedOptions[0];
  save({
    timezone: $("timezone").value.trim(),
    timezoneName: option && option.value !== "custom" ? option.textContent : "Custom",
    unitCount: Number($("unitCount").value), logLevel: $("logLevel").value,
  }, $("systemStatus"));
});
$("btnReboot").addEventListener("click", async () => {
  if (!confirm("Reboot the display controller?")) return;
  await api("/api/reboot", { body: {} }).catch(() => {});
});

// ---- Live state ----------------------------------------------------------------------------
function formatUptime(seconds) {
  const d = Math.floor(seconds / 86400), h = Math.floor(seconds / 3600) % 24, m = Math.floor(seconds / 60) % 60;
  return d ? `${d}d ${h}h` : h ? `${h}h ${m}m` : `${m}m ${seconds % 60}s`;
}

function renderBoard(state) {
  const board = $("board");
  const line = state.display.line || "";
  const units = state.units || [];
  if (board.children.length !== units.length) {
    board.innerHTML = units.map(() => '<div class="tile"></div>').join("");
  }
  units.forEach((u, i) => {
    const tile = board.children[i];
    tile.textContent = line[i] || " ";
    tile.classList.toggle("missing", !u.present);
    tile.classList.toggle("moving", u.status >= 1 && u.status <= 3);
    tile.title = `Unit ${i}: ${u.present ? u.statusName : "not answering"}`;
  });
}

function renderChips(state) {
  const wifi = $("chipWifi");
  wifi.textContent = state.wifi.connected ? `WiFi ${state.wifi.rssi} dBm` : "WiFi down";
  wifi.className = "chip " + (!state.wifi.connected ? "bad" : state.wifi.rssi < -75 ? "warn" : "good");
  $("chipTime").textContent = state.time.valid ? state.time.local.slice(11, 16) : "no time yet";
  const present = state.units.filter((u) => u.present && u.status !== 4).length;
  const chip = $("chipUnits");
  chip.textContent = `Units ${present}/${state.units.length}`;
  chip.className = "chip " + (present === state.units.length ? "good" : "bad");
  $("chipMode").textContent = state.display.status + (state.display.lineCount > 1
    ? ` · line ${state.display.lineIndex + 1}/${state.display.lineCount}` : "");
}

function cell(text, cls = "") { return `<td class="${cls}">${text}</td>`; }

function renderUnits(state) {
  const rows = state.units.map((u) => {
    const i = u.info;
    const statusClass = !u.present ? "bad" : u.status === 4 ? "bad" : u.status === 0 || u.status === -1 ? "good" : "warn";
    const supply = i ? `${(i.vcc / 1000).toFixed(2)} V (${(i.vccMin / 1000).toFixed(2)})` : "–";
    const supplyClass = i && i.vccMin && i.vccMin < 4500 ? "warn" : "";
    const magnet = i ? `${i.syncs} · ${i.lastSyncError > 0 ? "+" : ""}${i.lastSyncError} (${i.maxSyncError})` : "–";
    const magnetClass = i && i.maxSyncError > 10 ? "warn" : "";
    return `<tr>${cell(u.address)}${cell(u.present ? u.statusName : "not answering", statusClass)}` +
      cell(u.firmware + (i && i.noOffset ? " · no offset!" : ""), i && i.noOffset ? "warn" : "") +
      cell(supply, supplyClass) + cell(magnet, magnetClass) + cell(i ? i.misses : "–", i && i.misses ? "bad" : "") +
      cell(i ? i.boots : "–") + cell(u.i2cErrors, u.i2cErrors > 20 ? "warn" : "") +
      `<td><button data-rehome="${u.address}">Re-home</button> <button data-cal="${u.address}">Calibrate</button></td></tr>`;
  });
  $("unitTable").querySelector("tbody").innerHTML = rows.join("");
}

$("unitTable").addEventListener("click", (event) => {
  const rehome = event.target.dataset.rehome;
  const cal = event.target.dataset.cal;
  if (rehome !== undefined) command(`rehome ${rehome}`);
  if (cal !== undefined) {
    showTab("console");
    consoleSend(`cal ${cal}`);
  }
});
$$("[data-command]").forEach((b) => b.addEventListener("click", () => command(b.dataset.command)));

function renderDevice(state) {
  const facts = [
    ["Firmware", state.version],
    ["Uptime", formatUptime(state.uptime)],
    ["Free memory", `${Math.round(state.heap / 1024)} KB`],
    ["Address", `${state.wifi.ip} (flappy.local)`],
    ["WiFi", state.wifi.connected ? `${state.wifi.rssi} dBm, ${state.wifi.drops} drop(s) since boot` : "not connected"],
    ["Time", state.time.valid ? `${state.time.local} (${state.time.zone})` : "waiting for NTP"],
    ["Last update", `${state.display.lastMoveMs} ms to settle`],
  ];
  (state.printers || []).forEach((p) => facts.push([`Printer ${p.name}`, !p.reachable ? "not reachable"
    : p.state === "printing" ? `printing ${Math.round(p.progress * 100)} %${p.minutesLeft >= 0 ? `, ${p.minutesLeft} min left` : ""}` : p.state]));
  if (state.weather) facts.push(["Weather", `${Math.round(state.weather.now)}° ${state.weather.word}, high ${Math.round(state.weather.high)}, low ${Math.round(state.weather.low)}, rain ${state.weather.rain} %`]);
  if (state.bart) facts.push([`BART ${state.bart.station}`, state.bart.error || state.bart.departures.slice(0, 4).map((d) => `${d.to} ${d.min || "now"}`).join(", ")]);
  $("deviceInfo").innerHTML = facts.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join("");
}

async function pollState() {
  try {
    const state = await api("/api/state");
    lastState = state;
    $("chipOffline").hidden = true;
    renderBoard(state);
    renderChips(state);
    renderUnits(state);
    renderDevice(state);
  } catch (error) {
    $("chipOffline").hidden = false;
  }
}

// ---- Log -----------------------------------------------------------------------------------
let logSeq = 0;
async function pollLog() {
  if (!$("tab-system").classList.contains("active")) return;
  try {
    const data = await api(`/api/log?since=${logSeq}`);
    if (data.lines.length) {
      const out = $("logOut");
      out.textContent += data.lines.map((l) => l.text).join("\n") + "\n";
      if (out.textContent.length > 200000) out.textContent = out.textContent.slice(-150000);
      if ($("logFollow").checked) out.scrollTop = out.scrollHeight;
    }
    logSeq = data.last;
  } catch (error) { /* offline - the chip shows it */ }
}
$("btnCopyLog").addEventListener("click", () => navigator.clipboard.writeText($("logOut").textContent));

// ---- Console (WebSocket) -------------------------------------------------------------------
let socket = null;
let socketRetry = 1000;
const history = [];
let historyIndex = 0;

function consoleWrite(text) {
  const out = $("consoleOut");
  const atBottom = out.scrollHeight - out.scrollTop - out.clientHeight < 40;
  out.textContent += text.replace(/\r/g, "");
  if (out.textContent.length > 200000) out.textContent = out.textContent.slice(-150000);
  if (atBottom) out.scrollTop = out.scrollHeight;
}

function connectConsole() {
  if (socket && socket.readyState <= 1) return;
  socket = new WebSocket(`ws://${location.host}/ws`);
  $("wsStatus").textContent = "connecting…";
  socket.onopen = () => { $("wsStatus").textContent = "connected"; socketRetry = 1000; };
  socket.onmessage = (event) => consoleWrite(event.data);
  socket.onclose = () => {
    $("wsStatus").textContent = "disconnected - retrying";
    setTimeout(connectConsole, socketRetry);
    socketRetry = Math.min(socketRetry * 2, 15000);
  };
}

function consoleSend(line) {
  connectConsole();
  const send = () => socket.send(line);
  if (socket.readyState === 1) send();
  else socket.addEventListener("open", send, { once: true });
}

$("consoleForm").addEventListener("submit", (event) => {
  event.preventDefault();
  const line = $("consoleLine").value;
  consoleSend(line);  // an empty line is an Enter (used by 'cal')
  if (line.trim()) { history.push(line); historyIndex = history.length; }
  $("consoleLine").value = "";
});
$("consoleLine").addEventListener("keydown", (event) => {
  if (event.key === "ArrowUp" && historyIndex > 0) { historyIndex--; $("consoleLine").value = history[historyIndex]; event.preventDefault(); }
  if (event.key === "ArrowDown") {
    historyIndex = Math.min(history.length, historyIndex + 1);
    $("consoleLine").value = history[historyIndex] || "";
    event.preventDefault();
  }
});
$$("[data-console]").forEach((b) => b.addEventListener("click", () => consoleSend(b.dataset.console)));

// ---- Start ---------------------------------------------------------------------------------
loadSettings();
pollState();
setInterval(pollState, 1000);
setInterval(pollLog, 2000);
try { showTab(localStorage.getItem("tab") || "display"); } catch (e) { showTab("display"); }
