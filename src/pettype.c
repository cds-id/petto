#include "pettype.h"
#include <math.h>
#include <string.h>

/* ---- shared fluent-animation helpers ---------------------------------- */

/* Request a frame change: starts a cross-fade from the current frame. */
static void pet_set_frame(PetState *st, int f) {
    if (f == st->frame) return;
    st->frame_prev = st->frame;
    st->frame = f;
    st->frame_mix = 0.0;
}

/* Advance the active cross-fade. dur = fade duration (s). */
static void pet_advance_mix(PetState *st, double dt, double dur) {
    if (st->frame_mix < 1.0) {
        st->frame_mix += dt / (dur > 0 ? dur : 0.001);
        if (st->frame_mix > 1.0) st->frame_mix = 1.0;
    }
}

/* Critically-damped spring toward target (smooth, no overshoot). */
static double approach(double cur, double target, double dt, double rate) {
    double k = 1.0 - exp(-rate * dt);
    return cur + (target - cur) * k;
}

/* ---- ROCKET ----------------------------------------------------------
 * 16x20 pixel grid. Rows 0..16 are the body (shared by every frame);
 * rows 17..19 are the exhaust flame, which differs per frame:
 *   frame 0 = idle (tiny flame)
 *   frame 1 = thrust A
 *   frame 2 = thrust B (flicker)
 * Palette: '.' outline  'R' hull  'W' window  'G' nozzle
 *          'F' flame-core  'f' flame-tip  ' ' transparent
 */

#define ROCKET_BODY \
    "       ..       " \
    "      .RR.      " \
    "     .RRRR.     " \
    "     .RWWR.     " \
    "    .RWWWWR.    " \
    "    .RWWWWR.    " \
    "    .RRRRRR.    " \
    "    .RRRRRR.    " \
    "    .RRRRRR.    " \
    "    .RRRRRR.    " \
    "    .RRRRRR.    " \
    "   ..RRRRRR..   " \
    "  .R.RRRRRR.R.  " \
    " .RR.RRRRRR.RR. " \
    ".RRR.RRRRRR.RRR." \
    ".RRR.GGGGGG.RRR." \
    "    .GGGGGG.    "

static const char ROCKET_IDLE[] =
    ROCKET_BODY
    "                "
    "      ffff      "
    "       ff       ";

static const char ROCKET_THRUST_A[] =
    ROCKET_BODY
    "     FFFFFF     "
    "     ffFFff     "
    "      ffff      ";

static const char ROCKET_THRUST_B[] =
    ROCKET_BODY
    "    FFFFFFFF    "
    "     FFFFFF     "
    "     ffffff     ";

static void rocket_on_key(PetState *st) {
    if (st->mode == PET_LAUNCH) return; /* committed to flight, ignore keys */
    st->energy += 0.45;
    if (st->energy > 1.0) st->energy = 1.0;

    /* charge builds only while already energized (i.e. typing fast/sustained).
     * A single key after a pause adds little; rapid keys stack quickly. */
    st->charge += 0.06 + st->energy * 0.20;
    if (st->charge >= 1.0) {
        st->charge = 1.0;
        st->mode = PET_LAUNCH;
        st->vy = -120.0; /* initial upward kick (px/s) */
    } else if (st->mode == PET_IDLE) {
        st->mode = PET_THRUST;
    }
}

