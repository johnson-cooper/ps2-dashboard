#include "ps2_icon.h"

#include <string.h>

/* --- icon.sys ------------------------------------------------------- */

#define ICONSYS_OFF_TITLE 192
#define ICONSYS_TITLE_LEN 68
#define ICONSYS_OFF_NORMAL_ICON 260
#define ICONSYS_FILENAME_LEN 64
/* Covers magic + title + the normal-icon filename field - the only
 * fields this dashboard actually reads. The full on-disk file is always
 * 964 bytes (background/lighting fields + copy/delete icon references +
 * reserved padding), but nothing past offset 324 is needed here, and
 * requiring only that much is more lenient toward a slightly truncated
 * read without weakening any check this dashboard actually relies on. */
#define ICONSYS_MIN_SIZE (ICONSYS_OFF_NORMAL_ICON + ICONSYS_FILENAME_LEN)

/* icon.sys titles are Shift-JIS - this dashboard's only text renderer
 * (gfx/bitmap_font.c) is a plain 8x16 ASCII bitmap font with no
 * Shift-JIS glyphs, so a title containing any non-ASCII byte can't be
 * rendered correctly at all (a lone high-bit byte from a 2-byte SJIS
 * sequence would render as an unrelated Latin-1/garbage glyph, not
 * "degraded Japanese text"). A title is only accepted if every byte is a
 * plain printable ASCII character; anything else is rejected wholesale
 * and the caller falls back to the ELF's filename instead (plan section
 * 13's title priority). This is a real, documented limitation, not a bug
 * - see ps2_icon.h. */
static int sanitizeAscii(const unsigned char *src, int maxLen, char *out, int outSize, int rejectSlashes)
{
    int i;
    int n = 0;
    for (i = 0; i < maxLen && src[i] != 0; i++) {
        unsigned char c = src[i];
        if (c < 0x20 || c > 0x7E)
            return -1;
        if (rejectSlashes && (c == '/' || c == '\\'))
            return -1;
        if (n < outSize - 1)
            out[n++] = (char)c;
    }
    out[n] = '\0';
    return (n > 0) ? 0 : -1;
}

int ps2IconSysParse(const unsigned char *buf, unsigned int size, Ps2IconSys *out)
{
    memset(out, 0, sizeof(*out));

    if (size < ICONSYS_MIN_SIZE)
        return -1;
    if (buf[0] != 'P' || buf[1] != 'S' || buf[2] != '2' || buf[3] != 'D')
        return -1;

    /* Not fatal on its own - an unrepresentable/corrupt title just means
     * out->title stays empty and the caller uses the ELF's filename
     * instead (see the header comment). */
    if (sanitizeAscii(buf + ICONSYS_OFF_TITLE, ICONSYS_TITLE_LEN, out->title, sizeof(out->title), 0) < 0)
        out->title[0] = '\0';

    /* The icon filename IS load-bearing - reject the whole icon.sys as
     * unusable if it's missing/unsafe (defends against a filename trying
     * to escape the ELF's own directory via '/' or '..'). */
    if (sanitizeAscii(buf + ICONSYS_OFF_NORMAL_ICON, ICONSYS_FILENAME_LEN, out->normalIconFile,
                       sizeof(out->normalIconFile), 1) < 0)
        return -1;
    if (strcmp(out->normalIconFile, "..") == 0)
        return -1;

    return 0;
}

/* --- .ico 3D icon model ---------------------------------------------- */

#define ICONMODEL_HEADER_SIZE 20
#define ICONMODEL_TEX_W 128
#define ICONMODEL_TEX_H 128
#define ICONMODEL_TEX_PIXELS (ICONMODEL_TEX_W * ICONMODEL_TEX_H)
#define ICONMODEL_TEX_RAW_BYTES (ICONMODEL_TEX_PIXELS * 2) /* fixed - textures are always 128x128x16bpp, no header of their own (spec section 6) */

/* Defensive caps, not format limits - real icons are a few hundred to a
 * few thousand vertices with at most a handful of animation shapes; a
 * file claiming absurd values here is corrupt/hostile, not a bigger
 * legitimate icon. Keeping vertexCount * perVertexBytes comfortably
 * inside unsigned range avoids any need for 64-bit overflow arithmetic. */
#define ICONMODEL_MAX_ANIM_SHAPES 64
#define ICONMODEL_MAX_VERTICES 200000

/* How far past the end of the vertex segment to search for the
 * compressed-texture size field (see the header comment on the search
 * strategy below). Generous for any realistic single/few-frame icon
 * animation, tiny next to a multi-KB texture payload, and still a hard,
 * bounded scan - never a risk of a long/hanging search on a corrupt
 * file. */
