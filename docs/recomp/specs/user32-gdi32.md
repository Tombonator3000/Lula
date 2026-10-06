# Host runtime spec: USER32, GDI32 and comdlg32 in WET.EXE

Scope: every USER32 and GDI32 import in both IAT blocks, plus `comdlg32!GetOpenFileNameA`, as used by
`original/app/WET.EXE` (SHA-256 `8c223b52…cea9`, image base `0x400000`). This document is an
implementation spec for the host runtime of the static recompilation. It is analysis only. Nothing in
`original/app/` was modified.

Tags used below:

* **VERIFIED**: read directly from instructions or data in WET.EXE (capstone 5.0.3 and pefile, with
  addresses given).
* **INFERRED**: a conclusion drawn from verified facts or from Win32 semantics. It is not observed at
  runtime.

Method (reproducible): every 4-byte occurrence of an in-scope IAT slot address in `BEGTEXT` was located,
then decoded with capstone from the enclosing function start. Function starts are the Ghidra starts plus
every Watcom prologue `push imm; call 0x43371d` (1387 starts). A transitive reachability pass ran from the
entry point `0x442cfc`. Its roots were every DGROUP dword equal to a function start, and its edges were every
immediate or absolute operand that points into code. A site is **DEAD** when no reachable function
references its function. There are 390 call sites in total: 351 live and 39 dead. Appendix A lists all of them.

---

## 1. Key findings for the host implementer

1. **Single main window and table-driven WndProc** (VERIFIED). The window is a 640×480 `WS_POPUP|WS_SYSMENU`
   window. Its WndProc `0x4343f1` dispatches through a 2049-slot handler table at `0x486070`. Only 9 messages
   are handled. Every other message goes to `DefWindowProcA` (§5).
2. **Game logic runs on `WM_TIMER`** (VERIFIED). `SetTimer(hwnd, 1, 60, NULL)` drives the logic. The main loop
   is a busy loop that pulls exactly one `WM_TIMER` per iteration with `PeekMessageA(…, WM_TIMER, WM_TIMER, PM_REMOVE)`.
   It calls the game tick only when a timer message was pulled. Several wait loops count pending
   `WM_TIMER`s with `PM_NOREMOVE` (§7). The host `WM_TIMER` emulation sets game speed directly.
3. **Input is latched by four message handlers** (VERIFIED). WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONDBLCLK and
   WM_RBUTTONDOWN latch mouse state, and WM_KEYDOWN latches the key state. Snapshots are taken after each queue
   drain (§6). Button-up messages are **not** handled. The class has `CS_DBLCLKS`, and the second click of a
   double click is only seen as WM_LBUTTONDBLCLK, so the host must synthesise double clicks exactly as Windows
   does. WM_MOUSEMOVE `wParam` MK_ flags must be exact, because any value other than 1, 2 or 0x10 clears the
   click latches.
4. **Modal Win32 dialogs are used for game features** (VERIFIED). These are save, load, bank deposit and
   withdrawal, movie lists, text entry and work orders: 16 `DialogBoxParamA` call sites, 18 DLGPROCs and 8 of 13
   templates. The host needs a small dialog manager with STATIC, EDIT, LISTBOX and BUTTON controls, the
   WM_CTLCOLOR* protocol and modal-loop semantics (§12).
5. **GDI text goes onto DirectDraw surfaces** (VERIFIED). The only GDI text calls are inside two wrappers:
   `0x43cf4f` (TextOutA) and `0x43d079` (DrawTextA). They always obtain the HDC through
   `IDirectDrawSurface::GetDC` (vtable `+0x44`) and release it with `ReleaseDC` (`+0x68`). One font is used,
   from `CreateFontIndirectA`: "System Small", -14, bold (§18). 33 call sites draw text; the
   positions and strings are listed in §18.4.
6. **Repack patches that touch this scope** (VERIFIED, these are NOP or code-cave edits in the shipped EXE):
   * `SetSysColors` has been removed. There are 19-byte NOP blocks at `0x402765`, `0x401853`, `0x40a189` and
     `0x40a1d9`. `SetSysColors` is never called (§16).
   * `Sleep(32)` is called after every successful Flip (`0x43d776 → 0x434163`). The address of `Sleep` is
     stored in **WndProc table slot 0x7F7** (`0x48804c`), so the host must never deliver message 0x7F7 (§5.2).
7. **Dead framework code.** The window library contains alternative window, scroll and menu code that is
   unreachable. The affected functions are `0x433bfc` and `0x433e0a` (windowed variants), `0x4345b8` (WM_SIZE),
   `0x43475c` and `0x4348a0` (scroll handlers), `0x434716` (WM_PAINT) and `0x435f4b` (SetCursorPos helper).
   These account for every
   GetSystemMetrics, LoadMenuA, SetMenu, PostMessageA, InvalidateRect, GetStockObject and SetCursorPos call.
   The host can stub those imports (§17).

---

## 2. IAT slots in scope

The game block is VERIFIED from `analysis/binary/imports.tsv`. The Watcom runtime block uses the upper-case
`USER32.DLL` name.

| DLL | Import | IAT slot | Live sites | Dead sites | Notes |
|---|---|---|---|---|---|
| GDI32 | CreateFontIndirectA | 0x48b1c4 | 1 | 0 | 0x43ab46 |
| GDI32 | CreateSolidBrush | 0x48b1c8 | 18 | 0 | always 0x7ccaf8 (dialogs) |
| GDI32 | DPtoLP | 0x48b1cc | 1 | 0 | 0x43d105 |
| GDI32 | DeleteObject | 0x48b1d0 | 22 | 0 | brushes, font |
| GDI32 | GetStockObject | 0x48b1d4 | 0 | 1 | dead (0x433e53) |
| GDI32 | SelectObject | 0x48b1d8 | 2 | 0 | font into surface DC |
| GDI32 | SetBkColor | 0x48b1dc | 2 | 0 | text wrappers |
| GDI32 | SetBkMode | 0x48b1e0 | 22 | 0 | |
| GDI32 | SetTextAlign | 0x48b1e4 | 2 | 0 | always 0 |
| GDI32 | SetTextColor | 0x48b1e8 | 20 | 0 | |
| GDI32 | TextOutA | 0x48b1ec | 1 | 0 | 0x43d031 |
| USER32 | AppendMenuA | 0x48b340 | 7 | 0 | sound-format popup |
| USER32 | BeginPaint | 0x48b344 | 11 | 1 | dialog WM_PAINT only |
| USER32 | ClientToScreen | 0x48b348 | 1* | 4 | *0x433aa0, unreachable with live arguments |
| USER32 | CreatePopupMenu | 0x48b34c | 1 | 0 | |
| USER32 | CreateWindowExA | 0x48b350 | 1 | 2 | |
| USER32 | DefWindowProcA | 0x48b354 | 1 | 0 | 0x434445 |
| USER32 | DestroyMenu | 0x48b358 | 4 | 0 | |
| USER32 | DestroyWindow | 0x48b35c | 4 | 0 | error/teardown only |
| USER32 | DialogBoxParamA | 0x48b360 | 16 | 0 | |
| USER32 | DispatchMessageA | 0x48b364 | 7 | 0 | |
| USER32 | DrawTextA | 0x48b368 | 1 | 0 | 0x43d17d |
| USER32 | EndDialog | 0x48b36c | 19 | 0 | |
| USER32 | EndPaint | 0x48b370 | 11 | 1 | |
| USER32 | GetAsyncKeyState | 0x48b374 | 4 | 0 | VK 0x10/0x11/0x12, 0x1B (video) |
| USER32 | GetClientRect | 0x48b378 | 1* | 2 | *0x434470, unreachable with live arguments |
| USER32 | GetDlgItem | 0x48b37c | 23 | 0 | |
| USER32 | GetDlgItemInt | 0x48b380 | 4 | 0 | |
| USER32 | GetDlgItemTextA | 0x48b384 | 3 | 0 | |
| USER32 | GetMessageA | 0x48b388 | 1 | 0 | popup-menu pump |
| USER32 | GetSysColor | 0x48b38c | 1 | 0 | |
| USER32 | GetSystemMetrics | 0x48b390 | 0 | 4 | dead |
| USER32 | InvalidateRect | 0x48b394 | 0 | 1 | dead |
| USER32 | KillTimer | 0x48b398 | 3 | 0 | error/teardown only |
| USER32 | LoadCursorA | 0x48b39c | 2 | 0 | IDC_ARROW, IDC_WAIT |
| USER32 | LoadIconA | 0x48b3a0 | 1 | 0 | IDI_APPLICATION |
| USER32 | LoadMenuA | 0x48b3a4 | 0 | 2 | dead; EXE has no RT_MENU |
| USER32 | LoadStringA | 0x48b3a8 | 1 | 0 | wrapper 0x4047e8 (~190 callers) |
| USER32 | MessageBeep | 0x48b3ac | 3 | 0 | always 0xFFFFFFFF |
| USER32 | MessageBoxA | 0x48b3b0 | 6 | 1 | |
| USER32 | MsgWaitForMultipleObjects | 0x48b3b4 | 1 | 0 | video loop (out of scope) |
| USER32 | PeekMessageA | 0x48b3b8 | 10 | 0 | |
| USER32 | PostMessageA | 0x48b3bc | 0 | 1 | dead |
| USER32 | PostQuitMessage | 0x48b3c0 | 2 | 0 | error/teardown only |
| USER32 | RegisterClassA | 0x48b3c4 | 1 | 2 | |
| USER32 | ScreenToClient | 0x48b3c8 | 1 | 0 | WM_MOUSEMOVE handler |
| USER32 | SendMessageA | 0x48b3cc | 49 | 0 | LB_* to list boxes, one WM_SETREDRAW |
| USER32 | SetCursor | 0x48b3d0 | 2 | 0 | hourglass on/off |
| USER32 | SetCursorPos | 0x48b3d4 | 0 | 1 | dead |
| USER32 | SetDlgItemTextA | 0x48b3d8 | 26 | 0 | |
| USER32 | SetMenu | 0x48b3dc | 0 | 2 | dead |
| USER32 | SetScrollInfo | 0x48b3e0 | 2* | 4 | *0x43450d/0x434553, unreachable with live arguments |
| USER32 | SetSysColors | 0x48b3e4 | 0 | 0 | **never called** (NOP-patched) |
| USER32 | SetTimer | 0x48b3e8 | 1 | 1 | |
| USER32 | ShowCursor | 0x48b3ec | 3 | 0 | |
| USER32 | ShowWindow | 0x48b3f0 | 1 | 2 | |
| USER32 | TrackPopupMenu | 0x48b3f4 | 1 | 0 | |
| USER32 | TranslateMessage | 0x48b3f8 | 7 | 0 | |
| USER32 | UpdateWindow | 0x48b3fc | 1 | 2 | |
| USER32.DLL (Watcom RTL) | GetActiveWindow | 0x48b410 | 1 | 0 | via thunk 0x44cee8 |
| USER32.DLL (Watcom RTL) | wsprintfA | 0x48b414 | 13 | 5 | 11 direct + 2 via thunk 0x44ceee |
| comdlg32 | GetOpenFileNameA | 0x48b59c | 1 | 0 | via thunk 0x44cf60 |

VERIFIED: apart from the three noted thunks, every call goes through `call dword ptr cs:[slot]` (`2E FF 15`).
The jump-thunk table at `0x44cc0c..0x44ce46` (`jmp [slot]`) has no callers, except the Watcom thunks
`0x44cee8`, `0x44ceee` and `0x44cf60`.

---

## 3. Calling conventions

| Item | Convention | Evidence |
|---|---|---|
| USER32/GDI32/comdlg32 imports | `__stdcall`, args pushed right-to-left, callee pops | all sites (VERIFIED) |
| `wsprintfA` | `__cdecl` varargs; **caller pops** (`add esp,8/0xc/0x14` after every call) | e.g. 0x433b77, 0x434abf, 0x44803a (VERIFIED) |
| WndProc `0x4343f1` | `__stdcall(HWND,UINT,WPARAM,LPARAM)`, `ret 0x10`; preserves ebx/esi/edi/ebp | 0x434450 (VERIFIED) |
| All 18 DLGPROCs | `__stdcall(HWND,UINT,WPARAM,LPARAM)`, `ret 0x10`; preserve ebx/esi/edi/ebp | e.g. 0x40250d, 0x40d9e0 (VERIFIED) |
| Internal WndProc message handlers | **Watcom register ABI**: `eax=hwnd, edx=wParam, ebx=lParam, ecx=msg*4`; return in eax (non-zero means handled and is the LRESULT; 0 means DefWindowProcA) | 0x434419..0x434427 (VERIFIED) |

Host-to-guest callbacks are WndProc, DLGPROC and nothing else. There is no TimerProc
(`SetTimer(...,NULL)`) and no enum callbacks in this scope. Every guest callback begins with the Watcom stack
check `push N; call 0x43371d`. That check compares `esp-N` with the per-thread stack low-water mark returned
by `call [0x4520f0]` (VERIFIED, 0x43372d..0x433744). Host callbacks must therefore run on the guest main
thread stack, or on a stack the Watcom RTL knows about (INFERRED).

---

## 4. Window class, creation and start-up order

### 4.1 Start-up sequence (VERIFIED)

`WinMain` = `0x40399d` (`ret 0x10`; `hInstance=[esp+0x18]`, `nCmdShow=[esp+0x24]`). It stores
`hInstance` → `[0x455618]` and the returned hwnd → `[0x455614]`. It calls `0x401010(eax=hInstance, edx=nCmdShow)`:

| Address | Call | Effect in scope |
|---|---|---|
| 0x401055 | `0x4337dc` | clears handler table `0x486070` (0x2004 bytes), fills WNDCLASS `0x486048`, LoadIconA, LoadCursorA |
| 0x401085 | `0x435c84` | input object `[0x45501c]`; registers WM_MOUSEMOVE/LBUTTONDOWN/LBUTTONDBLCLK/RBUTTONDOWN/KEYDOWN handlers |
| 0x401149 | `0x433f2e(eax=hInst, edx=nCmdShow, ebx="WET THE SEXY EMPIRE ", ecx=title; stack 640, 480, 16)` | RegisterClassA, CreateWindowExA, SetTimer, ShowWindow, UpdateWindow, DirectDraw init, registers WM_DESTROY→0x434562 |
| 0x401182 | `0x40171a` | registers WM_ACTIVATEAPP→0x40a174 and WM_DESTROY→0x40a15c (replaces 0x434562) |
| 0x4011db | `0x435f26` | `ShowCursor(FALSE)` (system cursor hidden; the game draws its own) |
| 0x401229 | `0x4020e0(hwnd)` | CD check; may show `CD_NOT_FOUND_DLG` |
| 0x40271e (via 0x401535) | | GetSysColor ×5 (SetSysColors removed) |
| 0x43a9c8 (via 0x4337dc) | | `CreateFontIndirectA` → `[0x48a680]` |

### 4.2 WNDCLASSA at 0x486048 (VERIFIED, written by 0x4337dc and 0x433f2e)

| Field | Value | Source |
|---|---|---|
| style | `0x0B` = `CS_VREDRAW\|CS_HREDRAW\|CS_DBLCLKS` | 0x433869 |
| lpfnWndProc | `0x4343f1` | 0x433873 |
| cbClsExtra / cbWndExtra | 0 / 0 | 0x43387f/0x433887 |
| hInstance | WinMain hInstance | 0x433f74 |
| hIcon | `LoadIconA(NULL, IDI_APPLICATION=0x7F00)` | 0x4338a4 |
| hCursor | `LoadCursorA(NULL, IDC_ARROW=0x7F00)` | 0x4338b6 |
| hbrBackground | 0 (no erase) | 0x4338c4, 0x433f7c |
| lpszMenuName | `0x485ea0` (= the class name string) | 0x433892 |
| lpszClassName | `0x485ea0` = `"WET THE SEXY EMPIRE "` (trailing space; copied from 0x450c78) | 0x433898, 0x433f53 |

The PE has **no RT_MENU resource** (VERIFIED, resource directory). The class menu name therefore resolves to
nothing, and the window has no menu bar (INFERRED). The PE icon group `"WET"` exists but is not loaded by
code, because `IDI_APPLICATION` is used instead.

### 4.3 CreateWindowExA (0x433fb3, VERIFIED)

```
CreateWindowExA(dwExStyle = 0x00040000 /*WS_EX_APPWINDOW*/,
                lpClassName = 0x485ea0 "WET THE SEXY EMPIRE ",
                lpWindowName = 0x485ef0 "WET THE SEXY EMPIRE (C) NEW GENERATION 1996",
                dwStyle = 0x80080000 /*WS_POPUP|WS_SYSMENU*/,
                X = 0, Y = 0, nWidth = 640, nHeight = 480,
                hWndParent = NULL, hMenu = NULL, hInstance, lpParam = NULL)
```

