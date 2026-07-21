/*
 * sndtest — minimal guest sound-device exerciser for the lunatix VM.
 *
 * Runs as PID 1 to verify the Layer 2 kernel sound driver end-to-end without
 * DOOM: writes a fixed 689 Hz tone to /dev/dsp (PCM sink) and keys a 440 Hz
 * note on /dev/opl (OPL3 port) using the VM's known-good channel-0 patch, then
 * idles forever (PID 1 must not exit). With LUNATIX_SOUND_CAPTURE set on the VM
 * the resulting <prefix>-pcm.wav / <prefix>-opl.wav can be analyzed on the host.
 */

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <linux/soundcard.h>

/* One period of a sine in 16 samples (amp ~12000): tone = 11025/16 = 689 Hz. */
static const int16_t sine16[16] = {
    0, 4592, 8485, 11087, 12000, 11087, 8485, 4592,
    0, -4592, -8485, -11087, -12000, -11087, -8485, -4592
};

int main(void)
{
    /* The initramfs is the rootfs, so the kernel does not auto-mount devtmpfs;
     * mount it ourselves so the driver's /dev/dsp and /dev/opl nodes exist with
     * their real (dynamic) minors, regardless of any static nodes. */
    mkdir("/dev", 0755);
    mount("dev", "/dev", "devtmpfs", 0, NULL);

    /* --- OPL: program the VM's known-good 2-op patch and key a 440 Hz note. --- */
    int opl = open("/dev/opl", O_WRONLY);
    if (opl >= 0) {
        /* Packed (reg<<8)|val. Patch mirrors src/vm.c soundtest(); note-on at
         * 440 Hz => block 4, F-num 580: 0xA0=0x44, 0xB0=0x20|(4<<2)|2=0x32. */
        static const uint32_t regs[] = {
            0x10501,                        /* OPL3 NEW=1                     */
            0x2021, 0x401A, 0x60F2, 0x8025, /* modulator: EGT=1 (sustaining)  */
            0x2321, 0x4300, 0x63F2, 0x8315, /* carrier:   EGT=1 (sustaining)  */
            0xC03E,                         /* L+R, feedback, FM              */
            0xA044, 0xB032,                 /* F-num low; key-on, block, hi   */
        };
        for (unsigned i = 0; i < sizeof regs / sizeof regs[0]; i++)
            (void)!write(opl, &regs[i], 4);
    }

    /* --- PCM: 689 Hz tone to /dev/dsp. --- */
    int dsp = open("/dev/dsp", O_WRONLY);
    if (dsp >= 0) {
        int fmt = AFMT_S16_LE, stereo = 1, speed = 11025;
        ioctl(dsp, SNDCTL_DSP_SETFMT, &fmt);
        ioctl(dsp, SNDCTL_DSP_STEREO, &stereo);
        ioctl(dsp, SNDCTL_DSP_SPEED, &speed);

        int16_t block[512 * 2];
        for (int i = 0; i < 512; i++) {
            int16_t s = sine16[i & 15];
            block[2 * i] = s;
            block[2 * i + 1] = s;
        }
        /* ~4 s of tone; best-effort (VM drops oldest if we outrun its drain). */
        for (int b = 0; b < 172; b++)
            (void)!write(dsp, block, sizeof block);
    }

    /* PID 1 must never return. Idle; the OPL note keeps sounding (the VM keeps
     * generating it), so the capture accumulates the sustained tone. */
    for (;;)
        pause();
    return 0;
}
