# KERNEL32 host-runtime spec for WET.EXE

Scope: every KERNEL32 import in both IAT blocks of `original/app/WET.EXE`
(SHA-256 `8c223b52…cea9`, image base `0x400000`, entry `0x442cfc`). This is an analysis document for the
static recompilation's host runtime. It is not a decompilation.

Legend:
- **VERIFIED**: read directly from instructions, bytes or relocations in the binary. Addresses are guest VAs.
- **INFERRED**: interpretation (for example a Watcom library routine's name, or reachability that depends on runtime data).
- Reachability classes used in the tables:
  - `START`: runs during startup.
  - `RUN`: runs in normal play.
  - `EXIT`: runs at normal shutdown.
  - `COND`: runs only on a feature or error path.
  - `FATAL`/`EXC`: runs only on a fatal runtime error or after an OS-delivered exception.
  - `DEAD`: no static path reaches the site. Either no call, jump or relocated pointer reaches the containing function, or the only path runs through such a function.
  - `DEAD (MT)`: Watcom multi-thread support. `_beginthread` is never called (§6.11).
- Watcom register ABI in this binary: arguments in `EAX, EDX, EBX, ECX`, then the stack. Most game functions begin with
  `push N; call 0x43371d`, the `__CHK` stack probe, which pops its own argument.

Out of scope and not analysed: video/AVI/CUT, `CoCreateInstance`, `CoInitialize`, `CoUninitialize`, `mciSendCommandA`. §9 lists their sites only.

---

## 1. Summary for host implementers

1. **58 KERNEL32 imports, 104 static call sites.**
   - Game block: 16 imports, 32 sites, all `call dword ptr cs:[IAT]`.
   - Watcom runtime block: 42 imports, 72 sites, all `call <jmp-thunk>`.
   - Every import has at least one site. No IAT slot is ever read as data. All 90 relocations into KERNEL32 slots are `call [IAT]` (32) or `jmp [IAT]` thunks (58). VERIFIED.
2. **About 40 % of the sites are dead or never execute in a single-threaded GUI run.** This includes all
   thread, TLS, mutex and event APIs, `GlobalMemoryStatus`, `GetPrivateProfileStringA`, `MoveFileA`, `SetStdHandle`,
   `SetConsoleCtrlHandler`, `PeekConsoleInputA`, `WriteConsoleA`, `RtlUnwind` and `ExitThread`. These can be logging stubs that abort or return failure.
3. **Must work for a normal run:**
   - Runtime startup: `GetModuleHandleA`, `GetCurrentThreadId`, `GetStdHandle`, `GetEnvironmentStrings`, `GetModuleFileNameA`, `GetCommandLineA`, `GetVersion`, `VirtualAlloc`, `VirtualFree`.
   - Game file layer: `CreateFileA`, `ReadFile`, `WriteFile`, `SetFilePointer`, `CloseHandle`, `DeleteFileA`.
   - INI: `GetPrivateProfileIntA`, `WritePrivateProfileStringA`.
   - Other: `GetCurrentDirectoryA`, `GetTickCount`, `GetLocalTime`, `GetTimeZoneInformation`, `LoadLibraryA` + `GetProcAddress` (must return a callable **`Sleep`**), `ExitProcess`.
   - Std handles: `SetFilePointer` and `GetLastError` at exit, `GetFileType` on the first stdout/stderr write. They must fail gracefully.
4. **Paths:**
   - All game data is opened **relative to the process current directory**, with `\` separators. Some literals use `/` (§6.2).
   - Literals are upper case. Files on disk use mixed case, for example `DATA/BACK/back.tgp` and `DATA/SOUND/sound.tap`. The host must resolve paths case-insensitively and accept both separators.
   - The game writes `DATA\SAVE\…` and `DATA\DATABASE\…`. Those directories are **missing** in `original/app/` and must exist, in a writable copy.
5. **`fs` segment / TIB:** only `fs:[0]` (SEH list head, read and write) and `fs:[8]` (StackLimit, read) are used (§4.1).
6. **`ExitProcess` receives a garbage exit code (`ESI`, not `EAX`) on the normal exit path** (`0x44301b`). VERIFIED.
7. **Patched binary:** the `LoadLibraryA("KERNEL32.DLL")`/`GetProcAddress("Sleep")` code at `0x434133..0x43417c` is a later patch. It has no relocations, and stale relocations remain inside it. It installs `Sleep(32)` frame pacing (§6.10).

---

## 2. Method (reproducible)

- **Game-block sites:** every base relocation whose target is a KERNEL32 IAT slot was decoded with capstone.
- **Runtime-block sites:** the `jmp dword ptr [IAT]` thunks at `0x44ce4c..0x44cf4e` were found, then every `E8 rel32` in BEGTEXT whose target is a thunk. The scan used raw bytes, not the objdump listing; all hits were also instruction starts in the listing.
- **Unused game-block thunks:** game-block KERNEL32 slots also have `jmp [IAT]` thunks at `0x44cc42..0x44ce40`. **None of them is ever called.** VERIFIED.
  - Addresses: WriteFile `0x44cc42`, ReadFile `0x44cc48`, SetFilePointer `0x44cc4e`, CloseHandle `0x44cc54`, CreateFileA `0x44cc5a`, GetLastError `0x44cc60`, MultiByteToWideChar `0x44cc9c`, MoveFileA `0x44ccae`, GlobalMemoryStatus `0x44cd1a`, GetTickCount `0x44cd98`, GetCommandLineA `0x44cdc2`, WritePrivateProfileStringA `0x44cdc8`, GetPrivateProfileStringA `0x44cdce`, GetPrivateProfileIntA `0x44cdd4`, GetCurrentDirectoryA `0x44ce0a`, DeleteFileA `0x44ce40`.
- **Reachability:** a call graph was built from capstone linear disassembly per function. Function starts were the union of:
  - Ghidra entries;
  - every `push imm; call 0x43371d` prologue, of which 1,103 exist (442 are not Ghidra entries);
  - direct-call targets.

  Roots were the entry point plus all code addresses referenced from data. "DEAD" means no call, jump, relocation or raw dword in any section references the function (or every path into it runs through such a function). Raw dwords were checked explicitly for `0x434d8e`, `0x409e3f`, `0x439861`, `0x43b680`, `0x44b8d7`, `0x4479ef` and `0x447ca6`.
- **Ghidra function bounds are wrong in several places:**
  - WinMain `0x40399d` is folded into `FUN_004037e4`.
  - `0x43b680`, `0x40fca0`, `0x40d611`, `0x40d9e3`, `0x439861` and others are not separate Ghidra functions.
  - Containing-function addresses below use the corrected starts.

---

## 3. IAT layout (VERIFIED)

| Block | Slots | Imports | Called via |
|---|---|---|---|
| Game (`KERNEL32.dll`) | `0x48b238..0x48b274` | CloseHandle, CreateFileA, DeleteFileA, GetCommandLineA, GetCurrentDirectoryA, GetLastError, GetPrivateProfileIntA, GetPrivateProfileStringA, GetTickCount, GlobalMemoryStatus, MoveFileA, MultiByteToWideChar, ReadFile, SetFilePointer, WriteFile, WritePrivateProfileStringA | `2E FF 15 <slot>` (`call cs:[slot]`) |
| Runtime (`KERNEL32.DLL`) | `0x48b4c8..0x48b56c` | CloseHandle, CreateEventA, CreateFileA, CreateMutexA, CreateThread, DeleteFileA, ExitProcess, ExitThread, GetCommandLineA, GetConsoleMode, GetCurrentProcessId, GetCurrentThreadId, GetCurrentThread, GetEnvironmentStrings, GetFileType, GetLastError, GetLocalTime, GetModuleFileNameA, GetModuleHandleA, GetProcAddress, GetStdHandle, GetTimeZoneInformation, GetVersion, LoadLibraryA, PeekConsoleInputA, ReadConsoleInputA, ReleaseMutex, RtlUnwind, SetConsoleCtrlHandler, SetConsoleMode, SetEvent, SetFilePointer, SetStdHandle, TlsAlloc, TlsFree, TlsGetValue, TlsSetValue, VirtualAlloc, VirtualFree, WaitForSingleObject, WriteConsoleA, WriteFile | `E8` to thunk `FF 25 <slot>` at `0x44ce4c..0x44cf4e` |

Runtime thunk addresses:

| Thunk | Import | Thunk | Import |
|---|---|---|---|
| `0x44ce4c` | ExitThread | `0x44ceb8` | TlsGetValue |
| `0x44ce52` | CreateThread | `0x44cebe` | ReleaseMutex |
| `0x44ce58` | CreateEventA | `0x44cec4` | WaitForSingleObject |
| `0x44ce5e` | GetCurrentThread | `0x44ceca` | CreateMutexA |
| `0x44ce64` | SetEvent | `0x44ced0` | GetCurrentThreadId |
| `0x44ce6a` | GetFileType | `0x44ced6` | CloseHandle |
| `0x44ce70` | RtlUnwind | `0x44cedc` | GetProcAddress |
| `0x44ce76` | DeleteFileA | `0x44cee2` | LoadLibraryA |
| `0x44ce7c` | GetLastError | `0x44cef4` | SetConsoleCtrlHandler |
| `0x44ce82` | VirtualFree | `0x44cefa` | GetStdHandle |
| `0x44ce88` | GetTimeZoneInformation | `0x44cf00` | SetStdHandle |
| `0x44ce8e` | SetFilePointer | `0x44cf06` | GetLocalTime |
| `0x44ce94` | GetCurrentProcessId | `0x44cf0c` | WriteConsoleA |
| `0x44ce9a` | CreateFileA | `0x44cf12` | PeekConsoleInputA |
| `0x44cea0` | VirtualAlloc | `0x44cf18` | SetConsoleMode |
| `0x44cea6` | TlsFree | `0x44cf1e` | GetConsoleMode |
| `0x44ceac` | TlsSetValue | `0x44cf24` | ReadConsoleInputA |
| `0x44ceb2` | TlsAlloc | `0x44cf2a` | GetModuleHandleA |
| | | `0x44cf30` | GetVersion |
| | | `0x44cf36` | GetCommandLineA |
| | | `0x44cf3c` | GetModuleFileNameA |
| | | `0x44cf42` | GetEnvironmentStrings |
| | | `0x44cf48` | ExitProcess |
| | | `0x44cf4e` | WriteFile |

Host ABI for every import:
- `__stdcall`: the callee pops its arguments, the return value is in `EAX`, and `EBX/ESI/EDI/EBP` are preserved.
- Clobbering `ECX/EDX` matches Win32. Game wrappers save `ECX` themselves, for example at `0x442654` and `0x409e26`.
- `DF` must be clear on return.
- The patched caller at `0x434163` (`pushad; push 0x20; call eax; popad`) relies on `Sleep` popping its 4-byte argument.

---

## 4. Process prerequisites tied to KERNEL32 use

### 4.1 TIB / `fs` (VERIFIED: complete list of `fs:` operands in decoded code)

| Address | Instruction | Meaning |
|---|---|---|
| `0x442fe3` | `mov eax, fs:[8]` | `td->stack_low = TIB.StackLimit` (thread data `+0`) |
| `0x44a5ed` | `mov eax, fs:[8]` | `__init_stack_limits`: `td[0] = min(td[0], fs:[8])`; returns `esp - td[0]` |
| `0x448197` | `mov eax, fs:[eax]` (`eax=0`) | read SEH list head |
| `0x4481c2` | `mov fs:[edx], eax` | install the runtime SEH record (§6.13) |
| `0x4481d9` | `mov fs:[edx], eax` | unlink at exit |
| `0x44b374` | `push dword ptr fs:[0]` | C++ EH / jmpbuf save. DEAD |
| `0x44b389` | `cmp eax, fs:[0]` | before `RtlUnwind`. DEAD |

`0x44c3e4` decodes as `add fs:[eax],al`, but that address holds data after `call 0x44c478`. It is not code.

Segment registers:
- `push fs`/`pop fs` occur at `0x443027`/`0x443107`, inside `malloc` `0x443021`. `push gs`/`pop gs` at `0x443029`/`0x443105`.
- `mov fs,edx` and `mov gs,edx` at `0x44b3ef`/`0x44b3fc` are DEAD (longjmp restore).
- `mov e?x, ds` / `mov es, e?x` appear often. One example passes `ds` as a parameter: `0x442e62`, `0x44697e`.

Host requirements:
- Map `fs` to a TIB block in guest memory.
- Initialise `[0] = 0xFFFFFFFF` (end of the SEH chain).
- Set `[8]` to the lowest valid address of the guest main stack.
- The `__CHK` probe `0x43372d` compares `esp - framesize` against `td[0]`. A failure prints "Stack Overflow!" and exits (`0x433757`).
- `fs:[8] = 0` disables the check. That is acceptable but loses detection.
- Treat selector values as opaque constants. `pop fs` must not change the `fs` base.

PE stack size: `SizeOfStackReserve = SizeOfStackCommit = 0x32000`. A larger host stack is fine if `fs:[8]` matches it. The probe `0x43375c` touches every 4 KiB down to the new `esp`, so the whole stack must be committed.

### 4.2 Image / sections (VERIFIED)

- `VirtualSize` is 0 for every section except `.rsrc`. Use `SizeOfRawData`.
- `.bss` (`0x455000..0x48aa00`) has `PointerToRawData = 0`. It must be **zero-filled**, not read from file offset 0.
- `hInstance` passed to WinMain comes from `GetModuleHandleA(NULL)` at `0x44789d`. It must be `0x400000`: the game passes it on to USER32 resource and window calls, for example `DialogBoxParamA(…, "CD_NOT_FOUND_DLG", …)` at `0x402216` (INFERRED for the window-creation path).
- There are stale relocations in the patched region (§6.10). The image must not be rebased.

---

## 5. Watcom C runtime startup: entry `0x442cfc` → WinMain `0x40399d`

The entry point does two things:
- `0x442cfc` stores the WinMain address in the hook `[0x48a6f0] = 0x40399d`.
- It then jumps to `0x4477c4`, the Watcom NT WinMain driver.

| # | Address | Step | KERNEL32 used |
|---|---|---|---|
| 1 | `0x4477d2` | `__InitRtns(1)` (`0x44868a`): runs init-table entries with priority ≤ 1 (§5.1) | – |
| 2 | `0x4477d7..0x4477ef` | `alloca` thread data: size `[0x452644]=0xe2`, rounded to 4; zeroed with `memset` `0x4428e0` | – |
| 3 | `0x4477f9` → `0x442fbc` | `__NTInit(&seh_rec=[ebp-8], td)` | – |
| 3a | `0x442fc6` | `GetModuleHandleA(NULL)` → `EBX` | GetModuleHandleA |
| 3b | `0x442fd1` → `0x442e95` | `__NTMainInit(is_dll=0, td)`: | |
| | `0x442ea1` | `[0x48a690]=0` (not a DLL) | |
| | `0x442ea7` → `0x448371` | `td+0xc=1` (rand seed), `td+0xda=GetCurrentThreadId()` (`0x44839f`); `[0x48a694]=td` (`__GetThreadPtr` returns this) | GetCurrentThreadId |
| | `0x442ec2` | if td is NULL: `ExitProcess(1)`. Never taken | ExitProcess |
| | `0x442ec7` → `0x447ab7` | POSIX handle table: `GetStdHandle(-10/-11/-12)` become fds 0/1/2 in table `[0x48a6f8]`, count `[0x48a6f4]`. The table grows with `realloc`; this is the **first heap use** and so the first `VirtualAlloc` | GetStdHandle, VirtualAlloc |
| | `0x442ecc` | `GetEnvironmentStrings()` → `[0x4524c5]` | GetEnvironmentStrings |
| | `0x442ee2` | `GetModuleFileNameA(NULL, stackbuf, 0x104)` → `strdup` → `[0x452498]` (`_LpPgmName`; no reader) | GetModuleFileNameA |
| | `0x442ef3` | `GetCommandLineA()` → `strdup`, skip argv0 (quote-aware) and blanks → `[0x452494]` (`_LpCmdLine`; no reader) | GetCommandLineA |
| | `0x442f70` | second `GetModuleFileNameA(hModule)`, only if `is_dll`. DEAD | GetModuleFileNameA |
| | `0x442f81` | `GetVersion()` → bytes `[0x4524cb]`, `[0x4524cc]`, word `[0x4524cd]` | GetVersion |
| 3c | `0x442fd8` → `0x448189` | install SEH record `{prev=fs:[0], handler=0x447dd6}` at `[ebp-8]` of `0x4477c4`; `td+0x54 = &rec`; `fs:[0] = &rec` | (fs:[0]) |
| 3d | `0x442fdd` | `call [0x452130]` = `0x447d8b`: copy default signal table `0x45250c..` into `td+0x60..` | – |
| 3e | `0x442fe3` | `td[0] = fs:[8]` | (fs:[8]) |
| 3f | `0x442ff8` | `__InitRtns(0xff)`: remaining init-table entries (§5.1) | VirtualAlloc (via `malloc`) |
| 4 | `0x44780a` | `0x44a5ec`: stack limit and available stack. `[0x4524ac]=0`, so no stack-carved heap. `[0x4524b0]` is set | (fs:[8]) |
| 5 | `0x44782c` | `0x44a61b`: `_amblksiz [0x452704] = 0x8000` (minimum heap-segment growth) | – |
| 6 | `0x447831..0x447896` | `GetCommandLineA()` → `strdup` → skip argv0 and blanks. Char-class table `0x450a84`, bit 2 = blank | GetCommandLineA |
| 7 | `0x447898..0x4478a3` | `WinMain(GetModuleHandleA(NULL), 0, lpCmdLine, 10 /*SW_SHOWDEFAULT*/)` via `call [0x48a6f0]`. WinMain is `__stdcall`, `ret 0x10`. It reads only `hInstance` `[esp+0x18]` and `nCmdShow` `[esp+0x24]`; `lpCmdLine` is unused | GetModuleHandleA |
| 8 | `0x4478a9` | `exit(WinMain result)` → `0x4429d6` (§6.15) | ExitProcess |

### 5.1 Init/fini tables (VERIFIED)

The init table `XI` is at `0x453748..0x453790` and the fini table `YI` at `0x453790..0x4537ae`. Each entry is 6 bytes: `{u8 done(2), u8 prio, u32 fn}`.

`__InitRtns(limit)`:
- repeatedly picks the not-done entry with the **lowest** priority ≤ limit;
- on ties it takes the **later** table entry first, because the comparison is `ja`.

`__FiniRtns(0,0xff)` picks the **highest** priority first, with the same tie rule.

| Phase | Order | Fn | Prio | What (INFERRED name) | KERNEL32 |
|---|---|---|---|---|---|
| Init | 1 | `0x44a590` | 1 | `[0x452728]=0x44a4e9` (C++ EH hook) | – |
| Init | 2 | `0x448a3c` | 1 | reserve 0x20 bytes of thread data → `[0x48a698]` | – |
| Init | 3 | `0x442a80` | 1 | `ret` | – |
| Init | 4 | `0x44692a` | 2 | FPU init (`fninit`, 8087 detection) | – |
| Init | 5 | `0x44abc6` | 3 | FDIV-bug check → `[0x45258c]` | – |
| Init | 6 | `0x44b5fa` | 10 | `[0x48a6a8]=0x45272c` | – |
| Init | 7 | `0x442b23` | 10 | `[0x48a6ac]=0x4520e0` | – |
| Init | 8 | `0x44aa68` | 32 | build `environ` (`[0x45257c]`) from `[0x4524c5]` (`malloc`) | (VirtualAlloc) |
| Init | 9 | `0x448e24` | 32 | `__InitFiles`: `malloc(8)` list node per predefined stream stdin/stdout/stderr (`__iob` at `0x452144`, stride `0x1a`). Fatal "Not enough memory to allocate file structures" if `malloc` fails | (VirtualAlloc) |
| Init | 10 | `0x4468cb` | 32 | set `[0x452630]/[0x452634]` | – |
| Init | 11 | `0x43371c` | 32 | `ret` | – |
| Init | 12 | `0x4037e4` | 64 | game static initialiser (C++ global constructors) | – |
| Fini | 1 | `0x442a98` | 40 | C++ static destructors | – |
| Fini | 2 | `0x442901` | 32 | close all streams (no fd close for std streams) → for stdin/stdout/stderr: `SetFilePointer(FILE_CURRENT)`, possibly `SetFilePointer(FILE_BEGIN)`, `GetLastError` on failure | SetFilePointer, GetLastError (§6.12) |
| Fini | 3 | `0x4485d9` | 10 | destroy MT locks via `[0x452578]` (no-op `0x448216` in single-thread mode); `0x4484a0` → `TlsFree` guarded by `[0x4520ec] != -1` (never true) | (TlsFree: never) |
| Fini | 4 | `0x4469ca` | 10 | stdio lock cleanup | – |
| Fini | 5 | `0x442a81` | 1 | `ret` | – |

Runtime hook table `0x4520f0..0x452134` in single-thread mode (VERIFIED, DGROUP initial values):
- `[0x4520f0]=0x442e85`: `__GetThreadPtr`, returns `[0x48a694]`.
- `[0x4520f4..0x45212c]=0x442e8a`: `ret`, the no-op lock hooks.
- `[0x4520fc]=0x442e8b`: allocate POSIX handle.
- `[0x452100]=0x442e90`: free POSIX handle.
- `[0x452130]=0x447d8b`.
- `[0x452134]=0x447db8`: `ret`.

Only `0x4484c0` (`__InitMultipleThread`) replaces these hooks, and it runs only from `_beginthread`, which is DEAD.

---

## 6. Feature specifications

### 6.1 Game file layer (game-block `CreateFileA`, `ReadFile`, `WriteFile`, `SetFilePointer`, `CloseHandle`)

All game file access goes through eight wrappers. Each takes a "file manager" object in `EAX`, always `[0x455014]`; only `0x442539`/`0x43ff89` use it, as a path buffer at `+0..0x105`.

The error word **`[0x455030]`** is the only error channel callers check:
- 8 = open failed
- 9 = read failed
- 10 = write failed

The Watcom runtime's own `open`/`fopen` is **not linked**: runtime-block `CreateFileA` is called only for `conin$`/`conout$` (§6.12). VERIFIED.

| Wrapper | Signature (Watcom regs) | KERNEL32 call | Result handling | Callers |
|---|---|---|---|---|
| `0x4425ac` open | `EDX=path, EBX=mode` | `0x442639 CreateFileA(path, acc, share, NULL, disp, 0x10000080, NULL)` | `INVALID_HANDLE_VALUE` → returns 0 and sets `[0x455030]=8` | 73 |
| `0x442654` close | `EDX=h` | `0x442660 CloseHandle(h)` | `eax==1` → 0, otherwise −1. **Host must return exactly 1 on success** | 74 |
| `0x442675` tell | `EDX=h` | `0x442687 SetFilePointer(h,0,NULL,FILE_CURRENT)` | position returned unchanged | 20 |
| `0x442690` seek | `EDX=h, EBX=dist (signed 32), ECX=0/1/2` | `0x4426c1 SetFilePointer(h,dist,NULL,ECX)` | `0xFFFFFFFF` → −1, otherwise 0 | 101 |
| `0x4426d1` fread | `EDX=buf, EBX=size, ECX=count, [stack]=h` (`ret 4`) | `0x442710 ReadFile(h,buf,size,&n,NULL)`, once per element | stops on FALSE or `n≠size`; returns elements attempted, the failing one included | 117 |
| `0x44272c` fwrite | same | `0x44276b WriteFile(h,buf,size,&n,NULL)` per element | same | 43 |
| `0x44277d` getc | `EDX=h` | `0x4427a0 ReadFile(h,&b,1,&n,NULL)` | `b`, or −1 if FALSE or `n==0` (EOF) | 6 |
| `0x4427c5` putc | `EDX=ch, EBX=h` | `0x4427f1 WriteFile(h,&ch,1,&n,NULL)` | `ch`, or −1 | 13 |

Open-mode mapping in `0x4425ac` (VERIFIED). Mode strings sit at `0x4500d0..0x4500db`; `0x446340` is case-sensitive `strcmp`.

| Mode | `dwDesiredAccess` | `dwShareMode` | `dwCreationDisposition` | Note |
|---|---|---|---|---|
| `"rb"` | `0x80000000` GENERIC_READ | 1 FILE_SHARE_READ | 3 OPEN_EXISTING | used by nearly every reader |
| `"wb"` | `0x40000000` GENERIC_WRITE | 0 | 2 CREATE_ALWAYS | truncates |
| `"wb+"` | `0xC0000000` | 0 | 2 CREATE_ALWAYS | |
| `"rb+"` | `0xC0000000` | 0 | **4 OPEN_ALWAYS** | creates if missing, unlike C |
| anything else | `0xC0000000` | 0 | 4 OPEN_ALWAYS | |

`lpSecurityAttributes = NULL`, `dwFlagsAndAttributes = 0x10000080` (FILE_FLAG_RANDOM_ACCESS | FILE_ATTRIBUTE_NORMAL), `hTemplateFile = NULL`.

Host semantics:
- Handles must be non-zero and `≠ -1`. `0x4424d5` tests the handle for 0.
- ReadFile at EOF must return TRUE with `n=0`.
- `SetFilePointer` returns the new low 32-bit position, or `0xFFFFFFFF` with a last error.
- No file is larger than 4 GiB; `lpDistanceToMoveHigh` is always NULL.
- Share-mode conflicts do not matter: the game opens one handle per file, except copy (`0x4424b3`), which uses different files.
- Read-whole-file helper `0x43ff89`:
  - opens with `"rb"`;
  - seeks to the end and calls tell to get the size;
  - seeks to the start and does one `fread(size,1)`.

  An empty file must succeed. `CDROM.LOC` is 0 bytes in the repack.

### 6.2 Path derivation and complete file inventory

**Base directory.** Every path is relative to the process current directory, except W_DEBUG.DAT, which is absolute (built from `GetCurrentDirectoryA`). Neither the game nor the runtime derives data paths from `GetModuleFileNameA`. VERIFIED: no reader of `[0x452498]` or `[0x45249c]`.

**Path builder `0x40983f(name=EAX, mode=EDX)`** (98 callers):
1. clears the static 256-byte buffer `0x45523c`;
2. mode 0: `strcpy(buf, CDbase@0x4551d8); strcat(buf, name)` (62 callers);
3. mode 1: `strcpy(buf, name)` (30 callers; 6 pass a register);
4. returns `0x45523c`.

**CD base, `INIT_CD_PATH` `0x4020e0`:**
1. `memset(0x4551d8,0,100)`.
2. Read the whole of `CDROM.LOC` (cwd) into `0x4551d8` (`0x43ff89`).
3. If that fails: message `CD-PATH-INFO`.
4. Otherwise strip CR/LF and `strcat` the string at `0x44d5b8`. **That string is empty** (VERIFIED bytes), so no separator is ever appended. INFERRED: a repack patch.
5. Probe `resolve("DATA\CURSOR\CURSOR.TAF",0)` with `"rb"`.
6. On failure: `DialogBoxParamA("CD_NOT_FOUND_DLG", proc 0x402288)`. The dialog re-probes and writes the user's path to `CDROM.LOC` with `"wb"` (`0x4023f1`, `strlen(base)` bytes).
7. If still failing: `0x401757`, then `exit(-1)` (`0x40227e`).

With the shipped empty `CDROM.LOC`, mode 0 and mode 1 give the same relative path. If a user enters `D:\`, the host must map drive-letter paths.

`0x441f14` rewrites `/` to `\` up to the first `.`. It is used by the TAF/TBF loaders and the read helper, **not** by every opener, so the host's `CreateFileA` must accept both separators.

**Required host path handling:**
- Separators `\` and `/`; collapse duplicates (for example `C:\\W_DEBUG.DAT` when cwd is a root).
- **Case-insensitive** lookup. On-disk names include `back.tgp`, `sound.tap`, `Sekr_ani.TAF`, `Lula_ani.TAF`, `Extro.cut`, `cast_det.TAF`. Literals include `DATA/ANI/Verl_ani.TAF` and `DATA\ANI\Perskart.TAF`.
- Optional drive prefix `X:\`. A leading `\` means the root of the guest "drive".
- **Names containing spaces:** save slots use `"%3d"` (`"DATA\SAVE\SAVEGAME.  1"`) and screenshots use `"%4d.PCX"` (`"   0.PCX"`).

Files read (all `"rb"` unless noted). Literal sources are the string addresses in DGROUP:

| Path (as passed) | Opener / site | Note |
|---|---|---|
| `DATA\SAVE\WET.1ST` | `0x4012ce` in init `0x401010` | 0x84-byte settings block → `0x455034` |
| `CDROM.LOC` | `0x43ffc9` (via `0x402129`) | CD base path |
| `DATA\CURSOR\CURSOR.TAF` | `0x4021d8`, `0x4022f2`, `0x4023c0` | CD probe (mode 0) |
| `DATA\BACK\BACK.TGP`, `DATA\SOUND\SOUND.TAP`, `DATA\SOUND\MUSIC.TAP` (mode 0); `DATA\PERSO\PERSO.TAP`, `DATA\PERSO\EQUIP.TAP`, `DATA\PERSO\PORTRAIT.TGP` (mode 1) | `LOAD_ALL` `0x401ab6` | resource pools; handles stay open in `[0x455340..0x455358]` |
| `ADDGPX.TGP`, `ADDSND.TAP` (cwd), `DATA\SOUND\ADDSND.TAP` (mode 0) | `0x409a4f`, `0x410ee1`, `0x410f3b`, `0x410f11` | add-on pools |
| `DATA\DATABASE\FILMB.TMP` | `0x40208a` (probe, then delete), `0x411d94`, `0x411f1e`, `0x40bf2b`, `0x40c1b9`, `0x42a6ea` | game database; `"rb+"` at `0x411f5b`, `0x42a292`, `0x42a506` (OPEN_ALWAYS, read/write) |
| `DATA\SAVE\SAVEGAME.%3d`, `DATA\DATABASE\FILMB.%3d`, `DATA\DATABASE\STF1DAT.%3d` | load `0x40d9e3` (`0x40daec`, `0x40db96`, `0x40ddbc`), list `0x40d926`, `0x40fde5` | save slots |
| `DATA\PERSO\HLP.DAT`, `WET.DDF`, `DATA\DIALOG\*.TAF`, `DATA\ANI\*.TAF`, `DATA\BACK\*.TBF` | graphics/sound loaders `0x434ec2`, `0x435401`, `0x43e59c..0x441e1a`, `0x414d32`, `0x40725b` | names passed in by callers; 111 path-like literals in DGROUP, including `DATA/…` and a `?DATA/ANI/BUTCH.TAF` at `0x44e2e7` |
| `DATA\VIDEO\…` | `0x4092b1` / `0x40933b` | video: out of scope |

Files **written** (the host needs a writable directory tree containing `DATA\SAVE\` and `DATA\DATABASE\`, which are absent from `original/app/`):

| File | Where | API / mode | When |
|---|---|---|---|
| `<cwd>\W_DEBUG.DAT` | WinMain `0x4039c9..0x4039f3`; `0x409eb7` | `DeleteFileA` at start, then 59 `WritePrivateProfileStringA` trace writes | START / EXIT |
| `WET.INI` (no path, so the **Windows directory** under Win32 semantics) | `0x409e6a` | `WritePrivateProfileStringA` `[VIDEO] VideoPlay` | START (if missing) and EXIT |
| `DATA\SAVE\WET.1ST` | `0x401798` in `0x401757` | `"wb"`, 0x84 bytes from `0x455034` | EXIT and CD-failure exit |
| `CDROM.LOC` | `0x4023f1` in dialog proc `0x402288` | `"wb"` | COND |
| `DATA\DATABASE\FILMB.TMP` | `0x439204` (pool create `0x4391ce`, `"NGS"` header); updates `"rb+"` | create when missing (`0x411bff`) | RUN |
| `DATA\DATABASE\FILMB.TM@` | `0x442539` | `DeleteFileA`, then copy `FILMB.TMP` → `FILMB.TM@` (`0x4424b3`, byte-wise `getc`/`putc`) | before pool create |
| `DATA\SAVE\SAVEGAME.%3d` | `0x40d71d` in save dialog `0x40d611` | `"wb"` | RUN (save) |
| `DATA\DATABASE\FILMB.%3d` | `0x40d7cb` copy `FILMB.TMP` → slot | `"rb"` + `"wb"` | RUN (save) |
| `DATA\DATABASE\FILMB.TMP` | `0x40dbc0` copy slot → `FILMB.TMP` | `"wb"` | RUN (load) |
| `DATA\DATABASE\STF1DAT.%3d` | `0x40fcef` in `0x40fca0` | `"wb"` | RUN (save) |
| `"%4d.PCX"` (counter `[0x45dc10]` from 0, so `"   0.PCX"` …) | `0x409f76` → PCX writer `0x44033d` (`"wb"` at `0x440448`) | **F8** key (`[0x455384]==0x77`, dispatcher `0x4094d4`) | COND (screenshot) |

Writers with no static path: TBF `0x44060f`, TAF `0x4409c6`, TFF `0x440df8`, `0x44127b`, and pool compaction `0x439861`.

### 6.3 `DeleteFileA` / `MoveFileA`

All return values are ignored at game sites. The runtime `remove()` checks it.

| Site | Containing fn | Path | Condition |
|---|---|---|---|
| `0x4020ad` | `LOAD_ALL` `0x401ab6` | `DATA\DATABASE\FILMB.TMP` | only if it exists (open `"rb"` succeeded); stale database removed at startup |
| `0x402973` | `0x4028d4` (`INIT_GAME_VARS`) | `DATA\DATABASE\FILMB.TMP` | unconditional |
| `0x4039f3` | WinMain | `<cwd>\W_DEBUG.DAT` | always, at start |
| `0x40d7e6` | save `0x40d611` | `DATA\DATABASE\FILMB.%3d` | copy failed |
| `0x40dbd5` | load `0x40d9e3` | `DATA\DATABASE\FILMB.TMP` | slot database could not be opened |
| `0x40fcd7` | `0x40fca0` | `DATA\DATABASE\STF1DAT.%3d` | `[0x455030]≠0` on entry |
| `0x40fdb9` | `0x40fd83` | `DATA\DATABASE\STF1DAT.%3d` | `[0x455030]≠0` on entry |
| `0x442597` | `0x442539` | pool name with the last extension char replaced by `@` (`FILMB.TM@`) | before pool re-create |
| `0x439bcb` | `0x439861` | `<pool>.OLD` | DEAD |
| `0x44acf7` (R) | `remove()` `0x44acf4` | `EAX`; FALSE → errno via `GetLastError` | only via `fclose` of `tmpfile` streams, which are never created. DEAD in practice |

`MoveFileA` is used only in DEAD `0x439861`:
- `0x439be2`: `MoveFileA(pool, pool.OLD)`.
- `0x439bf6`: `MoveFileA(tmp, pool)`.

Minimal host: plain rename; fail if the target exists (Win32 semantics).

### 6.4 Profile (INI) APIs

| Wrapper | Site | Call | Users |
|---|---|---|---|
| `0x409e26` | `0x409e36` | `GetPrivateProfileIntA(sect=EDX, key=EBX, -1, file=EAX)` | only `0x403a3b`: `("VIDEO","VideoPlay",-1,"WET.INI")`. Result `cwde` (low 16 bits sign-extended) → `[0x4550d4]` (video on/off) |
| `0x409e3f` | `0x409e5c` | `GetPrivateProfileStringA(sect, key, "ini_no_string", 0x45dc50, 0x104, file)` | **no callers**: DEAD |
| `0x409e6a` | `0x409ea6` | `WritePrivateProfileStringA(sect=EDX, key=EBX, itoa(CX,10)+"", file=EAX)` | `0x403a6d` (START, if the key was missing → writes `1`) and `0x403cb8` (EXIT, writes the current `[0x4550d4]` low word) |
| `0x409eb7` | `0x409eea` | `WritePrivateProfileStringA(sect=EDX, key=EBX, strcpy(ECX)+"", file=EAX)` | 59 sites; always `("STARTUP_DEBUG", <key>, "OK", 0x45dabc = "<cwd>\W_DEBUG.DAT")` |

The `""` suffixes at `0x44dda1`/`0x44dda3` are empty strings (VERIFIED).

Trace keys written to `W_DEBUG.DAT` (diagnostic only, never read back):
- `WIN_MAINCheck1..10`, including `3.1` and `7.1`
- `INIT_ALLCheck1..11`, including `9.1`
- `INIT_VARSCheck1..4`
- `INIT_GAMECheck1..3`
- `LOAD_ALLCheck1..8`
- `INIT_CD_PATHCheck1..4`
- `INTROCheck1..3`
- `INIT_ONCE_VARSCheck1..2`
- `INIT_GAME_VARSCheck1..10`
- `INIT_STUFECheck1..3`

Host semantics:
- Real INI read/write with Win32 rules: case-insensitive section and key names; create the file and section if missing.
- `"WET.INI"` has no path, so map it to a host config file. Win32 would use `%WINDIR%\WET.INI`; the cwd is an acceptable host policy.
- `W_DEBUG.DAT` may be implemented or turned into a no-op with an optional log, since nothing reads it.
- `GetPrivateProfileIntA` must return the default `-1` for a missing key.

`VideoPlay` semantics (VERIFIED):
- `[0x4550d4]` starts at 1.
- An INI value overrides it.
- `-novideo` sets it to 0.
- **The value is persisted at exit**, so `-novideo` becomes sticky through `WET.INI`.

### 6.5 Command line

| Site | Consumer | Behaviour |
|---|---|---|
| `0x442ef3` (R) | `__NTMainInit` | `strdup` → skip program name → `[0x452494]` (no reader) |
| `0x447831` (R) | WinMain driver | `strdup` → skip program name → `lpCmdLine` argument (WinMain ignores it) |
| `0x409f0f` (G) | `0x409efa` (called from WinMain `0x403a74`) | `strcpy` into the **fixed 260-byte buffer `0x45d968`**, without a length check; split on `' '` only (no quote handling). Tokens after the first space go to `argv[]` at `0x45da6c`, which has room for 20 entries before overwriting the W_DEBUG path at `0x45dabc`. Count → `*(short*)EAX`. **The first token is never recorded** |

Recognised switches (VERIFIED: the only consumer is the loop `0x403a7d..0x403aa3`):

| Switch | Compare | Effect |
|---|---|---|
| `-novideo` (`0x44d96a`) | `stricmp` `0x442b5a` (ASCII case-insensitive), whole token | `[0x4550d4]=0`: no video; persisted to `WET.INI` |

There are no other switches. Host: `GetCommandLineA` must return a stable guest pointer, used three times, shorter than 260 bytes and with at most 20 extra tokens, for example `WET.EXE -novideo`. A program path containing spaces only adds harmless extra tokens.

### 6.6 Environment

- `0x442ecc GetEnvironmentStrings()` → `[0x4524c5]`, never freed. There is no `FreeEnvironmentStrings` import.
- XI `0x44aa68` copies the double-NUL block into `malloc` memory and builds `environ` `[0x45257c]` plus the flag bytes `[0x452580]`. An empty block (first byte 0) is allowed.
- `getenv` `0x443135` (ASCII case-**insensitive** `strnicmp` `0x443fe8` on the name, then `'='`) has 6 callers:

| Caller | Variable | Effect |
|---|---|---|
| `0x434cc5` (`0x434c54`, memory manager init, START) | `NGS-REVEAL` (obfuscated, byte−0x17 at `0x4518e8`) | if set: `printf("MEMORY_MANAGER_(C)_NEW_GENERATION_SOFTWARE\n")` |
| `0x435cec` (`0x435c84`) | `NGS-REVEAL` (−0x43) | `"CURSOR_CLASS_(C)_NEW_GENERATION_SOFTWARE"` |
| `0x436469` (`0x436400`) | `NGS-REVEAL` (−0x2d) | `"ERROR_HANDLER_(C)_NEW_GENERATION_SOFTWARE"` |
| `0x43aa50` (`0x43a9c8`) | `NGS-REVEAL` (−0x0e) | `"GRAPHICS_DRIVER_(C)_NEW_GENERATION_SOFTWARE"` |
| `0x43e604` (`0x43e59c`) | `NGS-REVEAL` (−0x33) | `"FILE_MANAGER_(C)_NEW_GENERATION_SOFTWARE"` |
| `0x449684` (`tzset` `0x44967f`) | `TZ` (`0x450434`) | if unset → `GetTimeZoneInformation` (§6.9) |

Host:
- Return a valid double-NUL block in guest memory; a minimal `"\0\0"` is fine.
- If `TZ` is set, Watcom parses it and `GetTimeZoneInformation` is skipped.
- `NGS-REVEAL` only produces stdout text (§6.12).

### 6.7 Module and directory APIs

| Site | Call | Use |
|---|---|---|
| `0x442fc6` | `GetModuleHandleA(NULL)` | `EBX`, used only by the DEAD DLL branch |
| `0x44789d` | `GetModuleHandleA(NULL)` | WinMain `hInstance` → **must be `0x400000`** |
| `0x442ee2` | `GetModuleFileNameA(NULL, buf, 0x104)` | `_LpPgmName`, unused |
| `0x442f70` | `GetModuleFileNameA(hMod, buf, 0x104)` | DEAD |
| `0x4480ef` | `GetModuleFileNameA(NULL, caption+0x13, 0x104)` | SEH-filter message-box caption `"Application Error: <path>"` |
| `0x4039c9` | `GetCurrentDirectoryA(0xF9, 0x45dabc)` | `0x104 − strlen("W_DEBUG.DAT")`. Return value ignored; the buffer is then `strcat`'d with `"\"` and `"W_DEBUG.DAT"` |

Host:
- Return a **short Windows-style** current directory (for example `C:\LULA`) that the path mapper translates back.
- If the real cwd is longer than 248 characters, Win32 would leave the buffer untouched (still zero), which gives `"\W_DEBUG.DAT"`.
- `GetModuleFileNameA` may return `C:\LULA\WET.EXE`.

### 6.8 Memory: `VirtualAlloc`, `VirtualFree` and the Watcom heap; `GlobalMemoryStatus`

The Watcom near heap is the only allocator. The game's "MEMORY_MANAGER" `0x434d10`/`0x434d6b` wraps `malloc`/`free` and counts against a **constant** budget `[0x488360]=0x7a1200` (8,000,000 bytes, set at `0x434ce6`). There are no `HeapAlloc`, `GlobalAlloc` or `LocalAlloc` imports. VERIFIED.

**Growth (`0x4489db`, called by `malloc` `0x443021` when the free lists cannot satisfy a request):**
1. **Shrink first.** `0x44a9a3` walks the segment list `[0x452138]`. Every segment whose single free block spans the whole segment is unlinked (`0x44aa15`) and released with `0x44a9c9 VirtualFree(seg, 0, MEM_RELEASE=0x8000)`. If that returns 0, the function returns −1 and the segment stays. Rovers `[0x45213c]`, `[0x452140]` and `[0x48a6b8]` are fixed up.
2. **Grow.** `0x44894b` fails unless `[0x452700]≠0` (initially 1) and `[0x452490]≠−2` (initially 0).
   - Size: `s = (req+0xb)&~7; s += 0x3c; s = max(s, _amblksiz&~1); s = roundup(s, 0x1000)`.
   - `_amblksiz` is `0x10` until step 5 of §5 and `0x8000` afterwards. The first segment, from the handle table in §5 step 3b, is therefore `0x1000` bytes; later ones are at least `0x8000`.
   - Call: `0x448983 VirtualAlloc(NULL, size, MEM_COMMIT=0x1000, PAGE_EXECUTE_READWRITE=0x40)`.
   - NULL → `malloc` returns NULL. This is non-fatal, except for `__InitFiles`.
   - Otherwise the segment header goes at the base (`[base]=size-4`, …) and is linked in with `0x4488d7`.

Host:
- Allocate from a guest arena. Use 4 KiB alignment; 64 KiB is recommended to mimic Windows.
- Addresses must be **below `0x80000000`** (INFERRED safety: Watcom pointer compares are unsigned, but game code is unchecked) and must not overlap the image (`0x400000..0x4a6000`), the stack, the TIB or host-synthesised thunks.
- Return zero-filled pages.
- `VirtualFree(MEM_RELEASE)` with `dwSize=0` must accept exactly the base returned by `VirtualAlloc`; return non-zero.
- Commit-only allocation (no `MEM_RESERVE` flag) must work, as on Windows.

`GlobalMemoryStatus` (`0x434da9` in `0x434d8e`):
- Fills a `MEMORYSTATUS` (`dwLength=0x20`).
- Copies `dwTotalPhys..dwAvailVirtual` (6 dwords) to `*EDX` if `EDX≠0`, and returns `dwAvailPhys`.
- **DEAD**: `0x434d8e` has no caller or reference. Minimal host: fill plausible values, for example 64 MiB physical and 2 GiB virtual.

### 6.9 Time

| Site | Call | Consumer |
|---|---|---|
| `0x4273b1`, `0x4269e3` | `GetTickCount` | scene event handler `0x4267c8` (registered as a callback at `0x426028`). On init: `[0x4847dc]=now`. On event 0xF: `while ((now-[0x4847dc]) >= 100) { [0x4847dc]+=100; step(); }`. Unsigned arithmetic (`jb`) |
| `0x4336f3`, `0x4335ea` | `GetTickCount` | scene handler `0x43353d` (registered at `0x4333b3`), same 100 ms fixed step with base `[0x485bcc]` |
| `0x44671d` | `GetLocalTime(&st)` | `0x446712` → `struct tm` (year−1900, month−1, …, `isdst=-1`); returns `wMilliseconds`. `time()` `0x442a13` rounds the second up if ms ≥ 500, then `mktime` `0x446777`. **Only caller:** `0x402796` → `srand(time(NULL))` at `0x40279b` in new-game init `0x40271e`. `rand` `0x442a4c` is an LCG (`*0x41c64e6d + 0x3039`, seed at `td+0xc`) |
| `0x4495b9` | `GetTimeZoneInformation(&tzi)` (0xac bytes, stack) | Watcom `tzset`, once per process (flag bit 1 of `[0x45262a]`), only if `TZ` is unset. Return 1 (standard): `daylight=0`. Return 2 (daylight): `daylight=1`, `[0x4525dc]=-DaylightBias*60`. Then `timezone [0x4525d8]=(Bias+StandardBias)*60`, and the names are narrowed from `StandardName`/`DaylightName` to `0x4525e4`/`0x452603`. **Any other value (0 = UNKNOWN or −1) leaves the defaults:** `timezone=0x4650` (5 h), `"EST"`/`"EDT"`, `dst=0xe10` |

Host:
- `GetTickCount` = milliseconds since start, 32-bit and wrapping. Game pacing depends on it.
- `GetLocalTime` is real local time, or fixed for deterministic tests (it seeds `rand`).
- Returning `TIME_ZONE_ID_UNKNOWN` (0) is acceptable: it only shifts the `srand` seed.

### 6.10 `LoadLibraryA` / `GetProcAddress`

| Site | Call | Notes |
|---|---|---|
| `0x434138` | `LoadLibraryA("KERNEL32.DLL")` (`0x44cf86`) | In `0x4340b3` (DirectDraw init), called from `0x434044` in `0x433f2e` (← init `0x401010`) and from `0x433d3d` in `0x433bfc` (INFERRED: re-init). Runs after `SetCooperativeLevel(hwnd, 0x11)`, and **only if** `IDirectDraw` vtbl+0x2c (GetCaps) returns 0 and `0x434a1c` accepts the pixel format. Otherwise `[0x48804c]` stays 0 (`.bss`) and the patched stubs below would `call 0`. RUN, once per init |
| `0x434145` | `GetProcAddress(hK32, "Sleep")` (`0x44cf80`) → `[0x48804c]` | `[0x48804c]` is called at two patched stubs (below) |
| `0x4480a2` | `LoadLibraryA("user32.dll")` (`0x45039a`) | SEH filter only (§6.13); 0 → fall back to `WriteFile(stderr)` |
| `0x4480b7` | `GetProcAddress(hU32, "MessageBoxExA")` (`0x4503a5`) | 0 → fallback. Otherwise called as `MessageBoxExA(NULL, msg, "Application Error: "+exe, 0x2010, 0)` |

Patched `Sleep` stubs:
- `0x434156`: `push 0; call [0x48804c]; ret`, i.e. `Sleep(0)`. Called from `0x43a19d` (video loop `0x43a113`).
- `0x434163`: `pushad; push 0x20; call [0x48804c]; popad; call 0x43adaf; jmp 0x43d77b`, i.e. `Sleep(32)`. The `jmp 0x434163` at `0x43d776` in `0x43d646` (INFERRED: input/frame loop) is the hook.

**This code is a binary patch** (VERIFIED):
- The strings sit in BEGTEXT padding after the thunk table, at `0x44cf80`.
- The new absolute operands at `0x434134`, `0x43414b`, `0x43415c` and `0x434167` have **no** base relocations.
- Stale relocations remain at `0x434139`, `0x434145`, `0x43414f`, `0x434161`, `0x434169`, `0x43416e` and `0x434175`, pointing into the middle of the patched instructions.
- `0x4340de` also has `test eax,eax` followed by 6 NOPs: the DirectDrawCreate failure branch was removed.

Host:
- `LoadLibraryA` returns pseudo-handles. It must accept at least `KERNEL32.DLL` and should accept case-insensitive names.
- `GetProcAddress(k32, "Sleep")` must return a **guest-callable address** that the indirect-call dispatcher maps to a host `Sleep(ms)`: stdcall, `ret 4`, really sleeping, which matters for pacing.
- `user32.dll`/`MessageBoxExA` may return NULL.

### 6.11 Threads, synchronisation, TLS (Watcom MT runtime): all DEAD

`_beginthread` is `0x44b8d7`, which is `call [0x45273c]` → `0x44c15f`. It has no caller or reference (VERIFIED). The swap routine `0x44b8ec` is also unreferenced. Consequences:

| Import | Sites | Role (INFERRED Watcom names) |
|---|---|---|
| CreateThread | `0x44c1fa` | `_beginthread`: `CreateThread(NULL, roundup4K(stk), 0x44c0b3, &startblk, 0, &tid)` |
| CreateEventA / WaitForSingleObject / CloseHandle / SetEvent | `0x44c1d5` (`NULL,FALSE,FALSE,"__bgnthd<tid hex>"`), `0x44c218` (INFINITE), `0x44c222`; `0x44c13c` in the thread entry `0x44c0b3` | the creator waits until the child has copied its start block; the child signals |
| GetCurrentThread | `0x44c19a` | stored in the start block |
| GetCurrentThreadId | `0x44c1be` (event name), `0x4482d1` (lock owner) | `0x44839f` is the live START call |
| CreateMutexA / WaitForSingleObject / ReleaseMutex | `0x4482f9`, `0x44857e` (`NULL,FALSE,NULL`); `0x448316` (INFINITE); `0x448347` | recursive lock `{h, created, owner_tid, count}` (`0x4482ca` acquire, `0x44832c` release); locks at `0x48a700..0x48a850`. Hooks installed only by `0x4484c0` |
| TlsAlloc | `0x4483b2`, `0x4483de` | index → `[0x4520ec]` (initially −1). Retried while index ≤ 2 if bit 15 of `[0x4524cd]` is set (Win32s quirk) |
| TlsGetValue | `0x44835a` (MT `__GetThreadPtr` `0x448351`), `0x44845b`, `0x44a871` | |
| TlsSetValue | `0x44843b`, `0x44847e`, `0x4485c3` | |
| TlsFree | `0x4484ae` | reachable from fini `0x4485d9`, but guarded by `[0x4520ec]≠−1`, so it never runs |
| CloseHandle | `0x448225` (lock), `0x44848c` (thread), `0x44c102` | |
| ExitThread | `0x44c255` (`_endthread` `0x44c233`, only from thread entry `0x44c154`) | |

Host: these can be stubs that log and abort. `TlsAlloc` may return `TLS_OUT_OF_INDEXES`. No callback into guest code (thread procs) ever originates from KERNEL32.

### 6.12 Std handles, Watcom stdio, console

- **Std handles.** `0x447ab7` runs at START: `GetStdHandle(-10,-11,-12)` (`0x447ac9`, `0x447ad5`, `0x447ae1`) → fds 0, 1, 2. NULL or invalid values are stored unchanged.
- **`SetStdHandle`** `0x447a1c` sits in `0x4479ef`, which has no caller: DEAD.
- **stdout use.** Only `printf` `0x443196` writes, and only when `NGS-REVEAL` is set (§6.6) or from the console filename prompt below. The first write to a stream:
  - calls `0x448dad` → `0x44ac1b` → `0x44b6ca GetFileType(h)`; FILE_TYPE_CHAR (2) means a tty;
  - allocates a 0x1000-byte buffer.

  Flush is `0x44ab20` write:
  - for append-mode fds (none exist), `SetFilePointer(h,0,NULL,FILE_END)` at `0x44ab4d` first;
  - then `0x44ab9b WriteFile(h, buf, len, &n, NULL)`;
  - FALSE → `__set_errno_nt` → `0x44ace7 GetLastError`.
- **At EXIT, for each of stdin, stdout and stderr** (fini `0x442901`, `fclose` without closing the fd):
  - `0x44ac6a SetFilePointer(h,0,NULL,FILE_CURRENT)`. On −1, `0x44ace7 GetLastError`.
  - Otherwise `0x448f10 SetFilePointer(h,pos,NULL,FILE_BEGIN)`.
  - `CloseHandle` `0x448f74` runs only for non-predefined streams, which never exist.

  Host: SetFilePointer on a std pseudo-handle should fail with `ERROR_INVALID_HANDLE` (6), or behave like a pipe. Both work.
- **Fatal messages.** `0x442e25` does `WriteFile([fd2], msg, strlen, &n, NULL)` at `0x442e50`, then `__exit(code)`. Used by:
  - `__CHK` "Stack Overflow!\n" (`0x433757`, code 1);
  - `__InitFiles` OOM;
  - `0x446978` (`__fatal_runtime_error`: "pure virtual function called!", "undefined constructor or destructor called!", C++ EH errors).

  Host: route std handles to host stderr so these messages are visible.
- **Console input.** `CreateFileA("conin$")` at `0x448c6e` and `CreateFileA("conout$")` at `0x448c98` run lazily in `0x448c44`. The results are cached in `[0x452584]`/`[0x452588]`, initially −1. In a GUI process both fail (−1).
  - `getch` `0x443361`:
    - `0x4433a5` `GetConsoleMode(conin,&m)`;
    - `0x4433ad` `SetConsoleMode(conin,0)`;
    - loop `0x4432d4 ReadConsoleInputA(conin,&rec,1,&n)`; FALSE → return −1;
    - `0x4433c0` `SetConsoleMode(conin,m)`.

    It is reachable only from the developer **console filename prompt** `0x441f49` ("Dateiname: "). Graphics loaders call that prompt when given an empty filename: live call sites `0x43f614`, `0x43f745`, `0x44190e` (guard verified at `0x441903`) and `0x441a69`; the call sites `0x440e50` and `0x4412d6` are in DEAD writers. `getch` is also called from DEAD `0x435862` and `0x43b680`. With −1 from `getch`, the prompt fills its buffer with `0xFF` and ends at the length limit (INFERRED).
  - `kbhit` `0x44608c` (`0x4460c7 PeekConsoleInputA`, `0x4460eb ReadConsoleInputA`) and `putch` `0x44610e` (`0x446157 WriteConsoleA`) are called only from `0x43b680`, which has no reference: DEAD.

    Note: `kbhit` reads `n` even when `PeekConsoleInputA` fails, so the host must write `*lpNumberOfEventsRead=0` on failure.
- **Console control.** `SetConsoleCtrlHandler(0x447b26, TRUE/FALSE)` at `0x447bd0`/`0x447bfc`. Install happens only through `signal()` `0x447ca6`, which is unreferenced. Removal is guarded by flag `[0x45256c]` (never set). Both DEAD.

Host minimal semantics:
- `CreateFileA("conin$"/"conout$")` → `INVALID_HANDLE_VALUE`.
- Console APIs return FALSE with `ERROR_INVALID_HANDLE`, and write 0 to count outputs.
- `GetFileType` → `FILE_TYPE_CHAR` for std handles routed to a terminal; otherwise `FILE_TYPE_UNKNOWN`.

### 6.13 SEH: `fs:[0]`, exception filter, `RtlUnwind`

- **Install** `0x448189` (START):
  - the record `{prev, handler}` lives at `[ebp-8]` of the WinMain driver frame;
  - `prev = fs:[0]`, `handler = 0x447dd6`, `fs:[0] = &rec`, `td+0x54 = &rec`.
- **Uninstall** `0x4481c7` (EXIT, from `__exit` `0x443004` and `_endthread`): `fs:[0] = rec.prev`, `td+0x54 = 0`.
- **This is the only SEH record the program ever creates.** Watcom C++ EH does not use `fs:[0]`.
- **Handler `0x447dd6`**: `__stdcall(EXCEPTION_RECORD*, frame, CONTEXT*, dispctx)`, `ret 0x10`.
  - Unwinding flags (`ExceptionFlags & 6`) → return 1 (ContinueSearch).
  - x87 exception codes `0xC000008D..0xC0000093` map to SIGFPE subcodes via the jump table `0x447dba`, and `raise` (`0x447c5f`) runs the handler in thread data. A user SIGFPE handler (none is ever installed, because `signal()` is unreferenced) would lead to ContinueExecution (0) after clearing the FPU status word at `CONTEXT+0x20`. With the defaults the filter returns ContinueSearch (1).
  - Other codes with a matching signal entry → `raise`.
  - Otherwise `wsprintfA` a message:
    - `0xC0000005`: "The instruction at %08lx referenced memory at %08lx…";
    - `0xC0000096`: privileged;
    - `0xC000001D`: illegal;
    - `0xC0000094`: divide by zero;
    - `0xC00000FD`: stack overflow;
    - otherwise "problem at address".
  - It then shows a `MessageBoxExA` if a window is active or `conout$` is unavailable, else writes to stderr, and returns 1.
- **Host:** there are no hardware exceptions in the recompiled code, so the host need not dispatch through `fs:[0]`. Keep `fs:[0]` as plain memory. On guest faults detected by the lifter (divide by zero, invalid memory), print an equivalent diagnostic and terminate.
- **`RtlUnwind`** `0x44b39c` in `0x44b381`. It is reached only from the C++ catch dispatch `0x449ec4` ← `0x44a384` ← `0x44a435` ← throw entry points `0x44a47c`, `0x44a48c` and `0x44a496`, none of which has an outside caller: DEAD.
  - Args: `(TargetFrame = jmpbuf+0x2c saved fs:[0], TargetIp = 0x44b3a1, NULL, 0)`.
  - Minimal host: call each record's handler from the `fs:[0]` head down to TargetFrame with `EXCEPTION_UNWINDING`; the only handler returns at once. Then set `fs:[0]=TargetFrame`, pop 16 bytes and return.

### 6.14 `GetLastError`

| Site | Consumer |
|---|---|
| `0x43e014` (G) | DirectDraw HRESULT-to-text `0x43de84` (from `0x433b8b`): the unknown-code fallback, `wsprintfA(buf,"Unknown Errorcode!!: %x\n", GetLastError())`. Diagnostic only |
| `0x44ace7` (R) | `__set_errno_nt` `0x44ace5`, after a failed runtime `WriteFile`, `SetFilePointer`, `CloseHandle` or `DeleteFileA`. Maps `0x7b`→1, `0xce`→9, `0xb7`→7, values above `0x13` → `0x13` via table `0x452705`; then stores the NT code at `td+8` and errno at `td+4` |

Host: keep a per-thread last-error value, set on every failing call. Typical values: 2, 3, 5, 6, 0x50, 0x57.

### 6.15 Exit paths (`ExitProcess`)

| Path | Sequence |
|---|---|
| Normal | WinMain returns → `0x4478a9 exit(eax)`: `0x4429d6` → hooks `[0x4520d0]`, `[0x4520d4]`, `[0x4520d8]` (all `ret` `0x4429d5`), `[0x452488]` (0) → `__exit` `0x443002`: `0x4481c7` (SEH unlink; console-handler check, no call) → `__FiniRtns(0,0xff)` (§5.1) → `[0x45212c]` (`ret`) → **`0x44301b push esi` / `0x44301c ExitProcess(ESI)`**. The status in `EBX` is discarded; INFERRED library defect |
| CD not found | `0x40227e exit(-1)` → same |
| Fatal runtime | `0x442e25` → `WriteFile(stderr)` → `jmp 0x443002`. Also `0x44bf04` (C++ EH, `__exit(1)`) |
| Thread-data allocation failure | `0x442ec2 ExitProcess(1)`. Not reachable for the EXE |

Host: `ExitProcess` must not return. Flush host state (INI, SDL), then exit. Ignore the guest code or log it; use 0.

### 6.16 Miscellaneous

| Import | Site | Semantics |
|---|---|---|
| GetVersion | `0x442f81` | stored only; read only by the DEAD `TlsAlloc` path. Any value works, for example `0x0A280105` (NT 5.1) or a Win95 value `0xC3B60004` |
| GetCurrentProcessId | `0x448e9d` (`0x448e9b` = `getpid`) | used only to build `"tPPPP_NN.tmp"` when closing a `tmpfile()` stream. No code sets the `_TMPFIL` flag, so DEAD in practice |
| MultiByteToWideChar | `0x439fdc` in `0x439f9e` | `(CP_ACP, 0, EDX, -1, wbuf[0x104], 0x104)`; return ignored. The wide name goes to `IGraphBuilder::RenderFile` (vtable `+0x34`): **video, out of scope**. Host: ASCII/Latin-1 → UTF-16, NUL-terminated |

---

## 7. Minimal host semantics per import (checklist)

| Import | Required behaviour | Priority |
|---|---|---|
| CloseHandle | close a host file handle; **return 1** | must |
| CreateFileA | §6.1 flag table; path mapping §6.2; `conin$`/`conout$` → −1; non-zero handles ≠ −1; set last error | must |
| DeleteFileA | delete the mapped path; TRUE/FALSE | must |
| GetCommandLineA | stable pointer to `"WET.EXE[ args]"`, < 260 bytes | must |
| GetCurrentDirectoryA | short Windows-style cwd that the mapper understands | must |
| GetLastError | per-thread last error | should |
| GetPrivateProfileIntA | INI read; default −1 | must |
| GetPrivateProfileStringA | DEAD; implement for completeness | stub |
| GetTickCount | ms counter, 32-bit wrap | must |
| GlobalMemoryStatus | DEAD; fill plausible values | stub |
| MoveFileA | DEAD; rename | stub |
| MultiByteToWideChar | `CP_ACP` widening (video only) | later |
| ReadFile / WriteFile | exact byte counts; EOF → TRUE with n=0; std handles → host stdio | must |
| SetFilePointer | 32-bit seek, methods 0/1/2; `0xFFFFFFFF` on error | must |
| WritePrivateProfileStringA | INI write (or no-op for `W_DEBUG.DAT`) | must |
| GetModuleHandleA | NULL → `0x400000` | must |
| GetModuleFileNameA | `"C:\…\WET.EXE"`; return length | must |
| GetEnvironmentStrings | double-NUL block in guest memory | must |
| GetVersion | constant | must (trivial) |
| GetStdHandle | pseudo-handles for host stdio, or NULL | must |
| GetFileType | `FILE_TYPE_CHAR`/`UNKNOWN` | should |
| GetCurrentThreadId | constant non-zero | must (trivial) |
| VirtualAlloc / VirtualFree | §6.8 | must |
| GetLocalTime / GetTimeZoneInformation | §6.9 | must |
| LoadLibraryA / GetProcAddress | §6.10; `Sleep` thunk is mandatory | must |
| ExitProcess | terminate | must |
| GetConsoleMode, SetConsoleMode, ReadConsoleInputA | fail (FALSE); zero outputs | should |
| PeekConsoleInputA, WriteConsoleA, SetConsoleCtrlHandler, SetStdHandle | DEAD; fail | stub |
| CreateThread, CreateEventA, SetEvent, CreateMutexA, ReleaseMutex, WaitForSingleObject, GetCurrentThread, ExitThread, TlsAlloc/Free/GetValue/SetValue, RtlUnwind | DEAD; log and abort (or §6.13 minimal RtlUnwind) | stub |
| GetCurrentProcessId | constant | stub |

---

## 8. Complete call-site table (104 sites)

Columns:
- **Blk**: G = game IAT block, R = Watcom runtime block.
- **Via**: `[IAT]` = `call cs:[slot]`; `thunk` = `E8` to a `jmp [slot]` thunk.
- **Fn**: corrected containing-function start.

| Import | Blk | IAT slot | Site | Fn | Via | Arguments | Result use | Reach |
|---|---|---|---|---|---|---|---|---|
| CloseHandle | G | `0x48b238` | `0x442660` | `0x442654` | [IAT] | EDX=h | 1→0 else -1 (cmp eax,1) | RUN |
| CloseHandle | R | `0x48b4c8` | `0x448225` | `0x448217` | thunk `0x44ced6` | lock.h | ignored | DEAD (MT) |
| CloseHandle | R | `0x48b4c8` | `0x44848c` | `0x448449` | thunk `0x44ced6` | td+0xde thread handle | ignored | DEAD (MT) |
| CloseHandle | R | `0x48b4c8` | `0x448f74` | `0x448f30` | thunk `0x44ced6` | fd table[fd] | 0 ⇒ errno 4, ret -1 | DEAD in practice (only non-std streams) |
| CloseHandle | R | `0x48b4c8` | `0x44c102` | `0x44c0b3` | thunk `0x44ced6` | thread handle | ignored | DEAD (MT) |
| CloseHandle | R | `0x48b4c8` | `0x44c222` | `0x44c15f` | thunk `0x44ced6` | event | ignored | DEAD (MT) |
| CreateEventA | R | `0x48b4cc` | `0x44c1d5` | `0x44c15f` | thunk `0x44ce58` | `NULL,FALSE,FALSE,"__bgnthd<tid hex>"` | stored | DEAD (MT) |
| CreateFileA | G | `0x48b23c` | `0x442639` | `0x4425ac` | [IAT] | EDX=path; access/share/creation from mode (see §6.1); attrs 0x10000080; tmpl 0 | -1→ret 0 + [0x455030]=8 | RUN |
| CreateFileA | R | `0x48b4d0` | `0x448c6e` | `0x448c44` | thunk `0x44ce9a` | `"conin$",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0x80,NULL` | →[0x452584] (cached, -1 = none) | COND (getch / console) |
| CreateFileA | R | `0x48b4d0` | `0x448c98` | `0x448c44` | thunk `0x44ce9a` | `"conout$",GENERIC_WRITE,FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0x80,NULL` | →[0x452588] | COND (exception filter / putch) |
| CreateMutexA | R | `0x48b4d4` | `0x4482f9` | `0x4482ca` | thunk `0x44ceca` | NULL,FALSE,NULL | →lock.h | DEAD (MT) |
| CreateMutexA | R | `0x48b4d4` | `0x44857e` | `0x4484c0` | thunk `0x44ceca` | NULL,FALSE,NULL | →[0x48a830] | DEAD (MT) |
| CreateThread | R | `0x48b4d8` | `0x44c1fa` | `0x44c15f` | thunk `0x44ce52` | NULL, stk(round 4K), 0x44c0b3, &startblk, 0, &tid | 0 ⇒ tid=-1 | DEAD (MT) |
| DeleteFileA | G | `0x48b240` | `0x4020ad` | `0x401ab6` | [IAT] | `"DATA\DATABASE\FILMB.TMP" (0x4550d8)` | ignored | START (only if FILMB.TMP opened OK) |
| DeleteFileA | G | `0x48b240` | `0x402973` | `0x4028d4` | [IAT] | `"DATA\DATABASE\FILMB.TMP" (0x4550d8)` | ignored | RUN (new-game init) |
| DeleteFileA | G | `0x48b240` | `0x4039f3` | `0x40399d` | [IAT] | `"<cwd>\W_DEBUG.DAT" (0x45dabc)` | ignored | START |
| DeleteFileA | G | `0x48b240` | `0x40d7e6` | `0x40d611` | [IAT] | `"DATA\DATABASE\FILMB.%3d" (stack)` | ignored | COND (save: copy failed) |
| DeleteFileA | G | `0x48b240` | `0x40dbd5` | `0x40d9e3` | [IAT] | `"DATA\DATABASE\FILMB.TMP" (0x4550d8)` | ignored | COND (load: slot DB missing) |
| DeleteFileA | G | `0x48b240` | `0x40fcd7` | `0x40fca0` | [IAT] | `"DATA\DATABASE\STF1DAT.%3d" (stack)` | ignored | COND (save with error pending) |
| DeleteFileA | G | `0x48b240` | `0x40fdb9` | `0x40fd83` | [IAT] | `"DATA\DATABASE\STF1DAT.%3d" (stack)` | ignored | COND (load with error pending) |
| DeleteFileA | G | `0x48b240` | `0x439bcb` | `0x439861` | [IAT] | `"<pool>.OLD" (stack)` | ignored | DEAD |
| DeleteFileA | G | `0x48b240` | `0x442597` | `0x442539` | [IAT] | `"<pool path, last ext char→'@'>" e.g. FILMB.TM@ (obj+0x105)` | ignored | COND (FILMB.TMP re-create) |
| DeleteFileA | R | `0x48b4dc` | `0x44acf7` | `0x44acf4` | thunk `0x44ce76` | EAX=path (remove()) | 0 ⇒ errno from GetLastError | DEAD in practice (tmpfile only) |
| ExitProcess | R | `0x48b4e0` | `0x442ec2` | `0x442e95` | thunk `0x44cf48` | 1 | - | DEAD in practice (td==NULL) |
| ExitProcess | R | `0x48b4e0` | `0x44301c` | `0x443002` | thunk `0x44cf48` | ESI (garbage, not the exit status!) | - | EXIT (normal + fatal) |
| ExitThread | R | `0x48b4e4` | `0x44c255` | `0x44c233` | thunk `0x44ce4c` | 0 | - | DEAD (MT) |
| GetCommandLineA | G | `0x48b244` | `0x409f0f` | `0x409efa` | [IAT] | - | strcpy to 0x45d968 (260 B), tokenised | START |
| GetCommandLineA | R | `0x48b4e8` | `0x442ef3` | `0x442e95` | thunk `0x44cf36` | - | strdup → skip argv0 → [0x452494] (unused) | START |
| GetCommandLineA | R | `0x48b4e8` | `0x447831` | `0x4477c4` | thunk `0x44cf36` | - | strdup → skip argv0 → WinMain lpCmdLine (unused by WinMain) | START |
| GetConsoleMode | R | `0x48b4ec` | `0x4433a5` | `0x443361` | thunk `0x44cf1e` | conin, &mode | ignored | COND (getch) |
| GetCurrentDirectoryA | G | `0x48b248` | `0x4039c9` | `0x40399d` | [IAT] | nBufferLength=0xF9, lpBuffer=0x45dabc | ignored (buffer used) | START |
| GetCurrentProcessId | R | `0x48b4f0` | `0x448e9d` | `0x448e9b` | thunk `0x44ce94` | - | tmp name tPPPP_NN.tmp | DEAD in practice |
| GetCurrentThread | R | `0x48b4f8` | `0x44c19a` | `0x44c15f` | thunk `0x44ce5e` | - | start block | DEAD (MT) |
| GetCurrentThreadId | R | `0x48b4f4` | `0x4482d1` | `0x4482ca` | thunk `0x44ced0` | - | owner compare | DEAD (MT) |
| GetCurrentThreadId | R | `0x48b4f4` | `0x44839f` | `0x448371` | thunk `0x44ced0` | - | td+0xda | START |
| GetCurrentThreadId | R | `0x48b4f4` | `0x44c1be` | `0x44c15f` | thunk `0x44ced0` | - | event name | DEAD (MT) |
| GetEnvironmentStrings | R | `0x48b4fc` | `0x442ecc` | `0x442e95` | thunk `0x44cf42` | - | →[0x4524c5]; parsed by XI 0x44aa68 | START |
| GetFileType | R | `0x48b500` | `0x44b6ca` | `0x44b6b2` | thunk `0x44ce6a` | fd table[fd] | ==2 (CHAR) ⇒ isatty | COND (first stdout/stderr write) |
| GetLastError | G | `0x48b24c` | `0x43e014` | `0x43de84` | [IAT] | - | `wsprintfA "Unknown Errorcode!!: %x\n"` | COND (DirectDraw error text) |
| GetLastError | R | `0x48b504` | `0x44ace7` | `0x44ace5` | thunk `0x44ce7c` | - | NT error → errno (td+4/td+8) | COND (after failing R-block I/O; ≥3× at exit if std handles invalid) |
| GetLocalTime | R | `0x48b508` | `0x44671d` | `0x446712` | thunk `0x44cf06` | &SYSTEMTIME (stack) | → struct tm; ms≥500 rounds up | RUN (time() for srand, once per new game) |
| GetModuleFileNameA | R | `0x48b50c` | `0x442ee2` | `0x442e95` | thunk `0x44cf3c` | NULL, buf(stack), 0x104 | strdup → [0x452498] (unused) | START |
| GetModuleFileNameA | R | `0x48b50c` | `0x442f70` | `0x442e95` | thunk `0x44cf3c` | hModule=EBX, buf, 0x104 | →[0x45249c] | DEAD (DLL-only branch) |
| GetModuleFileNameA | R | `0x48b50c` | `0x4480ef` | `0x447dd6` | thunk `0x44cf3c` | NULL, caption buf+0x13, 0x104 | MessageBoxExA caption | EXC (SEH filter) |
| GetModuleHandleA | R | `0x48b510` | `0x442fc6` | `0x442fbc` | thunk `0x44cf2a` | NULL | EBX (only for dead 0x442f70) | START |
| GetModuleHandleA | R | `0x48b510` | `0x44789d` | `0x4477c4` | thunk `0x44cf2a` | NULL | WinMain hInstance | START |
| GetPrivateProfileIntA | G | `0x48b250` | `0x409e36` | `0x409e26` | [IAT] | `sect=EDX key=EBX def=-1 file=EAX ("VIDEO","VideoPlay",-1,"WET.INI")` | cwde → [0x4550d4]; -1 ⇒ default 1 | START |
| GetPrivateProfileStringA | G | `0x48b254` | `0x409e5c` | `0x409e3f` | [IAT] | `sect=EDX key=EBX def="ini_no_string" buf=0x45dc50 size=0x104 file=EAX` | returns 0x45dc50 | DEAD |
| GetProcAddress | R | `0x48b514` | `0x434145` | `0x4340b3` | thunk `0x44cedc` | `h, "Sleep" (0x44cf80)` | →[0x48804c], called as Sleep(0)/Sleep(32) | RUN (DirectDraw init 0x4340b3) |
| GetProcAddress | R | `0x48b514` | `0x4480b7` | `0x447dd6` | thunk `0x44cedc` | `h, "MessageBoxExA" (0x4503a5)` | 0 ⇒ fallback WriteFile(stderr) | EXC (SEH filter) |
| GetStdHandle | R | `0x48b518` | `0x447ac9` | `0x447ab7` | thunk `0x44cefa` | STD_INPUT_HANDLE (-10) | → fd 0 | START |
| GetStdHandle | R | `0x48b518` | `0x447ad5` | `0x447ab7` | thunk `0x44cefa` | STD_OUTPUT_HANDLE (-11) | → fd 1 | START |
| GetStdHandle | R | `0x48b518` | `0x447ae1` | `0x447ab7` | thunk `0x44cefa` | STD_ERROR_HANDLE (-12) | → fd 2 | START |
| GetTickCount | G | `0x48b258` | `0x4269e3` | `0x4267c8` | [IAT] | - | now-[0x4847dc]; ≥100 ⇒ +=100, step | RUN (scene handler) |
| GetTickCount | G | `0x48b258` | `0x4273b1` | `0x4267c8` | [IAT] | - | →[0x4847dc] (timer base) | RUN |
| GetTickCount | G | `0x48b258` | `0x4335ea` | `0x43353d` | [IAT] | - | now-[0x485bcc]; ≥100 ⇒ +=100, step | RUN (scene handler) |
| GetTickCount | G | `0x48b258` | `0x4336f3` | `0x43353d` | [IAT] | - | →[0x485bcc] (timer base) | RUN |
| GetTimeZoneInformation | R | `0x48b51c` | `0x4495b9` | `0x449500` | thunk `0x44ce88` | &TIME_ZONE_INFORMATION (0xac, stack) | 1/2 used; 0/other ⇒ keep EST/EDT defaults | RUN (once, first mktime when TZ unset) |
| GetVersion | R | `0x48b520` | `0x442f81` | `0x442e95` | thunk `0x44cf30` | - | →[0x4524cb..0x4524ce] (read only by dead TlsAlloc path) | START |
| GlobalMemoryStatus | G | `0x48b25c` | `0x434da9` | `0x434d8e` | [IAT] | &MEMORYSTATUS (dwLength=0x20, stack) | copies 6 dwords; returns dwAvailPhys | DEAD |
| LoadLibraryA | R | `0x48b524` | `0x434138` | `0x4340b3` | thunk `0x44cee2` | `"KERNEL32.DLL" (0x44cf86)` | →GetProcAddress | RUN (DirectDraw init) |
| LoadLibraryA | R | `0x48b524` | `0x4480a2` | `0x447dd6` | thunk `0x44cee2` | `"user32.dll" (0x45039a)` | 0 ⇒ fallback WriteFile(stderr) | EXC (SEH filter) |
| MoveFileA | G | `0x48b260` | `0x439be2` | `0x439861` | [IAT] | `(orig, "<pool>.OLD")` | ignored | DEAD |
| MoveFileA | G | `0x48b260` | `0x439bf6` | `0x439861` | [IAT] | (tmp, orig) | ignored | DEAD |
| MultiByteToWideChar | G | `0x48b264` | `0x439fdc` | `0x439f9e` | [IAT] | CP_ACP,0,EDX(name),-1,wbuf(stack),0x104 | ignored; wbuf→IGraphBuilder::RenderFile | VIDEO (out of scope) |
| PeekConsoleInputA | R | `0x48b528` | `0x4460c7` | `0x44608c` | thunk `0x44cf12` | conin, rec, 1, &n | n checked even on failure | DEAD (kbhit only from unreferenced 0x43b680) |
| ReadConsoleInputA | R | `0x48b52c` | `0x4432d4` | `0x443256` | thunk `0x44cf24` | conin, rec(0x14), 1, &n | 0 ⇒ getch returns -1 | COND (getch) |
| ReadConsoleInputA | R | `0x48b52c` | `0x4460eb` | `0x44608c` | thunk `0x44cf24` | conin, rec, 1, &n | loop | DEAD |
| ReadFile | G | `0x48b268` | `0x442710` | `0x4426d1` | [IAT] | h=stack arg, buf=EDX(+=size), size=EBX, &n, NULL; loop ECX times | stop on FALSE or n≠size | RUN |
| ReadFile | G | `0x48b268` | `0x4427a0` | `0x44277d` | [IAT] | h=EDX, &byte, 1, &n, NULL | byte, or -1 if FALSE or n==0 | RUN |
| ReleaseMutex | R | `0x48b530` | `0x448347` | `0x44832c` | thunk `0x44cebe` | lock.h | ignored | DEAD (MT) |
| RtlUnwind | R | `0x48b534` | `0x44b39c` | `0x44b381` | thunk `0x44ce70` | jmpbuf+0x2c frame, 0x44b3a1, NULL, 0 | - | DEAD (C++ throw path unreferenced) |
| SetConsoleCtrlHandler | R | `0x48b538` | `0x447bd0` | `0x447bbe` | thunk `0x44cef4` | 0x447b26, TRUE | flag [0x45256c] | DEAD (signal() unreferenced) |
| SetConsoleCtrlHandler | R | `0x48b538` | `0x447bfc` | `0x447bea` | thunk `0x44cef4` | 0x447b26, FALSE | flag cleared | DEAD in practice (guarded by [0x45256c]==1) |
| SetConsoleMode | R | `0x48b53c` | `0x4433ad` | `0x443361` | thunk `0x44cf18` | conin, 0 | ignored | COND (getch) |
| SetConsoleMode | R | `0x48b53c` | `0x4433c0` | `0x443361` | thunk `0x44cf18` | conin, saved mode | ignored | COND (getch) |
| SetEvent | R | `0x48b540` | `0x44c13c` | `0x44c0b3` | thunk `0x44ce64` | startblk event | ignored | DEAD (MT) |
| SetFilePointer | G | `0x48b26c` | `0x442687` | `0x442675` | [IAT] | h=EDX, 0, NULL, FILE_CURRENT | returned as position | RUN |
| SetFilePointer | G | `0x48b26c` | `0x4426c1` | `0x442690` | [IAT] | h=EDX, dist=EBX, NULL, method=ECX(0/1/2) | -1⇒-1 else 0 | RUN |
| SetFilePointer | R | `0x48b544` | `0x448f10` | `0x448ef6` | thunk `0x44ce8e` | h, dist=ECX, NULL, method=EBX | -1 ⇒ errno | EXIT/COND (stdio seek-back; only if tell succeeded) |
| SetFilePointer | R | `0x48b544` | `0x44ab4d` | `0x44ab20` | thunk `0x44ce8e` | h, 0, NULL, FILE_END | -1 ⇒ errno | DEAD in practice (append-mode fds only) |
| SetFilePointer | R | `0x48b544` | `0x44ac6a` | `0x44ac4c` | thunk `0x44ce8e` | h, 0, NULL, FILE_CURRENT | -1 ⇒ errno | EXIT (3× for stdin/out/err) + COND |
| SetStdHandle | R | `0x48b548` | `0x447a1c` | `0x4479ef` | thunk `0x44cf00` | -10/-11/-12, h | ignored | DEAD (0x4479ef unreferenced) |
| TlsAlloc | R | `0x48b54c` | `0x4483b2` | `0x4483b0` | thunk `0x44ceb2` | - | →[0x4520ec] | DEAD (MT) |
| TlsAlloc | R | `0x48b54c` | `0x4483de` | `0x4483b0` | thunk `0x44ceb2` | - | Win32s retry while idx≤2 | DEAD (MT) |
| TlsFree | R | `0x48b550` | `0x4484ae` | `0x4484a0` | thunk `0x44cea6` | [0x4520ec] | - | EXIT but guarded: never runs while [0x4520ec]==-1 |
| TlsGetValue | R | `0x48b554` | `0x44835a` | `0x44832c` | thunk `0x44ceb8` | [0x4520ec] | td ptr | DEAD (MT) |
| TlsGetValue | R | `0x48b554` | `0x44845b` | `0x448449` | thunk `0x44ceb8` | [0x4520ec] | td ptr | DEAD (MT) |
| TlsGetValue | R | `0x48b554` | `0x44a871` | `0x44a85a` | thunk `0x44ceb8` | [0x4520ec] | td ptr | DEAD (MT) |
| TlsSetValue | R | `0x48b558` | `0x44843b` | `0x4483fc` | thunk `0x44ceac` | [0x4520ec], td | - | DEAD (MT) |
| TlsSetValue | R | `0x48b558` | `0x44847e` | `0x448449` | thunk `0x44ceac` | [0x4520ec], 0 | - | DEAD (MT) |
| TlsSetValue | R | `0x48b558` | `0x4485c3` | `0x4484c0` | thunk `0x44ceac` | [0x4520ec], main td | - | DEAD (MT) |
| VirtualAlloc | R | `0x48b55c` | `0x448983` | `0x44894b` | thunk `0x44cea0` | NULL, size (≥0x1000, 4K-rounded), MEM_COMMIT 0x1000, PAGE_EXECUTE_READWRITE 0x40 | NULL ⇒ malloc fails | START + RUN |
| VirtualFree | R | `0x48b560` | `0x44a9c9` | `0x44a9b7` | thunk `0x44ce82` | segment base, 0, MEM_RELEASE 0x8000 | 0 ⇒ -1 (segment kept) | RUN (heap shrink before each growth) |
| WaitForSingleObject | R | `0x48b564` | `0x448316` | `0x4482ca` | thunk `0x44cec4` | lock.h, INFINITE | - | DEAD (MT) |
| WaitForSingleObject | R | `0x48b564` | `0x44c218` | `0x44c15f` | thunk `0x44cec4` | event, INFINITE | - | DEAD (MT) |
| WriteConsoleA | R | `0x48b568` | `0x446157` | `0x44610e` | thunk `0x44cf0c` | conout, &ch, 1, &n, EDX | ignored | DEAD |
| WriteFile | G | `0x48b270` | `0x44276b` | `0x44272c` | [IAT] | h=stack arg, buf=EDX, size=EBX, &n, NULL; loop ECX times | stop on FALSE or n≠size | RUN |
| WriteFile | G | `0x48b270` | `0x4427f1` | `0x4427c5` | [IAT] | h=EBX, &byte(EDX), 1, &n, NULL | EDX, or -1 | RUN |
| WriteFile | R | `0x48b56c` | `0x442e50` | `0x442e25` | thunk `0x44cf4e` | fd2 handle, msg, strlen, &n, NULL | ignored → `__exit` | FATAL (stack overflow, no memory, …) |
| WriteFile | R | `0x48b56c` | `0x448171` | `0x447dd6` | thunk `0x44cf4e` | fd2 handle, msg, strlen, &n, NULL | - | EXC (SEH filter fallback) |
| WriteFile | R | `0x48b56c` | `0x44ab9b` | `0x44ab20` | thunk `0x44cf4e` | fd table[fd], buf, len, &n, NULL | FALSE ⇒ errno; n≠len ⇒ ENOSPC | COND (stdout/stderr flush) |
| WritePrivateProfileStringA | G | `0x48b274` | `0x409ea6` | `0x409e6a` | [IAT] | `sect=EDX key=EBX value=itoa(CX) file=EAX ("VIDEO","VideoPlay",…,"WET.INI")` | ignored | START + EXIT |
| WritePrivateProfileStringA | G | `0x48b274` | `0x409eea` | `0x409eb7` | [IAT] | `sect=EDX key=EBX value=ECX file=EAX ("STARTUP_DEBUG", "*CheckN", "OK", 0x45dabc)` | ignored | START/EXIT (59 callers) |

---

## 9. Out-of-scope call sites (listed only, not analysed)

- `CoInitialize`: `0x439e74`
- `CoUninitialize`: `0x439eea`
- `CoCreateInstance`: `0x43a083`
- `mciSendCommandA`: `0x44352a`, `0x4435b4`, `0x443664`, `0x443734`, `0x4437ae`, `0x443845`, `0x443a49`, `0x443b3e`, `0x443b9f`, `0x443beb`, `0x443cd0`, `0x443d7b`, `0x443dc5`, `0x443e2e`
- Video-path KERNEL32 users, documented above only as far as KERNEL32 is concerned:
  - `MultiByteToWideChar` `0x439fdc`
  - `Sleep(0)` stub `0x434156`, called from `0x43a19d`
  - `DATA\VIDEO\` open `0x40933b` in `0x4092b1`

## 10. Open questions and risks

Open questions:
1. **Exit code.** `ExitProcess(ESI)` at `0x44301b` is VERIFIED. Whether anything (an installer or launcher) relied on the exit status is unknown. Recommendation: ignore it.
2. **Std-handle policy.** Real Win9x/NT GUI processes usually get NULL std handles. The host can choose NULL (exact) or pseudo-handles to host stdio (better diagnostics). Both paths terminate correctly (§6.12).
3. **`WET.INI` location.** Win32 uses `%WINDIR%`; the host must pick a policy. Because `-novideo` is persisted, the INI location also decides whether video stays disabled.
4. **Reachability of the `0x441f49` console prompt.** It needs a loader called with an empty name. No such caller was found statically, but names are runtime data.
5. **`GetCurrentDirectoryA` length.** `W_DEBUG.DAT` path building breaks silently above 248 characters.

Risks for the recompiler and runtime:
1. **Patched code with stale relocations**, all VERIFIED:
   - `0x434133..0x43417c`: the new operands have no relocations; stale entries at `0x434139`, `0x434145`, `0x43414f`, `0x434161`, `0x434169`, `0x43416e`, `0x434175`.
   - `0x401853..0x401865`: a 19-byte NOP run with stale entries at `0x401854`, `0x401859`, `0x401862`.
   - `0x4340de..0x4340e3`: NOPs, no relocations involved.

   Do not use `.reloc` alone to tell pointers from data in these ranges, and never rebase the image.
2. **Ghidra boundaries:** WinMain `0x40399d` is not a Ghidra function, and neither are many `push N; call __CHK` functions: of 1,103 such prologues (VERIFIED raw scan), 442 are not Ghidra entries and 86 are missing from the objdump instruction stream. Objdump linear sweep desynchronises after alignment padding, for example at `0x43b670..0x43b680`, where a switch table sits in code.
3. **`.bss` has `PointerToRawData=0`** and must be zero-filled. All section `VirtualSize` values are 0, so use `SizeOfRawData`.
4. **`pop fs` inside `malloc`** (`0x443107`) must preserve the TIB base. **`fs:[8]`** must describe the real guest stack, or `__CHK` aborts with "Stack Overflow!".
5. **`strcmp` `0x446340` reads 4 bytes at a time**, so guest memory just past strings must be readable. **`CloseHandle` must return exactly 1**, or the game's close wrapper reports failure.
6. **Paths:** mixed `/` and `\`, upper-case literals against mixed-case files, names containing spaces (`SAVEGAME.  1`, `   0.PCX`), absolute `<cwd>\W_DEBUG.DAT`, and an optional `X:\` CD base from `CDROM.LOC`. The `DATA\SAVE` and `DATA\DATABASE` directories are missing in `original/app/` and must be created in the writable runtime copy, never in `original/app/`.
7. **`"rb+"` means OPEN_ALWAYS** (it creates the file). Emulating C `fopen` semantics instead would change behaviour.
8. **`Sleep` must be a real dispatchable guest address**, returned from `GetProcAddress`. The frame-pacing hook calls it through `call eax` inside `pushad`/`popad`.
9. **Command-line buffer overflow** (`0x409f0f` → 260-byte `0x45d968`; more than 20 tokens overwrite `0x45dabc`): keep the host command line short.
10. **`[0x48804c]` (`Sleep` pointer) is only set if DirectDraw init gets past GetCaps and the pixel-format check** (§6.10). If the host's DirectDraw layer fails there, the `Sleep(32)` hook at `0x434163` calls address 0.
