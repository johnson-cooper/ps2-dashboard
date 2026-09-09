#include "device_mgr.h"

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <netman.h>
#include <ps2ips.h>
#include <ps2sdkapi.h>
#include <string.h>

#include "smbman.h"
#include "../log/log.h"

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <iox_stat.h>

/* ps2ip_setconfig()/ps2ip_getconfig() aren't declared by ps2ips.h itself
 * (that header only has ps2ip_init()/ps2ip_deinit() - see ps2sdkapi.h's
 * own comment describing ps2ips as "this package's small RPC-client
 * sibling" of the *other*, mutually-exclusive EE-resident lwIP stack
 * package), but they ARE already declared transitively through
 * ps2ips.h's own <sys/socket.h> include (as ps2ip_setconfig/getconfig
 * macros forwarding to libcglue_ps2ip_setconfig/getconfig, using the
 * real t_ip_info type from ps2sdkapi.h) - confirmed by a real build
 * error the first time this tried to hand-declare a locally-defined
 * stand-in struct instead ("conflicting types"). Using the real
 * t_ip_info directly here, matching what's already in scope. */

/* No status logging here (deliberately) - callers that still want a
 * text log (e.g. a future debug-only build) can wrap loadModules() and
 * log SifLoadModule's own return themselves; libdebug's scr_printf and
 * gsKit both drive the GS directly and garble the display when combined
 * in the same program (confirmed in M7 - see plan section 7). Dashboard
 * calls this while gsKit owns the screen, so it must stay silent. */
static int loadModule(const char *path)
{
    return SifLoadModule(path, 0, NULL);
}

/* IOP modules embedded via bin2c'd package artifacts (embedded/ *_irx.c,
 * generated straight from the installed ps2sdk packages' own prebuilt
 * driver binaries - see main.c's own comment for why this replaces
 * loading from a "host:" path, which only resolves under whatever
 * emulator/dev setup happens to map "host:" to this project's own
 * modules/ directory - a real, confirmed boot failure otherwise (a
 * PCSX2 launch config pointing "host:" at the built ELF's own directory
 * instead of the project root left iomanX.irx/fileXio.irx unable to
 * load at all, hanging fileXioInit() before a single frame was drawn). */
static int loadModuleBuf(void *buf, unsigned int size)
{
    return SifExecModuleBuffer(buf, size, 0, NULL, NULL);
}

extern unsigned char mcman_irx[];
extern unsigned int size_mcman_irx;
extern unsigned char mcserv_irx[];
extern unsigned int size_mcserv_irx;
extern unsigned char usbd_irx[];
extern unsigned int size_usbd_irx;
extern unsigned char usbhdfsd_irx[];
extern unsigned int size_usbhdfsd_irx;
extern unsigned char cdvdman_irx[];
extern unsigned int size_cdvdman_irx;
extern unsigned char cdvdfsv_irx[];
extern unsigned int size_cdvdfsv_irx;
extern unsigned char ps2dev9_irx[];
extern unsigned int size_ps2dev9_irx;
extern unsigned char ps2atad_irx[];
extern unsigned int size_ps2atad_irx;
extern unsigned char netman_irx[];
extern unsigned int size_netman_irx;
extern unsigned char smap_irx[];
extern unsigned int size_smap_irx;
extern unsigned char ps2ips_irx[];
extern unsigned int size_ps2ips_irx;
extern unsigned char smbman_irx[];
extern unsigned int size_smbman_irx;

static void busyWait(int iterations)
{
    volatile int i;
    for (i = 0; i < iterations; i++) {
    }
}

/* Generic fileXio-based probe shared by every filesystem-backed family -
 * pre-checking via fileXioGetStat before any dopen/open avoids the
 * deadlock wLaunchELF hit opening an unformatted/absent memory card. */
static int probeFs(DeviceFamily *self)
{
    iox_stat_t st;
    return fileXioGetStat(self->mountPrefix, &st) >= 0;
}

/* --- Memory card (mc0:/mc1:) --------------------------------------- */

static int mcModulesLoaded = 0;

static int loadMC(DeviceFamily *self)
{
    (void)self;
    if (mcModulesLoaded)
        return 0;

    loadModule("rom0:SIO2MAN");
    if (loadModuleBuf(mcman_irx, size_mcman_irx) < 0)
        return -1;
    if (loadModuleBuf(mcserv_irx, size_mcserv_irx) < 0)
        return -1;

    mcModulesLoaded = 1;
    return 0;
}

