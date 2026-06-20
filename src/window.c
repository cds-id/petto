#include "window.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrender.h>
#include <cairo/cairo-xlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Detect a running compositor via the _NET_WM_CM_S<screen> selection owner. */
static int has_compositor(Display *dpy, int screen) {
    char name[32];
    snprintf(name, sizeof name, "_NET_WM_CM_S%d", screen);
    Atom a = XInternAtom(dpy, name, False);
    return XGetSelectionOwner(dpy, a) != None;
}

/* Find a 32-bit TrueColor visual with alpha. */
static int pick_argb_visual(Display *dpy, int screen, Visual **vout,
                            int *depthout) {
    XVisualInfo tmpl;
    tmpl.screen = screen;
    tmpl.depth = 32;
    tmpl.class = TrueColor;
    int n = 0;
    XVisualInfo *vi = XGetVisualInfo(
        dpy, VisualScreenMask | VisualDepthMask | VisualClassMask, &tmpl, &n);
    for (int i = 0; i < n; i++) {
        XRenderPictFormat *pf = XRenderFindVisualFormat(dpy, vi[i].visual);
        if (pf && pf->type == PictTypeDirect && pf->direct.alphaMask) {
            *vout = vi[i].visual;
            *depthout = vi[i].depth;
            XFree(vi);
            return 1;
        }
    }
    if (vi) XFree(vi);
    return 0;
}

/* Motif hints: strip the title bar / border so the WM shows no decorations. */
typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long          input_mode;
    unsigned long status;
} MotifWmHints;
#define MWM_HINTS_DECORATIONS (1L << 1)

static void set_no_decorations(PetWindow *pw) {
    Atom motif = XInternAtom(pw->dpy, "_MOTIF_WM_HINTS", False);
    MotifWmHints h = { MWM_HINTS_DECORATIONS, 0, 0 /*no decorations*/, 0, 0 };
    XChangeProperty(pw->dpy, pw->win, motif, motif, 32, PropModeReplace,
                    (unsigned char *)&h, 5);
}

static void set_wm_hints(PetWindow *pw) {
    Display *dpy = pw->dpy;
    set_no_decorations(pw);
    /* always on top */
    Atom wmState = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom above   = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(dpy, pw->win, wmState, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&above, 1);

    /* skip taskbar + pager, treat as utility/dock-ish */
    Atom wmType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom util   = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    XChangeProperty(dpy, pw->win, wmType, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&util, 1);

    Atom skip_tb = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom skip_pg = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
    Atom states[] = { above, skip_tb, skip_pg };
    XChangeProperty(dpy, pw->win, wmState, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)states, 3);
}

int petwin_create(PetWindow *pw, int w, int h, int x, int y) {
    memset(pw, 0, sizeof *pw);
    pw->dpy = XOpenDisplay(NULL);
    if (!pw->dpy) { fprintf(stderr, "cannot open display\n"); return -1; }
    pw->screen = DefaultScreen(pw->dpy);
    pw->root   = RootWindow(pw->dpy, pw->screen);
    pw->w = w; pw->h = h; pw->x = x; pw->y = y;
    pw->composited = has_compositor(pw->dpy, pw->screen);

    if (!pick_argb_visual(pw->dpy, pw->screen, &pw->visual, &pw->depth)) {
        /* no ARGB visual: use default, alpha won't work but shape will */
        pw->visual = DefaultVisual(pw->dpy, pw->screen);
        pw->depth  = DefaultDepth(pw->dpy, pw->screen);
    }
    pw->cmap = XCreateColormap(pw->dpy, pw->root, pw->visual, AllocNone);

    XSetWindowAttributes attr;
    memset(&attr, 0, sizeof attr);
    attr.colormap          = pw->cmap;
    attr.background_pixel   = 0;
    attr.border_pixel       = 0;
    attr.override_redirect  = True;  /* bypass WM: no frame, no tiling */
    attr.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                      PointerMotionMask | StructureNotifyMask;

    pw->win = XCreateWindow(
        pw->dpy, pw->root, x, y, w, h, 0, pw->depth, InputOutput, pw->visual,
        CWColormap | CWBackPixel | CWBorderPixel | CWOverrideRedirect |
            CWEventMask,
        &attr);

    set_wm_hints(pw);

    /* No resize, fixed size hint */
    XSizeHints sh;
    memset(&sh, 0, sizeof sh);
    sh.flags = PMinSize | PMaxSize | PPosition;
    sh.min_width = sh.max_width = w;
    sh.min_height = sh.max_height = h;
    sh.x = x; sh.y = y;
    XSetWMNormalHints(pw->dpy, pw->win, &sh);

    XStoreName(pw->dpy, pw->win, "petto");

    pw->csurf = cairo_xlib_surface_create(pw->dpy, pw->win, pw->visual, w, h);
    pw->cr = cairo_create(pw->csurf);

    XMapWindow(pw->dpy, pw->win);
    XRaiseWindow(pw->dpy, pw->win);
    XFlush(pw->dpy);
    return 0;
}

