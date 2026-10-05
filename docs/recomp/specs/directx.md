# DirectDraw / DirectSound host-runtime spec for WET.EXE

Scope: `DirectDrawCreate` and `DirectSoundCreate`, and every COM method call made on the objects they produce
(IDirectDraw, IDirectDrawSurface, IDirectDrawClipper, IDirectSound, IDirectSoundBuffer).
Binary: `original/app/WET.EXE` (SHA-256 `8c223b52…cea9`, image base `0x400000`, entry `0x442cfc`).
This is an analysis document for the static recompilation's host runtime. It is not a decompilation.

Legend:
- **VERIFIED**: read directly from instructions or data (capstone 5.0.3 over the PE image). Addresses are guest VAs.
- **INFERRED**: interpretation, or behaviour that depends on DirectX semantics or runtime data.
- Site status:
  - `LIVE`: statically reachable from the entry point or from WinMain `0x40399d`.
  - `NEVER`: statically reachable, but the guard data can never take the branch. Example: a flag whose only writers store 0.
  - `DEAD`: no call, jump, immediate or data reference reaches the containing function.
  - `COND`: live, but only on an error, first-run, user-action or video path.
- Watcom register ABI: game functions take arguments in `EAX, EDX, EBX, ECX`, then the stack. Most game functions start with `push N; call 0x43371d` (the stack probe).
- All COM methods are `__stdcall`: `this` is the first stack argument, the callee pops its arguments, and the result is in `EAX`.

Out of scope and not analysed: video/AVI/CUT, `CoCreateInstance`, `CoInitialize`, `CoUninitialize`, `mciSendCommandA`.
§9 lists only the sites met along the way. GDI text rendering is specified in `user32-gdi32.md` §18. This document covers only the DirectDraw side of `GetDC`/`ReleaseDC`.

---

## 1. Summary for host implementers

1. **Creation:** one `DirectDrawCreate` and one `DirectSoundCreate` site are live.
   - `0x4340d7`: `DirectDrawCreate(NULL, &[0x48807c], NULL)`.
   - `0x4366e6`: `DirectSoundCreate(NULL, &[0x48a4ec], NULL)`.

   Both are reached via `E8` calls to `jmp [IAT]` thunks: `0x44cf5a` → IAT `0x48b58c`, and `0x44cf54` → IAT `0x48b57c`. There is no `QueryInterface` on any DirectX object and no GUID is passed (all NULL). The game uses DirectX 3 era structures:
   - `DDCAPS` dwSize `0x13c`;
   - `DDSD_ALL = 0x7f9ee`;
   - `DSBUFFERDESC` dwSize `0x14`. VERIFIED.
2. **141 COM call sites.**

   | Group | Total | LIVE | NEVER | DEAD |
   |---|---|---|---|---|
   | DirectDraw | 82 | 51 | 7 | 24 |
   | DirectSound | 59 | 50 | 4 | 5 |

   The method set the host really needs:
   - **IDirectDraw:** GetCaps, GetDisplayMode, SetCooperativeLevel, SetDisplayMode, CreateSurface, FlipToGDISurface, WaitForVerticalBlank, Release.
   - **IDirectDrawSurface:** GetAttachedSurface, GetPixelFormat, Lock, Unlock, Flip, GetDC, ReleaseDC, GetBltStatus, GetFlipStatus, Restore, Release.

     `Blt`, `CreateClipper`, `IDirectDrawClipper::SetHWnd` and `SetClipper` are only on NEVER paths, but a cheap implementation is recommended.
   - **IDirectSound:** CreateSoundBuffer, DuplicateSoundBuffer, SetCooperativeLevel, Release.
   - **IDirectSoundBuffer:** GetCurrentPosition, GetStatus, Lock, Unlock, Play, Stop, SetCurrentPosition, SetFormat (primary only), SetVolume, SetPan, Restore, Release.
3. **Display:** fullscreen `640×480×16` with one flip chain (primary + 1 back buffer) plus one 640×480 off-screen "work" surface.
   - **Pixel format:** RGB565 is the native format and the one to report. RGB555 is supported only through a detection flag (§4.4).
4. **Persistent surface pointers (critical).** The game locks each surface once, records `lpSurface`, unlocks, and from then on writes pixels through the recorded pointer, outside Lock/Unlock (§4.5).
   - Surface memory must be guest memory at stable addresses for the whole surface lifetime, and must stay valid after `Unlock`.
   - `Flip` must swap the memory of primary and back (DirectDraw semantics). After each `Flip`, the game finds the new back buffer by comparing `Lock(back).lpSurface` with the recorded pointers.
   - `lPitch` must be **1280** (no padding): `0x409fb6` copies `0x96000` bytes linearly from the draw buffer.
5. **Presentation is Flip-only at runtime.**
   - Each frame calls `0x43d646`: `Flip(DDFLIP_WAIT)`, then `Lock`/`Unlock` back, then a patched `Sleep(32)`, then a draw-target switch.
   - The `Blt` present path `0x43d39d` is NEVER taken: both selector flags only ever hold 0.
6. **Text:** `GetDC`/`ReleaseDC` are called on the back-buffer surface. GDI output must land in the back surface's *current* memory, the same memory the software renderer writes.
7. **HRESULTs:** the host should return `DD_OK`/`DS_OK` from every method used.
   - `DDERR_WASSTILLDRAWING` (`0x8876021c`) causes busy-wait retries.
   - `DDERR_SURFACELOST` (`0x887601c2`) and `DSERR_BUFFERLOST` (`0x88780096`) trigger `Restore` and a retry.
   - Several failures are fatal or crash (§4.11, §5.9). Never lose surfaces or buffers.
8. **Sound is multi-threaded.** All voice start, repeat, stop and music streaming runs in a WINMM periodic timer callback `0x438003`, about every 2 ms (`timeSetEvent`, `TIME_PERIODIC`, dwUser=2). It calls DirectSound and `ReadFile` from that thread.
   - The main thread **busy-waits** for the callback at `0x437451`, `0x4376b8` and `0x438cbb`.
   - The callback must therefore run concurrently on its own host thread, and host DirectSound plus the file layer must be thread-safe.
9. **Sound model:**
   - Up to 200 static sample buffers (one full `Lock`, filled by `ReadFile`).
   - 8 SFX voices, each using a `DuplicateSoundBuffer` of a sample.
   - 1 music voice that streams a 1-second looping buffer in half-buffer chunks driven by `GetCurrentPosition`.
   - Volume and pan are in DirectSound units: volume in hundredths of a dB, ≤0; pan −10000..10000. `SetFrequency` is dead.

---

## 2. Method (reproducible)

- **Disassembly:** linear capstone sweep of BEGTEXT.
- **Function starts:** the union of:
  - Ghidra entries;
  - every `68 imm32 E8 rel32→0x43371d` prologue, found by a byte-pattern scan independent of the sweep;
  - direct call targets;
  - immediate code pointers.
- **Reachability:** roots are `0x442cfc`, `0x40399d` (WinMain, stored as an immediate at `0x442cfc`) and raw dwords in DGROUP/.rsrc. Edges are direct calls/jumps plus code-address immediates (handler registrations, callbacks).
  - `.bss` (`0x455000..0x48aa00`) is excluded from raw-dword scans: pefile maps file offset 0 there.
  - Over-approximation: functions sharing a tail with a live function can appear live. `0x436fb0` was checked by hand: it is DEAD.
- **COM sites:** every `call dword ptr [reg+disp]` in the DirectX code ranges was decoded. Ranges: `0x433000..0x439200`, `0x43a326..`, `0x43a96d..`, `0x43ab84..0x43ad90`, `0x43cf4f..0x43e4b0` and `0x44bf60..`.
  - The object was traced back to its global or struct field and the offset mapped to the DX3/DX5 vtable order. The order given in the task was checked against ddraw.h/dsound.h; all offsets used are consistent with it.
  - In the same ranges, 7 indirect calls are not COM (stack-probe misdecodes and the WndProc handler table at `0x434427`). Every other call was mapped.
- **Data flow for NEVER claims:** every instruction operand writing the guard field was enumerated, for example `DISP+0x3e`, `[0x483ac0]` and the voice fade field. Writes through computed pointers into those structures were looked for and not found. This rests on that static scan.

---

## 3. Object inventory (VERIFIED)

### 3.1 Display context `DISP` = `0x488074`

The pointer `[0x48a644] = 0x488074` is set in `0x43ab84`. The struct is zero-filled (`0x4c` bytes) by `0x4337dc`.

| Offset | Abs | Type | Meaning |
|---|---|---|---|
| +0x00 | 0x488074 | HWND | main window (`CreateWindowExA` result, `0x433fba`) |
| +0x04 | 0x488078 | HMENU | always 0 in live code |
| +0x08 | 0x48807c | IDirectDraw* | `lpDD` |
| +0x0c | 0x488080 | IDirectDrawSurface* | `surf[0]` primary (flip chain front) |
| +0x10 | 0x488084 | IDirectDrawSurface* | `surf[1]` back buffer (attached) |
| +0x14 | 0x488088 | IDirectDrawSurface* | `surf[2]` off-screen "work" surface |
| +0x18 | 0x48808c | IDirectDrawClipper* | windowed path only (NEVER) |
| +0x1c / +0x26 / +0x30 | | 3 × `{u32 ptr; u16 w; u16 h; u16 pitchBytes}` | `entry[0..2]`: `lpSurface`, `dwWidth`, `dwHeight` and `lPitch`, captured by `0x43d7c8` |
| +0x3a | 0x4880ae | u16 | `dispIdx` (Flip/Blt target). Set to 0 by `0x43d7c8`, never changed |
| +0x3c | 0x4880b0 | u16 | `drawIdx`: which `entry` is the current render target |
| +0x3e | 0x4880b2 | u16 | present mode: ≠0 means Blt present, 0 means Flip. **Only ever 0** (no writer besides the zero-fill) |
| +0x40 / +0x44 | 0x4880b4/b8 | i32 | client origin (`ClientToScreen`); 0 in fullscreen |
| +0x48 | 0x4880bc | u32 | extra DDSCAPS: `DDSCAPS_MODEX` (0x200000) if width==320, else 0 |