/* --- USB mass storage (mass:) --------------------------------------- */

static int loadUSB(DeviceFamily *self)
{
    (void)self;
    if (loadModuleBuf(usbd_irx, size_usbd_irx) < 0)
        return -1;
    if (loadModuleBuf(usbhdfsd_irx, size_usbhdfsd_irx) < 0)
        return -1;
    return 0;
}

/* USB enumeration happens asynchronously on the IOP after usbd/usbhdfsd
 * load - the device isn't necessarily visible as "mass:" the instant
 * SifLoadModule returns, so retry for a few seconds before giving up. */
static int probeUSB(DeviceFamily *self)
{
    int i;
    for (i = 0; i < 20; i++) {
        if (probeFs(self))
            return 1;
        busyWait(3000000);
    }
    return 0;
}

/* --- CD/DVD (cdrom0:) ------------------------------------------------
 * cdvdman.irx alone only provides the low-level sceCd* RPC (disc status/
 * type/RTC - what M11's dashboard status line uses directly). It does
 * NOT make cdrom0: browsable through fileXio/iomanX - that needs
 * cdvdfsv.irx, the separate driver providing the actual ISO9660
 * filesystem layer. Without it, fileXioGetStat("cdrom0:/") always fails
 * regardless of what's mounted, so probeFs() below would report cdrom0
 * unavailable even with a real disc inserted - a real bug this project
 * had for the whole of M6-M11, confirmed by cdvdfsv's package.yaml
 * declaring it as its own separate driver artifact, not bundled into
 * cdvdman's. */
static int loadCD(DeviceFamily *self)
{
    (void)self;
    if (loadModuleBuf(cdvdman_irx, size_cdvdman_irx) < 0)
        return -1;
    return loadModuleBuf(cdvdfsv_irx, size_cdvdfsv_irx) < 0 ? -1 : 0;
}

/* --- Internal HDD (hdd0:) --------------------------------------------
 * dev9 is shared with the network family below - guarded so it's only
 * ever loaded once regardless of which family reaches it first. */

static int dev9Loaded = 0;

static int loadDev9(void)
{
    if (dev9Loaded)
        return 0;
    if (loadModuleBuf(ps2dev9_irx, size_ps2dev9_irx) < 0)
        return -1;
    dev9Loaded = 1;
    return 0;
}

static int loadHDD(DeviceFamily *self)
{
    (void)self;
    /* dev9 -> atad only. Originally this also loaded ps2hdd.irx (the APA
     * partition driver) and, before that, ps2fs.irx on top of it -
     * removing ps2fs.irx alone did NOT fix a real, confirmed stall
     * booting with a freshly-enabled, unformatted/unpartitioned virtual
     * HDD in PCSX2 (black screen for a long time before eventually
     * continuing). Re-reading the IOP log more carefully: ps2hdd.irx's
     * own startup banner ("PS2 APA Driver v2.5") prints, but unlike
     * every other module load, no "loadmodule: id NN, ret Y" line ever
     * follows it in the captured log - meaning ps2hdd.irx's own SifLoad-
     * Module call hadn't returned yet. Its name says why: it's APA
     * (partition-table) aware itself, independent of ps2fs.irx layered
     * on top, so a blank/unpartitioned drive apparently makes ITS OWN
     * init slow too. This happens before gsKit even initializes, so
     * there's no way to recover or bound it from this side - only
     * ps2atad.irx (raw ATA/IDE presence, no partition-table parsing at
     * all) is loaded here now. Real PFS/HDD browsing needs partition-
     * table-aware pre-checks this project doesn't have yet - not
     * attempted. */
    if (loadDev9() < 0)
        return -1;
    if (loadModuleBuf(ps2atad_irx, size_ps2atad_irx) < 0)
        return -1;
    return 0;
}

/* hdd0: itself needs ps2fs.irx (not loaded, see loadHDD()'s comment) to
 * resolve at all, so probeFs() would always report it unavailable -
 * honest, but self-defeating given the drive's presence genuinely was
 * detected. "available" here means what it safely can: the HDD driver
 * stack itself loaded (a real drive responded), not that it's
 * browsable. */
static int probeHDD(DeviceFamily *self)
{
    return self->loaded;
}

