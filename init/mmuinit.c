/* PID 1 for the MMU image: run the game, then keep a shell alive forever.
 *
 * DOOM used to BE init, so quitting the game killed init and the kernel panicked. The first
 * fix ran the game and then exec'd a shell -- which made the SHELL pid 1, so the moment it
 * ended (a stray command, Ctrl-D, anything) init was dead again and the machine panicked. Init
 * must therefore own the shell rather than become it: fork it, wait for it, start another.
 * That is what respawning means, and it is why a real init never execs its children.
 *
 * Neither of the obvious shortcuts works here:
 *   - a #!/bin/sh script as /init: arch/subleq halts the machine outright when binfmt_script
 *     re-execs the interpreter (the last thing printed is "Run /init as init process");
 *   - BusyBox as init: this BusyBox is built without the init applet ("init: applet not
 *     found"), and rebuilding it for one applet is a lot of build for a fork and a wait.
 *
 * It is also the most honest test of the MMU userspace: fork(), execve() and wait() are what
 * an MMU is for, and execve() from userspace did not work on this port until start_thread()
 * learned to set `ra` -- every exec'd image resumed at word 0, the NULL guard, and died before
 * its first instruction.
 */
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* Run a program as a child and wait for it. Returns its wait status, or -1 if it never ran. */
static int run(const char *path, const char *argv0)
{
    int   status = 0;
    pid_t pid = fork();

    if (pid < 0) {
        printf("init: fork failed (errno %d)\n", errno);
        return -1;
    }
    if (pid == 0) {
        execl(path, argv0, (char *)0);
        printf("init: cannot exec %s (errno %d)\n", path, errno);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0)
        ;
    return status;
}

int main(void)
{
    /* So that typing `doom` at the prompt finds it: the game lives in /, not in /bin. */
    setenv("PATH", "/bin:/sbin:/", 1);
    setenv("HOME", "/", 1);

    printf("init: starting doom\n");
    printf("init: doom exited (status 0x%x)\n", run("/doom", "doom"));

    /* From here on, a shell -- forever. Each time it ends, start another, so the machine stays
     * up whatever happens at the prompt. */
    for (;;) {
        int status = run("/bin/sh", "sh");

        if (status < 0) {
            printf("init: no shell could be started; idling\n");
            for (;;)
                pause();
        }
        printf("\ninit: shell exited (status 0x%x) -- starting another\n", status);
    }
}
