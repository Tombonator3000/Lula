# Completeness critique of the WET.EXE host-runtime specs

Scope: a cross-check of the six specs in this directory (`kernel32.md`, `user32-gdi32.md`, `directx.md`,
`winmm-timing.md`, `code-discovery.md`, `resources-and-game-map.md`) for a host runtime that must run the
**whole** game natively. The critique covers four things:
- imports that no spec covers adequately (§1);
- contradictions between the specs (§2);
- claims that look wrong, with spot-checks against the binary (§3);
- facts the implementer needs first to reach the main menu (§4).

Binary: `original/app/WET.EXE` (SHA-256 `8c223b52…cea9`, image base `0x400000`, entry `0x442cfc`). Analysis only:
nothing outside this file was changed.

Evidence tags:
- **VERIFIED**: read in this review from instruction bytes, relocations or resource data with capstone 5.0.3 and
  pefile. The method is in §6.
- **OBSERVED**: seen in the Wine 9.0 relay trace of the original game (`build/wine-ref/trace-relay.log` and
  `build/wine-ref/api-sequence.md`). Those files are local and untracked (`build/` is not in Git).
- **INFERRED**: interpretation.

On-hold scope (video/AVI/CUT, `CoCreateInstance`, `CoInitialize`, `CoUninitialize`, `mciSendCommandA`): sites are
listed by address and reachability only. Their behaviour is not analysed here.

---

## 0. Summary

1. **Every one of the 143 imports is mentioned in at least one spec**, with call sites. There is no import without a
   spec. Coverage gaps are about behaviour and reachability, not about missing names (§1).
   - The most important gap: `CoInitialize` (`0x439e74`) runs during start-up and `CoUninitialize` (`0x439eea`) runs at
     exit, **even with `-novideo`**. No spec says so. A stub is therefore needed to reach the main menu.
