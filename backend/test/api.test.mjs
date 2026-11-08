import test from "node:test";
import assert from "node:assert/strict";
import { handle, hash } from "../src/worker.mjs";
import { LocalStore } from "../src/local-store.mjs";

const base = "http://local.test";
async function setup() { const key = "test-device-key-123456789012345678901234567890123"; return { key, store: new LocalStore({ id: "test", name: "Test display", apiKeyHash: await hash(key), latitude: 53.5, longitude: -113.5, radiusKm: 100, includeGrounded: false, debugMode: false, rotationRequested: false }) }; }
function request(path, options = {}) { return new Request(base + path, options); }
const liveSource = { fetch: async (url, init = {}) => {
  if (String(url).includes("/api/0/routeset")) { assert.equal(init.headers.Referer, "https://adsb.lol/"); return new Response(JSON.stringify([{ callsign: "WJA410", plausible: true, _airports: [{ iata: "YVR", lat: 49.1947, lon: -123.1792 }, { iata: "YEG", lat: 53.3097, lon: -113.58 }] }])); }
  if (String(url).includes("/v2/callsign/ACA123")) return new Response(JSON.stringify({ ac: [{ hex: "c0tracked", flight: "ACA123", r: "C-TRACK", t: "A320", alt_baro: 36000, gs: 450, track: 90, lat: 51.0, lon: -114.0 }] }));
  if (String(url).includes("api.adsbdb.com/v0/airline/WJA")) return new Response(JSON.stringify({ response: [{ name: "WestJet", icao: "WJA", country: "Canada" }] }));
  if (String(url).includes("api.adsbdb.com/v0/aircraft/c04abc")) return new Response(JSON.stringify({ response: { aircraft: { type: "737 MAX 8", manufacturer: "Boeing", registered_owner: "WestJet", registered_owner_country_name: "Canada" } } }));
  assert.match(String(url), /api\.adsb\.lol\/v2\/point/);
  return new Response(JSON.stringify({ ac: [{ hex: "c04abc", flight: "WJA410  ", r: "C-TEST", t: "B38M", alt_baro: 35000, baro_rate: -630, gs: 441, track: 67, lat: 53.6, lon: -113.6, dst: 5 }] }));
} };

test("device fetches cached flight-data contract", async () => {
  const { store, key } = await setup();
  const response = await handle(request("/flightdata", { headers: { "X-Device-Key": key } }), store, liveSource);
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.source, "adsb.lol");
  assert.equal(body.flights.length, 1);
  assert.equal(body.flights[0].altFt, 35000);
  assert.equal(body.flights[0].speedKt, 441);
  assert.equal(body.flights[0].origin, "YVR");
  assert.equal(body.flights[0].destination, "YEG");
  assert.equal(body.flights[0].airlineName, "WestJet");
  assert.equal(body.flights[0].aircraftTypeName, "737 MAX 8");
  assert.equal(body.flights[0].registeredOwner, "WestJet");
  assert.equal(Number.isFinite(body.flights[0].flightProgressPercent), true);
  assert.equal(body.flights[0].flightProgressEstimated, true);
});

test("a debug pack returns a full stable brand-test roster without calling a provider", async () => {
  const { store, key } = await setup();
  await store.updateSettings("test", { name: "Test display", latitude: 53.5, longitude: -113.5, radiusKm: 100, includeGrounded: false, debugMode: "canadian-scheduled" });
  const response = await handle(request("/flightdata", { headers: { "X-Device-Key": key } }), store, { fetch: async () => { throw new Error("debug mode must not fetch live data"); } });
  const body = await response.json();
  assert.equal(body.source, "debug:canadian-scheduled");
  assert.equal(body.flights.length, 20);
  assert.deepEqual(body.flights.slice(0, 10).map(flight => flight.flightNumber), ["WJA1", "ACA2", "JZA3", "ROU4", "WEN5", "FLE6", "POE7", "TSC8", "AKT9", "CAV10"]);
  assert.equal(new Set(body.flights.map(flight => flight.icao24)).size, 20);
});

test("an active special flight is added outside the local radius and marked for display priority", async () => {
  const { store, key } = await setup();
  await store.addTrackedFlight({ id: crypto.randomUUID(), deviceId: "test", flightNumber: "ACA123", title: "Family arrival", flightDate: new Date().toISOString().slice(0, 10), createdAt: Date.now() });
  const response = await handle(request("/flightdata", { headers: { "X-Device-Key": key } }), store, liveSource);
  const body = await response.json();
  const flight = body.flights.find(item => item.flightNumber === "ACA123");
  assert.equal(flight.isTrackedFlight, true);
  assert.equal(flight.specialFlight, true);
  assert.equal(flight.trackedFlightTitle, "Family arrival");
  assert.equal(flight.distanceKm > 100, true);
});

