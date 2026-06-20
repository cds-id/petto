#define _POSIX_C_SOURCE 200809L
#include "window.h"
#include "sprite.h"
#include "pettype.h"
#include "keyhook.h"
#include "pomodoro.h"
#include "blockscreen.h"

#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <time.h>
#include <errno.h>

/* Shared runtime state, referenced by the keyhook callback. */
typedef struct {
    const PetType *pt;
    PetState       st;
    volatile int   key_pending; /* incremented per keypress, drained on tick */
} App;

static volatile sig_atomic_t g_run = 1;
static void on_sigint(int s) { (void)s; g_run = 0; }

/* Global keypress callback (fires from keyhook on any window's key events). */
static void key_cb(int keycode, int pressed, void *user) {
    (void)keycode;
    App *app = (App *)user;
    if (pressed) app->key_pending++;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    const char *type_name = "rocket";
    int    pomo_on = 0;
    double work_min = 25, short_min = 5, long_min = 15;
    int    long_every = 4;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--type") == 0 && i + 1 < argc)
            type_name = argv[++i];
        else if (strcmp(argv[i], "--pomodoro") == 0)
            pomo_on = 1;
        else if (strcmp(argv[i], "--work") == 0 && i + 1 < argc)
            work_min = atof(argv[++i]);
        else if (strcmp(argv[i], "--short") == 0 && i + 1 < argc)
            short_min = atof(argv[++i]);
        else if (strcmp(argv[i], "--long") == 0 && i + 1 < argc)
            long_min = atof(argv[++i]);
        else if (strcmp(argv[i], "--long-every") == 0 && i + 1 < argc)
            long_every = atoi(argv[++i]);
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("usage: petto [--type rocket|cat|jarvis] [--pomodoro]\n"
                   "             [--work MIN] [--short MIN] [--long MIN]\n"
                   "             [--long-every N]\n");
            return 0;
        }
    }

    App app;
    memset(&app, 0, sizeof app);
    app.pt = pettype_by_name(type_name);
    if (!app.pt) { fprintf(stderr, "unknown pet type: %s\n", type_name); return 1; }

    /* Build sprite cache */
    Sprite spr;
    if (sprite_init(&spr, &app.pt->sprite, app.pt->scale) != 0) {
        fprintf(stderr, "sprite init failed\n");
        return 1;
    }
    int W = sprite_w(&spr), H = sprite_h(&spr);

    /* Initial spawn position (also the reset/landing target). */
    const int spawn_x = 200, spawn_y = 200;

    /* Window placed bottom-right-ish initially */
    PetWindow pw;
    if (petwin_create(&pw, W, H, spawn_x, spawn_y) != 0) {
        sprite_free(&spr);
        return 1;
    }
    int screen_h = DisplayHeight(pw.dpy, pw.screen);
    (void)screen_h;

    fprintf(stderr, "petto: type=%s size=%dx%d composited=%d\n",
            app.pt->name, W, H, pw.composited);

    /* Global keyhook (optional: pet still works as draggable toy without it) */
    Keyhook *kh = keyhook_create(key_cb, &app);
    if (!kh) fprintf(stderr, "petto: global keyhook disabled (no RECORD)\n");

    /* Pomodoro + break block screen (optional) */
    Pomodoro pomo;
    memset(&pomo, 0, sizeof pomo);
    BlockScreen bs;
    memset(&bs, 0, sizeof bs);
    if (pomo_on) {
        pomo_init(&pomo, work_min, short_min, long_min, long_every, now_sec());
        blockscreen_init(&bs, pw.dpy);
        fprintf(stderr, "petto: pomodoro on (work=%.0f short=%.0f long=%.0f "
                        "long_every=%d)\n",
                work_min, short_min, long_min, long_every);
    }

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    int xfd = ConnectionNumber(pw.dpy);
    int kfd = kh ? keyhook_fd(kh) : -1;

    const double frame_dt = 1.0 / 60.0;
    double last = now_sec();
    double last_raise = last;
    int need_draw = 1;

    while (g_run) {
        /* ---- multiplex window conn + keyhook conn with a frame timeout ---- */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        int maxfd = xfd;
        if (kfd >= 0) { FD_SET(kfd, &rfds); if (kfd > maxfd) maxfd = kfd; }

        struct timeval tv = { 0, (long)(frame_dt * 1e6) };
        int rv = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rv < 0 && errno != EINTR) break;

        /* ---- drain X events ---- */
        while (XPending(pw.dpy)) {
            XEvent ev;
            XNextEvent(pw.dpy, &ev);
            if (petwin_handle_event(&pw, &ev)) need_draw = 1;
        }

        /* ---- drain global key events ---- */
        if (kh && kfd >= 0 && FD_ISSET(kfd, &rfds))
            keyhook_process(kh);

        /* apply pending keystrokes to the pet */
        while (app.key_pending > 0) {
            app.key_pending--;
            if (app.pt->on_key) app.pt->on_key(&app.st);
            need_draw = 1;
        }

        /* ---- advance animation ---- */
        double t = now_sec();
        double dt = t - last;
        last = t;

        /* keep pet above other windows (override_redirect = no WM stacking) */
        if (t - last_raise > 1.0) { petwin_raise(&pw); last_raise = t; }
        int prev_frame = app.st.frame;
        double pe = app.st.energy;
        if (app.pt->tick) app.pt->tick(app.pt, &app.st, dt);
        if (app.st.frame != prev_frame || app.st.energy > 0.001 || pe > 0.001)
            need_draw = 1;
        if (app.pt->draw) need_draw = 1; /* procedural types animate every frame */

        /* ---- launch: fly the window upward, reset once off the top ---- */
        if (app.st.mode == PET_LAUNCH) {
            int ny = pw.y + (int)(app.st.win_dy);
            petwin_move(&pw, pw.x, ny);
            need_draw = 1;
            if (ny + H < 0) {
                /* gone to the moon: reset state + drop back at spawn */
                memset(&app.st, 0, sizeof app.st);
                app.key_pending = 0;
                petwin_move(&pw, spawn_x, spawn_y);
                petwin_raise(&pw);
            }
        }

        /* ---- pomodoro: drive breaks + block screen ---- */
        if (pomo_on) {
            pomo_update(&pomo, t);
            if (pomo.phase_changed) {
                pomo.phase_changed = 0;
                if (pomo_is_break(&pomo)) blockscreen_show(&bs);
                else                      blockscreen_hide(&bs);
            }
            if (blockscreen_visible(&bs)) {
                double total = (pomo.phase == POMO_SHORT_BREAK)
                                   ? pomo.short_secs
                                   : pomo.long_secs;
                blockscreen_draw(&bs, pomo_phase_label(&pomo),
                                 pomo_remaining(&pomo, t), total, t);
            }
        }

        /* ---- render ---- */
        if (need_draw) {
            cairo_t *cr = petwin_cairo(&pw);
            if (app.pt->draw) {
                /* procedural type: clear to transparent, then draw */
                cairo_save(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_set_source_rgba(cr, 0, 0, 0, 0);
                cairo_paint(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                app.pt->draw(app.pt, &app.st, cr, W, H);
                cairo_restore(cr);
            } else {
                sprite_draw(&spr, cr, app.st.frame,
                            app.st.shake_x, app.st.shake_y);
            }
            petwin_flush(&pw);
            if (!pw.composited) petwin_update_shape(&pw);
            need_draw = 0;
        }
    }

    if (kh) keyhook_destroy(kh);
    if (pomo_on) blockscreen_destroy(&bs);
    petwin_destroy(&pw);
    sprite_free(&spr);
    return 0;
}
