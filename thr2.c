/* thr2 -- ScummVM cannot create SDL's audio thread while /sdlaudio, calling the same function,
 * can. /bigthread ruled out size (48 MB .bss, 40 MB heap: all fine), so the next difference is
 * CONCURRENCY: ScummVM brings up SDL_INIT_TIMER first, which in a threaded SDL leaves a timer
 * thread running, and the audio thread is then the SECOND live thread. /bigthread joined each
 * thread before creating the next, so it never had two at once.
 *
 * linuxthreads places thread stacks at fixed addresses rather than wherever mmap likes, so "the
 * first one works and the second does not" is a shape this library can actually have.
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define N 6

static volatile int running;

static void *worker(void *a)
{
    (void)a;
    while (running) usleep(50000);   /* stay alive so the next create has company */
    return NULL;
}

int main(void)
{
    pthread_t t[N];
    int made = 0;

    running = 1;
    for (int i = 0; i < N; i++) {
        int rc = pthread_create(&t[i], NULL, worker, NULL);
        printf("thr2: create #%d rc=%d (%s), %d live\n", i + 1, rc,
               rc ? strerror(rc) : "ok", rc ? made : made + 1);
        fflush(stdout);
        if (rc != 0) break;
        made++;
        usleep(100000);
    }
    printf("thr2: %d concurrent threads\n", made); fflush(stdout);
    running = 0;
    for (int i = 0; i < made; i++) pthread_join(t[i], NULL);
    printf("thr2: joined all, done\n"); fflush(stdout);
    return 0;
}
