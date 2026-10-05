"""Container invariants, editable RGB565 route, and optional original-corpus checks."""
import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("lula_assets", ROOT / "tools/assets.py")
assets = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = assets
SPEC.loader.exec_module(assets)


def tbf(words, width, height, mode=2):
    pixels = struct.pack(f"<{len(words)}H", *words)
    header = struct.pack("<4sHHIHH", b"TBF\0", 16, mode, len(pixels), width, height)
    return header + (pixels if mode == 0 else assets.encode_rle(pixels))


def taf(empty=False, indexed=True):
    first = 787
    prefix = bytearray(first)
    pixel_data = struct.pack("<HH", 3, 0x1234)
    struct.pack_into("<4sHHI", prefix, 0, b"TAF\0", 16, 2 if empty else 1, 6)
    struct.pack_into("<I", prefix, 780, first)
    prefix[784:787] = b"\x02\x00\x01"
    frame = struct.pack("<HHHII", 2, 3, 1, first + 15 + len(pixel_data), first + 15)
    frame += b"\x83" + pixel_data
    offsets = [first]
    if empty:
        start = first + len(frame)
        offsets.append(start)
        frame += struct.pack("<HHHII", 2, 0, 0, start + 15, start + 15)
    footer = b"\x00" * (4 * len(offsets))
    if indexed:
        footer += struct.pack(f"<{len(offsets)}I", *offsets)
    return bytes(prefix) + frame + footer


