const $ = selector => document.querySelector(selector);
const form = $("#settingsForm");
const trackedFlightForm = $("#trackedFlightForm");
let map;
let marker;
let trackingSource;
let flightSource;
let planeRequestController;
let planeRequestNumber = 0;
let planeStatusTimer;
let toastHideTimer;
let flightResolveTimer;
let resolvedFlight = null;

const number = name => Number(form.elements[name].value);
const trackingFeature = () => ({
  type: "Feature",
  properties: {},
  geometry: { type: "Point", coordinates: [number("longitude"), number("latitude")] },
});

function radiusFeature() {
  const latitude = number("latitude") * Math.PI / 180;
  const longitude = number("longitude");
  const radiusKm = number("radiusKm");
  const coordinates = [];
  for (let bearing = 0; bearing <= 360; bearing += 6) {
    const radians = bearing * Math.PI / 180;
    const northKm = Math.cos(radians) * radiusKm;
    const eastKm = Math.sin(radians) * radiusKm;
    coordinates.push([
      longitude + eastKm / (111.32 * Math.cos(latitude)),
      number("latitude") + northKm / 110.574,
    ]);
  }
  return { type: "Feature", properties: {}, geometry: { type: "Polygon", coordinates: [coordinates] } };
}

function showRadius() {
  const radius = number("radiusKm");
  $("#radiusOut").value = `${radius} km`;
  $("#mapRadius").textContent = `${radius} km radius`;
}

function showToast(message, kind = "success") {
  const toast = $("#toast");
  clearTimeout(toastHideTimer);
  toast.textContent = message;
  toast.dataset.kind = kind;
  toast.classList.add("visible");
  toastHideTimer = setTimeout(() => {
    toast.classList.remove("visible");
  }, 3600);
}

function clearFlightMatch() {
  resolvedFlight = null;
  trackedFlightForm.elements.identifier.value = "";
  trackedFlightForm.elements.statusIdentifier.value = "";
  const match = $("#flightMatch");
  match.hidden = true;
  match.replaceChildren();
}

function chooseFlightMatch(candidate) {
  resolvedFlight = candidate;
  trackedFlightForm.elements.identifier.value = candidate.identifier;
  trackedFlightForm.elements.statusIdentifier.value = candidate.statusIdentifier || "";
  for (const button of document.querySelectorAll("#flightMatch button")) button.classList.toggle("selected", button.dataset.identifier === candidate.identifier);
}

async function resolveFlightInput() {
  const identifier = trackedFlightForm.elements.flightInput.value.trim();
  if (!identifier) return clearFlightMatch();
  const response = await fetch("/api/settings/tracked-flights/resolve", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ identifier, identifierType: trackedFlightForm.elements.identifierType.value }) });
  if (!response.ok) return clearFlightMatch();
  const { candidates = [], needsChoice } = await response.json();
  if (!candidates.length) return clearFlightMatch();
  const match = $("#flightMatch");
  const heading = document.createElement("strong");
  heading.textContent = needsChoice ? "Which operating carrier?" : "Live tracking match";
  const options = document.createElement("div");
  options.className = "flight-match-options";
  for (const candidate of candidates) {
    const button = document.createElement("button");
    button.type = "button";
    button.dataset.identifier = candidate.identifier;
    const label = document.createElement("strong");
    label.textContent = candidate.label;
    button.append(label);
    if (candidate.route) {
      const route = document.createElement("span");
      route.className = "flight-match-route";
      route.textContent = candidate.route;
      button.append(route);
    }
    button.addEventListener("click", () => chooseFlightMatch(candidate));
    options.append(button);
  }
  match.replaceChildren(heading, options);
  match.hidden = false;
  chooseFlightMatch(candidates[0]);
}

function selectTab(tabId) {
  for (const button of document.querySelectorAll(".tab")) {
    const selected = button.dataset.tab === tabId;
    button.classList.toggle("active", selected);
    button.setAttribute("aria-selected", String(selected));
  }
  for (const panel of [$("#displayTab"), $("#trackedTab")]) panel.hidden = panel.id !== tabId;
}