The hwnd is stored at `[0x488074]` (window module) and `[0x455614]` (game). After creation:

* `SetTimer(hwnd, 1, 60, NULL)` at 0x433fca. If it returns 0, the code calls `DestroyWindow(hwnd)` (0x434024)
  and sets the error code `[0x455030]=0x14`, which is fatal. **The host's SetTimer must succeed.**
* `ShowWindow(hwnd, nCmdShow)` at 0x433fe2 and `UpdateWindow(hwnd)` at 0x433ff0.
* On failure `wsprintfA(0x485f40, "Main Window Init Failed \n")` (0x43400c) is followed by the error box (§13).
* `0x43393d(640,480,1)` is then called. It is DirectDraw primary-surface creation. With `ebx=1` it takes the
  full-screen path, which sets `[0x4880b4]=[0x4880b8]=0` (the client origin). The windowed path
  (`ebx=0`: ClientToScreen 0x433aa0 and the scroll setup 0x434453) is only used by the dead variant 0x433bfc
  (VERIFIED: the only live caller 0x434085 passes `ebx=1`; 0x43426f and 0x44bf68 have no references).

Dead window variants (VERIFIED unreferenced): `0x433bfc` (style 0x00FF0000, size from
`GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`, LoadMenuA/SetMenu, `SetTimer(hwnd,1,1,NULL)`, registers
WM_PAINT/WM_SIZE/WM_HSCROLL/WM_VSCROLL/WM_DESTROY, `PostMessageA(hwnd, WM_PAINT, 0, 0)`). `0x433e0a`
(`GetStockObject(BLACK_BRUSH=4)` background, the same size logic and LoadMenuA/SetMenu).

---

## 5. WndProc 0x4343f1

### 5.1 Mechanics (VERIFIED, 0x4343f1..0x434450)

```c
LRESULT __stdcall WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT r = 0;
    if (msg < 0x801 && table[msg] /* dword at 0x486070 + msg*4 */)
        r = table[msg](eax=h, edx=wp, ebx=lp, ecx=msg*4);   // Watcom register call
    if (r == 0) r = DefWindowProcA(h, msg, wp, lp);
    return r;
}
```

* Registration helper `0x4343a2(eax=&{msg, fn})`: `msg < 0x800` writes `table[msg]`; `msg == 0x8000` writes
  `[0x488070]`. Getter `0x4343d5(eax=msg)`.
* `[0x488070]` is the slot for `msg==0x8000` (WM_APP), but the dispatcher indexes it as `msg==0x800`. It is
  never registered.
* Handler table storage: `0x486070..0x48806f` (0x800 dwords) plus `0x488070`. It is cleared at start-up by
  `memset(0x486070, 0, 0x2004)` (0x433864).

### 5.2 Hazard: slot 0x7F7 holds `Sleep` (VERIFIED)

`0x4340b3` (DirectDraw init, repack-patched) does
`[0x48804c] = GetProcAddress(LoadLibraryA("KERNEL32.DLL"), "Sleep")` (0x434133..0x43414a). It uses the strings
at 0x44cf86 and 0x44cf80. `0x48804c` = `0x486070 + 0x7F7*4`, so a message `0x7F7` reaching the WndProc would
call `Sleep` through the register ABI and corrupt the stack. **The host must never deliver message 0x7F7 to
this window** (INFERRED requirement). Patch users: `0x434163` (`pushad; Sleep(32); popad`, reached from the
Flip routine at 0x43d776) and `0x434156` (`Sleep(0)`, from the video module 0x43a19d).

### 5.3 Messages handled (all live registrations, VERIFIED)

| Msg | Name | Handler | Registered at | Behaviour | Returns |
|---|---|---|---|---|---|
| 0x0002 | WM_DESTROY | 0x434562 | 0x43409b | (replaced before any WM_DESTROY can occur) release DDraw, DestroyMenu, KillTimer(h,1), DestroyWindow, PostQuitMessage(0) | 1 |
| 0x0002 | WM_DESTROY | **0x40a15c** | 0x40174e (final) | `[0x4555e4]=1` (leave current game loop; see §10) | 1 |
| 0x001C | WM_ACTIVATEAPP | 0x40a174 | 0x401738 | `wParam==1`: `0x43daf1` (FlipToGDISurface/surface restore), `[0x45dbe0]=1` (game active), `[0x4555ec]=1`, `0x4166c5([0x45dbd8],1)`. Otherwise: `[0x45dbe0]=0`, `0x437c60` (sound stop), `0x4166c5([0x45dbd8],0)` | 0 (DefWindowProc also runs) |
| 0x0100 | WM_KEYDOWN | 0x436361 | 0x435da4 | see §6.2 | 0 |
| 0x0111 | WM_COMMAND | 0x43918f | 0x438d8b (temporary) | only while the sound popup menu is open: `id in [100..115]` → `[0x48a512]=id-100` | 1 |
| 0x0200 | WM_MOUSEMOVE | 0x4361c7 | 0x435d40 | see §6.1 | 0 |
| 0x0201 | WM_LBUTTONDOWN | 0x436304 | 0x435d59 | `[0x488b50]=1` | 0 |
| 0x0203 | WM_LBUTTONDBLCLK | 0x4362ed | 0x435d72 | `[0x488b5c]=1` | 0 |
| 0x0204 | WM_RBUTTONDOWN | 0x436336 | 0x435d8b | `[0x488b58]=1` | 0 |
| 0x03B9 | MM_MCINOTIFY | 0x4439b3 / 0x443f8f | 0x4436b8 / 0x443d1d | MCI module (out of scope; addresses only) | – |

At shutdown, `0x435db5` (from 0x40197a) unregisters 0x200, 0x201, 0x203, 0x202, 0x204, 0x205, 0x100 and 0x101,
and calls `ShowCursor(TRUE)` if the cursor is hidden. The handlers `0x43631b` (clears LMB and drag) and
`0x43634d` (clears RMB, LMB and drag) look like WM_LBUTTONUP and WM_RBUTTONUP handlers, but they are **never
registered** (VERIFIED: no references).

**Not handled. These go to DefWindowProcA (INFERRED host requirements):** WM_TIMER (no TimerProc, so nothing
happens), WM_PAINT (validate only; the class has no background brush), WM_CLOSE (DefWindowProc calls
DestroyWindow, which sends WM_DESTROY to 0x40a15c), WM_SETCURSOR (class cursor IDC_ARROW, invisible while the
ShowCursor count is below 0), WM_CHAR, WM_SYSKEYDOWN/UP, WM_KEYUP, WM_xBUTTONUP, WM_RBUTTONDBLCLK, WM_SYSCOMMAND,
WM_NC*, WM_CREATE and WM_ERASEBKGND. The host DefWindowProcA must at least implement WM_CLOSE → DestroyWindow
and WM_SYSCOMMAND/SC_CLOSE → WM_CLOSE. It should **ignore SC_KEYMENU** (Alt or F10) instead of entering a menu
loop. The window has no menu, so a modal menu loop would only freeze the game.

No `WM_USER`-range messages are used. `PostMessageA` is dead and the only live `SendMessageA` targets are
dialog controls (§12) (VERIFIED).

---

## 6. Input latches and snapshots

### 6.1 Mouse (VERIFIED, handler 0x4361c7; globals 0x488b34..0x488bc0)

Raw state (written by the handlers):

| Addr | Meaning |
|---|---|
| 0x488b34 | re-entrancy guard for WM_MOUSEMOVE |
| 0x488b3c / 0x488b40 | mouse x / y (int, sign-extended from LOWORD/HIWORD(lParam)), then adjusted (below) |
| 0x488b44 / 0x488b48 | drag start x / y (-1 = none) |
| 0x488b4c | dragging flag (1 after the 2nd move with exactly one button held) |
| 0x488b50 | LMB-pressed latch (WM_LBUTTONDOWN) |
| 0x488b54 | cleared only (never set by any handler) |
| 0x488b58 | RMB-pressed latch (WM_RBUTTONDOWN) |
| 0x488b5c | LMB double-click latch (WM_LBUTTONDBLCLK) |
| 0x488bc0 | move counter while a button is held (saturates at 2) |

WM_MOUSEMOVE logic:

```c
if (busy) return 0; busy = 1;
held = (wParam == MK_LBUTTON(1) || wParam == MK_RBUTTON(2) || wParam == MK_MBUTTON(0x10));
if (!held) { cnt=0; dragging=0; dragx=dragy=-1; lclick=0; [0x488b54]=0; rclick=0; }
x = (short)LOWORD(lParam); y = (short)HIWORD(lParam);
if (held) { if (cnt<2) cnt++; if (dragx==-1){dragx=x;dragy=y;} if (cnt==2) dragging=1; }
0x43de23(&ox,&oy);                 // viewport scroll offset of the draw context [0x48a644]+0x40/+0x44
POINT p={0,0}; ScreenToClient(hwnd,&p);   // VERIFIED 0x4362b5
x -= (short)ox + p.x;  y -= (short)oy + p.y;
busy = 0; return 0;
```

Host consequences (INFERRED):

* **MK_ flags must be exact.** Any modifier (`MK_SHIFT`, `MK_CONTROL`) or two held buttons clears the click
  latches. A click (down, then up) followed by a no-button WM_MOUSEMOVE before the next snapshot is **lost**.
  This is original behaviour. Do not synthesise extra WM_MOUSEMOVE events: post them only on real motion, and
  coalesce them as Windows does (only the last pending move survives).
* **Double click:** because of `CS_DBLCLKS`, a second left press within the double-click time (500 ms) and
  distance (4 px, `SM_CXDOUBLECLK`/`SM_CYDOUBLECLK`) must be delivered as `WM_LBUTTONDBLCLK (0x203)`
  **instead of** WM_LBUTTONDOWN. A right double click becomes WM_RBUTTONDBLCLK (0x206), which is not handled.
* **Coordinates:** keep the virtual window at screen (0,0) and make `ScreenToClient` and `ClientToScreen`
  identity transforms. lParam must be in 640×480 game coordinates. Scale SDL mouse coordinates before posting.

### 6.2 Keyboard (VERIFIED, handler 0x436361; globals 0x488b80..0x488b8c)

```c
if (!busy2 /*0x488b38*/) { busy2=1;
  [0x488b80] = wParam;                                    // virtual-key code (last one wins)
  if (GetAsyncKeyState(VK_CONTROL=0x11) & 0x8000) [0x488b8c]=1;   // 0x436387
  if (GetAsyncKeyState(VK_SHIFT  =0x10) & 0x8000) [0x488b88]=1;   // 0x4363a2
  if (GetAsyncKeyState(VK_MENU   =0x12) & 0x8000) [0x488b84]=1;   // 0x4363bd
  busy2=0; }
return 0;
```

Only bit 15 of GetAsyncKeyState is tested (`movsx edx,ax; test dh,0x80`). The modifier snapshots
(`0x455388/0x45538c/0x455390`) are **never read** by game code (VERIFIED: the only references are the clear at
0x40281e and the copy at 0x4058a9). Only WM_KEYDOWN is handled. To match Windows, the host should send
Alt+key and F10 as WM_SYSKEYDOWN, which the game ignores. Auto-repeat WM_KEYDOWN events can be passed through.

Virtual-key codes the game compares against the snapshot `[0x455384]` (VERIFIED):

| VK | Where | Meaning (INFERRED) |
|---|---|---|
| 0x0D Enter, 0x1B Esc, 0x20 Space | 0x404dc7.., 0x404ea0.. | skip waits/slideshows |
| 0x1B Esc | 0x410fe9 | cancel |
| 0x70 F1 | 0x409550 → 0x4095e0 | help (draws DATA\PERSO\HLP.DAT) |
| 0x71 F2 | 0x4094e5 → 0x410bc9 | options/menu |
| 0x56 'V', 0x76 F7 | 0x40954b, 0x4094f5 | `MessageBeep(-1)` and toggle `[0x45dc04]` (version overlay) |
| 0x77 F8 | 0x4094f7 → 0x409f76 | (game function) |

### 6.3 Snapshot (VERIFIED, 0x405862)

After each queue drain, `0x43619e` copies 16 bytes `0x488b80` → `0x455384` (vk, alt, shift, ctrl) and clears the
source. `0x436156` copies 0x24 bytes `0x488b3c` → `0x455394` (x, y, dragx, dragy, dragging, lclick, ?, rclick,
dblclick) and clears `0x488b50`, `0x488b54`, `0x488b58` and `0x488b5c`. Game code reads `0x455394` (x),
`0x455398` (y), `0x4553a8` (lclick) and `0x4553b0` (rclick) directly. It also passes the struct pointer
`0x455394` to 16 sites.

---

## 7. Message loops and frame pacing

### 7.1 Every message-retrieval site (VERIFIED)

| Site | Function | Call | Purpose |
|---|---|---|---|
| 0x404168 | 0x40413f main tick (called in `while(![0x4555e4])` at 0x403b80) | `PeekMessageA(&m, hwndMain, WM_TIMER, WM_TIMER, PM_REMOVE)` → Translate/Dispatch → if `[0x45dbe0]==1` call `0x4050c1` (game logic tick) | logic paced by WM_TIMER |
| 0x405883 | 0x405862 drain+snapshot | `while (PeekMessageA(&m, hwndMain, 0, 0, PM_REMOVE)) {Translate; Dispatch;}` then snapshot (§6.3) | input |
| 0x404e84 | 0x404e47 wait(n, abortable) | `PeekMessageA(&m, hwndMain, WM_TIMER, WM_TIMER, PM_NOREMOVE)` → `n--` if one is pending, then 0x405862. If `edx==1`, abort on vk 0x0D/0x1B/0x20 or a click | timed waits (17 callers), no rendering |
| 0x404db0 | 0x404cdb text slideshow | same NOREMOVE count (80 ticks/page) + 0x405862 + text render + flip | |
| 0x416556, 0x416587 | 0x416534 (pump helper called from 0x4156ea and 0x41603c) | `PeekMessageA(&m, NULL, WM_TIMER, WM_TIMER, PM_REMOVE)` → dispatch → `0x416347`; then drain `PeekMessageA(&m, NULL,0,0,PM_REMOVE)` (no hwnd filter) | timer-driven helper |
| 0x41cda8 | 0x41c9ad (loop 0x41cd84) | like 0x40413f with tick `0x41a95e` | sub-game loop |
| 0x435ed0 | 0x435e8f software-cursor draw | `PeekMessageA(&m, [0x488baa]=hwndMain, WM_TIMER, WM_TIMER, PM_NOREMOVE)`: a pending timer advances the cursor animation countdown | cursor animation |
| 0x438e6b, 0x438e81 | 0x438d56 popup menu | `while (PeekMessageA(&m,NULL,0,0,PM_NOREMOVE)) if (GetMessageA(&m,NULL,0,0)) {Translate;Dispatch;}` | drains the WM_COMMAND from TrackPopupMenu |
| 0x43a799, 0x43a7bd | 0x43a749 | MsgWaitForMultipleObjects + PeekMessage PM_REMOVE + `GetAsyncKeyState(VK_ESCAPE)` | **video playback, out of scope** |

Busy waits: `while ([0x4553a8]==1) 0x405862();` at 0x4043b6 and 0x416a8d. These require a non-blocking,
cheap PeekMessageA (VERIFIED).

### 7.2 Required WM_TIMER semantics (INFERRED from the above)

* Timer id 1, period 60 ms, posted to hwndMain, `lParam=0`, `wParam=1`. It is **never killed** in normal
  operation. KillTimer appears only in the error and teardown paths 0x43436f and 0x434592 (and 0x433907, which
  is unreachable).
* A timer is "ready" when its period has elapsed. A ready timer makes `PeekMessageA` with a filter that includes
  0x113 report a `WM_TIMER`, both with `PM_NOREMOVE` and `PM_REMOVE`. Removal (or GetMessage) clears ready. At
  most one WM_TIMER per timer is pending; missed periods are coalesced. Real Windows synthesises WM_TIMER only
  after posted and input messages. The host should report it last when no filter is given.
* An hwnd filter must be honoured. `PeekMessageA(hwndMain, …)` must not return thread messages (`hwnd==NULL`,
  for example WM_QUIT) or messages for dialog windows.

### 7.3 Frame pacing (VERIFIED code paths; resulting rates INFERRED)

* **Logic:** one `0x4050c1` tick per retrieved WM_TIMER, so at most one per 60 ms (about 16.7 Hz; Win9x
  timer granularity gives 55–60 ms).
