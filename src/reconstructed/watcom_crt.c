/* Watcom C runtime routines, reconstructed from WET.EXE. */
#include "reconstructed.h"

/* memcpy(void *dst = EAX, const void *src = EDX, size_t n = EBX) -> EAX = dst.
 *
 * The original (0x442be4) copies n / 4 dwords forward with 'repnz movsd',
 * then n % 4 bytes. ECX, ESI, EDI and ES are saved and restored. Copying in
 * the same order keeps overlapping copies identical to the original. */
RT_RECONSTRUCTED(0x00442be4)
RT_CHECK(0x00442be4, "eax:ptr edx:ptr ebx:size(0,600)")
uint32_t f_00442be4(Cpu *c)
{
    uint32_t dst = c->eax, src = c->edx, n = c->ebx;
    uint32_t words = n >> 2;
    if (dst >= src + n || src >= dst + n || dst <= src) {
        memmove(g_mem + dst, g_mem + src, n);       /* same result as a forward copy */
    } else {
        for (uint32_t i = 0; i < words; i++)
            W32(dst + 4 * i, R32(src + 4 * i));
        for (uint32_t i = words * 4; i < n; i++)
            W8(dst + i, R8(src + i));
    }
    /* Flags after the original: 'and cl, 3' on the remainder count. */
    uint32_t r = n & 3;
    c->cf = 0;
    c->of = 0;
    c->af = 0;
    c->zf = r == 0;
    c->sf = 0;
    c->pf = PARITY(r);
    c->eax = dst;
    return rt_return(c);
}

/* Get-PC stub used by the float-to-decimal conversion (called from 0x44c2b6
 * for every %f, %e and %g in printf). The original is 'call 0x44c478'
 * followed by a 176-byte table of 64-bit powers of ten at 0x44c3c8; the
 * target does 'pop edi; ret', so the net effect is EDI = 0x44c3c8 and a
 * return to the caller. Flags and other registers are unchanged. Hand-written
 * because the bytes after the call are data, not code to decode. */
RT_RECONSTRUCTED(0x0044c3c3)
RT_CHECK(0x0044c3c3, "")
uint32_t f_0044c3c3(Cpu *c)
{
    c->edi = 0x0044c3c8u;
    return rt_return(c);
}
