/* M7 texture/icon manager test harness: writes 8 embedded test icons to
 * mc0:, then cycles two 4-icon "pages" through a 2x2-slot (capacity 4)
 * atlas every few seconds - every page switch evicts all 4 resident
 * icons and decodes the other page's, since neither page's icons
 * overlap. Visual confirmation the atlas + LRU eviction works: the
 * grid always shows 4 correctly-colored, correctly-bordered icons
 * matching the current page, never garbage or a stale icon from the
 * other page.
 *
 * No libdebug (init_scr/scr_printf) here, deliberately: this is the
 * first program in the project to need both fileXio-based module
 * loading feedback AND real gsKit rendering in the same run, and
 * libdebug + gsKit both drive the GS directly - confirmed, empirically,
 * to garble the display when combined (overlapping/duplicated text,
 * squished sprite layout) the one time this was tried. The computed
 * VRAM budget numbers are instead written to a fixed, otherwise-unused
 * low-memory address for inspection via the PCSX2 debugger
 * (mcpcsx2 read_memory 0x00090000, 4x u32: fbSize, zSize, atlasSize,
 * total), rather than printed on screen. */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <gsKit.h>
#include <dmaKit.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "../dashboard/gfx/texture_atlas.h"

extern unsigned char icon_red_png[];
extern unsigned int size_icon_red_png;
extern unsigned char icon_green_png[];
extern unsigned int size_icon_green_png;
extern unsigned char icon_blue_png[];
extern unsigned int size_icon_blue_png;
extern unsigned char icon_yellow_png[];
extern unsigned int size_icon_yellow_png;
extern unsigned char icon_cyan_png[];
extern unsigned int size_icon_cyan_png;
extern unsigned char icon_magenta_png[];
extern unsigned int size_icon_magenta_png;
extern unsigned char icon_orange_png[];
extern unsigned int size_icon_orange_png;
extern unsigned char icon_white_png[];
extern unsigned int size_icon_white_png;

/* IOP modules embedded via bin2c'd package artifacts - see dashboard's
 * main.c for why this replaces loading modules from a "host:" path. */
extern unsigned char iomanX_irx[];
extern unsigned int size_iomanX_irx;
extern unsigned char fileXio_irx[];
extern unsigned int size_fileXio_irx;
extern unsigned char mcman_irx[];
extern unsigned int size_mcman_irx;
extern unsigned char mcserv_irx[];
extern unsigned int size_mcserv_irx;

#define DEBUG_INFO_ADDR 0x00090000

static void writeFile(const char *path, const void *data, unsigned int size)
{
    int fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd < 0)
        return;
    fileXioWrite(fd, data, size);
    fileXioClose(fd);
}

