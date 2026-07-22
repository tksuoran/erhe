#!/usr/bin/env python3
"""Run the Circular_ring_buffer_algorithm long-running smoke test.

The smoke test is GoogleTest-driven but takes its time budget and RNG seed
from environment variables (ERHE_STRESS_SECONDS, ERHE_STRESS_SEED). This
wrapper picks a random seed if --seed is not supplied, sets the env vars,
and always prints the seed so a failing run can be reproduced via
--seed <N>.

Usage (Windows; remember `py -3`, not `python` -- see AGENTS.md):
    py -3 scripts\\run_circular_ring_buffer_smoke_test.py [--seconds N] [--seed N]

On macOS / Linux:
    python3 scripts/run_circular_ring_buffer_smoke_test.py [--seconds N] [--seed N]

Requires that erhe_circular_ring_buffer_tests has been built first.
"""

from __future__ import annotations

import argparse
import os
import random
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent

# Search order for the test executable. The Windows Debug build (the one
# scripts/configure_vs2026_opengl.bat produces) is the typical case;
# Release and the macOS / Linux build trees are tried as fallbacks so the
# same script works after any of the supported configures.
EXE_CANDIDATES = [
    REPO_ROOT / "build_vs2026_opengl" / "src" / "erhe" / "circular_ring_buffer" / "test" / "Debug" / "erhe_circular_ring_buffer_tests.exe",
    REPO_ROOT / "build_vs2026_opengl" / "src" / "erhe" / "circular_ring_buffer" / "test" / "Release" / "erhe_circular_ring_buffer_tests.exe",
    REPO_ROOT / "build_xcode_opengl" / "src" / "erhe" / "circular_ring_buffer" / "test" / "Debug" / "erhe_circular_ring_buffer_tests",
    REPO_ROOT / "build_xcode_metal" / "src" / "erhe" / "circular_ring_buffer" / "test" / "Debug" / "erhe_circular_ring_buffer_tests",
    REPO_ROOT / "build" / "src" / "erhe" / "circular_ring_buffer" / "test" / "erhe_circular_ring_buffer_tests",
]

TEST_FILTER = "CircularRingBufferAlgorithm.stress_test_long_running_mixed_workload"


def find_test_exe() -> Path:
    for candidate in EXE_CANDIDATES:
        if candidate.exists():
            return candidate
    tried = "\n".join(f"  {p}" for p in EXE_CANDIDATES)
    sys.stderr.write(
        "ERROR: erhe_circular_ring_buffer_tests not found. Tried:\n"
        f"{tried}\n"
        "Build it first, e.g.:\n"
        "  cmake --build build_vs2026_opengl --target erhe_circular_ring_buffer_tests --config Debug\n"
    )
    raise SystemExit(1)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stress-run the circular ring buffer smoke test.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--seconds",
        type=int,
        default=10,
        help="Minimum wall-clock seconds to run the random-mix phase.",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=None,
        help="RNG seed (default: randomized via os.urandom; printed below).",
    )
    args = parser.parse_args()
    if args.seconds < 0:
        parser.error("--seconds must be >= 0")
    if args.seed is not None and args.seed < 0:
        parser.error("--seed must be >= 0")
    return args


def main() -> int:
    args = parse_args()

    # std::mt19937 takes a 32-bit seed; pick within that range so user
    # --seed values and randomized seeds are interchangeable.
    seed = args.seed if args.seed is not None else random.SystemRandom().randrange(2**32)

    print(f"[circular_ring_buffer smoke] seed   : {seed}")
    print(f"[circular_ring_buffer smoke] seconds: {args.seconds}")
    sys.stdout.flush()

    exe = find_test_exe()

    env = os.environ.copy()
    env["ERHE_STRESS_SECONDS"] = str(args.seconds)
    env["ERHE_STRESS_SEED"]    = str(seed)

    completed = subprocess.run(
        [str(exe), f"--gtest_filter={TEST_FILTER}"],
        env=env,
        check=False,
    )
    return completed.returncode


if __name__ == "__main__":
    sys.exit(main())
