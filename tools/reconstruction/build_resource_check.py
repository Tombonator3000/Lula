#!/usr/bin/env python3
"""Link the fixture verifier against an already built lula_core.

Build the game with CMake first. This tool does not change CMake or fncheck.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=root / 'build/game')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    link = build / 'CMakeFiles/lula-fncheck.dir/link.txt'
    if not link.is_file():
        parser.error(f'{link} is missing; build lula-fncheck with CMake first')
    tokens = shlex.split(link.read_text())
    objects = [build / token for token in tokens
               if token.endswith('.o') and 'CMakeFiles/lula_core.dir/' in token]
    if not objects or any(not path.is_file() for path in objects):
        parser.error('lula_core objects are missing; complete the CMake build first')
    try:
        output_arg = tokens.index('-o')
    except ValueError:
        parser.error('cannot read libraries from the fncheck link command')
    libraries = tokens[output_arg + 2:]
    cc = shlex.split(os.environ.get('CC', ''))
    if not cc:
        bundled = root / 'local/tools/game-gcc'
        cc = [str(bundled)] if bundled.is_file() else [tokens[0]]
    output = build / 'lula-resource-check'
    command = cc + ['-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root / 'src/runtime'),
                    str(root / 'tools/reconstruction/resource_check.c')]
    command += [str(path) for path in objects]
    command += ['-o', str(output)] + libraries
    subprocess.run(command, cwd=build, check=True)
    print(f'Built {output}')


if __name__ == '__main__':
    main()
