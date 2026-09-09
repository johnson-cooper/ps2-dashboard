# Application icons

The dashboard shows a real PS2-native icon for a discovered ELF when one is
available, falling back to a built-in badge otherwise. This document covers
the on-disk convention, the supported file formats, and the cache's
behavior/limits. The parser lives in `src/ee/dashboard/gfx/ps2_icon.c`; the
cache and discovery pipeline live in `src/ee/dashboard/gfx/icon_cache.c`.

## Directory convention

For a discovered ELF at:

```
mass:/APPS/OPL/OPNPS2LD.ELF
```

the dashboard looks for `icon.sys` in the **same directory as the ELF**:

```
APPS/
  OPL/
    OPNPS2LD.ELF
    icon.sys
    opl.ico          <- filename comes from icon.sys, not assumed
```

`icon.sys`'s "normal icon" field names the actual icon file (see below) - it
does not have to be called `opl.ico`, and it is always resolved relative to
`icon.sys`'s own directory. There is no recursive search and no alternate
directory checked; this matches the one-shot, non-recursive ELF scan the
rest of the dashboard already uses (`main.c`'s `scanDevice()`).

If `icon.sys` is missing, or names an icon file that can't be read/parsed,
the app just gets a fallback badge - this is not treated as an error.

## `icon.sys`

A fixed **964-byte** binary file, magic `"PS2D"` at offset 0. Only two things
are actually read:

- **Title** (68 bytes at offset 192): PS2 icon titles are Shift-JIS. This
  dashboard's only text renderer is a plain 8x16 ASCII bitmap font with no
  Shift-JIS glyphs, so a title is only used if **every byte is printable
  ASCII** - otherwise it's discarded and the ELF's filename (minus `.elf`)
  is used instead. This is a real, deliberate limitation, not a bug.
- **Normal icon filename** (64 bytes at offset 260): a plain filename (no
  path separators are accepted), resolved in `icon.sys`'s own directory.

Everything else in the file (background gradient, 3-light setup, the
copy/delete icon filename fields) is parsed by nothing here - this dashboard
draws its own animated background, not a per-icon one, and never needs the
"copy this save"/"delete this save" icon variants a real memory-card browser
would.

Layout confirmed byte-for-byte against a real sample file plus the
documented spec at ps2savetools.com/documents/iconsys-format/.

## `.ico` (the PS2 icon model)

Despite the extension, this is **not** a Windows icon. It's a 3D model: a
triangle mesh with optional keyframe animation, ending in an embedded
128x128 TIM texture (raw or RLE-compressed).

This dashboard **never renders the 3D geometry or its animation** - only
the trailing 2D texture is decoded, box-downsampled to a 64x64 thumbnail.
This was a deliberate choice given the tradeoffs:

| Option | Compatibility | Memory | Complexity | Real PS2 performance |
|---|---|---|---|---|
| A. Render the icon model | Needs a real (if tiny) 3D pipeline per tile | Vertex/normal/anim buffers per icon | High | Risky at grid scale |
| B. Render to texture once, cache | Same rendering cost, just amortized | Same buffers, once | High | Fine after the one-time cost, but still needs the renderer |
| **C. Decode the embedded texture only** (chosen) | Just needs the header + texture segment | One 32KB scratch buffer, reused | Low | Cheap, one-time RLE decode per icon |

Option C gives a real, representative 2D thumbnail (the same artwork the PS2
BIOS/Browser would show as the icon's "face") without ever needing a 3D
renderer in the dashboard at all.

The header/vertex/RLE-decode layout was confirmed against Martin Åkesson's
"PS2 Icon Format v0.5" and the `ticky/ps2iconsys` reference decoder, then
verified against a real sample file byte-by-byte. One deliberate leniency:
a real icon's very last RLE run has been observed to request a handful more
pixels than remain in the 128x128 budget (a harmless encoder quirk) - the
decoder clamps that final run to fit rather than rejecting an otherwise-good
icon over its last few pixels.

## Fallback icons

If any step fails - no `icon.sys`, an unparsable `icon.sys`, a missing or
malformed `.ico`, or a texture that doesn't decode to a full image - the app
gets a built-in badge instead (`src/ee/dashboard/gfx/fallback_icons.c`): a
tinted circular badge with a single upscaled letter, reusing this project's
existing embedded bitmap font glyphs rather than adding new binary art
assets. The badge/letter is chosen by the app's source device (Disc, USB,
HDD, Memory Card, Network) or, for a generic homebrew ELF, a plain "E".

## Cache behavior and limits

- Icons live in one small, fixed VRAM texture atlas: 4x6 cells of 64x64
  pixels (256x384 total, ~384KB of the PS2's 4MB VRAM budget). Allocated
  once at boot, exactly like the bitmap font and theme backgrounds - if
  that allocation fails, the dashboard runs with no icons at all rather
  than crashing (mirrors the existing `fontOk` pattern for the bitmap
  font).
- The first 8 slots are permanent, holding the built-in fallback badges.
  The remaining slots are a bounded LRU pool for real, decoded app icons -
  navigating through more apps than fit simply evicts the least-recently-
  shown one; nothing leaks.
- Every `AppEntry` tracks its own icon state (not requested / pending /
  loaded / missing / invalid) so a missing or corrupt icon is only ever
  attempted once, not re-decoded on every visit to the same tile.
- Real decoding (`icon.sys` read, `.ico` read, RLE decode, atlas upload)
  only happens from a single "process one pending icon" call, invoked at
  most once per frame - browsing into a Library screen full of
  never-before-seen icons costs one decode per frame, not a single
  multi-icon stutter.
- A device rescan (USB unplugged, disc swapped) resets every entry's icon
  state, since the same `entries[]` slot may now hold a completely
  different app - it will simply be re-requested and re-decoded the next
  time its tile is visible.
