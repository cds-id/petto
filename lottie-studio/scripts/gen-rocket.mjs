#!/usr/bin/env node
/* Generate a rocket Lottie animation for petto.
 * 90 frames @ 60fps (1.5s loop): hover bob + sway + flame flicker.
 * Palette matches petto's procedural rocket (red hull, blue window, flame). */
import { writeFileSync, mkdirSync } from "node:fs";

const W = 512, H = 512, FR = 60, OP = 90;
const cx = 256;

// helpers
const hold = (k) => ({ a: 0, k });

/* Build an animated property from stops [[t, value], ...].
 * Emits rlottie-valid keyframes: scalar bezier handles (i.x/o.x are numbers,
 * not arrays) and an explicit `e` (end value) on each segment. */
const seq = (stops, ix = 0.4, ox = 0.6) => {
  const k = [];
  for (let n = 0; n < stops.length; n++) {
    const [t, s] = stops[n];
    if (n < stops.length - 1) {
      k.push({
        i: { x: ix, y: 1 }, o: { x: ox, y: 0 },
        t, s, e: stops[n + 1][1],
      });
    } else {
      k.push({ t, s });   // terminal keyframe
    }
  }
  return { a: 1, k };
};

// colors (RGBA 0..1)
const RED   = [0.878, 0.227, 0.227, 1];
const REDHI = [0.96, 0.42, 0.42, 1];
const WIN   = [0.69, 0.89, 1.0, 1];
const NOZZLE= [0.50, 0.52, 0.56, 1];
const FLAME = [1.0, 0.59, 0.16, 1];
const FLTIP = [1.0, 0.91, 0.38, 1];

// transform block helper
function tr(p = [0, 0], a = [0, 0], s = [100, 100], r = 0, o = 100) {
  return { ty: "tr", p: hold(p), a: hold(a), s: hold(s), r: hold(r), o: hold(o) };
}

// a filled shape group
function group(nm, items) { return { ty: "gr", nm, it: items }; }
function fill(c, o = 100) { return { ty: "fl", nm: "fill", c: hold(c), o: hold(o) }; }
function rrect(sz, pos = [0, 0], round = 0) {
  return { ty: "rc", nm: "rc", p: hold(pos), s: hold(sz), r: hold(round) };
}
function ellipse(sz, pos = [0, 0]) {
  return { ty: "el", nm: "el", p: hold(pos), s: hold(sz) };
}

/* ---- ROCKET BODY (drawn around local origin, group placed at center) ----
 * Built from simple shapes: nose (triangle via rrect+rot is messy, use a
 * path), hull, window, fins, nozzle. We keep it shape-based for crisp scaling. */

// nose cone as a path (triangle, rounded tip)
function nosePath() {
  // points relative to body group origin (0,0 = body center)
  return {
    ty: "sh", nm: "nose",
    ks: hold({
      c: true,
      v: [[0, -150], [34, -86], [-34, -86]],
      i: [[0, 0], [0, 0], [0, 0]],
      o: [[0, 0], [0, 0], [0, 0]],
    }),
  };
}

// fins as paths (left + right)
function finPath(side) {
  const s = side; // +1 right, -1 left
  return {
    ty: "sh", nm: "fin",
    ks: hold({
      c: true,
      v: [[s * 34, 40], [s * 78, 110], [s * 34, 110]],
      i: [[0, 0], [0, 0], [0, 0]],
      o: [[0, 0], [0, 0], [0, 0]],
    }),
  };
}

// ---- flame: two stacked teardrops that we animate via scale keyframes ----
function flameLayer(name, color, baseW, baseH, sy0, sy1, phaseT) {
  // a downward ellipse used as flame; scale Y pulses for flicker
  return {
    ty: 4, nm: name, ip: 0, op: OP, st: 0,
    ks: {
      o: hold(100),
      r: hold(0),
      a: hold([0, 0, 0]),
      p: hold([cx, 360, 0]),
      s: seq([
        [0,         [100 * baseW, 100 * sy0]],
        [phaseT,    [100 * baseW * 0.86, 100 * sy1]],
        [OP * 0.66, [100 * baseW * 1.06, 100 * sy0 * 0.92]],
        [OP,        [100 * baseW, 100 * sy0]],
      ]),
    },
    shapes: [
      group("flame", [
        ellipse([baseW, baseH], [0, 0]),
        fill(color),
        tr([0, 0]),
      ]),
    ],
  };
}

// ---- the rocket body layer (bobs + sways) ----
const bodyLayer = {
  ty: 4, nm: "rocket-body", ip: 0, op: OP, st: 0,
  ks: {
    o: hold(100),
    // gentle sway rotation
    r: seq([[0, [-2.2]], [OP / 2, [2.2]], [OP, [-2.2]]]),
    a: hold([0, 0, 0]),
    // vertical bob around center
    p: seq([
      [0,      [cx, 250, 0]],
      [OP / 2, [cx, 230, 0]],
      [OP,     [cx, 250, 0]],
    ]),
    // subtle breathing squash
    s: seq([
      [0,      [100, 100]],
      [OP / 2, [98, 103]],
      [OP,     [100, 100]],
    ]),
  },
  shapes: [
    // nozzle (behind hull bottom)
    group("nozzle", [rrect([72, 26], [0, 92], 6), fill(NOZZLE), tr()]),
    // fins
    group("fin-l", [finPath(-1), fill(RED), tr()]),
    group("fin-r", [finPath(1), fill(RED), tr()]),
    // hull (rounded rect)
    group("hull", [rrect([84, 200], [0, 0], 36), fill(RED), tr()]),
    // hull highlight stripe
    group("hull-hi", [rrect([20, 180], [-26, 0], 12), fill(REDHI, 60), tr()]),
    // nose
    group("nose", [nosePath(), fill(RED), tr()]),
    // window ring
    group("win-ring", [ellipse([62, 62], [0, -36]), fill(NOZZLE), tr()]),
    // window
    group("window", [ellipse([48, 48], [0, -36]), fill(WIN), tr()]),
    // window glare
    group("win-glare", [ellipse([16, 16], [-10, -46]), fill([1,1,1,1], 70), tr()]),
  ],
};

