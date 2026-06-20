#define _POSIX_C_SOURCE 200809L
#include "dialog.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <cairo/cairo-xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- layout constants ---------------------------------------------------- */
#define DW 420
#define DH 460

/* A clickable rectangle with an id. */
typedef struct { double x, y, w, h; int id; } Hit;

enum {
    ID_NONE = 0,
    ID_PET_ROCKET, ID_PET_CAT, ID_PET_JARVIS,
    ID_POMO_TOGGLE,
    ID_WORK_DN, ID_WORK_UP,
    ID_SHORT_DN, ID_SHORT_UP,
    ID_LONG_DN, ID_LONG_UP,
    ID_EVERY_DN, ID_EVERY_UP,
    ID_SAVE, ID_CANCEL
};

typedef struct {
    Display *dpy;
    int screen;
    Window win;
    Visual *visual;
    Colormap cmap;
    int depth;
    cairo_surface_t *csurf;
    cairo_t *cr;
    Hit hits[32];
    int nhits;
    int hover;   /* id under pointer */
} Dlg;

/* ---- small drawing helpers ---------------------------------------------- */
static void rrect(cairo_t *cr, double x, double y, double w, double h,
                  double r) {
    double k = 0.5522847498 * r;
    cairo_move_to(cr, x + r, y);
    cairo_line_to(cr, x + w - r, y);
    cairo_curve_to(cr, x + w - r + k, y, x + w, y + r - k, x + w, y + r);
    cairo_line_to(cr, x + w, y + h - r);
    cairo_curve_to(cr, x + w, y + h - r + k, x + w - r + k, y + h, x + w - r,
                   y + h);
    cairo_line_to(cr, x + r, y + h);
    cairo_curve_to(cr, x + r - k, y + h, x, y + h - r + k, x, y + h - r);
    cairo_line_to(cr, x, y + r);
    cairo_curve_to(cr, x, y + r - k, x + r - k, y, x + r, y);
    cairo_close_path(cr);
}

static void text(cairo_t *cr, double x, double y, const char *s, double size,
                 int bold, double r, double g, double b, double a) {
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD
                                : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, s);
}

static void text_center(cairo_t *cr, double cx, double y, const char *s,
                        double size, int bold, double r, double g, double b,
                        double a) {
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD
                                : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, s, &ext);
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_move_to(cr, cx - ext.width / 2 - ext.x_bearing, y);
    cairo_show_text(cr, s);
}

static void add_hit(Dlg *d, double x, double y, double w, double h, int id) {
    if (d->nhits >= (int)(sizeof d->hits / sizeof d->hits[0])) return;
    d->hits[d->nhits++] = (Hit){ x, y, w, h, id };
}

static int hit_test(Dlg *d, int px, int py) {
    for (int i = d->nhits - 1; i >= 0; i--) {
        Hit *h = &d->hits[i];
        if (px >= h->x && px <= h->x + h->w && py >= h->y && py <= h->y + h->h)
            return h->id;
    }
    return ID_NONE;
}

/* A pill button; returns nothing, registers a hit. */
static void button(Dlg *d, double x, double y, double w, double h,
                   const char *label, int id, int active) {
    cairo_t *cr = d->cr;
    int hov = (d->hover == id);
    rrect(cr, x, y, w, h, 8);
    if (active)      cairo_set_source_rgba(cr, 0.20, 0.72, 0.85, 0.95);
    else if (hov)    cairo_set_source_rgba(cr, 0.28, 0.31, 0.40, 1.0);
    else             cairo_set_source_rgba(cr, 0.18, 0.20, 0.27, 1.0);
    cairo_fill(cr);
    double tc = active ? 0.05 : 0.90;
    text_center(cr, x + w / 2, y + h / 2 + 6, label, 15, active,
                tc, tc, tc, 1.0);
    add_hit(d, x, y, w, h, id);
}

/* +/- stepper row: label on left, value box, - and + buttons. */
static void stepper(Dlg *d, double y, const char *label, const char *valstr,
                    int dn_id, int up_id) {
    cairo_t *cr = d->cr;
    double x = 28;
    text(cr, x, y + 22, label, 14, 0, 0.75, 0.80, 0.88, 1.0);

    double bx = DW - 28 - 36 - 8 - 70 - 8 - 36;
    /* minus */
    button(d, bx, y, 36, 32, "-", dn_id, 0);
    /* value */
    rrect(cr, bx + 36 + 8, y, 70, 32, 6);
    cairo_set_source_rgba(cr, 0.10, 0.12, 0.16, 1.0);
    cairo_fill(cr);
    text_center(cr, bx + 36 + 8 + 35, y + 22, valstr, 15, 1, 1, 1, 1, 1);
    /* plus */
    button(d, bx + 36 + 8 + 70 + 8, y, 36, 32, "+", up_id, 0);
}