function showPlaneStatus(message, state = "ready") {
  const status = $("#planeStatus");
  clearTimeout(planeStatusTimer);
  status.textContent = message;
  status.dataset.state = state;
  status.hidden = false;
  if (state !== "loading") planeStatusTimer = setTimeout(() => { status.hidden = true; }, 2200);
}

function validLocation() {
  const latitude = number("latitude");
  const longitude = number("longitude");
  return Number.isFinite(latitude) && Number.isFinite(longitude) && latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180;
}

function updateTrackingArea() {
  showRadius();
  if (!map || !trackingSource || !validLocation()) return;
  const location = trackingFeature();
  trackingSource.setData(radiusFeature());
  marker.setLngLat(location.geometry.coordinates);
}

function focusTrackingArea() {
  if (!map || !validLocation()) return;
  const panelHeight = $("#settings").offsetHeight;
  const coordinates = radiusFeature().geometry.coordinates[0];
  const longitudes = coordinates.map(([longitude]) => longitude);
  const latitudes = coordinates.map(([, latitude]) => latitude);
  // Fit the complete tracking circle in the map area above the bottom sheet.
  // This keeps every returned aircraft on-screen instead of underneath the panel.
  map.fitBounds([[Math.min(...longitudes), Math.min(...latitudes)], [Math.max(...longitudes), Math.max(...latitudes)]], {
    padding: { top: 54, right: 28, bottom: panelHeight + 30, left: 28 },
    maxZoom: 13,
    duration: 360,
  });
}

function setLocation(latitude, longitude) {
  form.elements.latitude.value = latitude.toFixed(6);
  form.elements.longitude.value = longitude.toFixed(6);
  updateTrackingArea();
}

function initMap() {
  if (map || !window.maplibregl) return;
  map = new maplibregl.Map({
    container: "map",
    style: "https://tiles.openfreemap.org/styles/positron",
    center: [number("longitude"), number("latitude")],
    zoom: 8,
    attributionControl: true,
  });
  map.addControl(new maplibregl.NavigationControl({ showCompass: false }), "top-right");

  map.on("load", () => {
    map.addSource("tracking-area", { type: "geojson", data: radiusFeature() });
    trackingSource = map.getSource("tracking-area");
    map.addLayer({
      id: "tracking-radius",
      type: "fill",
      source: "tracking-area",
      paint: {
        "fill-color": "#1e8578",
        "fill-opacity": 0.14,
      },
    });
    map.addLayer({
      id: "tracking-radius-outline",
      type: "line",
      source: "tracking-area",
      paint: { "line-color": "#106258", "line-width": 2, "line-opacity": 0.9 },
    });
    const pin = document.createElement("button");
    pin.className = "map-pin";
    pin.type = "button";
    pin.setAttribute("aria-label", "Tracking location; drag to move");
    marker = new maplibregl.Marker({ element: pin, draggable: true, anchor: "center" }).setLngLat(trackingFeature().geometry.coordinates).addTo(map);
    marker.on("dragend", () => {
      const { lat, lng } = marker.getLngLat();
      setLocation(lat, lng);
      loadFlightOverlay({ useCurrentArea: true });
    });
    installFlightLayer();
    updateTrackingArea();
    focusTrackingArea();
  });
}

