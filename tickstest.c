/* tickstest -- does time advance for an SDL program on this guest?
 *
 * ScummVM sits on its splash screen forever. The splash is dismissed on a timer, and the engine's
 * main loop is paced by one too, so a clock that does not advance would look exactly like this --
 * while drawing itself is known to work (sdltest draws a pattern through the same SDL).
 *
 * Three clocks are printed side by side: SDL_GetTicks (what ScummVM's OSystem uses),
 * gettimeofday (what SDL is built on here) and clock_gettime(CLOCK_MONOTONIC).
 */
#include <SDL/SDL.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("tickstest: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    for (int i = 0; i < 5; i++) {
        struct timeval tv;
        struct timespec ts;

        gettimeofday(&tv, NULL);
        clock_gettime(CLOCK_MONOTONIC, &ts);
        printf("tickstest: SDL_GetTicks=%lu  gettimeofday=%ld.%06ld  monotonic=%ld.%09ld\n",
               (unsigned long)SDL_GetTicks(), (long)tv.tv_sec, (long)tv.tv_usec,
               (long)ts.tv_sec, (long)ts.tv_nsec);
        fflush(stdout);
        sleep(1);
    }
    /* And does SDL_Delay come back at all? */
    printf("tickstest: SDL_Delay(500)...\n");
    fflush(stdout);
    SDL_Delay(500);
    printf("tickstest: back, SDL_GetTicks=%lu\n", (unsigned long)SDL_GetTicks());
    SDL_Quit();
    return 0;
}
