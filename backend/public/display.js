const STORAGE_KEY = "plane-display-simulator/v1";
const POLL_INTERVAL_MS = 60_000;
const ROTATE_INTERVAL_MS = 8_000;
const AIRCRAFT_PLACEHOLDER = "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 72 72"><rect width="72" height="72" rx="12" fill="#e9fff0"/><path fill="#087c70" d="M36 8c-3 0-4.8 2.2-4.8 5.2v20L10 42v6l21.2-3.4v14.5l-7.1 7.8v2.8L36 67l11.9 2.7v-2.8l-7.1-7.8V44.6L62 48v-6L40.8 33.2v-20C40.8 10.2 39 8 36 8z"/></svg>');
// Add future airline palettes here. Unknown carriers always use the default display green.
const BRAND_THEMES = [
  { airlines: ["WJA", "WEN"], name: "westjet", colors: { accent: "#00AC9D", light: "#72e6dc", bright: "#d2fffa", muted: "#4fbab1", deep: "#092d2a", glow: "rgb(0 172 157 / .32)" } },
  { airlines: ["ACA", "JZA"], name: "air-canada", colors: { accent: "#F01428", light: "#ff9ca5", bright: "#ffe0e3", muted: "#db6570", deep: "#3a0c12", glow: "rgb(240 20 40 / .32)" } },
];
const DEFAULT_THEME = { name: "default", colors: { accent: "#a5f9b8", light: "#aaf4be", bright: "#d7ffe2", muted: "#75bd8a", deep: "#1d3828", glow: "rgb(148 255 183 / .3)" } };
const $ = selector => document.querySelector(selector);
let state = loadState();
let flightIndex = 0;
let displayedFlight = null;
let refreshPending = false;
let pollTimer;
let rotateTimer;
let airlineLogoCodes = new Set();

function loadState() {
  try { return JSON.parse(localStorage.getItem(STORAGE_KEY)) || { deviceKey: window.__LOCAL_DEMO_KEY__ || "", deviceConfig: null, payload: null, events: [] }; }
  catch { return { deviceKey: window.__LOCAL_DEMO_KEY__ || "", deviceConfig: null, payload: null, events: [] }; }
}
function saveState() { localStorage.setItem(STORAGE_KEY, JSON.stringify(state)); }
function maskedKey(value) { return value ? `${value.slice(0, 6)}…${value.slice(-4)}` : null; }
function setConnection(label, kind = "ready") { const node = $("#connection"); node.textContent = label; node.dataset.state = kind; }
function addEvent(message) { state.events = [{ at: new Date().toLocaleTimeString(), message }, ...state.events].slice(0, 12); saveState(); renderDebug(); }
function authHeaders() { return { "X-Device-Key": state.deviceKey }; }
function receivedFlights() { return Array.isArray(state.payload?.flights) ? state.payload.flights : []; }
function flights() { return state.forceEmptySky ? [] : receivedFlights(); }
function isSpecialFlight(flight) { return Boolean(flight?.specialFlight || flight?.isTrackedFlight); }
function specialFlights() { return flights().filter(isSpecialFlight); }
function cyclingFlights() { const special = specialFlights(); return special.length ? special : flights(); }
function sameFlight(first, second) {
  if (!first || !second) return false;
  return (first.icao24 && first.icao24 === second.icao24)
    || (first.flightNumber && first.flightNumber === second.flightNumber)
    || (first.registration && first.registration === second.registration);
}
function useAircraftPlaceholder() {
  const logo = $("#airlineLogo");
  logo.onerror = null;
  logo.src = AIRCRAFT_PLACEHOLDER;
  logo.alt = "Aircraft";
}
function renderLogo(flight) {
  const logo = $("#airlineLogo");
  const code = flight?.airline?.toUpperCase();
  if (!code || !airlineLogoCodes.has(code)) { useAircraftPlaceholder(); return; }
  logo.src = `/logos/airlines/${code}.png`;
  logo.alt = `${flight.airlineName || flight.registeredOwner || code} logo`;
  logo.onerror = useAircraftPlaceholder;
}
function applyBrandTheme(flight) {
  const code = flight?.airline?.toUpperCase();
  const theme = BRAND_THEMES.find(candidate => candidate.airlines.includes(code)) || DEFAULT_THEME;
  const screen = $(".screen");
  screen.dataset.brand = theme.name;
  for (const [name, value] of Object.entries(theme.colors)) screen.style.setProperty(`--brand-${name}`, value);
}
function telemetry(value, suffix, { signed = false, decimals = 0 } = {}) {
  if (!Number.isFinite(value)) return "—";
  const rounded = Number(value.toFixed(decimals));
  const prefix = signed && rounded > 0 ? "+" : "";
  return `${prefix}${rounded.toLocaleString()}${suffix}`;
}
function formatStatusTime(value) {
  const match = typeof value === "string" && value.match(/(?:T|\s)(\d{2}):(\d{2})/);
  if (!match) return null;
  const hours = Number(match[1]), minutes = match[2];
  return `${((hours + 11) % 12) + 1}:${minutes} ${hours >= 12 ? "PM" : "AM"}`;
}
function statusText(status) {
  if (!status) return null;
  const eta = formatStatusTime(status.estimatedArrival || status.scheduledArrival);
  const delay = Number.isFinite(status.arrivalDelayMinutes) && status.arrivalDelayMinutes > 0 ? `${status.arrivalDelayMinutes} MIN LATE` : null;
  if (eta && delay) return `ETA ${eta} LOCAL · ${delay}`;
  if (eta) return `ETA ${eta} LOCAL`;
  if (delay) return delay;
  return null;
}

