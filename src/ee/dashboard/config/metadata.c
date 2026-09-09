#include "metadata.h"
#include "../log/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <iox_stat.h>

#define METADATA_MAX_PATH 300
#define METADATA_MAX_CONTENT 256

/* Standard reflected CRC-32 (poly 0xEDB88320), bitwise rather than
 * table-driven - these files are a handful of bytes, so the simpler,
 * smaller implementation is the right tradeoff over a 1KB lookup table
 * for a handful of extra cycles per save. */
static unsigned int crc32(const unsigned char *data, int len)
{
    unsigned int crc = 0xFFFFFFFF;
    int i, j;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFF;
}

static void buildPath(char *out, const char *elfPath, const char *suffix)
{
    int i = 0;
    while (elfPath[i]) {
        out[i] = elfPath[i];
        i++;
    }
    int j = 0;
    while (suffix[j])
        out[i++] = suffix[j++];
    out[i] = '\0';
}

static int readWholeFile(const char *path, char *buf, int bufSize)
{
    int fd = fileXioOpen(path, FIO_O_RDONLY, 0666);
    if (fd < 0)
        return -1;
    int n = fileXioRead(fd, buf, bufSize - 1);
    fileXioClose(fd);
    if (n < 0)
        return -1;
    buf[n] = '\0';
    return n;
}

void metadataLoad(const char *elfPath, AppMetadata *out)
{
    out->launchCount = 0;
    out->favorite = 0;
    out->lastLaunchOrder = 0;

    char cfgPath[METADATA_MAX_PATH];
    buildPath(cfgPath, elfPath, ".cfg");

    char content[METADATA_MAX_CONTENT];
    int len = readWholeFile(cfgPath, content, sizeof(content));
    if (len < 0)
        return; /* no metadata yet - defaults are correct */

    /* Find the crc32= line; everything before it (including its
     * preceding newline) is what the CRC was computed over at save
     * time. Missing entirely, or not matching, means corrupt/partial -
     * fall back to defaults rather than trust a half-written file. */
    char *crcLine = strstr(content, "crc32=");
    if (!crcLine)
        return;

    unsigned int storedCrc = (unsigned int)strtoul(crcLine + 6, NULL, 16);
    int dataLen = (int)(crcLine - content);
    unsigned int actualCrc = crc32((const unsigned char *)content, dataLen);
    if (storedCrc != actualCrc) {
        logMsg("metadata corrupt: %s", elfPath);
        return;
    }

    /* Simple line-by-line key=value scan over the verified region. */
    char verified[METADATA_MAX_CONTENT];
    memcpy(verified, content, dataLen);
    verified[dataLen] = '\0';

    char *line = strtok(verified, "\n");
    while (line) {
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            const char *key = line;
            const char *value = eq + 1;
            if (strcmp(key, "launch_count") == 0)
                out->launchCount = atoi(value);
            else if (strcmp(key, "favorite") == 0)
                out->favorite = atoi(value);
            else if (strcmp(key, "last_launch_order") == 0)
                out->lastLaunchOrder = (unsigned int)strtoul(value, NULL, 10);
        }
        line = strtok(NULL, "\n");
    }
}

int metadataSave(const char *elfPath, const AppMetadata *meta)
{
    char cfgPath[METADATA_MAX_PATH];
    char tmpPath[METADATA_MAX_PATH];
    buildPath(cfgPath, elfPath, ".cfg");
    buildPath(tmpPath, elfPath, ".cfg.tmp");

    char content[METADATA_MAX_CONTENT];
    int dataLen = sprintf(content, "launch_count=%d\nfavorite=%d\nlast_launch_order=%u\n", meta->launchCount,
                           meta->favorite, meta->lastLaunchOrder);

    unsigned int crc = crc32((const unsigned char *)content, dataLen);
    int totalLen = dataLen + sprintf(content + dataLen, "crc32=%08X\n", crc);

    int fd = fileXioOpen(tmpPath, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd < 0)
        return -1;
    int written = fileXioWrite(fd, content, totalLen);
    fileXioClose(fd);
    if (written != totalLen) {
        fileXioRemove(tmpPath);
        return -1;
    }

    /* fileXioRename replaces an existing destination atomically at the
     * directory-entry level on FAT/PFS (mass:/hdd0:) - a reader can never
     * observe a half-written cfgPath there, only the old content or the
     * new. mcman has no rename primitive at all, though (its file API is
     * limited to open/close/read/write/erase/getdir - confirmed the real
     * reason wLaunchELF itself implements MC "rename" as a manual
     * copy+delete): fileXioRename against mc0:/mc1: always fails, which
     * used to make metadataSave silently discard every write and return
     * failure. Falling back to a direct overwrite of cfgPath keeps MC
     * working at all, at the cost of the rename step's atomicity there -
     * still safe against corruption *detection* (the CRC trailer catches
     * a torn write on the next load) even though a power-loss mid-write
     * can now lose data on MC specifically, same as any other MC write. */
    if (fileXioRename(tmpPath, cfgPath) < 0) {
        fd = fileXioOpen(cfgPath, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
        if (fd < 0) {
            fileXioRemove(tmpPath);
            return -1;
        }
        written = fileXioWrite(fd, content, totalLen);
        fileXioClose(fd);
        fileXioRemove(tmpPath);
        if (written != totalLen)
            return -1;
    }

    return 0;
}