### 3.2 Software render target (VERIFIED)

`0x43adaf(&entry)` stores the current draw buffer for the renderer (`0x4441bd`, `0x43dc5e`):

| Address | Meaning |
|---|---|
| `[0x452368]` | base pointer |
| `[0x48a63c]` | base pointer (copy) |
| `[0x45236c]` (word) | width |
| `[0x45236e]` (word) | height |
| `[0x452370]` (word) | pitch in pixels: `lPitch/2`, or the width if that is larger |

`[0x48a63c]` is pre-set to `0xa0000` (DOS VGA) by `0x43a9c8` and replaced once `0x43d7c8` runs. All pixel-writing renderers (`0x44479d`, `0x444816`, `0x444a52`, `0x444d1d`, `0x444e52`, `0x4458cc`, `0x445c8b`, `0x445edd`, …) address memory through `[0x452368]`.

### 3.3 DirectSound globals (VERIFIED)

| Address | Meaning |
|---|---|
| `0x48a4ec` | IDirectSound* `lpDS` |
| `0x48a4e8` | primary IDirectSoundBuffer* |
| `0x48a4d8..0x48a4e7` | primary WAVEFORMATEX, 16 bytes; `cbSize` is not set (it overlaps `0x48a4e8`) |
| `0x48a4d0` | last HRESULT |
| `0x48a4d4` | master volume cap (dB; config `[0x455724]`, 255 by default) |
| `0x48a4f0` | 16 × u16 "format idx supported" |
| `0x48a510` | u16 chosen primary format idx (−1 = none) |
| `0x48a514` / `0x48a518` | timer id / period |
| `0x48a51c` | timer re-entrancy flag |
| `0x48a51e` | "suspended" flag (samples held in RAM) |
| `0x48a520` | `waveOutGetNumDevs` |
| `[0x455024]` | sound object (0x76 bytes): `+2` HWND, `+6` 100-byte error text, `+0x6a` pan, `+0x72` timer active, `+0x74` re-init flag |

**Sample slot table** `0x488be0`: 200 entries × 30 (`0x1e`) bytes.

| Offset | Abs (slot 0) | Meaning |
|---|---|---|
| +0x00 | 0x488be0 | u16 loaded |
| +0x02 | 0x488be2 | u32 buffer bytes |
| +0x06 | 0x488be6 | IDirectSoundBuffer* |
| +0x0a | 0x488bea | RAM backup pointer while suspended |
| +0x0e | 0x488bee.. | 16-byte PCMWAVEFORMAT: tag, ch, rate, avg, align, bits. **No cbSize**: the next slot's `loaded` word follows |

**Voice table** `0x48a368`: 9 entries × 40 (`0x28`) bytes. Voices 0..7 are SFX; voice 8 is music.

| Offset | Abs (voice 0) | Meaning |
|---|---|---|
| +0x00 | 0x48a368 | i16 `curSlot` (−1 idle) |
| +0x02 | 0x48a36a | i16 `pausedSlot` |
| +0x04 | 0x48a36c | u32 loops (0xff = infinite) |
| +0x08 | 0x48a370 | frequency (dead) |
| +0x0c | 0x48a374 | pan/100 |
| +0x10 | 0x48a378 | volume/100 |
| +0x14 | 0x48a37c | IDirectSoundBuffer* |
| +0x18..+0x1e | 0x48a380..0x48a386 | fade mode, count, step and volume. Fade mode is only set by DEAD `0x437b71`, so it is dormant |
| +0x20 | 0x48a388 | u16 pending-stop |
| +0x22 | 0x48a38a | i16 `nextSlot` |
| +0x24 | 0x48a38c | u32 `nextLoops` |

**Music stream** `ST` = `0x48a350`, one entry of 0x18 bytes for voice 8.

| Offset | Abs | Meaning |
|---|---|---|
| +0x00 | 0x48a350 | file handle |
| +0x04 | 0x48a354 | "close on stop" flag (0 in live code) |
| +0x06 | 0x48a356 | data start offset |
| +0x0a | 0x48a35a | buffer bytes, rounded up to even |
| +0x0e | 0x48a35e | total data bytes |
| +0x12 | 0x48a362 | remaining bytes |
| +0x16 | 0x48a366 | u16 `nextHalf` |

Timer scratch: `0x488bc4` loop index, `0x488bc6` wrap flag, `0x488bc8` play cursor, `0x488bcc` write cursor / lock offset, `0x488bd0` lock length, `0x488bd4` half size, `0x488bd8` status, `0x488bdc` stop flag.

---

## 4. DirectDraw

### 4.1 Methods and stack sizes the host must provide

All are stdcall. Bytes popped include `this`.

| Interface | Off | Method | Pops | Used |
|---|---|---|---|---|
| IDirectDraw | 0x08 | Release | 4 | LIVE |
| | 0x10 | CreateClipper | 16 | NEVER |
| | 0x18 | CreateSurface | 16 | LIVE |
| | 0x20 | EnumDisplayModes | 20 | DEAD |
| | 0x28 | FlipToGDISurface | 4 | LIVE |
| | 0x2c | GetCaps | 12 | LIVE |
| | 0x30 | GetDisplayMode | 8 | LIVE |
| | 0x50 | SetCooperativeLevel | 12 | LIVE |
| | 0x54 | SetDisplayMode (v1: w,h,bpp) | 16 | LIVE |
| | 0x58 | WaitForVerticalBlank | 12 | LIVE |
| IDirectDrawSurface | 0x08 | Release | 4 | LIVE |
| | 0x14 | Blt | 24 | NEVER |
| | 0x2c | Flip | 12 | LIVE |
| | 0x30 | GetAttachedSurface | 12 | LIVE |
| | 0x34 | GetBltStatus | 8 | LIVE |
| | 0x44 | GetDC | 8 | LIVE |
| | 0x48 | GetFlipStatus | 8 | LIVE |
| | 0x54 | GetPixelFormat | 8 | LIVE |
| | 0x64 | Lock | 20 | LIVE |
| | 0x68 | ReleaseDC | 8 | LIVE |
| | 0x6c | Restore | 4 | LIVE (only after SURFACELOST) |
| | 0x70 | SetClipper | 8 | NEVER |
| | 0x74 | SetColorKey | 12 | DEAD |
| | 0x80 | Unlock | 8 | LIVE |
| IDirectDrawClipper | 0x20 | SetHWnd | 12 | NEVER |

- **Unused vtable slots:** fill them with a stub that logs and returns `DDERR_UNSUPPORTED`.
- **Object layout:** each object must be guest memory whose first dword points to a guest-memory vtable. Its slots hold guest addresses that the indirect-call dispatcher maps to host functions.
- **Registers:** the caller relies on `EBX/ESI/EDI/EBP` being preserved. Preserving `ECX/EDX` too is recommended; it is cheap and Watcom code sometimes reuses registers (INFERRED).

### 4.2 Start-up sequence (LIVE, VERIFIED)

`WinMain 0x40399d → 0x401010 → 0x433f2e(hInst, nCmdShow, class, title; stack 640, 480, 16)` (call at `0x401149`):

