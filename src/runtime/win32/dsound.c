/* DirectSound (DirectX 5 subset) mixed in software on the SDL audio thread.
 *
 * Buffer memory lives in guest memory so Lock() can hand the game plain
 * pointers. The mixer only reads that memory and the playback state, which
 * the guest side changes under the audio lock. */
#include "com.h"
#include "../platform.h"

#include <math.h>
#include <pthread.h>

#define DS_OK 0u
#define DSERR_INVALIDPARAM 0x80070057u
#define DSERR_BUFFERLOST 0x88780096u

#define DSBCAPS_PRIMARYBUFFER 0x1u
#define DSBPLAY_LOOPING 0x1u
#define DSBSTATUS_PLAYING 0x1u
#define DSBSTATUS_LOOPING 0x4u
#define DSBLOCK_FROMWRITECURSOR 0x1u
#define DSBLOCK_ENTIREBUFFER 0x2u

typedef struct Data { uint32_t mem, size, refs; } Data;

typedef struct Buffer {
    uint32_t obj, refs, flags;
    bool primary;
    Data *data;
    uint32_t channels, rate, bits, block;
    uint32_t freq;            /* playback frequency (SetFrequency) */
    int32_t volume, pan;      /* hundredths of dB */
    bool playing, looping;
    double pos;               /* in frames */
    float gain_l, gain_r;
} Buffer;

#define MAX_BUFFERS 128
static Buffer *buffers[MAX_BUFFERS];
static uint32_t vt_ds, vt_buf, ds_obj, ds_refs;
static uint32_t primary_fmt[4] = {2, 44100, 16, 4};

static void update_gain(Buffer *b)
{
    float v = b->volume <= -10000 ? 0.0f : powf(10.0f, (float)b->volume / 2000.0f);
    float l = 1.0f, r = 1.0f;
    if (b->pan < 0)
        r = b->pan <= -10000 ? 0.0f : powf(10.0f, (float)b->pan / 2000.0f);
    else if (b->pan > 0)
        l = b->pan >= 10000 ? 0.0f : powf(10.0f, (float)-b->pan / 2000.0f);
    b->gain_l = v * l;
    b->gain_r = v * r;
}

static inline float sample(Buffer *b, uint32_t frame, int ch)
{
    const uint8_t *p = g_mem + b->data->mem + frame * b->block;
    int c = b->channels == 2 ? ch : 0;
    if (b->bits == 16) {
        int16_t s;
        memcpy(&s, p + 2 * c, 2);
        return (float)s;
    }
    return (float)((int)p[c] - 128) * 256.0f;
}

static void mix(int16_t *out, int frames, void *user)
{
    (void)user;
    static float acc[8192 * 2];
    if (frames > 8192)
        frames = 8192;
    memset(acc, 0, (size_t)frames * 2 * sizeof *acc);
    int rate = plat_audio_rate();
    for (int i = 0; i < MAX_BUFFERS; i++) {
        Buffer *b = buffers[i];
        if (!b || b->primary || !b->playing || !b->data || !b->block)
            continue;
        uint32_t nframes = b->data->size / b->block;
        if (!nframes)
            continue;
        double step = (double)b->freq / (double)rate;
        for (int f = 0; f < frames; f++) {
            uint32_t fr = (uint32_t)b->pos;
            if (fr >= nframes) {
                if (b->looping) {
                    b->pos -= nframes;
                    fr = (uint32_t)b->pos;
                    if (fr >= nframes) { b->pos = 0; fr = 0; }
                } else {
                    b->playing = false;
                    b->pos = 0;
                    break;
                }
            }
            acc[2 * f] += sample(b, fr, 0) * b->gain_l;
            acc[2 * f + 1] += sample(b, fr, 1) * b->gain_r;
            b->pos += step;
        }
    }
    for (int f = 0; f < frames * 2; f++) {
        float v = acc[f];
        out[f] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int)v);
    }
}

static Buffer *buf_of(uint32_t obj)
{
    Buffer *b = com_host(obj);
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (buffers[i] == b && b)
            return b;
    return NULL;
}

static Buffer *new_buffer(void)
{
    Buffer *b = calloc(1, sizeof *b);
    b->refs = 1;
    b->volume = 0;
    b->pan = 0;
    update_gain(b);
    plat_audio_lock();
    int i;
    for (i = 0; i < MAX_BUFFERS && buffers[i]; i++) {}
    if (i == MAX_BUFFERS) {
        plat_audio_unlock();
        free(b);
        return NULL;
    }
    buffers[i] = b;
    plat_audio_unlock();
    b->obj = com_new(vt_buf, b);
    return b;
}

