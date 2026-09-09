#include "dashboard_ui.h"
#include "layout.h"
#include "../audio/audio.h"

#include <libpad.h>
#include <string.h>
#include <stdio.h>

/* PSBBN-inspired palette (plan section 1), translated directly from the
 * supplied HTML reference's CSS custom properties: --cyan:#8fa6b8,
 * --amber:#b8a06a, --text-dim:#54687a, plus plain white for the
 * selected/focused row. Intentionally independent of the still-fully-
 * functional theme/theme.c system (System > Theme still cycles the
 * user's bg/tile/focus/label colors, which continue to drive the
 * Library grid's own tile fills) - this is the dashboard's own fixed
 * chrome, matching the requested visual identity regardless of theme. */
#define COLOR_AMBER GS_SETREG_RGBAQ(184, 160, 106, 0x80, 0x00)
#define COLOR_CYAN GS_SETREG_RGBAQ(143, 166, 184, 0x80, 0x00)
#define COLOR_DIM GS_SETREG_RGBAQ(84, 104, 122, 0x80, 0x00)
#define COLOR_WHITE GS_SETREG_RGBAQ(255, 255, 255, 0x80, 0x00)
#define COLOR_ROW_HILITE GS_SETREG_RGBAQ(255, 255, 255, 0x14, 0x00)
#define COLOR_BORDER GS_SETREG_RGBAQ(255, 255, 255, 0x50, 0x00)

#define DASHBOARD_UI_MAX_FILTERED 64
#define HOME_ITEM_COUNT 6
#define LIBRARY_VISIBLE_ROWS 3

typedef struct {
    const char *label;
    char detail[64];
    int enabled;
    int actionEntryIndex; /* -1 if this item doesn't directly launch an app */
} HomeItem;

static void drawIconAt(GSGLOBAL *gs, IconCache *iconCache, int slot, float x, float y, float w, float h)
{
    if (!iconCache->ready) /* VRAM allocation failed at boot - degrade to no icon at all, never draw an unbound texture */
        return;

    float u0, v0, u1, v1;
    iconCacheSlotUV(iconCache, slot, &u0, &v0, &u1, &v1);

    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_set_test(gs, GS_ATEST_OFF);
    gsKit_set_test(gs, GS_ZTEST_OFF);
    gsKit_prim_sprite_texture(gs, &iconCache->texture, x, y, u0, v0, x + w, y + h, u1, v1, 1,
                               GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00));
    gsKit_set_test(gs, GS_ZTEST_ON);
    gsKit_set_test(gs, GS_ATEST_ON);
    gsKit_set_primalpha(gs, GS_BLEND_BACK2FRONT, 0);
}

static int filterMatches(LibraryFilter filter, const AppEntry *e, const AppMetadata *m)
{
    switch (filter) {
    case LIB_FILTER_FAVORITES:
        return m->favorite;
    case LIB_FILTER_MC:
        return e->device == DEVICE_MC0 || e->device == DEVICE_MC1;
    case LIB_FILTER_USB:
        return e->device == DEVICE_MASS;
    case LIB_FILTER_HDD:
        return e->device == DEVICE_HDD;
    case LIB_FILTER_NETWORK:
        return e->device == DEVICE_NETWORK || e->device == DEVICE_SMB;
    case LIB_FILTER_DISC:
        return e->device == DEVICE_CDROM;
    case LIB_FILTER_ALL:
    default:
        return 1;
    }
}

static int buildFilteredList(LibraryFilter filter, AppEntry *entries, AppMetadata *metas, int entryCount, int *outIdx,
                              int maxOut)
{
    int n = 0, i;
    for (i = 0; i < entryCount && n < maxOut; i++)
        if (filterMatches(filter, &entries[i], &metas[i]))
            outIdx[n++] = i;
    return n;
}

static int familyAvailable(DeviceFamily *families, int familyCount, const char *name)
{
    int i;
    for (i = 0; i < familyCount; i++)
        if (strcmp(families[i].name, name) == 0)
            return families[i].available;
    return 0;
}

