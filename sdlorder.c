/* sdlorder -- reproduce ScummVM's SDL start-up order exactly: VIDEO and TIMER first, THEN audio.
 * /sdlaudio opens audio on its own and succeeds, ScummVM opens it after the timer subsystem and
 * fails with "Couldn't create audio thread". If that order is the difference, this shows it, and
 * the SDL_CreateThread calls after it say whether threads work at all at that point.
 */
#include <SDL/SDL.h>
#include <stdio.h>

static void fill(void *ud, Uint8 *s, int len) { (void)ud; SDL_memset(s, 128, len); }
static int  spin(void *a) { (void)a; SDL_Delay(400); return 0; }

int main(void)
{
    SDL_AudioSpec want, have;

    printf("sdlorder: SDL_Init(VIDEO|TIMER)...\n"); fflush(stdout);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        printf("sdlorder: init failed: %s\n", SDL_GetError()); return 1;
    }
    printf("sdlorder: ok. SDL_CreateThread while the timer subsystem is up...\n"); fflush(stdout);
    SDL_Thread *th = SDL_CreateThread(spin, NULL);
    printf("sdlorder: thread %s (%s)\n", th ? "created" : "FAILED", th ? "" : SDL_GetError());
    fflush(stdout);
    if (th) SDL_WaitThread(th, NULL);

    printf("sdlorder: InitSubSystem(AUDIO)...\n"); fflush(stdout);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        printf("sdlorder: audio subsystem failed: %s\n", SDL_GetError());

    SDL_memset(&want, 0, sizeof want);
    want.freq = 11025; want.format = AUDIO_U8; want.channels = 1;
    want.samples = 512; want.callback = fill;
    printf("sdlorder: SDL_OpenAudio...\n"); fflush(stdout);
    if (SDL_OpenAudio(&want, &have) != 0)
        printf("sdlorder: open FAILED: %s\n", SDL_GetError());
    else {
        printf("sdlorder: open ok (%d Hz, %d ch)\n", have.freq, have.channels);
        SDL_PauseAudio(0);
        SDL_Delay(600);
        SDL_CloseAudio();
    }
    fflush(stdout);
    SDL_Quit();
    printf("sdlorder: done\n"); fflush(stdout);
    return 0;
}
