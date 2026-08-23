/* sdltest -- does SDL work on lunatix?
 *
 * SDL 1.2 is the last SDL with a framebuffer video driver: it draws straight onto /dev/fb0 and
 * speaks OSS to /dev/dsp, which is exactly the hardware this machine has (SDL2 wants KMSDRM or
 * a window system, neither of which exists here). If this program draws and beeps, then every
 * SDL 1.2 program is a porting job rather than a from-scratch backend -- ScummVM included.
 *
 * Deliberately unattended: it draws for a fixed number of frames and exits, so it can run from
 * init in a headless capture and be judged from a screenshot. ESC quits early.
 */
#include <SDL/SDL.h>
#include <stdio.h>
#include <unistd.h>

/* Unbuffered, straight to fd 2: a diagnostic that dies with the process is no diagnostic. The
 * first attempt at this test printed its failure to stdout and the message never arrived. */
#define SAY(...) do { char _b[256]; int _n = snprintf(_b, sizeof _b, __VA_ARGS__); \
                      if (_n > 0) (void)!write(2, _b, (size_t)_n); } while (0)
#include <stdlib.h>
#include <math.h>

#define W      320
#define H      200
#define FRAMES 120

static void beep(void)
{
    /* A short square wave through SDL's audio path, i.e. through /dev/dsp. Queued the simple
     * way -- a callback filling from a static phase -- because the point is the plumbing. */
    static Sint16 buf[8000];
    static int    pos;
    SDL_AudioSpec want;

    for (int i = 0; i < (int)(sizeof buf / sizeof buf[0]); i++)
        buf[i] = (i / 40) % 2 ? 6000 : -6000;      /* ~275 Hz at 22050 Hz */

    SDL_memset(&want, 0, sizeof want);
    want.freq = 22050;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.userdata = &pos;
    want.callback = NULL;                          /* filled in below */

    /* SDL 1.2 has no queueing API, so a callback it is. */
    extern void fill(void *ud, Uint8 *stream, int len);
    want.callback = fill;
    if (SDL_OpenAudio(&want, NULL) < 0) {
        SAY("sdltest: no audio (%s)\n", SDL_GetError());
        return;
    }
    SDL_PauseAudio(0);
}

void fill(void *ud, Uint8 *stream, int len)
{
    static double phase;
    Sint16 *out = (Sint16 *)stream;
    int     n = len / 2;

    for (int i = 0; i < n; i++) {
        out[i] = (Sint16)(5000.0 * sin(phase));
        phase += 2.0 * 3.14159265 * 330.0 / 22050.0;
    }
    (void)ud;
}

int main(int argc, char **argv)
{
    SDL_Surface *screen;
    int          frame;

    SAY("sdltest: SDL_Init...\n");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SAY("sdltest: SDL_Init(VIDEO) failed: %s\n", SDL_GetError());
        return 1;
    }
    SAY("sdltest: video driver is %s\n", SDL_VideoDriverName((char[32]){0}, 32));

    screen = SDL_SetVideoMode(W, H, 32, SDL_SWSURFACE);
    if (!screen) {
        SAY("sdltest: SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SAY("sdltest: got %dx%d, %d bpp, pitch %d\n",
        screen->w, screen->h, screen->format->BitsPerPixel, screen->pitch);

    if (SDL_Init(SDL_INIT_AUDIO) == 0)
        beep();
    else
        SAY("sdltest: audio init failed: %s\n", SDL_GetError());

    for (frame = 0; frame < FRAMES; frame++) {
        SDL_Event ev;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT ||
                (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE))
                goto done;
            if (ev.type == SDL_KEYDOWN)
                SAY("sdltest: key %d\n", (int)ev.key.keysym.sym);
        }

        if (SDL_MUSTLOCK(screen) && SDL_LockSurface(screen) < 0)
            continue;
        for (int y = 0; y < screen->h; y++) {
            Uint32 *row = (Uint32 *)((Uint8 *)screen->pixels + y * screen->pitch);

            for (int x = 0; x < screen->w; x++) {
                /* Something unmistakably drawn rather than left over: colour ramps, a moving
                 * bar, and a white border so the edges of the mode are visible. */
                int edge = (x < 2 || y < 2 || x >= screen->w - 2 || y >= screen->h - 2);
                int bar  = (((x + frame * 3) / 16) % 2) == 0;
                Uint8 r = (Uint8)(x * 255 / screen->w);
                Uint8 g = (Uint8)(y * 255 / screen->h);
                Uint8 b = (Uint8)(bar ? 200 : 40);

                row[x] = edge ? 0x00FFFFFFu : (Uint32)((r << 16) | (g << 8) | b);
            }
        }
        if (SDL_MUSTLOCK(screen))
            SDL_UnlockSurface(screen);
        SDL_Flip(screen);
    }

done:
    SAY("sdltest: %d frames drawn, quitting\n", frame);
    SDL_Quit();
    return 0;
}
