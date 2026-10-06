/* TBF stream reader from WET.EXE; resource specification B.6 in
 * docs/recomp/specs/resources-and-game-map.md. */
#include "reconstructed.h"

extern uint32_t f_0043371d(Cpu *c);
extern uint32_t f_004426d1(Cpu *c);
extern uint32_t f_00442431(Cpu *c);
extern uint32_t f_0043f7c1(Cpu *c);
extern uint32_t f_0043fbbe(Cpu *c);

/* EAX=file manager, EDX=file handle, EBX=destination (two u16 dimensions
 * followed by pixels), ECX=optional palette. Read the 16-byte header at the
 * current file position, decode TBF/TPF data, then consume the six-byte
 * next-record header used by sequential NGS reads. Preserve the original
 * stack layout: guest readers write into its local buffer and may alias it.
 * Version 19 is returned as 8, exactly as the original compatibility path.
 * The random profile covers missing/invalid handles; resource_check also
 * compares actual streams, palettes, malformed signatures and EOF paths. */
RT_RECONSTRUCTED(0x0043e8eb)
RT_CHECK(0x0043e8eb, "eax:ptr edx:size(0,1) ebx:ptr ecx:ptr")
uint32_t f_0043e8eb(Cpu *c)
{
    PUSH32(0x2c);
    rt_call_guest(c, f_0043371d, 0x0043e8f5);
    PUSH32(c->esi);
    PUSH32(c->edi);
    PUSH32(c->ebp);
    c->esp -= 24;
    c->edi = c->eax;
    c->ebp = c->edx;
    c->esi = c->eax + 0x105;
    W32(c->esp + 12, c->ecx);       /* palette */
    W32(c->esp + 8, c->ebx);        /* advancing pixel destination */
    W32(c->esp + 16, c->ebx);       /* dimensions */

    if (!c->ebx || !c->edx) {
        W16(0x00455030, 4);
    } else {
        PUSH32(c->edx);
        c->ecx = 1;
        c->ebx = 16;
        c->edx = c->esi;
        rt_call_guest(c, f_004426d1, 0x0043e933);
        if (!c->eax) {
            W16(0x00455030, 9);
        } else {
            c->edx = c->esi;
            c->eax = c->edi;
            rt_call_guest(c, f_00442431, 0x0043e944);
            W32(c->esp + 20, c->eax);
            c->eax = (uint32_t)(int32_t)(int16_t)c->ax;
            if (c->eax == UINT32_MAX) {
                W16(0x00455030, 15);
            } else {
                c->eax = (uint32_t)(int32_t)(int16_t)R16(c->esi + 4);
                if (c->eax == 19)
                    W16(c->esi + 4, 8);
                c->eax = (uint32_t)(int32_t)(int16_t)R16(c->esi + 4);
                if (c->eax == 8) {
                    c->edx = R32(c->esp + 12);
                    if (c->edx) {
                        PUSH32(c->ebp);
                        c->ecx = 1;
                        c->ebx = 768;
                        c->eax = c->edi;
                        rt_call_guest(c, f_004426d1, 0x0043e988);
                        if (!c->eax)
                            W16(0x00455030, 9);
                    }
                }
                if (R16(0x00455030)) {
                    W16(0x00455030, 9);
                } else {
                    c->ax = R16(c->esi + 12);
                    c->edx = R32(c->esp + 16);
                    W16(c->edx, c->ax);
                    c->ax = R16(c->esi + 14);
                    W16(c->edx + 2, c->ax);
                    W32(c->esp + 8, R32(c->esp + 8) + 4);
                    c->ebx = R32(c->esp + 20);
                    if (c->bx == 1) {
                        c->eax = R32(c->esp + 8);
                        PUSH32(c->eax);
                        c->ecx = R32(c->esi + 8);
                        c->ebx = (uint32_t)(int32_t)(int16_t)R16(c->esi + 6);
                        c->edx = c->ebp;
                        c->eax = c->edi;
                        rt_call_guest(c, f_0043f7c1, 0x0043e9e2);
                    } else if (c->bx == 5) {
                        c->ecx = R32(c->esp + 8);
                        PUSH32(c->ecx);
                        c->ecx = R32(c->esi + 8);
                        c->ebx = (uint32_t)(int32_t)(int16_t)R16(c->esi + 6);
                        c->edx = c->ebp;
                        c->eax = c->edi;
                        rt_call_guest(c, f_0043fbbe, 0x0043e9fb);
                    }
                }
            }
        }
    }

    if (!R16(0x00455030)) {
        PUSH32(c->ebp);
        c->ecx = 1;
        c->ebx = 6;
        c->edx = c->esp + 4;
        c->eax = c->edi;
        rt_call_guest(c, f_004426d1, 0x0043ea3c);
    }
    c->eax = 0;
    if (!R16(0x00455030)) {
        c->eax = (uint32_t)(int32_t)(int16_t)R16(c->esi + 4);
        if (c->eax == 19)
            c->eax = 8;
        else
            c->ax = R16(c->esi + 4);
    }
    c->esp += 24;
    c->ebp = R32(c->esp); c->esp += 4;
    c->edi = R32(c->esp); c->esp += 4;
    c->esi = R32(c->esp); c->esp += 4;
    return rt_return(c);
}
