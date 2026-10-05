# Code discovery spec for WET.EXE (static recompilation)

Scope: the code-discovery facts the static recompiler (`tools/recomp`) and its runtime need: every absolute pointer into
BEGTEXT, jump tables, vtables, callbacks and init/fini tables, function entries Ghidra missed or got wrong,
non-standard control flow, the Watcom runtime library map and odd/privileged instructions.
Binary: `original/app/WET.EXE`, SHA-256 `8c223b52…cea9`, PE32 i386, image base `0x400000`, entry `0x442cfc`.
This is an analysis document, not a decompilation.

Legend:
- **VERIFIED**: read directly from instruction bytes, relocations or data in the binary.
- **INFERRED**: interpretation (a Watcom routine's name, the purpose of a game routine, or runtime reachability that depends on data).
- **live / dead**: reachability from the PE entry `0x442cfc` over direct calls, jumps, jump tables and every code-pointer
  immediate or data pointer that a live instruction references (conservative: a function whose address is taken by live
  code counts as live even if the pointer is never called). "Dead" means no such path exists. VERIFIED by the closure
  computation in §8, except where a guard on a data value is cited.
- Watcom register ABI: arguments in `EAX, EDX, EBX, ECX`, then the stack. Game functions start with `push N; call 0x43371d` (`__CHK`).

Out of scope (on hold, not analysed): video/AVI/CUT, `CoCreateInstance`/`CoInitialize`/`CoUninitialize`, `mciSendCommandA`.
Code belonging to them is only listed by address: MIDI/MCI module `0x443418..0x443fe7` (functions `0x443418`, `0x443484`,
`0x44356a`, `0x4437ec`, `0x443948`, `0x4439b3`, `0x443a94`, `0x443ad4`, `0x443afb`, `0x443b0c`, `0x443c52`, `0x443e02`,
`0x443f8f`) and the MCI-notify WndProc handler registration at `0x443d11` (message `0x3B9`, handler `0x443f8f`).

Companion specs: `kernel32.md`, `user32-gdi32.md`, `directx.md` (COM calls, timer callback), `winmm-timing.md`.

---

## 0. Summary for the recompiler (actionable)

1. **`0x44c3c3` is real code, not data.** It is a 5-byte get-PC stub `call 0x44c478` followed by a 176-byte table of
   64-bit powers of ten (`0x44c3c8..0x44c477`) and `pop edi; ret` at `0x44c478`. Its only caller `0x44c2b4` uses it to get
   `EDI = 0x44c3c8`. The recompiler blacklists it, so `0x44c2b6` lifts to `rt_call_indirect(0x44c3c3)`, which traps.
   This path runs for **every `%f`/`%e`/`%g` printf conversion** (`0x44bbc7` → `0x44ba69` → `0x44c2b4`, unconditional at
   `0x44bcd4`). In the game it is reached by the version overlay `sprintf("Version: %f F DEBUG_DAT", …)` at `0x403d80`,
   which runs while `[0x45dc04] != 0`; `0x4094d4` toggles that flag when key code `0x76` (INFERRED: `VK_F7`) is in
   `[0x455384]`. Fix: make `0x44c3c3` an entry whose `call` has **no fall-through** (or hand-write it as `EDI = 0x44c3c8; return`). VERIFIED.
2. **Bogus recompiler entry `0x44cf80`.** It is the string `"Sleep"` (followed by `"KERNEL32.DLL"` at `0x44cf86`) written by
   a binary patch. A stale relocation at `0x43413e` happens to line up with the patched `push 0x44cf80`, so pointer
   discovery made it a function (`push ebx; insb` → trap). Blacklist it. VERIFIED.
3. **85 jump tables, all bounded by `cmp idx,N; ja default` immediately before the `jmp`.** The recompiler over-counts two:
   table `0x441414` has 8 entries (recomp: 16) and `0x4419e4` has 6 (recomp: 12), because the guard compares a 16-bit
   register (`cmp cx,7` / `cmp cx,5`). The extra "targets" are case labels of the adjacent table in the same function, so
   the generated code is still correct, but the bound detector should accept 8/16-bit compares and pre-scaled indexes. §2.2.
4. **No C++ vtables exist.** The game is C++ but only uses 21 global objects with constructors/destructors (no virtual
   calls). Every `call [reg+off]` outside the runtime is a COM call on a host-provided DirectX object (135 sites). §2.3.
5. **Only one function passes flags across `ret`:** `0x444050` (CF, consumed by `jae` at `0x444043`/`0x44404d`). Two
   independent scans agree; no function consumes flags on entry. §4.1.
6. **setjmp/longjmp and C++ throw/catch are linked but dead.** `setjmp 0x44b342` is unreferenced; `longjmp 0x44b381`
   (the only `RtlUnwind` caller) is reached only from dead C++ EH code; no `throw` site exists. Live EH runtime is limited to
   static-object construction/destruction and internal-error paths. §4.2–4.3.
7. **No self-modifying code.** BEGTEXT is `0x60000020` (code, execute, read; not writable), no `VirtualProtect`-like
   import exists, and no instruction stores to an absolute BEGTEXT address. Code reads its own bytes only through
   `cs:` tables (digit strings, the powers-of-ten table) and the 85 jump tables. §4.9–4.10.
8. **Never apply base relocations.** The image runs at its preferred base. 38 relocations are stale because of post-link
   byte patches (NOP sleds and two code inserts); applying them would corrupt code. §1.3.
9. **Traps:** privileged/DOS instructions (`int 0x10`, `int 0x31`, `int 0x33`, `in al,dx`, `int 6`) occur only in dead code;
   `int3` at `0x447b11` is guarded by a flag that is never set; `les` (3×) is live code but needs a far `%n` format, which
   no literal uses. §6.
10. **Reachability:** of the recompiler's 1,415 entries, 1,262 are live and 153 dead; 139 further `__CHK` functions were
    never decoded and form a closed, unreferenced set. Ghidra missed 471 entries (392 reached only through code pointers,
    79 only through calls from such code or from code after a patched NOP sled). §3.

---

## 1. Image layout relevant to code discovery

### 1.1 Sections (VERIFIED)

| Section | VA range | Characteristics | Notes |
|---|---|---|---|
| BEGTEXT | `0x401000..0x44cfff` | `0x60000020` code, execute, read | not writable |
| DGROUP | `0x44d000..0x4543ff` (raw) | `0xc0000040` data, read, write | init data, runtime hook variables, XI/YI tables |
| .bss | `0x455000..0x48a9ff` | `0xc0000080` | game globals, WndProc table `0x486070`, screen callbacks `0x483a70` |
| .idata | `0x48b000..` | | IAT, two blocks (game / Watcom runtime) |
| .reloc | `0x48c000..` | | 14,980 `HIGHLOW` entries |

### 1.2 BEGTEXT map (VERIFIED boundaries; module roles INFERRED)

| Range | Content |
|---|---|
| `0x401000..0x40100f` | Watcom `BEGTEXT` header: `0x401000 int3; 0x401001 jmp 0x401000` (trap loop), label `___begtext` = `0x401003` (4×`nop`), 8 zero bytes. Not a function, never executed. Its only reference is the dword at `0x442d0b`. |
| `0x401010..0x43371b` | Game code (C++); functions start with a `__CHK` prologue (exceptions: shared tails, §4.6). Jump tables sit directly before their functions. |
| `0x43371c..0x4337db` | Watcom pieces linked mid-image: `0x43371c` (empty XI routine), `__CHK 0x43371d`, `__STK 0x43372d`, page probe `0x43375c`, `operator new 0x43377c`. |
| `0x4337dc..0x4428df` | Game engine (window, DirectDraw, DirectSound, sprites, input, game file wrappers `0x442539..0x4428df`). |
| `0x4428e0..0x443417` | Watcom C library (memset, strings, rand, exit, memcpy/memmove, startup stub `0x442cfc`, malloc/free, printf, getenv). |
| `0x443418..0x443fe7` | Engine MIDI/MCI module (out of scope). |
| `0x443fe8..0x4440b3` | `strnicmp` `0x443fe8`; x87 `cos`/`sin` with range reduction (`0x44403c..0x44407a`); unreferenced `cos`/`sin`/`tan` stdcall wrappers `0x44407b..0x4440b3`. |
| `0x4440b4..0x446026` | Hand-written assembly graphics module (sprite/blit routines; dead VESA/DPMI/VGA-port leftovers at `0x4440b4..0x444261`). |
| `0x446027..0x44cc0b` | Watcom C library (itoa/atoi, strcmp, memset core, streams, time, FPU init, C++ EH, printf engine, `__NTMain`, SEH filter, MT/TLS, heap, float formatting, setjmp/longjmp, FDIV workaround). |
| `0x44cc0c..0x44cf65` | 143 `jmp dword ptr [IAT]` thunks (6 bytes each). The 96 thunks `0x44cc0c..0x44ce46` are never referenced; the 47 at `0x44ce4c..0x44cf60` are called by the runtime. |
| `0x44cf80..0x44cf92` | Patch data: `"Sleep\0"` (`0x44cf80`), `"KERNEL32.DLL\0"` (`0x44cf86`). |

Text bytes: 311,296. Bytes covered by decoded instructions: 275,483 (267,211 in live functions). Jump-table bytes: 2,348.

### 1.3 Relocations and binary patches (VERIFIED)

1,232 of the 14,980 relocated dwords point into BEGTEXT:

| Where the dword is | Count | What it is |
|---|---|---|
| BEGTEXT, jump-table slot | 587 | entries of the 85 decoded jump tables (§2.2) |
| BEGTEXT, displacement of `jmp cs:[idx*4+T]` | 85 | table base |
| BEGTEXT, displacement of `mov al/dl, cs:[edx+T]` | 2 | digit tables `0x442c71`, `0x446002` (§4.9) |
| BEGTEXT, code-pointer immediate (`push`/`mov`) in decoded code | 321 | callbacks and function pointers (§2.4) |
| BEGTEXT, not inside decoded code | 31 | 3 dead jump tables + their `jmp` (§2.2.2), 9 immediates in dead code, `0x442d0b`→`0x401003`, stale `0x44bf88` |
| DGROUP | 206 | 21 destructor-table pairs, 63 type-signature fields, 17 XI/YI entries, 64 FDIV-dispatch slots, 41 runtime hook variables (§2.3–2.5) |

**Binary patches.** The shipped EXE was patched after linking. The patches left the old relocation entries in place.
42 relocations lie inside patched bytes. 38 of them no longer sit on a 4-byte absolute operand (stale). Four happen to line
up with a new absolute operand: `0x40d42c` and `0x40ff39` (`[0x455734]`), `0x43413e` (`push 0x44cf80`, the bogus entry of
§0.2) and `0x44bf6c` (`[0x488080]`).

| Patch range | Content now | Stale relocations |
|---|---|---|
| `0x401433..0x40143e` | `mov word [0x455724],0xff` + 3 `nop` | `0x401435`, `0x40143b` |
| `0x401853..0x401865` | 19 `nop` | `0x401854`, `0x401859`, `0x401862` |
| `0x402765..0x402777` | 19 `nop` | `0x402766`, `0x40276b`, `0x402774` |
| `0x40a189..0x40a19b`, `0x40a1b9..0x40a1bd`, `0x40a1d9..0x40a1eb` | `nop` sleds | `0x40a18a`, `0x40a18f`, `0x40a198`, `0x40a1da`, `0x40a1df`, `0x40a1e8` |
| `0x40d3fb..0x40d447` | rewritten cases of the menu handler in screen callback `0x40d123` (live; reached from `0x40d47c`, `0x40d4af`): word stores to `[0x455734]`/`[0x455738]` = 160×120, 320×240, 640×480, then `jmp 0x40d152`; `0x40d440..0x40d445` 6 `nop` + `jmp 0x40d429` | `0x40d3fd`, `0x40d403`, `0x40d40d`, `0x40d41c`, `0x40d422`, `0x40d43b` |
| `0x40ff37..0x40ff53` | `mov [0x455734],0x280; mov [0x455738],0x1e0; …; 2 nop` | `0x40ff3f`, `0x40ff50` |
| `0x434133..0x43417c` | `LoadLibraryA("KERNEL32.DLL")`/`GetProcAddress("Sleep")` and the `Sleep(32)` detour (§4.6) | `0x434139` (on a `call rel32`), `0x434145`, `0x43414f`, `0x434161`, `0x434169`, `0x43416e`, `0x434175` |
| `0x44bf67..0x44bfa7` | unreferenced inserted routine `0x44bf68` (`IUnknown::Release` on `[0x488080]` + `call 0x43393d`) and a `jmp` table turned into `ret`+`nop` at `0x44bfa0` | `0x44bf68`, `0x44bf70`..`0x44bf84` (6), `0x44bf88` (old table entry 8, value `0x44bfa8` still intact), `0x44bfa4` |

Consequences:
- The runtime loader must not apply relocations (the image is at its preferred base, so Windows does not either).
- Discovery must not trust relocations inside these ranges.
- Ghidra stopped disassembling at the stale relocations in the NOP sleds. `FUN_00401757` ends at `0x401864` (real end
  `0x4019b2`) and `FUN_0040271e` at `0x402776`. That is why 12 callees are missing from Ghidra (§3.2). VERIFIED.

---

## 2. Absolute code pointers, classified

### 2.1 Overview

Every relocated dword that points into BEGTEXT is classified below. The recompiler's own classes (`cfg.py`,
`code_pointer_relocs`) agree with these counts: 587 table-slot, 408 insn (85 + 2 + 321), 206 data, 31 text-unowned.

### 2.2 Jump tables

#### 2.2.1 The 85 decoded tables (VERIFIED)