| # | Site | Call | Arguments / handling |
|---|---|---|---|
| 1 | `0x433fb3` | `CreateWindowExA` (USER32) | exStyle `0x40000` (WS_EX_APPWINDOW), style `0x80080000` (WS_POPUP\|WS_SYSMENU), 0,0,640,480 → `[0x488074]`; then `SetTimer(hwnd,1,60)`, `ShowWindow`, `UpdateWindow` |
| 2 | `0x4340d7` | `DirectDrawCreate(NULL, &[0x48807c], NULL)` | the result test at `0x4340dc` is followed by 6 NOPs (patched), so the **result is ignored** |
| 3 | `0x434107` | `lpDD->GetCaps(0x4880e8, 0x488224)` | both `DDCAPS.dwSize = 0x13c` (DX3 size). ≠0 → rest of init skipped silently. Caps content is **never read** |
| 4 | `0x434a51` (`0x434a1c`) | `lpDD->GetDisplayMode(&ddsd)` | ddsd: dwSize `0x6c`, dwFlags `0x1000` (DDSD_PIXELFORMAT), ddpf.dwSize `0x20`. ≠0 → "Display mode not determined!" (`0x44f1bc`), error `[0x455030]=0x14`. Reads only `ddpf.dwRGBBitCount` (+0x54): 16, 24 or 32 → OK; 4 → `0x44f126`; 8 → `0x44f148`; other → `0x44f16a` "%d"; plus "requires a HI-COLOUR MODE" (`0x44f182`) |
| 5 | `0x434130` | `lpDD->SetCooperativeLevel(hwnd, 0x11)` | DDSCL_EXCLUSIVE\|DDSCL_FULLSCREEN; result ignored |
| 6 | `0x434138..0x43414a` | `LoadLibraryA`/`GetProcAddress("Sleep")` → `[0x48804c]` | patch, see `kernel32.md` §6.10. Runs only if steps 3 and 4 succeed |
| 7 | `0x434262` (`0x434247`) | `lpDD->SetDisplayMode(640, 480, 16)` | args from `0x434064` (EAX=640, EDX=480, EBX=16); result ignored, wrapper returns 0 |
| 8 | `0x4339b6` (`0x43393d`) | `lpDD->CreateSurface(&ddsd, &[0x488080], NULL)` | ddsd: dwSize `0x6c`, dwFlags `0x29` (CAPS\|PITCH\|BACKBUFFERCOUNT), dwBackBufferCount 1, ddsCaps `0x218` (PRIMARYSURFACE\|FLIP\|COMPLEX), plus MODEX if width==320. lPitch=0 although DDSD_PITCH is set: ignore it. **Result ignored** |
| 9 | `0x4339d3` | `primary->GetAttachedSurface(&caps{0x4 DDSCAPS_BACKBUFFER}, &[0x488084])` | `0x887601c2` → `0x4339e7 Restore(primary)` → retry `0x433a02`; still ≠0 → "Primary Surface Initialization Failed!" (`0x44ef12`), error 0x14 and DD error text `0x43de84` |
| 10 | `0x433a53` | `lpDD->CreateSurface(&ddsd, &[0x488088], NULL)` | ddsd: dwSize `0x6c`, dwFlags `0xf` (CAPS\|HEIGHT\|WIDTH\|PITCH), dwHeight 480, dwWidth 640, lPitch 0, ddsCaps **`0x4` (DDSCAPS_BACKBUFFER!)**\|MODEX-flag, no pixel format. ≠0 → "Couldn't get Back Surface_1!" (`0x44eef4`). Host: treat as a plain 640×480 off-screen surface in the primary format |
| 11 | `0x43abbe` (`0x43ab84`) | `primary->GetPixelFormat(&pf)` | pf.dwSize `0x20`, dwFlags preset `0x40`. If `hr==0 && pf.dwRBitMask==0x7c00` → `[0x48a67c]=15`; otherwise it stays 16 (set by `0x43a9c8`) |
| 12 | `0x43d7c8` | Lock/Unlock of primary, back and work (§4.5) | captures `entry[0..2]`, `dispIdx=0`, `drawIdx=1`; render target = `entry[1]` |
| 13 | `0x43b3fb(0)` | — | clears all three buffers to 0 by direct memory writes (`0x44479d`, which writes `pitchPx*h*2` bytes) |
| 14 | `0x43409b` | `0x4343a2` | registers WM_DESTROY → `0x434562`, later replaced by `0x40a15c` (`0x40171a`) |

The windowed branch of `0x43393d` (`ebp==0`: ClientToScreen, `CreateClipper` `0x433adc`, `SetHWnd(0,hwnd)` `0x433af4`, `SetClipper` `0x433b0a`, two Blt presents) is NEVER taken. The only live caller `0x434085` passes `EBX=1`. VERIFIED.

Second `SetDisplayMode(640,480,16)` call sites, COND (VERIFIED):
- `0x401416`, with `word[0x45561c]`/`word[0x45561e]` = 640/480 and bpp 16. First-run path, when `DATA\SAVE\WET.1ST` cannot be read.
- `0x409493`, after video playback in `0x4092b1`.

Host: treat a repeated `SetDisplayMode` with the same mode as a no-op. Keep all surfaces and their memory; do not make them lost.

### 4.3 DDSURFACEDESC fields the game reads (VERIFIED)

DX3 layout is used (`dwSize 0x6c`, DDSD_ALL = `0x7f9ee`). After `Lock` the game reads only:
- `dwHeight` +0x08;
- `dwWidth` +0x0c;
- `lPitch` +0x10 (stored as u16);
- `lpSurface` +0x24.

After `GetDisplayMode` it reads only `ddpfPixelFormat.dwRGBBitCount` +0x54. On input, Lock descriptors carry dwFlags `0x7f9ef` or `0x7f9ee` and ddsCaps `0x218`. A real Lock ignores both, and so should the host.

### 4.4 Pixel format detection and supported formats

- `0x434a1c` checks only the **desktop** mode before `SetDisplayMode`. It accepts 16, 24 or 32 bpp and rejects 4, 8 or other values. It does not distinguish 565 from 555. VERIFIED.
- The real format switch is `[0x48a67c]`, set from `primary->GetPixelFormat` (step 11):
  - **16** (default): RGB565.
  - **15**: RGB555, when `dwRBitMask==0x7c00`.

  `0x43e528` returns it. `0x401251` passes `1` to the resource pool (`0x4428c8`) if 15, else `0`. `0x43e538` converts game colour constants only when 15. VERIFIED.
- **Host must report RGB565:**
  - dwFlags `DDPF_RGB` (0x40);
  - dwRGBBitCount 16;
  - R `0xf800`, G `0x07e0`, B `0x001f`.

  This keeps all data unconverted. Asset data is 565 (INFERRED from the conversion direction).
- There are no 24/32-bit render paths and no palette. `IDirectDrawPalette` is never used.

### 4.5 Surface memory model and frame composition (VERIFIED unless marked)

**Capture (`0x43d7c8`, called from `0x43ab84` at init)**. For each of primary (`0x43d83f`), back (`0x43d934`) and work (`0x43da10`):
1. `Lock(NULL, &ddsd, DDLOCK_WAIT=1, NULL)`. On `SURFACELOST`, call `Restore` and re-lock.
2. Store `lpSurface`, `dwWidth`, `dwHeight` and `lPitch` into `entry[i]`.
3. `Unlock(lpSurface)` (`0x43d8ef`, `0x43d9c1`, `0x43da9a`).

Failure messages:
- primary: `entry[0].ptr=0` + `MessageBoxA` "PRIMARY SURFACE NOT LOCKED" (`0x44fd23`);
- work: "SECONDARY SURFACE_2 NOT LOCKED" (`0x44fd3e`);
- back: silent.

Then:
- `dispIdx=0`, `drawIdx=1`;
- `0x43adaf(&entry[1])`: the render target becomes the back buffer memory;
- if any Restore happened, `0x43b3fb(0)` clears.

**Drawing.** All sprites, backgrounds, UI and the software mouse cursor are drawn by CPU code into `[0x452368]`, which is the recorded pointer. This happens **without** Lock. Only text goes through `GetDC` (§4.7).

**Present: `0x43d646`** (42 call sites, e.g. `0x40264b`, `0x403b3f`, `0x403ec6`, `0x409aec`, `0x4164be` …):
1. Present mode is word `DISP+0x3e` (always 0), so the Flip path is used:
   - `0x43d674`: `primary->Flip(NULL, DDFLIP_WAIT=1)`;
   - on `SURFACELOST`: `0x43d690 surf[dispIdx]->Restore()`, then `0x43d6a6` Flip again. Other results are ignored.
2. `0x43d6e4`: `back->Lock(NULL, &ddsd{0x6c, 0x7f9ee, caps 0x218}, DDLOCK_WAIT, NULL)`.
   - On `SURFACELOST`: `0x43d6f9 Restore`, then `0x43d716` re-lock.
   - On any other failure: return **without** switching the target.
3. `0x43d72d`: `back->Unlock(lpSurface)`.
4. For `i=0..2`: if `entry[i].ptr == lpSurface`, then `drawIdx=i`, then `jmp 0x434163`. That patch does `pushad; Sleep(32); popad`, `0x43adaf(&entry[i])`, and continues the loop. Frame pacing is therefore one `Sleep(32)` per present.

What the host must do (memory model VERIFIED; implementation INFERRED):
- Allocate three 640×480×2 buffers **A** (primary), **B** (back) and **C** (work) in guest memory, with `lPitch = 1280` exactly.
  - `0x409fb6` (save-game thumbnail, `0x409f76`) copies `0x96000` bytes linearly from the current draw buffer.
  - `0x444816` has a special path for width 640 and stride `0x500`.
- `Flip`: swap the buffers of primary and back, then present the primary's buffer (565 → host format, scaled). After a Flip, `Lock(back)` must return the *other* chain buffer, one of the two pointers recorded at init. If it returns any other address, the game silently keeps drawing into the front buffer.
- `FlipToGDISurface`: if primary does not currently hold A, swap back so it does, then present.
- `Lock`/`Unlock` must not copy, move or invalidate memory. Unlock is a no-op apart from state.
- The work surface C is only cleared (`0x43b3fb`) in live code. Its other users are DEAD or NEVER (`0x43d39d`, `0x43e4af`).

**`0x43daf1` "switch to GDI surface"** (LIVE). Callers: `0x40155b`, `0x4015e5`, `0x4018d3`, `0x401ac9`, `0x403f1d`, `0x4042d3`, WM_ACTIVATEAPP(1) `0x40a184`, and the sound-options popup `0x40d331`.
1. If word `DISP+0x3e != 1` (always true): `0x43db18 lpDD->FlipToGDISurface()`. On failure: `MessageBoxA` "Switch to GDI failed!" (`0x44fd5d`).
2. Then the same `Lock(back)`/`Unlock` and `entry` matching as `0x43d646`, but without the Sleep: `0x43db72`, `0x43db89` Restore, `0x43dba8`, `0x43dbc1`.

### 4.6 Blt present `0x43d39d` (NEVER at runtime, VERIFIED)