2. **Eleven contradictions between specs**, of which seven matter (§2). Each was settled against the binary:
   - the CD base path: relative, not `\`-rooted;
   - the owner of the `GetTickCount` sites;
   - DS_CENTER in the bank dialogs;
   - WM_CTLCOLOREDIT/BTN return values;
   - the role of `0x43e419`;
   - start-up-only `srand`;
   - the DirectSound volume of sound effects.
3. **29 claims were spot-checked: 21 held, 1 was imprecise, 7 failed** (§3.1). In row 6 the competing
   resources claim failed as well. Six more wrong or imprecise claims were found while checking (§3.2).
4. **Facts missing for the main menu** (§4):
   - no consolidated start-up gate table exists in Git;
   - the first-run video gate is not connected to the start-up path;
   - the x87 control word `0x127F` (53-bit precision) is undocumented;
   - the **Win32 system cursor is what the player sees in the main menu** (all DDF dialogs), which no spec states;
   - the COM stubs above are needed.

---

## 1. Import coverage (all 143 imports of `analysis/binary/imports.tsv`)

### 1.1 Coverage by group

| Group (IAT block) | Imports | Primary spec | Verdict |
|---|---|---|---|
| KERNEL32.dll (game) | 16 | `kernel32.md` §6–8 | Adequate. Two minor errors (§2 C1, C6; §3.2 X3) |
| KERNEL32.DLL (Watcom runtime) | 42 | `kernel32.md` §5–8 | Adequate. Every site is classified START/RUN/DEAD |
| USER32.dll (game) | 48 | `user32-gdi32.md`, `resources-and-game-map.md` A | Adequate except the rows in §1.2 |
| USER32.DLL (Watcom runtime) | 2 | `user32-gdi32.md` §13, §15 | Adequate. Wrong owner function (§2 C8) |
| GDI32.dll | 11 | `user32-gdi32.md` §18 | Adequate |
| WINMM.dll | 18 (17 in scope) | `winmm-timing.md` | Adequate except the volume claim (§2 C7) |
| DDRAW.dll / DSOUND.dll | 2 + COM | `directx.md` | Adequate. Two wrong error-path claims (§3.2 X1, X2) |
| comdlg32.dll | 1 | `user32-gdi32.md` §12.5 | Adequate |
| ole32.dll | 3 | on hold; sites only | **Gap: reachability** (§1.2) |

### 1.2 Imports with an inadequate spec

| Import | Gap | What the spec should add |
|---|---|---|
| `CoInitialize` / `CoUninitialize` (on hold) | Every spec lists `0x439e74` / `0x439eea` "addresses only". None says that they are on the **start-up and exit path regardless of `-novideo`**. OBSERVED: `CoInitialize(NULL)` from return address `0x439e7b` at t≈0.057 s, before `INIT_ALLCheck1`, retval 0 (trace line 3446). INFERRED from the resources map: the caller is the video-object constructor `0x439e2c`, constructed by INIT_ALL; `CoUninitialize` runs through the destructor `0x439ecd` from shutdown `0x401757`. | A one-line reachability note in the on-hold list: "a stub returning `S_OK` is required to reach the main menu; contract analysis is deferred with video". |
| `mciSendCommandA` (on hold) | Not reached before the main menu: 0 calls in the 30 s Wine trace, where `midiOutGetNumDevs` returned 0. No spec states this. | State that the main menu needs no MCI, and that `midiOutGetNumDevs()=0` keeps the MIDI module idle (`winmm-timing.md` §6.3 already gives the mechanism). |
| `ShowCursor` | `user32-gdi32.md` §9 describes the hide/show helpers but misses the **DDF dialog loop**. `0x4156ea` calls `0x4164d6` at `0x41571e`, which does `0x435f0e` (software cursor off) and `0x435ee9` (`ShowCursor(TRUE)`), refcounted in `[0x483ac4]`. `0x416504` (call at `0x41583b`) reverses this. VERIFIED. OBSERVED: `ShowCursor(1)` from `0x435f0b` just before the main-menu loop (trace line 14393). | The main menu and every DDF dialog show the **Win32 class cursor** (IDC_ARROW), not the TAF sprite. The host must draw a system cursor over the full-screen surface whenever the ShowCursor count is ≥ 0. |
| `DefWindowProcA` | The required default handling is listed (WM_CLOSE, SC_KEYMENU), but not WM_SETCURSOR. WM_SETCURSOR is what actually makes the IDC_ARROW (or IDC_WAIT after `SetCursor`) visible in the menu. | Add WM_SETCURSOR → class cursor (unless `SetCursor` overrode it), shown iff count ≥ 0. |
| `TranslateMessage` / `DispatchMessageA` | Only sites are listed; behaviour is unstated. | `TranslateMessage`: any behaviour is acceptable, because no WM_CHAR/WM_SYSCHAR handler is registered (the handler registrations are listed in `code-discovery.md` §2.4.2). `DispatchMessageA`: route to the WndProc of `msg.hwnd`, or to the dialog procedure for dialog windows; ignore `hwnd==NULL` messages. For WM_TIMER, never treat `lParam` as a TimerProc (it is 0). |
| `GetLocalTime` / `GetTimeZoneInformation` | `kernel32.md` §6.9 and §8 classify them as "RUN, once per new game". They run **once at START** (§2 C6). | Fix the reachability class. |
| `GetActiveWindow`, `wsprintfA` (`0x448035`, `0x44807c`) | `user32-gdi32.md` names the owner `0x447db8`, which is a 1-byte `ret` (§2 C8). | The owner is the SEH filter `0x447dd6`. |
| `MsgWaitForMultipleObjects`, `MultiByteToWideChar` | Video only; correctly deferred. | – |

All other imports have site tables, argument values, result handling and host semantics that are sufficient to
implement them.

---

## 2. Contradictions between specs

| # | Topic | Claim A | Claim B | Verdict (evidence) |
|---|---|---|---|---|
| C1 | Owner of `GetTickCount` `0x4269e3` / `0x4273b1` | `kernel32.md` §6.9, §8: handler `0x4267c8`, registered at `0x426028` | `winmm-timing.md` §1.11, §9.2: handler `0x426908`, registered at `0x426850` | **winmm is right.** VERIFIED: the nearest `push imm; call 0x43371d` prologue before both sites is `0x426908`. |
| C2 | CD base when `CDROM.LOC` is empty | `resources-and-game-map.md` B.6: `'\'` is appended, so CD files open as `\DATA\…` (rooted) | `kernel32.md` §6.2, `user32-gdi32.md` §12.5: the appended string at `0x44d5b8` is empty, so paths stay relative | **kernel32 is right.** VERIFIED: bytes at `0x44d5b8` = `00 00`; the `strcat` at `0x4021b8` appends "". OBSERVED: `CreateFileA("DATA\\CURSOR\\CURSOR.TAF")` from buffer `0x45523c` (trace line 4791). Side note: with an empty file, `0x40219e` reads the byte at `0x4551d7`, one byte **before** the buffer. Harmless. |
| C3 | DS_CENTER | `user32-gdi32.md` §12.4: "no template uses DS_CENTER" | `resources-and-game-map.md` A.4, A.9: AUSZAHLEN and EINZAHLEN have DS_CENTER | **resources is right.** VERIFIED: styles `0x940008c0` and `0x940008c4` both contain `0x800`. The host must centre these two dialogs on the 640×480 screen. |
| C4 | WM_CTLCOLOREDIT (`0x133`) / WM_CTLCOLORBTN (`0x135`) | `user32-gdi32.md` §12.2: return 0 (system default) | `resources-and-game-map.md` A.6: "0x133 and 0x135 fall into one of the ranges" (implying the tan brush) | **user32 is right.** VERIFIED in `0x40deab`: `0x133..0x135` go to `0x40dfa3` (`cmp eax,0x134`); only `0x134` reaches the brush path `0x40df7f`. Otherwise `0x40dfc1` returns `EBX=0`. |
| C5 | Role of `0x43e419` | `resources-and-game-map.md` B.2, B.4: "draw cursor/background" | `directx.md` §4.8: `GetBltStatus(2)` ×3 and `GetFlipStatus(2)` ×2, retried while WASSTILLDRAWING | **directx is right.** VERIFIED: `0x43e432`, `0x43e44d`, `0x43e468` are `call [edx+0x34]`; `0x43e483` and `0x43e49e` are `call [edx+0x48]`. The function draws nothing. |
| C6 | When `time()`/`srand` runs | `kernel32.md` §6.9: "only caller `0x402796` in new-game init `0x40271e`", class RUN, once per new game | `user32-gdi32.md` §4.1, `resources-and-game-map.md` B.2: `0x40271e` = INIT_ONCE_VARS at start-up | **START, once.** VERIFIED: the sole caller of `0x40271e` is `0x401579` in INIT_VARS `0x401535`, whose sole caller is `0x4011a0` in INIT_ALL. OBSERVED: `GetLocalTime` and `GetTimeZoneInformation` at t≈0.168 s, before the CD check. |
| C7 | DirectSound buffer volume | `winmm-timing.md` §1.8, §6.1: "pinned at 0 dB … callers pass `[0x455724]` (≥0), so buffer volume is always 0 dB" | `directx.md` §5.5: `SetVolume(clamp(min(vol,master)×100, −10000, 0))` | **directx is right; winmm is wrong for SFX.** VERIFIED: callers of `0x408752` (play SFX) pass ECX = 0 (37 sites), −10 (4), −15 (1) and −20 (1); 17 sites were not resolved by the simple scan. The value reaches `0x437ad2` via `0x408702`. OBSERVED: `IDirectSoundBuffer::SetVolume(-1000)` on the timer thread (trace line 13566). Only channel 8 (music; `0x40d275`, `0x40d3c0`) gets `[0x455724]` and therefore 0 dB. The host mixer **must** apply per-buffer attenuation. |
| C8 | Function that owns the SEH-filter sites (`GetActiveWindow` `0x448084`, `wsprintfA` `0x448035`/`0x44807c`, `GetModuleFileNameA` `0x4480ef`) | `user32-gdi32.md` §2, §13, App. A: `0x447db8` | `kernel32.md` §6.13, `code-discovery.md` §4.3: `0x447dd6` | **`0x447dd6` is right.** VERIFIED: `0x447db8` is `c3` (`ret`, the `[0x452134]` hook), followed by the jump table `0x447dba`; `0x447dd6` begins `push ebx; push esi; …; sub esp,0x220`. |
| C9 | Instruction form of the `Sleep` calls | `kernel32.md` §6.10, `code-discovery.md` §4.4, `user32-gdi32.md` §5.2: `call [0x48804c]` | `kernel32.md` §3, `code-discovery.md` §2.4.1: `mov eax,[0x48804c]; call eax` | **`call eax` is right.** VERIFIED at `0x43415b`/`0x434160` and `0x434166`/`0x43416b`. Lifter impact: these are register-indirect calls that the dispatcher must resolve to the host thunk. |
| C10 | What the `call [reg+disp]` sites in `0x4339b6..0x43e49e` are | `code-discovery.md` §0.4, §2.3: "135 sites … COM calls on DirectDraw/DirectSound objects that the host creates" | `directx.md` §9: about 29 DirectShow (video) COM sites inside that range, objects from `CoCreateInstance` | **code-discovery is wrong on attribution.** VERIFIED by re-running `cfg.Program.discover`: 136 decoded `call [reg+disp]` in that range = 111 DirectDraw/DirectSound + 24 in the video module `0x439e2c..0x43a98b` + 1 WndProc table call (`0x434427`). With video off, the 24 never execute. They must not be described as host-created DirectDraw/DirectSound objects. |
| C11 | `0x40413f` main loop | `resources-and-game-map.md` B.4: "called in a busy loop without Sleep" | `user32-gdi32.md` §7.3, `directx.md` §4.5: `Sleep(32)` after each present | Both are literally true: the loop has no `Sleep`, but each frame's present `0x43d646` does. The resources wording is misleading. |

