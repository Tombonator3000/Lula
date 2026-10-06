#!/usr/bin/env python3
"""Build save templates and run the scripted scenarios in tests/scenarios/.

    python3 tools/scenarios.py saves [NAME ...] [--force] [-j N]
    python3 tools/scenarios.py run [NAME|GLOB ...] [-j N] [--keep] [--frames MS]
    python3 tools/scenarios.py list [NAME|GLOB ...]
    python3 tools/scenarios.py recheck [NAME|GLOB ...]
    python3 tools/scenarios.py coverage [coverage_report.py options]

Save templates are described in tests/scenarios/saves.json and built into
build/scenarios/saves/NAME. Runs go to build/scenarios/runs/NAME/ (log.txt,
NAME.cov, input.txt; the copied save dir is deleted unless --keep).
See tests/scenarios/README.md for the scenario and recipe formats.
"""
import argparse
import concurrent.futures
import fnmatch
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('LULA_BINARY', ROOT / 'build/game/lula'))
SCEN_DIR = ROOT / 'tests/scenarios'
RECIPES = SCEN_DIR / 'saves.json'
OUT = ROOT / 'build/scenarios'
SAVES = OUT / 'saves'
RUNS = OUT / 'runs'

# Every run starts the guest wall clock here (kernel32 GetLocalTime). The game
# seeds rand() from time() once at start-up, so a fixed start clock makes the
# random events repeatable.
CLOCK = os.environ.get('LULA_SCENARIO_CLOCK', '1997-01-01T08:00:00')
GRACE = 30          # seconds after the scripted quit before a run is killed
SAVE_BASE = 0x455620
BAD_LINE = re.compile(r'lula\[(warn|trap|fatal)\]')

HEADER = re.compile(r'#\s*(seconds|save|check|reject|allow|covers|exit|flaky|expects|random|note)\s*:\s?(.*)$')
INPUT_LINE = re.compile(r'^\d+\s+(move|click|rclick|down|up|key|type|dump|quit)\b')

_print_lock = threading.Lock()


def say(*args):
    with _print_lock:
        print(*args, flush=True)


# ------------------------------------------------------------------ scenarios
class Scenario:
    def __init__(self, path):
        self.path = Path(path)
        self.name = self.path.stem
        self.seconds = None
        self.save = None
        self.checks, self.rejects, self.allows = [], [], []
        self.covers = []            # function entries that must have run
        self.exit = 0               # expected exit status when the game ends itself
        self.flaky = None
        self.lines = []
        self.errors = []
        for n, raw in enumerate(self.path.read_text(encoding='utf-8').splitlines(), 1):
            line = raw.rstrip()
            if not line.strip():
                continue
            if line.startswith('#'):
                m = HEADER.match(line)
                if not m:
                    continue
                key, val = m.group(1), m.group(2).strip()
                try:
                    if key == 'seconds':
                        self.seconds = int(val.split()[0])
                    elif key == 'save':
                        self.save = val.split()[0] if val else None
                    elif key == 'check':
                        self.checks.append(re.compile(val))
                    elif key == 'reject':
                        self.rejects.append(re.compile(val))
                    elif key == 'allow':
                        self.allows.append(re.compile(val))
                    elif key == 'exit':
                        self.exit = int(val.split()[0], 0)
                    elif key == 'covers':
                        self.covers += [int(x, 16) for x in val.replace(',', ' ').split()]
                    elif key == 'flaky':
                        self.flaky = val or 'yes'
                except (ValueError, re.error) as e:
                    self.errors.append(f'line {n}: bad "# {key}:" ({e})')
                continue
            if not INPUT_LINE.match(line):
                self.errors.append(f'line {n}: not an input line: {line[:60]}')
                continue
            self.lines.append(line)
        if self.seconds is None:
            self.errors.append('missing "# seconds: N"')
        if not self.save:
            self.errors.append('missing "# save: NAME|none"')
        last = 0
        for line in self.lines:
            t = int(line.split()[0])
            if t < last:
                self.errors.append(f'input times go backwards at "{line}"')
                break
            last = t
        if self.seconds is not None and last >= self.seconds * 1000:
            self.errors.append(f'input line at {last} ms is after "# seconds: {self.seconds}"')

    @property
    def save_name(self):
        return None if self.save in (None, 'none') else self.save


