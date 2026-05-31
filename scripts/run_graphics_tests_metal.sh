#!/bin/bash
# Configure + build + run the erhe_graphics tests on the Metal (build_xcode_metal) config.
#
# Tests are always run SERIALLY (never in parallel).
#
# Usage:
#   scripts/run_graphics_tests_metal.sh                  # configure, build, run
#   SKIP_CONFIGURE=1 scripts/run_graphics_tests_metal.sh # skip configure (faster re-runs)
#   GPU_MODE=ctest   scripts/run_graphics_tests_metal.sh # run each GPU test in its own
#                                                        # process (results survive a hard
#                                                        # abort / crash in one test)
#   CONFIG=Debug     scripts/run_graphics_tests_metal.sh # build config (default Debug)
#
# After a run, hand me the printed "run" log path (logs/gtests_run_*.log).

set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build_xcode_metal"
CONFIG="${CONFIG:-Debug}"
GPU_MODE="${GPU_MODE:-single}"     # single | ctest
SKIP_CONFIGURE="${SKIP_CONFIGURE:-0}"

TS="$(date +%Y%m%d_%H%M%S)"
mkdir -p logs
CONFIGURE_LOG="logs/gtests_configure_${TS}.log"
BUILD_LOG="logs/gtests_build_${TS}.log"
RUN_LOG="logs/gtests_run_${TS}.log"

bar() { printf '\n========== %s ==========\n' "$*"; }

# ---------- 1/3 configure ----------
if [ "$SKIP_CONFIGURE" = "1" ]; then
    bar "[1/3] Configure -- SKIPPED (SKIP_CONFIGURE=1)"
else
    bar "[1/3] Configure ($BUILD_DIR, Metal)"
    echo "(full log: $CONFIGURE_LOG)"
    if bash scripts/configure_xcode_metal.sh > "$CONFIGURE_LOG" 2>&1; then
        echo "Configure OK"
    else
        echo "Configure FAILED -- tail:"
        tail -25 "$CONFIGURE_LOG"
        exit 1
    fi
fi

# ---------- 2/3 build ----------
bar "[2/3] Build test targets ($CONFIG)"
echo "Targets: erhe_graphics_gpu_tests, erhe_graphics_tests"
echo "(full log: $BUILD_LOG)"
cmake --build "$BUILD_DIR" \
    --target erhe_graphics_gpu_tests \
    --target erhe_graphics_tests \
    --config "$CONFIG" > "$BUILD_LOG" 2>&1
BUILD_RC=$?
if [ "$BUILD_RC" -ne 0 ]; then
    echo "BUILD FAILED (rc=$BUILD_RC). Error lines:"
    grep -nE ': error:|clang: error:|Undefined symbol|ld: error|\*\* BUILD FAILED' "$BUILD_LOG" | head -40
    echo "--- tail ---"
    tail -20 "$BUILD_LOG"
    exit 1
fi
echo "BUILD SUCCEEDED"

GPU_BIN="$(find "$BUILD_DIR" -type f -name erhe_graphics_gpu_tests ! -path '*dSYM*' | head -1)"
DEV_BIN="$(find "$BUILD_DIR" -type f -name erhe_graphics_tests ! -path '*dSYM*' | head -1)"
if [ -z "$GPU_BIN" ] || [ -z "$DEV_BIN" ]; then
    echo "ERROR: test binaries not found after build"
    echo "  gpu=$GPU_BIN"
    echo "  dev=$DEV_BIN"
    exit 1
fi

# ---------- 3/3 run (serial) ----------
bar "[3/3] Run tests (serial; full log: $RUN_LOG)"
: > "$RUN_LOG"

echo "--- deviceless: erhe_graphics_tests ---" | tee -a "$RUN_LOG"
"$DEV_BIN" 2>&1 | tee -a "$RUN_LOG"
DEV_RC=${PIPESTATUS[0]}

if [ "$GPU_MODE" = "ctest" ]; then
    echo "--- GPU: Gpu_test.* via ctest (serial, one process per test) ---" | tee -a "$RUN_LOG"
    ctest --test-dir "$BUILD_DIR" -C "$CONFIG" -R 'Gpu_test\.' \
          --no-tests=error --output-on-failure --timeout 180 2>&1 | tee -a "$RUN_LOG"
    GPU_RC=${PIPESTATUS[0]}
else
    echo "--- GPU: erhe_graphics_gpu_tests (single process) ---" | tee -a "$RUN_LOG"
    "$GPU_BIN" 2>&1 | tee -a "$RUN_LOG"
    GPU_RC=${PIPESTATUS[0]}
fi

# ---------- summary ----------
bar "SUMMARY"
echo "deviceless (erhe_graphics_tests): rc=$DEV_RC"
echo "gpu (erhe_graphics_gpu_tests):    rc=$GPU_RC  [mode=$GPU_MODE]"
echo
echo "Result markers from run log:"
grep -E '^\[==========\] [0-9]+ test|^\[  PASSED  \]|^\[  FAILED  \] [0-9]+ test|^\[  SKIPPED \] [0-9]+ test|tests passed|tests failed out of' "$RUN_LOG" \
    || echo "  (no gtest/ctest summary line -- process likely aborted mid-run)"

if [ "$GPU_MODE" = "single" ] && [ "$GPU_RC" -ge 128 ]; then
    SIG=$((GPU_RC - 128))
    echo
    echo "WARNING: GPU test process killed by signal $SIG (6 = SIGABRT)."
    echo "         Tests after the abort point did NOT run."
    echo "         Re-run with:  GPU_MODE=ctest SKIP_CONFIGURE=1 $0"
    echo "         to get complete per-test results that survive the abort."
fi

echo
echo "Logs:"
echo "  configure: $CONFIGURE_LOG"
echo "  build:     $BUILD_LOG"
echo "  run:       $RUN_LOG   <-- paste this one back"

if [ "$DEV_RC" -eq 0 ] && [ "$GPU_RC" -eq 0 ]; then
    echo
    echo "RESULT: all tests passed"
    exit 0
fi
echo
echo "RESULT: failures present (see above)"
exit 1
