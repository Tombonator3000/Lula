/* Stream image decoders from WET.EXE; see resources-and-game-map.md B.6.
 *
 * The register fields below retain the Watcom ABI, including partial AX/CX
 * writes and the original 64,000-byte refill behavior. Locals that the game
 * addresses through its stack remain in guest memory: a guest file read can
 * write directly to one of them. No validation is added here; the original
 * decoder's clipping and error behavior are part of the compatibility ABI.
 */
#include "reconstructed.h"

uint32_t f_0043371d(Cpu *c);
uint32_t f_00442675(Cpu *c);
uint32_t f_00442690(Cpu *c);
uint32_t f_004426d1(Cpu *c);
uint32_t f_00442be4(Cpu *c);
uint32_t f_0044287d(Cpu *c);

/* Guest-frame slots after saving ESI/EDI/EBP and the three register args. */
enum {
    DECODE_BYTES = 0x00, FILE_HANDLE = 0x04, FILE_MANAGER = 0x08,
    BLOCK_LENGTH = 0x0c, INPUT_BUFFER = 0x10, REMAINING = 0x14,
    DESTINATION = 0x18, RUN_LENGTH = 0x1c, REPEAT_PIXEL = 0x20,
    INPUT_CAPACITY = 64000, INPUT_WORD_CAPACITY = 32000
};

static uint32_t slot(uint32_t frame, uint32_t off) { return R32(frame + off); }
static void set_slot(uint32_t frame, uint32_t off, uint32_t value) { W32(frame + off, value); }
static int32_t signed_run(uint32_t frame) { return (int16_t)R16(frame + RUN_LENGTH); }

/* File length after the current position, then restore that position. */
static void stream_remaining(Cpu *c, uint32_t frame, uint32_t tell_ra,
                             uint32_t end_ra, uint32_t end_tell_ra, uint32_t seek_ra)
{
    c->eax = slot(frame, FILE_MANAGER);
    rt_call_guest(c, f_00442675, tell_ra);
    c->esi = c->eax;
    c->ecx = 2;
    c->ebx = 0;
    c->edx = slot(frame, FILE_HANDLE);
    c->eax = slot(frame, FILE_MANAGER);
    rt_call_guest(c, f_00442690, end_ra);
    c->edx = slot(frame, FILE_HANDLE);
    c->eax = slot(frame, FILE_MANAGER);
    rt_call_guest(c, f_00442675, end_tell_ra);
    c->eax -= c->esi;
    set_slot(frame, REMAINING, c->eax);
    c->ecx = 0;
    c->ebx = c->esi;
    c->edx = slot(frame, FILE_HANDLE);
    c->eax = slot(frame, FILE_MANAGER);
    rt_call_guest(c, f_00442690, seek_ra);
}

static void read_block(Cpu *c, uint32_t frame, uint32_t count, uint32_t return_address)
{
    PUSH32(slot(frame, FILE_HANDLE));
    c->edx = slot(frame, FILE_MANAGER) + 0x8d5u;
    c->ecx = 1;
    c->ebx = count;
    c->eax = slot(frame, FILE_MANAGER);
    rt_call_guest(c, f_004426d1, return_address);
}

/* Mode 1 is byte RLE: each {u8 count,u8 value} pair expands count bytes.
 * This mode is supported by the original although the shipped RGB565 assets
 * use modes 0 and 2. Full blocks and the final block have subtly different
 * bounds and EAX behavior, so they remain distinct here. */
static void decode_byte_runs(Cpu *c, uint32_t frame)
{
    stream_remaining(c, frame, 0x0043f800, 0x0043f816, 0x0043f823, 0x0043f83a);
    set_slot(frame, BLOCK_LENGTH, INPUT_CAPACITY);
    c->esi = 0;
    for (;;) {
        c->eax = slot(frame, REMAINING);
        if (c->eax <= slot(frame, BLOCK_LENGTH) || R16(0x00455030) != 0 ||
            c->esi >= slot(frame, DECODE_BYTES))
            break;
        c->ebp = slot(frame, FILE_HANDLE);
        read_block(c, frame, slot(frame, BLOCK_LENGTH), 0x0043f88a);
        if (!c->eax) {
            W16(0x00455030, 9);
            continue;
        }
        c->edi = 0;
        c->eax = slot(frame, DESTINATION);
        while (c->edi < slot(frame, BLOCK_LENGTH) && c->esi < slot(frame, DECODE_BYTES)) {
            c->ebx = slot(frame, FILE_MANAGER);
            c->dh = 0;
            c->dl = R8(c->edi + c->ebx + 0x8d5u);
            c->bl = R8(c->edi + c->ebx + 0x8d6u);
            c->ecx = 0;
            c->ebp = slot(frame, DECODE_BYTES);
            while (c->cx < c->dx && c->esi < c->ebp) {
                W8(c->esi + c->eax, c->bl);
                c->esi++;
                c->ecx++;
            }
            c->edi += 2;
            RT_POLL();
        }
        c->edi -= 2;
        set_slot(frame, REMAINING, slot(frame, REMAINING) - c->edi);
        RT_POLL();
    }
    if (slot(frame, REMAINING) == 0 || R16(0x00455030) != 0 ||
        c->esi >= slot(frame, DECODE_BYTES))
        return;
    c->ebx = slot(frame, FILE_HANDLE);
    read_block(c, frame, slot(frame, REMAINING), 0x0043f927);
    if (!c->eax) {
        W16(0x00455030, 9);
        return;
    }
    c->edi = 0;
    c->ebp = slot(frame, DECODE_BYTES);
    while (c->esi < slot(frame, DECODE_BYTES) && c->edi < slot(frame, REMAINING)) {
        c->ebx = slot(frame, FILE_MANAGER);
        c->dh = 0;
        c->dl = R8(c->edi + c->ebx + 0x8d5u);
        c->bl = R8(c->edi + c->ebx + 0x8d6u);
        c->ecx = 0;
        c->eax = slot(frame, DESTINATION);
        while (c->cx < c->dx && c->esi < c->ebp) {
            W8(c->esi + c->eax, c->bl);
            c->esi++;
            c->ecx++;
        }
        c->edi += 2;
        RT_POLL();
    }
}

