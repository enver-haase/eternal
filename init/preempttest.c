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
#include <sys/types.h>

/*
 * This uses libc fork(), which only exists because uClibc is now built with SUBLEQ_MMU=y
 * (before that the C library selected ARCH_HAS_NO_MMU and offered only vfork, whose
 * semantics -- child first, parent frozen -- cannot express two tasks running at once).
 * So a successful run of this test is also the proof that the MMU userspace is real.
 */

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
	pid_t p = fork();

	if (p == 0) {
		/* The runaway: no syscalls, ever. Only a timer can take the CPU from this. */
		for (;;)
			spin(1000000);
	}

	if (p < 0) {
		write(1, "\npreempttest: FAIL (fork failed)\n", 33);
		return 1;
	}

	write(1, "\npreempttest: forked; child spinning, parent should keep running\n", 65);
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
