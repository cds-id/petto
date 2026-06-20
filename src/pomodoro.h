#ifndef POMODORO_H
#define POMODORO_H

/* Simple Pomodoro timer state machine.
 *
 * Cycle: WORK -> SHORT_BREAK -> WORK -> SHORT_BREAK ... and after every
 * `long_every` work sessions, a LONG_BREAK instead of a short one.
 *
 * During breaks the caller raises a full-screen block overlay. The phase
 * advances purely on wall-clock time fed via pomo_update(now).
 */

typedef enum {
    POMO_WORK = 0,
    POMO_SHORT_BREAK,
    POMO_LONG_BREAK
} PomoPhase;

typedef struct {
    int enabled;

    /* durations in seconds */
    double work_secs;
    double short_secs;
    double long_secs;
    int    long_every;     /* long break after this many work sessions */

    /* runtime */
    PomoPhase phase;
    double    phase_end;   /* monotonic timestamp when current phase ends */
    int       work_count;  /* completed work sessions */
    int       phase_changed; /* set on transition; caller clears */
} Pomodoro;

/* Initialize with minute values; start in WORK. now = monotonic seconds. */
void pomo_init(Pomodoro *p, double work_min, double short_min,
               double long_min, int long_every, double now);

/* Advance based on the current monotonic time. Sets phase_changed on a
 * transition so the caller can show/hide the block screen. */
void pomo_update(Pomodoro *p, double now);

/* True while in a break phase (caller should block the screen). */
int  pomo_is_break(const Pomodoro *p);

/* Seconds remaining in the current phase (>=0). */
double pomo_remaining(const Pomodoro *p, double now);

/* Skip the current phase immediately (e.g. user presses skip). */
void pomo_skip(Pomodoro *p, double now);

/* Human label for the current phase. */
const char *pomo_phase_label(const Pomodoro *p);

#endif
