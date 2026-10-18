#!/usr/bin/env bash
# Regenerates the recompiled C++ for Call of Duty 3 from default.xex on macOS (ARM64).
#
#   ./scripts/recompile.sh                 # regenerate C++ only
#   ./scripts/recompile.sh --build-tools   # rebuild XenonRecomp first
#   ./scripts/recompile.sh --switch-tables # regenerate the jump table TOML too
#   ./scripts/recompile.sh --build-lib     # also compile CoD3RecompLib
#   ./scripts/recompile.sh --build-host    # also compile CoD3 host executable
#   ./scripts/recompile.sh --iso <path>    # extract default.xex from ISO first if needed

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

BUILD_TOOLS=0
SWITCH_TABLES=0
BUILD_LIB=0
BUILD_HOST=0
ISO_PATH=""
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-tools|-BuildTools) BUILD_TOOLS=1; shift ;;
        --switch-tables|-SwitchTables) SWITCH_TABLES=1; shift ;;
        --build-lib|-BuildLib) BUILD_LIB=1; shift ;;
        --build-host|-BuildHost) BUILD_HOST=1; shift ;;
        --iso) ISO_PATH="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

XEX="$ROOT/CoD3RecompLib/private/default.xex"
CONFIG="$ROOT/CoD3RecompLib/config/CoD3.toml"
SWITCH_TOML="$ROOT/CoD3RecompLib/config/CoD3_switch_tables.toml"
PPC_DIR="$ROOT/CoD3RecompLib/ppc"
CONTEXT_HDR="$ROOT/tools/XenonRecomp/XenonUtils/ppc_context.h"
TOOL_BUILD="$ROOT/tools/build"
RECOMP="$TOOL_BUILD/XenonRecomp/XenonRecomp"
ANALYSE="$TOOL_BUILD/XenonAnalyse/XenonAnalyse"

mkdir -p "$ROOT/CoD3RecompLib/private"

# If default.xex is missing, check if an ISO was provided or sits in private/
if [[ ! -f "$XEX" ]]; then
    if [[ -z "$ISO_PATH" ]]; then
        ISO_CANDIDATE="$(find "$ROOT/CoD3RecompLib/private" "$ROOT" -maxdepth 2 -iname "*.iso" 2>/dev/null | head -n 1 || true)"
        if [[ -n "$ISO_CANDIDATE" ]]; then
            ISO_PATH="$ISO_CANDIDATE"
        fi
    fi
    if [[ -n "$ISO_PATH" && -f "$ISO_PATH" ]]; then
        echo "==> Extracting default.xex from $ISO_PATH"
        python3 "$ROOT/scripts/extract_iso.py" "$ISO_PATH" --extract default.xex "$XEX"
    elif [[ -f "$ROOT/game/default.xex" ]]; then
        echo "==> Copying default.xex from game/default.xex"
        cp "$ROOT/game/default.xex" "$XEX"
    fi
fi

if [[ ! -f "$XEX" ]]; then
    echo "Error: default.xex missing at $XEX. Copy it from your Xbox 360 disc dump or pass --iso <path>." >&2
    exit 1
fi

# --- 1. Host tools ---------------------------------------------------------
if [[ "$BUILD_TOOLS" -eq 1 || ! -x "$RECOMP" ]]; then
    echo "==> Building XenonRecomp and XenonAnalyse"
    cmake -S "$ROOT/tools/XenonRecomp" -B "$TOOL_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$TOOL_BUILD" --target XenonRecomp XenonAnalyse --parallel "$JOBS"
fi

# --- 2. Jump tables --------------------------------------------------------
if [[ "$SWITCH_TABLES" -eq 1 ]]; then
    echo "==> Detecting jump tables"
    "$ANALYSE" "$XEX" "$SWITCH_TOML"
    n="$(grep -c '^\[\[switch\]\]' "$SWITCH_TOML" || true)"
    echo "    $n switch tables"
fi

# --- 3. Recompile ----------------------------------------------------------
echo "==> Recompiling PowerPC to C++"
mkdir -p "$PPC_DIR"
rm -f "$PPC_DIR"/*

LOG="$ROOT/recomp.log"
"$RECOMP" "$CONFIG" "$CONTEXT_HDR" > "$LOG" 2>&1

DIAGNOSTICS="$(grep -v '^Recompiling functions' "$LOG" | sed '/^[[:space:]]*$/d' || true)"
FUNCTIONS="$(grep -h '^PPC_FUNC_IMPL' "$PPC_DIR"/ppc_recomp.*.cpp | wc -l | tr -d ' ')"
UNITS="$(find "$PPC_DIR" -maxdepth 1 -name '*.cpp' | wc -l | tr -d ' ')"

echo "    $FUNCTIONS functions across $UNITS translation units"
if [[ -n "$DIAGNOSTICS" ]]; then
    diag_count="$(printf "%s\n" "$DIAGNOSTICS" | wc -l | tr -d ' ')"
    echo "    $diag_count diagnostics (see $LOG):"
    printf "%s\n" "$DIAGNOSTICS" | head -n 20 | sed 's/^/      /'
else
    echo "    clean: every instruction translated"
fi

ERRORS="$(grep -h '// ERROR [0-9A-F]\+' "$PPC_DIR"/ppc_recomp.*.cpp | wc -l | tr -d ' ' || true)"
if [[ "$ERRORS" -gt 0 ]]; then
    echo "    $ERRORS branches out of their function (grep '// ERROR' in $PPC_DIR)"
fi

# --- 3b. The seams the recompiler cannot express -------------------------
echo "==> Patching the coroutine seam"
python3 "$ROOT/scripts/patch_recomp.py" main

# --- 4. Compile ------------------------------------------------------------
if [[ "$BUILD_LIB" -eq 1 || "$BUILD_HOST" -eq 1 ]]; then
    echo "==> Generating kernel import stubs"
    python3 "$ROOT/scripts/gen_import_stubs.py"

    echo "==> Compiling"
    BUILD="$ROOT/build"
    HOST_FLAG="-DCOD3_BUILD_HOST=OFF"
    if [[ "$BUILD_HOST" -eq 1 ]]; then
        HOST_FLAG="-DCOD3_BUILD_HOST=ON"
    fi
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release "$HOST_FLAG"
    if [[ "$BUILD_HOST" -eq 1 ]]; then
        cmake --build "$BUILD" --target CoD3 --parallel "$JOBS"
    else
        cmake --build "$BUILD" --target CoD3RecompLib --parallel "$JOBS"
    fi

    LIB="$BUILD/CoD3RecompLib/libCoD3RecompLib.a"
    if [[ -f "$LIB" ]]; then
        size_mb="$(du -m "$LIB" | cut -f1)"
        echo "    $LIB (${size_mb} MB)"
    fi
    EXE="$BUILD/CoD3Host/CoD3"
    if [[ -f "$EXE" ]]; then
        size_mb="$(du -m "$EXE" | cut -f1)"
        echo "    $EXE (${size_mb} MB)"
    fi
fi

echo "Done."
