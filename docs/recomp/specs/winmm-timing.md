# WINMM, timing and concurrency host-runtime spec for WET.EXE

Scope:
- Every WINMM import of `original/app/WET.EXE` except `mciSendCommandA`, which is on hold (§10 lists its sites only).
- The game's complete timing and concurrency model: threads, timer callbacks, shared state, wait loops and frame/tick pacing.

This is an analysis document for the static recompilation's host runtime. It is not a decompilation.

Image base `0x400000`, entry `0x442cfc`, Watcom C/C++ 32-bit. Companion specs in this directory: `kernel32.md`, `user32-gdi32.md` and `directx.md`. `directx.md` §5 describes the same sound engine from the DirectSound side and agrees on the T1 addresses and the spin loops.

Legend:
- **VERIFIED**: read directly from instructions, relocations or data in the binary. All addresses are guest VAs.
- **INFERRED**: interpretation (intent, Windows behaviour, rates), or something that depends on runtime data.
- Reachability: `START` (startup), `RUN` (normal play), `EXIT` (normal shutdown), `COND` (error or feature path only), `DEAD` (no call, jump, relocation or raw dword reaches the function).
- Thread names: **T0** is the main thread (WinMain, all game code, WndProc, DLGPROCs). **T1** is the WINMM periodic-timer thread that runs the TimeProc `0x438003`.
- Watcom register ABI: arguments in `EAX, EDX, EBX, ECX`, then the stack. Function prologue `push N; call 0x43371d` (`__CHK`).
- Word fields are often read as `mov r32,[addr-2]; sar r32,16`. This document always names the address of the word itself.

---

## 1. Summary for host implementers

1. **Exactly two guest threads exist in live code** (VERIFIED):
   - T0, the main thread.
   - T1, one WINMM periodic timer: `timeSetEvent(uDelay=[0x48a518], uResolution=10, lpTimeProc=0x438003, dwUser=2, fuEvent=TIME_PERIODIC)` at `0x436899`.

   There is nothing else:
   - `CreateThread` is reached only from the dead Watcom `_beginthread` (`0x44c1fa` in `0x44c15f`).
   - `SetTimer` has no TimerProc.
   - There is no DirectSound notify (IID absent), no enum callback and no console handler.
   - Live code uses no OS synchronization object (event, mutex, critical section or Interlocked).
2. **T1 is the sound engine's service routine.** Every tick (§5) it:
   - runs queued start and stop commands for 9 channels (8 SFX and 1 stream);
   - refills the streaming music/voice buffer from `DATA\SOUND\MUSIC` with `SetFilePointer`/`ReadFile`;
   - detects finished SFX buffers.

   It calls IDirectSound and IDirectSoundBuffer methods and KERNEL32 file I/O. It never posts messages, never sets events and never touches USER32, GDI or DirectDraw (VERIFIED).
3. **Timer period.** `[0x48a518]` is computed from `timeGetDevCaps` (§4.2). It is **2 ms** for any normal caps (`wPeriodMin=1`). The same value goes to `timeBeginPeriod`.
4. **Three call-free spin loops on T0 wait for T1** (VERIFIED, §7.3):

   | Back-edge → head | Function |
   |---|---|
   | `0x437463 → 0x437451` | `0x437400` (queue SFX) |
   | `0x4376c8 → 0x4376b8` | `0x43764d` (stop channel, channel 8 only) |
   | `0x438ccb → 0x438cbb` | `0x438c82` (synchronous stream stop) |

   Their bodies contain no call. T1 must be able to run while T0 is inside them.
   - A host that services the timer only inside `PeekMessage`/`GetMessage` **deadlocks**.
   - A host whose back-edge yield is not a fair handoff can **livelock**.
   - One of these loops runs inside the WndProc (`WM_ACTIVATEAPP(0)` → `0x437c60` → `0x43764d(8)`) and at process exit.
5. **`timeSetEvent` must return a non-zero id and must call back with `dwUser == 2`.**
   - A zero return is **not** detected as an error. Sound stays "enabled" and the first stream stop hangs.
   - A wrong `dwUser` makes every tick a no-op, which gives the same hang (VERIFIED `0x4368a5`, `0x438028`).
6. **`timeKillEvent` must not wait for T1 while T0 holds the guest lock.** No callback may start after it returns (§8, R6).
7. **T1 needs its own guest stack placed above the main thread's `td->stack_low`** (`[[0x48a694]]`, taken from the main TIB StackLimit).
   - Every callee runs the Watcom `__CHK` probe. That probe compares `esp-N` with the *main* thread's limit, because the runtime is single-threaded.
   - A T1 stack below that limit aborts with "Stack Overflow!" (VERIFIED `0x43372d..0x433746`).
8. **Volume.**
   - The options volume slider (`0x40d2d2`) only calls `waveOutSetVolume`. DirectSound buffer volume is pinned at 0 dB for every non-negative level (VERIFIED `0x437b17`).
   - The host must implement `waveOutSetVolume`/`waveOutGetVolume` as an **in-process master gain on its own audio mix**. They must never change the OS mixer.
   - `waveOutGetDevCapsA` must report `WAVECAPS_VOLUME`. Otherwise the startup save returns 0 and the exit path "restores" the volume to 0.
9. **Dead WINMM code** (VERIFIED, no references): all four `aux*` imports and `midiOutGetDevCapsA`, `midiOutSetVolume`, `midiOutGetVolume`.
   - The functions are `0x443e4b`, `0x443eea`, `0x44386d` and `0x4438d6`.
   - `midiOutGetNumDevs` is live, but it only gates the on-hold MCI MIDI module (§6.3).
10. **Pacing** (§9). The game has no `timeGetTime`, `QueryPerformanceCounter` or `rdtsc`. Its clocks are:
    - `WM_TIMER` id 1 every 60 ms: logic ticks, timed waits and the UI button animation;
    - `Flip(DDFLIP_WAIT)` plus the patched `Sleep(32)`: frame rate;
    - `GetTickCount` 100 ms fixed-step accumulators, in exactly two screens (the dice screen `0x426908` and the "SAD_EYE" screen `0x43353d`);
    - the 2 ms WINMM tick: audio;
    - the DirectSound play cursor: stream refill;
    - `GetLocalTime`, only for the `srand` seed.
11. **Correction to `kernel32.md` §6.9.** The GetTickCount sites `0x4269e3` and `0x4273b1` belong to handler **`0x426908`** (registered by `push 0x426908` at `0x426850`), not to `0x4267c8`. Handler `0x4267c8` (registered at `0x426027`) handles only messages 0x110 and 0x111 (VERIFIED). Message `0xF` is the UI framework's per-frame draw event (§9.3).

---

## 2. Method (reproducible)

- **IAT sites.** Every base relocation whose target is a WINMM slot (`0x48b148..0x48b18c`) was classified by its bytes:
  - `2E FF 15 <slot>` is `call cs:[slot]` (36 sites);
  - `FF 25 <slot>` is a jump thunk (18 thunks at `0x44cc0c..0x44ccfc`).

  No `E8`/`E9` and no raw dword in any section reaches a WINMM thunk, so **the thunks are dead**. No WINMM slot is read as data (VERIFIED).
- **Function starts** are `push imm32; call 0x43371d` prologues (1,103 of them). Callers come from all `E8`/`E9` targets in the objdump listing, plus raw-dword scans for code pointers (callbacks and UI handlers).
- **T1 closure.** Recursive-descent disassembly (capstone) from `0x438003`, following direct calls, gives 13 game functions plus `__CHK` and the fatal-exit path (Appendix B). Memory operands with absolute or displacement addresses ≥ `0x44f000` were collected per function.
- **Wait loops.** Every backward conditional or unconditional jump inside a function was examined. The loop body (target..jump) was checked for reads of T1-written addresses and for calls to the channel getters (`0x437965` etc.). Loops whose only T1-shared read is the error word `[0x455030]` are "loop while no error" loops, not waits (§7.6).