* **Rendering:** every iteration of the main loop while active runs `0x4094d4` (hotkeys), UI and draw code, then
  `0x403d0a` (HUD text, software cursor and `0x43d646` Flip with `DDFLIP_WAIT`). After each Flip, `Sleep(32)`
  runs via the repack patch (0x43d776 → 0x434163). Rendering is therefore capped at roughly 25–30 fps plus
  vsync.
* **Inactive** (`[0x45dbe0]==0` after WM_ACTIVATEAPP(0)): the main loop only peeks and drains, so it spins at
  100 % CPU. The host may sleep about 1–10 ms inside PeekMessageA when the queue is empty and no timer is ready.
  This preserves behaviour.
* There is no explicit `GetTickCount`/`timeGetTime` pacing in these loops; KERNEL32 and WINMM timing is outside
  this scope.

### 7.4 WM_QUIT

`PostQuitMessage(0)` is called only after a fatal error box (0x434385) and in the replaced WM_DESTROY handler
(0x4345a9). No live code checks for WM_QUIT. The hwnd-filtered pumps cannot see it, the NULL-filter pumps
(0x416587, 0x438e81) dispatch it or ignore it, and the GetMessage loop simply continues (VERIFIED, 0x438e88).
**Host recommendation:** treat `PostQuitMessage` after the main window was destroyed as a request to terminate
the process.

---

## 8. Timers

| Site | Call | Status |
|---|---|---|
| 0x433fca | `SetTimer(hwndMain, 1, 60, NULL)` | live |
| 0x433cd0 | `SetTimer(hwnd, 1, 1, NULL)` | dead (0x433bfc) |
| 0x43436f | `KillTimer(hwndMain, 1)` | live, after fatal error box OK |
| 0x434592 | `KillTimer(hwndMain, 1)` | handler replaced before use (practically dead) |
| 0x433907 | `KillTimer(hwndMain, 1)` | unreachable (`jmp 0x43391c` at 0x433902) |

WM_TIMER has no WndProc handler (§5.3). Its only consumers are the PeekMessage filters in §7.1.

---

## 9. Cursor

| Site | Call | Notes (VERIFIED) |
|---|---|---|
| 0x4338b6 | `LoadCursorA(NULL, IDC_ARROW 0x7F00)` | class cursor |
| 0x403b04 | `LoadCursorA(NULL, IDC_WAIT 0x7F02)` → esi | WinMain |
| 0x403bcf | `SetCursor(hWait)` → saved prev in edi | when a game session ends (`[0x4555e4]!=0`), around the reset `0x4015b9` |
| 0x403c36 | `SetCursor(prev)` | restore |
| 0x435dd6 / 0x435f04 | `ShowCursor(TRUE)` | `0x435db5` shutdown / `0x435ee9` "show system cursor" (guarded by `[obj+8]==0`) |
| 0x435f41 | `ShowCursor(FALSE)` | `0x435f26` "hide system cursor" (guarded by `[obj+8]==1`) |
| 0x4362b5 | `ScreenToClient(hwnd, &{0,0})` | mouse-move correction (§6.1) |
| 0x435f69/0x435f8a | ClientToScreen + `SetCursorPos` | **dead** (0x435f4b unreferenced) |

ShowCursor state machine (VERIFIED): the input object starts with `[obj+8]=1` ("visible", 0x435d25). Start-up
calls 0x435f26, so the count becomes -1 and the cursor is hidden. Message boxes and dialogs are wrapped in
`0x435f0e` (disable software cursor) and `0x435ee9` (`ShowCursor(TRUE)`, count 0), then `0x435f26`
(count -1) and `0x435e78` (re-enable software cursor). The guard keeps the count at 0 or -1. The host cursor
is visible iff the count is ≥ 0 (INFERRED). The in-game cursor is a sprite drawn by `0x43b53c`
(DATA\CURSOR\CURSOR.TAF) and is not a Win32 cursor.

Strings decoded in 0x435c84 (VERIFIED, bytes minus 0x43): `"NGS-REVEAL"` (checked with `0x443135`, probably
getenv) and `"CURSOR_CLASS_(C)_NEW_GENERATION_SOFTWARE"` (printed when the variable is set).

---

## 10. Activation, close and exit

* **WM_ACTIVATEAPP** (§5.3): the host should send `WM_ACTIVATEAPP(1)` on SDL focus gain and `(0)` on focus
  loss, as a **sent** message (delivered synchronously while the guest is inside PeekMessageA, GetMessageA or
  the next USER32 call; it is not returned by PeekMessage). Before the handler is registered (during
  ShowWindow), activation goes to DefWindowProc, and the game sets `[0x45dbe0]=1` itself (0x403a18).
* **Close:** SDL_QUIT → post `WM_CLOSE` → DefWindowProc → `DestroyWindow` → `WM_DESTROY` → `0x40a15c` sets
  `[0x4555e4]=1`. In the original, this ends the **current game session**: WinMain's loop exits to
  0x403b90 (reset, then the main menu again via `0x40fe68`). It does not end the process (VERIFIED,
  0x403b73..0x403c9b). The game then runs with a destroyed hwnd. **Host recommendation:** on a user close
  request, exit the process directly, or deliver WM_CLOSE and terminate once WinMain's main-menu loop sees
  `[0x4555e4]`. The "quit" item in the game menu sets `[0x4555e4]` in `0x40fe68` (0x40ff7c), and WinMain then
  returns (0x403ca0).
* **Normal exit** (`0x401757`): DestroyMenu (only if `[0x488078]`, never set), DeleteObject(font) (0x43ab78) and
  `ShowCursor(TRUE)`. The window is **not** destroyed explicitly; process exit does that (VERIFIED: the
  KillTimer and DestroyWindow calls in 0x4338d3 are skipped by the jmp at 0x433902).

---

## 11. Menus: one runtime popup, not a debug menu

`0x438d56` is the "Set Digital Output" popup. It is called from the in-game options screen (`0x40d123`
command 7, 0x40d340) and lets the user pick the DirectSound output format. It is a real user feature, not a
debug menu (VERIFIED):

```
save = handler(WM_COMMAND); handler(WM_COMMAND) = 0x43918f;
m = CreatePopupMenu();                                   // 0x438d90
AppendMenuA(m, MF_SEPARATOR 0x800, 0, NULL);             // 0x438da5
AppendMenuA(m, MF_DISABLED 0x2, 0, "      Set Digital Output");   // 0x438db6 (0x44f4cf)
AppendMenuA(m, MF_SEPARATOR, 0, NULL);
for i in 0..15: if supported[i] (word 0x48a4f0+2i):
    AppendMenuA(m, i==current(word 0x48a510) ? MF_CHECKED 0x8 : MF_STRING 0, 100+i, fmtname[i]);  // 0x438e00
AppendMenuA(m, MF_SEPARATOR, 0, NULL);
AppendMenuA(m, MF_STRING, 2, "            CANCEL");     // 0x438e30 (0x44f4e8)
AppendMenuA(m, MF_SEPARATOR, 0, NULL);
TrackPopupMenu(m, 0 /*TPM_LEFTALIGN|TPM_TOPALIGN|TPM_LEFTBUTTON*/, 100, 40, 0, hwndMain([sound+2]), NULL); // 0x438e57
while (PeekMessageA(&msg,NULL,0,0,PM_NOREMOVE)) if (GetMessageA(&msg,NULL,0,0)) {TranslateMessage;DispatchMessageA;}
handler(WM_COMMAND) = save; DestroyMenu(m);              // 0x438eb8
apply(word 0x48a512);                                    // 0x438f62
```

The format names in the table at `0x45201c` are `"8.000  kHz,   8-Bit, Mono"` … `"44.100 kHz, 16-Bit, Stereo"`
(16 entries, VERIFIED). `[0x45205c]` holds `"No Sound Available"`.

Host requirement: TrackPopupMenu at **screen** (100,40). It is modal. On selection it **posts**
`WM_COMMAND(MAKEWPARAM(id,0), 0)` to the owner, and the code drains that afterwards. A command id of 2 or
cancelling leaves `[0x48a512]` unchanged. LoadMenuA and SetMenu are dead and the PE has no RT_MENU.

---

## 12. Dialogs

### 12.1 DialogBoxParamA sites (VERIFIED)

`hInstance` is pushed as 0 or as a register that is 0 on the normal path. Host: **ignore hInstance and always
load templates from the EXE**. The owner is `[0x455614]` (main hwnd) everywhere, except 0x402216 where it is
the hwnd argument, which is the same window. `lParam` is always 0 (or a register that is 0). No DLGPROC was
seen reading the WM_INITDIALOG lParam (INFERRED).

| Site | Caller fn | Template | DLGPROC | Caller tests result |
|---|---|---|---|---|
| 0x402216 | 0x4020e0 (start-up CD check) | CD_NOT_FOUND_DLG (0x44d5d4) | 0x402288 | (re-checks error code) |
| 0x40d516 (path 0x40d501) | 0x40d123 options cmd 1 | GAME_IO_DLG (0x44dee6) | 0x40d9e3 (load) | – |
| 0x40d516 (path 0x40d1ed) | 0x40d123 options cmd 2 | GAME_IO_DLG (0x44def2) | 0x40d611 (save) | – |
| 0x40d6d7 | inside DLGPROC 0x40d611 (nested) | STANDARD_GET_TXT_DLG (0x44defe) | 0x40deab | `==1` |
| 0x40e08e | 0x40dfd8 | STANDARD_LIST_BOX_DLG (0x44e00c) | 0x40e19b | `==1` |
| 0x40facb | 0x40f9f4 | STANDARD_GET_TXT_DLG (0x44e0c4) | 0x40fb4d | – |
| 0x40fee6 | 0x40feac (main menu "load") | GAME_IO_DLG (0x44e10d) | 0x40d9e3 | `==2` (cancel) |
| 0x411442 | 0x4113b9 | STANDARD_LIST_BOX_DLG (0x44e1ca) | 0x4116a3 | `==1` |
| 0x4138ff | 0x4137d4 | EINZAHLEN_DLG (0x44e278) | 0x413944 | – |
| 0x4138ff (path 0x413910) | 0x4137d4 | AUSZAHLEN_DLG (0x44e286) | 0x413aec | – |
| 0x41a46d | 0x41a1dd | PERSONALKOSTEN_LIST_BOX_DLG (0x44e4a8) | 0x41d724 | – |
| 0x41e4e8 | 0x41e45a | STANDARD_GET_TXT_DLG (0x44e682) | 0x41e912 | `==1` |
| 0x41e57f | 0x41e45a | STANDARD_LIST_BOX_DLG (0x44e697) | 0x41ecf6 | `==1` |
| 0x41e57f (path 0x41e596) | 0x41e45a | STANDARD_LIST_BOX_DLG (0x44e6ad) | 0x41eaa7 | `==1` |
| 0x41f0d9 | 0x41efd2 | STANDARD_LIST_BOX_DLG (0x44e6c5) | 0x41eaa7 | `==1` |
| 0x41fea9 | 0x41fde8 | STANDARD_LIST_BOX_DLG (0x44e73a) | 0x42036d | – |
| 0x421f81 | 0x421caf | AUSZAHLEN_DLG (0x44e7ea) | 0x4220fb | – |
| 0x421f81 (path 0x421fa3) | 0x421caf | EINZAHLEN_DLG (0x44e7f8) | 0x4222a7 | – |
| 0x423674 (paths 0x423660, 0x423692) | 0x423448 | STANDARD_LIST_BOX_DLG (0x44e89b/0x44e8b1) | 0x4248dc | – |
| 0x429eb9 | 0x429d58 | WORK_ORDER_DLG (0x44eb32) | 0x42a720 | – |

These templates are never referenced: `DEBUG_EDIT_EQUIP_DLG`, `DEBUG_EDIT_PERSO_DLG`, `DEBUG_PERSO_DLG`,
`DEBUG_PORTRAIT_DLG` and `MAKLER_IMMO_DLG`. No string in the EXE names them (VERIFIED). They are the remains of
the debug/editor UI. Appendix B parses all 13 templates (class, id, text, style, DLU rect).

### 12.2 Common DLGPROC protocol (VERIFIED across all 18 procs)

* Around a dialog, the caller or the WM_INITDIALOG handler calls `0x403f09`. That sets `[0x4555e0]=1` and calls
  `0x43daf1` (`IDirectDraw::FlipToGDISurface`, vtable +0x28, error box "Switch to GDI failed!"). On close the
  code calls `0x404049` (`[0x4555e0]=0`) and often `0x403f22` (redraw both game buffers). While `[0x4555e0]` is
  set, `0x403d0a` skips HUD, cursor and Flip. **The host must composite dialogs over the current GDI/primary
  surface image.**
* **WM_INITDIALOG (0x110):** `CreateSolidBrush(0x007CCAF8)` (RGB 248,202,124, tan) is stored in a global, the
  controls are filled, and the proc returns 1 (focus to the first tabstop). Exception: 0x402288 returns 0.
* **WM_CTLCOLOR\*:** two patterns:
  * **A** (all procs): `WM_CTLCOLORMSGBOX 0x132` and `WM_CTLCOLORSTATIC 0x138` → `SetTextColor(hdc, 0)`,
    `SetBkMode(hdc, TRANSPARENT)`, return the brush. `WM_CTLCOLORDLG 0x136` and `WM_CTLCOLORSCROLLBAR 0x137` →
    return the brush. `0x133` (EDIT) and `0x135` (BTN) → 0 (system default).
  * **+LB:** additionally `WM_CTLCOLORLISTBOX 0x134` is treated like STATIC in 0x402288, 0x40deab, 0x40fb4d,
    0x413944, 0x413aec, 0x41e912, 0x4220fb and 0x4222a7.
  * **LB side effect:** in 0x41eaa7 (0x41ec60) and 0x42036d (0x42052f), `WM_CTLCOLORLISTBOX` sends
    `LB_GETCURSEL` to list 111 and does `SetDlgItemTextA(hDlg, 115, LoadString(910 "In planning" / 911 "In
    production"))`. It returns 0. This updates the status label on selection changes without LBS_NOTIFY.
    **The host must send WM_CTLCOLORLISTBOX each time a list box repaints, including after every selection
    change.**
* **WM_PAINT (0xF):** in 0x40e19b, 0x4116a3, 0x413944, 0x413aec, 0x41d724, 0x41e912, 0x41eaa7, 0x41ecf6,
  0x4220fb, 0x4222a7 and 0x4248dc, the proc only calls `BeginPaint(hDlg,&ps)` and `EndPaint(hDlg,&ps)` and
  returns 0 (return value verified for 0x40e19b and 0x413944). Host BeginPaint must send WM_ERASEBKGND, painting with the WM_CTLCOLORDLG brush, and validate.
* **WM_COMMAND (0x111):** `LOWORD(wParam)` = control id. Ids 1 (IDOK) and 2 (IDCANCEL) are handled; the proc
  returns 1. The host's dialog keyboard handling (Enter → IDOK via DM_GETDEFID=1, Esc → IDCANCEL) must send
  these as WM_COMMAND.
* **Brush lifetime quirk (VERIFIED):** 0x40d611 (save) and its nested 0x40deab share global `[0x45e0bc]`. The
  nested dialog overwrites it, deletes it on close, and the outer dialog then keeps returning and finally
  deletes the stale handle again (0x40d802). **Host brushes should be never-freed records**, so that stale
  handles still paint tan, and DeleteObject on unknown or deleted handles must be harmless.

### 12.3 Per-DLGPROC behaviour (VERIFIED; ids are decimal control ids)