/* ---- render the whole dialog -------------------------------------------- */
static void render(Dlg *d, Config *cfg, DialogMode mode) {
    cairo_t *cr = d->cr;
    d->nhits = 0;

    /* background */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, DH);
    cairo_pattern_add_color_stop_rgba(bg, 0, 0.09, 0.11, 0.16, 1);
    cairo_pattern_add_color_stop_rgba(bg, 1, 0.05, 0.06, 0.10, 1);
    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    double y = 28;
    if (mode == DIALOG_ONBOARDING) {
        text_center(cr, DW / 2, y, "Welcome to petto", 24, 1,
                    0.30, 0.80, 0.92, 1);
        y += 26;
        text_center(cr, DW / 2, y, "your desktop buddy that reacts as you type",
                    13, 0, 0.65, 0.72, 0.82, 1);
        y += 28;
    } else {
        text_center(cr, DW / 2, y, "petto settings", 22, 1,
                    0.30, 0.80, 0.92, 1);
        y += 34;
    }

    /* --- pet picker --- */
    text(cr, 28, y, "Pet", 14, 1, 0.75, 0.80, 0.88, 1);
    y += 12;
    double pw = (DW - 28 * 2 - 12 * 2) / 3.0;
    button(d, 28, y, pw, 40, "Rocket", ID_PET_ROCKET,
           strcmp(cfg->pet_type, "rocket") == 0);
    button(d, 28 + pw + 12, y, pw, 40, "Cat", ID_PET_CAT,
           strcmp(cfg->pet_type, "cat") == 0);
    button(d, 28 + (pw + 12) * 2, y, pw, 40, "Jarvis", ID_PET_JARVIS,
           strcmp(cfg->pet_type, "jarvis") == 0);
    y += 60;

    /* --- pomodoro toggle --- */
    text(cr, 28, y + 22, "Pomodoro timer", 14, 1, 0.75, 0.80, 0.88, 1);
    button(d, DW - 28 - 90, y, 90, 32, cfg->pomodoro ? "ON" : "OFF",
           ID_POMO_TOGGLE, cfg->pomodoro);
    y += 50;

    /* --- timer steppers (dimmed if pomodoro off, but still clickable) --- */
    char buf[32];
    snprintf(buf, sizeof buf, "%g min", cfg->work_min);
    stepper(d, y, "Focus", buf, ID_WORK_DN, ID_WORK_UP); y += 42;
    snprintf(buf, sizeof buf, "%g min", cfg->short_min);
    stepper(d, y, "Short break", buf, ID_SHORT_DN, ID_SHORT_UP); y += 42;
    snprintf(buf, sizeof buf, "%g min", cfg->long_min);
    stepper(d, y, "Long break", buf, ID_LONG_DN, ID_LONG_UP); y += 42;
    snprintf(buf, sizeof buf, "%d", cfg->long_every);
    stepper(d, y, "Long every (sessions)", buf, ID_EVERY_DN, ID_EVERY_UP);
    y += 54;

    /* --- action buttons --- */
    double bw = (DW - 28 * 2 - 12) / 2.0;
    button(d, 28, DH - 56, bw, 40,
           mode == DIALOG_ONBOARDING ? "Get started" : "Save", ID_SAVE, 1);
    button(d, 28 + bw + 12, DH - 56, bw, 40,
           mode == DIALOG_ONBOARDING ? "Skip" : "Cancel", ID_CANCEL, 0);

    cairo_surface_flush(d->csurf);
    XFlush(d->dpy);
}

