# Gameplay exploration of the recompiled game

Written 2026-10-06. Five agents drove `build/game/lula` headless with scripted input (`LULA_INPUT`) through the office, the bank and realtors, the agencies, the stage-1 districts and the day/month/year simulation, in about 400 runs. They used patched save games to reach stage 2 and 3. The scenario scripts they wrote are in `tests/scenarios/`.

## Coverage

Per-function counters (`LULA_COVERAGE`, merged with `tools/coverage_report.py`), live functions only (dead entries from `docs/recomp/specs/code-discovery.md` appendix B are left out):

| Module | Before | After |
|---|---|---|
| rooms, dialogs, simulation | 128 / 617 (20.7 %) | 472 / 617 (76.5 %) |
| shared game services | | 77 / 90 |
| sound system | | 43 / 44 |
| video (on hold) | | 10 / 19 |
| Watcom C runtime 2 | | 116 / 220 |
| all live functions | 482 / 1261 (38.2 %) | 955 / 1261 (75.7 %) |

"Before" is the main menu, a new game and the end-to-end tests. "After" merges the 314 coverage files of all exploration runs. The scenario files alone reach less, because many exploration runs were one-offs.

## What was reached

- **Main menu and options**: New game, Load game, End game with the exit credits, the options sheet (music, sound, video sizes, volume, balance, game speed), the "Set Digital Output" popup menu, save and load lists, the save name dialog, the F7 version overlay, F8 screenshots, F1 help pages. Settings persist in WET.1ST.
- **Stage 1**: town, motel and reception (rent), the motel room as office (camera, tripod, sessions list, equipment list), video store accessories, pawnshop (10:00 to 20:00 only), distributor, sex stores, bar (gambler with the WET 6 dice game, Dealer Don, stripper, girl, toilet), chicken farm, parking lot with the rockers, police and arrest, FBI car, and the switch to stage 2.
- **Stage 2**: office with laptop and secretary, town overview, bank with the deposit and withdrawal dialogs, realtor and building plot (rent, buy, sell, cancel lease), distributor (selling movie rights), casting office (staff costs list, job ads, staff file), Lula's flat, the Black Cat agency and Hell Devils jobs, the marketing office, day end with the daily costs sheet, month and year change, bankruptcy questions, and the switch to stage 3.
- **Stage 3**: lady realtor (mansions, renting sex stores), city map, office (flipchart, safe, party planning), property (hiring staff, expanding the site), the party slideshow, branches (staff, departments, balance sheet with capital in and out), Black Cat sabotage, advertising agency, airport with flights and random flight events, the launch site and the end slideshow ("CONGRATULATIONS").

No run logged a trap, a fatal error, an unimplemented import or an unimplemented COM method. The only warnings came from the video path below.

## Issues found

| Issue | Status |
|---|---|
| After a modal dialog closed, the game kept the mouse position from before the dialog, so the next click without moving hit the old hotspot (re-opened the deposit dialog, or entered a building after Load). The game hit-tests clicks at the last WM_MOUSEMOVE and has no GetCursorPos; Windows posts a mouse move when the window under the cursor changes. Confirmed against the original under Wine. | Fixed in `7116e60`: the dialog manager posts a WM_MOUSEMOVE when a dialog, message box or popup menu closes. Regression scenarios `bank_realtor_stale_mouse*.txt`. |
| With "Play videos" switched on in the options, F1 starts a help video. CoCreateInstance for the ActiveMovie filter graph is refused, the game shows "End Program ??" and its default button quits. | Open. Video is on hold until the user decides. Workaround: leave "Play videos" off (it starts off; the explorer had to switch it on). |
| Some DDF labels wrap or are clipped ("Lion beer sixpacks for", "Rental conditions:"), because the 14 px bold substitute font is wider than the label rectangles. | Partly settled. The runtime used fractional glyph positions and pair kerning; GDI uses whole-pixel advances and no kerning in TextOut/DrawText. After that change the text on all five main-menu buttons has the same left and right edges as the original under Wine, to the pixel. With those widths 37 of 219 single-line labels are wider than their rectangles, so the original under Wine wraps them the same way. Whether real Windows 95 picked a narrower font for the nonexistent "System Small" is still open; it needs a capture from real Windows 9x. |
| Test harness: `type` cut text at 31 characters and could not type spaces; script lines longer than 255 bytes were split into bogus events; the frame dumper skipped screens that were presented once and then held. | Fixed in `7116e60`. |

## Behaviour that looks odd but matches the original

Checked against the disassembly or the Wine run:

- About 6.5 s of black screen after End game: 200 WaitForVerticalBlank pairs at 0x402bcb.
- A mouse click that closes an F1 help page also reaches the hotspot under the cursor (0x404e47 does not clear the click). SPACE, RETURN or ESC close it cleanly.
- "Take out capital" at a store uses a signed compare, so 3000000000 is accepted and the account goes deeply negative. An original bug, reproduced faithfully.
- Empty title plaques on some sheets and the German "Leute" on the chambermaid sheet come from WET.DDF itself.
- A DDF sheet is hidden while a deposit dialog or an info box is open, the distributor is missing behind the movie list dialog, and a newly bought building cannot be clicked until the plot is re-entered. All the same under Wine.
- Random events (bank, distributor, Hell Devils job, flights) use rand() seeded from time().

## Not reached yet

- Selling finished movies (WORK_ORDER_DLG, the movie sale list 0x40e19b): needs finished movies and a fully equipped copier and warehouse, which takes many game days of production.
- The staffed paths of the marketing office, some branches helpers (0x422b6c, 0x422bf8, 0x422de5), the motel "sex toy" hotspot, the stage-1 police room function 0x43323a, and the later effects of a sabotage.
- 145 functions in "rooms, dialogs, simulation" have not run; `tools/coverage_report.py FILES --uncovered 'rooms, dialogs, simulation'` lists them.

## Notes on the process

The reports of the districts and simulation explorers were lost when the disk filled up near the end of the run; their scenarios and coverage files survived and are included above. Frame dumps (PPM, 900 KB each) were the cause; run scenarios without `LULA_FRAMEDUMP` unless a picture is needed.
