#ifndef PETTYPE_H
#define PETTYPE_H

#include "sprite.h"

/* A pet "type" bundles its sprite frames and behavior. Different types react
 * to typing differently. Behavior is data-driven via PetState updated each
 * frame tick and on each keystroke. */

typedef struct PetType PetType;

/* Behavior mode the pet is currently in. */
enum {
    PET_IDLE = 0,   /* calm: gentle float/bob */
    PET_THRUST,     /* reacting to typing: flame + vibrate */
    PET_LAUNCH      /* overdriven: flying off to the moon */
};

typedef struct {
    /* behavior */
    int    mode;

    /* animation */
    int   frame;        /* current (target) sprite frame index */
    int   frame_prev;   /* previous frame, for cross-fade */
    double frame_mix;   /* 0..1 blend from frame_prev -> frame */
    double anim_t;      /* seconds accumulated for frame advance */

    /* fluent transform set by tick(), consumed by main render */
    double sx, sy;      /* scale/squash (1.0 = none) */
    double rot;         /* rotation (radians) */
    double tilt_vel;    /* internal: spring velocity for tilt */

    /* reaction energy: 0..1, decays over time, bumped by keystrokes */
    double energy;

    /* sustained-fast-typing accumulator; triggers launch when full */
    double charge;

    /* launch physics */
    double vy;          /* vertical velocity (px/s, negative = up) */
    double win_dy;      /* window y-delta to apply THIS frame (main applies) */
    int    reset_request; /* set when the pet has left the screen */

    /* in-window draw offset applied at draw time (device px): bob + shake */
    double shake_x, shake_y;

    /* per-type scratch */
    double t;           /* free-running time (s) */
    double phase;       /* generic per-type accumulator (blink, spin, etc.) */
} PetState;

struct PetType {
    const char *name;
    SpriteDef   sprite;
    int         scale;           /* default upscale */
    double      idle_fps;        /* base frame rate when calm */

    /* Called once per keystroke. Implement type-specific reaction. */
    void (*on_key)(PetState *st);
    /* Called every tick (dt seconds). Advance animation, decay energy, etc. */
    void (*tick)(const PetType *pt, PetState *st, double dt);

    /* Optional procedural renderer. If non-NULL, main calls this instead of
     * blitting sprite frames (used by fully drawn types like jarvis). cr is
     * pre-cleared; w/h are the device pixel size of the window. */
    void (*draw)(const PetType *pt, const PetState *st, cairo_t *cr,
                 int w, int h);
};

/* Built-in types */
const PetType *pettype_rocket(void);
const PetType *pettype_cat(void);
const PetType *pettype_jarvis(void);

/* Lookup by name; NULL if unknown. */
const PetType *pettype_by_name(const char *name);

/* Reset state to a clean idle pose (scale 1, no active cross-fade). Use this
 * instead of memset so the fluent transform fields start at sane values. */
void pet_state_reset(PetState *st);

#endif
