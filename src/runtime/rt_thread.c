/* Guest threads. All guest code runs under one global lock, like the single
 * CPU the game was written for. The lock is released in blocking API calls
 * and handed over at loop back-edges when another thread is waiting. */
#include "rt.h"

#include <pthread.h>
#include <stdlib.h>

volatile int rt_gil_waiters;

static pthread_mutex_t gil_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gil_cond = PTHREAD_COND_INITIALIZER;
static unsigned long gil_next_ticket, gil_serving;
static __thread Cpu *cur_cpu;
static __thread int gil_depth;
static __thread int gil_no_yield;

/* The multimedia timer thread runs each callback to completion, like the
 * time-critical timer thread on a single-CPU Win9x machine (winmm-timing.md
 * R2): it never hands the lock over at back-edges or API returns. */
void rt_gil_set_no_yield(int on) { gil_no_yield = on; }

void rt_gil_init(void) {}

void rt_gil_acquire(void)
{
    if (gil_depth++)
        return;
    pthread_mutex_lock(&gil_mutex);
    unsigned long ticket = gil_next_ticket++;
    __atomic_add_fetch(&rt_gil_waiters, 1, __ATOMIC_RELAXED);
    while (ticket != gil_serving)
        pthread_cond_wait(&gil_cond, &gil_mutex);
    __atomic_sub_fetch(&rt_gil_waiters, 1, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&gil_mutex);
}

void rt_gil_release(void)
{
    if (--gil_depth)
        return;
    pthread_mutex_lock(&gil_mutex);
    gil_serving++;
    pthread_cond_broadcast(&gil_cond);
    pthread_mutex_unlock(&gil_mutex);
}

/* Called from generated code at loop back-edges when someone is waiting. */
void rt_gil_yield(void)
{
    if (gil_depth != 1 || gil_no_yield)
        return;
    rt_gil_release();
    rt_gil_acquire();
}

Cpu *rt_cpu_current(void)
{
    return cur_cpu;
}

void rt_cpu_bind(Cpu *c)
{
    cur_cpu = c;
    rt_fpu_sync_host(c);                     /* x87 precision/rounding of this guest thread */
}

static pthread_mutex_t slot_lock = PTHREAD_MUTEX_INITIALIZER;
static bool slot_used[RT_MAX_THREADS];
static uint32_t next_thread_id = 0x100;

Cpu *rt_cpu_new_thread(uint32_t *thread_id)
{
    int slot = -1;
    pthread_mutex_lock(&slot_lock);
    for (int i = 0; i < RT_MAX_THREADS; i++) {
        if (!slot_used[i]) {
            slot_used[i] = true;
            slot = i;
            break;
        }
    }
    uint32_t tid = next_thread_id += 4;
    pthread_mutex_unlock(&slot_lock);
    if (slot < 0)
        rt_fatal("too many guest threads");

    Cpu *c = calloc(1, sizeof *c);
    uint32_t base = RT_STACKS_BASE + (uint32_t)slot * RT_STACK_SIZE;
    if (!rt_mem_commit(base, RT_STACK_SIZE))
        rt_fatal("cannot commit guest stack");
    c->stack_limit = base + 0x1000;          /* keep a guard page's worth unused */
    c->stack_base = base + RT_STACK_SIZE;
    c->esp = c->stack_base - 16;
    c->thread_id = tid;

    /* Thread environment block: SEH chain, stack range, self pointer. */
    uint32_t teb = rt_low_alloc(0x1000);
    W32(teb + 0x00, 0xffffffffu);            /* no exception handler yet */
    W32(teb + 0x04, c->stack_base);
    W32(teb + 0x08, c->stack_limit);
    W32(teb + 0x18, teb);
    W32(teb + 0x24, tid);
    c->fs_base = teb;
    rt_fpu_init(c);
    if (cur_cpu)
        rt_fpu_sync_host(cur_cpu);           /* rt_fpu_init loaded 0x37f into this host thread */
    c->host = (void *)(intptr_t)slot;
    if (thread_id)
        *thread_id = tid;
    return c;
}

void rt_cpu_free(Cpu *c)
{
    int slot = (int)(intptr_t)c->host;
    pthread_mutex_lock(&slot_lock);
    if (slot >= 0 && slot < RT_MAX_THREADS)
        slot_used[slot] = false;
    pthread_mutex_unlock(&slot_lock);
    free(c);
}
