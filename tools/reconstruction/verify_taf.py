#!/usr/bin/env python3
"""Compare portable C TAF APIs/CLI with independent Python resource tooling.

Build first with tools/build_reconstruction.sh, then run this script. Originals
are read only; all outputs live in a temporary directory. No game launch here.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location("lula_assets", ROOT / "tools/assets.py")
assert _spec and _spec.loader
assets = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = assets
_spec.loader.exec_module(assets)


def run(argv: list[str | Path], *, okay: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([str(arg) for arg in argv], text=True, capture_output=True)
    if okay != (result.returncode == 0):
        raise AssertionError(f"Unexpected result ({result.returncode}): {argv}\n"
                             f"{result.stdout}{result.stderr}")
    # UBSan keeps running after a report (exit status 0), and an ASan abort on
    # an expected rejection also exits non-zero, so check the output itself.
    if "runtime error:" in result.stderr or "AddressSanitizer" in result.stderr:
        raise AssertionError(f"Sanitizer report: {argv}\n{result.stderr}")
    return result


def metadata_equal(before: bytes, after: bytes) -> None:
    original, edited = assets.parse_taf(before), assets.parse_taf(after)
    assert original.prefix == edited.prefix, "TAF prefix changed"
    assert original.footer[:len(original.frames) * 4] == edited.footer[:len(edited.frames) * 4], \
        "TAF placement metadata changed"
    for left, right in zip(original.frames, edited.frames):
        assert left[:6] == right[:6], "Frame mode/dimensions changed"
        if len(left) > 14:
            assert left[14] == right[14], "Frame opaque flag changed"


def verify(cli: Path, checker: Path) -> dict:
    paths = sorted(p for p in (ROOT / "original/app").rglob("*") if p.suffix.lower() == ".taf")
    if len(paths) != 78:
        raise AssertionError(f"Expected 78 original TAF files; found {len(paths)}")
    frames = nonempty = empty = changed = rejected = 0
    sentinel_path = sentinel_index = None
    with tempfile.TemporaryDirectory(prefix="lula-taf-verify-") as directory:
        work = Path(directory)
        for file_index, path in enumerate(paths):
            data = path.read_bytes()
            animation = assets.parse_taf(data)
            expected = bytearray()
            for index, frame in enumerate(animation.frames):
                width, height = struct.unpack_from("<HH", frame, 2)
                if width * height:
                    expected.extend(assets.decode_rle(frame[15:], width * height * 2))
                    nonempty += 1
                else:
                    empty += 1
                    sentinel_path, sentinel_index = path, index
            oracle = work / f"pixels-{file_index:03}.rgb565"
            oracle.write_bytes(expected)
            result = run([checker, path, oracle])
            assert int(result.stdout.split()[0]) == len(animation.frames)
            frames += len(animation.frames)
            info = json.loads(run([cli, "inspect-taf", path]).stdout)
            assert info["count"] == len(animation.frames)
            assert info["trailer_table_count"] == struct.unpack_from("<H", data, 784)[0]
            for index, frame in enumerate(animation.frames):
                if struct.unpack_from("<HH", frame, 2) == (0, 0):
                    continue
                ppm = work / "frame.ppm"
                output = work / "unchanged.taf"
                run([cli, "decode-taf", path, str(index), ppm])
                assert ppm.read_bytes() == assets.taf_frame_to_ppm(data, index)
                run([cli, "import-taf", path, str(index), ppm, output])
                assert output.read_bytes() == data, "PPM unchanged import altered original"
                ppm.unlink()
                output.unlink()
            if path.name.lower() in {"butch.taf", "cursor.taf", "bird1.taf", "std_but.taf"}:
                visible = [i for i, f in enumerate(animation.frames)
                           if struct.unpack_from("<HH", f, 2) != (0, 0)]
                for index in sorted({visible[0], visible[-1]}):
                    ppm = bytearray(assets.taf_frame_to_ppm(data, index))
                    ppm[-1] ^= 128  # Change a represented RGB565 component.
                    editing = work / "edit.ppm"
                    output = work / "edited.taf"
                    decoded = work / "edited.ppm"
                    editing.write_bytes(ppm)
                    run([cli, "import-taf", path, str(index), editing, output])
                    reference = assets.ppm_to_taf(bytes(ppm), data, index)
                    assert output.read_bytes() == reference, "C/Python changed output differs"
                    metadata_equal(data, reference)
                    run([cli, "decode-taf", output, str(index), decoded])
                    assert decoded.read_bytes() == assets.taf_frame_to_ppm(reference, index)
                    changed += 1
                    output.unlink()
                    decoded.unlink()
        assert (frames, nonempty, empty, changed) == (989, 988, 1, 8)

        # C CLI validation and exclusive-output behavior, on temporary files.
        template = paths[0].read_bytes()
        parsed = assets.parse_taf(template)
        template_path = work / "template.taf"
        template_path.write_bytes(template)
        width, height = struct.unpack_from("<HH", parsed.frames[0], 2)
        ppm = work / "edit.ppm"
        ppm.write_bytes(assets.taf_frame_to_ppm(template, 0))
        output = work / "out.taf"
        for index in ["-1", str(len(parsed.frames)), "65536", "not-an-index"]:
            run([cli, "decode-taf", template_path, index, work / "bad.ppm"], okay=False)
            run([cli, "import-taf", template_path, index, ppm, output], okay=False)
            assert not output.exists()
            rejected += 2
        wrong_ppm = work / "wrong.ppm"
        wrong_ppm.write_bytes(f"P6\n{width + 1} {height}\n255\n".encode()
                              + bytes((width + 1) * height * 3))
        run([cli, "import-taf", template_path, "0", wrong_ppm, output], okay=False)
        assert not output.exists()
        rejected += 1
        for content in [b"P6\n0 0\n255\n", b"P6\n1 1\n255\n\0\0", b"P6\n1 1\n256\n\0\0\0"]:
            wrong_ppm.write_bytes(content)
            run([cli, "import-taf", template_path, "0", wrong_ppm, output], okay=False)
            assert not output.exists()
            rejected += 1
        mutations = [template[:14], b"BAD!" + template[4:], template[:-1], template + b"\0"]
        for offset, fmt, value in [(4, "H", 17), (6, "H", 0), (8, "I", 0),
                                  (780, "I", len(template) + 1), (780, "I", 784), (780, "I", 785),
                                  (784, "H", 0), (784, "H", 3),
                                  (784, "H", 3 - parsed.table_count),
                                  (parsed.offsets[0], "H", 0),
                                  (parsed.offsets[0] + 2, "H", 0),
                                  (parsed.offsets[0] + 6, "I", parsed.offsets[0] + 14),
                                  (parsed.offsets[0] + 10, "I", parsed.offsets[0] + 16),
                                  (parsed.offsets[0] + 15, "H", 0x8000)]:
            data = bytearray(template)
            struct.pack_into("<" + fmt, data, offset, value)
            mutations.append(bytes(data))
        if struct.unpack_from("<H", template, 784)[0] == 2:
            data = bytearray(template)
            struct.pack_into("<I", data, len(data) - len(parsed.frames) * 4, parsed.offsets[0] + 1)
            mutations.append(bytes(data))
        for i, data in enumerate(mutations):
            malformed = work / f"malformed-{i:02}.taf"
            malformed.write_bytes(data)
            run([cli, "inspect-taf", malformed], okay=False)
            try:
                assets.parse_taf(data)
            except assets.FormatError:
                pass
            else:
                raise AssertionError(f"Python accepted malformed TAF case {i}")
            rejected += 1
        sentinel_output = work / "sentinel.ppm"
        run([cli, "decode-taf", sentinel_path, str(sentinel_index), sentinel_output], okay=False)
        run([cli, "import-taf", sentinel_path, str(sentinel_index), ppm, output], okay=False)
        assert not sentinel_output.exists() and not output.exists()
        rejected += 2
        output.write_bytes(b"preserve existing output")
        run([cli, "import-taf", template_path, "0", ppm, output], okay=False)
        assert output.read_bytes() == b"preserve existing output"
        existing_ppm = work / "existing.ppm"
        existing_ppm.write_bytes(b"preserve existing image")
        run([cli, "decode-taf", template_path, "0", existing_ppm], okay=False)
        assert existing_ppm.read_bytes() == b"preserve existing image"
        symlink = work / "link.taf"
        symlink.symlink_to(output)
        run([cli, "import-taf", template_path, "0", ppm, symlink], okay=False)
        assert symlink.is_symlink() and output.read_bytes() == b"preserve existing output"
        run([cli, "import-taf", template_path, "0", ppm, template_path], okay=False)
        assert template_path.read_bytes() == template
        rejected += 4
    return {"files": len(paths), "frames": frames, "nonempty_ppm_roundtrips": nonempty,
            "RGB565_PPM_values_verified": 65536,
            "empty_sentinels_preserved": empty, "unchanged_api_roundtrips": frames,
            "changed_C_Python_byte_matches": changed, "rejection_checks": rejected,
            "cli_sha256": hashlib.sha256(cli.read_bytes()).hexdigest(),
            "api_check_sha256": hashlib.sha256(checker.read_bytes()).hexdigest()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, default=ROOT / "build/reconstruction/lula-resource-cli")
    parser.add_argument("--api-check", type=Path, default=ROOT / "build/reconstruction/lula-taf-check")
    args = parser.parse_args()
    print(json.dumps(verify(args.cli.resolve(), args.api_check.resolve()), indent=2))


if __name__ == "__main__":
    main()