| DLGPROC | Template | WM_INITDIALOG | WM_COMMAND | EndDialog |
|---|---|---|---|---|
| 0x402288 | CD_NOT_FOUND | 0x403f09; brush `[0x45d960]`; returns 0 | 1 "Retry": re-test `<CD>\DATA\CURSOR\CURSOR.TAF` → on success EndDialog. 3 "End Program": error 0x14 + detail "CD NOT FOUND!" → caller shows error and exits (`0x401757`, `exit(-1)`). 150 "Browse": `0x402510` GetOpenFileNameA → writes CDROM.LOC → EndDialog; on cancel `0x403f22` + `SendMessageA(hDlg, WM_SETREDRAW, FALSE, 0)` (0x402485) | 1 |
| 0x40d611 | GAME_IO (save) | 0x403f09; brush `[0x45e0bc]`; list 145: 100× `LB_ADDSTRING` (savegame name or "EMPTY", files `DATA\SAVE\SAVEGAME.%3d`); `LB_SETCURSEL 0` | 1: `LB_GETCURSEL`, `LB_GETTEXTLEN`, `LB_GETTEXT`→0x455628; nested GET_TXT; if 1, write save. 2: close | 0 |
| 0x40d9e3 | GAME_IO (load) | as save, plus `SetDlgItemTextA(1,"Load Game")`, `SetDlgItemTextA(147,"Load Game")`; slots filtered by version float `[0x455624]` | 1: load selected; 2: close | 1 / 2 |
| 0x40deab | GET_TXT | `SetDlgItemTextA(109, 0x455628)`, `SetDlgItemTextA(110,"Savename:")` | 1: `GetDlgItemTextA(109, 0x455628, 80)` | 1 / 0 |
| 0x40e19b | LIST_BOX | list 111: RESETCONTENT, ADDSTRING film names, SETCURSEL 0; 112 ← LS 211, 115 ← LS 212 | 1: `LB_GETCURSEL` → index `[0x45e0c4]` | 1 / 0 |
| 0x40fb4d | GET_TXT | 109 ← 0x45e02c; 110 ← LS 3221 "Title of session:" | 1: `GetDlgItemTextA(109, 0x45e02c, 80)`, truncate to 79 | 1 / 0 |
| 0x4116a3 | LIST_BOX | list fill; 112 ← LS 2204; 115 ← LS 2203 | 1: `LB_GETCURSEL` | 1 / 0 |
| 0x413944 | EINZAHLEN | 107 and 113 ← `"%d"` of `[0x45568c]+[0x455684]` | 1: `GetDlgItemInt(113,&ok,FALSE)`; if ok and equals `0x413df0()`, `[0x45568c]-=v` and close; else stay | 0 |
| 0x413aec | AUSZAHLEN | 106, 114 ← `"%d"` values | 1: `GetDlgItemInt(114,&ok,FALSE)`, same pattern | 0 |
| 0x41d724 | PERSONALKOSTEN | list 111: RESETCONTENT, rows `"%-40s %4d  %6d$"` (LS id from record +0x390) and a `"Total"` row; SETCURSEL 0 | 1 or 2: close | 0 |
| 0x41e912 | GET_TXT | 110 ← LS 647 | 1: `GetDlgItemTextA(109, buf, 99)` | 1 / 0 |
| 0x41eaa7 | LIST_BOX | list fill, `LB_SETCURSEL n`; 112 ← LS 648 | 1: `LB_GETCURSEL` | 1 / 0 |
| 0x41ecf6 | LIST_BOX | 115 ← " "; list fill (LS id from record +0x25c); `LB_SETCURSEL n`; 112 ← LS 649 | 1: `LB_GETCURSEL` | 1 / 0 |
| 0x42036d | LIST_BOX | list fill, SETCURSEL 0 | 1: `LB_GETCURSEL` | 1 / 0 |
| 0x4220fb | AUSZAHLEN | 106, 114 ← `"%d"` | 1: `GetDlgItemInt(114,&ok,FALSE)` | 0 |
| 0x4222a7 | EINZAHLEN | 107, 113 ← `"%d"` | 1: `GetDlgItemInt(113,&ok,FALSE)` | 0 |
| 0x4248dc | LIST_BOX | list fill, `LB_SETCURSEL n`; 112 ← LS 2007; 115 ← LS 911 | 1: `LB_GETCURSEL` | 1 / 0 |
| 0x42a720 | WORK_ORDER | 0x403f09, software cursor off, ShowCursor; list 111 rows `"%s:\t %8ld Stück zu %2d$ \t%s"` (LS 1204+n); SETCURSEL 0 | 148 "Accept": `LB_GETCURSEL`, `LB_DELETESTRING`, `LB_SETCURSEL`. 149 "Remove": `LB_GETCURSEL`, `LB_DELETESTRING`, `LB_SETCURSEL`. 1 or 2: close, ShowCursor(FALSE), software cursor on | 0 |

"LS n" means the game's LoadStringA wrapper with id n (§14). "1 / 0" means IDOK → 1 and IDCANCEL → 0. The
cancel path enters the shared EndDialog tail with the result register still 0 (VERIFIED for 0x40e19b, 0x40deab
and 0x40fb4d; the same code shape in the others).

### 12.4 Control features the host dialog manager must support (VERIFIED from the code and the templates)

* **SendMessageA** (49 live sites): `LB_ADDSTRING 0x180`, `LB_DELETESTRING 0x182`, `LB_RESETCONTENT 0x184`,
  `LB_SETCURSEL 0x186`, `LB_GETCURSEL 0x188` (returns -1 with no selection), `LB_GETTEXT 0x189`,
  `LB_GETTEXTLEN 0x18a`, and `WM_SETREDRAW 0xB` (once, to the CD dialog).
* No list box in a used template has `LBS_SORT`, so LB_ADDSTRING appends. `WORK_ORDER` list style 0x50b000c0
  includes `LBS_USETABSTOPS`, so `'\t'` expands to the default tab stops (every 32 DLU). List boxes do not
  process `&` prefixes.
* `GetDlgItemTextA` (truncate to cchMax-1 and NUL-terminate), `SetDlgItemTextA`, `GetDlgItem`, and
  `GetDlgItemInt(h,id,&ok,FALSE)` (unsigned; `ok=FALSE` on empty or non-numeric input; leading and trailing
  blanks allowed). EDIT controls 113 and 114 have `ES_NUMBER` (0x2000).
* STATIC text processes `&` (no SS_NOPREFIX). BUTTON styles: push (0x50010f00), default push (0x50010f01),
  group box (0x50000107).
* Fonts from the templates: `"Helv"` 8 pt (maps to MS Sans Serif 8: dialog base units ≈ 6×13 px, so 1 DLU ≈
  1.5×1.625 px) and `"Fixedsys"` 9 pt (8×15 cell, 1 DLU = 2×1.875 px) for PERSONALKOSTEN and WORK_ORDER. The
  `"%-40s %4d %6d$"` rows rely on fixed pitch. WORK_ORDER is 320×130 DLU, which is 640×244 px, the full width.
  Dialog x,y are relative to the owner client (0,0); no template uses DS_CENTER.
* The system button colours stay at defaults, because SetSysColors was removed (§16).

### 12.5 GetOpenFileNameA (0x4025e3 via thunk 0x44cf60, in 0x402510, VERIFIED)

`OPENFILENAMEA` on the stack, zeroed: `lStructSize=0x4c`, `hwndOwner=hwndMain`, `lpstrFilter=0x450cc0`
(`"SEARCH FILE\0*.SEA\0\0"`), `lpstrCustomFilter=local copy`, `nMaxCustFilter=50`, `lpstrFile=0x4551d8`
(zeroed first), `nMaxFile=100`,
`Flags=0x201808` (`OFN_LONGNAMES|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR`) and
`lpstrDefExt="*.*"` (0x44d67c). On TRUE, the result is truncated after the last `'\\'`, leaving a directory
with a trailing backslash. That directory becomes the CD path. Host: a native file chooser, a path prompt, or
returning FALSE (the dialog stays open) are all acceptable. The shipped `CDROM.LOC` is empty (0 bytes), so the
CD path is "" and `DATA\CURSOR\CURSOR.TAF` resolves relative to the working directory (INFERRED; file-layer
scope). The dialog only appears if that file cannot be opened.

---

## 13. MessageBoxA

| Site | Function | hwnd | Text | Caption | uType | When (VERIFIED) |
|---|---|---|---|---|---|---|
| 0x404097 | 0x4040ba (shared tail at 0x40408f) | `[0x455614]` | argument (eax) | NULL ("Error") | 0x12010 = MB_ICONHAND\|MB_TASKMODAL\|MB_SETFOREGROUND | `"MAIN Window Creation Failed!"` (0x401525) or `"STRING NOT FOUND! (RECT NAMES)"` (0x404894) |
| (0x404097) | 0x40405e | `[0x455614]` | argument | `"INFORMATION"` | 0x12040 (MB_ICONINFORMATION…) | 0x40405e has **no callers** (dead entry into the same tail) |
| 0x434336 | 0x4342ef | `[0x488074]` | buffer 0x485f40 + `"\n End Program ??"` | `"ERROR"` | 1 = MB_OKCANCEL | when the buffer is non-empty or `[0x455030]!=0` after window or DirectDraw set-up. IDOK(1) → release DDraw, DestroyMenu, KillTimer, DestroyWindow, PostQuitMessage(0) |
| 0x436540 | 0x4364b5 | argument edx | error table string (English table 0x451e0c, selected by `[obj+4]=1`) + `"\n"` + detail | NULL | 0x12010 | generic game error reporter, 7 callers (0x4011f1, 0x401b07, 0x40226f, 0x4042e3, 0x411e04, 0x4151a1, 0x41f353) |
| 0x43d8a0 | 0x43d7c8 | `[[0x48a644]]` | `"PRIMARY SURFACE NOT LOCKED"` | NULL | 0 MB_OK | surface Lock failure |
| 0x43dac1 | 0x43d7c8 | same | `"SECONDARY SURFACE_2 NOT LOCKED"` | NULL | 0 | surface Lock failure |
| 0x43db30 | 0x43daf1 | same | `"Switch to GDI failed!"` | NULL | 0 | FlipToGDISurface failure |
| 0x434bae | 0x434afe | – | `"Direct Draw ERROR!!\n GetGPXModes\n"` | NULL | 0 | **dead** |

English error table at 0x451e0c, codes 1..20 (VERIFIED): 1 "No VGA/MCGA - Card detected" … 8 "Couldn't open
file ", 9 "Error while reading the file", 10 "Error while writing the file", 12 "File-Format not known",
15 "Invalid File-Format", 19 "Unused errorcode", 20 "User error". German table: 0x451b5c.

The Watcom RTL fatal-exception reporter `0x447db8` (VERIFIED) calls `GetActiveWindow()` (thunk 0x44cee8). If
that returns non-zero, or if console detection fails, it calls `LoadLibraryA("user32.dll")` and
`GetProcAddress(…, "MessageBoxExA")`, then `MessageBoxExA(NULL, msg + "\n\nClick on OK to terminate the
application", "Application Error: <GetModuleFileNameA path>", 0x2010 /*MB_ICONHAND|MB_TASKMODAL*/, 0)`.
Otherwise it writes to stderr. Host: GetProcAddress may return a host MessageBoxExA or NULL. NULL
falls back to WriteFile.

Host MessageBoxA: a modal SDL message box returning IDOK=1 or IDCANCEL=2. NULL caption means "Error". Show the
system cursor during the box; the game also calls ShowCursor around its own boxes.

---

## 14. LoadStringA

Single site 0x4047fd in wrapper `0x4047e8(eax=id, edx=buf, ebx=cchMax)` →
`LoadStringA([0x455618]=hInstance, id, buf, cchMax)`. Return values are tested (`test eax,eax`) at some sites,
for example 0x40488b ("STRING NOT FOUND"). There are about 190 wrapper call sites: 121 with constant ids
(211..5213) and 65 computed (VERIFIED).

Resource data (VERIFIED): 971 strings in RT_STRING blocks, ids 101..8042, language 1031 (de-DE) although the
text is English. They are UTF-16 in the PE. Non-ASCII code points are only U+00B4 `´` and U+0081 (in "B\x81ck",
which is `ü` in CP437). **Convert by Latin-1 truncation** (U+00xx → byte xx); this equals CP1252
WideCharToMultiByte for all 971 strings. Semantics: copy at most `cchMax-1` bytes, NUL-terminate, return the
length; return 0 for an unknown id. Strings contain `\n` (19 strings), `%s`/`%d` (formatted by the game's own
sprintf `0x442bb4`, not wsprintfA) and `&` (ids 642, 1204, 1208).

---

## 15. wsprintfA format strings (complete)

The IAT slot is 0x48b414 (Watcom block). The call is cdecl. Destination buffers are `0x485f40` (256 bytes) or
locals.

| Site | Live | Format (address) | Args |
|---|---|---|---|
| 0x433b70 | live | `"Couldn't get Back Surface_1!\n"` (0x44eef4) or `"Primary Surface Initialization Failed!"` (0x44ef12) | – |
| 0x43400c | live | `"Main Window Init Failed \n"` (0x44efc7) | – |
| 0x434191 | live | `"Direct Draw ERROR!!\n No Main Window initialized!\n"` (0x44f046) | – |
| 0x434a67 | live | `"Current Mode has 4 Bit per Pixel\n"` (0x44f126) / `"Current Mode has 8 Bit per Pixel\n"` (0x44f148) | – |
| 0x434ab8 | live | `"Unknown Pixelformat %d\n"` (0x44f16a) | bpp (int) |
| 0x434ae1 | live | `"Display mode not determined!"` (0x44f1bc) | – |
| 0x436ec0 | live | 14 DirectSound messages (0x44f24e..0x44f3e1), e.g. `"Out of memory!\n"`, `"Buffer lost!\n"` | – |
| 0x436fa1 | live | `"Unknown Errorcode!!: %x\n"` (0x44f408) | HRESULT |
| 0x43a546 | live (video) | `"Unknown Errorcode!!: %x\n"` (0x44fc70) | HRESULT |
| 0x43def0 | live | 17 DirectDraw messages (0x44fd73..0x44ff08), e.g. `"Surface lost!!!\n"`, `"Devicekontext existiert schon!!!\n"` | – |
| 0x43e022 | live | `"Unknown Errorcode!!: %x\n"` (0x44ff19) | GetLastError() |
| 0x448035 | live (RTL) | `"The instruction at %08lx referenced memory at %08lx\nThe memory could not be %s"` (0x45022a) | eip, addr, "read from"/"written" |
| 0x44807c | live (RTL) | one of `"An privileged instruction was executed at address %08lx"`, `"An illegal instruction was executed at address %08lx"`, `"An integer divide by zero was encountered at address %08lx"`, `"A stack overflow was encountered at address %08lx"`, `"The program encountered a problem at address %08lx and cannot continue"` | eip |
| 0x433cee, 0x433d29 | dead | `"Timer not initialized !!\n"`, `"Main Window Init Failed \n"` | – |
| 0x4341fb | dead | `"Direct Draw ERROR!!\n Set Cooperative Level Failed (%08lx)\n"` (0x44f078) / `"…Init Failed (%08lx)\n"` (0x44f0b3) | HRESULT |
| 0x434232 | dead | `"Direct Draw ERROR!!\n No Main Window initialized!\n"` (0x44f0dd) | – |
| 0x434b9a | dead | `"Direct Draw ERROR!!\n GetGPXModes\n"` | – |

The host must support `%d`, `%x`, `%08lx`, `%s` and literal text (wsprintf semantics: no floating point; `%l`
is accepted).

---

## 16. GetSysColor and SetSysColors

`0x40271e` (VERIFIED) stores `GetSysColor(idx[i])` for the five indices in the table at 0x450c50: 15, 15, 22,
16, 16 (COLOR_BTNFACE, COLOR_BTNFACE, COLOR_3DLIGHT, COLOR_BTNSHADOW, COLOR_BTNSHADOW). The values go to
`0x45d948[i]` and are never read again. The intended replacement colours sit at 0x450c64: `0x7ccaf8, 0x83a5cd,
0xa0daff, 0x4e9bcb, 0x62afdf` (tan theme).

`SetSysColors` is **never called**. There are four 19-byte NOP blocks at `0x402765` (after GetSysColor),
`0x401853` (shutdown), and `0x40a189`/`0x40a1d9` (WM_ACTIVATEAPP activate and deactivate). 19 bytes is exactly
`push imm32; push imm32; push 5; call dword ptr cs:[0x48b3e4]`, so the repack evidently removed the
global system-colour theming (INFERRED). Host: implement GetSysColor with Win95 defaults; SetSysColors can be a
no-op.

---

## 17. Other USER32

| API | Live sites | Spec |
|---|---|---|
| MessageBeep | 0x409516 (V/F7 toggle), 0x41dfa7, 0x41f33c (STORYFUC.TAF load error) | arg is always `0xFFFFFFFF`; a short beep or a no-op |
| GetActiveWindow | 0x448084 (RTL) | return hwndMain if it exists and is active, else 0 |
| ShowWindow / UpdateWindow | 0x433fe2 / 0x433ff0 | nCmdShow from WinMain; UpdateWindow sends WM_PAINT to DefWindowProc |
| DestroyWindow | 0x434024 (SetTimer failed), 0x43437c (error box OK), 0x4345a0 (replaced handler), 0x433915 (unreachable) | must send WM_DESTROY synchronously |
| DestroyMenu | 0x438eb8 (popup), 0x4338f3/0x43435b/0x43457e (`[0x488078]`, always 0) | accept NULL |
| PostQuitMessage | 0x434385, 0x4345a9 | see §7.4 |
| GetClientRect / SetScrollInfo | 0x434470 / 0x43450d, 0x434553 in 0x434453 | unreachable with live arguments (windowed path only). SCROLLINFO cbSize 0x1c, fMask 7, SB_HORZ/SB_VERT, redraw TRUE. Stub. |
| GetSystemMetrics, LoadMenuA, SetMenu, PostMessageA, InvalidateRect, SetCursorPos, GetStockObject | dead only | stub (SM_CXSCREEN→640, SM_CYSCREEN→480, GetStockObject(BLACK_BRUSH) → stock handle) |
| MsgWaitForMultipleObjects + GetAsyncKeyState(0x1B) | 0x43a799, 0x43a7e0 | video playback loop, **out of scope** |

