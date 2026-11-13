// Flappy web UI. Talks to the JSON API (see firmware/master/src/web.h) and the /ws console.
"use strict";

const $ = (id) => document.getElementById(id);
const $$ = (selector) => Array.from(document.querySelectorAll(selector));

// Train station mode: city -> lines (ids match firmware/master/src/content.h)
const TRAIN_CITIES = {
  "San Francisco Bay Area (BART)": [["bart-red", "Red (Richmond - Millbrae)"], ["bart-yellow", "Yellow (Antioch - Millbrae)"],
    ["bart-blue", "Blue (Dublin - Daly City)"], ["bart-green", "Green (Fremont - Richmond)"], ["bart-orange", "Orange (Richmond - Dublin)"]],
  "New York": [["nyc-7", "7 (Flushing - Hudson Yards)"], ["nyc-1", "1 (Van Cortlandt - South Ferry)"], ["nyc-a", "A (Inwood - Far Rockaway)"]],
  "Chicago": [["cta-red", "CTA Red (Howard - 95th)"], ["cta-blue", "CTA Blue (O'Hare - Forest Park)"]],
  "Paris": [["paris-1", "Metro 1 (La Defense - Vincennes)"], ["paris-14", "Metro 14 (Saint-Denis - Orly)"]],
  "Copenhagen": [["cph-m1", "M1 (Vanlose - Vestamager)"], ["cph-m2", "M2 (Vanlose - Airport)"]],
  "London": [["thameslink", "Thameslink (Bedford - Brighton)"]],
  "Long distance": [["eurostar", "Eurostar (London - Amsterdam)"], ["acela", "Acela (Boston - Washington)"],
    ["california", "California coast (Sacramento - San Diego)"]],
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

$("trainCity").innerHTML = Object.keys(TRAIN_CITIES).map((c) => `<option>${c}</option>`).join("");

function cityOfLine(id) {
  return Object.keys(TRAIN_CITIES).find((c) => TRAIN_CITIES[c].some(([lineId]) => lineId === id));
}

// Show / hide the parts that belong to "random" or "a line", and fill the line list for the city
function updateTrainPanel(lineToSelect) {
  const isLine = $("trainType").value === "line";
  $$("#tab-display .line-only").forEach((e) => { e.hidden = !isLine; });
  $$("#tab-display .random-only").forEach((e) => { e.hidden = isLine; });
  const lines = TRAIN_CITIES[$("trainCity").value] || [];
  $("trainLine").innerHTML = lines.map(([id, name]) => `<option value="${id}">${name}</option>`).join("");
  if (lineToSelect && lines.some(([id]) => id === lineToSelect)) $("trainLine").value = lineToSelect;
  updateTrainPreview();
}
$("trainType").addEventListener("change", () => updateTrainPanel());
$("trainCity").addEventListener("change", () => updateTrainPanel());
$("trainLine").addEventListener("change", () => updateTrainPreview());
$("trainStations").addEventListener("input", () => updateTrainPreview());

// Station list preview (the built-in lists come from the firmware: GET /api/trainlines)
let trainLineData = null;
async function updateTrainPreview() {
  if (!trainLineData) {
    try {
      trainLineData = await api("/api/trainlines");
    } catch (e) {
      $("trainPreviewTitle").textContent = "Stations (couldn't load the list)";
      return;
    }
  }
  let title;
  let stops;
  if ($("trainType").value === "line") {
    const line = trainLineData.lines.find((l) => l.id === $("trainLine").value);
    stops = line ? line.stops : [];
    title = `Stations on this line (${stops.length})`;
  } else {
    const own = $("trainStations").value.split(",").map((s) => s.trim().toUpperCase()).filter((s) => s);
    stops = own.length ? own : trainLineData.random;
    title = `${own.length ? "Your" : "Built-in"} random stations (${stops.length})`;
  }
  $("trainPreviewTitle").textContent = title;
  $("trainPreviewList").innerHTML = "";
  stops.forEach((stop) => {
    const li = document.createElement("li");
    li.textContent = stop;
    $("trainPreviewList").appendChild(li);
  });
}

function toDateInput(unix) {
  if (!unix) return "";
  const d = new Date(unix * 1000);
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

function fillForm(s) {
  setMode(s.mode);
  setAlign(s.alignment);
  $("text").value = s.text;
  $("clockHours").value = s.clockHours;
  $("clockLeadingZero").checked = s.clockLeadingZero;
  $("clockDay").value = s.clockDay;
  updateClockPreview();
  $("dateFormat").value = s.dateFormat;
  $("countdownDate").value = toDateInput(s.countdownTo);
  $("trainType").value = s.trainType === "bart" ? "line" : s.trainType;  // "bart" = older settings
  $("trainCity").value = cityOfLine(s.trainLine) || Object.keys(TRAIN_CITIES)[0];
  updateTrainPanel(s.trainLine);
  $("trainSeconds").value = s.trainSeconds;
  $("trainStations").value = s.trainStations;
  $("phrases").value = s.phrases;
  $("phraseMinSeconds").value = s.phraseMinSeconds;
  $("phraseMaxSeconds").value = s.phraseMaxSeconds;
  if (!numberSaveTimer) {  // don't throw away an edit that is about to be saved
    numberLists = Object.assign({}, s.numberLists || {});
    loadNumberList(s.numberList || "General");
  }
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
  $("weatherWhere").textContent = `${s.weatherPlace} (${s.weatherLat.toFixed(2)}, ${s.weatherLon.toFixed(2)})`;
  $("weatherPick").textContent = "";
  $("weatherResultsBox").hidden = true;
  $("weatherFahrenheit").checked = s.weatherFahrenheit;
  $("weatherSeconds").value = s.weatherSeconds;
  // BART
  $("bartStation").value = s.bartStation;
  $("bartDirection").value = s.bartDirection;
  $("bartSeconds").value = s.bartSeconds;
  $("bartKey").value = s.bartKey;
  // Printers
  renderPrinters(s.printers || []);
  const printerRule = (s.sources && s.sources.printers) || {};
  $("printerNotify").checked = !!printerRule.enabled;
  $("printerNotifySeconds").value = printerRule.showSeconds || 300;
  $("printerDoneRepeatCount").value = s.printerDoneRepeatCount;
  $("printerDoneRepeatMinutes").value = s.printerDoneRepeatMinutes;
  $("printerProgressStep").value = s.printerProgressStep;
  $("printerProgressSeconds").value = s.printerProgressSeconds;
  $("printerProgressPriority").value = s.printerProgressPriority;
  // Wave
  $("waveStyle").value = s.waveStyle;
  $("waveOrder").value = s.waveOrder;
  $("waveMs").value = s.waveMs;
  $("quietPowerSave").checked = s.quietPowerSave;
  $("quietRehome").checked = s.quietRehome;
  $("waveMsValue").textContent = `${$("waveMs").value} ms`;
  const known = TIMEZONES.find(([name, tz]) => tz === s.timezone);
  $("timezoneName").value = known ? known[1] : "custom";
  renderSchedule(s.scheduled || []);
  renderPopups(s.sources || {});
}

// Same rules as layout::clockText() in the firmware (text_layout.cpp)
function clockText(date, hours, zero, day, width) {
  const h24 = hours === "24";
  const h = h24 ? date.getHours() : (date.getHours() % 12 || 12);
  const pad = (n) => (zero ? String(n).padStart(2, "0") : String(n));
  const time = `${pad(h)}:${String(date.getMinutes()).padStart(2, "0")}${hours === "12ampm" ? (date.getHours() < 12 ? "AM" : "PM") : ""}`;
  const d = date.getDate();
  let prefix = "";
  let gap = " ";
  if (day === "number") { prefix = pad(d); gap = "  "; }
  if (day === "ordinal") {
    const ending = d % 100 >= 11 && d % 100 <= 13 ? "TH" : ({ 1: "ST", 2: "ND", 3: "RD" }[d % 10] || "TH");
    prefix = `${d}${ending}`;
  }
  if (day === "weekday") prefix = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"][date.getDay()];
  if (!prefix) return time;
  for (const g of [gap, " ", ""]) if ((prefix + g + time).length <= width) return prefix + g + time;
  return prefix + time;
}

function updateClockPreview() {
  const width = settings ? settings.unitCount : 10;
  const text = clockText(new Date(), $("clockHours").value, $("clockLeadingZero").checked, $("clockDay").value, width);
  $("clockPreview").textContent = `[${text.padEnd(width)}]`;
}
["clockHours", "clockDay", "clockLeadingZero"].forEach((id) => $(id).addEventListener("change", updateClockPreview));

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
  const patch = { mode: selectedMode, alignment: selectedAlign, speed: n("speed"), lineSeconds: n("lineSeconds"),
    waveStyle: $("waveStyle").value, waveOrder: $("waveOrder").value, waveMs: n("waveMs") };
  switch (selectedMode) {
    case "text": patch.text = $("text").value.toUpperCase(); break;
    case "clock":
      Object.assign(patch, { clockHours: $("clockHours").value, clockLeadingZero: $("clockLeadingZero").checked,
        clockDay: $("clockDay").value });
      break;
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
      Object.assign(patch, {
        sources: { printers: { enabled: $("printerNotify").checked, showSeconds: n("printerNotifySeconds") } },
        printerDoneRepeatCount: n("printerDoneRepeatCount"), printerDoneRepeatMinutes: n("printerDoneRepeatMinutes"),
        printerProgressStep: n("printerProgressStep"), printerProgressSeconds: n("printerProgressSeconds"),
        printerProgressPriority: $("printerProgressPriority").value,
      });
      break;
    case "famousnumbers":
      Object.assign(patch, { numbers: numbersFromTable(), numberMinSeconds: n("numberMinSeconds"),
        numberMaxSeconds: n("numberMaxSeconds"), numberNameSeconds: n("numberNameSeconds") });
      break;
  }
  save(patch, $("applyStatus"));
});

