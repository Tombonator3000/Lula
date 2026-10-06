#!/usr/bin/env python3
"""Prove an edited TAF travels through --mods into a presented game frame.

Uses the nonempty cursor frames as a neutral replacement: all four become
one solid RGB565 color at their original dimensions. No graphics are saved
to Git and originals/saves are never modified.
"""
import argparse
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("lula_assets", ROOT / "tools/assets.py")
assets = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = assets
spec.loader.exec_module(assets)


def read_rgb565(path):
    width, height, rgb = assets.parse_ppm(path.read_bytes())
    assert (width, height) == (640, 480)
    return [((rgb[i] >> 3) << 11) | ((rgb[i + 1] >> 2) << 5) | (rgb[i + 2] >> 3)
            for i in range(0, len(rgb), 3)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/game/lula")
    args = parser.parse_args()
    if not args.binary.is_file():
        parser.error("build the recompiled game first")
    runs = ROOT / "build/taf-runtime-check"
    runs.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=runs))
    source = (ROOT / "original/app/DATA/CURSOR/CURSOR.TAF").read_bytes()
    animation = assets.parse_taf(source)
    color = 0x1234
    mod = source
    expected = set()
    for index, frame in enumerate(animation.frames):
        width, height = struct.unpack_from("<HH", frame, 2)
        expected.add(width * height)
        mod = assets.replace_taf_frame(mod, index, struct.pack("<H", color) * (width * height))
    mods = work / "mods/DATA/CURSOR"
    mods.mkdir(parents=True)
    (mods / "CURSOR.TAF").write_bytes(mod)
    script = work / "input.txt"
    script.write_text("10000 move 505 88\n11000 click 505 88\n18000 move 320 240\n24000 quit\n")
    outputs = {}
    for name in ("original", "modified"):
        case = work / name
        frames = case / "frames"
        frames.mkdir(parents=True)
        env = dict(os.environ, LULA_HEADLESS="1", LULA_INPUT=str(script),
                   LULA_FRAMEDUMP=str(frames), LULA_FRAMEDUMP_MS="500")
        command = [str(args.binary.resolve()), "--save", str(case / "save")]
        if name == "modified":
            command += ["--mods", str(work / "mods")]
        command += ["--", "-novideo"]
        result = subprocess.run(command, env=env, capture_output=True, timeout=40)
        log = result.stdout + result.stderr
        (case / "run.log").write_bytes(log)
        if result.returncode or b"lula[trap]" in log or b"lula[fatal]" in log:
            raise RuntimeError(f"{name} runtime failed; see {case / 'run.log'}")
        captures = sorted(frames.glob("*.ppm"))
        if not captures:
            raise RuntimeError(f"{name} presented no frame")
        outputs[name] = [read_rgb565(p) for p in captures[-4:]]
        shutil.rmtree(frames)      # about 30 MB of PPM frames per run
    baseline_max = max(frame.count(color) for frame in outputs["original"])
    modified_counts = [frame.count(color) for frame in outputs["modified"]]
    # The cursor becomes exactly one original-size solid rectangle. Reject a
    # mere file-load success when it never reaches the presented pixels.
    delta = max(modified_counts) - baseline_max
    if delta not in expected:
        raise AssertionError(f"edited cursor not shown exactly: delta={delta}, expected={sorted(expected)}; {work}")
    report = dict(result="pass", source="DATA/CURSOR/CURSOR.TAF", frames_edited=len(animation.frames),
                  rgb565_color=color, added_presented_pixels=delta,
                  expected_cursor_pixels=sorted(expected), original_bytes=len(source), edited_bytes=len(mod),
                  runtime_dir=str(work), original_files_modified=False)
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
