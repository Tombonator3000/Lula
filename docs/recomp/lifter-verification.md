# Lifter verification against unicorn

`tests/recomp/unicorn_diff.py` checks, instruction by instruction, that the C
emitted by `tools/recomp/lift.py` does what the CPU does. The oracle is unicorn
2.1.1 (QEMU 5 TCG) running the original bytes of `WET.EXE` from the same initial
states.

```sh
python3 tests/recomp/unicorn_diff.py            # full run, about 3 minutes
python3 tests/recomp/unicorn_diff.py --quick    # about 30 s; also run by the unittest
python3 -m unittest tests.recomp.test_lifter_unicorn
```

Options: `--only REGEX` (group key), `--seed N`, `--states N`, `--no-seq`,
`--fpu-cw 0x37f`. The report goes to `build/recomp/unicorn_diff/{full,quick}/report.json`.
The exit status is 0 only when every compared state matches and every case had
at least one valid state.

## Method

1. **Code.** The image is loaded and discovered exactly as `python3 -m tools.recomp`
   does it: the seeds are the entry point, `analysis/decompiled/functions.tsv` and
   the `extra_entries` in `config.json`; the `blacklist` applies; then `flags.Liveness`
   runs. Each decoded instruction is counted once.
2. **Groups.** Instructions are grouped by prefix, mnemonic, operand kinds and
   sizes, e.g. `add r32,m32` or `rep movsd m32,m32`. The rep/repne prefix comes from
   the raw prefix bytes, because capstone drops F2 on `f2 a5`. Groups with fewer
   than 60 members are tested completely. Larger groups get one instance of every
   distinct encoding (opcode bytes, ModRM/SIB shape, displacement and immediate
   size, and register classes such as high byte, ESP/EBP base or same register)
   plus a deterministic sample up to 60.
3. **Single instructions.** A `Lifter` subclass materialises every flag the
   instruction defines (`need()` = def | conditional def) and never fuses. Its
   output is wrapped in one C test function per instruction. A conditional
   branch keeps its real lifted `goto`; the label at the end records the
   target. call, ret and jmp are skipped, and so are traps (int, int3, insb)
   and the single `mov ss, ...`.
4. **Fused chains.** Every flag producer/consumer chain the *production* lifter
   fuses is tested as a unit: 6,709 chains such as `cmp; jl; je` and
   `test; setne`. They are lifted with the real liveness. The flags compared are
   the ones live where the chain is left: after the last reader, or at the
   target of a taken branch.
5. **x87 status sequences.** Every `fcom*`/`ftst`/`fprem`/`fnstsw` up to the
   next jcc (`fcomp; fnstsw ax; sahf; jbe`, `fnstsw ax; and eax, 0x3800; je`,
   ...), lifted with the production lifter: 43 sequences.
6. **States.** Each case gets 11 edge states plus 9 random ones; `--quick` uses
   4 + 4.
   - Edge values for the data operands: 0, 1, 0x7f, 0x80, 0xff, 0x7fff,
     0x8000, 0xffff, 0x7fffffff, 0x80000000, 0xffffffff, sized to the operand.
     The first random states put the operands on the relations that decide
     branches: equal, zero result, imm−1 and imm+1.
   - jcc and setcc cycle through every combination of the flags they read.
   - CL counts for shifts and rotates: 0, 1, 7, 8, 15, 16, 31, 32, 33, 63, 255.
   - String ops: ESI/EDI in scratch memory, ECX of 0–40 under rep, and both DF
     values. Matching bytes are planted so that repe/repne stop early.
   - div/idiv: most random states are crafted not to fault; the others fault,
     and then both sides must trap.
   - Segment loads get valid selectors.
   - Memory operands are solved so the effective address lands in mapped
     memory. If the displacement is absolute, the base and index get small
     values. Otherwise the base points into scratch, or into the stack for
     ESP/EBP bases. Memory data gets the same edge and random values.
7. **Memory.** Both sides map the image at 0x400000 (real bytes), low memory
   0–0x10000, scratch 0x10000000 (+128 KiB, with an FS page at 0x1001f000) and a
   stack at 0x20000000 (+64 KiB), all filled with identical pseudo-random bytes.
   The C side reserves 4 GiB with `MAP_NORESERVE`. After each state, every
   byte that differs from the pristine image is recorded on both sides and
   restored.
   unicorn gets a GDT so that `mov eax, ds`, `push es` and FS-relative loads see
   the Windows selectors 0x2b and 0x53; FS has base 0x1001f000.
