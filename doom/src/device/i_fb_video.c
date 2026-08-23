#include "i_video.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include "v_video.h"

static FILE* fbfd = 0;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static long int screensize = 0;
static char *fbp = 0;
static int fb_stride_words = 0;  // framebuffer stride in uint32_t words


void I_InitGraphics (void)
{
    /* Open the file for reading and writing */
    fbfd = open("/dev/fb0", O_RDWR);
    if (!fbfd) {
            printf("Error: cannot open framebuffer device.\n");
            exit(1);
    }
    printf("The framebuffer device was opened successfully.\n");

    /* Get fixed screen information */
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        printf("Error reading fixed information.\n");
            exit(2);
    }

    /* Get variable screen information */
        if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
                printf("Error reading variable information.\n");
                exit(3);
        }

    /* Ask for our own resolution. The console runs big; the game renders 320x200 and lets the
     * host scale it, which costs the guest nothing -- as opposed to the 2x doubling into a
     * larger buffer this backend used to do, which cost 256000 pixel writes a frame. If the
     * driver will not switch, fall through and use whatever mode is set. */
    vinfo.xres = SCREENWIDTH;
    vinfo.yres = SCREENHEIGHT;
    vinfo.xres_virtual = SCREENWIDTH;
    vinfo.yres_virtual = SCREENHEIGHT;
    vinfo.bits_per_pixel = 32;
    if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo))
        printf("Warning: cannot set %dx%d; using the current mode\n", SCREENWIDTH, SCREENHEIGHT);
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        printf("Error re-reading variable information.\n");
        exit(3);
    }
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        printf("Error re-reading fixed information.\n");
        exit(2);
    }

    /* Figure out the size of the screen in bytes */
    screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
    printf("Screen size is %d\n",screensize);
    printf("Vinfo.bpp = %d\n",vinfo.bits_per_pixel);

    /* Map the device to memory */
    fbp = (char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED,fbfd, 0);
    if ((int64_t)fbp == -1) {
            printf("Error: failed to map framebuffer device to memory.\n");
            exit(4);
    }
    printf("The framebuffer device was mapped to memory successfully.\n");

    // Clear screen
    printf("\033[2J");

    // Graphics mode
    int tty_fd = open("/dev/tty0", O_RDWR);
    ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
    close(tty_fd);

    fb_stride_words = finfo.line_length / sizeof(uint32_t);
}


void I_ShutdownGraphics(void)
{
    munmap(fbp, screensize);

    /* The console mode is restored by the KERNEL when this fd closes (see subleqfb_release):
     * a game that crashes cannot be relied on to switch back, so it is not the game's job. */
    close(fbfd);

    // Text mode
    int tty_fd = open("/dev/tty0", O_RDWR);
    ioctl(tty_fd, KDSETMODE, KD_TEXT);
    close(tty_fd);

    // Clear screen
    printf("\033[2J");
}

void I_StartFrame (void)
{

}

// Palette array: palette index → XRGB8888.
// Used by non-colormapped pixel writes (V_DrawPatch, etc.).
uint32_t colors_raw[256];

// Takes full 8 bit values.
void I_SetPalette (byte* palette)
{
    byte r, g, b;
    // set the X colormap entries
    for (int i=0 ; i<256 ; i++)
    {
        r = gammatable[usegamma][*palette++];
        g = gammatable[usegamma][*palette++];
        b = gammatable[usegamma][*palette++];
        // Build XRGB8888 directly: 0x00RRGGBB
        colors_raw[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

    // Rebuild colormaps with XRGB values so render inner loops
    // write XRGB directly (no palette lookup needed in I_FinishUpdate).
    {
        extern lighttable_t* colormaps;
        extern int colormaps_length;
        extern byte* colormaps_raw;
        if (colormaps && colormaps_raw)
        {
            for (int i = 0; i < colormaps_length; i++)
                colormaps[i] = colors_raw[colormaps_raw[i]];
        }
    }
}

void I_UpdateNoBlit (void)
{

}

// I_FinishUpdate: straight 1:1 copy from screens[0] (XRGB) into the framebuffer, which is
// exactly SCREENWIDTH x SCREENHEIGHT. There is nothing to scale and nothing to centre: the
// host presents the frame at whatever size its window is. This replaced a 2x pixel-doubling
// blit into an 800x512 buffer, whose black border the guest also had to own -- four times the
// writes, for an image the host then scaled anyway.
void I_FinishUpdate (void)
{
    const uint32_t *src = (const uint32_t *)screens[0];
    uint32_t       *dst = (uint32_t *)fbp;
    int             n   = SCREENWIDTH * SCREENHEIGHT;
    int             i;

    // Word-at-a-time on purpose: the fb is contiguous at this resolution, and a word loop is
    // what the Subleq backend can actually do well.
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

void I_ReadScreen (uint32_t* scr)
{
    memcpy(scr, screens[0], SCREENWIDTH*SCREENHEIGHT*sizeof(uint32_t));
}
