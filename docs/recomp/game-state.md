# Game state reference: memory and save files

This document describes the state of WET.EXE ("Lula - The Sexy Empire", 1997) in guest memory and in its save files. It is for people who rebuild the game logic as readable C, edit saves, or write new scenarios.

It is compiled from the state notes of exploration round 2 (the base setup, the nine area reports with their verifiers, the movie-chain report and the critic), from the `patch` and `check` fields of the save recipes in `tests/scenarios/saves.json` and `tests/scenarios/saves/*.json`, and from checks made while writing it. Room tables, hotspots and the DDF dialog engine are in `docs/recomp/specs/resources-and-game-map.md` (B.5, B.7, B.9, B.10); this document does not repeat them.

## Conventions

- Addresses are guest virtual addresses of WET.EXE (image base 0x400000).
- Every field is a 32-bit little-endian signed integer (a dword) unless the size column says otherwise.
- The saved block starts at 0x455620. In a SAVEGAME file, offset = address - 0x455620.
- Record fields are written as `+0x14c`. For record 0 of a table the absolute address is often given too.
- Evidence tags:
  - `code 0xADDR`: read in the disassembly of WET.EXE (or in the generated C or the Ghidra output).
  - `save NAME`: seen in a save that the game itself wrote; the template is `build/scenarios/saves/NAME`, its recipe has the same name.
  - `patch NAME`: recipe NAME patched the field, and the game then behaved as described.
  - `run NAME`: seen in the log or the frames of the scenario `tests/scenarios/NAME.txt`.
  - `checked here`: checked while writing this document (capstone disassembly of WET.EXE, EQUIP.TAP, PERSO.TAP or a template save).
  - **INFERRED**: read from code or data but not confirmed by a run or a save.
- Round-2 sources are named after the area that wrote them: base, casting, planning, studio, cutcopy, warehouse, soundprops, marketing, leftovers, engine, movie (the production-chain explorer) and critic. Where two of them disagree, the text says so under **Disagreement**, and section 16 lists all of them.

---

## 1. Save files

### 1.1 Slots and files

| File | Content | Code | Can a recipe patch it? |
|---|---|---|---|
| `DATA/SAVE/SAVEGAME.  N` | Header, guest block, staff table (1.2) | save 0x40d71d in proc 0x40d611; load 0x40daec and the list 0x40ddbc in proc 0x40d9e3 | Yes (`patch`) |
| `DATA/DATABASE/STF1DAT.  N` | Stage-1 inventory: u32 count, then 0x68-byte records of the list [0x45d940] (12.4) | write 0x40fca0, read 0x40fd83 | No. `copy_slot` copies it |
| `DATA/DATABASE/FILMB.  N` | Byte copy of FILMB.TMP, the movies on sale (8.7) | 0x4424b3 called at 0x40d7cb on save; copied back on load (0x40dbc0). If the load finds no FILMB file for the slot, it deletes FILMB.TMP (DeleteFileA at 0x40dbd5), so the movies on sale are gone | No. `copy_slot` does not copy it |
| `DATA/DATABASE/FILMB.TMP` | Live NGS pool of sold movies | 0x411bff, 0x40bfed, 0x411ed9, 0x42a1ef, 0x42a4b9, 0x42a6b0; deleted at startup 0x4020ad | No |
| `DATA/SAVE/WET.1ST` | Settings, 0x84 bytes = guest 0x455034..0x4550b7 | read 0x4012ce, written 0x401798 | No |

- The name is `sprintf("%3d", slot)`, so slot 1 is `SAVEGAME.  1` with two spaces.
- The recipes use slots 1 to 4. Their Load-dialog entries are at (300,81), (300,94), (300,107) and (300,119).
- The host must create `DATA/SAVE` and `DATA/DATABASE`; the game imports no CreateDirectoryA (B.9).

### 1.2 SAVEGAME layout

| File offset | Address | Size | Content | Evidence |
|---|---|---|---|---|
| 0x0 | 0x455620 | 4 | `NGS\0` | checked here: every template |
| 0x4 | 0x455624 | 4 (float) | Save version 1.69 (0x3fd851ec). The save list compares it at 0x40de10: a file with a lower version gets an empty list entry (jb 0x40dd18 zeroes the name). The load proc reads the file without a version check | checked here: code 0x402f68, 0x40de10, 0x40dd18 |
| 0x8 | 0x455628 | 0x50 | Save name, NUL-terminated. The runner reads the name here | B.9; tools/scenarios.py `read_slot` |
| 0x58 | 0x455678 | to 0x7b8c | Rest of the guest block, up to 0x45d1ac inclusive (map in 1.3) | code 0x40d73f (length 0x7b8d) |
| 0x7b8d | pseudo 0x45d1ad | 553 x 40 = 22120 | Staff table (section 6), copied from the heap array [0x45555c], count [0x455348] = 553 | code 0x40d760; checked here |

Every template is 53749 bytes = 0x7b8d + 22120 (checked here).

**Pseudo addresses.** The staff table is not at a fixed guest address. Recipes still address it as if the block went on: staff record i, field f is at pseudo address 0x45d1ad + 40*i + f (file offset 0x7b8d + 40*i + f). The runner turns every address into a file offset, so this works for `patch` and `check` (base, used by prod_s2staff).

### 1.3 Map of the guest block

| Range | Content | Section |
|---|---|---|
| 0x455620..0x455677 | Header and save name | 1.2 |
| 0x455678..0x4556c3 | Bank deposit, interest rates, credit line, account, room, stage, credit warning, date, time, weekday, speed | 2, 3 |
| 0x4556c4..0x455717 | Weekday names, 7 x 12 bytes ("Monday" first) | checked here |
| 0x45571c..0x45572f | Sound settings that WET.1ST also stores (B.9). A load keeps the current values (1.4) | B.9; checked here: code 0x40da89, 0x40dc1e |
| 0x45573c..0x455848 | Stage-1 state: motel, police, FBI, royalties, goal flags | 12 |
| 0x455849..0x455aa0 | 15 building records of 40 bytes | 5 |
| 0x455aa1..0x457b48 | 10 movie records of 0x344 bytes | 8 |
| 0x457b49 | Movie count | 8 |
| 0x457b4d..0x458254 | Job ads, 0x24 bytes each | 6.6 |
| 0x458255 | Job ad count | 6.6 |
| 0x458259..0x458280 | Applicant list, 10 dwords | 6.6 |
| 0x458281 | Applicant count | 6.6 |
| 0x458289..0x458348 | 3 studio records of 0x40 bytes | 8.4 |
| 0x458349..0x458378 | Lula values | 11 |
| 0x458379..0x4595b8 | Equipment slot arrays | 9 |
| 0x4595b9..0x459628 | 4 advertising campaigns of 28 bytes | 10.1 |
| 0x459629..0x4596c8 | 10 scandals of 16 bytes | 10.2 |
| 0x4596c9..0x459814 | Video charts | 14.2 |
| 0x459815..0x459834 | Hell Devils sabotage job of stage-2 room 7 (the parking lot), read by the 11:00 hook 0x431691 | 10.5 |
| 0x459835..0x45d0ec | Stage-3 stores: 11 cities x 6 stores x 0xdc bytes | 13.2 |
| 0x45d0ed..0x45d1ac | Stage-3 flags, mansion, extensions, current city, parties | 13 |

### 1.4 What loading does, and what is not saved

The load proc reads the block (0x40db18), the staff table, FILMB and STF1DAT (0x40fd83). Then it calls INIT_STUFE 0x40989c with the saved stage (0x40dbfc). INIT_STUFE overwrites several saved fields (checked here: code 0x40989c..0x409a31):

| Field | Value after load |
|---|---|
| Credit line 0x455684 | 0 in stage 1, 70000 in stage 2, 100000 in stage 3. A credit line raised at the bank (+10 %, 0x4136eb) is lost on the next load, and patching 0x455684 has no effect |
| Overdraft rate 0x45567c | 12 + (([0x45533c] % 100) & 3) |
| Deposit rate 0x455680 | 2 + (([0x45533c] % 100) & 3) |
| Day hours [0x4555d0]/[0x4555d4] (not in the block) | stage 1: 7 and 23; stage 2: 9 and 19; stage 3: 9 and 21 |
| Video charts 0x4596c9.. | rebuilt in stage 2 by 0x40bfed (called at 0x40995b). This is why the base notes saw this region "change in every save" |
| Stage 3 | 0x40b952 copies the current city's name (string 5376 + [0x45d18d]) into the RAM buffer 0x45de7c. INIT_STUFE calls it at 0x409a0f and the load proc again at 0x40dc0f |
| Sound settings 0x45571c..0x45572f | Not taken from the save. The load proc stores the five current values before the read (0x40da89..0x40daad) and writes them back after INIT_STUFE (0x40dc1e..0x40dc46) |

INIT_STUFE also enters the stage's first room (stage 1 room 1, stage 2 room 5, stage 3 room 6). The load then goes on to the saved room (base: "the game opens the office first, then the saved room 20").

RAM-only state, lost by a save and a load:

| What | Address | Source |
|---|---|---|
| Warehouse list and orders, the day's direct sales, the marketing level | 0x484bbc/0x484d4c, 0x484d50/0x485070, 0x485080, 0x48507c | warehouse |
| Studio ready flags, pending events, pause message | 0x484848[3], 0x4848c8[3], 0x4848d4[3], 0x484864 | studio |
| Room monitors (cutting, copier, warehouse, dubbing) | 0x484afc..0x484b08, 0x484b70, 0x484bb8, 0x485984 | cutcopy, warehouse, soundprops |
| Rocket announced flag | 0x45dc08 | leftovers |
| Stage-1 distributor sheet state | 0x45e200..0x45e21c | engine |
| Stage-2 distributor busy state, rights or licence choice, selected movie | 0x45e2a8..0x45e2bc, 0x45e2ac, 0x45e2b0 | cutcopy; checked here |
| Movie planning work copy, current movie | 0x45d5f8, 0x483e24 | planning |
| Realtor mode and strip scroll of room 20 | 0x483b9c, 0x483c54 | base |
| Staff icon flag, simulation pause flag | 0x4555f4, 0x4555fc | 14.1 |

So room 20 always loads in plot mode, a warehouse has no orders until one hour after the next day end, and the rocket announcement does not come again after a load in the same run: only WinMain clears 0x45dc08, once at program start (0x403a20), so it comes again only after the program is restarted (checked here).

---

## 2. Clock, calendar and game speed

### 2.1 Fields

| Address | Field | Values | Evidence |
|---|---|---|---|
| 0x45569c | Day | 1..30 | code 0x40a26e |
| 0x4556a0 | Month | 1..12 | code 0x40a28f |
| 0x4556a4 | Year | 1997 at the start | code 0x40a2aa |
| 0x4556a8 | Hour | Normally stage day start .. closing hour. A stage switch keeps the old hour, so the game-written stage2 save starts stage 2 at 8:05, before the stage-2 day start 9 | code 0x40a22d; save stage2 |
| 0x4556ac | Minute | 0..59 | code 0x40a20e |
| 0x4556b8 | Weekday | 0 = Monday .. 6 = Sunday. Names at 0x4556c4 + 12*n | code 0x40a25a; cutcopy (date text 'Monday der 1.1.1997') |
| 0x4556bc | Minute length | One game minute lasts 0x4556bc + 1 clock calls | code 0x40a2b0 |
| 0x4556c0 | Minute countdown | Counts down to 0, then the minute advances | code 0x40a200 |
| [0x4555d0] | Day start hour (RAM) | 7 / 9 / 9 for stages 1 / 2 / 3 | code 0x4098ec, 0x409965, 0x4099c0 |
| [0x4555d4] | Closing hour (RAM) | 23 / 19 / 21 | same |
| [0x4555e8] | Day-end flag (RAM) | Set by the clock, cleared at the end of 0x404a33 | code 0x40a24a, 0x404cca |

### 2.2 How the clock runs (checked here: code 0x40a1f0)

- The game tick 0x4050c1 runs once per 60 ms WM_TIMER and calls the clock 0x40a1f0.
- If 0x4556c0 > 0, the clock only decrements it. Otherwise the minute advances and 0x4556c0 is reloaded from 0x4556bc.
- After minute 59 the hour advances. When the hour is already the closing hour, the day ends instead: [0x4555e8] = 1, the hour goes back to the day start, the weekday goes 6 -> 0, the day goes 30 -> 1 with the month, and month 12 -> 1 with the year. Every month has 30 days.
- So the stage-2 day ends at 19:59 and the next day starts at 9:00. Stage 1 ends at 23:59 (next 7:00), stage 3 at 21:59 (next 9:00).
- The clock stands still while a DDF sheet or WORK_ORDER_DLG is open (base, movie; seen in runs, not traced in code).

Game speed 0x4556bc:

| Value | Meaning | Evidence |
|---|---|---|
| 0 | Fastest: 1 call per minute, about 3.6 s per game hour | patch s2fast, leftovers_s1fast |
| 2 | Set when stage 3 starts | checked here: code 0x404ba9 |
| 3 | Normal: 4 calls per minute, 14.4 s per game hour. Set by a new game and by the stage-2 switch | code 0x4016e2, 0x4028a5, 0x408e7d |
| 30 | Recipe "slow clock": 31 calls per minute, about 112 s per game hour. Hourly events still fire, but only when a script runs past the next full hour (15.3) | patch prod_s2slow and many others |
| 400 | Recipe value for studio event tests: one minute lasts 401 ticks | patch studio_toys, studio_props |

The options sheet slider shows 6 - 0x4556bc and writes 0x4556bc = 6 - slider value (checked here: code 0x40d5f8, 0x40d456). Values above 6 can only come from a patch.

`0x40716d(n)` moves the clock n hours forward, keeps the minute and clamps the hour to the day start. It does not run the hourly hooks it passes (studio and marketing verifiers, code 0x40717f..0x407250). If it passes the closing hour, it sets the day-end flag [0x4555e8], sets the hour to 0 and moves the weekday and date on. The clamp then gives the next day's start hour (checked here: code 0x4071c3..0x40724a). Five places call it (checked here): the stage-1 wrapper 0x4090fb, which then calls the stage-1 hook 0x408a87 once per hour; the trash box (0x40e593); the studio break (0x425258); the stage-3 map in room 25 (0x42b4c9, n from 0x48514c), which checks the day-end flag right after; and the feminist and sabotage boxes (0x430827). So a 9:00 trash box can skip the 10:00 hooks of stage 2.

