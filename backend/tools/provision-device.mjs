import { createHash, randomBytes } from "node:crypto";

const values = Object.fromEntries(process.argv.slice(2).flatMap((argument, index, args) => argument.startsWith("--") ? [[argument.slice(2), args[index + 1]]] : []));
const id = values.id?.trim();
const name = values.name?.trim();
const latitude = Number(values.latitude);
const longitude = Number(values.longitude);
const radiusKm = Number(values["radius-km"]);

if (!id || !name || !Number.isFinite(latitude) || latitude < -90 || latitude > 90 || !Number.isFinite(longitude) || longitude < -180 || longitude > 180 || !Number.isFinite(radiusKm) || radiusKm < 1 || radiusKm > 100) {
  console.error("Usage: node tools/provision-device.mjs --id kitchen-display --name \"Kitchen display\" --latitude 53.5461 --longitude -113.4938 --radius-km 50");
  process.exit(1);
}

const key = randomBytes(32).toString("base64url");
// Match the Worker hash format exactly so a provisioned key works in D1.
const hash = `sha256:${createHash("sha256").update(key).digest("hex")}`;
const quote = value => `'${String(value).replaceAll("'", "''")}'`;
const now = Math.floor(Date.now() / 1000);

console.log("Save this device key somewhere safe; it is shown only once:\n");
console.log(key);
console.log("\nPaste this SQL into the D1 console:\n");
console.log(`INSERT INTO devices (id, name, api_key_hash, latitude, longitude, radius_km, include_grounded, rotation_requested, created_at, updated_at) VALUES (${quote(id)}, ${quote(name)}, ${quote(hash)}, ${latitude}, ${longitude}, ${radiusKm}, 0, 0, ${now}, ${now});`);