// ---- Famous numbers editor ------------------------------------------------------------------
// [number, name] - each at most 10 characters, only characters that are on the flaps
const NUMBER_PRESETS = {
  "General": [["3.14159", "PI"], ["2.71828", "EULER E"], ["1.61803", "GOLDEN"], ["2.9979 E 8", "LIGHT C"],
    ["6.022 E 23", "AVOGADRO"], ["9.80665", "GRAVITY G"], ["6.626 E-34", "PLANCK H"], ["-273.15", "ABS ZERO"],
    ["1.41421", "SQRT 2"], ["1.380 E-23", "BOLTZMANN"], ["6.674 E-11", "GRAV CONST"], ["42", "ANSWER"]],
  "Mathematics": [["3.14159", "PI"], ["6.28319", "TAU"], ["2.71828", "E"], ["1.61803", "GOLDEN"],
    ["1.41421", "SQRT 2"], ["1.73205", "SQRT 3"], ["0.69315", "LN 2"], ["0.57721", "EULER GAM"],
    ["0.91597", "CATALAN"], ["1.20206", "APERY"], ["2.68545", "KHINCHIN"], ["4.66920", "FEIGENBAUM"],
    ["2.41421", "SILVER"], ["1.32472", "PLASTIC"]],
  "Quantum mechanics": [["6.626 E-34", "PLANCK H"], ["1.055 E-34", "H BAR"], ["0.0072974", "FINE STRUC"],
    ["137.036", "1 OVER ALF"], ["9.109 E-31", "ELECTRON"], ["1.673 E-27", "PROTON"], ["1.675 E-27", "NEUTRON"],
    ["1.602 E-19", "CHARGE E"], ["5.292 E-11", "BOHR RAD"], ["1.097 E 7", "RYDBERG"], ["2.426 E-12", "COMPTON"],
    ["9.274 E-24", "BOHR MAG"], ["511 KEV", "ELECTRON"]],
  "Thermodynamics": [["1.380 E-23", "BOLTZMANN"], ["8.314", "GAS CONST"], ["-273.15", "ABS ZERO"],
    ["273.16", "TRIPLE PT"], ["5.670 E-8", "STEFAN BOL"], ["2.898 E-3", "WIEN"], ["6.022 E 23", "AVOGADRO"],
    ["4.184", "CALORIE J"], ["101325", "ATMOSPHERE"], ["100", "WATER BOIL"], ["2.7255", "CMB KELVIN"]],
  "Electromagnetism": [["2.9979 E 8", "LIGHT C"], ["1.602 E-19", "CHARGE E"], ["8.854 E-12", "EPSILON 0"],
    ["1.2566 E-6", "MU 0"], ["8.988 E 9", "COULOMB K"], ["96485", "FARADAY"], ["376.73", "VACUUM Z"],
    ["25812.8", "VON KLITZ"], ["2.068 E-15", "FLUX QUANT"]],
  "Astronomy": [["1.496 E 11", "AU METERS"], ["9.461 E 15", "LIGHT YEAR"], ["3.086 E 16", "PARSEC"],
    ["5.972 E 24", "EARTH KG"], ["1.989 E 30", "SUN KG"], ["6371", "EARTH KM"], ["384400", "MOON KM"],
    ["6.674 E-11", "GRAV CONST"], ["70", "HUBBLE"], ["13.8", "UNIVERSE"], ["365.256", "YEAR DAYS"]],
  "Chemistry": [["6.022 E 23", "AVOGADRO"], ["8.314", "GAS CONST"], ["96485", "FARADAY"], ["22.414", "MOLAR VOL"],
    ["1.008", "HYDROGEN"], ["12.011", "CARBON"], ["15.999", "OXYGEN"], ["18.015", "WATER"], ["7", "NEUTRAL PH"],
    ["1.660 E-27", "ATOMIC U"]],
};

