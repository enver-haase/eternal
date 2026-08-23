/* pthreadtest -- do threads work on this machine at all?
 *
 * SDL 1.2 crashed inside uClibc's __pthread_lock during SDL_Init, dereferencing NULL. That points
 * at threads rather than at SDL, and threads are worth knowing about on their own: fork() and
 * execve() work now, but nothing here has ever created a thread. This asks the smallest possible
 * version of the question, so the answer is not entangled with a 6 MB library.
 */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static int counter;

static void *worker(void *arg)
{
    printf("pthreadtest: thread running\n");
    for (int i = 0; i < 1000; i++) {
        pthread_mutex_lock(&mtx);
        counter++;
        pthread_mutex_unlock(&mtx);
    }
    printf("pthreadtest: thread done, counter=%d\n", counter);
    return arg;
}

int main(void)
{
    pthread_t t;
    void *ret = NULL;

    printf("pthreadtest: static mutex lock/unlock...\n");
    pthread_mutex_lock(&mtx);
    pthread_mutex_unlock(&mtx);
    printf("pthreadtest: ok\n");

    printf("pthreadtest: pthread_create...\n");
    if (pthread_create(&t, NULL, worker, NULL) != 0) {
        printf("pthreadtest: pthread_create FAILED\n");
        return 1;
    }
    printf("pthreadtest: created; joining\n");
    pthread_join(t, &ret);
    printf("pthreadtest: joined, counter=%d\n", counter);
    return 0;
}
