#!/bin/bash
# Configure + build + run the erhe_graphics tests on a Metal config:
# build_xcode_metal (Metal 3, default) or build_xcode_metal4 (METAL_BACKEND=metal4).
#
# Tests are always run SERIALLY (never in parallel).
#
# Usage:
#   scripts/run_graphics_tests_metal.sh                  # configure, build, run
#   SKIP_CONFIGURE=1 scripts/run_graphics_tests_metal.sh # skip configure (faster re-runs)
#   GPU_MODE=ctest   scripts/run_graphics_tests_metal.sh # run each GPU test case in its own
#                                                        # process via ctest (results survive
#                                                        # a hard abort / crash in one test;
#                                                        # reconfigures the tree with
#                                                        # -DERHE_GPU_TESTS_PER_PROCESS=ON)
#   SHUFFLE=1        scripts/run_graphics_tests_metal.sh # single mode: run the cases in a
#                                                        # random order (--gtest_shuffle) to
#                                                        # find order dependencies; the seed
#                                                        # is printed. SEED=<N> repeats one.
#   CONFIG=Debug     scripts/run_graphics_tests_metal.sh # build config (default Debug)
#   METAL_BACKEND=metal4 scripts/run_graphics_tests_metal.sh # Metal 4 backend
#                                                        # (build_xcode_metal4)
#
# After a run, hand me the printed "run" log path (logs/gtests_run_*.log).

set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

METAL_BACKEND="${METAL_BACKEND:-metal}"  # metal | metal4
case "$METAL_BACKEND" in
    metal|metal4) ;;
    *) echo "METAL_BACKEND must be metal or metal4 (got '$METAL_BACKEND')"; exit 1 ;;
esac
BUILD_DIR="build_xcode_${METAL_BACKEND}"
CONFIG="${CONFIG:-Debug}"
GPU_MODE="${GPU_MODE:-single}"     # single | ctest
case "$GPU_MODE" in
    single|ctest) ;;
    *) echo "GPU_MODE must be single or ctest (got '$GPU_MODE')"; exit 1 ;;
esac
SHUFFLE="${SHUFFLE:-0}"
SEED="${SEED:-}"
SKIP_CONFIGURE="${SKIP_CONFIGURE:-0}"
if [ -n "$SEED" ] && [ "$SHUFFLE" != "1" ]; then
    echo "SEED=$SEED needs SHUFFLE=1 (a seed only orders a shuffled run)"
    exit 1
fi
if [ "$SHUFFLE" = "1" ] && [ "$GPU_MODE" != "single" ]; then
    echo "SHUFFLE=1 needs GPU_MODE=single (ctest runs one case per process)"
    exit 1
fi

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
    bar "[1/3] Configure ($BUILD_DIR, $METAL_BACKEND)"
    echo "(full log: $CONFIGURE_LOG)"
    PER_PROCESS=OFF
    if [ "$GPU_MODE" = "ctest" ]; then
        PER_PROCESS=ON
    fi
    if bash "scripts/configure_xcode_${METAL_BACKEND}.sh" "-DERHE_GPU_TESTS_PER_PROCESS=${PER_PROCESS}" > "$CONFIGURE_LOG" 2>&1; then
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

# The binaries of the built configuration: an Xcode tree holds one per
# configuration (bin/Debug, bin/Release), and a search would pick any.
GPU_BIN="$BUILD_DIR/bin/$CONFIG/erhe_graphics_gpu_tests"
DEV_BIN="$BUILD_DIR/bin/$CONFIG/erhe_graphics_tests"
if [ ! -x "$GPU_BIN" ] || [ ! -x "$DEV_BIN" ]; then
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
    # Per-case ctest tests exist only in a tree configured with
    # -DERHE_GPU_TESTS_PER_PROCESS=ON (the configure step above passes it).
    # Every case of this binary, whatever its fixture: the discovered tests
    # are named <suite>.<case> after the binary's own list, which keeps the
    # other gpu binaries' cases out.
    echo "--- GPU: erhe_graphics_gpu_tests via ctest (serial, one process per case) ---" | tee -a "$RUN_LOG"
    GPU_CASES_REGEX="$("$GPU_BIN" --gtest_list_tests | awk '/^[^ ]/ { suite = $1 } /^  / { print "^" suite $1 "$" }' | sed 's/\./\\./g' | paste -sd '|' -)"
    ctest --test-dir "$BUILD_DIR" -C "$CONFIG" -L gpu -R "$GPU_CASES_REGEX" \
          --no-tests=error --output-on-failure --timeout 180 2>&1 | tee -a "$RUN_LOG"
    GPU_RC=${PIPESTATUS[0]}
else
    GPU_ARGS=()
    if [ "$SHUFFLE" = "1" ]; then
        if [ -z "$SEED" ]; then
            SEED=$(( (RANDOM % 99999) + 1 ))
        fi
        GPU_ARGS+=("--gtest_shuffle" "--gtest_random_seed=${SEED}")
        echo "--- GPU: erhe_graphics_gpu_tests (single process, shuffled, seed ${SEED}) ---" | tee -a "$RUN_LOG"
    else
        echo "--- GPU: erhe_graphics_gpu_tests (single process) ---" | tee -a "$RUN_LOG"
    fi
    "$GPU_BIN" ${GPU_ARGS[@]+"${GPU_ARGS[@]}"} 2>&1 | tee -a "$RUN_LOG"
    GPU_RC=${PIPESTATUS[0]}
fi

# ---------- summary ----------
bar "SUMMARY"
echo "deviceless (erhe_graphics_tests): rc=$DEV_RC"
echo "gpu (erhe_graphics_gpu_tests):    rc=$GPU_RC  [mode=$GPU_MODE]"
if [ "$SHUFFLE" = "1" ]; then
    echo "shuffle seed: $SEED  (repeat with SEED=$SEED SHUFFLE=1)"
fi
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
