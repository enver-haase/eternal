//
// Music backend for the lunatix Subleq VM: plays DOOM MUS lumps on the VM's
// OPL3 chip by translating them to OPL register writes on /dev/opl.
//
// Classic Linux DOOM shipped no working music (the DOS DMX library was
// proprietary and left out of the source release). This restores it the way
// chocolate-doom's OPL player does — using the WAD's GENMIDI instrument bank —
// but the FM synthesis itself runs in the VM (Layer 1), so the guest only does
// the cheap MUS -> register translation.
//
// Nine 2-operator melodic voices (OPL2-compatible layout, OPL3 stereo enabled).
// MUS is parsed directly (it is simpler than MIDI and is what I_RegisterSong
// receives). Driven by I_UpdateMusic(), called once per game loop.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

#include "z_zone.h"
#include "i_system.h"
#include "w_wad.h"
#include "doomdef.h"
#include "doomstat.h"
#include "i_sound.h"

typedef unsigned char u8;

// ------------------------------------------------------------------ OPL access

static int opl_fd = -1;

// Milliseconds from "now" at which the writes we are currently emitting are
// meant to take effect. The sequencer runs AHEAD of real time (see
// I_UpdateMusic) and stamps every register write with its intended moment; the
// VM's sound card holds the write and applies it at that sample position.
//
// This is what makes the music keep time on a machine this slow. The game loop
// here iterates only ~7 times a second, so without a timestamp every event in
// a 140 ms span lands on whichever loop iteration flushed it, and a score
// written on a 250 ms grid comes out with +-100 ms of jitter. The 15 spare bits
// of the /dev/opl word carry the offset, so a VM that ignores them (or an older
// one) still plays everything immediately, exactly as before.
static unsigned mus_sched_dt;   // 0 = apply immediately

// Register 0x1FF is not a real OPL register; the VM reads it as "drop every
// write still queued for the future". Needed because we now schedule over a
// second of music ahead: without it, stopping a song would leak the tail of it
// into the next one.
#define OPL_CMD_FLUSH 0x1FF

static void opl(int reg, int val)
{
    if (opl_fd < 0)
        return;
    unsigned int dt = mus_sched_dt > 0x7FFF ? 0x7FFF : mus_sched_dt;
    unsigned int packed = (unsigned int)((dt << 17)
                                         | ((reg & 0x1FF) << 8) | (val & 0xFF));
    (void)!write(opl_fd, &packed, 4);
}

static void opl_flush(void)
{
    unsigned save = mus_sched_dt;
    mus_sched_dt = 0;                       // the command itself is immediate
    opl(OPL_CMD_FLUSH, 0);
    mus_sched_dt = save;
}

// An OPL3 has eighteen 2-operator voices: nine per bank, the second bank
// addressed by OR-ing 0x100 into the register. We use all of them — E1M1 alone
// reaches ten simultaneous notes, and double-voice instruments take two voices
// each, so nine would steal voices from a piece that has not finished with them.
#define NUM_VOICES 18
#define VOICE_BANK(v)  ((v) >= 9 ? 0x100 : 0x000)
#define VOICE_CH(v)    ((v) % 9)

// Modulator/carrier operator register offsets for the 9 melodic channels.
static const u8 op_off[9][2] = {
    {0x00, 0x03}, {0x01, 0x04}, {0x02, 0x05},
    {0x08, 0x0B}, {0x09, 0x0C}, {0x0A, 0x0D},
    {0x10, 0x13}, {0x11, 0x14}, {0x12, 0x15},
};

// F-number per semitone; block = clamp(note/12 - 1, 0, 7). Gives exact pitches
// at the OPL3 native 49716 Hz (see the table derivation in the commit).
static const unsigned short note_fnum[12] = {
    0x159, 0x16D, 0x183, 0x19A, 0x1B3, 0x1CC,
    0x1E8, 0x205, 0x223, 0x244, 0x267, 0x28B,
};

// --------------------------------------------------------------- GENMIDI bank