All 85 are `jmp dword ptr cs:[reg*4 + T]`, except two with a pre-scaled index (`jmp cs:[reg + T]`, rows marked).
Each is bounded by `cmp idx, N` followed by `ja default` immediately before the `jmp`. Every target lies inside the owning
function and no target is a function entry. For all 80 owning functions, the last table ends exactly at the function's
entry address, so the tables occupy the bytes just before each function. Linear disassembly over them is garbage
(e.g. the "`retf`", "`les`/`lds`", "`pushal`" hits at `0x441414`, `0x41c445`, `0x425f41`).
`n` = number of slots (= guard + 1), `distinct` = distinct targets.

| # | jmp | table | n | distinct | index | bound (guard → default) | function | live |
|---|---|---|---|---|---|---|---|---|
| 1 | 0x403466 | 0x4028c4 | 4 | 4 | eax | `cmp eax,0x3; ja 0x403374 at 0x40345d` | 0x4028d4 | yes |
| 2 | 0x4036e1 | 0x403588 | 4 | 4 | eax | `cmp eax,0x3; ja 0x4035ef at 0x4036d8` | 0x403598 | yes |
| 3 | 0x404662 | 0x4042f0 | 34 | 22 | eax | `cmp eax,0x21; ja 0x4043d9 at 0x404659` | 0x404378 | yes |
| 4 | 0x405b27 | 0x405934 | 5 | 4 | eax | `cmp eax,0x4; ja 0x405c67 at 0x405b1e` | 0x405948 | yes |
| 5 | 0x4077af | 0x407774 | 11 | 11 | eax | `cmp eax,0xa; ja 0x4077c3 at 0x4077aa` | 0x4077a0 | yes |
| 6 | 0x409767 | 0x409558 | 34 | 21 | eax | `cmp eax,0x21; ja 0x409817 at 0x40975e` | 0x4095e0 | yes |
| 7 | 0x40a530 | 0x40a500 | 7 | 7 | eax | `cmp eax,0x6; ja 0x40a547 at 0x40a52b` | 0x40a51c | yes |
| 8 | 0x40a934 | 0x40a8e0 | 9 | 9 | eax | `cmp eax,0x8; ja 0x40aa74 at 0x40a92b` | 0x40a904 | yes |
| 9 | 0x40b3a4 | 0x40b374 | 7 | 7 | eax | `cmp eax,0x6; ja 0x40b3bb at 0x40b39f` | 0x40b390 | yes |
| 10 | 0x40bb21 | 0x40baf8 | 4 | 4 | eax | `cmp eax,0x3; ja 0x40bbb3 at 0x40bb18` | 0x40bb08 | yes |
| 11 | 0x40c5a5 | 0x40c57c | 4 | 4 | eax | `cmp eax,0x3; ja 0x40c3f7 at 0x40c59c` | 0x40c58c | yes |
| 12 | 0x40e193 | 0x40dfc8 | 4 | 3 | edx | `cmp edx,0x3; ja 0x40e002 at 0x40e18a` | 0x40dfd8 | yes |
| 13 | 0x40e763 | 0x40e720 | 10 | 10 | eax | `cmp eax,0x9; ja 0x40e77a at 0x40e75a` | 0x40e748 | yes |
| 14 | 0x40fade | 0x40f9d8 | 7 | 6 | eax | `cmp eax,0x6; ja 0x40fa1f at 0x40fad5` | 0x40f9f4 | yes |
| 15 | 0x40ff9e | 0x40fe94 | 6 | 5 | eax | `cmp eax,0x5; ja 0x40fecf at 0x40ff95` | 0x40feac | yes |
| 16 | 0x4104ed | 0x410318 | 9 | 8 | eax | `cmp eax,0x8; ja 0x4104fd at 0x4104e8` | 0x41033c | yes |
| 17 | 0x410a55 | 0x4108d8 | 7 | 6 | edx | `cmp edx,0x6; ja 0x41091f at 0x410a4c` | 0x4108f4 | yes |
| 18 | 0x410c1b | 0x410bec | 6 | 6 | eax | `cmp eax,0x5; ja 0x410c4b at 0x410c16` | 0x410c04 | yes |
| 19 | 0x412576 | 0x412544 | 5 | 5 | eax | `cmp eax,0x4; ja 0x41270a at 0x41256d` | 0x412558 | yes |
| 20 | 0x412ca1 | 0x412b4c | 7 | 5 | eax | `cmp eax,0x6; ja 0x412c3b at 0x412c9c` | 0x412b68 | yes |
| 21 | 0x414c2c | 0x414ba0 | 6 | 6 | edx | `cmp edx,0x5; ja 0x414c38 at 0x414c27` | 0x414bb8 | yes |
| 22 | 0x415382 | 0x4151bc | 7 | 7 | ecx | `cmp ecx,0x6; ja 0x415265 at 0x415379` | 0x4151d8 | yes |
| 23 | 0x415f81 | 0x415f34 | 7 | 4 | ebx | `cmp ebx,0x6; ja 0x415fbc at 0x415f7c` | 0x415f50 | yes |
| 24 | 0x416077 | 0x416020 | 7 | 7 | eax | `cmp eax,0x6; ja 0x4160f3 at 0x41606e` | 0x41603c | yes |
| 25 | 0x4163b7 | 0x41636c | 7 | 2 | ecx | `cmp ecx,0x6; ja 0x4163cd at 0x4163b2` | 0x416388 | yes |
| 26 | 0x41669d | 0x416650 | 7 | 5 | edx | `cmp edx,0x6; ja 0x41648b at 0x416694` | 0x41666c | yes |
| 27 | 0x416a2e | 0x416a00 | 4 | 4 | eax | `cmp eax,0x3; ja 0x416e99 at 0x416a25` | 0x416a10 | yes |
| 28 | 0x4170e1 | 0x417094 | 4 | 4 | eax | `cmp eax,0x3; ja 0x416ec8 at 0x4170d8` | 0x4170a4 | yes |
| 29 | 0x418014 | 0x417e74 | 15 | 14 | eax | `cmp eax,0xe; ja 0x417f49 at 0x41800b` | 0x417eb0 | yes |
| 30 | 0x418b67 | 0x418a88 | 6 | 3 | eax | `cmp eax,0x5; ja 0x418acd at 0x418b5e` | 0x418aa0 | yes |
| 31 | 0x418dcd | 0x418c48 | 5 | 3 | eax | `cmp eax,0x4; ja 0x418c8a at 0x418dc4` | 0x418c5c | yes |
| 32 | 0x419278 | 0x41924c | 4 | 4 | eax | `cmp eax,0x3; ja 0x419408 at 0x41926f` | 0x41925c | yes |
| 33 | 0x419ba4 | 0x419ad8 | 7 | 3 | edx | `cmp edx,0x6; ja 0x419be4 at 0x419b9f` | 0x419af4 | yes |
| 34 | 0x419ddf | 0x419be8 | 5 | 5 | edx | `cmp edx,0x4; ja 0x419d6c at 0x419dda` | 0x419bfc | yes |
| 35 | 0x41ab09 | 0x41aa2c | 4 | 3 | edx | `cmp edx,0x3; ja 0x41aa5c at 0x41ab00` | 0x41aa3c | yes |
| 36 | 0x41abec | 0x41ab14 | 4 | 3 | edx | `cmp edx,0x3; ja 0x41ab48 at 0x41abe3` | 0x41ab24 | yes |
| 37 | 0x41ad45 | 0x41abf4 | 6 | 5 | edx | `cmp edx,0x5; ja 0x41ac38 at 0x41ad3c` | 0x41ac0c | yes |
| 38 | 0x41aed5 | 0x41ad84 | 6 | 5 | edx | `cmp edx,0x5; ja 0x41adc8 at 0x41aecc` | 0x41ad9c | yes |
| 39 | 0x41b240 | 0x41b0fc | 5 | 4 | edx | `cmp edx,0x4; ja 0x41b13b at 0x41b237` | 0x41b110 | yes |
| 40 | 0x41b45e | 0x41b27c | 9 | 8 | eax | `cmp eax,0x8; ja 0x41b639 at 0x41b455` | 0x41b2b4 | yes |
| 41 | 0x41b879 | 0x41b2a0 | 5 | 5 | eax | `cmp eax,0x4; ja 0x41b81f at 0x41b874` | 0x41b2b4 | yes |
| 42 | 0x41b9d5 | 0x41b890 | 7 | 4 | eax | `cmp eax,0x6; ja 0x41b8f5 at 0x41b9cc` | 0x41b8ac | yes |
| 43 | 0x41be1c | 0x41bc30 | 7 | 6 | eax | `cmp eax,0x6; ja 0x41bf49 at 0x41be13` | 0x41bc4c | yes |
| 44 | 0x41c58e | 0x41c444 | 6 | 5 | eax | `cmp eax,0x5; ja 0x41c6ec at 0x41c585` | 0x41c45c | yes |
| 45 | 0x41d1ab | 0x41d0c4 | 4 | 3 | edx | `cmp edx,0x3; ja 0x41d0f8 at 0x41d1a2` | 0x41d0d4 | yes |
| 46 | 0x41d359 | 0x41d1b4 | 5 | 4 | eax | `cmp eax,0x4; ja 0x41d47b at 0x41d350` | 0x41d1c8 | yes |
| 47 | 0x41e878 | 0x41e7e8 | 6 | 5 | eax | `cmp eax,0x5; ja 0x41e823 at 0x41e873` | 0x41e800 | yes |
| 48 | 0x41f60d | 0x41f560 | 5 | 4 | edx | `cmp edx,0x4; ja 0x41f598 at 0x41f608` | 0x41f574 | yes |
| 49 | 0x41fe03 | 0x41fdd4 | 5 | 5 | eax | `cmp eax,0x4; ja 0x41ff4b at 0x41fdfa` | 0x41fde8 | yes |
| 50 | 0x420abe | 0x420a90 | 5 | 5 | eax | `cmp eax,0x4; ja 0x420c0b at 0x420ab5` | 0x420aa4 | yes |
| 51 | 0x4215dd | 0x42158c | 4 | 4 | eax | `cmp eax,0x3; ja 0x4215ff at 0x4215d8` | 0x42159c | yes |
| 52 | 0x422810 | 0x4227b8 | 4 | 4 | eax | `cmp eax,0x3; ja 0x422875 at 0x422807` | 0x4227c8 | yes |
| 53 | 0x422b86 | 0x422b58 | 5 | 5 | esi | `cmp esi,0x4; ja 0x422baf at 0x422b81` | 0x422b6c | yes |
| 54 | 0x422d6d | 0x422be8 | 4 | 4 | eax | `cmp eax,0x3; ja 0x422d75 at 0x422d68` | 0x422bf8 | yes |
| 55 | 0x423463 | 0x423428 | 8 | 8 | eax | `cmp eax,0x7; ja 0x42347a at 0x42345a` | 0x423448 | yes |
| 56 | 0x423b7a | 0x423b18 | 4 | 4 | edx | `cmp edx,0x3; ja 0x42388a at 0x423b71` | 0x423b28 | yes |
| 57 | 0x424e07 | 0x424d28 | 4 | 4 | edx | `cmp edx,0x3; ja 0x424e85 at 0x424dfe` | 0x424d38 | yes |
| 58 | 0x425501 | 0x4251d8 | 5 | 5 | ebx | `cmp ebx,0x4; ja 0x425215 at 0x4254f8` | 0x4251ec | yes |
| 59 | 0x42567e | 0x4255b0 | 4 | 3 | eax | `cmp eax,0x3; ja 0x4255e8 at 0x425675` | 0x4255c0 | yes |
| 60 | 0x425f8a | 0x425f38 | 12 | 12 | eax | `cmp eax,0xb; ja 0x42611e at 0x425f81` | 0x425f68 | yes |
| 61 | 0x426ab1 | 0x4268d8 | 12 | 7 | edx | `cmp edx,0xb; ja 0x42709a at 0x426aa8` | 0x426908 | yes |
| 62 | 0x42707f | 0x4268a8 | 12 | 6 | eax | `cmp eax,0xb; ja 0x426a95 at 0x427076` | 0x426908 | yes |
| 63 | 0x42860f | 0x4285d4 | 8 | 8 | eax | `cmp eax,0x7; ja 0x428834 at 0x428606` | 0x4285f4 | yes |
| 64 | 0x428b76 | 0x428a8c | 4 | 4 | edx (pre-scaled) | `cmp ebx,3; ja 0x428b7d at 0x428b71; edx=ebx*4 set at 0x428aad-0x428aaf (index already ×4)` | 0x428a9c | yes |
| 65 | 0x429086 | 0x429058 | 5 | 5 | eax | `cmp eax,0x4; ja 0x42924c at 0x42907d` | 0x42906c | yes |
| 66 | 0x429d72 | 0x429d40 | 6 | 6 | eax | `cmp eax,0x5; ja 0x429d89 at 0x429d69` | 0x429d58 | yes |
| 67 | 0x42ab0b | 0x42aa98 | 4 | 4 | eax | `cmp eax,0x3; ja 0x42aadc at 0x42ab06` | 0x42aaa8 | yes |
| 68 | 0x42c505 | 0x42c4d0 | 7 | 7 | eax | `cmp eax,0x6; ja 0x42c5bf at 0x42c4fc` | 0x42c4ec | yes |
| 69 | 0x42c9fa | 0x42c9d0 | 4 | 4 | eax | `cmp eax,0x3; ja 0x42ca11 at 0x42c9f1` | 0x42c9e0 | yes |
| 70 | 0x42d959 | 0x42d928 | 6 | 6 | eax | `cmp eax,0x5; ja 0x42da92 at 0x42d950` | 0x42d940 | yes |
| 71 | 0x42e43e | 0x42e414 | 4 | 4 | eax | `cmp eax,0x3; ja 0x42e455 at 0x42e435` | 0x42e424 | yes |
| 72 | 0x42ec5a | 0x42ec20 | 8 | 8 | eax | `cmp eax,0x7; ja 0x42ec71 at 0x42ec51` | 0x42ec40 | yes |
| 73 | 0x432cf4 | 0x4329d0 | 4 | 3 | edx | `cmp edx,0x3; ja 0x432a08 at 0x432ceb` | 0x4329e0 | yes |
| 74 | 0x432dcd | 0x432d08 | 5 | 5 | eax | `cmp eax,0x4; ja 0x432f85 at 0x432dc4` | 0x432d30 | yes |
| 75 | 0x4331bc | 0x432d1c | 5 | 5 | eax | `cmp eax,0x4; ja 0x433162 at 0x4331b7` | 0x432d30 | yes |
| 76 | 0x434796 | 0x434744 | 6 | 5 | eax | `cmp si,5; ja 0x434828 at 0x434787; xor eax,eax; mov ax,si` | 0x43475c | no |
| 77 | 0x4348d6 | 0x434888 | 6 | 5 | eax | `cmp si,5; ja 0x434968 at 0x4348c7; xor eax,eax; mov ax,si` | 0x4348a0 | no |
| 78 | 0x4414b5 | 0x441414 | 8 **recomp: 16** | 7 | eax | `cmp cx,7; ja 0x4414f8 at 0x4414ac; mov ax,cx (eax==0 from failed strchr at 0x4414a3/0x4414aa)` | 0x441454 | yes |
| 79 | 0x441771 | 0x441434 | 8 | 6 | eax | `cmp cx,7; ja 0x441742 at 0x441766; xor eax,eax; mov ax,cx` | 0x441454 | yes |
| 80 | 0x441a87 | 0x4419e4 | 6 **recomp: 12** | 4 | eax | `cmp cx,5; ja 0x441aab at 0x441a7e; mov ax,cx (eax==0 from failed strchr at 0x441a75/0x441a7c)` | 0x441a14 | yes |
| 81 | 0x441cc8 | 0x4419fc | 6 | 4 | eax | `cmp cx,5; ja 0x441b67 at 0x441cb9; xor eax,eax; mov ax,cx` | 0x441a14 | yes |
| 82 | 0x446b71 | 0x446a40 | 12 | 9 | eax | `cmp ax,0xb; ja 0x446aa9 at 0x446b64; movzx eax,ax` | 0x446a70 | yes |
| 83 | 0x447e22 | 0x447dba | 7 | 7 | eax | `cmp eax,0x6; ja 0x447f1b at 0x447e19` | 0x447dd6 | yes |
| 84 | 0x449ffb | 0x449ea0 | 9 | 7 | ecx (pre-scaled) | `cmp cl,8; ja 0x449fd0 at 0x449ff0; movzx ecx,cl; shl ecx,2 (index already ×4)` | 0x449ec4 | no |
| 85 | 0x44a253 | 0x44a1c8 | 7 | 6 | eax | `cmp al,6; ja 0x44a242 at 0x44a24c; movzx eax,al` | 0x44a1e4 | yes |

