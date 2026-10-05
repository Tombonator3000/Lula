/* NGS resource-pool readers, reconstructed from WET.EXE.
 * See docs/recomp/specs/resources-and-game-map.md, B.6. The file-manager
 * scratch header begins at +0x105. The pool index stores absolute offsets
 * to payloads; each payload has a six-byte length/type header before it.
 */
#include "reconstructed.h"
#include "gen_decls.h"

/* Keep the Watcom stack layout: the wrappers take the file handle on the
 * guest stack and the remaining arguments in registers. In particular, the
 * seek reader passes its own local slot to the file wrapper for the offset.
 */
static void ngs_read(Cpu *c, uint32_t length, uint32_t dest, uint32_t return_addr)
{
    PUSH32(c->edi);
    c->ecx = 1;
    c->ebx = length;
    c->edx = dest;
    c->eax = c->esi;
    rt_call_guest(c, f_004426d1, return_addr);
}

static void ngs_restore(Cpu *c, uint32_t local_size)
{
    c->esp += local_size;
    c->ebp = R32(c->esp); c->esp += 4;
    c->edi = R32(c->esp); c->esp += 4;
    c->esi = R32(c->esp); c->esp += 4;
    c->ecx = R32(c->esp); c->esp += 4;
}

/* Seek record EBX in pool EDX, using the file manager EAX. As in the
 * original, the clamp compares only the low 16 bits of the requested index.
 * A zero handle leaves the requested index unchanged; a failed read sets
 * the file-library error word to 9 and seeks back to the beginning.
 *
 * The stock profile covers null/invalid handles. Valid pools and their file
 * positions need a fixture comparison because a profile cannot open files.
 * No arithmetic flags are live at the return (the generated version also
 * leaves Cpu flags at the last callee's values).
 */
RT_RECONSTRUCTED(0x0043ebd3)
RT_CHECK(0x0043ebd3, "eax:ptr edx:size(0,1) ebx:int")
uint32_t f_0043ebd3(Cpu *c)
{
    PUSH32(0x20);
    rt_call_guest(c, f_0043371d, 0x0043ebdd);
    PUSH32(c->ecx);
    PUSH32(c->esi);
    PUSH32(c->edi);
    PUSH32(c->ebp);
    c->esp -= 8;
    c->esi = c->eax;
    c->edi = c->edx;
    W32(c->esp + 4, c->ebx);
    c->ebp = c->eax + 0x105;

    if (c->edx != 0) {
        c->ecx = 0;                 /* FILE_BEGIN */
        c->ebx = 0;
        rt_call_guest(c, f_00442690, 0x0043ec03);
        ngs_read(c, 8, c->ebp, 0x0043ec17);
        if (c->eax == 0)
            goto read_failed;

        c->ebx = 3;
        c->edx = 0x0044ff50;         /* "NGS" in the original image */
        c->eax = c->ebp;
        rt_call_guest(c, f_0044624a, 0x0043ec3b);
        if (c->eax != 0)
            goto done;

        c->eax = R32(c->esp + 4);
        c->bx = R16(c->ebp + 6);
        if (c->ax >= c->bx) {
            c->eax = c->ebx - 1;
            W32(c->esp + 4, c->eax);
        }

        c->ebx = R16(c->ebp + 6);
        c->eax = R16(c->esp + 4);
        c->ebx = 0u - ((c->ebx - c->eax) << 2);
        c->ecx = 2;                 /* FILE_END */
        c->edx = c->edi;
        c->eax = c->esi;
        rt_call_guest(c, f_00442690, 0x0043ec75);

        /* The read writes the absolute payload offset into the local slot.
         * ngs_read pushes the handle, so this is [esp+4] at the original
         * LEA instruction (0x43ec80), and [esp] after the call returns.
         */
        ngs_read(c, 4, c->esp, 0x0043ec8b);
        if (c->eax == 0)
            goto read_failed;
        c->ecx = 0;
        c->ebx = R32(c->esp);
        goto seek_payload;

read_failed:
        W16(0x00455030, 9);
        c->ecx = 0;
        c->ebx = 0;
seek_payload:
        c->edx = c->edi;
        c->eax = c->esi;
        rt_call_guest(c, f_00442690, 0x0043ec9d);
    }

done:
    c->eax = R32(c->esp + 4);
    ngs_restore(c, 8);
    return rt_return(c);
}

/* Read the current NGS record into EBX. Seek -6 from the current payload
 * position, read length/type into fm+0x105, then read the payload. Return
 * the header length even when a read fails, matching the original's error
 * handling and preservation of the existing scratch header on a null handle.
 */
RT_RECONSTRUCTED(0x00440041)
RT_CHECK(0x00440041, "eax:ptr edx:size(0,1) ebx:ptr")
uint32_t f_00440041(Cpu *c)
{
    PUSH32(0x1c);
    rt_call_guest(c, f_0043371d, 0x0044004b);
    PUSH32(c->ecx);
    PUSH32(c->esi);
    PUSH32(c->edi);
    PUSH32(c->ebp);
    c->esp -= 4;
    c->esi = c->eax;
    c->edi = c->edx;
    W32(c->esp, c->ebx);
    c->ebp = c->eax + 0x105;

    if (c->edx != 0) {
        c->ecx = 1;                 /* FILE_CURRENT */
        c->ebx = (uint32_t)-6;
        rt_call_guest(c, f_00442690, 0x00440072);
        ngs_read(c, 6, c->ebp, 0x00440086);
        if (c->eax != 0) {
            ngs_read(c, R32(c->ebp), R32(c->esp), 0x0044009e);
            if (c->eax != 0)
                goto done;
        }
        W16(0x00455030, 9);
    }

done:
    c->eax = R32(c->ebp);
    ngs_restore(c, 4);
    return rt_return(c);
}