static void rocket_tick(const PetType *pt, PetState *st, double dt) {
    st->t += dt;
    st->win_dy = 0.0;

    /* default transform; ticks below nudge it, springs smooth it */
    double tgt_sx = 1.0, tgt_sy = 1.0, tgt_rot = 0.0;

    if (st->mode == PET_LAUNCH) {
        st->vy -= 900.0 * dt;
        st->win_dy = st->vy * dt;
        st->energy = 1.0;
        st->charge = 1.0;

        /* fast flame flicker via cross-faded frames */
        st->anim_t += dt;
        if (st->anim_t >= 1.0 / 18.0) {
            st->anim_t = 0.0;
            pet_set_frame(st, (st->frame == 1) ? 2 : 1);
        }
        pet_advance_mix(st, dt, 0.04);
        /* stretch upward as it blasts off */
        tgt_sx = 0.86; tgt_sy = 1.22;
        st->shake_x = sin(st->t * 90.0) * 4.0;
        st->shake_y = cos(st->t * 70.0) * 4.0;
        st->sx = approach(st->sx, tgt_sx, dt, 18.0);
        st->sy = approach(st->sy, tgt_sy, dt, 18.0);
        st->rot = approach(st->rot, 0.0, dt, 12.0);
        return;
    }

    st->energy -= dt * 1.4;
    if (st->energy < 0.0) st->energy = 0.0;
    st->charge -= dt * 0.55;
    if (st->charge < 0.0) st->charge = 0.0;

    if (st->energy <= 0.12) st->mode = PET_IDLE;
    else                    st->mode = PET_THRUST;

    if (st->mode == PET_IDLE) {
        double fps = pt->idle_fps;
        st->anim_t += dt;
        if (st->anim_t >= 1.0 / fps) {
            st->anim_t = 0.0;
            pet_set_frame(st, (st->frame == 0) ? 1 : 0);
        }
        pet_advance_mix(st, dt, 0.35);   /* slow, soft idle fade */
        st->shake_x = 0.0;
        st->shake_y = sin(st->t * 2.2) * 2.0;     /* breathing float */
        tgt_sy = 1.0 + sin(st->t * 2.2) * 0.03;
        tgt_sx = 1.0 - sin(st->t * 2.2) * 0.03;
        tgt_rot = sin(st->t * 0.8) * 0.02;        /* lazy sway */
    } else {
        double fps = pt->idle_fps + st->energy * 14.0;
        st->anim_t += dt;
        if (st->anim_t >= 1.0 / fps) {
            st->anim_t = 0.0;
            pet_set_frame(st, (st->frame == 1) ? 2 : 1);
        }
        pet_advance_mix(st, dt, 0.06);   /* quick flicker fade */
        double amp = st->energy * 3.0;
        st->shake_x = sin(st->t * 60.0) * amp;
        st->shake_y = cos(st->t * 47.0) * amp;
        tgt_sy = 1.0 + st->energy * 0.10;
        tgt_sx = 1.0 - st->energy * 0.06;
        tgt_rot = sin(st->t * 38.0) * st->energy * 0.06;
    }

    /* spring transforms toward targets for buttery motion */
    st->sx  = approach(st->sx,  tgt_sx,  dt, 14.0);
    st->sy  = approach(st->sy,  tgt_sy,  dt, 14.0);
    st->rot = approach(st->rot, tgt_rot, dt, 16.0);
}

static const PetType ROCKET = {
    .name    = "rocket",
    .scale   = 4,
    .idle_fps = 2.0,
    .on_key  = rocket_on_key,
    .tick    = rocket_tick,
    .sprite  = {
        .gw = 16, .gh = 20,
        .nframes = 3,
        .frames = { ROCKET_IDLE, ROCKET_THRUST_A, ROCKET_THRUST_B },
        .npalette = 7,
        .palette = {
            { '.',  40,  40,  55, 255 }, /* outline */
            { 'R', 224,  58,  58, 255 }, /* hull red */
            { 'W', 176, 228, 255, 255 }, /* window */
            { 'G', 128, 132, 144, 255 }, /* nozzle grey */
            { 'F', 255, 150,  40, 255 }, /* flame core */
            { 'f', 255, 232,  96, 255 }, /* flame tip */
            { ' ',   0,   0,   0,   0 }, /* transparent */
        },
    },
};

const PetType *pettype_rocket(void) { return &ROCKET; }

