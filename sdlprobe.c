/* sdlprobe -- which SDL subsystem hangs? Init them one at a time, printing before each. */
#include <SDL/SDL.h>
#include <stdio.h>
#define STEP(msg) do { printf("sdlprobe: " msg "\n"); fflush(stdout); } while (0)
int main(void)
{
    STEP("SDL_Init(0)...");
    if (SDL_Init(0) != 0) { printf("sdlprobe: failed: %s\n", SDL_GetError()); return 1; }
    STEP("ok. InitSubSystem(TIMER)...");
    if (SDL_InitSubSystem(SDL_INIT_TIMER) != 0) printf("sdlprobe: timer failed: %s\n", SDL_GetError());
    STEP("ok. InitSubSystem(VIDEO)...");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) printf("sdlprobe: video failed: %s\n", SDL_GetError());
    STEP("ok. InitSubSystem(AUDIO)...");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) printf("sdlprobe: audio failed: %s\n", SDL_GetError());
    STEP("ok. all subsystems up");
    SDL_Quit();
    return 0;
}
