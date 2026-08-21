/* memtest: verify demand-paged BSS/heap writes persist on the MMU port.
 * Writes a sentinel across a multi-page BSS array, reads it back, and reports
 * the count of mismatches via the exit code so the kernel panic banner shows it:
 *   _exit(7)  => all writes persisted (no bug)
 *   _exit(42) => mismatches found (anon-page write-persistence / COW bug)
 * Also exercises a second pass to catch stale-copy-on-reread.
 */
#include <unistd.h>

#define N (1 << 20)          /* 1 MiB => ~256 pages */
static unsigned char buf[N];

static unsigned char val(unsigned i) { return (unsigned char)(i * 7u + 1u); }

int main(void)
{
    unsigned i;
    unsigned bad = 0;

    for (i = 0; i < N; i++)
        buf[i] = val(i);

    for (i = 0; i < N; i++)
        if (buf[i] != val(i))
            bad++;

    /* second read pass (catches a stale copy surfacing on re-read) */
    for (i = 0; i < N; i++)
        if (buf[i] != val(i))
            bad++;

    _exit(bad ? 42 : 7);
}
