#ifndef PS2LAUNCHER_DEVICE_MGR_H
#define PS2LAUNCHER_DEVICE_MGR_H

/* Device family descriptor: mount-point prefix, a lazy per-family IOP
 * module loader, and a probe function - the driver-registration pattern
 * from the plan (section 6), mirroring OPL's bdmsupport.c unified
 * block-device abstraction. Each family is loaded and probed at most
 * once per boot for now; periodic re-probing belongs to the real UI
 * event loop (M8) which doesn't exist yet. */
typedef struct DeviceFamily {
    const char *name;
    const char *mountPrefix; /* NULL for non-filesystem families (network) */
    int attempted;           /* loadModules() has been called */
    int loaded;              /* loadModules() succeeded */
    int available;           /* last probe()/quickRescan() result */
    int (*loadModules)(struct DeviceFamily *self);
    int (*probe)(struct DeviceFamily *self);
    /* M13 (plan section 11, "removed storage mid-browse"): a cheap,
     * single-shot recheck for periodic mid-session rescanning - NULL for
     * families that shouldn't be periodically rescanned. Deliberately
     * separate from probe(): probe() for mass/network/smb involves
     * multi-second retry loops or login sequences appropriate for the
     * one-time boot wait, but not for a recheck run every few seconds -
     * calling probe() itself periodically would introduce exactly the
     * kind of visible stutter/hang this milestone is trying to close. */
    int (*quickProbe)(struct DeviceFamily *self);
} DeviceFamily;

/* Returns the built-in family table and its length. Families are
 * ordered cheapest/most-likely-available first (mc0, mc1, mass, cdrom0,
 * hdd, network) purely so a straightforward top-to-bottom scan reports
 * the common cases before the ones that need hardware this environment
 * probably doesn't have. */
DeviceFamily *deviceMgrGetFamilies(int *count);

/* Lazily loads (if not already attempted) and probes one family,
 * updating its attempted/loaded/available fields in place. Safe to call
 * repeatedly - loadModules only actually runs once. */
void deviceMgrRefresh(DeviceFamily *family);

/* Cheap, no-retry recheck of one family's availability via its
 * quickProbe() - a no-op if quickProbe is NULL (this family isn't
 * periodically rescanned) or its modules never loaded. Safe to call
 * every frame if needed - unlike deviceMgrRefresh()'s probe(), this
 * never blocks on a multi-second retry loop. */
void deviceMgrQuickRescan(DeviceFamily *family);

#endif
