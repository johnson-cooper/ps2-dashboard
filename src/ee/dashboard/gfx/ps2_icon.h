#ifndef PS2LAUNCHER_PS2_ICON_H
#define PS2LAUNCHER_PS2_ICON_H

/* Parses the two PS2 application-icon file formats (plan sections 6-9).
 *
 * icon.sys: a fixed 964-byte metadata file - "PS2D" magic, a background
 * gradient + 3-light setup (both ignored here, this dashboard draws its
 * own PSBBN-style background, not an icon's), a title, and 3 icon
 * filename references (normal/copy/delete - usually all the same file).
 * Layout confirmed byte-for-byte against a real sample plus the
 * documented spec at https://www.ps2savetools.com/documents/iconsys-format/.
 *
 * <name>.ico: NOT a Windows icon despite the extension - a 3D model (a
 * triangle mesh plus optional keyframe animation) ending in an embedded
 * 128x128 TIM texture, stored raw or RLE-compressed. Layout and the RLE
 * algorithm confirmed against Martin Akesson's "PS2 Icon Format v0.5"
 * (ps2savetools.com), the ticky/ps2iconsys reference decoder, and a real
 * sample file (github.com/ticky/ps2iconsys, src/ps2_ps2icon.cpp).
 *
 * This dashboard never renders the 3D model or its animation - only the
 * trailing 2D texture is decoded, as a flat thumbnail (see the plan's
 * "Option C" - the best compatibility/memory/complexity tradeoff for a
 * dashboard tile, not a save-browser). Every parse function here is
 * defensive: a corrupt, truncated, or adversarial file degrades to a
 * negative return, never a crash or an out-of-bounds read/write. */

#define PS2ICON_TITLE_MAX 69 /* 68 bytes on disk + NUL */
#define PS2ICON_FILENAME_MAX 64

typedef struct {
    char title[PS2ICON_TITLE_MAX];          /* sanitized printable ASCII; empty if the on-disk title wasn't representable */
    char normalIconFile[PS2ICON_FILENAME_MAX]; /* plain filename (no path), to be resolved in the same directory as icon.sys itself */
} Ps2IconSys;

/* Parses an in-memory icon.sys buffer (read whole via fileXio - never
 * fopen, matching every other loader in this codebase; see
 * app/icon_discovery in main.c). Returns 0 on success, negative if the
 * buffer is too short or the magic doesn't match. A title that exists
 * but isn't representable in this dashboard's ASCII-only bitmap font
 * (gfx/bitmap_font.c) - e.g. genuine Shift-JIS text - is not itself a
 * parse failure: out->title is simply left empty, and the caller falls
 * back to the ELF's filename (plan section 13's title priority). */
int ps2IconSysParse(const unsigned char *buf, unsigned int size, Ps2IconSys *out);

/* Decodes a PS2 icon model (.ico)'s embedded texture into an RGBA8888
 * thumbnail of exactly outW x outH pixels (box-downsampled from the
 * format's native 128x128), written into a caller-owned buffer of
 * outW*outH*4 bytes. The geometry/animation segments are walked only far
 * enough to locate the texture (never rendered). Returns 0 on success,
 * negative if the file is too short, has an invalid header, or the
 * texture couldn't be located/decoded. */
int ps2IconModelDecodeThumbnail(const unsigned char *buf, unsigned int size, unsigned char *outRGBA, int outW, int outH);

#endif