function flapOk(text) {
  return text.length <= 10 && /^[A-Z0-9 $&#:.\-?!]*$/.test(text.toUpperCase());
}

function numberRow(number = "", name = "") {
  const tr = document.createElement("tr");
  tr.innerHTML = '<td><input type="text" data-k="number" maxlength="10"></td>' +
    '<td><input type="text" data-k="name" maxlength="10"></td>' +
    '<td><button type="button" title="Remove">&times;</button></td>';
  tr.querySelector('[data-k="number"]').value = number;
  tr.querySelector('[data-k="name"]').value = name;
  tr.querySelectorAll("input").forEach((input) => {
    const check = () => input.classList.toggle("invalid", !flapOk(input.value));
    input.addEventListener("input", check);
    check();
  });
  tr.querySelector("button").addEventListener("click", () => tr.remove());
  return tr;
}

function renderNumbers(pairs) {
  const body = $("numberTable").querySelector("tbody");
  body.innerHTML = "";
  pairs.forEach(([number, name]) => body.appendChild(numberRow(number, name)));
  for (let i = pairs.length; i < 3; i++) body.appendChild(numberRow());  // a few empty rows to type into
}

// settings.numbers is "number,name,number,name,..."
function parseNumbers(text) {
  const parts = (text || "").split(",").map((p) => p.trim()).filter((p) => p.length);
  const pairs = [];
  for (let i = 0; i + 1 < parts.length; i += 2) pairs.push([parts[i], parts[i + 1]]);
  return pairs;
}

function numbersFromTable() {
  return $$("#numberTable tbody tr")
    .map((tr) => [tr.querySelector('[data-k="number"]').value.trim().toUpperCase(),
      tr.querySelector('[data-k="name"]').value.trim().toUpperCase()])
    .filter(([number, name]) => number && name)
    .map(([number, name]) => `${number.replace(/,/g, "")},${name.replace(/,/g, "")}`)
    .join(",");
}

// Lists: the built-in presets, plus the user's edited versions kept on the device
// (settings.numberLists, only for lists that differ from their preset). "random" is a random mix
// that is saved as soon as it's drawn, so it stays the same until "Reset" draws a new one.
let numberLists = {};
let currentList = "General";
let numberSaveTimer = null;

const pairsText = (pairs) => pairs.map(([number, name]) => `${number},${name}`).join(",");

function randomPairs() {
  const seen = new Set();
  const all = Object.values(NUMBER_PRESETS).flat().filter(([, name]) => !seen.has(name) && seen.add(name));
  for (let i = all.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [all[i], all[j]] = [all[j], all[i]];
  }
  return all.slice(0, 15);
}

function updateNumberReset() {
  $("btnNumberReset").hidden = !(currentList in numberLists);
  $("btnNumberReset").textContent = currentList === "random" ? "Draw a new random mix" : "Reset to default";
}

function saveNumberLists() {
  clearTimeout(numberSaveTimer);
  numberSaveTimer = null;
  return api("/api/settings", { body: { numberList: currentList, numberLists } })
    .then(() => { $("numberSaved").textContent = "saved"; setTimeout(() => { $("numberSaved").textContent = ""; }, 1500); })
    .catch((e) => { $("numberSaved").textContent = e.message; });
}

// Called after every edit: remember this list's table, a moment after typing stops
function numbersEdited() {
  const text = numbersFromTable();
  const preset = currentList === "random" ? null : pairsText(NUMBER_PRESETS[currentList] || []);
  if (preset !== null && text === preset) delete numberLists[currentList];
  else numberLists[currentList] = text;
  updateNumberReset();
  clearTimeout(numberSaveTimer);
  numberSaveTimer = setTimeout(saveNumberLists, 1200);
}

function loadNumberList(name) {
  currentList = name in NUMBER_PRESETS || name === "random" ? name : "General";
  $("numberPreset").value = currentList;
  if (currentList in numberLists) {
    renderNumbers(parseNumbers(numberLists[currentList]));
  } else if (currentList === "random") {
    const pairs = randomPairs();
    renderNumbers(pairs);
    numberLists.random = pairsText(pairs);
    saveNumberLists();
  } else {
    renderNumbers(NUMBER_PRESETS[currentList]);
  }
  updateNumberReset();
}

$("numberPreset").innerHTML = Object.keys(NUMBER_PRESETS).map((k) => `<option>${k}</option>`).join("") +
  '<option value="random">Random mix (15 from all lists)</option>';
$("numberPreset").addEventListener("change", async () => {
  if (numberSaveTimer) await saveNumberLists();  // keep edits to the list we're leaving
  loadNumberList($("numberPreset").value);
  if (!numberSaveTimer) saveNumberLists();       // remember which list is picked
});
$("btnNumberReset").addEventListener("click", () => {
  delete numberLists[currentList];
  loadNumberList(currentList);  // random: draws a new mix
  saveNumberLists();
});
$("numberTable").addEventListener("input", numbersEdited);
$("numberTable").addEventListener("click", (event) => { if (event.target.tagName === "BUTTON") numbersEdited(); });
$("btnAddNumber").addEventListener("click", () => {
  const row = numberRow();
  $("numberTable").querySelector("tbody").appendChild(row);
  row.querySelector("input").focus();
});

// ---- Pop-ups tab ---------------------------------------------------------------------------
const SOURCE_NAMES = {
  printers: "3D printers (print done / progress)", notify: "Messages (Home Assistant, Siri, API)",
  scheduled: "Scheduled messages", weather: "Weather", bart: "BART departures", date: "Date",
  countdown: "Countdown", randomphrase: "Random phrase", famousnumbers: "Famous numbers", trainstation: "Train station board",
};
const EVENT_ONLY = ["notify", "scheduled"];
const DAY_LETTERS = ["S", "M", "T", "W", "T", "F", "S"];

// Hours a timer pop-up may run: "All day" (from/to empty), or a from-to window
// Suggested hours for a time window: the hours outside quiet hours, or 7am - 9pm
function defaultHours() {
  if (settings && settings.quietEnabled) return [settings.quietTo, settings.quietFrom];
  return ["07:00", "21:00"];
}

// "All day" ticked: the time boxes show the suggested window, greyed out. Unticked: editable.
function hoursCell(r) {
  const allDay = !r.from || !r.to || r.from === r.to;
  const [from, to] = allDay ? defaultHours() : [r.from, r.to];
  const off = allDay ? " disabled" : "";
  return `<label class="inline"><input type="checkbox" data-k="allDay" ${allDay ? "checked" : ""}> All day</label>` +
    `<span class="window"><input type="time" data-k="from" value="${from}"${off}>` +
    `<span class="dash">-</span><input type="time" data-k="to" value="${to}"${off}></span>`;
}

$("popupTable").addEventListener("change", (event) => {
  if (event.target.dataset.k !== "allDay") return;
  event.target.closest("td").querySelectorAll('input[type="time"]').forEach((t) => { t.disabled = event.target.checked; });
});

function renderPopups(sources) {
  const rows = Object.entries(SOURCE_NAMES).map(([id, label]) => {
    const r = sources[id] || {};
    const timer = !EVENT_ONLY.includes(id);
    const days = DAY_LETTERS.map((d, i) =>
      `<label class="day"><input type="checkbox" data-day="${i}" ${(r.days >> i) & 1 ? "checked" : ""}>${d}</label>`).join("");
    const extra = id === "bart"
      ? `<div class="sub"><label class="inline">also when a train is <input type="number" data-k="trainMinutes" min="0" max="60" value="${r.trainMinutes || 0}"> min away (0 = off)</label></div>`
      : "";
    const priorities = ["low", "normal", "high"].map((p) => `<option ${r.priority === p ? "selected" : ""}>${p}</option>`).join("");
    return `<tr data-source="${id}"><td>${label}${extra}</td>` +
      `<td><input type="checkbox" data-k="enabled" ${r.enabled ? "checked" : ""}></td>` +
      `<td><select data-k="priority">${priorities}</select></td>` +
      `<td><input type="number" data-k="showSeconds" min="3" max="7200" value="${r.showSeconds || 60}"></td>` +
      `<td>${timer ? `<input type="number" data-k="everyMinutes" min="0" max="1440" value="${r.everyMinutes || 0}">` : "event"}</td>` +
      `<td class="hours">${timer ? hoursCell(r) : ""}</td>` +
      `<td class="days">${timer ? days : ""}</td></tr>`;
  });
  $("popupTable").querySelector("tbody").innerHTML = rows.join("");
}

$("btnPopups").addEventListener("click", () => {
  const sources = {};
  $$("#popupTable tbody tr").forEach((tr) => {
    const get = (k) => tr.querySelector(`[data-k="${k}"]`);
    const r = { enabled: get("enabled").checked, priority: get("priority").value, showSeconds: Number(get("showSeconds").value) };
    if (get("everyMinutes")) r.everyMinutes = Number(get("everyMinutes").value);
    if (get("allDay")) {
      const allDay = get("allDay").checked || !get("from").value || !get("to").value;
      r.from = allDay ? "" : get("from").value;
      r.to = allDay ? "" : get("to").value;
    }
    const dayBoxes = tr.querySelectorAll("[data-day]");
    if (dayBoxes.length) r.days = Array.from(dayBoxes).reduce((m, b) => m | (b.checked ? 1 << Number(b.dataset.day) : 0), 0);
    if (get("trainMinutes")) r.trainMinutes = Number(get("trainMinutes").value);
    sources[tr.dataset.source] = r;
  });
  save({ sources }, $("popupStatus"));
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
      $("weatherResultsBox").hidden = false;
      $("weatherResults").innerHTML = results.length
        ? results.map((p, i) => `<option value="${i}">${p.name}, ${p.admin1 || ""} ${p.country_code || ""}</option>`).join("")
        : '<option value="">No city found - try another spelling</option>';
      $("weatherResults")._results = results;
      if (results.length) pickPlace(0);
    } catch (e) {
      $("weatherPick").textContent = "The city search needs internet access from this browser.";
    }
  }, 400);
});
$("weatherResults").addEventListener("change", () => pickPlace(Number($("weatherResults").value)));
function pickPlace(i) {
  const p = ($("weatherResults")._results || [])[i];
  if (!p) return;
  weatherPick = { lat: p.latitude, lon: p.longitude, place: p.name };
  $("weatherPick").textContent = `Selected ${p.name} (${p.latitude.toFixed(2)}, ${p.longitude.toFixed(2)}) - press "Show on display" to use it.`;
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
    const repeat = m.repeatMinutes && m.repeatCount ? ` · then every ${m.repeatMinutes} min, ${m.repeatCount} more` : "";
    const show = m.showSeconds ? ` · ${m.showSeconds} s` : "";
    li.innerHTML = `<div><strong></strong><div class="when">${when}${m.indefinitely ? " · keeps showing" : show + repeat}</div></div>`;
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
    await api("/api/message", { body: { text, at, indefinitely: $("schedForever").checked,
      showSeconds: Number($("schedShow").value) || 0, repeatMinutes: Number($("schedRepeatMinutes").value) || 0,
      repeatCount: Number($("schedRepeatCount").value) || 0 } });
    flash($("schedStatus"), "Scheduled");
    $("schedText").value = "";
    setTimeout(loadSettings, 400);
  } catch (error) {
    flash($("schedStatus"), error.message, false);
  }
});

