#!/usr/bin/env python3
"""Verify the original game and create separate, editable run directories."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = ROOT / 'original' / 'app'
MANIFEST = ROOT / 'analysis' / 'original-manifest.json'


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def inventory(archive=None):
    if not (ORIGINAL / 'WET.EXE').is_file():
        raise ValueError('Original files missing. Run git lfs pull or unpack first.')
    files = [{'path': p.relative_to(ORIGINAL).as_posix(),
              'size': p.stat().st_size, 'sha256': sha256(p)}
             for p in sorted(ORIGINAL.rglob('*')) if p.is_file()]
    data = {'schema': 1, 'root': 'original/app', 'files': files,
            'directories': [p.relative_to(ORIGINAL).as_posix()
                            for p in sorted(ORIGINAL.rglob('*')) if p.is_dir()],
            'file_count': len(files), 'total_bytes': sum(f['size'] for f in files)}
    if archive:
        path = Path(archive).resolve()
        data['source_archive'] = {'name': path.name, 'size': path.stat().st_size,
                                  'sha256': sha256(path)}
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    print(f"Recorded {len(files)} files, {data['total_bytes']} bytes")


def verify():
    data = json.loads(MANIFEST.read_text(encoding='utf-8'))
    expected = {f['path']: f for f in data['files']}
    actual = {p.relative_to(ORIGINAL).as_posix(): p
              for p in ORIGINAL.rglob('*') if p.is_file()}
    errors = [f'Missing: {p}' for p in sorted(expected.keys() - actual.keys())]
    errors += [f'Unexpected: {p}' for p in sorted(actual.keys() - expected.keys())]
    for path in sorted(expected.keys() & actual.keys()):
        record, file = expected[path], actual[path]
        if file.stat().st_size != record['size'] or sha256(file) != record['sha256']:
            errors.append(f'Changed or LFS pointer: {path}')
    if errors:
        raise ValueError('\n'.join(errors))
    print(f"PASS: {len(expected)} original files match SHA-256 manifest")
    return data


def configure_ini(text, updates):
    """Change only the global ddraw section and preserve comments/other sections."""
    lines = text.splitlines()
    in_global = False
    seen = set()
    output = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('['):
            if in_global:
                output.extend(f'{key}={value}' for key, value in updates.items() if key not in seen)
            in_global = stripped.lower() == '[ddraw]'
        match = re.match(r'^\s*([^;#=]+?)\s*=', line) if in_global else None
        if match and match.group(1).lower() in updates:
            key = match.group(1).lower()
            line = f'{key}={updates[key]}'
            seen.add(key)
        output.append(line)
    if in_global:
        output.extend(f'{key}={value}' for key, value in updates.items() if key not in seen)
    if not any(line.strip().lower() == '[ddraw]' for line in lines):
        raise ValueError('Missing [ddraw] section')
    return '\r\n'.join(output) + '\r\n'


def stage(profile, output=None):
    data = verify()
    destination = (Path(output) if output else ROOT / 'build' / f'runtime-{profile}').resolve()
    # Do not allow an output path to overwrite the baseline or tool/project tree.
    if ROOT not in destination.parents or (ROOT / 'build') not in destination.parents:
        raise ValueError('Run directories must be below this project/build/')
    if destination.exists():
        raise ValueError(f'Destination exists; preserve saves by choosing a new path: {destination}')
    shutil.copytree(ORIGINAL, destination)
    for directory in data['directories']:
        (destination / directory).mkdir(parents=True, exist_ok=True)
    # The repack nests its empty directories at DATA/DATA, but WET.EXE
    # statically references DATA\SAVE and DATA\DATABASE. Prepare those only
    # in run copies, preserving the exact extracted baseline.
    for directory in ('DATA/SAVE', 'DATA/DATABASE'):
        (destination / directory).mkdir(parents=True, exist_ok=True)
    if profile == 'hd1080':
        ini = destination / 'ddraw.ini'
        updates = {'width': '1920', 'height': '1080', 'fullscreen': 'false',
                   'windowed': 'true', 'maintas': 'true', 'boxing': 'false',
                   'shader': '', 'savesettings': '0'}
        ini.write_bytes(configure_ini(ini.read_bytes().decode('utf-8'), updates).encode('utf-8'))
    (destination / 'start-windows.cmd').write_bytes(
        b'@echo off\r\ncd /d "%~dp0"\r\nWET.EXE %*\r\n')
    launcher = destination / 'start-linux.sh'
    launcher.write_text('#!/bin/sh\nset -eu\ncd "$(dirname "$0")"\n'
                        'command -v wine >/dev/null 2>&1 || { echo "Wine is required to run WET.EXE" >&2; exit 1; }\n'
                        'export WINEDLLOVERRIDES="ddraw=n,b${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"\n'
                        'exec wine WET.EXE "$@"\n', encoding='utf-8')
    launcher.chmod(0o755)
    print(f'Staged {profile}: {destination}')
    if profile == 'hd1080':
        print('1920x1080 presentation scaling; original logical resolution and game code are unchanged.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    inv = commands.add_parser('inventory')
    inv.add_argument('--archive')
    commands.add_parser('verify')
    staging = commands.add_parser('stage')
    staging.add_argument('--profile', choices=['original', 'hd1080'], default='original')
    staging.add_argument('--output')
    args = parser.parse_args()
    try:
        if args.command == 'inventory':
            inventory(args.archive)
        elif args.command == 'verify':
            verify()
        else:
            stage(args.profile, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
