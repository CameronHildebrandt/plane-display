const encoder = new TextEncoder();
const PAIRING_TTL_SECONDS = 5 * 60;
const SESSION_TTL_SECONDS = 90 * 24 * 60 * 60;
const SNAPSHOT_TTL_SECONDS = 120;
const FLIGHT_STATUS_TTL_SECONDS = 10 * 60;
const AIRLABS_MONTHLY_REQUEST_CAP = 900;
const memoryLimits = new Map();
let openSkyToken = null;
// Corrections for confirmed bad/stale records returned by the free reference source.
// Check these before its 30-day cache so a previously cached bad record cannot win.
const AIRLINE_OVERRIDES = Object.freeze({
  AKT: { name: "Canadian North", country: "Canada" },
});

export default { fetch: (request, env) => handle(request, new D1Store(env.DB), env) };

export async function handle(request, store, env = {}) {
  const url = new URL(request.url);
  const path = url.pathname;
  if (path === "/flightdata" && request.method === "GET") return flightData(request, store, env);
  if (path === "/api/device/pairing" && request.method === "POST") return createPairing(request, store, url);
  if (path === "/api/device/config" && request.method === "GET") return deviceConfig(request, store);
  if (path === "/api/device/rotate" && request.method === "POST") return rotateDeviceKey(request, store);
  if (path.startsWith("/pair/") && request.method === "GET") return claimPairing(request, store, path.slice(6), url);
  if (path === "/api/settings" && request.method === "GET") return getSettings(request, store);
  if (path === "/api/settings/flights" && request.method === "GET") return settingsFlights(request, store, env, url);
  if (path === "/api/settings/tracked-flights" && request.method === "GET") return getTrackedFlights(request, store);
  if (path === "/api/settings/tracked-flights/resolve" && request.method === "POST") return resolveTrackedFlight(request, store, env);
  if (path === "/api/settings/tracked-flights" && request.method === "POST") return addTrackedFlight(request, store);
  if (path.startsWith("/api/settings/tracked-flights/") && request.method === "DELETE") return removeTrackedFlight(request, store, path.slice("/api/settings/tracked-flights/".length));
  if (path === "/api/settings" && request.method === "PUT") return saveSettings(request, store);
  if (path === "/api/settings/request-key-rotation" && request.method === "POST") return requestRotation(request, store);
  if (path === "/api/session" && request.method === "DELETE") return logout(request, store);
  if (path === "/settings" && request.method === "GET" && env.ASSETS) return env.ASSETS.fetch(new Request(new URL("/", request.url), request));
  if (path === "/display" && request.method === "GET" && env.ASSETS) return env.ASSETS.fetch(new Request(new URL("/display.html", request.url), request));
  if (env.ASSETS) return env.ASSETS.fetch(request);
  return json({ error: "Not found" }, 404);
}

async function flightData(request, store, env) {
  const device = await authenticateDevice(request, store);
  if (!device) return json({ error: "Device authentication required" }, 401);
  if (!take(`flight:${device.id}`, 70, 60)) return json({ error: "Slow down" }, 429);
  return flightPayload(device, store, env);
}

async function settingsFlights(request, store, env, url) {
  const device = await authenticateSession(request, store);
  if (!device) return json({ error: "Pair this browser from the display QR code." }, 401);
  const requested = mapSearchArea(url);
  if (!requested.valid) return json({ error: "Map location or radius is invalid." }, 422);
  // The owner map is deliberately a fresh look-up. It never uses the display cache.
  // It does not read or write the display's short-lived snapshot cache.
  if (!take(`settings-flight:${device.id}`, 10, 60)) return json({ error: "Slow down" }, 429);
  try {
    const searchDevice = requested.area || { ...device, radiusKm: 100 };
    const { source, flights } = await liveFlights({ ...device, ...searchDevice }, store, env);
    return json({ generatedAt: new Date().toISOString(), dataAgeSeconds: 0, source, flights }, 200, { "Cache-Control": "no-store" });
  } catch (error) {
    console.log("Owner map flight request failed", error instanceof Error ? error.message : String(error));
    return json({ error: "Live flight data is temporarily unavailable. Try again shortly." }, 503, { "Cache-Control": "no-store" });
  }
}

function mapSearchArea(url) {
  const fields = ["latitude", "longitude", "radiusKm"];
  const hasAny = fields.some(field => url.searchParams.has(field));
  if (!hasAny) return { valid: true, area: null };
  const latitude = Number(url.searchParams.get("latitude"));
  const longitude = Number(url.searchParams.get("longitude"));
  const radiusKm = Number(url.searchParams.get("radiusKm"));
  const valid = Number.isFinite(latitude) && latitude >= -90 && latitude <= 90 && Number.isFinite(longitude) && longitude >= -180 && longitude <= 180 && Number.isFinite(radiusKm) && radiusKm >= 1 && radiusKm <= 100;
  return { valid, area: valid ? { latitude, longitude, radiusKm } : null };
}

async function flightPayload(device, store, env) {
  const now = unix();
  const debugPack = selectedDebugPack(device.debugMode);
  if (debugPack) {
    const payload = { generatedAt: new Date().toISOString(), dataAgeSeconds: 0, source: `debug:${debugPack.id}`, flights: debugFlights(debugPack) };
    return json(payload, 200, { "Cache-Control": "no-store", "X-Data-Age-Seconds": "0" });
  }
  const cached = await store.getSnapshot(device.id);
  if (cached && cached.fetchedAt + SNAPSHOT_TTL_SECONDS > now) {
    return json(cached.payload, 200, { "Cache-Control": "private, max-age=60", "X-Data-Age-Seconds": String(now - cached.fetchedAt) });
  }
  try {
    const { source, flights } = await liveFlights(device, store, env);
    const payload = { generatedAt: new Date().toISOString(), dataAgeSeconds: 0, source, flights };
    await store.putSnapshot(device.id, payload, now);
    return json(payload, 200, { "Cache-Control": "private, max-age=60", "X-Data-Age-Seconds": "0" });
  } catch (error) {
    // A stale snapshot is much more useful to a display than an error during a provider hiccup.
    if (cached) {
      const payload = { ...cached.payload, dataAgeSeconds: now - cached.fetchedAt, stale: true };
      return json(payload, 200, { "Cache-Control": "no-store", "X-Data-Age-Seconds": String(now - cached.fetchedAt), "Warning": "110 - Response is stale" });
    }
    console.log("OpenSky request failed", error instanceof Error ? error.message : String(error));
    return json({ error: "Live flight data is temporarily unavailable. Try again shortly." }, 503, { "Cache-Control": "no-store" });
  }
}

async function createPairing(request, store, url) {
  const device = await authenticateDevice(request, store);
  if (!device) return json({ error: "Device authentication required" }, 401);
  const token = randomToken();
  const expiresAt = unix() + PAIRING_TTL_SECONDS;
  await store.createPairing(await hash(token), device.id, expiresAt);
  return json({ pairUrl: `${url.origin}/pair/${token}`, expiresAt: new Date(expiresAt * 1000).toISOString() });
}

async function claimPairing(request, store, token, url) {
  if (!isToken(token) || !take(`pair:${clientIp(request)}`, 10, 60)) return new Response("This pairing link is unavailable.", { status: 429 });
  const deviceId = await store.consumePairing(await hash(token), unix());
  if (!deviceId) return new Response("This pairing link has expired or has already been used.", { status: 410 });
  const session = randomToken();
  await store.createSession(await hash(session), deviceId, unix() + SESSION_TTL_SECONDS, unix());
  return new Response(null, { status: 302, headers: { Location: "/settings", "Set-Cookie": sessionCookie(session, url.protocol === "https:") } });
}

