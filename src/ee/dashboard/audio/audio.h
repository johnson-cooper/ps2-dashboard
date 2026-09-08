#ifndef PS2LAUNCHER_AUDIO_H
#define PS2LAUNCHER_AUDIO_H

/* M14 (optional, per the plan's own framing - "Optional: Sound
 * Integration"): a thin wrapper around audsrv for menu blips and BGM.
 * audsrv itself isn't part of the default ps2sdk registry (LGPL-2, kept
 * in its own repo rather than the AFL-licensed core tree - see its own
 * README) and was installed here as a custom package
 * (git.techwritescode.dev/ps2/audsrv), matching the plan's original M14
 * task.
 *
 * Two different playback paths for two different jobs:
 * - The menu blip uses audsrv_play_audio()'s plain PCM streaming path -
 *   raw 16-bit PCM, audsrv handles resampling to SPU2's native rate, no
 *   special encoding needed beyond stripping a WAV header. Simple, but
 *   its ring buffer is small (~5-9KB - see audio.c), fine for a short
 *   blip fed a chunk at a time, unreasonable for a 45-second track.
 * - BGM uses audsrv_load_adpcm()/audsrv_ch_play_adpcm() instead: the
 *   whole track is uploaded once to SPU2's own dedicated 2MB sound RAM
 *   as PS-ADPCM (SPU2's native compressed format, ~4:1 vs 16-bit PCM)
 *   and then plays back and loops entirely in hardware, no per-frame
 *   feeding at all. Encoded with adpenc, the real tool
 *   audsrv_load_adpcm() is built against (ps2build's own tools/adpenc.exe
 *   - a different tool, ps2adpcm.exe, produces a similar-looking but
 *   incompatible format that plays as silence, a real, documented trap).
 */

/* Loads libsd.irx (audsrv.irx's own IOP dependency) and audsrv.irx, and
 * initializes audsrv. Sound is explicitly optional per the plan - any
 * failure here just means audioPlayBlip() becomes a silent no-op for the
 * rest of the session, never something that blocks or degrades the rest
 * of the dashboard. */
void audioInit(void);

/* Fire-and-forget: queues the embedded blip for playback (configuring
 * audsrv's output format to match it first). Silently does nothing if
 * audioInit() failed or hasn't been called. */
void audioPlayBlip(void);

/* Must be called once per frame regardless of whether a blip was just
 * played - explicitly stops playback once the currently-playing blip's
 * own duration has elapsed (see audio.c's header comment for why this is
 * necessary: audsrv_play_audio() is a streaming API with no built-in
 * "end of clip" concept). A no-op on frames where nothing is playing. */
void audioTick(void);

/* Uploads the embedded BGM track to SPU2 and starts it looping. Safe to
 * call once at boot; does nothing if audioInit() failed. Unlike the
 * blip, once started this needs no further per-frame calls - the SPU2
 * hardware handles looping playback entirely on its own. */
void audioPlayMusic(void);

#endif
