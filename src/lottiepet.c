#include "lottiepet.h"
#include <rlottie_capi.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct LottiePet {
    Lottie_Animation *anim;
    cairo_surface_t  *surf;   /* ARGB32, w x h */
    int    w, h;
    size_t total;             /* total frames */
    double fps;               /* native frame rate */
    double pos;               /* current frame position (fractional) */
    size_t last_rendered;     /* avoid re-rendering same integer frame */
};

LottiePet *lottiepet_load(const char *path, int w, int h) {
    if (w < 1 || h < 1) return NULL;
    Lottie_Animation *a = lottie_animation_from_file(path);
    if (!a) return NULL;

    LottiePet *lp = calloc(1, sizeof *lp);
    if (!lp) { lottie_animation_destroy(a); return NULL; }
    lp->anim = a;
    lp->w = w;
    lp->h = h;
    lp->total = lottie_animation_get_totalframe(a);
    lp->fps = lottie_animation_get_framerate(a);
    if (lp->fps <= 0) lp->fps = 30.0;
    if (lp->total == 0) lp->total = 1;
    lp->pos = 0.0;
    lp->last_rendered = (size_t)-1;

    lp->surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(lp->surf) != CAIRO_STATUS_SUCCESS) {
        lottiepet_free(lp);
        return NULL;
    }
    return lp;
}

void lottiepet_free(LottiePet *lp) {
    if (!lp) return;
    if (lp->surf) cairo_surface_destroy(lp->surf);
    if (lp->anim) lottie_animation_destroy(lp->anim);
    free(lp);
}

static void render_frame(LottiePet *lp, size_t frame) {
    if (frame == lp->last_rendered) return;
    cairo_surface_flush(lp->surf);
    uint32_t *buf = (uint32_t *)cairo_image_surface_get_data(lp->surf);
    int stride = cairo_image_surface_get_stride(lp->surf);
    /* rlottie writes premultiplied ARGB32 with the given stride (bytes). */
    lottie_animation_render(lp->anim, frame, buf,
                            (size_t)lp->w, (size_t)lp->h, (size_t)stride);
    cairo_surface_mark_dirty(lp->surf);
    lp->last_rendered = frame;
}

void lottiepet_tick(LottiePet *lp, double dt, double energy) {
    if (!lp) return;
    /* base speed 1x; typing pushes up to ~2.2x for a lively reaction */
    double speed = 1.0 + energy * 1.2;
    lp->pos += dt * lp->fps * speed;
    /* wrap into [0, total) */
    double t = (double)lp->total;
    if (lp->pos >= t) lp->pos = fmod(lp->pos, t);
    size_t frame = (size_t)lp->pos;
    if (frame >= lp->total) frame = lp->total - 1;
    render_frame(lp, frame);
}

void lottiepet_draw(LottiePet *lp, cairo_t *cr, int cw, int ch,
                    double dx, double dy) {
    if (!lp) return;
    /* ensure at least one frame is rendered */
    if (lp->last_rendered == (size_t)-1) render_frame(lp, 0);

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    double ox = (cw - lp->w) / 2.0 + dx;
    double oy = (ch - lp->h) / 2.0 + dy;
    cairo_set_source_surface(cr, lp->surf, ox, oy);
    /* smooth scaling: Lottie is vector, bilinear keeps it crisp */
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
}

int lottiepet_w(const LottiePet *lp) { return lp ? lp->w : 0; }
int lottiepet_h(const LottiePet *lp) { return lp ? lp->h : 0; }
