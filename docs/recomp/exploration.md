# Gameplay exploration of the recompiled game

Written 2026-10-06. Five agents drove `build/game/lula` headless with scripted input (`LULA_INPUT`) through the office, the bank and realtors, the agencies, the stage-1 districts and the day/month/year simulation, in about 400 runs. They used patched save games to reach stage 2 and 3. The scenario scripts they wrote are in `tests/scenarios/`. Round 2, later the same day, explored movie production and the rest of the game; it has its own section at the end.

## Coverage after round 1

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

These numbers were made with the tools of round 1. Round 2 found two faults in the coverage tools (see "Tooling fixes after round 2"). The report counted the live function 0x44c3c3 as dead, so there are 1262 live functions, not 1261. memcpy (0x442be4) ran all the time but had no counter. The current numbers are in the round-2 coverage table.

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

## Not reached in round 1

Round 2 reached all of these:

- Selling finished movies. WORK_ORDER_DLG runs in `warehouse_orders` and `movie_sale_warehouse`; the movie sale list 0x40e19b runs in `cutcopy_laptop_sale` and `soundprops_sale_done`. `movie_sale_rights` and `movie_sale_license` cover the two other ways to sell.
- The staffed paths of the marketing office: the `marketing_*` scenarios.
- The branches helper 0x422b6c and the later effects of a sabotage: `leftovers_sabotage_dayend` and `leftovers_sabotage_expired`. The helpers 0x422bf8 and 0x422de5 had in fact run in round 1, in the simulation run L5, whose report was lost. No scenario covers them yet.
- The motel "sex toy" hotspot: `leftovers_motel_toy`.
- The stage-1 police room function 0x43323a had also run in round 1, in the districts runs, whose report was lost too. The round-1 scenarios `districts_police_arrest`, `districts_chicken_farm` and `districts_town_tour` reach it.
- Of the 145 functions in "rooms, dialogs, simulation" that had not run, none is left. The last two (the slider drag) are covered by `office_slider_drag`.

## Notes on the process

The reports of the districts and simulation explorers were lost when the disk filled up near the end of the run; their scenarios and coverage files survived and are included above. Frame dumps (PPM, 900 KB each) were the cause; run scenarios without `LULA_FRAMEDUMP` unless a picture is needed.

## Round 2: movie production and the rest of the game

Written 2026-10-06, after round 1. Round 1 left most of the stage-2 movie business unexplored, because it needs rented buildings, hired staff and many game days. Round 2 worked like this:

- A base agent built shared save templates and one entry scenario per production room.
- Nine area agents explored casting, movie planning, the studios, cutting and copying, the warehouse and recreation room, sound and props, marketing with staff, the stage-1/3 leftovers, and the engine and library functions. One more agent took a single movie through the whole chain from planning to sale.
- For each area, and for the movie chain, an independent verifier reran every scenario twice and checked the claims.
- A critic merged all coverage, rechecked every claim of a covered function and classified every function that has still never run.

Each agent ran at most two game processes at a time (`-j 2`). Round 2 added 90 scenarios and 83 save recipes. With `office_slider_drag`, written after the round, `tests/scenarios/` holds 155 scenarios. Their recipes are in `tests/scenarios/saves.json` (40) and in the 11 area files `tests/scenarios/saves/*.json` (83).

### Shared save templates

- `prod_s2slow`: the stage-2 save with a slow clock (31 timer calls per game minute instead of 4), so that the random 13:00 sabotage box (0x43070e) cannot interrupt a template script.
- `prod_s2all`: the game itself rents all 13 production buildings at the realtor and saves on the WET plot. The account is then patched to 1500000, because 2000000 or more switches to stage 3 at the 19:00 day end (0x404b75).
- `prod_s2staff`: `prod_s2all` plus one employee per job, patched into the staff table that follows the guest block in the save.

The 13 `prod_enter_*` scenarios enter each production room once, and `prod_staff_departments` checks the staffed versions. These 14 runs reached 70 functions that no earlier run had reached. Most area recipes start from `prod_s2staff`.

### What was reached

