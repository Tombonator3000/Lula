# Scripted scenarios

Each `*.txt` file here drives the recompiled game (`build/game/lula`) headless
with scripted mouse and keyboard input, and is judged by what the game writes
to its log. The scenarios came from exploring the game area by area (agencies,
bank and realtor, districts, office, simulation). `issues/` holds repros of
known runtime problems; they are not part of the default run.

## Running

```sh
python3 tools/scenarios.py run                    # every scenario, 3 at a time
python3 tools/scenarios.py run 'office_*' bank_realtor_bank_ok -j 2
python3 tools/scenarios.py run issues/NAME        # a repro from issues/
python3 tools/scenarios.py list                   # headers, check counts, problems
python3 tools/scenarios.py recheck                # judge the last logs again (after editing checks)
python3 tools/scenarios.py coverage               # tools/coverage_report.py over all runs
python3 -m unittest tests.test_scenarios          # quick subset (LULA_SCENARIOS=all for all)
```

`run` first builds the save templates the selected scenarios need, then runs
each scenario in `build/scenarios/runs/NAME/`: `input.txt` (the script with a
`quit` appended at `# seconds`), `log.txt` and `NAME.cov` (LULA_COVERAGE).
The copied save directory is deleted after the run unless `--keep` is given.
`--frames MS` dumps a frame every MS milliseconds into `runs/NAME/frames`
(one scenario at a time; delete the frames afterwards, they are large).
The table at the end lists every scenario with its result, wall time and the
first failing check. `PASS*` means a `# flaky:` scenario passed on its retry.

The game runs with `LULA_HEADLESS=1 LULA_MSGBOX_AUTO=1 LULA_LOG=3` and a fixed
wall clock, `LULA_CLOCK=1997-01-01T08:00:00` (override with
`LULA_SCENARIO_CLOCK`). The game seeds `rand()` from `time()` once at start-up,
so the fixed clock makes its random events repeatable: the random city data in
the save templates (the stores the realtor offers, where the rival stores
sit, rents), the bar guests, the flight events and so on come out the same in
every run. Several scenarios are tuned to that data (click positions, amounts
in `# check:` lines), so another clock value means rebuilding the templates
(their stamps include the clock) and retuning those scenarios. The runner drops the
per-read `ReadFile`/`SetFilePointer` trace lines from the log and keeps each
distinct `DrawTextA` line once per script step (the game redraws its texts
every frame), so `log.txt` stays small and still shows every text the game
drew, in order.

Script times are wall-clock milliseconds since the start of the process, while
the game itself slows down when the machine is busy. Leave a few seconds of
slack between steps and before `# seconds`.

## Scenario format

```
# seconds: 72
# expects: what a good run shows, step by step (for people)
# save: stage2
# note: "Stage2" is the Load-dialog entry at 300,119 (slot 4)
# check: DialogBoxParamA\(EINZAHLEN_DLG\)
# check: DrawTextA\("-43000 \$"
# reject: REGEX
# allow: REGEX
# flaky: why this scenario can still fail now and then
# random: notes about random events
# any other comment
11500 move 505 40
12000 click 505 40
36000 type 5000
37500 key RETURN
```

- `# seconds: N` (required): the runner appends `N*1000 quit`.
- `# save: NAME` (required): a recipe in `saves.json`, or `none` for an empty
  save directory (the game starts at the main menu either way).
- `# check: REGEX` (one or more): each must match somewhere in `log.txt`
  (Python `re`, searched over the whole log). Choose lines that prove the
  scenario reached what `# expects:` describes: Win32 dialogs
  (`DialogBoxParamA\(GAME_IO_DLG\) -> 1`, `dialog STANDARD_GET_TXT_DLG`),
  files the game opens (`CreateFileA\(DATA\\ANI\\BAUTEN\.TAF`), texts it draws
  (`DrawTextA\("The advertising budget is now \$35000`), popup menus
  (`TrackPopupMenu at 100,40 -> command 100`), saves it writes
  (`CreateFileA\(DATA\\SAVE\\SAVEGAME\.  1, write`).
- `# reject: REGEX`: must not match anywhere (regression tests).
- `# allow: REGEX`: a `lula[warn]` line matching it does not fail the run.
- `# flaky: reason`: the runner retries a failed run once.
- Input lines: `<ms> move X Y | click X Y | rclick X Y | key NAME | type TEXT |
  dump | quit`, times in ascending order (see the comment at the top of
  `src/runtime/platform_sdl.c`). `type` takes the rest of the line.