void petwin_destroy(PetWindow *pw) {
    if (!pw->dpy) return;
    if (pw->cr) cairo_destroy(pw->cr);
    if (pw->csurf) cairo_surface_destroy(pw->csurf);
    if (pw->win) XDestroyWindow(pw->dpy, pw->win);
    if (pw->cmap) XFreeColormap(pw->dpy, pw->cmap);
    XCloseDisplay(pw->dpy);
    pw->dpy = NULL;
}

cairo_t *petwin_cairo(PetWindow *pw) { return pw->cr; }

void petwin_flush(PetWindow *pw) {
    cairo_surface_flush(pw->csurf);
    XFlush(pw->dpy);
}

void petwin_move(PetWindow *pw, int x, int y) {
    pw->x = x; pw->y = y;
    XMoveWindow(pw->dpy, pw->win, x, y);
}

void petwin_raise(PetWindow *pw) {
    XRaiseWindow(pw->dpy, pw->win);
}

/* Build a 1-bit shape mask from the ARGB image surface alpha channel.
 * Only meaningful in non-composited fallback; clips fully transparent px. */
void petwin_update_shape(PetWindow *pw) {
    if (pw->composited) return; /* compositor handles alpha; no clipping */

    Pixmap mask = XCreatePixmap(pw->dpy, pw->win, pw->w, pw->h, 1);
    GC gc = XCreateGC(pw->dpy, mask, 0, NULL);

    /* Read back current window pixels via a snapshot image surface.
     * Simpler approach: re-derive mask from the cairo surface contents by
     * querying alpha. We render into an ARGB32 image, then threshold. */
    cairo_surface_t *img =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw->w, pw->h);
    cairo_t *ic = cairo_create(img);
    cairo_set_source_surface(ic, pw->csurf, 0, 0);
    cairo_paint(ic);
    cairo_destroy(ic);
    cairo_surface_flush(img);

    unsigned char *data = cairo_image_surface_get_data(img);
    int stride = cairo_image_surface_get_stride(img);

    /* clear mask to 0 */
    XSetForeground(pw->dpy, gc, 0);
    XFillRectangle(pw->dpy, mask, gc, 0, 0, pw->w, pw->h);
    XSetForeground(pw->dpy, gc, 1);

    for (int yy = 0; yy < pw->h; yy++) {
        uint32_t *row = (uint32_t *)(data + yy * stride);
        for (int xx = 0; xx < pw->w; xx++) {
            if ((row[xx] >> 24) & 0xff)
                XDrawPoint(pw->dpy, mask, gc, xx, yy);
        }
    }

    XShapeCombineMask(pw->dpy, pw->win, ShapeBounding, 0, 0, mask, ShapeSet);

    XFreeGC(pw->dpy, gc);
    XFreePixmap(pw->dpy, mask);
    cairo_surface_destroy(img);
}

int petwin_handle_event(PetWindow *pw, XEvent *ev) {
    switch (ev->type) {
    case Expose:
        return 1;
    case ButtonPress:
        if (ev->xbutton.button == Button1) {
            /* double-click: two presses within 400ms */
            if (ev->xbutton.time - pw->last_click_time < 400)
                pw->dbl_click = 1;
            pw->last_click_time = ev->xbutton.time;
            pw->dragging = 1;
            pw->moved_while_drag = 0;
            pw->drag_dx = ev->xbutton.x;
            pw->drag_dy = ev->xbutton.y;
        }
        return 0;
    case ButtonRelease:
        if (ev->xbutton.button == Button1) pw->dragging = 0;
        return 0;
    case MotionNotify:
        if (pw->dragging) {
            pw->moved_while_drag = 1;
            /* x_root/y_root are absolute; subtract grab offset */
            int nx = ev->xmotion.x_root - pw->drag_dx;
            int ny = ev->xmotion.y_root - pw->drag_dy;
            petwin_move(pw, nx, ny);
        }
        return 0;
    case ConfigureNotify:
        pw->x = ev->xconfigure.x;
        pw->y = ev->xconfigure.y;
        return 0;
    }
    return 0;
}
