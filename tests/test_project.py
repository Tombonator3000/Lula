import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('project', Path(__file__).resolve().parents[1] / 'tools/project.py')
project = importlib.util.module_from_spec(spec)
spec.loader.exec_module(project)


class ProjectSafetyTests(unittest.TestCase):
    def test_profile_preserves_game_specific_overrides(self):
        original = '; comment\r\n[ddraw]\r\nwidth=0\r\nheight=0\r\n[other]\r\nwidth=42\r\n'
        updated = project.configure_ini(original, {'width': '1920', 'height': '1080'})
        self.assertIn('; comment\r\n', updated)
        self.assertIn('[ddraw]\r\nwidth=1920\r\nheight=1080\r\n', updated)
        self.assertIn('[other]\r\nwidth=42\r\n', updated)

    def test_original_verification_detects_changed_missing_and_extra_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / 'original/app'
            original.mkdir(parents=True)
            (original / 'WET.EXE').write_bytes(b'fixture')
            with patch.object(project, 'ROOT', root), patch.object(project, 'ORIGINAL', original), patch.object(project, 'MANIFEST', root / 'manifest.json'):
                project.inventory()
                project.verify()
                (original / 'WET.EXE').write_bytes(b'changed')
                with self.assertRaisesRegex(ValueError, 'Changed'):
                    project.verify()
                (original / 'WET.EXE').unlink()
                with self.assertRaisesRegex(ValueError, 'Missing'):
                    project.verify()
                (original / 'extra').write_bytes(b'extra')
                with self.assertRaisesRegex(ValueError, 'Unexpected'):
                    project.verify()

    def test_staging_preserves_existing_saves_and_cannot_overwrite_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / 'original/app'
            original.mkdir(parents=True)
            (original / 'WET.EXE').write_bytes(b'fixture')
            with patch.object(project, 'ROOT', root), patch.object(project, 'ORIGINAL', original), patch.object(project, 'MANIFEST', root / 'manifest.json'):
                project.inventory()
                with self.assertRaisesRegex(ValueError, 'below'):
                    project.stage('original', original)
                (root / 'build').symlink_to(original, target_is_directory=True)
                with self.assertRaisesRegex(ValueError, 'below'):
                    project.stage('original')
                self.assertEqual(sorted(p.name for p in original.iterdir()), ['WET.EXE'])
                (root / 'build').unlink()
                destination = root / 'build/runtime-original'
                project.stage('original', destination)
                self.assertTrue((destination / 'DATA/SAVE').is_dir())
                self.assertTrue((destination / 'DATA/DATABASE').is_dir())
                save = destination / 'save.dat'
                save.write_bytes(b'precious save')
                with self.assertRaisesRegex(ValueError, 'exists'):
                    project.stage('original', destination)
                self.assertEqual(save.read_bytes(), b'precious save')


if __name__ == '__main__':
    unittest.main()
