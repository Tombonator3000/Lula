# WET.EXE: PE resources and game-code map (host runtime spec)

Scope: the `.rsrc` section of `original/app/WET.EXE` (SHA-256 `8c223b52...cea9`) and a module-level map of the game code, for the static recompilation (guest address == host offset, IAT slots routed to host C). Analysis only; nothing here changes the binary.

Evidence levels used below:

- **VERIFIED**: read directly from instruction bytes, the relocation table, the IAT or resource data, or reproduced by a script over the image.
- **INFERRED**: a role or name derived from strings, file names, call structure or control flow that was not executed. Room/subsystem names are always INFERRED unless stated otherwise.

Method (reproducible): `pefile` for the resource tree; every absolute reference was found through the `.reloc` table (14,980 HIGHLOW fixups), so each listed import call site is exhaustive for direct `call cs:[IAT]` references. `capstone` and `analysis/binary/disassembly.asm` were used for instruction context. Room setup functions were mapped per stage by emulating the room "enter" functions with `unicorn`, stubbing every call. Watcom ABI: arguments in EAX, EDX, EBX, ECX, then on the stack; `ret N` pops stack arguments. Every compiled game function starts with `push <frame>; call 0x43371d` (`__CHK`, the Watcom stack probe), which is used below to find real function starts.

Out of scope and only listed (section B.12): video/AVI/CUT, `CoCreateInstance`, `CoInitialize`, `CoUninitialize` and `mciSendCommandA`.

---

## Part A: the resource section

### A.1 Section facts (VERIFIED)

| Item | Value |
|---|---|
| Section | `.rsrc`, RVA 0x94000 (VA 0x494000), VirtualSize 0x11c9c, raw offset 0x5c000, raw size 0x11e00, characteristics 0x40000040 |
| Data directory 2 | RVA 0x94000, size 0x11c9c |
| Root directory | Characteristics 0, TimeDateStamp 0, version 4.0; all sub-directory timestamps 0 |
| Leaves | 119: 1 RT_ICON, 13 RT_DIALOG, 102 RT_STRING blocks, 1 RT_RCDATA, 1 RT_GROUP_ICON, 1 RT_MANIFEST |
| Absent types | RT_MENU, RT_CURSOR, RT_GROUP_CURSOR, RT_BITMAP, RT_ACCELERATOR, RT_VERSION, RT_FONT, RT_MESSAGETABLE, custom types. There is **no VERSIONINFO** and **no menu resource**. |
| Resource-related imports | `LoadStringA`, `LoadMenuA`, `LoadIconA`, `LoadCursorA`, `DialogBoxParamA` (game IAT block). No `FindResource*`, `LoadResource`, `LoadBitmapA`, `LoadAcceleratorsA`, `CreateDialog*` imports. All resource access goes through those five functions. |

Other section facts that matter to the loader: every section except `.rsrc` has VirtualSize 0 (Watcom linker), so mapped sizes come from SizeOfRawData (BEGTEXT 0x4c000 from VA 0x401000; `.bss` 0x35a00 from VA 0x455000 with no file data).

### A.2 Inventory (VERIFIED)

| # | Type | Name / ID | Lang | Data RVA (VA) | Size | Notes |
|---|---|---|---|---|---|---|
| 1 | RT_ICON (3) | 1 | 0x0 | 0x958e8 (0x4958e8) | 3752 | 48x48, 8 bpp (256-colour palette), BITMAPINFOHEADER biHeight=96 (XOR+AND) |
| 2 | RT_DIALOG (5) | AUSZAHLEN_DLG | 0x0 | 0x96790 (0x496790) | 348 |  |
| 3 | RT_DIALOG (5) | CD_NOT_FOUND_DLG | 0x0 | 0x968ec (0x4968ec) | 540 |  |
| 4 | RT_DIALOG (5) | DEBUG_EDIT_EQUIP_DLG | 0x0 | 0x96b08 (0x496b08) | 668 |  |
| 5 | RT_DIALOG (5) | DEBUG_EDIT_PERSO_DLG | 0x0 | 0x96da4 (0x496da4) | 1500 |  |
| 6 | RT_DIALOG (5) | DEBUG_PERSO_DLG | 0x0 | 0x97380 (0x497380) | 252 |  |
| 7 | RT_DIALOG (5) | DEBUG_PORTRAIT_DLG | 0x0 | 0x9747c (0x49747c) | 604 |  |
| 8 | RT_DIALOG (5) | EINZAHLEN_DLG | 0x0 | 0x976d8 (0x4976d8) | 348 |  |
| 9 | RT_DIALOG (5) | GAME_IO_DLG | 0x0 | 0x97834 (0x497834) | 220 |  |
| 10 | RT_DIALOG (5) | MAKLER_IMMO_DLG | 0x0 | 0x97910 (0x497910) | 732 |  |
| 11 | RT_DIALOG (5) | PERSONALKOSTEN_LIST_BOX_DLG | 0x0 | 0x97bec (0x497bec) | 348 |  |
| 12 | RT_DIALOG (5) | STANDARD_GET_TXT_DLG | 0x0 | 0x97d48 (0x497d48) | 228 |  |
| 13 | RT_DIALOG (5) | STANDARD_LIST_BOX_DLG | 0x0 | 0x97e2c (0x497e2c) | 316 |  |
| 14 | RT_DIALOG (5) | WORK_ORDER_DLG | 0x0 | 0x97f68 (0x497f68) | 284 |  |
| 117 | RT_RCDATA (10) | DLGINCLUDE | 0x0 | 0xa5aac (0x4a5aac) | 34 | ASCII path of the RC include file |
| 118 | RT_GROUP_ICON (14) | WET | 0x0 | 0xa5ad0 (0x4a5ad0) | 20 | 1 entry -> RT_ICON 1 (48x48, 8 bpp, 3752 bytes) |
| 119 | RT_MANIFEST (24) | 1 | 0x409 | 0xa5ae4 (0x4a5ae4) | 440 | XML, dpiAware True/PM, dpiAwareness PerMonitor |

RT_STRING blocks (all language 0x407, code page 1252 in the data entry):

| Block | IDs (block*16-16 .. +15) | Present IDs | Data VA | Size |
|---|---|---|---|---|
| 7 | 96-111 | 11 | 0x498084 | 324 |
| 8 | 112-127 | 3 | 0x4981c8 | 116 |
| 13 | 192-207 | 7 | 0x49823c | 138 |
| 14 | 208-223 | 14 | 0x4982c8 | 974 |
| 16 | 240-255 | 6 | 0x498698 | 358 |
| 17 | 256-271 | 16 | 0x498800 | 520 |
| 18 | 272-287 | 2 | 0x498a08 | 230 |
| 19 | 288-303 | 3 | 0x498af0 | 110 |
| 20 | 304-319 | 16 | 0x498b60 | 474 |
| 21 | 320-335 | 3 | 0x498d3c | 218 |
| 22 | 336-351 | 2 | 0x498e18 | 74 |
| 23 | 352-367 | 14 | 0x498e64 | 814 |
| 26 | 400-415 | 13 | 0x499194 | 986 |
| 32 | 496-511 | 11 | 0x499570 | 620 |
| 33 | 512-527 | 16 | 0x4997dc | 1146 |
| 34 | 528-543 | 1 | 0x499c58 | 88 |
| 38 | 592-607 | 8 | 0x499cb0 | 828 |
| 39 | 608-623 | 16 | 0x499fec | 1936 |
| 40 | 624-639 | 16 | 0x49a77c | 1910 |
| 41 | 640-655 | 16 | 0x49aef4 | 1670 |
| 42 | 656-671 | 2 | 0x49b57c | 334 |
| 44 | 688-703 | 4 | 0x49b6cc | 204 |
| 45 | 704-719 | 16 | 0x49b798 | 698 |
| 46 | 720-735 | 16 | 0x49ba54 | 312 |
| 47 | 736-751 | 16 | 0x49bb8c | 602 |
| 48 | 752-767 | 16 | 0x49bde8 | 670 |
| 49 | 768-783 | 16 | 0x49c088 | 682 |
| 50 | 784-799 | 9 | 0x49c334 | 842 |
| 51 | 800-815 | 16 | 0x49c680 | 728 |
| 54 | 848-863 | 13 | 0x49c958 | 992 |
| 57 | 896-911 | 11 | 0x49cd38 | 800 |
| 58 | 912-927 | 16 | 0x49d058 | 450 |
| 59 | 928-943 | 15 | 0x49d21c | 504 |
| 60 | 944-959 | 15 | 0x49d414 | 670 |
| 61 | 960-975 | 16 | 0x49d6b4 | 660 |
| 62 | 976-991 | 16 | 0x49d948 | 570 |
| 63 | 992-1007 | 12 | 0x49db84 | 378 |
| 69 | 1088-1103 | 3 | 0x49dd00 | 136 |
| 72 | 1136-1151 | 2 | 0x49dd88 | 210 |
| 73 | 1152-1167 | 1 | 0x49de5c | 190 |
| 76 | 1200-1215 | 15 | 0x49df1c | 1386 |
| 77 | 1216-1231 | 4 | 0x49e488 | 594 |
| 82 | 1296-1311 | 1 | 0x49e6dc | 58 |
| 88 | 1392-1407 | 7 | 0x49e718 | 330 |
| 89 | 1408-1423 | 7 | 0x49e864 | 320 |
| 94 | 1488-1503 | 3 | 0x49e9a4 | 182 |
| 95 | 1504-1519 | 4 | 0x49ea5c | 252 |
| 101 | 1600-1615 | 15 | 0x49eb58 | 1606 |
| 102 | 1616-1631 | 1 | 0x49f1a0 | 198 |
| 104 | 1648-1663 | 4 | 0x49f268 | 374 |
| 107 | 1696-1711 | 4 | 0x49f3e0 | 418 |
| 113 | 1792-1807 | 7 | 0x49f584 | 422 |
| 114 | 1808-1823 | 2 | 0x49f72c | 74 |
| 116 | 1840-1855 | 6 | 0x49f778 | 384 |
| 119 | 1888-1903 | 3 | 0x49f8f8 | 250 |
| 120 | 1904-1919 | 5 | 0x49f9f4 | 518 |
| 126 | 2000-2015 | 16 | 0x49fbfc | 1072 |
| 127 | 2016-2031 | 16 | 0x4a002c | 1396 |
| 128 | 2032-2047 | 16 | 0x4a05a0 | 932 |
| 129 | 2048-2063 | 16 | 0x4a0944 | 624 |
| 130 | 2064-2079 | 12 | 0x4a0bb4 | 766 |
| 132 | 2096-2111 | 12 | 0x4a0eb4 | 760 |
| 133 | 2112-2127 | 16 | 0x4a11ac | 724 |
| 134 | 2128-2143 | 16 | 0x4a1480 | 736 |
| 135 | 2144-2159 | 10 | 0x4a1760 | 356 |
| 138 | 2192-2207 | 8 | 0x4a18c4 | 570 |
| 139 | 2208-2223 | 4 | 0x4a1b00 | 402 |
| 194 | 3088-3103 | 3 | 0x4a1c94 | 72 |
| 195 | 3104-3119 | 16 | 0x4a1cdc | 1340 |
| 196 | 3120-3135 | 1 | 0x4a2218 | 178 |
| 201 | 3200-3215 | 12 | 0x4a22cc | 354 |
| 202 | 3216-3231 | 12 | 0x4a2430 | 1044 |
| 213 | 3392-3407 | 3 | 0x4a2844 | 98 |
| 269 | 4288-4303 | 3 | 0x4a28a8 | 112 |
| 270 | 4304-4319 | 5 | 0x4a2918 | 436 |
| 276 | 4400-4415 | 15 | 0x4a2acc | 430 |
| 277 | 4416-4431 | 2 | 0x4a2c7c | 84 |
| 282 | 4496-4511 | 11 | 0x4a2cd0 | 320 |
| 283 | 4512-4527 | 1 | 0x4a2e10 | 148 |
| 288 | 4592-4607 | 6 | 0x4a2ea4 | 340 |
| 294 | 4688-4703 | 3 | 0x4a2ff8 | 188 |
| 295 | 4704-4719 | 1 | 0x4a30b4 | 54 |
| 301 | 4800-4815 | 9 | 0x4a30ec | 668 |
| 307 | 4896-4911 | 5 | 0x4a3388 | 200 |
| 313 | 4992-5007 | 7 | 0x4a3450 | 148 |
| 314 | 5008-5023 | 16 | 0x4a34e4 | 1024 |
| 315 | 5024-5039 | 16 | 0x4a38e4 | 1262 |
| 316 | 5040-5055 | 2 | 0x4a3dd4 | 228 |
| 319 | 5088-5103 | 3 | 0x4a3eb8 | 100 |
| 320 | 5104-5119 | 6 | 0x4a3f1c | 230 |
| 326 | 5200-5215 | 13 | 0x4a4004 | 636 |
| 332 | 5296-5311 | 12 | 0x4a4280 | 480 |
| 333 | 5312-5327 | 16 | 0x4a4460 | 572 |
| 334 | 5328-5343 | 16 | 0x4a469c | 382 |
| 335 | 5344-5359 | 16 | 0x4a481c | 418 |
| 336 | 5360-5375 | 16 | 0x4a49c0 | 436 |
| 337 | 5376-5391 | 11 | 0x4a4b74 | 230 |
| 338 | 5392-5407 | 8 | 0x4a4c5c | 1194 |
| 339 | 5408-5423 | 7 | 0x4a5108 | 1426 |
| 501 | 8000-8015 | 16 | 0x4a569c | 392 |
| 502 | 8016-8031 | 16 | 0x4a5824 | 370 |
| 503 | 8032-8047 | 11 | 0x4a5998 | 274 |

Leaf numbers follow directory order; leaves 15-116 are the 102 string blocks in the second table.

String text is in RT_STRING language 0x407 (German) even though nearly all strings are English: the resources were translated in place. Dialogs and icon use language 0 (neutral). Only the manifest has language 0x409.

### A.3 Non-dialog, non-string resources

| Resource | Content (VERIFIED) | Runtime use |
|---|---|---|
| RT_GROUP_ICON "WET" | `00 00 01 00 01 00` + one entry: 48x48, 0 colours, planes 1, 8 bpp, 3752 bytes, ID 1 | Not loaded by code. `LoadIconA` is only called as `LoadIconA(NULL, IDI_APPLICATION=0x7f00)` (0x4338a4). Used by the shell for the EXE icon only. |
| RT_ICON 1 | BITMAPINFOHEADER: biSize 40, 48x96, 1 plane, 8 bpp, biSizeImage 2688 (2304 XOR + 384 AND), 256-entry palette | Unreferenced by code. |
| RT_RCDATA "DLGINCLUDE" | `d:\prg\PROJEKTE\WET\RES\wet_dia.h` + NUL (34 bytes) | Resource-editor artefact (dialog-ID header path). Unreferenced. |
| RT_MANIFEST 1 (lang 0x409) | `<assembly ... manifestVersion="1.0">` with `dpiAware True/PM` and `dpiAwareness PerMonitor` (2016 namespace) | Read by the Windows loader only. INFERRED: added after 1997 (the SMI/2016 namespace postdates the 1997 PE timestamp), probably by the repack. The host must ignore it. |

### A.4 Dialog templates

13 templates: 12 are DLGTEMPLATEEX (`01 00 FF FF` signature), DEBUG_PERSO_DLG is a classic DLGTEMPLATE. No template has a menu or a window class. All use DS_SETFONT. Fonts: "Helv" 8 pt (11 templates) and "Fixedsys" 9 pt (PERSONALKOSTEN_LIST_BOX_DLG, WORK_ORDER_DLG).

Peculiarities that a template parser must survive (VERIFIED):

- EDIT controls 123, 125, 126, 127, 128 and 129 in DEBUG_EDIT_PERSO_DLG, and STATIC 136/139 in DEBUG_PORTRAIT_DLG, have an **ordinal title** (`FFFF nnnn`) instead of a string. All of them are in unreferenced debug templates.
- Templates end with slack bytes after the last item (2 to 28 bytes). Some slack contains remnants of the German originals (for example "Abbrechen" in PERSONALKOSTEN_LIST_BOX_DLG). Parse by item count, never by size.
- Several control IDs repeat (ID 100 three times in DEBUG_EDIT_EQUIP_DLG). These are static labels only.
- Captions of the bank dialogs are German ("Einzahlen" = deposit, "Auszahlen" = withdraw) while their controls are English.

#### AUSZAHLEN_DLG (VA 0x496790, 348 bytes, DLGTEMPLATEEX) - referenced by code: yes

Style 0x940008c0 = WS_POPUP|WS_VISIBLE|WS_CLIPSIBLINGS | DS_SETFONT|DS_MODALFRAME|DS_CENTER; exStyle 0x108; rect (DLU) x=77 y=38 cx=155 cy=68; caption "Auszahlen"; font 8 pt "Helv"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 125 | STATIC | "Account:" | 11,7,40,9 | 0x50000000 SS_LEFT | 0x0 |
| 106 | STATIC | "12345678" | 75,7,44,9 | 0x50000000 SS_LEFT | 0x0 |
| 108 | STATIC | "Pay out:" | 11,23,41,8 | 0x50000000 SS_LEFT | 0x0 |
| 114 | EDIT | "123456789" | 75,22,63,12 | 0x50812080 ES_AUTOHSCROLL+ES_NUMBER+WS_BORDER+WS_TABSTOP | 0x20000 |
| 2 | BUTTON | "Cancel" | 91,48,52,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 1 | BUTTON | "OK" | 19,48,48,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |

Parsed 344 of 348 bytes; the remaining 4 bytes are slack.

#### CD_NOT_FOUND_DLG (VA 0x4968ec, 540 bytes, DLGTEMPLATEEX) - referenced by code: yes

Style 0x10c402c4 = WS_VISIBLE|WS_BORDER|WS_DLGFRAME|WS_THICKFRAME | DS_SETFONT|DS_MODALFRAME|DS_SETFOREGROUND|DS_3DLOOK; exStyle 0x20008; rect (DLU) x=33 y=22 cx=225 cy=95; caption "CD Finder"; font 8 pt "Helv"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 100 | STATIC | "The Program is unable to find the CD-ROM! Please insert the right CD, or use the Browse Button to enter a new path!" | 11,17,205,39 | 0x50000000 SS_LEFT | 0x0 |
| 101 | BUTTON | "STATUS" | 2,2,220,64 | 0x50000107 BS_GROUPBOX | 0x0 |
| 1 | BUTTON | "Retry" | 2,74,76,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 150 | BUTTON | "Browse" | 87,74,60,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 3 | BUTTON | "End Program" | 159,74,60,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |

Parsed 518 of 540 bytes; the remaining 22 bytes are slack.

#### DEBUG_EDIT_EQUIP_DLG (VA 0x496b08, 668 bytes, DLGTEMPLATEEX) - referenced by code: **no** (no name string in the image)

Style 0x10c800c4 = WS_VISIBLE|WS_BORDER|WS_DLGFRAME|WS_SYSMENU | DS_SETFONT|DS_MODALFRAME|DS_3DLOOK; exStyle 0x0; rect (DLU) x=36 y=30 cx=199 cy=134; caption "Edit Equipment"; font 8 pt "Helv"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 140 | EDIT | "" | 32,4,155,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 141 | EDIT | "" | 45,47,73,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 102 | EDIT | "" | 45,71,73,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 142 | EDIT | "" | 45,92,73,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 143 | BUTTON | "Select Typ" | 128,50,59,33 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 1 | BUTTON | "Ok" | 11,114,51,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 2 | BUTTON | "Cancel" | 131,114,51,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 100 | STATIC | "Name:" | 3,7,24,8 | 0x50000000 SS_LEFT | 0x0 |
| 103 | STATIC | "Kaufpreis:" | 3,50,35,8 | 0x50000000 SS_LEFT | 0x0 |
| 105 | STATIC | "Zustand:" | 3,74,30,8 | 0x50000000 SS_LEFT | 0x0 |
| 100 | STATIC | "Equipment Typ:" | 5,26,50,8 | 0x50000000 SS_LEFT | 0x0 |
| 144 | STATIC | "kein Typ selektiert" | 63,26,124,8 | 0x50000000 SS_LEFT | 0x0 |
| 100 | STATIC | "Qualitaet:" | 3,94,33,8 | 0x50000000 SS_LEFT | 0x0 |

Parsed 664 of 668 bytes; the remaining 4 bytes are slack.

#### DEBUG_EDIT_PERSO_DLG (VA 0x496da4, 1500 bytes, DLGTEMPLATEEX) - referenced by code: **no** (no name string in the image)

