/* mystery -- start Hi-Res Adventure #1: Mystery House.
 *
 * The same idea as wadrun: a tiny launcher so the game has a name at the prompt rather than a
 * command line to remember.
 *
 * Everything interesting is in /etc/scummvm.ini, which carries the target (so ScummVM does not
 * re-detect every engine it knows on each start) and ntsc=false. That last one matters more than
 * it looks: with the NTSC colour filter on -- ScummVM's default -- the ADL engine builds its
 * colour table with sin/cos/floor in double precision, and this machine has neither floating point
 * nor a multiply instruction. A profile of the startup showed all of its time in
 * __ieee754_rem_pio2, floor and __adddf3 under PixelWriterColorNTSC's constructor. PixelWriterColor
 * produces the same sixteen Apple II colours with integer arithmetic.
 *
 * Mystery House was released as freeware by Sierra, which is why it can live in the image at all;
 * the DOOM WADs beside it cannot.
 */
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define SCUMMVM "/usr/bin/scummvm"
#define CONFIG  "/etc/scummvm.ini"        /* no audio device: see the comment in the file */
#define CONFIG_SND "/etc/scummvm-sound.ini"
#define TARGET  "hires1"

int main(int argc, char **argv)
{
    /* Extra arguments are passed through, so `mystery -d3` still works for debugging. */
    char *args[20];
    int   n = 0;
    int   sound = 0;

    /* --sound puts the audio device back. It is off by default because this game has none:
     * ScummVM's mixer would otherwise spend its time mixing 44100 Hz stereo silence and writing
     * it to /dev/dsp, and every one of those samples is interpreted subleq. mute=true does NOT
     * avoid that -- it only turns the volume down, which is a different thing entirely. */
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--sound") == 0) {
            sound = 1;
            argv[i] = NULL;                      /* consumed: not a ScummVM option */
        }

    args[n++] = (char *)"scummvm";
    args[n++] = (char *)(sound ? "--config=" CONFIG_SND : "--config=" CONFIG);
    args[n++] = (char *)"--gui-theme=scummclassic";
    args[n++] = (char *)"--scale-factor=1";     /* no scaler: every pixel costs here */
    args[n++] = (char *)"--no-aspect-ratio";
    args[n++] = (char *)"--no-filtering";
    for (int i = 1; i < argc && n < 18; i++)
        if (argv[i]) args[n++] = argv[i];        /* NULL = consumed above */
    args[n++] = (char *)TARGET;
    args[n] = NULL;

    printf("mystery: Hi-Res Adventure #1: Mystery House (1980, public domain)%s\n",
           sound ? " [--sound: audio device open]"
                 : " [silent: no audio device -- pass --sound to open one]");
    fflush(stdout);
    execv(SCUMMVM, args);
    printf("mystery: cannot exec %s (errno %d)\n", SCUMMVM, errno);
    return 127;
}
