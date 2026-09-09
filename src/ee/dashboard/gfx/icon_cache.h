#ifndef PS2LAUNCHER_ICON_CACHE_H
#define PS2LAUNCHER_ICON_CACHE_H

#include <gsKit.h>

#include "fallback_icons.h"
#include "../app/app_entry.h"

/* The dashboard's whole icon subsystem (plan sections 6/7/10/11): a
 * single small VRAM texture atlas (same fixed-cell, single-upload, LRU
 * pattern as gfx/texture_atlas.c - proven, just not yet wired into the
 * dashboard target) holding:
 *   - the 8 built-in fallback badges (gfx/fallback_icons.h), rendered
 *     once at init into fixed, never-evicted slots;
 *   - up to ICON_CACHE_APP_SLOTS real, decoded PS2 app icons, LRU-
 *     evicted as the Library grid scrolls past more apps than fit.
 *
 * Real icon decoding (icon.sys discovery -> parse -> locate/read the
 * referenced .ico -> gfx/ps2_icon.c -> upload) only ever happens from
 * iconCacheProcessPending(), throttled to at most one app per call - the
 * caller (dashboard_ui.c) is expected to call it once per frame, never
 * more, so entering a Library screen full of never-before-seen icons
 * costs one decode per frame rather than a single multi-icon stutter
 * (plan section 17: "no per-frame icon decoding" means bounded, not
 * zero). Every AppEntry's iconState/iconSlot fields (app/app_entry.h)
 * are updated in place - a MISSING or INVALID result is cached exactly
 * like a LOADED one, so a bad/missing icon is never re-attempted on
 * every visit to the same tile. */

#define ICON_CACHE_CELL_SIZE 64
#define ICON_CACHE_COLS 4
#define ICON_CACHE_ROWS 6
#define ICON_CACHE_FALLBACK_SLOTS FALLBACK_COUNT
#define ICON_CACHE_APP_SLOTS ((ICON_CACHE_COLS * ICON_CACHE_ROWS) - ICON_CACHE_FALLBACK_SLOTS)
#define ICON_CACHE_PENDING_MAX 8

typedef struct {
    int ready; /* 0 if iconCacheInit() failed (VRAM exhaustion/allocation failure) - every other function below becomes a safe no-op, mirroring main.c's own fontOk gating around bitmapFontInit() */
    GSGLOBAL *gsGlobal;
    GSTEXTURE texture;
    unsigned char *pixels;

    /* LRU bookkeeping for the app-icon slots only (indices
     * ICON_CACHE_FALLBACK_SLOTS..capacity-1) - the fallback slots below
     * that are permanent and never appear here. slotKey is the AppEntry
     * whose icon currently occupies the slot (NULL = empty); using the
     * AppEntry's own address as the identity key is safe because
     * entries[] in main.c is a stable, file-scope static array for the
     * process's whole lifetime. */
    const AppEntry *slotKey[ICON_CACHE_COLS * ICON_CACHE_ROWS];
    unsigned int slotAge[ICON_CACHE_COLS * ICON_CACHE_ROWS];
    unsigned int clock;

    AppEntry *pending[ICON_CACHE_PENDING_MAX];
    int pendingCount;
} IconCache;

/* Allocates the atlas texture and renders all 8 fallback badges into
 * their permanent slots. Returns 0 on success, negative on VRAM
 * exhaustion (mirrors gfx/texture_atlas.c's own convention) - the caller
 * should treat that the same way main.c already treats a font/theme
 * load failure: degrade (no icons at all, fallbackSlot draws are simply
 * skipped) rather than crash. */
int iconCacheInit(IconCache *cache, GSGLOBAL *gsGlobal);

/* Always valid, no decode/eviction involved - the 8 fallback badges live
 * in fixed slots for the cache's entire lifetime. */
int iconCacheFallbackSlot(FallbackIconKind kind);

/* Called once per visible/soon-visible Library tile (or Home/System row
 * needing a device-type badge). A no-op if icon resolution has already
 * been requested or completed for this entry; otherwise marks
 * entry->iconState = ICON_PENDING and queues it (bounded - if the queue
 * is full, this entry simply waits for a future call once a queue slot
 * frees up). */
void iconCacheRequest(IconCache *cache, AppEntry *entry);

/* Performs at most one real decode: pops one entry from the pending
 * queue (if any) and resolves its icon end-to-end (icon.sys discovery,
 * parse, .ico read + decode, atlas upload), setting its iconState/
 * iconSlot in place. Meant to be called exactly once per frame - see the
 * header comment above. A no-op if the pending queue is empty. */
void iconCacheProcessPending(IconCache *cache);

/* Returns the slot to draw for `entry` right now - its own decoded icon
 * if iconState == ICON_LOADED, otherwise a sensible fallback badge based
 * on entry->device. Never fails/returns an invalid index. */
int iconCacheSlotForEntry(const IconCache *cache, const AppEntry *entry);

/* UV rectangle (texture-space pixels) for a given slot index, for
 * gsKit_prim_sprite_texture-style draws. */
void iconCacheSlotUV(const IconCache *cache, int slot, float *u0, float *v0, float *u1, float *v1);

#endif