/* --- Network ------------------------------------------------------------
 * "available" means a real, DHCP-assigned IP was obtained, not just that
 * the driver stack loaded - matching every other family's probe()
 * meaning "actually usable right now", not merely "attempted".
 *
 * ps2sdk ships two separate, mutually exclusive network stack
 * architectures: a full lwIP stack resident on the EE (package "ps2ip",
 * pairing with netman+smap alone - no documented EE init entry point in
 * its own headers), or a thin EE-side RPC client (package "ps2ips",
 * ps2sdkapi.h's own comment calls it "this package's small RPC-client
 * sibling") talking to an IOP-resident stack (ps2ips.irx, from the
 * separate "ps2ips-iop" package). Going with the latter here - it has a
 * clean, documented ps2ip_init()/ps2ip_deinit() entry point, unlike the
 * EE-resident alternative. This project's earlier module set
 * (ps2ip-nm.irx) was actually a THIRD, mismatched combination, paired
 * with netman+smap from the first architecture - fixed here. */

/* Confirmed via a live PCSX2 debugger session with real ethernet/HDD
 * enabled (M12 follow-up): loadDev9()/netman.irx/smap.irx all succeed
 * (real module IDs returned), but ps2ips.irx itself fails to load with
 * -200, an IOP-side generic module-load rejection - not a missing file
 * (verified present and correctly deployed at every path). By this point
 * in boot, 15 other IOP modules are already resident (SIO2MAN, PADMAN,
 * iomanX, fileXio, poweroff, mcman, mcserv, usbd, usbhdfsd, cdvdman,
 * cdvdfsv, ps2dev9, ps2atad, netman, smap) inside the IOP's small 2MB
 * RAM - IOP memory exhaustion is the leading hypothesis, though
 * confirming it would mean reordering module loading project-wide
 * (surgery affecting every device family, not just this one) - left as
 * a documented, real, known limitation rather than risking already-
 * working families for an unconfirmed theory. See the plan document's
 * M12 section for the full writeup. */
static int loadNetwork(DeviceFamily *self)
{
    (void)self;
    if (loadDev9() < 0)
        return -1;
    if (loadModuleBuf(netman_irx, size_netman_irx) < 0)
        return -1;
    if (loadModuleBuf(smap_irx, size_smap_irx) < 0)
        return -1;
    if (loadModuleBuf(ps2ips_irx, size_ps2ips_irx) < 0)
        return -1;

    if (NetManInit() < 0)
        return -1;
    if (ps2ip_init() < 0)
        return -1;

    return 0;
}

/* DHCP negotiation is asynchronous and can take a few seconds - same
 * retry-with-busyWait shape as probeUSB() above for USB enumeration.
 * "sm0" is the conventional netif name smap's driver registers under.
 * Untestable in PCSX2 (no real DEV9/network hardware configured here,
 * confirmed back in M6) - this always times out and reports unavailable
 * in this specific environment; the retry loop itself is what needs
 * confirming on real hardware, where DHCP negotiation genuinely takes
 * measurable time. */
static int probeNetwork(DeviceFamily *self)
{
    if (!self->loaded)
        return 0;

    int i;
    for (i = 0; i < 20; i++) {
        t_ip_info info;
        if (ps2ip_getconfig("sm0", &info) >= 0 && info.ipaddr.s_addr != 0)
            return 1;
        busyWait(3000000);
    }
    return 0;
}

/* --- SMB share (smb0:) --------------------------------------------------
 * smbman.irx exposes a "smb0:" file device once a share is open - browsed
 * via the exact same generic fileXio scan every other family uses
 * (scanDevice() in main.c), no special-casing needed once probeSmb()
 * below succeeds. Requires network's DHCP-assigned IP already up, so
 * it's listed (and therefore probed) after "network" in families[]
 * below.
 *
 * No settings UI exists yet to collect a real server IP/credentials/
 * share name - same scope cut as the missing static-IP-entry UI for
 * "network" above. These are placeholder values that will always fail
 * login against a real network (no such server exists), but the login/
 * open-share devctl sequence itself is genuine, working code - ready for
 * a future settings screen to supply real values instead. */
#define SMB_PLACEHOLDER_SERVER_IP "192.168.1.1"
#define SMB_PLACEHOLDER_SERVER_PORT 445
#define SMB_PLACEHOLDER_USER "guest"
#define SMB_PLACEHOLDER_PASSWORD ""
#define SMB_PLACEHOLDER_SHARE "PS2"