### 2.3 Days between dates

`0x405314(date)` returns the days since a date: (year - 1997) * 365 + month * 30 + day for the current date, minus the same sum for the given date (checked here: code 0x405314..0x40538e; cutcopy, casting, leftovers). A year counts 365 days but its 12 months only 360, so the count jumps by 6 from 30-12 to 1-1. All "older than N days" tests use it. Day 0 of month 1 is one day before 1-1-1997 (casting_ads note).

### 2.4 Hourly hooks (checked here: code 0x4050c1..0x40526c)

At minute 0 (guarded by 0x45d964), the tick first calls the stage hook [0x4550cc] (stage 1: 0x408a87, see 12.1). Then:

| Stage | Hour | Call | What it does |
|---|---|---|---|
| 2 | Odd h with start + 1 < h < closing - 1 (11, 13, 15, 17) | 0x41c19b | Applicant draw (6.6) |
| 2 | Every hour, when 0x40e3bf and 0x40e3ed pass | 0x42a0ac | Warehouse orders (8.8) |
| 2 | 9 (not on 1-1-1997) | 0x40e4a4 | Trash roll, then equipment wear 0x40ffa6 (9.4, 10.4) |
| 2 | 10 | 0x42f334, 0x428e0a, 0x42ddff | Scandal roll, cutting done, sound done |
| 2 | 11 | 0x431691 | Hell Devils job of room 7, the stage-2 parking lot (10.5). A waiting job hits when rand() % 100 > 50 (string 1804). After the hit, each 11:00 lowers the target company's chart sales, until the job's days are over and the block is cleared (checked here: code 0x431691..0x4317b8) |
| 2 | 1 and 13 | 0x43070e | Feminist and sabotage roll (10.4). Hour 1 never comes in stage 2 |
| 3 | 9 | 0x419a84 | Mansion extensions (13.3) |
| 3 | 10 | 0x410c04 | Party (13.4). With a buffet or girls booked, it holds a party when the days since 0x45d19d are a multiple of the period: it charges 200 * buffet + 1000 * girls, shows string 272 and sets 0x45e158 = 1. On other days it clears 0x45e158 (checked here: code 0x410c04..0x410ccf) |

After the hourly part, every tick in stage 2 calls the studio tick 0x42408f, unless [0x4555fc] != 0 (code 0x40525a).

**Disagreement:** base gave the applicant draw as "odd hours 9-17". Casting found 11, 13, 15 and 17, and the code above agrees.

### 2.5 Day end

The day end 0x404a33 runs when [0x4555e8] is set. It shows string 902 'And another busy day comes to an end.' and then (checked here: code 0x404a33..0x404cd5):

Stage 2:
1. If account + credit line > 0, clear the credit warning 0x455698.
2. Daily costs sheet (DDF 7, callback 0x40538f), which books the day (3.3).
3. Marketing level: 0x42ab22(0x42f2ba()) stores 25 per running campaign in 0x48507c.
4. If the warehouse (0x40e3bf) and the copier (0x40e3ed) pass: 0x42a1ef builds the warehouse list (8.8).
5. On the day end into a Monday (weekday 0): video charts 0x40bfed.
6. Staff update 0x41da0d (6.7).
7. Credit check (3.4).
8. Account >= 2000000: switch to stage 3 (section 4).

Stage 3: the bank interest (3.2), then 0x4225fc, 0x42244e(city) for the 11 cities, 0x41790c (the mansion rent check, 3.4), and the account minus 0x419de7(). Then the credit check and, at 50000000 or more, the rocket (section 4). The roles of the other calls are not documented except what section 13 says.

The warehouse notes add: on a stage-2 day end the 9:00 hourly hooks of the new day run before the day-end sequence (19:59 -> 9:00, then 0x40e4a4 and 0x42a0ac, then 0x404a33).

---

## 3. Account, credit line and bookkeeping

### 3.1 Fields

| Address | Field | Values | Evidence |
|---|---|---|---|
| 0x45568c | Account | Signed dollars | status line; every recipe check |
| 0x455684 | Credit line | Rewritten on load (1.4). Raised by 10 % at the bank (string 412, 0x4136eb) | checked here; check marketing_poor = 70000 |
| 0x455678 | Bank deposit (INFERRED name) | Earns 0x455680 % per 30 days. Written by the bank dialog 0x413dd4..0x413e0b | checked here: code 0x413d3e |
| 0x45567c | Overdraft rate | 12..15 % per 30 days | checked here: code 0x413ca5 |
| 0x455680 | Deposit rate | 2..5 % per 30 days | checked here: code 0x413d3e |
| 0x455698 | Credit warning given | 0 or 1 (3.4) | checked here: code 0x404b3c |

### 3.2 Money rules

- **Money check 0x40561d:** in stage 2, account + credit line >= amount. It only compares; the caller subtracts (planning, marketing, movie verifier). With account 30000 and credit 70000, 100000 $ can be spent (patch marketing_poor). With -69500, only 500 $ (patch planning_poor). In stage 1 it compares the account alone (amount <= account). Stage 3 uses the same test as stage 2 (checked here: code 0x405649..0x405661).
- **Bank interest (checked here):** 0x413ca5 returns the daily overdraft interest -account / 100 * [0x45567c] / 30 when the account is negative. 0x413d3e returns the daily deposit income [0x455678] / 100 * [0x455680] / 30. Both round a result above 0.5 and below 1 up to 1, and a result of 0.5 or less down to 0. Any larger result is truncated toward zero (0x442d7c runs frndint with the chop rounding mode). The overdraft line is 0 when the account is >= 0, and the income line is 0 when the deposit is <= 0 (checked here: code 0x413cc4, 0x413d5a, 0x442d85).
- **When money moves:** equipment, buildings bought, job ads (800 $ staff, 1200 $ actors), training, beauty operations (actress only), scandals and campaigns are paid at once. Renting pays nothing at once; rent is charged daily. A sale of rights pays the next day. Licences pay on Mondays. Direct sales pay at the next day end (section 8).

### 3.3 Daily costs (stage 2, callback 0x40538f)

The movie notes list the lines. The sheet adds receipts minus expenses to the account at 0x405612.

| Line | Function | Rule |
|---|---|---|
| Bank interest | 0x413ca5 | 3.2 |
| Rent | 0x418eb2 | Sum of rent / 30 of buildings that are rented and not bought (5.3) |
| Staff | 0x41d9c0 | Sum of pay +0x10 of people in status 1, 2, 3 or 5 |
| Bank income | 0x413d3e | 3.2 |
| Distributor royalties (control 0x13) | 0x411ed9 | 8.7 |
| Direct sales revenue (control 0x14) | 0x42ab76 returns 0x485080 | 8.8 |

Example (run movie_heal_dayend, save movie_healed): rent -1430 (14 buildings), staff -7837 (22 people), total -9267.

### 3.4 Credit limit and bankruptcy (checked here: code 0x404a91..0x404aa0, 0x404b0b..0x404b73, 0x404bc1..0x404c99)

- At a stage-2 or stage-3 day end, if account + credit line < 0 and 0x455698 = 0: string 907 'You've exceeded your credit limit, you have to balance your account today!', and 0x455698 = 1.
- A later day end clears the flag before the daily costs, but only when account + credit line > 0 (stage 2: 0x404a91..0x404aa0; stage 3: 0x404bc1..0x404bd0). If the flag is still 1 at the check after the costs, the bust dialog opens whatever the balance is by then (0x404b48, 0x404c72). The dialog is 0x41f615 (DDF record 0x59 with two buttons; string 908 'Your company is bust, Lula has left you ... Do you want to start again?' has the matching text, but the link was not traced). Its first button starts a new game in stage 1 (0x4015b9, then INIT_STUFE(1)); the other sets the quit flag 0x4555e4.
- In stage 3 the mansion rent check 0x41790c (called at 0x404c1b, before the credit check) sets 0x455698 = 1 at 0x417a0a when a lease date 0x45d109..0x45d111 is set and more than 7 days old. The check that follows then opens the bust dialog at the same day end, even with a positive balance. When the date is exactly 7 days old, the game asks for the rent instead (string 524). Paying sets the lease date to today; a refusal or too little money loses the mansion (string 525) (checked here: code 0x417919..0x417a0a).

---

## 4. Stages and how the game switches

| Address | Field | Values |
|---|---|---|
| 0x455694 | Stage | 1, 2, 3 |
| 0x455690 | Current room | 1..34 (B.5 of resources-and-game-map.md). A save stores the room it was made in |

| Switch | Condition and effect | Evidence |
|---|---|---|
| Stage 1 -> 2 | In the stage-1 day check 0x408dea: 0x455845 != 0 and 0x455841 != 0 and account >= 50000. Effects: the inventory list is cleared, a video plays, every staff status +4 is set to 0, INIT_STUFE(2), speed 3. The game goes on in the office (room 5). The clock is not reset there; the stage2 template was saved at 8:05 | checked here: code 0x408dfa..0x408e87; patch stage1_goal, s1flag |
| Buying the identity | 0x426755 sets 0x455841 = 1 and 0x455845 = 1 and pays [0x484724] = 10000 + 1000 * (rand() % 5 - 2), so 8000 to 12000 (set at 0x425d77). The switch to stage 2 then needs 50000 or more left after paying | checked here: code 0x425d4f..0x425d77, 0x42673d..0x42676e; s1rich note: account 70000 is enough |
| Stage 1 game over | 0x455845 set but 0x455841 = 0. Besides the purchase, 0x455845 is set when the FBI countdown 0x455831 reaches 0 (0x408ddb) and at the third arrest (0x433317). Then: when the FBI countdown is 0, string 3115 'The FBI have got you ... try again?'; after the third arrest (0x45582d >= 3), string 3119. Yes restarts stage 1, no quits | checked here: code 0x408ddb, 0x433317, 0x408ec2..0x408f76 |
| Stage 2 -> 3 | At the stage-2 day end, account >= 2000000 (cmp at 0x404b75): video 0x1f5e, then 0x403598 (its only caller; clears the extensions 0x45d13d[5] and fills the 11 x 6 store records with random data), INIT_STUFE(3), speed 2. Stage 3 starts at the Los Angeles realtor (room 6) | base; checked here: code 0x404b75..0x404ba9; patch stage2_rich, save stage3 |
| Rocket | Stage-3 day end, account >= 50000000 and 0x45dc08 = 0: 0x42977e (string 1152), then 0x45dc08 = 1 | leftovers; checked here: code 0x404c9f |
| Bust | 3.4 | |

**Disagreement:** base and the prod_s2all note say different things about where the stage-3 switch is: 0x404b75 (base) and 0x40528b (recipe note in prod.json). 0x40528b is inside the hotspot reset 0x40527f; 0x404b75 is the account compare (checked here).

Keep a stage-2 account below 2000000 in recipes, or the next day end moves to stage 3 (base problem 1; prod_s2all uses 1500000).

---

## 5. Buildings and the realtor plot

### 5.1 Building record (15 records of 40 bytes at 0x455849 + 40*i)

| Offset | Field | Values | Evidence |
|---|---|---|---|
| +0x00 | Monthly rent | 5.2 | save stage2 |
| +0x04 | Buy price | 5.2 | save stage2 |
| +0x08 | n (guarantee divisor) | 10..27 | save stage2 |
| +0x0c | Price with money-back guarantee = price * (1 + 1/n) | for example 30000 * (1 + 1/25) = 31200 | save stage2; checked here |
| +0x10 | Rented or owned flag. Gates the plot hotspot and the realtor strip | 0 / 1 | base; save prod_s2all |
| +0x14 | Bought | 0 / 1. The office (7) has 1 | base; checked here: save stage2 |
| +0x18 | Bought with the guarantee | 0 / 1 | base |
| +0x1c, +0x20, +0x24 | Day, month, year of the deal (0x4052ed) | 1, 1, 1997 after renting on day 1 | save prod_s2all |

Rented-flag address of building i: 0x455859 + 40*i.

### 5.2 The 15 buildings

Index i is also the realtor string 301 + i. Rooms come from the jump table 0x417e74 in the room-20 click handler 0x417eb0. Prices are from the stage2 save (checked here). Click points are plot positions the base notes measured.

| i | Building | Room | Rent | Buy | n | Rented flag | Plot click point (or rect) |
|---|---|---|---|---|---|---|---|
| 0 | Reception | 26 | 600 | 30000 | 25 | 0x455859 | 80,140 |
| 1 | Cutting | 29 | 3300 | 120000 | 14 | 0x455881 | 350,120 |
| 2 | Sound | 27 | 3600 | 180000 | 16 | 0x4558a9 | 460,120 |
| 3 | Copier | 30 | 3600 | 180000 | 12 | 0x4558d1 | 560,175 |
| 4 | Movie studio (studio 1) | 24 | 3600 | 180000 | 10 | 0x4558f9 | 230,250 |
| 5 | Marketing | 31 | 3000 | 150000 | 18 | 0x455921 | 400,210 |
| 6 | Warehouse | 25 | 4200 | 190000 | 22 | 0x455949 | 80,300 |
| 7 | Office ('Back to the office') | 5 | 0 | 0 | 0 | 0x455971 | 216..359 x 285..386 |
| 8 | Office with Lula's flat | 5 | 2100 | 110000 | 25 | 0x455999 | 216..416 x 258..387 |
| 9 | Movie planning | 22 | 3000 | 150000 | 20 | 0x4559c1 | 540,310 |
| 10 | Props | 32 | 2700 | 130000 | 19 | 0x4559e9 | 180,420 |
| 11 | Casting | 21 | 3000 | 150000 | 21 | 0x455a11 | 440,400 |
| 12 | Recreation | 28 | 1200 | 90000 | 27 | 0x455a39 | 580,440 |
| 13 | Studio 2 | 33 | 4500 | 200000 | 27 | 0x455a61 | 230,150 |
| 14 | Studio 3 | 34 | 4500 | 200000 | 27 | 0x455a89 | 70,400 |

