//
// SFX backend for the lunatix Subleq VM.
//
// Classic Linux DOOM mixed its sound effects in software and wrote S16 stereo
// straight to the OSS /dev/dsp device. We do exactly that: the VM's sound card
// (Layer 1) exposes /dev/dsp via the kernel driver (Layer 2), plays the PCM
// stream at native speed on the host, so all the guest does is the 8-channel
// mix DOOM already knew how to do. Music is handled separately (i_snd_music.c);
// the OPL synth lives in the VM, not here.
//
// Derived from id Software's linuxdoom-1.10 i_sound.c (DOOM Source License).
// Pitch shifting is dropped (fixed step) so we need no libm at init.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/soundcard.h>

#include "z_zone.h"
#include "i_system.h"
#include "w_wad.h"
#include "doomdef.h"
#include "doomstat.h"
#include "sounds.h"
#include "i_sound.h"

// The number of internal mixing channels,
//  the samples calculated for each mixing step,
//  the size of the 16bit, 2 hardware channel (stereo) mixing buffer,
//  and the samplerate of the raw data.
#define SAMPLECOUNT		512
#define NUM_CHANNELS		8
#define SAMPLERATE		11025	// Hz
#define SAMPLESIZE		2	// 16bit

// /dev/dsp file descriptor; -1 means sound is disabled (open failed).
static int audio_fd = -1;

// The actual lengths of all sound effects.
static int		lengths[NUMSFX];

// The global mixing buffer: SAMPLECOUNT stereo frames, interleaved L,R.
static signed short	mixbuffer[SAMPLECOUNT * 2];

// The channel data pointers, start and end.
static unsigned char*	channels[NUM_CHANNELS];
static unsigned char*	channelsend[NUM_CHANNELS];

// Time/gametic that the channel started playing, used to determine oldest.
static int		channelstart[NUM_CHANNELS];

// The sound in channel handles, determined on registration, ORs cnt with id.
static int		channelhandles[NUM_CHANNELS];

// SFX id of the playing sound effect. Used to catch duplicates (like chainsaw).
static int		channelids[NUM_CHANNELS];

// Volume lookups: vol_lookup[vol*256 + sample] maps an 8bit unsigned DMX sample
// (centred on 128) and a 0..127 volume to a signed 16bit contribution.
static int		vol_lookup[128 * 256];

// Hardware left and right channel volume lookup, per channel.
static int*		channelleftvol_lookup[NUM_CHANNELS];
static int*		channelrightvol_lookup[NUM_CHANNELS];

// Monotonically increasing handle counter (never 0 == "not playing").
static int		handlenums = 0;

//
// Load and pad a DMX sound lump.
//
// DOOM's DS* lumps are 8bit unsigned mono. The first 8 bytes are a header
// (uint16 format=3, uint16 samplerate, uint32 numsamples); we skip it and pad
// the tail up to a SAMPLECOUNT boundary with 128 (== silence) so the mixer can
// read a whole block past the true end without special-casing the last frame.
//
static void* getsfx(char* sfxname, int* len)
{
    unsigned char*	sfx;
    unsigned char*	paddedsfx;
    int			i;
    int			size;
    int			paddedsize;
    char		name[20];
    int			sfxlump;

    sprintf(name, "ds%s", sfxname);

    if (W_CheckNumForName(name) == -1)
	sfxlump = W_GetNumForName("dspistol");
    else
	sfxlump = W_GetNumForName(name);

    size = W_LumpLength(sfxlump);

    sfx = (unsigned char*)W_CacheLumpNum(sfxlump, PU_STATIC);

    // Pad the (size-8) sample bytes up to a whole number of mixing blocks.
    paddedsize = ((size - 8 + (SAMPLECOUNT - 1)) / SAMPLECOUNT) * SAMPLECOUNT;

    paddedsfx = (unsigned char*)Z_Malloc(paddedsize + 8, PU_STATIC, 0);

    memcpy(paddedsfx, sfx, size);
    for (i = size; i < paddedsize + 8; i++)
	paddedsfx[i] = 128;

    Z_Free(sfx);

    *len = paddedsize;

    // Skip the 8-byte header; the mixer wants the raw samples.
    return (void*)(paddedsfx + 8);
}

