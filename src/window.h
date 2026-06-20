#ifndef WINDOW_H
#define WINDOW_H

#include <X11/Xlib.h>
#include <cairo/cairo.h>

/* Borderless, always-on-top, click-and-drag transparent window for the pet.
 * Uses a 32-bit ARGB visual + a compositor for true alpha. If no compositor
 * is running we fall back to an XShape mask so the corners are clipped (hard
 * edges, no alpha) instead of showing a black box. */

typedef struct {
    Display  *dpy;
    int       screen;
    Window     root;
    Window     win;
    Visual    *visual;
    Colormap   cmap;
    int        depth;
    int        w, h;
    int        x, y;
    int        composited;     /* 1 if a compositor is present */

    cairo_surface_t *csurf;    /* xlib surface bound to the window */
    cairo_t         *cr;

    /* drag state */
    int  dragging;
    int  drag_dx, drag_dy;     /* pointer offset within window at grab */
} PetWindow;

/* Create the window sized w*h at position x,y. Returns 0 on success. */
int  petwin_create(PetWindow *pw, int w, int h, int x, int y);
void petwin_destroy(PetWindow *pw);

/* Cairo context for drawing the current frame (already cleared by caller). */
cairo_t *petwin_cairo(PetWindow *pw);

/* Push the drawn surface to the screen. */
void petwin_flush(PetWindow *pw);

/* Apply an XShape mask from the current ARGB pixels (only used in fallback
 * mode to clip transparent regions). Safe no-op when composited. */
void petwin_update_shape(PetWindow *pw);

/* Move window to absolute root coords. */
void petwin_move(PetWindow *pw, int x, int y);

/* Re-raise above other windows (needed under override_redirect). */
void petwin_raise(PetWindow *pw);

/* Handle a single X event (drag logic, expose). Returns 1 if a redraw is
 * needed. Pointer/Expose only; key handling is global via keyhook. */
int  petwin_handle_event(PetWindow *pw, XEvent *ev);

#endif
