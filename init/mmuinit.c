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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <linux/fb.h>

/* linux/kd.h values; the uClibc sysroot does not carry the header. */
#define KDSETMODE 0x4B3A
#define KD_TEXT   0x00

/* The console's own resolution. Matches SUBLEQ_FB_MAX_WIDTH/HEIGHT in the kernel. */
#define CONSOLE_W 1280
#define CONSOLE_H 960


/*
 * Put the console back into text mode. A graphical program sets KD_GRAPHICS so fbcon stops
 * drawing over it, and restores KD_TEXT on its way out -- but only if it gets a way out. Killed
 * or crashed, it leaves the console mute: the last frame stays on screen and everything after
 * it, including a kernel panic, is written to a console nobody is drawing. That is exactly what
 * a "standstill at 100% CPU" looks like from outside. Init owns the console, so init restores
 * it, every time a child ends.
 */
static void console_to_text(void)
{
    struct fb_var_screeninfo var;
    int fd = open("/dev/tty0", O_RDWR);

    if (fd >= 0) {
        (void)ioctl(fd, KDSETMODE, KD_TEXT);
        close(fd);
    }

    /*
     * And put the console's resolution back. The framebuffer driver tries to do this itself when
     * the last handle on /dev/fb0 closes, which is the right place for it -- but that hook never
     * fires on this port: mmap holds a reference on the file, and the unmap path does not release
     * it (the same breakage the "non-zero pgtables_bytes on freeing mm" BUG reports). So init
     * does it, which at least is not the game: it happens after ANY child ends, including one
     * that crashed halfway through a frame.
     */
    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0)
        return;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0 &&
        (var.xres != CONSOLE_W || var.yres != CONSOLE_H)) {
        var.xres = var.xres_virtual = CONSOLE_W;
        var.yres = var.yres_virtual = CONSOLE_H;
        var.bits_per_pixel = 32;
        var.activate = FB_ACTIVATE_NOW;
        if (ioctl(fd, FBIOPUT_VSCREENINFO, &var) != 0)
            printf("init: could not restore the console mode (errno %d)\n", errno);
    }
    close(fd);
}

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
    console_to_text();
    return status;
}

int main(void)
{
    /* So that typing `doom` at the prompt finds it: the game lives in /, not in /bin. */
    setenv("PATH", "/bin:/sbin:/", 1);
    setenv("HOME", "/root", 1);

    printf("init: starting doom\n");
    printf("init: doom exited (status 0x%x)\n", run("/bin/doom", "doom"));

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
