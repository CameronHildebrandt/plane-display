# Plane display backend

This is a locally runnable prototype of the backend and owner settings portal. It is deliberately dependency-free locally and uses the same Web Request/Response style as its Cloudflare Worker deployment target.

## Play with it now

```sh
cd backend
npm test
npm run dev
```

Open `http://localhost:8787`, then choose **Try the local demo**. That simulates the display requesting a short-lived QR pairing link; it creates a one-time browser session and takes you to the settings page.

For a browser-based display test rig, open `http://localhost:8787/display`. It starts with the local demo key, polls `/flightdata` once a minute, cycles aircraft every eight seconds, handles a requested device-key rotation, and keeps its key, local name, latest payload, and device configuration in that browser's local storage.

The local-only device key printed on startup can test the device API:

```sh
curl -H 'X-Device-Key: demo-device-key-please-change-for-real-1234567890' \
  http://localhost:8787/flightdata
```

The local server uses an in-memory preference store but fetches live nearby-aircraft states from ADSB.lol around the configured Edmonton location. It intentionally does not persist anything locally.

ADSB.lol gives the display richer live data (type and registration when broadcast) and is the primary source. Its published terms ask production users to contact them, so do that before deploying gifts at scale. If it is unavailable, the Worker falls back to OpenSky. For a higher free OpenSky allowance, create a free OpenSky API client and configure these Worker secrets:

```sh
wrangler secret put OPEN_SKY_CLIENT_ID
wrangler secret put OPEN_SKY_CLIENT_SECRET
```

With those secrets present, the fallback uses OAuth and has OpenSky's higher free daily allocation. The Worker uses a tight radius lookup and caches every result for two minutes.

For optional schedule and delay details on live special flights only, add an AirLabs key. The normal nearby-aircraft feed never calls AirLabs; a status result is cached for ten minutes and a provider failure simply omits the status line.

```sh
wrangler secret put AIRLABS_API_KEY
```

## API implemented

| Endpoint | Actor | Purpose |
| --- | --- | --- |
| `GET /flightdata` | display | Returns cached normalized flight data; requires `X-Device-Key`. |
| `POST /api/device/pairing` | display | Creates a single-use QR URL valid for five minutes. |
| `GET /pair/:token` | recipient browser | Consumes QR token and establishes a 90-day HttpOnly session. |
| `GET` / `PUT /api/settings` | paired browser | Reads or changes location, radius, and grounded-aircraft preference. |
| `POST /api/settings/tracked-flights/resolve` | paired browser | Suggests an ADS-B callsign for a ticket flight number (for example, `WS1098 → WJA1098`). |
| `GET` / `POST /api/settings/tracked-flights` | paired browser | Lists or adds a named flight number or tail number to track on a selected calendar date. |
| `DELETE /api/settings/tracked-flights/:id` | paired browser | Removes a saved special flight. |
| `POST /api/settings/request-key-rotation` | paired browser | Requests an on-device credential replacement. |
| `GET /api/device/config` | display | Gets its configuration and any pending rotation request. |
| `POST /api/device/rotate` | display | Receives a new device key after an owner-requested rotation. |

## Decisions in this first build

