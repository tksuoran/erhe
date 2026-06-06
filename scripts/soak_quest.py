#!/usr/bin/env python3
"""Quest main-loop-hang soak harness.

Hunts the intermittent render-thread CPU-spin documented in
doc/intermittent_main_loop_hang.md by repeatedly cold-starting the editor on a
Quest 3 and watching whether it reaches steady-state content rendering.

Subcommands:

    py -3 scripts/soak_quest.py prep
        Clean reinstall: uninstall (wipes the on-device SPIR-V cache + app
        data, so every run starts cold) then install the freshly built APK.

    py -3 scripts/soak_quest.py run <iter> [--init-ceiling S] [--post-window S]
        Stream logcat, launch the immersive activity, classify PHASE-AWARE,
        then force-stop. Outcomes: PASS / HANG / CRASH / INCONCLUSIVE.

    py -3 scripts/soak_quest.py batch --start A --end B [--init-ceiling S] [--post-window S]
        Run prep+run for iterations A..B unattended, stopping on the first
        HANG/CRASH (preserving captures) or on 2 consecutive INCONCLUSIVE
        (likely headset off-head / device issue). Per the project's Quest
        launch policy, batch runs need only a single headset confirmation at
        the start -- so the whole sweep runs without further prompts.

Classification (PASS = soak target reached; HANG = the bug we are hunting):
    PASS         -- "Main loop: completed frame 10" seen.
    HANG         -- watchdog "Main loop STALLED" seen, OR the post-init window
                    elapsed with no frame 10 (silent stall).
    HWASAN       -- HWAddressSanitizer report ("tag-mismatch") seen: a memory
                    bug caught at its source on a `-Phwasan` build. This is the
                    decisive reproduction signal for the HWASan soak.
    CRASH        -- a fatal/abort marker seen.
    INCONCLUSIVE -- init never reached "entering main loop" within the init
                    ceiling (e.g. headset off-head, app never ticked).

Why phase-aware: a cold start spends a long, variable time compiling SPIR-V,
but that happens entirely DURING init (before "editor ready, entering main
loop"). Once init is done a healthy editor reaches 10 frames within seconds
(even off-head: each tick still completes behind the 250 ms OpenXR throttle, so
10 frames take ~2.5 s). So slack is granted ONLY to the init phase; the
post-init phase is held to a TIGHT window, and a post-init stall is flagged in
seconds, not minutes. The watchdog's "Main loop STALLED" line remains the
authoritative CPU-spin detector and trips in ~6 s independently of the windows.

Run from the repo root on Windows with `py -3` (per CLAUDE.md).
"""

import argparse
import os
import queue
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
LOGDIR = REPO_ROOT / "logs"
# PKG/ACT are set per run from the --flavor arg (see set_flavor). The mobile
# flavor has no applicationIdSuffix (org.libsdl.app); quest adds ".quest". Both
# use the same ErheActivity class. The mobile flavor does NOT link OpenXR, so the
# editor runs FLAT -- useful for testing whether the hang needs the immersive
# path. (A flat 2D app on Quest still needs the headset on to be foregrounded.)
PKG = "org.libsdl.app.quest"
ACT = PKG + "/org.libsdl.app.ErheActivity"


def set_flavor(flavor: str) -> None:
    global PKG, ACT
    PKG = "org.libsdl.app" if flavor == "mobile" else "org.libsdl.app.quest"
    ACT = PKG + "/org.libsdl.app.ErheActivity"


def apk_for(build_type: str, flavor: str) -> Path:
    return (REPO_ROOT / "android-project" / "app" / "build" / "outputs" / "apk"
            / flavor / build_type / f"app-{flavor}-{build_type}.apk")

# Log markers (substring match against the logcat threadtime stream).
PASS_S = "Main loop: completed frame 10"   # soak target: 10 content frames
HANG_S = "Main loop STALLED"               # the watchdog's authoritative signal
# With the temporary build_polygon_fill / make_brushes-join validators in place
# (doc/intermittent_main_loop_hang.md), a reproduction no longer spins -- it logs
# "MESH CORRUPT ..." and continues to frame 10. So a corrupt-mesh log line is now
# an authoritative reproduction signal, just like the watchdog stall.
CORRUPT_S = "MESH CORRUPT"
# On a `-Phwasan` build, HWAddressSanitizer aborts the process with a
# "tag-mismatch" report the instant it catches the bad read/write -- naming the
# faulting PC + allocation. That report is the authoritative reproduction signal
# (checked before CRASH_RE, since the abort also emits a generic "Fatal signal").
HWASAN_S = "HWAddressSanitizer:"
READY_S = "editor ready, entering main loop"  # end of init (SPIR-V compiled)
CRASH_RE = re.compile(r"Fatal signal|libc: Fatal|FATAL EXCEPTION|beginning of crash")
FRAME_RE = re.compile(r"Main loop: completed frame (\d+)")
HANG_LINGER_S = 8.0  # after a stall is detected, keep capturing this long so the
                     # watchdog's breadcrumb-ring dump (culprit brush + build
                     # sequence) and its 5 s re-report land in the capture file
                     # before we force-stop the app.


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


