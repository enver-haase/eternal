/* sigprobe2 -- sigprobe said "got 0" and once "got 197755120", a value suspiciously close to the
 * loop counter the interrupted code was holding. Two very different faults produce that:
 *   (a) the handler never runs, and `got` keeps whatever was there, or
 *   (b) the handler runs but receives the interrupted register instead of the signal number.
 * So count invocations separately from the argument: `hits` is written with a CONSTANT, so it
 * cannot be confused with a stray register value, and `lastsig` records the argument as passed.
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static volatile sig_atomic_t hits, lastsig, ra_seen;

static void handler(int sig)
{
    hits = 0x5A5A;            /* a constant: proves the handler body ran */
    lastsig = sig;            /* the argument, as the kernel passed it */
}

#define SHOW(what) do { \
    printf("sigprobe2: %-22s hits=0x%x lastsig=%d\n", what, (unsigned)hits, (int)lastsig); \
    fflush(stdout); } while (0)

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    printf("sigprobe2: pid %d  (SIGUSR1=%d SIGUSR2=%d SIGALRM=%d)\n",
           (int)getpid(), SIGUSR1, SIGUSR2, SIGALRM);
    fflush(stdout);

    hits = 0; lastsig = -1;
    kill(getpid(), SIGUSR1);
    SHOW("kill self SIGUSR1");

    hits = 0; lastsig = -1;
    kill(getpid(), SIGUSR2);          /* a different number: is the argument merely stale? */
    SHOW("kill self SIGUSR2");

    hits = 0; lastsig = -1;
    alarm(1);
    for (long i = 0; i < 60000000L && !hits; i++)
        ;
    SHOW("alarm + user spin");

    hits = 0; lastsig = -1;
    alarm(1);
    pause();
    SHOW("alarm + pause");

    printf("sigprobe2: done\n"); fflush(stdout);
    return 0;
}