static void set_format(Buffer *b, uint32_t wfx)
{
    b->channels = R16(wfx + 2);
    b->rate = R32(wfx + 4);
    b->bits = R16(wfx + 14);
    b->block = R16(wfx + 12);
    if (!b->block)
        b->block = b->channels * b->bits / 8;
    b->freq = b->rate;
}

/* ================================================================ IDirectSound */
#define THIS ARG(0)

static uint32_t ds_QueryInterface(Cpu *c) { W32(ARG(2), THIS); ds_refs++; RET(3, DS_OK); }
static uint32_t ds_AddRef(Cpu *c) { RET(1, ++ds_refs); }
static uint32_t ds_Release(Cpu *c) { RET(1, ds_refs ? --ds_refs : 0); }

static uint32_t ds_CreateSoundBuffer(Cpu *c)
{
    uint32_t desc = ARG(1), out = ARG(2);
    uint32_t flags = R32(desc + 4), bytes = R32(desc + 8), wfx = R32(desc + 16);
    Buffer *b = new_buffer();
    if (!b)
        RET(4, E_OUTOFMEMORY);
    b->flags = flags;
    if (flags & DSBCAPS_PRIMARYBUFFER) {
        b->primary = true;
        b->channels = primary_fmt[0];
        b->rate = b->freq = primary_fmt[1];
        b->bits = primary_fmt[2];
        b->block = primary_fmt[3];
    } else {
        if (!wfx || !bytes) {
            RET(4, DSERR_INVALIDPARAM);
        }
        set_format(b, wfx);
        b->data = calloc(1, sizeof *b->data);
        b->data->mem = rt_heap_alloc(bytes + 16);
        b->data->size = bytes;
        b->data->refs = 1;
    }
    RT_INFO("IDirectSound::CreateSoundBuffer(flags=%x, %u bytes, %u Hz %u ch %u bit) -> %08x",
            flags, bytes, b->rate, b->channels, b->bits, b->obj);
    W32(out, b->obj);
    RET(4, DS_OK);
}

static uint32_t ds_GetCaps(Cpu *c)
{
    uint32_t p = ARG(1);
    uint32_t size = R32(p);
    memset(g_mem + p + 4, 0, size > 4 && size < 512 ? size - 4 : 92);
    W32(p + 4, 0x1u | 0x2u | 0x4u | 0x8u | 0x10u | 0x20u | 0x400u | 0x800u);
    W32(p + 8, 4000);
    W32(p + 12, 48000);
    RET(2, DS_OK);
}

static uint32_t ds_DuplicateSoundBuffer(Cpu *c)
{
    Buffer *src = buf_of(ARG(1));
    if (!src || src->primary)
        RET(3, DSERR_INVALIDPARAM);
    Buffer *b = new_buffer();
    if (!b)
        RET(3, E_OUTOFMEMORY);
    plat_audio_lock();
    b->flags = src->flags;
    b->data = src->data;
    b->data->refs++;
    b->channels = src->channels;
    b->rate = src->rate;
    b->bits = src->bits;
    b->block = src->block;
    b->freq = src->freq;
    b->volume = src->volume;
    b->pan = src->pan;
    update_gain(b);
    plat_audio_unlock();
    W32(ARG(2), b->obj);
    RET(3, DS_OK);
}

static uint32_t ds_SetCooperativeLevel(Cpu *c) { RET(3, DS_OK); }
static uint32_t ds_Compact(Cpu *c) { RET(1, DS_OK); }
static uint32_t ds_GetSpeakerConfig(Cpu *c) { W32(ARG(1), 4 /* stereo */); RET(2, DS_OK); }
static uint32_t ds_SetSpeakerConfig(Cpu *c) { RET(2, DS_OK); }

static const ComMethod ds_methods[] = {
    {"QueryInterface", ds_QueryInterface, 3}, {"AddRef", ds_AddRef, 1}, {"Release", ds_Release, 1},
    {"CreateSoundBuffer", ds_CreateSoundBuffer, 4}, {"GetCaps", ds_GetCaps, 2},
    {"DuplicateSoundBuffer", ds_DuplicateSoundBuffer, 3},
    {"SetCooperativeLevel", ds_SetCooperativeLevel, 3}, {"Compact", ds_Compact, 1},
    {"GetSpeakerConfig", ds_GetSpeakerConfig, 2}, {"SetSpeakerConfig", ds_SetSpeakerConfig, 2},
    {"Initialize", NULL, 2},
};

/* ================================================================ IDirectSoundBuffer */
#define BUF buf_of(THIS)

static uint32_t b_QueryInterface(Cpu *c) { Buffer *b = BUF; if (b) b->refs++; W32(ARG(2), THIS); RET(3, DS_OK); }
static uint32_t b_AddRef(Cpu *c) { Buffer *b = BUF; RET(1, b ? ++b->refs : 0); }

