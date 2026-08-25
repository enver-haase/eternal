/* spin2 -- where does the spin-loop flag go wrong?
 *
 * /sigprobe test 2 reports the loop counter where the signal number should be. The generated code
 * for the loop is correct (it counts down in r20/r21, reloads the volatile flag every pass, and
 * never stores to it), so the corruption happens at RUNTIME, around signal delivery -- and the
 * three candidates need separating:
 *
 *   1. the handler's store lands wrong,
 *   2. the interrupted context comes back wrong from sigreturn (the loop counter lives in r20,
 *      which is also the register the kernel puts a syscall return value in), or
 *   3. nothing is wrong with the flag at all and it is printf's variadic path that picks up a
 *      stale register.
 *
 * So: no varargs anywhere here (write() and a hand-rolled hex, both after the loop), the flag is
 * copied into a local the instant the loop ends, and a second constant-only witness says whether
 * the handler ran at all. Build at -O0/-O1/-O2 to bracket it like /pmf2.
 */
#include <signal.h>
#include <unistd.h>
#include <string.h>

static volatile sig_atomic_t flag;      /* first .bss object: the one that came back wrong */
static volatile sig_atomic_t witness;   /* second: it came back RIGHT in sigprobe2 */

static void handler(int sig)
{
    flag = sig;
    witness = 0x5A5A;
}

static void say(const char *label, long v)
{
    char buf[80];
    int  n = 0, i;

    while (*label) buf[n++] = *label++;
    buf[n++] = '0'; buf[n++] = 'x';
    for (i = 28; i >= 0; i -= 4) {
        unsigned d = (unsigned)((v >> i) & 0xF);
        buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    buf[n++] = '\n';
    write(1, buf, (size_t)n);
}

int main(void)
{
    long i;
    long flag_after, witness_after;

    signal(SIGALRM, handler);
    say("spin2: start, flag=", (long)flag);

    alarm(1);
    for (i = 0; i < 60000000L && !flag; i++)
        ;
    flag_after = (long)flag;            /* copy out immediately, before anything else runs */
    witness_after = (long)witness;

    say("spin2: counter  =", i);
    say("spin2: flag     =", flag_after);      /* expect 0xe (SIGALRM = 14) */
    say("spin2: witness  =", witness_after);   /* expect 0x5a5a */
    say("spin2: flag now =", (long)flag);      /* re-read: has it changed since? */
    return 0;
}
