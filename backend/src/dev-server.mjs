import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { handle, hash } from "./worker.mjs";
import { LocalStore } from "./local-store.mjs";

await loadDotEnv();
const port = Number(process.env.PORT || 8787);
const deviceKey = "demo-device-key-please-change-for-real-1234567890";
const store = new LocalStore({ id: "demo-display", name: "My plane display", apiKeyHash: await hash(deviceKey), latitude: 53.5461, longitude: -113.4938, radiusKm: 100, includeGrounded: false, rotationRequested: false });
const publicDir = new URL("../public/", import.meta.url);
const localEnv = {
  OPEN_SKY_CLIENT_ID: usableSecret(process.env.OPEN_SKY_CLIENT_ID),
  OPEN_SKY_CLIENT_SECRET: usableSecret(process.env.OPEN_SKY_CLIENT_SECRET),
  AIRLABS_API_KEY: usableSecret(process.env.AIRLABS_API_KEY),
};

createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || `localhost:${port}`}`);
  if (url.pathname === "/api/demo/pair") {
    const response = await handle(new Request(`${url.origin}/api/device/pairing`, { method: "POST", headers: { "X-Device-Key": deviceKey } }), store, localEnv);
    return send(res, response);
  }
  if (url.pathname.startsWith("/api/") || url.pathname === "/flightdata" || url.pathname.startsWith("/pair/")) {
    const chunks = []; for await (const chunk of req) chunks.push(chunk);
    const response = await handle(new Request(url, { method: req.method, headers: req.headers, body: chunks.length ? Buffer.concat(chunks) : undefined }), store, localEnv);
    return send(res, response);
  }
  const requested = url.pathname === "/" || url.pathname === "/settings" ? "index.html" : url.pathname === "/display" ? "display.html" : url.pathname.slice(1);
  if (requested.includes("..")) { res.writeHead(400); return res.end("Bad request"); }
  try {
    let content = await readFile(new URL(requested, publicDir));
    const type = { ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8" }[extname(requested)] || "application/octet-stream";
    if (requested === "index.html") content = Buffer.from(content.toString().replace("</head>", "<script>window.__LOCAL_DEMO__=true;</script></head>"));
    if (requested === "display.html") content = Buffer.from(content.toString().replace("</head>", `<script>window.__LOCAL_DEMO_KEY__=${JSON.stringify(deviceKey)};</script></head>`));
    res.writeHead(200, { "Content-Type": type, "Cache-Control": "no-store" }); res.end(content);
  } catch { res.writeHead(404); res.end("Not found"); }
}).listen(port, "127.0.0.1", () => {
  console.log(`Plane display backend demo: http://localhost:${port}`);
  console.log(`Device key for curl/firmware testing: ${deviceKey}`);
});

async function send(res, response) { const headers = Object.fromEntries(response.headers.entries()); res.writeHead(response.status, headers); res.end(Buffer.from(await response.arrayBuffer())); }

async function loadDotEnv() {
  try {
    const contents = await readFile(new URL("../.env", import.meta.url), "utf8");
    for (const rawLine of contents.split(/\r?\n/)) {
      const line = rawLine.trim();
      if (!line || line.startsWith("#")) continue;
      const separator = line.indexOf("=");
      if (separator < 1) continue;
      const name = line.slice(0, separator).trim();
      const value = line.slice(separator + 1).trim().replace(/^['"]|['"]$/g, "");
      if (name && process.env[name] === undefined) process.env[name] = value;
    }
  } catch (error) {
    if (error?.code !== "ENOENT") throw error;
  }
}

function usableSecret(value) { return value && !value.startsWith("paste-your-") ? value : undefined; }