test("an active special tail number uses the registration lookup and is marked for display priority", async () => {
  const { store, key } = await setup();
  await store.addTrackedFlight({ id: crypto.randomUUID(), deviceId: "test", flightNumber: "C-GMPO", identifierType: "tailNumber", title: "RCMP aircraft", flightDate: new Date().toISOString().slice(0, 10), createdAt: Date.now() });
  const source = { fetch: async url => {
    if (String(url).includes("/v2/reg/C-GMPO")) return new Response(JSON.stringify({ ac: [{ hex: "c0tail", flight: "CGMPO", r: "C-GMPO", t: "PC12", alt_baro: 12000, gs: 178, track: 350, lat: 53.576, lon: -113.614 }] }));
    if (String(url).includes("/api/0/routeset")) return new Response(JSON.stringify([]));
    if (String(url).includes("/v2/point/")) return new Response(JSON.stringify({ ac: [] }));
    return new Response(JSON.stringify({ response: [] }));
  } };
  const response = await handle(request("/flightdata", { headers: { "X-Device-Key": key } }), store, source);
  const [flight] = (await response.json()).flights;
  assert.equal(flight.registration, "C-GMPO");
  assert.equal(flight.specialFlight, true);
  assert.equal(flight.trackedFlightTitle, "RCMP aircraft");
});

test("confirmed carrier corrections override a stale third-party airline record", async () => {
  const { store, key } = await setup();
  let airlineLookupCalled = false;
  const source = { fetch: async (url) => {
    if (String(url).includes("/api/0/routeset")) return new Response(JSON.stringify([]));
    if (String(url).includes("api.adsbdb.com/v0/airline/AKT")) {
      airlineLookupCalled = true;
      return new Response(JSON.stringify({ response: [{ name: "Karat", icao: "AKT", country: "Russia" }] }));
    }
    assert.match(String(url), /api\.adsb\.lol\/v2\/point/);
    return new Response(JSON.stringify({ ac: [{ flight: "AKT1605", t: "B737", lat: 53.6, lon: -113.6, dst: 5 }] }));
  } };
  const response = await handle(request("/flightdata", { headers: { "X-Device-Key": key } }), store, source);
  const [flight] = (await response.json()).flights;
  assert.equal(flight.airline, "AKT");
  assert.equal(flight.airlineName, "Canadian North");
  assert.equal(flight.airlineCountry, "Canada");
  assert.equal(airlineLookupCalled, false);
});

test("single-use QR pairing creates a browser session that can change preferences", async () => {
  const { store, key } = await setup();
  const pairing = await handle(request("/api/device/pairing", { method: "POST", headers: { "X-Device-Key": key } }), store);
  const { pairUrl } = await pairing.json();
  const claimed = await handle(request(new URL(pairUrl).pathname), store);
  assert.equal(claimed.status, 302);
  const cookie = claimed.headers.get("set-cookie").split(";")[0];
  assert.equal((await handle(request(new URL(pairUrl).pathname), store)).status, 410);
  const freshMapSource = { fetch: async (url, init) => {
    if (String(url).includes("/v2/point/")) assert.match(String(url), /\/53\.5\/\-113\.5\/53\.995/);
    return liveSource.fetch(url, init);
  } };
  const flights = await handle(request("/api/settings/flights", { headers: { Cookie: cookie } }), store, freshMapSource);
  assert.equal(flights.status, 200);
  assert.equal((await flights.json()).flights[0].flightNumber, "WJA410");
  assert.equal(await store.getSnapshot("test"), null);
  const selectedAreaSource = { fetch: async (url, init) => {
    if (String(url).includes("/v2/point/")) assert.match(String(url), /\/53\.5\/\-113\.5\/13\.498/);
    return liveSource.fetch(url, init);
  } };
  const selectedArea = await handle(request("/api/settings/flights?latitude=53.5&longitude=-113.5&radiusKm=25", { headers: { Cookie: cookie } }), store, selectedAreaSource);
  assert.equal(selectedArea.status, 200);
  assert.equal((await selectedArea.json()).flights.length, 1);
  const changed = await handle(request("/api/settings", { method: "PUT", headers: { Cookie: cookie, "Content-Type": "application/json" }, body: JSON.stringify({ name: "Kitchen sky", latitude: 51.05, longitude: -114.07, radiusKm: 50, includeGrounded: true, debugPack: "canadian-scheduled" }) }), store);
  assert.equal(changed.status, 200);
  const settings = await handle(request("/api/settings", { headers: { Cookie: cookie } }), store);
  assert.deepEqual(await settings.json(), { id: "test", name: "Kitchen sky", latitude: 51.05, longitude: -114.07, radiusKm: 50, includeGrounded: true, debugPack: "canadian-scheduled", rotationRequested: false });
  await store.putSnapshot("test", { flights: [] }, Math.floor(Date.now() / 1000));
  const added = await handle(request("/api/settings/tracked-flights", { method: "POST", headers: { Cookie: cookie, "Content-Type": "application/json" }, body: JSON.stringify({ title: "A visit", flightNumber: "wja 410", flightDate: "2030-01-02" }) }), store);
  assert.equal(added.status, 201);
  const { trackedFlight } = await added.json();
  assert.equal(trackedFlight.flightNumber, "WJA410");
  assert.equal(await store.getSnapshot("test"), null);
  const list = await handle(request("/api/settings/tracked-flights", { headers: { Cookie: cookie } }), store);
  assert.equal((await list.json()).trackedFlights.length, 1);
  await store.putSnapshot("test", { flights: [{ specialFlight: true }] }, Math.floor(Date.now() / 1000));
  await store.putFlightStatus(trackedFlight.id, { payload: { status: "scheduled" }, fetchedAt: Math.floor(Date.now() / 1000) });
  const removed = await handle(request(`/api/settings/tracked-flights/${trackedFlight.id}`, { method: "DELETE", headers: { Cookie: cookie } }), store);
  assert.equal(removed.status, 200);
  assert.deepEqual(await removed.json(), { ok: true });
  assert.equal(await store.getSnapshot("test"), null);
  assert.equal(await store.getFlightStatus(trackedFlight.id), null);
  const emptyList = await handle(request("/api/settings/tracked-flights", { headers: { Cookie: cookie } }), store);
  assert.equal((await emptyList.json()).trackedFlights.length, 0);
});