static void buildHomeItems(HomeItem *items, AppEntry *entries, AppMetadata *metas, int entryCount,
                            DeviceFamily *families, int familyCount)
{
    int i;
    int mruIndex = -1;
    unsigned int mruOrder = 0;
    int discIndex = -1;
    for (i = 0; i < entryCount; i++) {
        if (metas[i].lastLaunchOrder > mruOrder) {
            mruOrder = metas[i].lastLaunchOrder;
            mruIndex = i;
        }
        if (entries[i].device == DEVICE_CDROM && discIndex < 0)
            discIndex = i;
    }

    items[0].label = "Continue";
    items[0].actionEntryIndex = mruIndex;
    items[0].enabled = (mruIndex >= 0);
    if (mruIndex >= 0)
        layoutTruncateToChars(items[0].detail, sizeof(items[0].detail), entries[mruIndex].title, 40);
    else
        strcpy(items[0].detail, "No recent apps");

    items[1].label = "Library";
    items[1].actionEntryIndex = -1;
    items[1].enabled = 1;
    sprintf(items[1].detail, "%d application%s", entryCount, entryCount == 1 ? "" : "s");

    items[2].label = "Launch Disc";
    items[2].actionEntryIndex = discIndex;
    items[2].enabled = (discIndex >= 0);
    strcpy(items[2].detail, discIndex >= 0 ? entries[discIndex].title : "No disc inserted");

    items[3].label = "Network";
    items[3].actionEntryIndex = -1;
    items[3].enabled = 1;
    strcpy(items[3].detail, familyAvailable(families, familyCount, "network") ? "Connected" : "Not connected");

    items[4].label = "System Settings";
    items[4].actionEntryIndex = -1;
    items[4].enabled = 1;
    items[4].detail[0] = '\0';

    items[5].label = "Save Manager";
    items[5].actionEntryIndex = -1;
    items[5].enabled = 0;
    strcpy(items[5].detail, "Not yet available");
}

void dashboardUiInit(DashboardUi *ui, int themeCount)
{
    memset(ui, 0, sizeof(*ui));
    ui->screen = SCREEN_HOME;
    ui->themeCount = themeCount;
    backgroundInit(&ui->background);
}

