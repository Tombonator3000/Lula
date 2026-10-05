/* Hand-reconstructed replacements for recompiled functions.
 *
 * Mark a definition with RT_RECONSTRUCTED(address) and the recompiler stops
 * emitting its own version of f_<address> (it keeps it as lifted_<address>
 * for comparison). A replacement must leave the registers, flags, stack and
 * memory exactly as the original would, because generated callers rely on
 * the machine state, not on C types. The Watcom register calling convention
 * passes the first arguments in EAX, EDX, EBX and ECX and returns in EAX.
 */
#pragma once
#include "rt_cpu.h"

#define RT_RECONSTRUCTED(address)

/* Pop the return address like a plain 'ret' and hand it to the caller. */
static inline uint32_t rt_return(Cpu *c)
{
    uint32_t ra = R32(c->esp);
    c->esp += 4;
    return ra;
}