---

## 18. GDI32

### 18.1 HDC sources (VERIFIED)

1. **Surface DCs:** all TextOutA and DrawTextA calls go through `0x43cf4f` and `0x43d079`. Every caller passes
   `edx=0`, so the wrapper calls `IDirectDrawSurface::GetDC` (vtable +0x44) on the current render target.
   That target is `ctx=[0x48a644]`: if `(short)ctx[+0x3c] != 0` use `ctx[+0xc + 4*idx]`, otherwise
   `ctx[+0x10]` (the back buffer). The wrapper retries while `DDERR_WASSTILLDRAWING` (0x8876021c) and calls
   `ReleaseDC` (+0x68) at the end. Host GetDC must return a DC whose drawing lands in the surface pixels
   (16-, 24- or 32-bit format; the game requires ≥16 bpp).
2. **Control-colour DCs:** `wParam` of WM_CTLCOLOR* in dialogs. Only SetTextColor and SetBkMode are applied.
3. **BeginPaint DCs:** in dialogs, unused for drawing.

There is no GetDC/ReleaseDC import, no CreateCompatibleDC and no bitmap GDI.

### 18.2 Font (VERIFIED, 0x43ab09..0x43ab4d)

The LOGFONTA is built on the stack and is not static. Reconstructed bytes (60):

```
f2 ff ff ff 00 00 00 00 00 00 00 00 00 00 00 00 bc 02 00 00 00 00 00 00 00 02 00 00
53 79 73 74 65 6d 20 53 6d 61 6c 6c 00 ...(zero to 32 bytes)
lfHeight=-14 (char height 14 px), lfWidth=0, esc/orient=0, lfWeight=700 (FW_BOLD),
italic/underline/strike=0, lfCharSet=0 (ANSI), lfOutPrecision=0, lfClipPrecision=2 (CLIP_STROKE_PRECIS),
lfQuality=0, lfPitchAndFamily=0, lfFaceName="System Small" (0x44fcf6)
```

The handle is stored at `[0x48a680]`, selected into every surface DC by the wrappers (and never deselected),
and deleted at shutdown (0x43ab78). It is the only CreateFontIndirectA call. The same routine decodes
`"NGS-REVEAL"` / `"GRAPHICS_DRIVER_(C)_NEW_GENERATION_SOFTWARE"` (bytes minus 0x0E).

**Estimate of the required face (INFERRED; see open questions):** no Windows font is named "System Small". The
mapper falls back by charset, weight and height. On Win9x with stock fonts this is most likely Arial Bold at a
14 px em (cell about 16 px, ascent about 13 px), rendered with **no anti-aliasing**. Layout spacing in the code
points the other way. HLP.DAT help lines are drawn **10 px apart** (0x409dca), and "Remaining:" (y=396) and
"%d h" (y=408) are 12 px apart. Help lines are about 100 characters wide on a 640 px screen. These facts
suggest the developers saw a font with cell height ≤ 10–12 px and an average width of about 6 px. Recommend a
configurable host font: a bold sans bitmap font, defaulting to a 14 px em (Liberation Sans Bold, metric-
compatible with Arial, 1-bit), with an alternative small profile (about 9–10 px cell). Choose between them
from a reference capture of the original under Windows or Wine.

### 18.3 Wrapper semantics (VERIFIED)

`0x43cf4f` TextOut wrapper: `(eax=ctx unused, edx=hdc|0, ebx=x, ecx=y; stack: color, bk, text)`, `ret 0xc`:

```
SetTextAlign(hdc, 0 /*TA_LEFT|TA_TOP|TA_NOUPDATECP*/);
SetTextColor(hdc, color);
if (bk & 1) SetBkMode(hdc, TRANSPARENT); else { SetBkMode(hdc, OPAQUE); SetBkColor(hdc, bk); }
if ([0x48a680]) SelectObject(hdc, font);
TextOutA(hdc, x, y, text, strlen(text));
```

`0x43d079` DrawText wrapper: `(edx=hdc|0, ebx=&RECT (copied), ecx=color; stack: bk, text, format)`, `ret 0xc`:

```
SetTextAlign(hdc, 0); DPtoLP(hdc, &rc, 2);          // MM_TEXT → identity
SetTextColor(hdc, color);
if (bk == 0x00FFFFFF) SetBkMode(hdc, TRANSPARENT); else { SetBkMode(hdc, OPAQUE); SetBkColor(hdc, bk); }
if (font) SelectObject(hdc, font);
DrawTextA(hdc, text, -1, &rc, format);
```

Note the different transparency rules: **odd bk** for TextOut and **exactly white** for DrawText.

COLORREFs are `0x00BBGGRR`. The values used are 0xFFFFFF white, 0x00FFFF yellow, 0x00FF00 green and 0 black,
plus data-driven values. Convert them to the surface pixel format.

DrawText formats used: `0x01` DT_CENTER, `0x05` DT_CENTER|DT_VCENTER (no DT_SINGLELINE, so VCENTER has no
effect), `0x10` DT_WORDBREAK, `0x11` DT_WORDBREAK|DT_CENTER, and `0x2010` DT_EDITCONTROL|DT_WORDBREAK. None
has DT_NOPREFIX, so `&x` underlines x and `&&` becomes `&`. This matters for film names such as "006 &
Moneyhenny" and "Sex Attack Film&Video". Line breaks on `\n` must work. OPAQUE mode fills only behind each
text line, not the whole rectangle.

### 18.4 Every text draw site (VERIFIED; x,y in 640×480 screen pixels)

TextOut (`0x43cf4f`):

| Site | x, y | Color / bk | Text |
|---|---|---|---|
| 0x403da9 | (screen_w/2, 0) | white / transparent | `"Version: %f F DEBUG_DAT"` (when `[0x45dc04]`, toggled by V or F7) |
| 0x403dee | (0, 0) | white / transparent | `"F1-Help"` or `0x40b942()` string |
| 0x4048ea / 0x40490d | (mx+2, my-18) then (mx, my-20) | black then white / transparent | hover "rect name" (LoadString), drop shadow |
| 0x404e25 | per-page table (ebp+i*8) | per-page table / per-page bk (may be opaque) | slideshow pages (0x404cdb) |
| 0x409dbb | (0, 10·n) | white / transparent | F1 help lines from DATA\PERSO\HLP.DAT |
| 0x4105a8 | (268, 224) | white / transparent | 0x45e0e0 |
| 0x41156d | (50, 100) | yellow / transparent | 0x45e2c0 |
| 0x41349a, 0x413542, 0x4136bb | (50, 100) | yellow / transparent | LS 403 / 405 / 412 |
| 0x413eca | (50, 100) | yellow / transparent | LS text + sprintf |
| 0x416f65 | (0, 155) | green / transparent | `"Visit:"` |
| 0x416f85 | (0, 125) | white / transparent | `"Free Objects : %d"` |
| 0x41963c | (mx, my-30) | white / transparent | `"Use Plane"` |
| 0x42012b | (9, 269) | white / transparent | `"Publicity:"` |
| 0x42016b | (9, 367) | white / transparent | `"Mood:"` |
| 0x4201a4 | (`[0x4842d8]`, `[0x4842dc]`) | `[0x4842e0]` / transparent | timed message 0x484270 |
| 0x423979 | (10, 396) | white / transparent | `"Remaining:"` |
| 0x423a0d | (10, 408) | yellow / transparent | `"%d h"` |

DrawText (`0x43d079`):

| Site | RECT (l,t,r,b) | Color / bk | Format | Text |
|---|---|---|---|---|
| 0x409c76 | (323, 0, 645, 222) | white / transp. | 0x10 | LS 5400+n (rotating "Infamous sayings", n = 0..14) |
| 0x40b45b | RECT at 0x45de6c | white / transp. | 0x01 | 0x45de7c |
| 0x4155e1 | object rect, inset 3 | obj+0x74 / obj+0x78 | 0x2010 | obj+0x10 (UI text boxes) |
| 0x4156df | object rect, inset 3 | obj+0x8c / obj+0x90 | 0x2010 | obj+0x28 |
| 0x43213a | object rect, inset 3 | obj+0x8c / obj+0x90 | 0x2010 | obj+0x28 |
| 0x420e20 | RECT 0x4843d8+16i | black / transp. | 0x05 | 0x484418+60i |
| 0x423954 | (0, 150, screen_w, 180) | white / transp. | 0x01 | 0x484864 |
| 0x423aaf | (140, 430, 520, 448) | white / transp. | 0x01 | `"Select Film"` or film name |
| 0x423b07 | (0, 0, screen_w, 30) | white / transp. | 0x01 | `"STUDIO %d"` |
| 0x424e79 | RECT 0x484854 | black / transp. | 0x10 | sprintf(0x4550d8, …) |
| 0x42965f | (0, screen_h-30, screen_w, screen_h) | white / transp. | 0x01 | `"Click on the girl to start the shuttle!"` |
| 0x429878 | (10, 10, 300, 470) | yellow / transp. | 0x11 | LS 1152 |
| 0x430234 | (441, 100, 630, 470) | white / transp. | 0x11 | LS 1607+n |

Most HUD and status text does **not** use GDI. It uses the game's own bitmap-font printer `0x43c3b4`, which is
outside this scope.

### 18.5 Brushes, objects and other GDI calls (VERIFIED)

* `CreateSolidBrush(0x7ccaf8)` in every dialog init (18 sites). There are no other brush colours.
* `DeleteObject`: dialog brushes (21 sites; double deletes happen, see §12.2) and the font (0x43ab78).
* `SelectObject` only selects the font into surface DCs (0x43d014, 0x43d160). The return value is ignored.
* `SetBkColor` only in the wrappers. `SetTextAlign` is always 0. `DPtoLP(hdc, rc, 2)` is identity.
* `GetStockObject(BLACK_BRUSH=4)` is in dead code only.

---

## 19. Out-of-scope sites met (addresses only)

* Video playback module `0x439e2c..0x43a9c7`: MsgWaitForMultipleObjects 0x43a799, PeekMessageA 0x43a7bd,
  Translate/Dispatch 0x43a7cb/0x43a7d5, GetAsyncKeyState(VK_ESCAPE) 0x43a7e0, wsprintfA 0x43a546,
  CoCreateInstance in 0x43a061, and Sleep(0) via 0x434156 at 0x43a19d.
* MCI modules `0x4434c1..0x443f8f`: MM_MCINOTIFY (0x3B9) handlers 0x4439b3 and 0x443f8f, registered at
  0x4436b8 and 0x443d1d. mciSendCommandA is used throughout.

## 20. Open questions and risks

1. **Realized GDI font** (§18.2): the LOGFONT face "System Small" (-14, bold) does not exist. Update 2026-10-06: the Wine capture's button texts have exactly the extents of Arial-metric bold at a 14 px em with whole-pixel advances and no kerning, which the runtime now reproduces. The layout spacing
   suggests a smaller font than Arial Bold 14. This needs a reference capture.
2. **WM_DESTROY during play** returns to the main menu with a dead hwnd (§10). The host must choose a
   close policy.
3. **Timer and render cadence** depend on the repack Sleep(32) patch and Flip(DDFLIP_WAIT). Match them if exact
   pacing matters.
4. **Dialog appearance:** tan background via the WM_CTLCOLOR brush, default grey buttons (SetSysColors
   removed), and Helv/Fixedsys fonts.
5. **The 0x7F7 WndProc slot** holds `Sleep` (§5.2).
6. **`.bss` VirtualSize is 0 in the PE headers.** pefile's mapped image fills 0x455000+ with file bytes, so
   loaders must zero-fill that region (this affects the handler table at 0x486070, which starts zeroed).

---

## Appendix A: complete in-scope call-site inventory (390 sites)

"Function" is the enclosing Watcom function start (prologue `push N; call 0x43371d`, or the Ghidra start). "Reach" comes from the transitive reachability pass (see the method paragraph at the top). "Pushed args" are the operands of the `push` instructions found by a linear backward scan from the call, listed first argument first. Where control flow joins a shared tail through `jmp` (for example 0x404097, 0x40d516, 0x4138ff, 0x41e57f, 0x421f81 and 0x423674), the scan shows only one path; §12/§13 give the per-path values. Register operands are unresolved here; the body sections resolve the relevant ones.