UiAction dashboardUiHandleInput(DashboardUi *ui, u32 edge, AppEntry *entries, AppMetadata *metas, int entryCount,
                                 DeviceFamily *families, int familyCount)
{
    UiAction action;
    action.kind = UI_ACTION_NONE;
    action.entryIndex = -1;

    if ((edge & PAD_CIRCLE) && ui->screen != SCREEN_HOME) {
        ui->screen = SCREEN_HOME;
        return action;
    }

    if (ui->screen == SCREEN_HOME) {
        HomeItem items[HOME_ITEM_COUNT];
        buildHomeItems(items, entries, metas, entryCount, families, familyCount);

        int before = ui->homeIndex;
        if (edge & PAD_DOWN)
            ui->homeIndex = (ui->homeIndex + 1) % HOME_ITEM_COUNT;
        if (edge & PAD_UP)
            ui->homeIndex = (ui->homeIndex - 1 + HOME_ITEM_COUNT) % HOME_ITEM_COUNT;
        if (ui->homeIndex != before)
            audioPlayBlip();

        if ((edge & PAD_CROSS) && items[ui->homeIndex].enabled) {
            switch (ui->homeIndex) {
            case 0:
            case 2:
                action.kind = UI_ACTION_LAUNCH;
                action.entryIndex = items[ui->homeIndex].actionEntryIndex;
                break;
            case 1:
                ui->screen = SCREEN_LIBRARY;
                ui->libraryFocus = 0;
                ui->libraryScrollRow = 0;
                ui->libraryFilter = LIB_FILTER_ALL;
                break;
            case 3:
                ui->screen = SCREEN_SYSTEM;
                ui->systemIndex = 1; /* Network row */
                break;
            case 4:
                ui->screen = SCREEN_SYSTEM;
                ui->systemIndex = 0;
                break;
            default:
                break;
            }
        }
        return action;
    }

    if (ui->screen == SCREEN_LIBRARY) {
        int filtered[DASHBOARD_UI_MAX_FILTERED];
        int n = buildFilteredList(ui->libraryFilter, entries, metas, entryCount, filtered, DASHBOARD_UI_MAX_FILTERED);
        if (ui->libraryFocus >= n)
            ui->libraryFocus = (n > 0) ? n - 1 : 0;

        if (edge & PAD_L1) {
            ui->libraryFilter = (ui->libraryFilter - 1 + LIB_FILTER_COUNT) % LIB_FILTER_COUNT;
            ui->libraryFocus = 0;
            ui->libraryScrollRow = 0;
        }
        if (edge & PAD_R1) {
            ui->libraryFilter = (ui->libraryFilter + 1) % LIB_FILTER_COUNT;
            ui->libraryFocus = 0;
            ui->libraryScrollRow = 0;
        }

        if (n > 0) {
            int before = ui->libraryFocus;
            if (edge & PAD_RIGHT)
                ui->libraryFocus = (ui->libraryFocus + 1) % n;
            if (edge & PAD_LEFT)
                ui->libraryFocus = (ui->libraryFocus - 1 + n) % n;
            if (edge & PAD_DOWN) {
                int nx = ui->libraryFocus + LIBRARY_COLS;
                if (nx < n)
                    ui->libraryFocus = nx;
            }
            if (edge & PAD_UP) {
                int nx = ui->libraryFocus - LIBRARY_COLS;
                if (nx >= 0)
                    ui->libraryFocus = nx;
            }
            if (ui->libraryFocus != before)
                audioPlayBlip();

            if (edge & PAD_TRIANGLE) {
                action.kind = UI_ACTION_TOGGLE_FAVORITE;
                action.entryIndex = filtered[ui->libraryFocus];
            }
            if (edge & PAD_CROSS) {
                action.kind = UI_ACTION_LAUNCH;
                action.entryIndex = filtered[ui->libraryFocus];
            }
        }
        return action;
    }

    /* SCREEN_SYSTEM */
    {
        int before = ui->systemIndex;
        if (edge & PAD_DOWN)
            ui->systemIndex = (ui->systemIndex + 1) % SYSTEM_ROW_COUNT;
        if (edge & PAD_UP)
            ui->systemIndex = (ui->systemIndex - 1 + SYSTEM_ROW_COUNT) % SYSTEM_ROW_COUNT;
        if (ui->systemIndex != before)
            audioPlayBlip();
    }

    if (ui->systemIndex == 3 && ui->themeCount > 0) { /* Theme row */
        if (edge & PAD_LEFT)
            ui->currentTheme = (ui->currentTheme - 1 + ui->themeCount) % ui->themeCount;
        if ((edge & PAD_RIGHT) || (edge & PAD_CROSS))
            ui->currentTheme = (ui->currentTheme + 1) % ui->themeCount;
    }
    if (ui->systemIndex == 7 && (edge & PAD_CROSS)) /* Power row */
        action.kind = UI_ACTION_POWEROFF;

    return action;
}

