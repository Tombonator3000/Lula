"""Differential verification of reconstructed C code; decoded images are not displayed.

Build the host CLI first. Set LULA_RESOURCE_CLI for a separate (e.g. sanitized)
build. Missing CLI/corpus dependencies are explicit skips, never a pass claim.
"""
import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("LULA_RESOURCE_CLI", ROOT / "build/reconstruction/lula-resource-cli"))
SPEC = importlib.util.spec_from_file_location("lula_native_reference", ROOT / "tools/assets.py")
assets = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = assets
SPEC.loader.exec_module(assets)


def make_tbf(words, width, height, mode=0):
    pixels = struct.pack(f"<{len(words)}H", *words)
    return struct.pack("<4sHHIHH", b"TBF\0", 16, mode, len(pixels), width, height) + (
        pixels if mode == 0 else assets.encode_rle(pixels))


@unittest.skipUnless(CLI.is_file(), "Build the host reconstructed resource CLI first")
class NativeResources(unittest.TestCase):
    def run_cli(self, *args, expected=0):
        result = subprocess.run([str(CLI), *map(str, args)], capture_output=True, timeout=30)
        self.assertEqual(result.returncode, expected,
                         f"CLI {args[0]}: {result.stderr.decode(errors='replace')}")
        return result

    def compare_image(self, data, target, *command):
        self.run_cli(*command, target)
        self.assertEqual(target.read_bytes(), assets.tbf_to_ppm(data))

    def test_all_rgb565_values_and_rle_boundaries_match_python(self):
        images = [make_tbf(range(65536), 256, 256),
                  make_tbf([0x1234] * 70000, 350, 200, mode=2),
                  make_tbf([i % 65536 for i in range(5000)], 100, 50, mode=2),
                  # A zero repeat command consumes a pixel and produces none.
                  struct.pack("<4sHHIHH4H", b"TBF\0", 16, 2, 2, 1, 1, 0, 123, 1, 321),
                  # Noncanonical one-literal commands are still valid.
                  struct.pack("<4sHHIHH6H", b"TBF\0", 16, 2, 6, 3, 1,
                              65535, 7, 65535, 7, 65535, 7)]
        with tempfile.TemporaryDirectory() as temp:
            for i, data in enumerate(images):
                source = Path(temp) / f"source-{i}.tbf"
                target = Path(temp) / f"target-{i}.ppm"
                source.write_bytes(data)
                self.compare_image(data, target, "decode-tbf", source)

    def test_ngs_unknown_words_empty_payload_and_selected_index(self):
        image = make_tbf([0xF800, 0x07E0, 0x001F], 3, 1, mode=2)
        data = assets.pack_ngs(assets.NGS(65535, (assets.Entry(0xFFFF, b""),
                                                    assets.Entry(1, image))))
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "source.ngs"
            source.write_bytes(data)
            metadata = json.loads(self.run_cli("inspect-ngs", source).stdout)
            self.assertEqual((metadata["version_word"], metadata["count"]), (65535, 2))
            self.assertEqual(metadata["entries"][0],
                             {"index": 0, "offset": 14, "size": 0, "type_word": 65535})
            target = Path(temp) / "empty.bin"
            self.run_cli("extract-ngs", source, 0, target)
            self.assertEqual(target.read_bytes(), b"")
            self.compare_image(image, Path(temp) / "image.ppm", "decode-ngs", source, 1)
            self.run_cli("extract-ngs", source, 2, Path(temp) / "missing.bin", expected=1)
            self.run_cli("decode-ngs", source, 0, Path(temp) / "empty.ppm", expected=1)
            for index in ("-1", "0x1", "1x", " 1", "65536", "99999999999999999999"):
                self.run_cli("extract-ngs", source, index, Path(temp) / "bad.bin", expected=2)

    def test_empty_ngs_and_malformed_index_headers_rejected(self):
        valid = assets.pack_ngs(assets.NGS(21, (assets.Entry(1, b"xx"),)))
        bad_offset = bytearray(valid)
        struct.pack_into("<I", bad_offset, len(bad_offset) - 4, 15)
        bad_size = bytearray(valid)
        struct.pack_into("<I", bad_size, 8, 0xFFFFFFFF)
        huge_index = struct.pack("<4sHH", b"NGS\0", 21, 65535)
        extra_gap = valid[:-4] + b"x" + valid[-4:]
        short_record = struct.pack("<4sHHI", b"NGS\0", 21, 1, 14)
        malformed = [b"", valid[:7], b"BAD!" + valid[4:], bad_offset, bad_size,
                     huge_index, extra_gap, short_record]
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "source.ngs"
            source.write_bytes(assets.pack_ngs(assets.NGS(21, ())))
            metadata = json.loads(self.run_cli("inspect-ngs", source).stdout)
            self.assertEqual((metadata["count"], metadata["entries"]), (0, []))
            for data in malformed:
                source.write_bytes(data)
                with self.subTest(data=data[:16]):
                    result = self.run_cli("inspect-ngs", source, expected=1)
                    self.assertEqual(result.stdout, b"")
                    self.assertIn(b"Resource error:", result.stderr)

    def test_tbf_size_compression_and_allocation_guards(self):
        raw = make_tbf([7], 1, 1)
        bad_version = bytearray(raw)
        struct.pack_into("<H", bad_version, 4, 24)
        bad_mode = bytearray(raw)
        struct.pack_into("<H", bad_mode, 6, 1)
        wrong_size = bytearray(raw)
        struct.pack_into("<I", wrong_size, 8, 4)
        header = struct.pack("<4sHHIHH", b"TBF\0", 16, 2, 2, 1, 1)
        malformed = [b"", raw[:15], b"BAD!" + raw[4:], bad_version, bad_mode, wrong_size,
                     raw[:-1], raw + b"x", header, header + b"\xff",
                     header + b"\xff\xff", header + b"\x01\x00",
                     header + struct.pack("<HH", 2, 0),
                     header + struct.pack("<HH", 0, 123),
                     struct.pack("<4sHHIHH", b"TBF\0", 16, 0, 0, 0, 1),
                     struct.pack("<4sHHIHH", b"TBF\0", 16, 0, 0xFFFFFFFF, 65535, 65535)]
        with tempfile.TemporaryDirectory() as temp:
            source, target = Path(temp) / "bad.tbf", Path(temp) / "bad.ppm"
            for data in malformed:
                source.write_bytes(data)
                with self.subTest(data=data[:16]):
                    self.run_cli("decode-tbf", source, target, expected=1)
                    self.assertFalse(target.exists())
            huge = Path(temp) / "oversized.ngs"
            with huge.open("wb") as output:
                output.seek(268435456)
                output.write(b"x")
            result = self.run_cli("inspect-ngs", huge, expected=1)
            self.assertIn(b"256 MiB", result.stderr)

    def test_existing_outputs_sources_and_symlink_targets_are_preserved(self):
        image = make_tbf([0xF800], 1, 1)
        archive = assets.pack_ngs(assets.NGS(21, (assets.Entry(1, image),)))
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "source.tbf"
            source.write_bytes(image)
            container = Path(temp) / "source.ngs"
            container.write_bytes(archive)
            target = Path(temp) / "existing.ppm"
            target.write_bytes(b"preserved")
            for command in (("decode-tbf", source, target),
                            ("decode-ngs", container, 0, target),
                            ("extract-ngs", container, 0, target),
                            ("decode-tbf", source, source),
                            ("extract-ngs", container, 0, container)):
                self.run_cli(*command, expected=1)
            self.assertEqual(target.read_bytes(), b"preserved")
            self.assertEqual(source.read_bytes(), image)
            self.assertEqual(container.read_bytes(), archive)
            if hasattr(os, "symlink"):
                link = Path(temp) / "output-link.ppm"
                link.symlink_to(target)
                self.run_cli("decode-tbf", source, link, expected=1)
                self.assertEqual(target.read_bytes(), b"preserved")
                self.assertTrue(link.is_symlink())

    @unittest.skipUnless((ROOT / "original/app/WET.EXE").is_file(),
                         "Original corpus not installed locally")
    def test_all_10_original_ngs_indexes_and_representative_payloads(self):
        count = 0
        with tempfile.TemporaryDirectory() as temp:
            for source in sorted((ROOT / "original/app").rglob("*")):
                if source.suffix.lower() not in (".tgp", ".tap", ".ddf"):
                    continue
                data = source.read_bytes()
                reference = assets.parse_ngs(data)
                metadata = json.loads(self.run_cli("inspect-ngs", source).stdout)
                self.assertEqual(metadata, {
                    "format": "NGS", "version_word": reference.version_word,
                    "count": len(reference.entries), "size": len(data),
                    "entries": [{"index": i, "offset": e.offset, "size": len(e.payload),
                                 "type_word": e.type_word}
                                for i, e in enumerate(reference.entries)]}, str(source))
                for index in sorted({0, len(reference.entries) // 2, len(reference.entries) - 1}):
                    output = Path(temp) / f"payload-{count}-{index}.bin"
                    self.run_cli("extract-ngs", source, index, output)
                    self.assertEqual(output.read_bytes(), reference.entries[index].payload,
                                     f"{source}, entry {index}")
                count += 1
        self.assertEqual(count, 10)

    @unittest.skipUnless((ROOT / "original/app/WET.EXE").is_file(),
                         "Original corpus not installed locally")
    def test_all_339_original_tbf_pixel_outputs_match_python(self):
        count = 0
        modes = {0: 0, 2: 0}
        with tempfile.TemporaryDirectory() as temp:
            for source in sorted((ROOT / "original/app").rglob("*")):
                if source.suffix.lower() == ".tbf":
                    entries = [(None, source.read_bytes())]
                elif source.suffix.lower() == ".tgp":
                    entries = list(enumerate(e.payload for e in
                                             assets.parse_ngs(source.read_bytes()).entries))
                else:
                    continue
                for index, data in entries:
                    mode = assets.parse_tbf(data).mode
                    modes[mode] += 1
                    output = Path(temp) / f"image-{count}.ppm"
                    command = ("decode-tbf", source) if index is None else ("decode-ngs", source, index)
                    with self.subTest(file=str(source), index=index):
                        self.compare_image(data, output, *command)
                    output.unlink()
                    count += 1
        self.assertEqual(count, 339)
        self.assertEqual(modes, {0: 122, 2: 217})


if __name__ == "__main__":
    unittest.main()