// The GENMIDI lump: 8-byte header "#OPL_II#", then 175 * 36-byte instruments.
// We read fields by byte offset to avoid any packed-struct/alignment surprises
// on the Subleq backend. Instrument layout (36 bytes):
//   +0  u16 flags        (bit0 = fixed pitch, bit2 = double voice)
//   +2  u8  fine tuning
//   +3  u8  fixed note
//   +4  voice 0 (16 bytes), +20 voice 1 (16 bytes)
// Voice layout (16 bytes): modulator op (6), feedback (1), carrier op (6),
//   unused (1), s16 note offset (2). Operator (6 bytes): tremolo, attack,
//   sustain, waveform, key-scale, level.
#define GENMIDI_NUM      175
#define INSTR_SIZE       36
#define GENMIDI_FLAG_FIXED  0x01
#define GENMIDI_FLAG_DOUBLE 0x04   // instrument plays as two detuned voices

static const u8 *genmidi;   // -> first instrument (past the 8-byte header)

static const u8 *instr_ptr(int i)
{
    if (i < 0) i = 0;
    if (i >= GENMIDI_NUM) i = GENMIDI_NUM - 1;
    return genmidi + i * INSTR_SIZE;
}

// A voice within an instrument (voice 0 used); returns pointer to its 16 bytes.
static const u8 *instr_voice(const u8 *instr)
{
    return instr + 4;
}

// ---------------------------------------------------------------- voice state

typedef struct {
    int      used;      // currently keyed on
    int      midi_ch;   // MUS channel that owns it
    int      note;      // MUS note playing
    unsigned age;       // allocation order, for oldest-steal
    u8       b0;        // last 0xB0 value (block+fnum-hi), for clean key-off
} oplvoice_t;

static oplvoice_t voices[NUM_VOICES];
static unsigned   voice_clock = 0;

// --------------------------------------------------------------- MUS channels

typedef struct {
    int instrument;     // GENMIDI index (program)
    int volume;         // 0..127
} muschan_t;

static muschan_t chans[16];

static int music_volume = 15;   // 0..15 (I_SetMusicVolume)

// -------------------------------------------------------------- MUS sequencer

static const u8 *song_base;     // start of the cached lump
static const u8 *mus_pos;       // current read position
static const u8 *mus_end;       // end of score
static const u8 *mus_loop;      // score start, for looping
static int       mus_playing;
static int       mus_paused;
static int       mus_looping;
static int       mus_delay;     // ticks remaining before next event group

// MUS score tick rate is 140 Hz. The sequencer keeps its own millisecond clock
// and runs AHEAD of real time by MUS_LOOKAHEAD_MS, stamping each register write
// with how far in the future it belongs (see opl() above). Tempo therefore comes
// from the wall clock and not from how often the render loop happens to call us,
// which on this VM is only ~7 Hz.
#define MUS_TICKS_PER_SEC     140
#define MUS_LOOKAHEAD_MS     3000   // see below: must outlast a level load
#define MUS_RESYNC_MS        3000   // stall longer than this: drop the backlog

// The lookahead is deliberately far longer than one game-loop period. This
// machine stops the world for over a second when DOOM loads a level, and the
// sequencer cannot run while it does; with only a couple of hundred milliseconds
// queued, the music fell silent for the whole load and the notes due in it were
// discarded by the resync — an audible hole with the last chord ringing out.
// Measured: the load stall at the demo transition silences the guest for about
// 2.9 s, and a 1.2 s queue left a 1.7 s hole in the music. Three seconds covers
// it, so the VM already holds those writes and keeps placing them on time while
// the guest is busy. The cost is that anything
// which changes the music has to flush what is queued (see opl_flush).

static unsigned mus_base_ms;        // wall-clock time of tick 0
static unsigned mus_ticks;          // song position, in MUS ticks since the base
static int      mus_clock_running;

// Wall clock in milliseconds. gettimeofday() reads the VM's RTC, the same clock
// I_GetTime uses, so this is real time even though the guest is far from it.
static unsigned mus_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (unsigned)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}

// Wall-clock time of a tick. 1000/140 ms per tick, kept in integers as 100/14.
static unsigned mus_tick_ms(unsigned tick)
{
    return mus_base_ms + (tick * 100u) / 14u;
}

