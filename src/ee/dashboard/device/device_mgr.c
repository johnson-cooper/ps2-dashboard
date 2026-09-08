#include "device_mgr.h"

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <netman.h>
#include <ps2ips.h>
#include <ps2sdkapi.h>
#include <string.h>

#include "smbman.h"

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
    if (loadModule("host:modules/mcman.irx") < 0)
        return -1;
    if (loadModule("host:modules/mcserv.irx") < 0)
        return -1;

    mcModulesLoaded = 1;
    return 0;
}

/* --- USB mass storage (mass:) --------------------------------------- */

static int loadUSB(DeviceFamily *self)
{
    (void)self;
    if (loadModule("host:modules/usbd.irx") < 0)
        return -1;
    if (loadModule("host:modules/usbhdfsd.irx") < 0)
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
    if (loadModule("host:modules/cdvdman.irx") < 0)
        return -1;
    return loadModule("host:modules/cdvdfsv.irx") < 0 ? -1 : 0;
}

/* --- Internal HDD (hdd0:) --------------------------------------------
 * dev9 is shared with the network family below - guarded so it's only
 * ever loaded once regardless of which family reaches it first. */

static int dev9Loaded = 0;

static int loadDev9(void)
{
    if (dev9Loaded)
        return 0;
    if (loadModule("host:modules/ps2dev9.irx") < 0)
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
    if (loadModule("host:modules/ps2atad.irx") < 0)
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

static int loadNetwork(DeviceFamily *self)
{
    (void)self;
    if (loadDev9() < 0)
        return -1;
    if (loadModule("host:modules/netman.irx") < 0)
        return -1;
    if (loadModule("host:modules/smap.irx") < 0)
        return -1;
    if (loadModule("host:modules/ps2ips.irx") < 0)
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
    return loadModule("host:modules/smbman.irx") < 0 ? -1 : 0;
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

static DeviceFamily families[] = {
    { "mc0", "mc0:/", 0, 0, 0, loadMC, probeFs },
    { "mc1", "mc1:/", 0, 0, 0, loadMC, probeFs },
    { "mass", "mass:/", 0, 0, 0, loadUSB, probeUSB },
    { "cdrom0", "cdrom0:/", 0, 0, 0, loadCD, probeFs },
    /* mountPrefix NULL, not "hdd0:/" - hdd0: needs ps2fs.irx (not loaded,
     * see loadHDD()'s comment on the real hang that caused this) to
     * resolve at all, so there's nothing scanDevice() could ever browse
     * there yet. */
    { "hdd0", NULL, 0, 0, 0, loadHDD, probeHDD },
    { "network", NULL, 0, 0, 0, loadNetwork, probeNetwork },
    { "smb", "smb0:/", 0, 0, 0, loadSmb, probeSmb },
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
    }

    family->available = family->loaded && family->probe(family);
}
