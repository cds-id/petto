#!/usr/bin/env node
/* Generate an HD rocket Lottie animation for petto.
 *
 * 90 frames @ 60fps (1.5s loop). Features:
 *   - gradient-shaded hull + nose (cylindrical light)
 *   - gradient window glass with glare + ring
 *   - shaded fins
 *   - metallic nozzle bell
 *   - LAYERED THRUSTER: outer orange cone, mid yellow, white-hot core,
 *     all flickering + pulsing in length, plus animated exhaust sparks
 *   - whole rocket hovers (bob), sways, and breathes
 *
 * Two outputs:
 *   - lottie-studio scene (with slots, for the Skottie player)
 *   - assets/rocket.json (inlined, rlottie-correct, for petto)
 */
import { writeFileSync, mkdirSync } from "node:fs";

const W = 512, H = 512, FR = 60, OP = 90;
const cx = 256;

/* ---- value helpers ---- */
const hold = (k) => ({ a: 0, k });

/* animated property from stops [[t,value],...]; rlottie-valid keyframes
 * (scalar bezier handles + explicit end value per segment). */
const seq = (stops, ix = 0.42, ox = 0.58) => {
  const k = [];
  for (let n = 0; n < stops.length; n++) {
    const [t, s] = stops[n];
    if (n < stops.length - 1)
      k.push({ i: { x: ix, y: 1 }, o: { x: ox, y: 0 }, t, s, e: stops[n + 1][1] });
    else k.push({ t, s });
  }
  return { a: 1, k };
};

/* ---- palette (RGBA 0..1) ---- */
const RED_HI = [0.96, 0.40, 0.40, 1];
const RED    = [0.86, 0.20, 0.20, 1];
const RED_LO = [0.55, 0.10, 0.12, 1];
const WIN_HI = [0.80, 0.95, 1.00, 1];
const WIN    = [0.30, 0.62, 0.92, 1];
const WIN_LO = [0.12, 0.30, 0.55, 1];
const STEEL_HI = [0.78, 0.80, 0.85, 1];
const STEEL  = [0.42, 0.45, 0.50, 1];
const STEEL_LO = [0.20, 0.22, 0.26, 1];
const FIN    = [0.70, 0.16, 0.16, 1];
const FLAME_O = [1.00, 0.45, 0.10, 1];   // outer orange
const FLAME_M = [1.00, 0.72, 0.18, 1];   // mid amber
const FLAME_Y = [1.00, 0.90, 0.40, 1];   // yellow
const FLAME_C = [1.00, 1.00, 0.92, 1];   // white-hot core

/* ---- shape primitives ---- */
const group = (nm, items) => ({ ty: "gr", nm, it: items });
const fill  = (c, o = 100) => ({ ty: "fl", nm: "fill", c: hold(c), o: hold(o) });
const rrect = (sz, pos = [0, 0], round = 0) =>
  ({ ty: "rc", nm: "rc", p: hold(pos), s: hold(sz), r: hold(round) });
const ellipse = (sz, pos = [0, 0]) =>
  ({ ty: "el", nm: "el", p: hold(pos), s: hold(sz) });
const tr = (p = [0, 0], a = [0, 0], s = [100, 100], r = 0, o = 100) =>
  ({ ty: "tr", p: hold(p), a: hold(a), s: hold(s), r: hold(r), o: hold(o) });

/* linear gradient fill. stops: [[off, [r,g,b,a]], ...]; s/e are endpoints. */
function grad(stops, s, e, type = 1, o = 100) {
  const flat = [];
  for (const [off, c] of stops) flat.push(off, c[0], c[1], c[2]);
  return {
    ty: "gf", nm: "grad", o: hold(o), r: 1, bm: 0, hd: false,
    g: { p: stops.length, k: hold(flat) },
    s: hold(s), e: hold(e), t: type,
  };
}

/* closed bezier path from vertices (auto smooth via tangents if given). */
function path(verts, inT, outT, closed = true) {
  const n = verts.length;
  const zeros = () => verts.map(() => [0, 0]);
  return {
    ty: "sh", nm: "sh",
    ks: hold({ c: closed, v: verts, i: inT || zeros(), o: outT || zeros() }),
  };
}

/* ---- NOSE CONE (smooth, gradient) ----
 * Curved ogive nose: tip at -150, shoulders at -78. */
function noseShape() {
  const v = [[0, -152], [42, -78], [-42, -78]];
  // gentle outward curve on the sides
  const i = [[0, 0], [-10, -24], [10, -24]];
  const o = [[0, 0], [10, 24], [-10, 24]];
  return path(v, i, o, true);
}

/* ---- FIN (swept, with inner shade overlay) ---- */
function finShape(side) {
  const s = side;
  const v = [[s * 30, 36], [s * 86, 120], [s * 30, 104]];
  const i = [[0, 0], [s * -6, -18], [0, 0]];
  const o = [[0, 0], [s * 6, 18], [0, 0]];
  return path(v, i, o, true);
}