function installFlightLayer() {
  const planeIcon = new Image(64, 64);
  planeIcon.addEventListener("load", () => {
    if (!map || map.hasImage("flight-aircraft")) return;
    map.addImage("flight-aircraft", planeIcon, { pixelRatio: 2 });
    map.addSource("flight-aircraft", { type: "geojson", data: { type: "FeatureCollection", features: [] } });
    flightSource = map.getSource("flight-aircraft");
    map.addLayer({ id: "flight-aircraft-halo", type: "circle", source: "flight-aircraft", paint: { "circle-radius": 12, "circle-color": "#087c70", "circle-stroke-width": 1.5, "circle-stroke-color": "#ffffff", "circle-opacity": .98 } });
    map.addLayer({ id: "flight-aircraft-icon", type: "symbol", source: "flight-aircraft", layout: { "icon-image": "flight-aircraft", "icon-size": .38, "icon-rotate": ["coalesce", ["get", "headingDeg"], 0], "icon-rotation-alignment": "map", "icon-pitch-alignment": "viewport", "icon-allow-overlap": true, "icon-ignore-placement": true } });
    map.addLayer({ id: "flight-aircraft-label", type: "symbol", source: "flight-aircraft", layout: { "text-field": ["get", "label"], "text-font": ["Noto Sans Bold"], "text-size": 9, "text-offset": [0, -2.05], "text-anchor": "bottom", "text-allow-overlap": true, "text-ignore-placement": true }, paint: { "text-color": "#15544b", "text-halo-color": "rgba(255,255,255,.95)", "text-halo-width": 1.35 } });
    loadFlightOverlay();
  });
  planeIcon.src = "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><path fill="white" d="M32 3c-2.7 0-4.3 2.1-4.3 4.8v18.5L8 35v5l19.7-3.2v14.4l-6.4 7.7v2.5l10.7-2.4 10.7 2.4v-2.5L36.3 51V36.8L56 40v-5l-19.7-8.7V7.8C36.3 5.1 34.7 3 32 3z"/></svg>');
}

async function loadFlightOverlay({ useCurrentArea = false } = {}) {
  if (!map || !flightSource) return;
  const requestNumber = ++planeRequestNumber;
  planeRequestController?.abort();
  planeRequestController = new AbortController();
  showPlaneStatus("Updating aircraft…", "loading");
  try {
    const params = useCurrentArea
      ? new URLSearchParams({ latitude: String(number("latitude")), longitude: String(number("longitude")), radiusKm: String(number("radiusKm")) })
      : null;
    const response = await fetch(`/api/settings/flights${params ? `?${params}` : ""}`, { signal: planeRequestController.signal });
    if (!response.ok) {
      if (requestNumber === planeRequestNumber) showPlaneStatus("Couldn’t update aircraft", "error");
      return;
    }
    const { flights = [] } = await response.json();
    if (requestNumber !== planeRequestNumber) return;
    const visibleFlights = flights.filter(flight => Number.isFinite(flight.latitude) && Number.isFinite(flight.longitude));
    flightSource.setData({ type: "FeatureCollection", features: visibleFlights.map(flight => ({ type: "Feature", properties: { label: flight.flightNumber || flight.registration || "Aircraft", headingDeg: flight.headingDeg }, geometry: { type: "Point", coordinates: [flight.longitude, flight.latitude] } })) });
    showPlaneStatus(`${visibleFlights.length} aircraft updated`, "ready");
  } catch (error) {
    if (error?.name === "AbortError") return;
    if (requestNumber === planeRequestNumber) showPlaneStatus("Couldn’t update aircraft", "error");
  }
}

async function load() {
  const response = await fetch("/api/settings");
  if (!response.ok) return;
  const device = await response.json();
  $("#welcome").hidden = true;
  $("#settings").hidden = false;
  $("#map").hidden = false;
  $("#mapHud").hidden = false;
  $("#deviceName").textContent = device.name;
  for (const name of ["name", "latitude", "longitude", "radiusKm"]) form.elements[name].value = device[name];
  form.elements.includeGrounded.checked = device.includeGrounded;
  const selectedDebugPack = [...form.elements.debugPack].find(input => input.value === (device.debugPack || ""));
  if (selectedDebugPack) selectedDebugPack.checked = true;
  trackedFlightForm.elements.flightDate.min = new Date().toISOString().slice(0, 10);
  if (!trackedFlightForm.elements.flightDate.value) trackedFlightForm.elements.flightDate.value = new Date().toISOString().slice(0, 10);
  showRadius();
  await loadTrackedFlights();
  requestAnimationFrame(initMap);
}

