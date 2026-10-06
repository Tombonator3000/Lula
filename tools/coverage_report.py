#!/usr/bin/env python3
"""Merge LULA_COVERAGE files from runs of build/game/lula and report which
recompiled functions have run, per module of WET.EXE.

    LULA_COVERAGE=build/cov/a.cov build/game/lula ...      (one file per run)
    python3 tools/coverage_report.py build/cov/*.cov [--uncovered MODULE]

Module ranges and the list of dead functions come from
docs/recomp/specs/resources-and-game-map.md (B.2) and
docs/recomp/specs/code-discovery.md (Appendix B).
"""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

MODULES = [
    (0x401010, 0x4037e3, 'startup, init, shutdown'),
    (0x4037e4, 0x403d09, 'constructors, WinMain'),
    (0x403d0a, 0x4058c7, 'frame loop, hotspots, rooms'),
    (0x4058c8, 0x40a01e, 'shared game services'),
    (0x40a01f, 0x43371c, 'rooms, dialogs, simulation'),
    (0x43371d, 0x4337db, 'compiler support'),
    (0x4337dc, 0x434c53, 'window, DirectDraw'),
    (0x434c54, 0x435400, 'file/memory manager'),
    (0x435401, 0x435c83, 'TFF loader'),
    (0x435c84, 0x4363ff, 'mouse/keyboard'),
    (0x436400, 0x4365d7, 'error object'),
    (0x4365d8, 0x439032, 'sound system'),
    (0x439033, 0x439e2b, 'NGS pool writer'),
    (0x439e2c, 0x43a98b, 'video (on hold)'),
    (0x43a98c, 0x43e527, '2D graphics engine'),
    (0x43e528, 0x442430, 'image/sound file library'),
    (0x442431, 0x4428df, 'file I/O wrappers'),
    (0x4428e0, 0x443417, 'Watcom C runtime'),
    (0x443418, 0x443fe7, 'MIDI/CD audio'),
    (0x443fe8, 0x445d97, 'blitters'),
    (0x445d98, 0x44cfa0, 'Watcom C runtime 2'),
]


def module_of(addr):
    for lo, hi, name in MODULES:
        if lo <= addr <= hi:
            return name
    return 'other'


def dead_functions():
    spec = ROOT / 'docs/recomp/specs/code-discovery.md'
    if not spec.is_file():
        return set()
    text = spec.read_text(encoding='utf-8')
    start = text.find('## Appendix B. Dead recompiler entries')
    end = text.find('\n## ', start + 10)
    dead = set()
    for line in text[start:end].splitlines():
        found = re.findall(r'`0x([0-9a-f]{6})`', line)
        # A note names the dead entry first and then the reason, e.g.
        # "`0x44c478` appears dead only because `0x44c3c3` is blacklisted";
        # the address in the reason is live code.
        if line.startswith('Note:'):
            found = found[:1]
        dead.update(int(m, 16) for m in found)
    return dead


def all_functions(gen_dir):
    report = gen_dir / 'gen_tables.c'
    return [int(m, 16) for m in re.findall(r'\{0x([0-9a-f]{8})u, f_', report.read_text())]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='+')
    ap.add_argument('--gen', default=str(ROOT / 'build/game/gen'))
    ap.add_argument('--uncovered', help='list uncovered live functions of this module')
    ap.add_argument('--json', help='write the summary as JSON')
    args = ap.parse_args()
    covered = set()
    for f in args.files:
        for line in Path(f).read_text().splitlines():
            if line.strip():
                covered.add(int(line.split()[0], 16))
    funcs = all_functions(Path(args.gen))
    dead = dead_functions()
    live = [a for a in funcs if a not in dead]
    rows = {}
    for a in live:
        m = module_of(a)
        r = rows.setdefault(m, [0, 0])
        r[1] += 1
        r[0] += a in covered
    total = sum(r[0] for r in rows.values()), sum(r[1] for r in rows.values())
    print(f'{"module":32} {"run":>5} {"live":>5}   share')
    for lo, hi, name in MODULES + [(0, 0, 'other')]:
        if name in rows:
            c, n = rows[name]
            print(f'{name:32} {c:5} {n:5}   {100 * c / n:5.1f} %')
    print(f'{"total (live functions)":32} {total[0]:5} {total[1]:5}   {100 * total[0] / total[1]:5.1f} %')
    if args.uncovered:
        print()
        for a in live:
            if module_of(a) == args.uncovered and a not in covered:
                print(f'{a:#x}')
    if args.json:
        Path(args.json).write_text(json.dumps({
            'covered': total[0], 'live': total[1],
            'modules': {k: {'run': v[0], 'live': v[1]} for k, v in rows.items()}}, indent=2) + '\n')


if __name__ == '__main__':
    main()
