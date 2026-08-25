/* sdlaudio -- does SDL's audio THREAD work now that condition variables do?
 *
 * SDL 1.2 feeds /dev/dsp from a thread it starts in SDL_OpenAudio, and that thread parks on a
 * condition variable between buffers. With cond_wait broken, SDL_Init(SDL_INIT_AUDIO) deadlocked,
 * which is why the guest's SDL was built without threads at all and had no sound.
 *
 * So: open the device, let the callback generate a square wave for two seconds, and say how many
 * times it was called. Non-zero calls and a non-silent capture (LUNATIX_SOUND_CAPTURE) mean the
 * thread ran and the samples reached the sound card.
 */
#include <SDL/SDL.h>
#include <stdio.h>

static volatile int calls;
static int phase;

static void fill(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    calls++;
    for (int i = 0; i < len; i++) {           /* ~440 Hz square at 11025 Hz, 8-bit unsigned */
        phase++;
        if (phase >= 25) phase = -25;
        stream[i] = phase < 0 ? 96 : 160;
    }
}

int main(void)
{
    SDL_AudioSpec want, have;

    printf("sdlaudio: SDL_Init(SDL_INIT_AUDIO)...\n"); fflush(stdout);
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        printf("sdlaudio: init failed: %s\n", SDL_GetError());
        return 1;
    }
    printf("sdlaudio: initialised, opening the device\n"); fflush(stdout);

    SDL_memset(&want, 0, sizeof want);
    want.freq = 11025;                        /* what the guest's sound card runs at */
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = 512;
    want.callback = fill;
    if (SDL_OpenAudio(&want, &have) != 0) {
        printf("sdlaudio: open failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    printf("sdlaudio: open ok (%d Hz, %d ch, %d samples)\n", have.freq, have.channels, have.samples);
    fflush(stdout);

    SDL_PauseAudio(0);
    for (int s = 0; s < 4; s++) {
        SDL_Delay(500);
        printf("sdlaudio: +%d ms  callback calls=%d\n", (s + 1) * 500, calls);
        fflush(stdout);
    }
    SDL_CloseAudio();
    SDL_Quit();
    printf("sdlaudio: done, %d calls -- %s\n", calls,
           calls > 0 ? "the audio thread ran" : "the audio thread never ran");
    fflush(stdout);
    return calls > 0 ? 0 : 1;
}