async function getSettings(request, store) {
  const device = await authenticateSession(request, store);
  return device ? json(publicDevice(device)) : json({ error: "Pair this browser from the display QR code." }, 401);
}

async function saveSettings(request, store) {
  const device = await authenticateSession(request, store);
  if (!device) return json({ error: "Unauthorized" }, 401);
  let body; try { body = await request.json(); } catch { return json({ error: "Expected JSON" }, 400); }
  const latitude = Number(body.latitude), longitude = Number(body.longitude), radiusKm = Number(body.radiusKm);
  if (!Number.isFinite(latitude) || latitude < -90 || latitude > 90 || !Number.isFinite(longitude) || longitude < -180 || longitude > 180 || !Number.isFinite(radiusKm) || radiusKm <= 0 || radiusKm > 300) return json({ error: "Use latitude −90…90, longitude −180…180, and radius 0…300 km." }, 422);
  const debugPack = selectedDebugPack(body.debugPack);
  await store.updateSettings(device.id, { name: cleanName(body.name), latitude, longitude, radiusKm, includeGrounded: Boolean(body.includeGrounded), hideIncompleteData: Boolean(body.hideIncompleteData), debugMode: debugPack?.id || "" }, unix());
  await store.deleteSnapshot(device.id);
  return json({ ok: true });
}

async function requestRotation(request, store) {
  const device = await authenticateSession(request, store);
  if (!device) return json({ error: "Unauthorized" }, 401);
  await store.requestRotation(device.id, unix());
  return json({ ok: true, message: "The display will receive a replacement key at its next check-in." });
}

async function deviceConfig(request, store) {
  const device = await authenticateDevice(request, store);
  if (!device) return json({ error: "Device authentication required" }, 401);
  return json({ name: device.name, latitude: device.latitude, longitude: device.longitude, radiusKm: device.radiusKm, includeGrounded: Boolean(device.includeGrounded), hideIncompleteData: Boolean(device.hideIncompleteData), debugPack: selectedDebugPack(device.debugMode)?.id || null, rotateKey: Boolean(device.rotationRequested) });
}

async function rotateDeviceKey(request, store) {
  const device = await authenticateDevice(request, store);
  if (!device || !device.rotationRequested) return json({ error: "No pending key rotation" }, 409);
  const nextKey = randomToken();
  await store.rotateKey(device.id, await hash(nextKey), device.apiKeyHash, unix() + 600, unix());
  return json({ deviceKey: nextKey, graceSeconds: 600 });
}

async function logout(request, store) {
  const token = cookie(request, "plane_session");
  if (token) await store.deleteSession(await hash(token));
  return new Response(null, { status: 204, headers: { "Set-Cookie": "plane_session=; HttpOnly; SameSite=Lax; Path=/; Max-Age=0" } });
}

async function authenticateDevice(request, store) {
  const key = request.headers.get("X-Device-Key");
  return isToken(key) ? store.findDeviceByKeyHash(await hash(key), unix()) : null;
}
async function authenticateSession(request, store) {
  const token = cookie(request, "plane_session");
  return isToken(token) ? store.findDeviceBySessionHash(await hash(token), unix()) : null;
}
async function liveFlights(device, store, env) {
  try {
    const nearby = await adsbLolFlights(device, store, env);
    const tracked = await activeTrackedFlights(device, store, env);
    return { source: "adsb.lol", flights: sortFlightsForDisplay(filterIncompleteFlights(device, mergeTrackedFlights(nearby, tracked))).slice(0, 20) };
  }
  catch (adsbError) {
    console.log("ADSB.lol request failed; trying OpenSky", adsbError instanceof Error ? adsbError.message : String(adsbError));
    return { source: "opensky", flights: sortFlightsForDisplay(filterIncompleteFlights(device, await openSkyFlights(device, env))).slice(0, 20) };
  }
}

function hasIdentifiedRoute(flight) {
  return Boolean(flight.origin && flight.destination && flight.origin !== "---" && flight.destination !== "---");
}
function hasIdentifiedCompany(flight) {
  return Boolean(flight.airlineName || flight.airline);
}
function filterIncompleteFlights(device, flights) {
  if (!device.hideIncompleteData) return flights;
  return flights.filter(flight => hasIdentifiedCompany(flight) && hasIdentifiedRoute(flight));
}

async function activeTrackedFlights(device, store, env) {
  const tracked = await store.getTrackedFlights(device.id, isoDate());
  if (!tracked.length) return [];
  const fetcher = env.fetch || fetch;
  const results = await Promise.all(tracked.map(async entry => {
    try {
      const lookupKind = entry.identifierType === "tailNumber" ? "reg" : "callsign";
      const response = await fetcher(`https://api.adsb.lol/v2/${lookupKind}/${encodeURIComponent(entry.identifier)}`);
      if (!response.ok) throw new Error(`ADSB.lol ${lookupKind} lookup returned ${response.status}`);
      const body = await response.json();
      const aircraft = Array.isArray(body.ac) ? body.ac : [];
      const flightStatus = await specialFlightStatus(entry, store, env);
      return aircraft.map(item => ({ ...normalizeTrackedAdsbLolState(item, device, entry), flightStatus })).filter(Boolean);
    } catch (error) {
      console.log("Tracked flight lookup failed", entry.identifier, error instanceof Error ? error.message : String(error));
      return [];
    }
  }));
  const flights = results.flat();
  const routed = applyRoutes(flights, await adsbLolRoutes(flights.map(flight => ({ flight: flight.flightNumber, lat: flight.latitude, lon: flight.longitude })), fetcher));
  return enrichAirlineNames(await enrichAircraftDetails(routed, store, fetcher), store, fetcher);
}

async function specialFlightStatus(entry, store, env) {
  if (!env.AIRLABS_API_KEY) return null;
  const cached = await store.getFlightStatus(entry.id);
  if (cached && cached.fetchedAt + FLIGHT_STATUS_TTL_SECONDS > unix()) return cached.payload;
  if (!await store.takeProviderQuota("airlabs", isoDate().slice(0, 7), AIRLABS_MONTHLY_REQUEST_CAP)) {
    console.log("AirLabs monthly safety cap reached");
    return cached?.payload || null;
  }
  const identifier = entry.statusIdentifier || entry.flightNumber;
  const parameter = /^[A-Z]{2}\d+$/.test(identifier) ? "flight_iata" : "flight_icao";
  try {
    const url = new URL("https://airlabs.co/api/v9/flight");
    url.searchParams.set(parameter, identifier);
    url.searchParams.set("_fields", "status,dep_time,dep_estimated,dep_actual,dep_delayed,arr_time,arr_estimated,arr_actual,arr_delayed,dep_iata,arr_iata");
    url.searchParams.set("api_key", env.AIRLABS_API_KEY);
    const response = await (env.fetch || fetch)(url);
    if (!response.ok) throw new Error(`AirLabs returned ${response.status}`);
    const body = await response.json();
    const source = body.response || body;
    if (!source || Array.isArray(source) || typeof source !== "object") return cached?.payload || null;
    const status = normalizeFlightStatus(source);
    await store.putFlightStatus(entry.id, { payload: status, fetchedAt: unix() });
    return status;
  } catch (error) {
    console.log("AirLabs status lookup failed", identifier, error instanceof Error ? error.message : String(error));
    return cached?.payload || null;
  }
}

function normalizeFlightStatus(status) {
  const number = value => typeof value === "number" && Number.isFinite(value) ? value : null;
  return { provider: "airlabs", status: typeof status.status === "string" ? status.status : null, scheduledDeparture: status.dep_time || null, estimatedDeparture: status.dep_estimated || null, actualDeparture: status.dep_actual || null, scheduledArrival: status.arr_time || null, estimatedArrival: status.arr_estimated || null, actualArrival: status.arr_actual || null, departureDelayMinutes: number(status.dep_delayed), arrivalDelayMinutes: number(status.arr_delayed), origin: status.dep_iata || null, destination: status.arr_iata || null };
}

