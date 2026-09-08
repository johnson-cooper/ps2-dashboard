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
    int available;           /* last probe() result */
    int (*loadModules)(struct DeviceFamily *self);
    int (*probe)(struct DeviceFamily *self);
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

#endif
