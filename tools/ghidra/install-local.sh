#!/usr/bin/env bash
# Pinned local toolchain; does not install packages globally or execute the game.
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
tool_root="$repo_root/local/tools"
download_root="$tool_root/downloads"
ghidra_zip=ghidra_12.1.4_PUBLIC_20260921.zip
ghidra_hash=ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db
java_version='21.0.12.1+1-1~26.04.4'
mkdir -p "$download_root" "$tool_root/java"
if [[ ! -f "$download_root/$ghidra_zip" ]]; then
  curl --fail --location --retry 3 --output "$download_root/$ghidra_zip" "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/$ghidra_zip"
fi
printf '%s  %s\n' "$ghidra_hash" "$download_root/$ghidra_zip" | sha256sum --check -
if [[ ! -d "$tool_root/ghidra_12.1.4_PUBLIC" ]]; then
  python3 - "$download_root/$ghidra_zip" "$tool_root" <<'PY'
from pathlib import Path
import sys,zipfile
root=Path(sys.argv[2])
with zipfile.ZipFile(sys.argv[1]) as archive:
    for member in archive.infolist():
        destination=root/member.filename
        if not destination.resolve().is_relative_to(root.resolve()): raise ValueError('Unsafe ZIP path')
        archive.extract(member,root)
        mode=(member.external_attr >> 16)&0o777
        if mode: destination.chmod(mode)
PY
fi
if [[ ! -x "$tool_root/java/usr/lib/jvm/java-21-openjdk-amd64/bin/javac" ]]; then
  (cd "$download_root" && apt download "openjdk-21-jdk-headless=$java_version" "openjdk-21-jre-headless=$java_version")
  printf '%s  %s\n' \
    a8c8d09f0c4d5e48a5d97a275f658dcfc72061d9d5b33d93db9c2ee45f74d0e7 "$download_root/openjdk-21-jdk-headless_${java_version}_amd64.deb" \
    174105c57728ea7652d2a49b5af2f529277dbef51c02417b931e792e54fb2b29 "$download_root/openjdk-21-jre-headless_${java_version}_amd64.deb" | sha256sum --check -
  dpkg-deb --extract "$download_root/openjdk-21-jdk-headless_${java_version}_amd64.deb" "$tool_root/java"
  dpkg-deb --extract "$download_root/openjdk-21-jre-headless_${java_version}_amd64.deb" "$tool_root/java"
fi
# Debian Java packages contain absolute /etc links; keep their configuration local.
python3 - "$tool_root/java" <<'PYLOCAL'
from pathlib import Path
import os,sys
root=Path(sys.argv[1]).resolve()
for path in root.rglob('*'):
    if path.is_symlink():
        target=path.readlink()
        replacement=root/str(target).lstrip('/')
        if target.is_absolute() and replacement.exists():
            path.unlink()
            path.symlink_to(os.path.relpath(replacement,path.parent))
PYLOCAL
"$tool_root/java/usr/lib/jvm/java-21-openjdk-amd64/bin/java" -version
printf 'Toolchain ready under %s\n' "$tool_root"