static void mus_clock_reset(void)
{
    mus_base_ms = mus_now_ms();
    mus_ticks = 0;
    mus_clock_running = 1;
    mus_sched_dt = 0;
}

// ------------------------------------------------------------ OPL programming

static void opl_program_voice(int v, const u8 *voice)
{
    int bank = VOICE_BANK(v);
    int mo = op_off[VOICE_CH(v)][0], co = op_off[VOICE_CH(v)][1];
    const u8 *mod = voice;
    const u8 *car = voice + 7;
    int fb = voice[6];

    opl(bank | (0x20 + mo), mod[0]); opl(bank | (0x60 + mo), mod[1]);
    opl(bank | (0x80 + mo), mod[2]); opl(bank | (0xE0 + mo), mod[3]);
    opl(bank | (0x40 + mo), (mod[4] & 0xC0) | (mod[5] & 0x3F));  // modulator level from patch
    opl(bank | (0x20 + co), car[0]); opl(bank | (0x60 + co), car[1]);
    opl(bank | (0x80 + co), car[2]); opl(bank | (0xE0 + co), car[3]);
    opl(bank | (0xC0 + VOICE_CH(v)), (fb & 0x0F) | 0x30);        // feedback/conn + L,R
}

// Set the carrier output level from the patch base + MUS/master volume.
// Extra attenuation for a MIDI volume of 0..127, in the register's 0.75 dB
// steps: att = 20*log10(127/vol) / 0.75. The previous curve interpolated
// linearly in the ATTENUATION domain, which is exponential in loudness — at
// half volume it threw away 28 dB instead of 6, and left the music inaudible
// under the sound effects. Volume is a dB scale; this table is that scale.
static const u8 vol_att[128] = {
    63, 56, 48, 43, 40, 37, 35, 34, 32, 31, 29, 28, 27, 26, 26, 25,
    24, 23, 23, 22, 21, 21, 20, 20, 19, 19, 18, 18, 18, 17, 17, 16,
    16, 16, 15, 15, 15, 14, 14, 14, 13, 13, 13, 13, 12, 12, 12, 12,
    11, 11, 11, 11, 10, 10, 10, 10,  9,  9,  9,  9,  9,  8,  8,  8,
     8,  8,  8,  7,  7,  7,  7,  7,  7,  6,  6,  6,  6,  6,  6,  5,
     5,  5,  5,  5,  5,  5,  5,  4,  4,  4,  4,  4,  4,  4,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,
};

static void opl_set_level(int v, const u8 *voice, int midi_ch)
{
    int co = op_off[VOICE_CH(v)][1];
    const u8 *car = voice + 7;
    int base = car[5] & 0x3F;                            // patch attenuation (0=loud)
    int vol  = (chans[midi_ch].volume * music_volume) / 15;  // 0..127
    if (vol > 127) vol = 127;
    if (vol < 0)   vol = 0;
    int att  = base + vol_att[vol];
    if (att > 63) att = 63;
    opl(VOICE_BANK(v) | (0x40 + co), (car[4] & 0xC0) | (att & 0x3F));
}

// `detune` shifts the pitch by a fraction of a semitone; GENMIDI gives the
// second voice of a double-voice instrument its own fine tuning, and that slight
// beating between the two is most of what makes those instruments sound the way
// they do.
static void opl_keyon(int v, int note, int detune)
{
    int bank = VOICE_BANK(v), ch = VOICE_CH(v);
    int block = note / 12 - 1;
    if (block < 0) block = 0;
    if (block > 7) block = 7;
    int fnum = note_fnum[note % 12];
    if (detune)
        fnum += (int)(((long)fnum * detune) / 1108);    // ~1/64 semitone per step
    if (fnum < 1) fnum = 1;
    if (fnum > 1023) fnum = 1023;
    opl(bank | (0xA0 + ch), fnum & 0xFF);
    voices[v].b0 = (u8)((block << 2) | ((fnum >> 8) & 3));
    opl(bank | (0xB0 + ch), 0x20 | voices[v].b0);       // 0x20 = key-on
}