const DEBUG_AIRPORTS = Object.freeze([
  ["YEG", "Edmonton"], ["YYC", "Calgary"], ["YVR", "Vancouver"], ["YYZ", "Toronto"], ["YUL", "Montreal"], ["YHZ", "Halifax"],
  ["JFK", "New York"], ["LAX", "Los Angeles"], ["ORD", "Chicago"], ["MEX", "Mexico City"], ["GRU", "Sao Paulo"], ["EZE", "Buenos Aires"],
  ["LHR", "London"], ["CDG", "Paris"], ["AMS", "Amsterdam"], ["FRA", "Frankfurt"], ["IST", "Istanbul"], ["DXB", "Dubai"],
  ["NRT", "Tokyo"], ["ICN", "Seoul"], ["SIN", "Singapore"], ["HKG", "Hong Kong"], ["DEL", "Delhi"], ["SYD", "Sydney"], ["AKL", "Auckland"],
  ["JNB", "Johannesburg"], ["CPT", "Cape Town"], ["NBO", "Nairobi"], ["CAI", "Cairo"]
]);
const DEBUG_AIRCRAFT = Object.freeze([
  ["B738", "Boeing 737-800"], ["B38M", "Boeing 737 MAX 8"], ["A320", "Airbus A320"], ["A21N", "Airbus A321neo"], ["DH8D", "Dash 8 Q400"],
  ["E75L", "Embraer 175"], ["CRJ9", "CRJ-900"], ["B789", "Boeing 787-9"], ["B77W", "Boeing 777-300ER"], ["C172", "Cessna 172"], ["PC12", "Pilatus PC-12"], ["B407", "Bell 407"]
]);
const DEBUG_PACKS = Object.freeze([
  { id: "canadian-scheduled", name: "Canadian scheduled", entries: [
    ["WJA", "WestJet"], ["ACA", "Air Canada"], ["JZA", "Air Canada Jazz"], ["ROU", "Air Canada Rouge"], ["WEN", "WestJet Encore"], ["FLE", "Flair Airlines"], ["POE", "Porter Airlines"], ["TSC", "Air Transat"], ["AKT", "Canadian North"], ["CAV", "Calm Air"], ["AIE", "Air Inuit"], ["ANT", "Air North"], ["CRQ", "Air Creebec"], ["BLS", "Bearskin Airlines"], ["PCO", "Pacific Coastal"], ["GLR", "Central Mountain Air"], ["PAG", "Perimeter Aviation"], ["WSG", "Wasaya Airways"], ["PVL", "Provincial Airlines"], ["ASP", "Air Sprint"]
  ] },
  { id: "canadian-specialist", name: "Canadian cargo, charter & public service", entries: [
    ["CGJ", "Cargojet"], ["MAL", "Morningstar Air Express"], ["PUL", "Purolator"], ["KFA", "KF Aerospace"], ["CNK", "Sunwest Aviation"], ["CWA", "CanWest Air"], ["STA", "STARS Air Ambulance"], ["CFC", "Royal Canadian Air Force"], ["RCMP", "RCMP Air Services"], ["SYB", "Skyservice"], ["HRT", "Chartright"], ["NRL", "Nolinor"], ["CHR", "Chrono Aviation"], ["KBA", "Kenn Borek Air"], ["TIN", "Air Tindi"], ["BFF", "Air Baffin"], ["HJT", "Helijet"], ["HBR", "Harbour Air"], ["PSC", "Pascan Aviation"], ["NWT", "North-Wright Airways"]
  ] },
  { id: "us-mainline", name: "U.S. mainline & low-cost", entries: [
    ["AAL", "American Airlines"], ["DAL", "Delta Air Lines"], ["UAL", "United Airlines"], ["SWA", "Southwest Airlines"], ["ASA", "Alaska Airlines"], ["HAL", "Hawaiian Airlines"], ["JBU", "JetBlue"], ["FFT", "Frontier Airlines"], ["NKS", "Spirit Airlines"], ["AAY", "Allegiant Air"], ["MXY", "Breeze Airways"], ["VXP", "Avelo Airlines"], ["SCX", "Sun Country Airlines"], ["JSX", "JSX"], ["KAP", "Cape Air"], ["BTQ", "Boutique Air"], ["VTE", "Contour Airlines"], ["SKW", "SkyWest Airlines"], ["RPA", "Republic Airways"], ["QXE", "Horizon Air"]
  ] },
  { id: "us-regional-cargo", name: "U.S. regional & cargo", entries: [
    ["ENY", "Envoy Air"], ["EDV", "Endeavor Air"], ["PDT", "Piedmont Airlines"], ["JIA", "PSA Airlines"], ["GJS", "GoJet Airlines"], ["UCA", "CommuteAir"], ["ASH", "Mesa Airlines"], ["AWI", "Air Wisconsin"], ["LYM", "Key Lime Air"], ["LTD", "Southern Airways Express"], ["GPD", "Tradewind Aviation"], ["FDX", "FedEx"], ["UPS", "UPS Airlines"], ["GTI", "Atlas Air"], ["ABX", "ABX Air"], ["ATI", "Air Transport International"], ["CKS", "Kalitta Air"], ["AJT", "Amerijet"], ["PAC", "Polar Air Cargo"], ["WGN", "Western Global Airlines"]
  ] },
  { id: "europe", name: "Europe & transatlantic", entries: [
    ["AFR", "Air France"], ["KLM", "KLM"], ["BAW", "British Airways"], ["DLH", "Lufthansa"], ["SWR", "Swiss"], ["AUA", "Austrian Airlines"], ["ICE", "Icelandair"], ["EIN", "Aer Lingus"], ["TAP", "TAP Air Portugal"], ["FIN", "Finnair"], ["LOT", "LOT Polish Airlines"], ["SAS", "Scandinavian Airlines"], ["CFG", "Condor"], ["EDW", "Edelweiss Air"], ["OCN", "Discover Airlines"], ["VIR", "Virgin Atlantic"], ["ITY", "ITA Airways"], ["BEL", "Brussels Airlines"], ["AEA", "Air Europa"], ["RAM", "Royal Air Maroc"]
  ] },
  { id: "asia-middle-east", name: "Asia & Middle East", entries: [
    ["CCA", "Air China"], ["CES", "China Eastern"], ["CSN", "China Southern"], ["CAL", "China Airlines"], ["CPA", "Cathay Pacific"], ["EVA", "EVA Air"], ["ANA", "All Nippon Airways"], ["JAL", "Japan Airlines"], ["KAL", "Korean Air"], ["AAR", "Asiana Airlines"], ["SIA", "Singapore Airlines"], ["THY", "Turkish Airlines"], ["UAE", "Emirates"], ["QTR", "Qatar Airways"], ["ETD", "Etihad Airways"], ["AIC", "Air India"], ["HVN", "Vietnam Airlines"], ["PAL", "Philippine Airlines"], ["MAS", "Malaysia Airlines"], ["CHH", "Hainan Airlines"]
  ] },
  { id: "americas-special", name: "Americas, global cargo & special visitors", entries: [
    ["AMX", "Aeromexico"], ["AVA", "Avianca"], ["CMP", "Copa Airlines"], ["LAN", "LATAM Chile"], ["TAM", "LATAM Brasil"], ["ARG", "Aerolíneas Argentinas"], ["GLO", "GOL"], ["VOI", "Volaris"], ["VIV", "VivaAerobus"], ["BWA", "Caribbean Airlines"], ["ANZ", "Air New Zealand"], ["QFA", "Qantas"], ["CLX", "Cargolux"], ["CKK", "China Cargo Airlines"], ["CAO", "Air China Cargo"], ["NCA", "Nippon Cargo Airlines"], ["DHK", "DHL Air"], ["ETH", "Ethiopian Airlines"], ["USM", "United States Military"], ["GOV", "Government / unidentified public service"]
  ] }
]);

