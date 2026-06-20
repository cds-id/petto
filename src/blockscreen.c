#include "blockscreen.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/Xrender.h>
#include <cairo/cairo-xlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int has_compositor(Display *dpy, int screen) {
    char name[32];
    snprintf(name, sizeof name, "_NET_WM_CM_S%d", screen);
    Atom a = XInternAtom(dpy, name, False);
    return XGetSelectionOwner(dpy, a) != None;
}

static int pick_argb_visual(Display *dpy, Visual **vout, int *depthout) {
    XVisualInfo tmpl;
    tmpl.screen = DefaultScreen(dpy);
    tmpl.depth = 32;
    tmpl.class = TrueColor;
    int n = 0;
    XVisualInfo *vi = XGetVisualInfo(
        dpy, VisualScreenMask | VisualDepthMask | VisualClassMask, &tmpl, &n);
    for (int i = 0; i < n; i++) {
        XRenderPictFormat *pf = XRenderFindVisualFormat(dpy, vi[i].visual);
        if (pf && pf->type == PictTypeDirect && pf->direct.alphaMask) {
            *vout = vi[i].visual; *depthout = vi[i].depth;
            XFree(vi); return 1;
        }
    }
    if (vi) XFree(vi);
    return 0;
}

int blockscreen_init(BlockScreen *bs, Display *dpy) {
    memset(bs, 0, sizeof *bs);
    bs->dpy = dpy;
    bs->screen = DefaultScreen(dpy);
    bs->root = RootWindow(dpy, bs->screen);
    bs->w = DisplayWidth(dpy, bs->screen);
    bs->h = DisplayHeight(dpy, bs->screen);
    bs->composited = has_compositor(dpy, bs->screen);

    if (!pick_argb_visual(dpy, &bs->visual, &bs->depth)) {
        bs->visual = DefaultVisual(dpy, bs->screen);
        bs->depth = DefaultDepth(dpy, bs->screen);
    }
    bs->cmap = XCreateColormap(dpy, bs->root, bs->visual, AllocNone);

    XSetWindowAttributes attr;
    memset(&attr, 0, sizeof attr);
    attr.colormap = bs->cmap;
    attr.background_pixel = 0;
    attr.border_pixel = 0;
    attr.override_redirect = True;   /* bypass WM, cover everything */
    attr.event_mask = ExposureMask | KeyPressMask | ButtonPressMask;

    bs->win = XCreateWindow(dpy, bs->root, 0, 0, bs->w, bs->h, 0, bs->depth,
                            InputOutput, bs->visual,
                            CWColormap | CWBackPixel | CWBorderPixel |
                                CWOverrideRedirect | CWEventMask,
                            &attr);

    bs->csurf = cairo_xlib_surface_create(dpy, bs->win, bs->visual,
                                          bs->w, bs->h);
    bs->cr = cairo_create(bs->csurf);
    return 0;
}

void blockscreen_destroy(BlockScreen *bs) {
    if (!bs->dpy) return;
    if (bs->mapped) blockscreen_hide(bs);
    if (bs->cr) cairo_destroy(bs->cr);
    if (bs->csurf) cairo_surface_destroy(bs->csurf);
    if (bs->win) XDestroyWindow(bs->dpy, bs->win);
    if (bs->cmap) XFreeColormap(bs->dpy, bs->cmap);
    bs->dpy = NULL;
}

void blockscreen_show(BlockScreen *bs) {
    if (bs->mapped) return;
    /* force exact full-screen geometry (some WMs reserve struts) */
    XMoveResizeWindow(bs->dpy, bs->win, 0, 0, bs->w, bs->h);
    XMapRaised(bs->dpy, bs->win);
    XRaiseWindow(bs->dpy, bs->win);
    /* grab keyboard so the user steps away from the keys */
    XGrabKeyboard(bs->dpy, bs->win, True, GrabModeAsync, GrabModeAsync,
                  CurrentTime);
    bs->mapped = 1;
    XFlush(bs->dpy);
}

void blockscreen_hide(BlockScreen *bs) {
    if (!bs->mapped) return;
    XUngrabKeyboard(bs->dpy, CurrentTime);
    XUnmapWindow(bs->dpy, bs->win);
    bs->mapped = 0;
    XFlush(bs->dpy);
}

