#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
local_prefix="$root/local/tools/mingw/usr"
compiler=${LULA_CC:-}
if [ -z "$compiler" ]; then
    if command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
        compiler=$(command -v i686-w64-mingw32-gcc)
    elif [ -x "$local_prefix/bin/i686-w64-mingw32-gcc-posix" ]; then
        compiler="$local_prefix/bin/i686-w64-mingw32-gcc-posix"
    else
        echo "Install an i686 MinGW-w64 C compiler or set LULA_CC." >&2
        exit 1
    fi
fi
options=()
if [[ "$compiler" == "$local_prefix/"* ]]; then
    export PATH="$local_prefix/bin:$PATH"
    options+=(--sysroot="$local_prefix" -B"$local_prefix/i686-w64-mingw32/bin/"
              -B"$local_prefix/i686-w64-mingw32/lib/" -I"$local_prefix/i686-w64-mingw32/include")
fi
mkdir -p "$root/build/native"
"$compiler" "${options[@]}" -std=c11 -Wall -Wextra -Werror -Os \
    "$root/native/toolchain_probe.c" -o "$root/build/native/lula-toolchain-probe.exe" -lddraw -ldxguid
file "$root/build/native/lula-toolchain-probe.exe"
echo "Build complete: native tooling probe. WET.EXE has not been rebuilt."