Notes on special guards (VERIFIED):
- `0x428b76`: index `edx = ebx*4` is set at `0x428aad..0x428aaf`; the guard is `cmp ebx,3` at `0x428b71`.
- `0x449ffb`: `cmp cl,8; ja; movzx ecx,cl; shl ecx,2`.
- `0x4414b5`/`0x441a87`: `mov ax,cx` only writes AX; EAX is 0 there because the preceding `strchr` (`0x442c5d`) returned 0 (`test eax,eax; jne` at `0x4414aa`/`0x441a7c`).
- Rows 76–77 (`0x434744`, `0x434888`, in the dead WM_HSCROLL/WM_VSCROLL handlers) and row 84 (`0x449ea0`, dead C++ EH) are in dead functions.

#### 2.2.2 Jump tables in code that is never decoded (VERIFIED dead)

| `jmp` | table | n | guard | containing code |
|---|---|---|---|---|
| `0x43c375` | `0x43b670` | 4 (`0x43c31d`, `0x43c330`, `0x43c342`, `0x43c352`) | `dec eax; cmp ax,3; ja 0x43c306` | dead `__CHK` function `0x43b680` |
| `0x44a39c` | `0x44a374` | 4 (`0x44a3ab`, `0x44a3a4`, `0x44a3b2`, `0x44a3db`) | `dec al; cmp al,3; ja 0x44a428` | C++ throw machinery `0x44a384` (dead) |
| `0x44b529` | `0x44b408` | 9 | `cmp al,8; ja 0x44b54c` | C++ EH `0x44b42c` (dead) |
| (`0x44bfa0`, patched to `ret`) | `0x44bf68` | 9 originally; 8 slots overwritten by patch code, slot 8 = `0x44bfa8` | `cmp bl,8; ja 0x44bfc3` | `0x44bf8c` (dead; patch made it return early) |
| `0x44c596` (`jmp [eax*4+0x454164]`, **DGROUP** table, no bound check) | `0x454164` | 64 → `0x44c59d..0x44ca1b` | none | FDIV-bug dispatcher `0x44c593`, unreferenced |

The recompiler skips the three BEGTEXT tables (adjacent-relocation rule in `cfg.py`). It does turn the 64 DGROUP slots
into function entries (§3.4).

### 2.3 Vtables and C++ objects

**There are no vtables.** VERIFIED:
- All 206 DGROUP→BEGTEXT pointers belong to the tables listed in §2.3–2.5. None is an array installed into an object.
- No instruction stores a DGROUP/BSS address into `[reg]` (vptr install).
- Every `call [reg+off]` in game/engine code (135 sites, `0x4339b6..0x43e49e`) loads its table from a COM interface pointer
  (`mov edx,[obj]; call [edx+off]`) on DirectDraw/DirectSound objects that the host creates (see `directx.md`).
- The remaining `call [reg+off]` sites are runtime structures (§2.4.4).

The game's C++ use is 21 global objects of 1-byte (empty) classes at `0x45dbc0..0x45dbd4`, constructed by the XI routine
`0x4037e4` (priority `0x40`). It first registers RW block `0x450ce4` with `0x442b2e`, then calls each constructor with
`EAX = &object` and stores the running count in `[0x450cec]` (state variable for unwinding). Destruction runs at exit
through YI `0x442a98` → `0x446baa` → `0x446a70`, which walks the RW table at `0x450834` (header `03 00 00 00`, then 21
pairs `{dtor, object}` at `0x450838..0x4508df`) and calls `[esi+4]`.

| object | ctor (called from `0x4037e4`) | dtor (table `0x450838`) | | object | ctor | dtor |
|---|---|---|---|---|---|---|
| `0x45dbc0` | `0x40a390` | `0x40a3bc` | | `0x45dbcb` | `0x428324` | `0x428379` |
| `0x45dbc1` | `0x40b97c` | `0x40b987` | | `0x45dbcc` | `0x428e60` | `0x428ea9` |
| `0x45dbc2` | `0x40cbf4` | `0x40cd19` | | `0x45dbcd` | `0x4298e4` | `0x429b77` |
| `0x45dbc3` | `0x41104c` | `0x411164` | | `0x45dbce` | `0x42b590` | `0x42b651` |
| `0x45dbc4` | `0x413110` | `0x413190` | | `0x45dbcf` | `0x42c108` | `0x42c212` |
| `0x45dbc5` | `0x4166d8` | `0x41673b` | | `0x45dbd0` | `0x42d5d0` | `0x42d694` |
| `0x45dbc6` | `0x417ab4` | `0x417cf6` | | `0x45dbd1` | `0x42e8f8` | `0x42ea9e` |
| `0x45dbc7` | `0x419f84` | `0x41a04c` | | `0x45dbd2` | `0x430270` | `0x4302e0` |
| `0x45dbc8` | `0x41de0c` | `0x41de2b` | | `0x45dbd3` | `0x4311f8` | `0x431266` |
| `0x45dbc9` | `0x41f6f8` | `0x41f9a4` | | `0x45dbd4` | `0x432630` | `0x4326c4` |
| `0x45dbca` | `0x422fb4` | `0x423066` | | | | |

DGROUP also holds 21 unreferenced 20-byte type-signature records at `0x4508e1 + 0x14*k` (k = 0..20), each
`{ctor, 0x442a84, dtor, 1, 0}` with the same ctor/dtor pairs as above (ctor first at `0x4508e1` = `0x432630`, last at
`0x450a71` = `0x40a390`). `0x442a84` is Watcom's "undefined constructor or destructor called!" stub (dead). The last byte
of the last record overlaps `0x450a84`, the base of the `ctype` table used as `[(c+1)+0x450a84]`. No code references the
records. VERIFIED.

### 2.4 Callbacks and function pointers

#### 2.4.1 Host→guest callbacks (VERIFIED sites; liveness as stated)

| Target | Kind / ABI | Installed at | Live? |
|---|---|---|---|
| `0x4343f1` | WndProc, `__stdcall`, `ret 0x10` | `mov [0x48604c],0x4343f1` at `0x433873` (WNDCLASS `0x486048`.lpfnWndProc) | live |
| 18 DLGPROCs: `0x402288`, `0x40d611`, `0x40d9e3`, `0x40deab`, `0x40e19b`, `0x40fb4d`, `0x4116a3`, `0x413944`, `0x413aec`, `0x41d724`, `0x41e912`, `0x41eaa7`, `0x41ecf6`, `0x42036d`, `0x4220fb`, `0x4222a7`, `0x4248dc`, `0x42a720` | `__stdcall`, `ret 0x10` | 21 pointer sites (18 `push imm`, 3 `mov eax,imm` at `0x40d1ee`/`0x40d502`/`0x41f0c4`) feeding the 16 `DialogBoxParamA` calls | live |
| `0x438003` | WINMM `TIMECALLBACK`, `ret 0x14`, runs on a timer thread | `push 0x438003` at `0x43688b` → `timeSetEvent` `0x436899` | live (see `directx.md` §5.7) |
| `0x447dd6` | SEH exception handler (`_EXCEPTION_REGISTRATION` handler) | `mov [eax+4],0x447dd6` at `0x4481b2`; record linked into `fs:[0]` at `0x4481c2` | installed at startup; runs only if the host raises a guest exception |
| `0x447b26` | console ctrl handler, `ret 4` | `push 0x447b26` at `0x447bf7` (`SetConsoleCtrlHandler` remove) and `0x447bcb` (install, undecoded) | never installed (`kernel32.md` §6.12) |
| `0x44c0b3` | thread proc of `_beginthread` | `push 0x44c0b3` at `0x44c1f2` → `CreateThread` `0x44c1fa` | dead |
| `0x434bc1` | `EnumDisplayModes` callback, `ret 8` | `push 0x434bc1` at `0x434b69` | dead (in unreferenced `0x434afe`) |
| `0x44b3a1` | `RtlUnwind` TargetIp | `push 0x44b3a1` at `0x44b396` | dead (longjmp) |
| `0x443f8f` | MCI notify handler (WndProc table, message `0x3B9`) | `0x443d11` | dead, out of scope |

There are no TimerProcs, enum callbacks, `qsort`/`bsearch` comparators, `atexit`/`onexit` registrations or `_beginthread`
users in live code (VERIFIED: the push/mov immediates in §2.4.2–2.4.3 exhaust all 321 code-pointer immediates).

Guest→host function pointer: the patch at `0x434133` stores `GetProcAddress(LoadLibraryA("KERNEL32.DLL"),"Sleep")` into
`[0x48804c]` and calls it with `call eax` at `0x434160` (`Sleep(0)`) and `0x43416b` (`Sleep(32)`). The host must return a
callable thunk address.

#### 2.4.2 Guest-internal function-pointer mechanisms (VERIFIED)

Called only by guest code with the Watcom register ABI, unless noted. All targets are recompiler entries.