8. **Comparison.**
   - All 8 GPRs, the exit address (taken / not taken), DF, and every changed
     memory byte.
   - Flags: the architecturally defined ones only. Not compared: AF for logic
     ops; AF, and OF unless count = 1, for shifts; CF too for SHL/SHR when the
     count ≥ operand size; OF unless count = 1 for rotates; SF/ZF/AF/PF for
     mul/imul; everything for div/idiv; OF/AF/CF for aam. A count of 0 must
     leave every flag unchanged. C flag values must be exactly 0 or 1.
   - x87, with CW 0x027F (53-bit precision, as on Windows) and RC cycling for
     fist/frndint:
     - TOP and all eight physical registers. unicorn's 80-bit value is rounded
       to double for the comparison.
     - Bit-exact: loads, stores, moves, compares, integer conversions,
       fnsave/frstor images. Relative 1e-12: arithmetic and fsqrt. Relative
       1e-9: fsin, fcos, fyl2x.
     - C0/C2/C3 after compares; C2 after fsin/fcos/fprem; the full status
       word after fninit, fnsave and frstor; AX or the memory word after fnstsw.
   - fprem is checked against the exact `math.fmod`, with quotient bits from
     exact rational arithmetic, because QEMU 5's fprem is inexact (below).
9. **Faults.** A #DE leaves unicorn in a state that turns the next one into a
   double fault, so the harness restores a clean unicorn context after every
   trap.

## Coverage (full run, seed 1)

| | |
|---|---|
| Instructions discovered | 78,107 |
| Skipped (not lifted as a single instruction body) | 12,661: call 7,820, jmp 3,326, ret 1,494, traps 20, `mov ss` 1 |
| Testable / groups | 65,446 in 245 groups (185 complete, 60 sampled with all encodings) |
| Single-instruction cases | 5,617 |
| Fused chains (production lifter) | 6,709 of 6,709 |
| x87 status sequences | 43 |
| States executed and compared | 247,380 (0 invalid, 0 failed) |
| Branch cases with both outcomes seen | 7,349 of 7,351 |

The two one-sided branches are inherent. `cmp cl, cl; jne` can never be taken,
and the `jp` after fprem loops only on a partial remainder, which the lifter
never produces (see below). Every x87 instruction form in the program is
covered, and all 245 groups pass.

## Bugs found and fixed

