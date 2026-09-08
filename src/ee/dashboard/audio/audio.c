#include "audio.h"

#include <sifrpc.h>
#include <loadfile.h>
#include <audsrv.h>

#include "../log/log.h"

extern unsigned char sfx_blip_pcm[];
extern unsigned int size_sfx_blip_pcm;
extern unsigned char music_adpcm[];
extern unsigned int size_music_adpcm;

/* The embedded blip's real format (menu_button_select.wav, downmixed to
 * mono - it shipped stereo). */
#define BLIP_SAMPLE_RATE 44100

static int audioOk = 0;

/* audsrv_play_audio() is a continuous-streaming ring-buffer API, not a
 * one-shot player, and its ring buffer is small: audsrv's own IOP source
 * sizes it as `feed_size * 10`, where feed_size scales with the sample
 * rate - for this project's 44100Hz mono blip that works out to roughly
 * 5-9KB, while the blip itself is ~36KB. Passing the whole buffer to one
 * audsrv_play_audio() call therefore silently drops most of it (its
 * return value is "bytes actually accepted", which was never being
 * checked) - confirmed as the real cause of an audible stutter, separate
 * from the earlier format-reset bug. Fed here in ring-buffer-sized
 * chunks across multiple frames instead, checking audsrv_available()
 * each frame rather than blocking with audsrv_wait_audio() (which would
 * stall the whole render loop while audio drains in real time - a
 * visible hitch, not just an audio one). */
static const unsigned char *blipCursor = NULL;
static int blipRemaining = 0;
static int blipStopFramesLeft = 0;

void audioInit(void)
{
    if (SifLoadModule("host:modules/libsd.irx", 0, NULL) < 0) {
        logMsg("audio: libsd.irx load failed");
        return;
    }
    if (SifLoadModule("host:modules/audsrv.irx", 0, NULL) < 0) {
        logMsg("audio: audsrv.irx load failed");
        return;
    }

    if (audsrv_init() != 0) {
        logMsg("audio: audsrv_init failed: %s", audsrv_get_error_string());
        return;
    }

    /* Set once here, not per play - the docs warn that changing format
     * while audio might still be playing risks mismatched/corrupted
     * output, and reconfiguring on every button press (including rapid
     * repeats, well within the blip's own duration) was confirmed to
     * cause an audible stutter on its own, separate from the ring-buffer
     * issue above. The format is fixed for as long as this project only
     * has one embedded sound. */
    audsrv_fmt_t fmt;
    fmt.freq = BLIP_SAMPLE_RATE;
    fmt.bits = 16;
    fmt.channels = 1;
    if (audsrv_set_format(&fmt) != 0) {
        logMsg("audio: audsrv_set_format failed: %s", audsrv_get_error_string());
        return;
    }

    /* Required once before any audsrv_load_adpcm()/audsrv_ch_play_adpcm()
     * call - matches the official playadpcm sample's own init sequence.
     * Independent of the PCM streaming path above (blip), which doesn't
     * touch the ADPCM channel system at all. */
    if (audsrv_adpcm_init() < 0)
        logMsg("audio: audsrv_adpcm_init failed");

    audioOk = 1;
    logMsg("audio: ready");
}

void audioPlayMusic(void)
{
    if (!audioOk)
        return;

    /* music_adpcm is the real adpenc-produced .sad file, embedded
     * verbatim (its own 16-byte "APCM" header included) - audsrv parses
     * that header itself (pitch/loop/channels) and DMAs the rest
     * straight to SPU2's dedicated sound RAM, so `sample` here is passed
     * in uninitialized; audsrv_load_adpcm() fills it in, matching the
     * official playadpcm sample exactly. Loop is already baked into the
     * encoded data (adpenc -L), so audsrv_ch_play_adpcm() loops it in
     * hardware with no further calls needed from us. */
    static audsrv_adpcm_t sample;
    if (audsrv_load_adpcm(&sample, music_adpcm, (int)size_music_adpcm) < 0) {
        logMsg("audio: music load failed: %s", audsrv_get_error_string());
        return;
    }

    int channel = audsrv_ch_play_adpcm(-1, &sample);
    if (channel < 0) {
        logMsg("audio: music play failed: %s", audsrv_get_error_string());
        return;
    }

    audsrv_adpcm_set_volume_and_pan(channel, MAX_VOLUME, 0);
    logMsg("audio: music playing on channel %d", channel);
}