static void drawHome(DashboardUi *ui, GSGLOBAL *gs, BitmapFont *font, int fontOk, LayoutRect *rect,
                      AppEntry *entries, AppMetadata *metas, int entryCount, DeviceFamily *families, int familyCount,
                      IconCache *iconCache)
{
    HomeItem items[HOME_ITEM_COUNT];
    buildHomeItems(items, entries, metas, entryCount, families, familyCount);

    float x = rect->marginX;
    float y = rect->marginY;

    if (fontOk)
        bitmapFontPrint(gs, font, x, y, COLOR_AMBER, "Main menu");
    y += 28.0f;

    int i;
    for (i = 0; i < HOME_ITEM_COUNT; i++) {
        int focused = (i == ui->homeIndex);
        float rowH = 22.0f;

        if (focused)
            layoutFillBlended(gs, x - 4.0f, y - 2.0f, x + rect->safeW * 0.34f, y + rowH - 4.0f, 1, COLOR_ROW_HILITE);

        u64 color = focused ? COLOR_WHITE : (items[i].enabled ? COLOR_CYAN : COLOR_DIM);
        if (fontOk) {
            bitmapFontPrint(gs, font, x, y, color, items[i].label);
            if (items[i].detail[0]) {
                /* items[i].detail can be an arbitrary app title (up to 40
                 * chars, from buildHomeItems) - at this column's real
                 * pixel budget (safe area's right edge minus the column's
                 * own x offset), not every 40-char title fits under the
                 * new proportional font, so re-truncate to the real
                 * measured width before drawing. */
                char detail[64];
                layoutTruncateToWidth(detail, sizeof(detail), items[i].detail, font, rect->safeW - 220.0f);
                bitmapFontPrint(gs, font, x + 220.0f, y, COLOR_DIM, detail);
            }
        }
        y += rowH;
    }

    /* Quick Resume tray (plan section 2): up to 4 most-recently-launched
     * apps, shown with their real icons where available. Purely
     * informational/visual - not independently focusable, keeping this
     * screen's navigation to a single list dimension. */
    int trayIdx[4];
    int trayCount = 0;
    {
        int used[DASHBOARD_UI_MAX_FILTERED];
        memset(used, 0, sizeof(used));
        int k;
        for (k = 0; k < 4; k++) {
            int best = -1;
            unsigned int bestOrder = 0;
            int j;
            for (j = 0; j < entryCount && j < DASHBOARD_UI_MAX_FILTERED; j++) {
                if (used[j] || metas[j].lastLaunchOrder == 0)
                    continue;
                if (metas[j].lastLaunchOrder > bestOrder) {
                    bestOrder = metas[j].lastLaunchOrder;
                    best = j;
                }
            }
            if (best < 0)
                break;
            used[best] = 1;
            trayIdx[trayCount++] = best;
        }
    }

    if (trayCount > 0) {
        float trayY = rect->safeBottom - 66.0f;
        if (fontOk)
            bitmapFontPrint(gs, font, x, trayY - 16.0f, COLOR_DIM, "QUICK RESUME");

        float trayX = x;
        for (i = 0; i < trayCount; i++) {
            int idx = trayIdx[i];
            iconCacheRequest(iconCache, &entries[idx]);
            int slot = iconCacheSlotForEntry(iconCache, &entries[idx]);
            layoutFillOpaque(gs, trayX, trayY, trayX + 48.0f, trayY + 48.0f, 1, GS_SETREG_RGBAQ(20, 24, 30, 0x00, 0));
            drawIconAt(gs, iconCache, slot, trayX + 4.0f, trayY + 4.0f, 40.0f, 40.0f);
            trayX += 58.0f;
        }
    }
}

