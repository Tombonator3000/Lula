/* Resource signature classification from WET.EXE; specification B.6 in
 * docs/recomp/specs/resources-and-game-map.md. */
#include "reconstructed.h"

extern uint32_t f_0043371d(Cpu *c); /* Watcom stack check */
extern uint32_t f_00443fe8(Cpu *c); /* case-insensitive, bounded comparison */

/* EDX points at at least three signature bytes; EAX (the file-manager
 * argument) is unused. TBF, TPF, TAF and TFF, in any ASCII letter case,
 * return 1, 5, 2 and 3 respectively; an unknown signature returns UINT32_MAX.
 *
 * Keep the four comparisons against guest image memory, rather than host
 * literals: the comparator also leaves its final pointer/result in EDX.
 * Guest calls, return addresses and register saves preserve those effects
 * and the stack contents, including signatures that alias guest memory. */
RT_RECONSTRUCTED(0x00442431)
RT_CHECK(0x00442431, "edx:ptr")
uint32_t f_00442431(Cpu *c)
{
    static const struct {
        uint32_t signature, type, return_address;
    } formats[] = {
        {0x004500bau, 1u, 0x00442456u}, /* TBF */
        {0x004500beu, 5u, 0x00442470u}, /* TPF */
        {0x004500c2u, 2u, 0x0044248au}, /* TAF */
        {0x004500c6u, 3u, 0x004424a4u}, /* TFF */
    };

    PUSH32(0x10u);
    rt_call_guest(c, f_0043371d, 0x0044243bu);
    PUSH32(c->ebx);
    PUSH32(c->ecx);
    PUSH32(c->esi);

    c->ecx = c->edx;
    c->esi = UINT32_MAX;
    for (size_t i = 0; i < sizeof formats / sizeof formats[0]; ++i) {
        c->ebx = 3;
        c->edx = formats[i].signature;
        c->eax = c->ecx;
        rt_call_guest(c, f_00443fe8, formats[i].return_address);

        /* Original TEST EAX,EAX; the following MOV preserves these flags. */
        c->cf = c->of = c->af = 0;
        c->zf = c->eax == 0;
        c->sf = c->eax >> 31;
        c->pf = PARITY(c->eax);
        if (c->eax == 0)
            c->esi = formats[i].type;
    }

    c->eax = c->esi;
    c->esi = R32(c->esp);
    c->esp += 4;
    c->ecx = R32(c->esp);
    c->esp += 4;
    c->ebx = R32(c->esp);
    c->esp += 4;
    return rt_return(c);
}
