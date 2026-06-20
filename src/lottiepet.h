#ifndef LOTTIEPET_H
#define LOTTIEPET_H

#include <cairo/cairo.h>

/* Renders a Lottie animation (via rlottie) into a cairo ARGB32 surface.
 * Used as an alternate pet renderer: petto plays the animation continuously,
 * and typing energy speeds up playback for a lively reaction.
 *
 * rlottie renders directly into a premultiplied-ARGB32 uint32 buffer with a
 * stride argument, which is byte-identical to a cairo image surface. */

typedef struct LottiePet LottiePet;

/* Load a Lottie JSON file and prepare a render surface of w x h device px.
 * Returns NULL on failure (missing file / parse error / rlottie absent). */
LottiePet *lottiepet_load(const char *path, int w, int h);
void       lottiepet_free(LottiePet *lp);

/* Advance playback by dt seconds. `energy` (0..1) scales playback speed so the
 * animation runs faster while the user types. Loops automatically. */
void lottiepet_tick(LottiePet *lp, double dt, double energy);

/* Blit the current frame onto cr, centered in a cw x ch canvas, with an
 * optional device-pixel offset (dx,dy) for bob/shake. Clears cr first. */
void lottiepet_draw(LottiePet *lp, cairo_t *cr, int cw, int ch,
                    double dx, double dy);

/* Native animation pixel size (the surface dimensions chosen at load). */
int lottiepet_w(const LottiePet *lp);
int lottiepet_h(const LottiePet *lp);

#endif