def scenario_paths(patterns):
    """All tests/scenarios/*.txt (not issues/), or the ones matching names, globs or paths."""
    default = sorted(SCEN_DIR.glob('*.txt'))
    if not patterns:
        return default
    out = []
    for pat in patterns:
        p = Path(pat)
        if p.suffix == '.txt' and p.is_file():
            out.append(p.resolve())
            continue
        stem = pat[:-4] if pat.endswith('.txt') else pat
        if '/' in stem:   # e.g. issues/NAME or issues/*
            hits = sorted(SCEN_DIR.glob(stem + '.txt'))
        else:
            hits = [q for q in default if fnmatch.fnmatchcase(q.stem, stem)]
        if not hits:
            raise SystemExit(f'no scenario matches {pat!r}')
        out.extend(hits)
    seen, uniq = set(), []
    for p in out:
        if p not in seen:
            seen.add(p)
            uniq.append(p)
    return uniq


# --------------------------------------------------------------------- game
def filter_log(raw, dest):
    """Copy a LULA_LOG=3 log without the per-read file trace lines and with
    each distinct DrawTextA and TextOutA record kept once per script step (the
    game redraws its texts every frame). A record is a "lula[" line plus the lines of a
    multi-line text."""
    seen = set()
    with open(raw, 'rb') as src, open(dest, 'wb') as out:
        record = b''

        def flush(rec):
            if rec.startswith(b'lula[trace] ReadFile(') or rec.startswith(b'lula[trace] SetFilePointer('):
                return
            if rec.startswith(b'lula[trace] DrawTextA(') or rec.startswith(b'lula[trace] TextOutA('):
                if rec in seen:
                    return
                seen.add(rec)
            elif rec.startswith(b'lula[info] script '):
                seen.clear()
            out.write(rec)
        for line in src:
            if line.startswith(b'lula[') and record:
                flush(record)
                record = b''
            record += line
        if record:
            flush(record)


def run_game(save_dir, lines, seconds, workdir, log_name, cov=None, frames=None):
    """Run build/game/lula headless with LINES as input; quit at SECONDS.
    Returns (returncode or None on timeout, wall seconds, log path)."""
    workdir.mkdir(parents=True, exist_ok=True)
    inp = workdir / f'{log_name}.input.txt'
    inp.write_text('\n'.join(lines + [f'{seconds * 1000} quit']) + '\n')
    raw = workdir / f'{log_name}.raw'
    log = workdir / f'{log_name}.txt'
    env = {k: v for k, v in os.environ.items() if not k.startswith('LULA_')}
    env.update(LULA_HEADLESS='1', LULA_MSGBOX_AUTO='1', LULA_LOG='3', LULA_INPUT=str(inp), LULA_CLOCK=CLOCK)
    if cov:
        env['LULA_COVERAGE'] = str(cov)
    if frames:
        fdir = workdir / 'frames'
        shutil.rmtree(fdir, ignore_errors=True)
        fdir.mkdir()
        env.update(LULA_FRAMEDUMP=str(fdir), LULA_FRAMEDUMP_MS=str(frames))
    start = time.monotonic()
    with open(raw, 'wb') as out:
        proc = subprocess.Popen([str(BINARY), '--save', str(save_dir), '--', '-novideo'],
                                cwd=ROOT, env=env, stdout=out, stderr=subprocess.STDOUT)
        try:
            rc = proc.wait(timeout=seconds + GRACE)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            rc = None
    wall = time.monotonic() - start
    filter_log(raw, log)
    raw.unlink()
    return rc, wall, log


def ran_functions(cov):
    """Entries with a non-zero count in a LULA_COVERAGE file."""
    ran = set()
    if cov and Path(cov).is_file():
        for line in Path(cov).read_text().splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[1] != '0':
                ran.add(int(parts[0], 16))
    return ran


def judge(rc, log, checks=(), rejects=(), allows=(), covers=(), cov=None, exit_status=0):
    """List of problems (empty = pass)."""
    problems = []
    text = log.read_bytes().decode('latin-1')
    if rc is None:
        problems.append('timeout: killed')
    elif rc < 0:
        problems.append(f'signal {-rc}')
    elif rc != exit_status:
        problems.append(f'exit status {rc}' + (f' (expected {exit_status})' if exit_status else ''))
    for line in text.splitlines():
        if BAD_LINE.search(line) and not any(a.search(line) for a in allows):
            problems.append(line.strip()[:160])
            break
    for c in checks:
        if not c.search(text):
            problems.append(f'check: {c.pattern}')
    for r in rejects:
        m = r.search(text)
        if m:
            problems.append(f'reject: {r.pattern} matched "{m.group(0)[:80]}"')
    if covers:
        ran = ran_functions(cov)
        missing = [a for a in covers if a not in ran]
        if missing:
            problems.append('covers: did not run ' + ' '.join(f'{a:#x}' for a in missing))
    return problems