function render() {
  const currentFlights = flights();
  const currentSpecialFlights = specialFlights();
  const specialMode = currentSpecialFlights.length > 0;
  const availableFlights = specialMode ? currentSpecialFlights : currentFlights;
  const showingIdleFlight = Boolean(state.forceEmptySky || (state.deviceKey && state.payload && !currentFlights.length));
  $("#savedName").textContent = state.deviceConfig?.name || "Unconfigured display";
  $("#identityForm").elements.deviceKey.value = state.deviceKey;
  $("#forceEmptySky").checked = Boolean(state.forceEmptySky);
  $(".screen").classList.toggle("is-idle", showingIdleFlight);
  for (const node of [$("#screenLabel"), $("#flightCard"), $("#cycleStatus")]) {
    node.hidden = false;
    node.toggleAttribute("aria-hidden", showingIdleFlight);
  }
  $("#idleFlight").hidden = !showingIdleFlight;
  const displayedMatchesMode = displayedFlight && isSpecialFlight(displayedFlight) === specialMode;
  if (!displayedMatchesMode || !availableFlights.some(flight => sameFlight(flight, displayedFlight))) {
    flightIndex = 0;
    displayedFlight = availableFlights[flightIndex] || null;
    refreshPending = false;
  }
  applyBrandTheme(showingIdleFlight ? null : displayedFlight);
  $(".screen").classList.toggle("is-special", Boolean(!showingIdleFlight && isSpecialFlight(displayedFlight)));
  if (!displayedFlight) {
    $("#screenLabel").textContent = state.deviceKey ? "NO AIRCRAFT IN RANGE" : "WAITING FOR DEVICE KEY";
    $("#flightNumber").textContent = "—";
    $("#flightNumber").dataset.kind = "none";
    $("#company").textContent = "—";
    $("#aircraftType").textContent = "—";
    $("#registration").textContent = "—";
    $("#heading").textContent = "—";
    $("#altitude").textContent = "—";
    $("#speed").textContent = "—";
    $("#verticalSpeed").textContent = "—";
    $("#flightStatus").hidden = true;
    renderLogo(null);
    $("#route").textContent = state.deviceKey ? "Try a refresh in a moment" : "Add a device key below";
    $("#progressBar").style.width = "0%";
    $("#progressText").textContent = "—";
    $("#cycleStatus").textContent = state.payload ? "0 nearby aircraft" : "No flight data yet";
  } else {
    const flight = displayedFlight;
    const progress = Number.isFinite(flight.flightProgressPercent) ? Math.max(0, Math.min(100, flight.flightProgressPercent)) : null;
    const standardLabel = flight.isTrackedFlight ? `SPECIAL FLIGHT · ${flight.trackedFlightTitle || "TRACKED"}` : "NEARBY AIRCRAFT";
    const primaryKind = flight.flightNumber ? "flight" : flight.registration ? "registration" : "unknown";
    $("#screenLabel").textContent = primaryKind === "registration" ? `${standardLabel} · REGISTRATION` : standardLabel;
    $("#flightNumber").textContent = flight.flightNumber || flight.registration || "UNKNOWN";
    $("#flightNumber").dataset.kind = primaryKind;
    $("#company").textContent = flight.airlineName || flight.registeredOwner || flight.airline || "OPERATOR UNAVAILABLE";
    $("#aircraftType").textContent = flight.aircraftTypeName || flight.aircraftType || "AIRCRAFT TYPE UNAVAILABLE";
    $("#registration").textContent = flight.registration || "—";
    $("#heading").textContent = telemetry(flight.headingDeg, "°");
    $("#altitude").textContent = telemetry(flight.altFt, " FT");
    $("#speed").textContent = telemetry(flight.speedKt, " KTS");
    $("#verticalSpeed").textContent = telemetry(flight.verticalSpeedFpm, " F/M", { signed: true });
    renderLogo(flight);
    $("#route").textContent = flight.origin && flight.destination ? `${flight.origin} → ${flight.destination}` : "ROUTE UNAVAILABLE";
    const flightStatus = statusText(flight.flightStatus);
    $("#flightStatus").textContent = flightStatus || "";
    $("#flightStatus").hidden = !flightStatus;
    $("#progressBar").style.width = `${progress ?? 0}%`;
    const progressLabel = flight.flightProgressEstimated ? "ROUTE PROGRESS (EST.)" : "FLIGHT PROGRESS";
    $("#progressText").textContent = progress === null ? `${progressLabel} —` : `${progressLabel} ${Math.round(progress)}%`;
    const refreshNote = refreshPending ? " · refreshed" : " · changes every 8 sec";
    $("#cycleStatus").textContent = specialMode
      ? `${Math.min(flightIndex + 1, currentSpecialFlights.length)} / ${currentSpecialFlights.length} SPECIAL ${currentSpecialFlights.length === 1 ? "FLIGHT" : "FLIGHTS"} · LIVE`
      : `${Math.min(flightIndex + 1, currentFlights.length)} / ${currentFlights.length}${refreshNote}`;
  }
  renderDebug();
}

