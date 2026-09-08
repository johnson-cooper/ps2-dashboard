#ifndef PS2LAUNCHER_ELFLOADER_H
#define PS2LAUNCHER_ELFLOADER_H

/* Reads and parses an ELF at `path` via fileXio and loads its PT_LOAD
 * segments to their vaddrs, then ExecPS2's into it.
 *
 * fileXio, not any of PS2SDK's "give it a path" loaders, because none of
 * LoadELFFromFile() (elf-loader-nocolour: internal file_exists() uses
 * POSIX stat()), elf-loader2's compat layer (uses stdio fopen()), or
 * SifLoadElf() (PCSX2 never attempts a "loadelf:" resolution for mc0:/
 * mass: - it only understands a hardcoded set of device prefixes) can
 * resolve iomanX-registered devices (mc0:/mc1:/mass:) in this ps2sdk +
 * PCSX2 environment - confirmed during the M4 chain-load spike. fileXio's
 * own calls are the one thing proven to work reliably against them.
 *
 * When reset_iop is set, resets and re-syncs the IOP before jumping
 * (SifIopReset+SifIopSync) - matching wLaunchELF's rule: only launches
 * that need a clean IOP (e.g. HDD-launched titles) should request this,
 * since it costs time and breaks host:/PS2Link compatibility otherwise.
 *
 * The caller (dashboard loading stage2, or stage2 loading the real
 * target) must itself be linked at a base address that doesn't overlap
 * wherever `path`'s segments will land - loading an ELF at an address
 * the *caller* is still running from corrupts the caller mid-load. This
 * function does not check for that; get the linkfile right instead (see
 * src/ee/stage2/linkfile).
 *
 * Returns a negative error code if the ELF couldn't be opened, read, or
 * validated. Does not return on success. */
int elfLoadAndExec(const char *path, int reset_iop, int argc, char *argv[]);

#endif