function selectedDebugPack(value) { return DEBUG_PACKS.find(pack => pack.id === value) || null; }

function debugFlights(pack) {
  return pack.entries.map(([airline, airlineName], index) => {
    const origin = randomDebugAirport();
    let destination = randomDebugAirport();
    while (destination[0] === origin[0]) destination = randomDebugAirport();
    const [aircraftType, aircraftTypeName] = DEBUG_AIRCRAFT[Math.floor(Math.random() * DEBUG_AIRCRAFT.length)];
    return {
      icao24: `debug-${pack.id.slice(0, 6)}-${String(index + 1).padStart(2, "0")}`,
      airline, airlineName, airlineCountry: null, flightNumber: `${airline}${index + 1}`,
      origin: origin[0], destination: destination[0], aircraftType, aircraftTypeName,
      registration: `T-${String(index + 1).padStart(3, "0")}`,
      altFt: randomInteger(800, 43000), verticalSpeedFpm: randomInteger(-2800, 2800), speedKt: randomInteger(80, 590), trackDeg: randomInteger(0, 359),
      distanceKm: randomInteger(1, 18000), flightProgressPercent: randomInteger(0, 100), flightProgressEstimated: true,
      latitude: null, longitude: null, source: "debug"
    };
  });
}
function randomDebugAirport() { return DEBUG_AIRPORTS[Math.floor(Math.random() * DEBUG_AIRPORTS.length)]; }
function randomInteger(min, max) { return min + Math.floor(Math.random() * (max - min + 1)); }

function mergeTrackedFlights(nearby, tracked) {
  const byIdentity = new Map(nearby.map(flight => [flight.icao24 || flight.flightNumber, flight]));
  for (const flight of tracked) {
    const key = flight.icao24 || flight.flightNumber;
    const existing = byIdentity.get(key);
    byIdentity.set(key, existing ? { ...existing, isTrackedFlight: true, specialFlight: true, trackedFlightTitle: flight.trackedFlightTitle } : flight);
  }
  return [...byIdentity.values()];
}

// A stable, useful response order matters to the display queue. Rich records
// earn their place first; when two records are equally complete, their visible
// identifier gives a deterministic alphabetical tie-breaker.
function flightDataScore(flight) {
  const fields = [
    flight.airline, flight.airlineName, flight.flightNumber, flight.origin,
    flight.destination, flight.aircraftType, flight.aircraftTypeName,
    flight.registration, flight.altFt, flight.verticalSpeedFpm, flight.speedKt,
    flight.trackDeg, flight.distanceKm, flight.flightProgressPercent,
    flight.latitude, flight.longitude
  ];
  return fields.reduce((score, value) => score + (value !== null && value !== undefined && value !== "" && value !== "---" ? 1 : 0), 0);
}
function flightAlphabeticalKey(flight) {
  return String(flight.flightNumber || flight.registration || flight.icao24 || "").toUpperCase();
}
function sortFlightsForDisplay(flights) {
  return [...flights].sort((a, b) =>
    // A configured special flight is an explicit user request, so it must
    // survive the top-20 trim and arrive ahead of ordinary nearby traffic.
    Number(Boolean(b.specialFlight)) - Number(Boolean(a.specialFlight)) ||
    flightDataScore(b) - flightDataScore(a) || flightAlphabeticalKey(a).localeCompare(flightAlphabeticalKey(b)));
}

async function adsbLolFlights(device, store, env) {
  const fetcher = env.fetch || fetch;
  const radiusNm = Math.min(250, device.radiusKm / 1.852);
  const response = await fetcher(`https://api.adsb.lol/v2/point/${device.latitude}/${device.longitude}/${radiusNm}`);
  if (!response.ok) throw new Error(`ADSB.lol returned ${response.status}`);
  const data = await response.json();
  const rawAircraft = Array.isArray(data.ac) ? data.ac : [];
  const flights = rawAircraft
    .filter(a => a.lat !== undefined && a.lon !== undefined && (device.includeGrounded || a.alt_baro !== "ground"))
    .map(a => normalizeAdsbLolState(a, device))
    .filter(Boolean)
    .sort((a, b) => a.distanceKm - b.distanceKm)
    .slice(0, 20);
  const routed = applyRoutes(flights, await adsbLolRoutes(rawAircraft, fetcher));
  return enrichAirlineNames(await enrichAircraftDetails(routed, store, fetcher), store, fetcher);
}

async function enrichAirlineNames(flights, store, fetcher) {
  // Registry identity is already an actual saved operator record. Only ask
  // the callsign lookup to fill gaps which the registry could not identify.
  const codes = [...new Set(flights.filter(f => f.airline && !f.airlineName).map(f => f.airline))];
  const names = new Map(await Promise.all(codes.map(async code => [code, await airlineName(code, store, fetcher)])));
  return flights.map(flight => ({
    ...flight,
    airlineName: flight.airlineName || (flight.airline ? names.get(flight.airline)?.name || null : null),
    airlineCountry: flight.airlineCountry || (flight.airline ? names.get(flight.airline)?.country || null : null)
  }));
}

async function airlineName(code, store, fetcher) {
  if (AIRLINE_OVERRIDES[code]) return AIRLINE_OVERRIDES[code];
  const maxAgeSeconds = 30 * 24 * 60 * 60;
  const cached = await store.getAirline(code);
  if (cached && cached.fetchedAt + maxAgeSeconds > unix()) return cached;
  try {
    const response = await fetcher(`https://api.adsbdb.com/v0/airline/${encodeURIComponent(code)}`);
    if (!response.ok) throw new Error(`ADSBDB airline lookup returned ${response.status}`);
    const body = await response.json();
    const airline = Array.isArray(body.response) ? body.response.find(a => a.icao === code) || body.response[0] : null;
    const result = { name: airline?.name || null, country: airline?.country || null, fetchedAt: unix() };
    await store.putAirline(code, result);
    return result;
  } catch (error) {
    console.log("Airline lookup failed", code, error instanceof Error ? error.message : String(error));
    return cached || null;
  }
}

async function enrichAircraftDetails(flights, store, fetcher) {
  const icao24s = [...new Set(flights.map(f => f.icao24).filter(Boolean))];
  const details = new Map(await Promise.all(icao24s.map(async icao24 => [icao24, await aircraftDetails(icao24, store, fetcher)])));
  return flights.map(flight => {
    const detail = flight.icao24 ? details.get(flight.icao24) : null;
    // Position reports sometimes carry an incorrect or stale ICAO type. The
    // transponder's ICAO24 address is stable, so prefer independently resolved
    // registry metadata when it yields a safe ICAO designator. Never discard a
    // usable live value merely because a registry lookup was unavailable.
    const registryType = registryAircraftTypeCode(detail);
    const registryOperator = registryOperatorCode(detail);
    return {
      ...flight,
      aircraftType: registryType || flight.aircraftType,
      airline: registryOperator || flight.airline,
      airlineName: detail?.ownerName || flight.airlineName || null,
      airlineCountry: detail?.ownerCountry || flight.airlineCountry || null,
      aircraftTypeName: detail?.typeName || null,
      aircraftManufacturer: detail?.manufacturer || null,
      registeredOwner: detail?.ownerName || null,
      registeredOwnerCountry: detail?.ownerCountry || null,
      aircraftIdentitySource: registryType || registryOperator || detail?.ownerName ? "adsbdb" : "adsb.lol"
    };
  });
}

function registryOperatorCode(detail) {
  const code = String(detail?.ownerCode || "").trim().toUpperCase();
  return /^[A-Z0-9]{2,4}$/.test(code) ? code : null;
}