static void opl_keyoff(int v)
{
    opl(VOICE_BANK(v) | (0xB0 + VOICE_CH(v)), voices[v].b0);   // clear key-on
}

// -------------------------------------------------------------- voice alloc

static int voice_alloc(int midi_ch, int note)
{
    int i, oldest = 0;
    unsigned oldest_age = 0xFFFFFFFFu;
    for (i = 0; i < NUM_VOICES; i++) {
        if (!voices[i].used) { oldest = i; break; }
        if (voices[i].age < oldest_age) { oldest_age = voices[i].age; oldest = i; }
    }
    if (voices[oldest].used)
        opl_keyoff(oldest);
    voices[oldest].used = 1;
    voices[oldest].midi_ch = midi_ch;
    voices[oldest].note = note;
    voices[oldest].age = ++voice_clock;
    return oldest;
}

static void voice_release(int midi_ch, int note)
{
    int i;
    for (i = 0; i < NUM_VOICES; i++) {
        // No early exit: a double-voice instrument holds two voices for the same
        // (channel, note) and both have to be released.
        if (voices[i].used && voices[i].midi_ch == midi_ch && voices[i].note == note) {
            opl_keyoff(i);
            voices[i].used = 0;
        }
    }
}

static void voices_all_off(void)
{
    int i;
    for (i = 0; i < NUM_VOICES; i++) {
        if (voices[i].used) { opl_keyoff(i); voices[i].used = 0; }
    }
}

// -------------------------------------------------------------- MUS events

// MUS percussion is channel 15; a note there selects a GENMIDI percussion
// instrument (indices 128..174 for notes 35..81).
static int channel_instrument(int midi_ch, int note)
{
    if (midi_ch == 15) {
        int idx = 128 + note - 35;
        if (idx < 128) idx = 128;
        if (idx >= GENMIDI_NUM) idx = GENMIDI_NUM - 1;
        return idx;
    }
    return chans[midi_ch].instrument;
}

static void mus_play_note(int midi_ch, int note, int vol)
{
    if (vol >= 0)
        chans[midi_ch].volume = vol;

    int instr_idx = channel_instrument(midi_ch, note);
    const u8 *instr = instr_ptr(instr_idx);
    const u8 *voice = instr_voice(instr);

    // Fixed-pitch instruments (most percussion) play at their fixed note.
    int play_note = note;
    unsigned short flags = (unsigned short)(instr[0] | (instr[1] << 8));
    if (flags & GENMIDI_FLAG_FIXED)
        play_note = instr[3];
    if (play_note < 0)   play_note = 0;
    if (play_note > 95)  play_note = 95;

    // A voice record carries its own base-note offset (signed, last two bytes).
    int off1 = (short)(voice[14] | (voice[15] << 8));
    int n1 = play_note + off1;
    if (n1 < 0)  n1 = 0;
    if (n1 > 95) n1 = 95;

    int v = voice_alloc(midi_ch, note);
    opl_program_voice(v, voice);
    opl_set_level(v, voice, midi_ch);
    opl_keyon(v, n1, 0);

    // Double-voice instruments are two 2-operator voices played together, the
    // second one detuned by the instrument's fine tuning (128 = dead centre).
    // Leaving it out is why our percussion sounded duller than it should.
    if (flags & GENMIDI_FLAG_DOUBLE) {
        const u8 *voice2 = instr + 4 + 16;
        int off2 = (short)(voice2[14] | (voice2[15] << 8));
        int n2 = play_note + off2;
        if (n2 < 0)  n2 = 0;
        if (n2 > 95) n2 = 95;
        int v2 = voice_alloc(midi_ch, note);
        opl_program_voice(v2, voice2);
        opl_set_level(v2, voice2, midi_ch);
        opl_keyon(v2, n2, (int)instr[2] - 128);
    }
}

