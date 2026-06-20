#define _POSIX_C_SOURCE 200809L
/*
 * petto - desktop pet that reacts as you type, with a Pomodoro timer.
 * An open source project by Cipta Dua Saudara (CDS).
 * Author: Indra Gunanda <indra.gunanda@ciptadusa.com>
 * https://github.com/cds-id/petto - https://open.ciptadusa.com
 * SPDX-License-Identifier: MIT
 */
#ifndef PETTO_VERSION
#define PETTO_VERSION "dev"
#endif
#include "window.h"
#include "sprite.h"
#include "pettype.h"
#include "keyhook.h"
#include "pomodoro.h"
#include "blockscreen.h"
#include "config.h"
#include "dialog.h"
#include "lottiepet.h"

#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <errno.h>

/* Shared runtime state, referenced by the keyhook callback. */
typedef struct {
    const PetType *pt;
    PetState       st;
    LottiePet     *lottie;      /* non-NULL when the pet is Lottie-rendered */
    volatile int   key_pending; /* incremented per keypress, drained on tick */
} App;

static volatile sig_atomic_t g_run = 1;
static void on_sigint(int s) { (void)s; g_run = 0; }

/* Resolve an asset filename to a readable path. Searches, in order:
 *   $PETTO_ASSET_DIR, ./assets, /usr/share/petto, /usr/local/share/petto.
 * Returns 1 and fills out[] on success. */
