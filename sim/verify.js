#!/usr/bin/env node
/* Headless verification for sim/index.html against the firmware logic:
 *  1. layout math == test_main_display.c assertions
 *  2. digit/text glyph ink counts == firmware/src C arrays
 *  3. full render() smoke test (no exceptions, expected ink footprint)
 * Run:  node sim/verify.js   (from the repo root)
 */
"use strict";
const fs = require("fs");
const path = require("path");

const fontJs = fs.readFileSync(path.join(__dirname, "font_data.js"), "utf8");
const html = fs.readFileSync(path.join(__dirname, "index.html"), "utf8");
const script = html.match(/<script>([\s\S]*)<\/script>/)[1];
const head = script.split("function render()")[0].split("function renderFB")[0];

let failures = 0;
function report(name, ok) {
  if (ok) { console.log("  ok   " + name); }
  else { console.error("  FAIL " + name); failures++; }
}

/* ---- 1. layout math (mirrors test_main_display.c) ---- */
{
  const FW = 48, TW = 12, GAP = 4;
  const code = `
    let l = layoutValue(6, 0, 20);
    ok(l.valid && l.startX === (20-6)*${FW} && l.endX === 20*${FW}, "layout 6/20");
    l = layoutValue(3, 100, 10);
    ok(l.valid && l.startX === 100 + (10-3)*${FW} && l.endX === 100 + 10*${FW}, "layout 3/10");
    l = layoutValue(11, 0, 10);
    ok(!l.valid && l.startX === 0 && l.endX === 10*${FW}, "layout 11/10 invalid");
    l = layoutValue(0, 0, 20);
    ok(l.valid && l.startX === l.endX, "layout empty");
    l = layoutValue(5, 0, 0);
    ok(!l.valid, "layout zero slots");
    { const base = 100; l = layoutValue(5, base, 10);
      ok(cursorX(l,0) === base + (10-5)*${FW}, "cursor0");
      ok(cursorX(l,3) === base + (10-5+3)*${FW}, "cursor3");
      ok(cursorX(l,5) === l.endX, "cursor5");
      ok(cursorX(l,99) === l.endX, "cursor clamp");
      ok(cursorX(l,6) === l.endX, "cursor6");
    }
    ok(specialColor(0) === 0xFFFF && specialColor(1) === 0xF800 &&
       specialColor(2) === 0xC618 && specialColor(9) === 0xFFFF, "special colors");
    ok(960 - ((4+3+4)*${TW} + 2*${GAP}) === 960 - (11*12 + 8), "status right-align");
  `;
  eval(fontJs + "\n" + head + "\nconst ok = (cond, name) => report(name, cond);\n" + code);
}

/* ---- 2. glyph ink counts (ground truth from firmware C arrays) ---- */
{
  const truth = { "0":1054,"1":722,"2":834,"3":857,"4":717,"5":886,"6":875,"7":707,
                  "8":1097,"9":880,".":137,"+":484,"-":156,"E":881,"e":820,"%":1058,
                  "m":960,"u":695,"k":829,"K":994,"M":1129,"W":1283,"V":818,"O":974,
                  "h":883,"D":1050,"A":904,"C":830 };
  const code = `
    for (const [ch, expect] of Object.entries(${JSON.stringify(truth)})) {
      const bmp = digitBitmap(ch);
      let ink = 0;
      for (let y = 0; y < 96; y++) for (let x = 0; x < 48; x++)
        if (bmp[y*6 + (x>>3)] & (0x80 >> (x & 7))) ink++;
      ok(ink === expect, "digit '" + ch + "' ink " + ink + " == C " + expect);
    }
    for (const ch of ["?","V","D","C","d","B","m","-","."]) {
      const bmp = textBitmap(ch);
      let ink = 0;
      for (let y = 0; y < 24; y++) for (let x = 0; x < 12; x++)
        if (bmp[y*2 + (x>>3)] & (0x80 >> (x & 7))) ink++;
      ok(bmp && ink > 0, "text glyph '" + ch + "' has ink");
    }
  `;
  eval(fontJs + "\n" + head + "\nconst ok = (cond, name) => report(name, cond);\n" + code);
}

/* ---- 3. full render() smoke test ---- */
{
  const makeCtx = () => {
    const px = new Uint8Array(960 * 320);
    return { px, fillStyle: "",
      fillRect(x, y, w, h) {
        for (let yy = y; yy < y + h; yy++) for (let xx = x; xx < x + w; xx++)
          if (xx >= 0 && yy >= 0 && xx < 960 && yy < 320)
            px[yy * 960 + xx] = (this.fillStyle === "#000") ? 0 : 1;
      },
      clearRect() {}, getImageData() { return { data: new Uint8ClampedArray(960 * 320 * 4) }; },
      createImageData(w, h) { return { data: new Uint8ClampedArray(w * h * 4) }; },
      putImageData() {}, drawImage() {},
    };
  };
  const panelCtx = makeCtx();
  const fbCtx = makeCtx();
  const makeEl = extra => Object.assign({ value:"", checked:false, dataset:{},
                                          addEventListener(){}, textContent:"" }, extra);
  global.document = {
    getElementById(id) {
      const map = {
        inpValue: makeEl({ value: "1.000000" }), inpUnit: makeEl({ value: "VDC" }),
        selSpecial: makeEl({ value: "0" }), inpNoData: makeEl({ checked: false }),
        inpBlink: makeEl({ checked: true }), inpPos: makeEl({ value: "2" }),
        posVal: makeEl({}), panel: { getContext: () => panelCtx },
        fb: { getContext: () => fbCtx },
      };
      return map[id] || makeEl({});
    },
    querySelectorAll() { return []; },
    createElement() { return { width:0, height:0, getContext:() => makeCtx() }; },
  };
  global.window = { __blinkVis: true };
  global.setInterval = () => 0;
  /* 布局参数 API：set / adjust / clamp / reset / export（Task 1） */
  const paramTests = `
    report("layout param API", typeof setLayoutParam === "function" &&
                               typeof adjustLayoutParam === "function");
    setLayoutParam("topBandH", 40);
    report("layout param set", L.topBandH === 40);
    adjustLayoutParam("topBandH", -1000);
    report("layout param clamp", L.topBandH === 8);
    resetLayout();
    report("layout param reset", L.topBandH === 24 && L.cursorH === 4);
    report("layout export", /MAIN_DISPLAY_TOP_BAND_H 24u/.test(exportLayoutText()));
  `;
  eval(fontJs + "\n" + script + "\n" + paramTests);

  let lit = 0;
  for (const v of panelCtx.px) if (v) lit++;
  report("normal-reading ink in range", lit > 6000 && lit < 20000);
  let minY = 999, maxY = -1;
  for (let y = 0; y < 320; y++) for (let x = 0; x < 960; x++)
    if (panelCtx.px[y * 960 + x]) { if (y < minY) minY = y; if (y > maxY) maxY = y; }
  report("top-band text near y=0", minY <= 24);
  report("big digits reach y~119", maxY >= 119);
}

if (failures) { console.error(`\n${failures} check(s) FAILED`); process.exit(1); }
console.log("\nsim/verify.js: ALL CHECKS PASS");