Style 0x10c800c6 = WS_VISIBLE|WS_BORDER|WS_DLGFRAME|WS_SYSMENU | DS_SYSMODAL|DS_SETFONT|DS_MODALFRAME|DS_3DLOOK; exStyle 0x0; rect (DLU) x=20 y=20 cx=269 cy=182; caption "Person bearbeiten"; font 8 pt "Helv"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 117 | EDIT | "Kunigunda Knautsch" | 35,4,95,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 120 | EDIT | "Hat bisher nur Werbespots f³r Lutscher gedreht" | 51,25,209,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 121 | EDIT | "" | 51,39,209,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 122 | EDIT | "" | 51,56,209,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 123 | EDIT | ordinal 25 | 52,74,21,12 | 0x50812080 ES_AUTOHSCROLL+ES_NUMBER+WS_BORDER+WS_TABSTOP | 0x20000 |
| 124 | EDIT | "122 59 92" | 52,90,47,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 125 | EDIT | ordinal 162 | 52,105,21,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 126 | EDIT | ordinal 60 | 52,121,23,12 | 0x50812080 ES_AUTOHSCROLL+ES_NUMBER+WS_BORDER+WS_TABSTOP | 0x20000 |
| 127 | EDIT | ordinal 100 | 69,137,37,12 | 0x50812080 ES_AUTOHSCROLL+ES_NUMBER+WS_BORDER+WS_TABSTOP | 0x20000 |
| 128 | EDIT | ordinal 6 | 111,153,19,12 | 0x50812080 ES_AUTOHSCROLL+ES_NUMBER+WS_BORDER+WS_TABSTOP | 0x20000 |
| 129 | EDIT | ordinal 7 | 35,165,19,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 130 | BUTTON | " Select Beruf " | 184,92,77,17 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 133 | BUTTON | " Select Portrait" | 184,116,77,17 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 1 | BUTTON | "OK" | 184,138,76,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 2 | BUTTON | "CANCEL" | 184,160,76,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 206 | STATIC | "Masse:" | 12,92,23,8 | 0x50000000 SS_LEFT | 0x0 |
| 207 | STATIC | "Gewicht:" | 12,123,32,8 | 0x50000000 SS_LEFT | 0x0 |
| 208 | STATIC | "Gehalt pro Tag:" | 12,139,52,8 | 0x50000000 SS_LEFT | 0x0 |
| 201 | STATIC | "Groesse:" | 11,107,28,8 | 0x50000000 SS_LEFT | 0x0 |
| 200 | STATIC | "Alter:" | 12,76,20,8 | 0x50000000 SS_LEFT | 0x0 |
| 205 | STATIC | "Erfahrung3:" | 12,58,37,8 | 0x50000000 SS_LEFT | 0x0 |
| 204 | STATIC | "Erfahrung2:" | 12,41,37,8 | 0x50000000 SS_LEFT | 0x0 |
| 211 | STATIC | "Erfahrung1:" | 11,27,37,8 | 0x50000000 SS_LEFT | 0x0 |
| 203 | STATIC | "Name:" | 12,5,23,8 | 0x50000000 SS_LEFT | 0x0 |
| 202 | STATIC | "Qualifikation/Geilheitspunkte:" | 12,155,95,8 | 0x50000000 SS_LEFT | 0x0 |
| 209 | STATIC | "Figur:" | 12,167,20,8 | 0x50000000 SS_LEFT | 0x0 |
| 100 | STATIC | "Beruf:" | 129,76,19,8 | 0x50000000 SS_LEFT | 0x0 |
| 131 | STATIC | "" | 151,77,111,8 | 0x50000000 SS_LEFT | 0x0 |

Parsed 1484 of 1500 bytes; the remaining 16 bytes are slack.

#### DEBUG_PERSO_DLG (VA 0x497380, 252 bytes, DLGTEMPLATE (classic)) - referenced by code: **no** (no name string in the image)

Style 0x10c800c6 = WS_VISIBLE|WS_BORDER|WS_DLGFRAME|WS_SYSMENU | DS_SYSMODAL|DS_SETFONT|DS_MODALFRAME|DS_3DLOOK; exStyle 0x0; rect (DLU) x=73 y=32 cx=223 cy=159; caption "Personal Pool bearbeiten"; font 8 pt "Helv"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 119 | LISTBOX | "" | 5,1,215,111 | 0x50a100c3 LBS_NOTIFY+LBS_SORT+LBS_HASSTRINGS+LBS_USETABSTOPS+WS_BORDER+WS_VSCROLL+WS_TABSTOP | 0x0 |
| 116 | BUTTON | "EDIT CHUNK" | 28,119,59,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 104 | BUTTON | "ADD CHUNK" | 146,119,59,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 1 | BUTTON | "OK" | 53,142,117,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |

Parsed 234 of 252 bytes; the remaining 18 bytes are slack.

#### DEBUG_PORTRAIT_DLG (VA 0x49747c, 604 bytes, DLGTEMPLATEEX) - referenced by code: **no** (no name string in the image)

Style 0x10c800c6 = WS_VISIBLE|WS_BORDER|WS_DLGFRAME|WS_SYSMENU | DS_SYSMODAL|DS_SETFONT|DS_MODALFRAME|DS_3DLOOK; exStyle 0x0; rect (DLU) x=20 y=40 cx=186 cy=121; caption "Select Portrait"; font 8 pt "Helv"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 137 | EDIT | " " | 6,79,35,14 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 138 | BUTTON | "GOTO" | 48,79,35,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 108 | BUTTON | "<" | 6,37,35,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 135 | BUTTON | ">" | 48,37,35,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 105 | BUTTON | "<<" | 6,55,35,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 134 | BUTTON | ">>" | 48,55,35,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 1 | BUTTON | "OK" | 6,103,35,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 2 | BUTTON | "Cancel" | 48,103,35,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 100 | STATIC | "PoolAnz:" | 7,6,31,9 | 0x50000000 SS_LEFT | 0x0 |
| 139 | STATIC | ordinal 1500 | 41,7,20,8 | 0x50000000 SS_LEFT | 0x0 |
| 102 | STATIC | "Bilder" | 65,7,20,8 | 0x50000000 SS_LEFT | 0x0 |
| 136 | STATIC | ordinal 1500 | 47,23,25,8 | 0x50000000 SS_LEFT | 0x0 |
| 112 | STATIC | "Nr.:" | 21,23,12,8 | 0x50000000 SS_LEFT | 0x0 |

Parsed 584 of 604 bytes; the remaining 20 bytes are slack.

#### EINZAHLEN_DLG (VA 0x4976d8, 348 bytes, DLGTEMPLATEEX) - referenced by code: yes

Style 0x940008c4 = WS_POPUP|WS_VISIBLE|WS_CLIPSIBLINGS | DS_SETFONT|DS_MODALFRAME|DS_3DLOOK|DS_CENTER; exStyle 0x108; rect (DLU) x=132 y=39 cx=169 cy=68; caption "Einzahlen"; font 8 pt "Helv"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 100 | STATIC | "Account:" | 5,4,29,8 | 0x50000000 SS_LEFT | 0x0 |
| 107 | STATIC | "1234567" | 49,4,43,9 | 0x50000000 SS_LEFT | 0x0 |
| 113 | EDIT | "12345678" | 49,24,69,12 | 0x50812080 ES_AUTOHSCROLL+ES_NUMBER+WS_BORDER+WS_TABSTOP | 0x20000 |
| 103 | STATIC | "Deposit:" | 5,26,35,9 | 0x50000000 SS_LEFT | 0x0 |
| 1 | BUTTON | "OK" | 13,49,40,14 | 0x50010f01 BS_DEFPUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x20200 |
| 2 | BUTTON | "Cancel" | 103,50,44,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x20200 |

Parsed 340 of 348 bytes; the remaining 8 bytes are slack.

#### GAME_IO_DLG (VA 0x497834, 220 bytes, DLGTEMPLATEEX) - referenced by code: yes

Style 0x90400044 = WS_POPUP|WS_VISIBLE|WS_DLGFRAME | DS_SETFONT|DS_3DLOOK; exStyle 0x0; rect (DLU) x=121 y=15 cx=186 cy=140; caption ""; font 8 pt "Helv"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 147 | STATIC | "Save Game:" | 7,5,53,8 | 0x50000000 SS_LEFT | 0x0 |
| 145 | LISTBOX | "" | 11,22,163,89 | 0x50210040 LBS_HASSTRINGS+WS_VSCROLL+WS_TABSTOP | 0x20000 |
| 1 | BUTTON | "Save" | 27,119,53,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 2 | BUTTON | "Cancel" | 110,119,53,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |

Parsed 216 of 220 bytes; the remaining 4 bytes are slack.

#### MAKLER_IMMO_DLG (VA 0x497910, 732 bytes, DLGTEMPLATEEX) - referenced by code: **no** (no name string in the image)

Style 0x900800c4 = WS_POPUP|WS_VISIBLE|WS_SYSMENU | DS_SETFONT|DS_MODALFRAME|DS_3DLOOK; exStyle 0x0; rect (DLU) x=69 y=43 cx=243 cy=120; caption "Immobilie ansehen"; font 8 pt "Helv"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 110 | BUTTON | "Empfang" | 2,2,239,112 | 0x50000107 BS_GROUPBOX | 0x0 |
| 101 | STATIC | "Rent per month:" | 14,26,52,8 | 0x50000000 SS_LEFT | 0x0 |
| 102 | STATIC | "Rent per day:" | 14,40,46,8 | 0x50000000 SS_LEFT | 0x0 |
| 103 | STATIC | "Price:" | 128,40,36,9 | 0x50000000 SS_LEFT | 0x0 |
| 105 | BUTTON | "RENT" | 9,15,113,70 | 0x50000107 BS_GROUPBOX | 0x0 |
| 115 | STATIC | "12345678" | 66,26,35,8 | 0x50000002 SS_RIGHT | 0x0 |
| 116 | STATIC | "12345678" | 66,40,35,8 | 0x50000002 SS_RIGHT | 0x0 |
| 117 | BUTTON | "Rent" | 15,58,91,20 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x20200 |
| 109 | BUTTON | "BUY" | 122,15,113,70 | 0x50000107 BS_GROUPBOX | 0x0 |
| 121 | BUTTON | "Re-sell guaranteed" | 128,26,84,10 | 0x50000103 BS_AUTOCHECKBOX | 0x0 |
| 119 | STATIC | "12345678" | 167,40,43,9 | 0x50000000 SS_LEFT | 0x0 |
| 118 | BUTTON | "Buy" | 127,58,99,21 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x20200 |
| 1 | BUTTON | "Go on" | 23,92,191,15 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x20200 |

Parsed 714 of 732 bytes; the remaining 18 bytes are slack.

#### PERSONALKOSTEN_LIST_BOX_DLG (VA 0x497bec, 348 bytes, DLGTEMPLATEEX) - referenced by code: yes

Style 0x900000c4 = WS_POPUP|WS_VISIBLE | DS_SETFONT|DS_MODALFRAME|DS_3DLOOK; exStyle 0x0; rect (DLU) x=9 y=45 cx=283 cy=123; caption "Dialog"; font 9 pt "Fixedsys"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 111 | LISTBOX | "" | 7,23,271,70 | 0x50a10040 LBS_HASSTRINGS+WS_BORDER+WS_VSCROLL+WS_TABSTOP | 0x20000 |
| 1 | BUTTON | "OK" | 92,103,99,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 112 | STATIC | "Staff costs:" | 107,2,76,8 | 0x50000000 SS_LEFT | 0x0 |
| 115 | STATIC | "Staff:                            Count:    Costs:" | 10,14,264,8 | 0x50000000 SS_LEFT | 0x0 |

Parsed 324 of 348 bytes; the remaining 24 bytes are slack.

#### STANDARD_GET_TXT_DLG (VA 0x497d48, 228 bytes, DLGTEMPLATEEX) - referenced by code: yes

Style 0x900000c2 = WS_POPUP|WS_VISIBLE | DS_SYSMODAL|DS_SETFONT|DS_MODALFRAME; exStyle 0x0; rect (DLU) x=121 y=40 cx=186 cy=50; caption "Dialog"; font 8 pt "Helv"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 109 | EDIT | "" | 19,15,152,12 | 0x50810080 ES_AUTOHSCROLL+WS_BORDER+WS_TABSTOP | 0x20000 |
| 1 | BUTTON | "OK" | 19,33,64,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 2 | BUTTON | "Cancel" | 107,33,64,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 110 | STATIC | "Enter text:" | 17,4,143,8 | 0x50000000 SS_LEFT | 0x0 |

Parsed 226 of 228 bytes; the remaining 2 bytes are slack.

#### STANDARD_LIST_BOX_DLG (VA 0x497e2c, 316 bytes, DLGTEMPLATEEX) - referenced by code: yes

Style 0x900000c4 = WS_POPUP|WS_VISIBLE | DS_SETFONT|DS_MODALFRAME|DS_3DLOOK; exStyle 0x0; rect (DLU) x=20 y=40 cx=283 cy=123; caption "Dialog"; font 8 pt "Helv"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 111 | LISTBOX | "" | 7,23,271,70 | 0x50a10040 LBS_HASSTRINGS+WS_BORDER+WS_VSCROLL+WS_TABSTOP | 0x20000 |
| 1 | BUTTON | "OK" | 7,103,99,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 2 | BUTTON | "Cancel" | 179,103,99,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 112 | STATIC | "ListboxHeadline" | 8,2,227,8 | 0x50000000 SS_LEFT | 0x0 |
| 115 | STATIC | "In production" | 117,11,52,8 | 0x50000000 SS_LEFT | 0x0 |

Parsed 294 of 316 bytes; the remaining 22 bytes are slack.

#### WORK_ORDER_DLG (VA 0x497f68, 284 bytes, DLGTEMPLATEEX) - referenced by code: yes

Style 0x900000c4 = WS_POPUP|WS_VISIBLE | DS_SETFONT|DS_MODALFRAME|DS_3DLOOK; exStyle 0x200; rect (DLU) x=5 y=29 cx=320 cy=130; caption ""; font 9 pt "Fixedsys"; menu/class none.

| ID | Class | Text | x,y,cx,cy | Style | exStyle |
|---|---|---|---|---|---|
| 100 | STATIC | "Orders" | 129,4,62,8 | 0x50000000 SS_LEFT | 0x0 |
| 148 | BUTTON | "Accept" | 8,108,61,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 149 | BUTTON | "Remove" | 250,109,61,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 1 | BUTTON | "OK" | 108,109,104,14 | 0x50010f00 BS_PUSHBUTTON+BS_CENTER+BS_VCENTER+WS_TABSTOP | 0x0 |
| 111 | LISTBOX | "" | 8,17,303,78 | 0x50b000c0 LBS_HASSTRINGS+LBS_USETABSTOPS+WS_BORDER+WS_VSCROLL+WS_HSCROLL | 0x20000 |

Parsed 256 of 284 bytes; the remaining 28 bytes are slack.

### A.5 Dialog invocations (all `DialogBoxParamA` call sites, VERIFIED)

`DialogBoxParamA` IAT slot 0x48b360 has 17 relocated references: 16 `call cs:[0x48b360]` in game code and the unused thunk at 0x44ce3a. Several sites are tail-merged: a second path pushes its own arguments and jumps into the shared call. Every reachable path is listed. The "Owner" column is the real function start (`__CHK` prologue); the Ghidra function in brackets often spans several real functions.

| Call (path) | Owner [Ghidra fn] | Template (string VA) | DlgProc | hWndParent | hInstance / lParam |
|---|---|---|---|---|---|
| 0x402216 | 0x4020e0 [0x4020e0] INIT_CD_PATH | CD_NOT_FOUND_DLG (0x44d5d4) | 0x402288 | ESI = main hwnd argument | 0 / 0 |
| 0x40d516 (path via 0x40d1fa) | 0x40d123 [0x40cffe] | GAME_IO_DLG (0x44def2) | 0x40d611 (save) | [0x455614] | EDI (zeroed at fn entry) / EDI |
| 0x40d516 (path via 0x40d50f) | 0x40d123 [0x40cffe] | GAME_IO_DLG (0x44dee6) | 0x40d9e3 (load) | [0x455614] | EDI / EDI |
| 0x40d6d7 | 0x40d611 [0x40cffe] | STANDARD_GET_TXT_DLG (0x44defe) | 0x40deab | [0x455614] | 0 / 0 |
| 0x40e08e | 0x40dfd8 [0x40cffe] | STANDARD_LIST_BOX_DLG (0x44e00c) | 0x40e19b | [0x455614] | 0 / 0 |
| 0x40facb | 0x40f9f4 [0x40f769] | STANDARD_GET_TXT_DLG (0x44e0c4) | 0x40fb4d | [0x455614] | EBX / EBX |
| 0x40fee6 | 0x40feac [0x40fe68] main-menu callback | GAME_IO_DLG (0x44e10d) | 0x40d9e3 (load) | [0x455614] | 0 / 0 |
| 0x411442 | 0x4113b9 [0x4111e1] | STANDARD_LIST_BOX_DLG (0x44e1ca) | 0x4116a3 | [0x455614] | 0 / 0 |
| 0x4138ff (path via 0x4138f9) | 0x4137d4 [0x41370a] | EINZAHLEN_DLG (0x44e278) | 0x413944 | [0x455614] | ECX / ECX |
| 0x4138ff (path via 0x41391d) | 0x4137d4 [0x41370a] | AUSZAHLEN_DLG (0x44e286) | 0x413aec | [0x455614] | ECX / ECX |
| 0x41a46d | 0x41a1dd [0x41a0a8] | PERSONALKOSTEN_LIST_BOX_DLG (0x44e4a8) | 0x41d724 | [0x455614] | 0 / 0 |
| 0x41e4e8 | 0x41e45a [0x41e221] | STANDARD_GET_TXT_DLG (0x44e682) | 0x41e912 | [0x455614] | EBP / EBP |
| 0x41e57f (path via 0x41e579) | 0x41e45a [0x41e221] | STANDARD_LIST_BOX_DLG (0x44e697) | 0x41ecf6 | [0x455614] | EBP / EBP |
| 0x41e57f (path via 0x41e5a3) | 0x41e45a [0x41e221] | STANDARD_LIST_BOX_DLG (0x44e6ad) | 0x41eaa7 | [0x455614] | EBP / EBP |
| 0x41f0d9 | 0x41efd2 [0x41eefb] | STANDARD_LIST_BOX_DLG (0x44e6c5) | 0x41eaa7 | [0x455614] | 0 / EBX |
| 0x41fea9 | 0x41fde8 [0x41fa1a] Lula click handler | STANDARD_LIST_BOX_DLG (0x44e73a) | 0x42036d | [0x455614] | 0 / 0 |
| 0x421f81 (path via 0x421f7b) | 0x421caf [0x421606] | AUSZAHLEN_DLG (0x44e7ea) | 0x4220fb | [0x455614] | EBP / EBP |
| 0x421f81 (path via 0x421fb0) | 0x421caf [0x421606] | EINZAHLEN_DLG (0x44e7f8) | 0x4222a7 | [0x455614] | EBP / EBP |
| 0x423674 (paths via 0x42366d, 0x4236a0) | 0x423448 [0x423104] studio click handler | STANDARD_LIST_BOX_DLG (0x44e89b / 0x44e8b1) | 0x4248dc | [0x455614] | 0 / 0 |
| 0x429eb9 | 0x429d58 [0x429bed] warehouse click handler | WORK_ORDER_DLG (0x44eb32) | 0x42a720 | [0x455614] | 0 / 0 |

Register-pushed hInstance/lParam values come from registers that are zeroed with `xor reg,reg` at the start of their owner function (VERIFIED for EDI, EBX, ECX, EBP at those owners); that they are still 0 at the call is INFERRED. **The host must treat hInstance NULL as the EXE image** (Win32 semantics for a NULL module).

Return values are tested: 0x40d6de, 0x40e095, 0x411449, 0x41e4ef, 0x41e586, 0x41f0e0 compare with 1 (IDOK); 0x40feed compares with 2 (IDCANCEL). `[0x455614]` is the main window handle stored by WinMain (0x403ac9) from INIT_ALL.

**Unreferenced templates (VERIFIED: no ASCII or UTF-16 copy of the name in the image, and named resources can only be loaded by name):** DEBUG_EDIT_EQUIP_DLG, DEBUG_EDIT_PERSO_DLG, DEBUG_PERSO_DLG, DEBUG_PORTRAIT_DLG, MAKLER_IMMO_DLG. The host does not need to support classic DLGTEMPLATE, BS_AUTOCHECKBOX or ordinal control titles for the shipping game.

GDI switch around dialogs: `0x403f09` sets [0x4555e0]=1 and falls into 0x43daf1 (DirectDraw `FlipToGDISurface` through vtable+0x28 of the DirectDraw object, then a back-buffer lock/unlock); `0x435ee9` calls `ShowCursor(TRUE)` once and `0x435f26` calls `ShowCursor(FALSE)` once; `0x404049` clears [0x4555e0]. This sequence directly precedes DialogBoxParamA only at 0x402216 (0x4021f8, 0x402202) and 0x41a46d (0x41a43f, 0x41a453), and the error/info boxes 0x4040ba/0x40405e (VERIFIED). For the other 14 paths no switch occurs within 40 instructions; whether a caller switched earlier was not traced. The host must therefore be able to show a Win32 dialog while the DirectDraw flipping chain is active.

### A.6 Dialog procedures (VERIFIED from each procedure body)

All procedures are Win32 stdcall `BOOL CALLBACK (hwnd, msg, wParam, lParam)` (`ret 0x10`), compiled with the stack probe. Common behaviour: WM_INITDIALOG (0x110) fills controls; WM_COMMAND (0x111) handles IDOK/IDCANCEL and custom IDs; the WM_CTLCOLOR* range 0x132-0x138 (compare chains on 0x132, 0x134, 0x136, 0x137, 0x138, so EDIT 0x133 and BTN 0x135 fall into one of the ranges) returns a brush from `CreateSolidBrush(0x007ccaf8)` (light orange, BGR) after `SetTextColor(hdc, 0)` and `SetBkMode(hdc, TRANSPARENT=1)`; on close the brush is deleted with `DeleteObject`, then `EndDialog`. Some handle WM_PAINT with BeginPaint/EndPaint only.