/* ---- THRUSTER FLAME LAYER ----
 * A teardrop cone (path) pointing down from the nozzle. Animated:
 *   - scale Y pulses (flame length flicker)
 *   - scale X jitter (width flicker)
 *   - opacity shimmer
 * baseLen/baseW set the cone size; phase offsets the flicker per layer. */
function flameCone(name, color, baseW, baseLen, phase, opa = 100) {
  // teardrop: rounded top at y=0, pointed tip at y=baseLen
  const v = [[0, 0], [baseW, baseLen * 0.34], [0, baseLen], [-baseW, baseLen * 0.34]];
  const i = [[baseW * 0.7, 0], [0, baseLen * 0.16], [baseW * 0.5, 0], [0, -baseLen * 0.16]];
  const o = [[-baseW * 0.7, 0], [0, -baseLen * 0.16], [-baseW * 0.5, 0], [0, baseLen * 0.16]];

  const p1 = (phase) % OP, p2 = (phase + OP * 0.5) % OP;
  return {
    ty: 4, nm: name, ip: 0, op: OP, st: 0,
    ks: {
      o: seq([[0, [opa]], [OP * 0.3, [opa * 0.82]], [OP * 0.6, [opa]], [OP, [opa]]]),
      r: hold(0),
      a: hold([0, 0, 0]),
      p: hold([cx, 308, 0]),                       // anchored at nozzle mouth
      s: seq([
        [0,          [100, 105]],
        [OP * 0.22,  [86, 124]],                    // long + narrow
        [OP * 0.46,  [114, 84]],                    // short + fat
        [OP * 0.72,  [92, 114]],
        [OP,         [100, 105]],
      ]),
    },
    shapes: [ group("flame", [ path(v, i, o, true), fill(color), tr([0, 0]) ]) ],
  };
}

/* exhaust spark: a small dot that falls + fades, looping. */
function spark(name, x0, sizePx, delay, speed) {
  const startY = 306, endY = 452;
  return {
    ty: 4, nm: name, ip: 0, op: OP, st: 0,
    ks: {
      o: seq([
        [0, [0]], [delay, [0]], [delay + 4, [90]],
        [Math.min(delay + speed, OP), [0]], [OP, [0]],
      ]),
      r: hold(0),
      a: hold([0, 0, 0]),
      p: seq([
        [0, [cx + x0, startY, 0]],
        [delay, [cx + x0, startY, 0]],
        [Math.min(delay + speed, OP), [cx + x0 * 1.6, endY, 0]],
        [OP, [cx + x0 * 1.6, endY, 0]],
      ]),
      s: hold([100, 100]),
    },
    shapes: [ group("spark", [ ellipse([sizePx, sizePx], [0, 0]), fill(FLAME_Y), tr() ]) ],
  };
}

/* ---- ROCKET BODY ---- */
const bodyLayer = {
  ty: 4, nm: "rocket-body", ip: 0, op: OP, st: 0,
  ks: {
    o: hold(100),
    r: seq([[0, [-2.4]], [OP / 2, [2.4]], [OP, [-2.4]]]),
    a: hold([0, 0, 0]),
    p: seq([[0, [cx, 224, 0]], [OP / 2, [cx, 204, 0]], [OP, [cx, 224, 0]]]),
    s: seq([[0, [100, 100]], [OP / 2, [98.5, 102]], [OP, [100, 100]]]),
  },
  shapes: [
    // nozzle bell (metallic gradient), behind hull bottom
    group("nozzle", [
      path([[-40, 96], [40, 96], [30, 60], [-30, 60]],
           [[0,0],[0,0],[0,0],[0,0]], [[0,0],[0,0],[0,0],[0,0]], true),
      grad([[0, STEEL_HI], [0.5, STEEL], [1, STEEL_LO]], [-40, 60], [40, 96], 1),
      tr(),
    ]),
    // fins (behind hull)
    group("fin-l", [finShape(-1), fill(FIN), tr()]),
    group("fin-r", [finShape(1), fill(FIN), tr()]),
    // hull: gradient cylinder (left light -> right shade)
    group("hull", [
      rrect([92, 210], [0, -6], 40),
      grad([[0, RED_HI], [0.45, RED], [1, RED_LO]], [-46, 0], [46, 0], 1),
      tr(),
    ]),
    // hull belly band (decorative ring)
    group("band", [rrect([94, 16], [0, 58], 6),
      grad([[0, STEEL_HI], [1, STEEL_LO]], [-47, 0], [47, 0], 1), tr()]),
    // nose cone (gradient)
    group("nose", [noseShape(),
      grad([[0, RED_HI], [1, RED_LO]], [-42, -120], [42, -78], 1), tr()]),
    // window ring (steel)
    group("win-ring", [ellipse([70, 70], [0, -44]), fill(STEEL), tr()]),
    // window glass (radial gradient)
    group("window", [ellipse([54, 54], [0, -44]),
      grad([[0, WIN_HI], [0.5, WIN], [1, WIN_LO]], [0, -44], [27, -17], 2), tr()]),
    // window glare
    group("glare", [ellipse([18, 22], [-11, -54]), fill([1, 1, 1, 1], 80), tr()]),
  ],
};

