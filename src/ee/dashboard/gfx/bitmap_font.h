#ifndef PS2LAUNCHER_BITMAP_FONT_H
#define PS2LAUNCHER_BITMAP_FONT_H

#include <gsKit.h>

/* Proportional Noto Sans text (embedded/noto_sans_ttf.c, subset with
 * fonttools down to the printable extended-ASCII range - codepoints
 * 0x20-0x7E and 0xA0-0xFF, i.e. Latin-1 - rather than embedding the full,
 * many-script upstream .ttf) - rasterized once at init by FreeType into
 * a single GS texture atlas, then drawn as plain textured sprites using
 * each glyph's own real metrics (bearing/advance) for spacing - same
 * "rasterize once, never per-frame" approach the project's old fixed
 * 8x16 bitmap font used (plan section 10), just with real proportional
 * metrics instead of a uniform cell. The 0x7F-0x9F gap (C1 controls, no
 * visible glyph in Latin-1) is kept in-range rather than carved out, so
 * BITMAP_FONT_FIRST_GLYPH..+COUNT stays one contiguous, simple-to-index
 * block - those codepoints just carry a zero-size glyph. */
#define BITMAP_FONT_FIRST_GLYPH 0x20 /* ' ' */
#define BITMAP_FONT_GLYPH_COUNT 224  /* through 0xFF inclusive */

typedef struct {
    short width, height;      /* glyph bitmap size in the atlas, pixels */
    short bearingX, bearingY; /* pen-to-bitmap-top-left offset, pixels */
    short advance;            /* pen advance to the next glyph, pixels */
    short atlasX, atlasY;     /* glyph bitmap's top-left texel in the atlas */
} GlyphMetrics;

typedef struct {
    GSTEXTURE texture;
    GlyphMetrics glyphs[BITMAP_FONT_GLYPH_COUNT];
    int lineHeight; /* baseline-to-baseline distance, pixels */
    int ascender;   /* baseline-to-top distance, pixels */
} BitmapFont;

/* Rasterizes the embedded Noto Sans subset into an RGBA atlas (white,
 * alpha = the glyph's own coverage) and uploads it to VRAM. Returns 0 on
 * success. */
int bitmapFontInit(BitmapFont *font, GSGLOBAL *gsGlobal);

/* Draws `text` starting at (x,y) using each glyph's real bearing/advance
 * metrics (proportional spacing, unlike the old fixed-width font), '\n'
 * starts a new line. `color`'s RGB tints the (otherwise white) glyphs;
 * alpha blending is configured internally around the draw calls
 * (PrimAlphaEnable alone doesn't configure the GS blend equation for
 * textured sprites - confirmed in M7, see plan section 7). */
void bitmapFontPrint(GSGLOBAL *gsGlobal, BitmapFont *font, float x, float y, u64 color, const char *text);

/* Sums real glyph advances for `text` (single line - stops at '\n' if
 * any) - the proportional-width replacement for the old "assume every
 * glyph is 8px" arithmetic call sites used to fit text into a fixed
 * pixel budget (grid cell, panel width) with the old monospace font. */
int bitmapFontTextWidth(const BitmapFont *font, const char *text);

#endif
