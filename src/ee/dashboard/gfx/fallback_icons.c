#include "fallback_icons.h"

#include <string.h>

extern unsigned char font_uLE[]; /* gfx/font_uLE.c - see bitmap_font.c for the atlas that also uses this */

#define GLYPH_W 8
#define GLYPH_H 16

typedef struct {
    unsigned char letter;
    unsigned char r, g, b;
} FallbackSpec;

/* One letter + tint per kind - restrained, muted tones consistent with
 * the PSBBN-inspired palette (dark background, muted cyan/blue, amber
 * accents) rather than bright/saturated "app icon" colors. */
static const FallbackSpec kFallbackSpecs[FALLBACK_COUNT] = {
    { 'E', 100, 130, 150 }, /* generic ELF / homebrew */
    { 'D', 184, 160, 106 }, /* disc - amber, matches section-heading accent */
    { 'U', 90, 140, 150 },  /* USB */
    { 'H', 110, 100, 130 }, /* HDD */
    { 'M', 120, 150, 170 }, /* memory card */
    { 'N', 80, 120, 160 },  /* network / SMB */
    { 'S', 130, 130, 140 }, /* settings */
    { 'F', 170, 140, 90 },  /* save manager */
};

static void setPixel(unsigned char *outRGBA, int cellSize, int x, int y, unsigned char r, unsigned char g,
                      unsigned char b, unsigned char a)
{
    if (x < 0 || y < 0 || x >= cellSize || y >= cellSize)
        return;
    unsigned char *p = outRGBA + (y * cellSize + x) * 4;
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
}

void fallbackIconRender(FallbackIconKind kind, unsigned char *outRGBA, int cellSize)
{
    if (kind < 0 || kind >= FALLBACK_COUNT)
        kind = FALLBACK_ELF;
    const FallbackSpec *spec = &kFallbackSpecs[kind];

    memset(outRGBA, 0, (size_t)cellSize * cellSize * 4);

    /* Filled circle badge, inset from the cell edges. */
    float cx = cellSize * 0.5f;
    float cy = cellSize * 0.5f;
    float radius = cellSize * 0.42f;
    float radiusSq = radius * radius;

    int x, y;
    for (y = 0; y < cellSize; y++) {
        for (x = 0; x < cellSize; x++) {
            float dx = (x + 0.5f) - cx;
            float dy = (y + 0.5f) - cy;
            if (dx * dx + dy * dy <= radiusSq)
                setPixel(outRGBA, cellSize, x, y, spec->r, spec->g, spec->b, 0xFF);
        }
    }

    /* One glyph from the existing bitmap font (gfx/font_uLE.c),
     * upscaled by nearest-neighbor onto the badge - reuses an existing
     * embedded asset rather than adding new binary art (plan section
     * 11). Kept at a fixed 1:2 width:height ratio, matching the source
     * glyph's own 8x16 shape. */
    int glyphOutH = (int)(cellSize * 0.6f);
    int glyphOutW = glyphOutH / 2;
    if (glyphOutW < 1)
        glyphOutW = 1;
    int originX = (cellSize - glyphOutW) / 2;
    int originY = (cellSize - glyphOutH) / 2;

    const unsigned char *glyphRows = font_uLE + (unsigned int)spec->letter * GLYPH_H;

    for (y = 0; y < glyphOutH; y++) {
        int sy = (y * GLYPH_H) / glyphOutH;
        unsigned char row = glyphRows[sy];
        for (x = 0; x < glyphOutW; x++) {
            int sx = (x * GLYPH_W) / glyphOutW;
            int bit = (row >> (7 - sx)) & 1;
            if (bit)
                setPixel(outRGBA, cellSize, originX + x, originY + y, 0xF0, 0xF0, 0xF0, 0xFF);
        }
    }
}