/* Negative s16 commands copy literal RGB565 words; nonnegative commands
 * repeat the next word. The original permits a literal run to cross the
 * 64,000-byte input boundary by reading the remaining words directly into
 * the destination. Its accounting and 16-bit arithmetic are preserved. */
static void decode_literal_run(Cpu *c, uint32_t frame)
{
    W16(frame + RUN_LENGTH, (uint16_t)-R16(frame + RUN_LENGTH));
    c->edx = (uint32_t)signed_run(frame);
    c->edx += c->edi;
    c->ebp = slot(frame, DESTINATION);
    c->ebp += c->esi;
    if (c->edx <= INPUT_WORD_CAPACITY) {
        c->ecx = (uint32_t)signed_run(frame);
        c->ecx += c->ecx;
        c->ebx = c->ecx;
        c->edx = c->eax;
        c->eax = c->ebp;
        rt_call_guest(c, f_00442be4, 0x0043fa8d);
        c->esi += c->ecx;
    } else {
        c->ecx = INPUT_WORD_CAPACITY;
        c->cx = (uint16_t)(c->cx - c->di);
        if (c->cx != 0) {
            c->ebx = (uint32_t)(int16_t)c->cx;
            c->ebx += c->ebx;
            c->edx = c->eax;
            c->eax = c->ebp;
            rt_call_guest(c, f_00442be4, 0x0043faa9);
        }
        c->eax = (uint32_t)(int16_t)c->cx;
        c->eax += c->eax;
        c->esi += c->eax;
        c->eax = slot(frame, RUN_LENGTH);
        c->eax -= c->ecx;
        c->ecx = c->eax;
        c->cx = (uint16_t)(c->cx + c->ax);
        if (c->cx != 0) {
            c->ebp = slot(frame, FILE_HANDLE);
            PUSH32(c->ebp);
            c->ebp = (uint32_t)(int16_t)c->cx;
            c->edx = slot(frame, DESTINATION);
            c->edx += c->esi;
            c->ecx = 1;
            c->ebx = c->ebp;
            c->eax = slot(frame, FILE_MANAGER);
            rt_call_guest(c, f_004426d1, 0x0043fadb);
            if (!c->eax)
                W16(0x00455030, 9);
            else {
                set_slot(frame, REMAINING, slot(frame, REMAINING) - c->ebp);
                c->esi += c->ebp;
            }
        }
    }
    c->eax = (uint32_t)signed_run(frame);
    c->edi += c->eax;
}

static void decode_repeat_run(Cpu *c, uint32_t frame)
{
    if (c->edi < slot(frame, BLOCK_LENGTH)) {
        c->ax = R16(c->eax);
        set_slot(frame, REPEAT_PIXEL, c->eax);
    } else {
        c->ebx = slot(frame, FILE_HANDLE);
        PUSH32(c->ebx);
        c->ecx = 1;
        c->ebx = 2;
        c->edx = frame + REPEAT_PIXEL;
        c->eax = slot(frame, FILE_MANAGER);
        rt_call_guest(c, f_004426d1, 0x0043fb29);
        if (!c->eax) {
            W16(0x00455030, 9);
            set_slot(frame, REMAINING, slot(frame, REMAINING) - 2);
        }
    }
    c->ecx = 0;
    c->edi++;
    for (;;) {
        c->eax = 0;
        c->ax = c->cx;
        c->edx = (uint32_t)signed_run(frame);
        if ((int32_t)c->eax >= (int32_t)c->edx)
            break;
        c->edx = slot(frame, DESTINATION);
        c->eax = slot(frame, REPEAT_PIXEL);
        W16(c->edx + c->esi, c->ax);
        c->esi += 2;
        c->ecx++;
        RT_POLL();
    }
}

