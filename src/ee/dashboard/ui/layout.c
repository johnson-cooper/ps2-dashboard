#include "layout.h"

#define TITLE_SAFE_MARGIN 0.08f

void layoutComputeSafeArea(LayoutRect *out, GSGLOBAL *gsGlobal, float statusLineHeight)
{
    out->screenW = (float)gsGlobal->Width;
    out->screenH = (float)gsGlobal->Height;
    out->marginX = out->screenW * TITLE_SAFE_MARGIN;
    out->marginY = out->screenH * TITLE_SAFE_MARGIN;
    out->safeW = out->screenW - 2 * out->marginX;
    out->safeH = (out->screenH - out->marginY) - out->marginY - statusLineHeight;
    out->safeBottom = out->marginY + out->safeH;
}

void layoutFillOpaque(GSGLOBAL *gsGlobal, float x0, float y0, float x1, float y1, int z, u64 color)
{
    gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_set_test(gsGlobal, GS_ATEST_OFF);
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    gsKit_prim_sprite(gsGlobal, x0, y0, x1, y1, z, color);
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    gsKit_set_test(gsGlobal, GS_ATEST_ON);
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
}

void layoutFillBlended(GSGLOBAL *gsGlobal, float x0, float y0, float x1, float y1, int z, u64 color)
{
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_set_test(gsGlobal, GS_ATEST_OFF);
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    gsKit_prim_sprite(gsGlobal, x0, y0, x1, y1, z, color);
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    gsKit_set_test(gsGlobal, GS_ATEST_ON);
    gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
}

void layoutTruncateToChars(char *out, int outSize, const char *text, int maxChars)
{
    int len = 0;
    while (text[len] && len < maxChars)
        len++;
    if (len > outSize - 1)
        len = outSize - 1;
    int i;
    for (i = 0; i < len; i++)
        out[i] = text[i];
    out[len] = '\0';
}
