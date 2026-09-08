#include "elfloader.h"

#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <string.h>

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

int elfLoadAndExec(const char *path, int reset_iop, int argc, char *argv[])
{
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