static int resolve_asset(const char *file, char *out, size_t n) {
    const char *dirs[5];
    int nd = 0;
    const char *env = getenv("PETTO_ASSET_DIR");
    if (env && env[0]) dirs[nd++] = env;
    dirs[nd++] = "assets";
    dirs[nd++] = "/usr/share/petto";
    dirs[nd++] = "/usr/local/share/petto";
    for (int i = 0; i < nd; i++) {
        snprintf(out, n, "%s/%s", dirs[i], file);
        if (access(out, R_OK) == 0) return 1;
    }
    return 0;
}

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
static int build_pet(App *app, Sprite *spr, PetWindow *pw, Config *cfg,
                     int *W, int *H, int first) {
    const PetType *pt = pettype_by_name(cfg->pet_type);
    if (!pt) pt = pettype_rocket();

    if (!first) {
        sprite_free(spr);
        petwin_destroy(pw);
        if (app->lottie) { lottiepet_free(app->lottie); app->lottie = NULL; }
        pet_state_reset(&app->st);
    }

    app->pt = pt;
    if (sprite_init(spr, &pt->sprite, pt->scale) != 0) {
        fprintf(stderr, "sprite init failed\n");
        return -1;
    }

    int sw, sh, margin;
    if (pt->lottie_file) {
        /* Lottie-rendered pet: size the window to the render px + margin. */
        sw = sh = pt->lottie_px;
        margin = sw / 4;
        if (margin < 12) margin = 12;
        *W = sw + margin * 2;
        *H = sh + margin * 2;
        char path[600];
        if (!resolve_asset(pt->lottie_file, path, sizeof path)) {
            fprintf(stderr, "petto: asset not found: %s\n", pt->lottie_file);
            return -1;
        }
        app->lottie = lottiepet_load(path, sw, sh);
        if (!app->lottie) {
            fprintf(stderr, "petto: failed to load Lottie %s\n", path);
            return -1;
        }
    } else {
        /* Window is larger than the sprite so squash/stretch/rotate + shake
         * have room and never clip at the edge. Sprite is centered in it. */
        sw = sprite_w(spr); sh = sprite_h(spr);
        margin = sw / 4;            /* ~25% breathing room each side */
        if (margin < 12) margin = 12;
        *W = sw + margin * 2;
        *H = sh + margin * 2;
    }

    /* Provisional position; corrected below once the screen size is known. */
    int px = cfg->spawn_x, py = cfg->spawn_y;
    if (px < 0 || py < 0) { px = 0; py = 0; }
    if (petwin_create(pw, *W, *H, px, py) != 0)
        return -1;

    /* Resolve auto/sentinel position to the bottom-right corner with a small
     * inset. Also clamp any saved position back on-screen. */
    int scr_w = DisplayWidth(pw->dpy, pw->screen);
    int scr_h = DisplayHeight(pw->dpy, pw->screen);
    const int inset = 24;
    if (cfg->spawn_x < 0 || cfg->spawn_y < 0) {
        px = scr_w - *W - inset;
        py = scr_h - *H - inset;
    } else {
        px = cfg->spawn_x;
        py = cfg->spawn_y;
    }
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px > scr_w - *W) px = scr_w - *W;
    if (py > scr_h - *H) py = scr_h - *H;
    cfg->spawn_x = px;
    cfg->spawn_y = py;
    petwin_move(pw, px, py);
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
            printf("usage: petto [--type rocket|cat|jarvis|rocket-lottie] [--pomodoro|--no-pomodoro]\n"
                   "             [--work MIN] [--short MIN] [--long MIN] [--long-every N]\n"
                   "             [--settings]   open the settings dialog on launch\n"
                   "Double-click the pet any time to open settings.\n");
            return 0;
        }
        else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("petto " PETTO_VERSION "\n"
                   "An open source project by Cipta Dua Saudara (CDS)\n"
                   "Author: Indra Gunanda <indra.gunanda@ciptadusa.com>\n"
                   "https://github.com/cds-id/petto - https://open.ciptadusa.com\n");
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
    pet_state_reset(&app.st);

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
            /* break overlay shares this display: let it consume skip events */
            if (cfg.pomodoro && blockscreen_visible(&bs) &&
                blockscreen_wants_skip(&bs, &ev)) {
                pomo_skip(&pomo, now_sec());   /* end the break early */
                blockscreen_hide(&bs);
                continue;
            }
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
        if (app.pt->tick) app.pt->tick(app.pt, &app.st, dt);
        if (app.lottie) lottiepet_tick(app.lottie, dt, app.st.energy);
        /* Fluent transforms (breathing squash/sway, cross-fades) change every
         * frame, so always redraw. Cheap: it's a single cached blit. */
        need_draw = 1;

        /* ---- launch: fly upward, reset once off the top ---- */
        if (app.st.mode == PET_LAUNCH) {
            int ny = pw.y + (int)(app.st.win_dy);
            petwin_move(&pw, pw.x, ny);
            need_draw = 1;
            if (ny + H < 0) {
                pet_state_reset(&app.st);
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
            if (app.lottie) {
                lottiepet_draw(app.lottie, cr, W, H,
                               app.st.shake_x, app.st.shake_y);
            } else if (app.pt->draw) {
                cairo_save(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_set_source_rgba(cr, 0, 0, 0, 0);
                cairo_paint(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                app.pt->draw(app.pt, &app.st, cr, W, H);
                cairo_restore(cr);
            } else {
                SpriteXform xf = {
                    .dx = app.st.shake_x, .dy = app.st.shake_y,
                    .sx = app.st.sx, .sy = app.st.sy,
                    .rot = app.st.rot, .alpha = 1.0
                };
                sprite_draw_ex(&spr, cr, app.st.frame_prev, app.st.frame,
                               app.st.frame_mix, &xf, W, H);
            }
            petwin_flush(&pw);
            if (!pw.composited) petwin_update_shape(&pw);
            need_draw = 0;
        }
    }

    /* Persist final position (unless mid-launch, when it's off-screen). */
    if (app.st.mode != PET_LAUNCH) {
        cfg.spawn_x = pw.x;
        cfg.spawn_y = pw.y;
        config_save(&cfg);
    }

    if (kh) keyhook_destroy(kh);
    blockscreen_destroy(&bs);
    petwin_destroy(&pw);
    sprite_free(&spr);
    if (app.lottie) lottiepet_free(app.lottie);
    return 0;
}