---

## 3. WINMM import inventory (VERIFIED)

| Import | IAT slot | Live sites | Containing function | Reach | Result use |
|---|---|---|---|---|---|
| auxGetDevCapsA | `0x48b148` | – (`0x443e8b`, `0x443f2d`) | `0x443e4b`, `0x443eea` | DEAD | – |
| auxGetNumDevs | `0x48b14c` | – (`0x443e65`, `0x443f07`) | same | DEAD | – |
| auxGetVolume | `0x48b150` | – (`0x443f64`) | `0x443eea` | DEAD | – |
| auxSetVolume | `0x48b154` | – (`0x443ed9`) | `0x443e4b` | DEAD | – |
| mciSendCommandA | `0x48b158` | 14 sites | MCI module | on hold (§10) | – |
| midiOutGetDevCapsA | `0x48b15c` | – (`0x443892`, `0x4438ff`) | `0x44386d`, `0x4438d6` | DEAD | – |
| midiOutGetNumDevs | `0x48b160` | `0x44346f` | `0x443418` ← `0x4365d8` ← `0x401010` | START | word `[0x48a6da]` |
| midiOutGetVolume | `0x48b164` | – (`0x44391e`) | `0x4438d6` | DEAD | – |
| midiOutSetVolume | `0x48b168` | – (`0x4438c8`) | `0x44386d` | DEAD | – |
| timeBeginPeriod | `0x48b16c` | `0x43687c` | `0x436807` | START, after video | ≠0 → error |
| timeEndPeriod | `0x48b170` | `0x4368bd`, `0x436926` | `0x436807` (COND), `0x4368fb` | COND / EXIT / video | ignored |
| timeGetDevCaps | `0x48b174` | `0x43683c` | `0x436807` | START, after video | ≠0 → error |
| timeKillEvent | `0x48b178` | `0x436918` | `0x4368fb` | EXIT, video, re-arm | ignored |
| timeSetEvent | `0x48b17c` | `0x436899` | `0x436807` | START, after video | → `[0x48a514]` |
| waveOutGetDevCapsA | `0x48b180` | `0x437f4d`, `0x437fba` | `0x437f28`, `0x437f91` | START, RUN, EXIT | gate |
| waveOutGetNumDevs | `0x48b184` | `0x43661b` | `0x4365d8` | START | word `[0x48a520]` |
| waveOutGetVolume | `0x48b188` | `0x437fd9` | `0x437f91` | START | saved |
| waveOutSetVolume | `0x48b18c` | `0x437f83` | `0x437f28` | START, RUN, EXIT | ignored |

Appendix A lists every site with its arguments.

**`timeGetTime` is not imported.** The only other clocks are KERNEL32 `GetTickCount` and `GetLocalTime`, and USER32 `SetTimer`.

---

## 4. Multimedia timer API

### 4.1 Sound object and life cycle (VERIFIED)

The sound object is `[0x455024]`, allocated at `0x4010c4` and constructed by `0x4365d8`. Its flags:
- word `+0x72`: "timer armed";
- word `+0x74`: "sound shut down".

| Step | Code | What happens |
|---|---|---|
| Construct | `0x4365d8` (from `0x4010cd`, START) | `0x443418`: MCI MIDI tables reset, `[0x48a6da]=midiOutGetNumDevs()`. `0x443a94`: second MCI table reset (`0x48a6dc..`). `0x4369ae`: channel and sample tables reset. `[0x48a51e]=0`. `[0x48a520]=waveOutGetNumDevs()`. `[0x48a4d4]=0` |
| Init | `0x4366ae(obj, hwnd)` (from `0x40129b`, `0x401372`, and `0x4094b1` after video) | If `[0x48a4ec]!=0`, returns at once. Otherwise:<br>1. `DirectSoundCreate(NULL, &[0x48a4ec], NULL)`;<br>2. `SetCooperativeLevel(hwnd, 3)`;<br>3. primary buffer (`0x436a5b`);<br>4. `SetCooperativeLevel(hwnd, 1)`;<br>5. **timer start `0x436807`**;<br>6. if `[0x48a51e]`, re-create the 200 sample buffers (`0x436b83`).<br>On DirectSoundCreate failure, `[0x455030]=0x14`. The caller `0x4012a0` clears it and leaves sound disabled (`[0x4555f0]=0`). |
| Shutdown | `0x436937(obj, mode)` | Order (VERIFIED):<br>1. if `mode==1`: `0x438c82(8)` (synchronous stream stop, **spins on T1**), then `[0x48a51e]=1`, then `0x436ce4` (INFERRED: copies sample data out so the buffers can be re-created after video);<br>2. `0x4372c7(i)` for i = 0..199 (free samples);<br>3. `0x436aed` (release primary);<br>4. `0x436b18` (release IDirectSound, `[0x48a4ec]=0`);<br>5. **`0x4368fb` (timer kill)**;<br>6. `0x4369ae` (reset tables, `[0x48a51c]=0`);<br>7. `[obj+0x74]=1`. |

Shutdown callers:
- `0x40184e` (mode 0, in `0x401757`, EXIT);
- `0x4092f5` (mode 1, video wrapper `0x4092b1`, out of scope);
- `0x436647` (`0x436636`, EXIT cleanup);
- `0x436721` (init failure);
- `0x4368ec` (timer failure).

**Teardown order hazard (VERIFIED order; consequence INFERRED).** IDirectSound and all buffers are released *before* `timeKillEvent`. In the original, a T1 tick can fire in that window. It does no harm only because:
- the released channels are already marked idle (`0x4372df` sets cur=-1 and the stop flag to 0);
- `0x438b84`, `0x437ea9`, `0x437de9` and `0x4374a3` check `[0x48a4ec]!=0` first.

The host must tolerate callbacks between `0x436b18` and `0x4368fb`.

### 4.2 Timer start `0x436807` (VERIFIED)

```
0x436823  call 0x4368fb                      ; kill a running timer first (if [obj+0x72])
0x436828  if [0x48a4ec]==0 → return          ; no DirectSound → no timer, no error
0x43683c  timeGetDevCaps(&tc /*esp*/, 8)     ; ≠0 → error
          a   = max(tc.wPeriodMin, 50)
          res = (a < tc.wPeriodMax) ? max(2, tc.wPeriodMin) : tc.wPeriodMax
0x436876  [0x48a518] = res
0x43687c  timeBeginPeriod(res)               ; ≠0 → timeEndPeriod(res) @0x4368bd, error
0x436899  id = timeSetEvent(res, 10, 0x438003, 2, 1 /*TIME_PERIODIC|TIME_CALLBACK_FUNCTION*/)
0x4368a0  [0x48a514] = id; if (id) word [obj+0x72] = 1   ; id==0: NO error is raised
error:    word [0x455030]=0x14; *[0x455018]="Can't set sound timer" (0x44f238); 0x436937(obj,0)
```

`res` computed from the caps (the code looks like a mis-expanded `min(max(min,50),max)`; its intent is INFERRED):

| `wPeriodMin` | `wPeriodMax` | `[0x48a518]` = uDelay = timeBeginPeriod argument |
|---|---|---|
| 1 | 1000000 (NT/XP and later) | **2** |
| 1 | 65535 (Win9x / Wine, INFERRED) | **2** |
| 10 | 1000000 | 10 |
| 1 | 40 | 40 |
| 0 | 0 | 0 (`timeBeginPeriod(0)` should fail, which disables sound) |

