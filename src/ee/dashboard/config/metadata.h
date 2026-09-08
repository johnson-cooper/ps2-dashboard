#ifndef PS2LAUNCHER_METADATA_H
#define PS2LAUNCHER_METADATA_H

/* Per-app metadata (plan section 8): one small sibling file per app
 * (e.g. "mc0:/target.elf" -> "mc0:/target.elf.cfg"), not one monolithic
 * index - bounds the blast radius of corruption to a single entry, and
 * a stable text format is trivially diffable/debuggable, unlike a
 * binary struct where one bad byte can misalign every field after it.
 *
 * A dedicated CFG/ directory (matching OPL's convention, which the plan
 * originally called for) is a reasonable later refinement once the
 * device manager grows real per-device path conventions; a plain
 * sibling file is the simplest thing that demonstrates real load/save/
 * corruption-recovery behavior for this milestone. */
typedef struct {
    int launchCount;
    int favorite;
} AppMetadata;

/* Fills *out with saved metadata for elfPath, or all-zero defaults if
 * no metadata file exists yet, or its CRC32 trailer doesn't match (a
 * corrupt/partial write) - callers never need to distinguish "missing"
 * from "corrupt": both safely degrade to defaults rather than fail. */
void metadataLoad(const char *elfPath, AppMetadata *out);

/* Writes metadata for elfPath via write-temp-then-rename (fileXioRename
 * is close to atomic at the directory-entry level on both FAT and PFS)
 * plus a CRC32 trailer line, so a power-loss mid-write either leaves
 * the previous, still-valid file in place, or a same-named .tmp file
 * that's simply ignored - never a half-written file mistaken for good
 * data. Returns 0 on success. */
int metadataSave(const char *elfPath, const AppMetadata *meta);

#endif
