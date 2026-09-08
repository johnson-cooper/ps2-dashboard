/* Minimal chain-load stub. Deliberately tiny, and linked to run at
 * 0x01000000 (see linkfile) rather than the standard 0x00100000 base -
 * both so it doesn't overlap the resident dashboard that loads it while
 * still running, and so IT can safely load the real target at the
 * standard 0x00100000 base once dashboard's memory is free.
 *
 * argv contract (set up by the dashboard's own ExecPS2 call into this):
 *   argv[0]    = path to the real target ELF
 *   argv[1]    = "1" to reset the IOP before jumping, "0" otherwise
 *   argv[2..]  = extra launch arguments for the target
 *
 * The target itself is launched with argv[0] = its own path (normal PS2
 * homebrew/C convention - a program's own argv[0] is its path) followed
 * by argv[2..] above; it never sees the reset-IOP flag, that's this
 * stub's own plumbing. Earlier this dropped argv[0] entirely and passed
 * an empty/one-past-end argv when there were no extra arguments, which
 * is undefined once the target's own crt0 touches argv[0] regardless of
 * argc - confirmed to produce inconsistent results (wrong color, an
 * outright crash) during the M8 spike.
 */
#include <stdlib.h>

#include "../common/elfloader.h"

#define MAX_TARGET_ARGS 16

int main(int argc, char *argv[])
{
    if (argc < 2)
        while (1) {
        }

    const char *targetPath = argv[0];
    int resetIop = atoi(argv[1]);

    static char *targetArgv[MAX_TARGET_ARGS];
    int targetArgc = 0;
    targetArgv[targetArgc++] = (char *)targetPath;
    int i;
    for (i = 2; i < argc && targetArgc < MAX_TARGET_ARGS; i++)
        targetArgv[targetArgc++] = argv[i];

    elfLoadAndExec(targetPath, resetIop, targetArgc, targetArgv);

    /* elfLoadAndExec doesn't return on success. */
    while (1) {
    }

    return 0;
}
