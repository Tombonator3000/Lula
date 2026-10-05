/* Dispatch between guest code and host functions, traps, logging, handles. */
#include "rt.h"

#include <pthread.h>
#include <stdlib.h>

int rt_log_level = RT_LOG_WARN;
int rt_trace_imports;

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

void rt_log(int level, const char *fmt, ...)
{
    static const char *tag[] = {"error", "warn", "info", "trace"};
    if (level > rt_log_level)
        return;
    va_list ap;
    va_start(ap, fmt);
    pthread_mutex_lock(&log_lock);
    fprintf(stderr, "lula[%s] ", tag[level]);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    pthread_mutex_unlock(&log_lock);
    va_end(ap);
}

void rt_dump_cpu(Cpu *c, FILE *out)
{
    if (!c)
        return;
    fprintf(out, "  eax=%08x ecx=%08x edx=%08x ebx=%08x\n  esp=%08x ebp=%08x esi=%08x edi=%08x\n",
            c->eax, c->ecx, c->edx, c->ebx, c->esp, c->ebp, c->esi, c->edi);
    fprintf(out, "  cf=%u pf=%u af=%u zf=%u sf=%u of=%u df=%u fs=%08x thread=%u\n",
            c->cf, c->pf, c->af, c->zf, c->sf, c->of, c->df, c->fs_base, c->thread_id);
    if (rt_mem_is_committed(c->esp, 64)) {
        fprintf(out, "  stack:");
        for (int i = 0; i < 16; i++)
            fprintf(out, "%s%08x", i % 8 ? " " : "\n    ", R32(c->esp + 4 * (uint32_t)i));
        fputc('\n', out);
    }
}

void rt_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "lula[fatal] ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    rt_dump_cpu(rt_cpu_current(), stderr);
    fflush(stderr);
    abort();
}

void rt_trap(Cpu *c, uint32_t addr, const char *what)
{
    fprintf(stderr, "lula[trap] guest %08x: %s\n", addr, what);
    rt_dump_cpu(c, stderr);
    fflush(stderr);
    abort();
}

uint32_t rt_unimplemented(Cpu *c, const char *name)
{
    rt_fatal("unimplemented import %s (called from %08x)", name, R32(c->esp));
}

/* ---- eflags ---- */
uint32_t rt_get_eflags(Cpu *c)
{
    return c->cf | 2u | (c->pf << 2) | (c->af << 4) | (c->zf << 6) | (c->sf << 7) |
           0x200u | (c->df << 10) | (c->of << 11);
}

void rt_set_eflags(Cpu *c, uint32_t v)
{
    c->cf = v & 1;
    c->pf = (v >> 2) & 1;
    c->af = (v >> 4) & 1;
    c->zf = (v >> 6) & 1;
    c->sf = (v >> 7) & 1;
    c->df = (v >> 10) & 1;
    c->of = (v >> 11) & 1;
}

/* ---- thunks ---- */
typedef struct Thunk { const char *name; GuestFn fn; } Thunk;
static Thunk thunks[RT_MAX_THUNKS];
static uint32_t nthunks;
static pthread_mutex_t thunk_lock = PTHREAD_MUTEX_INITIALIZER;

uint32_t rt_register_thunk(const char *name, GuestFn fn)
{
    pthread_mutex_lock(&thunk_lock);
    for (uint32_t i = 0; i < nthunks; i++) {
        if (thunks[i].fn == fn && strcmp(thunks[i].name, name) == 0) {
            pthread_mutex_unlock(&thunk_lock);
            return RT_THUNK_BASE + i * RT_THUNK_STRIDE;
        }
    }
    if (nthunks == RT_MAX_THUNKS)
        rt_fatal("too many host thunks");
    uint32_t i = nthunks++;
    thunks[i] = (Thunk){name, fn};
    pthread_mutex_unlock(&thunk_lock);
    return RT_THUNK_BASE + i * RT_THUNK_STRIDE;
}

const char *rt_thunk_name(uint32_t addr)
{
    if (addr < RT_THUNK_BASE)
        return NULL;
    uint32_t i = (addr - RT_THUNK_BASE) / RT_THUNK_STRIDE;
    return i < nthunks ? thunks[i].name : NULL;
}

GuestFn rt_lookup_guest(uint32_t addr)
{
    size_t lo = 0, hi = g_guest_entry_count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (g_guest_entries[mid].addr < addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < g_guest_entry_count && g_guest_entries[lo].addr == addr)
        return g_guest_entries[lo].fn;
    return NULL;
}

static uint32_t call_thunk(Cpu *c, uint32_t target)
{
    uint32_t i = (target - RT_THUNK_BASE) / RT_THUNK_STRIDE;
    if (i >= nthunks || (target - RT_THUNK_BASE) % RT_THUNK_STRIDE)
        rt_trap(c, target, "call into the host thunk area outside a registered thunk");
    if (rt_trace_imports)
        rt_log(RT_LOG_INFO, "call %s from %08x args %08x %08x %08x %08x", thunks[i].name,
               R32(c->esp), R32(c->esp + 4), R32(c->esp + 8), R32(c->esp + 12), R32(c->esp + 16));
    uint32_t ra = thunks[i].fn(c);
    if (rt_trace_imports)
        rt_log(RT_LOG_INFO, "  %s -> %08x", thunks[i].name, c->eax);
    return ra;
}

