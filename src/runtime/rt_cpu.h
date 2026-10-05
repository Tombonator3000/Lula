/* CPU state and memory model shared by generated and hand-written code.
 *
 * Guest memory is a flat 4 GiB reservation: guest address A lives at
 * g_mem + A. Generated functions take the Cpu of the current guest thread.
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stddef.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The runtime assumes a little-endian host"
#endif

#if defined(_WIN32) && !defined(__clang__)
#define RT_WEAK __attribute__((weak))
#else
#define RT_WEAK __attribute__((weak))
#endif
#define RT_NORETURN __attribute__((noreturn))

/* x87 registers are host long doubles. With GCC/Clang on x86 hosts that is
 * the x87 80-bit format itself: loads, stores and moves are exact, and the
 * arithmetic runs on the host x87, whose precision and rounding control
 * follow the guest control word (rt_fpu_set_cw). Other hosts round every
 * register to their long double precision and ignore precision control
 * (rt_fpu.c emits a #warning there). */
typedef long double fpreg_t;
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384 && !defined(RT_FPU_FORCE_SOFT)
#define RT_FPREG_X87 1
#define RT_FPREG_BYTES 10                /* significant bytes; the rest is padding */
#else
#define RT_FPREG_X87 0
#define RT_FPREG_BYTES sizeof(fpreg_t)
#endif
#if RT_FPREG_X87 && defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#define RT_HOST_X87 1                    /* x87 instructions and control word on the host */
#else
#define RT_HOST_X87 0
#endif

typedef struct Fpu {
    fpreg_t st[8];
    uint32_t top;
    uint16_t cw;           /* control word; change it with rt_fpu_set_cw */
    uint16_t sw;           /* exception bits; TOP and C0-C3 are kept apart */
    uint8_t c0, c1, c2, c3;
} Fpu;

#define REG32(n32, n16, n8l, n8h) \
    union { uint32_t n32; uint16_t n16; struct { uint8_t n8l, n8h; }; }
#define REG32X(n32, n16) union { uint32_t n32; uint16_t n16; }

typedef struct Cpu Cpu;
struct Cpu {
    REG32(eax, ax, al, ah);
    REG32(ecx, cx, cl, ch);
    REG32(edx, dx, dl, dh);
    REG32(ebx, bx, bl, bh);
    REG32X(esp, sp);
    REG32X(ebp, bp);
    REG32X(esi, si);
    REG32X(edi, di);
    uint32_t cf, pf, af, zf, sf, of, df;
    uint32_t fs_base;
    Fpu fpu;
    /* Runtime bookkeeping, not touched by generated code. */
    uint32_t thread_id;
    uint32_t stack_base, stack_limit;
    void *host;            /* owning host thread record */
    uint32_t last_error;
    uint32_t call_depth;
};

typedef uint32_t (*GuestFn)(Cpu *c);

extern uint8_t *g_mem;

static inline uint8_t R8(uint32_t a) { return g_mem[a]; }
static inline uint16_t R16(uint32_t a) { uint16_t v; memcpy(&v, g_mem + a, 2); return v; }
static inline uint32_t R32(uint32_t a) { uint32_t v; memcpy(&v, g_mem + a, 4); return v; }
static inline uint64_t R64(uint32_t a) { uint64_t v; memcpy(&v, g_mem + a, 8); return v; }
static inline void W8(uint32_t a, uint8_t v) { g_mem[a] = v; }
static inline void W16(uint32_t a, uint16_t v) { memcpy(g_mem + a, &v, 2); }
static inline void W32(uint32_t a, uint32_t v) { memcpy(g_mem + a, &v, 4); }
static inline void W64(uint32_t a, uint64_t v) { memcpy(g_mem + a, &v, 8); }

/* Host pointer for a guest address (NULL stays NULL). */
static inline void *G2H(uint32_t a) { return a ? (void *)(g_mem + a) : NULL; }

#define PUSH32(v) do { uint32_t _pv = (uint32_t)(v); c->esp -= 4; W32(c->esp, _pv); } while (0)
#define PARITY(v) rt_parity[(uint8_t)(v)]

extern const uint8_t rt_parity[256];

/* Loop back-edges: let a guest thread that waits for the global lock run. */
extern volatile int rt_gil_waiters;
void rt_gil_yield(void);
#define RT_POLL() do { if (__builtin_expect(rt_gil_waiters != 0, 0)) rt_gil_yield(); } while (0)

/* --- runtime services used by generated code --- */
uint32_t rt_call_indirect(Cpu *c, uint32_t target);
uint32_t rt_jump_indirect(Cpu *c, uint32_t target);
RT_NORETURN void rt_trap(Cpu *c, uint32_t addr, const char *what);
uint32_t rt_unimplemented(Cpu *c, const char *name);
uint32_t rt_get_eflags(Cpu *c);
void rt_set_eflags(Cpu *c, uint32_t v);

/* --- x87 --- */
#define ST(i) (c->fpu.st[(c->fpu.top + (i)) & 7])
#define FPUSH(v) do { fpreg_t _fv = (v); c->fpu.top = (c->fpu.top - 1) & 7; c->fpu.st[c->fpu.top] = _fv; } while (0)
#define FPOP() do { c->fpu.top = (c->fpu.top + 1) & 7; } while (0)