$("btnQuiet").addEventListener("click", () => save({
  quietEnabled: $("quietEnabled").checked, quietFrom: $("quietFrom").value, quietTo: $("quietTo").value,
  quietBlank: $("quietBlank").checked, quietPowerSave: $("quietPowerSave").checked,
  quietRehome: $("quietRehome").checked,
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
  if (state.weather && state.weather.valid) facts.push(["Weather", `${Math.round(state.weather.now)}° ${state.weather.word}, high ${Math.round(state.weather.high)}, low ${Math.round(state.weather.low)}, rain ${state.weather.rain} %`]);
  if (state.bart) facts.push([`BART ${state.bart.station}`, state.bart.error || state.bart.departures.slice(0, 4).map((d) => `${d.to} ${d.min || "now"}`).join(", ")]);
  $("deviceInfo").innerHTML = facts.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join("");
}

// ---- Weather panel: latest data and the download button --------------------------------------
function agoText(seconds) {
  if (seconds < 90) return `${seconds} s ago`;
  if (seconds < 5400) return `${Math.round(seconds / 60)} min ago`;
  return `${Math.round(seconds / 3600)} h ago`;
}

function renderWeatherLatest(state) {
  const w = state.weather;
  let text;
  if (!w || !w.attempts) {
    text = "No weather has been downloaded since the last boot.";
  } else if (w.valid) {
    text = `Latest: ${Math.round(w.now)}° ${w.word}, high ${Math.round(w.high)}, low ${Math.round(w.low)}, rain ${w.rain} % - downloaded ${agoText(w.ageS)}.`;
    if (w.error) text += ` The last attempt (${agoText(w.attemptAgeS)}) failed: ${w.error}`;
  } else {
    text = `No weather data yet. The last attempt (${agoText(w.attemptAgeS)}) failed: ${w.error || "no answer"}`;
  }
  $("weatherLatest").textContent = text;
  return w;
}

$("btnWeatherRefresh").addEventListener("click", async () => {
  const button = $("btnWeatherRefresh");
  const status = $("weatherStatus");
  const before = (lastState && lastState.weather && lastState.weather.attempts) || 0;
  button.disabled = true;
  status.className = "status";
  status.textContent = "Downloading...";
  try {
    await api("/api/weather/refresh", { body: {} });
    // Wait for the display to try (a failed HTTPS attempt can take ~15 s)
    for (let i = 0; i < 30; i++) {
      await new Promise((resolve) => setTimeout(resolve, 1500));
      const state = await api("/api/state");
      lastState = state;
      renderWeatherLatest(state);
      const w = state.weather;
      if (w && w.attempts > before) {
        if (w.error) flash(status, "Download failed (details above)", false);
        else flash(status, "Done");
        button.disabled = false;
        return;
      }
    }
    flash(status, "Still waiting - the display hasn't answered yet", false);
  } catch (error) {
    flash(status, error.message, false);
  }
  button.disabled = false;
});

async function pollState() {
  try {
    const state = await api("/api/state");
    lastState = state;
    $("chipOffline").hidden = true;
    renderBoard(state);
    renderWeatherLatest(state);
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
// Poll gently: every 5 s, and not at all while the tab is hidden (lets the ESP32 save power)
loadSettings();
pollState();
setInterval(() => { if (!document.hidden) pollState(); }, 5000);
setInterval(() => { if (!document.hidden) pollLog(); }, 3000);
document.addEventListener("visibilitychange", () => { if (!document.hidden) pollState(); });
try { showTab(localStorage.getItem("tab") || "display"); } catch (e) { showTab("display"); }
