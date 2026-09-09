#include "bitmap_font.h"

#include <kernel.h>
#include <string.h>
#include <malloc.h>

#include <ft2build.h>
#include FT_FREETYPE_H

extern unsigned char noto_sans_ttf[];
extern unsigned int size_noto_sans_ttf;

#define FONT_PIXEL_SIZE 16
#define ATLAS_COLS 10
#define ATLAS_ROWS ((BITMAP_FONT_GLYPH_COUNT + ATLAS_COLS - 1) / ATLAS_COLS)

int bitmapFontInit(BitmapFont *font, GSGLOBAL *gsGlobal)
{
    memset(font, 0, sizeof(*font));

    FT_Library ftLib;
    if (FT_Init_FreeType(&ftLib) != 0)
        return -1;

    FT_Face face;
    if (FT_New_Memory_Face(ftLib, noto_sans_ttf, (FT_Long)size_noto_sans_ttf, 0, &face) != 0) {
        FT_Done_FreeType(ftLib);
        return -1;
    }

    if (FT_Set_Pixel_Sizes(face, 0, FONT_PIXEL_SIZE) != 0) {
        FT_Done_Face(face);
        FT_Done_FreeType(ftLib);
        return -1;
    }

    font->lineHeight = (int)(face->size->metrics.height >> 6);
    font->ascender = (int)(face->size->metrics.ascender >> 6);

    /* Two passes: rasterize every glyph first and copy its bitmap out
     * (FT reuses the same internal buffer on every FT_Load_Char call, so
     * a glyph's pixels have to be copied out before loading the next
     * one) while measuring the largest glyph, THEN size and pack the
     * atlas - guessing a fixed cell size up front would either clip a
     * tall/wide glyph or waste VRAM padding every cell to fit one. */
    unsigned char *glyphBitmaps[BITMAP_FONT_GLYPH_COUNT];
    memset(glyphBitmaps, 0, sizeof(glyphBitmaps));
    int cellW = 1, cellH = 1;

    int i;
    for (i = 0; i < BITMAP_FONT_GLYPH_COUNT; i++) {
        unsigned long c = (unsigned long)(BITMAP_FONT_FIRST_GLYPH + i);
        GlyphMetrics *gm = &font->glyphs[i];

        if (FT_Load_Char(face, c, FT_LOAD_RENDER) != 0)
            continue;

        FT_GlyphSlot slot = face->glyph;
        int w = (int)slot->bitmap.width;
        int h = (int)slot->bitmap.rows;

        gm->width = (short)w;
        gm->height = (short)h;
        gm->bearingX = (short)slot->bitmap_left;
        gm->bearingY = (short)slot->bitmap_top;
        gm->advance = (short)(slot->advance.x >> 6);

        if (w > 0 && h > 0) {
            unsigned char *copy = (unsigned char *)malloc((size_t)(w * h));
            if (copy) {
                /* FT's own row pitch can exceed the bitmap width (row
                 * padding) - copy row by row rather than assuming
                 * pitch == width. */
                int row;
                for (row = 0; row < h; row++)
                    memcpy(copy + row * w, slot->bitmap.buffer + row * slot->bitmap.pitch, (size_t)w);
                glyphBitmaps[i] = copy;
            }
        }

        if (w > cellW)
            cellW = w;
        if (h > cellH)
            cellH = h;
    }

    int atlasW = ATLAS_COLS * cellW;
    int atlasH = ATLAS_ROWS * cellH;

    unsigned char *pixels = (unsigned char *)memalign(64, (size_t)(atlasW * atlasH * 4));
    if (!pixels) {
        for (i = 0; i < BITMAP_FONT_GLYPH_COUNT; i++)
            free(glyphBitmaps[i]);
        FT_Done_Face(face);
        FT_Done_FreeType(ftLib);
        return -1;
    }
    memset(pixels, 0, (size_t)(atlasW * atlasH * 4));

    for (i = 0; i < BITMAP_FONT_GLYPH_COUNT; i++) {
        GlyphMetrics *gm = &font->glyphs[i];
        int originX = (i % ATLAS_COLS) * cellW;
        int originY = (i / ATLAS_COLS) * cellH;
        gm->atlasX = (short)originX;
        gm->atlasY = (short)originY;

        if (!glyphBitmaps[i])
            continue;

        int gy;
        for (gy = 0; gy < gm->height; gy++) {
            int gx;
            for (gx = 0; gx < gm->width; gx++) {
                unsigned char a = glyphBitmaps[i][gy * gm->width + gx];
                unsigned char *p = pixels + ((originY + gy) * atlasW + (originX + gx)) * 4;
                p[0] = 0xFF;
                p[1] = 0xFF;
                p[2] = 0xFF;
                p[3] = a;
            }
        }
        free(glyphBitmaps[i]);
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ftLib);

    font->texture.Width = atlasW;
    font->texture.Height = atlasH;
    font->texture.PSM = GS_PSM_CT32;
    font->texture.Filter = GS_FILTER_NEAREST;
    font->texture.Mem = (u32 *)pixels;

    /* gsKit_texture_upload() doesn't allocate VRAM itself - confirmed in
     * M7 (plan section 7). */
    u32 vramSize = gsKit_texture_size(atlasW, atlasH, GS_PSM_CT32);
    font->texture.Vram = gsKit_vram_alloc(gsGlobal, vramSize, GSKIT_ALLOC_USERBUFFER);

    /* M13: gsKit_vram_alloc() doesn't fail loudly on overflow - checked
     * against the real, hard 4MB VRAM ceiling (measured in M7) instead
     * of guessing at a specific error sentinel value. The font atlas is
     * the very first VRAM allocation after the screen/Z buffers, so this
     * should never actually trip in practice, but main.c's caller checks
     * this return and degrades to a text-free UI rather than silently
     * corrupting whatever VRAM region this overflowed into. */
    if (font->texture.Vram + vramSize > 0x00400000) {
        free(pixels);
        return -1;
    }

    /* CPU wrote the decoded glyphs above; the DMA-based upload below
     * needs a cache flush first or it can read stale memory - also
     * confirmed in M7. */
    FlushCache(0);
    gsKit_texture_upload(gsGlobal, &font->texture);

    return 0;
}

