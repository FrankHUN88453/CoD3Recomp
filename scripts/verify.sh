#!/usr/bin/env bash
# Checks the recompiler against Xenia's PowerPC instruction tests on macOS (ARM64).
#
#   ./scripts/verify.sh              # the instructions this project added
#   ./scripts/verify.sh --all        # every test Xenia has
#   ./scripts/verify.sh --fetch      # re-fetch Xenia test sources

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

ALL=0
FETCH=0
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"

for arg in "$@"; do
    case "$arg" in
        --all|-All) ALL=1 ;;
        --fetch|-Fetch) FETCH=1 ;;
        *) echo "Unknown option: $arg (use --all or --fetch)" >&2; exit 1 ;;
    esac
done

TESTS="$ROOT/tools/xenia-tests/src/xenia/cpu/ppc/testing"
TOOLS_DIR="$ROOT/tools"
BUILD="$ROOT/build"
TOOL_BUILD="$ROOT/tools/build"
HARNESS="$ROOT/tools/XenonRecomp/XenonTests"

COVERED=(
    vslh vsrah vsubshs vspltish vmaxsh vandc vminsh
    vpkswss vctuxs vavguh vsel vpkswus vsrh vrlh
    vpkshss vpkuhus
)

# --- 1. Test sources -------------------------------------------------------
if [[ "$FETCH" -eq 1 || ! -f "$TESTS/instr_add.s" ]]; then
    echo "==> Fetching Xenia instruction tests"
    CLONE="$TOOLS_DIR/xenia-tests"
    rm -rf "$CLONE"
    mkdir -p "$CLONE"
    git -C "$CLONE" init -q .
    git -C "$CLONE" remote add origin https://github.com/xenia-project/xenia.git
    git -C "$CLONE" config core.sparseCheckout true
    echo "src/xenia/cpu/ppc/testing/*" > "$CLONE/.git/info/sparse-checkout"
    git -C "$CLONE" fetch --depth 1 origin master -q
    git -C "$CLONE" checkout -q FETCH_HEAD
fi

# --- 2. Tools --------------------------------------------------------------
echo "==> Building the assembler and the recompiler"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --target PpcAsm --parallel "$JOBS"
cmake -S "$ROOT/tools/XenonRecomp" -B "$TOOL_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$TOOL_BUILD" --target XenonRecomp --parallel "$JOBS"

# --- 3. Assemble -----------------------------------------------------------
echo "==> Assembling"
BIN="$TESTS/bin"
rm -rf "$BIN"
mkdir -p "$BIN"
ASM="$BUILD/PpcAsm/PpcAsm"

clean=0
problems=0
for source in "$TESTS"/*.s; do
    name="$(basename "$source" .s)"
    if "$ASM" "$source" "$BIN/$name.o" "$BIN/$name.dis" 0x10000 >/dev/null 2>&1; then
        clean=$((clean + 1))
    else
        rm -f "$BIN/$name.o" "$BIN/$name.dis"
        problems=$((problems + 1))
    fi
done
echo "    $clean assembled cleanly, $problems had instructions the assembler cannot encode"

# --- 4. Select -------------------------------------------------------------
if [[ "$ALL" -eq 1 ]]; then
    SELECTED="$TESTS/bin"
else
    SELECTED="$TESTS/bin-covered"
    rm -rf "$SELECTED"
    mkdir -p "$SELECTED"
    for op in "${COVERED[@]}"; do
        for ext in o dis; do
            file="$BIN/instr_$op.$ext"
            if [[ -f "$file" ]]; then
                cp "$file" "$SELECTED/"
            fi
        done
    done
    count="$(find "$SELECTED" -maxdepth 1 -name '*.o' | wc -l | tr -d ' ')"
    echo "    testing the $count instructions this project added"
fi

# --- 5. Recompile and run --------------------------------------------------
echo "==> Recompiling the tests"
rm -f "$HARNESS"/*.cpp
"$TOOL_BUILD/XenonRecomp/XenonRecomp" "$SELECTED" "$HARNESS" >/dev/null

echo "==> Building and running"
cmake -S "$ROOT/tools/XenonRecomp" -B "$TOOL_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$TOOL_BUILD" --target XenonTests --parallel "$JOBS"

set +e
output="$("$TOOL_BUILD/XenonTests/XenonTests" 2>&1)"
failures="$(printf "%s\n" "$output" | grep "EXPECTED" || true)"
set -e

echo ""
if [[ -z "$failures" ]]; then
    echo "Every check passed: each instruction produced the registers the hardware does."
else
    fail_count="$(printf "%s\n" "$failures" | wc -l | tr -d ' ')"
    echo "$fail_count checks failed:"
    printf "%s\n" "$failures" | head -n 40 | sed 's/^/  /'
    exit 1
fi
