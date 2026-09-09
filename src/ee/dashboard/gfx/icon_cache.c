#include "icon_cache.h"
#include "ps2_icon.h"

#include <kernel.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <iox_stat.h>

#define ICON_CACHE_CAPACITY (ICON_CACHE_COLS * ICON_CACHE_ROWS)
#define ICON_PATH_SCRATCH_MAX 320

/* Same fileXio-whole-file-read pattern as gfx/texture_atlas.c and
 * theme/theme.c - path-based loaders (fopen, gsKit's own png helper)
 * don't resolve iomanX devices (mc0:/mass:/...) in this ps2sdk build.
 * `maxSize` is a sanity cap against a corrupt/hostile file claiming an
 * implausible size, not a real format limit - see call sites. */
static unsigned char *readWholeFile(const char *path, unsigned int *outSize, unsigned int maxSize)
{
    iox_stat_t st;
    if (fileXioGetStat(path, &st) < 0)
        return NULL;
    if (st.size <= 0 || (unsigned int)st.size > maxSize)
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

    *outSize = (unsigned int)st.size;
    return buf;
}

/* Copies the directory portion of `path` (through the last '/',
 * inclusive) into out. Returns 0 on success, negative if `path` has no
 * '/' at all or the directory wouldn't fit outSize. */
static int dirOf(const char *path, char *out, int outSize)
{
    int len = 0;
    while (path[len])
        len++;

    int lastSlash = -1;
    int i;
    for (i = 0; i < len; i++)
        if (path[i] == '/')
            lastSlash = i;

    if (lastSlash < 0)
        return -1;
    int dirLen = lastSlash + 1;
    if (dirLen >= outSize)
        return -1;

    memcpy(out, path, dirLen);
    out[dirLen] = '\0';
    return 0;
}

static int safeConcat(char *out, int outSize, const char *a, const char *b)
{
    int la = 0;
    while (a[la])
        la++;
    int lb = 0;
    while (b[lb])
        lb++;
    if (la + lb >= outSize)
        return -1;
    memcpy(out, a, la);
    memcpy(out + la, b, lb + 1);
    return 0;
}

static FallbackIconKind fallbackForDevice(DeviceKind device)
{
    switch (device) {
    case DEVICE_CDROM:
        return FALLBACK_DISC;
    case DEVICE_MASS:
        return FALLBACK_USB;
    case DEVICE_HDD:
        return FALLBACK_HDD;
    case DEVICE_MC0:
    case DEVICE_MC1:
        return FALLBACK_MEMCARD;
    case DEVICE_NETWORK:
    case DEVICE_SMB:
        return FALLBACK_NETWORK;
    default:
        return FALLBACK_ELF;
    }
}

int iconCacheInit(IconCache *cache, GSGLOBAL *gsGlobal)
{
    memset(cache, 0, sizeof(*cache));
    cache->gsGlobal = gsGlobal;

    int atlasW = ICON_CACHE_COLS * ICON_CACHE_CELL_SIZE;
    int atlasH = ICON_CACHE_ROWS * ICON_CACHE_CELL_SIZE;

    cache->pixels = (unsigned char *)memalign(64, atlasW * atlasH * 4);
    if (!cache->pixels)
        return -1;
    memset(cache->pixels, 0, atlasW * atlasH * 4);

    cache->texture.Width = atlasW;
    cache->texture.Height = atlasH;
    cache->texture.PSM = GS_PSM_CT32;
    cache->texture.Filter = GS_FILTER_NEAREST;
    cache->texture.Mem = (u32 *)cache->pixels;

    u32 vramSize = gsKit_texture_size(atlasW, atlasH, GS_PSM_CT32);
    cache->texture.Vram = gsKit_vram_alloc(gsGlobal, vramSize, GSKIT_ALLOC_USERBUFFER);
    if (cache->texture.Vram + vramSize > 0x00400000) {
        free(cache->pixels);
        cache->pixels = NULL;
        return -1;
    }

    /* Render every fallback badge directly into its permanent slot -
     * fallbackIconRender() expects a tightly-packed cellSize x cellSize
     * buffer, so render into a small scratch buffer first, then copy
     * row by row into the atlas's strided layout. */
    static unsigned char scratch[ICON_CACHE_CELL_SIZE * ICON_CACHE_CELL_SIZE * 4];
    int atlasStride = atlasW * 4;
    int kind;
    for (kind = 0; kind < FALLBACK_COUNT; kind++) {
        int col = kind % ICON_CACHE_COLS;
        int row = kind / ICON_CACHE_COLS;
        unsigned char *slotOrigin = cache->pixels + row * ICON_CACHE_CELL_SIZE * atlasStride + col * ICON_CACHE_CELL_SIZE * 4;

        fallbackIconRender((FallbackIconKind)kind, scratch, ICON_CACHE_CELL_SIZE);

        int y;
        for (y = 0; y < ICON_CACHE_CELL_SIZE; y++)
            memcpy(slotOrigin + y * atlasStride, scratch + y * ICON_CACHE_CELL_SIZE * 4, ICON_CACHE_CELL_SIZE * 4);
    }

    FlushCache(0);
    gsKit_texture_upload(gsGlobal, &cache->texture);

    cache->ready = 1;
    return 0;
}

