/* keytest -- does an SDL program on this guest receive key events at all?
 *
 * Mystery House draws its title screen and never sees the G it asks for. Drawing goes straight to
 * /dev/fb0, so graphics working says nothing about input. SDL is built with DEBUG_KEYBOARD for this
 * run, so its own messages about which console it opened appear alongside these.
 */
#include <SDL/SDL.h>
#include <stdio.h>

int main(void)
{
    SDL_Surface *screen;
    SDL_Event ev;
    int seen = 0, ticks = 0;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("keytest: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    screen = SDL_SetVideoMode(320, 200, 0, SDL_SWSURFACE);
    printf("keytest: video %s, waiting for keys (ESC or 60s to quit)\n",
           screen ? "up" : "FAILED");
    fflush(stdout);

    while (ticks < 600) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_KEYDOWN) {
                printf("keytest: KEYDOWN sym=%d (%s) unicode=%d\n",
                       (int)ev.key.keysym.sym, SDL_GetKeyName(ev.key.keysym.sym),
                       (int)ev.key.keysym.unicode);
                fflush(stdout);
                seen++;
                if (ev.key.keysym.sym == SDLK_ESCAPE)
                    goto done;
            } else if (ev.type == SDL_QUIT) {
                goto done;
            }
        }
        SDL_Delay(100);
        ticks++;
    }
done:
    printf("keytest: %d key events seen\n", seen);
    fflush(stdout);
    SDL_Quit();
    return 0;
}
