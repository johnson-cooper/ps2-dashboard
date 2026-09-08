#ifndef PS2LAUNCHER_BITMAP_FONT_H
#define PS2LAUNCHER_BITMAP_FONT_H

#include <gsKit.h>

/* Fixed 8x16, 1-bit-per-pixel bitmap font (glyph data from wLaunchELF's
 * font_uLE.c - see that file's header for attribution), decoded once
 * into a 256-glyph GS texture atlas (a 16x16 grid, matching the plan's
 * "rasterize once, never per-frame" font guidance from section 10). */
typedef struct {
    GSTEXTURE texture;
    int cols, rows;
    int glyphW, glyphH;
} BitmapFont;

/* Decodes all 256 glyphs into an RGBA atlas (white, alpha = the glyph's
 * own 1bpp bitmap) and uploads it to VRAM. Returns 0 on success. */
int bitmapFontInit(BitmapFont *font, GSGLOBAL *gsGlobal);

/* Draws `text` starting at (x,y), one 8x16 cell per character (no
 * kerning/proportional spacing - matches the source bitmap format),
 * '\n' starts a new line. `color`'s RGB tints the (otherwise white)
 * glyphs; alpha blending is configured internally around the draw calls
 * (PrimAlphaEnable alone doesn't configure the GS blend equation for
 * textured sprites - confirmed in M7, see plan section 7). */
void bitmapFontPrint(GSGLOBAL *gsGlobal, BitmapFont *font, float x, float y, u64 color, const char *text);

#endif