int iconCacheFallbackSlot(FallbackIconKind kind)
{
    if (kind < 0 || kind >= FALLBACK_COUNT)
        kind = FALLBACK_ELF;
    return (int)kind;
}

void iconCacheRequest(IconCache *cache, AppEntry *entry)
{
    if (!cache->ready)
        return;
    if (entry->iconState != ICON_NOT_REQUESTED)
        return;
    if (cache->pendingCount >= ICON_CACHE_PENDING_MAX)
        return;

    int i;
    for (i = 0; i < cache->pendingCount; i++)
        if (cache->pending[i] == entry)
            return;

    entry->iconState = ICON_PENDING;
    cache->pending[cache->pendingCount++] = entry;
}

/* Decodes into a fresh app-icon slot (LRU among the ICON_CACHE_APP_SLOTS
 * slots past the fallback ones), uploads the whole atlas, and returns
 * the slot index. Re-uploading the whole atlas (not just the changed
 * region) mirrors gfx/texture_atlas.c's own choice - simple and correct
 * at this size/frequency (at most one decode per frame). */
static int decodeIntoSlot(IconCache *cache, AppEntry *entry, const unsigned char *modelBuf, unsigned int modelSize)
{
    int victim = ICON_CACHE_FALLBACK_SLOTS;
    int i;
    for (i = ICON_CACHE_FALLBACK_SLOTS; i < ICON_CACHE_CAPACITY; i++) {
        if (!cache->slotKey[i]) {
            victim = i;
            break;
        }
        if (cache->slotAge[i] < cache->slotAge[victim])
            victim = i;
    }

    static unsigned char scratch[ICON_CACHE_CELL_SIZE * ICON_CACHE_CELL_SIZE * 4];
    if (ps2IconModelDecodeThumbnail(modelBuf, modelSize, scratch, ICON_CACHE_CELL_SIZE, ICON_CACHE_CELL_SIZE) < 0)
        return -1;

    int col = victim % ICON_CACHE_COLS;
    int row = victim / ICON_CACHE_COLS;
    int atlasStride = cache->texture.Width * 4;
    unsigned char *slotOrigin =
        cache->pixels + row * ICON_CACHE_CELL_SIZE * atlasStride + col * ICON_CACHE_CELL_SIZE * 4;

    int y;
    for (y = 0; y < ICON_CACHE_CELL_SIZE; y++)
        memcpy(slotOrigin + y * atlasStride, scratch + y * ICON_CACHE_CELL_SIZE * 4, ICON_CACHE_CELL_SIZE * 4);

    /* The previous occupant (if any) goes back to "not resident" rather
     * than staying falsely marked LOADED with a slot index that no
     * longer holds its icon - it'll simply be re-requested and
     * re-decoded next time its tile is visible, same as any cache miss. */
    if (cache->slotKey[victim] && cache->slotKey[victim] != entry) {
        AppEntry *evicted = (AppEntry *)cache->slotKey[victim];
        if (evicted->iconState == ICON_LOADED)
            evicted->iconState = ICON_NOT_REQUESTED;
    }

    cache->slotKey[victim] = entry;
    cache->slotAge[victim] = ++cache->clock;

    FlushCache(0);
    gsKit_texture_upload(cache->gsGlobal, &cache->texture);

    return victim;
}