/* Conversions round with the host rounding control, which follows the guest's. */
static inline fpreg_t rt_f32(uint32_t bits) { float f; memcpy(&f, &bits, 4); return (fpreg_t)f; }
static inline fpreg_t rt_f64(uint64_t bits) { double d; memcpy(&d, &bits, 8); return (fpreg_t)d; }
static inline uint32_t rt_f32_bits(fpreg_t v) { float f = (float)v; uint32_t b; memcpy(&b, &f, 4); return b; }
static inline uint64_t rt_f64_bits(fpreg_t v) { double d = (double)v; uint64_t b; memcpy(&b, &d, 8); return b; }

fpreg_t rt_f80_load(uint32_t addr);
void rt_f80_store(uint32_t addr, fpreg_t v);
int64_t rt_fist(Cpu *c, fpreg_t v, int bits);
fpreg_t rt_frndint(Cpu *c, fpreg_t v);
void rt_fprem(Cpu *c);
void rt_fsin(Cpu *c);
void rt_fcos(Cpu *c);
void rt_fyl2x(Cpu *c);
/* fldl2t, fldl2e, fldpi, fldlg2, fldln2: rounded with the guest rounding control. */
enum { RT_FLDL2T, RT_FLDL2E, RT_FLDPI, RT_FLDLG2, RT_FLDLN2 };
fpreg_t rt_fpu_const(Cpu *c, int which);
void rt_fpu_init(Cpu *c);
void rt_fpu_set_cw(Cpu *c, uint16_t cw);
/* Load the guest control word's precision and rounding control into the
 * host x87 of the calling thread (after binding a Cpu to a host thread or
 * copying a Cpu). No-op on hosts without an x87. */
void rt_fpu_sync_host(Cpu *c);
void rt_fpu_save(Cpu *c, uint32_t addr);
void rt_fpu_restore(Cpu *c, uint32_t addr);

static inline void rt_fcom(Cpu *c, fpreg_t a, fpreg_t b)
{
    if (a != a || b != b) { c->fpu.c0 = c->fpu.c2 = c->fpu.c3 = 1; return; }
    c->fpu.c2 = 0;
    c->fpu.c0 = a < b;
    c->fpu.c3 = a == b;
}

static inline uint16_t rt_fpu_sw(Cpu *c)
{
    return (uint16_t)((c->fpu.sw & 0x80ffu) | (c->fpu.c0 << 8) | (c->fpu.c1 << 9) |
                      (c->fpu.c2 << 10) | ((c->fpu.top & 7) << 11) | (c->fpu.c3 << 14));
}

/* --- string instructions --- */
#define RT_STEP(n) (c->df ? (uint32_t)-(n) : (uint32_t)(n))

#define RT_STRING_SIZE(N, T, RD, WR, AREG)                                                    \
static inline void rt_movs##N(Cpu *c) { WR(c->edi, RD(c->esi)); c->esi += RT_STEP(N); c->edi += RT_STEP(N); } \
static inline void rt_rep_movs##N(Cpu *c) { while (c->ecx) { rt_movs##N(c); c->ecx--; } }       \
static inline void rt_stos##N(Cpu *c) { WR(c->edi, (T)c->AREG); c->edi += RT_STEP(N); }         \
static inline void rt_rep_stos##N(Cpu *c) { while (c->ecx) { rt_stos##N(c); c->ecx--; } }       \
static inline void rt_lods##N(Cpu *c) { c->AREG = RD(c->esi); c->esi += RT_STEP(N); }           \
static inline void rt_rep_lods##N(Cpu *c) { while (c->ecx) { rt_lods##N(c); c->ecx--; } }

RT_STRING_SIZE(1, uint8_t, R8, W8, al)
RT_STRING_SIZE(2, uint16_t, R16, W16, ax)
RT_STRING_SIZE(4, uint32_t, R32, W32, eax)

static inline void rt_cmp_flags(Cpu *c, uint32_t a, uint32_t b, int bits)
{
    uint32_t mask = bits == 32 ? 0xffffffffu : ((1u << bits) - 1);
    uint32_t r = (a - b) & mask;
    c->cf = a < b;
    c->zf = r == 0;
    c->sf = (r >> (bits - 1)) & 1;
    c->of = (((a ^ b) & (a ^ r)) >> (bits - 1)) & 1;
    c->af = ((a ^ b ^ r) >> 4) & 1;
    c->pf = PARITY(r);
}

#define RT_SCAN_SIZE(N, RD, AREG, BITS)                                                       \
static inline void rt_scas##N(Cpu *c) { rt_cmp_flags(c, c->AREG, RD(c->edi), BITS); c->edi += RT_STEP(N); } \
static inline void rt_repe_scas##N(Cpu *c) { while (c->ecx) { rt_scas##N(c); c->ecx--; if (!c->zf) break; } } \
static inline void rt_repne_scas##N(Cpu *c) { while (c->ecx) { rt_scas##N(c); c->ecx--; if (c->zf) break; } } \
static inline void rt_cmps##N(Cpu *c) { rt_cmp_flags(c, RD(c->esi), RD(c->edi), BITS); c->esi += RT_STEP(N); c->edi += RT_STEP(N); } \
static inline void rt_repe_cmps##N(Cpu *c) { while (c->ecx) { rt_cmps##N(c); c->ecx--; if (!c->zf) break; } } \
static inline void rt_repne_cmps##N(Cpu *c) { while (c->ecx) { rt_cmps##N(c); c->ecx--; if (c->zf) break; } }

RT_SCAN_SIZE(1, R8, al, 8)
RT_SCAN_SIZE(2, R16, ax, 16)
RT_SCAN_SIZE(4, R32, eax, 32)
