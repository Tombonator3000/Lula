#!/usr/bin/env bash
# Durable headless project + repeatable analysis exports; never starts WET.EXE.
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ "${1:-}" == --install ]]; then
  "$repo_root/tools/ghidra/install-local.sh"
  shift
fi
if [[ $# -gt 0 ]]; then echo 'Usage: tools/decompile.sh [--install]' >&2; exit 2; fi
binary="$repo_root/original/app/WET.EXE"
[[ -f "$binary" ]] || { echo "Missing input: $binary" >&2; exit 2; }
export JAVA_HOME="$repo_root/local/tools/java/usr/lib/jvm/java-21-openjdk-amd64"
export PATH="$JAVA_HOME/bin:$PATH"
export GHIDRA_JAVA_HOME="$JAVA_HOME"
export GHIDRA_HEADLESS_MAXMEM=${GHIDRA_HEADLESS_MAXMEM:-1G}
ghidra_root=${GHIDRA_HOME:-"$repo_root/local/tools/ghidra_12.1.4_PUBLIC"}
[[ -x "$ghidra_root/support/analyzeHeadless" && -x "$JAVA_HOME/bin/javac" ]] || { echo 'Local Ghidra/JDK unavailable. Run tools/decompile.sh --install first.' >&2; exit 2; }
project_root="$repo_root/local/ghidra"
output_root="$repo_root/analysis/decompiled"
mkdir -p "$project_root" "$output_root"
python3 "$repo_root/tools/analyze_pe.py" "$binary"
# Limit analysis resources and persist the program. No -deleteProject flag.
input_hash=$(sha256sum "$binary" | cut -d ' ' -f 1)
if [[ -f "$project_root/Lula.gpr" ]]; then
  [[ -f "$project_root/input.sha256" && "$(cat "$project_root/input.sha256")" == "$input_hash" ]] || { echo 'Project input differs. Preserve/rename local/ghidra and re-import deliberately.' >&2; exit 2; }
  input_args=(-process "$(basename "$binary")" -noanalysis)
else
  input_args=(-import "$binary" -analysisTimeoutPerFile 1800)
fi
# A script failure must not be masked by an old successful export summary.
rm -f -- "$output_root/summary.json"
"$ghidra_root/support/analyzeHeadless" "$project_root" Lula \
  "${input_args[@]}" -max-cpu 2 \
  -scriptPath "$repo_root/tools/ghidra" \
  -postScript LulaExport.java "$output_root" \
  -log "$output_root/ghidra.log" -scriptlog "$output_root/export.log"
printf '%s\n' "$input_hash" > "$project_root/input.sha256"
python3 - "$output_root/summary.json" <<'PY'
import json,sys
s=json.load(open(sys.argv[1]))
if s['cancelled'] or s['decompiled']==0: raise SystemExit('Export was cancelled or contains no decompiled functions')
print(f"Ghidra export verified: {s['decompiled']} complete; {s['failed']} incomplete; buildable_source=False")
PY
python3 "$repo_root/tools/ghidra/summarize-export.py"