// Process one group of simultaneous events, then set the delay to the next.
static void mus_advance_group(void)
{
    int guard = 2048;                            // safety: never spin on a malformed score
    for (;;) {
        if (--guard <= 0) { mus_delay = 1; return; }
        if (mus_pos >= mus_end) {
            if (mus_looping) { mus_pos = mus_loop; voices_all_off(); }
            else             { mus_playing = 0; voices_all_off(); return; }
        }

        u8 ev   = *mus_pos++;
        int last = ev & 0x80;
        int type = (ev >> 4) & 7;
        int ch   = ev & 0x0F;

        switch (type) {
        case 0:                                  // release note
            voice_release(ch, *mus_pos++ & 0x7F);
            break;
        case 1: {                                // play note (+ optional volume)
            u8 n = *mus_pos++;
            int vol = -1;
            if (n & 0x80) vol = *mus_pos++ & 0x7F;
            mus_play_note(ch, n & 0x7F, vol);
            break;
        }
        case 2:                                  // pitch bend (consume; not applied yet)
            mus_pos++;
            break;
        case 3:                                  // system event
            if ((*mus_pos++ & 0x7F) >= 10)       // all-notes/sounds-off family
                voices_all_off();
            break;
        case 4: {                                // controller change
            u8 ctl = *mus_pos++;
            u8 val = *mus_pos++;
            if (ctl == 0)      chans[ch].instrument = val;        // program
            else if (ctl == 3) chans[ch].volume = val & 0x7F;     // volume
            break;
        }
        case 6:                                  // score end
            if (mus_looping) { mus_pos = mus_loop; voices_all_off(); }
            else             { mus_playing = 0; voices_all_off(); return; }
            break;
        default:                                 // 5 = measure end, 7 = unused
            break;
        }

        if (last) {                              // variable-length delay follows
            int d = 0; u8 b;
            do { b = *mus_pos++; d = (d << 7) | (b & 0x7F); } while (b & 0x80);
            mus_delay = d;
            return;
        }
    }
}

// ------------------------------------------------------------------ public API

// Called once per game loop (from D_DoomLoop), which on this VM is only ~7 Hz.
// Rather than emitting whatever the score owes at that instant, run the
// sequencer forward to MUS_LOOKAHEAD_MS beyond real time and stamp every
// register write with the moment it belongs to; the sound card then places each
// write at the right sample. The result keeps the score's timing regardless of
// how jerkily this function is called.
void I_UpdateMusic(void)
{
    unsigned now, horizon;
    int guard = 8192;                       // never spin on a pathological score

    if (opl_fd < 0 || !mus_playing || mus_paused) {
        mus_clock_running = 0;
        return;
    }

    if (!mus_clock_running)
        mus_clock_reset();

    now = mus_now_ms();

    // A stall longer than MUS_RESYNC_MS (tab backgrounded, huge hitch) would
    // otherwise be repaid as one enormous burst of notes. Drop the backlog and
    // carry on from here instead.
    if ((int)(now - mus_tick_ms(mus_ticks)) > MUS_RESYNC_MS) {
        mus_base_ms = now;
        mus_ticks = 0;
    }

    horizon = now + MUS_LOOKAHEAD_MS;
    while (mus_playing && guard-- > 0) {
        unsigned tick_ms = mus_tick_ms(mus_ticks);
        if ((int)(tick_ms - horizon) > 0)
            break;                          // scheduled far enough ahead

        // How far in the future this tick is. Negative means we are behind
        // (the loop stalled): emit it immediately.
        int dt = (int)(tick_ms - now);
        mus_sched_dt = dt > 0 ? (unsigned)dt : 0;

        if (mus_delay > 0) {
            mus_delay--;
        } else {
            // Firing a group also sets the delay to the next one. That delay is
            // counted from THIS tick, so this tick is its first — without the
            // decrement below every group costs one tick too many. A note is two
            // groups (its note-on and its note-off), so the score dragged by two
            // ticks per note: 14.3 ms on a 250 ms grid, a measured +5.6%.
            mus_advance_group();
            if (mus_delay > 0) mus_delay--;
        }
        mus_ticks++;
    }
    mus_sched_dt = 0;                       // anything else is immediate
}