void iconCacheProcessPending(IconCache *cache)
{
    if (!cache->ready || cache->pendingCount == 0)
        return;

    AppEntry *entry = cache->pending[0];
    int i;
    for (i = 1; i < cache->pendingCount; i++)
        cache->pending[i - 1] = cache->pending[i];
    cache->pendingCount--;

    char dir[ICON_PATH_SCRATCH_MAX];
    if (dirOf(entry->path, dir, sizeof(dir)) < 0) {
        entry->iconState = ICON_MISSING;
        return;
    }

    char iconSysPath[ICON_PATH_SCRATCH_MAX];
    if (safeConcat(iconSysPath, sizeof(iconSysPath), dir, "icon.sys") < 0) {
        entry->iconState = ICON_MISSING;
        return;
    }

    /* icon.sys is always a small, fixed-shape file (964 bytes on a real
     * PS2) - 4096 is a generous sanity cap, not a real limit. */
    unsigned int iconSysSize;
    unsigned char *iconSysBuf = readWholeFile(iconSysPath, &iconSysSize, 4096);
    if (!iconSysBuf) {
        entry->iconState = ICON_MISSING; /* no icon.sys next to this ELF - not an error */
        return;
    }

    Ps2IconSys iconSys;
    int parseOk = (ps2IconSysParse(iconSysBuf, iconSysSize, &iconSys) == 0);
    free(iconSysBuf);
    if (!parseOk) {
        entry->iconState = ICON_INVALID;
        return;
    }

    /* Title priority (plan section 13): a sane, ASCII icon.sys title
     * beats the filename-derived default main.c already filled in. */
    if (iconSys.title[0]) {
        strncpy(entry->title, iconSys.title, APP_ENTRY_TITLE_MAX - 1);
        entry->title[APP_ENTRY_TITLE_MAX - 1] = '\0';
    }

    char iconPath[ICON_PATH_SCRATCH_MAX];
    if (safeConcat(iconPath, sizeof(iconPath), dir, iconSys.normalIconFile) < 0) {
        entry->iconState = ICON_INVALID;
        return;
    }

    /* A real PS2 icon model is typically tens to a few hundred KB - 2MB
     * is a generous sanity cap against a corrupt/hostile icon.sys
     * pointing at an implausibly huge file, not a real format limit. */
    unsigned int modelSize;
    unsigned char *modelBuf = readWholeFile(iconPath, &modelSize, 2 * 1024 * 1024);
    if (!modelBuf) {
        entry->iconState = ICON_MISSING;
        return;
    }

    int slot = decodeIntoSlot(cache, entry, modelBuf, modelSize);
    free(modelBuf);

    if (slot < 0) {
        entry->iconState = ICON_INVALID;
        return;
    }

    entry->iconSlot = slot;
    entry->iconState = ICON_LOADED;
}

int iconCacheSlotForEntry(const IconCache *cache, const AppEntry *entry)
{
    (void)cache;
    if (entry->iconState == ICON_LOADED)
        return entry->iconSlot;
    return iconCacheFallbackSlot(fallbackForDevice(entry->device));
}

void iconCacheSlotUV(const IconCache *cache, int slot, float *u0, float *v0, float *u1, float *v1)
{
    (void)cache;
    int col = slot % ICON_CACHE_COLS;
    int row = slot / ICON_CACHE_COLS;
    *u0 = (float)(col * ICON_CACHE_CELL_SIZE);
    *v0 = (float)(row * ICON_CACHE_CELL_SIZE);
    *u1 = (float)((col + 1) * ICON_CACHE_CELL_SIZE);
    *v1 = (float)((row + 1) * ICON_CACHE_CELL_SIZE);
}