// ADSBDB deliberately returns a human-readable registered model, whereas the
// matrix's sprite picker uses ICAO designators. This is a model normalizer, not
// a carrier fleet list: it applies to every aircraft regardless of operator.
function registryAircraftTypeCode(detail) {
  const typeCode = String(detail?.typeCode || "").trim().toUpperCase();
  if (/^[A-Z0-9]{2,5}$/.test(typeCode)) return typeCode;
  const type = String(detail?.typeName || "").trim().toUpperCase();
  const manufacturer = String(detail?.manufacturer || "").trim().toUpperCase();
  const identity = `${manufacturer} ${type}`;
  if (!type) return null;
  if (/^[A-Z0-9]{2,5}$/.test(type)) return type;

  // C208 is intentionally explicit: this turns "Cessna 208 Caravan" into
  // the same display/sprite code as a provider which already sent C208.
  if (/\b(CESSNA\s*)?208\b/.test(identity) || /\bCARAVAN\b/.test(identity)) return "C208";
  if (/\bROBINSON\s*R?22\b|\bR22\b/.test(identity)) return "R22";
  if (/\b737\s*MAX\s*8\b|\bMAX\s*8\b/.test(identity)) return "B38M";
  if (/\b737[- ]?800\b/.test(identity)) return "B738";
  if (/\b737[- ]?700\b/.test(identity)) return "B737";
  if (/\bAIRBUS\s*A?320\b|\bA320\b/.test(identity)) return "A320";
  if (/\bAIRBUS\s*A?321\b|\bA321\b/.test(identity)) return "A321";
  if (/\bAIRBUS\s*A?220\b|\bA220\b/.test(identity)) return "A220";
  if (/\bDASH[ -]?8\b|\bQ400\b/.test(identity)) return "DH8D";
  if (/\bKING AIR\s*B?350\b|\bB350\b/.test(identity)) return "B350";
  if (/\bCRJ[- ]?200\b/.test(identity)) return "CRJ2";
  if (/\bCRJ[- ]?700\b/.test(identity)) return "CRJ7";
  if (/\bCRJ[- ]?900\b|\bCRJ[- ]?1000\b/.test(identity)) return "CRJ9";
  return null;
}

async function aircraftDetails(icao24, store, fetcher) {
  const maxAgeSeconds = 30 * 24 * 60 * 60;
  const cached = await store.getAircraft(icao24);
  if (cached && cached.identityVersion >= 2 && cached.fetchedAt + maxAgeSeconds > unix()) return cached;
  try {
    const response = await fetcher(`https://api.adsbdb.com/v0/aircraft/${encodeURIComponent(icao24)}`);
    if (!response.ok) throw new Error(`ADSBDB aircraft lookup returned ${response.status}`);
    const aircraft = (await response.json()).response?.aircraft;
    const result = {
      typeCode: aircraft?.icao_type || null,
      typeName: aircraft?.type || null,
      manufacturer: aircraft?.manufacturer || null,
      ownerCode: aircraft?.registered_owner_operator_flag_code || null,
      ownerName: aircraft?.registered_owner || null,
      ownerCountry: aircraft?.registered_owner_country_name || null,
      identityVersion: 2,
      fetchedAt: unix()
    };
    await store.putAircraft(icao24, result);
    return result;
  } catch (error) {
    console.log("Aircraft lookup failed", icao24, error instanceof Error ? error.message : String(error));
    return cached || null;
  }
}

async function adsbLolRoutes(aircraft, fetcher) {
  const planes = aircraft
    .filter(a => typeof a.flight === "string" && a.flight.trim() && numberOrNull(a.lat) !== null && numberOrNull(a.lon) !== null)
    .slice(0, 20)
    .map(a => ({ callsign: a.flight.trim(), lat: a.lat, lng: a.lon }));
  if (!planes.length) return [];
  try {
    const response = await fetcher("https://api.adsb.lol/api/0/routeset", { method: "POST", headers: { "Content-Type": "application/json", "Referer": "https://adsb.lol/" }, body: JSON.stringify({ planes }) });
    if (!response.ok) throw new Error(`ADSB.lol routeset returned ${response.status}`);
    const routes = await response.json();
    return Array.isArray(routes) ? routes : [];
  } catch (error) {
    // Route enrichment is optional: a tracking response must never fail because it is unavailable.
    console.log("ADSB.lol route lookup failed", error instanceof Error ? error.message : String(error));
    return [];
  }
}

async function openSkyFlights(device, env) {
  const fetcher = env.fetch || fetch;
  const latitudeDelta = device.radiusKm / 111.32;
  const longitudeDelta = device.radiusKm / (111.32 * Math.max(.1, Math.cos(device.latitude * Math.PI / 180)));
  const params = new URLSearchParams({ lamin: String(device.latitude - latitudeDelta), lamax: String(device.latitude + latitudeDelta), lomin: String(device.longitude - longitudeDelta), lomax: String(device.longitude + longitudeDelta) });
  const headers = {};
  if (env.OPEN_SKY_CLIENT_ID && env.OPEN_SKY_CLIENT_SECRET) headers.Authorization = `Bearer ${await getOpenSkyToken(env, fetcher)}`;
  const response = await fetcher(`https://opensky-network.org/api/states/all?${params}`, { headers });
  if (!response.ok) throw new Error(`OpenSky returned ${response.status}`);
  const data = await response.json();
  const aircraft = Array.isArray(data.states) ? data.states : [];
  return aircraft
    .filter(s => s[5] !== null && s[6] !== null && (device.includeGrounded || !s[8]))
    .map(s => normalizeOpenSkyState(s, device))
    .filter(Boolean)
    .sort((a, b) => a.distanceKm - b.distanceKm)
    .slice(0, 20);
}