# -------------------------------------------------------------------- saves
def load_recipes():
    # saves.json plus any tests/scenarios/saves/*.json (one file per area, so
    # recipes for different areas can be written independently).
    recipes = {}
    for path in [RECIPES, *sorted((SCEN_DIR / 'saves').glob('*.json'))]:
        for k, v in json.loads(path.read_text(encoding='utf-8')).items():
            if k.startswith('_'):
                continue
            if k in recipes:
                raise SystemExit(f'{path.name}: recipe {k!r} is already defined in another file')
            recipes[k] = v
    for name, r in recipes.items():
        if r.get('from') is not None and r['from'] not in recipes:
            raise SystemExit(f'saves.json: {name}: unknown "from" {r["from"]!r}')
        if 'script' in r and 'seconds' not in r:
            raise SystemExit(f'saves.json: {name}: a script needs "seconds"')
        if 'check' not in r:
            raise SystemExit(f'saves.json: {name}: missing "check"')
    return recipes


def recipe_script(r):
    s = r.get('script')
    if s is None:
        return None
    if isinstance(s, str):
        s = (SCEN_DIR / 'saves' / s).read_text(encoding='utf-8').splitlines()
    return [x.strip() for x in s if x.strip() and not x.strip().startswith('#')]


def ancestry(recipes, name):
    chain = []
    while name is not None:
        if name in chain:
            raise SystemExit(f'saves.json: cycle at {name}')
        chain.append(name)
        name = recipes[name].get('from')
    return chain[::-1]


def stamp(recipes, name, _memo={}):
    """Hash of the recipe, its script and its parent's stamp."""
    if name in _memo:
        return _memo[name]
    r = recipes[name]
    h = hashlib.sha256(json.dumps({k: v for k, v in r.items() if k != 'note'}, sort_keys=True).encode())
    script = recipe_script(r)
    if script:   # the game's random data depends on the start clock
        h.update(('\n'.join(script) + '\nclock ' + CLOCK).encode())
    if r.get('from'):
        h.update(stamp(recipes, r['from']).encode())
    _memo[name] = h.hexdigest()[:16]
    return _memo[name]


def save_file(d, slot):
    return d / 'DATA' / 'SAVE' / f'SAVEGAME.{slot:3d}'


def data_file(d, slot):
    return d / 'DATA' / 'DATABASE' / f'STF1DAT.{slot:3d}'


def is_current(recipes, name):
    st = SAVES / f'{name}.stamp'
    return (SAVES / name).is_dir() and st.is_file() and st.read_text().strip() == stamp(recipes, name)


def apply_patch(path, patch):
    data = bytearray(path.read_bytes())
    for addr, val in patch.items():
        off = int(addr, 16) - SAVE_BASE
        if isinstance(val, int):
            raw = struct.pack('<i' if val < 0x80000000 else '<I', val)
        elif isinstance(val, str):
            raw = val.encode('latin-1')
        else:
            raise SystemExit(f'patch {addr}: value must be an int or a string')
        if off < 0 or off + len(raw) > len(data):
            raise SystemExit(f'patch {addr}: outside the save file')
        data[off:off + len(raw)] = raw
    path.write_bytes(data)


def read_slot(d, slot):
    p = save_file(d, slot)
    if not p.is_file():
        return None
    b = p.read_bytes()
    g = lambda a: struct.unpack_from('<i', b, a - SAVE_BASE)[0]
    return {'name': b[8:48].split(b'\0')[0].decode('latin-1'), 'room': g(0x455690), 'stage': g(0x455694),
            'account': g(0x45568c), 'day': g(0x45569c), 'month': g(0x4556a0), 'year': g(0x4556a4),
            'hour': g(0x4556a8), 'minute': g(0x4556ac)}


