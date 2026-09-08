/* Chain-load test target. Fills the screen solid green - visually
 * unambiguous proof the chain-load actually replaced the caller, since
 * neither the dashboard (M8: colored grid) nor stage2 (blank/black)
 * ever show a plain solid-green fill.
 *
 * Used to validate multi-argument marshalling too (M5's "hello"/"42"
 * argv check) - that logic has been removed now that M8's dashboard
 * doesn't pass those, and the marshalling itself is already proven and
 * documented in the plan (section 5); a real per-app argv check belongs
 * to whatever an actual homebrew target does with its own arguments,
 * not this generic test stub. */
#include <gsKit.h>
#include <dmaKit.h>

int main(int argc, char *argv[])
{
    GSGLOBAL *gsGlobal = gsKit_init_global();
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(gsGlobal);
    gsKit_mode_switch(gsGlobal, GS_ONESHOT);

    u64 green = GS_SETREG_RGBAQ(0x00, 0x80, 0x00, 0x00, 0x00);

    while (1) {
        gsKit_prim_sprite(gsGlobal, 0.0f, 0.0f, (float)gsGlobal->Width, (float)gsGlobal->Height, 1, green);
        gsKit_queue_exec(gsGlobal);
        gsKit_sync_flip(gsGlobal);
    }

    return 0;
}