static uint32_t b_Release(Cpu *c)
{
    Buffer *b = BUF;
    if (!b)
        RET(1, 0);
    uint32_t r = --b->refs;
    if (r == 0) {
        plat_audio_lock();
        for (int i = 0; i < MAX_BUFFERS; i++)
            if (buffers[i] == b)
                buffers[i] = NULL;
        plat_audio_unlock();
        if (b->data && --b->data->refs == 0) {
            rt_heap_free(b->data->mem);
            free(b->data);
        }
        com_free(b->obj);
        free(b);
    }
    RET(1, r);
}

static uint32_t b_GetCaps(Cpu *c)
{
    Buffer *b = BUF;
    uint32_t p = ARG(1);
    if (!b)
        RET(2, DSERR_INVALIDPARAM);
    W32(p + 4, b->flags);
    W32(p + 8, b->data ? b->data->size : 0);
    W32(p + 12, 0);
    W32(p + 16, 0);
    RET(2, DS_OK);
}

static uint32_t b_GetCurrentPosition(Cpu *c)
{
    Buffer *b = BUF;
    if (!b)
        RET(3, DSERR_INVALIDPARAM);
    uint32_t play = 0, write = 0;
    if (b->data && b->block) {
        plat_audio_lock();
        play = (uint32_t)b->pos * b->block;
        plat_audio_unlock();
        if (play >= b->data->size)
            play = 0;
        /* A playing buffer's write cursor runs about 15 ms ahead, like real
         * drivers; a stopped buffer's equals the play cursor. */
        write = b->playing ? (play + (b->freq * 15 / 1000) * b->block) % b->data->size : play;
    }
    if (ARG(1)) W32(ARG(1), play);
    if (ARG(2)) W32(ARG(2), write);
    RET(3, DS_OK);
}

static uint32_t b_GetFormat(Cpu *c)
{
    Buffer *b = BUF;
    uint32_t p = ARG(1), size = ARG(2), written = ARG(3);
    if (!b)
        RET(4, DSERR_INVALIDPARAM);
    if (p && size >= 16) {
        W16(p, 1);
        W16(p + 2, (uint16_t)b->channels);
        W32(p + 4, b->rate);
        W32(p + 8, b->rate * b->block);
        W16(p + 12, (uint16_t)b->block);
        W16(p + 14, (uint16_t)b->bits);
        if (size >= 18)
            W16(p + 16, 0);
    }
    if (written)
        W32(written, 18);
    RET(4, DS_OK);
}

static uint32_t b_GetVolume(Cpu *c) { Buffer *b = BUF; if (b) W32(ARG(1), (uint32_t)b->volume); RET(2, b ? DS_OK : DSERR_INVALIDPARAM); }
static uint32_t b_GetPan(Cpu *c) { Buffer *b = BUF; if (b) W32(ARG(1), (uint32_t)b->pan); RET(2, b ? DS_OK : DSERR_INVALIDPARAM); }
static uint32_t b_GetFrequency(Cpu *c) { Buffer *b = BUF; if (b) W32(ARG(1), b->freq); RET(2, b ? DS_OK : DSERR_INVALIDPARAM); }

static uint32_t b_GetStatus(Cpu *c)
{
    Buffer *b = BUF;
    if (!b)
        RET(2, DSERR_INVALIDPARAM);
    plat_audio_lock();
    uint32_t st = (b->playing ? DSBSTATUS_PLAYING : 0) | (b->playing && b->looping ? DSBSTATUS_LOOPING : 0);
    plat_audio_unlock();
    W32(ARG(1), st);
    RET(2, DS_OK);
}

static uint32_t b_Lock(Cpu *c)
{
    Buffer *b = BUF;
    uint32_t off = ARG(1), bytes = ARG(2), p1 = ARG(3), n1 = ARG(4), p2 = ARG(5), n2 = ARG(6), flags = ARG(7);
    if (!b || !b->data)
        RET(8, DSERR_INVALIDPARAM);
    uint32_t size = b->data->size;
    if (flags & DSBLOCK_FROMWRITECURSOR) {
        plat_audio_lock();
        uint32_t play = (uint32_t)b->pos * b->block;
        bool playing = b->playing;
        plat_audio_unlock();
        off = playing ? (play + b->block * (b->freq * 15 / 1000)) % size : play % size;
    }
    if (flags & DSBLOCK_ENTIREBUFFER)
        bytes = size;
    if (off >= size || bytes > size)
        RET(8, DSERR_INVALIDPARAM);
    uint32_t first = bytes <= size - off ? bytes : size - off;
    W32(p1, b->data->mem + off);
    W32(n1, first);
    if (p2) W32(p2, first < bytes ? b->data->mem : 0);
    if (n2) W32(n2, bytes - first);
    RET(8, DS_OK);
}

