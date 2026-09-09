#ifndef PS2LAUNCHER_FALLBACK_ICONS_H
#define PS2LAUNCHER_FALLBACK_ICONS_H

/* Built-in placeholder icons (plan section 11) for apps with no usable
 * PS2 icon, and for the Home/System screens' own device-type rows.
 * Deliberately reuses this dashboard's existing embedded bitmap font
 * glyph data (gfx/font_uLE.c) rather than adding new binary art assets -
 * a single upscaled letter on a tinted round badge, matching the
 * PSBBN-inspired restrained palette (plan section 1). Cheap and
 * deterministic: rendered once per kind at boot (gfx/icon_cache.c),
 * never per frame. */

typedef enum {
    FALLBACK_ELF = 0,
    FALLBACK_DISC,
    FALLBACK_USB,
    FALLBACK_HDD,
    FALLBACK_MEMCARD,
    FALLBACK_NETWORK,
    FALLBACK_SETTINGS,
    FALLBACK_SAVE,
    FALLBACK_COUNT
} FallbackIconKind;

/* Renders one placeholder icon directly into an RGBA8888 buffer of size
 * cellSize*cellSize*4 (row-major, no padding). Pixels outside the badge
 * are written fully transparent (alpha 0) so this can be decoded
 * straight into a shared icon atlas slot (gfx/icon_cache.c) that may
 * previously have held a different, now-evicted icon. */
void fallbackIconRender(FallbackIconKind kind, unsigned char *outRGBA, int cellSize);

#endif