Other facts:
- `fuEvent=1` means periodic, function callback, **no `TIME_KILL_SYNCHRONOUS`**.
- `uResolution=10` ms is advisory. With `timeBeginPeriod(2)` active, the effective period is about 2 ms (INFERRED).
- **The host can choose the T1 period through `wPeriodMin`.** Values 2..49 map one-to-one when `wPeriodMax>50`.
- **Fidelity default:** return `{1, 1000000}`, which gives 2 ms. Consequences of a longer period are in §5.6.

### 4.3 Timer kill `0x4368fb` (VERIFIED)

`if (word [obj+0x72]) { timeKillEvent([0x48a514]); timeEndPeriod([0x48a518]); word [obj+0x72]=0; }`

Return values are ignored. Callers:
- `0x436823`: re-arm, START and after video;
- `0x43699a`: shutdown, EXIT and video.

### 4.4 Host requirements

| API | Must | Notes |
|---|---|---|
| `timeGetDevCaps(p, 8)` | return 0. Write `wPeriodMin`, `wPeriodMax` (two UINTs) | `{1,1000000}` gives 2 ms |
| `timeBeginPeriod(n)` / `timeEndPeriod(n)` | return 0 for `1 ≤ n ≤ wPeriodMax` | No other effect needed. The host's `Sleep` should have about 1 ms precision regardless (INFERRED: on NT, the 2 ms period made `Sleep(32)` accurate) |
| `timeSetEvent(d, r, proc, user, 1)` | return a non-zero id. Call `proc` every `d` ms as stdcall `proc(id, 0, user, 0, 0)` (`ret 0x14`) on the T1 context (§8) | never zero; never re-enter; coalesce missed ticks |
| `timeKillEvent(id)` | stop future invocations and return without waiting for T1 while the guest lock is held | return 0 |

---

## 5. TimeProc `0x438003` (T1)

### 5.1 Entry, ABI and guard (VERIFIED)

- `void CALLBACK TimeProc(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR, DWORD_PTR)`, `ret 0x14` at `0x438672`. It saves EBX, ESI, EDI and EBP. Only `user` is read, as `[esp+0x1c]` after `__CHK` and 4 pushes.
- **Re-entrancy guard:** word `[0x48a51c]`.
  - If non-zero at entry → return immediately (`0x438019`, the flag is not touched).
  - Otherwise the routine sets it to 1, runs, and clears it at `0x438665`.
  - It is also cleared by `0x4369ae` (T0, table reset).
  - **If a callback is ever aborted mid-way, the flag stays 1 and the sound engine is dead.**
- If `user != 2` (`0x438028`), the routine does nothing except clear the guard.
- No x87, no string instructions and no `fs:` use, except on the `__CHK` overflow/fatal path (`0x4481d9`). No CRT calls (no malloc or stdio) (VERIFIED over the closure). It needs only the integer register file and EFLAGS. DF is irrelevant.

### 5.2 What one tick does (VERIFIED control flow)

| Phase | Loop | Action |
|---|---|---|
| 1. Command mailbox | ch 0..8 (`0x438078`, word counter `[0x488bc4]`) | **Stop:** if the stop flag `w[0x48a388+40ch]` is set: ch<8 → `0x437de9(ch)`, ch 8 → `0x438cd0(8)`. Both **clear the flag first**, then `Stop()` the buffer and mark the channel idle. ch 8 also releases the stream's sample slot (`0x4372df`).<br>**Start:** if the pending sample `w[0x48a38a+40ch] != -1` → `0x4374a3(ch, sample, w[0x48a38c+40ch])`: Release the old channel buffer, `IDirectSound::DuplicateSoundBuffer(master → &chan.buf)`, `SetCurrentPosition(0)`, `Play(loops==0xFF ? DSBPLAY_LOOPING : 0)`, set cur, loops, pan and volume, then pending = -1. |
| 2. Stream refill | s=0 only (`0x438296`, channel 8 = s+8) | If channel 8 is busy:<br>1. `GetCurrentPosition(&[0x488bc8] play, &[0x488bcc] write)`;<br>2. half = bufsize/2 (even);<br>3. if the half flag `w[0x48a366]` is 1 and play < half → fill the second half; if it is 0 and play > half → fill the first half. A fill is `0x438b84`: `Lock` (Restore+retry on `DSERR_BUFFERLOST`), `ReadFile(h, p1, n1)`, `Unlock`;<br>4. near the end of data: loops 0xFF or >1 → `SetFilePointer(h, dataStart, FILE_BEGIN)` (`0x43822d`) and continue (loop count decremented unless 0xFF). Loops ≤1 and remaining ≤4 → `0x438cd0(8)` (stop) and `[0x488bdc]=1`. |
| 3a. Fades | ch 0..8 (`0x43852f`) | Only if fade mode `w[0x48a380+40ch]` is 1 or 2: `SetVolume` ramp per tick, then stop when complete. **DEAD in practice:** the only writer of mode 1/2 is `0x437b71`, reached only from `0x437db3`, which has no reference (VERIFIED). Every other writer stores 0. |
| 3b. SFX completion | ch 0..7 (`0x438570`) | If busy:<br>1. `GetStatus(&[0x488bd8])`. If the HRESULT is 2 (never valid), `Restore` and `GetStatus` again;<br>2. if `hr==0` and the status has neither `DSBSTATUS_PLAYING` (1) nor `DSBSTATUS_LOOPING` (4): loops 0xFF → nothing; otherwise loops--. At 0 → `0x437de9(ch)` (idle); else `0x437ea9(buf,0)`, i.e. `Play` again **without** SetCurrentPosition. |

Any failing DirectSound call inside T1 writes word `[0x455030]=0x14` and stores an error-detail pointer through `[0x455018]` (`0x4365af`). The strings are "Start Playing", "Stop Buffer", "Start Buffer" and "Set Volume" (`0x44f43d..0x44f49a`).

### 5.3 Data structures shared with T0 (VERIFIED layouts)

**Channel table** (9 × 40 bytes at `0x48a368 + 40*ch`). Channels 0..7 are SFX, channel 8 is the stream. Addresses below are for ch 0.

| Off | Addr (ch 0) | Type | Meaning | Writers |
|---|---|---|---|---|
| +0x00 | `0x48a368` | word | current sample index, -1 = idle ("busy" test `0x437965`) | T0: `0x438712`, `0x4377f4`, `0x4372df`; T1: `0x4374a3`, `0x437de9`, `0x4372df` |
| +0x02 | `0x48a36a` | word | paused sample index | T0: `0x4376cd`/`0x4377f4`; T1: idle reset |
| +0x04 | `0x48a36c` | dword | loops left (0xFF = infinite) | T0: `0x438712`; T1: `0x4374a3`, phases 2 and 3b |
| +0x08 | `0x48a370` | dword | frequency | dead setters `0x43789a`, `0x4379a6` |
| +0x0C | `0x48a374` | dword | pan (-100..100) | T0: `0x437a36`; T1 re-applies in `0x4374a3` |
| +0x10 | `0x48a378` | dword | volume (≤0, ×100 = DSBVOLUME) | T0: `0x437ad2`; T1 (dead fade path) |
| +0x14 | `0x48a37c` | dword | channel `IDirectSoundBuffer*`. SFX: a duplicate made by T1. Stream: the stream sample's own buffer, set by T0 | T1: `0x4374a3`; T0: `0x438712` (`0x4388c3`) |
| +0x18..+0x1E | `0x48a380..0x48a386` | words | fade mode, count, step, current volume (dead) | – |
| **+0x20** | **`0x48a388`** | word | **stop request flag (mailbox)** | T0 sets (`0x43764d`, `0x437400`, `0x438c82`). T1 clears (`0x437de9` at `0x437e1e`, `0x438cd0` at `0x438cf6`, `0x4374a3`). T0 also clears it in `0x4372df` (`0x43736f`) |
| +0x22 | `0x48a38a` | word | pending sample to start, -1 = none (mailbox) | T0: `0x437400`; T1: `0x4374a3` resets to -1 |
| +0x24 | `0x48a38c` | dword | pending start loops (mailbox) | T0: `0x437400` |

