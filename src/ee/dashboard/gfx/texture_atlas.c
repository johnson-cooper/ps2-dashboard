#include "texture_atlas.h"

#include <kernel.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <setjmp.h>
#include <png.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <iox_stat.h>

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

/* Reads a whole file via fileXio into a freshly malloc'd buffer.
 * gsKit_texture_png() (gsToolkit.h) and libpng's own default I/O both go
 * through fopen(), which - like POSIX open()/stat() before it (see
 * src/ee/common/elfloader.c's header comment for the full evidence
 * trail from M4) - doesn't resolve iomanX devices (mc0:/mc1:/mass:) in
 * this ps2sdk build. fileXio does, so PNGs are read into memory
 * ourselves and decoded via libpng's png_set_read_fn memory-reader API
 * instead of any path-based loader. */
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

static int decodePngIntoAtlas(TextureAtlas *atlas, int slot, const unsigned char *fileBuf, unsigned int fileSize)
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

    if ((int)width != atlas->cellSize || (int)height != atlas->cellSize) {
        png_destroy_read_struct(&png, &info, NULL);
        return -2;
    }

    /* Normalize every input format (paletted, grayscale, 16-bit, no
     * alpha, ...) to plain 8-bit RGBA via libpng's own transforms,
     * rather than hand-rolling per-format conversion. */
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

    /* Decode directly into the slot's region of the atlas buffer - one
     * row-pointer array pointing at the right offsets, no extra copy. */
    int col = slot % atlas->cols;
    int row = slot / atlas->cols;
    int atlasStride = atlas->cols * atlas->cellSize * 4;
    unsigned char *slotOrigin =
        atlas->pixels + row * atlas->cellSize * atlasStride + col * atlas->cellSize * 4;

    png_bytep *rowPointers = (png_bytep *)malloc(sizeof(png_bytep) * atlas->cellSize);
    int i;
    for (i = 0; i < atlas->cellSize; i++)
        rowPointers[i] = slotOrigin + i * atlasStride;

    png_read_image(png, rowPointers);

    free(rowPointers);
    png_destroy_read_struct(&png, &info, NULL);
    return 0;
}

int textureAtlasInit(TextureAtlas *atlas, GSGLOBAL *gsGlobal, int cols, int rows, int cellSize)
{
    memset(atlas, 0, sizeof(*atlas));
    atlas->gsGlobal = gsGlobal;
    atlas->cols = cols;
    atlas->rows = rows;
    atlas->cellSize = cellSize;
    atlas->capacity = cols * rows;
    atlas->lastEvicted = -1;

    int atlasW = cols * cellSize;
    int atlasH = rows * cellSize;

    atlas->pixels = (unsigned char *)memalign(64, atlasW * atlasH * 4);
    if (!atlas->pixels)
        return -1;
    memset(atlas->pixels, 0, atlasW * atlasH * 4);

    atlas->slotPath = (const char **)calloc(atlas->capacity, sizeof(const char *));
    atlas->slotAge = (unsigned int *)calloc(atlas->capacity, sizeof(unsigned int));

    atlas->texture.Width = atlasW;
    atlas->texture.Height = atlasH;
    atlas->texture.PSM = GS_PSM_CT32;
    atlas->texture.Filter = GS_FILTER_NEAREST;
    atlas->texture.Mem = (u32 *)atlas->pixels;

    /* gsKit_texture_upload() does NOT allocate VRAM - it just DMAs Mem
     * to whatever Texture->Vram already holds. Left at its zeroed
     * default, that's VRAM address 0, which is also roughly where the
     * framebuffer lives - uploading there overwrites the framebuffer
     * instead of creating a separate texture (confirmed: this was the
     * actual cause of an all-background, no-icons black screen before
     * this was added). gsKit_vram_alloc() must be called explicitly
     * first, exactly once per texture's lifetime - not on every
     * re-upload after an LRU eviction, since the region doesn't move. */
    atlas->texture.Vram = gsKit_vram_alloc(gsGlobal, gsKit_texture_size(atlasW, atlasH, GS_PSM_CT32),
                                            GSKIT_ALLOC_USERBUFFER);

    FlushCache(0);
    gsKit_texture_upload(gsGlobal, &atlas->texture);

    return 0;
}

int textureAtlasGetSlot(TextureAtlas *atlas, const char *path)
{
    int i;
    for (i = 0; i < atlas->capacity; i++) {
        if (atlas->slotPath[i] && strcmp(atlas->slotPath[i], path) == 0) {
            atlas->slotAge[i] = ++atlas->clock;
            atlas->lastHit = 1;
            atlas->lastEvicted = -1;
            return i;
        }
    }

    /* Not resident - pick a slot: an empty one if any, else the
     * least-recently-used occupied one. */
    int victim = 0;
    int haveEmpty = 0;
    for (i = 0; i < atlas->capacity; i++) {
        if (!atlas->slotPath[i]) {
            victim = i;
            haveEmpty = 1;
            break;
        }
        if (atlas->slotAge[i] < atlas->slotAge[victim])
            victim = i;
    }

    unsigned int fileSize;
    unsigned char *fileBuf = readWholeFile(path, &fileSize);
    if (!fileBuf)
        return -1;

    int ret = decodePngIntoAtlas(atlas, victim, fileBuf, fileSize);
    free(fileBuf);
    if (ret < 0)
        return ret;

    atlas->lastEvicted = haveEmpty ? -1 : victim;
    atlas->lastHit = 0;
    atlas->slotPath[victim] = path;
    atlas->slotAge[victim] = ++atlas->clock;

    /* The decode above wrote pixels through the CPU cache; gsKit's DMA
     * upload reads directly from physical RAM, so without this flush it
     * can see stale (pre-decode, still-zeroed) memory instead of what
     * was just decoded - confirmed as the cause of icons rendering as
     * solid black despite the atlas otherwise positioning correctly. */
    FlushCache(0);

    /* Re-upload the whole atlas - simple and correct; a partial-region
     * update would be the natural next optimization once this is
     * measurably too slow for a real icon set, not before. */
    gsKit_texture_upload(atlas->gsGlobal, &atlas->texture);

    return victim;
}

void textureAtlasSlotUV(const TextureAtlas *atlas, int slot, float *u0, float *v0, float *u1, float *v1)
{
    int col = slot % atlas->cols;
    int row = slot / atlas->cols;
    *u0 = (float)(col * atlas->cellSize);
    *v0 = (float)(row * atlas->cellSize);
    *u1 = (float)((col + 1) * atlas->cellSize);
    *v1 = (float)((row + 1) * atlas->cellSize);
}
