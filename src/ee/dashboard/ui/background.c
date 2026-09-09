#include "background.h"

#include <math.h>

/* Tiny deterministic xorshift PRNG - a fixed seed is intentional (plan
 * section 3: "deterministic or pseudo-random collection of tiny stars"),
 * not a shortcut; it makes the layout reproducible across boots and
 * avoids needing a real entropy source this early (before RTC has even
 * been read) just to scatter some stars. */
static unsigned int rngState;

static unsigned int nextRand(void)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

static float randFrac(void)
{
    return (float)(nextRand() & 0xFFFF) / 65535.0f;
}

static float randRange(float lo, float hi)
{
    return lo + randFrac() * (hi - lo);
}

void backgroundInit(Background *bg)
{
    rngState = 0x9E3779B9u;

    int i;
    for (i = 0; i < BG_STAR_COUNT; i++) {
        BgStar *s = &bg->stars[i];
        s->xFrac = randFrac();
        s->yFrac = randFrac();
        s->big = (randFrac() < 0.15f) ? 1 : 0;
        s->baseBrightness = (unsigned char)(0x20 + randFrac() * 0x60); /* muted - "subtle glow, not bloom" */
        s->twinklePhase = randRange(0.0f, 6.28318f);
        s->twinkleSpeed = randRange(0.01f, 0.03f);
    }

    /* Right-biased placement (plan section 3) - x fractions stay in the
     * screen's right half, roughly matching the HTML reference's own
     * cube coordinates (52-90% width, 12-68% height). Alternating
     * blue/violet tint for visual variety, same as the reference. */
    for (i = 0; i < BG_CUBE_COUNT; i++) {
        BgCube *c = &bg->cubes[i];
        c->xFrac = randRange(0.50f, 0.93f);
        c->yFrac = randRange(0.10f, 0.72f);
        c->sizeFrac = randRange(0.035f, 0.075f);
        c->bobPhase = randRange(0.0f, 6.28318f);
        c->bobPeriodFrames = randRange(220.0f, 380.0f);
        c->violet = (i % 3 == 1) ? 1 : 0;
    }
}

void backgroundTick(Background *bg)
{
    bg->frame++;
}

void backgroundDraw(const Background *bg, GSGLOBAL *gsGlobal)
{
    float w = (float)gsGlobal->Width;
    float h = (float)gsGlobal->Height;

    /* Opaque full-screen clear FIRST, every frame, unconditionally - the
     * "very dark / black background" plan section 1 asks for, but also
     * load-bearing: gsGlobal double-buffers via gsKit_sync_flip(), so
     * any region not unconditionally redrawn every frame keeps showing
     * whatever was drawn there two frames ago on that same physical
     * buffer. Stars/cubes below are all translucent/additive-looking
     * draws that only ever add to what's already there - without this
     * clear underneath them first, menu text and highlight bars from
     * old frames accumulate frame over frame instead of being replaced
     * (the same double-buffering hazard documented in main.c around the
     * status line, just applied to the whole screen now that a flat/
     * theme background fill isn't what's clearing it anymore). Uses the
     * project's established opaque-fill guard (disable blending AND
     * both test stages around the draw) - see ui/layout.c. */
    gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_set_test(gsGlobal, GS_ATEST_OFF);
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    gsKit_prim_sprite(gsGlobal, 0.0f, 0.0f, w, h, 0, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    gsKit_set_test(gsGlobal, GS_ATEST_ON);
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;

    /* Stars: plain GS points, left at the project's normal blended
     * resting state (alpha blending on, both test stages on) - a single
     * point primitive with a translucent color is exactly what that
     * resting state is for, no special guard needed (unlike an opaque
     * fill meant to unconditionally overwrite prior content). */
    int i;
    for (i = 0; i < BG_STAR_COUNT; i++) {
        const BgStar *s = &bg->stars[i];
        float twinkle = sinf(bg->frame * s->twinkleSpeed + s->twinklePhase) * 24.0f;
        int alpha = (int)s->baseBrightness + (int)twinkle;
        if (alpha < 0x10)
            alpha = 0x10;
        if (alpha > 0x80)
            alpha = 0x80;

        float x = s->xFrac * w;
        float y = s->yFrac * h;
        u64 color = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, (u8)alpha, 0x00);

        gsKit_prim_point(gsGlobal, x, y, 0, color);
        if (s->big)
            gsKit_prim_point(gsGlobal, x + 1.0f, y, 0, color);
    }

    /* Floating cubes: 3 flat-shaded faces per cube via simple,
     * precomputed 2D offsets (an isometric-looking projection, not a
     * real 3D transform - plan section 3 explicitly allows this: "they
     * do NOT need to be actual expensive full 3D objects"). Colors and
     * alphas are a direct translation of the HTML reference's own
     * rgba() values (0.0-1.0 alpha scaled to the GS's 0-0x80 range). */
    for (i = 0; i < BG_CUBE_COUNT; i++) {
        const BgCube *c = &bg->cubes[i];
        float bob = sinf((bg->frame + c->bobPhase * 40.0f) * (6.28318f / c->bobPeriodFrames)) * 10.0f;

        float cx = c->xFrac * w;
        float cy = c->yFrac * h + bob;
        float s = c->sizeFrac * w;
        float skew = s * 0.4f;
        float topH = s * 0.25f;

        u8 topR, topG, topB, topA, frontR, frontG, frontB, frontA, sideR, sideG, sideB, sideA;
        if (c->violet) {
            topR = 58; topG = 50; topB = 70; topA = 31;
            frontR = 24; frontG = 18; frontB = 32; frontA = 33;
            sideR = 12; sideG = 9; sideB = 18; sideA = 41;
        } else {
            topR = 60; topG = 72; topB = 88; topA = 31;
            frontR = 18; frontG = 24; frontB = 34; frontA = 33;
            sideR = 8; sideG = 12; sideB = 18; sideA = 41;
        }

        gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
        gsKit_set_test(gsGlobal, GS_ATEST_OFF);
        gsKit_set_test(gsGlobal, GS_ZTEST_OFF);

        /* Top face - parallelogram above/behind the front face. */
        gsKit_prim_quad(gsGlobal, cx, cy, cx + s, cy, cx + s + skew, cy - topH, cx + skew, cy - topH, 0,
                         GS_SETREG_RGBAQ(topR, topG, topB, topA, 0x00));
        /* Front face - the plain camera-facing square. */
        gsKit_prim_quad(gsGlobal, cx, cy, cx + s, cy, cx + s, cy + s, cx, cy + s, 0,
                         GS_SETREG_RGBAQ(frontR, frontG, frontB, frontA, 0x00));
        /* Right/side face - parallelogram to the right of the front face. */
        gsKit_prim_quad(gsGlobal, cx + s, cy, cx + s + skew, cy - topH, cx + s + skew, cy - topH + s, cx + s, cy + s,
                         0, GS_SETREG_RGBAQ(sideR, sideG, sideB, sideA, 0x00));

        gsKit_set_test(gsGlobal, GS_ZTEST_ON);
        gsKit_set_test(gsGlobal, GS_ATEST_ON);
        gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
    }
}