async function getOpenSkyToken(env, fetcher) {
  if (openSkyToken && openSkyToken.expiresAt > unix() + 30) return openSkyToken.value;
  const response = await fetcher("https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token", { method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded" }, body: new URLSearchParams({ grant_type: "client_credentials", client_id: env.OPEN_SKY_CLIENT_ID, client_secret: env.OPEN_SKY_CLIENT_SECRET }).toString() });
  if (!response.ok) throw new Error(`OpenSky authentication returned ${response.status}`);
  const body = await response.json();
  if (!body.access_token) throw new Error("OpenSky authentication response did not contain a token");
  openSkyToken = { value: body.access_token, expiresAt: unix() + Number(body.expires_in || 1800) };
  return openSkyToken.value;
}

function normalizeOpenSkyState(s, device) {
  const latitude = numberOrNull(s[6]), longitude = numberOrNull(s[5]);
  const distanceKm = haversineKm(device.latitude, device.longitude, latitude, longitude);
  if (distanceKm > device.radiusKm) return null;
  const callsign = typeof s[1] === "string" ? s[1].trim() : null;
  const airline = callsign?.match(/^([A-Z]{3})/)?.[1] || null;
  return { airline, flightNumber: callsign, origin: null, destination: null, aircraftType: null, altFt: convert(s[7], 3.28084), verticalSpeedFpm: convert(s[11], 196.850394), speedKt: convert(s[9], 1.943844), trackDeg: numberOrNull(s[10]), registration: null, flightProgressPercent: null, latitude, longitude, distanceKm: round(distanceKm, 1), icao24: s[0] || null, lastContact: s[4] ? new Date(s[4] * 1000).toISOString() : null, source: "opensky" };
}
function normalizeAdsbLolState(a, device) {
  const latitude = numberOrNull(a.lat), longitude = numberOrNull(a.lon);
  // Always derive distance from latitude/longitude. This is lateral distance
  // from the display location, independent of altitude and source-specific
  // `dst` semantics.
  const distanceKm = haversineKm(device.latitude, device.longitude, latitude, longitude);
  if (distanceKm > device.radiusKm) return null;
  const callsign = typeof a.flight === "string" ? a.flight.trim() || null : null;
  const registration = a.r || null;
  const registrationCallsign = callsign && registration && callsign === registration.replaceAll("-", "");
  const dataFlags = [];
  if (a.dbFlags & 1) dataFlags.push("military");
  if (a.dbFlags & 2) dataFlags.push("interesting");
  if (a.dbFlags & 4) dataFlags.push("pia");
  if (a.dbFlags & 8) dataFlags.push("ladd");
  return { airline: registrationCallsign ? null : callsign?.match(/^([A-Z]{3})/)?.[1] || null, airlineName: null, airlineCountry: null, flightNumber: callsign, origin: null, destination: null, aircraftType: a.t || null, aircraftTypeName: null, aircraftManufacturer: null, registeredOwner: null, registeredOwnerCountry: null, altFt: numberOrNull(a.alt_baro), altGeomFt: numberOrNull(a.alt_geom), verticalSpeedFpm: numberOrNull(a.baro_rate ?? a.geom_rate), speedKt: numberOrNull(a.gs), trackDeg: numberOrNull(a.track), selectedAltitudeFt: numberOrNull(a.nav_altitude_mcp), selectedHeadingDeg: numberOrNull(a.nav_heading), registration, squawk: a.squawk || null, dataFlags, positionAgeSeconds: numberOrNull(a.seen_pos), signalStrengthDbfs: numberOrNull(a.rssi), flightProgressPercent: null, latitude, longitude, distanceKm: round(distanceKm, 1), icao24: a.hex || null, lastContact: numberOrNull(a.seen) === null ? null : new Date(Date.now() - a.seen * 1000).toISOString(), source: "adsb.lol" };
}
function normalizeTrackedAdsbLolState(a, device, tracked) {
  const flight = normalizeAdsbLolState({ ...a, dst: undefined }, { ...device, radiusKm: Infinity });
  return flight && { ...flight, isTrackedFlight: true, specialFlight: true, trackedFlightTitle: tracked.title };
}
function applyRoutes(flights, routes) {
  const byCallsign = new Map(routes.filter(r => typeof r.callsign === "string").map(r => [r.callsign.trim(), r]));
  return flights.map(flight => {
    const route = byCallsign.get(flight.flightNumber);
    const airports = route?._airports?.filter(a => (a.iata || a.icao) && numberOrNull(a.lat) !== null && numberOrNull(a.lon) !== null) || [];
    if (airports.length < 2) return flight;
    const activeLeg = inferActiveLeg(flight, airports);
    return { ...flight, airline: route.airline_code || flight.airline, origin: airportCode(activeLeg.origin), destination: airportCode(activeLeg.destination), flightProgressPercent: activeLeg.progressPercent, flightProgressEstimated: activeLeg.progressPercent !== null };
  });
}
function inferActiveLeg(flight, airports) {
  const legs = airports.slice(0, -1).map((origin, index) => ({ origin, destination: airports[index + 1] }));
  const scored = legs.map(leg => ({ ...leg, ...legScore(flight, leg) })).sort((a, b) => a.score - b.score);
  // The airport database can contain a stale or improbable route. Fall back to the whole route
  // if the aircraft is implausibly distant from every segment.
  return scored[0]?.crossTrackKm <= 250 ? scored[0] : { origin: airports[0], destination: airports.at(-1), progressPercent: null };
}
function legScore(flight, leg) {
  const start = { latitude: leg.origin.lat, longitude: leg.origin.lon }, end = { latitude: leg.destination.lat, longitude: leg.destination.lon };
  const d12 = angularDistance(start, end), d13 = angularDistance(start, flight);
  const bearingLeg = initialBearing(start, end), bearingToAircraft = initialBearing(start, flight);
  const bearingDifferenceRadians = (bearingToAircraft - bearingLeg) * Math.PI / 180;
  const crossTrackRadians = Math.asin(Math.sin(d13) * Math.sin(bearingDifferenceRadians));
  const alongTrack = Math.atan2(Math.sin(d13) * Math.cos(bearingDifferenceRadians), Math.cos(d13));
  const beforeStart = alongTrack < 0, pastEnd = alongTrack > d12;
  const crossTrackKm = beforeStart ? haversineKm(flight.latitude, flight.longitude, start.latitude, start.longitude) : pastEnd ? haversineKm(flight.latitude, flight.longitude, end.latitude, end.longitude) : Math.abs(crossTrackRadians) * 6371;
  const trackDifference = flight.trackDeg === null ? 0 : angularDifference(flight.trackDeg, bearingLeg);
  // Track resolves the ambiguity around a multi-stop airport without overpowering geographic fit.
  const progressPercent = d12 > 0 ? round(Math.max(0, Math.min(1, alongTrack / d12)) * 100, 1) : null;
  return { crossTrackKm, progressPercent, score: crossTrackKm + trackDifference / 180 * 50 };
}
function airportCode(airport) { return airport.iata || airport.icao; }
function angularDistance(a, b) { return haversineKm(a.latitude, a.longitude, b.latitude, b.longitude) / 6371; }
function initialBearing(a, b) { const rad = Math.PI / 180, lat1 = a.latitude * rad, lat2 = b.latitude * rad, deltaLon = (b.longitude - a.longitude) * rad; return (Math.atan2(Math.sin(deltaLon) * Math.cos(lat2), Math.cos(lat1) * Math.sin(lat2) - Math.sin(lat1) * Math.cos(lat2) * Math.cos(deltaLon)) / rad + 360) % 360; }
function angularDifference(a, b) { const diff = Math.abs(a - b) % 360; return diff > 180 ? 360 - diff : diff; }
function numberOrNull(value) { return typeof value === "number" && Number.isFinite(value) ? value : null; }
function convert(value, factor) { const n = numberOrNull(value); return n === null ? null : Math.round(n * factor); }
function round(value, decimals) { const n = 10 ** decimals; return Math.round(value * n) / n; }
function haversineKm(lat1, lon1, lat2, lon2) { const rad = Math.PI / 180, a = Math.sin((lat2 - lat1) * rad / 2) ** 2 + Math.cos(lat1 * rad) * Math.cos(lat2 * rad) * Math.sin((lon2 - lon1) * rad / 2) ** 2; return 6371 * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a)); }
function publicDevice(d) { return { id: d.id, name: d.name, latitude: d.latitude, longitude: d.longitude, radiusKm: d.radiusKm, includeGrounded: Boolean(d.includeGrounded), hideIncompleteData: Boolean(d.hideIncompleteData), debugPack: selectedDebugPack(d.debugMode)?.id || null, rotationRequested: Boolean(d.rotationRequested) }; }
async function getTrackedFlights(request, store) {
  const device = await authenticateSession(request, store);
  return device ? json({ trackedFlights: await store.getTrackedFlights(device.id) }) : json({ error: "Unauthorized" }, 401);
}
const TICKET_TO_CALLSIGNS = Object.freeze({
  WS: [{ code: "WJA", name: "WestJet" }, { code: "WEN", name: "WestJet Encore" }],
  AC: [{ code: "ACA", name: "Air Canada" }], PD: [{ code: "POE", name: "Porter Airlines" }],
  QK: [{ code: "JZA", name: "Air Canada Jazz" }], F8: [{ code: "FLE", name: "Flair Airlines" }],
  TS: [{ code: "TSC", name: "Air Transat" }], UA: [{ code: "UAL", name: "United Airlines" }],
  AA: [{ code: "AAL", name: "American Airlines" }], DL: [{ code: "DAL", name: "Delta Air Lines" }],
});
async function routeForTrackedCandidate(identifier, fetcher) {
  try {
    const response = await fetcher(`https://api.adsb.lol/v2/callsign/${encodeURIComponent(identifier)}`);
    if (!response.ok) return null;
    const aircraft = (await response.json()).ac?.find(item => numberOrNull(item.lat) !== null && numberOrNull(item.lon) !== null);
    if (!aircraft) return null;
    const routes = await adsbLolRoutes([aircraft], fetcher);
    const route = routes.find(item => item.callsign?.trim() === identifier);
    const airports = route?._airports?.filter(airport => airport.iata || airport.icao) || [];
    if (airports.length < 2) return null;
    return `${airportCode(airports[0])} → ${airportCode(airports[airports.length - 1])}`;
  } catch (error) {
    // Candidate resolution is a convenience; an unavailable lookup should
    // never prevent the owner from saving an otherwise valid special flight.
    console.log("Tracked candidate route lookup failed", identifier, error instanceof Error ? error.message : String(error));
    return null;
  }
}
async function resolveTrackedFlight(request, store, env) {
  const device = await authenticateSession(request, store);
  if (!device) return json({ error: "Unauthorized" }, 401);
  let body; try { body = await request.json(); } catch { return json({ error: "Expected JSON" }, 400); }
  const identifierType = body.identifierType === "tailNumber" ? "tailNumber" : "flightNumber";
  const input = identifierType === "tailNumber" ? cleanTailNumber(body.identifier) : cleanFlightNumber(body.identifier);
  if (!input) return json({ error: "Enter a valid flight or tail number." }, 422);
  if (identifierType === "tailNumber") return json({ input, identifierType, candidates: [{ identifier: input, label: `Tail ${input}`, statusIdentifier: null }] });
  const ticket = input.match(/^([A-Z]{2})(\d{1,6})$/);
  if (!ticket) return json({ input, identifierType, candidates: [{ identifier: input, label: `Use ADS-B callsign ${input}`, statusIdentifier: input }] });
  const [, airline, number] = ticket;
  const candidates = (TICKET_TO_CALLSIGNS[airline] || []).map(({ code, name }) => ({ identifier: `${code}${number}`, statusIdentifier: input, label: `${name} · ${code}${number}` }));
  if (!candidates.length) candidates.push({ identifier: input, statusIdentifier: input, label: `Use ${input} as the live callsign` });
  const fetcher = env.fetch || fetch;
  await Promise.all(candidates.map(async candidate => {
    candidate.route = await routeForTrackedCandidate(candidate.identifier, fetcher);
  }));
  return json({ input, identifierType, candidates, needsChoice: candidates.length > 1 });
}
async function addTrackedFlight(request, store) {
  const device = await authenticateSession(request, store);
  if (!device) return json({ error: "Unauthorized" }, 401);
  let body; try { body = await request.json(); } catch { return json({ error: "Expected JSON" }, 400); }
  const identifierType = body.identifierType === "tailNumber" ? "tailNumber" : "flightNumber";
  const identifier = identifierType === "tailNumber"
    ? cleanTailNumber(body.identifier ?? body.flightNumber)
    : cleanFlightNumber(body.identifier ?? body.flightNumber);
  const title = cleanTrackedTitle(body.title), flightDate = cleanDate(body.flightDate);
  if (!identifier || !title || !flightDate) return json({ error: "Enter a title, a valid flight or tail number, and a valid date." }, 422);
  const statusIdentifier = identifierType === "flightNumber" ? cleanFlightNumber(body.statusIdentifier) || identifier : null;
  const entry = { id: crypto.randomUUID(), deviceId: device.id, flightNumber: identifier, identifierType, statusIdentifier, title, flightDate, createdAt: unix() };
  await store.addTrackedFlight(entry);
  await store.deleteSnapshot(device.id);
  return json({ trackedFlight: publicTrackedFlight(entry) }, 201);
}
async function removeTrackedFlight(request, store, id) {
  const device = await authenticateSession(request, store);
  if (!device) return json({ error: "Unauthorized" }, 401);
  if (!/^[0-9a-f-]{36}$/i.test(id)) return json({ error: "Tracked flight not found." }, 404);
  const removed = await store.deleteTrackedFlight(device.id, id);
  if (!removed) return json({ error: "Tracked flight not found." }, 404);
  await Promise.all([store.deleteSnapshot(device.id), store.deleteFlightStatus(id)]);
  return json({ ok: true });
}
function publicTrackedFlight(entry) { const identifier = entry.identifier || entry.flightNumber; const identifierType = entry.identifierType || "flightNumber"; return { id: entry.id, identifier, identifierType, statusIdentifier: entry.statusIdentifier || null, flightNumber: identifierType === "flightNumber" ? identifier : null, tailNumber: identifierType === "tailNumber" ? identifier : null, title: entry.title, flightDate: entry.flightDate }; }
function cleanFlightNumber(value) { const compact = typeof value === "string" ? value.trim().toUpperCase().replace(/[^A-Z0-9]/g, "") : ""; return /^[A-Z0-9]{3,12}$/.test(compact) ? compact : null; }
function cleanTailNumber(value) { const tail = typeof value === "string" ? value.trim().toUpperCase().replace(/\s+/g, "") : ""; return /^[A-Z0-9][A-Z0-9-]{2,11}$/.test(tail) ? tail : null; }
function cleanTrackedTitle(value) { return typeof value === "string" ? value.trim().slice(0, 80) || null : null; }
function cleanDate(value) { return typeof value === "string" && /^\d{4}-\d{2}-\d{2}$/.test(value) && !Number.isNaN(Date.parse(`${value}T00:00:00Z`)) ? value : null; }
function isoDate() { return new Date().toISOString().slice(0, 10); }
function cleanName(value) { return typeof value === "string" ? value.trim().slice(0, 80) || "My plane display" : "My plane display"; }
function json(value, status = 200, headers = {}) { return new Response(JSON.stringify(value), { status, headers: { "Content-Type": "application/json; charset=utf-8", ...headers } }); }
function unix() { return Math.floor(Date.now() / 1000); }
function randomToken() { const bytes = crypto.getRandomValues(new Uint8Array(32)); return btoa(String.fromCharCode(...bytes)).replaceAll("+", "-").replaceAll("/", "_").replaceAll("=", ""); }
function isToken(value) { return typeof value === "string" && /^[A-Za-z0-9_-]{40,}$/.test(value); }
export async function hash(value) { const bytes = await crypto.subtle.digest("SHA-256", encoder.encode(value)); return `sha256:${[...new Uint8Array(bytes)].map(b => b.toString(16).padStart(2, "0")).join("")}`; }
// D1 remains the authority. This cap only makes abuse expensive for an attacker, not the database.
function take(key, cap, periodSeconds) { const now = unix(), item = memoryLimits.get(key); if (!item || item.until <= now) { memoryLimits.set(key, { count: 1, until: now + periodSeconds }); return true; } if (item.count >= cap) return false; item.count++; return true; }
function clientIp(request) { return request.headers.get("CF-Connecting-IP") || request.headers.get("X-Forwarded-For") || "local"; }
function cookie(request, name) { return request.headers.get("Cookie")?.split(";").map(v => v.trim()).find(v => v.startsWith(`${name}=`))?.slice(name.length + 1) || null; }
function sessionCookie(token, secure) { return `plane_session=${token}; HttpOnly; SameSite=Lax; Path=/; Max-Age=${SESSION_TTL_SECONDS}${secure ? "; Secure" : ""}`; }

