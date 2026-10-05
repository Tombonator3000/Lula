/* Runtime-internal interfaces (host side). Generated code only needs rt_cpu.h. */
#pragma once
#include "rt_cpu.h"
#include "rt_tables.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

/* ---- guest address space layout ---- */
#define RT_LOW_HEAP_BASE   0x00010000u   /* TEB/PEB, strings, small host objects */
#define RT_LOW_HEAP_END    0x00100000u
#define RT_STACKS_BASE     0x01000000u   /* one 1 MiB stack per guest thread */
#define RT_STACK_SIZE      0x00100000u
#define RT_MAX_THREADS     16
#define RT_HOST_HEAP_BASE  0x08000000u   /* COM objects, surfaces, sound buffers */
#define RT_HOST_HEAP_END   0x10000000u
#define RT_VALLOC_BASE     0x10000000u   /* VirtualAlloc arena */
#define RT_VALLOC_END      0x70000000u
#define RT_THUNK_BASE      0xf0000000u   /* host function addresses */
#define RT_THUNK_STRIDE    16u
#define RT_MAX_THUNKS      4096u
#define RT_MAGIC_RET       0xf00ffff0u   /* return address of host->guest calls */

/* ---- memory (rt_mem.c) ---- */
void rt_mem_init(void);
bool rt_mem_commit(uint32_t addr, uint32_t size);
void rt_mem_decommit(uint32_t addr, uint32_t size);
bool rt_mem_is_committed(uint32_t addr, uint32_t size);
uint32_t rt_low_alloc(uint32_t size);                 /* never freed */
uint32_t rt_heap_alloc(uint32_t size);                /* host heap, zeroed, 16-byte aligned */
void rt_heap_free(uint32_t addr);
uint32_t rt_valloc(uint32_t addr, uint32_t size, uint32_t type, uint32_t protect);
bool rt_vfree(uint32_t addr, uint32_t size, uint32_t type);
uint32_t rt_guest_strdup(const char *s);               /* low heap copy */
static inline char *rt_gstr(uint32_t a) { return (char *)G2H(a); }

/* ---- thunks and dispatch (rt_core.c) ---- */
uint32_t rt_register_thunk(const char *name, GuestFn fn);
GuestFn rt_lookup_guest(uint32_t addr);
const char *rt_thunk_name(uint32_t addr);
uint32_t rt_guest_call(Cpu *c, uint32_t fn, int nargs, ...);
uint32_t rt_guest_callv(Cpu *c, uint32_t fn, int nargs, const uint32_t *args);

/* stdcall helpers for host implementations of imports */
static inline uint32_t rt_arg(Cpu *c, int i) { return R32(c->esp + 4 + 4 * (uint32_t)i); }
static inline uint32_t rt_ret_stdcall(Cpu *c, int nargs, uint32_t value)
{
    uint32_t ra = R32(c->esp);
    c->eax = value;
    c->esp += 4 + 4 * (uint32_t)nargs;
    return ra;
}
static inline uint32_t rt_ret_cdecl(Cpu *c, uint32_t value)
{
    uint32_t ra = R32(c->esp);
    c->eax = value;
    c->esp += 4;
    return ra;
}

/* ---- threads and the global interpreter lock (rt_thread.c) ---- */
extern volatile int rt_gil_waiters;
void rt_gil_init(void);
void rt_gil_acquire(void);
void rt_gil_release(void);
void rt_gil_yield(void);
Cpu *rt_cpu_current(void);
Cpu *rt_cpu_new_thread(uint32_t *thread_id);
void rt_cpu_bind(Cpu *c);
void rt_cpu_free(Cpu *c);
#define RT_BLOCKING_BEGIN() rt_gil_release()
#define RT_BLOCKING_END() rt_gil_acquire()

/* ---- loader (rt_loader.c) ---- */
bool rt_load_image(const char *exe_path);
uint32_t rt_command_line(void);
void rt_set_command_line(const char *args);

/* ---- file system mapping (rt_vfs.c) ---- */
void rt_vfs_init(const char *data_dir, const char *save_dir);
/* Map a guest path for reading: returns malloc'd host path or NULL. */
char *rt_vfs_resolve_read(const char *guest_path);
/* Map a guest path for writing (creating parent dirs in the save overlay). */
char *rt_vfs_resolve_write(const char *guest_path, bool copy_existing);
const char *rt_vfs_game_dir_dos(void);    /* e.g. "C:\\LULA" */
const char *rt_vfs_data_dir(void);
const char *rt_vfs_save_dir(void);

/* ---- logging ---- */
enum { RT_LOG_ERR = 0, RT_LOG_WARN = 1, RT_LOG_INFO = 2, RT_LOG_TRACE = 3 };
extern int rt_log_level;
extern int rt_trace_imports;
void rt_log(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define RT_WARN(...) rt_log(RT_LOG_WARN, __VA_ARGS__)
#define RT_INFO(...) rt_log(RT_LOG_INFO, __VA_ARGS__)
#define RT_TRACE(...) do { if (rt_log_level >= RT_LOG_TRACE) rt_log(RT_LOG_TRACE, __VA_ARGS__); } while (0)
void rt_dump_cpu(Cpu *c, FILE *out);
RT_NORETURN void rt_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- handles (rt_core.c) ---- */
typedef enum { H_FREE = 0, H_FILE, H_EVENT, H_MUTEX, H_THREAD, H_FIND, H_OTHER } HandleKind;
typedef struct HandleObj {
    HandleKind kind;
    void *ptr;
    uint32_t refs;
} HandleObj;
uint32_t rt_handle_new(HandleKind kind, void *ptr);
HandleObj *rt_handle_get(uint32_t h, HandleKind kind);
void rt_handle_close(uint32_t h);

/* ---- initialisation of the API modules ---- */
void rt_platform_init(void);           /* SDL video/audio/events */
void rt_platform_run(void);            /* main-thread event loop; never returns */
void rt_platform_shutdown(void);
RT_NORETURN void rt_exit_process(uint32_t code);