| Mechanism | Store sites | Dispatch sites | Targets |
|---|---|---|---|
| **Screen callbacks.** `0x414e72(eax, edx, ebx, ecx, [stack]=fn)` stores the pushed `fn` into slot table `0x483a70[i]` (10 slots, in-use flags `0x483a08`). | 104 `push imm` + 1 `mov eax,0x41efd2` at `0x41e031`; list in App. A.1 | `call [ebp+0x483a70]` at `0x415241`, `0x4152bc`, `0x415b0e`, `0x4160d2`, `0x41614f`, `0x416279`; `call [edi+0x483a70]` at `0x415718`, `0x4157b8`, `0x4158e8` (EAX = slot, EDX = message code such as `0xF`, `0x110`, `0x111`) | 92 distinct (App. A.1), all live, none in Ghidra |
| **State slot `[0x4550b8]`** | 43 `mov [0x4550b8],imm` | `call [0x4550b8]` at `0x404252` | 43 (App. A.2) |
| **State slot `[0x4550c4]`** | 44 sites | `0x403d33`, `0x403f54`, `0x403fd4`, `0x404e01` | 42 |
| **State slot `[0x4550c8]`** | 36 sites | `0x4050d7` | 36 |
| **State slot `[0x4550cc]`** | `0x408a87` only | `0x40510f` | 1 |
| **State slot `[0x4550d0]`** | 21 sites | `0x40177a`, `0x408e2c`, `0x408f57`, `0x4098c2`, `0x40dc54`, `0x42471c`, `0x424855`, `0x4248b9` | 21 (all already Ghidra functions) |
| State slot `[0x4550bc]` | none (only zeroed at `0x4027a6`) | `0x404273`, guarded by `cmp [0x4550bc],0` | never called |
| **WndProc handler table** `0x486070[msg]` (msg < `0x800`; msg `0x8000` → `[0x488070]`), registered by `0x4343a2({msg, fn})`, read by `0x4343f1`; `0x4343d5` reads a slot | `0x40172e` (`0x1C`→`0x40a174`), `0x401744` (`0x2`→`0x40a15c`), `0x434091` (`0x2`→`0x434562`), `0x435d34` (`0x200`→`0x4361c7`), `0x435d4d` (`0x201`→`0x436304`), `0x435d66` (`0x203`→`0x4362ed`), `0x435d7f` (`0x204`→`0x436336`), `0x435d98` (`0x100`→`0x436361`), `0x438d7f` (`0x111`→`0x43918f`, restored at `0x438ea2`), `0x435ddd..0x435e64` (clear `0x200..0x205`, `0x100`, `0x101`) | `call [ecx+0x486070]` at `0x434427` | 9 live; dead registrations by `0x433bfc` (`0xF`→`0x434716`, `0x5`→`0x4345b8`, `0x114`→`0x43475c`, `0x115`→`0x4348a0`, `0x2`→`0x434562`) and `0x443d11` (`0x3B9`→`0x443f8f`) |
| `[0x45dc4c]`, set from the stack argument of `0x407c54` | `push` at `0x40ca67`, `0x40f3d9`, `0x40f575`, `0x42d19e`, `0x42d3bf` | `call [0x45dc4c]` at `0x407e6c`, `0x407f27` | `0x40c8f1`, `0x40f42e`, `0x40f57f`, `0x42ceac`, `0x42ce36` |
| `[0x48a62c]` | `mov [0x48a62c],0x43a8a8` at `0x439e88` | `0x43a871` | `0x43a8a8` |
| WinMain pointer `[0x48a6f0]` | `mov [0x48a6f0],0x40399d` at the PE entry `0x442cfc` | `call [0x48a6f0]` at `0x4478a3` (in `__NTMain 0x4477c4`) | `0x40399d` |
| printf output callback (`ECX` argument of `__prtf 0x446be6`) | `mov ecx,0x442ba1` at `0x442bbe` (sprintf: store to buffer); `mov ecx,0x448a4c` at `0x448aa4` (fprintf: `fputc`) | `call [esp+0x11c]` at `0x446cdb`, `0x446ddc`, `0x446dfe`, `0x446e28`, `0x446e70`, `0x446ec1`, `0x446eee`, `0x446f19`, `0x446f6a` | `0x442ba1`, `0x448a4c` |
| WndProc slots `0x487c70`/`0x487c74`/`0x487c78` (messages `0x700..0x702`) | never registered | `0x43486a`, `0x4349b0`, `0x4346f1` (all in dead functions) | none |

#### 2.4.3 Watcom runtime tables and hook variables (VERIFIED)

**XI (init) table `0x453748..0x45378f` and YI (fini) table `0x453790..0x4537ad`.** 6-byte records `{u8 type, u8 priority, u32 routine}`.
`type` 0 = pending, set to 2 when done. `__InitRtns 0x44868a` (limit in AL; walks `0x453748..0x453790`, lowest priority
first, `call eax` at `0x4486c7`) runs twice: from `__NTMain` at `0x4477d2` with AL=1 (priorities ≤ 1) and from `__NTInit` at `0x442ff8` with AL=`0xff` (all remaining). `__FiniRtns 0x4486d5` (DL = min, DH = max;
highest priority first, `call eax` at `0x448717`) runs from `__exit 0x443010` with range `0..0xff`.

| Table | Priority | Routine | Role (INFERRED) |
|---|---|---|---|
| XI | `0x01` | `0x442a80` | `ret` |
| XI | `0x01` | `0x448a3c` | reserve 0x20 bytes of per-thread data for C++ EH: `[0x48a698] = 0x44a5d7(0x20)` |
| XI | `0x01` | `0x44a590` | `[0x452728] = 0x44a4e9` (longjmp→C++ unwind hook) |
| XI | `0x02` | `0x44692a` | `__init_8087` (`fninit`, FPU detection `0x449b52`, `fnsave`/`frstor` hooks `0x4468e6`/`0x4468eb`) |
| XI | `0x03` | `0x44abc6` | Pentium FDIV-bug test (4195835/3145727); sets bit 0 of `[0x45258c]` if the FPU is faulty |
| XI | `0x0a` | `0x442b23` | `[0x48a6ac] = 0x4520e0` (C++ runtime accessor table `{0x442b13, 0x442b17, 0x442b1e}`) |
| XI | `0x0a` | `0x44b5fa` | `[0x48a6a8] = 0x45272c` (C++ EH accessor table) |
| XI | `0x20` | `0x43371c` | `ret` |
| XI | `0x20` | `0x4468cb` | `[0x452630] = 0x4499e7` (float printf `__EFG_printf`), `[0x452634] = 0x449b30` (float scanf hook) |
| XI | `0x20` | `0x448e24` | `__InitFiles`: link predefined streams `0x452144..` into list `0x48a6ec` |
| XI | `0x20` | `0x44aa68` | build `environ` array `[0x45257c]` from `[0x4524c5]` |
| XI | `0x40` | `0x4037e4` | game C++ static constructors (§2.3) |
| YI | `0x01` | `0x442a81` | `ret` |
| YI | `0x0a` | `0x4469ca` | release static lock `0x4524d4` via `[0x452578]` |
| YI | `0x0a` | `0x4485d9` | thread-data / TLS teardown |
| YI | `0x20` | `0x442901` | close all streams, free the stream list `0x48a6bc` |
| YI | `0x28` | `0x442a98` | C++ module static destructors (list `0x4524f8`, one block `0x450ce4`) |

**Hook variables** (DGROUP dwords with an initial code pointer; called through `call [abs]`). The only code that would
overwrite them is the dead MT initialiser `0x4484c0` (and `0x4485c8` for `[0x4520f0]`), so in practice they keep
their initial values:

| Slot | Initial | Live call sites | Role (INFERRED) |
|---|---|---|---|
| `0x4520d0`, `0x4520d4`, `0x4520d8` | `0x4429d5` (`ret`) | `0x4429d9`; `0x4429df`, `0x4429f1`; `0x4429f7` | exit hooks |
| `0x4520e0..0x4520e8` | `0x442b13`, `0x442b17`, `0x442b1e` | via `[0x48a6ac]` | C++ runtime accessors |
| `0x4520f0` | `0x442e85` (`mov eax,[0x48a694]; ret`) | 44 sites, e.g. `0x433738`, `0x442a42`, `0x449cff` | `__GetThreadPtr` (per-thread data block) |
| `0x4520f4`, `0x4520f8`, `0x452104`, `0x452108`, `0x45210c`, `0x452110`, `0x452114`, `0x452118`, `0x45211c`, `0x452120`, `0x452124`, `0x452128`, `0x45212c` | `0x442e8a` (`ret`) | 12 / 16 / 1 / 1 / 4 / – / 5 / – / dead / dead / 2 / 3 / 1 | stream, heap and I/O lock/unlock hooks |
| `0x4520fc`, `0x452100` | `0x442e8b` (`jmp 0x447976`), `0x442e90` (`jmp 0x447a8b`) | –, `0x446701` | stream helpers |
| `0x452130`, `0x452134` | `0x447d8b`, `0x447db8` | `0x442fdd`; dead | thread-data init / fini |
| `0x452570`, `0x452574`, `0x452578` | `0x448216` (`ret`) | `0x4469bc`, `0x4469c3`, 8 sites | static lock hooks |
| `0x452630`/`0x452634`/`0x452638`/`0x45263c` | `0x449b3e` ("Floating-point support not loaded"), same, `0x449b50` (`ret`), same | `0x4472bc` | float printf/scanf, FPU save/restore (rewritten by XI `0x4468cb` and `0x4468ef`) |
| `0x4524cf` | `0x446974` (`ret`) | none | unreferenced |
| `0x4526fc` | `0x44a7f5` ("ABNORMAL TERMINATION") | none (rewritten only at undecoded `0x447cc8`) | abort handler |
| `0x452728` | `0x44b5d6` (`ret`), XI sets `0x44a4e9` | `0x44b3b2` (dead longjmp) | longjmp unwind hook |
| `0x45272c..0x452744` | `0x44b5d8`, `0x44b5dc`, `0x44b5f5`, `0x44c002`, `0x44c15f`, `0x44c233`, `0x44b8d4` | `[0x452738]` at `0x44b687` (→ `0x44c002` → `jmp 0x44cc07`: `xor eax,eax; ret`); others dead | C++ EH / thread accessors |
| `0x452448`, `0x45244c`, `0x452454`, `0x452474`, `0x45247c`, `0x452488` | 0 | all guarded by a null check (`0x448f4a`, `0x44ab66`, `0x44337b`, `0x4429fd`) | windowed-console hooks, never set |

**FDIV dispatch table** `0x454164` (64 slots → `0x44c59d..0x44ca1b`): only `jmp [eax*4+0x454164]` at `0x44c596` uses it,
inside the unreferenced `0x44c593`. 16 of the targets are `add esp,0x2c; int 6` stubs. Dead.

#### 2.4.4 Inventory of non-import indirect calls (VERIFIED)

| Site(s) | Form | Possible targets |
|---|---|---|
| 135 sites in `0x4339b6..0x43e49e` | `call [reg+off]` | COM methods of host DirectDraw/DirectSound objects (`directx.md`) |
| `0x434427` | `call [ecx+0x486070]` | WndProc handler table (§2.4.2) |
| 9 sites `0x415241..0x416279` | `call [ebp/edi+0x483a70]` | 92 screen callbacks |
| `0x404252`, `0x404273`, `0x403d33`, `0x403f54`, `0x403fd4`, `0x404e01`, `0x4050d7`, `0x40510f`, 8× `[0x4550d0]` | `call [0x4550xx]` | state-slot sets (§2.4.2) |
| `0x407e6c`, `0x407f27` | `call [0x45dc4c]` | 5 targets |
| `0x43a871` | `call [0x48a62c]` | `0x43a8a8` |
| `0x4478a3` | `call [0x48a6f0]` | `0x40399d` (WinMain) |
| `0x434160`, `0x43416b` | `call eax` | host `Sleep` |
| `0x4486c7`, `0x448717` | `call eax` | XI / YI routines |
| `0x446cdb` … `0x446f6a` (9) | `call [esp+0x11c]` | `0x442ba1`, `0x448a4c` |
| `0x446a3b`, `0x446abc`, `0x446b0e`, `0x446b87`, `0x446b8c` | `call [esi+0xa]`, `[ecx+4]`, `[esi+6]`, `[esi+4]`, `[esp+0x18]` | destructors from RW tables (`0x40a3bc`…`0x4326c4`) and EH helpers |
| `0x449b94`, `0x449bc6` | `call [edx]`, `[ebx+8]` | C++ EH helpers (internal-error paths) |
| `0x4337b0`, `0x4337ce` | `call [esp+4]`, `[esp]` | new-handlers read from the C++ per-thread area (`[0x4520f0]() + [0x48a698]`) `+0x1c`/`+0x18`; no `set_new_handler` call exists in the game (INFERRED: always 0) |
| `0x447c92`, `0x447d71` | `call [esp]` | `signal()` handlers; `signal 0x447ca6` is unreferenced, so only defaults |
| `0x44a030`, `0x44bf13`, `0x44bf50` | `call [esp]` | `terminate`/`unexpected` handlers (internal-error paths; `0x44bf50` dead) |
| `0x448130` | `call [esp+0x22c]` | `MessageBoxExA` obtained with `GetProcAddress(LoadLibraryA("user32.dll"))` in the SEH filter (host function) |
| `call [abs]` through the hook variables of §2.4.3 | | initial values |
| dead: `0x449f53`, `0x449f67`, `0x44c151`, `0x44b3b2`, `0x44b8de`, `0x44b8e5`, `0x44a898`, `0x44a8f7`, `0x44a8e6`, `0x44a932`, `0x44c149`, `0x44c235`, `0x43486a`, `0x4349b0`, `0x4346f1`, `0x44bf50`, `0x44c10c`/`0x44c130` (`[0x4520f0]`) | | – |

There are no `jmp reg` / `jmp [mem]` other than the 85 jump tables and the 143 IAT thunks.

### 2.5 Pointers into BEGTEXT that are not code (VERIFIED)

| Address | Referenced from | What it is |
|---|---|---|
| `0x401003` | dword at `0x442d0b` | `___begtext` label; the dword keeps BEGTEXT alive; followed by `"WATCOM C/C++32 Run-Time system. (c) Copyright by WATCOM International Corp. 1988-1995…"` (`0x442d0f`) |
| `0x442c71` | `mov al, cs:[edx+0x442c71]` at `0x442cba` | `"0123456789abcdefghijklmnopqrstuvwxyz"` digit table of `utoa 0x442c96` |
| `0x446002` | `mov dl, cs:[edx+0x446002]` at `0x44604f` | same digits, `utoa` copy `0x446027` |
| `0x44c3c8..0x44c477` | `EDI` from get-PC stub `0x44c3c3`, read by `cmp edx/ecx, cs:[edi(+4)]` at `0x44c2c6..0x44c2df` | 21 × 8-byte `{hi, lo}` powers of ten (0, 1, 10 … 10^19) plus an all-ones sentinel at `0x44c470` |
| `0x44cf80`, `0x44cf86` | `push` at `0x43413d`, `0x434133` (patch, no valid relocation for the latter) | `"Sleep"`, `"KERNEL32.DLL"`; read by host `GetProcAddress`/`LoadLibraryA` |
| 85 + 3 tables | §2.2 | jump tables |

---

## 3. Function entries

### 3.1 Counts (VERIFIED)

| Set | Count |
|---|---|
| Ghidra functions (`functions.tsv`) | 945 (42 thunks) |
| Recompiler entries (`cfg.Program.discover`) | 1,415 |
| … in Ghidra | 944 (all but `0x44c3c3`) |
| … not in Ghidra | 471 |
| Live / dead recompiler entries | 1,262 / 153 |
| `push imm; call 0x43371d` prologues in BEGTEXT (raw scan) | 1,103 (442 not in Ghidra; 139 never decoded; 1 decoded but inlined: `0x43e639`, reached only by `jmp` from `0x434d0b`) |