def adb(*args: str, timeout: float = 120.0) -> subprocess.CompletedProcess:
    return subprocess.run([ADB, *args], capture_output=True, text=True, timeout=timeout)


def do_prep(apk: Path) -> bool:
    """Clean reinstall. Prints PREP_OK / PREP_FAIL; returns success."""
    if not apk.exists():
        print(f"PREP_FAIL APK not found: {apk} (run scripts/build_android.bat quest <task>)")
        return False
    state = adb("get-state").stdout.strip()
    if state != "device":
        print(f"PREP_FAIL no device (adb get-state='{state}')")
        return False
    adb("uninstall", PKG)  # may fail legitimately (not installed); ignore
    try:
        install = adb("install", "-r", "-g", "-d", str(apk), timeout=120.0)
    except subprocess.TimeoutExpired:
        # An `adb install` that hangs is almost always a Play Protect / Samsung
        # Auto Blocker "Send app for a security check?" dialog blocking the
        # install until dismissed. Fail this iteration gracefully (the batch
        # turns it into BATCH_ABORT) instead of crashing the whole soak. Disable
        # Auto Blocker / Play Protect on the device for unattended soaks.
        print("PREP_FAIL install timed out (>120s) -- likely a Play Protect / "
              "Auto Blocker security-check dialog blocking the install; disable "
              "it on the device for unattended soaks")
        return False
    if install.returncode != 0:
        print("PREP_FAIL install error:")
        sys.stdout.write((install.stdout or "") + (install.stderr or ""))
        return False
    print("PREP_OK clean reinstall complete")
    return True


def capture_tombstone(iteration: int, stamp: str):
    """Force a debuggerd tombstone (full all-thread native backtrace) on the
    still-alive app, then pull it to logs/. Returns the local Path or None.

    On a HANG the editor is CPU-spinning (it does not crash on its own), so we
    send it SIGABRT via run-as; debuggerd then ptrace-unwinds EVERY thread --
    including the spinning tick thread -- into a world-readable
    /data/tombstones/ file. On stock (user) devices like the S23, live
    ptrace/lldb attach is locked down but this tombstone path is not, so it is
    the way to get a native backtrace of the spin. Symbolize host-side with
    ndk-stack + the unstripped libmain.so (matched by build-id)."""
    pid = adb("shell", "pidof", PKG).stdout.strip()
    if not pid:
        print("  tombstone: process already gone, cannot capture")
        return None
    adb("shell", "run-as", PKG, "kill", "-6", pid)
    time.sleep(5.0)  # let debuggerd unwind all threads + write the tombstone
    newest = adb(
        "shell",
        "ls -t /data/tombstones/tombstone_* 2>/dev/null | grep -v '[.]pb' | head -1",
    ).stdout.strip()
    if not newest:
        print("  tombstone: none found under /data/tombstones")
        return None
    local = LOGDIR / f"soak_iter{iteration:02d}_{stamp}_tombstone.txt"
    pull = adb("pull", newest, str(local))
    if pull.returncode != 0 or not local.exists():
        print(f"  tombstone: pull failed ({newest})")
        return None
    print(f"  TOMBSTONE captured: {local.relative_to(REPO_ROOT)} (device: {newest})")
    return local


