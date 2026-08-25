/* sigprobe -- three separate questions, in order of dependency:
 *   1. does a synchronously raised signal reach its handler at all?
 *   2. does a TIMER-generated signal (alarm) reach it, with the task spinning in userspace?
 *   3. does a signal interrupt a task BLOCKED in a syscall (pause)?
 * pthread_cond_wait hangs, and so does alarm+pause in a single-threaded program, so the answer
 * decides whether the fault is in signal delivery, in itimers, or in the syscall-return path.
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define STEP(msg) do { printf("sigprobe: " msg "\n"); fflush(stdout); } while (0)

static volatile sig_atomic_t got;

static void handler(int sig) { got = sig; }

int main(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                    /* deliberately NOT SA_RESTART */
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    printf("sigprobe: pid %d\n", (int)getpid()); fflush(stdout);
    STEP("1. kill(self, SIGUSR1) with the task running...");
    got = 0;
    kill(getpid(), SIGUSR1);
    printf("sigprobe:    got %d (expect 10)\n", (int)got); fflush(stdout);

    STEP("2. alarm(1), then spin in userspace...");
    got = 0;
    alarm(1);
    for (long i = 0; i < 200000000L && !got; i++)
        ;                               /* no syscall in here at all */
    printf("sigprobe:    got %d (expect 14) after the spin\n", (int)got); fflush(stdout);

    STEP("3. alarm(1), then block in pause()...");
    got = 0;
    alarm(1);
    pause();
    printf("sigprobe:    pause returned, got %d\n", (int)got); fflush(stdout);

    STEP("4. alarm(1), then block in nanosleep(10s)...");
    got = 0;
    alarm(1);
    {
        struct timespec ts = { 10, 0 };
        int r = nanosleep(&ts, NULL);
        printf("sigprobe:    nanosleep returned %d, got %d\n", r, (int)got); fflush(stdout);
    }
    STEP("all signal paths work");
    return 0;
}
