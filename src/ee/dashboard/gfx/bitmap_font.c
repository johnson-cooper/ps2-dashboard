#include "bitmap_font.h"

#include <kernel.h>
#include <string.h>
#include <malloc.h>

extern unsigned char font_uLE[];

#define FONT_COLS 16
#define FONT_ROWS 16
#define GLYPH_W 8
#define GLYPH_H 16

int bitmapFontInit(BitmapFont *font, GSGLOBAL *gsGlobal)
{
    memset(font, 0, sizeof(*font));
    font->cols = FONT_COLS;
    font->rows = FONT_ROWS;
    font->glyphW = GLYPH_W;
    font->glyphH = GLYPH_H;

    int atlasW = FONT_COLS * GLYPH_W;
    int atlasH = FONT_ROWS * GLYPH_H;

    unsigned char *pixels = (unsigned char *)memalign(64, atlasW * atlasH * 4);
    if (!pixels)
        return -1;
    memset(pixels, 0, atlasW * atlasH * 4);

    int ch;
    for (ch = 0; ch < FONT_COLS * FONT_ROWS; ch++) {
        int originX = (ch % FONT_COLS) * GLYPH_W;
        int originY = (ch / FONT_COLS) * GLYPH_H;

        int gy;
        for (gy = 0; gy < GLYPH_H; gy++) {
            unsigned char row = font_uLE[ch * GLYPH_H + gy];
            int gx;
            for (gx = 0; gx < GLYPH_W; gx++) {
                int bit = (row >> (7 - gx)) & 1;
                unsigned char *p = pixels + ((originY + gy) * atlasW + (originX + gx)) * 4;
                p[0] = 0xFF;
                p[1] = 0xFF;
                p[2] = 0xFF;
                p[3] = bit ? 0xFF : 0x00;
            }
        }
    }

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
     * should never actually trip in practice, but main.c's caller now
     * checks this return and degrades to a text-free UI rather than
     * silently corrupting whatever VRAM region this overflowed into. */
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
            cy += font->glyphH;
            p++;
            continue;
        }

        int col = *p % font->cols;
        int row = *p / font->cols;
        float u0 = (float)(col * font->glyphW);
        float v0 = (float)(row * font->glyphH);

        /* Glyphs are packed edge-to-edge in the atlas with no gutter
         * between cells - an exact [u0, u0+glyphW) UV rectangle can have
         * its far edge round, under GS nearest-neighbor sampling, into
         * the first texel column/row of the *next* glyph in the atlas.
         * Invisible on static text (the sliver doesn't change), but very
         * visible the instant a character does change (a ticking clock,
         * a swapped disc name) since the bled-in sliver changes with it.
         * Insetting the far edge by a fraction of a texel keeps sampling
         * inside the intended cell without shrinking the glyph's actual
         * on-screen size (only which texels get read changes, not the
         * destination quad). */
        gsKit_prim_sprite_texture(gsGlobal, &font->texture, cx, cy, u0, v0, cx + font->glyphW, cy + font->glyphH,
                                   u0 + font->glyphW - 0.1f, v0 + font->glyphH - 0.1f, 1, color);

        cx += font->glyphW;
        p++;
    }

    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    gsKit_set_test(gsGlobal, GS_ATEST_ON);
    gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
}
