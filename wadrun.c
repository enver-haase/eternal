/* wadrun -- pick a DOOM edition by the name it was invoked as.
 *
 * The game finds its IWAD by looking in $DOOMWADDIR (default ".") for, in order, doom2.wad
 * (Doom II), doomu.wad (Ultimate, four episodes), doom.wad (registered, three) and doom1.wad
 * (shareware) -- see IdentifyVersion(). So one WAD per directory and DOOMWADDIR pointing at the
 * right one is all it takes to choose an edition; no patching of the game, and each edition
 * keeps its own name so the game identifies it correctly. Naming Ultimate's WAD doom.wad, for
 * instance, would silently run it as the three-episode registered game.
 *
 * Installed as /bin/doom, /bin/doom19, /bin/udoom and /bin/doom2, all links to this binary,
 * which dispatches on argv[0] the way BusyBox does. Arguments are passed through, so
 * `doom2 -warp 5` works.
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define GAME "/doom"

static const struct {
    const char *name;       /* how we were invoked */
    const char *dir;        /* the directory holding exactly that edition's IWAD */
    const char *what;
} editions[] = {
    { "doom",   "/wads/shareware",  "Doom shareware (episode 1)" },
    { "doom19", "/wads/registered", "Doom registered v1.9 (three episodes)" },
    { "udoom",  "/wads/ultimate",   "The Ultimate Doom (four episodes)" },
    { "doom2",  "/wads/doom2",      "Doom II: Hell on Earth" },
};

int main(int argc, char **argv)
{
    const char *self = argv[0] ? argv[0] : "doom";
    const char *slash = strrchr(self, '/');
    const char *base = slash ? slash + 1 : self;

    for (unsigned i = 0; i < sizeof editions / sizeof editions[0]; i++) {
        if (strcmp(base, editions[i].name) != 0)
            continue;

        printf("%s: %s\n", editions[i].name, editions[i].what);
        setenv("DOOMWADDIR", editions[i].dir, 1);
        argv[0] = (char *)"doom";
        execv(GAME, argv);
        printf("%s: cannot exec %s (errno %d)\n", editions[i].name, GAME, errno);
        return 127;
    }

    printf("wadrun: no edition called \"%s\". Try one of:\n", base);
    for (unsigned i = 0; i < sizeof editions / sizeof editions[0]; i++)
        printf("  %-7s %s\n", editions[i].name, editions[i].what);
    return 2;
}