| Import | Call site | Function | Reach | Pushed args (1st arg first, raw) |
|---|---|---|---|---|
| AppendMenuA | 0x00438da5 | 0x00438d56 | live | eax, 0x800, 0, 0 |
| AppendMenuA | 0x00438db6 | 0x00438d56 | live | ebx, 2, 0, 0x44f4cf |
| AppendMenuA | 0x00438dc7 | 0x00438d56 | live | ebx, 0x800, 0, 0 |
| AppendMenuA | 0x00438e00 | 0x00438d56 | live | esi, 8, edx, ecx |
| AppendMenuA | 0x00438e1f | 0x00438d56 | live | esi, 0x800, 0, 0 |
| AppendMenuA | 0x00438e30 | 0x00438d56 | live | esi, 0, 2, 0x44f4e8 |
| AppendMenuA | 0x00438e41 | 0x00438d56 | live | esi, 0x800, 0, 0 |
| BeginPaint | 0x0040e369 | 0x0040e19b | live | ebp, eax |
| BeginPaint | 0x00411880 | 0x004116a3 | live | ebp, eax |
| BeginPaint | 0x00413a75 | 0x00413944 | live | edi, eax |
| BeginPaint | 0x00413c2e | 0x00413aec | live | edi, eax |
| BeginPaint | 0x0041d94c | 0x0041d724 | live | esi, eax |
| BeginPaint | 0x0041ea31 | 0x0041e912 | live | edi, eax |
| BeginPaint | 0x0041ec2a | 0x0041eaa7 | live | ebp, eax |
| BeginPaint | 0x0041ee9e | 0x0041ecf6 | live | ebp, eax |
| BeginPaint | 0x0042223f | 0x004220fb | live | edi, eax |
| BeginPaint | 0x004223d8 | 0x004222a7 | live | edi, eax |
| BeginPaint | 0x00424af8 | 0x004248dc | live | ebp, eax |
| BeginPaint | 0x0043d61d | 0x0043d5ee | DEAD | ecx, eax |
| ClientToScreen | 0x00433aa0 | 0x0043393d | live | edi, eax |
| ClientToScreen | 0x00433eba | 0x00433e0a | DEAD | eax, edx |
| ClientToScreen | 0x00435f69 | 0x00435f4b | DEAD | edx, eax |
| ClientToScreen | 0x0043dcb9 | 0x0043dc8d | DEAD | esi, eax |
| ClientToScreen | 0x0043dd85 | 0x0043dd59 | DEAD | esi, eax |
| CreateFontIndirectA | 0x0043ab46 | 0x0043a9c8 | live | eax |
| CreatePopupMenu | 0x00438d90 | 0x00438d56 | live |  |
| CreateSolidBrush | 0x004024c3 | 0x00402288 | live | 0x7ccaf8 |
| CreateSolidBrush | 0x0040d86a | 0x0040d611 | live | 0x7ccaf8 |
| CreateSolidBrush | 0x0040dcd6 | 0x0040d9e3 | live | 0x7ccaf8 |
| CreateSolidBrush | 0x0040df71 | 0x0040deab | live | 0x7ccaf8 |
| CreateSolidBrush | 0x0040e334 | 0x0040e19b | live | 0x7ccaf8 |
| CreateSolidBrush | 0x0040fc42 | 0x0040fb4d | live | 0x7ccaf8 |
| CreateSolidBrush | 0x0041184b | 0x004116a3 | live | 0x7ccaf8 |
| CreateSolidBrush | 0x00413a5e | 0x00413944 | live | 0x7ccaf8 |
| CreateSolidBrush | 0x00413bbd | 0x00413aec | live | 0x7ccaf8 |
| CreateSolidBrush | 0x0041d935 | 0x0041d724 | live | 0x7ccaf8 |
| CreateSolidBrush | 0x0041ea1a | 0x0041e912 | live | 0x7ccaf8 |
| CreateSolidBrush | 0x0041ec15 | 0x0041eaa7 | live | 0x7ccaf8 |
| CreateSolidBrush | 0x0041ee83 | 0x0041ecf6 | live | 0x7ccaf8 |
| CreateSolidBrush | 0x004204ff | 0x0042036d | live | 0x7ccaf8 |
| CreateSolidBrush | 0x004221d2 | 0x004220fb | live | 0x7ccaf8 |
| CreateSolidBrush | 0x004223c1 | 0x004222a7 | live | 0x7ccaf8 |
| CreateSolidBrush | 0x00424ab9 | 0x004248dc | live | 0x7ccaf8 |
| CreateSolidBrush | 0x0042aa47 | 0x0042a720 | live | 0x7ccaf8 |
| CreateWindowExA | 0x00433c86 | 0x00433bfc | DEAD | 0, 0x485ea0, 0x485ef0, 0xff0000, 0, 0, ebx, eax, 0, 0, esi, 0 |
| CreateWindowExA | 0x00433e9d | 0x00433e0a | DEAD | 0, 0x485ea0, 0x485ef0, 0xff0000, 0, 0, edi, eax, 0, 0, esi, 0 |
| CreateWindowExA | 0x00433fb3 | 0x00433f2e | live | 0x40000, 0x485ea0, 0x485ef0, 0x80080000, 0, 0, ecx, ebp, 0, 0, esi, 0 |
| DefWindowProcA | 0x00434445 | 0x004343f1 | live | ebp, edi, esi, ecx |
| DeleteObject | 0x0040231b | 0x00402288 | live | eax |
| DeleteObject | 0x0040234e | 0x00402288 | live | edi |
| DeleteObject | 0x00402456 | 0x00402288 | live | ebx |
| DeleteObject | 0x0040d809 | 0x0040d611 | live | esi |
| DeleteObject | 0x0040dc67 | 0x0040d9e3 | live | eax |
| DeleteObject | 0x0040dc86 | 0x0040d9e3 | live | ebx |
| DeleteObject | 0x0040df15 | 0x0040deab | live | ecx |
| DeleteObject | 0x0040e259 | 0x0040e19b | live | eax |
| DeleteObject | 0x0040fbd2 | 0x0040fb4d | live | ecx |
| DeleteObject | 0x00411755 | 0x004116a3 | live | eax |
| DeleteObject | 0x004139d2 | 0x00413944 | live | eax |
| DeleteObject | 0x00413b77 | 0x00413aec | live | eax |
| DeleteObject | 0x0041d790 | 0x0041d724 | live | ebx |
| DeleteObject | 0x0041e9b6 | 0x0041e912 | live | ecx |
| DeleteObject | 0x0041eb4f | 0x0041eaa7 | live | ebx |
| DeleteObject | 0x0041ed8b | 0x0041ecf6 | live | ebx |
| DeleteObject | 0x00420431 | 0x0042036d | live | esi |
| DeleteObject | 0x00422191 | 0x004220fb | live | ecx |
| DeleteObject | 0x00422335 | 0x004222a7 | live | eax |
| DeleteObject | 0x004249c1 | 0x004248dc | live | eax |
| DeleteObject | 0x0042a938 | 0x0042a720 | live | ecx |
| DeleteObject | 0x0043ab78 | 0x0043ab5f | live | edx |
| DestroyMenu | 0x004338f3 | 0x004338d3 | live | edx |
| DestroyMenu | 0x0043435b | 0x004342ef | live | ecx |
| DestroyMenu | 0x0043457e | 0x00434562 | live | edx |
| DestroyMenu | 0x00438eb8 | 0x00438d56 | live | esi |
| DestroyWindow | 0x00433915 | 0x004338d3 | live | ebp |
| DestroyWindow | 0x00434024 | 0x00433f2e | live | eax |
| DestroyWindow | 0x0043437c | 0x004342ef | live | eax |
| DestroyWindow | 0x004345a0 | 0x00434562 | live | edi |
| DialogBoxParamA | 0x00402216 | 0x004020e0 | live | 0, 0x44d5d4, esi, 0x402288, 0 |
| DialogBoxParamA | 0x0040d516 | 0x0040d123 | live | edi, eax, edx, eax, edi |
| DialogBoxParamA | 0x0040d6d7 | 0x0040d611 | live | 0, 0x44defe, ecx, 0x40deab, 0 |
| DialogBoxParamA | 0x0040e08e | 0x0040dfd8 | live | 0, 0x44e00c, ecx, 0x40e19b, 0 |
| DialogBoxParamA | 0x0040facb | 0x0040f9f4 | live | ebx, 0x44e0c4, edx, 0x40fb4d, ebx |
| DialogBoxParamA | 0x0040fee6 | 0x0040feac | live | 0, 0x44e10d, ecx, 0x40d9e3, 0 |
| DialogBoxParamA | 0x00411442 | 0x004113b9 | live | 0, 0x44e1ca, ebx, 0x4116a3, 0 |
| DialogBoxParamA | 0x004138ff | 0x004137d4 | live | ecx, 0x44e278, ebx, 0x413944, ecx |
| DialogBoxParamA | 0x0041a46d | 0x0041a1dd | live | 0, 0x44e4a8, edi, 0x41d724, 0 |
| DialogBoxParamA | 0x0041e4e8 | 0x0041e45a | live | ebp, 0x44e682, ecx, 0x41e912, ebp |
| DialogBoxParamA | 0x0041e57f | 0x0041e45a | live | ebp, 0x44e697, ebx, 0x41ecf6, ebp |
| DialogBoxParamA | 0x0041f0d9 | 0x0041efd2 | live | 0, eax, ebx, eax, ebx |
| DialogBoxParamA | 0x0041fea9 | 0x0041fde8 | live | 0, 0x44e73a, esi, 0x42036d, 0 |
| DialogBoxParamA | 0x00421f81 | 0x00421caf | live | ebp, 0x44e7ea, ecx, 0x4220fb, ebp |
| DialogBoxParamA | 0x00423674 | 0x00423448 | live | 0, 0x44e89b, eax, 0x4248dc, 0 |
| DialogBoxParamA | 0x00429eb9 | 0x00429d58 | live | 0, 0x44eb32, ebx, 0x42a720, 0 |
| DispatchMessageA | 0x00404180 | 0x0040413f | live | eax |
| DispatchMessageA | 0x0040589b | 0x00405862 | live | eax |
| DispatchMessageA | 0x0041656e | 0x00416534 | live | eax |
| DispatchMessageA | 0x0041659f | 0x00416534 | live | eax |
| DispatchMessageA | 0x0041cdc2 | 0x0041c9ad | live | eax |
| DispatchMessageA | 0x00438e99 | 0x00438d56 | live | eax |
| DispatchMessageA | 0x0043a7d5 | 0x0043a749 | live | eax |
| DPtoLP | 0x0043d105 | 0x0043d079 | live | ecx, eax, 2 |
| DrawTextA | 0x0043d17d | 0x0043d079 | live | ebp, edi, -1, eax, ecx |
| EndDialog | 0x00402329 | 0x00402288 | live | edx, 1 |
| EndDialog | 0x0040235c | 0x00402288 | live | ebp, 1 |
| EndDialog | 0x0040d81f | 0x0040d611 | live | ebp, edi |
| EndDialog | 0x0040dc76 | 0x0040d9e3 | live | ebp, 1 |
| EndDialog | 0x0040df23 | 0x0040deab | live | esi, ebx |
| EndDialog | 0x0040e267 | 0x0040e19b | live | ebp, edi |
| EndDialog | 0x0040fbe0 | 0x0040fb4d | live | edi, esi |
| EndDialog | 0x00411763 | 0x004116a3 | live | ebp, edi |
| EndDialog | 0x004139e0 | 0x00413944 | live | edi, esi |
| EndDialog | 0x00413b85 | 0x00413aec | live | edi, esi |
| EndDialog | 0x0041d79a | 0x0041d724 | live | esi, 0 |
| EndDialog | 0x0041e9c4 | 0x0041e912 | live | edi, esi |
| EndDialog | 0x0041eb5d | 0x0041eaa7 | live | ebp, edi |
| EndDialog | 0x0041eda0 | 0x0041ecf6 | live | ecx, ebp |
| EndDialog | 0x0042043f | 0x0042036d | live | ebp, edi |
| EndDialog | 0x0042219f | 0x004220fb | live | edi, esi |
| EndDialog | 0x00422343 | 0x004222a7 | live | edi, esi |
| EndDialog | 0x004249cf | 0x004248dc | live | ebp, edi |
| EndDialog | 0x0042a946 | 0x0042a720 | live | esi, ebp |
| EndPaint | 0x0040e374 | 0x0040e19b | live | ebp, eax |
| EndPaint | 0x0041188b | 0x004116a3 | live | ebp, eax |
| EndPaint | 0x00413a82 | 0x00413944 | live | edi, eax |
| EndPaint | 0x00413c3b | 0x00413aec | live | edi, eax |
| EndPaint | 0x0041d95c | 0x0041d724 | live | esi, eax |
| EndPaint | 0x0041ea3e | 0x0041e912 | live | edi, eax |
| EndPaint | 0x0041ec35 | 0x0041eaa7 | live | ebp, eax |
| EndPaint | 0x0041eeab | 0x0041ecf6 | live | ebp, eax |
| EndPaint | 0x0042224c | 0x004220fb | live | edi, eax |
| EndPaint | 0x004223e5 | 0x004222a7 | live | edi, eax |
| EndPaint | 0x00424b03 | 0x004248dc | live | ebp, eax |
| EndPaint | 0x0043d62f | 0x0043d5ee | DEAD | esi, eax |
| GetActiveWindow | 0x00448084 | 0x00447db8 | live |  |
| GetAsyncKeyState | 0x00436387 | 0x00436361 | live | 0x11 |
| GetAsyncKeyState | 0x004363a2 | 0x00436361 | live | 0x10 |
| GetAsyncKeyState | 0x004363bd | 0x00436361 | live | 0x12 |
| GetAsyncKeyState | 0x0043a7e0 | 0x0043a749 | live | 0x1b |
| GetClientRect | 0x00434470 | 0x00434453 | live | edx, eax |
| GetClientRect | 0x0043dccb | 0x0043dc8d | DEAD | edi, eax |
| GetClientRect | 0x0043dd97 | 0x0043dd59 | DEAD | edi, eax |
| GetDlgItem | 0x0040d674 | 0x0040d611 | live | ebx, 0x91 |
| GetDlgItem | 0x0040d880 | 0x0040d611 | live | ebx, 0x91 |
| GetDlgItem | 0x0040da4c | 0x0040d9e3 | live | ebp, 0x91 |
| GetDlgItem | 0x0040dd09 | 0x0040d9e3 | live | ebp, 0x91 |
| GetDlgItem | 0x0040e1f4 | 0x0040e19b | live | ebp, 0x6f |
| GetDlgItem | 0x0040e29c | 0x0040e19b | live | ebp, 0x6f |
| GetDlgItem | 0x004116fc | 0x004116a3 | live | ebp, 0x6f |
| GetDlgItem | 0x004117c0 | 0x004116a3 | live | ebp, 0x6f |
| GetDlgItem | 0x0041d7d0 | 0x0041d724 | live | esi, 0x6f |
| GetDlgItem | 0x0041eb0a | 0x0041eaa7 | live | ebp, 0x6f |
| GetDlgItem | 0x0041eb8e | 0x0041eaa7 | live | ebp, 0x6f |
| GetDlgItem | 0x0041ec63 | 0x0041eaa7 | live | ebp, 0x6f |
| GetDlgItem | 0x0041ed64 | 0x0041ecf6 | live | edx, 0x6f |
| GetDlgItem | 0x0041ede7 | 0x0041ecf6 | live | esi, 0x6f |
| GetDlgItem | 0x004203c3 | 0x0042036d | live | ebp, 0x6f |
| GetDlgItem | 0x0042049c | 0x0042036d | live | ebp, 0x6f |
| GetDlgItem | 0x00420532 | 0x0042036d | live | ebp, 0x6f |
| GetDlgItem | 0x00424935 | 0x004248dc | live | ebp, 0x6f |
| GetDlgItem | 0x00424a2c | 0x004248dc | live | ebp, 0x6f |
| GetDlgItem | 0x00424ac8 | 0x004248dc | live | ebp, 0x6f |
| GetDlgItem | 0x0042a785 | 0x0042a720 | live | esi, 0x6f |
| GetDlgItem | 0x0042a88f | 0x0042a720 | live | esi, 0x6f |
| GetDlgItem | 0x0042a9b7 | 0x0042a720 | live | esi, 0x6f |
| GetDlgItemInt | 0x004139aa | 0x00413944 | live | edi, 0x71, eax, esi |
| GetDlgItemInt | 0x00413b4f | 0x00413aec | live | edi, 0x72, eax, esi |
| GetDlgItemInt | 0x0042216f | 0x004220fb | live | edi, 0x72, eax, esi |
| GetDlgItemInt | 0x0042230d | 0x004222a7 | live | edi, 0x71, eax, esi |
| GetDlgItemTextA | 0x0040df02 | 0x0040deab | live | esi, 0x6d, 0x455628, 0x50 |
| GetDlgItemTextA | 0x0040fba8 | 0x0040fb4d | live | edi, 0x6d, 0x45e02c, 0x50 |
| GetDlgItemTextA | 0x0041e976 | 0x0041e912 | live | edi, 0x6d, eax, 0x63 |
| GetMessageA | 0x00438e81 | 0x00438d56 | live | eax, 0, 0, 0 |
| GetOpenFileNameA | 0x004025e3 | 0x00402510 | live | eax |
| GetStockObject | 0x00433e53 | 0x00433e0a | DEAD | 4 |
| GetSysColor | 0x00402738 | 0x0040271e | live | edx |
| GetSystemMetrics | 0x00433c50 | 0x00433bfc | DEAD | 0 |
| GetSystemMetrics | 0x00433c5f | 0x00433bfc | DEAD | 1 |
| GetSystemMetrics | 0x00433e6d | 0x00433e0a | DEAD | 0 |
| GetSystemMetrics | 0x00433e78 | 0x00433e0a | DEAD | 1 |
| InvalidateRect | 0x00434702 | 0x004345b8 | DEAD | esi, 0, 1 |
| KillTimer | 0x00433907 | 0x004338d3 | live | esi, 1 |
| KillTimer | 0x0043436f | 0x004342ef | live | edi, 1 |
| KillTimer | 0x00434592 | 0x00434562 | live | ecx, 1 |
| LoadCursorA | 0x00403b04 | 0x0040399d | live | 0, 0x7f02 |
| LoadCursorA | 0x004338b6 | 0x004337dc | live | edi, 0x7f00 |
| LoadIconA | 0x004338a4 | 0x004337dc | live | ecx, 0x7f00 |
| LoadMenuA | 0x00433ca4 | 0x00433bfc | DEAD | esi, edx |
| LoadMenuA | 0x00433ee6 | 0x00433e0a | DEAD | esi, edi |
| LoadStringA | 0x004047fd | 0x004047e8 | live | edx, eax, edx, ebx |
| MessageBeep | 0x00409516 | 0x004094d4 | live | -1 |
| MessageBeep | 0x0041dfa7 | 0x0041df38 | live | -1 |
| MessageBeep | 0x0041f33c | 0x0041efd2 | live | -1 |
| MessageBoxA | 0x00404097 | 0x0040408f | live | edx, edx, 0x44db4e, 0x12040 |
| MessageBoxA | 0x00434336 | 0x004342ef | live | edx, 0x485f40, 0x44f120, 1 |
| MessageBoxA | 0x00434bae | 0x00434afe | DEAD | esi, 0x485f40, 0, 0 |
| MessageBoxA | 0x00436540 | 0x004364b5 | live | edi, eax, 0, 0x12010 |
| MessageBoxA | 0x0043d8a0 | 0x0043d7c8 | live | edi, 0x44fd23, 0, 0 |
| MessageBoxA | 0x0043dac1 | 0x0043d7c8 | live | edx, 0x44fd3e, 0, 0 |
| MessageBoxA | 0x0043db30 | 0x0043daf1 | live | edx, 0x44fd5d, 0, 0 |
| MsgWaitForMultipleObjects | 0x0043a799 | 0x0043a749 | live | eax, 0x48a528, 0, -1, 0xff |
| PeekMessageA | 0x00404168 | 0x0040413f | live | eax, edx, 0x113, 0x113, 1 |
| PeekMessageA | 0x00404db0 | 0x00404cdb | live | eax, edx, 0x113, 0x113, eax |
| PeekMessageA | 0x00404e84 | 0x00404e47 | live | eax, edx, 0x113, 0x113, esi |
| PeekMessageA | 0x00405883 | 0x00405862 | live | eax, edx, 0, 0, 1 |
| PeekMessageA | 0x00416556 | 0x00416534 | live | eax, 0, 0x113, 0x113, 1 |
| PeekMessageA | 0x00416587 | 0x00416534 | live | eax, 0, 0, 0, 1 |
| PeekMessageA | 0x0041cda8 | 0x0041c9ad | live | eax, ebx, 0x113, 0x113, 1 |
| PeekMessageA | 0x00435ed0 | 0x00435e8f | live | eax, esi, 0x113, 0x113, 0 |
| PeekMessageA | 0x00438e6b | 0x00438d56 | live | eax, 0, 0, 0, 0 |
| PeekMessageA | 0x0043a7bd | 0x0043a749 | live | eax, 0, 0, 0, 1 |
| PostMessageA | 0x00433df0 | 0x00433bfc | DEAD | esi, 0xf, 0, 0 |
| PostQuitMessage | 0x00434385 | 0x004342ef | live | 0 |
| PostQuitMessage | 0x004345a9 | 0x00434562 | live | 0 |
| RegisterClassA | 0x00433c47 | 0x00433bfc | DEAD | 0x486048 |
| RegisterClassA | 0x00433e64 | 0x00433e0a | DEAD | 0x486048 |
| RegisterClassA | 0x00433f87 | 0x00433f2e | live | 0x486048 |
| ScreenToClient | 0x004362b5 | 0x004361c7 | live | edi, eax |
| SelectObject | 0x0043d014 | 0x0043cf4f | live | edx, ecx |
| SelectObject | 0x0043d160 | 0x0043d079 | live | edx, ebp |
| SendMessageA | 0x00402485 | 0x00402288 | live | edx, 0xb, esi, esi |
| SendMessageA | 0x0040d687 | 0x0040d611 | live | eax, 0x188, 0, 0 |
| SendMessageA | 0x0040d6a4 | 0x0040d611 | live | edi, 0x18a, eax, 0 |
| SendMessageA | 0x0040d6bb | 0x0040d611 | live | edi, 0x189, esi, 0x455628 |
| SendMessageA | 0x0040d8d1 | 0x0040d611 | live | ecx, 0x180, 0, eax |
| SendMessageA | 0x0040d995 | 0x0040d611 | live | esi, 0x186, 0, 0 |
| SendMessageA | 0x0040da5f | 0x0040d9e3 | live | eax, 0x188, 0, 0 |
| SendMessageA | 0x0040da7a | 0x0040d9e3 | live | edi, 0x18a, eax, 0 |
| SendMessageA | 0x0040dd67 | 0x0040d9e3 | live | ecx, 0x180, 0, eax |
| SendMessageA | 0x0040de5f | 0x0040d9e3 | live | esi, 0x186, 0, 0 |
| SendMessageA | 0x0040e203 | 0x0040e19b | live | eax, 0x188, edi, edi |
| SendMessageA | 0x0040e2af | 0x0040e19b | live | eax, 0x184, 0, 0 |
| SendMessageA | 0x0040e2f2 | 0x0040e19b | live | esi, 0x180, 0, eax |
| SendMessageA | 0x0040e305 | 0x0040e19b | live | esi, 0x186, 0, 0 |
| SendMessageA | 0x0041170b | 0x004116a3 | live | eax, 0x188, edi, edi |
| SendMessageA | 0x004117d3 | 0x004116a3 | live | eax, 0x184, 0, 0 |
| SendMessageA | 0x00411804 | 0x004116a3 | live | esi, 0x180, 0, eax |
| SendMessageA | 0x00411817 | 0x004116a3 | live | esi, 0x186, 0, 0 |
| SendMessageA | 0x0041d7e8 | 0x0041d724 | live | eax, 0x184, 0, 0 |
| SendMessageA | 0x0041d854 | 0x0041d724 | live | eax, 0x180, 0, eax |
| SendMessageA | 0x0041d8dd | 0x0041d724 | live | ebx, 0x180, 0, eax |
| SendMessageA | 0x0041d918 | 0x0041d724 | live | ebx, 0x180, 0, eax |
| SendMessageA | 0x0041d929 | 0x0041d724 | live | ebx, 0x186, 0, 0 |
| SendMessageA | 0x0041eb19 | 0x0041eaa7 | live | eax, 0x188, edi, edi |
| SendMessageA | 0x0041eba1 | 0x0041eaa7 | live | eax, 0x184, 0, 0 |
| SendMessageA | 0x0041ebc9 | 0x0041eaa7 | live | esi, 0x180, 0, eax |
| SendMessageA | 0x0041ebe6 | 0x0041eaa7 | live | esi, 0x186, eax, 0 |
| SendMessageA | 0x0041ec72 | 0x0041eaa7 | live | eax, 0x188, edi, edi |
| SendMessageA | 0x0041ed73 | 0x0041ecf6 | live | eax, 0x188, ebp, ebp |
| SendMessageA | 0x0041edfa | 0x0041ecf6 | live | eax, 0x184, 0, 0 |
| SendMessageA | 0x0041ee30 | 0x0041ecf6 | live | edi, 0x180, 0, eax |
| SendMessageA | 0x0041ee4d | 0x0041ecf6 | live | edi, 0x186, edx, 0 |
| SendMessageA | 0x004203d2 | 0x0042036d | live | eax, 0x188, edi, edi |
| SendMessageA | 0x004204af | 0x0042036d | live | eax, 0x184, 0, 0 |
| SendMessageA | 0x004204e0 | 0x0042036d | live | esi, 0x180, 0, eax |
| SendMessageA | 0x004204f3 | 0x0042036d | live | esi, 0x186, 0, 0 |
| SendMessageA | 0x00420541 | 0x0042036d | live | eax, 0x188, edi, edi |
| SendMessageA | 0x00424944 | 0x004248dc | live | eax, 0x188, edi, edi |
| SendMessageA | 0x00424a3f | 0x004248dc | live | eax, 0x184, 0, 0 |
| SendMessageA | 0x00424a70 | 0x004248dc | live | esi, 0x180, 0, eax |
| SendMessageA | 0x00424a8a | 0x004248dc | live | esi, 0x186, ebx, 0 |
| SendMessageA | 0x0042a796 | 0x0042a720 | live | eax, 0x188, ebp, ebp |
| SendMessageA | 0x0042a7ef | 0x0042a720 | live | eax, 0x182, esi, 0 |
| SendMessageA | 0x0042a880 | 0x0042a720 | live | edi, 0x186, eax, 0 |
| SendMessageA | 0x0042a8a0 | 0x0042a720 | live | eax, 0x188, ebp, ebp |
| SendMessageA | 0x0042a906 | 0x0042a720 | live | edi, 0x182, ebp, 0 |
| SendMessageA | 0x0042a928 | 0x0042a720 | live | edi, 0x186, eax, 0 |
| SendMessageA | 0x0042aa21 | 0x0042a720 | live | edi, 0x180, 0, eax |
| SendMessageA | 0x0042aa3b | 0x0042a720 | live | edi, 0x186, 0, 0 |
| SetBkColor | 0x0043cffd | 0x0043cf4f | live | eax, ecx |
| SetBkColor | 0x0043d149 | 0x0043d079 | live | ecx, ebp |
| SetBkMode | 0x004024de | 0x00402288 | live | ebx, 1 |
| SetBkMode | 0x0040d9ab | 0x0040d611 | live | esi, 1 |
| SetBkMode | 0x0040de75 | 0x0040d9e3 | live | ebx, 1 |
| SetBkMode | 0x0040df94 | 0x0040deab | live | ebx, 1 |
| SetBkMode | 0x0040e38a | 0x0040e19b | live | ebx, 1 |
| SetBkMode | 0x0040fc65 | 0x0040fb4d | live | ebx, 1 |
| SetBkMode | 0x004118a1 | 0x004116a3 | live | ebx, 1 |
| SetBkMode | 0x00413aa6 | 0x00413944 | live | ebx, 1 |
| SetBkMode | 0x00413c62 | 0x00413aec | live | ebx, 1 |
| SetBkMode | 0x0041d97b | 0x0041d724 | live | ebx, 1 |
| SetBkMode | 0x0041ea62 | 0x0041e912 | live | ebx, 1 |
| SetBkMode | 0x0041ec4e | 0x0041eaa7 | live | ebx, 1 |
| SetBkMode | 0x0041eec3 | 0x0041ecf6 | live | esi, 1 |
| SetBkMode | 0x0042051d | 0x0042036d | live | ebx, 1 |
| SetBkMode | 0x00422262 | 0x004220fb | live | ebx, 1 |
| SetBkMode | 0x00422409 | 0x004222a7 | live | ebx, 1 |
| SetBkMode | 0x00424b19 | 0x004248dc | live | ebx, 1 |
| SetBkMode | 0x0042aa62 | 0x0042a720 | live | ebx, 1 |
| SetBkMode | 0x0043cfdc | 0x0043cf4f | live | edx, 1 |
| SetBkMode | 0x0043cfec | 0x0043cf4f | live | edx, 2 |
| SetBkMode | 0x0043d12c | 0x0043d079 | live | edi, 1 |
| SetBkMode | 0x0043d13c | 0x0043d079 | live | eax, 2 |
| SetCursor | 0x00403bcf | 0x0040399d | live | esi |
| SetCursor | 0x00403c36 | 0x0040399d | live | edi |
| SetCursorPos | 0x00435f8a | 0x00435f4b | DEAD | ebp, eax |
| SetDlgItemTextA | 0x0040dcea | 0x0040d9e3 | live | ebp, 1, 0x44dfd2 |
| SetDlgItemTextA | 0x0040dcfc | 0x0040d9e3 | live | ebp, 0x93, 0x44dfdc |
| SetDlgItemTextA | 0x0040df56 | 0x0040deab | live | esi, 0x6d, 0x455628 |
| SetDlgItemTextA | 0x0040df65 | 0x0040deab | live | esi, 0x6e, 0x44e002 |
| SetDlgItemTextA | 0x0040e328 | 0x0040e19b | live | ebp, 0x70, 0x4550d8 |
| SetDlgItemTextA | 0x0040e35c | 0x0040e19b | live | ebp, 0x73, 0x4550d8 |
| SetDlgItemTextA | 0x0040fc18 | 0x0040fb4d | live | edi, 0x6d, 0x45e02c |
| SetDlgItemTextA | 0x0040fc36 | 0x0040fb4d | live | edi, 0x6e, eax |
| SetDlgItemTextA | 0x0041183f | 0x004116a3 | live | ebp, 0x70, 0x4550d8 |
| SetDlgItemTextA | 0x00411873 | 0x004116a3 | live | ebp, 0x73, 0x4550d8 |
| SetDlgItemTextA | 0x00413a45 | 0x00413944 | live | edi, 0x6b, eax |
| SetDlgItemTextA | 0x00413a52 | 0x00413944 | live | edi, 0x71, eax |
| SetDlgItemTextA | 0x00413bf6 | 0x00413aec | live | edi, 0x6a, eax |
| SetDlgItemTextA | 0x00413c1c | 0x00413aec | live | edi, 0x72, eax |
| SetDlgItemTextA | 0x0041ea0e | 0x0041e912 | live | edi, 0x6e, eax |
| SetDlgItemTextA | 0x0041ec09 | 0x0041eaa7 | live | ebp, 0x70, 0x4550d8 |
| SetDlgItemTextA | 0x0041ecb5 | 0x0041eaa7 | live | ebp, 0x73, 0x4550d8 |
| SetDlgItemTextA | 0x0041eddd | 0x0041ecf6 | live | esi, 0x73, 0x44e6c3 |
| SetDlgItemTextA | 0x0041ee77 | 0x0041ecf6 | live | ebx, 0x70, 0x4550d8 |
| SetDlgItemTextA | 0x00420584 | 0x0042036d | live | ebp, 0x73, 0x4550d8 |
| SetDlgItemTextA | 0x0042220b | 0x004220fb | live | edi, 0x6a, eax |
| SetDlgItemTextA | 0x00422230 | 0x004220fb | live | edi, 0x72, eax |
| SetDlgItemTextA | 0x004223a8 | 0x004222a7 | live | edi, 0x6b, eax |
| SetDlgItemTextA | 0x004223b5 | 0x004222a7 | live | edi, 0x71, eax |
| SetDlgItemTextA | 0x00424aad | 0x004248dc | live | ebp, 0x70, 0x4550d8 |
| SetDlgItemTextA | 0x00424aeb | 0x004248dc | live | ebp, 0x73, 0x4550d8 |
| SetMenu | 0x00433cbc | 0x00433bfc | DEAD | ecx, eax |
| SetMenu | 0x00433efd | 0x00433e0a | DEAD | eax, eax |
| SetScrollInfo | 0x0043450d | 0x00434453 | live | ebp, esi, eax, 1 |
| SetScrollInfo | 0x00434553 | 0x00434453 | live | ecx, 1, eax, 1 |
| SetScrollInfo | 0x0043466a | 0x004345b8 | DEAD | esi, 0, eax, 1 |
| SetScrollInfo | 0x004346dd | 0x004345b8 | DEAD | esi, 1, eax, 1 |
| SetScrollInfo | 0x00434856 | 0x0043475c | DEAD | edi, 0, eax, 1 |
| SetScrollInfo | 0x00434996 | 0x004348a0 | DEAD | edi, 1, eax, 1 |
| SetTextAlign | 0x0043cfb6 | 0x0043cf4f | live | edx, ebx |
| SetTextAlign | 0x0043d0f2 | 0x0043d079 | live | edx, ebx |
| SetTextColor | 0x004024d4 | 0x00402288 | live | ebx, 0 |
| SetTextColor | 0x0040d9a1 | 0x0040d611 | live | esi, 0 |
| SetTextColor | 0x0040de6b | 0x0040d9e3 | live | ebx, 0 |
| SetTextColor | 0x0040df86 | 0x0040deab | live | edx, 0 |
| SetTextColor | 0x0040e380 | 0x0040e19b | live | ebx, 0 |
| SetTextColor | 0x0040fc57 | 0x0040fb4d | live | edx, 0 |
| SetTextColor | 0x00411897 | 0x004116a3 | live | ebx, 0 |
| SetTextColor | 0x00413a95 | 0x00413944 | live | edx, 0 |
| SetTextColor | 0x00413c51 | 0x00413aec | live | edx, 0 |
| SetTextColor | 0x0041d971 | 0x0041d724 | live | ebx, 0 |
| SetTextColor | 0x0041ea51 | 0x0041e912 | live | edx, 0 |
| SetTextColor | 0x0041ec44 | 0x0041eaa7 | live | ebx, 0 |
| SetTextColor | 0x0041eeb9 | 0x0041ecf6 | live | esi, 0 |
| SetTextColor | 0x00420513 | 0x0042036d | live | ebx, 0 |
| SetTextColor | 0x00422258 | 0x004220fb | live | ebx, 0 |
| SetTextColor | 0x004223f8 | 0x004222a7 | live | edx, 0 |
| SetTextColor | 0x00424b0f | 0x004248dc | live | ebx, 0 |
| SetTextColor | 0x0042aa58 | 0x0042a720 | live | ebx, 0 |
| SetTextColor | 0x0043cfc7 | 0x0043cf4f | live | eax, ecx |
| SetTextColor | 0x0043d112 | 0x0043d079 | live | edi, edi |
| SetTimer | 0x00433cd0 | 0x00433bfc | DEAD | esi, 1, 1, 0 |
| SetTimer | 0x00433fca | 0x00433f2e | live | eax, 1, 0x3c, 0 |
| ShowCursor | 0x00435dd6 | 0x00435db5 | live | 1 |
| ShowCursor | 0x00435f04 | 0x00435ee9 | live | 1 |
| ShowCursor | 0x00435f41 | 0x00435f26 | live | 0 |
| ShowWindow | 0x00433d00 | 0x00433bfc | DEAD | edi, edi |
| ShowWindow | 0x00433f0c | 0x00433e0a | DEAD | edx, ebx |
| ShowWindow | 0x00433fe2 | 0x00433f2e | live | esi, edi |
| TextOutA | 0x0043d031 | 0x0043cf4f | live | edi, edi, ebp, ecx, eax |
| TrackPopupMenu | 0x00438e57 | 0x00438d56 | live | esi, 0, 0x64, 0x28, 0, ecx, 0 |
| TranslateMessage | 0x00404176 | 0x0040413f | live | eax |
| TranslateMessage | 0x00405891 | 0x00405862 | live | eax |
| TranslateMessage | 0x00416564 | 0x00416534 | live | eax |
| TranslateMessage | 0x00416595 | 0x00416534 | live | eax |
| TranslateMessage | 0x0041cdb7 | 0x0041c9ad | live | eax |
| TranslateMessage | 0x00438e8f | 0x00438d56 | live | eax |
| TranslateMessage | 0x0043a7cb | 0x0043a749 | live | eax |
| UpdateWindow | 0x00433d0d | 0x00433bfc | DEAD | eax |
| UpdateWindow | 0x00433f1a | 0x00433e0a | DEAD | ebx |
| UpdateWindow | 0x00433ff0 | 0x00433f2e | live | edi |
| wsprintfA | 0x00433b70 | 0x0043393d | live | 0x485f40, 0x44ef12, 0x44eef4 |
| wsprintfA | 0x00433cee | 0x00433bfc | DEAD | 0x485f40, 0x44ef57 |
| wsprintfA | 0x00433d29 | 0x00433bfc | DEAD | 0x485f40, 0x44ef71 |
| wsprintfA | 0x0043400c | 0x00433f2e | live | 0x485f40, 0x44efc7 |
| wsprintfA | 0x00434191 | 0x00434163 | live | 0x485f40, 0x44f046 |
| wsprintfA | 0x004341fb | 0x004341a7 | DEAD | 0x485f40, 0x44f078, eax |
| wsprintfA | 0x00434232 | 0x004341a7 | DEAD | 0x485f40, 0x44f0dd, 0x44f0b3 |
| wsprintfA | 0x00434a67 | 0x00434a1c | live | 0x485f40, 0x44f126 |
| wsprintfA | 0x00434ab8 | 0x00434a1c | live | 0x485f40, 0x44f16a, eax |
| wsprintfA | 0x00434ae1 | 0x00434a1c | live | 0x485f40, 0x44f1bc |
| wsprintfA | 0x00434b9a | 0x00434afe | DEAD | 0x485f40, 0x44f1d9 |
| wsprintfA | 0x00436ec0 | 0x00436e35 | live | edx, eax, 0x44f308 |
| wsprintfA | 0x00436fa1 | 0x00436e35 | live | edx, 0x44f408, eax |
| wsprintfA | 0x0043a546 | 0x0043a4df | live | eax, 0x44fc70, eax |
| wsprintfA | 0x0043def0 | 0x0043de84 | live | ebx, eax, ecx |
| wsprintfA | 0x0043e022 | 0x0043de84 | live | ebx, 0x44ff19, eax |
| wsprintfA | 0x00448035 | 0x00447db8 | live | eax, 0x45022a, ebp |
| wsprintfA | 0x0044807c | 0x00447db8 | live | eax, 0x450353, eax |

