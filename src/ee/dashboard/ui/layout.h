#ifndef PS2LAUNCHER_LAYOUT_H
#define PS2LAUNCHER_LAYOUT_H

#include <gsKit.h>

#include "../gfx/bitmap_font.h"

/* Small, shared layout/draw helpers (plan section 4/5) - a normalized
 * title-safe rect computed once at boot (same numbers main.c already
 * used through M14, just factored out so every screen module shares one
 * definition instead of recomputing/hardcoding it), plus the project's
 * established GS draw-state guard (see main.c's own header comments:
 * PrimAlphaEnable alone doesn't configure the blend equation for
 * textured sprites, and alpha/Z testing are separate stages that must
 * both be disabled around a draw meant to unconditionally overwrite
 * whatever was there before, then restored afterward). Centralizing
 * this here means new screens don't each re-derive or copy-paste that
 * guard. */

/* Shared between main.c (which still draws the persistent status line
 * and debug overlay itself, below/over whatever dashboard_ui.c drew)
 * and dashboard_ui.c, so both compute the exact same safe-area rect. */
#define LAYOUT_STATUS_LINE_HEIGHT 20.0f

typedef struct {
    float marginX, marginY;
    float safeW, safeH;
    float safeBottom; /* top-safe-margin's Y plus safeH; where the status line strip begins */
    float screenW, screenH;
} LayoutRect;

/* Computes the title-safe rect for the current gsGlobal resolution
 * (plan section 4: 8% margins, one status-line row carved out of the
 * bottom - matches the pre-M15 layout exactly, just centralized). */
void layoutComputeSafeArea(LayoutRect *out, GSGLOBAL *gsGlobal, float statusLineHeight);

/* Opaque flat-color fill - disables blending AND both test stages around
 * the draw (matching every opaque fill in the pre-M15 main.c), since
 * alpha=0x00 on a flat fill has always meant "unused", not
 * "transparent", in this codebase's convention. */
void layoutFillOpaque(GSGLOBAL *gsGlobal, float x0, float y0, float x1, float y1, int z, u64 color);

/* Genuinely translucent flat-color fill (a list-row highlight, a subtle
 * panel tint) - configures the real blend equation and the same
 * alpha/Z-test guard bitmap_font.c uses around every text draw, restored
 * to the project's normal resting state afterward. */
void layoutFillBlended(GSGLOBAL *gsGlobal, float x0, float y0, float x1, float y1, int z, u64 color);

/* Truncates `text` in place (via `out`, at most outSize-1 chars) to fit
 * within `maxChars` 8px-wide bitmap-font glyphs - the exact
 * char-counting logic main.c used inline for grid labels through M14,
 * factored out so Home/Library/System all share it instead of each
 * re-deriving their own. Stale now that bitmap_font.c's glyphs are
 * proportional (see layoutTruncateToWidth below) - kept only for any
 * caller that still wants a plain character-count cap. */
void layoutTruncateToChars(char *out, int outSize, const char *text, int maxChars);

/* Truncates `text` in place (via `out`, at most outSize-1 chars) to the
 * longest prefix whose real, measured width (bitmapFontTextWidth, using
 * `font`'s actual per-glyph advances) is still <= maxWidthPx - the
 * proportional-font replacement for layoutTruncateToChars's "assume 8px
 * per glyph" arithmetic, which under- or over-fits text now that glyphs
 * have real, varying advance widths. */
void layoutTruncateToWidth(char *out, int outSize, const char *text, const BitmapFont *font, float maxWidthPx);

#endif