The function is statically reachable from:
- `0x4164b3` in `0x41649d`, only if `[0x483ac0]==1`. The only writers of `[0x483ac0]` are `0x414caa` (0) and `0x4164cf` (`EDX=0` from `0x401b2c`).
- `0x433b14`/`0x433b20` (windowed branch);
- `0x43d77d` (`DISP+0x3e≠0`).

What it would do:
1. Swap `drawIdx` between 1 and 2.
2. `0x43d455 surf[dispIdx]->GetBltStatus(DDGBS_ISBLTDONE=2)`, retried while `WASSTILLDRAWING`.
3. `0x43d487 primary->Blt(&dst, src=old target surface, &src, DDBLT_WAIT=0x1000000, NULL)`, with:
   - `src` = `{0,0,640,480}` inset by margins `[0x48a66c..0x48a678]`;
   - `dst` = `src` shifted by the client origin `[0x48a658/0x48a65c]`.
4. Retry while `WASSTILLDRAWING`. On `SURFACELOST`: `0x43d4aa Restore`, then retry `0x43d4d5`.

Host: an unscaled rectangle copy with clipping is enough as a safety net.

### 4.7 GetDC / ReleaseDC (text)

Wrappers (all LIVE; GDI details in `user32-gdi32.md` §18):
- `0x43cf4f`: TextOutA, 20 call sites.
- `0x43d079`: DrawTextA, 13 call sites.

Surface choice: all 33 callers pass `EDX=0` (VERIFIED). 31 sites use `xor edx,edx`. The other two use `xor edx,ebp` (`0x423a0b`, after `mov edx,ebp` at `0x4239da`; the only call in between is guest `sprintf` `0x442bb4`, which saves and restores `EDX`) and `xor edx,esi` (`0x420e1e`, after `mov edx,esi` at `0x420df5`; no call in between). The wrapper then takes:
- `surf[drawIdx]` if `drawIdx≠0` (`0x43cf82`/`0x43d0bb`);
- otherwise `back` (`0x43cf90`/`0x43d0cb`).

In Flip mode `drawIdx ∈ {0,1}`, so this is always the **back** surface object.

Sequence:
1. `GetDC(&hdc)`, retried while `0x8876021c`. Any other error: no text.
2. GDI calls.
3. `ReleaseDC(hdc)` at `0x43d061` / `0x43d1af`.

Host: `GetDC` returns a DC bound to the back surface's *current* buffer (565, pitch 1280). Text must be rasterised into guest memory synchronously, no later than `ReleaseDC`. CPU drawing and GDI text interleave within a frame.

### 4.8 Vertical blank and status waits (LIVE)

- `0x43ace2`: `0x43acfd lpDD->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN=1, NULL)`. Callers: `0x405faf`, `0x41c137`, `0x41c5eb`, `0x41c898` (UI animation loops); `0x43bab6` is DEAD.
- `0x43ad47(n)`: `n ×` [`0x43ad69 WaitForVerticalBlank(1,0)`; `0x43ad7b WaitForVerticalBlank(DDWAITVB_BLOCKEND=4,0)`]. Callers:
  - `0x402bcb`/`0x402be1` (n=2), inside a 50-iteration loop that interleaves `rand()` (`0x442a4c`): 200 begin/end pairs at start-up;
  - `0x41572d` (n=4);
  - `0x43bb56`/`0x43bbe2` are DEAD.
- `0x43e419` (callers `0x403f41`, `0x403fc1`, `0x404287`, `0x4157dc`, `0x425a53`, `0x425b82`, `0x43020e`):
  - `GetBltStatus(2)` on primary/back/work (`0x43e432`/`0x43e44d`/`0x43e468`);
  - `GetFlipStatus(DDGFS_ISFLIPDONE=2)` on primary/back (`0x43e483`/`0x43e49e`);
  - each retried while `WASSTILLDRAWING`.

  Host: return `DD_OK` immediately.

Host (INFERRED): emulate a real 60 Hz vertical blank.
- `BLOCKBEGIN`: sleep until the next 1/60 s boundary.
- `BLOCKEND`: return at once or within ~1 ms.

These waits pace animations. Returning immediately speeds up UI effects, which conflicts with the timing rule in `AGENTS.md`. The repack ships cnc-ddraw with `vsync=false`, `maxfps=-1` (`original/app/ddraw.ini`). A Wine reference run can calibrate the rate.

### 4.9 Video module and other DirectDraw calls (listed only; video not analysed)

| Site | Context | Call |
|---|---|---|
| `0x43a343` in `0x43a326` (callers `0x43a139`, `0x43a240`) | before or while video plays | `lpDD->SetCooperativeLevel(hwnd, DDSCL_NORMAL=8)`. The object comes via `[0x48a52e]=&DISP` (`0x439e9c`), `+8` |
| `0x43a343` via `0x43a96d` (callers `0x43a1d7`, `0x43a2c9`, `0x43a824`) | after video | `lpDD->SetCooperativeLevel(hwnd, 0x11)` |
| `0x409493` in `0x4092b1` | after video | `SetDisplayMode(640,480,16)` (§4.2) |
| `0x43a19d` | video loop | patched `Sleep(0)` stub `0x434156` |

The video module reads only `DISP+0` (hwnd) and `DISP+8` (lpDD). It makes no surface calls. Host: `SetCooperativeLevel` may simply record the flags. Surfaces must survive the NORMAL→EXCLUSIVE switch and the following `SetDisplayMode`. No surface re-capture happens after video (`0x43d78a` is DEAD).

### 4.10 Shutdown and error path

- **Normal exit:** `0x4338d3` (from `0x4019a8`), then `0x4342a2`:
  1. `0x4342c6 primary->Release()`;
  2. `0x4342d9 lpDD->Release()`, then `[0x48807c]=0`, `[0x486040]=0`.

  Back, work and clipper are never released. Back goes with the primary; work leaks. After that come `DestroyMenu(0)` and `DeleteObject(font)` (`0x43ab5f`). `DestroyWindow` is skipped: the `jmp` at `0x433902` is unconditional.
- **Error box `0x4342ef`:** called after every init step. If `[0x485f40]` is non-empty or `[0x455030]≠0`:
  - `MessageBoxA(hwnd, msg+"\n End Program ??", "ERROR", MB_OKCANCEL)`;
  - on OK: `0x4342a2`, `KillTimer(hwnd,1)`, `DestroyWindow`, `PostQuitMessage(0)`.

### 4.11 DirectDraw HRESULT dependencies

| Result | Where | Effect if the host returns it |
|---|---|---|
| `DirectDrawCreate` ≠ 0 | `0x4340dc` (check NOPped) | crash (uses `[0x48807c]`) |
| `GetCaps` ≠ 0 | `0x43410a` | init stops silently: no cooperative level, `Sleep` pointer stays 0, so the `call 0` happens at the first present (`0x434166`) |
| `GetDisplayMode` ≠ 0 or bpp ∉ {16,24,32} | `0x434a54`, `0x434a84` | fatal error box |
| `GetAttachedSurface` ≠ 0 | `0x4339d8`, `0x433a07` | fatal; `0x887601c2` gets one Restore and a retry |
| `CreateSurface(work)` ≠ 0 | `0x433a58` | fatal |
| `Lock` ≠ 0 | `0x43d87f`, `0x43d973`, `0x43da50`, `0x43d719`, `0x43dbad` | message box (capture), or the draw target is not updated (present) |
| `FlipToGDISurface` ≠ 0 | `0x43db1b` | message box |
| `GetDC` ≠ 0 | `0x43cf95`, `0x43d0d0` | text not drawn |
| `0x8876021c` WASSTILLDRAWING | GetDC, ReleaseDC loop, Blt, GetBltStatus, GetFlipStatus | busy retry. Never return it |
| `0x887601c2` SURFACELOST | GetAttachedSurface, Flip, Lock, Blt | Restore and retry once. Never return it |
| Ignored entirely | SetCooperativeLevel, SetDisplayMode, CreateSurface(primary), Unlock, ReleaseDC (only checked for WASSTILLDRAWING), WaitForVerticalBlank, Release | — |

`0x43de84` turns 21 DD codes into text for the error box. It is called only from `0x433b8b`.

### 4.12 Every DirectDraw call site

Object names are as in §3.1. **All live sites use `DISP` globals or `[0x48a644]`.**