A run fails on a non-zero exit status, a signal, a timeout (the game did not
quit 30 s after `# seconds`), any `lula[warn]`, `lula[trap]` or `lula[fatal]`
line not covered by `# allow:`, a missing check or a matched reject. Only
`# flaky:` scenarios get a second try.

When a modal dialog, message box or popup menu closes, the runtime posts a
`WM_MOUSEMOVE` at the current cursor position, as Windows does. A click that
needs the cursor somewhere else therefore needs its own `move` first.

## Adding a scenario

1. Pick or add a save recipe (below), or use `# save: none`.
2. Write the input lines. To see what the game shows, run it once with frames:
   `python3 tools/scenarios.py run NAME --frames 1000 --keep`, look at
   `build/scenarios/runs/NAME/frames`, then delete them.
3. Read `build/scenarios/runs/NAME/log.txt` and add `# check:` lines for the
   steps that matter; `recheck NAME` tries them on that log without a new run.
   Texts are often padded with spaces, so write `DrawTextA\("\s*Text\s*"`.
4. Run it a few times, also next to other runs (`-j 3`), and give it enough
   slack to pass every time.

## Save recipes (`saves.json`)

Recipes live in `saves.json` and in any `saves/*.json` (one file per area, so
areas can add recipes without touching each other's files; a name may be
defined only once across all files).

`python3 tools/scenarios.py saves [NAME ...] [--force] [-j N]` builds templates
into `build/scenarios/saves/NAME` in dependency order, several at a time. A
template is rebuilt when its recipe, its script file or its parent changed
(`NAME.stamp` holds the hash); `--force` rebuilds the named ones (all when no
name is given). A failed build is left in `build/scenarios/saves/.failed-NAME`
with its log in `build/scenarios/saves/.logs/NAME.txt`.

```json
"stage2": {
  "from": "stage1_goal",
  "note": "what the template is",
  "script": ["11500 move 505 40", "12000 click 505 40", "..."],
  "seconds": 40,
  "check": {"slot": 4, "stage": 2, "room": 5, "name": "Stage2"}
},
"stage2_rich": {
  "from": "stage2",
  "patch": {"0x45568c": 2500000, "0x4556a8": 18, "0x4556ac": 55},
  "check": {"slot": 4, "stage": 2, "account": 2500000}
}
```

- `from`: the parent template, or `null` for an empty save directory.
- `script`: input lines as a list, or a file name in `tests/scenarios/saves/`.
  The game runs on a copy of the parent with this input and quits at
  `seconds`. Scripts save through F2 (or the motel-room suitcase) -> Save game
  -> slot -> Save -> name -> RETURN.
- `slot`: the slot `patch` applies to (default 4).
- `patch`: `{"0xADDRESS": value}`. An integer is written as a little-endian
  dword, a string as raw bytes (`"TestMovie\u0000"`). The SAVEGAME file is the
  guest memory block from 0x455620, so the file offset is address - 0x455620
  (0x45568c account, 0x455690 room, 0x455694 stage, 0x45569c day, 0x4556a0
  month, 0x4556a8 hour, 0x4556ac minute, 0x4556b8 weekday, 0x4556bc minutes
  per tick).
- `copy_slot`: `[{"from": 2, "to": 4}]` copies `SAVEGAME.  2` and
  `STF1DAT.  2` to slot 4 (applied before `patch`).
- `check`: `{"slot", "stage", "room", "name", "account", "day", "month",
  "year", "hour", "minute"}` plus `"0xADDRESS": dword` keys (or a list of
  such objects), asserted at the end.

Build steps: copy the parent, run the script, delete `W_DEBUG.DAT`, copy
slots, patch, check. Slots and their Load-dialog entries: slot 1 at (300,81),
slot 2 at (300,94), slot 3 at (300,107), slot 4 at (300,119); the files are
`DATA/SAVE/SAVEGAME.  N` and `DATA/DATABASE/STF1DAT.  N` (two spaces).

The two main chains are `s1base` (a new game saved in the town, slot 4) with
`stage1_goal -> stage2 -> stage2_rich -> stage3`, and `office1` (a new game
saved in the rented motel room, slot 1) with `s1flag -> s2cast -> s2lula` and
`s1flag3 -> s3office -> s3b -> store3`, where each step saves into the next
slot. A full build from scratch takes about five minutes with `-j 3` (the
office chain office1 -> s3office -> s3b -> store3 is the critical path).
