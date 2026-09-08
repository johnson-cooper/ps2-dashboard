#include "theme.h"

#include <kernel.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <setjmp.h>
#include <png.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <iox_stat.h>

#define THEME_MAX_PATH 300
#define THEME_MAX_CFG 512

typedef struct {
    const unsigned char *buf;
    unsigned int pos;
    unsigned int size;
} PngMemReader;

static void pngMemRead(png_structp png, png_bytep outBytes, png_size_t byteCountToRead)
{
    PngMemReader *r = (PngMemReader *)png_get_io_ptr(png);
    if (r->pos + byteCountToRead > r->size) {
        png_error(png, "read past end of buffer");
        return;
    }
    memcpy(outBytes, r->buf + r->pos, byteCountToRead);
    r->pos += byteCountToRead;
}

/* Same fileXio-based whole-file read as gfx/texture_atlas.c and
 * config/metadata.c - path-based loaders (fopen/libpng's own I/O) don't
 * resolve iomanX devices here (see elfloader.c's header comment for the
 * full M4 evidence trail). */
static unsigned char *readWholeFile(const char *path, unsigned int *outSize)
{
    iox_stat_t st;
    if (fileXioGetStat(path, &st) < 0)
        return NULL;

    int fd = fileXioOpen(path, FIO_O_RDONLY, 0666);
    if (fd < 0)
        return NULL;

    unsigned char *buf = (unsigned char *)malloc(st.size);
    if (!buf) {
        fileXioClose(fd);
        return NULL;
    }

    int n = fileXioRead(fd, buf, st.size);
    fileXioClose(fd);

    if (n != (int)st.size) {
        free(buf);
        return NULL;
    }

    *outSize = st.size;
    return buf;
}

static void buildPath(char *out, const char *dir, const char *name)
{
    int i = 0;
    while (dir[i]) {
        out[i] = dir[i];
        i++;
    }
    int j = 0;
    while (name[j])
        out[i++] = name[j++];
    out[i] = '\0';
}

/* Decodes an arbitrary-size PNG into a freshly allocated RGBA8 buffer
 * and uploads it as its own GS texture. Unlike gfx/texture_atlas.c, this
 * is a single full image, not a fixed-cell atlas slot - a theme has at
 * most one background, loaded once at boot and never evicted. */
static int decodeBackground(GSTEXTURE *tex, GSGLOBAL *gsGlobal, const unsigned char *fileBuf, unsigned int fileSize)
{
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png)
        return -1;

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        return -1;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        return -1;
    }

    PngMemReader reader = { fileBuf, 0, fileSize };
    png_set_read_fn(png, &reader, pngMemRead);

    png_read_info(png, info);

    png_uint_32 width, height;
    int bitDepth, colorType;
    png_get_IHDR(png, info, &width, &height, &bitDepth, &colorType, NULL, NULL, NULL);

    /* Normalize every input format to plain 8-bit RGBA, same transform
     * set as gfx/texture_atlas.c's icon decoder. */
    if (bitDepth == 16)
        png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    unsigned char *pixels = (unsigned char *)memalign(64, width * height * 4);
    if (!pixels) {
        png_destroy_read_struct(&png, &info, NULL);
        return -1;
    }

    png_bytep *rowPointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
    unsigned int y;
    for (y = 0; y < height; y++)
        rowPointers[y] = pixels + y * width * 4;

    png_read_image(png, rowPointers);

    free(rowPointers);
    png_destroy_read_struct(&png, &info, NULL);

    tex->Width = width;
    tex->Height = height;
    tex->PSM = GS_PSM_CT32;
    /* GS_FILTER_NEAREST, not LINEAR - matches every other texture in
     * this project (gfx/bitmap_font.c, gfx/texture_atlas.c), both
     * confirmed reliable; LINEAR was an untested, unproven choice here
     * and a suspect for the rapid full-screen flicker seen on first
     * test (bilinear sampling at a large background→screen magnification
     * factor is a much more PS2-GS-specific, less-travelled code path). */
    tex->Filter = GS_FILTER_NEAREST;
    tex->Mem = (u32 *)pixels;

    /* gsKit_texture_upload() doesn't allocate VRAM itself - M7's
     * confirmed gotcha (plan section 7), applies here too. */
    u32 vramSize = gsKit_texture_size(width, height, GS_PSM_CT32);
    tex->Vram = gsKit_vram_alloc(gsGlobal, vramSize, GSKIT_ALLOC_USERBUFFER);

    /* M13: gsKit_vram_alloc() doesn't fail loudly on overflow - checked
     * against the real, hard 4MB VRAM ceiling (measured in M7) instead
     * of guessing at a specific error sentinel value. themeLoad()'s
     * caller already treats a non-zero return here as "no background,
     * fall back to solid color" - this just makes that fallback actually
     * reachable instead of silently corrupting VRAM past its end. */
    if (tex->Vram + vramSize > 0x00400000) {
        free(pixels);
        return -1;
    }

    FlushCache(0);
    gsKit_texture_upload(gsGlobal, tex);

    return 0;
}