static void drawLibrary(DashboardUi *ui, GSGLOBAL *gs, BitmapFont *font, int fontOk, Theme *theme, LayoutRect *rect,
                         AppEntry *entries, AppMetadata *metas, int entryCount, IconCache *iconCache)
{
    int filtered[DASHBOARD_UI_MAX_FILTERED];
    int n = buildFilteredList(ui->libraryFilter, entries, metas, entryCount, filtered, DASHBOARD_UI_MAX_FILTERED);
    if (ui->libraryFocus >= n)
        ui->libraryFocus = (n > 0) ? n - 1 : 0;

    int focusRow = (n > 0) ? ui->libraryFocus / LIBRARY_COLS : 0;
    if (focusRow < ui->libraryScrollRow)
        ui->libraryScrollRow = focusRow;
    if (focusRow >= ui->libraryScrollRow + LIBRARY_VISIBLE_ROWS)
        ui->libraryScrollRow = focusRow - LIBRARY_VISIBLE_ROWS + 1;
    if (ui->libraryScrollRow < 0)
        ui->libraryScrollRow = 0;

    float x0 = rect->marginX;
    float y0 = rect->marginY;

    if (fontOk)
        bitmapFontPrint(gs, font, x0, y0, COLOR_AMBER, "Library");

    static const char *filterNames[LIB_FILTER_COUNT] = { "All", "Favs", "MC", "USB", "HDD", "Net", "Disc" };
    float pillX = x0 + 110.0f;
    int f;
    for (f = 0; f < LIB_FILTER_COUNT; f++) {
        int active = (f == ui->libraryFilter);
        float pw = 46.0f;
        if (active)
            layoutFillBlended(gs, pillX, y0, pillX + pw, y0 + 16.0f, 1, COLOR_ROW_HILITE);
        if (fontOk)
            bitmapFontPrint(gs, font, pillX + 4.0f, y0, active ? COLOR_WHITE : COLOR_DIM, filterNames[f]);
        pillX += pw + 4.0f;
    }

    y0 += 26.0f;
    float cellGap = 14.0f;
    float cellSize = (rect->safeW - (LIBRARY_COLS - 1) * cellGap) / LIBRARY_COLS;
    float iconSize = cellSize * 0.6f;

    int visibleStart = ui->libraryScrollRow * LIBRARY_COLS;
    int visibleEnd = visibleStart + LIBRARY_VISIBLE_ROWS * LIBRARY_COLS;
    if (visibleEnd > n)
        visibleEnd = n;

    int i;
    for (i = visibleStart; i < visibleEnd; i++) {
        int idx = filtered[i];
        int col = (i - visibleStart) % LIBRARY_COLS;
        int row = (i - visibleStart) / LIBRARY_COLS;
        float tx = x0 + col * (cellSize + cellGap);
        float ty = y0 + row * (cellSize + cellGap);

        int focused = (i == ui->libraryFocus);
        layoutFillOpaque(gs, tx, ty, tx + cellSize, ty + cellSize, 1, focused ? theme->focusColor : theme->tileColor);
        if (focused) {
            layoutFillBlended(gs, tx - 2.0f, ty - 2.0f, tx + cellSize + 2.0f, ty, 2, COLOR_BORDER);
            layoutFillBlended(gs, tx - 2.0f, ty + cellSize, tx + cellSize + 2.0f, ty + cellSize + 2.0f, 2,
                               COLOR_BORDER);
            layoutFillBlended(gs, tx - 2.0f, ty, tx, ty + cellSize, 2, COLOR_BORDER);
            layoutFillBlended(gs, tx + cellSize, ty, tx + cellSize + 2.0f, ty + cellSize, 2, COLOR_BORDER);
        }

        iconCacheRequest(iconCache, &entries[idx]);
        int slot = iconCacheSlotForEntry(iconCache, &entries[idx]);
        drawIconAt(gs, iconCache, slot, tx + (cellSize - iconSize) * 0.5f, ty + 6.0f, iconSize, iconSize);

        if (fontOk) {
            char label[24];
            layoutTruncateToWidth(label, sizeof(label), entries[idx].title, font, cellSize - 8.0f);
            bitmapFontPrint(gs, font, tx + 4.0f, ty + cellSize - 18.0f, focused ? COLOR_WHITE : COLOR_CYAN, label);
        }
    }

    /* Focused-tile detail panel (plan section 2: "on selection,
     * optionally expose extra information") - always shown for whichever
     * tile is focused, rather than gated behind another button, to avoid
     * a second, redundant control for the same information. */
    if (n > 0) {
        int idx = filtered[ui->libraryFocus];
        /* entries[idx].path can be up to APP_ENTRY_PATH_MAX (256) bytes -
         * this buffer must comfortably fit that plus every other field
         * sprintf'd in below it, or a long real-world path (plan section
         * 19: "very long paths") would overflow it. The result is then
         * truncated to the safe area's own width before drawing - the
         * bitmap font has no wrapping, so an untruncated long path would
         * simply run off the title-safe area (and off-screen) instead. */
        char full[APP_ENTRY_PATH_MAX + 64];
        sprintf(full, "%s  |  %s  |  %s  |  %d launch%s", entries[idx].path, deviceKindName(entries[idx].device),
                metas[idx].favorite ? "Favorite" : "Not favorited", metas[idx].launchCount,
                metas[idx].launchCount == 1 ? "" : "es");

        char detail[128];
        layoutTruncateToWidth(detail, sizeof(detail), full, font, rect->safeW);
        if (fontOk)
            bitmapFontPrint(gs, font, x0, rect->safeBottom - 18.0f, COLOR_DIM, detail);
    } else if (fontOk) {
        bitmapFontPrint(gs, font, x0, y0, COLOR_DIM, "No applications match this filter.");
    }
}