---

## Appendix B: dialog templates (VERIFIED, parsed from RT_DIALOG)

Rects are in dialog units (x, y, cx, cy). `ord N` is a numeric (0xFFFF) text field. All templates are DLGTEMPLATEEX except DEBUG_PERSO_DLG.

### AUSZAHLEN_DLG (used)

style 0x940008c0, exstyle 0x108, rect (77, 38, 155, 68), title 'Auszahlen', font 'Helv' 8pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 125 | STATIC | Account: | 0x50000000 | 0x0 | (11, 7, 40, 9) |
| 106 | STATIC | 12345678 | 0x50000000 | 0x0 | (75, 7, 44, 9) |
| 108 | STATIC | Pay out: | 0x50000000 | 0x0 | (11, 23, 41, 8) |
| 114 | EDIT | 123456789 | 0x50812080 | 0x20000 | (75, 22, 63, 12) |
| 2 | BUTTON | Cancel | 0x50010f00 | 0x0 | (91, 48, 52, 14) |
| 1 | BUTTON | OK | 0x50010f00 | 0x0 | (19, 48, 48, 14) |

### CD_NOT_FOUND_DLG (used)

style 0x10c402c4, exstyle 0x20008, rect (33, 22, 225, 95), title 'CD Finder', font 'Helv' 8pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 100 | STATIC | The Program is unable to find the CD-ROM! Please insert the right CD, or use the Browse Button to enter a new path! | 0x50000000 | 0x0 | (11, 17, 205, 39) |
| 101 | BUTTON | STATUS | 0x50000107 | 0x0 | (2, 2, 220, 64) |
| 1 | BUTTON | Retry | 0x50010f00 | 0x0 | (2, 74, 76, 14) |
| 150 | BUTTON | Browse | 0x50010f00 | 0x0 | (87, 74, 60, 14) |
| 3 | BUTTON | End Program | 0x50010f00 | 0x0 | (159, 74, 60, 14) |

