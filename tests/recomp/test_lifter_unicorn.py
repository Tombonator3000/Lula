"""Quick instruction-level differential test of the lifter against unicorn.

Runs tests/recomp/unicorn_diff.py --quick (about 1200 lifted instructions and
fused flag chains, 8 states each). The full run is
`python3 tests/recomp/unicorn_diff.py`.
"""
import contextlib
import importlib.util
import io
import unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location('unicorn_diff', Path(__file__).resolve().parent / 'unicorn_diff.py')
unicorn_diff = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(unicorn_diff)
MISSING = unicorn_diff.missing_requirements()


@unittest.skipIf(MISSING, 'needs ' + ', '.join(MISSING))
class LifterUnicornTest(unittest.TestCase):
    def test_quick_differential(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = unicorn_diff.main(['--quick', '--show', '10'])
        self.assertEqual(status, 0, out.getvalue()[-8000:])


if __name__ == '__main__':
    unittest.main()