Notation differences (not contradictions): handler-registration sites are given as the immediate store in
`code-discovery.md` (for example `0x40172e`) and as the `call 0x4343a2` in `user32-gdi32.md` (for example `0x401738`).
VERIFIED: both belong to the same sequence. `kernel32.md` §6.10 mentions a second caller of `0x4340b3`, `0x433d3d` in
`0x433bfc`, without marking it DEAD. `user32-gdi32.md` and `code-discovery.md` mark `0x433bfc` dead. The rendering-rate
estimates differ: about 25–30 fps (`user32`), 20–28 (`winmm`), about 31 (`directx`), and about 29 OBSERVED. All are
INFERRED and compatible.

---

## 3. Claims that look wrong; spot-checks

### 3.1 Spot-check results (29 claims; capstone/pefile over the PE image)

Results: **H** = held, **I** = imprecise (semantically close), **F** = failed.

| # | Claim (spec §) | Check | Result |
|---|---|---|---|
| 1 | Sleep patch: `LoadLibraryA("KERNEL32.DLL")`, `GetProcAddress("Sleep")` → `[0x48804c]` (`kernel32` §6.10) | `0x434133 push 0x44cf86; 0x434138 call 0x44cee2; 0x43413d push 0x44cf80; 0x434145 call 0x44cedc; 0x43414a mov [0x48804c],eax`; strings "Sleep" and "KERNEL32.DLL" at `0x44cf80`/`0x44cf86` | H |
| 2 | Sleep stubs use `call [0x48804c]` (`kernel32` §6.10, `code-discovery` §4.4) | `mov eax,[0x48804c]; call eax` | I (C9) |
| 3 | Relocations in `0x434130..0x434180` are `0x434139`, `0x43413e`, `0x434145`, `0x43414f`, `0x434161`, `0x434169`, `0x43416e`, `0x434175`, of which only `0x43413e` is aligned (`code-discovery` §1.3) | `.reloc` HIGHLOW set (14,980 entries) | H |
| 4 | 19-byte NOP sled at `0x401853` with stale relocations `0x401854`, `0x401859`, `0x401862` (`kernel32` §10) | bytes `90…`; `.reloc` also has `0x401868` (just after the sled) | H |
| 5 | DirectDrawCreate failure check removed: `test eax,eax` + 6 NOPs at `0x4340de` (`directx` §4.2, `kernel32` §6.10) | `0x4340dc 85c0`, `0x4340de..0x4340e3 90×6` | H |
| 6 | Empty string appended to the CD base (`kernel32` §6.2) vs `'\'` (`resources` B.6) | `0x44d5b8 = 00`; `0x4021ae mov edx,0x44d5b8; strcat` | H for kernel32, **F for resources** |
| 7 | `GetTickCount` sites in handler `0x4267c8` (`kernel32` §6.9) | prologue scan → `0x426908` | **F** (winmm H) |
| 8 | `ExitProcess(ESI)` at `0x44301b` (`kernel32` §1.6) | `0x44301b push esi; 0x44301c call 0x44cf48` | H |
| 9 | `SetTimer(hwnd, 1, 60, NULL)` at `0x433fca` (`user32` §4.3) | `push 0; push 0x3c; push 1; push eax` | H |
| 10 | WndProc dispatch: `msg < 0x801` → `[0x486070+msg*4]` with `EAX=hwnd, EDX=wParam, EBX=lParam, ECX=msg*4`; 0 → DefWindowProcA (`user32` §5.1) | `0x434405 cmp edx,0x801; jae`; `0x434427 call [ecx+0x486070]`; `0x434445 DefWindowProcA` | H; `0x486070+0x7F7*4 = 0x48804c` also holds (`user32` §5.2) |
| 11 | WM_MOUSEMOVE "held" iff wParam ∈ {1, 2, 0x10}; on release, clears `0x488b50`/`54`/`58` but not `0x488b5c` (`user32` §6.1) | `0x4361f5..0x4361ff`; stores at `0x436223..0x43622f` | H |
| 12 | WM_CTLCOLOR `0x133`/`0x135` → 0 (`user32` §12.2) | `0x40dec1..0x40dfc1` | H (resources F, C4) |
| 13 | No template uses DS_CENTER (`user32` §12.4) | RT_DIALOG styles: AUSZAHLEN `0x940008c0`, EINZAHLEN `0x940008c4` | **F** |
| 14 | Timer period: `a=max(min,50); res = a<max ? max(2,min) : max`; `timeSetEvent(res,10,0x438003,2,1)` (`winmm` §4.2) | `0x43684b..0x436899` | H |
| 15 | `je` patched to `nop nop` at `0x437f63` in SetWaveVolume (`winmm` §6.1) | `0x437f63 90 90` | H |
| 16 | SFX buffer volume always 0 dB (`winmm` §6.1) | `0x437ad2` clamps only positive values to 0; SFX callers pass −10/−15/−20 | **F** (C7) |
| 17 | `0x43e419` = Blt/Flip status waits (`directx` §4.8) | vtable `+0x34` ×3, `+0x48` ×2 | H (resources F, C5) |
| 18 | One `Sleep(32)` per matching entry; the loop continues after a match (`winmm` §9.1) | `0x43d753 cmp eax,[ecx+0x1c]`; match → `jmp 0x434163` → `popal` restores EDX → `0x43d77b jmp 0x43d737 (inc edx)` | H |
| 19 | Open-mode table: `"rb+"` → `0xC0000000`/share 0/OPEN_ALWAYS; `"rb"` → GENERIC_READ/FILE_SHARE_READ/OPEN_EXISTING; attributes `0x10000080` (`kernel32` §6.1) | `0x4425bf..0x442639`; mode strings at `0x4500d0` = `rb+`, `wb+`, `wb`, `rb` | H |
| 20 | `CloseHandle` must return exactly 1 (`kernel32` §6.1) | `0x442667 cmp eax,1; sete al` | H |
| 21 | `srand(time())` once per new game (`kernel32` §6.9) | `0x40271e` ← only `0x401579` (INIT_VARS) ← only `0x4011a0` (INIT_ALL) | **F** (C6) |
| 22 | `VirtualAlloc(NULL, size, MEM_COMMIT 0x1000, PAGE_EXECUTE_READWRITE 0x40)` at `0x448983` (`kernel32` §6.8) | `push 0x40; push 0x1000; push ecx; push 0` | H |
| 23 | `GetPrivateProfileIntA` result is `cwde`-extended into `[0x4550d4]`; −1 → 1 and written back (`kernel32` §6.4) | `0x403a40 cwde; 0x403a41 mov [0x4550d4],eax; cmp eax,-1; …; call 0x409e6a` | H |
| 24 | DirectSound primary `CreateSoundBuffer` failure → NULL dereference at `0x43676b` (`directx` §5.2) | `0x436721 call 0x436937` → `jmp 0x436766` → `mov eax,[0x48a4ec]; mov edx,[eax]` | H |
| 25 | `0x447db8` is the RTL exception reporter (`user32` §13) | `0x447db8 = c3` | **F** (C8) |
| 26 | `0x44c3c3` get-PC stub: `call 0x44c478` → `pop edi; ret`; called from `0x44c2b6` (`code-discovery` §0.1) | bytes as stated | H |
| 27 | Screenshot copies `0x96000` bytes linearly, so pitch must be 1280 (`directx` §1.4) | `0x409fad mov ebx,0x96000; 0x409fb6 call 0x442c09` | H |
| 28 | `0x437965` is "Set Frequency" (`resources` B.8) | the body returns `word[0x48a368+40ch] != -1`, i.e. is_busy | **F** (as `winmm`/`directx` say: is_busy) |
| 29 | "135 COM sites, all DirectDraw/DirectSound" (`code-discovery` §2.3) | cfg re-run: 111 DD/DS + 24 video + 1 WndProc table | **F** (attribution; the count is close) (C10) |

