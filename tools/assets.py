#!/usr/bin/env python3
"""Verified Lula container and RGB565 tools; no third-party dependencies.

NGS payloads can be replaced and packed with recalculated sizes and offsets.
TAF splitting/packing preserves every byte, but deliberately rejects edits.
PPM is a lossless, editable interchange for TBF's existing RGB565 pixels.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


class FormatError(ValueError):
    """Malformed data or an unverified format variant."""


MAX_PIXELS = 16_777_216


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@dataclass(frozen=True)
class Entry:
    type_word: int
    payload: bytes
    offset: int = 0


@dataclass(frozen=True)
class NGS:
    version_word: int
    entries: tuple[Entry, ...]


def parse_ngs(data: bytes) -> NGS:
    if len(data) < 8 or data[:4] != b"NGS\0":
        raise FormatError("Expected an NGS container header")
    version, count = struct.unpack_from("<HH", data, 4)
    table_start = len(data) - count * 4
    if table_start < 8:
        raise FormatError("NGS index extends before the header")
    offsets = struct.unpack_from(f"<{count}I", data, table_start)
    cursor, entries = 8, []
    for index, offset in enumerate(offsets):
        if cursor + 6 > table_start or offset != cursor + 6:
            raise FormatError(f"NGS entry {index}: inconsistent payload offset")
        size, type_word = struct.unpack_from("<IH", data, cursor)
        if offset + size > table_start:
            raise FormatError(f"NGS entry {index}: truncated payload")
        entries.append(Entry(type_word, data[offset:offset + size], offset))
        cursor = offset + size
    if cursor != table_start:
        raise FormatError("Unexpected bytes between NGS payloads and index")
    return NGS(version, tuple(entries))


def pack_ngs(archive: NGS) -> bytes:
    if not 0 <= archive.version_word <= 65535 or len(archive.entries) > 65535:
        raise FormatError("NGS version/count exceeds a 16-bit field")
    result = bytearray(struct.pack("<4sHH", b"NGS\0", archive.version_word,
                                   len(archive.entries)))
    offsets = []
    for entry in archive.entries:
        if not 0 <= entry.type_word <= 65535 or len(entry.payload) > 0xFFFFFFFF:
            raise FormatError("NGS entry fields exceed their integer limits")
        offsets.append(len(result) + 6)
        result.extend(struct.pack("<IH", len(entry.payload), entry.type_word))
        result.extend(entry.payload)
    if len(result) + len(offsets) * 4 > 0xFFFFFFFF:
        raise FormatError("NGS exceeds its 32-bit index capacity")
    result.extend(struct.pack(f"<{len(offsets)}I", *offsets))
    return bytes(result)


def pixel_size(width: int, height: int, *, empty: bool = False) -> int:
    if width * height > MAX_PIXELS or (not empty and (width == 0 or height == 0)):
        raise FormatError("Unsupported image dimensions")
    return width * height * 2


def decode_rle(data: bytes, expected_size: int) -> bytes:
    """Observed 16-bit codec: 0..0xefff repeat, 0xf000..0xffff literal."""
    out = bytearray()
    cursor = 0
    while cursor < len(data):
        if cursor + 2 > len(data):
            raise FormatError("Truncated RLE command")
        code = struct.unpack_from("<H", data, cursor)[0]
        cursor += 2
        count = 65536 - code if code >= 0xF000 else code
        if len(out) + count * 2 > expected_size:
            raise FormatError("RLE command expands beyond image dimensions")
        if code >= 0xF000:
            size = count * 2
            if cursor + size > len(data):
                raise FormatError("Truncated RLE literal")
            out.extend(data[cursor:cursor + size])
            cursor += size
        else:
            if cursor + 2 > len(data):
                raise FormatError("Truncated RLE repeated pixel")
            out.extend(data[cursor:cursor + 2] * count)
            cursor += 2
    if len(out) != expected_size:
        raise FormatError("RLE decoded size differs from image dimensions")
    return bytes(out)


def encode_rle(pixels: bytes) -> bytes:
    if len(pixels) % 2:
        raise FormatError("RGB565 pixels must contain complete 16-bit words")
    words = [item[0] for item in struct.iter_unpack("<H", pixels)]
    result = bytearray()
    cursor = 0
    while cursor < len(words):
        end = cursor + 1
        while end < len(words) and words[end] == words[cursor] and end - cursor < 0xEFFF:
            end += 1
        if end - cursor >= 2:
            result.extend(struct.pack("<HH", end - cursor, words[cursor]))
            cursor = end
            continue
        start = cursor
        cursor += 1
        while cursor < len(words) and cursor - start < 4096:
            if cursor + 1 < len(words) and words[cursor] == words[cursor + 1]:
                break
            cursor += 1
        literals = words[start:cursor]
        result.extend(struct.pack("<H", 65536 - len(literals)))
        result.extend(struct.pack(f"<{len(literals)}H", *literals))
    return bytes(result)


@dataclass(frozen=True)
class TBF:
    version_word: int
    mode: int
    width: int
    height: int
    pixels: bytes


def parse_tbf(data: bytes) -> TBF:
    if len(data) < 16 or data[:4] != b"TBF\0":
        raise FormatError("Expected a TBF image header")
    version, mode, raw_size, width, height = struct.unpack_from("<HHIHH", data, 4)
    expected = pixel_size(width, height)
    if version != 16 or raw_size != expected:
        raise FormatError("Unverified TBF version or inconsistent raw size")
    if mode == 0:
        if len(data) != expected + 16:
            raise FormatError("Raw TBF has unexpected bytes or truncated pixels")
        pixels = data[16:]
    elif mode == 2:
        pixels = decode_rle(data[16:], expected)
    else:
        raise FormatError(f"TBF mode {mode} has not been verified")
    return TBF(version, mode, width, height, pixels)


def tbf_to_ppm(data: bytes) -> bytes:
    image = parse_tbf(data)
    rgb = bytearray()
    for (word,) in struct.iter_unpack("<H", image.pixels):
        # Bit replication provides the full display range and roundtrips exactly.
        r, g, b = (word >> 11) & 31, (word >> 5) & 63, word & 31
        rgb.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return f"P6\n{image.width} {image.height}\n255\n".encode("ascii") + rgb


def parse_ppm(data: bytes) -> tuple[int, int, bytes]:
    cursor, tokens = 0, []
    whitespace = b" \t\r\n\v\f"
    while len(tokens) < 4:
        while cursor < len(data) and data[cursor] in whitespace:
            cursor += 1
        if cursor < len(data) and data[cursor] == 35:
            newline = data.find(b"\n", cursor)
            if newline == -1:
                raise FormatError("Unterminated PPM comment")
            cursor = newline + 1
            continue
        start = cursor
        while cursor < len(data) and data[cursor] not in whitespace:
            cursor += 1
        if start == cursor:
            raise FormatError("Truncated PPM header")
        tokens.append(data[start:cursor])
    if tokens[0] != b"P6" or tokens[3] != b"255":
        raise FormatError("Only binary P6 PPM with maxval 255 is supported")
    try:
        width, height = int(tokens[1]), int(tokens[2])
    except ValueError as error:
        raise FormatError("Invalid PPM dimensions") from error
    if not 0 < width <= 65535 or not 0 < height <= 65535:
        raise FormatError("PPM dimensions exceed TBF's fields")
    pixel_size(width, height)
    # Exactly one separator; raster bytes themselves may be whitespace or '#'.
    if cursor >= len(data) or data[cursor] not in whitespace:
        raise FormatError("Missing PPM raster separator")
    cursor += 2 if data[cursor:cursor + 2] == b"\r\n" else 1
    rgb = data[cursor:]
    if len(rgb) != width * height * 3:
        raise FormatError("PPM raster length differs from its dimensions")
    return width, height, rgb


def ppm_to_tbf(ppm: bytes, template: bytes) -> bytes:
    image = parse_tbf(template)
    width, height, rgb = parse_ppm(ppm)
    if (width, height) != (image.width, image.height):
        raise FormatError("Dimension changes require engine/layout analysis; keep template dimensions")
    pixels = bytearray()
    for i in range(0, len(rgb), 3):
        r, g, b = rgb[i:i + 3]
        pixels.extend(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
    if pixels == image.pixels:
        return template  # Preserve original, potentially noncanonical RLE commands.
    payload = bytes(pixels) if image.mode == 0 else encode_rle(bytes(pixels))
    return template[:16] + payload


@dataclass(frozen=True)
class TAF:
    prefix: bytes
    frames: tuple[bytes, ...]
    footer: bytes
    offsets: tuple[int, ...]


def parse_taf(data: bytes) -> TAF:
    if len(data) < 787 or data[:4] != b"TAF\0":
        raise FormatError("Expected a complete TAF header")
    version, count, raw_size = struct.unpack_from("<HHI", data, 4)
    if version != 16 or not count:
        raise FormatError("Unverified TAF version or empty animation")
    first = struct.unpack_from("<I", data, 780)[0]
    if not 784 <= first <= len(data):
        raise FormatError("Invalid TAF first-frame offset")
    cursor, total, frames, offsets = first, 0, [], []
    for i in range(count):
        if cursor + 14 > len(data):
            raise FormatError(f"TAF frame {i}: truncated header")
        mode, width, height, end, payload_start = struct.unpack_from("<HHHII", data, cursor)
        expected = pixel_size(width, height, empty=True)
        total += expected
        # BUTCH.TAF ends with a verified 14-byte, 0x0 sentinel frame.
        empty_sentinel = width == height == 0 and end == payload_start == cursor + 15
        if empty_sentinel:
            end -= 1
        if mode != 2 or payload_start != cursor + 15 or end > len(data) or end < cursor + 14:
            raise FormatError(f"TAF frame {i}: unverified structure")
        if not empty_sentinel:
            if cursor + 15 > end:
                raise FormatError(f"TAF frame {i}: missing flag byte")
            decode_rle(data[payload_start:end], expected)
        offsets.append(cursor)
        frames.append(data[cursor:end])
        cursor = end
    if total != raw_size:
        raise FormatError("TAF total decoded size does not match header")
    footer = data[cursor:]
    if len(footer) not in (count * 4, count * 8):
        raise FormatError("Unverified TAF trailer structure")
    if len(footer) == count * 8:
        if struct.unpack_from(f"<{count}I", footer, count * 4) != tuple(offsets):
            raise FormatError("TAF optional index does not match frame offsets")
    return TAF(data[:first], tuple(frames), footer, tuple(offsets))


def inspect(data: bytes) -> dict:
    result = {"bytes": len(data), "sha256": digest(data)}
    if data[:4] == b"NGS\0":
        archive = parse_ngs(data)
        result.update(format="NGS", version_word=archive.version_word, count=len(archive.entries),
                      entries=[{"index": i, "type_word": e.type_word, "offset": e.offset,
                                "bytes": len(e.payload), "signature": e.payload[:4].hex()}
                               for i, e in enumerate(archive.entries)])
    elif data[:4] == b"TBF\0":
        image = parse_tbf(data)
        result.update(format="TBF", mode=image.mode, width=image.width, height=image.height,
                      rgb565_sha256=digest(image.pixels))
    elif data[:4] == b"TAF\0":
        animation = parse_taf(data)
        result.update(format="TAF", count=len(animation.frames),
                      entries=[{"index": i, "offset": o, "bytes": len(f),
                                "width": struct.unpack_from("<H", f, 2)[0],
                                "height": struct.unpack_from("<H", f, 4)[0]}
                               for i, (o, f) in enumerate(zip(animation.offsets, animation.frames))])
    elif len(data) >= 12 and data[:4] == b"RIFF" and data[8:12] in (b"AVI ", b"WAVE"):
        if struct.unpack_from("<I", data, 4)[0] + 8 != len(data):
            raise FormatError("RIFF size differs from file size")
        result.update(format="AVI" if data[8:12] == b"AVI " else "WAV")
    else:
        raise FormatError("Unsupported file signature")
    return result


def unpack(data: bytes, target: Path) -> dict:
    if target.exists():
        raise FormatError("Extraction destination exists; choose a new directory")
    parts = []
    manifest = {"schema": "lula-assets-v1", "source_sha256": digest(data)}
    if data[:4] == b"NGS\0":
        archive = parse_ngs(data)
        manifest.update(format="NGS", version_word=archive.version_word)
        for i, e in enumerate(archive.entries):
            extension = "tbf" if e.payload[:4] == b"TBF\0" else "wav" if e.payload[:4] == b"RIFF" and e.payload[8:12] == b"WAVE" else "bin"
            parts.append((f"{i:04d}.{extension}", e.payload, {"type_word": e.type_word}))
    elif data[:4] == b"TAF\0":
        animation = parse_taf(data)
        manifest.update(format="TAF", frame_count=len(animation.frames), edits_supported=False)
        parts.append(("prefix.bin", animation.prefix, {}))
        parts.extend((f"{i:04d}.frame", f, {}) for i, f in enumerate(animation.frames))
        parts.append(("footer.bin", animation.footer, {}))
    else:
        raise FormatError("Unpack supports verified NGS and TAF containers")
    target.mkdir(parents=True)
    manifest["entries"] = []
    for filename, payload, metadata in parts:
        (target / filename).write_bytes(payload)
        manifest["entries"].append({"file": filename, "bytes": len(payload),
                                    "sha256": digest(payload), **metadata})
    (target / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def repack(source: Path) -> bytes:
    manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != "lula-assets-v1":
        raise FormatError("Unsupported manifest schema")
    payloads, names = [], set()
    for entry in manifest["entries"]:
        name = entry["file"]
        if not isinstance(name, str) or Path(name).name != name or name in names:
            raise FormatError("Unsafe or duplicate manifest filename")
        names.add(name)
        path = source / name
        if path.is_symlink() or not path.is_file():
            raise FormatError("Payload must be an existing regular file without symlinks")
        payloads.append(path.read_bytes())
    if manifest["format"] == "NGS":
        for payload in payloads:
            if payload[:4] == b"TBF\0":
                parse_tbf(payload)
            elif payload[:4] == b"RIFF":
                inspect(payload)
        return pack_ngs(NGS(manifest["version_word"], tuple(
            Entry(e["type_word"], p) for e, p in zip(manifest["entries"], payloads))))
    if manifest["format"] == "TAF":
        if len(payloads) != manifest["frame_count"] + 2:
            raise FormatError("TAF manifest part count changed")
        for e, p in zip(manifest["entries"], payloads):
            if digest(p) != e["sha256"]:
                raise FormatError("TAF edits are not supported; preserve frame/metadata bytes")
        result = b"".join(payloads)
        if digest(result) != manifest["source_sha256"]:
            raise FormatError("TAF parts no longer reproduce the original file")
        parse_taf(result)
        return result
    raise FormatError("Unsupported manifest format")


def write_new(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as handle:
        handle.write(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    command = sub.add_parser("list", help="Inspect verified asset metadata as JSON")
    command.add_argument("source", type=Path)
    command = sub.add_parser("unpack", help="Split NGS/TAF without changing original files")
    command.add_argument("source", type=Path)
    command.add_argument("destination", type=Path)
    command = sub.add_parser("pack", help="Rebuild an extracted container into a new file")
    command.add_argument("source", type=Path)
    command.add_argument("destination", type=Path)
    command = sub.add_parser("export-tbf", help="Export a TBF to binary PPM")
    command.add_argument("source", type=Path)
    command.add_argument("destination", type=Path)
    command = sub.add_parser("import-tbf", help="Import same-sized PPM using an original TBF template")
    command.add_argument("source", type=Path)
    command.add_argument("template", type=Path)
    command.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "list":
            print(json.dumps(inspect(args.source.read_bytes()), indent=2))
        elif args.command == "unpack":
            manifest = unpack(args.source.read_bytes(), args.destination)
            print(json.dumps({"format": manifest["format"], "parts": len(manifest["entries"])}))
        elif args.command == "pack":
            data = repack(args.source)
            write_new(args.destination, data)
            print(json.dumps({"bytes": len(data), "sha256": digest(data)}))
        elif args.command == "export-tbf":
            write_new(args.destination, tbf_to_ppm(args.source.read_bytes()))
        elif args.command == "import-tbf":
            write_new(args.destination, ppm_to_tbf(args.source.read_bytes(), args.template.read_bytes()))
        return 0
    except (FormatError, OSError, ValueError, KeyError, TypeError, struct.error) as error:
        print(f"Asset error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
