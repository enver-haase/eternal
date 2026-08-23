/*
 * preempttest - acceptance test for user-mode preemption on the lunatix MMU machine.
 *
 * The child spins forever and never makes a syscall. If the machine cannot preempt a user
 * task, the scheduler never gets the CPU back once the child is running and the parent's
 * output stops dead. If preemption works, the parent keeps printing while the child spins,
 * which also exercises the per-task RTE resume PC: two user tasks are then going in and out
 * of the trap path in turn, which a single global resume PC would get wrong.
 *
 * Runs as init; see mmu_initramfs_preempt.txt.
 */
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>

/*
 * fork() does not exist in this userspace yet: the MMU sysroot's uClibc is still configured
 * ARCH_HAS_NO_MMU=y, so it offers only vfork (whose semantics -- child first, parent frozen --
 * cannot express "two tasks running at once"). The kernel implements clone perfectly well
 * (arch/subleq copy_thread), so call it directly: clone(SIGCHLD, 0, ...) with a NULL stack is
 * exactly what fork() is. Drop this shim once uClibc is rebuilt in MMU mode.
 */
static long fork_via_clone(void)
{
	return syscall(SYS_clone, (long)SIGCHLD, 0L, 0L, 0L, 0L);
}

static void spin(unsigned long n)
{
	volatile unsigned long x = 0;
	while (n--)
		x += n;
}

int main(void)
{
	char c;
	int i;
	long p = fork_via_clone();

	if (p == 0) {
		/* The runaway: no syscalls, ever. Only a timer can take the CPU from this. */
		for (;;)
			spin(1000000);
	}

	if (p < 0) {
		write(1, "\npreempttest: FAIL (clone failed)\n", 34);
		return 1;
	}

	write(1, "\npreempttest: child spinning; parent should keep running\n", 57);
	for (i = 0; i < 10; i++) {
		spin(300000);
		c = '.';
		write(1, &c, 1);
	}
	write(1, "\npreempttest: PASS - parent survived a syscall-free spinner\n", 60);

	/* Leave the spinner running: if the kernel is still in control the machine stays
	 * alive, and these extra rounds are the proof. */
	for (i = 0; i < 3; i++) {
		spin(300000);
		c = '+';
		write(1, &c, 1);
	}
	write(1, "\npreempttest: still scheduling\n", 31);
	return 0;
}
