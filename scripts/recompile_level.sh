#!/usr/bin/env bash
# Recompiles one level's DLL (sp/<name>/<name>.dll) into CoD3RecompLib/levels/<name>.
#
#   ./scripts/recompile_level.sh saint_lo
#   ./scripts/recompile_level.sh --all
#   ./scripts/recompile_level.sh --all --game-root /path/to/game

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

LEVEL=""
ALL=0
GAME_ROOT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all|-All)
            ALL=1
            shift
            ;;
        --game-root|-GameRoot)
            GAME_ROOT="${2:-}"
            shift 2
            ;;
        -*)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
        *)
            LEVEL="$1"
            shift
            ;;
    esac
done

RECOMP="$ROOT/tools/build/XenonRecomp/XenonRecomp"
ANALYSE="$ROOT/tools/build/XenonAnalyse/XenonAnalyse"
SCAN="$ROOT/tools/build-scan/CoD3Scan"
CONTEXT_HDR="$ROOT/tools/XenonRecomp/XenonUtils/ppc_context.h"

for tool in "$RECOMP" "$ANALYSE" "$SCAN"; do
    if [[ ! -x "$tool" ]]; then
        echo "ERROR: $tool is missing; build the tools first (./scripts/recompile.sh --build-tools)" >&2
        exit 1
    fi
done

if [[ -z "$GAME_ROOT" ]]; then
    if [[ -f "$ROOT/game/default.xex" ]]; then
        GAME_ROOT="$ROOT/game"
    elif [[ -f "$ROOT/game.path" ]]; then
        GAME_ROOT="$(tr -d '\r\n' < "$ROOT/game.path")"
    fi
    if [[ -z "$GAME_ROOT" ]]; then
        echo "ERROR: the installed game was not found; pass --game-root /path/to/game" >&2
        exit 1
    fi
fi

