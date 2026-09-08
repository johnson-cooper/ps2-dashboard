#ifndef PS2LAUNCHER_SMBMAN_H
#define PS2LAUNCHER_SMBMAN_H

/* SMB device (smbman.irx) devctl command/struct interface, reused from
 * jimmikaelkael's canonical ps2-smbman (the same driver ps2sdk's own
 * "smbman" package installs), https://github.com/jimmikaelkael/ps2-smbman,
 * licensed under the Academic Free License v3.0 (compatible with the
 * AFL-2.0 the rest of ps2sdk uses). Per AFL's attribution requirement:
 * this file's constants/structs are unmodified from that upstream
 * smbman.h (only NO_PASSWORD/PLAINTEXT_PASSWORD/HASHED_PASSWORD were
 * renamed with an SMB_ prefix to avoid colliding with unrelated project
 * symbols - values and struct layouts are identical). smbman.irx ships
 * with no EE-side header of its own in the ps2sdk package registry -
 * every real consumer (OPL, wLaunchELF) vendors this same interface by
 * hand for that reason. */

#define SMB_NO_PASSWORD -1
#define SMB_PLAINTEXT_PASSWORD 0
#define SMB_HASHED_PASSWORD 1

#define SMB_DEVCTL_GETPASSWORDHASHES 0xC0DE0001
#define SMB_DEVCTL_LOGON 0xC0DE0002
#define SMB_DEVCTL_LOGOFF 0xC0DE0003
#define SMB_DEVCTL_GETSHARELIST 0xC0DE0004
#define SMB_DEVCTL_OPENSHARE 0xC0DE0005
#define SMB_DEVCTL_CLOSESHARE 0xC0DE0006
#define SMB_DEVCTL_ECHO 0xC0DE0007
#define SMB_DEVCTL_QUERYDISKINFO 0xC0DE0008

typedef struct { /* size = 536 */
    char serverIP[16];
    int serverPort;
    char User[256];
    char Password[256];
    int PasswordType; /* SMB_PLAINTEXT_PASSWORD or SMB_HASHED_PASSWORD */
} smbLogOn_in_t;

typedef struct { /* size = 8 */
    void *EE_addr;
    int maxent;
} smbGetShareList_in_t;

typedef struct { /* size = 520 */
    char ShareName[256];
    char Password[256];
    int PasswordType;
} smbOpenShare_in_t;

typedef struct { /* size = 16 */
    int TotalUnits;
    int BlocksPerUnit;
    int BlockSize;
    int FreeUnits;
} smbQueryDiskInfo_out_t;

typedef struct { /* size = 512 */
    char ShareName[256];
    char ShareComment[256];
} ShareEntry_t;

#endif
