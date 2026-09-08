#ifndef PS2LAUNCHER_LOG_H
#define PS2LAUNCHER_LOG_H

/* M13 (plan section 11's own explicit task, "ring-buffer/file logger"):
 * a minimal, always-on logger. Every debugging session this project has
 * gone through relied on the PCSX2 debugger (live memory reads, fixed-
 * address dumps) - none of that exists on real hardware. This is the
 * only diagnostic record a real deployment will have, so it's meant to
 * capture the same class of events (device probe results, launch
 * attempts/failures, corruption detection) that got hand-instrumented
 * with temporary debug buffers throughout this project's own PCSX2
 * debugging sessions. */

void logInit(void);

/* Gates the mc0:/launcher.log disk flush (not the in-memory ring, which
 * is always kept current) - off by default, since most launches are
 * ordinary use, not a debugging session, and writing to MC on every
 * device probe/launch attempt for a log nobody's checking is avoidable
 * MC wear. Sticky: pass 1 once (e.g. the first time the debug overlay is
 * opened) to start flushing to disk for the rest of the session, even if
 * later disabled/closed again. */
void logSetDebugEnabled(int enabled);

/* Appends one line (printf-style, truncated to fit) to the in-memory
 * ring buffer, and flushes the whole buffer to mc0:/launcher.log if
 * debug logging has been enabled. A write failure here is itself
 * silently ignored - a broken logger must never break the dashboard it's
 * trying to help debug. */
void logMsg(const char *fmt, ...);

/* Returns the current number of buffered lines and a pointer to line
 * index i (0 = oldest currently held), for the debug overlay to render.
 * NULL if i is out of range. */
int logLineCount(void);
const char *logLine(int i);

#endif
