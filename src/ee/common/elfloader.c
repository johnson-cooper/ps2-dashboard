#include "elfloader.h"

#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <libcdvd.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

typedef struct {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int version;
    unsigned int entry;
    unsigned int phoff;
    unsigned int shoff;
    unsigned int flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} elf32_ehdr_t;

typedef struct {
    unsigned int type;
    unsigned int offset;
    unsigned int vaddr;
    unsigned int paddr;
    unsigned int filesz;
    unsigned int memsz;
    unsigned int flags;
    unsigned int align;
} elf32_phdr_t;

#define ELF_MAGIC 0x464c457f
#define PT_LOAD 1
#define MAX_PHDRS 16

/* cdrom0: targets can't use fileXio - confirmed during the M11 disc-boot
 * work that fileXioOpen()/fileXioDopen()+fileXioDread() fail outright
 * against cdrom0: even though fileXioGetStat() on the root succeeds
 * (cdvdfsv's fileXio compatibility layer doesn't cover real file lookup
 * the way it does for MC/USB's FAT-style drivers). Real disc-launcher
 * homebrew (wLaunchELF/OPL) reads disc files via libcdvd's own
 * sceCdSearchFile()+sceCdRead() instead - which only understands whole
 * 2048-byte sectors at an absolute LSN, not arbitrary byte offsets, so
 * unlike the fileXio path's incremental seek+read, the whole file is
 * read into one buffer up front and the ELF header/program headers/
 * segments are all parsed directly out of that buffer. */
static int isCdrom0Path(const char *path)
{
    return strncmp(path, "cdrom0:", 7) == 0;
}

static unsigned char *readWholeDiscFile(const char *path, unsigned int *outSize)
{
    /* path is "cdrom0:/NAME;1" - sceCdSearchFile wants "\NAME;1". */
    const char *name = path + 7;
    if (*name == '/')
        name++;

    char sceName[68];
    sceName[0] = '\\';
    int i;
    for (i = 0; name[i] && i < (int)sizeof(sceName) - 2; i++)
        sceName[i + 1] = name[i];
    sceName[i + 1] = '\0';

    sceCdlFILE file;
    if (!sceCdSearchFile(&file, sceName))
        return NULL;

    unsigned int sectors = (file.size + 2047) / 2048;
    if (sectors == 0)
        sectors = 1;

    unsigned char *buf = (unsigned char *)memalign(64, sectors * 2048);
    if (!buf)
        return NULL;

    static sceCdRMode mode;
    mode.trycount = 0;
    mode.spindlctrl = 0;
    mode.datapattern = SCECdSecS2048;
    mode.pad = 0;

    if (!sceCdRead(file.lsn, sectors, buf, &mode)) {
        free(buf);
        return NULL;
    }
    sceCdSync(0); /* sceCdRead is non-blocking - this is the documented way to wait for it */

    *outSize = file.size;
    return buf;
}

static int elfLoadAndExecFromDisc(const char *path, int reset_iop, int argc, char *argv[])
{
    unsigned int fileSize;
    unsigned char *buf = readWholeDiscFile(path, &fileSize);
    if (!buf)
        return -1;

    if (fileSize < sizeof(elf32_ehdr_t)) {
        free(buf);
        return -1;
    }

    elf32_ehdr_t *eh = (elf32_ehdr_t *)buf;
    if (*(unsigned int *)eh->ident != ELF_MAGIC || eh->phnum > MAX_PHDRS) {
        free(buf);
        return -2;
    }

    elf32_phdr_t *phdrs = (elf32_phdr_t *)(buf + eh->phoff);

    int i;
    for (i = 0; i < eh->phnum; i++) {
        if (phdrs[i].type != PT_LOAD)
            continue;

        memcpy((void *)phdrs[i].vaddr, buf + phdrs[i].offset, phdrs[i].filesz);
        if (phdrs[i].memsz > phdrs[i].filesz)
            memset((void *)(phdrs[i].vaddr + phdrs[i].filesz), 0,
                   phdrs[i].memsz - phdrs[i].filesz);
    }

    unsigned int entry = eh->entry;
    free(buf);

    if (reset_iop) {
        while (!SifIopReset("", 0)) {
        }
        while (!SifIopSync()) {
        }
    }

    FlushCache(0);
    FlushCache(2);

    ExecPS2((void *)entry, NULL, argc, argv);

    /* Unreachable on success. */
    return -4;
}

int elfLoadAndExec(const char *path, int reset_iop, int argc, char *argv[])
{
    if (isCdrom0Path(path))
        return elfLoadAndExecFromDisc(path, reset_iop, argc, argv);

    int fd = fileXioOpen(path, FIO_O_RDONLY, 0666);
    if (fd < 0)
        return fd;

    elf32_ehdr_t eh;
    if (fileXioRead(fd, &eh, sizeof(eh)) != (int)sizeof(eh)) {
        fileXioClose(fd);
        return -1;
    }

    if (*(unsigned int *)eh.ident != ELF_MAGIC || eh.phnum > MAX_PHDRS) {
        fileXioClose(fd);
        return -2;
    }

    static elf32_phdr_t phdrs[MAX_PHDRS];
    int phSize = eh.phnum * eh.phentsize;

    fileXioLseek(fd, eh.phoff, FIO_SEEK_SET);
    if (fileXioRead(fd, phdrs, phSize) != phSize) {
        fileXioClose(fd);
        return -3;
    }

    int i;
    for (i = 0; i < eh.phnum; i++) {
        if (phdrs[i].type != PT_LOAD)
            continue;

        fileXioLseek(fd, phdrs[i].offset, FIO_SEEK_SET);
        fileXioRead(fd, (void *)phdrs[i].vaddr, phdrs[i].filesz);

        if (phdrs[i].memsz > phdrs[i].filesz)
            memset((void *)(phdrs[i].vaddr + phdrs[i].filesz), 0,
                   phdrs[i].memsz - phdrs[i].filesz);
    }
    fileXioClose(fd);

    if (reset_iop) {
        while (!SifIopReset("", 0)) {
        }
        while (!SifIopSync()) {
        }
    }

    FlushCache(0);
    FlushCache(2);

    ExecPS2((void *)eh.entry, NULL, argc, argv);

    /* Unreachable on success. */
    return -4;
}