### 3.2 Entries Ghidra missed (471, VERIFIED)

Ghidra only follows direct calls from code it has already decoded, so it misses functions reached only through pointers:

| Discovered through | Count | Live | List |
|---|---|---|---|
| screen-callback pushes (`0x414e72`) | 92 | 92 | App. A.1 |
| state slots `0x4550b8` / `0x4550c4` / `0x4550c8` | 43 / 41 / 35 | all | App. A.2 |
| DLGPROC | 18 | 18 | §2.4.1 |
| WndProc message handlers | 10 | 9 | §2.4.2 |
| `0x407c54` callbacks | 5 | 5 | §2.4.2 |
| WndProc `0x4343f1`, WinMain `0x40399d`, timer `0x438003`, SEH handler `0x447dd6`, printf callbacks `0x442ba1`/`0x448a4c`, FP hooks `0x4499e7`/`0x449b30`/`0x4468e6`/`0x4468eb`, unwind hook `0x44a4e9` | 11 | 11 | – |
| DGROUP tables: dtors (21) + `0x442a84`, XI/YI (15), runtime hooks (`0x442b13`, `0x442b17`, `0x442b1e`, `0x442e8b`, `0x442e90`, `0x446974`, `0x449b50`, `0x44b5d8`, `0x44b5dc`, `0x44b5f5`, `0x44c15f`) | 48 | 40 | §2.3–2.4.3 |
| FDIV dispatch table `0x454164` | 64 | 0 | App. B |
| dead MT initialiser `0x4484c0` register immediates and `0x448351` | 16 | 0 | App. B |
| immediates in undecoded dead code (`0x4345b8`, `0x434716`, `0x43475c`, `0x4348a0`, `0x434bc1`, `0x4439b3`, `0x447b26`) | 7 | 1 (`0x447b26`, pointer only) | – |
| bogus: `0x44cf80` (string) | 1 | – | §0.2 |
| `0x44b3a1` (RtlUnwind target) | 1 | 0 | – |
| **call-only** (callers are pointer-only functions, or code past a patched NOP sled in `0x401757`/`0x40271e`) | 79 | 77 | App. A.3 |

The 12 call-only entries caused by Ghidra's truncation of `FUN_00401757` and `FUN_0040271e` at the stale relocations
(§1.3): `0x401a19`, `0x414cc7`, `0x4338d3`, `0x434cfc`, `0x435db5`, `0x4364aa`, `0x436636`, `0x4391c3`, `0x439ecd`,
`0x44290d`, `0x442a13`, `0x442a70`.

### 3.3 Ghidra "functions" that are not ordinary function starts (VERIFIED)

| Entry | What it really is |
|---|---|
| `0x44c3c3` | **Real code**, get-PC stub (§0.1, §4.4). Only the bytes after the `call` (`0x44c3c8..0x44c477`) are data; Ghidra decoded them as `aam`/`daa`/`sldt`/`arpl`/`lcall` (pcode warning at `0x44c46a`). |
| `0x44c478` | `pop edi; ret`, the second half of the get-PC idiom; never called directly. |
| `0x40408f`, `0x407167`, `0x40e461`, `0x41c194`, `0x43dd52`, `0x43e82b`, `0x43e8e4`, `0x44242b` | Shared tails/epilogues entered only by `jmp`/`jcc` from other functions (§4.6). No prologue. |
| `0x44b3a1` | Label inside `longjmp` (the `RtlUnwind` return address); dead. |
| `0x434163`, `0x43d77b` | Patch detour pieces (§4.6). |
| `0x443002` | `__exit`, entered only by `jmp` (from `0x4429ee`, `0x442e25`, `0x44bed0`). Never returns. |
| `0x4477c4` | `__NTMain`, entered by `jmp` from the PE entry. |
| `0x447b4a`, `0x44b9c0`, `0x44cc07` | Alternative entries reached by `jcc`/`jmp` from `0x447b26`, `0x44ba05`, `0x44c002`. |
| `0x44b2d8`, `0x44b33c`, `0x44c002` | Single `jmp` thunks (to `0x44bed0`, `0x44bf2c`, `0x44cc07`). |
| `0x4440b4`, `0x4441cd`, `0x445d98`, `0x44cb00` | Unreferenced (no call, jump or pointer). `0x4440b4`/`0x4441cd` contain `int 0x10`/`int 0x31` (§6). |
| `0x444262`, `0x4443f9`, `0x44465a`, `0x444751`, `0x4447da`, `0x4449a4`, `0x445153`, `0x445745`, `0x44582d`, `0x445dd6`, `0x4461dd`, `0x449dd7`, `0x449ec4`, `0x44a807` | Called only from undecoded dead code. |

`0x401000` is not a function (Watcom `int3; jmp $-1` trap loop, §1.2) and is correctly absent from both sets.

### 3.4 Recompiler entries that should be reviewed

| Entry | Issue | Recommendation |
|---|---|---|
| `0x44cf80` | string `"Sleep"` decoded as code (`push ebx; insb` → trap) | blacklist |
| `0x44c3c3` | blacklisted, but live real code | un-blacklist with no-fall-through call (§0.1) |
| `0x44c59d..0x44ca1b` (64) | FDIV dispatch targets, dead; 16 are `int 6` stubs | may keep (never called) or blacklist |
| 153 dead entries | App. B | may keep; never called |

---

## 4. Non-standard control flow

### 4.1 Flags passed across `call`/`ret` (VERIFIED)

Only `0x444050` returns a result in a flag. Two independent checks were run: (a) the recompiler's interprocedural
liveness (`flags.Liveness`: `ret_live` non-zero only for `0x444050` = CF, `entry_live` empty), and (b) a scan of the
instructions after all 7,820 decoded `call`/`call [..]` sites for a flag read before a flag write. Both found only:

```
0x44403c fcos ; 0x44403e call 0x444050 ; 0x444043 jae 0x44403c ; ret     (cos, ST0 in/out)
0x444046 fsin ; 0x444048 call 0x444050 ; 0x44404d jae 0x444046 ; ret     (sin)
0x444050: push ebp; mov ebp,esp; push eax; fnstsw [ebp-2]; mov ah,[ebp-1]; or ah,1; sahf
          jnp ret_cf1                 ; C2=0 -> result valid, return CF=1
          fld tbyte [0x45235c]        ; 2*pi (80-bit 0x4001_c90fdaa22168c235)
          fxch; L: fprem; fnstsw; sahf; jp L ; fstp st(1); clc   ; reduced, return CF=0 -> retry
  ret_cf1: pop eax; pop ebp; ret
```

Host x87 requirements: `fsin`/`fcos` must set C2=1 when |x| ≥ 2^63 (out of range) and C2=0 otherwise. `fprem` must
report an incomplete reduction in C2. `sahf` must map C2 (AH bit 2) to PF and C0 to CF.

DF across calls (VERIFIED): `std` occurs only at `0x442c27` (`memmove`), `0x4453a0`, `0x4456ac` (mirrored blits). On every
path from each `std` to a `ret` there is a `cld`, and there are no calls in between. DF = 0 at all function boundaries.

### 4.2 setjmp / longjmp (VERIFIED)

| Address | Routine | Status |
|---|---|---|
| `0x44b342` | `setjmp(EAX = jmp_buf)`: stores ebx, ecx, edx, esi, edi, ebp at +0..+0x14, return EIP (`pop [eax+0x18]`) +0x18, ESP +0x1c, es/ds/cs/fs/gs/ss at +0x20/+0x22/+0x24/+0x26/+0x28/+0x2a, `fs:[0]` at +0x2c; returns 0 | unreferenced (not decoded by Ghidra or the recompiler) |
| `0x44b381` | `longjmp(EAX = jmp_buf, EDX = val)`: if `[eax+0x2c] != fs:[0]`, `RtlUnwind([eax+0x2c], 0x44b3a1, 0, 0)` at `0x44b39c`; calls the hook `[0x452728]`; reloads `ss` (`0x44b3ba`), `esp` (`0x44b3bd`), pushes the saved EIP, restores registers; `verr` checks the saved es/fs/gs selectors before loading them (0 if unreadable); `ret` to the saved EIP; val 0 → 1 | reached only by `jmp` at `0x449eff` from dead `0x449ec4` → dead |

This is the only `RtlUnwind` call and the only code that loads `ss` or `esp` from memory. The runtime's
"return-address mismatch" unwinding is therefore not needed for longjmp in this binary.

### 4.3 C++ exception handling and SEH (VERIFIED)

- **No throw sites.** The throw entry points `0x44a47c` (rethrow), `0x44a48c`, `0x44a496` → `0x44a435` → `0x44a268`/`0x44a2ae`/`0x44a307`/`0x44a384`
  have no references at all.
- **Live EH code** is limited to:
  - static-object registration and destruction (`0x442b2e`, `0x442a98`, `0x446ba0`, `0x446baa`, `0x446a70` with jump table `0x446a40`, `0x44a59c`, `0x4469dc`, `0x446a26`);
  - `0x44a49e`/`0x44a4e9` (pointer-live unwind hook; its only caller is the dead longjmp);
  - internal-error reporting (`0x449cff` "stack data has been corrupted!", `0x449ce8` → `0x44b2d8` → `terminate 0x44bed0`, `0x44a1e4`). These run only on corrupted EH state.
- **SEH**: `__NTInit 0x442fbc` → `0x448189` installs one record `{prev = fs:[0], handler = 0x447dd6}` at `[ebp-8]` of `__NTMain`
  (`0x4481b2..0x4481c2`); `0x4481c7` removes it at exit. `0x447dd6` is the Watcom exception filter that formats
  "The instruction at %08lx referenced memory at %08lx…" and shows it with `MessageBoxExA` (`LoadLibraryA("user32.dll")`/`GetProcAddress` at `0x44809d`/`0x4480b1`), then terminates.
  It runs only if the host delivers a guest exception. `fs` usage: `fs:[0]` read/write, `fs:[8]` (StackLimit) read at `0x442fe3` and `0x44a5ed`.

### 4.4 Code that manipulates return addresses or the caller's stack (VERIFIED)

| Site | Behaviour |
|---|---|
| `0x44c3c3` → `0x44c478` | get-PC: `call` pushes `0x44c3c8`, the callee does `pop edi; ret`, so control returns to the caller of `0x44c3c3` with `EDI = 0x44c3c8`. The only call/pop idiom in BEGTEXT (raw scan of all `E8` to a `pop`). |
| `__CHK 0x43371d` | `xchg [esp+4], eax` (swap pushed frame size and EAX), `call 0x43372d`, restore EAX, `ret 4`. Pops its caller's `push N`. |
| `0x43372d` → `0x43375c` | `0x43372d` pushes the size, `0x43375c` (page probe, writes one dword every 4 KiB below ESP) ends with `ret 4` and pops that push; `0x43372d` then `ret`s. The callee balances the caller's stack. |
| `0x43372d` | fails (`"Stack Overflow!"`, fatal `0x442e5c`) if `ESP - size <= [threadptr+0]`; the limit comes from `fs:[8]` (`0x44a5ed`). The host TIB StackLimit must be the real bottom of the guest stack. |
| `0x444816` | `pushal` at entry, `mov bp, cx` (uses BP as a counter), `popal; pop ebp; ret`. |
| `0x434163` (patch) | `pushal; push 0x20; call [Sleep]; popal; call 0x43adaf; jmp 0x43d77b`. |
| `0x4448ec`/`0x4448ee`/`0x444940`/`0x44493e`, `0x444c49`/`0x444c4e`, `0x444cc5`/`0x444cca`, `0x445db3`/`0x445dd3`, `0x449b52`/`0x449b76` | 16-bit `push`/`pop` (`66` prefix, 2-byte stack slots): `bx`, `dx`, `ax` |
| `call [esp]`/`call [esp+4]` (§2.4.4) | function pointers fetched into a stack slot and called |

No other return-address manipulation was found: no `push imm; ret` pair, no `call $+5`, no function entry that pops or rewrites `[esp]` (except the shared tails of §4.6, which are entered by `jmp`), no `jmp reg`.

### 4.5 Calls into the middle of functions / multiple entry points (VERIFIED)

Function bodies that fall through into another entry (the recompiler duplicates the shared code into both functions):

| Falls from | Into entry | How |
|---|---|---|
| `0x407b9f` | `0x407bbd` | `mov [eax+8],edx` at `0x407bba` falls through; `0x407bbd` is also called from 11 sites |
| `0x4081be` | `0x4081cb` | `push 4; call __CHK; mov eax,[eax+0x24]` then falls into the next `__CHK` function |
| `0x408441` | `0x408451` | same pattern (`add edx,[eax+4]; mov eax,[eax+0x24]`) |
| `0x403f09` | `0x403f22` | after `call 0x43daf1` at `0x403f1d` |
| `0x435e78` | `0x435e8f` | after `mov [eax+0xc],1` |
| `0x4372c7` | `0x4372df` | `cwde` at `0x4372de` |
| `0x44290d` | `0x442912` | `mov eax,3` then falls into `fcloseall` core |
| `0x442e85` | `0x442e8a` | `0x442e8a` is the shared `ret` hook |
| `0x448d75` | `0x448d7a` | `mov eax,0xd` then the errno setter |
| `0x448496` | `0x4484a0` | after `call 0x448449` |
| `0x4482c5`→`0x4482ca`, `0x448327`→`0x44832c`, `0x447d8b`→`0x447db8` | | dead MT code |
| `0x43372d` | `0x43375c` | **after the noreturn fatal call** `0x433757` |
| `0x443002` | `0x443021` (`malloc`) | **after `call ExitProcess`** at `0x44301c` |

### 4.6 Jumps between functions (VERIFIED)

