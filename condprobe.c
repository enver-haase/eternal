/* condprobe -- the smallest possible pthread_cond_wait, with a print between every step, so the
 * hang can be attributed to one line rather than to "condvars".
 *
 * linuxthreads suspends a waiter in sigsuspend and wakes it with a restart signal, so this is
 * really a signal test wearing a pthread hat: if the waiter never comes back, the question is
 * whether the signal was sent, whether it was delivered, and whether the handler returned.
 */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv  = PTHREAD_COND_INITIALIZER;
static volatile int ready;

#define SAY(...) do { printf("condprobe: " __VA_ARGS__); putchar('\n'); fflush(stdout); } while (0)

static void *waiter(void *arg)
{
    (void)arg;
    SAY("waiter: alive, locking");
    pthread_mutex_lock(&mtx);
    SAY("waiter: locked, ready=%d", ready);
    while (!ready) {
        SAY("waiter: entering cond_wait");
        pthread_cond_wait(&cv, &mtx);
        SAY("waiter: RETURNED from cond_wait, ready=%d", ready);
    }
    pthread_mutex_unlock(&mtx);
    SAY("waiter: unlocked, returning");
    return NULL;
}

int main(void)
{
    pthread_t t;

    SAY("main: creating waiter");
    if (pthread_create(&t, NULL, waiter, NULL) != 0) { SAY("main: create FAILED"); return 1; }
    SAY("main: created, sleeping 2s");
    sleep(2);
    SAY("main: locking");
    pthread_mutex_lock(&mtx);
    ready = 1;
    SAY("main: signalling");
    pthread_cond_signal(&cv);
    SAY("main: signalled, unlocking");
    pthread_mutex_unlock(&mtx);
    SAY("main: joining");
    pthread_join(t, NULL);
    SAY("main: joined -- condvars work");
    return 0;
}