Channel 8 lives at `0x48a4a8`. Its stop flag is **`0x48a4c8`**.

**Stream descriptor** (24 bytes at `0x48a350`, one entry):

| Off | Field |
|---|---|
| +0x00 | dword file handle (from `[0x455354]`) |
| +0x04 | word "close on stop", always 0: CloseHandle at `0x438d4c` never runs |
| +0x06 | dword data start offset |
| +0x0A | dword buffer bytes (= nAvgBytesPerSec, made even) |
| +0x0E | dword data size |
| +0x12 | dword bytes remaining |
| +0x16 | word next-half flag (`0x48a366`) |

**Sample table:** 200 × 30 bytes at `0x488be0`. Fields: used flag, buffer bytes, master `IDirectSoundBuffer*` at +6, data pointer at +0xA, 16-byte PCMWAVEFORMAT at +0xE. The stream also allocates one sample slot (`0x438712` → `0x4373b4`). T1 frees it in `0x4372df`.

**T1 scratch** `0x488bc4..0x488bdf` (loop counter, rewind flag, play and write cursors, fill length, half size, status, end flag). **Private to T1:** no instruction outside `0x438003..0x438672` references it (VERIFIED).

**Other shared globals:**

| Global | Meaning | Who reads and writes |
|---|---|---|
| `[0x48a4ec]` | IDirectSound* | T0 writes, both read |
| `[0x48a4e8]` | primary buffer | T0 |
| `[0x48a4d0]` | last HRESULT | both write |
| `[0x48a4d4]` | DirectSound volume cap | T0 writes (`0x437d6f`), T1 reads |
| `[0x48a51e]` | "reload samples" flag | T0 |
| word `[0x455030]` | global error code | both write; T0 polls it everywhere |
| `[[0x455018]]` | error detail | both |
| `[0x455014]` | file-system object, used as `this` by the file wrappers but not dereferenced by them | both |

### 5.4 Calls made on T1 (VERIFIED)

**Guest functions:** `0x4374a3`, `0x437de9`, `0x437ea9`, `0x438b84`, `0x438cd0`, `0x4372df`, `0x437a36`, `0x437ad2`, `0x4365af`, and the file wrappers `0x442690`, `0x4426d1`, `0x442654`.

**KERNEL32 (game IAT):**
- `SetFilePointer` at `0x4426c1` (stream rewind);
- `ReadFile` at `0x442710` (refill, half a buffer at most, into the locked region);
- `CloseHandle` at `0x442660` (never executed, see the stream descriptor).

**COM (vtable offset → method):**

| Interface | Methods (call sites) |
|---|---|
| IDirectSound | DuplicateSoundBuffer `+0x14` (`0x43755f`) |
| IDirectSoundBuffer | Release `+0x08` (`0x437525`, `0x43738d`) |
| | GetCurrentPosition `+0x10` (`0x4382d5`) |
| | GetStatus `+0x24` (`0x4385a7`, `0x4385f0`) |
| | Lock `+0x2c` (`0x438bd9`, `0x438c10`) |
| | Play `+0x30` (`0x437ed5`, `0x437ef2`) |
| | SetCurrentPosition `+0x34` (`0x43759c`) |
| | SetVolume `+0x3c` (`0x437b65`, plus the dead fade sites `0x4383dc`, `0x438476`) |
| | SetPan `+0x40` (`0x437ac6`) |
| | Stop `+0x48` (`0x437e76`, `0x437350`) |
| | Unlock `+0x4c` (`0x438c6c`) |
| | Restore `+0x50` (`0x438bec`, `0x437ee7`, `0x4385ce`) |

**Not called on T1:** USER32, GDI, DirectDraw, PostMessage/SendMessage, SetEvent, Sleep, any wait, malloc/free.

### 5.5 Host-visible requirements derived from T1 (cross-reference for the DirectSound spec)

- **Every one of the calls above must succeed (DS_OK) on valid objects.** Otherwise T1 raises the global error 0x14 asynchronously, and 163 T0 loop back-edges that poll `[0x455030]` abort.
- **`GetStatus`** must report the end of a non-looping buffer promptly, with neither bit 1 nor bit 4 set. Otherwise the channel stays busy forever: SFX channels leak, and is_busy(8) loops never end.
- **A non-looping buffer that ended must restart from position 0 on the next `Play`.** T1's replay path (`0x438660`) does not call SetCurrentPosition (INFERRED: DirectSound/Wine reset the play position to 0 at the end).
- **`GetCurrentPosition` must advance in real time** even with no audio device. Otherwise the stream never refills, and the 1-second looping buffer repeats.
- **`Lock` must return pointers into guest memory** (T1 passes them to `ReadFile`). The host audio mixer thread reads them concurrently, so a host-side audio mutex is needed. That mutex is never held while guest code runs.
- **`DuplicateSoundBuffer`** must share refcounted sample memory. T0 releases master buffers (`0x43738d`) while duplicates may still exist.

### 5.6 Timing dependencies on the T1 period

| Effect | 2 ms (original) | Longer host period P |
|---|---|---|
| SFX start latency (mailbox) | ≤ 2 ms | ≤ P |
| T0 spin-wait duration (§7.3) | ≤ 2 ms per wait | ≤ P per wait (each SFX replace and each stream change) |
| SFX "busy" after the sound ended | ≤ 2 ms | ≤ P. Fewer free channels for `0x40868e`'s 8-way search |
| Stream refill margin | 500 ms (two halves of a 1-second buffer) | Safe for P ≲ 200 ms |
| Stream end | stopped on the first tick after the last chunk is *written* (`0x438380..0x438391`) | same |

The stream-end behaviour is a quirk (INFERRED from the control flow; needs runtime confirmation). The tail of every non-looping stream, from about the last half-buffer up to roughly 0.5–1 s, is never played. Keep it unless asked to fix it.

---

## 6. Volume and device-capability functions

### 6.1 Wave (live)

| Function | Code (VERIFIED) | Callers |
|---|---|---|
| `0x437f28` SetWaveVolume(v) | If word `[0x48a520]` (num devs) is 0 → return. `waveOutGetDevCapsA(WAVE_MAPPER=0xFFFFFFFF, &caps, 0x34)`; ≠0 → return. `dwSupport & WAVECAPS_VOLUME` is computed, but **its `je` was patched to `nop nop` at `0x437f63`**. Then `v=min(v,255)` (unsigned) and `waveOutSetVolume((HWAVEOUT)-1, (v*0x101) \| (v*0x101)<<16)` | `0x40138f` (START: apply the configured `[0x455724]` after sound init, saved-config path); `0x40176c` (EXIT, first statement of `0x401757`: restore the saved `[0x45d95c]`); `0x40d2eb` (options slider: `[0x455724]=ecx`) |
| `0x437f91` GetWaveVolume() | If num devs is 0, GetDevCaps fails, or `!(dwSupport & 4)` → return 0. Else `waveOutGetVolume((HWAVEOUT)-1, &dw)` and return `(dw & 0xFFFF) / 0x101` (left channel, 0..255) | `0x401178` (START, right after window/DirectDraw creation) → `[0x45d95c]` |

The game's volume `[0x455724]`:
- is 0..255;
- defaults to `0xFF` (`0x401433`, no config);
- is loaded from `DATA\SAVE\WET.1ST` (`0x401335`).

On the DirectSound side, `0x437ad2(ch, v)`:
- clamps `v` to `[0x48a4d4]`;
- computes `dsvol = v*100`;
- **forces `dsvol=0` when it is ≥0**, then clamps to ≥ -10000.

