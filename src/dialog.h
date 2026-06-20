#ifndef DIALOG_H
#define DIALOG_H

#include "config.h"

/* Self-contained settings dialog rendered with cairo/X11 (no GTK dependency).
 * Runs a modal event loop: opens its own window, lets the user pick the pet
 * type and tune the Pomodoro timer, and writes the result back into cfg.
 *
 * Two modes:
 *   DIALOG_SETTINGS   - normal settings (opened by double-clicking the pet)
 *   DIALOG_ONBOARDING - first-run welcome; shows intro text + same controls
 *
 * Returns 1 if the user pressed Save (cfg updated + caller should persist),
 * 0 if cancelled/closed (cfg unchanged), -1 on failure to open. */

typedef enum {
    DIALOG_SETTINGS = 0,
    DIALOG_ONBOARDING
} DialogMode;

int dialog_run(Config *cfg, DialogMode mode);

#endif