/* ---- value mutation on click -------------------------------------------- */
static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void apply_click(Config *cfg, int id) {
    switch (id) {
    case ID_PET_ROCKET: snprintf(cfg->pet_type, sizeof cfg->pet_type, "rocket"); break;
    case ID_PET_CAT:    snprintf(cfg->pet_type, sizeof cfg->pet_type, "cat");    break;
    case ID_PET_JARVIS: snprintf(cfg->pet_type, sizeof cfg->pet_type, "jarvis"); break;
    case ID_POMO_TOGGLE: cfg->pomodoro = !cfg->pomodoro; break;
    case ID_WORK_DN:  cfg->work_min  = clampd(cfg->work_min  - 1, 1, 120); break;
    case ID_WORK_UP:  cfg->work_min  = clampd(cfg->work_min  + 1, 1, 120); break;
    case ID_SHORT_DN: cfg->short_min = clampd(cfg->short_min - 1, 1, 60);  break;
    case ID_SHORT_UP: cfg->short_min = clampd(cfg->short_min + 1, 1, 60);  break;
    case ID_LONG_DN:  cfg->long_min  = clampd(cfg->long_min  - 1, 1, 60);  break;
    case ID_LONG_UP:  cfg->long_min  = clampd(cfg->long_min  + 1, 1, 60);  break;
    case ID_EVERY_DN: cfg->long_every = (int)clampd(cfg->long_every - 1, 2, 12); break;
    case ID_EVERY_UP: cfg->long_every = (int)clampd(cfg->long_every + 1, 2, 12); break;
    }
}

/* ---- modal loop ---------------------------------------------------------- */
int dialog_run(Config *cfg, DialogMode mode) {
    Dlg d;
    memset(&d, 0, sizeof d);
    d.dpy = XOpenDisplay(NULL);
    if (!d.dpy) return -1;
    d.screen = DefaultScreen(d.dpy);
    Window root = RootWindow(d.dpy, d.screen);

    d.visual = DefaultVisual(d.dpy, d.screen);
    d.depth = DefaultDepth(d.dpy, d.screen);
    d.cmap = DefaultColormap(d.dpy, d.screen);

    int sw = DisplayWidth(d.dpy, d.screen), sh = DisplayHeight(d.dpy, d.screen);
    int x = (sw - DW) / 2, yy = (sh - DH) / 2;

    XSetWindowAttributes attr;
    memset(&attr, 0, sizeof attr);
    attr.colormap = d.cmap;
    attr.background_pixel = BlackPixel(d.dpy, d.screen);
    attr.border_pixel = 0;
    attr.event_mask = ExposureMask | ButtonPressMask | PointerMotionMask |
                      KeyPressMask | StructureNotifyMask;

    d.win = XCreateWindow(d.dpy, root, x, yy, DW, DH, 0, d.depth, InputOutput,
                          d.visual,
                          CWColormap | CWBackPixel | CWBorderPixel | CWEventMask,
                          &attr);
    XStoreName(d.dpy, d.win, "petto settings");

    /* dialog window type so the WM floats + centers it with a title bar */
    Atom wt = XInternAtom(d.dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom dlg = XInternAtom(d.dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(d.dpy, d.win, wt, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&dlg, 1);
    /* allow close via WM */
    Atom wmdel = XInternAtom(d.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d.dpy, d.win, &wmdel, 1);

    d.csurf = cairo_xlib_surface_create(d.dpy, d.win, d.visual, DW, DH);
    d.cr = cairo_create(d.csurf);

    XMapRaised(d.dpy, d.win);
    XFlush(d.dpy);

    int result = 0;
    int running = 1;
    render(&d, cfg, mode);

    while (running) {
        XEvent ev;
        XNextEvent(d.dpy, &ev);
        switch (ev.type) {
        case Expose:
            render(&d, cfg, mode);
            break;
        case MotionNotify: {
            int nh = hit_test(&d, ev.xmotion.x, ev.xmotion.y);
            if (nh != d.hover) { d.hover = nh; render(&d, cfg, mode); }
            break;
        }
        case ButtonPress: {
            if (ev.xbutton.button != Button1) break;
            int id = hit_test(&d, ev.xbutton.x, ev.xbutton.y);
            if (id == ID_SAVE)      { result = 1; running = 0; }
            else if (id == ID_CANCEL) { result = 0; running = 0; }
            else if (id != ID_NONE) { apply_click(cfg, id); render(&d, cfg, mode); }
            break;
        }
        case KeyPress: {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            /* Esc cancels. (No Enter-to-save: a stray Return from launch can
             * leak in before the user interacts.) */
            if (ks == XK_Escape) { result = 0; running = 0; }
            break;
        }
        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == wmdel) { result = 0; running = 0; }
            break;
        }
    }

    cairo_destroy(d.cr);
    cairo_surface_destroy(d.csurf);
    XDestroyWindow(d.dpy, d.win);
    XCloseDisplay(d.dpy);
    return result;
}