uint32_t rt_call_indirect(Cpu *c, uint32_t target)
{
    if (target >= RT_THUNK_BASE && target < RT_THUNK_BASE + RT_MAX_THUNKS * RT_THUNK_STRIDE)
        return call_thunk(c, target);
    GuestFn fn = rt_lookup_guest(target);
    if (!fn) {
        char msg[160];
        snprintf(msg, sizeof msg, "indirect call to %08x, which is not a known function entry "
                 "(add it to tools/recomp/config.json extra_entries and regenerate)", target);
        rt_trap(c, target, msg);
    }
    return fn(c);
}

uint32_t rt_jump_indirect(Cpu *c, uint32_t target)
{
    if (target >= RT_THUNK_BASE && target < RT_THUNK_BASE + RT_MAX_THUNKS * RT_THUNK_STRIDE)
        return call_thunk(c, target);
    GuestFn fn = rt_lookup_guest(target);
    if (fn)
        return fn(c);
    /* Not a function: a jump to a return address (longjmp and friends).
     * Unwind the C frames until the call site that owns this address. */
    return target;
}

/* ---- host -> guest calls ---- */
uint32_t rt_guest_callv(Cpu *c, uint32_t fn, int nargs, const uint32_t *args)
{
    uint32_t saved[6] = {c->ebx, c->esi, c->edi, c->ebp, c->ecx, c->edx};
    uint32_t esp = c->esp;
    for (int i = nargs - 1; i >= 0; i--)
        PUSH32(args[i]);
    PUSH32(RT_MAGIC_RET);
    uint32_t ra = rt_call_indirect(c, fn);
    if (ra != RT_MAGIC_RET) {
        char msg[128];
        snprintf(msg, sizeof msg, "guest callback %08x returned to %08x instead of the host", fn, ra);
        rt_trap(c, fn, msg);
    }
    if (c->esp != esp && c->esp != esp - 4u * (uint32_t)nargs)
        RT_WARN("callback %08x left esp off by %d", fn, (int)(c->esp - esp));
    c->esp = esp;
    c->ebx = saved[0]; c->esi = saved[1]; c->edi = saved[2]; c->ebp = saved[3];
    c->ecx = saved[4]; c->edx = saved[5];
    return c->eax;
}

uint32_t rt_guest_call(Cpu *c, uint32_t fn, int nargs, ...)
{
    uint32_t args[16];
    va_list ap;
    va_start(ap, nargs);
    for (int i = 0; i < nargs && i < 16; i++)
        args[i] = va_arg(ap, uint32_t);
    va_end(ap);
    return rt_guest_callv(c, fn, nargs, args);
}

/* ---- kernel handles ---- */
#define MAX_HANDLES 1024
static HandleObj handles[MAX_HANDLES];
static pthread_mutex_t handle_lock = PTHREAD_MUTEX_INITIALIZER;

/* Handles are small multiples of 4 above 0x100, never 0 or -1. */
uint32_t rt_handle_new(HandleKind kind, void *ptr)
{
    pthread_mutex_lock(&handle_lock);
    for (uint32_t i = 1; i < MAX_HANDLES; i++) {
        if (handles[i].kind == H_FREE) {
            handles[i] = (HandleObj){kind, ptr, 1};
            pthread_mutex_unlock(&handle_lock);
            return 0x100 + i * 4;
        }
    }
    pthread_mutex_unlock(&handle_lock);
    return 0;
}

HandleObj *rt_handle_get(uint32_t h, HandleKind kind)
{
    if (h < 0x100 || (h - 0x100) % 4)
        return NULL;
    uint32_t i = (h - 0x100) / 4;
    if (i >= MAX_HANDLES || handles[i].kind == H_FREE)
        return NULL;
    if (kind != H_FREE && handles[i].kind != kind)
        return NULL;
    return &handles[i];
}

void rt_handle_close(uint32_t h)
{
    HandleObj *o = rt_handle_get(h, H_FREE);
    if (o)
        memset(o, 0, sizeof *o);
}

/* ---- coverage ---- */
/* LULA_COVERAGE=file: write "address calls" for every function that ran.
 * Called periodically by the platform loop and at exit, so a killed test
 * run still leaves a recent report. */
void rt_coverage_dump(void)
{
    const char *path = getenv("LULA_COVERAGE");
    if (!path)
        return;
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    for (uint32_t i = 0; i < rt_cov_count && i < g_guest_entry_count; i++)
        if (rt_cov[i])
            fprintf(f, "%08x %u\n", g_guest_entries[i].addr, rt_cov[i]);
    fclose(f);
    rename(tmp, path);
}
