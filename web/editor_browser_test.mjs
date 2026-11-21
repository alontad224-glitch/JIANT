// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
// (SMK-37, ports/) the editor in a real browser (Playwright's Chromium) on its mock device (?mock=1: Web MIDI faked by
// the page itself):
//   node web/editor_browser_test.mjs [out-dir-for-screenshots]
// An FM-1 (no 'D' / 'H' tags): "FM-1" shown, no SAMPLE / GRAIN offered, no sample pages on DRUM. An SMK-37 ('D' 1 1):
// its name. A big CPU (?hp=1: 'D' 1 3, 'H' 2 15 16): SAMPLE and GRAIN offered, their pages (SAMPLE WARP TRIG REC,
// GRAIN CLOUD TRAVEL SOURCE PITCH) and DRUM-X's SAMPLES; the FILTER section of P_COUNT 123 firmware.
import path from "path";
import { createRequire } from "module";
const require = createRequire(import.meta.url);
const { chromium } = require("playwright");

const HERE = path.dirname(new URL(import.meta.url).pathname);
const OUT = process.argv[2] || null;
const page0 = "file://" + path.join(HERE, "editor.html");
let fails = 0;
const check = (what, ok, extra = "") => { console.log(`editor (browser): ${what.padEnd(70)} ${ok ? "ok" : "FAIL"} ${extra}`); if (!ok) fails++; };

const b = await chromium.launch();
async function open(qs) {
  const p = await b.newPage({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: 2 });
  p.on("pageerror", (e) => { console.log("pageerror:", e.message); fails++; });
  await p.goto(`${page0}?mock=1${qs}`);
  await p.click("#connect");
  await p.waitForFunction(() => /·/.test(document.querySelector("#version").textContent), null, { timeout: 60000 });
  return p;
}
const heads = (p) => p.$$eval("#groups h2, #groups h3", (a) => a.map((x) => x.textContent.replace(/[^\x20-\x7e]/g, "").trim()));   /* (an icon's glyph out) */
const engines = (p) => p.$$eval("#engine option", (a) => a.map((x) => x.textContent));
/* the controls are rebuilt as the replies come: wait until the headings hold what is expected (or 20 s) */
async function settle(p, want = []) {
  let last = "", same = 0;
  for (let i = 0; i < 200 && same < 10; i++) {
    await p.waitForTimeout(100);
    const h = await heads(p), now = JSON.stringify(h);
    same = now === last && want.every((x) => h.includes(x)) ? same + 1 : 0;
    last = now;
  }
}
async function engine(p, e, want) {
  await p.selectOption("#engine", String(e));
  await settle(p, want);
}

{
  const p = await open("");
  await settle(p, ["FILTER"]);
  const h = await heads(p), en = await engines(p);
  check("FM-1: its name, the structured layout (ENV .. FILTER)", (await p.textContent("#version")).startsWith("FM-1 ·") &&
    h.includes("ENV") && h.includes("FILTER"));
  check("FM-1: no SAMPLE / GRAIN to pick", !en.includes("SAMPLE") && !en.includes("GRAIN"));
  await engine(p, 10, ["EDIT DRUM"]);
  check("FM-1: DRUM without the sample pages", !(await heads(p)).includes("SAMPLES"));
  await p.close();
}
{
  const p = await open("&board=1&port=JIANT%20SMK-37");
  check("SMK-37: shown by its tag", (await p.textContent("#version")).startsWith("SMK-37 Pro ·"));
  await p.close();
}
{
  const p = await open("&hp=1");
  check("big CPU: shown, SAMPLE and GRAIN offered", (await p.textContent("#version")).startsWith("JIANT FM app / plugin ·") &&
    (await engines(p)).slice(-2).join() === "SAMPLE,GRAIN");
  for (const [e, name, want] of [[4, "sample", ["EDIT SAMPLE", "SAMPLE", "WARP", "TRIG", "REC"]],
    [8, "grain", ["EDIT GRAIN", "GRAIN", "CLOUD", "TRAVEL", "SOURCE", "PITCH"]], [10, "drum", ["EDIT DRUM", "SAMPLES", "SAMPLES 2"]]]) {
    await engine(p, e, want);
    const h = await heads(p);
    check(`big CPU: ${name.toUpperCase()}'s pages (${want.slice(1).join(" ")})`, want.every((x) => h.includes(x)), want.filter((x) => !h.includes(x)).join());
    if (OUT) await p.screenshot({ path: path.join(OUT, `editor-${name}.png`), fullPage: true });
  }
  const labels = await p.$$eval("#groups .row", (a) => a.map((x) => x.textContent));
  check("big CPU: DRUM-X's SMORPH BLEND SLOT MAP SLEVEL", ["SMORPH", "BLEND", "MAP", "SLEVEL"].every((l) => labels.some((t) => t.startsWith(l))));
  await engine(p, 0, ["EDIT ANALOG"]);
  check("big CPU: ANALOG without them", !(await heads(p)).some((x) => ["TRIG", "TRAVEL", "SAMPLES"].includes(x)));
  await p.close();
}
await b.close();
console.log(fails ? `EDITOR BROWSER TEST FAILED (${fails})` : "editor browser test passed");
process.exit(fails ? 1 : 0);