- **130 `jmp` to another entry** (tail calls). Targets that are ordinary functions: game functions with a `__CHK`
  prologue `0x403f22`, `0x404049`, `0x404378` (18 sites), `0x405005`, `0x4058c8`, `0x4081cb`, `0x408451`, `0x4088f5` (15),
  `0x414ff9`, `0x4165ae`, `0x435f0e`, `0x43764d` (21); runtime functions `0x443002`, `0x4477c4`, `0x447976`, `0x447a8b`,
  `0x4482ca`, `0x44832c`, `0x448d7a`, `0x449ce8`, `0x44b33c`, `0x44b381`, `0x44b9c0`, `0x44bed0`, `0x44bf2c`, `0x44cc07`.
- **Shared-tail fragments** (no prologue; the jumping function's frame must match the fragment's pops). Decoded sources:
  `0x40408f` ← `0x4040e8` (fn `0x4040ba`); `0x407167` ← `0x4070da`, `0x4070f3`, `0x40710a`, `0x40711e` (fn `0x4070c0`), `0x40874d` (fn `0x40868e`);
  `0x40e461` ← `0x40e440` (fn `0x40e424`), `0x40e483` (fn `0x40e467`), `0x40e6e3`, `0x40e71b` (fn `0x40e5a5`);
  `0x41c194` ← 4 `jcc` in `0x41bc4c` and 10 `jmp`/`jcc` in `0x41cea2`; `0x43dd52` ← `0x43ab5a` (fn `0x43a9c8`), `0x43c7fe` (fn `0x43c3b4`);
  `0x43e82b` ← `0x44003c` (fn `0x43ff89`); `0x43e8e4` ← `0x43ff76`, `0x43ff84` (fn `0x43ff02`);
  `0x44242b` (epilogue `pop ebp; pop edi; …`) ← `0x43fbb9` (fn `0x43f7c1`), `0x44060a` (fn `0x44033d`), `0x442727` (fn `0x4426d1`);
  `0x44b3a1` ← `0x44b392` (dead longjmp). Undecoded dead code also jumps to `0x43dd52` (`0x43c318`) and `0x44242b` (`0x43f472`, `0x44104a`).
- **17 conditional jumps to another entry** (conditional tail calls): `0x40e440`/`0x40e483`/`0x40e6e3`→`0x40e461`;
  `0x41ab36`/`0x41d0e6` (`jbe`)→`0x403f09`; `0x41f586`→`0x403f22`; `0x41bf4c`, `0x41bf59`, `0x41c16d`, `0x41c176`, `0x41cf08`, `0x41cf68`, `0x41d047`→`0x41c194`;
  `0x4428fa` (`delete`: `jne free`)→`0x4431b8`; `0x443248`→`0x448aee`; `0x446927`→`0x4468ef`; `0x447b43`→`0x447b4a`.
- `jmp` to a function start that is not an entry: `0x434d0b` → `0x43e639` (`push 4; call __CHK; ret`); the recompiler inlines it.
- **Patch detour** (live): inside `0x43d646`, `0x43d776 jmp 0x434163` → `pushal … popal; call 0x43adaf; jmp 0x43d77b` →
  `0x43d77b jmp 0x43d737` (back into the loop of `0x43d646`). The recompiler treats `0x434163` and `0x43d77b` as entries,
  so this works as two tail calls plus a duplicated loop body.
- No tail jumps into import thunks; no `jmp [IAT]` outside the thunk area.

### 4.7 Overlapping instructions (VERIFIED)

One, in `strncat 0x4433d7`: `0x4433f4 66 a9 89 cf test ax,0xcf89`; `jne`/`jecxz` at `0x4433f1`/`0x4433e9` jump to `0x4433f6`
(`89 cf mov edi,ecx`), inside that instruction. The decoder must allow two decodings of the same bytes (`cfg.py` does).

### 4.8 Functions that never return (VERIFIED)

| Entry | Why |
|---|---|
| `0x442cfc` (PE entry) | `jmp 0x4477c4` |
| `0x4477c4` `__NTMain` | calls WinMain, then `exit 0x4429d6` |
| `0x4429d6` `exit`, `0x4429ee` | end in `jmp 0x443002` |
| `0x443002` `__exit` | `__FiniRtns`, then `ExitProcess(ESI)` at `0x44301c` (garbage exit code, `kernel32.md`) |
| `0x442e25` | write message to stderr, `jmp 0x443002` |
| `0x44cf48` (thunk `ExitProcess`), `0x44ce4c` (thunk `ExitThread`), `0x44c233` (`_endthread`, dead) | import never returns |
| `0x4440b4`, `0x4441cd`, 16 FDIV `int 6` stubs | trap (dead) |
| Effectively noreturn (return only if `[0x452500] != 0`, which is never written): `0x442e5c` (fatal runtime error), `0x446978`, and their wrappers `0x44a7f5` (ABNORMAL TERMINATION), `0x449b3e`, `0x448e24` (error path) | |

Recommendation: model `ExitProcess`/`ExitThread` as noreturn so that `0x443002` does not include `malloc`, and do not fall through after `0x433757`.

### 4.9 Code that reads its own bytes (VERIFIED)

Only through the `cs:` table reads at `0x442cba`, `0x44604f` and `0x44c2c6..0x44c2df` (§2.5), the 85 jump tables, and the
host reading the patch strings at `0x44cf80`/`0x44cf86`. No instruction has an absolute memory operand in BEGTEXT other
than these. No FPU constant lives in BEGTEXT (`0x45235c`, `0x454158..0x454160` are DGROUP). Guest memory must keep
the original BEGTEXT bytes mapped and readable.

### 4.10 Self-modifying code (VERIFIED)

None: BEGTEXT is not writable, there is no `VirtualProtect`/`WriteProcessMemory`/`FlushInstructionCache` import, no
store to an absolute BEGTEXT address, and no call or jump into DGROUP, BSS or heap. (`VirtualAlloc` memory is only used by the heap.)

---

## 5. Watcom runtime library map

Names are INFERRED from behaviour (VERIFIED instruction evidence cited). "Game calls" = direct `call` sites in
`0x401010..0x4428df`. Register arguments in Watcom order. These are candidates for host replacement through
`RT_RECONSTRUCTED`; keep the exact register/flag contract and the caller-visible side effects.

### 5.1 Memory and strings

| Address | Routine | Contract / evidence | Game calls |
|---|---|---|---|
| `0x442be4` | `memcpy(eax=dst, edx=src, ebx=n)` → eax=dst | `repne movsd` (F2 A5) + `repne movsb`; already reconstructed | 94 |
| `0x442c09` | `memmove` | `std; rep movsw; adc; rep movsb; cld` for overlap, else forward | 14 |
| `0x4428e0` | `memset(eax=dst, dl=byte, ebx=n)` → eax | replicates DL to EDX, calls `0x4463f0` (byte align) → `0x446427` (dword fill, `cmp [eax+0x20],dl` touch) | 181 |
| `0x4429bc` | `strlen(eax)` | `repne scasb` | 21 |
| `0x44296a` | `strcpy(eax, edx)` → eax | 2-byte unrolled | 109 |
| `0x442989` | `strcat(eax, edx)` → eax | | 49 |
| `0x442d99` | `strncpy(eax, edx, ebx)` | pads with 0 | 5 |
| `0x4433d7` | `strncat(eax, edx, ebx)` | overlapping instruction (§4.7) | 1 |
| `0x446340` | `strcmp(eax, edx)` | dword compare with `0xfefefeff` zero-byte trick; returns `sbb/or 1` sign | 4 |
| `0x44624a` | `strncmp(eax, edx, ebx)` | | 5 |
| `0x442b5a` | `stricmp(eax, edx)` | ASCII A–Z folding | 3 |
| `0x443fe8` | `strnicmp(eax, edx, ebx)` | | 6 |
| `0x442c5d` | `strchr(eax, dl)` | | 14 |
| `0x44a5c9` | `toupper` | | – |
| `0x450a84` | `ctype` table, indexed `[c+1]` (bit 2 = space, bit 0x20 = digit) | data | – |

### 5.2 Conversion, rand, misc

| Address | Routine | Notes | Game calls |
|---|---|---|---|
| `0x442c96` / `0x446027` | `utoa(eax=val, edx=buf, ebx=radix)` (two identical copies) | `div`; `cs:` digit table | – / 1 |
| `0x442ce0` / `0x446071` | `itoa`/`ltoa` (negative only for radix 10) | | 5 / 1 |
| `0x446170` | `atoi(eax)` | skips ctype-space, sign, digits | 1 |
| `0x4461dd` | `atol` (identical copy) | dead | – |
| `0x442c56` | `abs` | | 8 |
| `0x442a4c` | `rand()` | `seed = seed*0x41c64e6d + 0x3039` in thread data `+0xc` (via `0x442a42` → `[0x4520f0]`), returns `(seed>>16)&0x7fff` | 145 |
| `0x442a70` | `srand(eax)` | | 1 |
| `0x442a13` | `time(eax=t*)` | `GetLocalTime` (`0x446712`) + `mktime` (`0x446777`); rounds up if ms ≥ 500 | 1 |
| `0x443135` | `getenv(eax)` | `environ` `[0x45257c]`, `strnicmp` | 5 |
| `0x443361` | `getch()` (console) | console input path (`kernel32.md`) | 1 |
| `0x44290d` / `0x442912` | `fcloseall` / close streams from level | | 1 |

### 5.3 Heap, new/delete

| Address | Routine | Notes | Game calls |
|---|---|---|---|
| `0x443021` | `malloc(eax=size)` | heap list `0x452138`; grows with `0x4489db` → `0x44894b` (`VirtualAlloc`); `push/pop es, fs, gs`; `mov ds,edx` in `0x448724` (same value) | 2 (+ via `new`) |
| `0x4431b8` | `free(eax)` | `0x4487cc` | 1 |
| `0x44a965` | `calloc(eax, edx)` | | – |
| `0x44a775` | `realloc(eax, edx)` | `_msize 0x44b6f3`, `_expand 0x44b6fc`; copy with `repne movsd/movsb` | – |
| `0x43377c` | `operator new(eax=size)` | `malloc`, then new-handler loop (C++ per-thread area `+0x1c`, `+0x18`) | 24 |
| `0x4428f8` | `operator delete(eax)` | `test eax,eax; jne free` (conditional tail jump) | 22 |
| `0x44a9a3`, `0x44a9b7`, `0x44aa15` | heap shrink (`VirtualFree`) | | – |

### 5.4 Formatted output and streams

| Address | Routine | Notes | Game calls |
|---|---|---|---|
| `0x442bb4` | `sprintf(buf, fmt, …)` (all on stack, caller cleans) | `__prtf(eax=&ctx, edx=fmt, ebx=&args, ecx=0x442ba1)`; NUL-terminates | 211 |
| `0x443196` | `printf(fmt, …)` | stdout `0x45215e` via `0x448a5d` (`__fprtf`, output `0x448a4c` → `fputc 0x446273`) | 12 |
| `0x446be6` | `__prtf` engine | `%n` with far modifier uses `les` (§6); floats through `[0x452630]` → `0x4499e7` → `0x44ae8a` → `0x44bbc7` → `0x44ba69` → `0x44c2b4` → get-PC `0x44c3c3` | – |
| `0x4472c3`, `0x446f85`, `0x4470c3`, `0x447122`, `0x447146`, `0x447169`, `0x4471c7` | format-spec helpers | | – |
| `0x446273` | `fputc(eax=ch, edx=FILE*)` | | 9 |
| `0x448aee`, `0x448bd8`, `0x448dad`, `0x448ea5`, `0x448ef6`, `0x448f30`, `0x44ab20`, `0x44ac4c`, `0x44b6b2` | flush, flushall, buffer alloc, close, seek, write, `isatty` (`GetFileType`) | | – |
| `0x448c44`, `0x448cad`, `0x448cb8` | `conin$`/`conout$` handles | | – |
| `0x448d75`, `0x448d7a`, `0x448d88`, `0x448d9f`, `0x44ac94`, `0x44ace5` | errno / `_doserrno` setters | | – |

The game's file layer is **not** Watcom stdio: `0x4425ac` open, `0x442654` close, `0x442675` tell, `0x442690` seek,
`0x4426d1` read (`fread`-like, `ret 4`), `0x44272c` write, `0x44277d` getc, `0x4427c5` putc, `0x442539` delete; all call
the game-block KERNEL32 imports directly (contracts in `kernel32.md` §6.1).

### 5.5 FPU and math helpers

| Address | Routine | Notes | Game calls |
|---|---|---|---|
| `0x442d7c` | `__CHP` (truncate ST0) | `fnstcw`, set RC = chop (`[esp+1]=0x1f`), `frndint`, restore CW | 3 |
| `0x442dee` | `sqrt` | `ftst`; negative → `0x4478b6` (matherr, `0x44a626`/`0x44a630`) | 1 |
| `0x44403c` / `0x444046` / `0x444050` | `cos` / `sin` / range reduction | flags across `ret` (§4.1) | 1 / 1 |
| `0x44c29a` | `modf` | `ret 0xc` | – |
| `0x44692a`, `0x449b52`, `0x4468e6`, `0x4468eb` | FPU init, 287/387 detection (1/0, ±inf compare), `fnsave`/`frstor` hooks | | – |
| `0x44abc6`, `0x44c47c`, `0x44ca2f`, `0x44ca42`, `0x44cab4`, `0x44cb00` | Pentium FDIV-bug test and safe `fdiv` (scales both operands by 15/16 with `[0x454158]` when the divisor matches table `0x454148` and `[0x45258c]` bit 0 is set) | with a correct host FPU the flag stays 0 | – |
| `0x44ae8a`, `0x44b0c4`, `0x44bbc7`, `0x44ba69`, `0x44baaa`, `0x44c2b4`, `0x44c2ee`, `0x44c359`, `0x44be34`, `0x44be7f`, `0x44cb98`, `0x44ad13`, `0x44adb6`, `0x44ae19`, `0x44b92b`, `0x44b9c0`, `0x44ba2d` | float ↔ decimal conversion (`__cvt`/`__EFG`); 64-bit integer helpers live only here (`0x44be34` decimal string → u64, `0x44be7f` u64 → double, `0x44c2ee` double → u64 with rounding, `0x44c359` u64 → digits) | | – |

