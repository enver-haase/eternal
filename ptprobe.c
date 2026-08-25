/* ptprobe -- which pthread primitive hangs on this guest?
 *
 * pthread_create and a mutex-protected counter work (pthreadtest). SDL_Init hangs with threads
 * enabled, and SDL builds its threads on semaphores and condition variables, both of which lean on
 * linuxthreads' signal-based suspend/restart. This walks the primitives one at a time.
 */
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <unistd.h>

#define STEP(msg) do { printf("ptprobe: " msg "\n"); fflush(stdout); } while (0)

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv  = PTHREAD_COND_INITIALIZER;
static sem_t sem;
static int ready;

static void *cond_waiter(void *arg)
{
    pthread_mutex_lock(&mtx);
    while (!ready)
        pthread_cond_wait(&cv, &mtx);
    pthread_mutex_unlock(&mtx);
    STEP("  waiter: woke from cond_wait");
    return NULL;
}

static void *sem_waiter(void *arg)
{
    sem_wait(&sem);
    STEP("  waiter: returned from sem_wait");
    return NULL;
}

int main(void)
{
    pthread_t t;

    STEP("static mutex lock/unlock...");
    pthread_mutex_lock(&mtx);
    pthread_mutex_unlock(&mtx);
    STEP("ok. sem_init/post/wait in one thread...");
    sem_init(&sem, 0, 0);
    sem_post(&sem);
    sem_wait(&sem);
    STEP("ok. thread + condvar...");
    pthread_create(&t, NULL, cond_waiter, NULL);
    sleep(1);
    pthread_mutex_lock(&mtx);
    ready = 1;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mtx);
    pthread_join(t, NULL);
    STEP("ok. thread + semaphore...");
    pthread_create(&t, NULL, sem_waiter, NULL);
    sleep(1);
    sem_post(&sem);
    pthread_join(t, NULL);
    STEP("ok. all primitives work");
    return 0;
}