static void decode_word_runs(Cpu *c, uint32_t frame)
{
    c->eax = slot(frame, FILE_MANAGER);
    c->eax += 0x8d5u;
    set_slot(frame, INPUT_BUFFER, c->eax);
    stream_remaining(c, frame, 0x0043f996, 0x0043f9ac, 0x0043f9b9, 0x0043f9d0);
    c->esi = 0;
    for (;;) {
        c->ecx = slot(frame, REMAINING);
        c->ebx = c->ecx > INPUT_CAPACITY ? INPUT_CAPACITY : c->ecx;
        c->edi = 0;
        c->eax = c->ebx;
        c->eax >>= 1;
        set_slot(frame, BLOCK_LENGTH, c->eax);
        set_slot(frame, REMAINING, slot(frame, REMAINING) - c->ebx);
        /* EBX still contains the byte length across the push and setup. */
        c->eax = slot(frame, FILE_HANDLE);
        read_block(c, frame, c->ebx, 0x0043fa12);
        if (!c->eax)
            W16(0x00455030, 9);
        else {
            while (c->edi < slot(frame, BLOCK_LENGTH) && c->esi < slot(frame, DECODE_BYTES)) {
                c->eax = c->edi + c->edi;
                c->ecx = slot(frame, INPUT_BUFFER);
                c->eax += c->ecx;
                c->ax = R16(c->eax);
                W16(frame + RUN_LENGTH, c->ax);
                c->edx = (uint32_t)signed_run(frame);
                c->edi++;
                c->eax = c->edi + c->edi;
                c->eax += c->ecx;
                if ((int32_t)c->edx < 0)
                    decode_literal_run(c, frame);
                else
                    decode_repeat_run(c, frame);
                RT_POLL();
            }
        }
        if (slot(frame, REMAINING) == 0 || c->esi >= slot(frame, DECODE_BYTES))
            break;
        RT_POLL();
    }
}

/* (fm=EAX, handle=EDX, mode=BX, decoded bytes=ECX, stack arg0=dest).
 * See resources-and-game-map.md B.6 and original code 0x43f7c1..0x43fbbd.
 * Consumes the destination stack argument and restores ESI/EDI/EBP. The
 * remaining register results, including EAX=RGB mode after normal decoding,
 * intentionally follow the original rather than a conventional C return.
 * This profile covers invalid-handle/error paths; successful decoding and
 * block boundaries require the separate file-backed differential verifier.
 */
RT_RECONSTRUCTED(0x0043f7c1)
RT_CHECK(0x0043f7c1, "eax:ptr edx:size(0,0) ebx:size(0,3) ecx:size(0,4096) arg0:ptr")
uint32_t f_0043f7c1(Cpu *c)
{
    PUSH32(0x38);
    rt_call_guest(c, f_0043371d, 0x0043f7cb);
    PUSH32(c->esi);
    PUSH32(c->edi);
    PUSH32(c->ebp);
    c->esp -= 0x18;
    PUSH32(c->eax);
    PUSH32(c->edx);
    PUSH32(c->ecx);
    uint32_t frame = c->esp;
    c->eax = slot(frame, 0x34);
    set_slot(frame, DESTINATION, c->eax);
    if (c->bx == 1)
        decode_byte_runs(c, frame);
    else if (c->bx == 2)
        decode_word_runs(c, frame);
    else {
        c->ebx = slot(frame, FILE_HANDLE);
        PUSH32(c->ebx);
        c->ecx = 1;
        c->ebx = slot(frame, DECODE_BYTES);
        c->edx = slot(frame, 0x34);
        c->eax = slot(frame, FILE_MANAGER);
        rt_call_guest(c, f_004426d1, 0x0043fb92);
        if (!c->eax)
            W16(0x00455030, 9);
    }
    c->eax = R32(0x0048a68c);
    if (c->eax == 1) {
        c->ebx = slot(frame, DECODE_BYTES);
        c->edx = slot(frame, 0x34);
        c->eax = slot(frame, FILE_MANAGER);
        rt_call_guest(c, f_0044287d, 0x0043fbb6);
    }
    /* The original's final flag writer is ADD ESP,0x24. */
    uint32_t old_sp = c->esp, new_sp = old_sp + 0x24;
    c->cf = new_sp < old_sp;
    c->pf = PARITY(new_sp);
    c->af = ((old_sp ^ 0x24u ^ new_sp) >> 4) & 1;
    c->zf = new_sp == 0;
    c->sf = new_sp >> 31;
    c->of = ((~(old_sp ^ 0x24u) & (old_sp ^ new_sp)) >> 31) & 1;
    c->esp = new_sp;
    c->ebp = R32(c->esp); c->esp += 4;
    c->edi = R32(c->esp); c->esp += 4;
    c->esi = R32(c->esp); c->esp += 4;
    return rt_return_pop(c, 4);
}