**64-bit helpers:** the game code calls no 64-bit runtime helper (`__U8D`, `__I8M`, etc. are not linked); all 64-bit
arithmetic is inline (`mul`/`imul`/`div` on EDX:EAX) or inside the float conversion code above. VERIFIED from the list of
game → runtime call targets.

### 5.6 Startup, exit, stack check

| Address | Routine | Notes |
|---|---|---|
| `0x442cfc` | `_cstart_` | `[0x48a6f0] = 0x40399d`; `jmp 0x4477c4`; followed by `dd ___begtext` and the copyright string |
| `0x4477c4` | `__NTMain` | `__InitRtns`, `__NTInit 0x442fbc` (`GetModuleHandleA`, `0x442e95` environment/argv, SEH install `0x448189`, `[0x452130]`), stack limit `0x44a5ec`, `_amblksiz` `0x44a61b`, `GetCommandLineA`, `0x448644` argv copy, `call [0x48a6f0]` (WinMain), `exit` |
| `0x4429d6` / `0x4429ee` / `0x443002` | `exit` / `_exit` hooks / `__exit` | §4.8 |
| `0x442e5c`, `0x442e25`, `0x446978` | fatal runtime error output (stderr `WriteFile`), then `__exit` | |
| `0x447b00` | `__WVIDEO` debugger hook | `int3` (§6) |
| `0x43371d` / `0x43372d` / `0x43375c` | `__CHK` / `__STK` / page probe | §4.4. Game functions call `__CHK` in their prologue |
| `0x44868a` / `0x4486d5` | `__InitRtns` / `__FiniRtns` | §2.4.3 |
| `0x447c26`, `0x447c5f`, `0x447b26`, `0x447b4a`, `0x447b7f`, `0x447bea` | `raise`/signal dispatch, console ctrl handler | default actions only |
| `0x447dd6` | SEH exception filter | §4.3 |
| `0x4483b0`..`0x4484c0`, `0x44a85a`..`0x44a8f2`, `0x44c0b3`..`0x44c233`, `0x448217`..`0x448351` | multi-thread runtime (TLS, mutexes, `_beginthread`) | dead (`0x4484a0`, `0x448371` and `0x4485d9` run) |
| `0x448fa4`..`0x449815` | `localtime`/`mktime`/`tzset` (`GetTimeZoneInformation`, "EST"/"EDT") | |
| `0x449b7c`..`0x44a59c`, `0x44a1e4`, `0x44b57b`..`0x44b605`, `0x44bed0`..`0x44c026` | C++ EH runtime | §4.3 |

---

## 6. Odd and privileged instructions