The callers pass `[0x455724]` (≥0), so buffer volume is always 0 dB (VERIFIED `0x437b17`). **The audible effect of the slider exists only through `waveOutSetVolume`.**

The neighbouring pan slider:
- `0x40d306`: `[0x455728]=value-100`;
- `0x437d3e` → `0x437a36`: SetPan(`v*100`, clamped to ±10000) on all 9 channels.

Host requirements:
- **`waveOutGetNumDevs`** returns ≥1.
- **`waveOutGetDevCapsA(WAVE_MAPPER)`** returns 0 with `dwSupport ⊇ WAVECAPS_VOLUME|WAVECAPS_LRVOLUME` (0xC).
- **`waveOutSetVolume`/`waveOutGetVolume`** accept the handle `0xFFFFFFFF`. They store the value and apply it as master gain to the DirectSound mix: L from the low word, R from the high word, initial value `0xFFFFFFFF`. MSDN describes the scale as logarithmic, so use a perceptual curve (INFERRED). **Never touch the OS mixer.**
- With volume support missing, GetVolume returns 0 and exit calls `waveOutSetVolume(0)`. That is harmless only if the volume is in-process.

### 6.2 MIDI volume (DEAD) and aux / CD volume (DEAD)

These functions are unreferenced (VERIFIED).

| Function | Behaviour |
|---|---|
| `0x44386d` SetMidiVolume(v) | `[0x48a6da]`; `midiOutGetDevCapsA(MIDI_MAPPER=-1, &caps, 0x34)`; needs `MIDICAPS_VOLUME` (1); `midiOutSetVolume((HMIDIOUT)-1, v*0x101*0x10001)` |
| `0x4438d6` GetMidiVolume() | caps as above; `midiOutGetVolume(-1, &dw)` → `(dw & 0xFFFF)/0x101` |
| `0x443e4b` SetCDVolume(v) | `auxGetNumDevs`. The loop `auxGetDevCapsA(i, &caps, 0x30)` takes the first device with `wTechnology == AUXCAPS_CDAUDIO` (1, at caps+0x28). It needs `AUXCAPS_VOLUME` (1, caps+0x2c). Then `auxSetVolume(i, v*0x101*0x10001)` |
| `0x443eea` GetCDVolume() | same search; `auxGetVolume(i, &dw)` → `(dw & 0xFFFF)/0x101` |

Host: the seven imports can be stubs: return 0 devices, `MMSYSERR_NODRIVER`, or a logging abort.

### 6.3 `midiOutGetNumDevs` (live, START)

`[0x48a6da]` is read only by:
- the dead functions above;
- the MCI-MIDI validator `0x443948`.

`0x443948` behaves as follows:
- **count 0 → return 0 without an error**, so every MCI-MIDI entry point that calls it does nothing;
- count ≠0 → device must be 0, else fatal 0x14 "Invalid MidiDeviceNr";
- then `[0x48a6d4]` (notify HWND) must be set, else fatal 0x14 "HWND is missing".

Its callers are `0x4434d2`, `0x44356a`, `0x4435c7`, `0x4436de`, `0x443747` and `0x4437ec`.

**Returning 0 cleanly disables the on-hold MIDI module.** The choice belongs to the MCI decision.

---

## 7. Concurrency model

### 7.1 Threads (VERIFIED unless marked)

| Thread | Created by | Runs | Lifetime |
|---|---|---|---|
| T0 main | process | everything except the TimeProc: WinMain `0x40399d`, WndProc `0x4343f1` (incl. MM_MCINOTIFY handlers, out of scope), 18 DLGPROCs, UI handlers, file I/O, DirectDraw | process |
| T1 WINMM timer | `timeSetEvent` `0x436899` | `0x438003` only, every `[0x48a518]` ms | `0x436899` → `0x436918`. Re-created around video playback (`0x4092b1`) |
| Watcom `_beginthread` threads | `CreateThread` `0x44c1fa` | – | DEAD (see `kernel32.md` §6.11). CreateEventA, SetEvent, WaitForSingleObject, CreateMutexA and ReleaseMutex are only in dead MT runtime code |
| Host audio mixer (host-internal) | host DirectSound | reads guest-memory sound buffers | never runs guest code; must never take the guest lock |
| Video / ActiveMovie threads | out of scope | – | T0 waits with `MsgWaitForMultipleObjects` at `0x43a799` (out of scope) |

### 7.2 Synchronization actually used

1. **None at the OS level** (VERIFIED).
2. **Mailbox in plain memory** (channel +0x20/+0x22/+0x24). T0 writes the request. T1 consumes it on its next tick and clears the flag. T0 may spin until it is cleared (§7.3).
3. **T1 re-entrancy guard** word `[0x48a51c]` (T1-private apart from the reset in `0x4369ae`).
4. **Polling of T1-maintained state** through `0x437965` is_busy(ch) = (word `0x48a368+40ch` != -1):
   - `0x40868e` free-channel search (bounded: 8 tries, wraps the start index);
   - `0x4087b2`, `0x40886c`, `0x4088f5` (bounded channel scans);
   - `0x40a2eb` (per logic tick: restart music if channel 8 is idle);
   - `0x410f88` (voice loop, §7.4);
   - UI handler `0x42e0c4` (no loop).
5. **No locking around direct T0 edits of channel state** that T1 also edits:
   - `0x4372c7`/`0x4372df` (free sample: Stop channel buffers, cur=-1, stop flag=0);
   - `0x4376cd` (pause);
   - `0x4377f4` (resume, Play);
   - `0x437a36` (pan), `0x437ad2` (volume);
   - `0x438712` (stream setup: writes the stream descriptor and channel 8 directly, then Play).

### 7.3 T0 busy-waits on T1 state (VERIFIED: no call inside the loop body)

| # | Back-edge → head | Function | Spins while | Set by | Cleared by (T1) | Reached from |
|---|---|---|---|---|---|---|
| W1 | `0x437463 → 0x437451` | `0x437400` QueueStart(sample bx, ch si 0..7, loops ecx) | `w[0x48a388+40ch] != 0` | a previous `0x43764d(ch)` or `0x437400` on a busy channel | `0x437de9` (`0x437e1e`), `0x4374a3` | `0x408727` in `0x40868e` ← `0x408752` (play SFX by id). RUN, frequent |
| W2 | `0x4376c8 → 0x4376b8` | `0x43764d` StopChannel(ch) – **spins only for ch 8**, and only if cur or paused ≠ -1 | `w[0x48a4c8] != 0` | `0x4376a1` | `0x438cd0` (`0x438cf6`) | 36 sites (`0x405697` … `0x43273a`), including:<br>- `0x437c60` StopAll ← **WndProc WM_ACTIVATEAPP(0) `0x40a1ca`**, exit `0x401842`, WinMain `0x403ba9`/`0x403cd6`, options `0x40d336`, `0x4247f9`;<br>- voice `0x41101a`;<br>- options music-off `0x40d22a`;<br>- UI handlers `0x42debc`, `0x42e11f`, `0x42e13f`, `0x42e1bc`. |
| W3 | `0x438ccb → 0x438cbb` | `0x438c82` StopStreamSync(ch 8), **unconditional** once `[0x48a4ec]!=0` | `w[0x48a4c8] != 0` | `0x438cb0` | `0x438cd0` (`0x438cf6`) | `0x438796` in `0x438712` (each stream start while channel 8 is busy). `0x438712` has 11 live call sites in 9 functions: `0x409a32`, `0x40a1f0`, `0x40d123`, `0x410ebf`, `0x41f615`, `0x42950e`, `0x42977e`, `0x42de52`, `0x42e0c4`; `0x436958` in `0x436937` mode 1 (before video) |

