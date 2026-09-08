#include "device_mgr.h"

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <iox_stat.h>

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
    /* dev9 -> atad -> hdd -> fs, in order; abort as soon as one fails
     * rather than attempting later modules against a driver that isn't
     * there - PFS partition mount/creation is a separate, later concern
     * (section 6 flags HDD as "needs extra modules, moderate risk" and
     * expects most environments, this one included, to not have one). */
    if (loadDev9() < 0)
        return -1;
    if (loadModule("host:modules/ps2atad.irx") < 0)
        return -1;
    if (loadModule("host:modules/ps2hdd.irx") < 0)
        return -1;
    if (loadModule("host:modules/ps2fs.irx") < 0)
        return -1;
    return 0;
}

/* --- Network ----------------------------------------------------------
 * No filesystem mount point - "available" here just means the driver
 * stack loaded, not that a link/IP address exists yet (that's later
 * work, once there's a settings UI to configure it). */

static int loadNetwork(DeviceFamily *self)
{
    (void)self;
    if (loadDev9() < 0)
        return -1;
    if (loadModule("host:modules/netman.irx") < 0)
        return -1;
    if (loadModule("host:modules/smap.irx") < 0)
        return -1;
    if (loadModule("host:modules/ps2ip-nm.irx") < 0)
        return -1;
    return 0;
}

static int probeNetwork(DeviceFamily *self)
{
    return self->loaded;
}

static DeviceFamily families[] = {
    { "mc0", "mc0:/", 0, 0, 0, loadMC, probeFs },
    { "mc1", "mc1:/", 0, 0, 0, loadMC, probeFs },
    { "mass", "mass:/", 0, 0, 0, loadUSB, probeUSB },
    { "cdrom0", "cdrom0:/", 0, 0, 0, loadCD, probeFs },
    { "hdd0", "hdd0:/", 0, 0, 0, loadHDD, probeFs },
    { "network", NULL, 0, 0, 0, loadNetwork, probeNetwork },
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
