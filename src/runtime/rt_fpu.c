/* x87 helpers and the parity table used by generated code (see rt_cpu.h).
 *
 * Registers are fpreg_t (long double). On x86 hosts that is the x87 80-bit
 * format, so 80-bit loads and stores are plain copies. The host x87 does the
 * arithmetic, and its precision and rounding control follow the guest
 * control word (rt_fpu_set_cw / rt_fpu_sync_host). The instructions whose
 * results only a real x87 defines (fsin, fcos, fyl2x, fprem, fist,
 * frndint) run as the host instruction. Other hosts use the conversion
 * code and long double libm below. Verified against unicorn and the host x87
 * by tests/recomp/unicorn_diff.py.
 */
#include "rt_cpu.h"

#if !RT_FPREG_X87 && !defined(RT_FPU_FORCE_SOFT)
#warning "long double is not the x87 80-bit format on this host: x87 registers are rounded to its precision and precision control is ignored"
#endif

/* 1 when the byte has an even number of set bits (EFLAGS.PF). */
#define P2(n) n, n ^ 1, n ^ 1, n
#define P4(n) P2(n), P2(n ^ 1), P2(n ^ 1), P2(n)
#define P6(n) P4(n), P4(n ^ 1), P4(n ^ 1), P4(n)
const uint8_t rt_parity[256] = { P6(1), P6(0), P6(0), P6(1) };
#undef P2
#undef P4
#undef P6

/* ------------------------------------------------ 80-bit representation */

/* Mantissa (explicit integer bit) and sign/exponent word of a register. */
static void f80_split(fpreg_t v, uint64_t *mp, uint16_t *sep)
{
#if RT_FPREG_X87
    uint8_t b[sizeof v];
    memcpy(b, &v, sizeof v);
    memcpy(mp, b, 8);
    memcpy(sep, b + 8, 2);
#else
    uint16_t sign = signbit(v) ? 0x8000 : 0;
    uint64_t m;
    int e;
    if (v != v) {
        m = 0xc000000000000000ull;                 /* quiet NaN */
        e = 0x7fff;
    } else if (v - v != 0) {
        m = 1ull << 63;                            /* infinity */
        e = 0x7fff;
    } else if (v == 0) {
        m = 0;
        e = 0;
    } else {
        int x, shift = 64;
        fpreg_t f = frexpl(fabsl(v), &x);          /* |v| = f * 2^x, f in [0.5, 1) */
        e = x - 1 + 16383;
        if (e < 1) {                               /* extended denormal */
            shift = 63 + e;
            e = 0;
        }
        fpreg_t mf = ldexpl(f, shift), fl = floorl(mf), d = mf - fl;
        m = (uint64_t)fl;
        if (d > 0.5L || (d == 0.5L && (m & 1))) {  /* round to nearest even */
            if (++m == 0) {
                m = 1ull << 63;
                e++;
            }
        }
        if (e == 0 && (m >> 63))
            e = 1;                                 /* rounded up to the smallest normal */
        if (e >= 0x7fff) {
            m = 1ull << 63;
            e = 0x7fff;
        }
    }
    *mp = m;
    *sep = (uint16_t)(sign | e);
#endif
}

static fpreg_t f80_join(uint64_t m, uint16_t se)
{
#if RT_FPREG_X87
    fpreg_t v;
    uint8_t b[sizeof v];
    memset(b, 0, sizeof b);
    memcpy(b, &m, 8);
    memcpy(b + 8, &se, 2);
    memcpy(&v, b, sizeof v);
    return v;
#else
    int neg = se >> 15, e = se & 0x7fff;
    fpreg_t v;
    if (e == 0x7fff) {
        v = (m << 1) == 0 ? (fpreg_t)INFINITY : (fpreg_t)NAN;
    } else if (m == 0) {
        v = 0;
    } else if (LDBL_MANT_DIG >= 64) {
        v = ldexpl((fpreg_t)m, (e ? e : 1) - 16383 - 63);   /* exact */
    } else {
        /* long double is double: round the 64-bit mantissa to nearest even,
         * also in the double denormal range. */
        int ex = (e ? e : 1) - 16383 - 63;
        int p = 63 - __builtin_clzll(m);           /* index of the top set bit */
        int top = p + ex;                          /* value in [2^top, 2^(top+1)) */
        int drop;
        if (top > 1023)
            return neg ? -(fpreg_t)INFINITY : (fpreg_t)INFINITY;
        drop = top >= -1022 ? p + 1 - 53 : -1074 - ex;
        if (drop <= 0) {
            v = ldexpl((fpreg_t)m, ex);
        } else if (drop > 64) {
            v = 0;
        } else {
            uint64_t q, rem, half;
            if (drop == 64) {
                q = 0; rem = m; half = 1ull << 63;
            } else {
                q = m >> drop; rem = m & ((1ull << drop) - 1); half = 1ull << (drop - 1);
            }
            if (rem > half || (rem == half && (q & 1)))
                q++;
            v = ldexpl((fpreg_t)q, ex + drop);
        }
    }
    return neg ? -v : v;
#endif
}

