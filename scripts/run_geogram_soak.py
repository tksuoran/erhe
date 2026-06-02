#!/usr/bin/env python3
"""Push and run the geogram_soak harness on an attached ARM Android device.

geogram_soak is a headless reproduction of the intermittent main-loop hang
(doc/intermittent_main_loop_hang.md): N taskflow workers concurrently build the
editor's brush shapes through Geogram (sys:multithread), mirroring
Scene_builder::make_brushes, and validate each mesh after the join. The
corruption is ARM-only, so the useful runs are on-device -- but it is a plain
CLI program, so it runs via `adb shell` with NO headset / controllers /
immersive gate (a Samsung phone or a powered Quest both work).

Usage (run from the repo root with `py -3`, per CLAUDE.md):

    py -3 scripts/run_geogram_soak.py [options] -- [harness args]

    py -3 scripts/run_geogram_soak.py -- --workers 8 --multithread on --iters 5000
    py -3 scripts/run_geogram_soak.py --no-push -- --workers 1 --iters 5000
    py -3 scripts/run_geogram_soak.py --timeout 600 -- --iters 0   # run forever, 10 min cap

Options:
    --build-dir DIR  CMake build dir holding the arm64 ELF (default build_android_soak)
    --serial S       adb device serial (default: ANDROID_SERIAL env, else the
                     sole attached device)
    --no-push        skip pushing the ELF (it is unchanged between A/B configs)
    --timeout S      kill the on-device run after S seconds (default: none)
    --                everything after this is passed verbatim to geogram_soak

Exit code mirrors the harness: 0 CLEAN, 2 corruption detected, 1 usage/other.
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
REMOTE_PATH = "/data/local/tmp/geogram_soak"
EXIT_MARKER = "GEOGRAM_SOAK_EXIT="


def find_adb() -> str:
    android_home = os.environ.get("ANDROID_HOME", "")
    if not android_home:
        local = os.environ.get("LOCALAPPDATA", "")
        if local and (Path(local) / "Android" / "Sdk").is_dir():
            android_home = str(Path(local) / "Android" / "Sdk")
    if not android_home:
        sys.exit("ERROR: ANDROID_HOME unset and %LOCALAPPDATA%/Android/Sdk missing")
    for name in ("adb.exe", "adb"):
        candidate = Path(android_home) / "platform-tools" / name
        if candidate.exists():
            return str(candidate)
    sys.exit(f"ERROR: adb not found under {android_home}/platform-tools")


ADB = find_adb()


def find_elf(build_dir: Path) -> Path:
    """Locate the geogram_soak ELF in the CMake build tree."""
    if not build_dir.is_absolute():
        build_dir = REPO_ROOT / build_dir
    candidates = [p for p in build_dir.rglob("geogram_soak")
                  if p.is_file() and p.suffix not in (".o", ".obj")]
    if not candidates:
        sys.exit(f"ERROR: geogram_soak ELF not found under {build_dir} "
                 f"(build it: ninja -C {build_dir} geogram_soak)")
    # Prefer one under src/geogram_soak/ if multiple match.
    for p in candidates:
        if "geogram_soak" in p.parent.name:
            return p
    return candidates[0]


def resolve_serial(explicit: str | None) -> list[str]:
    """Return the adb device-selection args ([] or ['-s', serial])."""
    if explicit:
        return ["-s", explicit]
    env = os.environ.get("ANDROID_SERIAL", "")
    if env:
        return ["-s", env]
    out = subprocess.run([ADB, "devices"], capture_output=True, text=True).stdout
    devices = [line.split("\t")[0] for line in out.splitlines()[1:]
               if line.strip() and line.endswith("device")]
    if len(devices) == 1:
        return ["-s", devices[0]]
    if not devices:
        sys.exit("ERROR: no attached adb device (check `adb devices`)")
    sys.exit(f"ERROR: multiple devices {devices}; pass --serial or set ANDROID_SERIAL")


def main() -> int:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--build-dir", default="build_android_soak")
    parser.add_argument("--serial", default=None)
    parser.add_argument("--no-push", action="store_true")
    parser.add_argument("--timeout", type=float, default=None)
    parser.add_argument("harness_args", nargs=argparse.REMAINDER,
                        help="args after -- are forwarded to geogram_soak")
    args = parser.parse_args()

    sys.stdout.reconfigure(line_buffering=True)
    sel = resolve_serial(args.serial)

    # Strip a leading '--' separator from REMAINDER.
    harness_args = args.harness_args
    if harness_args and harness_args[0] == "--":
        harness_args = harness_args[1:]

    if not args.no_push:
        elf = find_elf(Path(args.build_dir))
        print(f"[run_geogram_soak] pushing {elf.relative_to(REPO_ROOT)} -> {REMOTE_PATH}")
        push = subprocess.run([ADB, *sel, "push", str(elf), REMOTE_PATH],
                              capture_output=True, text=True)
        if push.returncode != 0:
            sys.stdout.write(push.stdout + push.stderr)
            return 1
        subprocess.run([ADB, *sel, "shell", "chmod", "755", REMOTE_PATH], check=False)

    remote_cmd = REMOTE_PATH + " " + " ".join(harness_args) + f'; echo "{EXIT_MARKER}$?"'
    print(f"[run_geogram_soak] adb shell {REMOTE_PATH} {' '.join(harness_args)}")
    proc = subprocess.Popen([ADB, *sel, "shell", remote_cmd],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, bufsize=1, errors="replace")

    harness_exit = None
    t0 = time.monotonic()
    assert proc.stdout is not None
    try:
        for line in proc.stdout:
            line = line.rstrip("\r\n")
            if line.startswith(EXIT_MARKER):
                try:
                    harness_exit = int(line[len(EXIT_MARKER):].strip())
                except ValueError:
                    harness_exit = None
                continue
            print(line)
            if args.timeout is not None and (time.monotonic() - t0) > args.timeout:
                print(f"[run_geogram_soak] timeout {args.timeout}s -- killing on-device run")
                subprocess.run([ADB, *sel, "shell", "pkill", "-f", "geogram_soak"], check=False)
                proc.kill()
                return 124
    finally:
        proc.wait()

    if harness_exit is None:
        print("[run_geogram_soak] WARNING: no exit marker seen (run interrupted?)")
        return proc.returncode or 1
    return harness_exit


if __name__ == "__main__":
    sys.exit(main())