async function loadLogoRegistry() {
  try {
    const response = await fetch("/airline-logos.json");
    if (!response.ok) throw new Error("Logo registry unavailable");
    airlineLogoCodes = new Set((await response.json()).codes || []);
    render();
  } catch { addEvent("Airline-logo registry unavailable; using text fallback"); }
}

function renderDebug() {
  const safeConfig = { deviceKey: maskedKey(state.deviceKey), deviceConfig: state.deviceConfig, simulator: { forceEmptySky: Boolean(state.forceEmptySky) }, lastPayload: state.payload ? { generatedAt: state.payload.generatedAt, source: state.payload.source, receivedFlights: receivedFlights().length, displayedFlights: flights().length } : null, pollIntervalSeconds: 60, rotationIntervalSeconds: 8 };
  $("#savedConfig").textContent = JSON.stringify(safeConfig, null, 2);
  $("#eventLog").replaceChildren(...state.events.map(event => { const item = document.createElement("li"); item.textContent = `${event.at} — ${event.message}`; return item; }));
}

async function syncConfig() {
  const response = await fetch("/api/device/config", { headers: authHeaders() });
  if (!response.ok) throw new Error(response.status === 401 ? "Device key was rejected" : `Config request failed (${response.status})`);
  state.deviceConfig = await response.json();
  if (state.deviceConfig.rotateKey) {
    addEvent("Key rotation requested; getting replacement key");
    const rotation = await fetch("/api/device/rotate", { method: "POST", headers: authHeaders() });
    if (!rotation.ok) throw new Error("Key rotation failed");
    state.deviceKey = (await rotation.json()).deviceKey;
    state.deviceConfig = await (await fetch("/api/device/config", { headers: authHeaders() })).json();
    addEvent("Device key rotated and saved locally");
  }
  saveState();
}

