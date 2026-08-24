/* mystery -- start Hi-Res Adventure #1: Mystery House.
 *
 * The same idea as wadrun: a tiny launcher so the game has a name at the prompt, rather than a
 * command line to remember. ScummVM is pointed at the game's directory and told to detect and
 * start what it finds, so it never shows its launcher GUI -- on a machine this slow, a menu you
 * have to click through is worse than no menu. --auto-detect rather than the target id "hires1",
 * because an id has to exist in scummvm.ini first and there is exactly one game here anyway.
 *
 * Mystery House was released into the public domain by Sierra, which is why it can live in the
 * image at all; the DOOM WADs beside it cannot.
 */
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#define SCUMMVM "/usr/bin/scummvm"
#define GAMEDIR "/games/mysthouse"

int main(int argc, char **argv)
{
    /* Extra arguments are passed through, so `mystery -d3` still works for debugging. */
    char *args[16];
    int   n = 0;

    args[n++] = (char *)"scummvm";
    args[n++] = (char *)"--path=" GAMEDIR;
    args[n++] = (char *)"--auto-detect";
    for (int i = 1; i < argc && n < 14; i++)
        args[n++] = argv[i];
    args[n] = NULL;

    printf("mystery: Hi-Res Adventure #1: Mystery House (1980, public domain)\n");
    execv(SCUMMVM, args);
    printf("mystery: cannot exec %s (errno %d)\n", SCUMMVM, errno);
    return 127;
}