Properties:
- All three exit immediately if `[0x48a4ec]==0` (no DirectSound), so a silent host is safe.
- They terminate **only** if T1 runs a tick.
- In the original they took ≤ one period (≈2 ms).
- W2 and W3 clear only after T1's `0x438cd0` has *started*. The flag is cleared **before** `Stop`/`Release` are done (`0x438cf6` precedes `0x438d0b`). In the original, T0 could resume while T1 was still tearing down. On a uniprocessor this did not happen, because T1 runs at TIME_CRITICAL priority (INFERRED).

### 7.4 Pumped waits that depend on T1 (no deadlock risk, but timing depends on it)

| Loop | Function | Behaviour |
|---|---|---|
| `0x411006/0x411010 → 0x410f7e` | `0x410ebf` (voice slideshow; from `0x4107c8`, `0x42950e`) | Starts voice stream `0x438712(…, 8, loops=1)`. Then `while (is_busy(8) && !abort) { draw frame (0x43ebd3, 0x43e8eb, 0x43b578), Flip 0x43d646, wait 60 WM_TIMER ticks abortable (0x404e47(0x3c,1)) }`. Abort is ESC (`[0x455384]==0x1b`) or a click (`[0x4553b0]==1`). Then `0x43764d(8)`. **End-of-voice is checked only every ~3.6 s** (60 ticks × 60 ms) |
| per logic tick (not a loop) | `0x40a1f0` ← `0x4050c1` | If sound is on (`[0x4555f0]==1`), music is on (`[0x45571c]`), the app is active (`[0x4555ec]==1`) and `!is_busy(8)`: seek the MUSIC entry `[0x45535c]` and `0x438712(…, 8, 0xFF)` |

### 7.5 Latent races present in the original (INFERRED; preserve or document, do not "fix" silently)

1. **MUSIC handle `[0x455354]` file position.** The handle is opened at `0x402003` from `DATA\SOUND\MUSIC…`.
   - T0 positions it with `0x43ebd3` (several `SetFilePointer` and `ReadFile` calls on the NGS directory) and then calls `0x438712`. `0x438712` only then stops the old stream (W3) and reads the 44-byte WAV header.
   - If a music stream is still playing, a T1 refill (`ReadFile` on the same handle) between T0's seek and the stop moves the position. The voice then starts at a wrong offset, or the header check "WAVE" fails, giving error `0xC` "Can't get Sample ID".
   - Window: from `0x43ec98` to `0x438cb0`. It contains back-edges, for example in `memset` `0x4428e0` called at `0x43874c`.
   - At risk is every T0 seek-and-start pair that can run while a stream plays. The clearest case is the voice start `0x410f62` → `0x410f79`, which interrupts music. Other pairs, where the risk depends on state, load `[0x455354]` at `0x409a82`/`0x409a97`, `0x40d240`/`0x40d250`, `0x41f62c`/`0x41f641`, `0x42959b`/`0x4295ab`, `0x429819`/`0x42982e`, `0x42989a`/`0x4298aa`, `0x42df7b`/`0x42dfaa` and `0x42e14d`/`0x42e15d`.
   - Not affected: the music restart in `0x40a1f0` (`0x40a2fe`/`0x40a30e`), which runs only when channel 8 is idle.
2. **Teardown before `timeKillEvent`** (§4.1).
3. **Unsynchronized T0 channel edits** (§7.2, item 5) racing T1 phase 3b. Example: T0 `0x4372df` Stops a channel while T1 Replays it.

Under the recommended model (§8, R2 and R3), races 2 and 3 can only happen at T0 back-edges, and the state there is consistent. Race 1 remains possible with the same low probability as the original.

### 7.6 Error word `[0x455030]`

T1 writes the global error code (0x14, plus 9 at `0x438c71`).
- **0x14** on any DirectSound failure.
- **9** on a stream read failure. That path is unreachable in practice: the wrapper `0x4426d1` returns the number of *attempted* blocks, so it returns 1 for count 1 (VERIFIED `0x44271b → 0x4426f8`).

163 T0 loop back-edges (in file parsers and UI loops) use `[0x455030]` as their continue condition. They are listed by the scan in §2, for example `0x402288`, `0x40d611`, `0x440f44` and `0x441a14`.

These are not waits. An asynchronous 0x14 from T1 would make them stop early and show the fatal error box. This is a further reason for the DS_OK requirement in §5.5.

---

## 8. Host execution model under a global guest lock (GIL)

Assumption from the task: guest code runs under one global lock, released at loop back-edges and in blocking host calls. Requirements:

| # | Requirement | Why (VERIFIED evidence) |
|---|---|---|
| R1 | **Yield at every taken back-edge when T1 is waiting, with a fair handoff.** Use a ticket lock or a condition-variable handoff, so that T1 is guaranteed to acquire the lock before T0 re-acquires it. A cheap "T1 pending" atomic flag check keeps the common path fast. | W1–W3 contain no calls. With a non-fair mutex, T0 re-acquires immediately and spins forever: a **livelock** at the first SFX replace or stream change. |
| R2 | **T1 runs each TimeProc to completion without yielding at its own back-edges.** T1 should take the lock with priority over T0's next quantum. | Models the Win9x uniprocessor with a TIME_CRITICAL timer thread (INFERRED). Makes W2/W3 observe a completed stop (§7.3) and keeps T1's multi-step updates atomic with respect to T0. T1's loops are bounded (≤9 iterations, plus one `ReadFile` loop). |
| R3 | **Never re-enter the TimeProc and never run two instances at once. Coalesce missed ticks** (run once, then reschedule from now). | Guard `[0x48a51c]` (§5.1). A backlog after a long T0 stall is useless work. |
| R4 | **T1 must keep running while the app is inactive, minimized, in modal loops or shutting down, until `timeKillEvent` or process exit.** | WM_ACTIVATEAPP(0) itself spins (W2 via `0x40a1ca`). The exit path `0x401757` spins (W2 via `0x401842`) before `timeKillEvent`. Pausing T1 on focus loss, or stopping it at SDL_QUIT before the guest exit code runs, **deadlocks**. |
| R5 | **Release the GIL in every blocking or long host call on T0:** `Sleep` (`0x434163`, `0x434156`), PeekMessage/GetMessage when idle, Flip/Blt/Lock with WAIT, `DialogBoxParamA`, `MessageBoxA`, `TrackPopupMenu`, `GetOpenFileNameA` modal loops, and video waits. Re-acquire it to call WndProc or a DLGPROC. | Music must keep streaming during modal dialogs, as on Windows. T1 starvation of more than about 500 ms makes the 1-second buffer audibly repeat. |
| R6 | **`timeKillEvent` must not join T1 while holding the GIL.** Set "killed" and return; T1 checks it after acquiring the GIL and before calling the proc. If a join is required, release the GIL around it. | T1 may be blocked waiting for the GIL, which gives a **deadlock**. The guest frees the tables right after (`0x4369ae`). |
| R7 | **Lock ordering: the GIL is outermost.** Never (re)acquire the GIL while holding a host lock (audio mutex, file table, message queue). Never invoke guest code (WndProc, DLGPROC, TimeProc) while holding a host lock. Host file calls are fast, so prefer *not* releasing the GIL inside ReadFile/SetFilePointer. If a host call does release it, the handle table must be thread-safe. | T1 and T0 both call DirectSound and KERNEL32 file I/O on the MUSIC handle. WM_ACTIVATEAPP may be delivered from inside host calls and then spins on T1. |
| R8 | **Separate guest context for T1:** its own register file and EFLAGS (x87 not needed, but cheap to keep separate), plus its own guest stack (≥16 KiB; 64 KiB recommended). The stack must be at an address **above** the main thread's StackLimit `td[0]` (`[[0x48a694]]`, set from `fs:[8]` at `0x442fe3`). Give T1 a TIB (`fs`) too, for the fatal path. | `__CHK` `0x43372d`: `if (N >= esp \|\| esp-N <= td->stack_low) → "Stack Overflow!" exit`. The td is the main thread's even on T1, because the runtime is single-threaded (`[0x4520f0] → 0x442e85`). |
| R9 | **The callback gets `dwUser` exactly** (2) and `uMsg=0`, `dw1=dw2=0`, stdcall. | `0x438028`. |
| R10 | **The audio mixer thread never takes the GIL** and only reads buffer memory under the host audio mutex. | Avoids a lock cycle with R7. |