static int loadSmb(DeviceFamily *self)
{
    (void)self;
    return loadModuleBuf(smbman_irx, size_smbman_irx) < 0 ? -1 : 0;
}

static int networkAvailable(void)
{
    int count;
    DeviceFamily *fams = deviceMgrGetFamilies(&count);
    int i;
    for (i = 0; i < count; i++) {
        if (strcmp(fams[i].name, "network") == 0)
            return fams[i].available;
    }
    return 0;
}

static int probeSmb(DeviceFamily *self)
{
    if (!self->loaded || !networkAvailable())
        return 0;

    smbLogOn_in_t login;
    memset(&login, 0, sizeof(login));
    strncpy(login.serverIP, SMB_PLACEHOLDER_SERVER_IP, sizeof(login.serverIP) - 1);
    login.serverPort = SMB_PLACEHOLDER_SERVER_PORT;
    strncpy(login.User, SMB_PLACEHOLDER_USER, sizeof(login.User) - 1);
    strncpy(login.Password, SMB_PLACEHOLDER_PASSWORD, sizeof(login.Password) - 1);
    login.PasswordType = SMB_PLAINTEXT_PASSWORD;

    if (fileXioDevctl("smb0:", SMB_DEVCTL_LOGON, &login, sizeof(login), NULL, 0) < 0)
        return 0;

    smbOpenShare_in_t openShare;
    memset(&openShare, 0, sizeof(openShare));
    strncpy(openShare.ShareName, SMB_PLACEHOLDER_SHARE, sizeof(openShare.ShareName) - 1);
    openShare.PasswordType = SMB_PLAINTEXT_PASSWORD;

    if (fileXioDevctl("smb0:", SMB_DEVCTL_OPENSHARE, &openShare, sizeof(openShare), NULL, 0) < 0) {
        fileXioDevctl("smb0:", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0);
        return 0;
    }

    return 1;
}

/* quickProbe is NULL for hdd0/network/smb - none are physically
 * "removable" mid-session the way MC/USB/disc media are, and their real
 * probe()s are multi-second retry/login sequences unsuitable for a
 * recheck run every few seconds (see device_mgr.h's comment). mc0/mc1/
 * cdrom0 reuse probeFs directly (already a single, cheap call); mass
 * gets probeFs too, specifically bypassing probeUSB's boot-only
 * enumeration-wait retry loop. */
static DeviceFamily families[] = {
    { "mc0", "mc0:/", 0, 0, 0, loadMC, probeFs, probeFs },
    { "mc1", "mc1:/", 0, 0, 0, loadMC, probeFs, probeFs },
    { "mass", "mass:/", 0, 0, 0, loadUSB, probeUSB, probeFs },
    { "cdrom0", "cdrom0:/", 0, 0, 0, loadCD, probeFs, probeFs },
    /* mountPrefix NULL, not "hdd0:/" - hdd0: needs ps2fs.irx (not loaded,
     * see loadHDD()'s comment on the real hang that caused this) to
     * resolve at all, so there's nothing scanDevice() could ever browse
     * there yet. */
    { "hdd0", NULL, 0, 0, 0, loadHDD, probeHDD, NULL },
    { "network", NULL, 0, 0, 0, loadNetwork, probeNetwork, NULL },
    { "smb", "smb0:/", 0, 0, 0, loadSmb, probeSmb, NULL },
};

DeviceFamily *deviceMgrGetFamilies(int *count)
{
    *count = sizeof(families) / sizeof(families[0]);
    return families;
}

void deviceMgrRefresh(DeviceFamily *family)
{
    if (!family->attempted) {
        family->attempted = 1;
        family->loaded = (family->loadModules(family) == 0);
        if (!family->loaded)
            logMsg("device %s: module load failed", family->name);
    }

    family->available = family->loaded && family->probe(family);
    logMsg("device %s: %s", family->name, family->available ? "available" : "unavailable");
}

void deviceMgrQuickRescan(DeviceFamily *family)
{
    if (!family->quickProbe || !family->loaded)
        return;

    int wasAvailable = family->available;
    family->available = family->quickProbe(family);
    if (family->available != wasAvailable)
        logMsg("device %s: %s (rescan)", family->name, family->available ? "available" : "unavailable");
}
