#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
if [ "$#" -ne 1 ]; then
    echo 'Usage: tools/unpack.sh "/path/to/Lula - The Sexy Empire (Repack) v2.rar"' >&2
    exit 1
fi
archive=$(realpath "$1")
extractor=${LULA_INNOEXTRACT:-}
if [ -z "$extractor" ]; then
    if command -v innoextract >/dev/null 2>&1; then
        extractor=$(command -v innoextract)
    else
        extractor="$root/local/tools/innoextract/usr/bin/innoextract"
    fi
fi
command -v 7z >/dev/null 2>&1 || { echo '7z is required' >&2; exit 1; }
[ -x "$extractor" ] || { echo 'innoextract is required; see docs/setup.md' >&2; exit 1; }
[ ! -e "$root/original" ] || { echo 'original/ already exists; verify it or use a fresh checkout.' >&2; exit 1; }
# Validate the supplied archive against the known three-file outer package.
python3 - "$archive" "$root/analysis/original-manifest.json" <<'PY'
import hashlib, json, sys
from pathlib import Path
archive, manifest = map(Path, sys.argv[1:])
expected = json.loads(manifest.read_text())['source_archive']
h = hashlib.sha256()
with archive.open('rb') as f:
    for block in iter(lambda: f.read(1024*1024), b''):
        h.update(block)
if archive.stat().st_size != expected['size'] or h.hexdigest() != expected['sha256']:
    sys.exit('This is not the recorded source archive. Review a different package before extraction.')
PY
work=$(mktemp -d "$root/local-unpack.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
7z x -y -o"$work" "$archive"
"$extractor" --extract --output-dir "$root/original" "$work/Lula - The Sexy Empire_Repack_v2_Setup.exe"
python3 "$root/tools/project.py" verify