/* ---- CAT -------------------------------------------------------------
 * 16x16. Frames: 0 idle, 1 blink (eyes closed), 2 react (ears/tail up).
 * Idle: occasional blink + lazy breathing. Typing: snaps to react with a
 * startled hop, settles back when typing stops.
 * Palette: '.' outline 'C' fur 'W' belly 'E' eye 'P' nose '-' closed-eye
 */
static const char CAT_IDLE[] =
    "  ..      ..    "
    "  .C.    .C.    "
    "  .CC....CC.    "
    " .CCCCCCCCCC.   "
    " .C.CCCCCC.C.   "
    " .CEC.CC.CEC.   "
    " .CCCCCCCCCC.   "
    " .CCC.PP.CCC.   "
    " .CCCCCCCCCC.   "
    "  .CCCCCCCC.    "
    "  .CWWWWWWC..   "
    "  .CWWWWWWC.C.  "
    "  .CWWWWWWC.CC. "
    "  .CCCCCCCC..C. "
    "  .C......C..C. "
    "   ........  .. ";

static const char CAT_BLINK[] =
    "  ..      ..    "
    "  .C.    .C.    "
    "  .CC....CC.    "
    " .CCCCCCCCCC.   "
    " .CCCCCCCCCC.   "
    " .C--C.CC--C.   "
    " .CCCCCCCCCC.   "
    " .CCC.PP.CCC.   "
    " .CCCCCCCCCC.   "
    "  .CCCCCCCC.    "
    "  .CWWWWWWC..   "
    "  .CWWWWWWC.C.  "
    "  .CWWWWWWC.CC. "
    "  .CCCCCCCC..C. "
    "  .C......C..C. "
    "   ........  .. ";

static const char CAT_REACT[] =
    "  .        .    "
    "  ..      ..    "
    "  .C......C.    "
    " .CCCCCCCCCC. . "
    " .C.CCCCCC.C..C."
    " .CEC.CC.CEC.CC."
    " .CCCCCCCCCC.C. "
    " .CCC.PP.CCC.C. "
    " .CCCCCCCCCC.C. "
    "  .CCCCCCCC.C.  "
    "  .CWWWWWWC.    "
    "  .CWWWWWWC.    "
    "  .CWWWWWWC.    "
    "  .CCCCCCCC.    "
    "  .C......C.    "
    "   ........     ";

static void cat_on_key(PetState *st) {
    st->energy += 0.5;
    if (st->energy > 1.0) st->energy = 1.0;
    st->mode = PET_THRUST;
}

static void cat_tick(const PetType *pt, PetState *st, double dt) {
    (void)pt;
    st->t += dt;
    st->energy -= dt * 1.1;
    if (st->energy < 0.0) st->energy = 0.0;

    double tgt_sx = 1.0, tgt_sy = 1.0, tgt_rot = 0.0;

    if (st->energy > 0.18) {
        /* react: ears/tail up, small startled hop */
        st->mode = PET_THRUST;
        pet_set_frame(st, 2);
        pet_advance_mix(st, dt, 0.08);
        st->shake_x = sin(st->t * 30.0) * (st->energy * 1.5);
        double hop = fabs(sin(st->t * 18.0));
        st->shake_y = -hop * (st->energy * 3.0);   /* hop */
        /* squash on landing, stretch at peak of hop */
        tgt_sy = 1.0 + hop * st->energy * 0.14;
        tgt_sx = 1.0 - hop * st->energy * 0.10;
        tgt_rot = sin(st->t * 22.0) * st->energy * 0.05;
    } else {
        st->mode = PET_IDLE;
        /* blink ~0.12s every ~3s */
        st->phase += dt;
        double cycle = fmod(st->phase, 3.0);
        pet_set_frame(st, (cycle < 0.12) ? 1 : 0);
        pet_advance_mix(st, dt, 0.10);             /* fast eyelid fade */
        st->shake_x = 0.0;
        st->shake_y = sin(st->t * 1.6) * 1.5;      /* lazy breathing */
        tgt_sy = 1.0 + sin(st->t * 1.6) * 0.04;    /* breathing belly */
        tgt_sx = 1.0 - sin(st->t * 1.6) * 0.04;
    }

    st->sx  = approach(st->sx,  tgt_sx,  dt, 12.0);
    st->sy  = approach(st->sy,  tgt_sy,  dt, 12.0);
    st->rot = approach(st->rot, tgt_rot, dt, 14.0);
}

