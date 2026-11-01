import { mkdir, readdir, rename, rm, writeFile } from "node:fs/promises";
import { basename, join } from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const source = process.argv[2];
if (!source) throw new Error("Usage: node tools/build-airline-logo-registry.mjs /path/to/source/logos");
const destination = new URL("../public/logos/airlines/", import.meta.url);
const manifestPath = new URL("../public/airline-logos.json", import.meta.url);

// A broad, deliberately curated base of carriers most likely to appear in North
// American and international ADS-B traffic. The remainder is filled from the
// same source collection to make this first local registry exactly 200 logos.
const priority = `AAL AAR AAY ABW ABY ADB ACA ADH AEA AEE AER AFR AFL AHK AIC AIE AIZ AJX ALO AMC AMX ANA APJ ARG ASA ASH AUA AUI AVA AWE AWQ AXM AZA BAW BCS BEE BEL BER BLA BMA BOV BQB BTI BTK BWA CAL CCA CEB CES CFG CFE CHH CHQ CJT CKS CLH CLX CMB CMP CND CPA CRL CSC CSN CSA CSZ CTV CXA DAL DLA DLH EAL EIN EJU ELY ENY ETD ETH EUK EVA EVE EWG EXS EZY FBU FDX FFT FIN FJI FPO GEC GFA GIA GLO GOW GSW GTI GWI HAL HKE HOP HVN IAD IAW IBE ICE IGO IRA JAF JAI JAL JAT JBU JIA JJP JKK JNA JPA JST JTA JZA KAC KAL KFR KLM KNE KQA LAM LAN LCC LDA LDM LNI LOG LOT LPE LUF MAA MAL MAS MDA MEA MNA MNB MNO MPE MSR MXA NAX NKS NOS NWA NZM OHY OMA PAL PGT PIA PNA POE PSC PVL QFA QTR RAM RJA ROT RPA RYR RZO SAA SAS SBI SEH SEJ SIA SKW SVA SWA SWG SWR TAM TAP THA THY TOM TSC TUI TVS UAE UAL UPS VDA VJC VIR VIV VLG VOI VOZ VTI VTV WEN WJA WZZ XAX XLF`.split(" ");
const files = await readdir(source);
const available = new Set(files.filter(file => /^[A-Z0-9]{3}\.png$/.test(file)).map(file => basename(file, ".png")));
const selected = [...new Set(priority.filter(code => available.has(code)))];
for (const code of [...available].sort()) if (selected.length < 200 && !selected.includes(code)) selected.push(code);
if (selected.length < 200) throw new Error(`Source only supplied ${selected.length} usable logos.`);

await mkdir(destination, { recursive: true });
await Promise.all(selected.map(async code => {
  const input = join(source, `${code}.png`);
  const output = new URL(`${code}.png`, destination);
  const optimized = new URL(`${code}.optimized.png`, destination);
  await rm(optimized, { force: true });
  await new Promise((resolve, reject) => {
    const child = spawn("sips", ["-z", "72", "72", input, "--out", fileURLToPath(optimized)], { stdio: "ignore" });
    child.on("error", reject); child.on("exit", status => status === 0 ? resolve() : reject(new Error(`sips failed for ${code}`)));
  });
  await rm(output, { force: true });
  await rename(optimized, output);
}));
await writeFile(manifestPath, `${JSON.stringify({ version: 1, format: "png", size: "72x72", source: "https://github.com/theqkash/esp32flight-logos", notice: "Airline marks are trademarks of their respective owners; bundled solely to identify carriers in this flight-tracking display.", codes: selected.sort() }, null, 2)}\n`);
console.log(`Built ${selected.length} optimized airline logos.`);