async function poll() {
  if (!state.deviceKey) { setConnection("OFFLINE", "error"); render(); return; }
  setConnection("UPDATING", "working");
  try {
    await syncConfig();
    const response = await fetch("/flightdata", { headers: authHeaders() });
    if (!response.ok) throw new Error(`Flight request failed (${response.status})`);
    const wasDisplayingFlight = Boolean(displayedFlight);
    state.payload = await response.json();
    if (wasDisplayingFlight) refreshPending = true;
    else if (flights().length) { flightIndex = 0; displayedFlight = flights()[0]; }
    saveState();
    addEvent(`Updated: ${flights().length} aircraft from ${state.payload.source || "source"}`);
    setConnection("ONLINE");
  } catch (error) {
    addEvent(error instanceof Error ? error.message : "Update failed");
    setConnection("OFFLINE", "error");
  }
  render();
}

function advanceFlight({ useRefreshedFirst = false } = {}) {
  const available = cyclingFlights();
  if (!available.length) { displayedFlight = null; refreshPending = false; render(); return; }
  if (useRefreshedFirst && refreshPending) flightIndex = 0;
  else flightIndex = (flightIndex + 1) % available.length;
  displayedFlight = available[flightIndex];
  refreshPending = false;
  render();
}

async function createPairing() {
  if (!state.deviceKey) { addEvent("Add a device key before creating a setup link"); return; }
  const dialog = $("#pairingDialog");
  const status = $("#pairingStatus");
  const link = $("#pairLink");
  link.value = "";
  status.textContent = "Creating a secure link…";
  if (!dialog.open) dialog.showModal();
  try {
    const response = await fetch("/api/device/pairing", { method: "POST", headers: authHeaders() });
    if (!response.ok) throw new Error(`Couldn’t create setup link (${response.status})`);
    const pairing = await response.json();
    link.value = pairing.pairUrl;
    if (!window.QRCode?.toCanvas) throw new Error("QR code renderer did not load; use the copyable link instead.");
    await window.QRCode.toCanvas($("#pairCode"), pairing.pairUrl, { width: 240, margin: 1, color: { dark: "#07150e", light: "#e9fff0" } });
    status.textContent = `Scan or copy this link. It expires at ${new Date(pairing.expiresAt).toLocaleTimeString()} and works once.`;
    addEvent("Created one-time management link");
  } catch (error) {
    status.textContent = error instanceof Error ? error.message : "Couldn’t create setup link.";
    addEvent(status.textContent);
  }
}

function beginPolling() {
  clearInterval(pollTimer); clearInterval(rotateTimer);
  poll();
  pollTimer = setInterval(poll, POLL_INTERVAL_MS);
  rotateTimer = setInterval(() => advanceFlight({ useRefreshedFirst: true }), ROTATE_INTERVAL_MS);
}

$("#identityForm").addEventListener("submit", event => {
  event.preventDefault();
  const data = new FormData(event.currentTarget);
  state.deviceKey = String(data.get("deviceKey") || "").trim();
  delete state.localName;
  state.deviceConfig = null;
  saveState(); addEvent("Simulator identity saved locally"); beginPolling();
});
$("#forceEmptySky").addEventListener("change", event => {
  state.forceEmptySky = event.currentTarget.checked;
  displayedFlight = null;
  flightIndex = 0;
  refreshPending = false;
  saveState();
  addEvent(state.forceEmptySky ? "Simulator: empty sky enabled" : "Simulator: live aircraft restored");
  render();
});
$("#nextFlight").addEventListener("click", () => advanceFlight());
$("#refresh").addEventListener("click", poll);
$("#manage").addEventListener("click", createPairing);
$("#closePairing").addEventListener("click", () => $("#pairingDialog").close());
$("#copyPairLink").addEventListener("click", async () => {
  const link = $("#pairLink");
  if (!link.value) return;
  try { await navigator.clipboard.writeText(link.value); $("#copyPairLink").textContent = "Copied"; }
  catch { link.select(); document.execCommand("copy"); $("#copyPairLink").textContent = "Copied"; }
  setTimeout(() => { $("#copyPairLink").textContent = "Copy link"; }, 1800);
});
$("#clearLog").addEventListener("click", () => { state.events = []; saveState(); renderDebug(); });
render(); beginPolling();
loadLogoRegistry();