int themeLoad(const char *themeDir, Theme *out, GSGLOBAL *gsGlobal)
{
    char cfgPath[THEME_MAX_PATH];
    buildPath(cfgPath, themeDir, "theme.cfg");

    unsigned int cfgSize;
    unsigned char *cfgBuf = readWholeFile(cfgPath, &cfgSize);
    if (!cfgBuf)
        return -1;

    char content[THEME_MAX_CFG];
    if (cfgSize >= sizeof(content))
        cfgSize = sizeof(content) - 1;
    memcpy(content, cfgBuf, cfgSize);
    content[cfgSize] = '\0';
    free(cfgBuf);

    Theme theme;
    memset(&theme, 0, sizeof(theme));
    char backgroundFile[64];
    backgroundFile[0] = '\0';

    char *line = strtok(content, "\n");
    while (line) {
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            const char *key = line;
            char *value = eq + 1;
            int vlen = strlen(value);
            if (vlen > 0 && value[vlen - 1] == '\r')
                value[vlen - 1] = '\0';

            if (strcmp(key, "name") == 0) {
                strncpy(theme.name, value, sizeof(theme.name) - 1);
            } else if (strcmp(key, "bg_color") == 0) {
                unsigned int rgb = strtoul(value, NULL, 16);
                theme.bgColor = GS_SETREG_RGBAQ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0x00, 0x00);
            } else if (strcmp(key, "tile_color") == 0) {
                unsigned int rgb = strtoul(value, NULL, 16);
                theme.tileColor = GS_SETREG_RGBAQ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0x00, 0x00);
            } else if (strcmp(key, "focus_color") == 0) {
                unsigned int rgb = strtoul(value, NULL, 16);
                theme.focusColor = GS_SETREG_RGBAQ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0x00, 0x00);
            } else if (strcmp(key, "label_color") == 0) {
                unsigned int rgb = strtoul(value, NULL, 16);
                /* Text is always drawn blended (bitmap_font.c) - alpha
                 * 0x80 is "fully opaque" in the GS's fixed-point blend
                 * scale, not a theme-configurable value. */
                theme.labelColor = GS_SETREG_RGBAQ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0x80, 0x00);
            } else if (strcmp(key, "background") == 0) {
                if (strcmp(value, "none") != 0)
                    strncpy(backgroundFile, value, sizeof(backgroundFile) - 1);
            }
        }
        line = strtok(NULL, "\n");
    }

    if (backgroundFile[0]) {
        char bgPath[THEME_MAX_PATH];
        buildPath(bgPath, themeDir, backgroundFile);

        unsigned int pngSize;
        unsigned char *pngBuf = readWholeFile(bgPath, &pngSize);
        if (pngBuf) {
            if (decodeBackground(&theme.background, gsGlobal, pngBuf, pngSize) == 0)
                theme.hasBackground = 1;
            free(pngBuf);
        }
    }

    *out = theme;
    return 0;
}
