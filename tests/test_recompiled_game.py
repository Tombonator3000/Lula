"""End-to-end check of the recompiled game (build/game/lula).

Runs the native build headless with -novideo and compares the main menu with
the original game. The expected value is a SHA-256 over the RGB565 frame with
the five button labels blanked out: the labels are drawn with a host font,
everything else must be pixel-identical to WET.EXE running under Wine
(reference captured on 2026-10-05, see docs/recompilation.md). Only a hash is
stored, no game graphics.

Skips when the build, the game data or a working SDL dummy driver is missing.
"""
import hashlib
import importlib.util
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('LULA_BINARY', ROOT / 'build/game/lula'))

MENU_LABELS = [(388, 28, 622, 56), (388, 74, 622, 102), (388, 121, 622, 149),
               (388, 166, 622, 194), (507, 228, 622, 256)]
MENU_SHA256 = '89b9fbf739c5c6752414ec195ef8f90cb5ad9f84c2b4fb529224e17fe95340dc'


def read_ppm(path):
    data = path.read_bytes()
    # Header written by platform_sdl.c: "P6\n640 480\n255\n".
    parts = data.split(b'\n', 3)
    if parts[0] != b'P6' or parts[1] != b'640 480' or parts[2] != b'255':
        raise ValueError(f'unexpected PPM header in {path}')
    return parts[3]


def masked_rgb565(rgb, masks):
    out = bytearray(640 * 480 * 2)
    for y in range(480):
        row_masks = [(x0, x1) for x0, y0, x1, y1 in masks if y0 <= y < y1]
        for x in range(640):
            if any(x0 <= x < x1 for x0, x1 in row_masks):
                continue
            i = (y * 640 + x) * 3
            v = ((rgb[i] >> 3) << 11) | ((rgb[i + 1] >> 2) << 5) | (rgb[i + 2] >> 3)
            out[(y * 640 + x) * 2] = v & 0xff
            out[(y * 640 + x) * 2 + 1] = v >> 8
    return hashlib.sha256(bytes(out)).hexdigest()


