#ifndef PS2LAUNCHER_THEME_H
#define PS2LAUNCHER_THEME_H

#include <gsKit.h>

/* OPL-style theme (plan section 7): a small theme.cfg (key=value lines:
 * name, bg_color/tile_color/focus_color/label_color as 6-hex-digit
 * RRGGBB, background=<filename or "none">) plus an optional background
 * image living alongside it in the same folder. */
typedef struct {
    char name[32];
    u64 bgColor;
    u64 tileColor;
    u64 focusColor;
    u64 labelColor;
    GSTEXTURE background;
    int hasBackground;
} Theme;

/* Loads themeDir/theme.cfg and, if named, themeDir/<background file> as
 * a GS texture. The background is decoded at its own (power-of-two)
 * pixel dimensions and is meant to be drawn stretched to fill the whole
 * screen via UV mapping at draw time (see main.c) - that decouples the
 * baked asset from gsKit_init_global()'s runtime-detected resolution,
 * which varies by TV standard/mode. A missing or corrupt background
 * asset degrades to a solid-color theme rather than failing the whole
 * load. Returns 0 on success (including the solid-color-only case); a
 * negative value only if theme.cfg itself couldn't be read. */
int themeLoad(const char *themeDir, Theme *out, GSGLOBAL *gsGlobal);

#endif