| Site | Function | Status | Object | Method | Notes |
|---|---|---|---|---|---|
| 0x434107 | 0x4340b3 | LIVE | lpDD | GetCaps | §4.2 #3 |
| 0x434130 | 0x4340b3 | LIVE | lpDD | SetCooperativeLevel | `0x11` |
| 0x434a51 | 0x434a1c | LIVE | lpDD | GetDisplayMode | §4.2 #4 |
| 0x434262 | 0x434247 | LIVE | lpDD | SetDisplayMode | 640,480,16 (callers 0x434064, 0x401416, 0x409493) |
| 0x4339b6 | 0x43393d | LIVE | lpDD | CreateSurface | primary + 1 back |
| 0x4339d3 | 0x43393d | LIVE | primary | GetAttachedSurface | caps 4 |
| 0x4339e7 | 0x43393d | LIVE | primary | Restore | after SURFACELOST |
| 0x433a02 | 0x43393d | LIVE | primary | GetAttachedSurface | retry |
| 0x433a53 | 0x43393d | LIVE | lpDD | CreateSurface | work 640×480, caps 4 |
| 0x433adc | 0x43393d | NEVER | lpDD | CreateClipper | (0, &[0x48808c], NULL) |
| 0x433af4 | 0x43393d | NEVER | clipper | SetHWnd | (0, hwnd) |
| 0x433b0a | 0x43393d | NEVER | primary | SetClipper | clipper |
| 0x43abbe | 0x43ab84 | LIVE | primary | GetPixelFormat | 565/555 |
| 0x43d83f / 0x43d87a | 0x43d7c8 | LIVE | primary | Lock | (NULL, &ddsd, 1, NULL) |
| 0x43d856 | 0x43d7c8 | LIVE | primary | Restore | |
| 0x43d8ef | 0x43d7c8 | LIVE | primary | Unlock | entry[0].ptr |
| 0x43d934 / 0x43d96e | 0x43d7c8 | LIVE | back | Lock | |
| 0x43d94b | 0x43d7c8 | LIVE | back | Restore | |
| 0x43d9c1 | 0x43d7c8 | LIVE | back | Unlock | entry[1].ptr |
| 0x43da10 / 0x43da4b | 0x43d7c8 | LIVE | work | Lock | |
| 0x43da27 | 0x43d7c8 | LIVE | work | Restore | |
| 0x43da9a | 0x43d7c8 | LIVE | work | Unlock | entry[2].ptr |
| 0x43d674 / 0x43d6a6 | 0x43d646 | LIVE | primary | Flip | (NULL, DDFLIP_WAIT) |
| 0x43d690 | 0x43d646 | LIVE | primary (`surf[dispIdx]`) | Restore | |
| 0x43d6e4 / 0x43d716 | 0x43d646 | LIVE | back | Lock | (NULL, &ddsd, 1, NULL) |
| 0x43d6f9 | 0x43d646 | LIVE | back | Restore | |
| 0x43d72d | 0x43d646 | LIVE | back | Unlock | lpSurface |
| 0x43db18 | 0x43daf1 | LIVE | lpDD | FlipToGDISurface | |
| 0x43db72 / 0x43dba8 | 0x43daf1 | LIVE | back | Lock | |
| 0x43db89 | 0x43daf1 | LIVE | back | Restore | |
| 0x43dbc1 | 0x43daf1 | LIVE | back | Unlock | |
| 0x43cf82 / 0x43cf90 | 0x43cf4f | LIVE | back (`surf[drawIdx]`) | GetDC | |
| 0x43d061 | 0x43cf4f | LIVE | same | ReleaseDC | |
| 0x43d0bb / 0x43d0cb | 0x43d079 | LIVE | back (`surf[drawIdx]`) | GetDC | |
| 0x43d1af | 0x43d079 | LIVE | same | ReleaseDC | |
| 0x43acfd | 0x43ace2 | LIVE | lpDD | WaitForVerticalBlank | (1, NULL) |
| 0x43ad69 / 0x43ad7b | 0x43ad47 | LIVE | lpDD | WaitForVerticalBlank | (1, NULL) / (4, NULL) |
| 0x43e432 / 0x43e44d / 0x43e468 | 0x43e419 | LIVE | primary / back / work | GetBltStatus | (2) |
| 0x43e483 / 0x43e49e | 0x43e419 | LIVE | primary / back | GetFlipStatus | (2) |
| 0x43d455 | 0x43d39d | NEVER | primary | GetBltStatus | (2) |
| 0x43d487 / 0x43d4d5 | 0x43d39d | NEVER | primary | Blt | flags 0x1000000 |
| 0x43d4aa | 0x43d39d | NEVER | primary | Restore | |
| 0x4342c6 | 0x4342a2 | LIVE (exit) | primary | Release | |
| 0x4342d9 | 0x4342a2 | LIVE (exit) | lpDD | Release | |
| 0x43a343 | 0x43a326 / 0x43a96d | COND (video) | lpDD | SetCooperativeLevel | 8 / 0x11 |

DEAD DirectDraw sites (no references; host stubs suffice):

| Site(s) | Function | Call(s) |
|---|---|---|
| 0x4341c6, 0x4341e0 | 0x4341a7 | 2nd `DirectDrawCreate` + `SetCooperativeLevel(hwnd, DDSCL_NORMAL)` |
| 0x43428b | 0x43426f | SetDisplayMode |
| 0x4349f7 | 0x4349c0 | GetCaps (copy-out) |
| 0x434b1b, 0x434b60, 0x434b78, 0x434b82 | 0x434afe | 3rd `DirectDrawCreate` + `SetCooperativeLevel(hwnd, 0x55)` + `EnumDisplayModes(0, NULL, ctx, cb=0x434bc1)` + Release |

EnumDisplayModes callback `0x434bc1` (stdcall, `ret 8`):
- stores `dwWidth`, `dwHeight` and `dwRGBBitCount` into a 100-entry table at `0x485c46` (count at `0x485c44`, 6-byte entries);
- skips the mode if a bpp filter `[0x485e9e]≠0` does not match;
- always returns 1 (DDENUMRET_OK).

Further DEAD surface sites:

| Site(s) | Function | Call(s) |
|---|---|---|
| 0x43d550, 0x43d57a, 0x43d59d, 0x43d5c0 | 0x43d506 | GetBltStatus, Blt, Restore, Blt |
| 0x43d7a1, 0x43d7af, 0x43d7bd | 0x43d78a | Restore all three, then re-capture |
| 0x43e234, 0x43e243, 0x43e25c | 0x43e1ce | Blt, Restore, Blt (DDBLT_WAIT) |
| 0x43e2d1, 0x43e2ef, 0x43e30a, 0x43e328, 0x43e340 | 0x43e266 | SetColorKey(DDCKEY_SRCBLT=8, {k,k}), Restore, Blt (DDBLT_WAIT\|DDBLT_KEYSRC = 0x1008000) |
| 0x43e37a | 0x43e34b | GetDC |
| 0x43e3d1 | 0x43e39c | ReleaseDC |
| 0x43ad1d | 0x43ad03 | `WaitForVerticalBlank(4)` (shares 0x43acfd) |
| 0x44bf73 | stub 0x44bf68 | `primary->Release()` + `call 0x43393d`. Unreferenced patch remnant at the end of BEGTEXT (no E8/E9 or raw reference) |

---

## 5. DirectSound

### 5.1 Methods and stack sizes

| Interface | Off | Method | Pops | Used |
|---|---|---|---|---|
| IDirectSound | 0x08 | Release | 4 | LIVE |
| | 0x0c | CreateSoundBuffer | 16 | LIVE |
| | 0x14 | DuplicateSoundBuffer | 12 | LIVE (timer thread) |
| | 0x18 | SetCooperativeLevel | 12 | LIVE |
| IDirectSoundBuffer | 0x08 | Release | 4 | LIVE |
| | 0x10 | GetCurrentPosition | 12 | LIVE |
| | 0x18 | GetVolume | 8 | DEAD |
| | 0x20 | GetFrequency | 8 | DEAD |
| | 0x24 | GetStatus | 8 | LIVE |
| | 0x2c | Lock | 32 | LIVE |
| | 0x30 | Play | 16 | LIVE |
| | 0x34 | SetCurrentPosition | 8 | LIVE |
| | 0x38 | SetFormat | 8 | LIVE (primary) |
| | 0x3c | SetVolume | 8 | LIVE |
| | 0x40 | SetPan | 8 | LIVE |
| | 0x44 | SetFrequency | 8 | DEAD |
| | 0x48 | Stop | 4 | LIVE |
| | 0x4c | Unlock | 20 | LIVE |
| | 0x50 | Restore | 4 | LIVE (after BUFFERLOST) |

### 5.2 Initialisation `0x4366ae(obj=[0x455024], EDX=hwnd)` (VERIFIED)

Callers: `0x40129b` (START), `0x401372` (no-op if already created), `0x4094b1` (re-init after video).

1. If `lpDS≠0`, set `obj+0x74=0` and return.
2. `obj+2=hwnd`. `0x4434c1`/`0x443afb` store hwnd for the CD-audio module (out of scope).
3. `0x4366e6`: `DirectSoundCreate(NULL, &[0x48a4ec], NULL)`. If ≠0: `lpDS=0`, error text (`0x436e35`), `[0x455030]=0x14`. **Sound is then disabled**; at `0x4012a8` the game clears the error and continues with `[0x4555f0]=0`.
4. `0x436706`: `SetCooperativeLevel(hwnd, DSSCL_EXCLUSIVE=3)`. If ≠0, go to step 8.
5. `0x436a5b`:
   - pause all voices (`0x437c88`);
   - release the old primary (`0x436a84`);
   - `0x436ad1 CreateSoundBuffer(&{dwSize 0x14, dwFlags DSBCAPS_PRIMARYBUFFER=1, 0, 0, lpwfx NULL}, &[0x48a4e8], NULL)`;
   - resume voices (`0x437cb8`).

   If ≠0, `0x436937(obj,0)` releases lpDS. Then step 8 dereferences `[0x48a4ec]==0`: **crash at `0x43676b`**. The host must not fail this call.
6. Primary format: if `obj+0x74 && [0x48a510]≠−1`, call `0x439033(obj, [0x48a510])`. Otherwise `0x438edd` probes all 16 formats (§5.3). If none is accepted, `[0x48a4d0]=DSERR_BADFORMAT (0x88780064)`.
7. (Both branches continue.)
8. `0x436774`: `SetCooperativeLevel(hwnd, DSSCL_NORMAL=1)`; result → `[0x48a4d0]`.
9. If `[0x48a4d0]≠0`: error text and `[0x455030]=0x14`, so sound is disabled.

   Otherwise `0x436807` sets up the timer (WINMM):
   - `timeGetDevCaps`; period = `max(2, wPeriodMin)` unless `max(wPeriodMin, 50) ≥ wPeriodMax`, in which case `wPeriodMax`;
   - `timeBeginPeriod(period)`;
   - `timeSetEvent(period, 10, 0x438003, 2, TIME_PERIODIC)`.

   On failure: "Can't set sound timer" and shutdown. If `[0x48a51e]` (resuming after suspend), recreate each sample from its RAM backup with `0x436b83`.

