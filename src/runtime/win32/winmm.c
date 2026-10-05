/* WINMM: multimedia timers on host threads, device queries and volumes. */
#include "win32.h"
#include "../platform.h"

#include <pthread.h>
#include <time.h>

#define TIME_PERIODIC 1u
#define MMSYSERR_NOERROR 0u
#define MMSYSERR_BADDEVICEID 2u
#define MMSYSERR_NOTSUPPORTED 8u

typedef struct MMTimer {
    bool used, stop;
    uint32_t id, delay, callback, user, flags;
    pthread_t thread;
} MMTimer;

static MMTimer mmtimers[16];
static pthread_mutex_t mm_lock = PTHREAD_MUTEX_INITIALIZER;

static void sleep_until(struct timespec *t)
{
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, t, NULL) != 0) {}
}

static void *timer_main(void *arg)
{
    MMTimer *t = arg;
    uint32_t tid;
    Cpu *c = rt_cpu_new_thread(&tid);
    rt_cpu_bind(c);
    rt_gil_set_no_yield(1);
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (;;) {
        long ns = (long)t->delay * 1000000L;
        next.tv_nsec += ns;
        while (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }
        sleep_until(&next);
        if (t->stop)
            break;
        rt_gil_acquire();
        if (!t->stop)
            rt_guest_call(c, t->callback, 5, t->id, 0, t->user, 0, 0);
        rt_gil_release();
        if (!(t->flags & TIME_PERIODIC))
            break;
        /* Do not try to catch up after a long stall (the game was busy). */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > next.tv_sec + 1)
            next = now;
    }
    pthread_mutex_lock(&mm_lock);
    t->used = false;
    pthread_mutex_unlock(&mm_lock);
    rt_cpu_free(c);
    return NULL;
}

WINAPI_FN(winmm, timeSetEvent)
{
    uint32_t delay = ARG(0), cb = ARG(2), user = ARG(3), flags = ARG(4);
    pthread_mutex_lock(&mm_lock);
    MMTimer *t = NULL;
    for (int i = 0; i < 16; i++)
        if (!mmtimers[i].used) {
            t = &mmtimers[i];
            *t = (MMTimer){true, false, 0x10u + (uint32_t)i, delay ? delay : 1, cb, user, flags, 0};
            break;
        }
    pthread_mutex_unlock(&mm_lock);
    if (!t)
        RET(5, 0);
    if (flags & 0x30)
        RT_WARN("timeSetEvent: event/pulse callbacks (flags %x) are not supported", flags);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);
    pthread_create(&t->thread, &attr, timer_main, t);
    pthread_detach(t->thread);
    RT_INFO("timeSetEvent(%u ms, callback %08x, user %08x, flags %x) -> %u", delay, cb, user, flags, t->id);
    RET(5, t->id);
}

WINAPI_FN(winmm, timeKillEvent)
{
    uint32_t id = ARG(0);
    pthread_mutex_lock(&mm_lock);
    bool found = false;
    for (int i = 0; i < 16; i++)
        if (mmtimers[i].used && mmtimers[i].id == id) {
            mmtimers[i].stop = true;
            found = true;
        }
    pthread_mutex_unlock(&mm_lock);
    RET(1, found ? MMSYSERR_NOERROR : 97u /* TIMERR_NOCANDO */);
}

WINAPI_FN(winmm, timeBeginPeriod) { RET(1, MMSYSERR_NOERROR); }
WINAPI_FN(winmm, timeEndPeriod) { RET(1, MMSYSERR_NOERROR); }

WINAPI_FN(winmm, timeGetDevCaps)
{
    uint32_t p = ARG(0);
    if (p && ARG(1) >= 8) {
        W32(p, 1);
        W32(p + 4, 1000000);
    }
    RET(2, MMSYSERR_NOERROR);
}

/* ---- devices: one wave output device, no MIDI or aux devices ---- */
static uint32_t wave_volume = 0xffffffffu;

WINAPI_FN(winmm, waveOutGetNumDevs) { RET(0, 1); }

WINAPI_FN(winmm, waveOutGetDevCapsA)
{
    uint32_t dev = ARG(0), p = ARG(1), size = ARG(2);
    if (dev != 0 && dev != 0xffffffffu)
        RET(3, MMSYSERR_BADDEVICEID);
    if (p && size >= 52) {
        memset(g_mem + p, 0, size);
        W16(p, 1);                  /* wMid */
        W16(p + 2, 1);              /* wPid */
        W32(p + 4, 0x0400);         /* vDriverVersion */
        put_gstr(p + 8, 32, "Lula SDL audio");
        W32(p + 40, 0xfff);         /* dwFormats: everything up to 44k 16-bit stereo */
        W16(p + 44, 2);             /* wChannels */
        W32(p + 48, 0x0c);          /* WAVECAPS_VOLUME | WAVECAPS_LRVOLUME */
    }
    RET(3, MMSYSERR_NOERROR);
}

WINAPI_FN(winmm, waveOutGetVolume)
{
    if (ARG(1))
        W32(ARG(1), wave_volume);
    RET(2, MMSYSERR_NOERROR);
}

/* The options slider only changes the wave volume: apply it as a master
 * gain on our own mix, never on the host mixer (winmm-timing.md 6.1). */
void dsound_set_master_volume(uint32_t volume);

WINAPI_FN(winmm, waveOutSetVolume)
{
    wave_volume = ARG(1);
    dsound_set_master_volume(wave_volume);
    RET(2, MMSYSERR_NOERROR);
}

WINAPI_FN(winmm, midiOutGetNumDevs) { RET(0, 0); }
WINAPI_FN(winmm, midiOutGetDevCapsA) { RET(3, MMSYSERR_BADDEVICEID); }
WINAPI_FN(winmm, midiOutGetVolume) { RET(2, MMSYSERR_BADDEVICEID); }
WINAPI_FN(winmm, midiOutSetVolume) { RET(2, MMSYSERR_BADDEVICEID); }
WINAPI_FN(winmm, auxGetNumDevs) { RET(0, 0); }
WINAPI_FN(winmm, auxGetDevCapsA) { RET(3, MMSYSERR_BADDEVICEID); }
WINAPI_FN(winmm, auxGetVolume) { RET(2, MMSYSERR_BADDEVICEID); }
WINAPI_FN(winmm, auxSetVolume) { RET(2, MMSYSERR_BADDEVICEID); }

/* MCI is on hold together with video playback: report "not supported". */
WINAPI_FN(winmm, mciSendCommandA)
{
    RT_WARN("mciSendCommandA(dev %u, msg %04x) is not supported", ARG(0), ARG(1));
    RET(4, 274u /* MCIERR_UNSUPPORTED_FUNCTION */);
}

/* Not imported, but reachable through GetProcAddress. */
uint32_t h_kernel32_Sleep(Cpu *c)
{
    uint32_t ms = ARG(0);
    RT_BLOCKING_BEGIN();
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    if (ms == 0)
        sched_yield();
    else
        nanosleep(&ts, NULL);
    RT_BLOCKING_END();
    RET(1, 0);
}

uint32_t h_winmm_timeGetTime(Cpu *c)
{
    RET(0, plat_ticks_ms());
}