- **Casting office (room 21).** Job ads for actors and actresses with their sliders (1200 $ each). The applicant draw at 11, 13, 15 and 17 from ads that are one to three days old. Hiring an applicant (with its animation) and rejecting one. The actors file: pay steps of 100 $, training (11 days at 300 $ a day) and firing. The staff file and the staff cost figures ("Total 22 7837$", equal to the sum computed from the save). Casting actors for a movie, and casting Lula from her flat (room 23), including "Sorry, I can only act in ONE movie at a time!". Two scenarios load saves that the game wrote after hiring and training, so those changes survive save and load. 9 scenarios, 6 recipes.
- **Movie planning (room 22).** The screenplay sheet: a new title (a three-letter title is refused), the storyline list and the category. The storyboard: up to 12 scenes at 1000 $ each (120 minutes), removing a scene, and Conclude, which charges the planning cost. The staff icon that appears with two screenplay writers and opens the staff chooser. Every refusal: no staff, no screenplay, staff missing at Conclude, already 9 movies, and no money. Firing a cast actor from the actors file. 8 scenarios, 7 recipes.
- **Studios (rooms 24, 33 and 34).** Assigning the lighting engineer, cameraman and director, and buying a light and a camera. The film strip appears only when crew, equipment and a planned movie are all there. Moving crew between studios. Starting a shoot: the film list, the director's recommended days and changing the shooting time. The end of shooting, both inside the studio and while the player is on the plot. The events during shooting: actor problems (whip, break, a 500 $ bonus, removing an actor), props wishes (buying a sex toy, or taking one from the store), and an ignored wish that makes the director quit. 10 scenarios, 9 recipes.
- **Cutting and copying (rooms 29 and 30).** Buying an editing machine, the four cutting monitors, and the "Cut films" sheet with its movie list and cutting days (80 minutes take 2 days, 20 minutes 1, 120 minutes 3). Cutting ends at 10:00 on the day the code computes. Selling a cut movie at the distributor: the offer of $73437 equals the value computed from the code. The office laptop's "Put movies on sale" with all four of its checks passing, and its refusals (no fully equipped copier, movie still being cut). The copier room: the rat animation, the monitor, the copying girl's card and buying a copying machine. 7 scenarios, 8 recipes.
- **Warehouse and recreation room (rooms 25 and 28).** A movie put on sale at the laptop gets a record in FILMB.TMP. At the next day end the warehouse list is built from it, and orders arrive during the next day. "Process orders" opens WORK_ORDER_DLG, where orders are accepted or removed. The revenue shows in the next Daily costs ("Direct sales revenue 4557 $" for 651 copies at 7 $), and the sold copies are written back to FILMB.TMP. The warehouse monitor stays on across room changes. In the recreation room, leisure equipment is bought and sold back at 90 %. 3 scenarios, 4 recipes.
- **Sound studio and props (rooms 27 and 32).** The sound studio with and without its staff (the dubber and sound engineer animations and cards), the dubbing monitor, buying a mixer and adding a sound track: ten tracks, Listen plays a track through DirectSound, and the job takes 2 days for 80 minutes. The list shows only cut movies without sound. A movie with finished sound gets a higher value (50000; the code gives 44000 without the track), and one still being dubbed is refused at the laptop. The props room: staff cards, the set builder animation, buying and selling stage sets, and a nine-line talk slideshow between the props staff. 10 scenarios, 9 recipes.
- **Marketing office with staff (room 31).** The four advertising campaigns (8500 $ to 160000 $): starting them, running ones hidden, refused without money, paid from the credit line. The ten scandals (10000 $ to 180000 $): each can be prepared once per game, and at 10:00 one breaks and its newspaper headline appears, in the office, on the plot, and on the next day for the next scandal. The cards of the advertising boss and the switchboard operator. 5 scenarios plus the reception one below, 3 recipes.
- **Reception (room 26).** Without a doorman, "Fit out doorman" answers "You haven't got a doorman yet". With one, the room shows the doorman animation (PFOERTNE.TBF), and "Fit out doorman" opens the guns sheet (450 $ to 4500 $); a bought pistol appears in the sell list. Scenarios `prod_enter_reception`, `marketing_reception` and `prod_staff_departments`. From the code only: a rented reception and each doorman lower the chance of the 13:00 feminist and sabotage boxes (0x43070e).
- **The full movie chain.** One movie, "Hot Sauna", goes through every step, and the game writes every state itself. The 21 recipes in `saves/movie.json` only move the date, clock or game speed between steps.
  1. Planned on day 1 (4000 $).
  2. Beauty clinic "Lift thighs" for both actors (only the actress is charged), then healed at a day end (the actor's level goes from 8 to 9).
  3. Actors and Lula cast.
  4. Shot in studio 1 in one day instead of the four recommended, with an actor problem paid with a 500 $ bonus.
  5. Cut (1 day), copier bought, sound track added (1 day). The account is now 1447229, exactly 1500000 minus the costs.
  6. Sold in each of the three ways. Movie rights to the distributor: $76562 declined, then $115780 accepted and paid as "Distributor royalties" at the day end. A licence at $3 per unit: weekly royalties of 37851 $ on Monday. Direct sales through the laptop and the warehouse: four orders and "Direct sales revenue 17883 $".

  Every amount the game showed matched a hand calculation from the code. The stage-2 beauty clinic (9 functions) ran here for the first time. 7 scenarios, including the clinic's refusal for cast actors.
- **Stage-1 and stage-3 leftovers.** Stage 1: the motel "Sex toy" hotspot and list; photos at the distributor (an undeveloped film cannot be sold); pawning and buying back at the pawnshop (75 $, 120 $, 60 $); film development at the video store (17 hours) and collecting the photos (20 $), which the distributor then makes an offer for. Stage 2: equipment wear at 9:00 (an item at 0 % breaks and is removed) and cancelling a studio lease (the movie goes back to "planned"). Stage 3: selling a mansion back to the lady realtor ($861300), building a pool on the property (with its "7 days remaining" sign), the day-end effect of a sabotage and its end, and the rocket message when the account reaches 50000000 $. 13 scenarios, 10 recipes.
- **Engine and library functions.** The stage-1 two-list sheet in the sex store (Buy, Back and the list arrows). The motel eviction: the player's things are sold, the owner calls the police, arrest. The photo chain from development to the sale at the distributor (600 $). The "CD not found" dialog with its Browse button ran in a manual run with a CDROM.LOC that names a missing directory, which a recipe cannot create. The engine agent also classified its 42 target functions and all never-run Watcom runtime functions; the critic finished that work (see "What is left"). 3 scenarios, 3 recipes.
- **Slider drag (after the round).** `office_slider_drag` drags the Volume slider on the options sheet with the new `down` and `up` script ops. While the button is held, the DDF modal loop calls the drag handler 0x4158f3, which moves the thumb through 0x415efc.

### Coverage

The critic's table, plus a column for the state after the fixes. Live functions only.

| Module | Round-2 explorers | + critic's manual runs | Now |
|---|---|---|---|
| startup, init, shutdown | 13 / 15 | 15 / 15 | 15 / 15 |
| constructors, WinMain | 2 / 2 | 2 / 2 | 2 / 2 |
| frame loop, hotspots, rooms | 34 / 36 | 34 / 36 | 34 / 36 |
| shared game services | 90 / 90 | 90 / 90 | 90 / 90 |
| rooms, dialogs, simulation | 615 / 617 | 615 / 617 | 617 / 617 |
| compiler support | 4 / 4 | 4 / 4 | 4 / 4 |
| window, DirectDraw | 14 / 17 | 14 / 17 | 14 / 17 |
| file/memory manager | 5 / 5 | 5 / 5 | 5 / 5 |
| TFF loader | 2 / 2 | 2 / 2 | 2 / 2 |
| mouse/keyboard | 17 / 17 | 17 / 17 | 17 / 17 |
| error object | 6 / 6 | 6 / 6 | 6 / 6 |
| sound system | 43 / 44 | 43 / 44 | 43 / 44 |
| NGS pool writer | 8 / 8 | 8 / 8 | 8 / 8 |
| video (on hold) | 10 / 19 | 10 / 19 | 10 / 19 |
| 2D graphics engine | 37 / 40 | 37 / 40 | 37 / 40 |
| image/sound file library | 21 / 24 | 21 / 24 | 21 / 24 |
| file I/O wrappers | 12 / 13 | 12 / 13 | 12 / 13 |
| Watcom C runtime | 42 / 50 | 42 / 50 | 43 / 50 |
| MIDI/CD audio (on hold) | 6 / 11 | 6 / 11 | 6 / 11 |
| blitters | 18 / 21 | 18 / 21 | 18 / 21 |
| Watcom C runtime 2 | 116 / 220 | 118 / 220 | 119 / 221 |
| all live functions | 1115 / 1261 (88.4 %) | 1119 / 1261 (88.7 %) | 1123 / 1262 (89.0 %) |
| without video and MIDI | 1099 / 1231 (89.3 %) | 1103 / 1231 (89.6 %) | 1107 / 1232 (89.9 %) |

- "Round-2 explorers" is the critic's "before": the merge of 468 coverage files in `build/explore` and `build/scenarios` after all area agents and the movie agent.
- "+ critic's manual runs" adds the critic's two manual runs in `build/explore/critic/`: the CD dialog and a start on a daylight-saving switch day.
- "Now" is the merge of the 470 coverage files on the evening of 2026-10-06, after the tooling fixes and with `office_slider_drag`. The slider drag adds 0x4158f3 and 0x415efc. memcpy (0x442be4) and 0x44c3c3 now have counters, and 0x44c3c3 now counts as a live function, so there are 1262.

Round 1 ended at 955 of 1261 (75.7 %), with 472 of 617 in "rooms, dialogs, simulation".

No live function in "rooms, dialogs, simulation" or "shared game services" is left unrun. After the round only the slider-drag pair 0x4158f3/0x415efc was left, because no script could hold a mouse button during a move. The new scenario `office_slider_drag` covers it with the new `down` and `up` script ops. Everything that has still never run is engine or library code; "What is left" classifies all of it.

### Original-game behaviour reproduced faithfully

Round 2 found many places where WET.EXE or WET.DDF itself does something odd or wrong. An explorer or a verifier found each row below, and a verifier checked it against the disassembly of the original. Where a scenario reaches it, the run shows the same result as the code. The recompiled game does exactly what the original does.

These are not runtime bugs. Do not fix them in the recompiled runtime: the goal is a faithful copy of the original, and several scenarios and recipes depend on this behaviour. The round-1 list "Behaviour that looks odd but matches the original" above still holds.

| Address | What the original does | Seen in |
|---|---|---|
| 0x41b9ba | The training slider passes min + value to its callback (in 0x41b8ac), which adds 1 again. Slider value 9 gives 11 days; the preset shows 5 days at value 5. | `casting_actors_file` |
| 0x42036d | The Lula cast list never sets its headline, so it shows the template text "ListboxHeadline". It uses the list row as a movie index, both for the row colours (0x42052f) and for the Lula flag on OK (0x420416). | `casting_lula_cast` |
| 0x41c19b | After deleting an expired job ad, the applicant draw skips the ad that moved into the freed slot for that hour. | code only |
| 0x41eaa7 | OK in the title list reloads the stored movie record, so unsaved changes on the sheet are lost. | `planning_screenplay` |
| 0x41e912, 0x41e500 | Only 9 movies can be planned, although there are 10 records. The refused 10th title is still copied into title slot 9. | `planning_poor` (the copy: code only) |
| 0x41eaa7 | With no movie, OK in the empty title list uses index -1 and copies "record -1" (0x45575d) into the work copy. | code only |
| 0x41efd2 | The storyboard label "Basic price 10 minutes :" never gets a value: control 26 is empty in WET.DDF and the code never sets it. | `planning_storyboard` |
| 0x41dd79 | Removing a person from a cast shifts the list but leaves the old last value and writes -1 one place after the new end (527, 17, 0 becomes 17, 0, -1). | `planning_fire_actor` |
| 0x4282af | For a cast member not in status 2 it passes the list index, not the person, to 0x42568e, which still lowers the count (call at 0x4282c6). The cast is silently emptied, and START then says "you should cast at least two actors". Actors in training or sick lose their parts the same way. | `studio_cast_dropped` |
| 0x4248dc | OK in the film list without a selection assigns movie record 0, whatever its state. LB_SETCURSEL gets the record index, not the list row. | code only |
| 0x423fc1 | The branch that shows string 2009 can never run. | code only |
| 0x4242f2 | When shooting ends while the player is in the studio, the hotspot count stays 1, so only the exit works until the room is entered again. | `studio_shoot_end` |
| 0x425a1e | A wrongly bought sex toy is paid and thrown away when the player answers no. Closing the buy sheet without buying still asks; yes appends an empty slot, reads catalog record -1 and adds 1 to the director's frustration. | `studio_props_buy`, a probe run |
| 0x40716d | The clock jumps (the studio break, the 9:00 trash box, the 13:00 feminist and sabotage boxes) step the clock directly and skip the hourly hooks they pass: the studio hour countdown 0x425703 and the 10:00 scandal, copier and sound jobs. The break keeps the minute (8:30 becomes 9:30). | `studio_event_actor` (the skipped 10:00 jobs: code only) |
| 0x428e0a, 0x42ddff | Cutting and sound jobs end at 10:00 only when the days since the start are greater than the job's days (a strict >). A 2-day job started on day 1 ends on day 4, not day 3. | recipes `cutcopy_day3`, `cutcopy_cutdone`, `soundprops_day3`, `soundprops_sounddone`; `soundprops_sale_busy` |
| 0x411a4c | Every valuation adds (sound + cutter quality) / 2 to the movie's +0x2fc again (0x411a92). Declining a distributor offer raises the next one ($76562, then $115780). | `movie_sale_rights` |
| 0x411bff | On a category match it adds 30 % to the first sales window (+0x11c) but replaces the second and third with 30 % of themselves ("mov" instead of "add" at 0x411cd8 and 0x411d33). | `soundprops_sale_done`, recipe `movie_onsale` |
| 0x411bff | The title is copied with strcpy into a stack buffer that is never cleared, so the FILMB.TMP bytes after the title differ between template builds. The game fields are the same. | recipe `warehouse_sale` (a) |
| 0x42dcee | Sound quality divides by 3 a sum whose first term is a stack slot set to 0 at 0x42dd03 and never changed. | `soundprops_sale_done`, the movie chain |
| 0x406261 | When a bought item lands in the last slot of the shared buy sheet, the count is not incremented and Buy is disabled. Reopening the sheet counts one slot past the array; for sound equipment that slot is the count itself. | code only |
| 0x406261 | Hovering over a list arrow already scrolls the buy list (WM_DRAWITEM branch). | `warehouse_recreation` |
| 0x42c5f8 | The props room registers 7 hotspots but sets the count to 6, so "Buy props" is never hit-tested. Its click case is only a ret anyway. | `soundprops_props_staff` |
| WET.DDF, DDF 60 | The title "Films which need sound track:" wraps inside a 16 px high rectangle, so only "Films which need" is visible. | `soundprops_sound_track` |
| 0x42a720 | Accept checks the copier capacity with the order quantity plus entry +0x104 (0x42a7d0), but 0x42a0ac already added the quantity to +0x104. Each order counts twice. | code only (a) |
| 0x42a1ef | Orders still open at the day end are dropped (the order table is cleared). The warehouse list and its orders are not saved, so after loading there are none until the list is rebuilt at the next day end. | `warehouse_orders` (a) |
| 0x43015e | A breaking scandal always calls the marketing office's leave function 0x42eaf2 (0x430176), not the current room's. On the plot the old copy of BAUTEN.TAF is not freed before it is loaded again, and room 20 always comes back in plot mode. | `marketing_plot_news`, `marketing_nextday` |
| 0x402f72, 0x42f2ba | The campaign value field (+4: 25, 15, 10, 50) is never read. Every running campaign gives the same 25 points. | code only |
| 0x4329e0 | The beauty clinic never charges for an actor. The actor branch (0x432af5..0x432b62) has no subtraction; the actress branch subtracts at 0x432cd1. | `movie_beauty_clinic` |
| 0x4331c4 | The operation sheet checks the first radio button but keeps the earlier choice in 0x485c10, so OK without a new pick charges the earlier operation. | `movie_beauty_clinic` |
| 0x425775 | The Lula part of the movie quality (+0x30c) is read from 0x458349, which nothing writes, so it is always 0. | `movie_shooting_end`, `studio_shoot_end` |
| 0x4100a7 | The equipment wear message is built from slot 0 after 0x410194 removed the broken item, so the category can be wrong. When the only item breaks it reads EQUIP.TAP record -1 and says "Camera equipment" for a leisure item. | `leftovers_equipment_lastbreaks` |
| 0x4100a7 | The message reads "Leisure equipment equipment" (string 220 "%s equipment" with string 945). | `leftovers_equipment_breaks` |
| 0x418fc4 | Cancelling a studio lease sets the cast entries to -1 but keeps the cast count. Code that walks the cast without a -1 test (0x4282af) would index the staff table at -1. | code only |
| 0x418fc4 | Cancelling a studio lease clears the studio record, camera and lighting included, with no refund. | `leftovers_studio_lease` (a) |
| 0x418489 | A cancelled building's plot hotspot stays active until room 20 is set up again, because plot hotspots are registered only at setup. | `leftovers_studio_lease` |
| 0x416c3c | With the buy-back guarantee the lady realtor always makes an offer (without it, only in 30 % of the tries). The offer is still the list price 850000 plus finished extensions minus up to 34 %, not the 935000 paid. | `leftovers_mansion_sale` |
| 0x416c3c | A mansion sale does not clear the price paid (0x45d0fd). | `leftovers_mansion_sale` |
| WET.DDF offset 970026 | "Pawnstore things" is followed by about 85 0xff bytes, and the game draws them. | `leftovers_pawnshop_items` |
| 0x408dea | The motel eviction check clears the evicted flag (+0x10) but never the "owner told the cops" flag (+0x14, 0x455750). | `engine_motel_evict` |
| 0x443002 | `__exit` pushes ESI instead of the exit status in EBX (0x44301b), so "End Program" in the CD dialog exits with a garbage code (65552 here). | manual CD run |

(a) The explorer noted this without an explicit verifier verdict. It was checked against the disassembly while this section was written.

### Round 2 found no runtime bugs

No explorer, verifier or critic found a place where the recompiled game differs from the original. No log of any of their runs had a `lula[trap]` or `lula[fatal]`. The only `lula[warn]` was the intended GetOpenFileNameA stub in the manual CD run: the runtime has no file dialogs, so Browse is cancelled. No `issues/` repro was needed. The things that did go wrong were in the tools; they are listed under "Tooling fixes after round 2".

### How round 2 was verified

- Each area and the movie chain had an independent verifier. It ran every scenario of the area twice, in two separate `tools/scenarios.py run ... -j 2` invocations. In both runs it checked that every function the explorer claimed appears in that scenario's coverage file. None was missing.
- The verifiers decoded the save templates and `--keep` saves and compared them with the claimed state: account balances, staff statuses, movie fields, studio records and FILMB.TMP records.
- They read the disassembly behind each reported quirk (the table above).
- They looked for weak checks, that is checks that would also pass if the step failed. The common case was a "sheet reopened" check that also matched when the sheet never closed, because the runner logs each text once per script step. Another was an unbounded `.*` that a later event could satisfy. They tightened checks in 21 scenarios. They tested each new check against the real logs and against edited logs that simulate the failure, and reran the edited scenarios twice more. `warehouse_orders` also got more slack in its timing.
- The critic merged all coverage (468 files). It confirmed that each of the 160 "covered" claims in the area reports is in the merged coverage and is a real function entry in `gen_tables.c`. It spot-checked behaviour claims (for example the uncharged actor operation) and classified all 132 live functions outside video and MIDI that had never run. It corrected a few call-site addresses in the engine report; no verdict changed.

### Tooling fixes after round 2

- **Coverage of reconstructed functions.** Functions replaced by hand-written code in `src/reconstructed/` had no coverage counter: 0x442be4 (memcpy), 0x44c3c3, 0x43e8eb, 0x43ebd3, 0x43f7c1, 0x440041 and 0x442431. The five resource functions only looked covered because of `.cov` files written before they were reconstructed. The lifter now counts each of them at its direct call sites (`counted_at_call` in `tools/recomp/lift.py`).
- **0x44c3c3 counted as dead.** `tools/coverage_report.py` took every address on a line of code-discovery appendix B, including the live 0x44c3c3 named in the reason of a note. It now takes only the first address of a `Note:` line, so 0x44c3c3 counts as live (1262 live functions).
- **TextOutA.** The game draws hotspot labels with TextOutA, and `src/runtime/win32/gdi32.c` traces it, but `build/game/lula` was older than that trace. No log had a TextOutA line, so checks had to use other texts. The game is rebuilt, and hover labels can now be checked in the log.
- **`# covers:` header.** `# covers: 0xADDR ...` makes a scenario require that these functions ran, read from the run's coverage file. It is for effects the log cannot show, such as a slider drag.
- **`down` and `up` script ops.** `down X Y` and `up X Y` in `src/runtime/platform_sdl.c` press and release the left button, so `move` lines in between drag. Before, `click` pressed and released at once and `move` carried no button, so no script could drag.

### What is left

After the fixes, 139 live functions have never run: 9 in video and 5 in MIDI/CD audio, which are on hold and left out, and 125 others. The critic classified each of them; its list had 132, of which 4 ran in its manual runs, memcpy turned out to run, and the slider pair is now covered.

| Class | Count | What it would take |
|---|---|---|
| Unreachable with this game's code and data | 93 | Nothing. 81 are in Watcom C runtime 2 (stdio streams, console I/O, signals and fatal errors, C++ exception handling, math errors, FPU set-up and the Pentium FDIV workaround, unused printf formats, heap). The rest: 3 each in the Watcom C runtime and the image/sound file library, 2 each in window/DirectDraw, the 2D graphics engine and the blitters. |
| Error paths | 22 | A failing host API (CreateWindowExA, DirectDraw or DirectSound creation, VirtualAlloc), a CPU exception (SEH), a stack overflow or a missing string resource. That needs fault injection in the runtime. |
| Time zone | 4 | A TZ variable, which the runtime's fixed environment block does not have: 0x4496c8, 0x4496ec and 0x449815 parse it, and 0x448fdf handles daylight-saving rules that start and end in the same month. |
| Debug code | 3 | The environment variable NGS-REVEAL, which makes 0x443196 (with 0x448a4c and 0x448a5d) print hidden NGS copyright banners. |
| Host configuration | 2 | A 15-bit display (0x44287d) or a surface pitch other than 1280 bytes (0x444e52). A real Windows machine could run them; the runtime always reports RGB565 at 640x480. |
| Not attempted | 1 | 0x434156, the Sleep in the video playback loop (video is on hold). |

Why so much library code never runs: the game never uses Watcom stdio (its file layer calls kernel32 directly), its format strings use only d, ld, lu, s, x, lx, %% and one %f (the F7 version line), and it registers only plain destructor tables. The engine functions in the list wait for data the game does not have, for example lines that are not axis-aligned (0x445c8b), a TAF type other than 16 (0x441a14) or a TPF file outside the video files (0x43fbbe).

Other open points:

- Four functions ran only in the critic's manual runs, and the runner cannot reach them: the CD dialog 0x402288 and 0x402510 with the GetOpenFileNameA thunk 0x44cf60 (a recipe cannot write CDROM.LOC), and 0x4493bb, which runs only when the game starts on a daylight-saving switch day (`LULA_SCENARIO_CLOCK` is global, so it would change rand() and every template). Their coverage files are in `build/explore/critic/`.
- At the time of writing, a few functions had run only in one-off exploration runs and in no scenario, for example 0x422bf8 and 0x422de5 (the round-1 simulation run L5) and 0x40a15c (the WM_DESTROY handler, reached in two round-1 office runs). Comparing the coverage of the scenario runs (`python3 tools/scenarios.py coverage`) with the merge that includes `build/explore` lists them all.
- Branches inside covered functions that no run took: the copier capacity refusal in Accept (it needs a movie value of about 75000 or more), a match in 0x42ab33 (it needs a type-5 entry in 0x4596c9, whose writer was not found) and the random resignation at the day end (0x41da0d calling 0x41dd79).
- Video is still on hold, and the font question from round 1 is still open. Round 2 adds one clipped text to it: DDF 42 shows only "Your photos are" of "Your photos are ready " with the substitute font.
- The round-1 scenario `office_stage2_casting.txt` clicks "Look at applications" when there are no applicants. That hotspot is not registered then, so the click does nothing, although the `# expects:` line says the sheet opens. No check depends on it, but the text should be corrected.