static uint32_t b_Unlock(Cpu *c) { RET(5, DS_OK); }

static uint32_t b_Play(Cpu *c)
{
    Buffer *b = BUF;
    if (!b)
        RET(4, DSERR_INVALIDPARAM);
    plat_audio_lock();
    b->playing = !b->primary;
    b->looping = (ARG(3) & DSBPLAY_LOOPING) != 0;
    plat_audio_unlock();
    RET(4, DS_OK);
}

static uint32_t b_SetCurrentPosition(Cpu *c)
{
    Buffer *b = BUF;
    if (!b || !b->block)
        RET(2, b ? DS_OK : DSERR_INVALIDPARAM);
    plat_audio_lock();
    b->pos = (double)(ARG(1) / b->block);
    plat_audio_unlock();
    RET(2, DS_OK);
}

static uint32_t b_SetFormat(Cpu *c)
{
    Buffer *b = BUF;
    uint32_t wfx = ARG(1);
    if (!b || !wfx)
        RET(2, DSERR_INVALIDPARAM);
    if (b->primary) {
        primary_fmt[0] = R16(wfx + 2);
        primary_fmt[1] = R32(wfx + 4);
        primary_fmt[2] = R16(wfx + 14);
        primary_fmt[3] = R16(wfx + 12);
    }
    set_format(b, wfx);
    RET(2, DS_OK);
}

static uint32_t b_SetVolume(Cpu *c)
{
    Buffer *b = BUF;
    if (!b)
        RET(2, DSERR_INVALIDPARAM);
    plat_audio_lock();
    b->volume = (int32_t)ARG(1);
    update_gain(b);
    plat_audio_unlock();
    RET(2, DS_OK);
}

static uint32_t b_SetPan(Cpu *c)
{
    Buffer *b = BUF;
    if (!b)
        RET(2, DSERR_INVALIDPARAM);
    plat_audio_lock();
    b->pan = (int32_t)ARG(1);
    update_gain(b);
    plat_audio_unlock();
    RET(2, DS_OK);
}

static uint32_t b_SetFrequency(Cpu *c)
{
    Buffer *b = BUF;
    if (!b)
        RET(2, DSERR_INVALIDPARAM);
    plat_audio_lock();
    b->freq = ARG(1) ? ARG(1) : b->rate;
    plat_audio_unlock();
    RET(2, DS_OK);
}

static uint32_t b_Stop(Cpu *c)
{
    Buffer *b = BUF;
    if (b) {
        plat_audio_lock();
        b->playing = false;
        plat_audio_unlock();
    }
    RET(1, DS_OK);
}

static uint32_t b_Restore(Cpu *c) { RET(1, DS_OK); }
static uint32_t b_Initialize(Cpu *c) { RET(3, DS_OK); }

static const ComMethod buf_methods[] = {
    {"QueryInterface", b_QueryInterface, 3}, {"AddRef", b_AddRef, 1}, {"Release", b_Release, 1},
    {"GetCaps", b_GetCaps, 2}, {"GetCurrentPosition", b_GetCurrentPosition, 3},
    {"GetFormat", b_GetFormat, 4}, {"GetVolume", b_GetVolume, 2}, {"GetPan", b_GetPan, 2},
    {"GetFrequency", b_GetFrequency, 2}, {"GetStatus", b_GetStatus, 2},
    {"Initialize", b_Initialize, 3}, {"Lock", b_Lock, 8}, {"Play", b_Play, 4},
    {"SetCurrentPosition", b_SetCurrentPosition, 2}, {"SetFormat", b_SetFormat, 2},
    {"SetVolume", b_SetVolume, 2}, {"SetPan", b_SetPan, 2}, {"SetFrequency", b_SetFrequency, 2},
    {"Stop", b_Stop, 1}, {"Unlock", b_Unlock, 5}, {"Restore", b_Restore, 1},
};

WINAPI_FN(dsound, DirectSoundCreate)
{
    uint32_t out = ARG(1);
    if (!vt_ds) {
        vt_ds = com_vtable("IDirectSound", ds_methods, (int)(sizeof ds_methods / sizeof *ds_methods));
        vt_buf = com_vtable("IDirectSoundBuffer", buf_methods,
                            (int)(sizeof buf_methods / sizeof *buf_methods));
        plat_audio_start(44100, mix, NULL);
    }
    static int host;
    if (!ds_obj)
        ds_obj = com_new(vt_ds, &host);
    ds_refs++;
    W32(out, ds_obj);
    RT_INFO("DirectSoundCreate -> %08x", ds_obj);
    RET(3, DS_OK);
}