int main(int argc, char *argv[])
{
    SifInitRpc(0);

    /* See dashboard's main.c for why this is required before any
     * SifExecModuleBuffer call. */
    sbv_patch_enable_lmb();

    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifExecModuleBuffer(iomanX_irx, size_iomanX_irx, 0, NULL, NULL);
    SifExecModuleBuffer(fileXio_irx, size_fileXio_irx, 0, NULL, NULL);
    SifExecModuleBuffer(mcman_irx, size_mcman_irx, 0, NULL, NULL);
    SifExecModuleBuffer(mcserv_irx, size_mcserv_irx, 0, NULL, NULL);
    fileXioInit();
    fileXioSetRWBufferSize(128 * 1024);

    writeFile("mc0:/icon_red.png", icon_red_png, size_icon_red_png);
    writeFile("mc0:/icon_green.png", icon_green_png, size_icon_green_png);
    writeFile("mc0:/icon_blue.png", icon_blue_png, size_icon_blue_png);
    writeFile("mc0:/icon_yellow.png", icon_yellow_png, size_icon_yellow_png);
    writeFile("mc0:/icon_cyan.png", icon_cyan_png, size_icon_cyan_png);
    writeFile("mc0:/icon_magenta.png", icon_magenta_png, size_icon_magenta_png);
    writeFile("mc0:/icon_orange.png", icon_orange_png, size_icon_orange_png);
    writeFile("mc0:/icon_white.png", icon_white_png, size_icon_white_png);

    /* gsKit_init_global() auto-detects Mode/Interlace/Field/Width/Height -
     * confirmed working as-is in M1/M2, don't override. */
    GSGLOBAL *gsGlobal = gsKit_init_global();
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(gsGlobal);
    gsKit_mode_switch(gsGlobal, GS_ONESHOT);

    TextureAtlas atlas;
    textureAtlasInit(&atlas, gsGlobal, 2, 2, 64);

    /* VRAM budget: gsKit_texture_size gives the real GS-aligned byte
     * footprint for a given width/height/PSM - used here for the
     * framebuffer(s), Z-buffer, and atlas the same way, rather than a
     * hand-rolled width*height*bpp estimate, so this is a real
     * "measured against gsKit's own allocator", not a guess. Confirmed
     * non-overlapping against gsGlobal's own ScreenBuffer[0]/[1]/ZBuffer
     * addresses during the M7 spike. */
    u32 fbSize = gsKit_texture_size(gsGlobal->Width, gsGlobal->Height, gsGlobal->PSM);
    if (gsGlobal->DoubleBuffering == GS_SETTING_ON)
        fbSize *= 2;
    u32 zSize = (gsGlobal->ZBuffering == GS_SETTING_ON)
                    ? gsKit_texture_size(gsGlobal->Width, gsGlobal->Height, gsGlobal->PSMZ)
                    : 0;
    u32 atlasSize = gsKit_texture_size(atlas.texture.Width, atlas.texture.Height, atlas.texture.PSM);
    u32 total = fbSize + zSize + atlasSize;

    volatile u32 *debugInfo = (volatile u32 *)DEBUG_INFO_ADDR;
    debugInfo[0] = fbSize;
    debugInfo[1] = zSize;
    debugInfo[2] = atlasSize;
    debugInfo[3] = total;

    const char *pageA[4] = { "mc0:/icon_red.png", "mc0:/icon_green.png",
                             "mc0:/icon_blue.png", "mc0:/icon_yellow.png" };
    const char *pageB[4] = { "mc0:/icon_cyan.png", "mc0:/icon_magenta.png",
                             "mc0:/icon_orange.png", "mc0:/icon_white.png" };

    u64 background = GS_SETREG_RGBAQ(0x10, 0x10, 0x10, 0x00, 0x00);
    u64 white = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00);
    float cellPx = 128.0f;
    float gapPx = 40.0f;
    float originX = 60.0f;
    float originY = 60.0f;

    int frame = 0;
    while (1) {
        int page = (frame / 180) % 2; /* switch page every ~3s at 60fps */
        const char **current = page == 0 ? pageA : pageB;

        /* Full-screen fill before drawing icons each frame - gsKit_clear()
         * doesn't reliably present when called every frame in GS_ONESHOT
         * mode in this environment (see plan section 7's confirmed
         * gotcha from M1/M2); a full-screen sprite is the working
         * equivalent. */
        gsKit_prim_sprite(gsGlobal, 0.0f, 0.0f, (float)gsGlobal->Width, (float)gsGlobal->Height, 0, background);

        /* gsKit's own png-texture example explicitly sets a blend
         * equation and disables alpha testing around textured sprite
         * draws - PrimAlphaEnable alone (set once, above) doesn't
         * configure the actual blend function, and without this the
         * default blend state produced wrong, channel-mixed-looking,
         * flickering colors (confirmed the real cause during the M7
         * spike - not a pixel byte-order issue, which was a red
         * herring). */
        gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
        gsKit_set_test(gsGlobal, GS_ATEST_OFF);

        int i;
        for (i = 0; i < 4; i++) {
            int slot = textureAtlasGetSlot(&atlas, current[i]);
            if (slot < 0)
                continue;

            float u0, v0, u1, v1;
            textureAtlasSlotUV(&atlas, slot, &u0, &v0, &u1, &v1);

            float x = originX + (i % 2) * (cellPx + gapPx);
            float y = originY + (i / 2) * (cellPx + gapPx);

            gsKit_prim_sprite_texture(gsGlobal, &atlas.texture, x, y, u0, v0, x + cellPx, y + cellPx, u1, v1, 1,
                                       white);
        }

        gsKit_set_test(gsGlobal, GS_ATEST_ON);
        gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);

        gsKit_queue_exec(gsGlobal);
        gsKit_sync_flip(gsGlobal);
        frame++;
    }

    return 0;
}
