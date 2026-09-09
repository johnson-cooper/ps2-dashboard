#ifndef PS2LAUNCHER_APP_ENTRY_H
#define PS2LAUNCHER_APP_ENTRY_H

/* Shared "discovered application" record (M15 GUI redesign). Extends the
 * plain path-only AppEntry main.c used through M14 with the fields the
 * new Home/Library screens need (a display title, the source device, and
 * an optional PS2-native icon reference) - see gfx/ps2_icon.h for how the
 * icon itself is discovered/decoded and gfx/icon_cache.h for how it's
 * cached. Kept as plain fixed-size fields (no pointers/allocation) so
 * entries[] in main.c can stay a flat, bounded static array exactly like
 * before, just with more fields per element. */

#define APP_ENTRY_PATH_MAX 256
#define APP_ENTRY_TITLE_MAX 64

typedef enum {
    DEVICE_UNKNOWN = 0,
    DEVICE_MC0,
    DEVICE_MC1,
    DEVICE_MASS,
    DEVICE_CDROM,
    DEVICE_HDD,
    DEVICE_NETWORK,
    DEVICE_SMB
} DeviceKind;

/* Icon resolution (icon.sys discovery -> .ico parse -> GS texture) is
 * always attempted lazily, on demand, from the Library screen - never at
 * scan time. rescanEntries() runs on every boot and on every periodic
 * removable-media recheck (M13) and must stay cheap; a synchronous
 * fileXio+decode per entry there would reintroduce exactly the kind of
 * per-rescan stutter the M13 quick-rescan split was designed to avoid.
 * See gfx/icon_cache.c for the actual one-decode-per-frame throttling. */
typedef enum {
    ICON_NOT_REQUESTED = 0,
    ICON_PENDING,
    ICON_LOADED,
    ICON_MISSING, /* no icon.sys, or icon.sys named no usable icon - not an error */
    ICON_INVALID  /* icon.sys or its referenced .ico exists but is malformed/unsupported */
} IconLoadState;

typedef struct {
    char path[APP_ENTRY_PATH_MAX];
    char title[APP_ENTRY_TITLE_MAX]; /* filename-derived by default; overwritten if icon.sys yields a sane title (plan section 13) */
    DeviceKind device;

    IconLoadState iconState;
    int iconSlot; /* meaningful only once iconState == ICON_LOADED; index into the shared IconCache atlas (gfx/icon_cache.h) */
} AppEntry;

/* Short, human-readable label for a device kind - used by the Library's
 * per-tile detail panel and its device category filter (plan sections
 * 2/16). Never NULL. */
const char *deviceKindName(DeviceKind kind);

#endif