def check_save(d, check):
    problems = [f'{f} missing' for f in check.get('files', []) if not (d / f).is_file()]
    check = {k: v for k, v in check.items() if k != 'files'}
    if not check:              # only files to check (a template without a save)
        return problems
    slot = check.get('slot', 4)
    info = read_slot(d, slot)
    if info is None:
        return [f'slot {slot}: {save_file(d, slot).relative_to(d)} missing']
    if not data_file(d, slot).is_file():
        return [f'slot {slot}: {data_file(d, slot).relative_to(d)} missing']
    data = save_file(d, slot).read_bytes()
    for k, v in check.items():
        if k == 'slot':
            continue
        if k.startswith('0x'):   # a dword at a guest address
            got = struct.unpack_from('<i', data, int(k, 16) - SAVE_BASE)[0]
        elif k in info:
            got = info[k]
        else:
            problems.append(f'unknown check key {k!r}')
            continue
        if got != v:
            problems.append(f'slot {slot}: {k} is {got!r}, expected {v!r}')
    return problems


def build_save(recipes, name):
    """Build one template (its parent must exist). Returns a list of problems."""
    r = recipes[name]
    work = SAVES / f'.work-{name}'
    shutil.rmtree(work, ignore_errors=True)
    if r.get('from'):
        shutil.copytree(SAVES / r['from'], work, symlinks=True)
    else:
        work.mkdir(parents=True)
    problems = []
    script = recipe_script(r)
    logs = SAVES / '.logs'
    if script is not None:
        rc, wall, log = run_game(work, script, int(r['seconds']), logs, name)
        allows = [re.compile(a) for a in r.get('allow', [])]
        problems += judge(rc, log, allows=allows)
    for junk in ('W_DEBUG.DAT',):
        (work / junk).unlink(missing_ok=True)
    for rel, text in r.get('files', {}).items():
        # Extra game files in the overlay, e.g. a CDROM.LOC that points nowhere.
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(text.encode('latin-1'))
    for spec in r.get('copy_slot', []):
        a, b = spec['from'], spec['to']
        for f in (save_file, data_file):
            shutil.copyfile(f(work, a), f(work, b))
    if r.get('patch'):
        apply_patch(save_file(work, r.get('slot', 4)), r['patch'])
    checks = r['check'] if isinstance(r['check'], list) else [r['check']]
    for c in checks:
        problems += check_save(work, c)
    if problems:
        bad = SAVES / f'.failed-{name}'
        shutil.rmtree(bad, ignore_errors=True)
        work.rename(bad)
        return problems
    shutil.rmtree(SAVES / name, ignore_errors=True)
    work.rename(SAVES / name)
    (SAVES / f'{name}.stamp').write_text(stamp(recipes, name) + '\n')
    shutil.rmtree(SAVES / f'.failed-{name}', ignore_errors=True)
    return []


# ---------------------------------------------------------------- scheduler
class Job:
    def __init__(self, key, deps, fn, weight=0):
        self.key, self.deps, self.fn, self.weight = key, deps, fn, weight
        self.result = None   # None = not run, True = ok, str = failure reason


def run_jobs(jobs, workers):
    """Run jobs (dict key -> Job) respecting deps, up to WORKERS at a time.
    Ready jobs start in order of their longest remaining chain (by weight)."""
    users = {k: [] for k in jobs}
    for j in jobs.values():
        for d in j.deps:
            users[d].append(j.key)
    tail = {}

    def chain(k):
        if k not in tail:
            tail[k] = jobs[k].weight + max((chain(u) for u in users[k]), default=0)
        return tail[k]
    pending = sorted((k for k, j in jobs.items() if j.result is None), key=lambda k: -chain(k))
    running = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        while pending or running:
            progress = True
            while progress:
                progress = False
                for key in list(pending):
                    job = jobs[key]
                    failed = [d for d in job.deps if jobs[d].result not in (None, True)]
                    if failed:
                        job.result = f'needs {failed[0]}, which failed'
                        job.fn(skip=job.result)
                        pending.remove(key)
                        progress = True
                    elif all(jobs[d].result is True for d in job.deps) and len(running) < workers:
                        running[pool.submit(job.fn)] = job
                        pending.remove(key)
            if not running:
                if pending:
                    raise SystemExit('dependency deadlock: ' + ', '.join(pending))
                break
            done, _ = concurrent.futures.wait(running, return_when=concurrent.futures.FIRST_COMPLETED)
            for fut in done:
                job = running.pop(fut)
                try:
                    res = fut.result()
                except Exception as e:  # noqa: BLE001 - report and keep the other jobs going
                    res = f'{type(e).__name__}: {e}'
                job.result = True if res is True else (res or 'failed')