class Containers(unittest.TestCase):
    def test_ngs_preserves_unknown_words_and_empty_payload(self):
        archive = assets.NGS(24, (assets.Entry(27, b"wav bytes"), assets.Entry(0xFFFF, b"")))
        data = assets.pack_ngs(archive)
        parsed = assets.parse_ngs(data)
        self.assertEqual(assets.pack_ngs(parsed), data)
        self.assertEqual(parsed.entries[1].type_word, 0xFFFF)
        self.assertEqual(parsed.entries[1].payload, b"")

    def test_ngs_changed_size_recalculates_index_and_chunk_headers(self):
        archive = assets.NGS(21, (assets.Entry(24, b"x"), assets.Entry(21, b"abc")))
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "unpacked"
            original = assets.pack_ngs(archive)
            assets.unpack(original, target)
            (target / "0000.bin").write_bytes(b"a longer changed payload")
            changed = assets.repack(target)
            parsed = assets.parse_ngs(changed)
            self.assertEqual(parsed.entries[0].payload, b"a longer changed payload")
            self.assertEqual(parsed.entries[1].payload, b"abc")
            self.assertEqual(parsed.entries[1].offset, 20 + len(parsed.entries[0].payload))
            self.assertEqual(assets.pack_ngs(parsed), changed)

    def test_ngs_rejects_corrupt_index_and_chunk_size(self):
        data = bytearray(assets.pack_ngs(assets.NGS(21, (assets.Entry(1, b"xx"),))))
        struct.pack_into("<I", data, len(data) - 4, 15)
        with self.assertRaises(assets.FormatError):
            assets.parse_ngs(data)
        struct.pack_into("<I", data, len(data) - 4, 14)
        struct.pack_into("<I", data, 8, 0xFFFFFFFF)
        with self.assertRaises(assets.FormatError):
            assets.parse_ngs(data)

    def test_taf_preserves_prefix_flags_footer_and_empty_sentinel(self):
        for empty in (False, True):
            for indexed in (False, True):
                original = taf(empty, indexed)
                parsed = assets.parse_taf(original)
                self.assertEqual(len(parsed.frames), 2 if empty else 1)
                with tempfile.TemporaryDirectory() as temp:
                    target = Path(temp) / "unpacked"
                    assets.unpack(original, target)
                    self.assertEqual(assets.repack(target), original)
                    frame = target / "0000.frame"
                    frame.write_bytes(frame.read_bytes()[:-1] + b"\x42")
                    with self.assertRaisesRegex(assets.FormatError, "TAF edits"):
                        assets.repack(target)

    def test_taf_rejects_bad_frame_offset_and_trailer(self):
        data = bytearray(taf())
        struct.pack_into("<I", data, 787 + 10, 802 + 1)
        with self.assertRaises(assets.FormatError):
            assets.parse_taf(data)
        with self.assertRaises(assets.FormatError):
            assets.parse_taf(taf() + b"unknown")

    def test_manifest_rejects_path_traversal_duplicates_and_symlinks(self):
        original = assets.pack_ngs(assets.NGS(21, (assets.Entry(24, b"x"),)))
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "unpacked"
            manifest = assets.unpack(original, target)
            manifest["entries"][0]["file"] = "../outside.bin"
            (target / "manifest.json").write_text(json.dumps(manifest))
            with self.assertRaises(assets.FormatError):
                assets.repack(target)
            manifest["entries"][0]["file"] = "0000.bin"
            manifest["entries"] *= 2
            (target / "manifest.json").write_text(json.dumps(manifest))
            with self.assertRaises(assets.FormatError):
                assets.repack(target)
            manifest["entries"] = manifest["entries"][:1]
            (target / "manifest.json").write_text(json.dumps(manifest))
            (target / "0000.bin").unlink()
            (target / "0000.bin").symlink_to(target / "manifest.json")
            with self.assertRaises(assets.FormatError):
                assets.repack(target)

    def test_output_never_overwrites_existing_data(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "original"
            path.write_bytes(b"preserved")
            with self.assertRaises(FileExistsError):
                assets.write_new(path, b"new")
            with self.assertRaises(assets.FormatError):
                assets.unpack(assets.pack_ngs(assets.NGS(21, ())), Path(temp))
            self.assertEqual(path.read_bytes(), b"preserved")


class Graphics(unittest.TestCase):
    def test_all_65536_rgb565_colors_survive_ppm_roundtrip(self):
        original = tbf(range(65536), 256, 256, mode=0)
        self.assertEqual(assets.ppm_to_tbf(assets.tbf_to_ppm(original), original), original)

    def test_unchanged_noncanonical_rle_keeps_original_commands(self):
        header = struct.pack("<4sHHIHH", b"TBF\0", 16, 2, 6, 3, 1)
        # Three single literal commands instead of one repeat command.
        original = header + struct.pack("<6H", 65535, 7, 65535, 7, 65535, 7)
        self.assertEqual(assets.ppm_to_tbf(assets.tbf_to_ppm(original), original), original)

    def test_changed_ppm_reencodes_both_supported_tbf_modes(self):
        for mode in (0, 2):
            original = tbf([0x1234, 0x1234, 0xFFFF], 3, 1, mode)
            changed = b"P6\n3 1\n255\n" + bytes([255, 0, 0, 0, 255, 0, 0, 0, 255])
            packed = assets.ppm_to_tbf(changed, original)
            parsed = assets.parse_tbf(packed)
            self.assertEqual(parsed.mode, mode)
            self.assertEqual(parsed.pixels, struct.pack("<3H", 0xF800, 0x07E0, 0x001F))

    def test_rle_literal_and_repeat_field_boundaries(self):
        pixels = struct.pack("<5000H", *[i % 65536 for i in range(5000)])
        self.assertEqual(assets.decode_rle(assets.encode_rle(pixels), len(pixels)), pixels)
        pixels = b"\x00\xF8" * 70000
        self.assertEqual(assets.decode_rle(assets.encode_rle(pixels), len(pixels)), pixels)
        self.assertEqual(assets.decode_rle(struct.pack("<HHHH", 0, 123, 1, 321), 2), struct.pack("<H", 321))

    def test_rejects_truncated_rle_and_wrong_dimensions(self):
        for payload in (b"\xff", b"\xff\xff", struct.pack("<HH", 4, 0)):
            with self.assertRaises(assets.FormatError):
                assets.decode_rle(payload, 2)
        original = tbf([0], 1, 1)
        with self.assertRaisesRegex(assets.FormatError, "Dimension changes"):
            assets.ppm_to_tbf(b"P6\n2 1\n255\n" + bytes(6), original)
        malformed = bytearray(original)
        struct.pack_into("<I", malformed, 8, 4)
        with self.assertRaises(assets.FormatError):
            assets.parse_tbf(malformed)

    def test_ppm_comments_and_whitespace_valued_pixels(self):
        data = b"P6\n# comment\n1 1\n255\n" + b"\n #"
        self.assertEqual(assets.parse_ppm(data), (1, 1, b"\n #"))
        with self.assertRaises(assets.FormatError):
            assets.parse_ppm(data + b"x")


@unittest.skipUnless((ROOT / "original/app/WET.EXE").is_file(), "Original corpus not installed locally")
class OriginalCorpus(unittest.TestCase):
    def test_all_ngs_containers_byte_identical_roundtrip(self):
        count = 0
        for path in (ROOT / "original/app").rglob("*"):
            if path.suffix.lower() in (".tgp", ".tap", ".ddf"):
                original = path.read_bytes()
                self.assertEqual(assets.pack_ngs(assets.parse_ngs(original)), original, str(path))
                count += 1
        self.assertEqual(count, 10)

    def test_all_tbf_images_decode_and_identical_ppm_reimport(self):
        count = 0
        for path in (ROOT / "original/app").rglob("*"):
            if path.suffix.lower() == ".tbf":
                images = [path.read_bytes()]
            elif path.suffix.lower() == ".tgp":
                images = [e.payload for e in assets.parse_ngs(path.read_bytes()).entries]
            else:
                continue
            for i, image in enumerate(images):
                self.assertEqual(assets.ppm_to_tbf(assets.tbf_to_ppm(image), image), image,
                                 f"{path}, entry {i}")
                count += 1
        self.assertEqual(count, 339)

    def test_all_taf_frames_and_optional_indexes_validate(self):
        files = frames = 0
        for path in (ROOT / "original/app").rglob("*"):
            if path.suffix.lower() != ".taf":
                continue
            original = path.read_bytes()
            parsed = assets.parse_taf(original)
            self.assertEqual(parsed.prefix + b"".join(parsed.frames) + parsed.footer, original, str(path))
            files += 1
            frames += len(parsed.frames)
        self.assertEqual((files, frames), (78, 989))

    def test_all_cut_files_are_size_consistent_avi_containers(self):
        count = 0
        for path in (ROOT / "original/app").rglob("*"):
            if path.suffix.lower() == ".cut":
                self.assertEqual(assets.inspect(path.read_bytes())["format"], "AVI", str(path))
                count += 1
        self.assertEqual(count, 43)


if __name__ == "__main__":
    unittest.main()