`0x401385` then calls `0x438f62(obj, saved idx)`, which re-applies the configured primary format if it is supported.

### 5.3 Primary format probe `0x439033(obj, idx)` (VERIFIED)

WAVEFORMATEX at `0x48a4d8`, fields as a function of `idx` (0..15):

| Field | Value |
|---|---|
| wFormatTag | 1 (PCM) |
| nChannels | `idx odd` ? 2 : 1 |
| wBitsPerSample | `(idx%4)<2` ? 8 : 16 |
| nBlockAlign | `nChannels × bits/8` |
| nSamplesPerSec | idx 0–3: 8000; 4–7: 11025; 8–11: 22050; 12–15: 44100 |
| nAvgBytesPerSec | rate × nBlockAlign |

Sequence:
1. `0x439140 SetCooperativeLevel(hwnd, 3)`;
2. `0x439151 primary->SetFormat(&wfx)`;
3. `0x439168 SetCooperativeLevel(hwnd, 1)`.

If SetFormat returns 0, `supported[idx]=1` and the probe returns 1. `0x438edd` probes `idx=0..15` (`0x438fea`), then sets the highest supported idx again and stores it in `[0x48a510]`. At start-up that is 17 SetFormat calls and 34 SetCooperativeLevel calls.

Host (INFERRED): accept every format. The game then settles on idx 15 (44100 Hz, 16-bit stereo); this can be the mixer output format. Do not stop or reset secondary buffers on primary SetFormat or SetCooperativeLevel.

The developer menu `0x438d56` lets the user pick the format via `TrackPopupMenu`. It is reached from the options screen `0x40d340` (COND, after `0x43daf1` and stopping all voices).

### 5.4 Static sample buffers (VERIFIED)

`0x43700a(obj, EDX=file handle)` is called from `0x4085b7` (game sample manager `0x40855e`, 64 ids). The handle is `[0x455358]`, opened at `0x401fa2` on `DATA\SOUND\SOUND.TAP` (`0x44d4ac`, mode "rb"), and positioned at the sample by `0x43ebd3` (VERIFIED). `0x436fb0` is DEAD.

1. Find a free slot (`0x4373b4`; at most 200).
2. `ReadFile` 44 bytes.
   - Check `"WAVE"` at +8 (`0x44f424`).
   - Copy the fmt fields from +0x14..+0x23 into `slot.wfx` and take the data size from +0x28.
   - Only the **canonical 44-byte RIFF header** is supported; there is no chunk walking.
3. `0x43713d`: `CreateSoundBuffer(&{0x14, dwFlags 0x40e0, dwBufferBytes=dataSize, 0, &slot.wfx}, &slot.buf, NULL)`.
   - `0x40e0` = DSBCAPS_CTRLFREQUENCY \| CTRLPAN \| CTRLVOLUME \| STICKYFOCUS.
   - `lpwfxFormat` points at 16 bytes without `cbSize`: ignore `cbSize` for PCM.
4. `0x437173`: `Lock(0, dataSize, &p1,&b1,&p2,&b2, 0)`. On `0x88780096`: `0x43718b Restore`, then `0x4371b4` re-lock.
5. `ReadFile` b1 bytes **directly into p1**, and b2 into p2 if `p2≠NULL`.
6. `0x437242`: `Unlock(p1,b1,p2,b2)`; mark `slot.loaded=1`, `slot.size=dataSize`.

Formats present in the data (a scan of all `RIFF…WAVE` headers in the TAP files; all canonical):

| File | Count | Formats |
|---|---|---|
| sound.tap | 65 | 8-bit mono 22050 ×36, 11025 ×12, 8000 ×1; 16-bit mono 22050 ×14; 16-bit stereo 44100 ×2. Data size 68..1,070,664 B |
| ADDSND.TAP | 1 | 16-bit stereo 22050, 7,109,464 B |
| MUSIC.TAP | 41 | 16-bit stereo 22050 ×27, 16-bit stereo 44100 ×12, 16-bit mono 22050 ×2 (streamed, §5.6) |

`Lock` memory must be guest memory and stay readable. Suspend copies it back out (§5.8). Release is `0x4372df`:
1. `Stop` (`0x437350`) every voice whose `curSlot` is this slot;
2. `0x43738d Release(slot.buf)`.

### 5.5 SFX voices 0..7 (VERIFIED)

**Request (main thread).** `0x40868e` picks the next idle voice (round robin, `0x437965`). It then calls:
1. `0x437ad2` (volume → `voice+0x10`);
2. `0x437a36` (pan → `voice+0x0c`);
3. `0x437400(voice, slot, loops)`.

`0x437400` **spins while `voice.pending≠0`** (`0x437451..0x437463`), then sets `nextSlot` and `nextLoops`, and sets `pending=1` only if the voice is busy or paused.

**Start (timer thread).** The timer runs `0x4374a3(voice, slot, loops)` for every voice whose `nextSlot≠−1`:
1. `0x437525 voice.buf->Release()` (old duplicate, if any).
2. `0x43755f lpDS->DuplicateSoundBuffer(slot.buf, &voice.buf)`.
3. `0x43759c SetCurrentPosition(0)`.
4. `0x437ea9`: `Play(0, 0, loops==0xff ? DSBPLAY_LOOPING : 0)` (`0x437ed5`). On BUFFERLOST: `0x437ee7 Restore`, then `0x437ef2` Play.
5. Set `curSlot=slot`, `loops`, `nextSlot=−1`.
6. `0x437a36` → `0x437ac6 SetPan(clamp(pan×100, −10000, 10000))`.
7. `0x437ad2` → `0x437b65 SetVolume(clamp(min(vol, master)×100, −10000, 0))`.

Pan and volume are set *after* Play.

**Repeat and finish (timer thread, `0x438562..0x438660`, voices 0..7, every tick):**
- `0x4385a7 GetStatus(&st)`. The code compares the **HRESULT** with 2 (a bug: meant `DSBSTATUS_BUFFERLOST`); then `0x4385ce Restore` and `0x4385f0` GetStatus. A conforming host never returns 2, so that branch is NEVER.
- If neither `DSBSTATUS_PLAYING(1)` nor `LOOPING(4)` is set and `loops≠0xff`, then `loops--`:
  - if `loops` reaches 0: `0x437de9` (`0x437e76 Stop`, `curSlot=−1`);
  - otherwise `0x43865b Play(0,0,0)` again, **without** SetCurrentPosition.

  So the host must reset a non-looping buffer's position to 0 when it plays to the end (Windows/Wine DirectSound behaviour; INFERRED).

**Other control paths (main thread):**

| Function | Purpose | Steps |
|---|---|---|
| `0x43764d(v)` | stop request | `pending=1`; for v==8 it spins until the timer clears it (`0x4376b8`) |
| `0x437c60` | stop all | calls `0x43764d` for v=0..8. Used from WM_ACTIVATEAPP(0) `0x40a1ca` and others |
| `0x4376cd(v)` | pause | 1. `0x437729 GetStatus` (`0x43773f` Restore if hr==2: NEVER); 2. if playing or looping, `0x437786 Stop`; 3. `pausedSlot=curSlot`, `curSlot=−1` |
| `0x4377f4(v)` | resume | `curSlot=pausedSlot`; `Play(…, v<8 ? loops : 1)` (`0x43786d`). **dwFlags = raw loop count** for SFX: the host must treat only bit 0 (DSBPLAY_LOOPING) and ignore other bits |
| `0x437d6f(x)` | master volume | `[0x48a4d4]=x`, then SetVolume on all voices |
| `0x437d3e(x)` | balance (pan) for all voices | config `[0x455728]` = slider − 100 (`0x40d309`), i.e. ±100 → ±10000 |

### 5.6 Music streaming, voice 8 (VERIFIED)

**Open (main thread): `0x438712(obj, EDX=handle, EBX=8, ECX=loops)`.** Callers: `0x409aac`, `0x40a323`, `0x40d265`, `0x40d3b0`, `0x410f79`, `0x41f656`, `0x4295c0`, `0x429843`, `0x4298bf`, `0x42dfbf`, `0x42e172`. `0x4386e2` is DEAD.
1. If the voice is active: `0x438c82` stop request (spins).
2. Take a slot; read the 44-byte header as in §5.4.
3. `0x43888d CreateSoundBuffer(&{0x14, 0x40e0, dwBufferBytes = nAvgBytesPerSec (one second), 0, &slot.wfx}, &slot.buf, NULL)`; `voice[8].buf = slot.buf` (no AddRef).
4. Fill `ST`: `dataStart` = file position (`0x442675`); `bufBytes` = avg rounded up to even; `total` = `remaining` = data size.
5. `0x438929 SetCurrentPosition(0)`.
6. `0x43896c Lock(0, min(bufBytes,total), …, DSBLOCK_FROMWRITECURSOR=1)`. The buffer is stopped at position 0, so the write cursor is 0 (BUFFERLOST: `0x438984 Restore`, `0x4389a9`).
7. `ReadFile` straight into p1/p2; `remaining -= b1+b2`.
8. `0x438a4b Unlock`.
9. `loops = max(loops,1)`; Play with `DSBPLAY_LOOPING` (`0x438a96` → `0x437ed5`); `SetPan` (`0x438afa`); `nextHalf=0`.