Plot hotspots are tested in index order and the first match wins. Building 8 (216..416 x 258..387) overlaps the office and covers the safe plot point 366,300, so once Lula's flat is rented, 366,300 opens the office (movie notes). The other safe point, 300,395, lies below building 8 (y > 387) and hits no building.

### 5.3 Rules

- **Rent** needs account + credit line >= the monthly rent (0x40561d) and deducts nothing at once. **Buy** deducts +0x04, or +0x0c when the guarantee toggle (control 9) is set (base).
- **Daily rent** is the sum of rent / 30 over buildings that are rented and not bought (0x418eb2). With 14 buildings rented it is 1430 $ (movie, checked here from the record values).
- **Office record 7:** the room-20 setup sets 0x455971 = 1 every time (0x4184a9). Renting Lula's flat (8) clears it (base).
- **Lula's flat:** 0x455999 = 1 makes the office broom cupboard (office hotspot 5) lead to room 23; otherwise string 209 'That's just a junk room.' (casting, code 0x40cf34).
- **Cancel lease** (DDF 5, 0x418aa0) needs 30 days or more since the deal date (0x405314), else string 318. DDF 5 shows the 'Present rental' as rent / 30 (leftovers, patch leftovers_s2lease).
- **Cancelling a studio lease** (0x418fc4) puts its movie back to state 1. It writes -1 into the cast entries but keeps the cast count +0x2f4. The studio's camera and lighting are gone without a refund (leftovers).
- **Sell** (DDF 6) offers a price only when the building was bought with the guarantee or [0x45533c] % 100 > 80, else string 322. The price is (+0x04 / 100) * (50 + [0x45533c] % 30), so 50 to 79 % of the buy price, also for a guarantee purchase. It is paid at once, and +0x10, +0x14 and +0x18 are cleared (checked here: code 0x418cd7..0x418d83; leftovers).
- **Hotspots:** plot hotspots are registered only for flag != 0 and only at the setup 0x418489. A newly rented building is clickable only after re-entering the plot; a cancelled one stays clickable until then (base, leftovers).

### 5.4 Room 20 modes (RAM)

