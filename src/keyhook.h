#ifndef KEYHOOK_H
#define KEYHOOK_H

/* Global keystroke capture via the X RECORD extension. RECORD requires its
 * own dedicated display connection (the "data" connection) separate from the
 * one used to draw the window. We expose the data connection's fd so the main
 * loop can select() on it alongside the window connection. */

typedef void (*KeyhookCb)(int keycode, int pressed, void *user);

typedef struct Keyhook Keyhook;

/* Create the hook. ctrl_dpy_name should match the main display (NULL = $DISPLAY).
 * Returns NULL on failure (e.g. RECORD extension unavailable). */
Keyhook *keyhook_create(KeyhookCb cb, void *user);
void     keyhook_destroy(Keyhook *kh);

/* fd of the RECORD data connection, for select(). */
int  keyhook_fd(const Keyhook *kh);

/* Drain pending records on the data connection; invokes cb per key event. */
void keyhook_process(Keyhook *kh);

#endif