static void drawSystem(DashboardUi *ui, GSGLOBAL *gs, BitmapFont *font, int fontOk, Theme *themes, LayoutRect *rect,
                        DeviceFamily *families, int familyCount, const char *statusLine)
{
    static const char *labels[SYSTEM_ROW_COUNT] = { "Video",   "Network", "Storage", "Theme",
                                                     "Audio",   "Memory Cards", "Disc", "Power" };

    float x = rect->marginX;
    float y = rect->marginY;

    if (fontOk)
        bitmapFontPrint(gs, font, x, y, COLOR_AMBER, "System settings");
    y += 28.0f;

    int i;
    for (i = 0; i < SYSTEM_ROW_COUNT; i++) {
        int focused = (i == ui->systemIndex);
        float rowH = 22.0f;
        if (focused)
            layoutFillBlended(gs, x - 4.0f, y - 2.0f, x + rect->safeW * 0.34f, y + rowH - 4.0f, 1, COLOR_ROW_HILITE);

        char detail[96];
        detail[0] = '\0';
        switch (i) {
        case 0:
            sprintf(detail, "%dx%d, %s", gs->Width, gs->Height,
                    gs->Interlace == GS_INTERLACED ? "interlaced" : "progressive");
            break;
        case 1:
            strcpy(detail, familyAvailable(families, familyCount, "network") ? "Connected" : "Not connected");
            break;
        case 2:
            sprintf(detail, "USB: %s   HDD: %s", familyAvailable(families, familyCount, "mass") ? "Ready" : "None",
                    familyAvailable(families, familyCount, "hdd0") ? "Ready" : "None");
            break;
        case 3:
            sprintf(detail, "%s", themes[ui->currentTheme].name[0] ? themes[ui->currentTheme].name : "(unnamed)");
            break;
        case 4:
            strcpy(detail, "Not yet configurable");
            break;
        case 5:
            sprintf(detail, "MC0: %s   MC1: %s", familyAvailable(families, familyCount, "mc0") ? "Ready" : "None",
                    familyAvailable(families, familyCount, "mc1") ? "Ready" : "None");
            break;
        case 6:
            layoutTruncateToWidth(detail, sizeof(detail), statusLine, font, rect->safeW - 200.0f);
            break;
        case 7:
            strcpy(detail, "Press CROSS to shut down");
            break;
        default:
            break;
        }

        u64 color = focused ? COLOR_WHITE : COLOR_CYAN;
        if (fontOk) {
            bitmapFontPrint(gs, font, x, y, color, labels[i]);
            bitmapFontPrint(gs, font, x + 200.0f, y, COLOR_DIM, detail);
        }
        y += rowH;
    }
}

void dashboardUiDraw(DashboardUi *ui, GSGLOBAL *gsGlobal, BitmapFont *font, int fontOk, Theme *themes,
                      AppEntry *entries, AppMetadata *metas, int entryCount, DeviceFamily *families, int familyCount,
                      IconCache *iconCache, const char *statusLine)
{
    LayoutRect rect;
    layoutComputeSafeArea(&rect, gsGlobal, LAYOUT_STATUS_LINE_HEIGHT);

    backgroundTick(&ui->background);
    backgroundDraw(&ui->background, gsGlobal);

    iconCacheProcessPending(iconCache);

    switch (ui->screen) {
    case SCREEN_HOME:
        drawHome(ui, gsGlobal, font, fontOk, &rect, entries, metas, entryCount, families, familyCount, iconCache);
        break;
    case SCREEN_LIBRARY:
        drawLibrary(ui, gsGlobal, font, fontOk, &themes[ui->currentTheme], &rect, entries, metas, entryCount,
                    iconCache);
        break;
    case SCREEN_SYSTEM:
        drawSystem(ui, gsGlobal, font, fontOk, themes, &rect, families, familyCount, statusLine);
        break;
    }
}
