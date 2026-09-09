#ifndef PS2LAUNCHER_BACKGROUND_H
#define PS2LAUNCHER_BACKGROUND_H

#include <gsKit.h>

/* PSBBN-inspired animated background (plan section 3): a sparse, mostly-
 * static starfield with a subtle twinkle, plus a handful of translucent
 * floating "glass" cubes biased toward the right side of the screen -
 * reproduced natively with plain GS primitives (points for stars,
 * flat-shaded quads for cube faces), not any literal port of the HTML
 * reference's CSS. Deliberately cheap: fixed, precomputed star/cube
 * layouts (no particle system), a handful of quads per cube, and no
 * per-frame filesystem/texture work at all - see plan section 17.
 *
 * This never draws over the title-safe content area's text - callers
 * are expected to draw this first, before any menu/grid/panel content,
 * exactly like the flat theme background it replaces. */

#define BG_STAR_COUNT 90
#define BG_CUBE_COUNT 9

typedef struct {
    float xFrac, yFrac; /* position as a fraction of screen size - resolution-independent */
    unsigned char baseBrightness; /* 0-255 alpha baseline */
    unsigned char big;            /* 1 = drawn as a 2px star, 0 = 1px */
    float twinklePhase;
    float twinkleSpeed;
} BgStar;

typedef struct {
    float xFrac, yFrac; /* anchor position, right-biased (plan section 3) */
    float sizeFrac;      /* cube edge length, as a fraction of screen width */
    float bobPhase;
    float bobPeriodFrames;
    int violet; /* 0 = blue-gray variant, 1 = violet variant - matches the HTML reference's two cube tints */
} BgCube;

typedef struct {
    BgStar stars[BG_STAR_COUNT];
    BgCube cubes[BG_CUBE_COUNT];
    unsigned int frame;
} Background;

/* Precomputes a deterministic star/cube layout (a fixed seed - "sparse
 * starfield" doesn't need true randomness, and determinism makes this
 * reproducible for testing). Independent of gsGlobal's resolution: every
 * position/size is stored as a fraction, resolved against the current
 * screen size at draw time. */
void backgroundInit(Background *bg);

/* Advances the animation state - call exactly once per frame regardless
 * of which screen is active, so the background keeps animating
 * consistently across Home/Library/System. */
void backgroundTick(Background *bg);

/* Draws the starfield then the cubes, in that order, across the whole
 * screen (not just the title-safe area - matches the HTML reference's
 * full-bleed `.frame` background). Must be called before any menu/grid
 * text so cubes/stars never sit visually on top of readable content. */
void backgroundDraw(const Background *bg, GSGLOBAL *gsGlobal);

#endif
