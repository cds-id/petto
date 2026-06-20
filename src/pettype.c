#include "pettype.h"
#include <math.h>
#include <string.h>

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

    if (st->mode == PET_LAUNCH) {
        /* accelerate upward (negative y), strong flame, heavy shake */
        st->vy -= 900.0 * dt;          /* thrust accel */
        st->win_dy = st->vy * dt;      /* main moves the window by this */
        st->energy = 1.0;
        st->charge = 1.0;

        /* fast flame flicker + violent shake */
        st->anim_t += dt;
        if (st->anim_t >= 1.0 / 18.0) {
            st->anim_t = 0.0;
            st->frame = (st->frame == 1) ? 2 : 1;
        }
        st->shake_x = sin(st->t * 90.0) * 4.0;
        st->shake_y = cos(st->t * 70.0) * 4.0;
        return; /* reset_request raised by main when off-screen */
    }

    /* energy + charge bleed off when not typing */
    st->energy -= dt * 1.4;
    if (st->energy < 0.0) st->energy = 0.0;
    st->charge -= dt * 0.55;
    if (st->charge < 0.0) st->charge = 0.0;

    if (st->energy <= 0.12) st->mode = PET_IDLE;
    else                    st->mode = PET_THRUST;

    if (st->mode == PET_IDLE) {
        /* gentle idle: slow 2-frame bob, soft vertical float, no shake */
        double fps = pt->idle_fps;
        st->anim_t += dt;
        if (st->anim_t >= 1.0 / fps) {
            st->anim_t = 0.0;
            st->frame = (st->frame == 0) ? 1 : 0; /* idle <-> small thrust */
        }
        st->shake_x = 0.0;
        st->shake_y = sin(st->t * 2.2) * 2.0;  /* breathing float */
    } else {
        /* thrust: flame flicker + vibrate scaled by energy */
        double fps = pt->idle_fps + st->energy * 14.0;
        st->anim_t += dt;
        if (st->anim_t >= 1.0 / fps) {
            st->anim_t = 0.0;
            st->frame = (st->frame == 1) ? 2 : 1;
        }
        double amp = st->energy * 3.0;
        st->shake_x = sin(st->t * 60.0) * amp;
        st->shake_y = cos(st->t * 47.0) * amp;
    }
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

const PetType *pettype_by_name(const char *name) {
    if (!name) return pettype_rocket();
    if (strcmp(name, "rocket") == 0) return pettype_rocket();
    return NULL;
}