//
// Start a sound in a free (or the oldest) channel and return its handle.
//
static int addsfx(int sfxid, int volume, int seperation)
{
    int		i;
    int		rc = -1;
    int		oldest = gametic;
    int		oldestnum = 0;
    int		slot;
    int		rightvol;
    int		leftvol;

    // Chainsaw troubles: unless the sound is looped, avoid two copies of a
    // "singular" sound stacking up — reuse the channel already playing it.
    if (sfxid == sfx_sawup
	|| sfxid == sfx_sawidl
	|| sfxid == sfx_sawful
	|| sfxid == sfx_sawhit
	|| sfxid == sfx_stnmov
	|| sfxid == sfx_pistol)
    {
	for (i = 0; i < NUM_CHANNELS; i++)
	{
	    if (channels[i] && channelids[i] == sfxid)
	    {
		channels[i] = 0;	// stop the current one
		break;
	    }
	}
    }

    // Find a free channel, otherwise the one that started longest ago.
    for (i = 0; (i < NUM_CHANNELS) && channels[i]; i++)
    {
	if (channelstart[i] < oldest)
	{
	    oldestnum = i;
	    oldest = channelstart[i];
	}
    }

    slot = (i == NUM_CHANNELS) ? oldestnum : i;

    // Set pointers to raw sample data start and end (no pitch shifting).
    channels[slot] = (unsigned char*)S_sfx[sfxid].data;
    channelsend[slot] = channels[slot] + lengths[sfxid];

    // Assign a handle; keep it non-zero and roughly unique.
    if (++handlenums == 0)
	handlenums = 1;
    rc = handlenums;
    channelhandles[slot] = rc;

    channelstart[slot] = gametic;
    channelids[slot] = sfxid;

    // Separation (stereo pan) 0..255, plus distance-attenuated volume 0..127.
    // Same quadratic pan curve as linuxdoom.
    seperation += 1;
    leftvol = volume - ((volume * seperation * seperation) >> 16);
    seperation = seperation - 257;
    rightvol = volume - ((volume * seperation * seperation) >> 16);

    if (rightvol < 0 || rightvol > 127)
	I_Error("rightvol out of bounds");
    if (leftvol < 0 || leftvol > 127)
	I_Error("leftvol out of bounds");

    channelleftvol_lookup[slot] = &vol_lookup[leftvol * 256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol * 256];

    return rc;
}

//
// SFX API
//

int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    if (audio_fd < 0)
	return -1;

    (void)pitch;
    (void)priority;

    return addsfx(id, vol, sep);
}

void I_StopSound(int handle)
{
    int i;
    for (i = 0; i < NUM_CHANNELS; i++)
    {
	if (channelhandles[i] == handle)
	{
	    channels[i] = 0;
	    channelhandles[i] = 0;
	    return;
	}
    }
}

int I_SoundIsPlaying(int handle)
{
    int i;
    for (i = 0; i < NUM_CHANNELS; i++)
    {
	if (channelhandles[i] == handle && channels[i])
	    return 1;
    }
    return 0;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    int		i;
    int		leftvol;
    int		rightvol;

    (void)pitch;

    for (i = 0; i < NUM_CHANNELS; i++)
    {
	if (channelhandles[i] != handle || !channels[i])
	    continue;

	sep += 1;
	leftvol = vol - ((vol * sep * sep) >> 16);
	sep = sep - 257;
	rightvol = vol - ((vol * sep * sep) >> 16);

	if (leftvol < 0) leftvol = 0; else if (leftvol > 127) leftvol = 127;
	if (rightvol < 0) rightvol = 0; else if (rightvol > 127) rightvol = 127;

	channelleftvol_lookup[i] = &vol_lookup[leftvol * 256];
	channelrightvol_lookup[i] = &vol_lookup[rightvol * 256];
	return;
    }
}