int blockscreen_visible(const BlockScreen *bs) { return bs->mapped; }

Window blockscreen_window(const BlockScreen *bs) { return bs->win; }

int blockscreen_wants_skip(BlockScreen *bs, XEvent *ev) {
    if (!bs->mapped) return 0;
    if (ev->type == KeyPress) {
        KeySym ks = XLookupKeysym(&ev->xkey, 0);
        if (ks == XK_Escape) return 1;   /* Esc dismisses the break early */
    }
    if (ev->type == ButtonPress)
        return 1;   /* a deliberate click on the overlay also skips */
    return 0;
}

void blockscreen_draw(BlockScreen *bs, const char *label, double remaining,
                      double total, double t) {
    if (!bs->mapped) return;
    cairo_t *cr = bs->cr;
    double W = bs->w, H = bs->h;
    double cx = W / 2.0, cy = H / 2.0;
    const double TAU = 6.28318530718;

    /* ---- backdrop: radial gradient vignette ---- */
    double bg_a = bs->composited ? 0.90 : 1.0;
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_pattern_t *bg = cairo_pattern_create_radial(
        cx, cy, 0, cx, cy, (W > H ? W : H) * 0.75);
    cairo_pattern_add_color_stop_rgba(bg, 0.0, 0.10, 0.13, 0.20, bg_a);
    cairo_pattern_add_color_stop_rgba(bg, 1.0, 0.02, 0.03, 0.06, bg_a);
    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* break vs (rare) work color theme */
    double aR = 0.35, aG = 0.85, aB = 0.95; /* calm teal for breaks */

    double ring_r = 150.0;
    double frac = (total > 0.0) ? (remaining / total) : 0.0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    double breathe = 0.5 + 0.5 * sin(t * 1.6);

    /* ---- track ring (faint full circle) ---- */
    cairo_set_line_width(cr, 8.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
    cairo_arc(cr, cx, cy, ring_r, 0, TAU);
    cairo_stroke(cr);

    /* ---- progress ring (depletes clockwise from top) ---- */
    double a0 = -TAU / 4.0;            /* 12 o'clock */
    double a1 = a0 + TAU * frac;
    cairo_set_line_width(cr, 8.0);
    cairo_set_source_rgba(cr, aR, aG, aB, 0.95);
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx, cy, ring_r, a0, a1);
    cairo_stroke(cr);

    /* soft glow dot at the ring head */
    double hx = cx + ring_r * cos(a1), hy = cy + ring_r * sin(a1);
    cairo_set_source_rgba(cr, aR, aG, aB, 0.25 + 0.35 * breathe);
    cairo_arc(cr, hx, hy, 10.0 + 4.0 * breathe, 0, TAU);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    cairo_arc(cr, hx, hy, 4.0, 0, TAU);
    cairo_fill(cr);

    /* ---- phase label (above) ---- */
    cairo_text_extents_t ext;
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 26);
    cairo_text_extents(cr, label, &ext);
    cairo_set_source_rgba(cr, aR, aG, aB, 0.9);
    cairo_move_to(cr, cx - ext.width / 2 - ext.x_bearing, cy - 36);
    cairo_show_text(cr, label);

    /* ---- countdown mm:ss (center) ---- */
    int total_s = (int)(remaining + 0.5);
    int mm = total_s / 60, ss = total_s % 60;
    char buf[16];
    snprintf(buf, sizeof buf, "%02d:%02d", mm, ss);
    cairo_set_font_size(cr, 72);
    cairo_text_extents(cr, buf, &ext);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_move_to(cr, cx - ext.width / 2 - ext.x_bearing, cy + 26);
    cairo_show_text(cr, buf);

    /* ---- hint (bottom, gentle pulse) ---- */
    const char *hint = "Step away  -  press Esc or click to skip the break";
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 18);
    cairo_text_extents(cr, hint, &ext);
    cairo_set_source_rgba(cr, 0.7, 0.78, 0.86, 0.45 + 0.35 * breathe);
    cairo_move_to(cr, cx - ext.width / 2 - ext.x_bearing, H - 80);
    cairo_show_text(cr, hint);

    cairo_surface_flush(bs->csurf);
    XFlush(bs->dpy);
}
