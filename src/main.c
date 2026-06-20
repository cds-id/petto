#define _POSIX_C_SOURCE 200809L
#include "window.h"
#include "sprite.h"
#include "pettype.h"
#include "keyhook.h"
#include "pomodoro.h"
#include "blockscreen.h"
#include "config.h"
#include "dialog.h"

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

/* (Re)build the sprite cache + pet window for the pet type in cfg. On a type
 * change we tear down the old window/sprite and create new ones sized to the
 * new sprite. Returns 0 on success. */
static int build_pet(App *app, Sprite *spr, PetWindow *pw, const Config *cfg,
                     int *W, int *H, int first) {
    const PetType *pt = pettype_by_name(cfg->pet_type);
    if (!pt) pt = pettype_rocket();

    if (!first) {
        sprite_free(spr);
        petwin_destroy(pw);
        memset(&app->st, 0, sizeof app->st);
    }

    app->pt = pt;
    if (sprite_init(spr, &pt->sprite, pt->scale) != 0) {
        fprintf(stderr, "sprite init failed\n");
        return -1;
    }
    *W = sprite_w(spr);
    *H = sprite_h(spr);
    if (petwin_create(pw, *W, *H, cfg->spawn_x, cfg->spawn_y) != 0)
        return -1;
    return 0;
}

/* Start/refresh the pomodoro timer from cfg. */
static void apply_pomodoro(Pomodoro *pomo, const Config *cfg) {
    if (cfg->pomodoro)
        pomo_init(pomo, cfg->work_min, cfg->short_min, cfg->long_min,
                  cfg->long_every, now_sec());
    else
        memset(pomo, 0, sizeof *pomo); /* disabled */
}

int main(int argc, char **argv) {
    /* ---- config: load persisted settings, allow CLI overrides ---- */
    Config cfg;
    config_load(&cfg);

    int force_settings = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--type") == 0 && i + 1 < argc)
            snprintf(cfg.pet_type, sizeof cfg.pet_type, "%s", argv[++i]);
        else if (strcmp(argv[i], "--pomodoro") == 0) cfg.pomodoro = 1;
        else if (strcmp(argv[i], "--no-pomodoro") == 0) cfg.pomodoro = 0;
        else if (strcmp(argv[i], "--work") == 0 && i + 1 < argc)
            cfg.work_min = atof(argv[++i]);
        else if (strcmp(argv[i], "--short") == 0 && i + 1 < argc)
            cfg.short_min = atof(argv[++i]);
        else if (strcmp(argv[i], "--long") == 0 && i + 1 < argc)
            cfg.long_min = atof(argv[++i]);
        else if (strcmp(argv[i], "--long-every") == 0 && i + 1 < argc)
            cfg.long_every = atoi(argv[++i]);
        else if (strcmp(argv[i], "--settings") == 0) force_settings = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("usage: petto [--type rocket|cat|jarvis] [--pomodoro|--no-pomodoro]\n"
                   "             [--work MIN] [--short MIN] [--long MIN] [--long-every N]\n"
                   "             [--settings]   open the settings dialog on launch\n"
                   "Double-click the pet any time to open settings.\n");
            return 0;
        }
    }

    /* ---- first run: onboarding dialog ---- */
    if (!cfg.onboarded) {
        if (dialog_run(&cfg, DIALOG_ONBOARDING) == 1) {
            cfg.onboarded = 1;
            config_save(&cfg);
        } else {
            /* skipped: still mark onboarded so we don't nag every launch */
            cfg.onboarded = 1;
            config_save(&cfg);
        }
    } else if (force_settings) {
        if (dialog_run(&cfg, DIALOG_SETTINGS) == 1) config_save(&cfg);
    }

    App app;
    memset(&app, 0, sizeof app);

    Sprite spr;
    PetWindow pw;
    int W = 0, H = 0;
    if (build_pet(&app, &spr, &pw, &cfg, &W, &H, 1) != 0) return 1;

    fprintf(stderr, "petto: type=%s size=%dx%d composited=%d pomodoro=%d\n",
            app.pt->name, W, H, pw.composited, cfg.pomodoro);

    Keyhook *kh = keyhook_create(key_cb, &app);
    if (!kh) fprintf(stderr, "petto: global keyhook disabled (no RECORD)\n");

    /* Pomodoro + break block screen */
    Pomodoro pomo;
    apply_pomodoro(&pomo, &cfg);
    BlockScreen bs;
    memset(&bs, 0, sizeof bs);
    blockscreen_init(&bs, pw.dpy);

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    int kfd = kh ? keyhook_fd(kh) : -1;

    const double frame_dt = 1.0 / 60.0;
    double last = now_sec();
    double last_raise = last;
    int need_draw = 1;

    while (g_run) {
        int xfd = ConnectionNumber(pw.dpy);

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

        /* ---- double-click on the pet -> open settings dialog ---- */
        if (pw.dbl_click) {
            pw.dbl_click = 0;
            /* remember position so the new window respawns where it was */
            cfg.spawn_x = pw.x;
            cfg.spawn_y = pw.y;
            char old_type[32];
            snprintf(old_type, sizeof old_type, "%s", cfg.pet_type);
            int old_pomo = cfg.pomodoro;

            if (dialog_run(&cfg, DIALOG_SETTINGS) == 1) {
                config_save(&cfg);
                /* rebuild pet window if the type changed */
                if (strcmp(old_type, cfg.pet_type) != 0) {
                    if (build_pet(&app, &spr, &pw, &cfg, &W, &H, 0) != 0) break;
                    blockscreen_destroy(&bs);
                    blockscreen_init(&bs, pw.dpy);
                }
                /* (re)start pomodoro if toggled or values changed */
                if (cfg.pomodoro || old_pomo) apply_pomodoro(&pomo, &cfg);
                if (!cfg.pomodoro && blockscreen_visible(&bs))
                    blockscreen_hide(&bs);
            }
            last = now_sec();
            need_draw = 1;
            continue;
        }

        /* ---- drain global key events ---- */
        if (kh && kfd >= 0 && FD_ISSET(kfd, &rfds))
            keyhook_process(kh);

        while (app.key_pending > 0) {
            app.key_pending--;
            if (app.pt->on_key) app.pt->on_key(&app.st);
            need_draw = 1;
        }

        /* ---- advance animation ---- */
        double t = now_sec();
        double dt = t - last;
        last = t;

        if (t - last_raise > 1.0) { petwin_raise(&pw); last_raise = t; }
        int prev_frame = app.st.frame;
        double pe = app.st.energy;
        if (app.pt->tick) app.pt->tick(app.pt, &app.st, dt);
        if (app.st.frame != prev_frame || app.st.energy > 0.001 || pe > 0.001)
            need_draw = 1;
        if (app.pt->draw) need_draw = 1;

        /* ---- launch: fly upward, reset once off the top ---- */
        if (app.st.mode == PET_LAUNCH) {
            int ny = pw.y + (int)(app.st.win_dy);
            petwin_move(&pw, pw.x, ny);
            need_draw = 1;
            if (ny + H < 0) {
                memset(&app.st, 0, sizeof app.st);
                app.key_pending = 0;
                petwin_move(&pw, cfg.spawn_x, cfg.spawn_y);
                petwin_raise(&pw);
            }
        }

        /* ---- pomodoro: drive breaks + block screen ---- */
        if (cfg.pomodoro) {
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
    blockscreen_destroy(&bs);
    petwin_destroy(&pw);
    sprite_free(&spr);
    return 0;
}