Tally: 21 H, 1 I (#2), 7 F (#7, #13, #16, #21, #25, #28, #29). Row 6 also refutes the competing claim in
`resources-and-game-map.md` B.6.

### 3.2 Further wrong or doubtful claims found while checking

| # | Spec § | Claim | Finding |
|---|---|---|---|
| X1 | `directx.md` §5.2 step 6, §5.9 | "all 16 SetFormat ≠ 0 → `[0x48a4d0]=DSERR_BADFORMAT` … sound disabled" | **Wrong.** VERIFIED: `0x436753` stores BADFORMAT and jumps to `0x436766`. There, `SetCooperativeLevel(hwnd,1)` at `0x436774` **overwrites** `[0x48a4d0]` at `0x436777`. If that call succeeds, the timer is armed (`0x4367a7 call 0x436807`) and sound stays enabled without a valid primary format. |
| X2 | `directx.md` §5.9 | "`SetCooperativeLevel(3)` ≠ 0 → skips primary creation; sound disabled" | **Wrong for the same reason.** `0x436710 jne 0x436766` → `SetCooperativeLevel(1)` overwrites the error. Low impact: the host should accept both calls anyway. |
| X3 | `kernel32.md` §6.8 | "the first segment … is 0x1000 bytes; later ones are at least 0x8000" | Imprecise. `_amblksiz` is `0x10` until `0x44a61b` runs (step 5). Every segment allocated before that is just 4 KiB-rounded. OBSERVED: `VirtualAlloc` sizes `0x1000`, **`0x3000`** (environment copy under Wine), then `0x8000`, `0x11000`, `0x97000` ×2, `0xd8000` … (trace lines 3184–4841). |
| X4 | `resources-and-game-map.md` A.8 | "Thunk table 0x44cc07-0x44cf65 … one per import (143)" | `0x44cc07` is `xor eax,eax; ret` (`code-discovery.md` §3.3). The thunks start at `0x44cc0c`. |
| X5 | `user32-gdi32.md` §1.6 | "`Sleep(32)` is called after every successful Flip" | `Sleep` runs once per **matching entry** after a successful back-buffer `Lock`, whatever the Flip result (`0x43d6e4..0x43d776`). With a non-matching pointer no `Sleep` runs, and with two matches two run (`winmm-timing.md` §9.1 has this right). |
| X6 | `directx.md` §4.11 | "the `call 0` happens at the first present (`0x434166`)" | The faulting instruction is `0x43416b call eax`; `0x434166` is the `mov eax,[0x48804c]`. |

---

## 4. Missing facts the implementer needs first to reach the main menu

Ordered by when they bite during start-up.

### M1. The start-up gate table is not in Git

The only ordered list of imports up to the main menu is `build/wine-ref/api-sequence.md`, which is local and
untracked. The specs scatter the pieces. Condensed gates (sites VERIFIED, order OBSERVED):

| Step | Import(s) (site) | Required result | If it fails |
|---|---|---|---|
| CRT | `GetModuleHandleA(NULL)` (`0x44789d`), `GetStdHandle` ×3, `GetEnvironmentStrings`, `GetModuleFileNameA`, `GetCommandLineA`, `GetVersion`, `VirtualAlloc` | `0x400000`; a double-NUL block; a command line **starting with a program token** | INFERRED: `hInstance` ≠ `0x400000` breaks `LoadStringA([0x455618], …)` unless the host maps every hInstance to the EXE |
| WinMain | `GetCurrentDirectoryA` (`0x4039c9`), `DeleteFileA` W_DEBUG, `GetPrivateProfileIntA`/`WritePrivateProfileStringA` (WET.INI), `GetCommandLineA` (`0x409f0f`) | see M2 for `-novideo` | – |
| Singletons | `CreateFontIndirectA` (`0x43ab46`), `LoadIconA`, `LoadCursorA`, `midiOutGetNumDevs` (`0x44346f`), `waveOutGetNumDevs` (`0x43661b`), **`CoInitialize` (`0x439e74`)** | non-zero handles; 0 MIDI devices recommended | see M3 |
| Window | `RegisterClassA`, `CreateWindowExA` (`0x433fb3`), **`SetTimer` (`0x433fca`)**, `ShowWindow`, `UpdateWindow` | `SetTimer` ≠ 0 | `SetTimer`=0 → `DestroyWindow` + error 0x14 → fatal box |
| DirectDraw | `DirectDrawCreate` (check NOPped), **`GetCaps`** (`0x434107`), **`GetDisplayMode`** (bpp 16/24/32), `SetCooperativeLevel(0x11)`, `LoadLibraryA`/`GetProcAddress("Sleep")`, `SetDisplayMode(640,480,16)`, `CreateSurface` ×2, `GetAttachedSurface`, `GetPixelFormat` (565), `Lock`/`Unlock` ×3 | DD_OK; pitch 1280; stable pointers | `GetCaps`≠0 → `[0x48804c]=0` → `call eax` with EAX=0 at `0x43416b` on the first present |
| INIT_VARS | `waveOutGetDevCapsA`/`waveOutGetVolume`, `FlipToGDISurface`, `GetSysColor` ×5, `GetLocalTime`, `GetTimeZoneInformation`, `DeleteFileA` FILMB.TMP, **200 × `WaitForVerticalBlank`(1)/(4)**, `ShowCursor(FALSE)` | – | the vblank loop is the black screen (6.65 s under cnc-ddraw) |
| CD check | `CreateFileA("CDROM.LOC")`, probe `DATA\CURSOR\CURSOR.TAF` (relative, C2) | the probe opens | `CD_NOT_FOUND_DLG` modal dialog (needs the dialog manager and `GetOpenFileNameA`) |
| LOAD_ALL | `WET.DDF`, `DATA\DIALOG\DIA_BACK.TGP`, `STD_BUT.TAF`, `BACK.TGP`, `PERSO.TAP`, `EQUIP.TAP`, `PORTRAIT.TGP`, `SOUND.TAP`, `MUSIC.TAP`; `FILMB.TMP` absent | case-insensitive VFS | error 8 → fatal box |
| Sound | `DirectSoundCreate`, `SetCooperativeLevel(3)`, **primary `CreateSoundBuffer`**, 17 × `SetFormat` (OBSERVED count: 17), `timeGetDevCaps`/`timeBeginPeriod(2)`/**`timeSetEvent`** | non-zero timer id; dwUser=2 | `DirectSoundCreate` failure = silent, safe; primary failure = crash at `0x43676b`; timer id 0 = later hang (`winmm-timing.md` §8.1) |
| First run | `CreateFileA("DATA\SAVE\WET.1ST")` fails → `SetDisplayMode` again (`0x401416`) → INTRO `0x40261b(0)` | see M2 | – |
| Menu | `CURSOR.TAF` ×3, `LoadCursorA(IDC_WAIT)`, INIT_STUFE (3 secondary buffers, 4 TAFs; T1 starts the looping ambient SFX at −10 dB, C7), Flip+`Sleep(32)`, DDF record 87, **`ShowCursor(TRUE)`** (M5), 4 vblank pairs, then the menu loop (`0x41655d`/`0x41658e` PeekMessage, 5 × GetDC/DrawTextA `0x2010`/ReleaseDC, Flip, `Sleep(32)`) | – | – |

Recommendation: copy this table, or the full `api-sequence.md`, into `docs/recomp/` so that it is versioned.

### M2. The video gate on the start-up path (sites only; video is on hold)

- On the first run (no `WET.1ST`), INIT_ALL calls INTRO `0x40261b` with `EAX=0` at `0x40141d`. INTRO calls the video
  wrapper `0x4092b1` at `0x4026e3` (`EAX=0x1f5a`). VERIFIED.
  - With `EAX≠0` (main-menu items, `0x40ff4d`/`0x40ff5b`) it also calls it at `0x4026a5` and `0x4026c4`.
- The **only** gate is `cmp dword [0x4550d4],0` / `je 0x4094cd` at `0x4092c7` (VERIFIED). The other 21 callers of
  `0x4092b1` share it (24 call sites in total).
- `[0x4550d4]` is 1 unless `WET.INI [VIDEO] VideoPlay=0`, or the command line contains `-novideo` **as a non-first
  token**. `0x409efa` drops the first token (`kernel32.md` §6.5).
- A host without video must therefore present a command line such as `WET.EXE -novideo`, or pre-seed `VideoPlay=0`.
  Otherwise the first run enters the on-hold video module before the main menu.

### M3. ole32 stubs are required even with `-novideo` (sites and reachability only)

- `CoInitialize` `0x439e74` is OBSERVED at start-up (retval 0).
- `CoUninitialize` `0x439eea` is on the shutdown path (INFERRED from the destructor call chain in
  `resources-and-game-map.md` B.3).
- `CoCreateInstance` `0x43a083` is not reached before the main menu (OBSERVED: absent from the trace).

### M4. x87 control word (no spec mentions it)

- XI init `0x44692a` (priority 2) runs `fninit`; `fnstcw` then reads `0x037F`.
- High byte 3 leads to `0x4468ef` → `0x449b52`, which ends with `fninit; fldcw [esp]` at `0x449b6f`. The loaded value
  comes from `[0x452640]` = **`0x127F`** (VERIFIED).
- `0x127F` means **53-bit precision**, round to nearest, all exceptions masked.
- `0x449b52` also contains a live 16-bit `push ax`/`pop ax` and a 1/0 versus −(1/0) compare (FPU detection). The host
  FPU must produce IEEE infinities with masked exceptions.
- The T1 timer callback uses no x87 (`winmm-timing.md` §5.1), so a per-thread control word only matters for future
  threads.

### M5. The cursor the player sees in the main menu

- In-game screens draw the TAF sprite cursor.
- **All DDF dialogs, the main menu included, switch to the Win32 cursor**: `0x4156ea` → `0x4164d6` → `0x435f0e` +
  `0x435ee9` → `ShowCursor(TRUE)`, with count −1 → 0 (VERIFIED; OBSERVED at `0x435f0b`).
- In the original, Windows draws IDC_ARROW (class cursor `0x4338b6`) over the exclusive full-screen surface. The host
  must render it itself. Without this, the main menu has no visible pointer.
- The hourglass (`SetCursor(IDC_WAIT)` at `0x403bcf`) appears only between sessions.

### M6. CD path and file root

Settled in C2: an empty `CDROM.LOC` gives **relative** paths from the process current directory. A rooted `\DATA\`
mapping is not needed. `resources-and-game-map.md` B.6 should be corrected.

### M7. One guest memory map

The constraints are spread across four specs and should be one table:

| Region | Constraint | Source |
|---|---|---|
| Image | `0x400000..0x4a5e00` (`.rsrc` ends at `0x494000+0x11e00`); `.bss` zero-filled; no relocation | `kernel32.md` §4.2, `resources-and-game-map.md` A.1 |
| Main stack | ≥ `0x32000`, fully committed; `fs:[8]` = its lowest address | `kernel32.md` §4.1 |
| T1 stack | its own, ≥ 16 KiB, placed **above** the main StackLimit | `winmm-timing.md` R8 |
| TIB | `fs:[0]` = −1 initially, `fs:[8]` as above; `pop fs` must not move the base | `kernel32.md` §4.1 |
| Heap arena | `VirtualAlloc` blocks below `0x80000000`, zero-filled | `kernel32.md` §6.8 |
| Surfaces | three 614,400-byte RGB565 buffers, pitch 1280, at stable addresses | `directx.md` §4.5 |
| Sound buffers | `Lock` pointers in guest memory, read concurrently by the mixer | `directx.md` §5.4, `winmm-timing.md` R10 |
| Host thunks | IAT values, the `Sleep` address and COM vtables must be guest addresses outside all of the above, dispatched to host code | `kernel32.md` §3, `directx.md` §4.1 |

### M8. Audible menu ambience

The menu starts a looping SFX on the timer thread with `SetVolume(-1000)` (C7). A host that ignores buffer volume plays
it 10 dB too loud. `winmm-timing.md` would currently mislead an implementer here.

### M9. Error-box path before the menu

Every init step ends in `0x4342ef`. If `[0x485f40]` is non-empty or `[0x455030]≠0`, it shows
`MessageBoxA(…, "ERROR", MB_OKCANCEL)` (`directx.md` §4.10). `MessageBoxA` must therefore work before the first frame,
over a window that may not be shown yet.

---

## 5. Recommended edits per spec

| Spec | Edit |
|---|---|
| `kernel32.md` | §6.9/§8: `GetTickCount` owner → `0x426908` (registered at `0x426850`). `GetLocalTime`/`GetTimeZoneInformation`/`srand` → START once (INIT_ONCE_VARS `0x40271e` ← `0x401535` ← `0x401010`). §6.10: `mov eax,[0x48804c]; call eax`. §6.8: segment sizes before `_amblksiz` is set. §6.10: mark `0x433bfc` DEAD. |
| `user32-gdi32.md` | §12.4: AUSZAHLEN/EINZAHLEN have DS_CENTER. §2/§13/App. A: owner `0x447dd6`, not `0x447db8`. §9: add the DDF-loop cursor switch (`0x4164d6`/`0x416504`, `[0x483ac4]`) and DefWindowProc WM_SETCURSOR. §1.6: Sleep-per-match wording. Add a behaviour note for `TranslateMessage`/`DispatchMessageA`. |
| `directx.md` | §5.2 step 6 and §5.9: the BADFORMAT and `SetCooperativeLevel(3)` paths are overwritten at `0x436777`. §4.11: the faulting site is `0x43416b`. |
| `winmm-timing.md` | §1.8/§6.1: buffer volume is 0 dB only for channel 8 (music) and other non-negative values; SFX are attenuated by 0/−10/−15/−20 dB per call site (`0x408752` callers); the host mixer must honour `SetVolume`. |
| `code-discovery.md` | §0.4/§2.3: of the 136 decoded `call [reg+disp]` in `0x4339b6..0x43e49e`, 24 are video (DirectShow, on hold) and 1 is the WndProc table call; 111 are DirectDraw/DirectSound. §4.4: `call eax`, not `call [Sleep]`. |
| `resources-and-game-map.md` | B.6: an empty CDROM.LOC gives relative paths (no `\`). A.6: WM_CTLCOLOREDIT/BTN return 0. B.2/B.4: `0x43e419` = Blt/Flip status waits. B.8: `0x437965` = is_busy. A.8: the thunks start at `0x44cc0c`. |
| new | Version a start-up gate table (M1) and a guest memory map (M7). Add the x87 control-word fact (M4) to `code-discovery.md` §5.5 or `kernel32.md` §5.1. Add an on-hold reachability note for `CoInitialize`/`CoUninitialize`/INTRO video (M2, M3). |

---

## 6. Method (reproducible)

- **Image mapping.** pefile `sections[*].get_data()` placed at `VirtualAddress` (base `0x400000`). `.bss` is never
  read for code facts.
- **Prologues.** A raw scan of BEGTEXT for `68 imm32 E8 rel32` with target `0x43371d` gave 1,103 starts. A site's
  owner is the nearest prologue at or below it. This was used for C1 and C6, and to find callers by `E8`/`E9` rel32
  scan.
- **Disassembly.** capstone 5.0.3, 32-bit, at the cited addresses.
- **Relocations.** `DIRECTORY_ENTRY_BASERELOC`, type 3 (14,980 entries).
- **Dialog styles.** The DWORD at template offset 12 of each DLGTEMPLATEEX (signature `0xFFFF0001`).
- **COM-site count.** `tools/recomp/cfg.Program.discover` with Ghidra seeds and `config.json` gave 1,415 entries.
  Indirect `call [reg+disp]` without an IAT slot were counted by address range.
- **Wine observations.** `build/wine-ref/trace-relay.log`, at the line numbers cited (Wine 9.0, `-novideo`, builtin
  ddraw, ALSA null device).
