#ifndef PS2LAUNCHER_DASHBOARD_UI_H
#define PS2LAUNCHER_DASHBOARD_UI_H

#include <gsKit.h>

#include "../app/app_entry.h"
#include "../config/metadata.h"
#include "../device/device_mgr.h"
#include "../gfx/bitmap_font.h"
#include "../gfx/icon_cache.h"
#include "../theme/theme.h"
#include "background.h"

/* The dashboard's screen framework (plan sections 2/5/21): three
 * PSBBN-style screens (Home/Library/System) sharing one navigation
 * model, drawn over the animated background (ui/background.h). This
 * module owns screen state, focus/scroll/filter state, and rendering -
 * it deliberately does NOT own device scanning, ELF launching, or
 * metadata persistence (main.c keeps calling elfLoadAndExec()/
 * metadataSave() itself, unchanged): dashboardUiHandleInput() only
 * reports back *what* the user asked for via a UiAction, so the
 * existing, already-tested launch/favorite/poweroff code paths in
 * main.c stay exactly as they were (plan section 14). */

typedef enum { SCREEN_HOME = 0, SCREEN_LIBRARY, SCREEN_SYSTEM } DashboardScreen;

typedef enum {
    LIB_FILTER_ALL = 0,
    LIB_FILTER_FAVORITES,
    LIB_FILTER_MC,
    LIB_FILTER_USB,
    LIB_FILTER_HDD,
    LIB_FILTER_NETWORK,
    LIB_FILTER_DISC,
    LIB_FILTER_COUNT
} LibraryFilter;

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_LAUNCH,
    UI_ACTION_TOGGLE_FAVORITE,
    UI_ACTION_POWEROFF
} UiActionKind;

typedef struct {
    UiActionKind kind;
    int entryIndex; /* valid for LAUNCH/TOGGLE_FAVORITE - an index into the caller's entries[]/metas[] arrays */
} UiAction;

#define LIBRARY_COLS 5
#define SYSTEM_ROW_COUNT 8

typedef struct {
    DashboardScreen screen;

    int homeIndex;
    int libraryFocus; /* index into the *filtered* list, not entries[] directly */
    int libraryScrollRow;
    LibraryFilter libraryFilter;
    int systemIndex;

    int currentTheme;
    int themeCount;

    Background background;
} DashboardUi;

void dashboardUiInit(DashboardUi *ui, int themeCount);

/* Handles one frame's worth of already edge-detected pad input (`edge`,
 * same convention as main.c's own pad-read loop) and returns at most one
 * action for the caller to actually perform. entries/metas/entryCount
 * are the same arrays main.c has always owned; families/familyCount let
 * Home reach "Launch Disc" and System reach live device status. */
UiAction dashboardUiHandleInput(DashboardUi *ui, u32 edge, AppEntry *entries, AppMetadata *metas, int entryCount,
                                 DeviceFamily *families, int familyCount);

/* Draws the current screen over the animated background, requests
 * icons for any newly-visible Library tiles, and processes at most one
 * pending icon decode (gfx/icon_cache.h) - callers don't need to
 * separately remember to pace icon decoding themselves. */
void dashboardUiDraw(DashboardUi *ui, GSGLOBAL *gsGlobal, BitmapFont *font, int fontOk, Theme *themes,
                      AppEntry *entries, AppMetadata *metas, int entryCount, DeviceFamily *families, int familyCount,
                      IconCache *iconCache, const char *statusLine);

#endif