void I_InitMusic(void)
{
    int i;

    opl_fd = open("/dev/opl", O_WRONLY);
    if (opl_fd < 0) {
        printf("I_InitMusic: /dev/opl unavailable; music disabled\n");
        return;
    }

    if (W_CheckNumForName("GENMIDI") < 0) {
        printf("I_InitMusic: no GENMIDI lump; music disabled\n");
        close(opl_fd);
        opl_fd = -1;
        return;
    }
    genmidi = (const u8 *)W_CacheLumpName("GENMIDI", PU_STATIC) + 8;  // skip header

    // Reset the chip: OPL3 mode on, no rhythm, all voices silent.
    opl(0x105, 0x01);              // OPL3 NEW = 1 (enables the L/R bits we set)
    opl(0x08, 0x00);
    opl(0xBD, 0x00);
    for (i = 0; i < 9; i++) {
        opl(0xA0 + i, 0x00);
        opl(0xB0 + i, 0x00);       // key off, block 0
        voices[i].used = 0;
    }
    for (i = 0; i < 16; i++) {
        chans[i].instrument = 0;
        chans[i].volume = 100;
    }

    printf("I_InitMusic: OPL music ready (/dev/opl, GENMIDI %d instruments)\n", GENMIDI_NUM);
}

int I_RegisterSong(void *data, const char *name)
{
    const u8 *d = (const u8 *)data;

    if (opl_fd < 0 || !data)
        return 0;

    // MUS header: "MUS\x1a", u16 SongLen, u16 SongStart, ...
    if (!(d[0] == 'M' && d[1] == 'U' && d[2] == 'S' && d[3] == 0x1A))
        return 0;                                  // not MUS (e.g. raw MIDI) — unsupported

    int song_len   = d[4] | (d[5] << 8);
    int song_start = d[6] | (d[7] << 8);

    song_base = d;
    mus_loop  = d + song_start;
    mus_end   = d + song_start + song_len;
    mus_pos   = mus_loop;
    mus_delay = 0;
    mus_playing = 0;
    mus_paused = 0;
    return 1;
}

void I_PlaySong(int handle, int looping)
{
    if (opl_fd < 0 || !handle)
        return;
    int i;
    for (i = 0; i < 16; i++) chans[i].volume = 100;
    voices_all_off();
    opl_flush();                        // drop anything queued from the last song
    mus_pos = mus_loop;
    mus_delay = 0;
    mus_looping = looping;
    mus_paused = 0;
    mus_playing = 1;
    mus_clock_reset();

    // Fill the queue right now rather than waiting for the next game loop.
    // DOOM calls this just BEFORE it loads the level, and loading stops this
    // machine for the better part of two seconds — measured 1779 ms. Without
    // priming, the flush above leaves the VM with nothing to play and the music
    // does not resume until the load finishes: an audible hole at every level
    // change. Priming hands the VM MUS_LOOKAHEAD_MS of music first, and it keeps
    // placing those writes on time while the guest is frozen.
    I_UpdateMusic();
}

void I_StopSong(int handle)
{
    mus_playing = 0;
    voices_all_off();
    // Deliberately NOT flushing here. What is queued is up to MUS_LOOKAHEAD_MS of
    // this song, and the guest is usually about to stall for a second or more
    // loading the next level — that queued music is exactly what covers the stall.
    // Flushing it here left a 1.65 s hole in the music at every demo transition.
    // I_PlaySong flushes instead, and it does so at the moment the next song's
    // first note is scheduled, so there is no gap and no overlap.
}

void I_PauseSong(int handle)
{
    mus_paused = 1;
    voices_all_off();
    opl_flush();          // likewise: a pause has to silence what is queued too
}

void I_ResumeSong(int handle)
{
    mus_paused = 0;
}

void I_UnRegisterSong(int handle)
{
    mus_playing = 0;
    voices_all_off();
    song_base = mus_pos = mus_end = mus_loop = NULL;
}

void I_SetMusicVolume(int volume)
{
    music_volume = volume;               // 0..15
    if (music_volume < 0)  music_volume = 0;
    if (music_volume > 15) music_volume = 15;
}

void I_ShutdownMusic(void)
{
    if (opl_fd >= 0) {
        mus_playing = 0;
        voices_all_off();
        close(opl_fd);
        opl_fd = -1;
    }
}
