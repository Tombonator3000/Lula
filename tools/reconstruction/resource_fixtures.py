#!/usr/bin/env python3
"""File-backed cases for resource_check, without copying assets into Git.

Run from the repo: python3 tools/reconstruction/resource_fixtures.py
Then: build/game/lula-resource-check . build/resource-check/fixtures.tsv
The original files are read only; synthetic streams go under build/.
"""
import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("lula_assets", ROOT / "tools/assets.py")
assets = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = assets
spec.loader.exec_module(assets)


def main():
    work = ROOT / "build/resource-check"
    work.mkdir(parents=True, exist_ok=True)
    rows = []

    def add(kind, path, offset=0, mode=0, decoded=0, error=0, convert=0, palette=0):
        path = str(path.relative_to(ROOT)) if isinstance(path, Path) else path
        rows.append("\t".join(map(str, (kind, path, offset, mode, decoded, error, convert, palette))))

    def synthetic(name, data):
        path = work / name
        path.write_bytes(data)
        return path

    # Each case variant and each prefix position, not just unknown random bytes.
    for signature in ("TBF", "TPF", "TAF", "TFF"):
        for mask in range(8):
            letters = "".join(c.lower() if mask & (1 << i) else c for i, c in enumerate(signature))
            for fourth in ("x", "F"):
                add("signature", letters + fourth)
    for signature in ("XXX", "TBX", "TAx", "TFX", "XBF", "0BF", "T0F"):
        add("signature", signature)

    pools = images = frames = 0
    for path in sorted((ROOT / "original/app").rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() not in (".tgp", ".tap", ".ddf", ".tbf", ".taf"):
            continue
        data = path.read_bytes()
        if data[:4] == b"NGS\0":
            pools += 1
            pool = assets.parse_ngs(data)
            offsets = struct.unpack_from(f"<{len(pool.entries)}I", data, len(data) - len(pool.entries) * 4)
            for index, (entry, offset) in enumerate(zip(pool.entries, offsets)):
                add("ngs-seek", path, mode=index, decoded=len(entry.payload))
                add("ngs-read", path, offset, decoded=len(entry.payload))
                if entry.payload[:4] == b"TBF\0":
                    image = assets.parse_tbf(entry.payload)
                    images += 1
                    for convert in (0, 1):
                        add("tbf", path, offset, decoded=len(image.pixels) + 4, convert=convert)
                        add("rle", path, offset + 16, image.mode, len(image.pixels), convert=convert)
            for index in (len(pool.entries), 0xffff, 0x10000, 0xffffffff):
                add("ngs-seek", path, mode=index)
        elif data[:4] == b"TBF\0":
            image = assets.parse_tbf(data)
            images += 1
            for convert in (0, 1):
                add("tbf", path, decoded=len(image.pixels) + 4, convert=convert)
                add("rle", path, 16, image.mode, len(image.pixels), convert=convert)
        elif data[:4] == b"TAF\0":
            animation = assets.parse_taf(data)
            for frame, offset in zip(animation.frames, animation.offsets):
                mode, width, height = struct.unpack_from("<HHH", frame)
                frames += 1
                if not width and not height:
                    continue       # loader sentinel; not a stream decoder input
                for convert in (0, 1):
                    add("rle", path, offset + 15, mode, width * height * 2, convert=convert)

    # Larger inputs force the original 64,000-byte refill/crossing branches.
    words = b"".join(struct.pack("<H", (i * 109 + 17) & 0xffff) for i in range(70000))
    encoded = assets.encode_rle(words)
    for name, payload, mode, limit in (
        ("raw-large.bin", words, 0, len(words)),
        ("word-large.bin", encoded, 2, len(words)),
        ("byte-small.bin", b"\x03\x12\x02\xfe\x01\x00", 1, 6),
        ("byte-large.bin", b"\x01\x71" * 34000, 1, 34000),
        ("repeat-boundary.bin", b"\xff\xff\x12\x34" * 15999 + b"\x03\x00\xab\xcd", 2, 32004),
        ("zero-run.bin", b"\0\0\xaa\xbb\x02\0\x34\x12", 2, 4),
    ):
        path = synthetic(name, payload)
        for convert in (0, 1):
            add("rle", path, mode=mode, decoded=limit, convert=convert)
            add("rle", path, mode=mode, decoded=limit, error=9, convert=convert)

    for signature in (b"TBF", b"TPF", b"TAF", b"TFF", b"BAD"):
        for version in (8, 16, 19, 0x8000, 0xffff):
            header = signature + b"\0" + struct.pack("<HHIHH", version, 0, 8, 2, 2)
            payload = bytes(768) if version in (8, 19) else b""
            path = synthetic(f"header-{signature.decode()}-{version}.bin", header + payload + b"\x42\x17" * 4 + bytes(6))
            for palette in (0, 1):
                add("tbf", path, decoded=12, palette=palette)
                add("tbf", path, decoded=12, palette=palette, error=5)
    for name, payload in (("short-header.bin", b"TBF\0"), ("short-palette.bin", b"TBF\0" + struct.pack("<HHIHH", 8, 0, 8, 2, 2)),
                          ("short-trailer.bin", b"TBF\0" + struct.pack("<HHIHH", 16, 0, 8, 2, 2) + bytes(8)),
                          ("bad-ngs.bin", b"BAD\0" + bytes(4)), ("empty-ngs.bin", b"NGS\0" + bytes(4))):
        path = synthetic(name, payload)
        add("tbf", path, decoded=12, palette=1)
        add("ngs-seek", path, mode=2)
    (work / "fixtures.tsv").write_text("\n".join(rows) + "\n")
    print(f"{len(rows)} directed cases: {pools} NGS pools, {images} TBF images, {frames} TAF frames")


if __name__ == "__main__":
    main()