def do_run(iteration: int, init_ceiling: float, post_window: float,
           grab_tombstone: bool = False) -> str:
    """Launch + phase-aware watch + force-stop. Prints RESULT; returns outcome."""
    LOGDIR.mkdir(exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    capture = LOGDIR / f"soak_iter{iteration:02d}_{stamp}.log"

    # Clear the in-memory ring, then start streaming BEFORE launch so no
    # startup line is missed.
    adb("logcat", "-c")
    proc = subprocess.Popen(
        [ADB, "logcat", "-v", "threadtime"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1, errors="replace",
    )
    line_q: "queue.Queue" = queue.Queue()

    def reader() -> None:
        try:
            assert proc.stdout is not None
            for line in proc.stdout:
                line_q.put(line)
        finally:
            line_q.put(None)

    threading.Thread(target=reader, daemon=True).start()

    # Launch the immersive activity.
    adb("shell", "am", "start", "-n", ACT)
    t0 = time.monotonic()
    phase = "init"          # "init" -> "post_init"
    init_s = None           # elapsed at "entering main loop"
    frames = 0
    result = None

    cap = open(capture, "w", encoding="utf-8", errors="replace")
    hang_until = None        # when HANG detected, linger until this time so the
    decided_elapsed = None   # ring dump is captured; verdict time excludes linger
    try:
        while True:
            now = time.monotonic()
            elapsed = now - t0
            if result in ("HANG", "CORRUPT", "HWASAN"):
                # Verdict reached; keep draining lines into the capture for a
                # short linger so the watchdog's breadcrumb-ring dump (which
                # names the culprit brush + the build sequence) and its 5 s
                # re-report -- or the MESH CORRUPT detail line + any further
                # corrupt brushes, or the full HWASan report (stack + tag map) --
                # land in the file before we force-stop.
                if now >= hang_until:
                    break
            elif result is not None:
                break
            else:
                # No verdict yet: enforce phase deadlines regardless of line
                # flow (a hung editor goes silent, but Android system logging
                # keeps the stream alive anyway).
                if phase == "init" and elapsed >= init_ceiling:
                    result = "INCONCLUSIVE"
                    decided_elapsed = elapsed
                    break
                if phase == "post_init" and (elapsed - init_s) >= post_window:
                    result = "HANG"
                    decided_elapsed = elapsed
                    hang_until = now + HANG_LINGER_S
                    continue
            try:
                line = line_q.get(timeout=0.25)
            except queue.Empty:
                continue
            if line is None:
                if result is None:
                    result = "INCONCLUSIVE"   # logcat stream ended unexpectedly
                    decided_elapsed = elapsed
                break
            cap.write(line)
            match = FRAME_RE.search(line)
            if match:
                frames = max(frames, int(match.group(1)))
            if result is None:
                if CORRUPT_S in line:
                    # A validator caught the corrupt mesh (build_polygon_fill guard
                    # or make_brushes-join check). Checked before PASS: the corrupt
                    # log precedes frame 10 since the guard skips the fill and lets
                    # the editor continue. Linger to capture the detail + breadcrumb.
                    result = "CORRUPT"
                    decided_elapsed = time.monotonic() - t0
                    hang_until = time.monotonic() + HANG_LINGER_S
                elif HWASAN_S in line:
                    # HWASan caught the corrupting access. Linger to capture the
                    # full report (faulting PC, allocation, stack, tag map) which
                    # follows this header line. Checked before CRASH_RE because
                    # the subsequent abort also emits a generic "Fatal signal".
                    result = "HWASAN"
                    decided_elapsed = time.monotonic() - t0
                    hang_until = time.monotonic() + HANG_LINGER_S
                elif HANG_S in line:
                    result = "HANG"
                    decided_elapsed = time.monotonic() - t0
                    hang_until = time.monotonic() + HANG_LINGER_S
                elif PASS_S in line:
                    result = "PASS"
                    decided_elapsed = time.monotonic() - t0
                    break
                elif CRASH_RE.search(line):
                    result = "CRASH"
                    decided_elapsed = time.monotonic() - t0
                    break
                elif phase == "init" and READY_S in line:
                    phase = "post_init"
                    init_s = elapsed
    finally:
        cap.flush()
        cap.close()
        # While the app is still alive (force-stop is below), force a tombstone
        # so we get a native backtrace of the spinning thread. Only on a real
        # reproduction, and only when asked (-Phwasan / --capture-tombstone).
        if grab_tombstone and result in ("HANG", "CORRUPT", "HWASAN", "CRASH"):
            capture_tombstone(iteration, stamp)
        try:
            proc.terminate()
        except Exception:
            pass
        adb("shell", "am", "force-stop", PKG)

    end_elapsed = round(decided_elapsed if decided_elapsed is not None else (time.monotonic() - t0), 1)
    if init_s is None:
        init_disp, post_disp, started = end_elapsed, 0.0, "no"
    else:
        init_disp, post_disp, started = round(init_s, 1), round(end_elapsed - init_s, 1), "yes"

    rel = capture.relative_to(REPO_ROOT)
    print(
        f"RESULT iter={iteration} {result} frames={frames} "
        f"init_s={init_disp} post_s={post_disp} total_s={end_elapsed} "
        f"started={started} capture={rel}"
    )

    if result in ("HANG", "CORRUPT", "HWASAN", "CRASH"):
        if result in ("HANG", "CORRUPT"):
            keys = ("STALLED", "breadcrumb ", "thumbnail: brush", "MESH CORRUPT", "build_polygon_fill")
        elif result == "HWASAN":
            keys = ("HWAddressSanitizer", "tag-mismatch", "of size", "heap chunk",
                    "Cause:", "is located", "thumbnail: brush", "build_polygon_fill",
                    "liberhe", "libmain", "libgeogram")
        else:
            keys = ("Fatal signal", "libc", "FATAL", "abort", "Abort", "ERHE")
        evidence = [
            line.rstrip()
            for line in open(capture, encoding="utf-8", errors="replace")
            if any(k in line for k in keys)
        ]
        print(f"----- {result} evidence (last 40 of {len(evidence)}) -----")
        for line in evidence[-40:]:
            print(line)
    return result


def cmd_prep(a: argparse.Namespace) -> int:
    set_flavor(a.flavor)
    return 0 if do_prep(apk_for(a.build_type, a.flavor)) else 1


def cmd_run(a: argparse.Namespace) -> int:
    set_flavor(a.flavor)
    do_run(a.iter, a.init_ceiling, a.post_window, a.capture_tombstone)
    return 0


def cmd_batch(a: argparse.Namespace) -> int:
    set_flavor(a.flavor)
    results = []
    consecutive_incon = 0
    apk = apk_for(a.build_type, a.flavor)
    for i in range(a.start, a.end + 1):
        print(f"=== iteration {i} / {a.end} ===")
        # --no-reinstall: relaunch the already-installed app (fresh process, warm
        # SPIR-V) instead of clean-reinstalling. Dodges the Play Protect / Auto
        # Blocker install dialog entirely and is much faster; the concurrent-init
        # contention that triggers the hang runs on every launch regardless.
        if not a.no_reinstall:
            if not do_prep(apk):
                print(f"BATCH_ABORT prep failed at iter {i}")
                break
        result = do_run(i, a.init_ceiling, a.post_window, a.capture_tombstone)
        results.append((i, result))
        if result in ("HANG", "CORRUPT", "HWASAN", "CRASH"):
            print(f"BATCH_STOP {result} at iter {i}; preserving captures for analysis.")
            break
        if result == "INCONCLUSIVE":
            consecutive_incon += 1
            if consecutive_incon >= 2:
                print("BATCH_PAUSE 2 consecutive INCONCLUSIVE -- likely headset "
                      "off-head or device issue; stopping so it can be re-checked.")
                break
        else:
            consecutive_incon = 0
        time.sleep(2)  # settle between clean reinstalls
    counts: dict = {}
    for _, r in results:
        counts[r] = counts.get(r, 0) + 1
    summary = " ".join(f"{k}={v}" for k, v in sorted(counts.items()))
    print(f"BATCH_SUMMARY {summary} ran={len(results)}")
    return 0


def main() -> None:
    sys.stdout.reconfigure(line_buffering=True)  # live progress in background output
    parser = argparse.ArgumentParser(description="Quest main-loop-hang soak harness")
    sub = parser.add_subparsers(dest="cmd", required=True)

    prep = sub.add_parser("prep", help="clean reinstall")
    prep.add_argument("--build-type", choices=["debug", "release"], default="debug", dest="build_type")
    prep.add_argument("--flavor", choices=["mobile", "quest"], default="quest", dest="flavor")
    prep.set_defaults(func=cmd_prep)

    run = sub.add_parser("run", help="launch + phase-aware watch")
    run.add_argument("iter", type=int, help="iteration number (for labelling)")
    run.add_argument("--init-ceiling", type=float, default=180.0, dest="init_ceiling",
                     help="max seconds for init incl. SPIR-V compile (default 180)")
    run.add_argument("--post-window", type=float, default=60.0, dest="post_window",
                     help="max seconds after init to reach frame 10 (default 60)")
    run.add_argument("--flavor", choices=["mobile", "quest"], default="quest", dest="flavor")
    run.add_argument("--capture-tombstone", action="store_true", dest="capture_tombstone",
                     help="on HANG/CORRUPT/HWASAN/CRASH, SIGABRT the still-alive app and pull "
                          "the debuggerd tombstone (full all-thread native backtrace of the spin)")
    run.set_defaults(func=cmd_run)

    batch = sub.add_parser("batch", help="prep+run a range of iterations unattended")
    batch.add_argument("--start", type=int, required=True)
    batch.add_argument("--end", type=int, required=True)
    batch.add_argument("--init-ceiling", type=float, default=30.0, dest="init_ceiling",
                       help="max seconds for init incl. SPIR-V compile (default 30)")
    batch.add_argument("--post-window", type=float, default=15.0, dest="post_window",
                       help="max seconds after init to reach frame 10 (default 15)")
    batch.add_argument("--build-type", choices=["debug", "release"], default="debug", dest="build_type")
    batch.add_argument("--flavor", choices=["mobile", "quest"], default="quest", dest="flavor")
    batch.add_argument("--no-reinstall", action="store_true", dest="no_reinstall",
                       help="relaunch the already-installed app (fresh process, warm "
                            "SPIR-V) instead of clean-reinstalling -- dodges the Play "
                            "Protect/Auto Blocker install dialog and is faster")
    batch.add_argument("--capture-tombstone", action="store_true", dest="capture_tombstone",
                       help="on the reproduction, SIGABRT the still-alive app and pull the "
                            "debuggerd tombstone (full all-thread native backtrace of the spin)")
    batch.set_defaults(func=cmd_batch)

    args = parser.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