export class D1Store {
  constructor(db) { this.db = db; }
  async findDeviceByKeyHash(keyHash, now) { return deviceRow(await this.db.prepare("SELECT * FROM devices WHERE api_key_hash = ? OR (previous_api_key_hash = ? AND previous_key_expires_at > ?)").bind(keyHash, keyHash, now).first()); }
  async getSnapshot(id) { const r = await this.db.prepare("SELECT payload_json, fetched_at FROM flight_snapshots WHERE device_id = ?").bind(id).first(); return r && { payload: JSON.parse(r.payload_json), fetchedAt: r.fetched_at }; }
  async putSnapshot(id, payload, now) { await this.db.prepare("INSERT INTO flight_snapshots (device_id, payload_json, fetched_at) VALUES (?, ?, ?) ON CONFLICT(device_id) DO UPDATE SET payload_json=excluded.payload_json, fetched_at=excluded.fetched_at").bind(id, JSON.stringify(payload), now).run(); }
  async deleteSnapshot(id) { await this.db.prepare("DELETE FROM flight_snapshots WHERE device_id = ?").bind(id).run(); }
  async createPairing(h, id, expires) { await this.db.prepare("INSERT INTO pairing_codes (token_hash, device_id, expires_at) VALUES (?, ?, ?)").bind(h, id, expires).run(); }
  async consumePairing(h, now) { const row = await this.db.prepare("UPDATE pairing_codes SET used_at = ? WHERE token_hash = ? AND used_at IS NULL AND expires_at > ? RETURNING device_id").bind(now, h, now).first(); return row?.device_id || null; }
  async createSession(h, id, expires, now) { await this.db.prepare("INSERT INTO sessions (token_hash, device_id, expires_at, created_at) VALUES (?, ?, ?, ?)").bind(h, id, expires, now).run(); }
  async findDeviceBySessionHash(h, now) { return deviceRow(await this.db.prepare("SELECT d.* FROM sessions s JOIN devices d ON d.id=s.device_id WHERE s.token_hash=? AND s.expires_at > ?").bind(h, now).first()); }
  async updateSettings(id, s, now) { await this.db.prepare("UPDATE devices SET name=?, latitude=?, longitude=?, radius_km=?, include_grounded=?, hide_incomplete_data=?, debug_mode=?, updated_at=? WHERE id=?").bind(s.name, s.latitude, s.longitude, s.radiusKm, s.includeGrounded ? 1 : 0, s.hideIncompleteData ? 1 : 0, s.debugMode || "", now, id).run(); }
  async requestRotation(id, now) { await this.db.prepare("UPDATE devices SET rotation_requested=1, updated_at=? WHERE id=?").bind(now, id).run(); }
  async rotateKey(id, next, prior, expires, now) { await this.db.prepare("UPDATE devices SET api_key_hash=?, previous_api_key_hash=?, previous_key_expires_at=?, rotation_requested=0, updated_at=? WHERE id=?").bind(next, prior, expires, now, id).run(); }
  async deleteSession(h) { await this.db.prepare("DELETE FROM sessions WHERE token_hash=?").bind(h).run(); }
  async getAirline(code) { const r = await this.db.prepare("SELECT name, country, fetched_at FROM airline_cache WHERE icao_code=?").bind(code).first(); return r && { name: r.name, country: r.country, fetchedAt: r.fetched_at }; }
  async putAirline(code, airline) { await this.db.prepare("INSERT INTO airline_cache (icao_code, name, country, fetched_at) VALUES (?, ?, ?, ?) ON CONFLICT(icao_code) DO UPDATE SET name=excluded.name, country=excluded.country, fetched_at=excluded.fetched_at").bind(code, airline.name, airline.country, airline.fetchedAt).run(); }
  async getAircraft(icao24) { const r = await this.db.prepare("SELECT type_code, type_name, manufacturer, owner_code, owner_name, owner_country, identity_version, fetched_at FROM aircraft_cache WHERE icao24=?").bind(icao24).first(); return r && { typeCode: r.type_code, typeName: r.type_name, manufacturer: r.manufacturer, ownerCode: r.owner_code, ownerName: r.owner_name, ownerCountry: r.owner_country, identityVersion: r.identity_version, fetchedAt: r.fetched_at }; }
  async putAircraft(icao24, aircraft) { await this.db.prepare("INSERT INTO aircraft_cache (icao24, type_code, type_name, manufacturer, owner_code, owner_name, owner_country, identity_version, fetched_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT(icao24) DO UPDATE SET type_code=excluded.type_code, type_name=excluded.type_name, manufacturer=excluded.manufacturer, owner_code=excluded.owner_code, owner_name=excluded.owner_name, owner_country=excluded.owner_country, identity_version=excluded.identity_version, fetched_at=excluded.fetched_at").bind(icao24, aircraft.typeCode, aircraft.typeName, aircraft.manufacturer, aircraft.ownerCode, aircraft.ownerName, aircraft.ownerCountry, aircraft.identityVersion, aircraft.fetchedAt).run(); }
  async getTrackedFlights(deviceId, flightDate) { const query = flightDate ? "SELECT * FROM tracked_flights WHERE device_id=? AND flight_date=? ORDER BY created_at DESC" : "SELECT * FROM tracked_flights WHERE device_id=? ORDER BY flight_date ASC, created_at DESC"; const { results } = await this.db.prepare(query).bind(...(flightDate ? [deviceId, flightDate] : [deviceId])).all(); return results.map(trackedFlightRow); }
  async addTrackedFlight(entry) { await this.db.prepare("INSERT INTO tracked_flights (id, device_id, flight_number, identifier_type, status_identifier, title, flight_date, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)").bind(entry.id, entry.deviceId, entry.flightNumber, entry.identifierType, entry.statusIdentifier, entry.title, entry.flightDate, entry.createdAt).run(); }
  async deleteTrackedFlight(deviceId, id) { const result = await this.db.prepare("DELETE FROM tracked_flights WHERE id=? AND device_id=?").bind(id, deviceId).run(); return result.meta.changes > 0; }
  async getFlightStatus(id) { const r = await this.db.prepare("SELECT payload_json, fetched_at FROM flight_status_cache WHERE tracked_flight_id=?").bind(id).first(); return r && { payload: JSON.parse(r.payload_json), fetchedAt: r.fetched_at }; }
  async putFlightStatus(id, status) { await this.db.prepare("INSERT INTO flight_status_cache (tracked_flight_id, payload_json, fetched_at) VALUES (?, ?, ?) ON CONFLICT(tracked_flight_id) DO UPDATE SET payload_json=excluded.payload_json, fetched_at=excluded.fetched_at").bind(id, JSON.stringify(status.payload), status.fetchedAt).run(); }
  async deleteFlightStatus(id) { await this.db.prepare("DELETE FROM flight_status_cache WHERE tracked_flight_id=?").bind(id).run(); }
  async takeProviderQuota(provider, month, limit) { const existing = await this.db.prepare("SELECT requests FROM provider_usage WHERE provider=? AND usage_month=?").bind(provider, month).first(); if (existing && existing.requests >= limit) return false; await this.db.prepare("INSERT INTO provider_usage (provider, usage_month, requests) VALUES (?, ?, 1) ON CONFLICT(provider, usage_month) DO UPDATE SET requests=requests + 1").bind(provider, month).run(); return true; }
}

function trackedFlightRow(row) { const identifierType = row.identifier_type || "flightNumber"; return { id: row.id, deviceId: row.device_id, flightNumber: row.flight_number, identifier: row.flight_number, identifierType, statusIdentifier: row.status_identifier || null, title: row.title, flightDate: row.flight_date, createdAt: row.created_at }; }

function deviceRow(row) {
  if (!row) return null;
  return { id: row.id, name: row.name, apiKeyHash: row.api_key_hash, latitude: row.latitude, longitude: row.longitude, radiusKm: row.radius_km, includeGrounded: row.include_grounded, hideIncompleteData: row.hide_incomplete_data, debugMode: row.debug_mode, rotationRequested: row.rotation_requested };
}
