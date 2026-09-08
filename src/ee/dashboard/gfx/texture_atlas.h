#ifndef PS2LAUNCHER_TEXTURE_ATLAS_H
#define PS2LAUNCHER_TEXTURE_ATLAS_H

#include <gsKit.h>

/* Fixed-size-cell icon texture atlas with LRU eviction. All cells are
 * cellSize x cellSize RGBA32 - fine for a first-pass icon manager
 * (uniform icon dimensions); scaling/cropping mismatched source images
 * is future work. Backed by a single VRAM texture (one gsKit upload
 * covers every resident icon) rather than one texture per icon, per the
 * plan's VRAM-budget guidance (section 7/10) - minimizes GS texture
 * switches and avoids per-icon VRAM allocation overhead. */
typedef struct {
    GSGLOBAL *gsGlobal;
    GSTEXTURE texture;
    unsigned char *pixels; /* EE-side atlas buffer, cols*cellSize x rows*cellSize RGBA32 */
    int cols, rows, cellSize;
    int capacity; /* cols*rows */

    /* Per-slot LRU bookkeeping. slotPath[i] is NULL for an empty slot.
     * slotAge is a monotonically increasing "clock" - the slot with the
     * smallest slotAge is the least recently used. */
    const char **slotPath;
    unsigned int *slotAge;
    unsigned int clock;

    /* Set by textureAtlasGetSlot() on its most recent call. */
    int lastEvicted;
    int lastHit;
} TextureAtlas;

/* Allocates the EE-side pixel buffer and uploads a cleared cols x rows
 * grid of cellSize x cellSize cells as one GS_PSM_CT32 VRAM texture. */
int textureAtlasInit(TextureAtlas *atlas, GSGLOBAL *gsGlobal, int cols, int rows, int cellSize);

/* Returns the slot index holding `path`'s icon, decoding and evicting
 * (LRU) into a slot first if it isn't already resident. Reads the PNG
 * via fileXio (NOT gsKit_texture_png/fopen - see the .c file for why)
 * and decodes directly into the slot's region of the atlas buffer, then
 * re-uploads the whole atlas texture. Sets atlas->lastHit (1 if already
 * resident, 0 if freshly decoded) and atlas->lastEvicted (the slot index
 * that was evicted, or -1 if an empty slot was used instead).
 * Returns a negative value if `path` couldn't be read or wasn't a valid
 * cellSize x cellSize PNG. */
int textureAtlasGetSlot(TextureAtlas *atlas, const char *path);

/* UV rectangle (in texture-space pixels, for gsKit_prim_sprite_texture
 * style calls) for a given slot. */
void textureAtlasSlotUV(const TextureAtlas *atlas, int slot, float *u0, float *v0, float *u1, float *v1);

#endif