static const PetType CAT = {
    .name     = "cat",
    .scale    = 4,
    .idle_fps = 2.0,
    .on_key   = cat_on_key,
    .tick     = cat_tick,
    .draw     = NULL,
    .sprite   = {
        .gw = 16, .gh = 16,
        .nframes = 3,
        .frames = { CAT_IDLE, CAT_BLINK, CAT_REACT },
        .npalette = 7,
        .palette = {
            { '.',  35,  30,  40, 255 },
            { 'C', 120, 120, 130, 255 },
            { 'W', 235, 235, 240, 255 },
            { 'E',  90, 220, 140, 255 },
            { 'P', 240, 150, 170, 255 },
            { '-',  35,  30,  40, 255 },
            { ' ',   0,   0,   0,   0 },
        },
    },
};

const PetType *pettype_cat(void) { return &CAT; }

/* ---- JARVIS ----------------------------------------------------------
 * Fully procedural circular "matrix" HUD: concentric rotating arc segments
 * + a pulsing core. No sprite frames; rendered via the draw() hook. Typing
 * energy speeds up rotation, brightens, and shifts cyan -> amber.
 * State use: phase = ring rotation angle, t = free time, energy = activity.
 */
#define JARVIS_PX 64   /* logical canvas size; window is this * scale */

static void jarvis_on_key(PetState *st) {
    st->energy += 0.4;
    if (st->energy > 1.0) st->energy = 1.0;
    st->mode = PET_THRUST;
}

static void jarvis_tick(const PetType *pt, PetState *st, double dt) {
    (void)pt;
    st->t += dt;
    st->energy -= dt * 0.9;
    if (st->energy < 0.0) st->energy = 0.0;
    st->mode = (st->energy > 0.12) ? PET_THRUST : PET_IDLE;
    /* rotation speed: idle slow, faster with energy */
    st->phase += dt * (0.6 + st->energy * 6.0);
    st->shake_x = st->shake_y = 0.0; /* HUD does not shake */
}

static void arc(cairo_t *cr, double cx, double cy, double r, double a0,
                double a1, double width, double R, double G, double B,
                double A) {
    cairo_set_line_width(cr, width);
    cairo_set_source_rgba(cr, R, G, B, A);
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx, cy, r, a0, a1);
    cairo_stroke(cr);
}

static void jarvis_draw(const PetType *pt, const PetState *st, cairo_t *cr,
                        int w, int h) {
    (void)pt;
    double cx = w / 2.0, cy = h / 2.0;
    double base = (w < h ? w : h) / 2.0 - 2.0;
    double e = st->energy;
    double ang = st->phase;
    double TAU = 6.28318530718;

    /* color: cyan (idle) -> amber (active) */
    double R = 0.10 + e * 0.90;
    double G = 0.85 - e * 0.35;
    double B = 1.00 - e * 0.80;
    double glow = 0.55 + e * 0.45;

    /* outer ring: 3 rotating arc segments */
    for (int i = 0; i < 3; i++) {
        double a0 = ang + i * (TAU / 3.0);
        arc(cr, cx, cy, base, a0, a0 + TAU / 5.0, 2.0 + e * 1.5,
            R, G, B, glow);
    }
    /* middle ring: counter-rotating dashed feel via 6 short ticks */
    for (int i = 0; i < 6; i++) {
        double a0 = -ang * 1.6 + i * (TAU / 6.0);
        arc(cr, cx, cy, base * 0.7, a0, a0 + 0.18, 2.0, R, G, B, glow * 0.9);
    }
    /* inner ring: thin full circle */
    arc(cr, cx, cy, base * 0.45, 0, TAU, 1.0, R, G, B, glow * 0.6);

    /* pulsing core */
    double pulse = 0.5 + 0.5 * sin(st->t * (3.0 + e * 8.0));
    double cr_r = base * (0.12 + 0.10 * pulse) + e * 4.0;
    cairo_set_source_rgba(cr, R, G, B, 0.30 + 0.50 * pulse);
    cairo_arc(cr, cx, cy, cr_r, 0, TAU);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.6 + 0.4 * pulse);
    cairo_arc(cr, cx, cy, cr_r * 0.4, 0, TAU);
    cairo_fill(cr);
}

