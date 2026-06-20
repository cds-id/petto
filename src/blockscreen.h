#ifndef BLOCKSCREEN_H
#define BLOCKSCREEN_H

#include <X11/Xlib.h>
#include <cairo/cairo.h>

/* Full-screen "break" overlay shown during Pomodoro breaks. It grabs the
 * keyboard so the user actually steps away, draws a dimmed background with a
 * centered message + live countdown, and can be dismissed by the caller.
 *
 * Uses its own override-redirect window covering the whole screen with an
 * ARGB visual (semi-transparent) when a compositor is present, otherwise an
 * opaque fill. Created lazily on first show().
 */

typedef struct {
    Display *dpy;        /* shared with the main connection */
    int      screen;
    Window    root;
    Window    win;
    Visual   *visual;
    Colormap  cmap;
    int       depth;
    int       w, h;
    int       composited;
    int       mapped;

    cairo_surface_t *csurf;
    cairo_t         *cr;
} BlockScreen;

/* Bind to an existing display (the window connection). Returns 0 on success.
 * Does not map a window yet. */
int  blockscreen_init(BlockScreen *bs, Display *dpy);
void blockscreen_destroy(BlockScreen *bs);

/* Show / hide the overlay. show grabs keyboard; hide releases it. */
void blockscreen_show(BlockScreen *bs);
void blockscreen_hide(BlockScreen *bs);
int  blockscreen_visible(const BlockScreen *bs);

/* Redraw with the given phase label, remaining seconds, and the total phase
 * length (for the progress ring). t is monotonic time for subtle animation. */
void blockscreen_draw(BlockScreen *bs, const char *label, double remaining,
                      double total, double t);

#endif