- Realtor mode 0x483b9c = 1 only when the plot is entered from room 6 (pre-call 0x418457). A loaded save in room 20 is always in plot mode.
- Realtor mode shows a film strip of buildings with flag != 1, except 7, starting at index [0x483c54]. Tiles are at x = 16 + 102*s (+1 for s >= 3), y 10..83; arrows at 0..29 and 630..640.
- A tile opens the DDF 4 info sheet (callback 0x4185fd: Rent 245,228, Buy 245,325, guarantee toggle 219..336 x 285..301). In realtor mode a click on a rented building on the plot opens DDF 5 (cancel lease, callback 0x418aa0), or DDF 6 (sell, callback 0x418c5c) when +0x14 = 1. A click on building 7 or 8 (the office, Lula's flat) goes back to room 6 instead, so Lula's flat cannot be cancelled or sold there (checked here: code 0x417f1a..0x418115).

---

## 6. Staff table

### 6.1 Staff record (553 records of 40 bytes)

In memory: the heap array [0x45555c], count [0x455348] = 553. In the save: file offset 0x7b8d + 40*i, pseudo address 0x45d1ad + 40*i.

| Offset | Field | Values | Evidence |
|---|---|---|---|
| +0x00 | Job | 0..21 (6.2) | base; save stage2 |
| +0x04 | Status | 0..5 (6.3) | base, casting; patch prod_s2staff |
| +0x08 | Unknown | 0 in all 553 records of movie_beauty | checked here |
| +0x0c | Level (wrench icons) | 0..9. PERSO.TAP gives 2..9 to everyone except person 9 (Ozzy Osram, lighting engineer), who has 0 | base; save casting_hired; checked here: PERSO.TAP, save stage2 |
| +0x10 | Current pay per day | dollars | casting; save casting_trained (2330) |
| +0x14 | Refusal and firing counter | Reject +10, fire +1, director quits +1, pay quit +1 (6.7). While it is not 0, an applicant draw skips the person. When the draw would have taken the person, it decrements the counter with a 29 % chance (rand() % 100 > 70) | casting; save casting_hired, casting_trained; checked here: code 0x41c378..0x41c3c7 |
| +0x18, +0x1c, +0x20 | Start date of training or sickness | day, month, year | save casting_trained, movie_beauty |
| +0x24 | Days of training or sickness | | same |

In a fresh stage-2 save, +0x00, +0x0c and +0x10 equal PERSO.TAP's job (+0x16a), A (+0x172) and normal pay (+0x16e) for all 553 people (checked here: save stage2). Every status and counter is 0.

**Disagreement:** base read +0x14 as "must be 0 to apply (INFERRED)". Casting found it is the refusal and firing counter, verified in two saves.

Addresses for one person: status of person 527 = 0x45d1ad + 40*527 + 4 = 0x462409.

### 6.2 Jobs

Names are strings 912 + job; staff ad texts 757 + job for jobs 0..19 (base). Strings 777 and 778 are not the ad texts of jobs 20 and 21: from 777 on, the strings describe a staff applicant's training (777 + A, 'no training and a complete dope.', 'no training and a total zombie.', ...). Actor and actress ads use 706/726/736/746 + A or B (6.6). "People" is the count in PERSO.TAP (checked here).

| Job | Name | Department and use | People |
|---|---|---|---|
| 0 | Lighting engineer | Studios, studio record +0x20 | 22 |
| 1 | Casting director | Casting (21). A working one (status 2) re-rolls the level of a hired actor or actress (6.5). More than one sets the staff icon (0x41c994) | 20 |
| 2 | Cutter | Cutting (29). 'Cut films' needs one in status 1, 2 or 3; only status 2 adds cutter quality | 19 |
| 3 | Screenplay writer | Planning (22) | 20 |
| 4 | Cameraman | Studios, +0x24 | 20 |
| 5 | Copying girl | Copier (30); laptop sale gate 0x40e3ed | 18 |
| 6 | Set builder | Props (32); crew quality | 19 |
| 7 | Warehousewoman | Warehouse (25); laptop sale gate 0x40e3bf | 19 |
| 8 | Doorman | Reception (26); guns; the 13:00 roll | 18 |
| 9 | Production assistant | Planning (22) | 20 |
| 10 | Production designer | Planning (22) | 20 |
| 11 | Cleaning lady | The 9:00 trash roll | 10 |
| 12 | Director | Studios, +0x28 | 19 |
| 13 | Props assistant | Props (32); hands sex toys to the studio | 19 |
| 14 | Props manageress | Props (32) | 9 |
| 15 | Secretary | Office. More than one sets the staff icon (0x40cdcd) | 10 |
| 16 | Dubber | Sound (27) | 39 |
| 17 | Switchboard operator | Marketing (31): advertising | 10 |
| 18 | Sound engineer | Sound (27) | 39 |
| 19 | Advertising manager | Marketing (31): scandals | 10 |
| 20 | Actor | Cast | 31 |
| 21 | Actress | Cast | 142 |

Department checks by room (base): 31 uses job 19 (and 17), 25 job 7, 26 job 8, 27 jobs 16 and 18, 29 job 2, 30 job 5, 32 jobs 6, 13 and 14, studios 0, 4 and 12, planning 3, 9 and 10 (0x41f4d7).

### 6.3 Status (+0x04)

| Value | Meaning | Card text | Evidence |
|---|---|---|---|
| 0 | Free (not employed) | | base |
| 1 | Hired, idle | 962 'Is glad there's nothing to do.' | base; run casting_saved_hire |
| 2 | Hired, working in its department (or cast in a movie, or in a studio crew) | 963 'Is occupying the available place right now.' | base |
| 3 | In training | 966 'Is still in training for %d day(s)!' | casting; save casting_trained |
| 4 | Applicant | | casting; save casting_hired |
| 5 | Sick (after a beauty operation) | 967 'Is still sick for %d day(s)!' | movie; save movie_beauty |

Helpers:
- `0x4057d2(job)` counts people of a job in status 1, 2 or 3 (base, soundprops).
- `0x4056a4(EAX start, EDX job, ECX status, EBX direction)` returns the next person with that job and status, or -1. EBX = 0 searches forward, otherwise backward. Status 1 also matches 2, 3 and 5 (soundprops; checked here: code 0x4056f8..0x40570a).

### 6.4 Assigning staff

`0x40590d(job)` opens the staff card DDF 0x19 (Perskart.TAF, callback 0x405948). Its assign button (control 1, 0x4059ad) gives the shown person status 2 and puts the job's current worker (the first person in status 2) back to status 1, so one person per job works. Dubbers (16) differ: the button toggles 1 and 2, and up to 5 work at once (a sixth sends the first back to 1). For studio crew (jobs 0, 4, 12) the replaced worker is the current studio's, and 0x424eba also stores the person in the studio record (8.4); the person still gets status 2 (checked here: code 0x4059ad..0x405ac7).

### 6.5 Hiring

The hire code 0x41c9ad turns an applicant (status 4) into status 1. It then sets status 2 if the person is the first of its job and the job is not 0, 4, 12, 20 or 21. Doormen and cleaning ladies always get 2 (base). Only actors and actresses get a new level. With a working casting director (status 2, level L) it starts from (A + B) / 2: below L it adds rand() % L, above L it subtracts (rand() % L) / 2, equal keeps the old level; then it is capped at 9. In casting_hired it came out as min(9, (4 + 7) / 2 + rand() % 7) = 9 (casting). Without one, an actor or actress gets B when B < A and keeps the level otherwise. Every other job gets level A in both cases (checked here: code 0x41ca76..0x41cb4d, 0x41ce22..0x41ce55). The applicant leaves the list.

prod_s2staff patched one person per job to the status the hire code gives the first employee. These people appear in many recipes (checked here: save prod_s2staff):

| Person | Name | Job | Status | Level | Pay |
|---|---|---|---|---|---|
| 164 | Jack Zasas | 0 lighting engineer | 1 | 6 | 200 |
| 149 | Ramira Eagleye | 1 casting director | 2 | 7 | 1500 |
| 193 | Kerstin Loisi | 2 cutter | 2 | 6 | 150 |
| 208 | Stanley Smith | 3 writer | 2 | 6 | 250 |
| 230 | Charlie Eichler | 4 cameraman | 1 | 6 | 210 |
| 257 | Regina Lustweib | 5 copying girl | 2 | 6 | 200 |
| 280 | Katja Rest | 6 set builder | 2 | 6 | 200 |
| 286 | Gerda Moller | 7 warehousewoman | 2 | 6 | 230 |
| 307 | Martin Rowers | 8 doorman | 2 | 6 | 160 |
| 323 | Judy Icecream | 9 production assistant | 2 | 6 | 200 |
| 343 | Iris Schnorchel | 10 production designer | 2 | 6 | 130 |
| 363 | Ursula Eierstock | 11 cleaning lady | 2 | 6 | 70 |
| 377 | Bernie Pornolando | 12 director | 1 | 7 | 300 |
| 394 | Manfred Woped | 13 props assistant | 2 | 6 | 150 |
| 550 | Linda Molly | 14 props manageress | 2 | 9 | 128 |
| 408 | Mandy White | 15 secretary | 2 | 6 | 100 |
| 418 | Susi Oralo | 16 dubber | 2 | 6 | 133 |
| 437 | Helga Schnabel | 17 switchboard operator | 2 | 6 | 100 |
| 447 | Stefan Raabs | 18 sound engineer | 2 | 6 | 166 |
| 466 | Nina Mount | 19 advertising manager | 2 | 6 | 160 |
| 527 | Thommy Session | 20 actor | 1 | 8 | 2230 |
| 17 | Cassandra Casablanca | 21 actress | 1 | 9 | 870 |

### 6.6 Job ads and applicants

**Job ad record** (0x24 bytes at 0x457b4d + 0x24*j, count 0x458255):

| Offset | Field | Evidence |
|---|---|---|
| +0x00 | Job | save casting_ads (0x457b4d = 20) |
| +0x04 | A: actors potency (text 736 + A), actresses experience (706 + A) | save casting_ads |
| +0x08 | B: actors figure (746 + B), actresses looks (726 + B) | save casting_ads |
| +0x0c | Pay-class limit, index into 0x483d4c: 100, 200, 350, 500, 800, 1000, 1500, 2000, 3000, 4500 | save casting_ads; casting |
| +0x10, +0x14, +0x18 | Day, month, year the ad was placed | save casting_ads |
| +0x1c | Unknown | |
| +0x20 | Cost: 1200 for actors, 800 for staff | save casting_ads |

The space holds 50 records; the casting notes give a limit of 48 (code, not run). The sheet buffer is 0x4553b8 (job), 0x4553bc (A), 0x4553c0 (B), 0x4553c4 (pay class), 0x4553c8..0x4553d0 (date), 0x4553d8 (cost).

**Disagreement:** base had "+4 job, +0xc level threshold (INFERRED)". Casting's layout above is confirmed by the casting_ads check.

**Applicants:** list 0x458259 (10 dwords, person indices), count 0x458281; the current one on the sheet is [0x483d78] (RAM).

- The draw 0x41c19b runs at 11, 13, 15 and 17 in stage 2 (2.4). It does nothing while [0x483d74] != 0, which the casting office sets while the player is inside (string 792 'So long as you hang around here, there won't be any applications!').
- It draws only for ads 1 to 3 days old (0x405314), so nobody applies on the day of the ad. Older ads are deleted.
- Each draw 0x41c25f walks the free people (status 0) of the ad's job. A person matches when PERSO A equals the ad's +0x04, the pay class of the normal pay is at most +0x0c (the class is the first index in 0x483d4c whose value is >= the pay, 9 above 4500), and, for actor and actress ads only, B equals +0x08. A match is taken with a 39 % chance (rand() % 100 > 60) and gets status 4. A person whose refusal counter is not 0 is skipped instead (6.1). The list stops at 9 people (count + 1 < 10), although it has room for 10 (checked here: code 0x41c25f..0x41c437; the casting_hired recipe note: the 11:00 draw filled 9 places).
- Original quirk: after deleting an expired ad, the loop skips the ad that moved into its slot for that hour (casting verifier, code 0x41c20b).
- Reject: status 0, +0x14 += 10. Hire: 6.5.
- With the runner's fixed LULA_CLOCK the drawn list is the same in every run (casting).

### 6.7 Training, firing, quitting and the day-end update

| Event | Effect | Evidence |
|---|---|---|
| Training (actors file card, DDF 26/27) | 300 $ per day paid at once; status 3; date +0x18..+0x20; days +0x24. The slider passes min + value and the callback adds 1 again, so value 9 gives 11 days (original quirk) | save casting_trained (11 days, 3300 $) |
| Pay +/- on a card | Steps of 100 on +0x10 | save casting_trained |
| Fire (actors file card, 0x41b3b2) | 0x41dd79 removes the person from the cast of every movie (7.1); then status 0 and +0x14 += 1 | save casting_trained; planning; checked here: code 0x41b3b2..0x41b3dd |
| Director quits | When studio +0x2c > +0x30: string 2073, status 0, +0x14 += 1 | studio; run studio_props_angry |
| Day-end update 0x41da0d | Only actors and actresses (jobs 20, 21) are checked for training and sickness. Training (status 3) is over when the days since +0x18 >= +0x24: level += those days / 5 (max 9), status 1. Sick (status 5) with the same test: level + 1 (max 9), status 1. Both clear date and days. Then one quit check covers every job: a person in status 1, 2, 3 or 5 whose pay +0x10 is below the PERSO.TAP normal pay quits when rand() % 100 > pay * 100 / normal pay. Quitting means status 0, +0x14 += 1 and string 790 '%s is frustrated and has given notice!'. A quitting actor or actress is also removed from every cast (0x41dd79 at 0x41dbb1). When a working staff member quits and exactly one colleague of the job is left, that colleague gets status 2 (not for doormen and cleaning ladies) | movie; save movie_healed; checked here: code 0x41da20..0x41dd31 |

### 6.8 PERSO.TAP

553 records of 382 bytes, read into the buffer 0x4553dc (casting): name +0; age +0x50; texts +0x54 (measurements, for example '89/73/99'), +0x7a, +0xca, +0x11a; dwords +0x72 and +0x76 (181 and 81 for Cassandra Casablanca, probably height and weight, INFERRED); job +0x16a; normal pay +0x16e; A +0x172; B +0x176; portrait +0x17a.

---

## 7. Actors and casting

### 7.1 Cast list of a movie

| Field | Offset | Movie 0 address | Notes |
|---|---|---|---|
| Lula cast | +0x1b0 | 0x455c51 | 0 / 1 |
| Cast list | +0x1b4 | 0x455c55 | Person indices; 0x140 bytes. A fresh record holds 0, not -1 (planning) |
| Cast count | +0x2f4 | 0x455d95 | |

- **Cast actors** (casting office, 0x41cea2 -> 0x41d0d4 -> 0x41d1c8): lists movies in state 1. Casting a person sets status 1 -> 2, appends to the list and increments the count (save casting_cast).
- **Cast Lula** (room 23, STANDARD_LIST_BOX_DLG 0x42036d): OK sets +0x1b0. Lula can be in only one movie in state > 1 (string 803 'Sorry, I can only act in ONE movie at a time!', 0x41fe76; patch casting_lula_busy).
  OK first clears +0x1b0 in every movie (0x4203fb). Original quirk: it then writes the flag to movie[list index] (0x420416), not to the matching state-1 movie. This only matters when a movie in another state comes first (casting verifier; checked here: code 0x4203d9..0x420423).
- **Removing a person** (0x41dd79, from firing or a pay quit): shifts the rest of the list down. The shift also copies the slot after the old end into the old last place; then -1 is written into the slot after the old end (the old count index), and the count is decremented. Cast 527, 17, 0 with count 2 becomes 17, 0, -1 with count 1; the 0 at index 1 is the old slot 2 (planning, save from planning_fire_actor; checked here: code 0x41dd8f..0x41dde1).
- **0x42568e** (studio removal) searches the list for a person, then writes -1 at cast[count] and decrements the count even when it found nothing (studio verifier, code 0x4256e0..0x4256f7).
- **Studio actor check 0x4282af** runs every tick for a studio with a movie. For every cast member not in status 2 it passes the list index, not the person, to 0x42568e, so the cast empties itself (original quirk; patch studio_idle_cast, run studio_cast_dropped ends with 2008 'Well honey, you should cast at least two actors.'). Consequence: cast actors must be in status 2. Actors in status 1, 3 or 5 lose the movie this way.
- **After shooting** the cast goes back to status 1, the list and count stay, and +0x1b0 is cleared (8.4).
- **Lease cancel** 0x418fc4 writes -1 into the cast entries but keeps the count (5.3).

### 7.2 Actors file and UI state (RAM)

[0x483d3c] job 20 or 21; [0x483d44] current actor; [0x483d40] current actress; [0x483d94] training days; [0x483d84] selected movie in the cast sheet; staff card [0x483d48] person and [0x483d38] job; hire animation [0x483d20], [0x483d7c], [0x483d80], with [0x4555fc] = 1 while it plays (casting).

### 7.3 Beauty clinic (stage 2, room 8)

| Item | Value | Evidence |
|---|---|---|
| Operation prices | 0x485c14[5]: 12000, 18000, 8000, 10000, 15000 | movie |
| Choice | 0x485c10 = radio control - 6. The sheet init does not reset it, so OK without a pick repeats the last operation (original quirk) | movie; movie verifier |
| Effect | Status 5, date +0x18 (0x4052ed), days +0x24 = price / 1000 | save movie_beauty |
| Payment | Only the actress branch subtracts the price (0x432cd1). The actor branch 0x432af5..0x432b62 never charges (original quirk) | save movie_beauty: 2 x 8000 cost 8000 |
| Refusals | Status 2: 1903/1904 'No dice, as long as he's/she's cast in a movie!'; status 3: 1905/1906; status 5: 1907/1908 | run movie_beauty_refused (1903, 1904); runs movie_beauty_clinic and movie_heal_dayend (1907); 1905, 1906 and 1908 only from code 0x432d73..0x432da4 (INFERRED) |
| Healing | Day-end update 0x41da0d (6.7): level + 1, max 9 | save movie_healed (527: 8 -> 9) |

RAM: 0x485c08 person, 0x485c0c job, 0x485bd8 HAND_ANI.

---

## 8. Movies

### 8.1 Movie record (10 records of 0x344 bytes at 0x455aa1 + 0x344*i; count 0x457b49)

Only 9 movies can be planned (cmp 9 at 0x41e500, string 602), although there are 10 records. Record -1 would be 0x45575d.

| Offset | Movie 0 | Field | Values | Evidence |
|---|---|---|---|---|
| +0x000 | 0x455aa1 | Title | 0x100 bytes, NUL-terminated | save planning_movie |
| +0x100 | 0x455ba1 | Storyline | n of string 604 + n, 43 entries | save planning_movie (3), movie_planned (5) |
| +0x104 | 0x455ba5 | Category | 0 Family, 50 Soft, 100 Hardcore. A cleared record has 100 | save planning_movie (50), movie_planned (100) |
| +0x108 | 0x455ba9 | Scene list | STORYFUC picture index per scene, -1 = empty. 16 dwords up to +0x147 are cleared to -1; at most 12 are used | save planning_done; checked here: save movie_onsale |
| +0x148 | 0x455be9 | Length in minutes | 10 per scene, max 120 (string 650) | save planning_done (30) |
| +0x14c | 0x455bed | State | 0..3 (8.2) | many saves |
| +0x150 | 0x455bf1 | Basic price per 10 minutes | 1000 in a fresh record | save planning_movie |
| +0x154 | 0x455bf5 | Total basic price, paid at Conclude (0x41f071) | | save planning_done (3000) |
| +0x158 | 0x455bf9 | Recommended shooting days = minutes / 10 | | save studio_shoot (3), movie_shooting (4) |
| +0x15c..+0x164 | 0x455bfd | Shooting start day, month, year | | save movie_shooting (9-1-1997) |
| +0x168..+0x170 | 0x455c09 | Shooting end day, month, year | | save movie_shot (10-1-1997) |
| +0x174, +0x178 | 0x455c15 | Chosen shooting days (both written) | | save studio_shoot (1, 1) |
| +0x17c | 0x455c1d | Shooting paused | 1 while the studio is not ready | studio; checked here: code 0x4241ad, 0x425744 |
| +0x180 | 0x455c21 | Unknown; no code access found | | checked here |
| +0x184 | 0x455c25 | Cutting state | 0 none, 1 being cut, 3 cut | save cutcopy_cutting, cutcopy_cutdone |
| +0x188..+0x190 | 0x455c29 | Cutting start day, month, year (0x4052ed copies 0x45569c..0x4556a4) | | save cutcopy_cutting |
| +0x194 | 0x455c35 | Cutting days = max(1, (minutes / 10) >> 2) | 80 min: 2; 20: 1; 120: 3 | save cutcopy_cutting; run cutcopy_cut_list |
| +0x198 | 0x455c39 | Sound state | 0 none, 1 being added, 3 done | save soundprops_mixing, soundprops_sounddone |
| +0x19c | 0x455c3d | Sound track | control + 0x11 of DDF 70: 0x13 Rock2, 8 Beat 1, 16 Beat 2, Disco 1, Disco 2, Pop 1, Tango (0x19 = 25), Waltz, Samba, March (0x1c) | save soundprops_mixing (25) |
| +0x1a0..+0x1a8 | 0x455c41 | Sound start day, month, year | | save movie_mixing (13-1-1997) |
| +0x1ac | 0x455c4d | Sound days = max(1, (minutes / 10) >> 2) | | save soundprops_mixing (2) |
| +0x1b0 | 0x455c51 | Lula cast | 0 / 1. Cleared when shooting ends | save casting_cast, movie_shot |
| +0x1b4 | 0x455c55 | Cast list | 7.1 | |
| +0x2f4 | 0x455d95 | Cast count | 7.1 | |
| +0x2f8 | 0x455d99 | Cast quality | 8.5 | save movie_shot (90) |
| +0x2fc | 0x455d9d | Crew quality | 8.5 | save movie_shot (62) |
| +0x300 | 0x455da1 | Studio equipment quality | 8.5 | save movie_shot (35) |
| +0x304 | 0x455da5 | Sound equipment value | written by each valuation | code 0x411aab |
| +0x308 | 0x455da9 | Cutting equipment value | written by each valuation | code 0x411ad0 |
| +0x30c | 0x455dad | Lula quality | [0x458349] if Lula is cast; always 0 | save movie_shot |
| +0x310 | 0x455db1 | Value / 1000 | written by each valuation | code 0x411afe |
| +0x314..+0x343 | | Unknown | | |

**Disagreements:**
- +0x104: base called it "story value (0x41e800)". Planning, cutcopy and warehouse call it the category, and the saves agree (Soft = 50). 0x41e800 is the category sheet's callback (DDF 9).
- +0x19c: base put the sound date here. Soundprops showed it is the track and the date is at +0x1a0 (save soundprops_mixing).
- +0x17c: base called it "shooting progress". The studio notes call it the pause flag; the code agrees (checked here).

The planning sheets work on a copy of the record at 0x45d5f8 (RAM). The current movie index is 0x483e24, the title list 0x483e2c + 100*i (10 slots), the sheet title count 0x45d93c (planning).

### 8.2 States and transitions

| From | To | Trigger | Code | Evidence |
|---|---|---|---|---|
| (none) | 0 planning | 'Write screenplay' -> New -> title -> thumbs up. Count + 1. Refused with string 602 when 9 movies exist | 0x41e45a, 0x41e912 | save planning_movie; patch planning_poor |
| 0 | 0 | Each storyboard scene adds the basic price to +0x154 and 10 minutes. It needs the money check (903) but pays nothing yet. More than 12 scenes: string 650 | 0x41efd2 | run planning_limits, planning_poor |
| 0 | 1 planning finished | 'Conclude planning' -> yes (651). Needs a writer, a designer and an assistant (all three counted in 0x484224, else 654). Pays +0x154 | 0x41f067, 0x41f071 | save planning_done |
| 1 | 2 in production | Studio START with a movie picked in the strip | 0x42359e | save studio_shoot |
| 2 | 3 shot | Hours left reach 0 at a :00 or :30 tick | 0x4247ad in 0x4242f2 | save movie_shot, patch studio_end |
| 2 (or any with a studio) | 1 | The studio's lease is cancelled | 0x418fc4 | leftovers; run leftovers_studio_lease |
| 3 | removed | Sold at the laptop or the distributor. 0x411e13 compacts the records, clears the last one (+0x104 = 100, +0x150 = 1000, scenes -1), decrements the count and the studio movie indices above it | 0x411e13 | save warehouse_sale, movie_onsale |

Cutting and sound run in parallel fields of a state-3 movie:

| Field | 0 -> 1 | 1 -> 3 |
|---|---|---|
| Cutting +0x184 | 'Cut films' (DDF 60, 0x428c1d): lists movies with state 3 and cutting 0. Needs a cutter (0x4057d2(2)) and a cutting machine. Stores the date and the days | At 10:00, 0x428e0a sets 3 when the days since +0x188 are greater than +0x194. A 2-day cut started on day 1 finishes on day 4, because the compare is a strict '>' (original) |
| Sound +0x198 | 'Add sound track' (DDF 60, 0x42de52; DDF 70, 0x42e0c4): lists movies with state 3, cutting 3 and sound 0 | At 10:00, 0x42ddff, same rule with +0x1a0 and +0x1ac |

Evidence: saves cutcopy_day3 (still 1 on day 3), cutcopy_cutdone (3 on day 4), soundprops_day3, soundprops_sounddone, movie_cutdone, movie_done.

A movie does not have to be cut or dubbed to be sold: the laptop only refuses while cutting or sound is in progress (8.7).

### 8.3 Timeline of the full chain (movie notes)

'Hot Sauna', storyline 5, Hardcore, 4 scenes, 40 minutes:

| Day | Step | Account |
|---|---|---|
| 1 | Planned, 4000 $ | 1496000 |
| 1 | Beauty operation for both actors (actress 8000 $; actor free) | 1488000 |
| 9 | Healed (527 level 8 -> 9); day ends cost 9267 $ each | 1478733 |
| 9 | Cast 527, 17 and Lula | 1478733 |
| 9 | Studio 1: Small Spotlight 120 $, Castor AX 80 150 $; 1 day instead of 4, frustration + 1 | 1478463 |
| 10 | Shot (10 hours, one 500 $ actor bonus) | 1459429 on day 11 |
| 11 | Cutting machine 1000 $, copier 10000 $; cut 1 day | 1448429 |
| 13 | Cut done at 10:00; sound machine 1200 $, Tango, 1 day | 1447229 |
| 15 | Sound done at 10:00 | 1447229 |

A forced rebuild of all 21 movie recipes gave byte-identical SAVEGAME files.

### 8.4 Studios and shooting

**Studio record** (3 records of 0x40 bytes: 0x458289 studio 1 = room 24, 0x4582c9 = room 33, 0x458309 = room 34). The current index is [0x484844] (set by the pre-call 0x423071, EDX 0/1/2) and the pointer [0x484810] (RAM).

| Offset | Studio 1 | Field | Evidence |
|---|---|---|---|
| +0x00 | 0x458289 | Camera slot (16 bytes, 9.2) | save studio_ready (55 'Castor AX 80', rating 3) |
| +0x10 | 0x458299 | Lighting slot (16 bytes) | save studio_ready (214 'Small Spotlight', rating 4) |
| +0x20 | 0x4582a9 | Lighting engineer (person) | save studio_ready (164) |
| +0x24 | 0x4582ad | Cameraman | save studio_ready (230) |
| +0x28 | 0x4582b1 | Director | save studio_ready (377) |
| +0x2c | 0x4582b5 | Director frustration | save studio_shoot (1) |
| +0x30 | 0x4582b9 | Director patience = 15 - level | save studio_ready (8 for level 7) |
| +0x34 | 0x4582bd | Movie being shot, -1 none | save studio_shoot (0), movie_shot (-1) |
| +0x38 | 0x4582c1 | Hours left = days * 10 | save studio_shoot (10) |
| +0x3c | 0x4582c5 | Shooting running | save studio_shoot (1) |

**Disagreement:** base read +0x2c as a "shooting-day counter". The studio notes show it is the director's frustration, compared with the patience at +0x30 (saves studio_shoot and the kept save of studio_props_angry: +0x2c = 9, director quit).

**Ready flag (RAM)** 0x484848[studio] = crew in status 2, both slots filled and a movie in state 1. It gates the film strip (hotspot 6, click 330,438) and START (hotspot 7, click 573,434).

**Shooting rules (studio, movie):**
- START needs at least two actors in the cast (string 2008). Cast members that are not in status 2 have already been dropped by 0x4282af (7.1). Picking the movie in the film strip sets +0x34 (0x42497f) and clears that movie from the other studios. START refuses while +0x34 = -1 (0x42353b). It opens the days sheet, which sets +0x38 = days * 10 (0x424be3), and then sets state 2, the start date and +0x3c = 1 (0x42359e..0x4235ce).
- The days sheet (0x424b50) shows the recommendation +0x158. Choosing fewer days than minutes / 10 adds 1 to the frustration. Choosing that many or more takes 1 off a frustration above 0 (checked here: code 0x424b98..0x424bcc).
- At every :00, 0x425703 decrements +0x38 of a state-2 movie that is not paused.
- At every :00 and :30 tick, 0x42408f calls 0x4242f2 once for each studio whose movie can shoot (not paused) and has no pending event (0x4848d4[studio] = 0). 0x4242f2 walks all three studios and skips paused movies. With hours left, a studio gets an event only when rand() % 100 > 93; at 0 hours its shooting ends (checked here: code 0x42428a..0x4242b5, 0x42438c..0x4243a0). 0x42408f runs on every WM_TIMER tick while [0x4555fc] = 0.
- Ending (0x4242f2, 0x425775): quality values (8.5); Lula's counter 0x458371 + 1 (max 100) if +0x1b0 = 1; state 3; +0x17c = 0; +0x1b0 = 0; end date; actors and actresses of the cast back to status 1; +0x34 = -1; +0x3c = 0. Sex toys lent to this studio are freed (store slot +0xc = -1) and lose 5 condition points (0x42435a..0x42438a). The ending does not touch the camera or the lighting. Their 100 -> 99 in movie_shot comes from the 9:00 wear roll 0x40ffa6 (9.4), which takes 1 point when rand() % 100 > 50 (code 0x4100cb..0x4100e6). The roll of day 10 left them at 100, as +0x300 = 35 at the end of shooting on day 10 shows (8.5); the roll of day 11 took the point.
- If shooting ends while the player is in the studio, the hotspot count stays 1 until re-entry (original quirk, run studio_shoot_end).

**Events (studio):**
- Event type: the per-tick accumulator 0x45533c % 100 < 50 gives an actor problem, otherwise a props wish with need rand() % 24. With the runner's fixed clock the type depends on the minute countdown 0x4556c0 at load (0 or 1: actor; 2: props; patch studio_props).
- Actor problem (DREH_MSG_STUDIO_DLG / DREH_MSG_ANYWHERE_DLG, then DREH_EVENT_ACTION_DLG): break (clock + 1 h with 0x40716d), whip, money (500 $ bonus), boot (remove from the cast), card.
- Props wish (need = catalogue +0x54): with a toy for that need already given to this studio, nothing is shown (patch studio_toys). With a free toy in the store and a working props assistant, the roll hands the toy over by itself (studio_props_store note). A free toy without a props assistant opens DREH_TOY2_DLG (shop, use from the store, decline); no toy opens DREH_TOY1_DLG (shop or decline), both from 0x4250fd. Declining adds 1 frustration at each step (0x4242f2 on the plot, 0x425509 in the studio).
- Original quirk in 0x425a1e: a wrong toy is paid and thrown away when the player answers no. Closing the shop without buying still asks 2078; yes adds 1 frustration and, while the props room is rented (0x4559e9), appends an empty slot (item -1) to the sex-toy store.

RAM: 0x4848c8[3] problem or props need; 0x4848d4[3] pending event (1 actor, 2 props); 0x4848e0 + 100*i event text; 0x484a10[3] problem person; 0x484a0c studio for DDF 30; [0x45d4d0] hotspot count (8, or 1 while shooting).

### 8.5 Quality fields (0x425775 at the end of shooting)

| Field | Formula | Example (movie_shot) |
|---|---|---|
| +0x2f8 cast | Sum of 10 * level over the +0x2f4 cast entries, divided by +0x2f4. An entry of -1 adds 0 but still counts in the divisor (checked here: code 0x4257a5..0x425803) | (90 + 90) / 2 = 90 |
| +0x2fc crew | 10 * (director + lighting + cameraman + casting director (job 1) + writer (3) + designer (10) + assistant (9) + set builder (6)) / 8 | 10 * (7+6+6+7+6+6+6+6) / 8 = 62 |
| +0x300 equipment | (10 * (camera rating * condition / 100) + 10 * (light rating * condition / 100)) >> 1. Each division truncates before the * 10, so rating 3 at 99 % gives 20, not 29 (checked here: code 0x4259d4..0x425a10) | (30 + 40) / 2 = 35 |
| +0x30c Lula | [0x458349] if Lula is cast. Nothing writes 0x458349, so it is 0 | 0 |

Other quality inputs (movie, cutcopy, soundprops):
- Sound quality 0x42dcee = (0 + 10 * sound engineer level + average 10 * dubber level) / 3. The first term is a stack slot set to 0 at 0x42dd03 and never changed (original quirk). Example 40.
- Cutter quality 0x428b83 = 10 * level of the first cutter in status 2. Example 60. An idle cutter allows cutting but adds 0 (cutcopy, from code).
- Sound equipment value 0x42ddab (example 30 with one '3 Mix 3000', rating 3) and cutting equipment value 0x428bc9 (example 20 with one 'Cut and Go', rating 2), from the slot ratings.

### 8.6 Movie value 0x411a4c

Called by the laptop sale (0x40dfd8) and by every distributor offer (0x4118d8). Steps (cutcopy, soundprops, warehouse, movie; code 0x411a4c..0x411be5):

1. +0x2fc += (sound quality if +0x198 = 3, plus cutter quality if +0x184 = 3) / 2, using an arithmetic shift. This runs again on every call, so each valuation raises the next one (original quirk).
2. +0x304 = 0x42ddab if sound is done, else 0. +0x308 = 0x428bc9 if cut, else 0.
3. +0x310 = 30 % of +0x2f8 + 15 % of +0x2fc + 15 % of +0x300 + 10 % of +0x304 + 15 % of +0x308 + 15 % of +0x30c, each term truncated.
4. If (minutes / 10) * 3 > cast count - 1 and the value is 6 or more, subtract 5. Otherwise, if the value is 94 or less, add 5.
5. Return +0x310 * 1000.

Examples: 'Hot Sauna' 27 + 16 + 5 + 3 + 3 + 0 - 5 = 49, so 49000 (run movie_sale_rights). Quality 60 everywhere, uncut and undubbed: 40000 (warehouse). The same movie cut, with cutter quality 60 and cutting equipment value 20: 47, so the rights offer was 47000 * 1.5625 = $73437 (cutcopy).

### 8.7 Sale paths and the FILMB record

**Laptop gates** (office laptop 'Put movies on sale', 0x40dfd8). The refusals are tried in the order 213, 215, 216, 218, 219:

| Gate | Test | Refusal |
|---|---|---|
| A shot movie | 0x406018(3) > 0 | 213 'You haven't got any finished movies!' |
| Copier | 0x40e3ed: copier building 0x4558d1 = 1, a copying girl (0x4057d2(5)) and 0x459329 > 0 machines | 215 |
| Warehouse | 0x40e3bf: warehouse building 0x455949 = 1 and a warehousewoman (0x4057d2(7)) | 216 |
| Not being cut | 0x40e424: a state-3 movie with +0x184 != 1 | 218 |
| No sound in progress | 0x40e467: a state-3 movie with +0x198 != 1 | 219 |

**FILMB.TMP:** an NGS pool. Header 'NGS\0', u16 0x18, u16 count, u32 0x128, u16 0x18 (14 bytes), then records of 0x128 bytes rewritten in place (warehouse, soundprops).

| Offset | Field |
|---|---|
| +0x000 | Title, copied with strcpy into an uncleared stack buffer, so the bytes after the NUL differ between builds |
| +0x100, +0x104, +0x108 | Sale day, month, year (0x4052ed) |
| +0x10c | Price code: > 0 rights sale (the amount, paid once); -1..-3 licence (n $ per unit); < -100 laptop sale, -(100 + max(1, V / 1000 >> 2)) |
| +0x110, +0x114, +0x118 | Laptop: copies sold in the 0-30, 31-60 and 61-90 day windows (-1 until the first write-back). Licence: the royalties paid in the same three windows. +0x110 replaces its -1 at the first payment; +0x114 and +0x118 add to the -1, so they end 1 low (checked here: code 0x411fc9..0x411fe1, 0x41206d, 0x4120ed) |
| +0x11c | Value V of the 0-30 day window |
| +0x120 | Value of the 31-60 day window = +0x11c / 2 |
| +0x124 | Value of the 61-90 day window = +0x120 / 2 |

0x411bff(movie, EDX = code, EBX = value) appends the record. Each of the three window values has its own roll. When rand() % 3 * 50 equals the category +0x104, +0x11c gets 30 % added, but +0x120 and +0x124 are replaced by 30 % of themselves (mov instead of add at 0x411cd8 and 0x411d33; original quirk). +0x124 is computed from the already changed +0x120. Seen: +0x120 = 6000 from 20000 (warehouse_sale), 7500 and 1125 (soundprops_sale_done), 7350 and 1102 with +0x11c unchanged at 49000 (movie_onsale).

The three sale paths (one movie can take only one, since the record is removed):

| Path | How | Money |
|---|---|---|
| Laptop (direct sales) | Code < -100. From the next day end the warehouse sells copies (8.8) | Each day the accepted orders; the window values / 30 copies per day for up to 90 days |
| Rights (distributor 'Sell movie rights', 0x45e2ac = 1) | Offer = (V + V/2 + V/16, + 30 % when rand() % 3 * 50 = category) * m, with m = rand() % 3 and 0 counting as 1. 0x411bff(EDX = offer, EBX = base) | The offer once, in the Daily costs on the day the sale is 1 day old ('Distributor royalties'). Example 115780 |
| Licences (distributor 'Grant movie licenses', 0x45e2ac = 0) | n = rand() % 4; 0 gives 2208 'isn't interested'. Else '$n per unit, to be paid weekly'. 0x411bff(EDX = -n, EBX = V) | Only on Mondays and for records younger than 91 days: the window value moves 3 % up (rand() % 100 > 70) or down, then pays (value >> 2) * n. Example (49000 + 3 %) >> 2 * 3 = 37851 |

Evidence: movie (offers 76562 and 115780 recomputed; licence 37851; FILMB write-back +0x11c = 50470, +0x110 = 37851), cutcopy ($73437), warehouse_sale, movie_onsale, movie_licensed.

**Disagreement:** the warehouse notes say the rights path calls 0x411bff "with EDX = rand%3 (0 counts as 1)". The movie notes say EDX is the offer, which the paid 115780 confirms. The code passes EDX = m * offer (checked here: 0x41197b..0x4119d3). rand() % 3 is the multiplier, not the code.

A declined offer leaves the movie record in memory with the raised +0x2fc, so the next offer is higher (76562 -> 89062 base, movie).

**Distributor busy roll** (stage 2, 0x4111e1, cutcopy): rand() % 100 > 80 sets [0x4555fc] = 1 and 0x45e2a8 = 1, stores the text 2210 at 0x45e2c0, the day and hour at 0x45e2b4/0x45e2b8 and busy hours 0x45e2bc = rand() % 4 + 3. 0x412267, the first call of the distributor setup 0x4111e1, refuses entry with string 2209 while the stored day is today and the stored hour + 0x45e2bc is above the hour; otherwise it clears 0x45e2bc (checked here: code 0x4111fd, 0x412267..0x4122be). All RAM.

### 8.8 Warehouse list and orders (RAM)

| Address | Field |
|---|---|
| 0x484bbc[100] | Pointers to 0x110-byte entries: +0 title, +0x100 expected copies q, +0x104 copies ordered so far, +0x108 copies accepted today, +0x10c price per copy |
| 0x484d4c | Entry count |
| 0x484d50 | Orders, 16 bytes each, max 50: +0 entry index, +4 customer (string 1204 Sex&Sell Inc., 1205 GreenFuck Inc., 1206 SVE Inc., 1207 BuyMe Video, 1208 Sex Attack Film&Video), +8 quantity, +0xc price |
| 0x485070 | Order count (getter 0x42ab86) |
| 0x485080 | The day's direct sales revenue (getter 0x42ab76, Daily costs control 0x14) |
| 0x48507c | Marketing level, 25 per running campaign (10.1) |
| 0x485078 | Cleared at the day end; meaning unknown |

- **Day end (0x42a1ef):** one entry per FILMB record with code < -100 and age <= 90 days. Copies = window value / 30. Then copies += copies * (0x48507c / 2) / 100 for marketing (0x42a314) and, when 0x42ab33 finds the record at chart place p (0..9), copies += (30 - p) * copies / 100 (0x42a339). Price = -code - 100 - copies / 1000, +/- rand() % 3, at least 1. Example: code -112, V 49000: 1633 copies at 11 $.
- **Hourly (0x42a0ac):** draws orders until they add up to q. Orders still open at the day end are dropped.
- **Accept (WORK_ORDER_DLG, 0x42a720):** the copier capacity test 0x42aaa8 sums catalog +0x54 of the copiers (0 -> 5000, 1 -> 10000, 2 -> 15000, 3 -> 30000). Original quirk: the order is counted twice against capacity.
- **Write-back (0x42a4b9):** after the Daily costs, the copies sold go into the FILMB window field (warehouse: +0x110 = 651).
- The room-20 paint 0x418360 draws 'Order: %d' on the warehouse building (not traced in the log).
- Examples: warehouse_orders 651 x 7 = 4557 $; movie_sale_warehouse 17883 $.

---

## 9. Equipment, props and sets

### 9.1 EQUIP.TAP catalogue

325 records of 0x68 bytes, loaded to [0x455560], count [0x4555cc]. The file has 6-byte NGS record headers.

| Offset | Field |
|---|---|
| +0x00 | Name |
| +0x50 | Category: 0 lighting, 1 camera, 2 copying machine, 3 video editing machine, 4 packing machine, 5 guns, 6 leisure, 7 stage props, 8 sound equipment, 9 35mm film, 10 sex toy (strings 939-949) |
| +0x54 | Sex toys: props need 0..23 (string 2038 + need). Copiers: capacity class |
| +0x58 | Price |
| +0x5c | Condition, 100 |
| +0x60 | 0 in the catalogue (owner in the stage-1 inventory) |
| +0x64 | Rating 1..9 |

Items the recipes use (checked here):

| Index | Name | Category | +0x54 | Price | Rating |
|---|---|---|---|---|---|
| 2 | Super Dildo Neon | 10 | 3 | 50 | 6 |
| 3 | Flutschi Kato Sato | 10 | 0 | 34 | 4 |
| 7 | Arnie II Super Power | 10 | 5 | 150 | 6 |
| 26 | Cut and Go | 3 | 0 | 1000 | 2 |
| 29 | Emil Copy 5000 | 2 | 0 | 10000 | 2 |
| 38 | Lay On Me | 6 | 0 | 2550 | 6 |
| 42 | Castle Everrandy | 7 | 0 | 21000 | 5 |
| 45 | 3 Mix 3000 | 8 | 0 | 1200 | 3 |
| 55 | Castor AX 80 | 1 | 0 | 150 | 3 |
| 178 | Pink Plush | 6 | 0 | 1289 | 2 |
| 214 | Small Spotlight | 0 | 0 | 120 | 4 |

### 9.2 Equipment slot (16 bytes)

| Offset | Field |
|---|---|
| +0x0 | EQUIP.TAP index, -1 = empty |
| +0x4 | Condition % |
| +0x8 | Catalogue rating +0x64 |
| +0xc | Sex toy store: the studio using the toy (0..2), -1 free. Other arrays: -1 |

**Disagreement:** base (and the warehouse_leisure and leftovers_s2wear notes) read +0x8 as the category, from the slot (38, 100, 6, -1). Cutcopy, studio and soundprops read it as the catalogue rating +0x64. Checked here: 'Lay On Me' (38) has category 6 and rating 6, so that slot cannot decide. 'Cut and Go' (26) has category 3 and rating 2, and the game wrote (26, 100, 2, -1) (save cutcopy_cutting). +0x8 is the rating. leftovers_s2wear patched 6 for 'Pink Plush' (rating 2); the game would write 2.

### 9.3 Arrays per department

Each array is followed by a dword holding the used-slot count. 0x406121(EAX category, EDX array, EBX slots) runs the buy/sell sheet and returns that count.

| Array | Slots | Count | Category | Room | Notes | Evidence |
|---|---|---|---|---|---|---|
| 0x458379 | 10 | 0x458419 | 8 sound | 27 | | save soundprops_mixing |
| 0x45841d | 10 | 0x4584bd | 3 cutting | 29 | | save cutcopy_cutting |
| 0x4584c1 | 200 | 0x459141 | 10 sex toys | bought at the stage-2 sex store, used by the studios | The buy sheet opens only when the props building is rented (0x4559e9 = 1). Otherwise the click shows string 2102 'You have no props department where you could store the things!' (checked here: code 0x40bb57..0x40bb82) | patch studio_toys |
| 0x459145 | 20 | 0x459285 | 7 sets | 32 'Buy sets' | 'Buy props' is never hit-tested (hotspot count 6 for 7 slots) | save soundprops_propsets |
| 0x459289 | 10 | 0x459329 | 2 copiers | 30 | | save cutcopy_copier |
| 0x45932d | 10 | 0x4593cd | unknown | | Only initialised (0x402ed8) and worn; no buy code fills it. Probably the unused packing machines (INFERRED) | leftovers, marketing |
| 0x4593d1 | 10 | 0x459471 | 5 guns | 26 | Needs a doorman, else 1702 | run marketing_reception |
| 0x459475 | 20 | 0x4595b5 | 6 leisure | 28 | | save warehouse_leisure |
| studio +0x00 | 1 | | 1 camera | 24/33/34 | | save studio_ready |
| studio +0x10 | 1 | | 0 lighting | 24/33/34 | | save studio_ready |

### 9.4 Buy, sell and wear

- **Buy** deducts the price at once (account + credit line test) and copies the 16-byte entry to slot[count]. Original quirk: when the item lands in the last slot, the count is not incremented. Reopening the sheet does not repair this: the recount reads one slot past the array (for sound that slot is the count dword itself) and reaches the capacity, and the sheet's init then sets a full count back to capacity - 1 (soundprops verifier; checked here: code 0x406200, 0x40673b).
- **Sell** pays price * (condition - 10) / 100 (cutcopy: 9000 for a 10000 $ copier). At condition 100 that is the 90 % the warehouse, soundprops and marketing notes report. It removes the slot and moves the rest up.
- **Wear** (stage 2, 9:00, after the trash roll): 0x40ffa6 runs 0x4100a7 over every array above and the camera and lighting slots of all three studios. Each item loses 1 % with a 49 % chance (rand() % 100 > 50). An item below 30 % breaks when rand() % 100 >= its condition, and 0x410194 removes it (patch leftovers_s2wear).
  Original quirk (INFERRED, code 0x4100c4..0x410126): the loop runs to the old count. After a break in any slot but the last, it reaches the emptied last slot (condition -1), which always breaks again, so the count drops by one more for every emptied slot it reaches. With two items where the first breaks, the count becomes 0 while the second item still sits in slot 0. Opening the buy/sell sheet recounts the slots and repairs the count (0x4061ed).
  Original quirk: the message is built from slot 0 after the removal. When the only item breaks it reads catalogue record -1 (run leftovers_equipment_lastbreaks shows 'Camera equipment'). The text is string 220 '%s equipment' with the category name, so it reads 'Leisure equipment equipment'.
- **End of shooting** (0x4242f2) does not touch the studio camera and light. It frees the sex toys that studio used (+0xc = -1) and takes 5 off their condition (checked here: code 0x42435a..0x424383). The 99 % that the camera and light show in movie_shot and later saves comes from the daily 9:00 wear roll; the quality written at the end of shooting still used 100 % (+0x300 = 35, save movie_shot).

### 9.5 Props room globals (RAM)

REQUISIT.TAF 0x4852bc; set builder animation 0x48533c; props talk 0x485344/0x485348 (strings 1406-1414) and 0x485310 (soundprops). The setup 0x42c28f starts the talk on rand() % 100 > 90 per entry.

---

## 10. Marketing, scandals, sabotage and other events

### 10.1 Advertising campaigns (4 records of 28 bytes at 0x4595b9)

Index 0 Lula Promotion Tour, 1 Ads, 2 Posters, 3 TV ads (DDF 0x3a controls 2-5). Started in the marketing office (hotspot 6, needs a switchboard operator, job 17).

| Offset | Field | Values |
|---|---|---|
| +0x00 | Price | 25000, 14000, 8500, 160000 |
| +0x04 | Value | 25, 15, 10, 50. Written by the init 0x402f72 and never read |
| +0x08, +0x0c, +0x10 | Start day, month, year | |
| +0x14 | Duration in days | 20, 40, 60, 45 |
| +0x18 | Running | 0 / 1. Addresses 0x4595d1, 0x4595ed, 0x459609, 0x459625 |

At the day end 0x42f2ba returns 25 per running campaign; 0x42ab22 stores it in 0x48507c (8.8). 0x42f2ba also ends a campaign when the days since its start exceed +0x14 (running = 0, date cleared); that campaign still counts on that day (checked here: code 0x42f2e6..0x42f32c). Every campaign has the same effect; only price and duration differ (marketing; save marketing_ads).

### 10.2 Scandals (10 records of 16 bytes at 0x459629)

Prepared in the marketing office (hotspot 7, needs an advertising manager, job 19). DDF 0x3b controls 2-11.

| Offset | Field | Values |
|---|---|---|
| +0x00 | Price | 35000, 28000, 64000, 12000, 80000, 15000, 70000, 30000, 10000, 180000 |
| +0x04 | Points | 5, 5, 10, 5, 15, 5, 10, 10, 5, 30 (checked here: new-game init 0x403005..0x403123, save stage2) |
| +0x08 | Prepared and paid. Never cleared, so each scandal can be used once per game | 0 / 1 |
| +0x0c | Broken (in the papers) | 0 / 1 |

- 0x42f334 at 10:00 rolls for prepared scandals and skips broken ones (+0x0c test at 0x42f367). When one breaks: newspaper headline string 1607 + index (rect 441,100,630,470), +0x0c = 1, and 0x458375 += points (save marketing_news: 0x458375 = 5). The roll walks the scandals in index order. A prepared, unbroken scandal breaks when rand() % 100 > 50 (49 %), and the roll stops after the first break, so at most one scandal breaks per day (checked here: code 0x42f34d..0x42f39c). With the fixed runner clock the first prepared scandal broke on the first roll in all 12 runs.
- 0x458375 is only read by Lula's flat (11).
- Original quirk: the newspaper code 0x43015e always calls the room-31 leave function 0x42eaf2, not the current room's. A newspaper on the plot loads BAUTEN.TAF again without freeing it, and room 20 comes back in plot mode (marketing and verifier).

### 10.3 Reception and doorman

Room 26 loads PFOERTNE.TBF and its sound only when 0x4057d2(8) > 0. 'Fit out doorman' buys guns (9.3). Guns are only worn; the 13:00 roll counts doormen, not guns (marketing).

### 10.4 Random day events (checked here)

| Event | When | Rule | Effect |
|---|---|---|---|
| Feminists or sabotage (0x43070e) | Stage 2, 13:00 | rand() % 100 > threshold, threshold = 80 (90 when the reception, building 0, is rented) + 5 * doormen. Then rand() % 100 > 50: feminists if fewer than 2 doormen; else sabotage if fewer than 4 doormen | Info box 1703 (1..4 h) or 1704 (1..7 h); the clock jumps that many hours (0x40716d) |
| Trash (0x40e4a4) | Stage 2, 9:00, not on 1-1-1997 | rand() % 100 > 90 and (buildings with flag 1, office included) / 2 > cleaning ladies in status 1, 2, 3 or 5 | Box 217 with rand() % (buildings / ladies, or buildings when there are no ladies) + 1 hours; clock jump. Then the wear roll always runs |

**Disagreement:** the marketing notes say the trash roll counts cleaning ladies "with status 1 only (0x4056a4(job 11, status 1))". Soundprops says status 1 in 0x4056a4 also matches 2, 3 and 5. The code agrees with soundprops (checked here: 0x4056f8..0x40570a).

### 10.5 Sabotage: the Hell Devils (stage 2) and the Black Cat agency (stage 3)

In stage 2, room 7 is the parking lot: the town hotspot 6 'To the parking lot' enters it (0x40a593). There the Hell Devils rockers take sabotage jobs ('Sabotage hostile companies', sheet callback 0x4314ef). Stage 3 reaches room 7 as the Black Cat agency (city hotspot 1, 0x40b3bc). The stage-2 job is kept in 0x459815..0x459834 (checked here: code 0x4312fb, 0x4313bc, 0x4314ef..0x43168c, 0x431691..0x4317b3):

| Address | Field |
|---|---|
| 0x459815 | Target company 0..4 (control - 2). Names are strings 1805 + n: Drippin' Lips, 2 Tail Hearts, Lemon Juice, Double D, Burning Heels |
| 0x459819 | Job ordered |
| 0x45981d | Hit done |
| 0x459821, 0x459825, 0x459829 | Order date |
| 0x45982d | Duration in days = strength / 10 |
| 0x459831 | Strength (Effect slider). The sheet shows strength * 10 sixpacks; the job costs strength * 500 $ (money check 0x40561d, else string 903) |

Opening the sheet clears the block and sets the strength to 1. While a job waits for its hit, the hotspot answers 1802 'We never do more than one job at once!' instead of opening the sheet. At 11:00, a waiting job hits when rand() % 100 > 50: message 1804 'The %s company has been 'visited' by the Hell Devils!', 0x45981d = 1. After the hit, entering room 7 shows 1803 'The rockers have disappeared for a while.' and returns to the town. On each later 11:00 the hook lowers the target's 10 chart sales figures (0x45974d + 4 * (10 * target + k), 14.2) by 50 * strength, or sets a figure to 0 when it is 50 or less. It clears the block when the days since the order exceed the duration. Example (run agencies_blackcat_stage2): 930 sixpacks = 46500 $, strength 93. Stage-3 store sabotage is in 13.2.

---

## 11. Lula

| Address | Field | Evidence |
|---|---|---|
| 0x455999 | Lula's flat rented (building 8 flag); room 23 is reachable | patch casting_base, movie_s2 |
| 0x458349 | Lula quality input for +0x30c. Never written; 0 in every template | movie verifier |
| 0x45834d | Lula's mood 0..100, recomputed by 0x4201f4: min(100, presents * 100 / 16 + min(100, account * 100 / 500000) / 2). Patching it has no lasting effect | checked here: code 0x4201f4..0x420285 |
| 0x458351[8] | Presents bought (1 = bought). Prices at 0x484324 (RAM, set at 0x41f70f): 500, 2500, 3800, 6000, 17000, 25000, 64000, 700000; string 814 'Do you really want to buy that?'. The price is paid at once | checked here: code 0x42062f..0x42066c |
| 0x458371 | Movies shot with Lula, + 1 per movie, max 100 | save movie_shot (1) |
| 0x458375 | Sum of the points of broken scandals | save marketing_news (5) |

Room 23 gauge 0x4842e4 (marketing): min(10, (min(100, [0x458371] * 30 / 100) + min(100, [0x458375] * 70 / 100)) / 2), from 0x4202f1. The mood function 0x4201f4 also sets that gauge (to mood / 10) and picks an animation (<= 33, <= 66, above) (checked here).

---

## 12. Stage 1

### 12.1 Clock and hours

- Day 7:00 to 23:59. The stage-1 hourly hook [0x4550cc] = 0x408a87 counts the motel rent, the film development and the FBI countdown. The day end at 23:59 jumps to 7:00 and does not run the hook for the night hours (leftovers, engine).
- `0x4090fb(n)` moves the clock n hours forward with 0x40716d and then runs the hook n times (sessions, police) (checked here: code 0x4090fb..0x40911c).
- Opening hours: table 0x450f0c (u16 from, to), read by 0x40899b(place). Video store (7) and pawnshop (4) 10-20, sex store (5) and bar (9) 10-24, the rest 0-24.

### 12.2 Motel record (0x45573c)

| Address | Offset | Field | Evidence |
|---|---|---|---|
| 0x455740 | +0x04 | Rent hours left. The hook counts it down; at 4 it sets the reminder; at 0 it sets evicted. The countdown stops while +0x30 = 2, and a brawl complaint sets it to 1, which the same hook call counts down to 0. Paying the rent +0x00 (100 $ at a new game, 0x403154) adds 17 hours, or 119 hours at the price [0x485b1c] when +0x08 is set (0x431064..0x43109e) | patch s1rentfbi (4), leftovers_s1film (300); checked here: code 0x408ae3, 0x408ad7 |
| 0x455748 | +0x0c | Rent reminder. Shown by 0x408dea as 3111, or as 3118 when +0x14 is set | engine |
| 0x45574c | +0x10 | Evicted | patch engine_s1evict |
| 0x455750 | +0x14 | The owner told the cops. Raised from 0 to 1 by the motel reception at 0x430aef, only when it is 0 (0x430aa2), 0x455799 > 30 and the account >= 8000; the same code raises the rent +0x00 to 250 %. 0x408dea never clears it, so the player is arrested again after release | patch engine_s1evict; engine verifier; checked here |
| 0x455760 | +0x24 | Brawl countdown: hours the motel owner is out of action, bought from the rocker on the parking lot (0x431a0c, string 4804 'The motel owner should be out of action for about %d hours..'; refused with 4808 while it runs). The hook counts it down | engine; checked here |
| 0x455764 | +0x28 | Brawl count | engine |
| 0x455768 | +0x2c | Brawl complaint (countdown ended with count >= 3; the hook also sets the rent hours +0x04 to 1) | engine; checked here: code 0x408ad2..0x408add |
| 0x45576c | +0x30 | Byte. 2 while the brawl countdown runs (set with it at 0x431a11, cleared at 0x408ac3 when it ends). While it is 2 the hook does not count the rent hours down, and the motel exterior and reception code test it too (0x42f476, 0x42f5a4, 0x430ec3) | engine; checked here |

### 12.3 Day check, police and FBI (0x408dea, checked here)

The day check runs from room ticks with AL = place code (town 0, motel exterior 1, motel reception 2, motel room 3, pawnshop 4, sex store 5, parking lot 6, video store 7, police 8 or 0xe, bar 9, bar toilet 0xa (room 27), 0xb (room 28), distributor 0xc). The same codes index the opening hours in 12.1. It returns 8 to change to room 8, the police cell.

| Address | Field | Notes |
|---|---|---|
| 0x455831 | FBI countdown | 170 at a new game (0x4032cf); - 1 per hour in the hook 0x408a87. At 154, 137, 120, 103, 86, 69 and 52 a video plays once (table 0x450ef0). At 51 it sets 0x455835; at 8 it sets 0x455839; at 0 it sets 0x455845. The rocker on the stage-1 parking lot (room 7) can push it back (0x431a6d, string 4806). The s1fbi note: at 40 the FBI car is in town and Dealer Don is always in the bar |
| 0x455835 | FBI in town | String 3113 'The FBI are in town and after you!' |
| 0x455839 | FBI warning | String 3116 'Do something, or else the FBI will get you in a few hours!' |
| 0x455841 | Don's identity bought | See section 4 |
| 0x455845 | Stage-1 end trigger | See section 4 |
| 0x45582d | Arrest count | After the third arrest: 3119 |
| 0x455829 | Jail hours left | 0x4090b6(hours, text) arrests only when 0x455825 <= 0 and 0x455829 = 0. It shows the text and string 980 'You're arrested and end up in the can', then sets the hours: 0x11 = 17 for the rent complaint, 0x22 = 34 for the brawl. The hook counts it down. Patch s1arrest uses 3; the kept save of engine_motel_evict had 17 (checked here: code 0x4090b6, 0x408c61) |
| 0x455825 | Hours the police leave you alone | The rocker on the stage-1 parking lot (room 7) adds hours (0x431aa5, string 4805 'The cops should leave you in peace for about %d hours..'). The hook counts it down. An arrest needs it <= 0 (checked here) |

Eviction (only when AL is not 2 or 3): text 3112, the player's items of category <= 1 or 9..10 are removed, [0x455779] = 0, then 0x4090b6(0x11, 3117) if +0x14 > 0. Then the brawl complaint gives 0x4090b6(0x22, 3120).

Other stage-1 fields from recipes: 0x455791 = 1 Lula wants a partner (s1partner); 0x45583d bird kills left, 4 at a new game, 1 means the next kill pays the 500 $ reward (s1chick); 0x455799 a counter that starts at 10, is raised by 8 or 15 in the motel session code (0x40f91e) and compared with 30 and 100 (meaning not identified; s1rentfbi patches 40).

### 12.4 Inventory (STF1DAT)

The list [0x45d940] (constructor 0x407926) holds 0x68-byte records, the same size as EQUIP.TAP. It is saved as STF1DAT.N (u32 count, then the records). Recipes cannot patch it.

| Offset | Field | Values |
|---|---|---|
| +0x00 | Name | |
| +0x50 | Category | 0 spotlight, 1 camera, 9 film, 10 sex toy, 0x64 pawnable extra, 0x65 session |
| +0x54 | Kind | Cameras: 0 photo, 2 video. Films: 1 photo, 2 video. Sessions: 0 undeveloped photos, 1 developed photos, 2 video |
| +0x58 | Value or cost | Developed photos: 20 (the fee) |
| +0x5c | Condition, or hours left to develop | Develop sheet sets 17 |
| +0x60 | Owner | 2 player, 1 pawned, 0 at the video store or in development |
| +0x64 | Count or quality | Sale price = quality * 6000 / 10 |

Sources: engine, leftovers; verified by reading STF1DAT after runs (template engine_s1photos: 'Session 1' = category 0x65, kind 1, +0x58 20, +0x5c 0, owner 0, +0x64 1).

List API (engine): 0x407a2e add, 0x407bbd remove current, 0x407b9f set current and remove, 0x408451 get, 0x4081cb count, 0x40799a reset, 0x407aad(list, EDX category or -1, EBX owner or -1) find next, 0x407a76 current, 0x407967 destroy.

- **Photo session** (motel room, 0x40f769): Lula's pose 0x45dfae >> 16 must be 2 or 3, else 3216. The film becomes a session item (category 0x65, kind 0).
- **Development**: the hourly hook counts +0x5c down for category-0x65 items with owner 0 and kind 0. At 0 it sets kind 1 and +0x58 = 20.
- **Pawnshop**: pawn = value * 50 / 100 (0x45e3a8); buy back = value * 80 / 100 (0x45e3a4). Lists 0x45e38c (pawned) and 0x45e390 (yours, categories 0, 1, 0x64).
- **Motel setup** 0x40f0ac sets flags 0x45dfe8..0x45dffc and 0x45e004 (a session exists) and 0x45e008 (number of sex toys; motel hotspot 7 when > 0). RAM.

### 12.5 Stage-1 distributor (room 3)

| Address | Field |
|---|---|
| 0x455815 | Royalty sum: a royalties sale adds 6000 * q / 10 * 32 / 100 and sets 0x45581d to the day. The first hourly hook on another day adds the whole sum to 0x455819 and keeps sum - sum * 25 / 100; when that is below 10, the sum is cleared and 0x45581d set to -1 (checked here: code 0x408c86..0x408ce4, 0x412f70..0x412f80) |
| 0x45581d | Day of month of the last royalty sale or payout, -1 for none. Only the day number is compared with 0x45569c |
| 0x455819 | Royalty money due, paid out by 'Agent' (string 5212) |
| 0x45e200 | Refusal flag (DDF 52 with 5211); RAM |
| 0x45e20c, 0x45e210, 0x45e214, 0x45e218, 0x45e21c | 1 photos / 0 video; temporary sale list; selected row; amount; 1 royalties / 0 fixed price (RAM) |

A fixed-price sale adds 6000 * q / 10 to the account; q is the item's +0x64 (engine; run engine_distributor_sale: 19698 -> 20298).

---

## 13. Stage 3

### 13.1 City

0x45d18d is the current city, 0..10 (names are strings 5376 + city: 0 Atlanta, 1 Boston, 2 Chicago, 3 San Francisco, 4 Las Vegas, 5 Miami, 6 New Orleans, 7 New York, 8 Seattle, 9 Washington D.C., 10 Los Angeles). A new game sets 10 (checked here: code 0x40331e, 0x40b6d1; 10 in every stage-3 template).

### 13.2 Stores (11 cities x 6 stores x 0xdc bytes at 0x459835)

Store i of city c is at 0x459835 + 0x528*c + 0xdc*i.

| Offset | Field | Evidence |
|---|---|---|
| +0x00 | Owner: -1 none, 0..4 rival chains, 5 the player | checked here: save store3 (the player's store has 5; code compares with 5 and -1) |
| +0x04, +0x08 | Unknown (values 0..3 and 0..2 in store3) | checked here |
| +0x0c | City | leftovers; checked here |
| +0x10 | Treatment (sabotage) kind, -1 none | leftovers |
| +0x14..+0x1c | Treatment date | leftovers |
| +0x20 | Treatment days, -1 = no end | leftovers |
| +0x24 | 15 department types | leftovers |
| +0xa4, +0xa8, +0xac | Rental date of the player's store | checked here: code 0x417797; save store3 (2-1-1997) |
| +0xb0 | Monthly rent (the player's store) | save store3 (7800) |
| +0xbc | Capital or turnover. Rival stores above 10000000 get a random treatment at the day end | save store3 (50000); leftovers |
| +0xc0, +0xd4 | Turnover fields | leftovers (sabotage changed them) |

With the runner's fixed LULA_CLOCK, the player's first store in Los Angeles is store 0 at 0x45cbc5 (rent 0x45cc75, capital 0x45cc81), and the Drippin' Lips LA store is store 3 at 0x45ce59.

**Disagreement:** the s3broke note in saves.json says "the store record of store3 starts at 0x45cc69" (the store3 note gives no address). 0x45cc69 is +0xa4 of store 0, where the rental date starts. The leftovers layout puts store 0 of city 10 at 0x45cbc5, and the store3 template agrees (owner 5 there; 0x45cc75 is +0xb0 and 0x45cc81 is +0xbc). The patched addresses are right; only the record start in the note is wrong.

Sabotage (leftovers): Black Cat -> Sabotage -> Intimidation -> store -> 8000 $ sets +0x10 = 0, the date and 7 days (save leftovers_s3sab). 0x422b6c ends a treatment at the day end when the days since its date exceed +0x20: +0x10 = -1, date cleared (patch leftovers_s3sabend).

### 13.3 Mansion and extensions

| Address | Field | Evidence |
|---|---|---|
| 0x45d0f9 | Mansion + 1 (0 = none) | save leftovers_s3mansion |
| 0x45d0fd | Price paid; not cleared by a sale | save leftovers_s3mansion (935000) |
| 0x45d101 | Weekly rent, 0 when bought | same |
| 0x45d105 | Bought with the guarantee | same |
| 0x45d109, 0x45d10d, 0x45d111 | Lease date | same |
| 0x45d13d[5] | Extensions (pool, fence, ...): 0 none, 1 being built, 3 finished. The 9:00 hook 0x419a84 finishes them | patch leftovers_s3mansion |
| 0x45d151 + 12*i | Extension start dates | leftovers |
| 0x483c94[i] | Extension days (RAM table) | leftovers |

The mansion table is 0x483ad4 + 12*i (rent, buy price, ...). The pool costs 20000 $ and takes 7 days. Selling (0x416c3c..0x416d53): offer = price + 0x417a18() - (rand() % 35) %. Without the guarantee an offer comes only on rand() % 100 > 70 (else 511). A lease date gives 513. Accepting clears 0x45d13d[5], 0x45d0f9 and 0x45d105 and adds the offer. Original quirk: the offer is based on 850000, not the 935000 paid (run leftovers_mansion_sale: $861300).

### 13.4 Parties

From the s3party recipe: 0x45d191 party frequency (5 = weekly; the strings 256-261 run Yearly, Half-yearly, Every two months, Monthly, Every two weeks, Weekly), 0x45d195 buffet (20), 0x45d199 girls (36). 0x45d19d/0x45d1a1/0x45d1a5 is not the last party date: the OK of the party sheet writes today's date there (0x410a0c). The stage-3 10:00 hook 0x410c04 holds a party when buffet or girls is not 0 and the days since that date are a multiple of the period (frequency 0..5 = 360, 180, 60, 30, 14, 7 days). A party costs buffet * 200 + girls * 1000 $, shows string 272 'There's a party at your house this evening.' and adds 1 to the party counter 0x45d1a9 (checked here: code 0x410c04..0x410cbb; run office_stage3_party: account 1650000 -> 1610000 = 20 * 200 + 36 * 1000). The names buffet and girls come from the recipe note.

### 13.5 Other stage-3 fields

0x45d0ed, 0x45d0f1 and 0x45d0f5 are flags (checked here). 0x45d0ed: central warehouse bought. With 10 or more stores the realtor asks string 526 'For more than 10 stores, you need a central warehouse. Buy it for $500,000?'; yes sets it and takes 500000 $ (0x416c20, 0x416e41). 0x45d0f1: the lady realtor's first visit is done. Setup 0x4168e0 calls 0x41730e(0) once and sets it; it is 1 in every stage-3 template. 0x45d0f5: Lula's present in the stage-3 office is opened. 0x410460 sets it; while it is 0, the office shows string 254 and string 255 'Don't run away honey, you have to open my present first!'. It is 0 in s3office and 1 in s3b and store3.

---

## 14. Other globals

### 14.1 Flags outside the saved block (RAM)

| Address | Meaning | Evidence |
|---|---|---|
| [0x4555e4] | Quit or leave the session | B.10 |
| [0x4555f4] | Staff icon at 40,380. Set by room 22 setup when a planning job has more than one person, by the office setup (more than one secretary) and by hiring a second casting director; cleared by ChangeRoom. A click on its pixels sets the hovered hotspot 0x45d4d4 = 0xff | planning |
| [0x4555fc] | Shared pause flag, 15 writers (18 references with the 3 reads at 0x405263, 0x4115b9, 0x4201ae): the hire animation, the studio equipment sheet, the distributor busy roll and others. While it is set the studio tick 0x42408f is skipped | casting, studio, cutcopy; checked here: code 0x405263 |
| [0x45533c] | Per-tick accumulator written by 0x40a347. Used as a random source (studio events, building sale, interest rates at load) | studio |
| 0x4553a8 / 0x4553b0 | Left / right click of this frame (latched by 0x436304/0x436336, copied by 0x436156) | soundprops |
| 0x45d1b0 + 16*i, 0x45d4dc + 4*i, [0x45d4d0], [0x45d4d4] | Hotspot rects, label ids, count, hovered index | engine |
| [0x45535c] | Room music id. 0x40566e stores it, and the clock 0x40a1f0 restarts that track when music channel 8 has stopped (0x40a2f7) | cutcopy; checked here: code 0x405679, 0x40a2c5..0x40a33a |

### 14.2 Video charts (in the saved block)

| Address | Field |
|---|---|
| 0x4596c9 | 10 entries of 12 bytes: +0 title id, +4 group, +8 sales. For a built-in title, +0 is its index 0..49 in 0x45974d, +4 = index / 10 is its company (string 1805 + n) and the title is string 2103 + index ('Do It Again Sam', ...). +4 = 5 marks a player's laptop film: +0 is then its FILMB record index and +8 is copies sold * 30 / days (31-60 days: +0x114 * 60 / days, 61-90: +0x118 * 90 / days). Only records with code < -100, age 1..90 days and +0x110 > 0 take part (checked here: code 0x40c094..0x40c0c6, 0x40c265..0x40c304) |
| 0x45974d | 50 dwords, sales figures of the built-in titles |
| 0x4596b9, 0x4596bd | Not chart fields: the price (180000) and points (30) of scandal 9 (0x459629 + 16 * 9, see 10.2), set by the new-game init (0x403119, 0x403123) |

The charts are rebuilt by 0x40bfed at every stage-2 load and on the day end into a Monday. 0x42ab33 looks for an entry with +4 = 5 and the given record, for the warehouse chart term (checked here: code 0x40c094..0x40c0c6, 0x42ab33; saves stage2, movie_s2). The movie notes add that a laptop record enters the charts only with copies * 30 / days above the top-10 values, about 90000; 1633 copies a day cannot reach that.

### 14.3 State that no save can set (critic)

Some code depends on state outside the save files. The critic notes list how to reach it:

| Code | What it needs |
|---|---|
| CD dialog 0x402288, Browse 0x402510, GetOpenFileNameA 0x44cf60 | A CDROM.LOC naming a missing directory (for example `X:\NOWHERE\`) at the root of the `--save` overlay directory. Browse is at 225,167 and End Program at 333,167. Recipes cannot write CDROM.LOC |
| DST transition 0x4493bb | Start clock on an EST5EDT switch day (1997-04-06 or 1997-10-26) through LULA_CLOCK or LULA_SCENARIO_CLOCK. The runner variable changes rand() and forces all templates to be rebuilt |
| Slider drag 0x4158f3, 0x415efc | Reachable now. The runner has `down X Y` and `up X Y` script ops, and a `move` while the button is down carries it in wparam. Scenario office_slider_drag covers both (run office_slider_drag: 0x4158f3 188 hits, 0x415efc 107) |
| TZ parsing 0x4496c8, 0x4496ec, 0x449815, 0x448fdf | A TZ entry in the runtime's fixed environment block (src/runtime/win32/kernel32.c) |
| Display paths 0x44287d, 0x444e52 | A 15-bit display or a pitch other than 1280 reported by the runtime |
| Debug 0x443196, 0x448a4c, 0x448a5d | The environment variable NGS-REVEAL |
| 22 error paths | A failing host API, a CPU exception, a stack overflow or a missing string resource |

---

## 15. How to patch a save

### 15.1 Recipe format

Recipes live in `tests/scenarios/saves.json` and `tests/scenarios/saves/*.json` (one file per area; a name may be defined once). `python3 tools/scenarios.py saves NAME` builds `build/scenarios/saves/NAME`.

```json
"my_movie": {
  "from": "prod_s2staff",
  "note": "One shot movie, slow clock.",
  "patch": {"0x457b49": 1, "0x455aa1": "My Movie\u0000", "0x455be9": 40,
            "0x455bed": 3, "0x4556bc": 30, "0x4556c0": 0},
  "check": {"slot": 4, "stage": 2, "room": 20, "name": "ProdAll",
            "0x457b49": 1, "0x455bed": 3}
}
```

| Key | Meaning |
|---|---|
| `from` | Parent template, or `null` for an empty save directory |
| `script`, `seconds` | Input lines (a list, or a file in `tests/scenarios/saves/`). The game runs on a copy of the parent and quits at `seconds`. A script saves through F2 -> Save game -> slot -> Save -> name -> RETURN |
| `slot` | The slot `patch` writes to (default 4) |
| `copy_slot` | `[{"from": 2, "to": 4}]` copies SAVEGAME and STF1DAT, not FILMB, before the patch |
| `patch` | `{"0xADDRESS": value}`. An int is written as a little-endian dword; negative values work. A string is written as raw latin-1 bytes; add `\u0000` yourself |
| `check` | `slot`, `stage`, `room`, `name`, `account`, `day`, `month`, `year`, `hour`, `minute`, and `"0xADDRESS": dword` (read as signed). It may be a list of such objects (several slots) |

Build order: copy the parent, run the script, delete W_DEBUG.DAT, copy slots, patch, check. A failed build stays in `build/scenarios/saves/.failed-NAME` with its log in `.logs/NAME.txt`.

### 15.2 Address formulas

File offset = address - 0x455620. The runner refuses a patch outside the file.

| Data | Address |
|---|---|
| Staff person i, field f | 0x45d1ad + 40*i + f (pseudo address, after the guest block) |
| Building i, field f | 0x455849 + 40*i + f |
| Movie i, field f | 0x455aa1 + 0x344*i + f |
| Job ad j, field f | 0x457b4d + 0x24*j + f |
| Studio k, field f | 0x458289 + 0x40*k + f |
| Slot j of an equipment array A, field f | A + 16*j + f |
| Campaign j / scandal j | 0x4595b9 + 28*j + f / 0x459629 + 16*j + f |
| Store i of city c, field f | 0x459835 + 0x528*c + 0xdc*i + f |

### 15.3 Rules that keep a patched save consistent

- **Clock:** set `0x4556c0` to 0 together with any clock patch (the countdown was the only byte that differed between two builds of the same script, and it decides the studio event type). Patch the weekday 0x4556b8 together with the day. Months have 30 days.
- **Slow clock:** 0x4556bc = 30 makes one game minute last 31 ticks of 60 ms, so a game hour takes about 112 s. Hourly events (the 13:00 sabotage box, 10:00 reports, applicant draws) still fire, but only when a script runs past the next full hour. Restore 3 (stage 2; stage 3 starts with 2) when the scenario needs the normal speed.
- **Stage 2 money:** keep the account below 2000000, or the next day end moves to stage 3. The credit line cannot be patched (1.4).
- **Counts:** when you fill a table, set its count too: movies 0x457b49, job ads 0x458255, applicants 0x458281, each equipment array's count dword.
- **Equipment slots:** write all four dwords. Use the catalogue rating at +8, not the category.
- **Staff:** patch status +4 only to values the game uses. Cast members must be in status 2 before a studio uses the movie (7.1). Studio crew must be in status 2 and also be stored in the studio record (+0x20/+0x24/+0x28).
- **Movies:** a movie for a studio needs state 1 and a cast; for cutting, sound or a sale it needs state 3. A fresh record has category 100 and price 1000 per 10 minutes.
- **Events at 9:00:** a day end into a day other than 1-1-1997 runs the trash roll; its clock jump can skip the 10:00 hooks. The recipes avoid it by patching the date instead of running a day end (marketing_news).
- **Fixed clock:** the runner starts the game with a fixed LULA_CLOCK, so rand() repeats. Changing LULA_SCENARIO_CLOCK changes every random result and forces all templates to be rebuilt.
- **Not patchable:** STF1DAT, FILMB, and all RAM-only state (1.4). Reach those through a script.
- **Room 20:** a save made on the plot loads in plot mode; one click per building enters it.

---

## 16. Disagreements and corrections

| Topic | One side | Other side | Resolution |
|---|---|---|---|
| Equipment slot +0x8 | base, warehouse_leisure and leftovers_s2wear notes: category | cutcopy, studio, soundprops: EQUIP.TAP +0x64 rating | Rating. 'Cut and Go' (category 3, rating 2) is saved as 2 (save cutcopy_cutting; checked here) |
| Movie +0x104 | base: "story value (0x41e800)" | planning, cutcopy, warehouse: category 0/50/100 | Category (save planning_movie) |
| Movie +0x19c / +0x1a0 | base: sound date at +0x19c | soundprops: +0x19c track, date at +0x1a0 | Soundprops (save soundprops_mixing) |
| Movie +0x17c | base: shooting progress | studio: paused | Paused (checked here: code 0x4241ad, 0x42428a, 0x425744) |
| Studio +0x2c | base: shooting-day counter | studio: director frustration, patience at +0x30 | Frustration (saves studio_shoot, studio_props_angry) |
| Job ad layout | base: +4 job, +0xc level threshold (INFERRED) | casting: +0 job, +4 A, +8 B, +0xc pay class | Casting (save casting_ads) |
| Applicant draw hours | base: odd hours 9-17 | casting: 11, 13, 15, 17 | Casting (checked here: code 0x405123..0x40514b) |
| Staff +0x14 | base: must be 0 to apply (INFERRED) | casting: refusal and firing counter | Casting (saves casting_hired, casting_trained) |
| Stage-3 switch | base: cmp at 0x404b75 | prod.json note: 0x40528b | 0x404b75 (checked here) |
| 13:00 roll | base: 13:00 | marketing: hours 1 and 13 | Both are in the code; hour 1 never comes in stage 2 |
| Trash roll ladies | marketing: status 1 only | soundprops: 0x4056a4 status 1 also matches 2, 3, 5 | Soundprops (checked here) |
| Rights sale call | warehouse: EDX = rand%3 | movie: EDX = offer | EDX = multiplier * offer (checked here: 0x41197b..0x4119d3) |
| Store record start (store3) | s3broke note in saves.json: 0x45cc69 (+0xa4 of store 0) | leftovers layout: 0x45cbc5 for store 0 of city 10 | Leftovers (checked here: save store3) |
| Sound quality term | movie: "a stack slot that is never set" | movie verifier: set to 0 at 0x42dd03 and never changed | Same result; the verifier is exact |
| Value bonus | movie: "else +5" | cutcopy, soundprops, movie verifier: +5 only when the value is <= 94 | <= 94 (code 0x411bd7) |
| Lula flag on Cast Lula | casting: sets the flag of the selected state-1 movie | casting verifier: writes movie[list index] (0x420416) | Verifier |
| Unidentified region 0x4596c9..0x45980f | base: unidentified simulation data, changes in every save | | Video charts (0x4596c9..0x459814, 14.2). The room-7 job block starts after the region, at 0x459815. The region changes because the charts are rebuilt at every stage-2 load (checked here) |
| Credit line | planning, marketing: 70000 in stage 2 | | True after every load; INIT_STUFE rewrites it (checked here) |
