#include "app_entry.h"

const char *deviceKindName(DeviceKind kind)
{
    switch (kind) {
    case DEVICE_MC0:
        return "Memory Card 1";
    case DEVICE_MC1:
        return "Memory Card 2";
    case DEVICE_MASS:
        return "USB";
    case DEVICE_CDROM:
        return "Disc";
    case DEVICE_HDD:
        return "HDD";
    case DEVICE_NETWORK:
        return "Network";
    case DEVICE_SMB:
        return "Network Share";
    default:
        return "Unknown";
    }
}
