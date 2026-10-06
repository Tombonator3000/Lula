"""Scripted game scenarios (tests/scenarios/*.txt) as unit tests.

Each scenario drives build/game/lula headless with scripted input and is judged
by its "# check:" / "# reject:" lines and the absence of lula[warn|trap|fatal]
lines (see tests/scenarios/README.md). The save templates are built on demand
into build/scenarios/saves (tools/scenarios.py saves).

By default a quick subset runs (about 2 to 4 minutes with 3 parallel runs,
including the save templates it needs). Environment:

    LULA_SCENARIOS=all            run every scenario
    LULA_SCENARIOS=name,glob,...  run the matching scenarios
    LULA_SCENARIO_JOBS=N          parallel runs (default 3)

Skips when the build or the game data is missing.
"""
import importlib.util
import os
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('lula_scenarios', ROOT / 'tools/scenarios.py')
scenarios = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(scenarios)

# Short scenarios: two from a new game (save: none) and four on the cheapest
# recipe chain (s1base -> stage1_goal -> stage2).
QUICK = [
    'office_fkeys',
    'office_options_menu',
    'districts_police_arrest',
    'bank_realtor_distributor_nomovies',
    'bank_realtor_stage2_transition',
    'bank_realtor_stale_mouse_load',
]


@unittest.skipUnless(scenarios.BINARY.is_file(),
                     'build the game first: cmake -S . -B build/game && cmake --build build/game')
@unittest.skipUnless((ROOT / 'original/app/WET.EXE').is_file(), 'original game data not installed')
class Scenarios(unittest.TestCase):
    def test_scenarios(self):
        sel = os.environ.get('LULA_SCENARIOS', '').strip()
        names = [] if sel == 'all' else ([n.strip() for n in sel.split(',') if n.strip()] if sel else QUICK)
        jobs = int(os.environ.get('LULA_SCENARIO_JOBS', '3'))
        results = scenarios.run_many(names, jobs=jobs)
        self.assertTrue(results, 'no scenario selected')
        for name in sorted(results):
            status, wall, why = results[name]
            with self.subTest(scenario=name):
                self.assertIn(status, ('PASS', 'PASS*'),
                              f'{name}: {why} (log: build/scenarios/runs/{name}/log.txt)')


if __name__ == '__main__':
    unittest.main()