test("rotation is requested by owner and applied only after authenticated device check-in", async () => {
  const { store, key } = await setup();
  const token = "a".repeat(43); await store.createSession(await hash(token), "test", Math.floor(Date.now() / 1000) + 60, Math.floor(Date.now() / 1000));
  const owner = { Cookie: `plane_session=${token}` };
  assert.equal((await handle(request("/api/settings/request-key-rotation", { method: "POST", headers: owner }), store)).status, 200);
  const config = await handle(request("/api/device/config", { headers: { "X-Device-Key": key } }), store);
  const configBody = await config.json();
  assert.equal(configBody.name, "Test display");
  assert.equal(configBody.rotateKey, true);
  const rotated = await handle(request("/api/device/rotate", { method: "POST", headers: { "X-Device-Key": key } }), store);
  const { deviceKey } = await rotated.json();
  assert.match(deviceKey, /^[A-Za-z0-9_-]{43}$/);
  assert.equal((await handle(request("/flightdata", { headers: { "X-Device-Key": deviceKey } }), store, liveSource)).status, 200);
});

test("a live special flight carries cached optional AirLabs ETA data without affecting its ADS-B tracking", async () => {
  const { store, key } = await setup();
  await store.addTrackedFlight({ id: "status-flight", deviceId: "test", flightNumber: "WJA1098", statusIdentifier: "WS1098", identifierType: "flightNumber", title: "Phoenix", flightDate: new Date().toISOString().slice(0, 10), createdAt: Date.now() });
  let airLabsCalls = 0;
  const source = { fetch: async (url, init) => {
    if (String(url).includes("airlabs.co/api/v9/flight")) {
      airLabsCalls++;
      assert.match(String(url), /flight_iata=WS1098/);
      return new Response(JSON.stringify({ response: { status: "en-route", arr_time: "2026-11-01 10:00", arr_estimated: "2026-11-01 10:30", arr_delayed: 30 } }));
    }
    if (String(url).includes("/v2/callsign/WJA1098")) return new Response(JSON.stringify({ ac: [{ hex: "c0status", flight: "WJA1098", r: "C-ETA", t: "B38M", alt_baro: 34000, gs: 440, track: 180, lat: 44, lon: -112 }] }));
    if (String(url).includes("/api/0/routeset")) return new Response(JSON.stringify([]));
    if (String(url).includes("/v2/point/")) return new Response(JSON.stringify({ ac: [] }));
    return new Response(JSON.stringify({ response: [] }));
  } };
  const response = await handle(request("/flightdata", { headers: { "X-Device-Key": key } }), store, { ...source, AIRLABS_API_KEY: "test-key" });
  const [flight] = (await response.json()).flights;
  assert.equal(flight.flightStatus.estimatedArrival, "2026-11-01 10:30");
  assert.equal(flight.flightStatus.arrivalDelayMinutes, 30);
  assert.equal(airLabsCalls, 1);
});

test("ticket flight numbers resolve to operating ADS-B callsign choices", async () => {
  const { store } = await setup();
  const token = "b".repeat(43);
  await store.createSession(await hash(token), "test", Math.floor(Date.now() / 1000) + 60, Math.floor(Date.now() / 1000));
  const response = await handle(request("/api/settings/tracked-flights/resolve", { method: "POST", headers: { Cookie: `plane_session=${token}`, "Content-Type": "application/json" }, body: JSON.stringify({ identifier: "ws 1098", identifierType: "flightNumber" }) }), store);
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.deepEqual(body.candidates.map(candidate => candidate.identifier), ["WJA1098", "WEN1098"]);
  assert.equal(body.candidates[0].statusIdentifier, "WS1098");
});