#define ICONMODEL_ANIM_SEARCH_WINDOW 4096

static unsigned int readU32LE(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned int readU16LE(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static void timTexelToRGB(unsigned int texel, unsigned int *r, unsigned int *g, unsigned int *b)
{
    /* BGR555-ish TIM texel -> 8bpp per channel (PS2 Icon Format v0.5,
     * section 6.1's conversion table). Bit 15 (STP) is unused here - an
     * icon thumbnail is always fully opaque. */
    *r = (texel & 0x1F) * 8;
    *g = ((texel >> 5) & 0x1F) * 8;
    *b = ((texel >> 10) & 0x1F) * 8;
}

/* Un-RLEs a compressed TIM payload into a fixed 128x128 texel budget.
 * Algorithm per "PS2 Icon Format v0.5" section 6.2, cross-checked
 * against the ticky/ps2iconsys reference decoder (src/ps2_ps2icon.cpp):
 * a run of u16 words, each led by a u16 code - code < 0xFF00 replicates
 * the following u16 pixel value `code` times; code >= 0xFF00 copies the
 * next (0x10000 - code) u16 values literally.
 *
 * Real, working icon files (including the one this dashboard ships as a
 * reference) have been observed to let their very last run's requested
 * count run a little past the exact 16384-pixel end of the image - the
 * reference decoder above doesn't bounds-check its output array at all,
 * so this harmless-in-practice overshoot is silently absorbed there.
 * This decoder is bounds-checked (never writes past outTexels' fixed
 * size) and treats hitting the pixel budget as success rather than an
 * error, clamping only the final run rather than rejecting an otherwise
 * good icon over its last few pixels. Any unused trailing input bytes
 * once the budget is filled are simply ignored - this dashboard only
 * ever needs a complete decoded image, not a byte-exact re-encode.
 * Running out of INPUT before the budget is filled, however, is treated
 * as genuine corruption (returns -1). */
static int rleDecodeTexture(const unsigned char *data, unsigned int compressedSize, unsigned short *outTexels)
{
    unsigned int inPos = 0;
    unsigned int outPos = 0;
    /* Hard iteration cap: every real op consumes at least 2 input bytes,
     * so this can never legitimately be reached - it only guards a
     * corrupt stream that (e.g.) encodes count=0 forever from looping
     * without making progress. */
    unsigned int opsLeft = compressedSize / 2 + ICONMODEL_TEX_PIXELS + 16;

    while (outPos < ICONMODEL_TEX_PIXELS) {
        if (opsLeft-- == 0)
            return -1;
        if (inPos + 2 > compressedSize)
            return -1;
        unsigned int code = readU16LE(data + inPos);
        inPos += 2;

        if (code < 0xFF00) {
            if (inPos + 2 > compressedSize)
                return -1;
            unsigned int value = readU16LE(data + inPos);
            inPos += 2;

            unsigned int count = code;
            if (outPos + count > ICONMODEL_TEX_PIXELS)
                count = ICONMODEL_TEX_PIXELS - outPos; /* absorb a harmless final-run overshoot - see comment above */

            unsigned int i;
            for (i = 0; i < count; i++)
                outTexels[outPos++] = (unsigned short)value;
        } else {
            unsigned int length = 0x10000u - code;
            unsigned int i;
            for (i = 0; i < length; i++) {
                if (outPos >= ICONMODEL_TEX_PIXELS)
                    break; /* same final-run overshoot allowance, literal case */
                if (inPos + 2 > compressedSize)
                    return -1;
                outTexels[outPos++] = (unsigned short)readU16LE(data + inPos);
                inPos += 2;
            }
        }
    }

    return 0;
}

/* Locates the texture segment without needing to fully, precisely parse
 * the animation segment's internal fields - the animation segment's
 * documented layout has real, confirmed ambiguity even in its own
 * reference sources (frame-key semantics vary between documented
 * specs), and this dashboard never renders animation anyway (see
 * ps2_icon.h). Instead, this exploits two facts the format layout does
 * guarantee: the texture segment is always the last thing in the file,
 * and - only for the compressed case - its first u32 is exactly the
 * number of bytes remaining after it. Searching a small bounded window
 * right after the (reliably computed) vertex segment for a u32 value
 * that exactly equals "bytes remaining after this field" is safe (never
 * an out-of-bounds read), bounded (never a long/hanging scan), and
 * correct regardless of exactly how many animation frames/keys sit in
 * between - a coincidental false match is astronomically unlikely (a
 * random field would have to exactly predict the file's own remaining
 * length). Returns 0 and fills outDataOffset / outCompressedSize on a
 * confident match, negative otherwise. */
static int findCompressedTexture(const unsigned char *buf, unsigned int size, unsigned int searchStart,
                                  unsigned int *outDataOffset, unsigned int *outCompressedSize)
{
    unsigned int windowEnd = searchStart + ICONMODEL_ANIM_SEARCH_WINDOW;
    if (windowEnd > size)
        windowEnd = size;

    unsigned int off;
    for (off = searchStart; off + 4 <= windowEnd; off += 4) {
        unsigned int val = readU32LE(buf + off);
        unsigned int remaining = size - off - 4;
        if (val > 0 && val == remaining) {
            *outDataOffset = off + 4;
            *outCompressedSize = val;
            return 0;
        }
    }
    return -1;
}

int ps2IconModelDecodeThumbnail(const unsigned char *buf, unsigned int size, unsigned char *outRGBA, int outW,
                                 int outH)
{
    if (size < ICONMODEL_HEADER_SIZE)
        return -1;
    if (readU32LE(buf + 0) != 0x00010000u)
        return -1;

    unsigned int animationShapes = readU32LE(buf + 4);
    unsigned int vertexCount = readU32LE(buf + 16);

    if (animationShapes < 1 || animationShapes > ICONMODEL_MAX_ANIM_SHAPES)
        return -1;
    if (vertexCount == 0 || vertexCount > ICONMODEL_MAX_VERTICES || vertexCount % 3 != 0)
        return -1;

    unsigned int perVertexBytes = animationShapes * 8 + 16;
    unsigned int vertexSegmentSize = vertexCount * perVertexBytes;
    unsigned int vertexSegmentEnd = ICONMODEL_HEADER_SIZE + vertexSegmentSize;
    if (vertexSegmentEnd < ICONMODEL_HEADER_SIZE || vertexSegmentEnd > size) /* overflow or doesn't fit */
        return -1;

    /* Static, reused scratch buffer - decoding is already throttled to at
     * most one icon per frame (gfx/icon_cache.c), and a fixed 32KB buffer
     * here avoids any per-icon heap allocation/churn (plan section 17). */
    static unsigned short texels[ICONMODEL_TEX_PIXELS];

    unsigned int dataOffset, compressedSize;
    if (findCompressedTexture(buf, size, vertexSegmentEnd, &dataOffset, &compressedSize) == 0) {
        if (rleDecodeTexture(buf + dataOffset, compressedSize, texels) < 0)
            return -1;
    } else if (size >= vertexSegmentEnd + ICONMODEL_TEX_RAW_BYTES) {
        /* Not compressed (or the search above genuinely found nothing) -
         * fall back to reading the last 128x128x16bpp block directly;
         * per the format's own layout, the texture is always the final
         * segment regardless of how much animation data precedes it. */
        unsigned int rawOffset = size - ICONMODEL_TEX_RAW_BYTES;
        unsigned int i;
        for (i = 0; i < ICONMODEL_TEX_PIXELS; i++)
            texels[i] = (unsigned short)readU16LE(buf + rawOffset + i * 2);
    } else {
        return -1;
    }

    if (outW <= 0 || outH <= 0 || outW > ICONMODEL_TEX_W || outH > ICONMODEL_TEX_H)
        return -1;

    /* Box-downsample 128x128 -> outW x outH, converting BGR555->RGBA8888
     * per source texel as it's sampled (never materializing a full
     * 128x128 RGBA intermediate). */
    int ox, oy;
    for (oy = 0; oy < outH; oy++) {
        int syStart = (oy * ICONMODEL_TEX_H) / outH;
        int syEnd = ((oy + 1) * ICONMODEL_TEX_H) / outH;
        if (syEnd <= syStart)
            syEnd = syStart + 1;

        for (ox = 0; ox < outW; ox++) {
            int sxStart = (ox * ICONMODEL_TEX_W) / outW;
            int sxEnd = ((ox + 1) * ICONMODEL_TEX_W) / outW;
            if (sxEnd <= sxStart)
                sxEnd = sxStart + 1;

            unsigned int sumR = 0, sumG = 0, sumB = 0, count = 0;
            int sy, sx;
            for (sy = syStart; sy < syEnd; sy++) {
                for (sx = sxStart; sx < sxEnd; sx++) {
                    unsigned int r, g, b;
                    timTexelToRGB(texels[sy * ICONMODEL_TEX_W + sx], &r, &g, &b);
                    sumR += r;
                    sumG += g;
                    sumB += b;
                    count++;
                }
            }

            unsigned char *dst = outRGBA + (oy * outW + ox) * 4;
            dst[0] = (unsigned char)(sumR / count);
            dst[1] = (unsigned char)(sumG / count);
            dst[2] = (unsigned char)(sumB / count);
            dst[3] = 0xFF;
        }
    }

    return 0;
}
