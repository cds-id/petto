#include "sprite.h"
#include <stdlib.h>
#include <string.h>

static const PaletteEntry *find_pal(const SpriteDef *d, char k) {
    for (int i = 0; i < d->npalette; i++)
        if (d->palette[i].key == k) return &d->palette[i];
    return NULL;
}

/* Render one frame grid into an ARGB32 surface, scaled by integer factor.
 * Cairo ARGB32 is premultiplied alpha, native-endian uint32 per pixel. */
static cairo_surface_t *render_frame(const SpriteDef *d, const char *grid,
                                     int scale) {
    int W = d->gw * scale, H = d->gh * scale;
    cairo_surface_t *surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) return NULL;

    cairo_surface_flush(surf);
    uint32_t *px = (uint32_t *)cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf) / 4;

    for (int gy = 0; gy < d->gh; gy++) {
        for (int gx = 0; gx < d->gw; gx++) {
            char k = grid[gy * d->gw + gx];
            const PaletteEntry *p = find_pal(d, k);
            uint32_t argb;
            if (!p || p->a == 0) {
                argb = 0; /* transparent */
            } else {
                /* premultiply */
                uint32_t a = p->a;
                uint32_t r = (uint32_t)p->r * a / 255;
                uint32_t g = (uint32_t)p->g * a / 255;
                uint32_t b = (uint32_t)p->b * a / 255;
                argb = (a << 24) | (r << 16) | (g << 8) | b;
            }
            /* expand pixel to scale x scale block */
            for (int sy = 0; sy < scale; sy++) {
                uint32_t *row = px + (gy * scale + sy) * stride + gx * scale;
                for (int sx = 0; sx < scale; sx++) row[sx] = argb;
            }
        }
    }
    cairo_surface_mark_dirty(surf);
    return surf;
}

int sprite_init(Sprite *s, const SpriteDef *def, int scale) {
    if (scale < 1) scale = 1;
    s->def = def;
    s->scale = scale;
    for (int i = 0; i < SPRITE_MAX_FRAMES; i++) s->surf[i] = NULL;
    for (int i = 0; i < def->nframes; i++) {
        s->surf[i] = render_frame(def, def->frames[i], scale);
        if (!s->surf[i]) { sprite_free(s); return -1; }
    }
    return 0;
}

void sprite_free(Sprite *s) {
    if (!s) return;
    for (int i = 0; i < SPRITE_MAX_FRAMES; i++) {
        if (s->surf[i]) { cairo_surface_destroy(s->surf[i]); s->surf[i] = NULL; }
    }
}

int sprite_w(const Sprite *s) { return s->def->gw * s->scale; }
int sprite_h(const Sprite *s) { return s->def->gh * s->scale; }

void sprite_draw(const Sprite *s, cairo_t *cr, int frame, double dx, double dy) {
    if (frame < 0 || frame >= s->def->nframes || !s->surf[frame]) return;
    cairo_save(cr);
    /* clear to fully transparent first */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_surface(cr, s->surf[frame], dx, dy);
    /* nearest-neighbor: keep already-scaled pixels crisp (no blur on dx/dy) */
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}
