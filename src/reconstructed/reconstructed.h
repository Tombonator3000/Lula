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

/* Input profile for tools/fncheck, e.g.
 * RT_CHECK(0x00442be4, "eax:ptr edx:ptr ebx:size(0,4096)").
 * Kinds: ptr (into a scratch buffer), size(lo,hi), int, any. Registers not
 * named get random values; arg0.. name stack arguments above the return
 * address. */
#define RT_CHECK(address, profile)

/* Pop the return address like a plain 'ret' and hand it to the caller. */
static inline uint32_t rt_return(Cpu *c)
{
    uint32_t ra = R32(c->esp);
    c->esp += 4;
    return ra;
}

/* 'ret n' for stdcall-style callees that also pop n bytes of arguments. */
static inline uint32_t rt_return_pop(Cpu *c, uint32_t n)
{
    uint32_t ra = R32(c->esp);
    c->esp += 4 + n;
    return ra;
}

/* Call another guest function the way the original 'call' did: push the
 * original return address (ret_addr, the address after the call
 * instruction) and run the callee. Register arguments are set in c before
 * the call; stack arguments are pushed with PUSH32 first. A callee that
 * returns somewhere else (longjmp-style) is reported, as reconstructed code
 * has no label to continue at. */
static inline void rt_call_guest(Cpu *c, GuestFn fn, uint32_t ret_addr)
{
    PUSH32(ret_addr);
    uint32_t ra = fn(c);
    if (ra != ret_addr)
        rt_trap(c, ret_addr, "reconstructed caller: callee returned to an unexpected address");
}