def save_jobs(recipes, wanted, force, jobs):
    """Add jobs that build WANTED templates (and missing or stale ancestors)."""
    needed = []
    for w in wanted:
        for a in ancestry(recipes, w):
            if a not in needed:
                needed.append(a)
    rebuild = set()
    for n in needed:   # ancestry order: parents come first
        parent = recipes[n].get('from')
        if (force and n in wanted) or not is_current(recipes, n) or parent in rebuild:
            rebuild.add(n)
    for n in needed:
        key = 'save:' + n
        parent = recipes[n].get('from')
        deps = ['save:' + parent] if parent else []
        if n not in rebuild:
            job = Job(key, deps, lambda skip=None: True)
            job.result = True
            jobs[key] = job
            continue

        def fn(skip=None, n=n):
            if skip:
                say(f'save {n:14} SKIPPED ({skip})')
                return skip
            t0 = time.monotonic()
            say(f'save {n:14} building')
            problems = build_save(recipes, n)
            dt = time.monotonic() - t0
            if problems:
                say(f'save {n:14} FAILED after {dt:.0f} s: ' + '; '.join(problems[:3]) +
                    f'  (log: {(SAVES / ".logs" / (n + ".txt")).relative_to(ROOT)})')
                return 'save failed: ' + problems[0]
            say(f'save {n:14} ok ({dt:.0f} s)')
            return True
        jobs[key] = Job(key, deps, fn, int(recipes[n].get('seconds', 0)))
    return [n for n in needed if n in rebuild]


# ---------------------------------------------------------------- commands
def cmd_saves(args):
    recipes = load_recipes()
    for n in args.names:
        if n not in recipes:
            raise SystemExit(f'unknown save recipe {n!r}')
    wanted = args.names or list(recipes)
    SAVES.mkdir(parents=True, exist_ok=True)
    jobs = {}
    t0 = time.monotonic()
    todo = save_jobs(recipes, wanted, args.force, jobs)
    if not todo:
        say('all save templates are up to date')
        return 0
    run_jobs(jobs, args.jobs)
    bad = [k for k, j in jobs.items() if j.result is not True]
    say(f'{len(todo) - len(bad)} of {len(todo)} templates built in {time.monotonic() - t0:.0f} s')
    return 1 if bad else 0


def run_scenario(sc, keep, frames):
    d = RUNS / sc.name
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    save = d / 'save'
    if sc.save_name:
        shutil.copytree(SAVES / sc.save_name, save, symlinks=True)
    else:
        save.mkdir()
    rc, wall, log = run_game(save, list(sc.lines), sc.seconds, d, 'log', cov=d / f'{sc.name}.cov', frames=frames)
    (d / 'log.input.txt').rename(d / 'input.txt')
    if not keep:
        shutil.rmtree(save, ignore_errors=True)
    problems = judge(rc, log, sc.checks, sc.rejects, sc.allows, sc.covers, d / f'{sc.name}.cov', sc.exit)
    if not sc.checks:
        problems.append('no "# check: REGEX" line')
    return problems, wall


def run_many(names=(), jobs=3, keep=False, frames=None):
    """Build the needed saves and run the scenarios matching NAMES (all if
    empty). Returns {name: (status, wall seconds, first problem)} with status
    PASS, PASS* (passed on the retry of a flaky scenario), FAIL or ERROR."""
    scenarios = [Scenario(p) for p in scenario_paths(names)]
    recipes = load_recipes()
    todo, results = {}, {}
    for s in scenarios:
        if s.errors:
            results[s.name] = ('ERROR', 0.0, s.errors[0])
        elif s.save_name and s.save_name not in recipes:
            results[s.name] = ('ERROR', 0.0, f'unknown save {s.save_name!r}')
    scenarios = [s for s in scenarios if s.name not in results]
    if frames and len(scenarios) > 1:
        raise SystemExit('--frames is for debugging a single scenario')
    SAVES.mkdir(parents=True, exist_ok=True)
    save_jobs(recipes, sorted({s.save_name for s in scenarios if s.save_name}), False, todo)
    for s in scenarios:
        def fn(skip=None, s=s):
            if skip:
                results[s.name] = ('ERROR', 0.0, skip)
                say(f'{s.name:42} ERROR  {skip}')
                return skip
            problems, wall = run_scenario(s, keep, frames)
            status = 'FAIL' if problems else 'PASS'
            if problems and s.flaky:
                say(f'{s.name:42} retry  ({problems[0][:70]})')
                first = problems[0]
                problems, wall2 = run_scenario(s, keep, frames)
                wall += wall2
                status = 'FAIL' if problems else 'PASS*'
                if not problems:
                    problems = [f'first run: {first}']
            results[s.name] = (status, wall, problems[0] if problems else '')
            say(f'{s.name:42} {status:6} {wall:5.0f} s  {problems[0] if problems else ""}')
            return True
        todo['run:' + s.name] = Job('run:' + s.name, ['save:' + s.save_name] if s.save_name else [], fn,
                                    s.seconds * (2 if s.flaky else 1))
    run_jobs(todo, jobs)
    return results


