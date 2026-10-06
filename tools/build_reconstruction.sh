#!/usr/bin/env bash
# Build reconstructed resource code as runnable host tooling and Windows x86.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
host_prefix="$root/local/tools/host/usr"
windows_prefix="$root/local/tools/mingw/usr"
sanitize=false
case "${1:-}" in
    '') ;;
    --sanitize) sanitize=true ;;
    *) echo 'Usage: tools/build_reconstruction.sh [--sanitize]' >&2; exit 2 ;;
esac
[[ $# -le 1 ]] || { echo 'Too many arguments' >&2; exit 2; }
host_compiler=${LULA_HOST_CC:-}
if [[ -z "$host_compiler" ]]; then
    if command -v cc >/dev/null 2>&1; then
        host_compiler=$(command -v cc)
    elif [[ -x "$host_prefix/bin/x86_64-linux-gnu-gcc-15" ]]; then
        host_compiler="$host_prefix/bin/x86_64-linux-gnu-gcc-15"
    else
        echo 'Host C11 compiler required; set LULA_HOST_CC or see docs/reconstruction/resources.md' >&2
        exit 1
    fi
fi
host_options=()
if [[ "$host_compiler" == "$host_prefix/"* ]]; then
    host_options+=(-B"$host_prefix/libexec/gcc/x86_64-linux-gnu/15/"
                  -B/usr/libexec/gcc/x86_64-linux-gnu/15/
                  -B"$host_prefix/lib/gcc/x86_64-linux-gnu/15/" -B/usr/lib/x86_64-linux-gnu/)
fi
output_root="$root/build/reconstruction"
mkdir -p "$output_root"
if $sanitize; then
    host_output="$output_root/lula-resource-cli-sanitize"
    host_options+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    if [[ "$host_compiler" == "$host_prefix/"* ]]; then
        host_options+=(-Wl,-rpath,"$host_prefix/lib/x86_64-linux-gnu")
    fi
else
    host_output="$output_root/lula-resource-cli"
    host_options+=(-O2)
fi
sources=("$root/reconstruction/resources/lula_resources.c" "$root/reconstruction/resources/resource_cli.c")
"$host_compiler" "${host_options[@]}" -std=c11 -Wall -Wextra -Werror \
    "${sources[@]}" -o "$host_output"
file "$host_output"
if ! $sanitize; then
    windows_compiler=${LULA_CC:-}
    if [[ -z "$windows_compiler" ]]; then
        if command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
            windows_compiler=$(command -v i686-w64-mingw32-gcc)
        elif [[ -x "$windows_prefix/bin/i686-w64-mingw32-gcc-posix" ]]; then
            windows_compiler="$windows_prefix/bin/i686-w64-mingw32-gcc-posix"
        fi
    fi
    if [[ -n "$windows_compiler" ]]; then
        windows_options=()
        if [[ "$windows_compiler" == "$windows_prefix/"* ]]; then
            export PATH="$windows_prefix/bin:$PATH"
            windows_options+=(--sysroot="$windows_prefix" -B"$windows_prefix/i686-w64-mingw32/bin/"
                              -B"$windows_prefix/i686-w64-mingw32/lib/" -I"$windows_prefix/i686-w64-mingw32/include")
        fi
        "$windows_compiler" "${windows_options[@]}" -std=c11 -D__USE_MINGW_ANSI_STDIO=1 -Wall -Wextra -Werror -O2 \
            "${sources[@]}" -o "$output_root/lula-resource-cli.exe"
        file "$output_root/lula-resource-cli.exe"
    else
        echo 'Windows x86 cross-build unavailable; host build is complete.' >&2
    fi
fi
echo 'Built reconstructed resource tooling. This does not rebuild or launch WET.EXE.'