const doc = {
  v: "5.7.0", fr: FR, ip: 0, op: OP, w: W, h: H, nm: "petto rocket",
  assets: [],
  slots: {
    bgColor:    { p: hold([0, 0, 0, 0]) },        // transparent default
    hullColor:  { p: hold(RED) },
    flameColor: { p: hold(FLAME) },
  },
  layers: [
    // flame layers render first (behind body bottom). Outer then inner.
    flameLayer("flame-outer", FLAME, 0.62, 150, 0.85, 1.25, OP * 0.28),
    flameLayer("flame-inner", FLTIP, 0.36, 96, 0.75, 1.10, OP * 0.36),
    bodyLayer,
    // background (last = bottom)
    {
      ty: 4, nm: "background", ip: 0, op: OP, st: 0,
      ks: { o: hold(100), r: hold(0), a: hold([0,0,0]), s: hold([100,100,100]), p: hold([256,256,0]) },
      shapes: [ group("bg", [ rrect([512, 512], [0, 0], 0), { ty: "fl", nm: "bgfill", c: { sid: "bgColor" }, o: hold(100) }, tr() ]) ],
    },
  ],
};

// wire slots into actual properties
// hull fill -> hullColor ; flame outer fill -> flameColor
doc.layers[2].shapes.find(g => g.nm === "hull").it.find(i => i.ty === "fl").c = { sid: "hullColor" };
doc.layers[0].shapes[0].it.find(i => i.ty === "fl").c = { sid: "flameColor" };

/* ---- normalize to full Bodymovin fields ----
 * Skottie (studio player) is lenient, but standalone rlottie needs the
 * complete schema: layer ind/ddd/ao/bm/sr/ks.sk/sa, shape group np/cix/ix/bm,
 * fill/stroke r/bm/hd, transform fields, and a `d` direction on shapes. */
function norm(d) {
  d.ddd = 0;
  d.layers.forEach((l, i) => {
    l.ddd = 0;
    l.ind = i + 1;
    l.ao = 0;
    l.bm = 0;
    l.sr = 1;
    if (l.st === undefined) l.st = 0;
    // transform needs skew/skewAxis
    l.ks.sk = l.ks.sk || { a: 0, k: 0 };
    l.ks.sa = l.ks.sa || { a: 0, k: 0 };
    (l.shapes || []).forEach(normShape);
  });
  return d;
}
function normShape(s) {
  if (s.ty === "gr") {
    s.np = (s.it || []).length;
    s.cix = 2; s.ix = 1; s.bm = 0; s.hd = false;
    s.it.forEach(normShape);
  } else if (s.ty === "fl") {
    s.r = s.r ?? 1; s.bm = s.bm ?? 0; s.hd = false;
    if (s.o === undefined) s.o = { a: 0, k: 100 };
  } else if (s.ty === "tr") {
    s.sk = s.sk || { a: 0, k: 0 };
    s.sa = s.sa || { a: 0, k: 0 };
  } else if (s.ty === "sh") {
    s.d = s.d ?? 1; s.ix = s.ix ?? 1; s.hd = false;
  } else if (s.ty === "rc" || s.ty === "el") {
    s.d = s.d ?? 1;
  }
}
norm(doc);

const dir = "public/projects/main-project/scene-1";
mkdirSync(dir, { recursive: true });
writeFileSync(`${dir}/lottie.json`, JSON.stringify(doc, null, 2));
writeFileSync(`${dir}/controls.json`, JSON.stringify({
  controls: [
    { sid: "bgColor", label: "Background color" },
    { sid: "hullColor", label: "Hull color" },
    { sid: "flameColor", label: "Flame color" },
  ],
}, null, 2));
console.log("wrote", `${dir}/lottie.json`);

/* ---- petto asset: a fully INLINE variant (no slots).
 * Standalone rlottie does not resolve Skottie slots, so slotted color
 * properties render empty/white. The studio player above keeps slots for
 * live editing; petto ships this inlined copy with concrete colors. */
const inlineDoc = JSON.parse(JSON.stringify(doc));
inlineDoc.nm = "petto rocket (inline)";
delete inlineDoc.slots;
/* Drop the background layer entirely: petto composites over the desktop, and
 * rlottie uses fill opacity (not color-alpha), so a 'transparent' bg fill
 * would render as opaque black covering the whole frame. */
inlineDoc.layers = inlineDoc.layers.filter(l => l.nm !== "background");
// hull + flame back to concrete colors
inlineDoc.layers.find(l => l.nm === "rocket-body").shapes
  .find(g => g.nm === "hull").it.find(i => i.ty === "fl").c = hold(RED);
inlineDoc.layers.find(l => l.nm === "flame-outer")
  .shapes[0].it.find(i => i.ty === "fl").c = hold(FLAME);
norm(inlineDoc);

const assetDir = "../assets";
try { mkdirSync(assetDir, { recursive: true }); } catch {}
writeFileSync(`${assetDir}/rocket.json`, JSON.stringify(inlineDoc));
console.log("wrote", `${assetDir}/rocket.json (inline, for petto)`);