def cmd_run(args):
    t0 = time.monotonic()
    results = run_many(args.names, args.jobs, args.keep, args.frames)
    print()
    print(f'{"scenario":42} {"result":6} {"secs":>5}  failing check')
    for name in sorted(results):
        status, wall, why = results[name]
        print(f'{name:42} {status:6} {wall:5.0f}  {why}')
    n_fail = sum(1 for v in results.values() if v[0] in ('FAIL', 'ERROR'))
    n_retry = sum(1 for v in results.values() if v[0] == 'PASS*')
    print(f'\n{len(results) - n_fail} of {len(results)} passed ({n_retry} after a retry), '
          f'{time.monotonic() - t0:.0f} s wall')
    return 1 if n_fail else 0


def cmd_recheck(args):
    """Judge the existing logs in build/scenarios/runs again with the current
    headers (for writing checks; the exit status of those runs is not kept)."""
    bad = 0
    for p in scenario_paths(args.names):
        s = Scenario(p)
        log = RUNS / s.name / 'log.txt'
        if not log.is_file():
            print(f'{s.name:42} no log')
            continue
        m = re.search(r'lula\[fatal\] signal (\d+)', log.read_bytes().decode('latin-1'))
        problems = s.errors + judge(-int(m.group(1)) if m else 0, log, s.checks, s.rejects, s.allows,
                                    s.covers, RUNS / s.name / f'{s.name}.cov')
        if not s.checks:
            problems.append('no "# check: REGEX" line')
        bad += bool(problems)
        print(f'{s.name:42} {"FAIL" if problems else "ok":5} {problems[0] if problems else ""}')
    return 1 if bad else 0


def cmd_list(args):
    for p in scenario_paths(args.names):
        s = Scenario(p)
        flags = (' flaky' if s.flaky else '') + ('' if s.checks else ' NO CHECKS') + \
            (' ERROR: ' + '; '.join(s.errors) if s.errors else '')
        print(f'{s.name:42} {s.seconds or 0:4} s  save {s.save or "?":12} {len(s.checks)} checks{flags}')
    return 0


def cmd_coverage(args):
    files = sorted(str(p) for p in RUNS.glob('*/*.cov'))
    if not files:
        raise SystemExit('no coverage files under build/scenarios/runs (run scenarios first)')
    return subprocess.call([sys.executable, str(ROOT / 'tools/coverage_report.py'), *files, *args.rest])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('saves', help='build save templates')
    p.add_argument('names', nargs='*')
    p.add_argument('--force', action='store_true', help='rebuild the named templates (all if none named)')
    p.add_argument('-j', '--jobs', type=int, default=3)
    p = sub.add_parser('run', help='run scenarios')
    p.add_argument('names', nargs='*')
    p.add_argument('-j', '--jobs', type=int, default=3)
    p.add_argument('--keep', action='store_true', help='keep the copied save dir of each run')
    p.add_argument('--frames', type=int, metavar='MS', help='dump frames every MS ms (one scenario only)')
    p = sub.add_parser('recheck', help='judge the logs of earlier runs again with the current headers')
    p.add_argument('names', nargs='*')
    p = sub.add_parser('list', help='list scenarios and header problems')
    p.add_argument('names', nargs='*')
    p = sub.add_parser('coverage', help='coverage report over build/scenarios/runs/*/*.cov')
    p.add_argument('rest', nargs=argparse.REMAINDER)
    args = ap.parse_args(argv)
    if args.cmd in ('saves', 'run') and not BINARY.is_file():
        raise SystemExit(f'{BINARY} missing: cmake -S . -B build/game && cmake --build build/game')
    return {'saves': cmd_saves, 'run': cmd_run, 'list': cmd_list, 'recheck': cmd_recheck,
            'coverage': cmd_coverage}[args.cmd](args)


if __name__ == '__main__':
    sys.exit(main())
