/* sdlthreadtest -- which part of SDL's thread layer works on this guest?
 *
 * pthreads themselves work (pthreadtest: "thread done, counter=1000"), but ScummVM now stops
 * right after audio init, which is where its timer manager creates an SDL thread. SDL's threads
 * are built on pthread mutexes, semaphores and condition variables -- the last two lean on
 * signals in linuxthreads -- so this walks the layers one at a time and says where it stops.
 */
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <stdio.h>

static int counter;

static int worker(void *arg)
{
    for (int i = 0; i < 1000; i++)
        counter++;
    return 42;
}

static Uint32 tick(Uint32 interval, void *arg)
{
    int *n = (int *)arg;
    (*n)++;
    return interval;
}

#define STEP(msg) do { printf("sdlthreadtest: " msg "\n"); fflush(stdout); } while (0)

int main(void)
{
    SDL_Thread *th;
    SDL_sem *sem;
    SDL_mutex *mtx;
    SDL_cond *cond;
    SDL_TimerID id;
    int status = 0, ticks = 0;

    STEP("SDL_Init(VIDEO|TIMER)...");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        printf("sdlthreadtest: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    STEP("ok. SDL_CreateMutex...");
    mtx = SDL_CreateMutex();
    printf("sdlthreadtest: mutex %p, lock/unlock...\n", (void *)mtx); fflush(stdout);
    SDL_mutexP(mtx); SDL_mutexV(mtx);

    STEP("ok. SDL_CreateSemaphore...");
    sem = SDL_CreateSemaphore(0);
    printf("sdlthreadtest: sem %p, post/wait...\n", (void *)sem); fflush(stdout);
    SDL_SemPost(sem); SDL_SemWait(sem);

    STEP("ok. SDL_CreateCond...");
    cond = SDL_CreateCond();
    printf("sdlthreadtest: cond %p\n", (void *)cond); fflush(stdout);

    STEP("ok. SDL_CreateThread...");
    th = SDL_CreateThread(worker, NULL);
    if (!th) {
        printf("sdlthreadtest: SDL_CreateThread failed: %s\n", SDL_GetError());
        return 1;
    }
    STEP("created, waiting...");
    SDL_WaitThread(th, &status);
    printf("sdlthreadtest: thread returned %d, counter=%d\n", status, counter); fflush(stdout);

    STEP("SDL_AddTimer...");
    id = SDL_AddTimer(100, tick, &ticks);
    printf("sdlthreadtest: timer id %p, sleeping 1s...\n", (void *)id); fflush(stdout);
    SDL_Delay(1000);
    printf("sdlthreadtest: timer fired %d times\n", ticks); fflush(stdout);
    if (id) SDL_RemoveTimer(id);

    STEP("SDL_InitSubSystem(AUDIO)...");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        printf("sdlthreadtest: audio init failed: %s\n", SDL_GetError());
    else
    {
        char drv[64] = "";
        SDL_AudioDriverName(drv, sizeof drv);
        printf("sdlthreadtest: audio driver \"%s\"\n", drv);
    }
    fflush(stdout);

    STEP("all steps passed");
    SDL_Quit();
    return 0;
}