async function loadTrackedFlights() {
  const response = await fetch("/api/settings/tracked-flights");
  if (!response.ok) return;
  const { trackedFlights = [] } = await response.json();
  const list = $("#trackedFlightList");
  list.replaceChildren();
  if (!trackedFlights.length) {
    list.textContent = "No special flights planned yet.";
    list.classList.add("empty");
    return;
  }
  list.classList.remove("empty");
  for (const flight of trackedFlights) {
    const item = document.createElement("article");
    item.className = "tracked-flight";
    const details = document.createElement("div");
    const title = document.createElement("strong");
    title.textContent = flight.title;
    const meta = document.createElement("span");
    const identifier = flight.identifierType === "tailNumber" ? `Tail ${flight.identifier}` : `${flight.statusIdentifier || flight.identifier} → ${flight.identifier}`;
    meta.textContent = `${identifier} · ${flight.flightDate}`;
    details.append(title, meta);
    const remove = document.createElement("button");
    remove.className = "ghost remove-flight";
    remove.type = "button";
    remove.textContent = "Remove";
    remove.addEventListener("click", async () => {
      remove.disabled = true;
      remove.textContent = "Removing…";
      try {
        const response = await fetch(`/api/settings/tracked-flights/${flight.id}`, { method: "DELETE" });
        if (!response.ok) throw new Error("Removal failed");
        await loadTrackedFlights();
        showToast("Flight removed. Display updates within a minute.");
      } catch {
        remove.disabled = false;
        remove.textContent = "Remove";
        showToast("Couldn’t remove flight.", "error");
      }
    });
    item.append(details, remove);
    list.append(item);
  }
}

form.elements.radiusKm.addEventListener("input", updateTrackingArea);
document.querySelectorAll(".tab").forEach(button => button.addEventListener("click", () => selectTab(button.dataset.tab)));
form.elements.radiusKm.addEventListener("change", () => { focusTrackingArea(); loadFlightOverlay({ useCurrentArea: true }); });
for (const name of ["latitude", "longitude"]) {
  form.elements[name].addEventListener("change", () => { updateTrackingArea(); focusTrackingArea(); });
}
form.addEventListener("submit", async event => {
  event.preventDefault();
  const data = Object.fromEntries(new FormData(form));
  data.includeGrounded = form.elements.includeGrounded.checked;
  for (const name of ["latitude", "longitude", "radiusKm"]) data[name] = Number(data[name]);
  const response = await fetch("/api/settings", { method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify(data) });
  const result = await response.json();
  showToast(response.ok ? "Settings saved." : "Couldn’t save settings.", response.ok ? "success" : "error");
  if (response.ok) $("#deviceName").textContent = data.name;
});
trackedFlightForm.addEventListener("submit", async event => {
  event.preventDefault();
  const data = Object.fromEntries(new FormData(trackedFlightForm));
  if (!data.identifier) {
    showToast("Choose a live tracking match first.", "error");
    return;
  }
  const response = await fetch("/api/settings/tracked-flights", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(data) });
  if (response.ok) {
    trackedFlightForm.reset();
    trackedFlightForm.elements.identifierType.dispatchEvent(new Event("change"));
    await loadTrackedFlights();
    showToast("Flight added.");
  } else {
    showToast("Check the flight details.", "error");
  }
});
trackedFlightForm.elements.identifierType.addEventListener("change", event => {
  const tailNumber = event.currentTarget.value === "tailNumber";
  const label = $("#trackedIdentifierLabel");
  label.firstChild.textContent = tailNumber ? "Tail number" : "Flight number on the ticket";
  const input = trackedFlightForm.elements.flightInput;
  input.placeholder = tailNumber ? "C-GMPO" : "WS1098";
  input.value = "";
  clearFlightMatch();
});
trackedFlightForm.elements.flightInput.addEventListener("input", () => {
  clearTimeout(flightResolveTimer);
  clearFlightMatch();
  flightResolveTimer = setTimeout(resolveFlightInput, 350);
});
$("#fitMap").addEventListener("click", focusTrackingArea);
$("#logout").addEventListener("click", async () => { await fetch("/api/session", { method: "DELETE" }); location.href = "/"; });
$("#rotate").addEventListener("click", async () => {
  const response = await fetch("/api/settings/request-key-rotation", { method: "POST" });
  const result = await response.json();
  showToast(response.ok ? "Key rotation requested." : "Couldn’t request key rotation.", response.ok ? "success" : "error");
});
if (window.__LOCAL_DEMO__) {
  const demo = $("#demo");
  demo.hidden = false;
  demo.addEventListener("click", async () => { const response = await fetch("/api/demo/pair", { method: "POST" }); location.href = (await response.json()).pairUrl; });
}
load();
