#include "pomodoro.h"

static double phase_duration(const Pomodoro *p, PomoPhase ph) {
    switch (ph) {
    case POMO_WORK:        return p->work_secs;
    case POMO_SHORT_BREAK: return p->short_secs;
    case POMO_LONG_BREAK:  return p->long_secs;
    }
    return p->work_secs;
}

static void enter_phase(Pomodoro *p, PomoPhase ph, double now) {
    p->phase = ph;
    p->phase_end = now + phase_duration(p, ph);
    p->phase_changed = 1;
}

void pomo_init(Pomodoro *p, double work_min, double short_min,
               double long_min, int long_every, double now) {
    p->enabled    = 1;
    p->work_secs  = work_min  * 60.0;
    p->short_secs = short_min * 60.0;
    p->long_secs  = long_min  * 60.0;
    p->long_every = long_every > 0 ? long_every : 4;
    p->work_count = 0;
    p->phase_changed = 0;
    enter_phase(p, POMO_WORK, now);
    p->phase_changed = 0; /* don't treat the initial WORK as a transition */
}

/* Decide which phase follows the one that just ended. */
static void advance_phase(Pomodoro *p, double now) {
    if (p->phase == POMO_WORK) {
        p->work_count++;
        if (p->work_count % p->long_every == 0)
            enter_phase(p, POMO_LONG_BREAK, now);
        else
            enter_phase(p, POMO_SHORT_BREAK, now);
    } else {
        /* any break -> back to work */
        enter_phase(p, POMO_WORK, now);
    }
}

void pomo_update(Pomodoro *p, double now) {
    if (!p->enabled) return;
    if (now >= p->phase_end)
        advance_phase(p, now);
}

int pomo_is_break(const Pomodoro *p) {
    return p->enabled &&
           (p->phase == POMO_SHORT_BREAK || p->phase == POMO_LONG_BREAK);
}

double pomo_remaining(const Pomodoro *p, double now) {
    double r = p->phase_end - now;
    return r > 0 ? r : 0.0;
}

void pomo_skip(Pomodoro *p, double now) {
    if (!p->enabled) return;
    advance_phase(p, now);
}

const char *pomo_phase_label(const Pomodoro *p) {
    switch (p->phase) {
    case POMO_WORK:        return "FOCUS";
    case POMO_SHORT_BREAK: return "SHORT BREAK";
    case POMO_LONG_BREAK:  return "LONG BREAK";
    }
    return "";
}
