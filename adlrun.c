/* adlrun -- start a ScummVM ADL game (Apple II Hi-Res Adventure) by the name it was invoked as.
 *
 * The same trick as wadrun: /mystery and /timezone are symlinks to this program, so the shell
 * offers a game name rather than a command line to remember. What differs per game is not just the
 * target -- it is whether the machine should open an audio device at all:
 *
 *   mystery   Hi-Res Adventure #1, Mystery House. NO sound exists in this game: in ScummVM's ADL
 *             engine the Apple II speaker synth is only wired up by hires5.cpp, and hires1.cpp has
 *             no sound code at all, matching the silent 1980 original. So it runs with
 *             scummvm-silent.ini, which switches the device off. Worth 35% of the startup time and
 *             half of all syscalls on this machine -- ScummVM's mute=true does NOT do that, it only
 *             turns the volume down while the mixer keeps writing 44100 Hz stereo silence.
 *
 *   timezone  Hi-Res Adventure #5, Time Zone. This one DOES make sound, so it gets the normal
 *             configuration, audio device and all.
 *
 * Mystery House is in the image because Sierra released it as freeware in 1987. Time Zone was
 * never released that way -- like the DOOM WADs, its twelve disk images cannot live here, so the
 * launcher checks and says what is missing instead of leaving ScummVM to fail obscurely.
 */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#define SCUMMVM      "/usr/bin/scummvm"
#define CONFIG       "/etc/scummvm.ini"          /* the normal config: sound and all */
#define CONFIG_SILENT "/etc/scummvm-silent.ini"  /* no audio device */

static const struct edition {
    const char *name;       /* invoked as */
    const char *target;     /* ScummVM target in the config file */
    const char *dir;        /* where its data must be */
    const char *what;
    int         silent;     /* 1 = the game has no sound: do not open a device */
    const char *needs;      /* what the data looks like, for when it is absent */
} editions[] = {
    { "mystery",  "hires1", "/games/mysthouse",
      "Hi-Res Adventure #1: Mystery House (1980, freeware since 1987)", 1,
      "MYSTHOUS.DSK" },
    { "timezone", "hires5", "/games/timezone",
      "Hi-Res Adventure #5: Time Zone (1982)", 0,
      "twelve Apple II disk images of 143360 bytes each, named tzone1a tzone1b tzone2c tzone2d\n"
      "         tzone3e tzone3f tzone4g tzone4h tzone5i tzone5j tzone6k tzone6l" },
};

/* Is there anything at all in the game's directory? ScummVM's own failure for missing data is a
 * detection message from inside a full engine start-up, which on this machine is a minute away. */
static int has_data(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;

    if (!d) return 0;
    while ((e = readdir(d)) != NULL)
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) { found = 1; break; }
    closedir(d);
    return found;
}

int main(int argc, char **argv)
{
    const char *self = argv[0] ? argv[0] : "mystery";
    const char *base = strrchr(self, '/');
    const struct edition *ed = NULL;
    char *args[20];
    int   n = 0, sound = 0;

    base = base ? base + 1 : self;
    for (unsigned i = 0; i < sizeof editions / sizeof editions[0]; i++)
        if (strcmp(base, editions[i].name) == 0) ed = &editions[i];
    if (!ed) {
        printf("adlrun: no game called \"%s\". Try one of:\n", base);
        for (unsigned i = 0; i < sizeof editions / sizeof editions[0]; i++)
            printf("  %-9s %s\n", editions[i].name, editions[i].what);
        return 2;
    }

    /* --sound opens the audio device even for a game that has none (for testing the audio path);
     * --silent is the other way round, for a game that has sound but is not worth the cycles. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sound") == 0)  { sound = 1;  argv[i] = NULL; }
        if (strcmp(argv[i], "--silent") == 0) { sound = -1; argv[i] = NULL; }
    }
    if (sound == 0) sound = ed->silent ? -1 : 1;

    printf("%s: %s%s\n", ed->name, ed->what, sound > 0 ? "" : " [silent: no audio device]");
    fflush(stdout);

    if (!has_data(ed->dir)) {
        printf("%s: nothing in %s -- the game data is not in this image.\n"
               "         It needs: %s\n"
               "         Copy it into eternal/games/%s and rebuild the boot image.\n",
               ed->name, ed->dir, ed->needs, strrchr(ed->dir, '/') + 1);
        fflush(stdout);
        return 1;
    }

    args[n++] = (char *)"scummvm";
    args[n++] = (char *)(sound > 0 ? "--config=" CONFIG : "--config=" CONFIG_SILENT);
    args[n++] = (char *)"--gui-theme=scummclassic";
    args[n++] = (char *)"--scale-factor=1";      /* no scaler: every pixel costs here */
    args[n++] = (char *)"--no-aspect-ratio";
    args[n++] = (char *)"--no-filtering";
    for (int i = 1; i < argc && n < 18; i++)
        if (argv[i]) args[n++] = argv[i];        /* NULL = consumed above */
    args[n++] = (char *)ed->target;
    args[n] = NULL;

    execv(SCUMMVM, args);
    printf("%s: cannot exec %s (errno %d)\n", ed->name, SCUMMVM, errno);
    return 127;
}