void audioPlayBlip(void)
{
    if (!audioOk)
        return;

    /* Deliberately NOT calling audsrv_stop_audio() here - that was tried
     * as a fix for the blip going silent after repeated use, and it was
     * actually the real CAUSE, confirmed by reading audsrv's IOP source:
     * its internal drain thread (the one that advances the ring buffer's
     * read pointer, `if (playing && ...)`) is gated by the same
     * `playing` flag audsrv_stop_audio() clears to 0. Calling it here
     * froze the read pointer immediately on every new press; the write
     * pointer (still advancing as audioTick() feeds new chunks) would
     * eventually catch up to that frozen read pointer, filling the ring
     * buffer permanently (audsrv_available() stuck at 0 forever) - and
     * since audioTick() only ever calls audsrv_play_audio() (the one
     * thing that resets playing=1 and un-freezes the drain thread) when
     * avail > 0, that's an unrecoverable deadlock once it happens.
     * Simply overwriting the cursor/remaining state below is enough: the
     * drain thread doesn't care *which* blip it's draining, it just
     * keeps consuming whatever's actually in the buffer, so interrupting
     * mid-play needs no explicit stop - only the natural end-of-clip
     * stop (via blipStopFramesLeft below) still needs one. */
    blipCursor = sfx_blip_pcm;
    blipRemaining = (int)size_sfx_blip_pcm;

    /* Started immediately, not once all chunks finish sending - real
     * playback begins as soon as the first chunk is queued, and since
     * the ring buffer usually refills fast enough to hand off every
     * chunk within just the first frame or two, timing duration from
     * "done sending" (the earlier version of this) meant the stop timer
     * fired well after the audio had actually finished playing, leaving
     * a window where the idle ring buffer looped its tail - confirmed
     * real: exactly the stutter reported near the end of the clip.
     * size_sfx_blip_pcm is bytes of 16-bit mono PCM, so /2 is sample
     * count; duration in ~60Hz video frames, rounded up by one so it's
     * never cut off a frame early. Video frame rate and the SPU2 audio
     * clock aren't actually locked together, so this is an
     * approximation - fine for a short UI blip, same tolerance already
     * accepted for the once-a-second RTC poll elsewhere in this
     * project. */
    unsigned int sampleCount = size_sfx_blip_pcm / 2;
    blipStopFramesLeft = (sampleCount * 60) / BLIP_SAMPLE_RATE + 1;
}

void audioTick(void)
{
    if (!audioOk)
        return;

    /* Feeding and the stop countdown run concurrently, not one after the
     * other - see audioPlayBlip()'s comment on why the countdown starts
     * at trigger time rather than waiting for feeding to finish. */
    if (blipRemaining > 0) {
        /* Defensive logging kept permanently, not just for the M14
         * debugging session that needed it - if audsrv ever gets into a
         * bad state again (a real error, or avail stuck at 0), this is
         * the only diagnostic trail a real deployment will have, same
         * reasoning as every other logMsg() call in this project. Costs
         * nothing when everything's working, since it only fires on an
         * actual failure/stuck condition. */
        static int stuckFrames = 0;
        int avail = audsrv_available();
        if (avail > 0) {
            int chunk = (avail < blipRemaining) ? avail : blipRemaining;
            int sent = audsrv_play_audio((const char *)blipCursor, chunk);
            if (sent > 0) {
                blipCursor += sent;
                blipRemaining -= sent;
                stuckFrames = 0;
            } else {
                logMsg("audio: play ret=%d avail=%d chunk=%d err=%s", sent, avail, chunk,
                       audsrv_get_error_string());
            }
        } else {
            stuckFrames++;
            if (stuckFrames == 60)
                logMsg("audio: stuck 1s, avail=%d remaining=%d err=%s", avail, blipRemaining,
                       audsrv_get_error_string());
        }
    }

    if (blipStopFramesLeft > 0) {
        blipStopFramesLeft--;
        if (blipStopFramesLeft == 0) {
            /* Never stop while there's still unsent data - audsrv_stop_
             * audio() freezes the drain thread (see audioPlayBlip()'s
             * comment), and firing it mid-transfer is just the same
             * deadlock reached a different way: a rapid-enough repeat
             * can mean the full ~36KB blip hasn't finished feeding by
             * the time this fixed ~25-frame countdown elapses (the ring
             * buffer only holds ~50ms at a time, so feeding is itself
             * rate-limited by real playback time, not just CPU speed) -
             * confirmed real, this was still killing the blip after the
             * earlier fix. Deferring a frame at a time until sending
             * actually finishes, instead of stopping on a schedule that
             * assumed it always would have by now. */
            if (blipRemaining > 0)
                blipStopFramesLeft = 1;
            else
                audsrv_stop_audio();
        }
    }
}