### DEBUG_EDIT_EQUIP_DLG (UNUSED)

style 0x10c800c4, exstyle 0x0, rect (36, 30, 199, 134), title 'Edit Equipment', font 'Helv' 8pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 140 | EDIT |  | 0x50810080 | 0x20000 | (32, 4, 155, 12) |
| 141 | EDIT |  | 0x50810080 | 0x20000 | (45, 47, 73, 12) |
| 102 | EDIT |  | 0x50810080 | 0x20000 | (45, 71, 73, 12) |
| 142 | EDIT |  | 0x50810080 | 0x20000 | (45, 92, 73, 12) |
| 143 | BUTTON | Select Typ | 0x50010f00 | 0x0 | (128, 50, 59, 33) |
| 1 | BUTTON | Ok | 0x50010f00 | 0x0 | (11, 114, 51, 14) |
| 2 | BUTTON | Cancel | 0x50010f00 | 0x0 | (131, 114, 51, 14) |
| 100 | STATIC | Name: | 0x50000000 | 0x0 | (3, 7, 24, 8) |
| 103 | STATIC | Kaufpreis: | 0x50000000 | 0x0 | (3, 50, 35, 8) |
| 105 | STATIC | Zustand: | 0x50000000 | 0x0 | (3, 74, 30, 8) |
| 100 | STATIC | Equipment Typ: | 0x50000000 | 0x0 | (5, 26, 50, 8) |
| 144 | STATIC | kein Typ selektiert | 0x50000000 | 0x0 | (63, 26, 124, 8) |
| 100 | STATIC | Qualitaet: | 0x50000000 | 0x0 | (3, 94, 33, 8) |

### DEBUG_EDIT_PERSO_DLG (UNUSED)

style 0x10c800c6, exstyle 0x0, rect (20, 20, 269, 182), title 'Person bearbeiten', font 'Helv' 8pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 117 | EDIT | Kunigunda Knautsch | 0x50810080 | 0x20000 | (35, 4, 95, 12) |
| 120 | EDIT | Hat bisher nur Werbespots f³r Lutscher gedreht | 0x50810080 | 0x20000 | (51, 25, 209, 12) |
| 121 | EDIT |  | 0x50810080 | 0x20000 | (51, 39, 209, 12) |
| 122 | EDIT |  | 0x50810080 | 0x20000 | (51, 56, 209, 12) |
| 123 | EDIT | ord 25 | 0x50812080 | 0x20000 | (52, 74, 21, 12) |
| 124 | EDIT | 122 59 92 | 0x50810080 | 0x20000 | (52, 90, 47, 12) |
| 125 | EDIT | ord 162 | 0x50810080 | 0x20000 | (52, 105, 21, 12) |
| 126 | EDIT | ord 60 | 0x50812080 | 0x20000 | (52, 121, 23, 12) |
| 127 | EDIT | ord 100 | 0x50812080 | 0x20000 | (69, 137, 37, 12) |
| 128 | EDIT | ord 6 | 0x50812080 | 0x20000 | (111, 153, 19, 12) |
| 129 | EDIT | ord 7 | 0x50810080 | 0x20000 | (35, 165, 19, 12) |
| 130 | BUTTON |  Select Beruf  | 0x50010f00 | 0x0 | (184, 92, 77, 17) |
| 133 | BUTTON |  Select Portrait | 0x50010f00 | 0x0 | (184, 116, 77, 17) |
| 1 | BUTTON | OK | 0x50010f00 | 0x0 | (184, 138, 76, 14) |
| 2 | BUTTON | CANCEL | 0x50010f00 | 0x0 | (184, 160, 76, 14) |
| 206 | STATIC | Masse: | 0x50000000 | 0x0 | (12, 92, 23, 8) |
| 207 | STATIC | Gewicht: | 0x50000000 | 0x0 | (12, 123, 32, 8) |
| 208 | STATIC | Gehalt pro Tag: | 0x50000000 | 0x0 | (12, 139, 52, 8) |
| 201 | STATIC | Groesse: | 0x50000000 | 0x0 | (11, 107, 28, 8) |
| 200 | STATIC | Alter: | 0x50000000 | 0x0 | (12, 76, 20, 8) |
| 205 | STATIC | Erfahrung3: | 0x50000000 | 0x0 | (12, 58, 37, 8) |
| 204 | STATIC | Erfahrung2: | 0x50000000 | 0x0 | (12, 41, 37, 8) |
| 211 | STATIC | Erfahrung1: | 0x50000000 | 0x0 | (11, 27, 37, 8) |
| 203 | STATIC | Name: | 0x50000000 | 0x0 | (12, 5, 23, 8) |
| 202 | STATIC | Qualifikation/Geilheitspunkte: | 0x50000000 | 0x0 | (12, 155, 95, 8) |
| 209 | STATIC | Figur: | 0x50000000 | 0x0 | (12, 167, 20, 8) |
| 100 | STATIC | Beruf: | 0x50000000 | 0x0 | (129, 76, 19, 8) |
| 131 | STATIC |  | 0x50000000 | 0x0 | (151, 77, 111, 8) |

### DEBUG_PERSO_DLG (UNUSED)

style 0x10c800c6, exstyle 0x0, rect (73, 32, 223, 159), title 'Personal Pool bearbeiten', font 'Helv' 8pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 119 | LISTBOX |  | 0x50a100c3 | 0x0 | (5, 1, 215, 111) |
| 116 | BUTTON | EDIT CHUNK | 0x50010f00 | 0x0 | (28, 119, 59, 14) |
| 104 | BUTTON | ADD CHUNK | 0x50010f00 | 0x0 | (146, 119, 59, 14) |
| 1 | BUTTON | OK | 0x50010f00 | 0x0 | (53, 142, 117, 14) |

### DEBUG_PORTRAIT_DLG (UNUSED)

style 0x10c800c6, exstyle 0x0, rect (20, 40, 186, 121), title 'Select Portrait', font 'Helv' 8pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 137 | EDIT |   | 0x50810080 | 0x20000 | (6, 79, 35, 14) |
| 138 | BUTTON | GOTO | 0x50010f00 | 0x0 | (48, 79, 35, 14) |
| 108 | BUTTON | < | 0x50010f00 | 0x0 | (6, 37, 35, 14) |
| 135 | BUTTON | > | 0x50010f00 | 0x0 | (48, 37, 35, 14) |
| 105 | BUTTON | << | 0x50010f00 | 0x0 | (6, 55, 35, 14) |
| 134 | BUTTON | >> | 0x50010f00 | 0x0 | (48, 55, 35, 14) |
| 1 | BUTTON | OK | 0x50010f00 | 0x0 | (6, 103, 35, 14) |
| 2 | BUTTON | Cancel | 0x50010f00 | 0x0 | (48, 103, 35, 14) |
| 100 | STATIC | PoolAnz: | 0x50000000 | 0x0 | (7, 6, 31, 9) |
| 139 | STATIC | ord 1500 | 0x50000000 | 0x0 | (41, 7, 20, 8) |
| 102 | STATIC | Bilder | 0x50000000 | 0x0 | (65, 7, 20, 8) |
| 136 | STATIC | ord 1500 | 0x50000000 | 0x0 | (47, 23, 25, 8) |
| 112 | STATIC | Nr.: | 0x50000000 | 0x0 | (21, 23, 12, 8) |

### EINZAHLEN_DLG (used)

style 0x940008c4, exstyle 0x108, rect (132, 39, 169, 68), title 'Einzahlen', font 'Helv' 8pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 100 | STATIC | Account: | 0x50000000 | 0x0 | (5, 4, 29, 8) |
| 107 | STATIC | 1234567 | 0x50000000 | 0x0 | (49, 4, 43, 9) |
| 113 | EDIT | 12345678 | 0x50812080 | 0x20000 | (49, 24, 69, 12) |
| 103 | STATIC | Deposit: | 0x50000000 | 0x0 | (5, 26, 35, 9) |
| 1 | BUTTON | OK | 0x50010f01 | 0x20200 | (13, 49, 40, 14) |
| 2 | BUTTON | Cancel | 0x50010f00 | 0x20200 | (103, 50, 44, 14) |

### GAME_IO_DLG (used)

style 0x90400044, exstyle 0x0, rect (121, 15, 186, 140), title '', font 'Helv' 8pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 147 | STATIC | Save Game: | 0x50000000 | 0x0 | (7, 5, 53, 8) |
| 145 | LISTBOX |  | 0x50210040 | 0x20000 | (11, 22, 163, 89) |
| 1 | BUTTON | Save | 0x50010f00 | 0x0 | (27, 119, 53, 14) |
| 2 | BUTTON | Cancel | 0x50010f00 | 0x0 | (110, 119, 53, 14) |

### MAKLER_IMMO_DLG (UNUSED)

style 0x900800c4, exstyle 0x0, rect (69, 43, 243, 120), title 'Immobilie ansehen', font 'Helv' 8pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 110 | BUTTON | Empfang | 0x50000107 | 0x0 | (2, 2, 239, 112) |
| 101 | STATIC | Rent per month: | 0x50000000 | 0x0 | (14, 26, 52, 8) |
| 102 | STATIC | Rent per day: | 0x50000000 | 0x0 | (14, 40, 46, 8) |
| 103 | STATIC | Price: | 0x50000000 | 0x0 | (128, 40, 36, 9) |
| 105 | BUTTON | RENT | 0x50000107 | 0x0 | (9, 15, 113, 70) |
| 115 | STATIC | 12345678 | 0x50000002 | 0x0 | (66, 26, 35, 8) |
| 116 | STATIC | 12345678 | 0x50000002 | 0x0 | (66, 40, 35, 8) |
| 117 | BUTTON | Rent | 0x50010f00 | 0x20200 | (15, 58, 91, 20) |
| 109 | BUTTON | BUY | 0x50000107 | 0x0 | (122, 15, 113, 70) |
| 121 | BUTTON | Re-sell guaranteed | 0x50000103 | 0x0 | (128, 26, 84, 10) |
| 119 | STATIC | 12345678 | 0x50000000 | 0x0 | (167, 40, 43, 9) |
| 118 | BUTTON | Buy | 0x50010f00 | 0x20200 | (127, 58, 99, 21) |
| 1 | BUTTON | Go on | 0x50010f00 | 0x20200 | (23, 92, 191, 15) |

### PERSONALKOSTEN_LIST_BOX_DLG (used)

style 0x900000c4, exstyle 0x0, rect (9, 45, 283, 123), title 'Dialog', font 'Fixedsys' 9pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 111 | LISTBOX |  | 0x50a10040 | 0x20000 | (7, 23, 271, 70) |
| 1 | BUTTON | OK | 0x50010f00 | 0x0 | (92, 103, 99, 14) |
| 112 | STATIC | Staff costs: | 0x50000000 | 0x0 | (107, 2, 76, 8) |
| 115 | STATIC | Staff:                            Count:    Costs: | 0x50000000 | 0x0 | (10, 14, 264, 8) |

### STANDARD_GET_TXT_DLG (used)

style 0x900000c2, exstyle 0x0, rect (121, 40, 186, 50), title 'Dialog', font 'Helv' 8pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 109 | EDIT |  | 0x50810080 | 0x20000 | (19, 15, 152, 12) |
| 1 | BUTTON | OK | 0x50010f00 | 0x0 | (19, 33, 64, 14) |
| 2 | BUTTON | Cancel | 0x50010f00 | 0x0 | (107, 33, 64, 14) |
| 110 | STATIC | Enter text: | 0x50000000 | 0x0 | (17, 4, 143, 8) |

### STANDARD_LIST_BOX_DLG (used)

style 0x900000c4, exstyle 0x0, rect (20, 40, 283, 123), title 'Dialog', font 'Helv' 8pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 111 | LISTBOX |  | 0x50a10040 | 0x20000 | (7, 23, 271, 70) |
| 1 | BUTTON | OK | 0x50010f00 | 0x0 | (7, 103, 99, 14) |
| 2 | BUTTON | Cancel | 0x50010f00 | 0x0 | (179, 103, 99, 14) |
| 112 | STATIC | ListboxHeadline | 0x50000000 | 0x0 | (8, 2, 227, 8) |
| 115 | STATIC | In production | 0x50000000 | 0x0 | (117, 11, 52, 8) |

### WORK_ORDER_DLG (used)

style 0x900000c4, exstyle 0x200, rect (5, 29, 320, 130), title '', font 'Fixedsys' 9pt

| id | class | text | style | exstyle | rect |
|---|---|---|---|---|---|
| 100 | STATIC | Orders | 0x50000000 | 0x0 | (129, 4, 62, 8) |
| 148 | BUTTON | Accept | 0x50010f00 | 0x0 | (8, 108, 61, 14) |
| 149 | BUTTON | Remove | 0x50010f00 | 0x0 | (250, 109, 61, 14) |
| 1 | BUTTON | OK | 0x50010f00 | 0x0 | (108, 109, 104, 14) |
| 111 | LISTBOX |  | 0x50b000c0 | 0x20000 | (8, 17, 303, 78) |