/* ---- assemble document ---- */
const doc = {
  v: "5.7.0", fr: FR, ip: 0, op: OP, w: W, h: H, nm: "petto rocket HD",
  assets: [],
  slots: {
    bgColor:    { p: hold([0, 0, 0, 0]) },
    hullColor:  { p: hold(RED) },
    flameColor: { p: hold(FLAME_O) },
  },
  layers: [
    // sparks (frontmost of the exhaust, but behind body bottom visually fine)
    spark("spark-1", -8, 7, OP * 0.05, OP * 0.55),
    spark("spark-2",  10, 6, OP * 0.32, OP * 0.5),
    spark("spark-3", -2, 8, OP * 0.6, OP * 0.6),
    // thruster flame: outer -> core (drawn back to front)
    flameCone("flame-outer", FLAME_O, 58, 156, OP * 0.0, 90),
    flameCone("flame-mid",   FLAME_M, 42, 126, OP * 0.18, 96),
    flameCone("flame-yellow",FLAME_Y, 28, 96,  OP * 0.34, 100),
    flameCone("flame-core",  FLAME_C, 15, 62,  OP * 0.5, 100),
    // rocket on top
    bodyLayer,
    // background (studio only; stripped for petto)
    {
      ty: 4, nm: "background", ip: 0, op: OP, st: 0,
      ks: { o: hold(100), r: hold(0), a: hold([0,0,0]), s: hold([100,100,100]), p: hold([256,256,0]) },
      shapes: [ group("bg", [ rrect([512, 512], [0, 0], 0),
        { ty: "fl", nm: "bgfill", c: { sid: "bgColor" }, o: hold(100) }, tr() ]) ],
    },
  ],
};

/* wire slots (studio editing) */
const L = (nm) => doc.layers.find(l => l.nm === nm);
// hull gradient can't be slotted simply; expose flame outer color + bg
L("flame-outer").shapes[0].it.find(i => i.ty === "fl").c = { sid: "flameColor" };

/* ---- normalize to full Bodymovin schema (rlottie strictness) ---- */
function norm(d) {
  d.ddd = 0;
  d.layers.forEach((l, i) => {
    l.ddd = 0; l.ind = i + 1; l.ao = 0; l.bm = 0; l.sr = 1;
    if (l.st === undefined) l.st = 0;
    l.ks.sk = l.ks.sk || { a: 0, k: 0 };
    l.ks.sa = l.ks.sa || { a: 0, k: 0 };
    (l.shapes || []).forEach(normShape);
  });
  return d;
}
function normShape(s) {
  if (s.ty === "gr") {
    s.np = (s.it || []).length; s.cix = 2; s.ix = 1; s.bm = 0; s.hd = false;
    s.it.forEach(normShape);
  } else if (s.ty === "fl") {
    s.r = s.r ?? 1; s.bm = s.bm ?? 0; s.hd = false;
    if (s.o === undefined) s.o = { a: 0, k: 100 };
  } else if (s.ty === "gf") {
    s.bm = s.bm ?? 0; s.hd = false; s.r = s.r ?? 1;
  } else if (s.ty === "tr") {
    s.sk = s.sk || { a: 0, k: 0 }; s.sa = s.sa || { a: 0, k: 0 };
  } else if (s.ty === "sh") {
    s.d = s.d ?? 1; s.ix = s.ix ?? 1; s.hd = false;
  } else if (s.ty === "rc" || s.ty === "el") {
    s.d = s.d ?? 1;
  }
}
norm(doc);

/* ---- write studio scene ---- */
const dir = "public/projects/main-project/scene-1";
mkdirSync(dir, { recursive: true });
writeFileSync(`${dir}/lottie.json`, JSON.stringify(doc, null, 2));
writeFileSync(`${dir}/controls.json`, JSON.stringify({
  controls: [
    { sid: "bgColor", label: "Background color" },
    { sid: "flameColor", label: "Flame color" },
  ],
}, null, 2));
console.log("wrote", `${dir}/lottie.json`);

/* ---- write petto asset (inlined, no slots, no bg) ---- */
const inlineDoc = JSON.parse(JSON.stringify(doc));
inlineDoc.nm = "petto rocket HD (inline)";
delete inlineDoc.slots;
inlineDoc.layers = inlineDoc.layers.filter(l => l.nm !== "background");
inlineDoc.layers.find(l => l.nm === "flame-outer")
  .shapes[0].it.find(i => i.ty === "fl").c = hold(FLAME_O);
norm(inlineDoc);

const assetDir = "../assets";
try { mkdirSync(assetDir, { recursive: true }); } catch {}
writeFileSync(`${assetDir}/rocket.json`, JSON.stringify(inlineDoc));
console.log("wrote", `${assetDir}/rocket.json (inline, for petto)`);