LEVELS=()
if [[ "$ALL" -eq 1 ]]; then
    for dir in "$GAME_ROOT/sp"/*/; do
        [[ -d "$dir" ]] || continue
        lname="$(basename "$dir")"
        if [[ -f "$dir/$lname.dll" ]]; then
            LEVELS+=("$lname")
        fi
    done
elif [[ -n "$LEVEL" ]]; then
    LEVELS=("$LEVEL")
else
    echo "ERROR: name a level, or pass --all" >&2
    exit 1
fi

mkdir -p "$ROOT/CoD3RecompLib/private"

for name in "${LEVELS[@]}"; do
    echo "==> $name"
    DLL="$GAME_ROOT/sp/$name/$name.dll"
    if [[ ! -f "$DLL" ]]; then
        echo "ERROR: $DLL is missing" >&2
        exit 1
    fi
    PRIVATE="$ROOT/CoD3RecompLib/private/$name.dll"
    cp -f "$DLL" "$PRIVATE"

    # 1. The helpers, from the scan.
    scan_text="$("$SCAN" "$PRIVATE" 2>&1 || true)"
    helpers="$(printf "%s\n" "$scan_text" | sed -nE 's/^((rest|save)(gprlr|fpr|vmx)_(14|64)_address = 0x[0-9A-F]+).*/\1/p' || true)"
    helper_count="$(printf "%s\n" "$helpers" | grep -c '=' || true)"
    if [[ "$helper_count" -lt 4 ]]; then
        echo "ERROR: $name: the save/restore helpers were not all found:" >&2
        echo "$scan_text" >&2
        exit 1
    fi
    info="$(printf "%s\n" "$scan_text" | sed -nE 's/.*base[[:space:]]+= (0x[0-9A-F]+).*/\1/p' | head -n 1)"
    entry="$(printf "%s\n" "$scan_text" | sed -nE 's/.*entry_point = (0x[0-9A-F]+).*/\1/p' | head -n 1)"
    [[ -n "$info" ]] || info="?"
    [[ -n "$entry" ]] || entry="?"

    CONFIG="$ROOT/CoD3RecompLib/config/$name.toml"
    SWITCH_TOML="$ROOT/CoD3RecompLib/config/${name}_switch_tables.toml"
    OUT="$ROOT/CoD3RecompLib/levels/$name"
    mkdir -p "$OUT"
    rm -f "$OUT"/*

    # 2. Jump tables.
    "$ANALYSE" "$PRIVATE" "$SWITCH_TOML" >/dev/null

    # 3. Recompile until the function boundaries are right.
    KEPT_PATH="$ROOT/CoD3RecompLib/config/${name}_functions.toml"
    LOG="$ROOT/recomp_$name.log"

    python3 - "$ROOT" "$name" "$info" "$entry" "$helpers" <<'PY'
import os
import re
import subprocess
import sys

root, name, info, entry, helpers_raw = sys.argv[1:6]
helpers = [line.strip() for line in helpers_raw.splitlines() if line.strip()]
config_dir = os.path.join(root, "CoD3RecompLib", "config")
config_path = os.path.join(config_dir, f"{name}.toml")
kept_path = os.path.join(config_dir, f"{name}_functions.toml")
out_dir = os.path.join(root, "CoD3RecompLib", "levels", name)
private_dll = os.path.join(root, "CoD3RecompLib", "private", f"{name}.dll")
log_path = os.path.join(root, f"recomp_{name}.log")
recomp_bin = os.path.join(root, "tools", "build", "XenonRecomp", "XenonRecomp")
scan_bin = os.path.join(root, "tools", "build-scan", "CoD3Scan")
context_hdr = os.path.join(root, "tools", "XenonRecomp", "XenonUtils", "ppc_context.h")

func_re = re.compile(r"^\s*\{\s*address\s*=\s*(0x[0-9A-F]+),\s*size\s*=\s*0x[0-9A-F]+\s*\},.*$")

def read_kept():
    lines = []
    if os.path.isfile(kept_path):
        with open(kept_path, "r", encoding="utf-8") as f:
            for line in f:
                if func_re.match(line):
                    lines.append(line.rstrip())
    return lines

def merge_functions(scan_lines, hand_lines):
    by_addr = {}
    for line in list(scan_lines) + list(hand_lines):
        m = re.search(r"address = (0x[0-9A-F]+)", line)
        if m:
            by_addr[m.group(1)] = line
    return [by_addr[k] for k in sorted(by_addr.keys())]

def invoke_recompile(functions):
    lines = [
        f"# Call of Duty 3 (Xbox 360) - the {name} level's own code: sp\\{name}\\{name}.dll",
        f"# Generated by scripts/recompile_level.ps1 from CoD3Scan; image base {info}, entry {entry}.",
        "",
        "[main]",
        f'file_path = "../private/{name}.dll"',
        f'out_directory_path = "../levels/{name}"',
        f'switch_table_file_path = "{name}_switch_tables.toml"',
        "",
        "skip_lr = false",
        "skip_msr = false",
        "ctr_as_local = false",
        "xer_as_local = false",
        "reserved_as_local = false",
        "cr_as_local = false",
        "non_argument_as_local = false",
        "non_volatile_as_local = false",
        "",
    ] + helpers + [
        "",
        "invalid_instructions = [",
        "    { data = 0x00000000, size = 4 }, # padding",
        "]",
    ]
    if functions:
        lines += [
            "",
            "# Bounded by CoD3Scan --fixfuncs from the recompiler's reports and by",
            f"# scripts/level_functions.py (config/{name}_functions.toml).",
            "functions = [",
        ] + functions + ["]"]

    with open(config_path, "w", encoding="ascii") as f:
        f.write("\n".join(lines) + "\n")

    for fname in os.listdir(out_dir):
        fpath = os.path.join(out_dir, fname)
        if os.path.isfile(fpath):
            os.remove(fpath)

    with open(log_path, "w", encoding="utf-8") as logf:
        res = subprocess.run([recomp_bin, config_path, context_hdr], cwd=config_dir, stdout=logf, stderr=subprocess.STDOUT)
    if res.returncode != 0:
        raise SystemExit(f"XenonRecomp failed for {name} (exit {res.returncode}). See {log_path}")

kept = read_kept()
if kept:
    print(f"    {len(kept)} function boundaries kept from {kept_path}")
functions = merge_functions([], kept)

for p in range(1, 5):
    invoke_recompile(functions)
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        log_text = f.read()
    if "is trying to jump outside function" not in log_text:
        break
    fix_out = subprocess.check_output([scan_bin, private_dll, "--fixfuncs", log_path], text=True, stderr=subprocess.STDOUT)
    found = [line.rstrip() for line in fix_out.splitlines() if func_re.match(line)]
    if not found:
        raise SystemExit(f"{name}: functions to fix were reported but CoD3Scan found none")
    functions = merge_functions(functions + found, kept)
    print(f"    pass {p}: {len(found)} function boundaries fixed")

for p in range(1, 7):
    report = subprocess.check_output([sys.executable, os.path.join(root, "scripts", "level_functions.py"), name], text=True).strip()
    print(f"    {report}")
    if ": 0 function extents" in report:
        break
    kept = read_kept()
    functions = merge_functions(functions, kept)
    invoke_recompile(functions)
PY

    errors="$(grep -h '// ERROR [0-9A-F]\+' "$OUT"/ppc_recomp.*.cpp 2>/dev/null | wc -l | tr -d ' ' || true)"
    if [[ "$errors" -gt 0 ]]; then
        echo "    $errors branches out of their function remain (grep '// ERROR' in $OUT); pieces of padding, or extend by hand in $KEPT_PATH"
    fi
    count="$(grep -h '^PPC_FUNC_IMPL' "$OUT"/ppc_recomp.*.cpp 2>/dev/null | wc -l | tr -d ' ' || true)"
    unknown="$(grep -c 'Unrecognized instruction' "$LOG" 2>/dev/null || true)"
    echo "    $count functions, $unknown unrecognized instructions"

    # 4. Names and seams.
    python3 "$ROOT/scripts/rename_level.py" "$name"
    python3 "$ROOT/scripts/patch_recomp.py" "$name"
done

echo "Done. Reconfigure the build (cmake) so the new level libraries are picked up."