**Refill (timer thread, `0x438296..0x43839b`, every tick while `voice[8].curSlot≠−1`):**

```
hr = buf->GetCurrentPosition(&play /*0x488bc8*/, &write /*0x488bcc*/)   // 0x4382d5; hr!=0 -> skip
half = (bufBytes>>1) & ~1 ; len = half ; wrap = 0 ; stop = 0
if (half > remaining) {
    len = remaining & ~1
    if (loops != 0xff && loops <= 1) { if (len <= 4) { stopStream(8) /*0x438cd0*/; stop = 1; } }
    else wrap = 1
}
if (!stop) {
    if      (nextHalf==1 && play <  half) { nextHalf=0; off = bufBytes>>1; }
    else if (nextHalf==0 && play >  half) { nextHalf=1; off = 0; }
    else { wrap = 0; goto done; }
    if (len) { fill(off,len); remaining -= len; }       // 0x438b84
    if (wrap) { if (loops!=0xff) loops--; off += len; len = half - len;
                seek(handle, dataStart) /*0x442690*/; remaining = total;
                if (len) { fill(off,len); remaining -= len; } }
}
```

`fill` = `0x438b84`:
1. `0x438bd9 Lock(off, len, &p1,&b1,&p2,&b2, 0)` (BUFFERLOST: `0x438bec Restore`, `0x438c10`);
2. `ReadFile(handle, p1, b1)`;
3. `0x438c6c Unlock(p1,b1,p2,b2)`.

The Lock result is **not checked** otherwise. On failure, uninitialised `p1`/`b1` are used. `off+len ≤ bufBytes` always, so there is no wrap-around region.

**Stop: `0x438cd0`**
- `0x4372df(slot)`: `0x437350 Stop`, `0x43738d Release`;
- `curSlot=−1`;
- the file is closed only if `ST+4` is set (never in live code).

On the last pass the stream is stopped one tick after the final chunk is queued, which cuts the tail. Reproduce this; do not fix it.

Host requirements (INFERRED):
- the play cursor must advance in real time with actual audio consumption;
- 1-second buffers (88,200 / 176,400 / 44,100 bytes);
- tick ≤ ~10 ms is sufficient.

### 5.7 Timer callback `0x438003` (VERIFIED)

`TIMECALLBACK(uID, uMsg, dwUser, dw1, dw2)`: stdcall, `ret 0x14`. It acts only if `dwUser==2` (`0x438028`). It guards against re-entry with `[0x48a51c]` (it returns at once if set).

Per tick:
1. For v=0..8 with `pending`: `0x437de9` (Stop) for v<8, or `0x438cd0` for v=8. Then for every v with `nextSlot≠−1`: `0x4374a3` (start).
2. Music refill (§5.6).
3. Fades: dormant.
   - Fade mode is set only by DEAD `0x437b71`.
   - So `0x4383dc`/`0x438476` SetVolume are NEVER.
4. SFX status poll and repeat (§5.5).

DirectSound methods called on the timer thread: DuplicateSoundBuffer, Release, SetCurrentPosition, Play, Restore, SetPan, SetVolume, Stop, GetStatus, GetCurrentPosition, Lock and Unlock. The timer thread also calls `ReadFile`/`SetFilePointer` through the shared pool object `[0x455014]`.

Errors raised there write the global `[0x455030]=0x14` and an error message read later by the main thread. Spurious failures therefore surface as game error boxes.

Host: run the callback on a dedicated host thread with its own guest register context and stack. Serialise ticks. Make host DirectSound (buffer state, mixer) and the file layer thread-safe.

### 5.8 Suspend and resume around video; shutdown (VERIFIED)

**Suspend: `0x4092b1` (video) calls `0x436937(obj, 1)`.**
1. Stop voice 8 (`0x438c82`, spins).
2. `[0x48a51e]=1`.
3. `0x436ce4`: for each loaded slot, `malloc` a backup, then `0x436d89 Lock(0,size,…)` (BUFFERLOST: `0x436da1`, `0x436dcc`), `memcpy` out, `0x436e18 Unlock`, then `0x4372df` Stop and Release.
4. Release the primary (`0x436b09`) and lpDS (`0x436b34`), and kill the timer.
5. Voice table reset by `0x4369ae`. The voice duplicates are **not** released: the host must keep sample data alive while any duplicate references it.

**Resume: `0x4094b1` → `0x4366ae`.** A new `DirectSoundCreate`, then `0x436b83` per slot:
1. `0x436bf8 CreateSoundBuffer`;
2. `0x436c30 Lock` (`0x436c48`/`0x436c73`);
3. `memcpy` in;
4. `0x436cc6 Unlock`.

The host must therefore support creating DirectSound again after a release.

**Shutdown: `0x436636` (from `0x40193b`) → `0x436937(obj, 0)`:** release everything and free the backups.

### 5.9 DirectSound HRESULT dependencies

| Result | Where | Effect |
|---|---|---|
| `DirectSoundCreate` ≠ 0 | `0x4366f0` | sound disabled, game continues (safe way to run without audio) |
| `SetCooperativeLevel(3)` ≠ 0 | `0x43670e` | skips primary creation; sound disabled |
| `CreateSoundBuffer(primary)` ≠ 0 | `0x436719` | **crash** (`0x43676b` NULL deref) |
| all 16 `SetFormat` ≠ 0 | `0x438edd` returns −1, checked at `0x436745` | `[0x48a4d0]=DSERR_BADFORMAT` (`0x436753`), sound disabled |
| `CreateSoundBuffer(secondary)` ≠ 0 | `0x437147`, `0x438897`, `0x436c02` | error 0x14 (sample or music not loaded) |
| `Lock` ≠ 0 except BUFFERLOST | `0x436c7b`, `0x436dd4`, `0x4371bc`; `0x438b84` unchecked | skipped, or garbage pointer use (stream) |
| `DuplicateSoundBuffer`, `SetCurrentPosition`, `Play`, `Stop`, `SetVolume` ≠ 0 | timer and main | error 0x14 + message ("Start Playing", "Pause Channel", "Stop Buffer", …) |
| `GetStatus` == 2 | `0x437731`, `0x4385af` | bogus Restore (never return 2) |
| `0x88780096` BUFFERLOST | Lock, Play | Restore + one retry. Never return it |
| `0x88780064` | set by the game itself | — |

`0x436e35` maps 15 DSERR codes to text (strings `0x44f24e..0x44f3e1`).

### 5.10 Every DirectSound call site

| Site | Function | Status | Object | Method | Notes |
|---|---|---|---|---|---|
| 0x4366e6 | 0x4366ae | LIVE | — | DirectSoundCreate | (NULL, &[0x48a4ec], NULL) |
| 0x436706 | 0x4366ae | LIVE | lpDS | SetCooperativeLevel | (hwnd, 3) |
| 0x436774 | 0x4366ae | LIVE | lpDS | SetCooperativeLevel | (hwnd, 1) |
| 0x436a84 | 0x436a5b | LIVE | primary | Release | |
| 0x436ad1 | 0x436a5b | LIVE | lpDS | CreateSoundBuffer | primary, flags 1 |
| 0x439140 / 0x439168 | 0x439033 | LIVE | lpDS | SetCooperativeLevel | 3 / 1 |
| 0x439151 | 0x439033 | LIVE | primary | SetFormat | &0x48a4d8 |
| 0x43713d | 0x43700a | LIVE | lpDS | CreateSoundBuffer | 0x40e0, data size |
| 0x437173 / 0x4371b4 | 0x43700a | LIVE | slot.buf | Lock | (0, size, …, 0) |
| 0x43718b | 0x43700a | LIVE | slot.buf | Restore | |
| 0x437242 | 0x43700a | LIVE | slot.buf | Unlock | |
| 0x436bf8 | 0x436b83 | LIVE | lpDS | CreateSoundBuffer | re-create after suspend |
| 0x436c30 / 0x436c73 | 0x436b83 | LIVE | slot.buf | Lock | |
| 0x436c48 | 0x436b83 | LIVE | slot.buf | Restore | |
| 0x436cc6 | 0x436b83 | LIVE | slot.buf | Unlock | |
| 0x436d89 / 0x436dcc | 0x436ce4 | LIVE | slot.buf | Lock | backup |
| 0x436da1 | 0x436ce4 | LIVE | slot.buf | Restore | |
| 0x436e18 | 0x436ce4 | LIVE | slot.buf | Unlock | |
| 0x437350 | 0x4372df | LIVE | voice.buf | Stop | voices using the slot |
| 0x43738d | 0x4372df | LIVE | slot.buf | Release | |
| 0x437525 | 0x4374a3 | LIVE (timer) | voice.buf | Release | old duplicate |
| 0x43755f | 0x4374a3 | LIVE (timer) | lpDS | DuplicateSoundBuffer | |
| 0x43759c | 0x4374a3 | LIVE (timer) | voice.buf | SetCurrentPosition | 0 |
| 0x437ed5 / 0x437ef2 | 0x437ea9 | LIVE | arg buf | Play | (0, 0, flags) |
| 0x437ee7 | 0x437ea9 | LIVE | arg buf | Restore | |
| 0x437ac6 | 0x437a36 | LIVE | voice.buf | SetPan | |
| 0x437b65 | 0x437ad2 | LIVE | voice.buf | SetVolume | |
| 0x437729 / 0x43774e | 0x4376cd | LIVE | voice.buf | GetStatus | |
| 0x43773f | 0x4376cd | NEVER | voice.buf | Restore | hr==2 |
| 0x437786 | 0x4376cd | LIVE | voice.buf | Stop | pause |
| 0x437e76 | 0x437de9 | LIVE | voice.buf | Stop | |
| 0x4382d5 | 0x438003 | LIVE (timer) | voice[8].buf | GetCurrentPosition | |
| 0x4385a7 / 0x4385f0 | 0x438003 | LIVE (timer) | voice[0..7].buf | GetStatus | |
| 0x4385ce | 0x438003 | NEVER | voice.buf | Restore | hr==2 |
| 0x4383dc / 0x438476 | 0x438003 | NEVER | voice.buf | SetVolume | fade |
| 0x43888d | 0x438712 | LIVE | lpDS | CreateSoundBuffer | music, 1 s |
| 0x438929 | 0x438712 | LIVE | music buf | SetCurrentPosition | 0 |
| 0x43896c / 0x4389a9 | 0x438712 | LIVE | music buf | Lock | flags 1 (FROMWRITECURSOR) |
| 0x438984 | 0x438712 | LIVE | music buf | Restore | |
| 0x438a4b | 0x438712 | LIVE | music buf | Unlock | |
| 0x438bd9 / 0x438c10 | 0x438b84 | LIVE (timer) | voice[8].buf | Lock | (off, len, …, 0) |
| 0x438bec | 0x438b84 | LIVE (timer) | voice[8].buf | Restore | |
| 0x438c6c | 0x438b84 | LIVE (timer) | voice[8].buf | Unlock | |
| 0x436b09 | 0x436aed | LIVE | primary | Release | |
| 0x436b34 | 0x436b18 | LIVE | lpDS | Release | |

