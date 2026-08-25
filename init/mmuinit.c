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
#include <sys/mount.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <linux/fb.h>
#include <sys/ioctl.h>

/* asm-generic/ioctls.h value; not exposed by the sysroot headers we build against. */
#ifndef TIOCSCTTY
#define TIOCSCTTY 0x540E
#endif

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
static int run3(const char *path, const char *argv0, const char *a1, const char *a2);

static int run(const char *path, const char *argv0)
{
    return run3(path, argv0, NULL, NULL);
}

static int run3(const char *path, const char *argv0, const char *a1, const char *a2)
{
    int   status = 0;
    pid_t pid = fork();

    if (pid < 0) {
        printf("init: fork failed (errno %d)\n", errno);
        return -1;
    }
    if (pid == 0) {
        /*
         * Deliberately NOT a session leader with a controlling terminal. Giving children one
         * looked like the right thing -- SDL's framebuffer driver complains it cannot find "a
         * console terminal" without it -- and it is fatal: that driver then opens /dev/tty and
         * calls TIOCNOTTY to detach, which sends SIGHUP to the session's foreground group, i.e.
         * to itself, and the default action kills it. With no controlling terminal the same code
         * finds nothing to detach from and carries on. What SDL actually needs is /dev/tty1..4
         * to exist, so its VT_OPENQRY path finds a free virtual terminal to open.
         */
        execl(path, argv0, a1, a2, (char *)0);
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
    /*
     * Mount /proc. BusyBox's shell re-executes itself through /proc/self/exe -- for subshells
     * always, and for external commands too when it is built for NOMMU -- so without procfs the
     * shell answers "can't re-execute the shell" and running any program from the prompt simply
     * hangs. Builtins and in-process applets keep working, which makes it look like a problem
     * with the program rather than a missing filesystem.
     */
    /*
     * devtmpfs first: the sound devices are misc devices with dynamic minors, so no static node
     * can name them, and without /dev/dsp SDL reports "No available audio device". devtmpfs also
     * supplies console/null/zero/tty*/fb0, so nothing is lost by covering the static set.
     */
    if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) != 0)
        printf("init: cannot mount /dev (errno %d)\n", errno);

    if (mount("proc", "/proc", "proc", 0, NULL) != 0)
        printf("init: cannot mount /proc (errno %d)\n", errno);
    if (mount("sysfs", "/sys", "sysfs", 0, NULL) != 0)
        printf("init: cannot mount /sys (errno %d)\n", errno);

    /* So that typing `doom` at the prompt finds it: the game lives in /, not in /bin. */
    setenv("PATH", "/bin:/sbin:/", 1);
    setenv("HOME", "/root", 1);
    /* This machine has no mouse device, and SDL's framebuffer driver treats that as fatal unless
     * told otherwise -- it fails SDL_Init with "Unable to open mouse". Set for everything init
     * starts, since it is a property of the machine rather than of any one program. */
    setenv("SDL_NOMOUSE", "1", 1);

    /* No program is started automatically: the machine comes up at a prompt, and what runs is
     * the operator's choice -- doom / doom19 / udoom / doom2 for the game, mystery for Mystery
     * House. (There used to be an automatic DOOM run here to make the framebuffer release path
     * testable unattended; LUNATIX_KEYSCRIPT does that job now without the machine booting into
     * a game, which also stopped everything typed at the prompt from landing in DOOM.) */

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