| DlgProc | Template | Messages compared | Controls and calls |
|---|---|---|---|
| 0x402288 | CD_NOT_FOUND_DLG | 0x110, 0x111, 0x132, 0x134, 0x136-0x138 | Retry (1) re-opens `DATA\CURSOR\CURSOR.TAF` under the CD prefix (0x4022d7); Browse (150) calls 0x402510, which calls `GetOpenFileNameA` through thunk 0x44cf60 (call at 0x4025e3); End Program (3); `SendMessageA(hwnd, WM_SETREDRAW 0xb, ...)` at 0x402485; `EndDialog(.,1)` at 0x402329/0x40235c |
| 0x40d611 | GAME_IO_DLG (save) | 0x110, 0x111, 0x132, 0x136-0x138 | Listbox 145 (0x91): LB_GETCURSEL 0x188, LB_GETTEXTLEN 0x18a, LB_GETTEXT 0x189 into 0x455628, LB_ADDSTRING 0x180, LB_SETCURSEL 0x186; nested STANDARD_GET_TXT_DLG for the save name; writes the save (section B.9) |
| 0x40d9e3 | GAME_IO_DLG (load) | 0x110, 0x111, 0x132, 0x136-0x138 | `SetDlgItemTextA(1, "Load Game")` (0x44dfd2) and `SetDlgItemTextA(147, "Load Game")` (0x44dfdc) relabel the Save button and the static; listbox 145 as above; reads the save, deletes `FILMB.TMP` if needed, calls room-leave hook [0x4550d0] at 0x40dc54 and `ChangeRoom` (0x404378) at 0x40dc5c |
| 0x40deab, 0x40fb4d, 0x41e912 | STANDARD_GET_TXT_DLG | 0x110, 0x111, 0x132, 0x134, 0x136-0x138 | Edit 109 (0x6d): `GetDlgItemTextA` (cch 0x50 into 0x455628 or 0x45e02c; cch 0x63 at 0x41e976), `SetDlgItemTextA`; static 110 (0x6e) gets the prompt (e.g. "Savename:" 0x44e002, string 647 "New movie title: ", 3221 "Title of session:") |
| 0x40e19b, 0x4116a3, 0x41ecf6, 0x41eaa7, 0x42036d, 0x4248dc | STANDARD_LIST_BOX_DLG | 0x110, 0x111, 0x132, (0x134), 0x136-0x138 | Listbox 111 (0x6f): LB_RESETCONTENT 0x184, LB_ADDSTRING 0x180, LB_SETCURSEL 0x186, LB_GETCURSEL 0x188; static 112 (0x70) headline and 115 (0x73) column header from 0x4550d8 |
| 0x413944, 0x4222a7 | EINZAHLEN_DLG (deposit) | 0x110, 0x111, 0x132, 0x134, 0x136-0x138 | `GetDlgItemInt(113)`; `SetDlgItemTextA(107)` account, `SetDlgItemTextA(113)` |
| 0x413aec, 0x4220fb | AUSZAHLEN_DLG (withdraw) | 0x110, 0x111, 0x132, 0x134, 0x136-0x138 | `GetDlgItemInt(114)`; `SetDlgItemTextA(106)`, `SetDlgItemTextA(114)` |
| 0x41d724 | PERSONALKOSTEN_LIST_BOX_DLG | 0x110, 0x111, 0x132, 0x136-0x138 | Listbox 111: LB_RESETCONTENT, LB_ADDSTRING x3 sites, LB_SETCURSEL; `EndDialog(.,0)` |
| 0x42a720 | WORK_ORDER_DLG | 0x110, 0x111, 0x132, 0x136-0x138 | Listbox 111: LB_GETCURSEL, LB_DELETESTRING 0x182 (Accept 148 / Remove 149), LB_SETCURSEL, LB_ADDSTRING |

Listbox styles in use: LBS_HASSTRINGS only (STANDARD_LIST_BOX_DLG, PERSONALKOSTEN), LBS_HASSTRINGS|LBS_USETABSTOPS with WS_HSCROLL (WORK_ORDER), LBS_HASSTRINGS with WS_VSCROLL (GAME_IO). No LBS_NOTIFY in referenced templates, so no LBN_* notifications are expected. Buttons are BS_PUSHBUTTON|BS_CENTER|BS_VCENTER (0x0f00), one BS_DEFPUSHBUTTON (EINZAHLEN OK, 0x0f01) and BS_GROUPBOX (0x07). Edits use ES_AUTOHSCROLL, some with ES_NUMBER (0x2000) and WS_EX_CLIENTEDGE (0x200).

### A.7 String tables

971 strings with IDs between 101 and 8042 in 102 blocks (VERIFIED). Every block parses exactly (16 length-prefixed UTF-16LE entries, no trailing bytes). Maximum length 171 characters. Non-ASCII characters (VERIFIED): U+00B4 in IDs 753, 1805, 5401, 5402 and 5405, and U+0081 in ID 975 ("B\x81ck", a mangled CP437 u-umlaut). Every character is at most U+00FF, so Latin-1 truncation gives the same bytes as Windows' CP1252 conversion, including 0x81. Two strings (319, 320) contain the spreadsheet artefact `#NAME?`.

The full table is in Appendix A. Present ID runs (VERIFIED; gaps of up to 3 merged): 101-114, 201-221, 250-273, 301-322, 350-365, 401-413, 501-528, 600-657, 700-792, 800-815, 850-862, 901-997, 1001-1006, 1101-1103, 1150-1152, 1201-1219, 1301, 1401-1414, 1501-1507, 1601-1616, 1650-1653, 1701-1704, 1801-1809, 1850-1855, 1901-1908, 2000-2079, 2100-2153, 2200-2211, 3101-3120, 3201-3230, 3401-3403, 4301-4308, 4401-4417, 4501-4512, 4601-4606, 4701-4704, 4801-4809, 4901-4905, 5001-5041, 5101-5109, 5201-5213, 5300-5386, 5400-5414, 8000-8042. Content (INFERRED): 101-114 stage-2 town hotspots; 201-273 office and parties; 301-365 property, rooms and staff; 401-413 bank; 501-528 realtor; 600-657 movie planning and plot titles; 700-815 staff attributes and Lula; 850-862 stores; 901-997 global messages, jobs, equipment and moods; 1001-2211 one block per department (1001 cutting, 1101 copying, 1150 map, 1201 warehouse, 1301 recreation, 1401 props, 1501 dubbing, 1601/1650 advertising, 1701 doorman, 1801/1850 sabotage, 1901 beauty clinic, 2000 studio, 2100 sex store, 2200 distributor); 3101-3403 stage-1 town and Lula sessions; 4301-5213 per-location texts; 5300-5375 item names ("Lamp", ...); 5376-5386 US cities; 5400-5414 "infamous sayings" shown in the exit credits; 8000-8042 video file names `*.cut` (used only by the video code, out of scope).

### A.8 Code that loads resources (VERIFIED)