| Where | Problem | Fix |
|---|---|---|
| `tools/recomp/lift.py` `i_aam` | `aam 0x0a` (2 sites in the number-to-ASCII routine 0x44c359, reached from 0x44bbc7) was lifted as a trap | AH = AL / k, AL = AL % k; SF/ZF/PF from AL, OF/AF/CF written as 0 (undefined); k = 0 traps |
| `lift.py` `i_les` | `les edx, [m]` (3 sites, Watcom printf's far `%n` path) was lifted as a trap | the offset goes to the register; the selector is ignored, as for `mov es, r` |
| `lift.py` `i_fpu` fprem, `src/runtime/rt_fpu.c` `rt_fprem`, declared in `rt_cpu.h` | fprem left C0/C1/C3 (quotient bits Q2/Q0/Q1) untouched; `fnstsw` stored the stale bits (found by the x87 sequence test). Dead in WET.EXE: only CF is live after 0x444050 returns, and `clc` clears it | exact remainder plus the quotient bits, C2 = 0 |
| `lift.py` `fusion_chain` | Only the flags live after the *last* fused reader were materialised. A taken branch in the middle of a chain could reach code that reads a flag the setter skipped, e.g. `cmp; je L; jl M` where L reads CF | also materialise the flags live at every intermediate branch target. Latent: a static check finds no such chain in WET.EXE, and the generated C is byte-identical |
| `lift.py` `fusion_chain` | A shift by an immediate 0 defines no flags, but could still be fused (the reader would see the shift result instead of the old flags) | no fusion when the setter defines nothing. Latent: no such instruction exists |

Also confirmed: the `rep`/`repne` detection from raw prefix bytes in
`tools/recomp/cfg.py` (the `f2 a5` capstone issue, fixed in the main session).
Reverting it makes 5 `movsd` cases fail.

## Harness sensitivity (mutation check)

49 deliberate bugs were injected into the lifter output, one at a time. Every
mutation whose instruction occurs in WET.EXE was caught, including:

- wrong CF/OF/AF formulas (sub, inc, add, shr, shl, sar, rcr, rol, neg, adc, sbb);
- signedness of idiv, imul, movsx and fild;
- a missing #DE on div;
- loop/jecxz and setg/jbe conditions;
- dropped rep/repne;
- swapped fsub/fdivr operands;
- fistp ignoring the rounding control, frndint truncating;
- fnstsw losing C3, fcomp operand order, fxch;
- fstp m80, fld m32;
- a missing C2 after fsin;
- signed/unsigned in fused `test` conditions;
- fusion dropping live flags;
- fprem without quotient bits.

Mutations of `lahf`, `cdq` and `leave` matched nothing (those instructions do
not occur). The mid-chain-target fusion mutation matched nothing either, which
is consistent with the static check above.

## Known and intentional differences

- **x87 values are doubles.** Comparisons round unicorn's 80-bit registers to
  double, so they show that each operation is correctly rounded to double. They
  do not show that 80-bit behaviour is preserved.
  - Watcom's startup `fninit` (0x446940) leaves CW = 0x037F, i.e. 64-bit
    precision. The routine at 0x44c47c forces PC = 64 and keeps `fstp/fld
    xword` temporaries.
  - Under `--fpu-cw 0x37f`, single operations still round to the same double.
    However, unicorn's register then holds more precision than the C double in
    the following number of states: fdiv 150, fmul 93, fdivp 111, fmulp 41,
    fdivr 18, fsqrt 10, fadd 8, fsub 7, fsubp 9. So chained arithmetic can
    differ in the last bit.
  - Values beyond the double exponent range become ±inf/0.
  - 64-bit m80 mantissas are rounded on load; constants like fldlg2 are
    rounded to double.
  - With CW 0x027F, the wider-than-double states come only from:
    - m80 loads and frstor images;
    - those constants;
    - results outside the double exponent range (a few fdiv/fmul/fyl2x with
      extreme operands);
    - unicorn's inexact fprem.
- **fprem.** The lifter always completes the reduction (C2 = 0). The hardware
  reports a partial remainder for exponent differences ≥ 64; a `jp` loop ends
  with the same remainder and quotient bits.
  unicorn's fprem is itself inexact (QEMU 5 computes `st0 − st1·trunc(st0/st1)`
  in doubles; up to 2e-5 relative error was seen). The harness therefore checks
  against `math.fmod` and lists unicorn's deviation as known: 30 states.
- **Undefined flags** are written as deterministic values (0, or a formula) and
  are not compared.
- **x87 exception flags** (IE…PE), C1 and the tag word are not modelled, except
  that fnstsw copies the stored bits. `fnsave` writes tags computed from the
  values (no empty tracking) and zero instruction/operand pointers, which
  matches QEMU.
- **Segments.** The model is flat: segment loads are ignored, and `mov r, sreg`
  / `push sreg` produce the Windows selectors (CS 0x23, FS 0x53, others 0x2b).
  `push sreg` writes 32 bits, where modern CPUs may write only 16. verr
  always sets ZF.
- **pushfd/popfd** go through `rt_get_eflags`/`rt_set_eflags`. The harness
  versions push the status flags | DF | 0x202, which matches unicorn. The runtime
  versions must do the same for pushfd to be bit-exact.
- **Transcendentals.** QEMU 5 computes fsin, fcos and fyl2x with host libm on
  doubles, so agreement here does not prove equality with a real x87 for large
  arguments. QEMU's fyl2x with ST0 ≤ 0 differs from hardware and is not tested.
- **Not covered here:** calls, returns, jumps, jump tables and continuation
  handling (`ra` mismatch paths). Those belong to runtime tests.

## Build check

After the fixes, `python3 -m tools.recomp --out build/recomp/gen` generates
1,415 functions with 0 unsupported instruction kinds (before: aam 2, les 3).
All 39 generated files pass `gcc -std=gnu11 -fsyntax-only -Wall
-Wno-unused-label -I src/runtime` with no errors. There are 36 harmless
`-Wunused-variable` warnings: `r` in compares whose flags are dead, and `of0` in
`rcr` when OF is dead.