fpreg_t rt_f80_load(uint32_t addr)
{
    return f80_join(R64(addr), R16(addr + 8));
}

void rt_f80_store(uint32_t addr, fpreg_t v)
{
    uint64_t m;
    uint16_t se;
    f80_split(v, &m, &se);
    W64(addr, m);
    W16(addr + 8, se);
}

/* ------------------------------------------------------ control word */

void rt_fpu_sync_host(Cpu *c)
{
#if RT_HOST_X87
    /* Precision and rounding control from the guest; every exception stays
     * masked on the host. */
    uint16_t cw = (uint16_t)((c->fpu.cw & 0x0f00u) | 0x007fu);
    __asm__ volatile("fldcw %0" : : "m"(cw) : "memory");
#else
    (void)c;
#endif
}

void rt_fpu_set_cw(Cpu *c, uint16_t cw)
{
    c->fpu.cw = cw;
    rt_fpu_sync_host(c);
}

void rt_fpu_init(Cpu *c)
{
    c->fpu.sw = 0;
    c->fpu.top = 0;
    c->fpu.c0 = c->fpu.c1 = c->fpu.c2 = c->fpu.c3 = 0;
    rt_fpu_set_cw(c, 0x037f);
}

/* ------------------------------------------------------- instructions */

#if RT_HOST_X87
/* Run a host instruction with the guest's rounding control at 64-bit
 * precision, independent of the current host control word. */
#define WITH_GUEST_RC(c, body) do {                                              \
        uint16_t _old, _cw = (uint16_t)(((c)->fpu.cw & 0x0c00u) | 0x037fu);      \
        __asm__ volatile("fnstcw %0" : "=m"(_old));                              \
        __asm__ volatile("fldcw %0" : : "m"(_cw));                               \
        body;                                                                    \
        __asm__ volatile("fldcw %0" : : "m"(_old));                              \
    } while (0)
#else
/* Round to an integral value with the x87 rounding control. */
static fpreg_t fpu_round(Cpu *c, fpreg_t v)
{
    fpreg_t f;
    switch ((c->fpu.cw >> 10) & 3) {
    case 1: return floorl(v);
    case 2: return ceill(v);
    case 3: return truncl(v);
    default:
        if (v != v || v - v != 0)                  /* NaN or infinity */
            return v;
        f = floorl(v);
        {
            fpreg_t d = v - f;
            if (d > 0.5L || (d == 0.5L && fmodl(f, 2.0L) != 0))
                f += 1.0L;
        }
        return f == 0 ? copysignl(0.0L, v) : f;
    }
}
#endif

fpreg_t rt_frndint(Cpu *c, fpreg_t v)
{
#if RT_HOST_X87
    WITH_GUEST_RC(c, __asm__ volatile("frndint" : "+t"(v)));
    return v;
#else
    return fpu_round(c, v);
#endif
}

/* fist/fistp: the integer in the low 'bits' bits of the result; out of range
 * and NaN give the integer indefinite (0x8000, 0x80000000, ...). */
int64_t rt_fist(Cpu *c, fpreg_t v, int bits)
{
#if RT_HOST_X87
    int64_t r;
    if (bits == 16) {
        int16_t s;
        WITH_GUEST_RC(c, __asm__ volatile("fistps %0" : "=m"(s) : "t"(v) : "st"));
        r = s;
    } else if (bits == 32) {
        int32_t s;
        WITH_GUEST_RC(c, __asm__ volatile("fistpl %0" : "=m"(s) : "t"(v) : "st"));
        r = s;
    } else {
        WITH_GUEST_RC(c, __asm__ volatile("fistpll %0" : "=m"(r) : "t"(v) : "st"));
    }
    return r;
#else
    fpreg_t r = fpu_round(c, v);
    fpreg_t lim = ldexpl(1.0L, bits - 1);
    int64_t indefinite = (int64_t)(UINT64_C(0xffffffffffffffff) << (bits - 1));
    if (r != r || r < -lim || r >= lim)
        return indefinite;
    return (int64_t)r;
#endif
}

static void set_c(Cpu *c, uint16_t sw)
{
    c->fpu.c0 = (sw >> 8) & 1;
    c->fpu.c1 = (sw >> 9) & 1;
    c->fpu.c2 = (sw >> 10) & 1;
    c->fpu.c3 = (sw >> 14) & 1;
}

/* fprem: truncating remainder of ST(0) by ST(1), quotient bits Q2, Q1, Q0 in
 * C0, C3, C1. The host instruction may leave a partial remainder (C2 = 1)
 * when the exponents differ by 64 or more, exactly like the guest's x87; the
 * fallback always completes (C2 = 0), which a fprem loop cannot tell apart. */