void bitmapFontPrint(GSGLOBAL *gsGlobal, BitmapFont *font, float x, float y, u64 color, const char *text)
{
    /* PrimAlphaEnable alone doesn't configure the GS blend equation for
     * textured sprites - confirmed the dominant, hardest-to-diagnose bug
     * of M7 (plan section 7). Self-contained here (set up and torn down
     * around every call) rather than hoisted to the caller, since a
     * label might be drawn from several different call sites and
     * correctness matters more than the modest state-change overhead of
     * a handful of text draws per frame. */
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_set_test(gsGlobal, GS_ATEST_OFF);
    /* Z-testing is a separate preset from alpha-testing on this same
     * function - GS_ATEST_OFF above never touched it. Every prior "why
     * won't this redraw over old content" bug in this project turned out
     * to be one of these test/blend stages left in a leftover state;
     * this is the one stage never yet addressed. If Z-test defaults to
     * enabled with a strict-greater comparison and the Z-buffer is never
     * cleared between frames, every glyph redrawn at the same Z as a
     * prior frame would fail the test and never actually write - explains
     * "old text doesn't disappear, new text only shows where nothing was
     * drawn before" exactly. */
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);

    float cx = x, cy = y;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if (*p == '\n') {
            cx = x;
            cy += font->lineHeight;
            p++;
            continue;
        }

        if (*p >= BITMAP_FONT_FIRST_GLYPH && *p < BITMAP_FONT_FIRST_GLYPH + BITMAP_FONT_GLYPH_COUNT) {
            const GlyphMetrics *gm = &font->glyphs[*p - BITMAP_FONT_FIRST_GLYPH];

            if (gm->width > 0 && gm->height > 0) {
                float gx = cx + gm->bearingX;
                float gy = cy + font->ascender - gm->bearingY;
                float u0 = (float)gm->atlasX;
                float v0 = (float)gm->atlasY;

                /* Glyphs are packed edge-to-edge in the atlas with no
                 * gutter between cells - an exact [u0, u0+width) UV
                 * rectangle can have its far edge round, under GS
                 * nearest-neighbor sampling, into the first texel
                 * column/row of the next glyph in the atlas. Invisible on
                 * static text (the sliver doesn't change), but very
                 * visible the instant a character does change (a ticking
                 * clock, a swapped disc name) since the bled-in sliver
                 * changes with it. Insetting the far edge by a fraction
                 * of a texel keeps sampling inside the intended cell
                 * without shrinking the glyph's actual on-screen size
                 * (only which texels get read changes, not the
                 * destination quad). */
                gsKit_prim_sprite_texture(gsGlobal, &font->texture, gx, gy, u0, v0, gx + gm->width, gy + gm->height,
                                           u0 + gm->width - 0.1f, v0 + gm->height - 0.1f, 1, color);
            }

            cx += gm->advance;
        }

        p++;
    }

    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    gsKit_set_test(gsGlobal, GS_ATEST_ON);
    gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
}

int bitmapFontTextWidth(const BitmapFont *font, const char *text)
{
    int width = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p && *p != '\n') {
        if (*p >= BITMAP_FONT_FIRST_GLYPH && *p < BITMAP_FONT_FIRST_GLYPH + BITMAP_FONT_GLYPH_COUNT)
            width += font->glyphs[*p - BITMAP_FONT_FIRST_GLYPH].advance;
        p++;
    }
    return width;
}