| Address | Instruction | Containing code | Reachable on Win32? | Semantics the lifter needs |
|---|---|---|---|---|
| `0x434163` / `0x43416d` | `pushal` / `popal` | Sleep patch detour (§4.6), called from live `0x43d646` | **live**, every frame | exact `PUSHAD` order (EAX, ECX, EDX, EBX, original ESP, EBP, ESI, EDI); `POPAD` skips ESP. `Sleep` is stdcall (pops 4) |
| `0x444819` / `0x4449a1` | `pushal` / `popal` | sprite blit `0x444816`, called from `0x43d37e` (live) | **live** | as above; body changes BP with `mov bp,cx`, which `popal` restores |
| `0x442c27` | `std` | `memmove 0x442c09` | **live** | DF=1 for `rep movsw`/`movsb`; `cld` at `0x442c37` |
| `0x4453a0`, `0x4456ac` | `std` | mirrored blits `0x4452ba`, `0x4455b9` (live) | **live** | backward `lodsd`/`lodsw`; `cld` at `0x44542d`, `0x44573d` |
| `0x4440bd`, `0x4440d5` | `int 0x10` (AH=0Fh get mode; AX=4F02h VBE set mode) | `0x4440b4` (DOS VESA init) | dead: no reference | would fault (#GP) on Win32; trap |
| `0x4441e5` | `int 0x31` (AX=0300h, DPMI simulate real-mode interrupt) | `0x4441cd` | dead: no reference | trap |
| `0x444168`, `0x444258` | `int 0x10`, `int 0x31` | undecoded `0x44415f`, `0x4441f0` | dead | trap |
| `0x44417d`, `0x44418f`, `0x4441a1`, `0x4441b3` | `in al,dx` (DX=`0x3DA`, VGA retrace wait) | undecoded `0x444175..0x4441bc` | dead | trap |
| `0x43d287` | `int 0x33` (AX=3, DOS mouse) | dead `__CHK` function `0x43d256` | dead | trap |
| 16 sites `0x44c5a6`…`0x44c9cd` | `int 6` | FDIV dispatch stubs (table `0x454164`) | dead (`0x44c593` unreferenced) | trap |
| `0x446c90`, `0x446d10`, `0x446d57` | `les edx, [mem]` | `__prtf 0x446be6`, `%n` with the far (`F`) modifier, followed by `mov es:[edx], eax/ax` | live function; needs a format with `%Fn`/`%Fhn`/`%Fln`. No such literal exists in DGROUP (VERIFIED: there is no `%n` at all). INFERRED unreachable | load EDX = dword [m], ES = word [m+4]; `es:` memory accesses use flat addressing |
| `0x447b11` | `int3` (+ `jmp $+8` over `"WVIDEO"`) | `0x447b00`, called by the fatal-error paths `0x442e5c`/`0x446978` | guarded by `cmp byte [0x452500],0`; `0x452500` is 0 and has no writer → not reached | trap (abort). Treating it as a no-op would make fatal errors return |
| `0x401000` | `int3; jmp 0x401000` | BEGTEXT header | never executed | not an entry |
| `0x44b3db`, `0x44b3e8`, `0x44b3f5` | `verr dx` | `longjmp 0x44b381` | dead | ZF=1 if the selector is readable. If ever needed: ZF=1 for the host's flat selectors, then `mov es/fs/gs` |
| `0x44b3ba`, `0x44b3bd` | `mov ss,[eax+0x2a]`; `mov esp,[eax+0x1c]` | longjmp | dead | – |
| many (`0x4429c3`, `0x442c25`, `0x448728`, `0x4487c2`, …) | `mov es/ds, r32`, `push/pop es/ds/fs/gs` | runtime (live) | **live** | store selectors as plain values. Memory accesses ignore segment bases except `fs:` (TIB). Pushes are 4 bytes |
| 10 sites (`0x442bf8`, `0x442bff`, `0x448677`, `0x44867e`, `0x449755`, `0x44975c`, `0x44a7ca`, `0x44a7d1`, dead `0x449f3c`/`0x449f43`) | `F2 A5`/`F2 A4` = `repne movsd/movsb` | memcpy, argv copy, tz, realloc | **live** | behaves like `rep` (F2/F3 are equivalent for `movs`); capstone drops the prefix, so read the raw bytes (`cfg.py` does) |
| `0x4448ec` etc. (§4.4) | 16-bit `push`/`pop` | asm blitters, FPU detect | **live** | 2-byte stack slots |

Not present in decoded code: `cli`, `sti`, `hlt`, `out`, `ins`/`outs`, `lgdt`/`lidt`, `iretd`, `retf`, `lcall`/`ljmp`,
`sldt`, `arpl`, `bound`, `enter`/`leave`. Linear-sweep hits for these (e.g. `sldt 0x44c406`, `arpl 0x44c45a`, `lcall 0x44c41e`,
`retf 0x441414`, `cli 0x40f9d9`) are all inside data (jump tables, the powers-of-ten table, strings). VERIFIED.

---

## 7. Recommendations for the entry and trap tables

1. Remove `0x44c3c3` from `config.json` `blacklist`. Make it an entry, and treat the `call 0x44c478` at `0x44c3c3` as
   "returns to the caller of `0x44c3c3`" (no fall-through decode at `0x44c3c8`), or reconstruct it by hand:
   `EDI = 0x44c3c8; return to caller` (no flags or other registers change). Regression test: press F7 (or set
   `[0x45dc04]=1`) and render a frame; `sprintf("%f")` must work.
2. Add `0x44cf80` to the blacklist (string). Optionally blacklist the 64 FDIV targets `0x44c59d..0x44ca1b`.
3. Jump-table bound detection: accept `cmp r16/r8, imm` on the low part of the index register and the
   `mov edx,ebx; shl edx,2 … cmp ebx,N` / `movzx; shl` pre-scaled forms. Expected counts are in §2.2.1 (only `0x441414`
   = 8 and `0x4419e4` = 6 differ from today's output).
4. Mark `ExitProcess`/`ExitThread` imports noreturn. Optionally mark `0x442e5c`/`0x446978` noreturn when `[0x452500]==0`.
5. Never apply `.reloc`. Ignore relocations inside the patch ranges of §1.3 for discovery.
6. Trap list for the lifter (abort with diagnostic if reached): §6 rows marked dead, plus `int3` `0x447b11`.
7. Keep BEGTEXT bytes mapped read-only in guest memory (jump tables, digit tables, powers of ten, patch strings).
8. Host x87 must honour C2 for `fsin`/`fcos`/`fprem` (§4.1). Host `GetProcAddress("Sleep")` must return a callable thunk.

---

## 8. Method (reproducible)

All numbers come from Python 3 + capstone 5.0.3 + pefile on `original/app/WET.EXE`, reusing `tools/recomp/pe.py` and `cfg.py`:

1. Load the image with `pe.load`, then run `cfg.Program.discover` with seeds = PE entry + `functions.tsv`, using the
   blacklist from `config.json`. This gives 1,415 entries, 85 tables and 587 slots.
2. Classify every relocated dword whose value is in `0x401000..0x44cfff` by location: table slot, an operand of a decoded
   instruction (checked with capstone `imm_offset`/`disp_offset` and 4-byte operand size), not inside decoded code
   (then the linear `analysis/binary/disassembly.asm` was used), or DGROUP.
3. Jump-table bounds were taken from the instruction pair immediately before each `jmp` (`cmp idx,N; ja`); the 10
   irregular cases were read by hand.
4. Raw scans over BEGTEXT: all `E8`/`E9`/`0F 8x` rel32 and all absolute dwords equal to a candidate address (unreferenced
   code), `push imm; call 0x43371d` prologues, `E8` to a `pop`, F2-prefixed string moves.
5. Reachability: worklist from `0x442cfc` over direct calls/jumps, tail calls, code-pointer immediates and memory
   displacements of live instructions; a DGROUP pointer slot becomes live when a live instruction references it (the
   destructor table through the RW block `0x450ce4`; XI/YI through `__InitRtns`/`__FiniRtns`).
6. Flag liveness: `tools/recomp/flags.py`, plus a separate scan of the instructions after every call.
7. Noreturn: fixed point over functions in which no path reaches `ret` without passing a call to a noreturn import or function.

---

## Appendix A. Entries Ghidra missed

### A.1 Screen callbacks (92 targets; registered through `0x414e72`)

`0x404f07`, `0x404f93`, `0x40538f`, `0x405948`, `0x406261`, `0x407cd8`, `0x40bd16`, `0x40c94c`, `0x40cb7d`, `0x40d123`, `0x40dfd8`, `0x40f5cb`, `0x40f9f4`, `0x40feac`, `0x4108f4`, `0x410ae3`, `0x410cd0`, `0x412b68`, `0x412f0c`, `0x413095`, `0x4137d4`, `0x414318`, `0x417362`, `0x4176c5`, `0x4185fd`, `0x418aa0`, `0x418c5c`, `0x419822`, `0x419af4`, `0x419bfc`, `0x41aa3c`, `0x41ab24`, `0x41ac0c`, `0x41ad9c`, `0x41af11`, `0x41b110`, `0x41b2b4`, `0x41b8ac`, `0x41ba46`, `0x41bc4c`, `0x41c45c`, `0x41cea2`, `0x41d0d4`, `0x41d1c8`, `0x41e45a`, `0x41e800`, `0x41efd2`, `0x41f574`, `0x41f6ab`, `0x4205b7`, `0x420f43`, `0x421101`, `0x4212ba`, `0x421693`, `0x421caf`, `0x424b50`, `0x424d38`, `0x424ffb`, `0x425075`, `0x4251ec`, `0x425509`, `0x4255c0`, `0x426431`, `0x4266d2`, `0x4267c8`, `0x426908`, `0x427573`, `0x427891`, `0x427cd6`, `0x4281eb`, `0x428c1d`, `0x42b054`, `0x42bc5b`, `0x42cee6`, `0x42d45b`, `0x42de52`, `0x42e0c4`, `0x42f03d`, `0x42f183`, `0x42f883`, `0x42f989`, `0x42fbeb`, `0x430f9d`, `0x4311a7`, `0x4314ef`, `0x43197e`, `0x431dbd`, `0x43239f`, `0x4329e0`, `0x432d30`, `0x4331c4`, `0x43353d`

Registration sites (105): `0x404aa6`, `0x404ee8`, `0x404f7f`, `0x40591f`, `0x4060f1`, `0x406222`, `0x407c9a`, `0x40bb3a`, `0x40c5c8`, `0x40c5e5`, `0x40cf79`, `0x40e162`, `0x40e77e`, `0x40e9df`, `0x40f792`, `0x40fe75`, `0x410401`, `0x410424`, `0x410435`, `0x410446`, `0x410bd6`, `0x4125aa`, `0x412618`, `0x4126b4`, `0x412bb4`, `0x4132fe`, `0x4140cb`, `0x416b6d`, `0x416c45`, `0x4180b3`, `0x4180ee`, `0x4180ff`, `0x419294`, `0x4192b4`, `0x419b38`, `0x41a323`, `0x41a3a6`, `0x41a3cf`, `0x41a3e3`, `0x41a4bd`, `0x41a4ea`, `0x41aa8b`, `0x41aaec`, `0x41ab7a`, `0x41abc3`, `0x41af6e`, `0x41b32c`, `0x41b33d`, `0x41baa0`, `0x41ced9`, `0x41d12f`, `0x41d182`, `0x41e031`, `0x41e071`, `0x41e0be`, `0x41e0ea`, `0x41f673`, `0x41ff02`, `0x420b0f`, `0x420b5a`, `0x420ba5`, `0x420bed`, `0x420c82`, `0x4233f4`, `0x423577`, `0x4246d4`, `0x4246fc`, `0x424836`, `0x424899`, `0x424c24`, `0x425123`, `0x42519f`, `0x4251b0`, `0x425fbd`, `0x426016`, `0x426027`, `0x426042`, `0x426072`, `0x426850`, `0x4275e7`, `0x427d7f`, `0x428819`, `0x42acaf`, `0x42ba75`, `0x42ca71`, `0x42ca99`, `0x42da77`, `0x42dec1`, `0x42ed69`, `0x42ed95`, `0x42f587`, `0x42f62f`, `0x42fb22`, `0x430ae5`, `0x430b0b`, `0x431408`, `0x431896`, `0x431ce1`, `0x431e4e`, `0x43286e`, `0x432a51`, `0x432a75`, `0x432bb6`, `0x432bd6`, `0x4333b2`

### A.2 State-slot targets

- `[0x4550b8]` (43): `0x40a51c`, `0x40a904`, `0x40b390`, `0x40bb08`, `0x40c58c`, `0x40ce79`, `0x40e748`, `0x41033c`, `0x4113b9`, `0x412558`, `0x4132e0`, `0x41409f`, `0x41681c`, `0x416a10`, `0x417eb0`, `0x41925c`, `0x41a1dd`, `0x41df38`, `0x41fde8`, `0x420aa4`, `0x423448`, `0x425f68`, `0x4285f4`, `0x42906c`, `0x42950e`, `0x429d58`, `0x42ac5e`, `0x42b797`, `0x42ba3f`, `0x42c4ec`, `0x42c9e0`, `0x42d940`, `0x42e424`, `0x42ec40`, `0x42f536`, `0x42fab3`, `0x4304b2`, `0x430a52`, `0x4313bc`, `0x43186a`, `0x431cb5`, `0x432842`, `0x433374`
- `[0x4550c4]` (42; 41 not in Ghidra): `0x40a5a2`, `0x40ab74`, `0x40b416`, `0x40bbce`, `0x40c619`, `0x40cfbe`, `0x40ebec`, `0x410500`, `0x4114c5`, `0x412757`, `0x413799`, `0x414129`, `0x416853`, `0x416ea0`, `0x418124`, `0x419455`, `0x41a72f`, `0x41e33a`, `0x42007a`, `0x420cab`, `0x42388e`, `0x42615a`, `0x4289a2`, `0x42935b`, `0x4295e9`, `0x42a04f`, `0x42ad85`, `0x42b8d0`, `0x42bab7`, `0x42c6f9`, `0x42cb0e`, `0x42dbd9`, `0x42e661`, `0x42ef01`, `0x42f699`, `0x42fb4f`, `0x4305af`, `0x430bb9`, `0x4318bd`, `0x431cfe`, `0x432923`, `0x433402`
- `[0x4550c8]` (36; 35 not in Ghidra): `0x40a6d6`, `0x40adcd`, `0x40b8d0`, `0x40bc1c`, `0x40c6de`, `0x40ef78`, `0x4107c8`, `0x41158e`, `0x4128fb`, `0x4137a4`, `0x4141cc`, `0x418989`, `0x41a95e`, `0x41e402`, `0x4201c2`, `0x423b28`, `0x4262c2`, `0x428a06`, `0x4293d3`, `0x4296d9`, `0x42a08f`, `0x42b35f`, `0x42b92d`, `0x42bb1d`, `0x42c7b0`, `0x42cc83`, `0x42dca3`, `0x42e821`, `0x42eff9`, `0x42f74c`, `0x43060b`, `0x430cc4`, `0x4314af`, `0x43190d`, `0x432944`, `0x433491`
- `[0x4550cc]` (1): `0x408a87`. `[0x4550d0]` (21, all in Ghidra): `0x40a410`, `0x40b9cf`, `0x40cd6d`, `0x4111ac`, `0x4131dd`, `0x41677e`, `0x417d39`, `0x41a081`, `0x41de60`, `0x41f9e7`, `0x4230cf`, `0x4283ae`, `0x428eec`, `0x429bba`, `0x42b699`, `0x42c25a`, `0x42d6dc`, `0x42eaf2`, `0x430328`, `0x4312ba`, `0x43270a`

### A.3 Call-only entries (79)

`0x401a19`, `0x40fca0`, `0x414cc7`, `0x4166c5`, `0x418f02`, `0x418f46`, `0x418f87`, `0x42ab86`, `0x4338d3`, `0x4343d5`, `0x434cfc`, `0x435db5`, `0x4364aa`, `0x436636`, `0x438d56`, `0x438fa4`, `0x4391c3`, `0x439ecd`, `0x43ab5f`, `0x43d5ee`, `0x44290d`, `0x442912`, `0x442a13`, `0x442a70`, `0x443246`, `0x443484`, `0x44356a`, `0x4437ec`, `0x443948`, `0x443ad4`, `0x443b0c`, `0x443c52`, `0x443e02`, `0x44654b`, `0x4465e1`, `0x4465f6`, `0x446602`, `0x446667`, `0x446712`, `0x446777`, `0x4468ef`, `0x446920`, `0x4469af`, `0x446baa`, `0x4484a0`, `0x448bd8`, `0x448cb8`, `0x448e9b`, `0x448ea5`, `0x448f30`, `0x448f9f`, `0x448fa4`, `0x448fdf`, `0x4490c4`, `0x4490f8`, `0x4493bb`, `0x4493eb`, `0x44967f`, `0x4496c8`, `0x4496ec`, `0x449815`, `0x449ad1`, `0x449b52`, `0x44a002`, `0x44a067`, `0x44a08c`, `0x44a1e4`, `0x44a49e`, `0x44a5d7`, `0x44a760`, `0x44a97d`, `0x44ac4c`, `0x44b57b`, `0x44ce76`, `0x44ce88`, `0x44ce94`, `0x44cee8`, `0x44ceee`, `0x44cf06`

## Appendix B. Dead recompiler entries (153)

- **FDIV dispatch targets (table 0x454164) 64:** `0x44c59d`, `0x44c5a3`, `0x44c5a8`, `0x44c5ae`, `0x44c5b3`, `0x44c5b9`, `0x44c5bf`, `0x44c5c5`, `0x44c5cb`, `0x44c5e7`, `0x44c5ec`, `0x44c602`, `0x44c607`, `0x44c623`, `0x44c633`, `0x44c647`, `0x44c657`, `0x44c677`, `0x44c67c`, `0x44c696`, `0x44c69b`, `0x44c6bb`, `0x44c6cf`, `0x44c6e7`, `0x44c6fb`, `0x44c71b`, `0x44c720`, `0x44c73a`, `0x44c73f`, `0x44c75f`, `0x44c773`, `0x44c78b`, `0x44c79f`, `0x44c7bf`, `0x44c7c4`, `0x44c7de`, `0x44c7e3`, `0x44c803`, `0x44c817`, `0x44c82f`, `0x44c843`, `0x44c863`, `0x44c868`, `0x44c882`, `0x44c887`, `0x44c8a7`, `0x44c8bb`, `0x44c8d3`, `0x44c8e7`, `0x44c907`, `0x44c90c`, `0x44c926`, `0x44c92b`, `0x44c94b`, `0x44c95f`, `0x44c977`, `0x44c98b`, `0x44c9ab`, `0x44c9b0`, `0x44c9ca`, `0x44c9cf`, `0x44c9ef`, `0x44ca03`, `0x44ca1b`
- **Watcom MT/TLS (0x4484c0 family) 30:** `0x448217`, `0x448243`, `0x44824d`, `0x448257`, `0x448264`, `0x448274`, `0x448279`, `0x448295`, `0x44829c`, `0x4482a6`, `0x4482ad`, `0x4482b7`, `0x4482be`, `0x4482c5`, `0x4482ca`, `0x448327`, `0x44832c`, `0x448351`, `0x4483b0`, `0x4483fc`, `0x448449`, `0x448496`, `0x4484c0`, `0x44a85a`, `0x44a891`, `0x44a8f2`, `0x44c0b3`, `0x44c15f`, `0x44c233`, `0x44ce4c`
- **C++ EH, setjmp/longjmp and EH accessors 17:** `0x442a84`, `0x442b17`, `0x442b1e`, `0x449dd7`, `0x449e13`, `0x449ec4`, `0x44a807`, `0x44b33c`, `0x44b381`, `0x44b3a1`, `0x44b5dc`, `0x44b5f5`, `0x44b8d4`, `0x44b8de`, `0x44b8e5`, `0x44bf2c`, `0x44bf8c`
- **DOS/VESA leftovers 2:** `0x4440b4`, `0x4441cd`
- **other 40** (dead window/scroll handlers, MCI, dead asm blit variants, unused thunks): `0x4345b8`, `0x434716`, `0x43475c`, `0x4348a0`, `0x434bc1`, `0x43d5ee`, `0x43dc8d`, `0x43dd59`, `0x442e8b`, `0x4439b3`, `0x443c52`, `0x443f8f`, `0x444262`, `0x4443f9`, `0x44465a`, `0x444751`, `0x4447da`, `0x4449a4`, `0x445153`, `0x445745`, `0x44582d`, `0x445d98`, `0x445dd6`, `0x4461dd`, `0x446974`, `0x447db8`, `0x44c478`, `0x44ca42`, `0x44cb00`, `0x44ce52`, `0x44ce58`, `0x44ce5e`, `0x44ce64`, `0x44ce70`, `0x44ceac`, `0x44ceb2`, `0x44ceb8`, `0x44cebe`, `0x44cec4`, `0x44ceca`

Note: `0x44c478` appears dead only because `0x44c3c3` is blacklisted (§0.1).

## Appendix C. `__CHK` functions never decoded (139, unreferenced closed set)

Every raw `call`/`jmp`/`jcc` or absolute-dword reference to these comes from code that is itself never decoded (VERIFIED by raw scan), so the set is closed. Examples: `0x433bfc` (alternate window
creation with `CreateWindowExA`), `0x434afe` (third `DirectDrawCreate` + `EnumDisplayModes`), `0x43b680`, `0x43d256`
(DOS mouse `int 0x33`).

`0x40405e`, `0x407b35`, `0x407b4c`, `0x4085da`, `0x408637`, `0x409e3f`, `0x415029`, `0x415d08`, `0x415d53`, `0x415d64`, `0x415d75`, `0x415e41`, `0x4314a4`, `0x432332`, `0x433b98`, `0x433ba9`, `0x433bfc`, `0x433e0a`, `0x4341a7`, `0x43426f`, `0x4349c0`, `0x434afe`, `0x434d8e`, `0x434de7`, `0x434dfa`, `0x434fc6`, `0x43501e`, `0x4354c1`, `0x435862`, `0x435a7b`, `0x435b58`, `0x435bd8`, `0x435f4b`, `0x435fbf`, `0x43610b`, `0x43611b`, `0x436146`, `0x43618e`, `0x43631b`, `0x43634d`, `0x4363e1`, `0x4365bc`, `0x436b43`, `0x436fb0`, `0x43789a`, `0x437901`, `0x437933`, `0x4379a6`, `0x437b71`, `0x437ce8`, `0x437da3`, `0x437db3`, `0x438675`, `0x438fb5`, `0x438fda`, `0x4392cc`, `0x43932c`, `0x439861`, `0x439c92`, `0x43a276`, `0x43a2f8`, `0x43a34a`, `0x43a6ed`, `0x43a893`, `0x43ac69`, `0x43acb0`, `0x43ad03`, `0x43ad1f`, `0x43ad33`, `0x43ad88`, `0x43ae28`, `0x43ae4e`, `0x43ae70`, `0x43ae95`, `0x43b0a2`, `0x43b24d`, `0x43b3e7`, `0x43b476`, `0x43b5e6`, `0x43b680`, `0x43c37d`, `0x43c803`, `0x43cc15`, `0x43cd6e`, `0x43cf22`, `0x43cf36`, `0x43d1c6`, `0x43d1dd`, `0x43d21c`, `0x43d239`, `0x43d256`, `0x43d506`, `0x43d78a`, `0x43dc1c`, `0x43e02f`, `0x43e075`, `0x43e0b7`, `0x43e0fc`, `0x43e1ce`, `0x43e266`, `0x43e34b`, `0x43e39c`, `0x43e3e6`, `0x43e402`, `0x43e4af`, `0x43e4f2`, `0x43e50b`, `0x43e644`, `0x43ea65`, `0x43eca6`, `0x43f1ad`, `0x43f5bf`, `0x43f6c7`, `0x43fc6a`, `0x43fda7`, `0x43fe2f`, `0x4400b6`, `0x44026c`, `0x44060f`, `0x4407a4`, `0x4409c6`, `0x440df8`, `0x440f44`, `0x44127b`, `0x441779`, `0x441cd0`, `0x442330`, `0x44280d`, `0x4434d2`, `0x4435c7`, `0x4436de`, `0x443747`, `0x44386d`, `0x4438d6`, `0x443b51`, `0x443d43`, `0x443d8e`, `0x443e4b`, `0x443eea`