void rt_fprem(Cpu *c)
{
    fpreg_t a = ST(0), b = ST(1);
#if RT_HOST_X87
    uint16_t sw;
    __asm__("fprem\n\tfnstsw %1" : "+t"(a), "=a"(sw) : "u"(b));
    ST(0) = a;
    set_c(c, sw);
#else
    fpreg_t r = fmodl(a, b);
    unsigned q = 0;
    if (r == r && b - b == 0) {                    /* finite operands, b != 0 */
        fpreg_t b8 = 8.0L * b;
        fpreg_t r8 = b8 - b8 == 0 ? fmodl(a, b8) : a; /* 8b overflows: |a / b| < 8 */
        q = (unsigned)floorl(fabsl((r8 - r) / b) + 0.5L) & 7;
    }
    ST(0) = r;
    c->fpu.c2 = 0;
    c->fpu.c0 = (q >> 2) & 1;
    c->fpu.c3 = (q >> 1) & 1;
    c->fpu.c1 = q & 1;
#endif
}

/* The x87 keeps these constants with more than 64 bits and rounds them with
 * the rounding control (Intel SDM vol. 1, 8.3.5): 'down' is the truncated
 * 64-bit mantissa; round to nearest adds one unit where 'up' is set. */
fpreg_t rt_fpu_const(Cpu *c, int which)
{
    static const struct { uint64_t down; uint16_t se; uint8_t nearest_up; } k[] = {
        [RT_FLDL2T] = {0xd49a784bcd1b8afeull, 0x4000, 0},   /* log2(10) */
        [RT_FLDL2E] = {0xb8aa3b295c17f0bbull, 0x3fff, 1},   /* log2(e) */
        [RT_FLDPI]  = {0xc90fdaa22168c234ull, 0x4000, 1},   /* pi */
        [RT_FLDLG2] = {0x9a209a84fbcff798ull, 0x3ffd, 1},   /* log10(2) */
        [RT_FLDLN2] = {0xb17217f7d1cf79abull, 0x3ffe, 1},   /* ln(2) */
    };
    unsigned rc = (c->fpu.cw >> 10) & 3;
    uint64_t m = k[which].down + (rc == 2 ? 1 : rc == 0 ? k[which].nearest_up : 0);
    return f80_join(m, k[which].se);
}

/* fsin/fcos: |ST(0)| >= 2^63 leaves the operand and sets C2. */
void rt_fsin(Cpu *c)
{
    fpreg_t x = ST(0);
#if RT_HOST_X87
    uint16_t sw;
    __asm__("fsin\n\tfnstsw %1" : "+t"(x), "=a"(sw));
    c->fpu.c2 = (sw >> 10) & 1;
#else
    c->fpu.c2 = x - x == 0 && !(fabsl(x) < 0x1p63L);
    if (!c->fpu.c2)
        x = sinl(x);
#endif
    ST(0) = x;
}

void rt_fcos(Cpu *c)
{
    fpreg_t x = ST(0);
#if RT_HOST_X87
    uint16_t sw;
    __asm__("fcos\n\tfnstsw %1" : "+t"(x), "=a"(sw));
    c->fpu.c2 = (sw >> 10) & 1;
#else
    c->fpu.c2 = x - x == 0 && !(fabsl(x) < 0x1p63L);
    if (!c->fpu.c2)
        x = cosl(x);
#endif
    ST(0) = x;
}

/* fyl2x: ST(1) = ST(1) * log2(ST(0)), pop. The x87 rounds once. */
void rt_fyl2x(Cpu *c)
{
    fpreg_t x = ST(0), y = ST(1), r;
#if RT_HOST_X87
    __asm__("fyl2x" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
#else
    r = y * log2l(x);
#endif
    FPOP();
    ST(0) = r;
}

/* ------------------------------------------------------- save/restore */

/* Tag of a register as fnsave reports it: 0 valid, 1 zero, 2 special. */
static uint32_t fpu_tag(fpreg_t v)
{
    uint64_t m;
    uint16_t se;
    f80_split(v, &m, &se);
    int e = se & 0x7fff;
    if (e == 0 && m == 0)
        return 1;
    if (e == 0 || e == 0x7fff || !(m >> 63))
        return 2;                                  /* NaN, infinity, denormal, unnormal */
    return 0;
}

/* fnsave/fsave, 32-bit protected-mode layout (108 bytes). The register
 * stack is not tracked for emptiness, so every register is saved as in use.
 * The instruction/operand pointer fields are written as zero. */
void rt_fpu_save(Cpu *c, uint32_t addr)
{
    uint32_t tw = 0;
    for (int i = 0; i < 8; i++)
        tw |= fpu_tag(c->fpu.st[i]) << (2 * i);
    W32(addr, c->fpu.cw);
    W32(addr + 4, rt_fpu_sw(c));
    W32(addr + 8, tw);
    for (int i = 12; i < 28; i += 4)
        W32(addr + i, 0);
    for (int i = 0; i < 8; i++)
        rt_f80_store(addr + 28 + 10 * i, ST(i));
    rt_fpu_init(c);
}

void rt_fpu_restore(Cpu *c, uint32_t addr)
{
    uint16_t sw = R16(addr + 4);
    c->fpu.sw = sw & 0x80ffu;
    c->fpu.top = (sw >> 11) & 7;
    set_c(c, sw);
    for (int i = 0; i < 8; i++)
        ST(i) = rt_f80_load(addr + 28 + 10 * i);
    rt_fpu_set_cw(c, R16(addr));
}