DEAD DirectSound sites:

| Site | Function | Call |
|---|---|---|
| 0x4378e7 | 0x43789a | GetFrequency |
| 0x437a10 | 0x4379a6 | SetFrequency, clamp 100..100000 |
| 0x437bff | 0x437b71 | GetVolume |
| 0x437c46 | 0x437b71 | SetVolume (fade setup) |
| 0x437d30 | 0x437ce8 | primary SetFormat |

Simultaneous sound objects:
- 1 IDirectSound;
- 1 primary;
- ≤200 static slot buffers (the game manager uses ≤64 ids, plus the music slot);
- ≤8 live duplicates (more leak across a suspend).

At most 9 secondary buffers are audible at once: 8 SFX and 1 music.

---

## 6. Host implementation checklist

1. `DirectDrawCreate`/`DirectSoundCreate` at IAT `0x48b58c`/`0x48b57c`: stdcall, 3 args (`ret 12`). Return guest COM objects with guest vtables.
2. **IDirectDraw**:
   - `GetCaps`: accept dwSize `0x13c`, zero-fill, return 0.
   - `GetDisplayMode`: return 0 with bpp 16 (or 32) in `ddpf.dwRGBBitCount`.
   - `SetCooperativeLevel`/`SetDisplayMode`: return 0. 640×480×16 only; same-mode calls are no-ops.
   - `WaitForVerticalBlank`: 60 Hz emulation.
   - `FlipToGDISurface`: §4.5.
3. **Surfaces**:
   - Allocate A, B and C as 614,400-byte guest buffers, pitch 1280, RGB565.
   - `CreateSurface` with caps `0x218` + count 1 makes the A/B chain; caps `0x4` + w/h makes C.
   - `GetAttachedSurface(BACKBUFFER)` returns B's object.
   - `Lock` fills `dwHeight`, `dwWidth`, `lPitch`, `lpSurface` (current buffer) and the 565 pixel format.
   - `Flip` swaps and presents.
   - `GetPixelFormat`: 565.
   - `GetBltStatus`, `GetFlipStatus`, `Restore`: 0.
   - `GetDC`/`ReleaseDC`: a GDI DC over the current buffer.
   - `Blt`: plain copy.
4. **Presentation:** show the primary's buffer after every `Flip` and `FlipToGDISurface`. Frame pacing comes from the guest's `Sleep(32)` (keep `GetProcAddress("Sleep")` working, see `kernel32.md`).
5. **DirectSound**:
   - accept DSBUFFERDESC size `0x14`, PCM 8/16-bit, mono/stereo, 8000–44100 Hz; ignore `cbSize`;
   - honour `DSBPLAY_LOOPING` (bit 0 only);
   - reset position to 0 when a non-looping buffer ends;
   - `GetStatus` bits PLAYING (1) and LOOPING (4);
   - `GetCurrentPosition` from the real mixer position;
   - `Lock`/`Unlock` on guest memory, including `DSBLOCK_FROMWRITECURSOR` on a stopped buffer;
   - `DuplicateSoundBuffer` shares data with independent position, volume, pan and state;
   - volume (hundredths of a dB) and pan semantics;
   - primary `SetFormat` accepts everything.
6. **Threading:** the WINMM timer callback must run concurrently. Guard DirectSound and file I/O with locks. The main thread spin-waits on the timer at `0x437451`, `0x4376b8` and `0x438cbb`.
7. **Never return** `WASSTILLDRAWING`, `SURFACELOST`, `BUFFERLOST` or the value 2 from `GetStatus`.

---

## 7. Open questions

1. Vertical-blank rate and Flip timing: the frame rate is capped at about 31 fps by `Sleep(32)` plus any vsync wait in Flip. Should `Flip` wait for a 60 Hz vblank as real hardware did, or return at once as cnc-ddraw does with `vsync=false`? Calibrate against the Wine reference run.
2. Whether DirectX 3/5 DirectSound rejected the non-standard `Play` flags passed by `0x4377f4` (loop count as dwFlags) is not verified. The host should mask to bit 0.
3. (Resolved) All 33 text call sites pass HDC=0. See §4.7.
4. Real DirectDraw would probably refuse `DDSCAPS_BACKBUFFER` alone on a standalone surface (`0x433a53`), or the `DDSD_PITCH` flag with `lPitch=0` (`0x4339b6`, `0x433a53`). cnc-ddraw accepts both; the host should too.
5. 8-bit sample conversion (unsigned PCM) and the resampling quality for 8000/11025 Hz SFX are host choices. They are not visible to the game.

---

## 8. Glossary of addresses

| Address | Function |
|---|---|
| 0x433f2e | window and DirectDraw init (calls 0x4340b3, 0x434247, 0x43393d) |
| 0x4340b3 | DirectDrawCreate, GetCaps, desktop-format check, cooperative level, Sleep patch |
| 0x434a1c | desktop bpp check (GetDisplayMode) |
| 0x434247 | SetDisplayMode wrapper |
| 0x43393d | surface creation |
| 0x43ab84 | pixel-format detection and render-target setup |
| 0x43d7c8 | surface pointer capture |
| 0x43d646 | present (Flip + back-buffer resync + Sleep(32)) |
| 0x43daf1 | FlipToGDISurface + resync |
| 0x43d39d | Blt present (NEVER) |
| 0x43cf4f / 0x43d079 | text via GetDC |
| 0x43ace2 / 0x43ad47 | vblank waits |
| 0x43e419 | Blt/Flip status waits |
| 0x4342a2 | DirectDraw release |
| 0x4342ef | DirectDraw error box |
| 0x4365d8 | sound object constructor |
| 0x4366ae | DirectSound init |
| 0x436807 | sound timer setup |
| 0x436937 | sound shutdown/suspend |
| 0x439033 | primary format probe |
| 0x438edd | best-format search |
| 0x43700a | sample loader |
| 0x436b83 / 0x436ce4 | restore / backup slots |
| 0x4372df | release slot |
| 0x437400 | queue SFX |
| 0x4374a3 | start voice |
| 0x4376cd / 0x4377f4 | pause / resume voice |
| 0x437de9 | stop voice |
| 0x437ea9 | Play wrapper |
| 0x437a36 / 0x437ad2 | pan / volume |
| 0x438712 | open music stream |
| 0x438b84 | stream fill |
| 0x438cd0 | stop stream |
| 0x438003 | sound timer callback |

---

## 9. Out-of-scope call sites met (not analysed)

- `CoInitialize` `0x439e74` (in `0x439e2c`); `CoUninitialize` `0x439eea` (in `0x439ecd`); `CoCreateInstance` `0x43a083` (in `0x43a061`).
- `mciSendCommandA` `0x443b3e` (in `0x443b0c`). The CD-audio helpers `0x443418`/`0x443a94` (from `0x4365d8`) and `0x4434c1`/`0x443afb` (store hwnd, from `0x4366cf`/`0x4366da`) belong to that module.
- Video COM calls on DirectShow objects held at `0x48a532..0x48a546`. These are **not** DirectDraw or DirectSound objects:
  - Release: `0x439f15`, `0x439f2d`, `0x439f45`, `0x439f5c`, `0x439f74`, `0x439f8c`;
  - QueryInterface with IIDs at `0x4540a0`/`0x454070`/`0x454060`/`0x454080`/`0x453e50`: `0x43a5ab`, `0x43a607`, `0x43a663`, `0x43a6bf`, `0x43a71b`;
  - others: `0x439ff2`, `0x43a006`, `0x43a02a`, `0x43a0aa`, `0x43a184`, `0x43a195`, `0x43a1c7`, `0x43a224`, `0x43a2b4`, `0x43a38f`, `0x43a3ad`, `0x43a3e7`, `0x43a3f2`, `0x43a46c`, `0x43a48e`, `0x43a817`, `0x43a863`, `0x43a922`.
