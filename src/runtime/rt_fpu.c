/* x87 helpers and the parity table used by generated code (see rt_cpu.h).
 *
 * The register stack is kept in host doubles. Conversions to and from the
 * 80-bit extended format are exact where the value fits a double and
 * round-to-nearest-even otherwise. Rounding for fist/frndint follows the
 * rounding-control field (bits 10-11) of the guest control word. Verified
 * against unicorn by tests/recomp/unicorn_diff.py.
 */
#include "rt_cpu.h"

/* 1 when the byte has an even number of set bits (EFLAGS.PF). */
#define P2(n) n, n ^ 1, n ^ 1, n
#define P4(n) P2(n), P2(n ^ 1), P2(n ^ 1), P2(n)
#define P6(n) P4(n), P4(n ^ 1), P4(n ^ 1), P4(n)
const uint8_t rt_parity[256] = { P6(1), P6(0), P6(0), P6(1) };
#undef P2
#undef P4
#undef P6

double rt_f80_load(uint32_t addr)
{
    uint64_t m = R64(addr);
    uint16_t se = R16(addr + 8);
    int neg = se >> 15, e = se & 0x7fff;
    double v;
    if (e == 0x7fff) {
        if ((m << 1) == 0) {
            v = INFINITY;
        } else {
            /* NaN: keep the top payload bits and force a quiet NaN. */
            uint64_t b = 0x7ff8000000000000ull | ((m >> 11) & 0x000fffffffffffffull);
            memcpy(&v, &b, 8);
        }
    } else if (m == 0) {
        v = 0.0;
    } else {
        /* value = m * 2^ex; denormals use the minimum exponent. */
        int ex = (e ? e : 1) - 16383 - 63;
        int p = 63 - __builtin_clzll(m);           /* index of the top set bit */
        int top = p + ex;                          /* value in [2^top, 2^(top+1)) */
        int drop;
        if (top > 1023) {
            v = INFINITY;
            return neg ? -v : v;
        }
        drop = top >= -1022 ? p + 1 - 53 : -1074 - ex;
        if (drop <= 0) {
            v = ldexp((double)m, ex);
        } else if (drop > 64) {
            v = 0.0;
        } else {
            uint64_t q, rem, half;
            if (drop == 64) {
                q = 0; rem = m; half = 1ull << 63;
            } else {
                q = m >> drop; rem = m & ((1ull << drop) - 1); half = 1ull << (drop - 1);
            }
            if (rem > half || (rem == half && (q & 1)))
                q++;
            v = ldexp((double)q, ex + drop);
        }
    }
    return neg ? -v : v;
}

void rt_f80_store(uint32_t addr, double v)
{
    uint64_t b, m;
    uint16_t se;
    memcpy(&b, &v, 8);
    int neg = (int)(b >> 63), e = (int)((b >> 52) & 0x7ff);
    uint64_t frac = b & 0x000fffffffffffffull;
    if (e == 0x7ff) {
        m = (1ull << 63) | (frac << 11);
        se = 0x7fff;
    } else if (e == 0) {
        if (frac == 0) {
            m = 0;
            se = 0;
        } else {
            /* Double denormal: normal in the extended format. */
            int p = 63 - __builtin_clzll(frac);
            m = frac << (63 - p);
            se = (uint16_t)(p - 1074 + 16383);
        }
    } else {
        m = (1ull << 63) | (frac << 11);
        se = (uint16_t)(e - 1023 + 16383);
    }
    if (neg)
        se |= 0x8000;
    W64(addr, m);
    W16(addr + 8, se);
}

/* Round to an integral value with the x87 rounding control. */
static double fpu_round(Cpu *c, double v)
{
    double f;
    switch ((c->fpu.cw >> 10) & 3) {
    case 1: return floor(v);
    case 2: return ceil(v);
    case 3: return trunc(v);
    default:
        if (v != v || v - v != 0)                  /* NaN or infinity */
            return v;
        f = floor(v);
        {
            double d = v - f;                      /* exact */
            if (d > 0.5 || (d == 0.5 && fmod(f, 2.0) != 0))
                f += 1.0;
        }
        return f == 0 ? copysign(0.0, v) : f;
    }
}

double rt_frndint(Cpu *c, double v)
{
    return fpu_round(c, v);
}

int64_t rt_fist(Cpu *c, double v, int bits)
{
    double r = fpu_round(c, v);
    double lim = ldexp(1.0, bits - 1);
    int64_t indefinite = (int64_t)(UINT64_C(0xffffffffffffffff) << (bits - 1));
    if (r != r || r < -lim || r >= lim)
        return indefinite;                         /* integer indefinite, e.g. 0x80000000 */
    return (int64_t)r;
}

void rt_fpu_init(Cpu *c)
{
    c->fpu.cw = 0x037f;
    c->fpu.sw = 0;
    c->fpu.top = 0;
    c->fpu.c0 = c->fpu.c1 = c->fpu.c2 = c->fpu.c3 = 0;
}

/* Tag of a register as fnsave reports it: 0 valid, 1 zero, 2 special. */
static uint32_t fpu_tag(double v)
{
    if (v == 0)
        return 1;
    if (v != v || v - v != 0)
        return 2;                                  /* NaN or infinity */
    return 0;                                      /* double denormals are normal in 80 bits */
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
    c->fpu.cw = R16(addr);
    c->fpu.sw = sw & 0x80ffu;
    c->fpu.top = (sw >> 11) & 7;
    c->fpu.c0 = (sw >> 8) & 1;
    c->fpu.c1 = (sw >> 9) & 1;
    c->fpu.c2 = (sw >> 10) & 1;
    c->fpu.c3 = (sw >> 14) & 1;
    for (int i = 0; i < 8; i++)
        ST(i) = rt_f80_load(addr + 28 + 10 * i);
}
