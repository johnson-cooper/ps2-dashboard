#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#define LOG_MAX_LINES 32
#define LOG_LINE_LEN 72

static char lines[LOG_MAX_LINES][LOG_LINE_LEN];
static int lineCount = 0;    /* number of valid lines, up to LOG_MAX_LINES */
static int nextSlot = 0;     /* ring position the next line gets written to */
static int debugEnabled = 0; /* gates the disk flush below, not the in-memory ring */

void logInit(void)
{
    lineCount = 0;
    nextSlot = 0;
    debugEnabled = 0;
}

/* The in-memory ring buffer is always kept up to date regardless of this
 * flag, so the debug overlay (toggled independently) always has
 * something to show the moment it's opened - only the mc0:/launcher.log
 * *disk write* is gated. Most launches/boots are ordinary use, not a
 * debugging session, and writing to MC on every single device probe and
 * launch attempt for a log nobody's looking at is real, avoidable MC
 * wear (same reasoning already applied to metadata writes in M9). Sticky
 * once set - opening the overlay once starts logging to disk for the
 * rest of the session, even if it's closed again right after, so a
 * crash shortly after checking it once still gets captured. */
void logSetDebugEnabled(int enabled)
{
    debugEnabled = enabled;
}

/* Whole-buffer overwrite on every call, not an append - simplest way to
 * keep the on-disk file capped at LOG_MAX_LINES (an ever-growing append
 * log would eventually exhaust MC space, exactly the MC-wear/capacity
 * concern already flagged for metadata writes in M9). Log events are
 * inherently infrequent (device probes, launches, failures - never a
 * per-frame hot path), so flushing on every call is cheap in practice,
 * and this is the only way to guarantee a line survives a crash right
 * after it's logged. */
static void flushToDisk(void)
{
    int fd = fileXioOpen("mc0:/launcher.log", FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd < 0)
        return;

    int start = (lineCount < LOG_MAX_LINES) ? 0 : nextSlot;
    int i;
    for (i = 0; i < lineCount; i++) {
        int idx = (start + i) % LOG_MAX_LINES;
        int len = strlen(lines[idx]);
        fileXioWrite(fd, lines[idx], len);
        fileXioWrite(fd, "\n", 1);
    }
    fileXioClose(fd);
}

void logMsg(const char *fmt, ...)
{
    char *dst = lines[nextSlot];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, LOG_LINE_LEN, fmt, ap);
    va_end(ap);

    nextSlot = (nextSlot + 1) % LOG_MAX_LINES;
    if (lineCount < LOG_MAX_LINES)
        lineCount++;

    if (debugEnabled)
        flushToDisk();
}

int logLineCount(void)
{
    return lineCount;
}

const char *logLine(int i)
{
    if (i < 0 || i >= lineCount)
        return NULL;
    int start = (lineCount < LOG_MAX_LINES) ? 0 : nextSlot;
    int idx = (start + i) % LOG_MAX_LINES;
    return lines[idx];
}