**Alternative single-threaded design (acceptable, deterministic).** Instead of a real T1, a host timer sets an atomic "tick due" flag. The host then runs the TimeProc synchronously:
1. at T0 back-edges whenever the flag is set (at least W1–W3 must be covered);
2. inside every blocking host call of R5 (for example, sleep until min(deadline, next tick), then run).

Rules for this design:
- Switch to the dedicated T1 guest stack and save and restore the **complete** T0 context, EFLAGS included.
- Suppress nested servicing while the proc runs: its own back-edges must not re-trigger it.
- Never service ticks only from PeekMessage/GetMessage: **W1–W3 would deadlock**.

### 8.1 Deadlock and behaviour-change checklist

| Scenario | Outcome |
|---|---|
| `timeSetEvent` stub returns 0, or never fires | Silent at init (no error, `0x4368a5`). Then W3 hangs on the first stream change, and W2 on deactivate or exit while music plays. SFX never play, because T1 starts them. |
| TimeProc serviced only from the message pump | Deadlock in W1/W2/W3 (no calls inside). |
| Unfair GIL re-acquire at a back-edge | Livelock in W1–W3 at 100 % CPU. |
| T1 paused on WM_ACTIVATEAPP(0) or minimize | Deadlock inside the WndProc (W2 via `0x437c60`). |
| `timeKillEvent` joins T1 while holding the GIL | Deadlock at exit or video. |
| T1 stack below the main StackLimit | Immediate "Stack Overflow!" exit on the first tick. |
| `dwUser` not passed | Every tick is a no-op, which deadlocks as above. |
| T1 yields at its own back-edges | Not a deadlock. T0 can observe half-finished teardown (§7.3); the original did not do this on Win9x. |
| T1 period > 2 ms | Slower SFX start, stop and finish detection; no functional change up to about 200 ms (§5.6). |
| Host DirectSound returns errors | T1 sets `[0x455030]=0x14` asynchronously, which aborts unrelated T0 loops and shows the fatal box. |
| DirectSoundCreate fails | Sound disabled cleanly (`0x4012a0`): no timer and no spins. A valid fallback for a silent host. |

---

## 9. Frame and tick pacing

### 9.1 Time sources and what they drive (VERIFIED sites)

| Source | Where | Drives |
|---|---|---|
| `WM_TIMER` id 1, 60 ms (`SetTimer` `0x433fca`) | main tick `0x40413f` (one `0x4050c1` logic tick per WM_TIMER pulled); sub-game `0x41c9ad`; pump `0x416534` | Game logic speed. See `user32-gdi32.md` §7 |
| WM_TIMER counting (PM_NOREMOVE) | `0x404e47` wait(n) (17 callers, e.g. the voice loop with n=60); `0x404cdb` slideshow (80 ticks per page); `0x435e8f` cursor animation | Timed waits and animations |
| WM_TIMER countdown `[0x483a60]` | `0x41603c`: UI button press sets 2, `0x416347` decrements per WM_TIMER pulled by `0x416534`, and the draw message 0xF is dispatched each iteration | Button press feedback for 2 ticks (about 120 ms) |
| `Flip(DDFLIP_WAIT)` | `0x43d674` in `0x43d646` (42 callers) | Frame rate (vsync) |
| `Sleep(32)` (repack patch) | `0x43d776 → 0x434163` (`pushad; Sleep(32); popad; call 0x43adaf`) | Frame-rate cap |
| `GetTickCount` | `0x4273b1`/`0x4269e3` (handler `0x426908`); `0x4336f3`/`0x4335ea` (handler `0x43353d`) | 100 ms fixed-step animation (§9.2) |
| WINMM periodic, `[0x48a518]` ms | T1 `0x438003` | Audio service (§5) |
| DirectSound play cursor | `0x4382d5` | Stream refill |
| `GetLocalTime` | `0x44671d` via `time()` → `srand` at `0x40279b` | RNG seed only |

**About the `Sleep(32)` patch.** It sits inside the loop `0x43d737..0x43d77b` that runs after Flip and the back-buffer Lock/Unlock.
- The loop compares the locked `lpSurface` (`[esp+0x24]`) with the 3 recorded buffer pointers (`[[0x48a644]+0x1c+10*i]`).
- **Sleep runs once for every matching entry.**
- The host DirectDraw must return a back-buffer pointer that matches exactly one recorded entry. Zero matches means no Sleep, and the draw pointer `0x43adaf` is not updated. Two matches mean two Sleeps per frame (VERIFIED control flow).

### 9.2 GetTickCount fixed-step screens (VERIFIED)

Both handlers are game-UI handlers called with `EAX=slot` and `EDX=message`. Message `0x110` means init; message `0xF` is the per-frame draw (§9.3).

| Handler | Registered | Asset | Init (msg 0x110) | Per frame (msg 0xF) |
|---|---|---|---|---|
| `0x426908` (frame 0x158) | `0x426850` (`push 0x426908`) | dice: `DATA/ANI/WRF_ANI.TAF`, `WUERFEL.TAF` (`0x427299`, `0x4272bc`) | `[0x4847dc]=GetTickCount()` at `0x4273b1` | `0x4269e3`: `d=now-[0x4847dc]` (unsigned). `while (d >= 100) { [0x4847dc]+=100; d-=100; advance 0x484730 ×2 and 0x48475c ×2 (0x405005); … counter [0x4847d8] … }`. Then draw (`0x427071`) |
| `0x43353d` (frame 0x10) | `0x4333b2` (`push 0x43353d`) | `DATA/ANI/SAD_EYE.TAF` (`0x43367f`) | `[0x485bcc]=GetTickCount()`, `[0x485bd0]=0` at `0x4336f3` | `0x4335ea`: same accumulator on `[0x485bcc]`. Each step advances `0x485ba0` and decrements `[0x485bd0]`. At ≤0 it re-randomizes with `rand()` (`[0x485bc4]=(rand()%3)*2`, …) |

Animation therefore runs at 10 steps per second, independent of the frame rate and of WM_TIMER.
- The catch-up loop runs `floor(elapsed/100)` steps in one frame, for example after the window was inactive.
- The host `GetTickCount` must be monotonic (32-bit millisecond wrap is safe, because the subtraction is unsigned). A wall-clock jump causes a burst of steps.

### 9.3 Message 0xF = framework draw event (VERIFIED)

`0x4151d8` dispatches `call [0x483a70 + slot*4]` with `EDX=0xF` (`0x41523a`/`0x415241`). It is called from:
- `0x4157e3`, inside the **modal screen loop** of `0x4156ea`. `0x4156ea` is entered from the screen-open function `0x414e72` (e.g. `0x404a33`). The loop `0x415746..0x4157f9` runs once per frame: pump one WM_TIMER and drain (`0x416534`) → input (`0x436156`, `0x4158f3`, `0x415847`) → `0x43e419` → **draw `0x4151d8` (message 0xF)** → present `0x41649d` (`0x43d646` Flip, including the `Sleep(32)` patch).
- `0x4160a8` and `0x416123`, in the button-press loop `0x41603c`.