@unittest.skipUnless(BINARY.is_file(), 'build the game first: cmake -S . -B build/game && cmake --build build/game')
@unittest.skipUnless((ROOT / 'original/app/WET.EXE').is_file(), 'original game data not installed')
class RecompiledGame(unittest.TestCase):
    def run_game(self, seconds, script='', extra=()):
        runs = ROOT / 'build' / 'test-runs'
        runs.mkdir(parents=True, exist_ok=True)
        temp = Path(tempfile.mkdtemp(prefix='lula-', dir=runs))
        frames = temp / 'frames'
        frames.mkdir()
        (temp / 'input.txt').write_text(script)
        env = dict(os.environ, LULA_HEADLESS='1', LULA_FRAMEDUMP=str(frames),
                   LULA_FRAMEDUMP_MS='500', LULA_INPUT=str(temp / 'input.txt'))
        proc = subprocess.Popen([str(BINARY), '--save', str(temp / 'save'), *extra, '--', '-novideo'],
                                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            time.sleep(seconds)
            self.assertIsNone(proc.poll(), 'the game exited early:\n' +
                              (proc.stdout.read().decode(errors='replace') if proc.poll() is not None else ''))
        finally:
            proc.kill()
            log = proc.communicate()[0].decode(errors='replace')
        return temp, sorted(frames.glob('*.ppm')), log

    def test_menu_sound_is_mixed_from_original_samples(self):
        """The menu loops sound.tap entry 9 (22050 Hz, 8 bit) at -10 dB."""
        import struct
        import wave
        spec = importlib.util.spec_from_file_location('lula_assets_snd', ROOT / 'tools/assets.py')
        assets = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = assets
        spec.loader.exec_module(assets)
        wav = assets.parse_ngs((ROOT / 'original/app/DATA/SOUND/sound.tap').read_bytes()).entries[9].payload
        start = wav.find(b'data')
        size = struct.unpack_from('<I', wav, start + 4)[0]
        samples = wav[start + 8:start + 8 + size]
        runs = ROOT / 'build' / 'test-runs'
        runs.mkdir(parents=True, exist_ok=True)
        dump = Path(tempfile.mkdtemp(prefix='audio-', dir=runs)) / 'mix.wav'
        os.environ['LULA_AUDIODUMP'] = str(dump)
        try:
            self.run_game(8)
        finally:
            del os.environ['LULA_AUDIODUMP']
        with wave.open(str(dump)) as w:
            self.assertEqual((w.getframerate(), w.getnchannels()), (44100, 2))
            raw = w.readframes(w.getnframes())
        left = struct.unpack(f'<{len(raw) // 2}h', raw)[0::2]
        first = next((i for i, v in enumerate(left) if v), None)
        self.assertIsNotNone(first, 'no audio was mixed')
        gain = 10 ** (-10 / 20)
        expected = [int((samples[k // 2] - 128) * 256 * gain) for k in range(2000)]
        self.assertEqual(list(left[first:first + 2000]), expected)

    def test_replacement_graphics_from_mods_directory(self):
        """Edit the menu background with tools/assets.py and load it via --mods."""
        spec = importlib.util.spec_from_file_location('lula_assets', ROOT / 'tools/assets.py')
        assets = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = assets
        spec.loader.exec_module(assets)
        source = ROOT / 'original/app/DATA/DIALOG/DIA_BACK.TGP'
        archive = assets.parse_ngs(source.read_bytes())
        entries = list(archive.entries)
        menu = entries[71].payload               # the main menu picture, 640x480
        ppm = bytearray(assets.tbf_to_ppm(menu))
        start = ppm.index(b'255\n') + 4
        for y in range(400, 440):
            for x in range(20, 120):
                i = start + (y * 640 + x) * 3
                ppm[i:i + 3] = b'\x00\xff\x00'
        entries[71] = assets.Entry(entries[71].type_word, assets.ppm_to_tbf(bytes(ppm), menu))
        runs = ROOT / 'build' / 'test-runs'
        runs.mkdir(parents=True, exist_ok=True)
        mods = Path(tempfile.mkdtemp(prefix='mods-', dir=runs))
        (mods / 'DATA' / 'DIALOG').mkdir(parents=True)
        (mods / 'DATA' / 'DIALOG' / 'DIA_BACK.TGP').write_bytes(
            assets.pack_ngs(assets.NGS(archive.version_word, tuple(entries))))
        temp, frames, log = self.run_game(8, extra=('--mods', str(mods)))
        rgb = read_ppm(frames[-1])
        green = sum(1 for i in range(0, len(rgb), 3) if rgb[i:i + 3] == b'\x00\xff\x00')
        self.assertEqual(green, 4000, f'replacement graphic not shown; frames in {temp}')

    def test_main_menu_matches_original(self):
        temp, frames, log = self.run_game(8)
        self.assertNotIn('lula[trap]', log)
        self.assertNotIn('lula[fatal]', log)
        hashes = [masked_rgb565(read_ppm(f), MENU_LABELS) for f in frames[-4:]]
        self.assertIn(MENU_SHA256, hashes, f'main menu differs from the original; frames in {temp}')
        # The original data is read-only: nothing may be written outside the save dir.
        self.assertTrue((temp / 'save' / 'W_DEBUG.DAT').is_file())

    def test_new_game_reaches_city_without_traps(self):
        # The menu is up after 3-7 s depending on machine load; click late.
        temp, frames, log = self.run_game(20, '14000 move 505 88\n15000 click 505 88\n')
        self.assertNotIn('lula[trap]', log)
        self.assertNotIn('lula[fatal]', log)
        menu = [masked_rgb565(read_ppm(f), MENU_LABELS) for f in frames]
        self.assertIn(MENU_SHA256, menu, 'main menu never appeared')
        self.assertNotEqual(menu[-1], MENU_SHA256, f'still in the main menu after the click; frames in {temp}')


if __name__ == '__main__':
    unittest.main()