static const PetType JARVIS = {
    .name     = "jarvis",
    .scale    = 2,
    .idle_fps = 30.0,
    .on_key   = jarvis_on_key,
    .tick     = jarvis_tick,
    .draw     = jarvis_draw,
    .sprite   = {
        /* gw*scale x gh*scale defines the window size; no frames used */
        .gw = JARVIS_PX, .gh = JARVIS_PX,
        .nframes = 0,
        .frames = { 0 },
        .npalette = 0,
        .palette = { { 0 } },
    },
};

const PetType *pettype_jarvis(void) { return &JARVIS; }

/* ---- ROCKET (LOTTIE) -------------------------------------------------
 * Same energy/launch behavior as the procedural rocket, but rendered from a
 * Lottie file via rlottie. Has no sprite frames or draw() hook; main detects
 * lottie_file and drives lottiepet. Reuses rocket on_key/tick for energy so
 * the launch-to-moon mechanic still works. */
static void lrocket_on_key(PetState *st) { rocket_on_key(st); }

static void lrocket_tick(const PetType *pt, PetState *st, double dt) {
    (void)pt;
    /* energy + launch physics, but skip sprite-frame bookkeeping */
    st->t += dt;
    st->win_dy = 0.0;
    if (st->mode == PET_LAUNCH) {
        st->vy -= 900.0 * dt;
        st->win_dy = st->vy * dt;
        st->energy = 1.0;
        st->charge = 1.0;
        st->shake_x = sin(st->t * 90.0) * 4.0;
        st->shake_y = cos(st->t * 70.0) * 4.0;
        return;
    }
    st->energy -= dt * 1.4;
    if (st->energy < 0.0) st->energy = 0.0;
    st->charge -= dt * 0.55;
    if (st->charge < 0.0) st->charge = 0.0;
    st->mode = (st->energy <= 0.12) ? PET_IDLE : PET_THRUST;
    /* gentle bob; Lottie itself carries the squash/flame motion */
    st->shake_x = 0.0;
    st->shake_y = sin(st->t * 2.2) * 2.0;
}

static const PetType ROCKET_LOTTIE = {
    .name        = "rocket-lottie",
    .scale       = 1,
    .idle_fps    = 60.0,
    .on_key      = lrocket_on_key,
    .tick        = lrocket_tick,
    .draw        = NULL,
    .lottie_file = "rocket.json",
    .lottie_px   = 256,
    .sprite      = { .gw = 1, .gh = 1, .nframes = 0, .frames = { 0 },
                     .npalette = 0, .palette = { { 0 } } },
};

const PetType *pettype_rocket_lottie(void) { return &ROCKET_LOTTIE; }

const PetType *pettype_by_name(const char *name) {
    if (!name) return pettype_rocket();
    if (strcmp(name, "rocket") == 0) return pettype_rocket();
    if (strcmp(name, "cat")    == 0) return pettype_cat();
    if (strcmp(name, "jarvis") == 0) return pettype_jarvis();
    if (strcmp(name, "rocket-lottie") == 0 || strcmp(name, "lottie") == 0)
        return pettype_rocket_lottie();
    return NULL;
}

void pet_state_reset(PetState *st) {
    memset(st, 0, sizeof *st);
    st->sx = 1.0;
    st->sy = 1.0;
    st->frame_mix = 1.0;   /* no cross-fade in progress */
}