- **Runtime:** Cloudflare Worker, using modern JavaScript. This avoids a running server and supports the free plan later.
- **Database:** Cloudflare D1 with the ordered migrations in `migrations/`.
- **Authentication:** every display gets a 256-bit bearer API key. Browser access comes only from a display-created, random QR pairing token. Pairing tokens are single-use and expire after five minutes; browser session and key material are stored only as SHA-256 hashes.
- **Key rotation:** owner requests it in the portal; the authenticated display sees the request, asks for a new key, persists it, and gets a ten-minute old-key grace period to tolerate a lost response.
- **Route inference:** ADSB.lol is primary and OpenSky is the fallback. One batched ADSB.lol route lookup per fresh snapshot returns a possible multi-stop route. The API returns only `origin` and `destination`: for a multi-stop route, it selects the active leg from great-circle proximity and aircraft heading (for example, `ANC → ORD`, not `PVG → ORD`). When the aircraft is plausibly near that leg, `flightProgressPercent` is its great-circle position along the leg and `flightProgressEstimated` is `true`; it is intentionally not presented as timetable progress.
- **Carrier names:** `airline` remains the three-letter ICAO code, while `airlineName` and `airlineCountry` come from ADSBDB's free airline lookup and are cached in D1 for 30 days.
- **Aircraft identity:** ADSBDB also resolves each ICAO24 address into `aircraftTypeName`, `aircraftManufacturer`, `registeredOwner`, and `registeredOwnerCountry`, likewise cached for 30 days. Live fields include `dataFlags` (such as `ladd`), squawk, geometric altitude, selected altitude, signal strength, and position age.
- **Data freshness:** per-device flight snapshots are cached for 120 seconds. The display can poll once per minute without every poll consuming upstream flight-provider quota.
- **Special flights:** a recipient can save a title plus a ticket flight number (for example `WS1098`) or tail number (for example `C-GMPO`), and a calendar date. The portal suggests the matching ADS-B callsign, preserving the ticket number for AirLabs status lookups. On that date the Worker does a live global ADSB.lol callsign or registration lookup, merges any aircraft currently broadcasting into `/flightdata`, and adds `specialFlight: true` plus `trackedFlightTitle`. When `AIRLABS_API_KEY` is configured, only a live special aircraft gets optional cached schedule/ETA/delay data. Any AirLabs failure leaves the live aircraft response intact, without an arrival-status field.

## Cloudflare deployment checklist

1. Create a Workers Free account and a D1 database named `plane-display`.
2. Replace `database_id` in `wrangler.jsonc` with the D1 database ID.
3. Apply every migration in `migrations/` with Wrangler, in numeric order.
4. Create a device record during a provisioning step, putting only a SHA-256 API-key hash in `devices`.
5. Deploy the Worker with its `public/` assets. Configure a custom domain and turn on Bot Fight Mode.
6. Add Cloudflare's free edge rate-limit rule for `/pair/*`, then use the app-level limits and cache already present here.
7. Add the authenticated OpenSky credentials as Worker secrets.

### Provision a real display

After the migrations have been applied, generate a new device key and the matching hashed database record locally:

```sh
node tools/provision-device.mjs --id kitchen-display --name "Kitchen display" --latitude 53.5461 --longitude -113.4938 --radius-km 50
```

The script prints the one-time device key and an `INSERT` statement. Run that statement in the production D1 console, then paste the device key into the physical display or the browser simulator. Never commit the key or the generated SQL output.

Do not put the Worker on a paid usage plan unless you deliberately want paid overage behavior. On the Free plan, limits protect against billing surprises but an attack can still cause temporary unavailability.

### GitHub Actions deployment (separate Cloudflare account)

The repository includes `.github/workflows/deploy.yml`. It tests every push to `main`, then applies remaining D1 migrations and deploys the Worker. It intentionally uses GitHub Actions rather than Cloudflare's Git integration, so the GitHub account can remain connected to a different Cloudflare account.

Create a GitHub environment named `production` and add these environment secrets there:

- `CLOUDFLARE_ACCOUNT_ID`: the Cloudflare account ID that owns this Worker and D1 database.
- `CLOUDFLARE_API_TOKEN`: a token scoped to that account, with permission to update the existing Worker and edit the `plane-display` D1 database.

Create the `plane-display` D1 database and an empty `winglet` Worker once in the target Cloudflare dashboard before enabling the workflow. This permits a narrowly scoped deployment token: Worker **Editor** for only `winglet`, plus D1 **Edit** for only `plane-display`. Keep OpenSky and AirLabs keys as Cloudflare Worker secrets, not GitHub secrets; ordinary deploys preserve those values.
