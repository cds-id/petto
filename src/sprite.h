#ifndef SPRITE_H
#define SPRITE_H

#include <stdint.h>
#include <cairo/cairo.h>

/* Bit-style sprite: pixel-grid frames defined as palette-index arrays.
 * Each char in a frame row maps to a palette entry. ' ' (space) = transparent.
 * Frames are rendered to ARGB32 cairo surfaces once, then blitted scaled
 * with nearest-neighbor filtering to keep pixel-art crisp. */

#define SPRITE_MAX_FRAMES 8
#define SPRITE_MAX_PALETTE 16

typedef struct {
    char key;          /* char used in the grid rows */
    uint8_t r, g, b, a;
} PaletteEntry;

typedef struct {
    int gw, gh;                         /* grid width/height in pixels */
    int nframes;
    const char *frames[SPRITE_MAX_FRAMES]; /* each: gw*gh chars, row-major */
    int npalette;
    PaletteEntry palette[SPRITE_MAX_PALETTE];
} SpriteDef;

typedef struct {
    const SpriteDef *def;
    int scale;                          /* integer upscale factor */
    cairo_surface_t *surf[SPRITE_MAX_FRAMES];
} Sprite;

/* Build cached cairo surfaces from a SpriteDef at the given integer scale. */
int  sprite_init(Sprite *s, const SpriteDef *def, int scale);
void sprite_free(Sprite *s);

/* Pixel dimensions of the rendered sprite (grid * scale). */
int  sprite_w(const Sprite *s);
int  sprite_h(const Sprite *s);

/* Draw frame at (x,y) device pixels onto cr, with optional sub-pixel offset
 * (used for shake). dx/dy in device pixels. */
void sprite_draw(const Sprite *s, cairo_t *cr, int frame, double dx, double dy);

#endif