//
// Mix the active channels into mixbuffer (S16 interleaved stereo).
//
void I_UpdateSound(void)
{
    signed short*	leftout;
    signed short*	rightout;
    signed short*	leftend;
    int			step = 2;	// stride to next frame in the buffer
    int			chan;
    int			dl;
    int			dr;

    if (audio_fd < 0)
	return;

    leftout = mixbuffer;
    rightout = mixbuffer + 1;
    leftend = mixbuffer + SAMPLECOUNT * step;

    while (leftout != leftend)
    {
	dl = 0;
	dr = 0;

	for (chan = 0; chan < NUM_CHANNELS; chan++)
	{
	    if (!channels[chan])
		continue;

	    // Add the (volume-scaled) current sample from this channel.
	    dl += channelleftvol_lookup[chan][*channels[chan]];
	    dr += channelrightvol_lookup[chan][*channels[chan]];

	    // Advance one sample (native rate, no pitch shift).
	    channels[chan]++;

	    if (channels[chan] >= channelsend[chan])
		channels[chan] = 0;
	}

	// Clamp to signed 16bit and store.
	if (dl > 0x7fff)	  *leftout = 0x7fff;
	else if (dl < -0x8000)	  *leftout = -0x8000;
	else			  *leftout = (signed short)dl;

	if (dr > 0x7fff)	  *rightout = 0x7fff;
	else if (dr < -0x8000)	  *rightout = -0x8000;
	else			  *rightout = (signed short)dr;

	leftout += step;
	rightout += step;
    }
}

//
// Push the mixed block to /dev/dsp. The VM plays it back at native speed.
//
void I_SubmitSound(void)
{
    if (audio_fd < 0)
	return;

    (void)!write(audio_fd, mixbuffer, sizeof(mixbuffer));
}

void I_SetChannels(void)
{
    int		i;
    int		j;

    // Build the volume lookup: for each volume 0..127 and each 8bit sample,
    // map to a signed contribution centred on 0.
    for (i = 0; i < 128; i++)
	for (j = 0; j < 256; j++)
	    vol_lookup[i * 256 + j] = (i * (j - 128) * 256) / 127;

    for (i = 0; i < NUM_CHANNELS; i++)
    {
	channels[i] = 0;
	channelsend[i] = 0;
	channelhandles[i] = 0;
	channelids[i] = -1;
	channelstart[i] = 0;
    }
}

void I_InitSound(void)
{
    int		i;
    int		fmt;
    int		stereo;
    int		speed;

    audio_fd = open("/dev/dsp", O_WRONLY);
    if (audio_fd < 0)
    {
	printf("I_InitSound: /dev/dsp unavailable (%s); sound disabled\n",
	       strerror(errno));
	return;
    }

    fmt = AFMT_S16_LE;
    ioctl(audio_fd, SNDCTL_DSP_SETFMT, &fmt);
    stereo = 1;
    ioctl(audio_fd, SNDCTL_DSP_STEREO, &stereo);
    speed = SAMPLERATE;
    ioctl(audio_fd, SNDCTL_DSP_SPEED, &speed);

    I_SetChannels();

    // Precache every sound effect (chase link fields for aliases).
    for (i = 1; i < NUMSFX; i++)
    {
	if (!S_sfx[i].link)
	{
	    S_sfx[i].data = getsfx(S_sfx[i].name, &lengths[i]);
	}
	else
	{
	    S_sfx[i].data = S_sfx[i].link->data;
	    lengths[i] = lengths[(int)(S_sfx[i].link - S_sfx)];
	}
    }

    printf("I_InitSound: %d sound effects precached, /dev/dsp @ %d Hz\n",
	   NUMSFX - 1, speed);
}

void I_ShutdownSound(void)
{
    if (audio_fd >= 0)
    {
	close(audio_fd);
	audio_fd = -1;
    }
}