The framework reuses Windows message numbers (0x110 WM_INITDIALOG, 0x111 WM_COMMAND, 0x201 click, 0x2B, and 0xF = WM_PAINT). These handlers are game-internal: they are **not** Win32 DLGPROCs, and the host never calls them.

### 9.4 Resulting rates (INFERRED)

| Rate | Value |
|---|---|
| Logic | about 16.7 Hz (60 ms WM_TIMER, coalesced) |
| Rendering | 1 / (draw time + 32 ms + wait for vsync), about 20–28 fps |
| Dice and SAD_EYE animation | 10 Hz wall clock |
| Audio service | 500 Hz |

Host recommendations:
- Generate WM_TIMER from a monotonic clock at 60 ms (see `user32-gdi32.md` §7.2).
- Implement `Sleep(n)` with about 1 ms precision.
- Do not tie WM_TIMER or GetTickCount to frame presentation.

---

## 10. Out-of-scope sites met (addresses only, not analysed)

**mciSendCommandA** (14 sites):
- `0x44352a`, `0x4435b4`, `0x443664`, `0x443734`, `0x4437ae`, `0x443845`, `0x443a49`;
- `0x443b3e`, `0x443b9f`, `0x443beb`, `0x443cd0`, `0x443d7b`, `0x443dc5`, `0x443e2e`.

Its unused thunk is at `0x44cc36`. MCI module entry points called by the sound init are `0x4434c1` and `0x443afb` (store the notify HWND). The MM_MCINOTIFY handlers `0x4439b3` and `0x443f8f` are on T0 (WndProc).

**ole32:**
- `CoInitialize` at `0x439e74`;
- `CoUninitialize` at `0x439eea`;
- `CoCreateInstance` at `0x43a083`.

**Video:**
- `MsgWaitForMultipleObjects` at `0x43a799` (handle array `0x48a528`);
- `Sleep(0)` stub `0x434156`, called at `0x43a19d`;
- video wrapper `0x4092b1`. Before the video it pauses SFX (`0x4376cd`) and shuts sound down with `0x436937(obj,1)`, which includes W3 and `timeKillEvent`. After the video it re-inits (`0x4366ae`, which includes `timeSetEvent`) and resumes (`0x4377f4`).

---

## 11. Open questions and risks

1. **Effective T1 period on the reference platforms.** The 2 ms value comes from code plus assumed caps. Confirm it under Wine with a trace of `timeGetDevCaps`/`timeSetEvent` arguments.
2. **Stream tail truncation** (§5.6). Confirm at runtime against Wine before deciding whether to preserve it.
3. **Range of the volume slider value** (`ecx` at `0x40d2e3`, from `[ctrl+0x30]+[ctrl+0x38]`, `0x415afc`) is not verified. The game clamps it to 0..255.
4. **DirectSound end-of-buffer position reset** (§5.5) is assumed from DirectSound/Wine behaviour. The DirectSound spec must pin it down.
5. **`Sleep(32)` match logic** (§9.1) couples pacing to the host DirectDraw `Lock` pointers.
6. **Race 1 in §7.5** may surface more often if the host releases the GIL inside file I/O.

---

## Appendix A. Every WINMM call site (VERIFIED; all are `call cs:[slot]`)

Arguments are in C order.

| Site | Import | Function | Arguments | Result | Reach |
|---|---|---|---|---|---|
| `0x43661b` | waveOutGetNumDevs | `0x4365d8` | – | → word `[0x48a520]` | START |
| `0x43683c` | timeGetDevCaps | `0x436807` | `&tc` (stack), 8 | ≠0 → error | START, video |
| `0x43687c` | timeBeginPeriod | `0x436807` | `[0x48a518]` | ≠0 → End + error | START, video |
| `0x436899` | timeSetEvent | `0x436807` | `[0x48a518]`, 10, `0x438003`, 2, 1 | → `[0x48a514]`; ≠0 → `[obj+0x72]=1` | START, video |
| `0x4368bd` | timeEndPeriod | `0x436807` | `[0x48a518]` | ignored | COND |
| `0x436918` | timeKillEvent | `0x4368fb` | `[0x48a514]` | ignored | EXIT, video, re-arm |
| `0x436926` | timeEndPeriod | `0x4368fb` | `[0x48a518]` | ignored | EXIT, video, re-arm |
| `0x437f4d` | waveOutGetDevCapsA | `0x437f28` | `0xFFFFFFFF`, `&caps`, 0x34 | ≠0 → skip | START, RUN, EXIT |
| `0x437f83` | waveOutSetVolume | `0x437f28` | `(HWAVEOUT)0xFFFFFFFF`, `v*0x101*0x10001` | ignored | START, RUN, EXIT |
| `0x437fba` | waveOutGetDevCapsA | `0x437f91` | `0xFFFFFFFF`, `&caps`, 0x34 | ≠0 → 0 | START |
| `0x437fd9` | waveOutGetVolume | `0x437f91` | `(HWAVEOUT)0xFFFFFFFF`, `&dw` | `(dw&0xFFFF)/0x101` | START |
| `0x44346f` | midiOutGetNumDevs | `0x443418` | – | → word `[0x48a6da]` | START |
| `0x443892` | midiOutGetDevCapsA | `0x44386d` | -1, `&caps`, 0x34 | – | DEAD |
| `0x4438c8` | midiOutSetVolume | `0x44386d` | -1, vol | – | DEAD |
| `0x4438ff` | midiOutGetDevCapsA | `0x4438d6` | -1, `&caps`, 0x34 | – | DEAD |
| `0x44391e` | midiOutGetVolume | `0x4438d6` | -1, `&dw` | – | DEAD |
| `0x443e65` | auxGetNumDevs | `0x443e4b` | – | – | DEAD |
| `0x443e8b` | auxGetDevCapsA | `0x443e4b` | i, `&caps`, 0x30 | – | DEAD |
| `0x443ed9` | auxSetVolume | `0x443e4b` | dev, vol | – | DEAD |
| `0x443f07` | auxGetNumDevs | `0x443eea` | – | – | DEAD |
| `0x443f2d` | auxGetDevCapsA | `0x443eea` | i, `&caps`, 0x30 | – | DEAD |
| `0x443f64` | auxGetVolume | `0x443eea` | dev, `&dw` | – | DEAD |
| 14 sites (§10) | mciSendCommandA | MCI module | – | – | on hold |

Unused WINMM thunks (VERIFIED, never called): `0x44cc0c`, `0x44cc12`, `0x44cc18`, `0x44cc1e`, `0x44cc24`, `0x44cc2a`, `0x44cc30`, `0x44cc36`, `0x44cc3c`, `0x44cccc`, `0x44ccd2`, `0x44ccd8`, `0x44ccde`, `0x44cce4`, `0x44ccea`, `0x44ccf0`, `0x44ccf6`, `0x44ccfc`.

## Appendix B. T1 call closure (VERIFIED, recursive descent from `0x438003`)

| Group | Functions |
|---|---|
| Normal path | `0x438003`, `0x4374a3`, `0x437de9`, `0x437ea9`, `0x438b84`, `0x438cd0`, `0x4372df`, `0x437a36`, `0x437ad2`, `0x4365af` |
| File wrappers | `0x442690` (SetFilePointer), `0x4426d1` (ReadFile, tail `0x44242b`), `0x442654` (CloseHandle) |
| Stack check | `0x43371d`/`0x43372d`/`0x43375c` (`__CHK`) |
| Overflow/fatal path only | `0x442e5c`, `0x442e25`, `0x4481c7`, … |

Functions called **only** by T1: `0x4374a3`, `0x437de9`, `0x438b84`, `0x438cd0`.

Shared with T0: `0x4372df` (T0 enters through `0x4372c7`), `0x437a36`, `0x437ad2`, `0x437ea9`, `0x4365af`, and the file wrappers.