| API (IAT slot) | Call sites in game code | Arguments | Notes |
|---|---|---|---|
| `LoadStringA` (0x48b3a8) | **1**: 0x4047fd in wrapper 0x4047e8 | `LoadStringA([0x455618], id=EAX, buf=EDX, cch=EBX)`; returns length in EAX | [0x455618] is WinMain's hInstance, stored at 0x403af7 |
| `LoadIconA` (0x48b3a0) | 1: 0x4338a4 in 0x4337dc (graphics object constructor) | `(NULL, IDI_APPLICATION 0x7f00)` -> WNDCLASS.hIcon [0x48605c] | No custom icon is loaded |
| `LoadCursorA` (0x48b39c) | 2: 0x4338b6 `(NULL, IDC_ARROW 0x7f00)` -> WNDCLASS.hCursor [0x486060]; 0x403b04 in WinMain `(NULL, IDC_WAIT 0x7f02)` | | WinMain sets the wait cursor with `SetCursor` at 0x403bcf and restores the previous one at 0x403c36 around INIT_GAME |
| `LoadMenuA` (0x48b3a4) | 2: 0x433ca4 in 0x433bfc and 0x433ee6 in 0x433e0a | `(hInstance, menu name from a stack argument)`, result to [0x488078], then `SetMenu` (0x433cbc, 0x433efd) | **Dead code**: 0x433bfc and 0x433e0a (alternative window creators, not in Ghidra's list) have no direct call, no jump and no relocated pointer. The live creator 0x433f2e never calls LoadMenuA. |
| `DialogBoxParamA` (0x48b360) | 16 (section A.5) | | |
| Thunk table 0x44cc07-0x44cf65 | `jmp ds:[IAT]`, one per import (143) | | Game code calls imports directly (`call cs:[IAT]`) except `GetOpenFileNameA` (thunk 0x44cf60), `DirectDrawCreate` (0x44cf5a), `DirectSoundCreate` (0x44cf54) and `LoadLibraryA`/`GetProcAddress` (0x44cee2/0x44cedc, from 0x4340b3). The Watcom runtime uses its KERNEL32.DLL/USER32.DLL thunks 0x44ce4c-0x44cf4e. The remaining game-block thunks are unused. |

WNDCLASS at 0x486048 (filled by 0x4337dc and 0x433f2e): style 0xb (CS_VREDRAW|CS_HREDRAW|CS_DBLCLKS), lpfnWndProc 0x4343f1, hInstance [0x486058], hbrBackground 0, **lpszMenuName = lpszClassName = 0x485ea0**. INIT_ALL passes class name "WET THE SEXY EMPIRE " (with a trailing space, copied from 0x450c78) and title "WET THE SEXY EMPIRE (C) NEW GENERATION 1996" (0x450c90). Because no menu named "WET THE SEXY EMPIRE " exists, Windows creates the window without a menu; the host must not fail RegisterClassA/CreateWindowExA on a missing class menu.

**String-ID wrappers (VERIFIED):**

| Wrapper | Signature | Behaviour |
|---|---|---|
| 0x4047e8 | EAX=id, EDX=buffer, EBX=cch | Thin LoadStringA wrapper (186 direct call sites) |
| 0x4058c8 | EAX=id, EDX=mode | Loads into 0x4550d8 (cch 0x100), then mode 1 -> 0x404f66 (in-game yes/no box, returns choice) else 0x404ecf (in-game info box). 138 call/jump sites. |
| 0x40899b | AL=time slot, EDX=id | Opening-hours check: if closed and id != -1, formats string 979 "Unfortunately %s is being closed" with the place name id and shows 0x404ecf. 13 call sites. |
| 0x404806 | (none) | Tooltip of the hovered hotspot: `id = [0x45d4dc + 4*[0x45d4d4]]`, loads into 0x45d5a8 (cch 0x50), caches the index in [0x45d5a4]; on failure calls 0x4040ba with "STRING NOT FOUND! (RECT NAMES)" (0x44db5a) |

Resolved IDs: 196 distinct defined IDs are loaded with an immediate, 120 come from hotspot registrations (section B.5), 45 call sites use `base + index` (44 direct, 1 through 0x4058c8) and 22 are dynamic (21 direct, 1 through 0x4058c8). Bases in use: 256, 262, 360, 505, 517, 521, 604, 706, 716, 726, 736, 746, 757, 777, 850, 912, 939, 962, 1204, 1210, 1607, 1805, 1903, 2015, 2038, 2103, 2204, 5376, 5400. A heuristic run (immediate IDs, hotspot IDs and up to 64 consecutive IDs after each base) leaves 261 IDs apparently unreferenced, mostly 301-317, 952-961, 5300-5375 and the video names 8000-8042; treat that as INFERRED, because table-driven IDs (for example 0x45d4dc, [ebp+0x72], [eax+0x451084], [0x4847f0]) are not resolved statically. The per-function list is Appendix B.

**IDs that are requested but have zero-length entries (VERIFIED):** 3211, 3212, 3213 (0x40f489) and 3223, 3224, 3225 (0x40f30b). Their blocks exist, the entries are empty. The destination buffers are uninitialised stack memory, so `LoadStringA` must write an empty string and return 0 (Windows behaviour for an empty entry).

### A.9 Host requirements for resources

1. Map `.rsrc` at 0x494000 from the image and serve all resources from guest memory; no external resource files.
2. Resolve `hInstance` NULL, the value returned by `GetModuleHandleA(NULL)`, and [0x455618] to the EXE image.
3. `LoadStringA`: block `(id >> 4) + 1`, first (only) language 0x407, entry `id & 15`; copy at most `cch-1` characters with Latin-1 truncation, NUL-terminate, return the copied length; for an empty or missing entry write `""` (when `cch > 0`) and return 0. The current `src/runtime/win32/res.c` + `user32.c` implementation already behaves like this.
4. `DialogBoxParamA` must be modal and synchronous, parse DLGTEMPLATEEX by item count, create STATIC (SS_LEFT/SS_RIGHT), EDIT (ES_AUTOHSCROLL, ES_NUMBER), BUTTON (push, default push, group box) and LISTBOX (LBS_HASSTRINGS, tab stops, scroll bars), and call the guest dialog procedure for WM_INITDIALOG (lParam 0), WM_COMMAND (`MAKEWPARAM(id, BN_CLICKED)` for buttons; IDOK=1 for Enter, IDCANCEL=2 for Esc), WM_CTLCOLOR* (use the returned HBRUSH, text colour and background mode set by the procedure) and WM_PAINT. It must support `GetDlgItem`, `SetDlgItemTextA`, `GetDlgItemTextA`, `GetDlgItemInt` (lpTranslated from EAX, bSigned from ESI), `SendMessageA` with LB_ADDSTRING, LB_DELETESTRING, LB_RESETCONTENT, LB_SETCURSEL, LB_GETCURSEL, LB_GETTEXT, LB_GETTEXTLEN and WM_SETREDRAW, and `EndDialog` (the value is returned by DialogBoxParamA). Dialog units: "Helv" 8 pt maps to MS Sans Serif 8 (INFERRED base units about 6x13 px, so 1 DLU = 1.5 px horizontally and 1.625 px vertically); "Fixedsys" 9 pt is about 8x15 px. DS_CENTER centres on the 640x480 screen.
5. Dialogs appear over a fullscreen DirectDraw 640x480 game. The host should draw them over the last presented frame and keep mouse coordinates in game space.
6. `LoadIconA`/`LoadCursorA` only need system IDs 0x7f00 (icon), 0x7f00 and 0x7f02 (cursors). Return distinct non-zero handles; `SetCursor` with the wait cursor may be ignored while the game draws its own cursor (`DATA\CURSOR\CURSOR.TAF`).
7. `LoadMenuA`/`SetMenu` are unreachable. Popup menus (`CreatePopupMenu`, `AppendMenuA` x7, `TrackPopupMenu`, `DestroyMenu`) are only used by 0x438d56 (section B.7). `GetOpenFileNameA` is only used by the CD-path Browse button.
8. The manifest, the icon and DLGINCLUDE need no runtime support.

---

## Part B: game-code map

### B.1 Function boundaries (VERIFIED)

| Source | Count |
|---|---|
| Ghidra `functions.tsv` | 945 (42 thunks) |
| `push imm32; call 0x43371d` prologues (every compiled game function) | 1103, from 0x401010 to 0x443f8f |
| ...of which already Ghidra entries | 661 |
| ...missing from Ghidra | 442: 274 referenced by a relocated pointer (callbacks, hook values, jump-table-free function pointers), 45 only by direct `call`, 1 only by `jmp`, 122 with no static reference (dead code or reached through computed addresses) |
| Ghidra entries without the prologue | 284 (Watcom runtime, assembler blitters, thunks, tiny leaf functions such as 0x40408f, 0x407167, 0x4154cc) |

Ghidra merges several real functions into one: 0x4037e4 also contains WinMain 0x40399d; 0x43393d spans 0x433bfc, 0x433d33-region code and 0x433e0a; 0x43ebd3 contains the TAF loader 0x43eca6; most room setup functions also contain their click, paint and tick hooks. The recompiler's own discovery (1415 functions in `docs/recompilation.md`) is consistent with this. Appendix C lists the 442 missing entries.

Code embedded jump tables seen during this work (VERIFIED): 0x4042f0 (34 entries, ChangeRoom), 0x40a500 (room 1 stage 2 click handler), 0x40fe94 (main-menu callback). These lie inside BEGTEXT between functions.

`__CHK` 0x43371d: `xchg [esp+4],eax; call 0x43372d; mov eax,[esp+4]; ret 4`. It preserves EAX and pops the frame size; 0x43372d reports "Stack Overflow!".

### B.2 Module map by address range

| Range | Module | Evidence | Key entries |
|---|---|---|---|
| 0x401010-0x4037e3 | Startup, global init, shutdown, CD check | STARTUP_DEBUG checkpoint strings "INIT_ALLCheck*", pool names | 0x401010 INIT_ALL, 0x401535 INIT_VARS, 0x4015b9 INIT_GAME, 0x40171a WndProc hook registration, 0x401757 shutdown, 0x401ab6 LOAD_ALL, 0x4020e0 INIT_CD_PATH, 0x40261b INTRO, 0x40271e INIT_ONCE_VARS, 0x4028d4 INIT_GAME_VARS |
| 0x4037e4-0x403d09 | C++ static constructors + WinMain | Watcom XI table 0x453748; entry writes 0x40399d to [0x48a6f0] | 0x4037e4, 0x40399d |
| 0x403d0a-0x4058c7 | Frame loop, screen refresh, hotspots, scene switching, message boxes | PeekMessage loops, room jump table | 0x40413f, 0x404378, 0x4040ea, 0x40527f, 0x4050c1, 0x405862, 0x404ecf, 0x404f66 |
| 0x4058c8-0x40a01e | Shared game services | Strings "Perskart.TAF", "WET.INI", "%4d .PCX", "DATA\PERSO\HLP.DAT" | 0x4058c8 message by ID, 0x40590d person card, 0x407926 list container ctor ([0x45d940]), 0x4084d9/0x40855e sound-effect manager ([0x45d944]), 0x408a87 hourly hook ([0x4550cc]), 0x40983f path builder, 0x40989c INIT_STUFE, 0x409a32 exit credits, 0x409c83 help text (HLP.DAT), 0x409e26/0x409e3f/0x409e6a/0x409eb7 INI helpers, 0x409efa argv, 0x409f76 screenshot (F8), 0x4092b1 video (out of scope) |
| 0x40a01f-0x43371c | Room/scene classes (one C++ object per room) and their dialogs and simulation | Section B.5 | 21 room objects 0x45dbc0-0x45dbd4 |
| 0x43371d-0x4337db | Compiler support | | 0x43371d `__CHK`, 0x43377c `operator new` (size in EAX) |
| 0x4337dc-0x434c53 | Window, WndProc, DirectDraw | RegisterClassA, CreateWindowExA, SetTimer, DirectDrawCreate, "HI-COLOUR MODE" | 0x4337dc graphics-object ctor, 0x433f2e create window, 0x4340b3 DirectDraw init, 0x434247 set display mode, 0x4342ef destroy, 0x4343a2 handler registry, 0x4343f1 WndProc, 0x434a1c pixel-format check |
| 0x434c54-0x435400 | File/memory manager object ([0x455014]) | GlobalMemoryStatus, "rb" | 0x434c54 ctor (size 0x102d5), 0x434d10 alloc, 0x434d6b free, 0x434ec2 load TAF file |
| 0x435401-0x435c83 | TFF loader/debug dump | "Breite =%d", "Hoehe =%d", "Anzahl = %d", "TFF" | 0x435401 (INFERRED: TAF/TFF frame-set loader with debug output) |
| 0x435c84-0x4363ff | Mouse/keyboard object ([0x45501c]) | ShowCursor, SetCursorPos, GetAsyncKeyState, ScreenToClient | ctor 0x435c84 registers WM_MOUSEMOVE 0x4361c7, WM_LBUTTONDOWN 0x436304, WM_LBUTTONDBLCLK 0x4362ed, WM_RBUTTONDOWN 0x436336, WM_KEYDOWN 0x436361 |
| 0x436400-0x4365d7 | Error object ([0x455018]) | MessageBoxA | 0x4365af set message text, 0x4364b5 show error box |
| 0x4365d8-0x439032 | DirectSound sound system ([0x455024]) | waveOut*, timeSetEvent, DSERR strings, "WAVE" | 0x4365d8 ctor, 0x4366ae init, 0x436807 MM timer, 0x438003 timer callback, 0x43700a WAVE parse, 0x438712 play pool chunk, 0x437f28/0x437f91 volume, 0x438d56 output-device popup menu |
| 0x4391b8-0x439e2b | POOL_CLASS: NGS pool writer ([0x45dbdc]) | "POOL_CLASS->" strings, MoveFileA, DeleteFileA | 0x4391ce create, 0x4393b0/0x43940e header and index, 0x439590 append chunk |
| 0x439e2c-0x43a98b | Video (ActiveMovie/COM) | out of scope | listed in B.12 |
| 0x43a9c8-0x43e527 | 2D graphics engine ([0x455010], DD context [0x48a644]) | CreateFontIndirectA "System Small", TextOutA, DrawTextA, surface lock errors, DDERR strings | 0x43a9c8 font/target init, 0x43b3cf clear, 0x43b578 full-screen image blit, 0x43c3b4 sprite/animation draw, 0x43cf4f text out, 0x43d079 DrawText, 0x43d646 flip + relock, 0x43daf1 switch to GDI, 0x43de84 DDERR text, 0x43e419 draw cursor/background |
| 0x43e528-0x442430 | Image/sound file library | ".PCX", ".TBF", ".TAF", ".TFF", ".VOC", ".MOD", ".TMF", "NGS", "Dateiname:" | B.6 |
| 0x442431-0x4428df | File I/O wrappers on the game KERNEL32 block | CreateFileA, ReadFile, WriteFile, SetFilePointer, CloseHandle, DeleteFileA | 0x442431 signature type, 0x4424b3 copy file, 0x442539 delete, 0x4425ac open, 0x442654 close, 0x442675 tell/size, 0x442690 seek, 0x4426d1 read, 0x44272c write, 0x44277d getc, 0x4427c5 putc, 0x4428c8 set RGB-mode flag |
| 0x4428e0-0x443417 | Watcom C runtime | no `__CHK` | 0x4428e0 memset, 0x4428f8 delete, 0x44296a strcpy, 0x442989 strcat, 0x4429bc strlen, 0x4429d6 exit, 0x442bb4 sprintf, 0x442cfc **entry** |
| 0x443418-0x443fe7 | MIDI and CD-audio module (game code) | midiOut*, aux*, mciSendCommandA (out of scope), MM_MCINOTIFY handlers | 0x443418, 0x4434c1 MIDI, 0x443afb CD audio |
| 0x443fe8-0x445d97 | Hand-written blitters/RLE (no `__CHK`) | 640/0x500 stride special case | 0x444816 sprite blit, 0x444b00, 0x444d1d, 0x444e52, 0x4458cc |
| 0x445d98-0x44cc06 | Watcom C runtime (printf, file streams, threads, exceptions, startup) | | 0x4477c4 cstart, 0x44868a XI initializer walker |
| 0x44cc07-0x44cfa0 | Import thunks | `jmp ds:[IAT]` | |

### B.3 Startup and shutdown (VERIFIED)

1. `0x442cfc` entry: `mov [0x48a6f0], 0x40399d` (WinMain pointer), `jmp 0x4477c4` (Watcom cstart).
2. cstart runs the XI initializer table 0x453748..0x453790 (12 six-byte entries `{u8 done, u8 priority, u32 fn}` walked by 0x44868a, lowest priority first). The only game entry is 0x4037e4 (priority 0x40), which constructs the room objects 0x45dbc0..0x45dbd4 and counts them in [0x450cec] (0x15 = 21 when done). It then calls `GetModuleHandleA` (thunk 0x44cf2a at 0x44789d), `call [0x48a6f0]` at 0x4478a3, and `exit(eax)` (0x4429d6).
3. `0x40399d WinMain` (stdcall, `ret 0x10`): deletes `<cwd>\W_DEBUG.DAT`, writes checkpoint entries through 0x409eb7 (`WritePrivateProfileStringA("STARTUP_DEBUG", "WIN_MAINCheckN", "OK", W_DEBUG.DAT)`), reads `WET.INI [VIDEO] VideoPlay` (GetPrivateProfileIntA, default -1; writes 1 if missing), scans argv for `-novideo`, calls INIT_ALL, stores hwnd [0x455614] and hInstance [0x455618], `LoadCursorA(IDC_WAIT)`, INIT_STUFE (0x40989c), clears/flips twice (0x43b3cf + 0x43d646), runs the main menu 0x40fe68, then loops `while ([0x4555e4] == 0) 0x40413f();`. When the session flag [0x4555e4] is set, it stops sound, restores the cursor, re-runs INIT_GAME (0x4015b9) and INIT_STUFE, shows the main menu again and re-checks the flag (a quit from the menu leaves it set). Exit path (0x403ca0): writes VideoPlay, stops sound (0x437c60), exit credits 0x409a32, shutdown 0x401757, returns EBP (0 if INIT_ALL failed).
4. `0x401010 INIT_ALL(EAX=hInstance, EDX=nCmdShow)` creates singletons with `operator new` + ctor (sizes and ctors VERIFIED, roles INFERRED from usage):

| Global | Size | Ctor | Role |
|---|---|---|---|
| [0x455010] | 0xb43 | 0x4337dc | Graphics/window object (WNDCLASS 0x486048) |
| [0x455014] | 0x102d5 | 0x434c54 | File and memory manager (I/O buffer at +0x105) |
| [0x45501c] | 0x10 | 0x435c84 | Mouse and keyboard |
| [0x455018] | 0x206 | 0x436400 | Error reporter |
| [0x45dbd8] | 1 | 0x414c3c | DDF dialog engine ("DIA") |
| [0x455024] | 0x76 | 0x4365d8 | DirectSound sound system |
| [0x45dbdc] | 1 | 0x4391b8 | POOL_CLASS |
| [0x455028] | 1 | 0x439e2c | Video (out of scope) |
| [0x45d940] | 0x10 | 0x407926 | Equipment list (0x68-byte records, saved in STF1DAT) |
| [0x45d944] | 0x116 | 0x4084d9 | Sound-effect manager (SOUND.TAP) |

   Then `0x433f2e(hInst, nCmdShow, class, title; push 640, 480, 16)` (`ret 0xc`) creates the window, a 60 ms `SetTimer(hwnd, 1, 0x3c, NULL)` and DirectDraw; then 0x437f91 (wave volume -> [0x45d95c]), 0x40171a (WndProc handlers), INIT_VARS, 0x43dc33 (copies the configured mode width/height/depth from 0x45236c..0x452370 to [0x45561c], [0x45561e] and a local), 0x4019b7, INIT_CD_PATH, pixel-format probe 0x43e528 (if it returns 0xf, `0x4428c8(fm, 1)` sets [0x48a68c]; INFERRED: 15-bit RGB555 mode), LOAD_ALL, sound init 0x4366ae, reads `DATA\SAVE\WET.1ST` (or zeroes it, sets display mode 640x480x16 via 0x434247 and plays INTRO 0x40261b on the first run), loads `CURSOR.TAF` into [0x455364] and [0x455610] = 0x41648d([0x45dbd8]).
5. Global error code **WORD [0x455030]** (689 references) gates almost every step. Codes written (VERIFIED): 5 allocation failed (0x434faa), 8 open/copy failed, 9 read failed, 0xa write failed, 0xf wrong file type (0x43ef03), 0x14 window or timer init failed. 0x4365af sets an error text on [0x455018] and 0x4364b5 shows it.
6. `0x401757` shutdown: calls the room-leave hook, writes `WET.1ST`, frees the sound-effect manager, stops sound, closes the five pool handles (0x455340/44/4c/58/54), switches to GDI, deletes all singletons in reverse order (destructors 0x407967, 0x439ecd, 0x4391c3, 0x436636, 0x414cc7, 0x4364aa, 0x435db5, 0x434cfc, 0x4338d3, each followed by `operator delete` 0x4428f8). The 19-byte NOP run at 0x401853-0x401865 (and 3 NOPs at 0x40143c, 2 at 0x40ff52) look like patched-out calls (INFERRED).

### B.4 Main loop, timers and event hooks (VERIFIED)

`0x40413f` (one frame, called in a busy loop without Sleep):

1. `PeekMessageA(&msg, hwnd, WM_TIMER, WM_TIMER, PM_REMOVE)`; if a timer message was taken: Translate/Dispatch, then `0x4050c1` (game tick) when in game mode ([0x45dbe0] == 1).
2. `0x405862`: drains all remaining messages with `PeekMessageA(&msg, hwnd, 0, 0, PM_REMOVE)`, then polls the mouse (0x43619e into 0x455384, 0x436156 into 0x455394).
3. In game mode: hot keys 0x4094d4 (last key [0x455384]: VK_F1 0x70 help 0x4095e0, VK_F2 0x71 options 0x410bc9, VK_F7 0x76 and 'V' 0x56 toggle [0x45dc04] with MessageBeep, VK_F8 0x77 screenshot 0x409f76); hotspot hit test `0x4040ea(table 0x45d1b0, start 0, count [0x45d4d0], point 0x455394)` into [0x45d4d4], tooltip 0x40466a; if a click is pending ([0x4553a8] != 0) call **[0x4550b8](EAX=hwnd, EDX=0, EBX=0)**; if [0x4553b0] != 0 call [0x4550bc]; then 0x43e419 (cursor), 0x403ed5, status bar 0x403d0a, end-of-day processing 0x404a33 when [0x4555e8] is set (strings 902, 907), and error display.

Timers and callbacks the host must drive:

| Source | Period / trigger | Guest code | Notes |
|---|---|---|---|
| `SetTimer(hwnd, 1, 60 ms)` at 0x433fca | WM_TIMER | WndProc 0x4343f1, then 0x4050c1 from the main loop | 0x4050c1 runs the scene tick hook [0x4550c8], the clock 0x40a1f0 (minute 0-59, hour up to closing hour [0x4555d4], then day+1 with 30-day months and 12 months, year+1, weekday 0-6), and once per game hour [0x4550cc] (0x408a87) plus stage-2 hourly room events |
| `timeSetEvent(delay=[0x48a518], resolution=10, 0x438003, dwUser=2, TIME_PERIODIC)` at 0x436899 | multimedia timer, separate thread on Windows | 0x438003 (sound streaming/mixing; reentrancy flag [0x48a51c]; returns early unless dwUser == 2) | Delay logic at 0x43683c: with `timeGetDevCaps` wPeriodMin <= 2 and wPeriodMax > 50 the delay is **2 ms** |
| WndProc 0x4343f1 | every message | handler table 0x486070[0x800] (+[0x488070] for 0x8000) | Handlers are called with EAX=hwnd, EDX=wParam, EBX=lParam, ECX=msg*4; non-zero EAX means handled, otherwise DefWindowProcA |

Registered WndProc handlers (`0x4343a2({msg, fn})`): WM_DESTROY 0x2 -> 0x434562 (0x43409b), then replaced by 0x40a15c (0x40174e); WM_ACTIVATEAPP 0x1c -> 0x40a174 (0x401738); mouse/keyboard as in B.2; WM_COMMAND 0x111 -> 0x43918f temporarily inside 0x438d56; MM_MCINOTIFY 0x3b9 -> 0x4439b3 and 0x443f8f (MIDI/CD). The windowed-mode path at 0x433d75-0x433dde registers WM_PAINT 0xf, WM_SIZE 0x5, WM_HSCROLL 0x114, WM_VSCROLL 0x115 and is only reached through the 0x43393d path (INFERRED unused in fullscreen).

Scene hook globals (VERIFIED by all writers and readers):

| Global | Meaning | Called from |
|---|---|---|
| [0x4550b8] | Scene click handler | 0x404252 when [0x4553a8] != 0 |
| [0x4550bc] | Right-click handler; its only writer stores 0 (0x4027a6 after `xor ebx,ebx` at 0x402778), so it is never called | 0x404273 |
| [0x4550c0] | Unused hook (cleared only) | - |
| [0x4550c4] | Scene paint hook | 0x403d33, 0x403f54, 0x403fd4, 0x404e01 |
| [0x4550c8] | Scene tick hook | 0x4050d7 |
| [0x4550cc] | Global hourly hook, set to 0x408a87 at 0x4032fb (INIT_GAME_VARS); 0x408a87 is also called directly at 0x409117 | 0x40510f |
| [0x4550d0] | Current room's leave function | 0x40177a, 0x408e2c, 0x408f57, 0x4098c2, 0x40dc54, 0x42471c, 0x424855, 0x4248b9 |

`0x40527f` resets the hotspot table (0x320 bytes at 0x45d1b0), the string-ID table (0xc8 bytes at 0x45d4dc), the tooltip buffer and the hooks b8/c0/c4/c8. All hook values are code addresses that Ghidra does not list as functions (Appendix C marks them with `*`).

### B.5 Rooms, stages and hotspots

`0x404378 ChangeRoom(EAX=room)`: clears the tooltip, stops sound effects (0x4088f5 on [0x45d944]), waits while a click is pending, and if `room != [0x455690]` dispatches through the jump table at 0x4042f0 (`jmp cs:[eax*4+0x4042f0]` at 0x404662, index room-1). Each case calls the room object's enter method with `this` in EAX, stores the room's leave function in [0x4550d0], sets [0x455690] = room and redraws via 0x403f22. EAX returns 1 on success.

Each enter method switches on the **stage [0x455694]** (1, 2 or 3; `INIT_STUFE` = "init stage" 0x40989c) and calls one setup function, which registers hotspots, loads TAF/TBF files and sets the click, paint and tick hooks. Stage mapping obtained by emulating each enter method with [0x455694] = 1, 2, 3 (VERIFIED):

| Room ID | Case VA | Object (`this`) | Pre-call | Enter fn | Leave fn -> [0x4550d0] | Stage 1 setup | Stage 2 setup | Stage 3 setup |
|---|---|---|---|---|---|---|---|---|
| 1 | 0x4043f0 | 0x45dbc0 | - | 0x40a3c7 | 0x40a410 | 0x40a748 | 0x40a451 | 0x40b25c |
| 2 | 0x40440b | 0x45dbc4 | - | 0x41319b | 0x4131dd | 0x413ef9 | 0x413217 | 0x413217 |
| 3 | 0x404421 | 0x45dbc3 | - | 0x41116f | 0x4111ac | 0x41233f | 0x4111e1 | - |
| 4 | 0x404437 | 0x45dbc1 | - | 0x40b992 | 0x40b9cf | 0x40c3f9 | 0x40ba04 | - |
| 5 | 0x40444d | 0x45dbc2 | - | 0x40cd24 | 0x40cd6d | 0x40e5a5 | 0x40cdae | 0x4101db |
| 6 | 0x404463 | 0x45dbc5 | - | 0x416746 | 0x41677e | - | 0x4167b1 | 0x4168e0 |
| 7 | 0x404479 | 0x45dbd3 | - | 0x431271 | 0x4312ba | 0x4317c3 | 0x4312fb | 0x431c1b |
| 8 | 0x404492 | 0x45dbd4 | - | 0x4326cf | 0x43270a | 0x43323a | 0x43273f | - |
| 20 | 0x4044ab | 0x45dbc6 | 0x418457 | 0x417d01 | 0x417d39 | - | 0x417d6c | 0x419115 |
| 21 | 0x4044e5 | 0x45dbc7 | - | 0x41a057 | 0x41a081 | - | 0x41a0a8 | - |
| 22 | 0x4044fe | 0x45dbc8 | - | 0x41de36 | 0x41de60 | - | 0x41de87 | - |
| 23 | 0x404517 | 0x45dbc9 | - | 0x41f9af | 0x41f9e7 | - | 0x41fa1a | 0x4208f2 |
| 24 | 0x404530 | 0x45dbca | 0x423071 | 0x423092 | 0x4230cf | 0x425c85 | 0x423104 | - |
| 25 | 0x40456d | 0x45dbcd | 0x42b518 | 0x429b82 | 0x429bba | - | 0x429bed | 0x42ab96 |
| 26 | 0x4045a7 | 0x45dbd2 | - | 0x4302eb | 0x430328 | 0x430833 | 0x43035d | - |
| 27 | 0x4045c0 | 0x45dbd0 | - | 0x42d69f | 0x42d6dc | 0x42e20c | 0x42d711 | - |
| 28 | 0x4045d9 | 0x45dbce | - | 0x42b65c | 0x42b699 | 0x42b98e | 0x42b6ce | - |
| 29 | 0x4045f2 | 0x45dbcb | - | 0x428384 | 0x4283ae | - | 0x4283d5 | - |
| 30 | 0x40460b | 0x45dbcc | - | 0x428eb4 | 0x428eec | - | 0x428f1f | 0x429445 |
| 31 | 0x404624 | 0x45dbd1 | - | 0x42eaa9 | 0x42eaf2 | 0x42f3a9 | 0x42eb33 | 0x42fa1c |
| 32 | 0x40463d | 0x45dbcf | - | 0x42c21d | 0x42c25a | 0x42c7de | 0x42c28f | - |
| 33 | 0x404530 | 0x45dbca | 0x423071 | 0x423092 | 0x4230cf | 0x425c85 | 0x423104 | - |
| 34 | 0x404530 | 0x45dbca | 0x423071 | 0x423092 | 0x4230cf | 0x425c85 | 0x423104 | - |

Room IDs 9-19 (case 0x4043d9) are rejected: 0x404378 returns 0 without changing [0x455690]. Valid range checked by `lea eax,[esi-1]; cmp eax,0x21; ja` at 0x404656 (IDs 1..34).

| Setup fn | click hook [0x4550b8] | paint hook [0x4550c4] | tick hook [0x4550c8] | Files named in setup fn | Hotspot labels (string IDs resolved) |
|---|---|---|---|---|---|
| 0x40a748 | 0x40a904 | 0x40ab74 | 0x40adcd | BIRD1.TAF, CHICKEN.TAF, FADKREUZ.TAF, FBI_CAR.TAF | 0:Bar; 1:Police; 2:Video store; 3:Sex store; 4:Parking lot; 5:Pawnstore; 6:Motel; 7:Distributor; 8:Chicken farm |
| 0x40a451 | 0x40a51c | 0x40a5a2 | 0x40a6d6 | - | 0:Realtor; 1:Sex store; 2:Bank; 3:Office; 4:Distributor; 5:Beauty clinic; 6:To the parking lot |
| 0x40b25c | 0x40b390 | 0x40b416 | 0x40b8d0 | CITIES.TAF, CTY_MEN.TAF | 0:To the bank; 1:To the Black Cat agency; 2:To the lady realtor; 3:To the advertising agency; 6:To the branches; 5:To the airport |
| 0x413ef9 | 0x41409f | 0x414129 | 0x4141cc | BUTCH.TAF, DET_ANI.TAF, PFND_ITM.TAF | 0:To the town; 1:The owner |
| 0x413217 | 0x4132e0 | 0x413799 | 0x4137a4 | - | 0:Do business with the bank; 1:Back to the town |
| 0x41233f | 0x412558 | 0x412757 | 0x4128fb | VER_ANI1.TAF, VER_ANI2.TAF, VER_ICON.TAF, VIDEO.TAF | 0:To the town; 1:Photos; 2:Video; 3:Agent |
| 0x4111e1 | 0x4113b9 | 0x4114c5 | 0x41158e | Verl_ani.TAF | 0:To the town; 1:To the town; 2:Grant movie licenses; 3:Sell movie rights |
| 0x40c3f9 | 0x40c58c | 0x40c619 | 0x40c6de | SHOP1_~1.TAF | 0:To the town; 2:Sex toys |
| 0x40ba04 | 0x40bb08 | 0x40bbce | 0x40bc1c | SEX_ST2.TAF | 0:To the town; 1:Look at video charts; 2:Buy sex toy |
| 0x40e5a5 | 0x40e748 | 0x40ebec | 0x40ef78 | GIRL_FLM.TAF, LULA_ZIM.TAF, VER_ICON.TAF, ZIM_ANI1.TAF, ZIM_DET.TAF | - |
| 0x40cdae | 0x40ce79 | 0x40cfbe | - | SEKR_ANI.TAF | 0:To the overview; 1:To the overview; 2:Laptop; 3:Secretary; 4:Secretary; 5:Broom cupboard; 6:To the other WET buildings |
| 0x4101db | 0x41033c | 0x410500 | 0x4107c8 | OFFICE3.TAF, S_PARCEL.TBF | 0:To the overview; 1:To the property; 2:Laptop; 3:Flipchart; 4:Safe; 5:Planning parties; 6:Secretary; 7:Secretary; 6:Lula's present; 7:Lula's present; 8:Lula's present |
| 0x4167b1 | 0x41681c | 0x416853 | - | - | 0:Back to the town; 1:Look at real estate |
| 0x4168e0 | 0x416a10 | 0x416ea0 | - | MKL3_MEN.TAF | - |
| 0x4317c3 | 0x43186a | 0x4318bd | 0x43190d | - | 0:To the town |
| 0x4312fb | 0x4313bc | - | 0x4314af | - | 0:To the town; 1:Sabotage hostile companies |
| 0x431c1b | 0x431cb5 | 0x431cfe | - | GEG_MINI.TAF | 0:To the town; 1:Sabotage hostile companies; 2:Sabotage hostile companies |
| 0x43323a | 0x433374 | 0x433402 | 0x433491 | - | - |
| 0x43273f | 0x432842 | 0x432923 | 0x432944 | HAND_ANI.TAF | 0:To the town; 1:Give actors beauty treatment |
| 0x417d6c | 0x417eb0 | 0x418124 | 0x418989 | BAUTEN.TAF, MAKL_ROO.TAF | - |
| 0x419115 | 0x41925c | 0x419455 | - | LAGE_ST3.TAF | 0:To the office; 1:Hire staff |
| 0x41a0a8 | 0x41a1dd | 0x41a72f | 0x41a95e | CAST_DET.TAF | 0:To the other offices; 2:Place a job ad for actors; 3:Look at actors file; 4:Look at actors file; 5:Cast actors; 6:Produce cost figures; 7:Place a job ad for staff; 8:Look at staff file; 9:Look at applications |
| 0x41de87 | - | - | - | FILMPLAN.TAF | - |
| 0x41fa1a | 0x41fde8 | 0x42007a | 0x4201c2 | LULA_ANI.TAF, LULA_BAR.TAF, LULA_SCH.TAF | 0:To the office; 2:Cast Lula for movie; 3:Buy a present for Lula |
| 0x4208f2 | 0x420aa4 | 0x420cab | - | ABTL_ICO.TAF, DOWN_ICO.TAF, GEG_MINI.TAF | 0:To the town |
| 0x425c85 | 0x425f68 | 0x42615a | 0x4262c2 | BARLEUTE.TAF, LULA1.TAF | 0:To the town; 1:Toilet |
| 0x423104 | 0x423448 | 0x42388e | 0x423b28 | STUD_ANI.TAF, stud_dia.TAF | 0:To the other offices; 1:Hire lighting engineer; 3:Choose lighting; 2:Assign cameraman; 4:Choose camera; 5:Assign director |
| 0x429bed | 0x429d58 | 0x42a04f | 0x42a08f | LAG_FLM.TAF | 0:To the other offices; 1:Warehouse monitor; 2:Assign warehousewoman; 3:Process orders |
| 0x42ab96 | 0x42ac5e | 0x42ad85 | 0x42b35f | MAP3_ICO.TAF | 12:To the town; 13:To the town |
| 0x430833 | 0x430a52 | 0x430bb9 | 0x430cc4 | REZ_ANI.TAF, REZ_DET.TAF | 0:Exit |
| 0x43035d | 0x4304b2 | 0x4305af | 0x43060b | PFOERTNE.TBF | 0:To the other offices; 1:Fit out doorman |
| 0x42e20c | 0x42e424 | 0x42e661 | 0x42e821 | DOWN1.TBF, KLOSPRIT.TAF, PISSED.TAF | 0:Bar; 1:Drunk man |
| 0x42d711 | 0x42d940 | 0x42dbd9 | 0x42dca3 | TON_FLM.TAF, TON_MUS.TAF, TON_SPK.TAF | 0:To the other offices; 2:Dubbing monitor; 1:Assign (male or female) dubber; 3:Assign musician/sound engineer; 4:Buy equipment; 5:Add sound track to film |
| 0x42b98e | 0x42ba3f | 0x42bab7 | 0x42bb1d | - | 0:To the town; 1:Ma |
| 0x42b6ce | 0x42b797 | 0x42b8d0 | 0x42b92d | FREIZEIT.TAF | 0:To the other offices; 1:Buy equipment |
| 0x4283d5 | 0x4285f4 | 0x4289a2 | 0x428a06 | CUT_FLM1.TAF, CUT_FLM2.TAF, CUT_FLM3.TAF, CUT_FLM4.TAF | 4:To the other offices; 0:Cutting monitor; 1:Cutting monitor; 2:Cutting monitor; 3:Cutting monitor; 5:Assign cutter; 6:Buy equipment; 7:Cut films |
| 0x428f1f | 0x42906c | 0x42935b | 0x4293d3 | COPY_FLM.TAF | 0:To the other offices; 2:Copying monitor; 3:Hire copying girl; 4:Buy copying machines |
| 0x429445 | 0x42950e | 0x4295e9 | 0x4296d9 | NASA02.TBF | 0:To the map |
| 0x42f3a9 | 0x42f536 | 0x42f699 | 0x42f74c | MOT_ANI.TAF | 0:To the town; 2:Reception |
| 0x42eb33 | 0x42ec40 | 0x42ef01 | 0x42eff9 | WERBUNG.TAF | 0:To the other offices; 4:Assign advertising boss; 5:Assign switchboard operator; 6:Start advertising; 7:Start scandals |
| 0x42fa1c | 0x42fab3 | 0x42fb4f | - | ST3_SHOP.TAF | 0:To the town; 1:Sign contract for advertising |
| 0x42c7de | 0x42c9e0 | 0x42cb0e | 0x42cc83 | FOTOS1.TAF, MAN_ANI.TAF | 0:To the town; 3:Accessories |
| 0x42c28f | 0x42c4ec | 0x42c6f9 | 0x42c7b0 | REQUISIT.TAF | 0:To the other offices; 2:Hire assistants; 6:Buy props; 3:Hire set builder; 4:Buy sets; 5:Hire props manageress |

Pre-calls (VERIFIED): room 20 calls `0x418457(this, EDX = (previous room == 6))` only in stage 2; room 25 calls `0x42b518(this, EDX)` with EDX = 1 when the previous room is 20, EDX = 0 when [0x45d18d] == 10, and skips the call otherwise; rooms 24/33/34 share case 0x404530 (see below).

Room names (INFERRED from hotspot labels and file names): 1 town/city/USA map, 2 pawnshop (stage 1) or bank, 3 distributor (Verleih), 4 video/sex store, 5 home/office (stage 3 office with safe and flipchart), 6 realtor (Makler), 7 Black Cat agency (sabotage), 8 beauty clinic, 20 property/buildings, 21 casting office, 22 movie planning, 23 Lula, 24/33/34 movie studios 1-3 (pre-call 0x423071 gets EDX = 0 for room 24, 1 for 33, 2 otherwise) and the stage-1 bar, 25 warehouse or airport map, 26 reception/doorman, 27 sound studio or the stage-1 bar toilet, 28 recreation room, 29 cutting department, 30 copier or launch site (NASA02.TBF), 31 marketing office or motel, 32 props or photo shop.

Hotspots: `0x40604d(EAX=idx, EDX=left, EBX=top, ECX=right, [esp+4]=bottom, [esp+8]=string ID)`, `ret 8`, stores a 16-byte rectangle at `0x45d1b0 + 16*idx` (50 slots) and the ID at `0x45d4dc + 4*idx` when the ID is not 0 or -1. 214 registrations, all coordinates in 640x480 screen space (VERIFIED; full list in Appendix D). The hit test 0x4040ea uses inclusive bounds (`left <= x <= right`, `top <= y <= bottom`) and returns the first match.

### B.6 Resource pools and file formats

Pool handles opened by LOAD_ALL (VERIFIED). "CD" means the path passes through `0x40983f(name, 0)`, which prefixes the CD root from `CDROM.LOC` (buffer 0x4551d8); mode 1 uses the name unchanged (relative to the current directory).

| Handle | File | Prefix | NGS version / records in the shipped file | Use |
|---|---|---|---|---|
| [0x455340] | `DATA\BACK\BACK.TGP` (shipped as `back.tgp`) | CD | 21 / 81 TBF | Room backgrounds |
| [0x455344] | `DATA\PERSO\PERSO.TAP` | HD | 24 / 553 records of 382 bytes | People database; count in [0x455348]; 40-byte summary array [0x45555c] |
| (closed after load) | `DATA\PERSO\EQUIP.TAP` | HD | 24 / 325 records of 104 bytes | Equipment database copied to [0x455560], count [0x4555cc] |
| [0x45534c] | `DATA\PERSO\PORTRAIT.TGP` | HD | 21 / 172 TBF | Portraits; count [0x455350] |
| [0x455358] | `DATA\SOUND\SOUND.TAP` (shipped as `sound.tap`) | CD | 24 / 65 WAV | Sound effects; [0x455720] = 1 if open |
| [0x455354] | `DATA\SOUND\MUSIC.TAP` | CD | 24 / 41 WAV | Music; [0x45571c] is set to 1 when open (0x402023) and cleared again at 0x40202f; INIT_ALL later restores it from WET.1ST |
| [0x483a30] | `WET.DDF` | as given | 24 / 90 | Dialog definitions (B.7) |
| [0x483a34] | `DATA\DIALOG\DIA_BACK.TGP` (path from DDF record 0) | as given | 21 / 73 TBF | Dialog backgrounds |
| [0x483a38] | `DATA\DIALOG\STD_BUT.TAF` (path from DDF record 0) | loaded whole | TAF | Standard dialog buttons |
| local | `ADDGPX.TGP` | cwd | 21 / 8 TBF | Exit credits images 0-3 (0x409a32), also opened by 0x410ebf |
| local | `DATA\SOUND\ADDSND.TAP` | | 24 / 1 WAV | 0x410ebf |
| [0x455364] | `DATA\CURSOR\CURSOR.TAF` | CD | TAF | Mouse cursor frames |

Format readers and how they match `docs/asset-formats.md`:

| Function | Format | Behaviour (VERIFIED unless marked) | Matches asset-formats.md |
|---|---|---|---|
| 0x43ebd3 | NGS seek | `(EAX=fm, EDX=handle, EBX=index)`: seek 0, read 8-byte header into fm+0x105, compare "NGS" (3 bytes), clamp index to count-1, seek to `-(count-index)*4` from end, read the u32 offset, seek there. Returns the index. | Yes: header `NGS\0`, u16 version, u16 count; trailing u32 absolute payload offsets |
| 0x440041 | NGS read record | `(fm, handle, dest)`: seek -6 relative, read the 6-byte record header (u32 length, u16 type) into fm+0x105, read `length` bytes into dest, return length. Callers walk sequentially by seeking +6 after each payload. | Yes: payload offsets point 6 bytes after each record header |
| 0x43e8eb | TBF from current stream position | 13 callers; VERIFIED with ADDGPX.TGP at 0x409ace after 0x43ebd3; calls 0x43f7c1, 0x43fbbe, 0x442431 | Modes 0/2 per asset-formats.md (decoder not re-derived here) |
| 0x43e833 | TBF by file name | strings ".TBF", "TBF", "rb" (INFERRED: default extension) | Yes (INFERRED) |
| 0x43eca6 (inside Ghidra's 0x43ebd3) | TAF by file name | appends ".TAF" when no dot, normalises '/', reads the 0x312-byte header (`TAF\0`, version, frame count, size, 768-byte block, first-frame offset), requires type 2 from 0x442431, uses the frame-offset table if u16 at 0x30e >= 2 else walks 14-byte frame headers, decodes with 0x43f7c1 | Yes (frame header u16 mode, u16 w, u16 h, u32 end, u32 start, flag byte) |
| 0x434ec2 | Load whole TAF into one allocation | `(fm, path)` -> object `{u16 frames, u16 ?, u32 ?, u32 data, ...}`; uses 0x4418bb (size scan), 0x43ef7b (frame table), 0x43f477 (decode), 0x441a14 for type 8 | Yes (INFERRED object layout) |
| 0x43f7c1 | RGB565 RLE frame decoder | 4 callers | Mode-2 commands per asset-formats.md (INFERRED, not re-derived) |
| 0x442431 | Signature check | "TBF", "TPF", "TAF", "TFF" -> type code (TAF = 2) | TPF/TFF not documented in asset-formats.md |
| 0x43e59c, 0x4400b6, 0x43fbbe, 0x441454, 0x44033d | PCX reader, TMF reader (".PCX" default), VOC reader, type-by-extension (.TBF .PCX .TFF .VOC .MOD .TMF, MOD tags "M.K." "M!K!" "FLT4" "FLT8"), PCX/TBF/TAF writer | Generic library; writer used by the F8 screenshot (`%4d.PCX`, 0x409f76) | Not in the shipped data except TBF/TAF |
| 0x43700a | WAVE chunk parser | "WAVE", "Can't get Sample ID" | TAP type 27 payloads are WAV |
| 0x414d32 | WET.DDF open | seeks record 0, reads 0x204 bytes: two 256-byte paths (`DATA\DIALOG\DIA_BACK.TGP`, `DATA\DIALOG\STD_BUT.TAF`) + 4 bytes, opens the TGP and loads the TAF | DDF record 0 is 516 bytes (type 24) |
| 0x43ff89 | Whole text file into a buffer | used for `CDROM.LOC` | |

Path handling (VERIFIED): `0x441f14` converts '/' to '\' only up to the first '.', and many paths are passed raw. Code spellings differ in case from the shipped files (`BACK.TGP` vs `back.tgp`, `SOUND.TAP` vs `sound.tap`, `Perskart.TAF` vs `PERSKART.TAF`, `LAGE_ST3.TAF` vs `lage_st3.TAF`, `stud_dia.TAF` vs `STUD_DIA.TAF`, `CAST_DET`, `CTY_MEN`, `MAP3_ICO`, `SEKR_ANI`, `Verl_ani`, `LULA_ANI`). The host file layer must accept both separators and match case-insensitively (already the design of `rt_vfs.c`).

`CDROM.LOC` (0x4020e0): read whole (max 0x64 bytes) into 0x4551d8, CR/LF replaced by NUL, a '\' appended if the last character is not '\'. The shipped file is 0 bytes, so the CD prefix becomes `\` and CD resources are opened as `\DATA\...` (INFERRED: requires the host to map a rooted `\DATA` path to the game directory, or to supply a CDROM.LOC). If `\DATA\CURSOR\CURSOR.TAF` cannot be opened, CD_NOT_FOUND_DLG is shown; on failure the game shows the error and calls `exit(-1)`.

### B.7 In-game UI: the DDF dialog engine (VERIFIED unless marked)

Most in-game windows are not Win32 dialogs but records of `WET.DDF`, drawn by the game:

- `0x414e72 DDF_DoDialog(EAX=this [0x45dbd8], EDX=x, EBX=y, ECX=record index, [esp+4]=callback)`, `ret 4`: finds one of 10 free slots (flags 0x483a08[10], callbacks 0x483a70[10]), reads record `ECX` of WET.DDF into `0x45e3e8 + slot*0x3bd0` (records 1-89 are exactly 0x3bd0 = 15312 bytes), loads background TBF `[record+4]` from DIA_BACK.TGP unless -1, positions it (0x4163d2) and runs the modal loop 0x4156ea (which pumps messages through 0x416534 and draws the cursor). The callback receives Win32-like messages: 0x40feac compares EDX with 0x110 (init) and 0x111 (command) and switches on EBX+1 (control index) through the jump table 0x40fe94.
- `0x414ff9` ends a DDF dialog (37 callers; called as `(EAX=this, EDX=ESI of the callback, EBX=1)` at 0x40ff30; argument meaning INFERRED); `0x416388` returns a control record (47 callers; field +0x18 read as state, INFERRED); `0x4165ae`, `0x415ec6`, `0x415f50` set control text/state (INFERRED).
- `0x404ecf(text)` / `0x404f66(text)`: in-game info box and yes/no box built on DDF dialogs; `0x4058c8` loads their text from string resources.
- Main menu: `0x40fe68` = `DDF_DoDialog(-1, -1, 87, 0x40feac)`. Callback cases: load game (GAME_IO_DLG with 0x40d9e3), new game (`0x40261b(0)` intro / `0x40261b(1)`), quit (string 901 yes/no, sets [0x4555e4] = 1).
- Options dialog: F2 calls 0x410bc9, which tail-jumps into the main-menu call sequence with `DDF_DoDialog(-1, -1, 36, 0x40d123)`. Callback 0x40d123 contains Save (GAME_IO_DLG/0x40d611), Load (GAME_IO_DLG/0x40d9e3), the sound settings with `0x438d56` (call at 0x40d340) and volume controls. 0x438d56 builds a popup menu: separator, "      Set Digital Output", separator, one item per output format from the pointer table 0x45201c ("8.000  kHz,   8-Bit, Mono", "8.000  kHz,   8-Bit, Stereo", ... the current one with MF_CHECKED 0x8), separator, "            CANCEL"; then `TrackPopupMenu(hmenu, 0, 100, 40, 0, hwnd, NULL)` and a `GetMessageA` loop while its temporary WM_COMMAND handler 0x43918f is installed.

### B.8 Sound, music, MIDI/CD (summary)

- DirectSound via thunk 0x44cf54 from 0x4366ae; 0x436807 sets up the multimedia timer (B.4). DSERR texts at 0x436e35.
- Channel API on [0x455024] (names from their message strings, INFERRED): 0x4374a3 start, 0x4376cd pause, 0x4377f4 resume, 0x437965 "Set Frequency" (also used as a channel-busy query at 0x40a2eb), 0x437de9 stop buffer, 0x437ea9 start buffer, 0x43764d stop channel, 0x437c60 stop all, 0x437d3e/0x437d6f/0x438f62 settings from WET.1ST.
- `0x438712(EAX=sound obj, EDX=pool handle, EBX=channel, ECX=0xff)` (ECX meaning INFERRED: volume or loop) plays the current NGS record of a pool (e.g. MUSIC.TAP record 5 on channel 8 in the credits, 0x409aac). The clock 0x40a1f0 restarts music on channel 8 when it stops.
- Sound effects: [0x45d944] manager; `0x40855e(mgr, EDX=effect id)` seeks SOUND.TAP (0x43ebd3) and parses the WAV (0x43700a); 0x408752 (51 callers), 0x408814, 0x40886c, 0x4088f5 (stop all, called by ChangeRoom) manage effects (INFERRED names).
- MIDI (0x4434c1, "sequencer") and CD audio (0x443afb, "cdaudio") use `mciSendCommandA` (out of scope) and register MM_MCINOTIFY handlers.

### B.9 Files written and read (save/load)

No `CreateDirectoryA` is imported (VERIFIED), and `original/app` has no `DATA\SAVE` or `DATA\DATABASE` directory. The host must create both in the writable save root, otherwise every save fails with error 8.

| File | Code | Format (VERIFIED layout; field names INFERRED) |
|---|---|---|
| `DATA\SAVE\WET.1ST` | read 0x4012ce in INIT_ALL, written 0x401798 in shutdown | Exactly 0x84 bytes = guest block 0x455034..0x4550b7. Known fields: +0x04 <- [0x4555f0] (sound on), +0x08 [0x45571c] (music), +0x0c [0x455720] (effects), +0x10 [0x45572c], +0x14 [0x455724] (wave volume, passed to 0x437f28), +0x18 [0x455728]. Missing file = first run (intro, defaults). |
| `DATA\SAVE\SAVEGAME.%3d` | write 0x40d71d (save proc 0x40d611), read 0x40daec and 0x40ddbc (load proc 0x40d9e3) | Name from `sprintf("%3d", slot)`, so slot 1 is `SAVEGAME.  1` (spaces in the name). Content: 0x7b8d bytes of guest block 0x455620..0x45d1ac (whole game state, including room [0x455690], stage [0x455694], date/time, account [0x45568c]), followed by `[0x455348] * 40` bytes of the PERSO summary array [0x45555c]. |
| `DATA\DATABASE\FILMB.TMP` | created through POOL_CLASS (0x4391ce from 0x411bff), read/updated by 0x40bfed, 0x411ed9, 0x42a1ef, 0x42a4b9, 0x42a6b0 (`rb`, `rb+`) | NGS pool (POOL_CLASS writes `NGS\0`, u16 version, u16 count, empty 6-byte headers, index); movie records of 0x128 bytes, rewritten in place (seek -0x128). Deleted at startup (0x4020ad) and in INIT_GAME_VARS. |
| `DATA\DATABASE\FILMB.%3d` | save: copy TMP -> slot (0x4424b3 at 0x40d7cb); load: copy slot -> TMP or delete TMP | byte copy of FILMB.TMP |
| `DATA\DATABASE\STF1DAT.%3d` | write 0x40fca0, read 0x40fd83 (called from save/load) | u32 count, then count records of 0x68 bytes from the list [0x45d940] (same record size as EQUIP.TAP) |
| `W_DEBUG.DAT` (cwd) | deleted at WinMain start, then `WritePrivateProfileStringA` per checkpoint via 0x409eb7 | INI text, section STARTUP_DEBUG and others, value "OK" |
| `WET.INI` (Windows directory semantics of the profile API) | 0x409e26 / 0x409e6a | `[VIDEO] VideoPlay=<int>` |
| `<nnnn>.PCX` | F8 screenshot 0x409f76 | PCX writer 0x44033d |
| `CDROM.LOC`, `ADDGPX.TGP`, `DATA\PERSO\HLP.DAT` | read only | HLP.DAT is parsed for BEGIN/END sections by 0x409c83 |

The save dialogs read and write the save name at 0x455628 (`LB_GETTEXT`, `GetDlgItemTextA` with cch 0x50); that address lies inside the saved block, so the name is stored in the save itself (INFERRED).

### B.10 Key game-state globals (VERIFIED addresses, INFERRED names)

| Address | Meaning | Evidence |
|---|---|---|
| WORD [0x455030] | Global error code | 689 references, see B.3 |
| [0x455614] / [0x455618] | Main hwnd / hInstance | WinMain 0x403ac9 / 0x403af7 |
| [0x455690] | Current room ID (1-34) | ChangeRoom |
| [0x455694] | Stage 1-3 | room enter switches |
| [0x45569c], [0x4556a0], [0x4556a4] | Day, month, year (start 1-1-1997) | status line format "%s %d-%d-%d %d:%2d Account: %ld" (0x403f99), test against 0x7cd at 0x4051a1 |
| [0x4556a8], [0x4556ac] | Hour, minute | clock 0x40a1f0 |
| [0x4556b8] | Weekday 0-6 (names at 0x4556c4 + 12*n) | 0x403f83 |
| [0x4556bc], [0x4556c0] | Minute length in ticks / countdown | 0x40a1f0 |
| [0x4555d0], [0x4555d4] | Day start hour / closing hour | 0x40a1f0 |
| [0x45568c] | Account balance | status line |
| [0x4555e4] | Quit/leave session flag | main loop |
| [0x45dbe0] | 1 = in game (frame logic active) | 0x40413f |
| [0x4553a8] / [0x4553b0] | Left / right click pending (INFERRED) | 0x40413f, 0x404378 |
| 0x455384 / 0x455394 | Last key and mouse state / mouse point | 0x405862 |
| 0x4550d8 | 256-byte general text buffer (195 references) | sprintf/LoadString target |
| [0x48a644] | DirectDraw context (+0 hwnd, +8 IDirectDraw, +0xc primary, +0x10 back buffer, +0x3c windowed flag) | 0x43daf1, 0x43d646 |

### B.11 The 30 most important functions

| # | VA | Role | Level |
|---|---|---|---|
| 1 | 0x442cfc | Entry: set WinMain pointer, jump to Watcom cstart 0x4477c4 | VERIFIED |
| 2 | 0x4037e4 | Static constructors of the 21 room objects (XI priority 0x40) | VERIFIED |
| 3 | 0x40399d | WinMain (stdcall 16): INI, argv, INIT_ALL, menu, session loop, exit | VERIFIED |
| 4 | 0x401010 | INIT_ALL(hInst, nCmdShow): singletons, window, CD path, pools, WET.1ST | VERIFIED |
| 5 | 0x401ab6 | LOAD_ALL: opens BACK/PERSO/EQUIP/PORTRAIT/SOUND/MUSIC pools, WET.DDF | VERIFIED |
| 6 | 0x4020e0 | INIT_CD_PATH: CDROM.LOC, CD_NOT_FOUND_DLG | VERIFIED |
| 7 | 0x401757 | Shutdown: leave hook, write WET.1ST, close pools, destroy singletons | VERIFIED |
| 8 | 0x40413f | One frame: WM_TIMER dispatch, tick, input, hotspots, click hook, redraw | VERIFIED |
| 9 | 0x405862 | Drain message queue and poll mouse/keyboard | VERIFIED |
| 10 | 0x4050c1 | Game tick: scene tick hook, clock, hourly simulation | VERIFIED |
| 11 | 0x40a1f0 | Game clock (minutes, hours, days, 30-day months), music restart | VERIFIED |
| 12 | 0x404378 | ChangeRoom via jump table 0x4042f0 | VERIFIED |
| 13 | 0x40989c | INIT_STUFE(stage, room): rebuild scene after new game or load | INFERRED role |
| 14 | 0x40527f | Reset hotspots and scene hooks | VERIFIED |
| 15 | 0x40604d | Register hotspot (rect + string ID) | VERIFIED |
| 16 | 0x4040ea | Hotspot hit test | VERIFIED |
| 17 | 0x4047e8 | LoadStringA wrapper | VERIFIED |
| 18 | 0x4058c8 | Message/question box by string ID | VERIFIED |
| 19 | 0x414e72 | DDF_DoDialog: in-game modal dialog from WET.DDF | VERIFIED |
| 20 | 0x414d32 | Open WET.DDF, DIA_BACK.TGP and STD_BUT.TAF | VERIFIED |
| 21 | 0x40fe68 | Main menu (DDF record 87, callback 0x40feac) | VERIFIED |
| 22 | 0x40d611 / 0x40d9e3 | Save / load dialog procedures (SAVEGAME, FILMB, STF1DAT) | VERIFIED |
| 23 | 0x433f2e | Create main window, 60 ms timer, DirectDraw (640, 480, 16) | VERIFIED |
| 24 | 0x4343f1 | WndProc with per-message handler table (0x4343a2 registers) | VERIFIED |
| 25 | 0x43d646 | Flip and re-lock back buffer (restores lost surfaces) | VERIFIED |
| 26 | 0x43ebd3 | NGS: seek to record N | VERIFIED |
| 27 | 0x440041 | NGS: read current record | VERIFIED |
| 28 | 0x434ec2 | Load complete TAF animation | VERIFIED (layout INFERRED) |
| 29 | 0x4425ac | File open wrapper (`rb`, `wb`, `rb+`, `wb+` on CreateFileA); 0x4426d1 read, 0x44272c write, 0x442690 seek | VERIFIED |
| 30 | 0x438003 | Multimedia-timer sound callback (runs on another thread on Windows) | VERIFIED |

Close runners-up: 0x444816 sprite blit (640/0x500 stride case), 0x43c3b4 sprite/animation draw (10 callers), 0x43b578 full-screen image blit, 0x40855e sound-effect play, 0x438712 pool playback, 0x4391ce POOL_CLASS create, 0x403f22 full scene redraw.

### B.12 Out-of-scope call sites (listed only)

| Import | Call sites (owner) |
|---|---|
| CoInitialize | 0x439e74 (0x439e2c) |
| CoUninitialize | 0x439eea (0x439e2c) |
| CoCreateInstance | 0x43a083 (0x43a061) |
| mciSendCommandA | 0x44352a, 0x4435b4, 0x443664, 0x443734, 0x4437ae, 0x443845, 0x443a49 (MIDI 0x4434c1); 0x443b3e, 0x443b9f, 0x443beb, 0x443cd0, 0x443d7b, 0x443dc5, 0x443e2e (CD audio 0x443afb) |
| Video code | 0x4092b1 (`DATA\VIDEO\`, string IDs 8000-8042, re-inits sound with 0x4366ae at 0x4094b1), 0x439e2c-0x43a98b |

### B.13 Notes for the recompiler and runtime

1. Indirect call targets that are not Ghidra functions: all scene hooks (B.5), dialog procedures (A.5), WndProc handlers (B.4), the MM-timer callback 0x438003, the DDF callbacks (e.g. 0x40feac, 0x40d123) and WinMain 0x40399d. 274 such entries are found through relocations (Appendix C).
2. Tail-merged call sites (`push ...; jmp <into another call sequence>`) exist around DialogBoxParamA (0x40d1ff, 0x413922, 0x41e5a8, 0x421fb5, 0x4236a5). A per-function lifter must allow jumps into the middle of another function's call sequence.
3. Game timing depends on WM_TIMER (60 ms) and on PeekMessageA message-range filtering; the main loop never blocks.
4. 0x438003 is reentrant-guarded, but on Windows it runs concurrently with the game thread. Running it on a host thread needs the global guest lock.
5. The game writes directly into the locked back buffer between flips; the DirectDraw layer must keep surface memory in guest space at a stable address while locked.
6. String and dialog resources are read from the image only; no file outside `original/app` is needed for them.

---

## Appendix A: all string resources (ID, text; `\n` = newline)

```
101	Distributor
102	Realtor
103	Sex store
104	Bank
105	Office
106	Beauty clinic
107	To the parking lot
108	To the bank
109	To the Black Cat agency
110	To the lady realtor
111	To the advertising agency
112	To the office
113	To the airport
114	To the branches
201	To the overview
202	Flipchart
203	Door
204	Laptop
205	Statue
206	Secretary
207	Safe
208	To the other WET buildings
209	That's just a junk room.
210	Broom cupboard
211	Movies for sale
212	Following movies:
213	You haven't got any finished movies!
214	%s is going to the sale
215	You haven't got a fully equipped copier
216	You haven't got a fully equipped warehouse
217	Your buildings are full of trash. Your people need %d hours to clean them!
218	The movie is being cut!
219	The sound track is being added!
220	Something's gone wrong with your %s equipment.
221	Because you haven't hired a secretary, you can't undress one!
250	To the property
251	Planning parties
252	Lula's present
253	Catch me!!
254	A little present from me to celebrate our new home..
255	Don't run away honey, you have to open my present first!
256	  Yearly
257	 Half-yearly
258	 Every two months
259	  Monthly
260	Every two weeks
261	  Weekly
262	Ain't nothing to eat
263	What the dog left over
264	Really pathetic
265	Only for guests
266	Normal size
267	Well provided
268	Fantastic food
269	Top class provisions
270	Super-fantastic style
271	Hyper-horny state buffet
272	There's a party at your house this evening.
273	There's a boring party going on in your cabin right now.
301	Reception
302	Cutting department
303	Sound studio
304	Copier
305	Movie studio
306	Marketing office
307	Warehouse
308	Back to the office
309	Back to the office
310	Movie planning
311	Props
312	Casting office
313	Recreation room
314	Studio 2
315	Studio 3
316	Back to the realtor
317	Money-back guarantee
318	You can't cancel till next month.
319	#NAME?
320	#NAME?
321	The realtor offers you $%d. Do you want to sell?
322	The realtor isn't interested right now.
350	Hire staff
351	Expand site
352	Buy private jet
353	The private jet costs $850,000. Want to buy it?
354	The plane will be ready to use in %d days.
355	You'll need a runway and a hangar!
356	The building work will take about %d days.
357	The work will cost $%ld. Start building work?
358	Do you really want to fire the girl pilot?
359	Hire girl pilot for $8000 a month?
360	Chambermaid
361	Gardener
362	Cook
363	Girl pilot
364	Nightwatchman
365	You still haven't got a pilot for the plane!
401	Do business with the bank
402	Back to the town
403	Get lost! Can't you see I'm busy!
404	I shouldn't disturb the manager for at least %d hours
405	Oh well.. Looks as if I've been stood up.
406	Offer the bank manager to let Lula help out??
407	Mhhm, I know someone who could help out.
408	I would be really grateful.
409	I'd better leave you alone with Lula.
410	Thank you, come back in 2 hours.
411	Then we'll talk about your credit limit.
412	I've raised your credit limit by 10%
413	I shouldn't disturb the manager for at least an hour
501	Back to the town
502	Look at real estate
503	Look at property
504	Look at what's offered
505	Mansion 'Like It'
506	Mansion 'Pretty Good'
507	Mansion 'Megaswank'
508	Mansion is rented for a week.
509	Mansion is sold to you.
510	The lady realtor offers you $%ld for the property. Accept the offer?
511	The lady realtor isn't interested right now.
512	You must sell your other mansion first. One is enough.
513	The lease for your present property doesn't run out for %d day(s).
514	It would be better to look for a decent roof over your head first!
515	Rent sex store
516	Look for mansion
517	Little junk store
518	Medium-sized store
519	Sex wholesale market
520	Sex'n sell superstore
521	Suburbs
522	Near city center
523	City center
524	The rental for the mansion is due today. Pay the rental?
525	You must have a new home by this evening!
526	For more than 10 stores, you need a central warehouse. Buy it for $500,000?
527	A store this big needs $%ld startup capital. Rent the shop?
528	Back to the realtor's office
600	Work on storyboard
601	Write screenplay
602	Honey! You're really creative, but it's better to make one whole movie than dream about %d!
603	Define the subject of the movie
604	Slow Bang 2 - sex in slow motion for you if it's usually over too fast
605	Sex in Hospital - adventures of a never-satisfied nurse
606	Battle Sex - a female battalion in the wrong dugout
607	Sex Hackers - this is how programmers do it - complete perverts!!!
608	Crash Sex Dummies - sex in, on and under the auto
609	Sex Castle - a young couple do it for the ghosts of an old castle
610	Frankenstick - the ingenious creation is revealed as a sex monster
611	Orgasm Pizza - a pizza party turns into a wild orgy
612	A cleaning lady brings a zonked-out baseball team back on form
613	Full Commitment - a woman blows herself all the way to the top of the recording company.
614	The Saddle-horn - a riding instructor lets it all hang out - sex in the paddock
615	How the Knights did it - into battle in full armor
616	Sex Attack - aliens make humans into ever-ready sex slaves
617	Suck Rogers - the hottest buck in the Universe - robot sex
618	RoboRub - a cyborg goes crazy and screws through Manhattan
619	Deep Sex 9 - man bitten by moray eel during underwater sex
620	SexMax- superheroine makes the blind see by supersex
621	Virtual Vagina and her sex adventures in cyberspace
622	Dream Girl - inflatable sex doll changes into horny girl
623	Paris 06 - a man can do it only on the Eiffel tower
624	Wild West Sex - 3 gun-crazy girls force sheriff into group sex
625	Highway 69 - a tailback becomes a hot orgy
626	Mermaids Blow it Better... and sink the Titanic
627	The English Patient... revealed as natural wonder (16 inches)
628	Sex Fighter 3 - crazy girl students jump karate teacher
629	Match Ball - a mixed double gets out of hand
630	Dirty Henry - he looks like spew, but don't let him get his pants off
631	Miss Cilla's feeling for sex gets her into more and more adventurous positions
632	12 a.m. - sex in the lunch break - cafeteria report
633	Sex in the Gravel Pit - 2 pimply youths dredge 2 girls (fantastic!)
634	Ink Girls - the fantasies of a crazy graphic artist come true
635	Sushi Sex - couple find out that fish is aphrodisiac
636	Bang Danger - gnome has become too small and is raped by Amazons
637	Charlie's Tools - Charlie's girl wants to know at last
638	Star Sex Voyager - Captain Darling lets her whole crew have it
639	Little Red Riding Hood - the wolf is really a horny ram in a wolf-skin
640	Snow White...doesn't just give it to the seven dwarves
641	Dragonfart - a dragon has bad flatulence, only good sex helps
642	006 & Moneyhenny - the horny truth about the famous agent
643	Sex at Court - what the tabloids don't tell you
644	Lubber Fantasies - a man dies and is reborn as a condom.
645	The Desert Planet - spice makes the guys hot - giant penis worms
646	Story???! What for??! Why not pure sex.
647	New movie title: 
648	Movie title to be worked on:
649	Possible story lines:
650	Unfortunately we can only produce movies up to 120 minutes
651	You can't work on the movie any more HERE after that! Finish anyway??
652	The planning for this movie is already finished!
653	Fantastic idea! I just don't believe that people will buy a blank tape!
654	You can't finish planning until you've hired all your staff!
655	This is the place for the production designer, you haven't hired one!
656	Unfortunately you still haven't got a screenplay writer, even if it looks as if you have!
657	You can see the production assistant, but you haven't got one!
700	Place a job ad for actors
701	Place a job ad for staff
702	Look at actors file
703	Look at staff file
704	Cast actors
705	Produce cost figures
706	completely inexperienced and untalented
707	completely inexperienced and talented
708	fairly inexperienced and untalented
709	fairly inexperienced and talented
710	amateur and untalented
711	amateur and talented
712	semi-professional
713	professional, experienced
714	fully professional
715	super-professional
716	really pathetic
717	pathetic
718	very small
719	small
720	normal
721	good
722	very good
723	above-average
724	exaggerated
725	really princely
726	nightmarish
727	terrible
728	very bad
729	bad
730	average
731	great
732	good
733	super-good
734	exaggerated
735	hyper-brilliant
736	completely impotent
737	almost impotent
738	slightly potent
739	fairly potent
740	averagely potent
741	above-average potent
742	good, very potent
743	professional, super-potent
744	fully professional, mega-potent
745	super-professional, hyper-potent
746	beer belly and drooping ass
747	lost figure
748	spare tire
749	flabby arms
750	average figure
751	good ass
752	figure in good training.
753	with ´washboard stomach´.
754	body in peak training.
755	Super-duper fit body.
756	The newspaper isn't taking any more ads right now!
757	a lighting engineer
758	a casting director
759	a cutter
760	a screenplay writer
761	a cameraman
762	a girl to make the copies
763	a set builder
764	a storewoman
765	a doorman
766	a production assistant
767	a production designer
768	a cleaning lady
769	a director
770	a props assistant
771	a props manageress
772	a secretary
773	a (male or female) dubber
774	a switchboard operator
775	a sound engineer/musician
776	an advertising manageress
777	no training and a complete dope.
778	no training and a total zombie.
779	no training and work-shy.
780	no training.
781	with training.
782	with good training.
783	with very good training.
784	with very good training and experience.
785	finished college.
786	finished college and 10 years experience.
787	Look at applications
788	Yeah well, make a movie like this, you should know you need at least two actors!
789	Send %s for training at %d dollars a day?
790	%s is frustrated and has given notice!
791	Don't you think you should think about something else sometimes?
792	So long as you hang around here, there won't be any applications!
800	Hi honey..
801	Oooh..You get me going....
802	You dare to show your face here!? FAILURE!!
803	Sorry, I can only act in ONE movie at a time!
804	Cast Lula for movie
805	Buy a present for Lula
806	Exclusive perfume
807	Designer underwear
808	Pedigree pussy (not shaved)
809	Fur coat (synthetic)
810	Motor-bike
811	Designer watch
812	Convertible
813	Luxury mansion
814	Do you really want to buy that?
815	In Lula's possession.
850	Staff
851	Departments
852	Balance sheet
853	Competition
854	The store is short of $%ld of stock for the campaign. Stock up??
855	The conversion costs $%ld, the extra stock costs $%ld. Do conversion?
856	The switch has freed up $%d as store capital!
857	The store hasn't got enough capital to pay out money!
858	The conversion costs $%ld. Do conversion?
859	%s (%s/%s) only has capital for %d days!
860	%s (%s,%s) is busted. Cancel lease?
861	The cost difference has been deducted from the account.
862	One of your stores has been sabotaged!
901	Are you sure you want to leave me already?
902	And another busy day comes to an end.
903	Unfortunately you haven't got enough money!
904	To the other offices
905	To the office
906	To the town
907	You've exceeded your credit limit, you have to balance your account today!
908	Your company is bust, Lula has left you, and you're on the street. Do you want to start again?
909	There's no screenplay yet!
910	In planning
911	In production
912	Lighting engineer
913	Casting Director
914	Cutter
915	Screenplay writer
916	Cameraman
917	Copying girl
918	Set builder
919	Warehousewoman
920	Doorman
921	Production assistant
922	Production designer
923	Cleaning lady
924	Director
925	Props assistant
926	Props manageress
927	Secretary
928	Dubber
929	Switchboard operator
930	Sound engineer
931	Advertising manager
932	Actor
933	Actress
934	Victim
936	You haven't got an actress
937	You haven't got an actor
938	You haven't got any staff in this department
939	Lighting
940	Camera
941	Copying machine
942	Video editing machine
943	Packing machine
944	Guns
945	Leisure equipment
946	Stage props
947	Sound equipment
948	35mm film
949	Sex toy
951	There's no movie in production yet!
952	Doesn't feel too good with you.
953	Doesn't feel at all good with you.
954	Is angry about the type of treatment.
955	Is overworked and underpaid.
956	Is pissed off with you.
957	Feels good with you.
958	Is glad there's nothing to do.
959	Feels really good.
960	Thinks you're the best boss ever.
961	Is as happy as a pig in shit.
962	Is glad there's nothing to do.
963	Is occupying the available place right now.
964	Has a part in this movie.
965	Already has a part in another movie.
966	Is still in training for %d day(s)!
967	Is still sick for %d day(s)!
968	Has a part in a movie!
969	Buy
970	Sell
971	Buy %s
972	Sell %s
973	Use
974	Use %s
975	Bck
976	You can't sell that while it's needed for the movie!
977	That's already being used!
978	$%d was deducted from your account
979	Unfortunately %s is being closed
980	You're arrested and end up in the can
981	Spot    
982	Camera  
983	V-camera
984	Film    
985	V-tape  
986	Dildo   
987	Whip    
988	Vibrator
989	P-egg   
990	Vaseline
991	Mass.oil
992	Chain   
993	Corset 
994	Boots   
995	Teddy   
996	Photos 
997	You haven't got any equipment yet!
1001	Cutting monitor
1002	Assign cutter
1003	Buy equipment
1004	Cut films
1005	Produced films:
1006	Cutting the film will take %d day(s)
1101	Copying monitor
1102	Hire copying girl
1103	Buy copying machines
1150	To the map
1151	CONGRATULATIONS you have completed all three stages! Do you want to play again?
1152	You now have enough money to launch the rocket!\nEverything is ready in Houston.
1201	Warehouse monitor
1202	Assign warehousewoman
1203	Process orders
1204	Sex&Sell Inc.        
1205	GreenFuck Inc.       
1206	SVE Inc.             
1207	BuyMe Video          
1208	Sex Attack Film&Video
1209	You haven't got any orders!
1210	The pilot fell asleep and went off course. The flight is delayed by %d hours.
1211	There's a thunderstorm over the destination airport. The flight will last %d hours longer.
1212	Feminists have occupied the airport. Everything is delayed by %d hours.
1213	A drunk flight engineer has killed an engine. The flight will last %d hours longer.
1214	The plane had to turn round because the pilot forgot his condoms. Delay %d hours.
1215	The whole crew was abducted by a UFO and Lula had to escape from the plane. Delay %d hours.
1216	The plane flew in circles inside a tornado for about %d hours.
1217	The pilot stopped on Highway 66 to get himself a beer. That lasted %d hours.
1218	The plane made a short flight into orbit, which took an extra %d hours.
1219	The fuel won't last as far as the destination. Result: %d hours on foot.
1301	Buy equipment
1401	Hire assistants
1402	Buy props
1403	Hire set builder
1404	Buy sets
1405	Hire props manageress
1406	Look darling, he looks just like our boss
1407	Don't let that put you off. I'M COMING!
1408	Who did you mean?
1409	Who do you think!
1410	But he looks just like our boss!
1411	Let's see. Mhmm..
1412	He doesn't just look like him, it IS him!
1413	Whoops!
1414	Yeah, whoops!
1501	Dubbing monitor
1502	Assign (male or female) dubber
1503	Assign musician/sound engineer
1504	Buy equipment
1505	Add sound track to film
1506	Films which need sound track:
1507	Adding sound track will take about %d day(s).
1601	Assign advertising boss
1602	Assign switchboard operator
1603	Start advertising
1604	Start scandals
1605	Advertising this way will cost you $%ld. Start advertising?
1606	This scandal will cost you $%ld. Prepare scandal?
1607	Major pile-up after striptease on bridge over Atlanta expressway!
1608	WET company's porn star strips on morning talkshow!
1609	Lula, WET company's porn star, fucks mayor with cameras watching!
1610	Lula, WET company's porn star, sells dildos at car boot sale!
1611	Porn star Lula gives autographs for hour at Oregon nursery school!
1612	At midday in Chicago, unmistakable photos of porn star float down from sky!
1613	Lula, WET company's star, is candidate for mayor of WET City!
1614	Cardinal has heart attack after porn star bathes naked in fountain in St. Peter's Square!
1615	Porn star Lula distributes condoms to congregation after service!
1616	Scandal in White House! President caught in flagrante with WET company's porn star!
1650	Sign contract for advertising
1651	You haven't got a store in this city!
1652	The shop's advertising budget must be raised by $%ld. Do it?
1653	The advertising budget is now $%ld per month.
1701	Fit out doorman
1702	You haven't got a doorman yet
1703	Your site has been attacked by feminists, who demonstrated there for %d hours!
1704	You've been sabotaged! The whole operation came to a stop for %d hours.
1801	Sabotage hostile companies
1802	We never do more than one job at once!
1803	The rockers have disappeared for a while.
1804	The %s company has been 'visited' by the Hell Devils!
1805	Drippin´ Lips
1806	2 Tail Hearts
1807	Lemon Juice
1808	Double D
1809	Burning Heels
1850	Kind of treatment:
1851	No stores in the city
1852	This store is already being 'treated'
1853	You haven't chosen a store yet!
1854	Store is 'treated' for %d days.
1855	All stores are already being sabotaged
1901	Give actors beauty treatment
1902	The operation you want costs $%d. Do it?
1903	No dice, as long as he's cast in a movie!
1904	No dice, as long as she's cast in a movie!
1905	He can't be operated on while he's still in training!
1906	She can't be operated on while she's still in training!
1907	He's still sick because of the last operation!
1908	She's still sick because of the last operation!
2000	Hire lighting engineer
2001	Assign director
2002	Assign cameraman
2003	Start shooting
2004	Break off shooting
2005	Choose lighting
2006	Choose camera
2007	Available titles:
2008	Well honey, you should cast at least two actors.
2009	You can't wait for it? But you still have to choose a movie!
2010	The director recommends %d days of shooting
2011	Days of shooting set at %d
2012	There aren't enough staff to carry on shooting
2013	There isn't enough equipment to carry on shooting
2014	There aren't enough actors to carry on shooting
2015	%s has swallowed the wrong way during fellatio and has had to throw up!
2016	%s has got in a panic about fucking!
2017	%s can't open her partner's zip!
2018	%s fainted when she saw her partner!
2019	%s refuses to suck her partner as long as he stinks of fish!
2020	%s has diarrhea and has to go to the toilet urgently!
2021	%s has broken a tooth during a blow job!
2022	%s has gone to sleep and stinks of booze!
2023	%s was hit by a falling spotlight!
2024	%s has got his prick tangled in his partner's bra!
2025	%s has got lumbago!
2026	%s stinks of smoked herring and has put the whole team to flight.
2027	%s has fallen out of bed and sprained his wrist!
2028	%s has driven his partner away with enormous farts!
2029	%s is having problems with his staying power!
2030	%s came before the camera was running!
2031	%s doesn't dare to drop his pants!
2032	%s has a bad hangover and doesn't feel like it at all!
2033	%s is scared stupid of his partner!
2034	%s has burnt his backside on a spotlight!
2035	%s slept on the traction bench last night and can't do anything today!
2036	%s has to go urgently to the little boys' room
2037	For the next scene, we need %s!
2038	a tube of lubricating cream
2039	a small dildo
2040	a medium-sized dildo
2041	a giant dildo
2042	a whip
2043	a powerful vibrator
2044	a set of pleasure eggs
2045	an inflatable sex doll
2046	a traction bench
2047	a pleasure plug
2048	some slippery vaseline
2049	massage oil
2050	a penis frill
2051	a garden hose
2052	a nipple chain
2053	a rubber suit
2054	a knobbed condom
2055	a tickling attachment
2056	rubber panty-hose
2057	a rubber corset
2058	a pair of rubber gloves
2059	a pair of rubber boots
2060	half a chicken
2061	a teddy bear
2062	OK, big man! Everyone take a break for 1 hour!
2063	%s still isn't going on!
2064	Shooting is continuing!
2065	Yeah baby, be HARD!
2066	You're slacking off baby, that didn't work!
2067	You were really convincing!
2068	You pay a $500 bonus
2069	You really want to fire %s?
2070	OK, %s has been fired!
2071	Actually take %s out of the cast?
2072	%s has lost the part!
2073	The director is completely frustrated and has given notice!
2078	The director didn't ask for that. Do it anyway?
2079	That's already being used!
2100	Look at video charts
2101	Buy sex toy
2102	You have no props department where you could store the things!
2103	Do It Again Sam
2104	Little Red Riding Hood And The Horny Wolf
2105	Great Time Under The Mini-Skirt
2106	In The Woods And On The Heather
2107	The Horror Cabinet Of Dr. Sexglove
2108	Alice's Panties In Wonderland
2109	The Ex-Sperminator
2110	Making Music At Home: Blowing The Horn And Other Pleasures
2111	Fuck Zone 2048
2112	Jungle Sex Olympics
2113	WET ATTACK, The Space Orgy
2114	Star Sex I
2115	When Men Still Had Tails
2116	Housewives' Sex Report
2117	Attack Of The Fuckers
2118	Summer, Sun, Sand And More
2119	Sex, Beating And Libido
2120	Cybersex II
2121	Don't Talk With Your Mouth Full
2122	Splash Hat
2123	Revenge Of The Groaner Kebab
2124	World Of Porn I
2125	Noisy Tales. The Stories Of A Blow Boy
2126	Hip And Hop In Rapper Paradise
2127	Bikini Party
2128	Horny Xmas
2129	New York Beauty
2130	With A Tail Of Long Ago
2131	Sweet Chicks Of Beverly Hills
2132	With The Horny Guys
2133	Horny Sister
2134	Rosie Never-Satisfied In Spain
2135	Adam And Eve Did It
2136	Plumber In High Boots
2137	The Sexy Wives Of Windsor
2138	Where There's One Trick There's Another
2139	Billy Goat And Sexy Sue
2140	The Tits Are Here!
2141	Horrorfuckers On Stage
2142	The Man With The Wolf's Tail
2143	You Only Come Twice
2144	World Of Porn II
2145	Bumfuckers
2146	Star Sex II
2147	A Porn Star In Comic Land
2148	Fucksport. There Is Only One!
2149	Please Pant
2150	Superdildo On Cunt Hunt
2151	Mad Dog Rubber
2152	Cybersex I
2153	Video Charts:
2200	Grant movie licenses
2201	Sell movie rights
2202	Unfortunately you have no movies to sell!
2203	The following movies:
2204	Grant movie licenses
2205	Sell movie rights
2206	The distributor offers you a fixed price of $%lu. Sell?
2207	The distributor offers you $%lu per unit, to be paid weekly. Accept the offer?
2208	The distributor isn't interested right now!
2209	You'd better not interrupt the distributor yet!
2210	What is this, you wanna hold my prick? Get outa here!
2211	The money will be transferred by tomorrow!
3101	Bar
3102	Police
3103	Video store
3104	Sex store
3105	Parking lot
3106	Pawnstore
3107	Motel
3108	Distributor
3109	Chicken farm
3110	The city authorities pay you a reward of $500 for killing vermin
3111	If you don't pay the rent, we'll be on the street!
3112	You've been kicked out of the motel room and the owner's sold your things!
3113	The FBI are in town and after you!
3114	You have won and reached Stage 2!
3115	The FBI have got you and you're going away for a long time... try again?
3116	Do something, or else the FBI will get you in a few hours!
3117	The motel owner has told the cops because you haven't paid the rental.
3118	You must pay the rental, or else the motel owner's going to the cops.
3119	After the third arrest, you've been handed over to the FBI... \ntry again?
3120	The motel owner has filed a complaint with the cops because of the brawl.
3201	TV
3202	To the motel
3203	Suitcase
3204	Sex toy
3205	Camera
3206	Video camera
3207	Tripod
3208	Finished sessions
3209	Lula
3210	My equipment
3214	Hmmm, I'd like a partner - want to get me one?
3215	I told you: I want a partner!
3216	Don't you want to photograph me like this?
3217	Sorry, I've run out of time - I have to go and warm up the locals in the bar.
3218	Don't you think I've earned a rest?
3219	There's no sense in a session without me, yeah?
3220	Your photographic equipment is missing something, honey!
3221	Title of session:
3222	Sex toy
3226	Hey, big man, how about some sex toys?
3227	Don't be so greedy! I need something good to play.
3228	I have to go to work, bye
3229	You're missing the lighting
3230	For a session, you need lighting and a camera with film or video camera with cassette
3401	To the town
3402	The pawnstore
3403	The owner
4301	To the town
4302	Ma
4303	Look for a girl for a trick
4304	For $%d, %s will do just about anything for you
4305	The chicken farm
4306	The girl is yours for a day. She's waiting in your room
4307	You haven't got a room for the girl to wait in!
4308	Lula doesn't want a partner right now
4401	To the town
4402	Shopping list:
4403	        Buy
4404	        Back
4405	Films in development:
4406	Completed films:
4407	        Back
4408	To development
4409	Finished photos
4410	Salesman
4411	Video cameras:
4412	Cameras:
4413	Spotlights:
4414	Miniature films:
4415	Video cassettes:
4416	The video store
4417	Accessories
4501	Bar
4502	Drunk man
4503	Teeth
4504	Wristwatch
4505	Money
4506	Lighter
4507	Gold tooth
4508	Watch
4509	Envelope
4510	You really gonna beat him up?
4511	Don't you think you're going too far, you old sadist!
4512	Oh shit - the guy you were blackmailing has told the cops!
4601	To the town
4602	Reception
4603	Motel room
4604	Do you want to hand over the $1000?
4605	Pay the rental first!
4606	You haven't paid the rental, so the motel owner has locked the room!
4701	Exit
4702	The rent is paid for %d days
4703	It's still too early for the next rent payment
4704	Motel owner
4801	To the town
4802	Rocker
4803	%d six-packs of beer
4804	The motel owner should be out of action for about %d hours..
4805	The cops should leave you in peace for about %d hours..
4806	The FBI agent shouldn't come back to town for about %d hours..
4807	The FBI aren't in town yet
4808	You want the motel owner to croak!? ...
4809	It's cool. The cops have other problems
4901	To the town
4902	You're free!
4903	SM
4904	Don't you think you'll be back here soon enough?
4905	End arrest?
5001	To the town
5002	Toilet
5003	Lady bar owner
5004	Dealer Don
5005	Girl
5006	Gambler
5007	Victim
5008	You're in luck!  I'm going to hire your little friend as a
5009	STRIPPER.
5010	I'll pay her $100 and $150 for 3 hours in the evening.
5011	When should Lula start work?
5012	I've had them enlarged for you!
5013	That's how I look? Get lost, fella!
5014	Depends what I get for it...
5015	Yeah, I can try it
5016	The bar
5017	Huh - you really want me to fire Lula?
5018	Lula already works here. %d:00 to %d:00
5019	Lula
5020	You really want me to give up work?
5021	I'd rather do it myself!
5022	No thanks, if your friend doesn't want to, better not!
5023	The blackmail letter has been sent
5024	No-one has won
5025	You've won\n\nYour %d sixes with %d dice beat %d sixes with %d dice
5026	You've lost\n\nYour %d sixes with %d dice are beaten by %d sixes with %d dice
5027	You get double your stake: $%d
5028	Your opponent gets double his stake: $%d
5029	You get: $%d
5030	Your opponent gets: $%d
5031	New game\n\nYour opponent starts
5032	New game\n\nYou can start\nand set the stake
5033	married
5034	unmarried
5035	I can seduce this guy later - I have to start work now!
5036	You have to stop the game - your opponent doesn't want to go on playing
5037	Choose the number of dice ...
5038	Phooey, the FBI can't touch you now. Another 50000 bucks and this dump is history!
5039	You already have a new identity!
5040	We're not playing for peanuts here. You must put in $%ld!
5041	You ain't got the dough to go on playing!
5101	To the town
5102	Sex toys:
5103	Shopping list:
5104	        Buy
5105	        Back
5106	Sex toys
5107	Salesman
5108	If anything's going right now, it's home videos.
5109	The sex shop
5201	To the town
5202	Photos
5203	Video
5204	Agent
5205	Lula
5206	Royalty money
5207	OK! I'm offering you %d%% royalties.\nPaid every day
5208	I'm giving you $%d - although you would have got more in royalties
5209	You ain't got photos!
5210	You ain't got videos!
5211	Piss off, you clown, I've got a riding lesson!
5212	Royalty money: $%d
5213	Show me what you've got or get out!
5300	Lamp 
5301	Halogen lamp
5302	Spotlight
5303	Lighting system
5304	Camera
5305	Reflex camera
5306	Video camera
5307	Professional movie camera
5308	5000 copies/day copying machine
5309	10000 copies/day copying machine
5310	15000 copies/day copying machine
5311	30000 copies/day copying machine
5312	PC editing software
5313	Video editing system
5314	Professional editing computer
5315	5000 copies/day packing machine
5316	10000 copies/day packing machine
5317	15000 copies/day packing machine
5318	30000 copies/day packing machine
5319	Pistol
5320	Shotgun
5321	Machine-gun
5322	Bazooka
5323	Couch
5324	Pool
5325	Fitness machine
5326	Drinks bar
5327	Knight set
5328	City set
5329	Hospital set
5330	Living-room
5331	Movie studio
5332	Big city
5333	Bedroom
5334	12-track mixer
5335	24-track mixer
5336	32-track mixer
5337	64-track mixer
5338	128-track mixer
5339	DAT recorder
5340	FX machine
5341	Keyboard
5342	Sampler
5343	CD player
5344	Speakers
5345	Equalizer
5346	Amplifier
5347	Microphone
5348	Pocket film
5349	35mm film
5350	Video cassette
5351	20mm professional film
5352	Lubricating cream
5353	Small dildo
5354	Medium-sized dildo
5355	Giant dildo
5356	Whip
5357	Vibrator
5358	Pleasure eggs
5359	Inflatable sex doll
5360	Traction bench
5361	Pleasure plug
5362	Vaseline
5363	Massage oil
5364	Penis frill
5365	Garden hose
5366	Nipple chain
5367	Rubber suit
5368	Knobbed condom
5369	Tickling attachment
5370	Rubber pantyhose
5371	Rubber corset
5372	Rubber gloves
5373	Rubber boots
5374	Half a chicken
5375	Teddy bear
5376	Atlanta
5377	Boston
5378	Chicago
5379	San Francisco
5380	Las Vegas
5381	Miami
5382	New Orleans
5383	New York
5384	Seattle
5385	Washington D.C.
5386	Los Angeles
5400	Infamous sayings:\n\n...DEEPER! ...DEEPER!... \nAnita G.
5401	Infamous sayings:\n\n..We should call our company 'Ram Software´...\nCarsten Korte
5402	And then there was:\n\n..The fireman's wife who shouted ´More hose, more hose, the fire's further in!´
5403	Infamous sayings:\n\n..Oh, no, don't do that to me...\nHelmut Theuerkauf
5404	Infamous sayings:\n\n..The intro has got really horny...\nCarsten Wieland
5405	Under the heading of ´Stupid comments´:\n\nBetter good on top than bad underneath!
5406	Infamous sayings:\n\n..It ought to work, I dunno...\nKarl Czisch
5407	Infamous sayings:\n\n..Whorehouse, porn - it's all the same...\nAnita G.
5408	And then there was:\n\n..The man who jumped out of the wardrobe into bed with his wife. He was never seen again...
5409	And then there was:\n\n..Two women who were tearing their husbands to pieces:\n>>My husband always comes too late!<< The other replies,>>With mine it's the other way round.<<
5410	Infamous sayings:\n\n..Women were only invented so that men had to invent new survival strategies.\nHelmut Theuerkauf
5411	Infamous sayings:\n\n..I'm OK, you probably aren't...\nAlex Diessner
5412	On doctors' advice, WET should not be consumed for more than 2-3 hours a day...
5413	We thank the editors of PC-Power, and the CDV employees who collaborated as actors!
5414	Special greetings to Megabyte Langenfeld for support with test computers!
8000	Foto1.cut
8001	Klo1.cut
8002	Sado1.cut
8003	Video1.cut
8004	wr_intro.cut
8005	wr_fhelp.cut
8006	wr_cthlp.cut
8007	wr_mohlp.cut
8008	wr_rzhlp.cut
8009	wr_bahlp.cut
8010	wr_aghlp.cut
8011	wr_knast.cut
8012	wr_hdhlp.cut
8013	wr_sxhlp.cut
8014	wr_pfhlp.cut
8015	wr_chhlp.cut
8016	erpres1.cut
8017	st2_init.cut
8018	st2_help.cut
8019	int01.cut
8020	wr_mzhlp.cut
8021	wr_vihlp.cut
8022	EXTRO.cut
8023	EMANZEN.cut
8024	FBI.cut
8025	HOTEL.cut
8026	INTRO.cut
8027	SABOTAG1.cut
8028	SABOTAG2.cut
8029	SCHMUTZ.cut
8030	ST3_01.cut
8031	SMNEWS1.cut
8032	SMNEWS2.cut
8033	SMNEWS3.cut
8034	SMNEWS4.cut
8035	SMNEWS5.cut
8036	SMNEWS6.cut
8037	SMNEWS7.cut
8038	SMNEWS8A.cut
8039	SMNEWS8B.cut
8040	SCANDAL.cut
8041	FOTO02.CUT
8042	WINNER.CUT
```

## Appendix B: string-ID consumers per function

Direct LoadStringA wrapper calls (0x4047e8), message boxes (0x4058c8) and opening-hours checks (0x40899b), grouped by the real owning function. `N+idx` means `base N + register index`; `dynamic` gives the instruction that produces the ID.

| Owner (prologue) | call site:ID (M = via 0x4058c8 message box, C = via 0x40899b opening-hours check; others call 0x4047e8 directly) |
|---|---|
| 0x404806 | 404886:dynamic (mov    eax,ecx) |
| 0x404a33 | 404a67:902, 404b30:907, 404c5a:907 |
| 0x404cdb | 404d73:dynamic (mov    eax,DWORD PTR [ecx+0x40]) |
| 0x4058c8 | 4058e6:dynamic (call   0x43371d) |
| 0x405948 | 405dfe:962+idx |
| 0x406261 | 406470:M 903, 4066b4:M 977, 4067ac:973, 4067eb:975, 406831:974, 406860:939+idx, 40689f:972, 406e6d:dynamic (lea    eax,[edx+ebx*1]), 406fa1:dynamic (lea    eax,[edx+ebp*1]) |
| 0x40899b | 4089f2:979, 408a05:dynamic (mov    eax,ecx) |
| 0x408a3a | 408a5b:978 |
| 0x408dea | 408f0d:M 3115, 408f2f:M 3119, 408fa8:M 3118, 408fd7:M 3112, 40908a:M 3113, 4090a7:M 3116 |
| 0x4090b6 | 4090d9:M dynamic (jne    0x4090f7), 4090e5:M 980 |
| 0x4092b1 | 409312:dynamic (mov    eax,ecx) |
| 0x409bf8 | 409c3a:5400+idx |
| 0x40a904 | 40a957:C -1 (none), 40a97a:C -1 (none), 40a99d:C -1 (none), 40a9c0:C -1 (none), 40a9e6:C -1 (none), 40aa0c:C -1 (none), 40aa32:C -1 (none), 40aa58:C -1 (none) |
| 0x40adcd | 40b106:M 3110 |
| 0x40b583 | 40b6e5:5376+idx |
| 0x40b952 | 40b972:5376+idx |
| 0x40bb08 | 40bb82:M 2102 |
| 0x40bd16 | 40be7c:2153, 40bfcc:2103+idx |
| 0x40c6de | 40c8d0:C 5109 |
| 0x40c8f1 | 40c922:dynamic (jl     0x40c927) |
| 0x40c94c | 40ca2a:5102, 40ca3c:5103, 40ca4e:5104, 40ca60:5105, 40cb0a:M 903 |
| 0x40cb7d | 40cbe4:dynamic (mov    eax,ds:0x45df08) |
| 0x40ce79 | 40cf2d:M 221 |
| 0x40dfd8 | 40e0bd:214, 40e121:M 215 |
| 0x40e19b | 40e31b:211, 40e34f:212 |
| 0x40e4a4 | 40e56f:217 |
| 0x40e748 | 40e8b4:M 3215, 40e8e1:M 3214, 40e969:M 3226, 40e99a:M 3218, 40e9b3:M 3220 |
| 0x40ef78 | 40f086:M 3228 |
| 0x40f30b | 40f393:3222, 40f3a6:3223, 40f3bc:3224, 40f3d2:3225 |
| 0x40f42e | 40f45f:dynamic (jl     0x40f464) |
| 0x40f489 | 40f52f:3210, 40f542:3211, 40f558:3212, 40f56e:3213 |
| 0x40f57f | 40f5b0:dynamic (jl     0x40f5b5) |
| 0x40fb4d | 40fc2b:3221 |
| 0x40feac | 40ff6c:M 901 |
| 0x4100a7 | 41014f:939+idx, 410162:220 |
| 0x4101db | 410309:M 254 |
| 0x41033c | 4103d6:M 255, 410481:253 |
| 0x4107c8 | 4108be:M 273 |
| 0x4108f4 | 41094a:256+idx, 410999:262+idx |
| 0x410c04 | 410cab:M 272 |
| 0x4111e1 | 411321:2210 |
| 0x4113b9 | 41145c:M 2202 |
| 0x4116a3 | 411832:2204+idx, 411866:2203 |
| 0x4118d8 | 41199d:2206, 4119cc:M 2211, 4119f3:M 2208, 411a09:2207 |
| 0x412267 | 4122a5:M 2209 |
| 0x412558 | 412630:M 5209, 4126ca:5212 |
| 0x412f0c | 412fcd:5211, 413040:5207, 413056:5208 |
| 0x413095 | 413101:5213 |
| 0x413381 | 413473:403, 41351b:405, 413570:406, 413694:412 |
| 0x413e1a | 413e94:404 |
| 0x4141cc | 4142fa:C 3402 |
| 0x414318 | 414452:M 903 |
| 0x416a10 | 416a54:M 514, 416b18:M 903, 416bfb:M 526, 416cc4:510, 416d7a:513, 416e24:M 526, 416e5c:M 903 |
| 0x417362 | 4173ba:M 508, 41741d:M 903, 417479:M 509, 41761e:505+idx |
| 0x4176c5 | 417748:527, 4177cf:M 903, 417857:517+idx, 417878:521+idx |
| 0x41790c | 41795d:M 524, 4179c7:M 525, 4179f2:M 525 |
| 0x4185fd | 41872b:M 903, 4187d6:M 903, 418860:dynamic (mov    eax,DWORD PTR [ebx*4+0x45d4dc]) |
| 0x418aa0 | 418b51:M 318, 418bae:dynamic (mov    eax,DWORD PTR [eax*4+0x45d4dc]) |
| 0x418c5c | 418d17:321, 418db2:M 322, 418e14:dynamic (mov    eax,ecx), 418e4b:320 |
| 0x41925c | 41938e:M 353, 4193b2:354, 4193f5:M 903 |
| 0x419822 | 4198a6:357, 4198df:356, 419938:M 903 |
| 0x419af4 | 419b6d:M 358, 419b8b:M 359 |
| 0x419bfc | 419c4e:360+idx |
| 0x41a1dd | 41a349:M 756 |
| 0x41a95e | 41a9fa:M 791 |
| 0x41aa3c | 41aac1:M 756, 41aacd:M 903 |
| 0x41ab24 | 41aba6:M 937, 41abdb:M 936 |
| 0x41ac0c | 41ac5f:726+idx, 41ac82:706+idx, 41aca5:716+idx |
| 0x41ad9c | 41adef:746+idx, 41ae12:736+idx, 41ae35:716+idx |
| 0x41af11 | 41af95:M 756 |
| 0x41b110 | 41b162:757+idx, 41b185:777+idx, 41b1a8:716+idx |
| 0x41b2b4 | 41b7b9:962, 41b7de:966 |
| 0x41b8ac | 41b99f:M 903, 41b9ff:789 |
| 0x41ba46 | 41bac7:M 938 |
| 0x41bc4c | 41c085:962 |
| 0x41d0d4 | 41d15b:M 937, 41d19a:M 936 |
| 0x41d1c8 | 41d6b5:962+idx, 41d6cf:962+idx |
| 0x41d724 | 41d822:912+idx |
| 0x41da0d | 41dc0f:790 |
| 0x41df38 | 41e051:M 909 |
| 0x41e45a | 41e540:602, 41e7d9:604+idx |
| 0x41e912 | 41ea03:647 |
| 0x41eaa7 | 41ebfc:648, 41eca8:910 |
| 0x41ecf6 | 41ee20:604+idx, 41ee63:649 |
| 0x41efd2 | 41f01e:M 654, 41f059:M 651, 41f0f8:M 652 |
| 0x41f574 | 41f5d1:M 938 |
| 0x41fa1a | 41fb9c:800, 41fc69:801, 41fd25:802 |
| 0x41fde8 | 41febb:M 951 |
| 0x42036d | 420577:910 |
| 0x4205b7 | 420648:M 814, 42067d:M 903, 4208a4:dynamic (add    eax,esi), 4208e7:815 |
| 0x4208f2 | 4209d3:850+idx |
| 0x420f43 | 4210a9:517+idx, 4210c9:1805+idx, 4210ea:521+idx |
| 0x421101 | 4212a2:521+idx |
| 0x421693 | 421788:858, 4217c6:M 903, 421800:856 |
| 0x421caf | 421f99:M 857 |
| 0x42244e | 4224a1:M 861, 422530:860, 422546:517+idx, 42255d:5376+idx, 422576:521+idx |
| 0x422de5 | 422faa:M 862 |
| 0x423448 | 4234a0:M 938 |
| 0x423e61 | 423fb5:2013 |
| 0x423fc1 | 424059:2014, 42406b:M 2009 |
| 0x42408f | 4240fa:M 2073, 4241ce:2014 |
| 0x4242f2 | 424536:2015+idx, 424656:2037, 424671:2038+idx |
| 0x4248dc | 424aa0:2007, 424ade:911 |
| 0x424b50 | 424c08:2011, 424cd7:2010 |
| 0x4251ec | 42524e:M 2062, 425286:M 2064, 4252a1:2063, 4252d0:M 2065, 425307:M 2066, 425331:M 2068, 42536a:M 903, 425383:2069, 42542b:2070, 42546c:2071 |
| 0x425509 | 42554f:M 2064, 42557c:M 2064 |
| 0x4255c0 | 42560d:M 2064, 425658:M 2064 |
| 0x425a1e | 425ac8:2078 |
| 0x425b4d | 425c03:M 2078, 425c5f:M 2079 |
| 0x425f68 | 425fe3:5018, 4260ac:M 5023, 4260d3:M 5020 |
| 0x4262c2 | 4263f6:C 5016 |
| 0x426431 | 426627:5010, 42665e:5011 |
| 0x4266d2 | 42674e:M 903 |
| 0x4267c8 | 42681c:5040, 42684b:M 5032 |
| 0x426908 | 426e3b:M 5024, 426e6b:5026, 426edd:5028, 426f1d:5030, 426fb7:M 5031, 42704a:M 5032, 4274d8:5037, 427538:M 5041, 42754a:M 5036 |
| 0x427573 | 4277a6:dynamic (mov    eax,ds:0x4847f0) |
| 0x427891 | 4279c8:M 4307, 4279d8:M 4306, 4279e8:M 903, 427c26:dynamic (mov    eax,ds:0x4847f0), 427ccc:dynamic (mov    eax,ds:0x4847f0) |
| 0x427cd6 | 427dc2:M 5023, 427e1b:M 5035, 42813e:5034 |
| 0x4285f4 | 4287d4:M 938 |
| 0x428c1d | 428cbf:1006, 428d5f:1005 |
| 0x42906c | 42922b:M 938 |
| 0x42950e | 4295cf:M 1151 |
| 0x42977e | 429857:1152 |
| 0x429d58 | 429e84:M 938 |
| 0x42a720 | 42a9de:1204+idx |
| 0x42b054 | 42b103:M 903, 42b1ef:5376+idx |
| 0x42b35f | 42b3cf:1210+idx |
| 0x42ba3f | 42ba97:M 4308 |
| 0x42bc5b | 42bd47:M 4307, 42bd57:M 4306, 42bd6a:M 903, 42bfc8:4304 |
| 0x42c4ec | 42c575:M 938 |
| 0x42cc83 | 42cdf9:C 4416 |
| 0x42ce36 | 42ce82:dynamic (jl     0x42ce87) |
| 0x42cee6 | 42d01b:M 903, 42d15e:4405, 42d173:4406, 42d185:4407, 42d197:4408, 42d37a:dynamic (mov    eax,DWORD PTR [ebp+0x72]), 42d38f:4402, 42d3a1:4403, 42d3b3:4404 |
| 0x42d45b | 42d505:M 903 |
| 0x42d940 | 42da21:M 938 |
| 0x42de52 | 42df28:1507, 42e011:1506 |
| 0x42e424 | 42e46f:M 4510, 42e5a9:dynamic (mov    eax,DWORD PTR [eax+0x451084]) |
| 0x42e821 | 42e8db:C 5016 |
| 0x42ec40 | 42ed3e:M 938 |
| 0x42f03d | 42f0bd:1605, 42f113:M 903 |
| 0x42f183 | 42f206:1606, 42f24d:M 903 |
| 0x42f536 | 42f5b4:M 4606 |
| 0x42f883 | 42f8d2:M 903, 42f95d:4604 |
| 0x42f989 | 42f9f0:4605 |
| 0x42fab3 | 42fb47:M 1651 |
| 0x42fbeb | 42fd6f:1653 |
| 0x43015e | 430204:1607+idx |
| 0x4304b2 | 430509:M 1702 |
| 0x43070e | 430803:1704 |
| 0x430a52 | 430b2d:4702, 430b6c:4703 |
| 0x430f9d | 431094:M 903 |
| 0x4312fb | 431324:M 1803 |
| 0x4313bc | 431401:M 1802 |
| 0x4314ef | 431650:M 903 |
| 0x431691 | 4316eb:1804, 431703:1805+idx |
| 0x43197e | 4319e0:M 903, 431a2a:4804 |
| 0x431dbd | 431ef6:1854, 431f6a:M 1853, 432180:1850, 43227c:1855 |
| 0x43239f | 432466:M 1852, 432600:517+idx |
| 0x4329e0 | 432ac1:1902, 432b6b:M 903, 432b86:M 937, 432c22:1902 |
| 0x432d30 | 432d7b:M 1903+idx, 4330fc:962, 433121:966 |
| 0x43323a | 433301:M 4904 |
| 0x433374 | 4333e2:M 4902 |

## Appendix C: function entries missing from Ghidra

442 addresses with a `push imm32; call 0x43371d` prologue that are not in `analysis/decompiled/functions.tsv`. `*` = referenced by a relocated pointer (callback, hook or handler).

```
401a19  402288* 40399d* 40405e  404f07* 404f93* 40538f* 405948* 406261* 407b35 
407b4c  407cd8* 4085da  408637  409e3f  40a15c* 40a174* 40a3bc* 40a51c* 40a5a2*
40a6d6* 40a904* 40ab74* 40adcd* 40b390* 40b416* 40b8d0* 40b987* 40bb08* 40bbce*
40bc1c* 40bd16* 40c58c* 40c619* 40c6de* 40c8f1* 40c94c* 40cb7d* 40cd19* 40ce79*
40cfbe* 40d123* 40d611* 40d9e3* 40deab* 40dfd8* 40e19b* 40e748* 40ebec* 40ef78*
40f42e* 40f57f* 40f5cb* 40f9f4* 40fb4d* 40fca0  40feac* 41033c* 410500* 4107c8*
4108f4* 410ae3* 410cd0* 411164* 4113b9* 4114c5* 41158e* 4116a3* 412558* 412757*
4128fb* 412b68* 412f0c* 413095* 413190* 4132e0* 413799* 4137a4* 4137d4* 413944*
413aec* 41409f* 414129* 4141cc* 414318* 414cc7  415029  415d08  415d53  415d64 
415d75  415e41  4166c5  41673b* 41681c* 416853* 416a10* 416ea0* 417362* 4176c5*
417cf6* 417eb0* 418124* 4185fd* 418989* 418aa0* 418c5c* 418f02  418f46  418f87 
41925c* 419455* 419822* 419af4* 419bfc* 41a04c* 41a1dd* 41a72f* 41aa3c* 41ab24*
41ac0c* 41ad9c* 41af11* 41b110* 41b2b4* 41b8ac* 41ba46* 41bc4c* 41c45c* 41cea2*
41d0d4* 41d1c8* 41d724* 41de2b* 41df38* 41e33a* 41e402* 41e45a* 41e800* 41e912*
41eaa7* 41ecf6* 41efd2* 41f574* 41f6ab* 41f9a4* 41fde8* 4201c2* 42036d* 4205b7*
420aa4* 420cab* 420f43* 421101* 4212ba* 421693* 421caf* 4220fb* 4222a7* 423066*
423448* 42388e* 423b28* 4248dc* 424b50* 424d38* 424ffb* 425075* 4251ec* 425509*
4255c0* 425f68* 42615a* 4262c2* 426431* 4266d2* 4267c8* 426908* 427573* 427891*
427cd6* 4281eb* 428379* 4285f4* 4289a2* 428a06* 428c1d* 428ea9* 42906c* 42935b*
4293d3* 42950e* 4295e9* 4296d9* 429b77* 429d58* 42a04f* 42a08f* 42a720* 42ab86 
42ac5e* 42ad85* 42b054* 42b35f* 42b651* 42b797* 42b8d0* 42b92d* 42ba3f* 42bab7*
42bb1d* 42bc5b* 42c212* 42c4ec* 42c6f9* 42c7b0* 42c9e0* 42cb0e* 42cc83* 42ce36*
42ceac* 42cee6* 42d45b* 42d694* 42d940* 42dbd9* 42dca3* 42de52* 42e0c4* 42e424*
42e661* 42e821* 42ea9e* 42ec40* 42ef01* 42eff9* 42f03d* 42f183* 42f536* 42f699*
42f74c* 42f883* 42f989* 42fab3* 42fb4f* 42fbeb* 4302e0* 4304b2* 4305af* 43060b*
430a52* 430bb9* 430cc4* 430f9d* 4311a7* 431266* 4313bc* 4314a4  4314af* 4314ef*
43186a* 4318bd* 43190d* 43197e* 431cb5* 431cfe* 431dbd* 432332  43239f* 4326c4*
432842* 432923* 432944* 4329e0* 432d30* 4331c4* 433374* 433402* 433491* 43353d*
4338d3  433b98  433ba9  433bfc  433e0a  4341a7  43426f  4343d5  4343f1* 434562*
4345b8* 434716* 43475c* 4348a0* 4349c0  434afe  434bc1* 434cfc  434d8e  434de7 
434dfa  434fc6  43501e  4354c1  435862  435a7b  435b58  435bd8  435db5  435f4b 
435fbf  43610b  43611b  436146  43618e  4361c7* 4362ed* 436304* 43631b  436336*
43634d  436361* 4363e1  4364aa  4365bc  436636  436b43  436fb0  43789a  437901 
437933  4379a6  437b71  437ce8  437da3  437db3  438003* 438675  438d56  438fa4 
438fb5  438fda  43918f* 4391c3  4392cc  43932c  439861  439c92  439ecd  43a276 
43a2f8  43a34a  43a6ed  43a893  43ab5f  43ac69  43acb0  43ad03  43ad1f  43ad33 
43ad88  43ae28  43ae4e  43ae70  43ae95  43b0a2  43b24d  43b3e7  43b476  43b5e6 
43b680  43c37d  43c803  43cc15  43cd6e  43cf22  43cf36  43d1c6  43d1dd  43d21c 
43d239  43d256  43d506  43d5ee  43d78a  43dc1c  43e02f  43e075  43e0b7  43e0fc 
43e1ce  43e266  43e34b  43e39c  43e3e6  43e402  43e4af  43e4f2  43e50b  43e639 
43e644  43ea65  43eca6  43f1ad  43f5bf  43f6c7  43fc6a  43fda7  43fe2f  4400b6 
44026c  44060f  4407a4  4409c6  440df8  440f44  44127b  441779  441cd0  442330 
44280d  443484  4434d2  44356a  4435c7  4436de  443747  4437ec  44386d  4438d6 
443948  4439b3* 443ad4  443b0c  443b51  443c52  443d43  443d8e  443e02  443e4b 
443eea  443f8f*
```

## Appendix D: all hotspot registrations (calls to 0x40604d)

`?` = register value not resolvable by a linear backward scan, `-1` = slot cleared or no tooltip.

| Registering fn | idx: (left,top,right,bottom) string-ID "text" |
|---|---|
| 0x40a5d0 | 0:(356,136,458,262) 102 "Realtor"; 1:(322,302,470,413) 103 "Sex store"; 2:(132,276,310,405) 104 "Bank"; 3:(0,415,640,480) 105 "Office"; 4:(523,104,585,272) 101 "Distributor"; 5:(17,95,139,285) 106 "Beauty clinic"; 6:(186,125,250,246) 107 "To the parking lot" |
| 0x40ac4e | 0:(245,128,363,238) 3101 "Bar"; 1:(366,155,475,250) 3102 "Police"; 2:(497,124,627,285) 3103 "Video store"; 3:(143,275,309,398) 3104 "Sex store"; 4:(450,355,577,464) 3105 "Parking lot"; 5:(2,234,133,364) 3106 "Pawnstore"; 6:(4,94,151,197) 3107 "Motel"; 7:(382,69,477,151) 3108 "Distributor"; 8:(276,58,378,126) 3109 "Chicken farm"; 0:(?,?,-1,-1) -1 |
| 0x40b583 | 0:(364,11,?,eax) 108 "To the bank"; 1:(365,96,?,eax) 109 "To the Black Cat agency"; 2:(365,178,?,eax) 110 "To the lady realtor"; 3:(366,259,?,edi) 111 "To the advertising agency"; 4:(0,0,-1,-1) -1; 6:(?,?,?,eax) 114 "To the branches"; 6:(0,0,-1,-1) -1; 5:(36,297,303,457) 113 "To the airport" |
| 0x40bc6d | 0:(0,344,640,480) 906 "To the town"; 1:(0,0,116,343) 2100 "Look at video charts"; 2:(443,73,640,346) 2101 "Buy sex toy"; 3:(297,122,407,187) -1 |
| 0x40c648 | 0:(0,343,215,479) 5101 "To the town"; 1:(?,?,-1,-1) -1; 2:(266,317,639,433) 5106 "Sex toys" |
| 0x40cffe | 0:(0,0,312,90) 201 "To the overview"; 1:(118,90,312,222) 201 "To the overview"; 2:(224,234,331,287) 204 "Laptop"; 3:(294,184,410,279) 206 "Secretary"; 4:(271,290,417,424) 206 "Secretary"; 5:(403,52,483,295) 210 "Broom cupboard"; 6:(494,37,640,190) 208 "To the other WET buildings" |
| 0x40ed6d | 0:(526,55,613,285) 3202 "To the motel"; 3:(422,176,509,216) 3203 "Suitcase"; 7:(?,?,-1,-1) -1; 5:(?,?,-1,-1) -1; 4:(?,?,-1,-1) -1; 6:(537,380,634,474) 3208 "Finished sessions"; 2:(114,172,264,401) 3209 "Lula"; 9:(221,73,281,277) -1; 8:(11,88,197,209) -1 |
| 0x410618 | 0:(0,0,200,300) 201 "To the overview"; 1:(233,0,413,269) 250 "To the property"; 2:(235,297,316,351) 204 "Laptop"; 3:(435,221,506,291) 202 "Flipchart"; 4:(484,317,593,412) 207 "Safe"; 5:(520,209,612,306) 251 "Planning parties"; 6:(313,251,375,467) 206 "Secretary"; 7:(266,360,312,467) 206 "Secretary"; 6:(313,251,375,467) 252 "Lula's present"; 7:(266,360,312,467) 252 "Lula's present"; 8:(243,224,400,480) 252 "Lula's present" |
| 0x4115d4 | 0:(15,0,169,91) 906 "To the town"; 1:(19,91,60,239) 906 "To the town"; 2:(379,228,501,289) 2200 "Grant movie licenses"; 3:(97,308,208,362) 2201 "Sell movie rights"; 4:(485,100,547,196) -1 |
| 0x41282f | 0:(0,398,292,479) 5201 "To the town"; 1:(210,209,347,267) 5202 "Photos"; 2:(49,88,171,208) 5203 "Video"; 3:(295,52,419,198) 5204 "Agent" |
| 0x4128fb | 4:(?,?,-1,-1) -1 |
| 0x41370a | 0:(105,110,313,302) 401 "Do business with the bank"; 1:(339,14,640,201) 402 "Back to the town"; 2:(7,237,95,303) -1 |
| 0x414171 | 0:(0,327,280,479) 3401 "To the town"; 1:(326,74,521,310) 3403 "The owner" |
| 0x416877 | 0:(490,0,639,176) 501 "Back to the town"; 1:(0,330,220,407) 502 "Look at real estate" |
| 0x4170a4 | 0:(422,250,606,480) 528 "Back to the realtor's office"; 1:(15,33,320,197) 503 "Look at property"; 2:(338,24,625,218) 503 "Look at property"; 3:(6,264,299,463) 503 "Look at property"; 0:(0,0,640,176) 501 "Back to the town"; 1:(365,180,640,480) 501 "Back to the town"; 2:(24,185,232,374) 515 "Rent sex store"; 3:(256,221,350,390) 516 "Look for mansion"; 0:(432,127,640,480) 528 "Back to the realtor's office"; 1:(109,21,209,96) -1; 2:(212,21,312,96) -1; 3:(315,21,415,96) -1; 4:(418,21,518,96) -1; 0:(0,0,640,55) 905 "To the office"; 1:(120,90,520,390) 504 "Look at what's offered" |
| 0x418489 | ?:(?,?,?,ebx) ebx; ?:(?,10,?,eax) ?; 21:(0,6,29,83) -1; 22:(630,6,640,83) -1 |
| 0x4196e3 | 0:(35,42,328,193) 905 "To the office"; 1:(392,401,?,ebp) 350 "Hire staff"; 2:(?,1,0,eax) -1; 3:(?,1,0,0) -1 |
| 0x41a59c | 0:(421,81,518,244) 904 "To the other offices"; 1:(388,318,408,348) -1; 2:(233,271,308,296) 700 "Place a job ad for actors"; 3:(7,157,150,214) 702 "Look at actors file"; 4:(17,215,84,337) 702 "Look at actors file"; 5:(168,85,381,204) 704 "Cast actors"; 6:(89,222,183,282) 705 "Produce cost figures"; 7:(501,248,602,277) 701 "Place a job ad for staff"; 8:(480,288,549,359) 703 "Look at staff file"; 9:(325,379,490,476) 787 "Look at applications" |
| 0x41e221 | 0:(182,0,639,84) 904 "To the other offices"; 1:(295,79,364,205) 904 "To the other offices"; 2:(0,96,158,291) 600 "Work on storyboard"; 3:(192,330,333,403) -1; 4:(389,193,448,227) -1; 5:(311,298,500,408) 601 "Write screenplay"; 6:(329,206,456,276) 603 "Define the subject of the movie" |
| 0x41ffa4 | 0:(0,0,161,269) 905 "To the office"; 2:(483,387,?,eax) 804 "Cast Lula for movie"; 3:(565,385,?,eax) 805 "Buy a present for Lula"; 4:(425,293,484,345) -1 |
| 0x420e8d | 0:(544,167,640,480) 906 "To the town"; 1:(0,416,137,480) -1; 2:(138,416,273,480) -1; 3:(274,416,409,480) -1 |
| 0x4236e3 | 0:(0,0,640,80) 904 "To the other offices"; 1:(0,103,109,480) 2000 "Hire lighting engineer"; 3:(105,74,234,202) 2005 "Choose lighting"; 2:(546,79,640,330) 2002 "Assign cameraman"; 4:(395,120,545,209) 2006 "Choose camera"; 5:(213,182,432,392) 2001 "Assign director"; 7:(532,406,?,eax) -1; 6:(?,?,?,eax) -1 |
| 0x4261ba | 0:(0,425,241,479) 5001 "To the town"; 1:(90,57,138,157) 5002 "Toilet"; ?:(?,?,?,eax) ecx; 11:(?,?,-1,-1) -1; 2:(165,62,305,138) -1 |
| 0x428884 | 4:(0,285,330,480) 904 "To the other offices"; 0:(364,0,464,52) 1001 "Cutting monitor"; 1:(498,0,610,76) 1001 "Cutting monitor"; 2:(348,114,450,203) 1001 "Cutting monitor"; 3:(481,137,594,236) 1001 "Cutting monitor"; 5:(0,19,154,243) 1002 "Assign cutter"; 6:(466,289,640,455) 1003 "Buy equipment"; 7:(211,75,360,296) 1004 "Cut films" |
| 0x4292a3 | 0:(0,0,640,66) 904 "To the other offices"; 1:(308,157,390,215) -1; 2:(479,226,559,306) 1101 "Copying monitor"; 3:(256,70,474,354) 1102 "Hire copying girl"; 4:(0,194,210,389) 1103 "Buy copying machines" |
| 0x429693 | 0:(0,0,640,176) 1150 "To the map" |
| 0x429f5d | 0:(243,0,640,172) 904 "To the other offices"; 1:(420,183,509,266) 1201 "Warehouse monitor"; 2:(109,57,277,293) 1202 "Assign warehousewoman"; 3:(341,263,478,395) 1203 "Process orders"; 4:(334,197,409,255) -1; 5:(503,274,566,308) -1 |
| 0x42af61 | ?:(?,?,?,edi) -1; ?:(?,?,?,ecx) -1; ?:(?,1,0,0) -1; 12:(0,205,50,255) 906 "To the town"; 13:(136,0,640,96) 906 "To the town" |
| 0x42b84a | 0:(0,0,640,80) 904 "To the other offices"; 1:(421,221,640,433) 1301 "Buy equipment"; 2:(4,235,160,447) -1 |
| 0x42bac2 | 0:(0,334,129,479) 4301 "To the town"; 1:(513,235,633,394) 4302 "Ma" |
| 0x42c5f8 | 0:(0,212,254,480) 904 "To the other offices"; 1:(370,231,423,287) -1; 2:(18,75,70,192) 1401 "Hire assistants"; 6:(70,131,215,209) 1402 "Buy props"; 3:(256,87,371,413) 1403 "Hire set builder"; 4:(413,53,640,407) 1404 "Buy sets"; 5:(86,0,179,114) 1405 "Hire props manageress" |
| 0x42cbb2 | 0:(0,395,283,479) 4401 "To the town"; 3:(52,239,463,359) 4417 "Accessories"; 1:(?,?,-1,-1) -1; 2:(?,?,-1,-1) -1 |
| 0x42daf2 | 0:(0,256,172,480) 904 "To the other offices"; 2:(34,178,104,244) 1501 "Dubbing monitor"; 1:(151,39,307,163) 1502 "Assign (male or female) dubber"; 3:(393,144,550,415) 1503 "Assign musician/sound engineer"; 4:(216,343,387,480) 1504 "Buy equipment"; 5:(195,214,371,340) 1505 "Add sound track to film" |
| 0x42e6f5 | 0:(395,404,639,479) 4501 "Bar"; 1:(131,67,262,366) 4502 "Drunk man"; 2:(?,?,-1,-1) -1; 3:(?,?,-1,-1) -1 |
| 0x42eddd | 0:(0,308,640,480) 904 "To the other offices"; 1:(68,201,80,231) -1; 2:(444,150,505,190) -1; 3:(191,81,277,128) -1; 4:(33,49,139,251) 1601 "Assign advertising boss"; 5:(429,67,524,231) 1602 "Assign switchboard operator"; 6:(383,243,640,346) 1603 "Start advertising"; 7:(158,206,297,278) 1604 "Start scandals" |
| 0x42f6b6 | 0:(0,154,70,479) 4601 "To the town"; 2:(296,256,452,367) 4602 "Reception"; 1:(?,?,-1,-1) -1 |
| 0x42fb82 | 0:(0,0,277,480) 906 "To the town"; 1:(365,35,483,476) 1650 "Sign contract for advertising" |
| 0x430540 | 0:(145,64,407,398) 904 "To the other offices"; 1:(435,202,640,433) 1701 "Fit out doorman" |
| 0x430c61 | 0:(0,95,78,326) 4701 "Exit" |
| 0x43143e | 0:(0,0,640,80) 906 "To the town"; 1:(320,91,630,480) 1801 "Sabotage hostile companies" |
| 0x4318c8 | 0:(0,0,90,243) 4801 "To the town" |
| 0x431d31 | 0:(0,0,285,311) 906 "To the town"; 1:(322,12,455,209) 1801 "Sabotage hostile companies"; 2:(221,231,485,445) 1801 "Sabotage hostile companies" |
| 0x4328bd | 0:(0,0,640,80) 906 "To the town"; 1:(20,48,228,305) 1901 "Give actors beauty treatment" |
| 0x43340d | 1:(?,?,-1,-1) -1; 0:(?,?,-1,-1) -1 |
