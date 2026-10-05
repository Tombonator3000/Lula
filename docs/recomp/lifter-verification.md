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
`--fpu-cw CW` (default 0x127F, the control word WET.EXE runs with; see
[x87 precision](#x87-precision)), `--out DIR`. The report goes to
`build/recomp/unicorn_diff/{full,quick}/report.json`.
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
5. **x87 sequences.** These are lifted with the production lifter: 52
   sequences in all.
   - Every `fcom*`/`ftst`/`fprem`/`fnstsw` up to the next jcc, e.g.
     `fcomp; fnstsw ax; sahf; jbe` or `fnstsw ax; and eax, 0x3800; je`.
   - Every `fldcw` with the code that follows it, e.g.
     `fldcw [esp+0x1c]; fdivp st(1)`. These show that a new control word
     reaches the arithmetic after it.
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
   - x87: TOP and all eight physical registers as 80-bit values, bit for bit.
     - The initial control word is 0x127F. RC cycles through all four modes
       for fist/frndint, and a third of the other states use directed
       rounding.
     - Register values are full 80-bit numbers. 40 % have a 64-bit mantissa
       that no double can hold; a few have exponents beyond the double range
       or are extended denormals.
     - fsin, fcos, fyl2x and fprem are the exception: unicorn computes them
       in host doubles or inexactly (below). They are compared bit for bit
       with the *host x87* running the same instruction on the same operands,
       and only with a 1e-13 tolerance against unicorn.
     - fprem with complete reduction is also checked against exact rational
       arithmetic: the remainder and the quotient bits C0/C3/C1.
     - The fldl2t/fldl2e/fldpi/fldlg2/fldln2 constants are checked against
       the true constant rounded with RC.
     - Also compared: C0/C2/C3 after compares; C2 after fsin/fcos/fprem; the
       full status word after fninit, fnsave and frstor; AX or the memory
       word after fnstsw.
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
| x87 sequences | 52: 43 status sequences, 9 starting at `fldcw` |
| States executed and compared | 247,560 (0 invalid, 0 failed, 47 known unicorn deviations) |
| x87 states compared bit-exactly with a non-double value in a register | 8,882 (e.g. fld 1,705, fstp 1,678, fxch 1,190, fdiv 214, fmul 178, fdivp 155) |
| Branch cases with both outcomes seen | 7,350 of 7,353 |

The three one-sided branches:
- `cmp cl, cl; jne` can never be taken.
- Two `fldcw` sequences at 0x44c4d6 and 0x44c544 end in `je` on
  `[esp+0x18] & 0x7fff` (the exponent of a long double argument), which
  random stack contents rarely hit.

Every x87 instruction form in the program is covered, and all 245 groups pass.
The x87 groups and sequences also pass bit-exactly with `--fpu-cw` 0x37F
(64-bit precision), 0x27F (53-bit) and 0x07F (24-bit): 8,860 states each.

## x87 precision

**What WET.EXE runs with.** Watcom's 8087 initialiser 0x44692a (in the
startup initialiser table, reloc 0x453762) runs `fninit`, which sets CW =
0x37F. It then calls 0x4468ef → 0x449b52, which runs `fninit` again and then
`fldcw` with the word at 0x452640, **0x127F**: 53-bit precision, round to
nearest, all exceptions masked. A gdb trace of `rt_fpu_set_cw` during a
headless start confirms it: the main guest thread loads 0x127F and nothing
else up to the main menu.

The other `fldcw` sites, from the static scan:

| Site | Control word | Purpose |
|---|---|---|
| 0x442d8a / 0x442d8f | high byte 0x1F (PC 64, truncate), then restore | `frndint` helper |
| 0x44c4d6, 0x44c544 | `or 0x33f; and 0xf3ff` (PC 64, nearest), then restored at 0x44c4f8, 0x44c50f, 0x44c583, 0x44c58c | 64-bit precision section around fmul/fdivp |
| 0x44a840 | `_control87` (0x44a807) | no caller found (no call, no relocation) |

No site sets 24-bit precision. So the game mixes 53-bit (the default) and
64-bit precision. A fixed host precision cannot be bit-exact for both.

**How the runtime does it** (`src/runtime/rt_cpu.h`, `rt_fpu.c`):

- Registers are `fpreg_t` = `long double`. With GCC/Clang on x86 that is the
  x87 80-bit format: fld/fstp m80, fxch and frstor/fnsave are plain copies.
- The arithmetic in generated code (`a + b`, `sqrtl`, conversions to
  float/double) runs on the host x87. `rt_fpu_set_cw` (emitted for `fldcw`,
  and called by fninit, frstor and `rt_cpu_bind`) loads the guest control
  word's precision and rounding control into the host x87 control word, with
  every exception masked. Precision control and rounding control are
  therefore emulated by the hardware itself, per guest thread, at no cost per
  instruction.
- fsin, fcos, fyl2x and fprem run as the host instruction. Their results are
  defined by the x87 implementation, not by libm: glibc's `sinl` differs from
  `fsin` in 64 % of random inputs, and `y*log2l(x)` differs from `fyl2x` in
  25 %. fist and frndint also run as the host instruction, with the guest RC.
- fldl2t/fldl2e/fldpi/fldlg2/fldln2 are rounded with RC, as the x87 does
  (Intel SDM vol. 1, 8.3.5): `rt_fpu_const`.
- Other hosts (`LDBL_MANT_DIG != 64`, e.g. ARM64) get a `#warning`. They use
  long double libm and the old conversion code, and ignore precision control.
  The fallback paths were checked against the x87 paths on 200,000 random
  inputs by building `rt_fpu.c` with `-DRT_FPU_FORCE_SOFT`: round trip, fist
  16/32/64, frndint and constants in all four RC modes, and fprem. They are
  identical except for NaN payloads.

**Compiler assumptions.** The generated sources are compiled without
`-frounding-math`. Ordering is still safe:
- every lifted x87 instruction stores its result into `c->fpu` before the
  next out-of-line `rt_fpu_set_cw` call;
- GCC folds only operations whose operands are both constants.

Adding `-frounding-math` to the generated sources would make this formal.

**Negative control.** With the host control-word sync disabled, 546 states
fail at CW 0x127F: arithmetic and float/double stores differ in the last bits.

## Bugs found and fixed

| Where | Problem | Fix |
|---|---|---|
| `tools/recomp/lift.py` `i_aam` | `aam 0x0a` (2 sites in the number-to-ASCII routine 0x44c359, reached from 0x44bbc7) was lifted as a trap | AH = AL / k, AL = AL % k; SF/ZF/PF from AL, OF/AF/CF written as 0 (undefined); k = 0 traps |
| `lift.py` `i_les` | `les edx, [m]` (3 sites, Watcom printf's far `%n` path) was lifted as a trap | the offset goes to the register; the selector is ignored, as for `mov es, r` |
| `lift.py` `i_fpu` fprem, `src/runtime/rt_fpu.c` `rt_fprem`, declared in `rt_cpu.h` | fprem left C0/C1/C3 (quotient bits Q2/Q0/Q1) untouched; `fnstsw` stored the stale bits (found by the x87 sequence test). Dead in WET.EXE: only CF is live after 0x444050 returns, and `clc` clears it | exact remainder plus the quotient bits, C2 = 0 |
| `lift.py` `fusion_chain` | Only the flags live after the *last* fused reader were materialised. A taken branch in the middle of a chain could reach code that reads a flag the setter skipped, e.g. `cmp; je L; jl M` where L reads CF | also materialise the flags live at every intermediate branch target. Latent: a static check finds no such chain in WET.EXE, and the generated C is byte-identical |
| `lift.py` `fusion_chain` | A shift by an immediate 0 defines no flags, but could still be fused (the reader would see the shift result instead of the old flags) | no fusion when the setter defines nothing. Latent: no such instruction exists |
| x87 in `rt_cpu.h`, `rt_fpu.c`, `lift.py` `i_fpu` | Registers were doubles. 64-bit mantissas, the extended exponent range and m80 values were lost, and precision control was ignored | 80-bit `fpreg_t`, host control-word sync, host instructions for fsin/fcos/fyl2x/fprem/fist/frndint |
| `lift.py` `i_fpu` constants, `rt_fpu_const` | fldlg2 etc. were fixed literals, rounded to nearest | rounded with RC like the x87 (QEMU 5 gets this wrong; see below) |
| `rt_fprem` | Always completed the reduction | host `fprem`: a partial remainder with C2 = 1 for exponent differences ≥ 64, as on the x87; the `jp` loop at 0x444069 now runs more than once for such operands |

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

The second round, on the 80-bit x87, caught:
- `fldcw` without the host sync;
- fsin via `sinl`;
- fprem via `fmodl` (quotient bits);
- fyl2x as `y*log2l(x)`;
- fld m80 through a double;
- fild via float;
- an off-by-one fstp m64;
- swapped fdivr operands;
- fxch as a no-op.

"fldlg2 as a literal rounded to nearest" was not caught. GCC turns that
literal into the host `fldlg2` instruction, which rounds with the (synced)
RC, so it is correct on this host by accident. `rt_fpu_const` does not depend
on that.

## Known and intentional differences

- **unicorn's x87 deviations (QEMU 5), listed as known, 47 states.** In
  every one of them C matches the host x87 or an exact reference:
  - fprem is computed in doubles. It is inexact (up to 2e-5 relative), and it
    reduces only partially from 53 bits of exponent difference instead of 64.
  - fsin, fcos and fyl2x use host libm on doubles. For example, `fsin(pi)`
    on a real x87 is 1.2246063538223773e-16 because of its 66-bit pi, while
    unicorn gives libm's 1.2246467991473532e-16. Operands that underflow to
    0 as a double make QEMU's fyl2x refuse to pop.
  - The load-constant instructions ignore RC.
- **x87 hardware dependence.** fsin, fcos, fyl2x and fprem give whatever the
  host x87 gives (here an Intel Xeon). AMD and the Pentium-era x87 may differ
  in the last bit of transcendental results.
- **Hosts without an x87 long double** (ARM64, MSVC) round registers to their
  long double, ignore precision control and use libm for the transcendentals
  (`#warning` in `rt_fpu.c`).
- **New guest threads** start with CW 0x37F (`rt_fpu_init` in
  `rt_cpu_new_thread`). Windows starts new threads with 0x27F. This only
  matters if a secondary thread does x87 arithmetic before loading its own
  control word.
- **fprem quotient bits** after a partial remainder are the low bits of that
  step's quotient, as on the x87. A complete reduction gives the low bits of
  the full quotient.
- **Undefined flags** are written as deterministic values (0, or a formula) and
  are not compared.
- **x87 exception flags** (IE…PE), C1 and the tag word are not modelled, except
  that fnstsw copies the stored bits and fprem sets C1. Unmasked exceptions
  are never raised: the host always runs with every exception masked.
  `fnsave` writes tags computed from the values (no empty tracking) and zero
  instruction/operand pointers, which matches QEMU.
- **Segments.** The model is flat: segment loads are ignored, and `mov r, sreg`
  / `push sreg` produce the Windows selectors (CS 0x23, FS 0x53, others 0x2b).
  `push sreg` writes 32 bits, where modern CPUs may write only 16. verr
  always sets ZF.
- **pushfd/popfd** go through `rt_get_eflags`/`rt_set_eflags`. The harness
  versions push the status flags | DF | 0x202, which matches unicorn. The runtime
  versions must do the same for pushfd to be bit-exact.
- **Not covered here:** calls, returns, jumps, jump tables and continuation
  handling (`ra` mismatch paths). Those belong to runtime tests.

## Build check

`cmake --build build/game` builds without warnings, all 6 tests in
`tests.test_recompiled_game` pass, and so does `build/game/lula-fncheck`.
`src/runtime/rt_fpu.c` also passes `-Wall -Wextra` with gcc and clang, and an
aarch64 syntax check (only the intended `#warning`).

After the fixes, `python3 -m tools.recomp --out build/recomp/gen` generates
1,415 functions with 0 unsupported instruction kinds (before: aam 2, les 3).
All 39 generated files pass `gcc -std=gnu11 -fsyntax-only -Wall
-Wno-unused-label -I src/runtime` with no errors. There are 36 harmless
`-Wunused-variable` warnings: `r` in compares whose flags are dead, and `of0` in
`rcr` when OF is dead.
