#!/usr/bin/env python3
"""Quest benchmark runs (doc/plans/quest_performance.md "Benchmark mode").

Subcommands:

    py -3 scripts/quest_bench.py run --bench config/editor/benchmark_orbit.json
            [--scene config/editor/commands_bench_heavy.json]
            [--worn] [--gpu-metrics] [--repeat N] [--push] [--timeout S]
            [--bench-to-device] [--editor-args "..."]
            [--mcp TOOL[=JSON]]... [--load-scene <device path>]
            [--mcp-measure TOOL[=JSON]]...
        Launches the installed editor APK with
        `--benchmark <bench> [--commands <scene>]` (the launch intent's `args`
        extra, ErheActivity.getArguments()), N times, one launch each. Each run:
        force-stops the app, starts scripts/quest_logcat.sh, launches, waits
        (bounded) for the `Benchmark result <name>:` or `Benchmark failed
        <name>:` line in the capture, pulls the result JSON from the app's
        external files directory (RESULT_DIR; plain adb, no run-as, so a
        release APK works), fills the device identity from adb getprop when the
        editor left it empty, and stores the JSON under
        memory-bank/local/quest_runs/. With --repeat N > 1 a repeat summary
        (per-term median across runs, min / max spread) is stored and printed
        as well.

        An automated run (doc/agents/quest.md "Launch protocol"): no user
        prompt. Before each launch the proximity sensor is bypassed and the
        guardian paused; after each run (always: also when the result line
        never arrives, on failure or on Ctrl-C) the app is force-stopped and
        normal sensor and guardian operation restored (doc/agents/quest.md
        "Off-head runs").

        --worn       skips the proximity bypass and its restore: an interactive
                     run with the headset worn, whose launch the user confirmed.

        A benchmark file with display_refresh_rate_hz also sets the developer
        property debug.oculus.refreshRate to that rate before the launch and
        clears it in the same restore (doc/agents/quest.md "Off-head runs":
        the runtime accepts xrRequestDisplayRefreshRateFB but does not switch
        the display on its own).
        --gpu-metrics runs ovrgpuprofiler for the measured window (started at
                     the driver's "measuring" line, measure_frames at the
                     benchmark's refresh rate long). Metric ids: --gpu-metric-ids,
                     else an `ovrgpuprofiler_ids::<id,id,...>` line in
                     memory-bank/local/quest.md, else the ids whose names match
                     DEFAULT_GPU_METRIC_NAMES in `ovrgpuprofiler -m`. The GPU
                     frequency metrics are always included.
        --push       copies the local benchmark (and --scene) file over the
                     device's writable copy first. Needs a debuggable APK
                     (run-as); on a release APK it fails: there a changed
                     benchmark or commands file needs a rebuild and a clean
                     reinstall (the APK's config/ files are migrated to the
                     writable directory only when missing there).
        --bench-to-device  copies the local benchmark file with plain adb push
                     to BENCH_DEVICE_DIR (the app's external files directory,
                     writable without run-as, so a release APK works) and
                     passes that absolute path to --benchmark.
        --editor-args  extra editor arguments appended to the launch args,
                     e.g. "--no-scene".

        MCP during the run (doc/agents/quest.md "Reaching the MCP server on
        the device"): with any of --mcp, --load-scene or --mcp-measure, the
        run waits for the editor's `MCP server: listening on 127.0.0.1:<port>`
        line and frame 12, forwards host port --mcp-port to that device port
        (adb forward; removed after the run) and makes the calls during the
        benchmark's warmup, in this order:
        --mcp TOOL[=JSON]  (repeatable) e.g.
                     'set_graphics_preset={"light_shading": "clustered"}'.
        --load-scene PATH  load_scene of a file on the device (absolute
                     path), waits until it is listed and get_async_status is
                     idle, then set_headset_scene to it. The benchmark file's
                     warmup_frames must outlast this (the settle that starts
                     the measured window waits only for loads already in
                     flight): the run fails when the settle line came first.
        --mcp-measure TOOL[=JSON]  (repeatable) calls made once the measured
                     window started (reset_composition_pass_stats first, then
                     a 3 s wait); the replies are stored in the result's
                     host.mcp_measure.

    py -3 scripts/quest_bench.py compare a.json b.json
        Prints two results (or repeat summaries) side by side with deltas, and
        whether their input hashes match (same camera poses and frame_dt).

Run from the repo root on Windows with `py -3` (AGENTS.md).
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
LOGDIR    = REPO_ROOT / "logs"
RUNS_DIR  = REPO_ROOT / "memory-bank" / "local" / "quest_runs"
LOCAL_QUEST_MD = REPO_ROOT / "memory-bank" / "local" / "quest.md"
PKG = "org.libsdl.app.quest"
ACT = PKG + "/org.libsdl.app.ErheActivity"
# Where the editor writes benchmark results on Android: the app's external
# files directory (SDL_GetAndroidExternalStoragePath()) + /logs.
RESULT_DIR = f"/sdcard/Android/data/{PKG}/files/logs"
# Where --bench-to-device puts the benchmark file (plain adb push).
BENCH_DEVICE_DIR = f"/sdcard/Android/data/{PKG}/files/bench"
MCP_LISTEN_RE = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+)")
MCP_READY_LINE = "Main loop: completed frame 12"
CAPTURE_APPEAR_TIMEOUT_S = 10.0

# Terms of the result JSON "summary", in print order.
TERMS = ["wall_us", "full_tick_us", "m1_us", "m2_us", "m3_us", "r_us"]

# ovrgpuprofiler metric names selected from `ovrgpuprofiler -m` when no id
# list is configured (substring match, case-insensitive). The frequency
# entries are always included so clock changes between runs are recorded.
DEFAULT_GPU_METRIC_NAMES = [
    "GPU Frequency",
    "GPU Average Frequency",
    "GPU % Utilization",
    "GPU % Bus Busy",
    "% Vertex Fetch Stall",
    "% Texture Fetch Stall",
    "% Shaders Busy",
    "% Shaders Stalled",
    "% Time Shading Fragments",
    "% Time Shading Vertices",
    "% Nearest Filtered",
    "% Linear Filtered",
    "% Non-Base Level Textures",
    "% LRZ Busy",
]
FREQUENCY_METRIC_NAMES = ["GPU Frequency", "GPU Average Frequency"]

CRASH_RE = re.compile(r"Fatal signal|libc: Fatal|FATAL EXCEPTION|beginning of crash")


def find_adb() -> str:
    # ANDROID_HOME, else Android Studio's default SDK root for the host OS.
    android_home = os.environ.get("ANDROID_HOME", "")
    if not android_home:
        local = os.environ.get("LOCALAPPDATA", "")
        defaults = [
            Path(local) / "Android" / "Sdk" if local else None,  # Windows
            Path.home() / "Library" / "Android" / "sdk",           # macOS
            Path.home() / "Android" / "Sdk",                       # Linux
        ]
        for candidate in defaults:
            if candidate is not None and candidate.is_dir():
                android_home = str(candidate)
                break
    if not android_home:
        sys.exit("ERROR: ANDROID_HOME unset and no Android Studio default SDK root exists")
    for name in ("adb.exe", "adb"):
        candidate = Path(android_home) / "platform-tools" / name
        if candidate.exists():
            return str(candidate)
    sys.exit(f"ERROR: adb not found under {android_home}/platform-tools")


_ADB = None


def adb_path() -> str:
    global _ADB
    if _ADB is None:
        _ADB = find_adb()
    return _ADB


def adb(*args: str, timeout: float = 120.0, check: bool = False) -> subprocess.CompletedProcess:
    result = subprocess.run([adb_path(), *args], capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=timeout)
    if check and result.returncode != 0:
        raise RuntimeError(f"adb {' '.join(args)} failed: {result.stderr.strip()}")
    return result


def adb_shell(command: str, timeout: float = 120.0) -> subprocess.CompletedProcess:
    # One string: adb passes it to the device shell as the command line.
    return adb("shell", command, timeout=timeout)


def find_bash() -> str:
    for candidate in (r"C:\Program Files\Git\bin\bash.exe", r"C:\Program Files\Git\usr\bin\bash.exe"):
        if Path(candidate).exists():
            return candidate
    return "bash"


# --- logcat capture -------------------------------------------------------

def start_logcat() -> Path:
    result = subprocess.run([find_bash(), str(REPO_ROOT / "scripts" / "quest_logcat.sh")],
                            capture_output=True, text=True, cwd=str(REPO_ROOT), timeout=60)
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if (result.returncode != 0) or not lines:
        raise RuntimeError(f"quest_logcat.sh failed: {result.stderr.strip()}")
    # Git Bash prints /d/... paths: convert that form first, then resolve a
    # relative path against the repository root.
    text = lines[-1].replace("\\", "/")
    match = re.match(r"^/([a-zA-Z])/(.*)$", text)
    path = Path(f"{match.group(1)}:/{match.group(2)}") if match else Path(text)
    if not path.is_absolute():
        path = REPO_ROOT / path
    # Fail loudly when the capture does not appear (a wrong path would make
    # the run wait for lines that land elsewhere).
    deadline = time.monotonic() + CAPTURE_APPEAR_TIMEOUT_S
    while not path.exists():
        if time.monotonic() > deadline:
            raise RuntimeError(f"logcat capture {path} did not appear within {CAPTURE_APPEAR_TIMEOUT_S:.0f} s (quest_logcat.sh printed '{lines[-1]}')")
        time.sleep(0.2)
    return path


def stop_logcat() -> None:
    subprocess.run([find_bash(), str(REPO_ROOT / "scripts" / "quest_logcat.sh"), "stop"],
                   capture_output=True, text=True, cwd=str(REPO_ROOT), timeout=60)


class Capture_tail:
    """Reads new lines of the growing logcat capture file."""

    def __init__(self, path: Path):
        self.path = path
        self.offset = 0

    def new_lines(self) -> list:
        if not self.path.exists():
            return []
        with open(self.path, "r", encoding="utf-8", errors="replace", newline="") as f:
            f.seek(self.offset)
            data = f.read()
            self.offset = f.tell()
        return data.splitlines()


# --- GPU metrics ----------------------------------------------------------

def gpu_metric_ids(explicit: str) -> str:
    if explicit:
        return explicit
    if LOCAL_QUEST_MD.exists():
        text = LOCAL_QUEST_MD.read_text(encoding="utf-8", errors="replace")
        match = re.search(r"ovrgpuprofiler_ids::([0-9,]+)", text)
        if match:
            return ensure_frequency_ids(match.group(1))
    listing = adb_shell("ovrgpuprofiler -m", timeout=30).stdout
    ids = []
    for line in listing.splitlines():
        match = re.match(r"^\s*\[?(\d+)\]?[\s:.)\-]+(.+?)\s*$", line)
        if not match:
            continue
        name = match.group(2).lower()
        if any(wanted.lower() in name for wanted in DEFAULT_GPU_METRIC_NAMES):
            ids.append(match.group(1))
    if not ids:
        raise RuntimeError("no ovrgpuprofiler metric ids found (pass --gpu-metric-ids)")
    return ",".join(ids)


def ensure_frequency_ids(ids: str) -> str:
    listing = adb_shell("ovrgpuprofiler -m", timeout=30).stdout
    present = set(ids.split(","))
    for line in listing.splitlines():
        match = re.match(r"^\s*\[?(\d+)\]?[\s:.)\-]+(.+?)\s*$", line)
        if match and any(f.lower() == match.group(2).strip().lower() for f in FREQUENCY_METRIC_NAMES):
            present.add(match.group(1))
    return ",".join(sorted(present, key=int))


def parse_gpu_metrics(text: str) -> dict:
    samples = {}
    for line in text.splitlines():
        if ":" not in line:
            continue
        name, _, value = line.rpartition(":")
        try:
            number = float(value.strip())
        except ValueError:
            continue
        samples.setdefault(name.strip(), []).append(number)
    return {
        name: {"mean": statistics.fmean(values), "min": min(values), "max": max(values), "samples": len(values)}
        for name, values in samples.items()
    }


class Gpu_profiler:
    def __init__(self, ids: str, seconds: int):
        self.ids = ids
        self.seconds = seconds
        self.output = ""
        self.thread = None

    def start(self) -> None:
        def body():
            result = adb_shell(f'timeout {self.seconds} ovrgpuprofiler -r"{self.ids}"', timeout=self.seconds + 60)
            self.output = result.stdout
        self.thread = threading.Thread(target=body, daemon=True)
        self.thread.start()

    def join(self) -> dict:
        if self.thread is None:
            return {}
        self.thread.join(timeout=self.seconds + 90)
        return parse_gpu_metrics(self.output)


# --- one run ---------------------------------------------------------------

def push_config(local_path: str) -> None:
    """Copies a local config file over the device's writable copy (files/<path>)."""
    source = REPO_ROOT / local_path
    if not source.exists():
        raise RuntimeError(f"--push: {source} not found")
    probe = adb_shell(f"run-as {PKG} true")
    if (probe.returncode != 0) or ("not debuggable" in (probe.stdout + probe.stderr)):
        raise RuntimeError(
            f"--push needs a debuggable APK (run-as {PKG} failed: {(probe.stdout + probe.stderr).strip()}); "
            "on a release APK a changed benchmark or commands file needs a rebuild and a clean reinstall"
        )
    staging = "/data/local/tmp/erhe_bench_push.json"
    adb("push", str(source), staging, check=True)
    device_dir = os.path.dirname(local_path).replace("\\", "/")
    device_path = local_path.replace("\\", "/")
    result = adb_shell(f"cat {staging} | run-as {PKG} sh -c 'mkdir -p files/{device_dir} && cat > files/{device_path}'")
    if result.returncode != 0:
        raise RuntimeError(f"--push of {local_path} failed: {result.stderr.strip()}")
    print(f"pushed {local_path}")


def off_head_begin() -> None:
    adb_shell(f"am force-stop {PKG}")
    adb_shell("input keyevent KEYCODE_WAKEUP")
    adb_shell("am broadcast -a com.oculus.vrpowermanager.prox_close")
    adb_shell("setprop debug.oculus.guardian_pause 1")


def off_head_end() -> None:
    errors = []
    for command in (
        f"am force-stop {PKG}",
        "am broadcast -a com.oculus.vrpowermanager.automation_disable",
        "setprop debug.oculus.guardian_pause 0",
    ):
        try:
            adb_shell(command, timeout=60)
        except Exception as error:
            errors.append(f"{command}: {error}")
    if errors:
        raise RuntimeError("; ".join(errors))


def refresh_rate_property_value(bench: dict) -> str:
    rate = float(bench["display_refresh_rate_hz"])
    return str(int(rate)) if rate.is_integer() else f"{rate:g}"


def refresh_rate_begin(bench: dict) -> None:
    adb_shell(f"setprop debug.oculus.refreshRate {refresh_rate_property_value(bench)}")


def refresh_rate_end() -> None:
    adb_shell("setprop debug.oculus.refreshRate '\"\"'", timeout=60)


def launch(editor_args: str) -> None:
    command = (
        "am start -W -a android.intent.action.MAIN -c android.intent.category.LAUNCHER "
        f"-c com.oculus.intent.category.VR -n {ACT} --es args '{editor_args}'"
    )
    result = adb_shell(command, timeout=120)
    if result.returncode != 0:
        raise RuntimeError(f"am start failed: {result.stdout.strip()} {result.stderr.strip()}")


def push_bench_to_device(local_path: str) -> str:
    """Copies the benchmark file into BENCH_DEVICE_DIR; returns the device path."""
    source = REPO_ROOT / local_path
    if not source.exists():
        raise RuntimeError(f"--bench-to-device: {source} not found")
    # The app creates its external files directory on its first run; a
    # directory the shell made first is not readable by the app.
    if adb_shell(f"ls -d /sdcard/Android/data/{PKG}/files").returncode != 0:
        raise RuntimeError(f"/sdcard/Android/data/{PKG}/files missing: run any benchmark once so the app creates it")
    adb_shell(f"mkdir -p {BENCH_DEVICE_DIR}")
    device_path = f"{BENCH_DEVICE_DIR}/{source.name}"
    adb("push", str(source), device_path, check=True)
    adb_shell(f"chmod -R a+rX {BENCH_DEVICE_DIR}")
    print(f"pushed {local_path} -> {device_path}")
    return device_path


def parse_mcp_call(text: str) -> tuple:
    tool, separator, arguments = text.partition("=")
    return tool.strip(), (json.loads(arguments) if separator else {})


def mcp_call(port: int, tool: str, arguments: dict | None = None):
    # The client of scripts/scene_load_profile.py (mcp_call.py, base64 args).
    sys.path.insert(0, str(REPO_ROOT / "scripts"))
    import scene_load_profile
    return scene_load_profile.mcp(port, tool, arguments)


def uses_mcp(args) -> bool:
    return bool(args.mcp or args.load_scene or args.mcp_measure)


def mcp_load_scene(port: int, path: str, timeout_s: float) -> dict:
    """load_scene, wait until listed and idle, set_headset_scene."""
    sys.path.insert(0, str(REPO_ROOT / "scripts"))
    import scene_load_profile
    reply = mcp_call(port, "load_scene", {"path": path})
    if not (isinstance(reply, dict) and reply.get("queued")):
        raise RuntimeError(f"load_scene {path} refused: {reply}")
    scene_name = reply.get("scene_name", "")
    listed_s, idle_s = scene_load_profile.wait_until_settled(port, path, timeout_s)
    if idle_s is None:
        raise RuntimeError(f"load_scene {path}: not idle within {timeout_s:.0f} s (listed {listed_s})")
    headset = mcp_call(port, "set_headset_scene", {"scene_name": scene_name})
    return {"path": path, "scene_name": scene_name, "listed_s": round(listed_s, 2), "idle_s": round(idle_s, 2), "set_headset_scene": headset}


def fill_identity(result: dict) -> None:
    identity = result.setdefault("identity", {})
    if not identity.get("device_model"):
        identity["device_model"] = adb_shell("getprop ro.product.model").stdout.strip()
    if not identity.get("os_build"):
        identity["os_build"] = adb_shell("getprop ro.build.display.id").stdout.strip()


def run_once(args, bench: dict, run_index: int, stamp: str) -> dict:
    name = bench["name"]
    editor_args = f"--benchmark {args.device_bench}"
    if args.scene:
        editor_args += f" --commands {args.scene}"
    if args.editor_args:
        editor_args += f" {args.editor_args}"
    refresh_hz = float(bench.get("display_refresh_rate_hz", 72.0))
    measure_seconds = int(bench.get("measure_frames", 600) / refresh_hz) + 3

    capture_path = None
    profiler = None
    forwarded = False
    mcp_host = {}
    try:
        adb_shell(f"am force-stop {PKG}")
        adb_shell(f"rm -f {RESULT_DIR}/benchmark_{name}.json")
        capture_path = start_logcat()
        tail = Capture_tail(capture_path)
        if not args.worn:
            off_head_begin()
        if "display_refresh_rate_hz" in bench:
            refresh_rate_begin(bench)
        print(f"[{run_index}] launch: {editor_args} (capture {capture_path})")
        launch(editor_args)
        deadline = time.monotonic() + args.timeout
        outcome = None
        settled = False
        device_port = None
        mcp_ready = False
        warmup_done = not uses_mcp(args)
        measure_done = not args.mcp_measure
        while time.monotonic() < deadline:
            for line in tail.new_lines():
                match = MCP_LISTEN_RE.search(line)
                if match:
                    device_port = int(match.group(1))
                if MCP_READY_LINE in line:
                    mcp_ready = True
                if f"Benchmark {name}: scene settled" in line:
                    settled = True
                    if not warmup_done:
                        raise RuntimeError(f"the measured window started before the warmup MCP calls finished: raise warmup_frames (capture {capture_path})")
                if args.gpu_metrics and (profiler is None) and (f"Benchmark {name}: scene settled" in line):
                    profiler = Gpu_profiler(gpu_metric_ids(args.gpu_metric_ids), measure_seconds)
                    profiler.start()
                if f"Benchmark result {name}:" in line:
                    outcome = "result"
                    print(f"[{run_index}] {line[line.index('Benchmark result'):]}")
                elif f"Benchmark failed {name}:" in line:
                    outcome = "failed"
                    print(f"[{run_index}] {line[line.index('Benchmark failed'):]}")
                elif CRASH_RE.search(line):
                    outcome = "crash"
                    print(f"[{run_index}] crash marker: {line}")
            if outcome is not None:
                break
            if (not warmup_done) and mcp_ready and (device_port is not None):
                adb("forward", f"tcp:{args.mcp_port}", f"tcp:{device_port}", check=True)
                forwarded = True
                print(f"[{run_index}] MCP: host tcp:{args.mcp_port} -> device tcp:{device_port}")
                mcp_host["warmup"] = []
                for call in args.mcp:
                    tool, arguments = parse_mcp_call(call)
                    reply = mcp_call(args.mcp_port, tool, arguments)
                    print(f"[{run_index}] MCP {tool}: {json.dumps(reply)[:300]}")
                    mcp_host["warmup"].append({"tool": tool, "arguments": arguments, "reply": reply})
                if args.load_scene:
                    mcp_host["load_scene"] = mcp_load_scene(args.mcp_port, args.load_scene, args.timeout)
                    print(f"[{run_index}] MCP load_scene: {json.dumps(mcp_host['load_scene'])}")
                warmup_done = True
                continue  # lines that arrived during the calls are read next pass
            if settled and (not measure_done):
                mcp_call(args.mcp_port, "reset_composition_pass_stats")
                time.sleep(3.0)
                mcp_host["measure"] = []
                for call in args.mcp_measure:
                    tool, arguments = parse_mcp_call(call)
                    mcp_host["measure"].append({"tool": tool, "arguments": arguments, "reply": mcp_call(args.mcp_port, tool, arguments)})
                print(f"[{run_index}] MCP measure calls done ({len(args.mcp_measure)})")
                measure_done = True
                continue
            time.sleep(0.5)  # polling the capture file, not waiting for readiness
        if outcome is None:
            raise RuntimeError(f"no benchmark result within {args.timeout} s (capture {capture_path})")
        if outcome != "result":
            raise RuntimeError(f"benchmark {outcome} (capture {capture_path})")
        # The JSON is written right after the result line.
        pulled = None
        for _ in range(20):
            out = adb("exec-out", "cat", f"{RESULT_DIR}/benchmark_{name}.json", timeout=60)
            if (out.returncode == 0) and out.stdout.strip().startswith("{"):
                pulled = out.stdout
                break
            time.sleep(0.5)
        if pulled is None:
            raise RuntimeError(f"{RESULT_DIR}/benchmark_{name}.json not readable on the device")
        result = json.loads(pulled)
        fill_identity(result)
        result["host"] = {
            "run_index": run_index,
            "worn": bool(args.worn),
            "scene": args.scene or "",
            "capture": str(capture_path),
            "gpu_metrics": profiler.join() if profiler is not None else {},
            "editor_args": editor_args,
            "mcp_warmup": mcp_host.get("warmup", []),
            "load_scene": mcp_host.get("load_scene", {}),
            "mcp_measure": mcp_host.get("measure", []),
        }
        RUNS_DIR.mkdir(parents=True, exist_ok=True)
        out_path = RUNS_DIR / f"{stamp}_{name}_{run_index}.json"
        out_path.write_text(json.dumps(result, indent=2), encoding="utf-8", newline="")
        print(f"[{run_index}] stored {out_path}")
        return result
    finally:
        # Restore even when a step above raised (each step on its own, so one
        # failing adb call does not skip the rest).
        restore_steps = [off_head_end] if not args.worn else [lambda: adb_shell(f"am force-stop {PKG}")]
        if "display_refresh_rate_hz" in bench:
            restore_steps.append(refresh_rate_end)
        if forwarded:
            restore_steps.append(lambda: adb("forward", "--remove", f"tcp:{args.mcp_port}"))
        restore_steps.append(stop_logcat)
        for step in restore_steps:
            try:
                step()
            except Exception as error:  # report and keep restoring
                print(f"[{run_index}] restore step failed: {error}", file=sys.stderr)


# --- summaries ----------------------------------------------------------------

def headline(result: dict) -> dict:
    values = {
        "wall_time_s":       result.get("wall_time_s", 0.0),
        "frames_per_second": result.get("frames_per_second", 0.0),
    }
    summary = result.get("summary", {})
    for term in TERMS:
        values[term] = summary.get(term, {}).get("median", 0.0)
    for counter, stats in summary.get("xr_counters", {}).items():
        values[f"xr:{counter}"] = stats.get("median", 0.0)
    # GPU timer series: only the passes that ran in the measured window.
    for gpu_pass, stats in summary.get("gpu_passes", {}).items():
        if stats.get("max", 0.0) > 0.0:
            values[f"gpu_pass:{gpu_pass}"] = stats.get("median", 0.0)
    if "gpu_total_us" in summary:
        values["gpu_pass:(total)"] = summary["gpu_total_us"].get("median", 0.0)
    for metric, stats in result.get("host", {}).get("gpu_metrics", {}).items():
        values[f"gpu:{metric}"] = stats.get("mean", 0.0)
    return values


def repeat_summary(results: list) -> dict:
    keys = []
    for result in results:
        for key in headline(result):
            if key not in keys:
                keys.append(key)
    per_key = {key: [headline(r).get(key) for r in results if key in headline(r)] for key in keys}
    aggregate = {key: {"median": statistics.median(v), "min": min(v), "max": max(v)} for key, v in per_key.items() if v}
    hashes = sorted({r.get("input_hash", "") for r in results})
    return {
        "runs": len(results),
        "name": results[0].get("name", ""),
        "input_hash": hashes[0] if len(hashes) == 1 else "mismatch:" + ",".join(hashes),
        "identity": results[0].get("identity", {}),
        "refresh": results[0].get("refresh", {}),
        "aggregate": aggregate,
    }


def as_values(data: dict) -> dict:
    """Headline values of a result file or a repeat summary (its medians)."""
    if "aggregate" in data:
        return {key: stats["median"] for key, stats in data["aggregate"].items()}
    return headline(data)


def print_repeat(summary: dict) -> None:
    print(f"\n{summary['name']}: {summary['runs']} runs, input_hash {summary['input_hash']}")
    print(f"{'term':<44} {'median':>14} {'min':>14} {'max':>14} {'spread %':>9}")
    for key, stats in summary["aggregate"].items():
        spread = (100.0 * (stats["max"] - stats["min"]) / stats["median"]) if stats["median"] else 0.0
        print(f"{key:<44} {stats['median']:>14.3f} {stats['min']:>14.3f} {stats['max']:>14.3f} {spread:>9.2f}")


def cmd_run(args) -> int:
    bench_path = REPO_ROOT / args.bench
    bench = json.loads(bench_path.read_text(encoding="utf-8"))
    if args.push:
        push_config(args.bench)
        if args.scene:
            push_config(args.scene)
    args.device_bench = push_bench_to_device(args.bench) if args.bench_to_device else args.bench
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    results = []
    for run_index in range(args.repeat):
        results.append(run_once(args, bench, run_index, stamp))
    if args.repeat > 1:
        summary = repeat_summary(results)
        out_path = RUNS_DIR / f"{stamp}_{bench['name']}_repeat.json"
        out_path.write_text(json.dumps(summary, indent=2), encoding="utf-8", newline="")
        print_repeat(summary)
        print(f"stored {out_path}")
    else:
        values = headline(results[0])
        for key, value in values.items():
            print(f"{key:<44} {value:>14.3f}")
    return 0


def cmd_compare(args) -> int:
    a = json.loads(Path(args.a).read_text(encoding="utf-8"))
    b = json.loads(Path(args.b).read_text(encoding="utf-8"))
    va, vb = as_values(a), as_values(b)
    hash_a, hash_b = a.get("input_hash", ""), b.get("input_hash", "")
    print(f"A: {args.a}  ({a.get('identity', {}).get('version', '?')})")
    print(f"B: {args.b}  ({b.get('identity', {}).get('version', '?')})")
    print(f"input_hash: {'MATCH' if (hash_a and (hash_a == hash_b)) else 'DIFFER'} ({hash_a} / {hash_b})")
    ra = a.get("refresh", {}).get("display_refresh_hz", 0.0)
    rb = b.get("refresh", {}).get("display_refresh_hz", 0.0)
    print(f"refresh_hz: {ra} / {rb}; frames_at_refresh_ceiling: "
          f"{a.get('refresh', {}).get('frames_at_refresh_ceiling', '?')} / {b.get('refresh', {}).get('frames_at_refresh_ceiling', '?')}")
    print(f"\n{'term':<44} {'A':>14} {'B':>14} {'B-A':>14} {'%':>8}")
    for key in list(dict.fromkeys(list(va.keys()) + list(vb.keys()))):
        x, y = va.get(key), vb.get(key)
        if (x is None) or (y is None):
            print(f"{key:<44} {str(x):>14} {str(y):>14}")
            continue
        delta = y - x
        percent = (100.0 * delta / x) if x else 0.0
        print(f"{key:<44} {x:>14.3f} {y:>14.3f} {delta:>14.3f} {percent:>8.2f}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Quest benchmark runs (doc/agents/quest.md \"Benchmark runs\")")
    sub = parser.add_subparsers(dest="command", required=True)

    run = sub.add_parser("run", help="launch the installed APK in benchmark mode and collect the result")
    run.add_argument("--bench", required=True, help="benchmark file, repo-relative (same path on the device), e.g. config/editor/benchmark_orbit.json")
    run.add_argument("--scene", default="", help="startup commands file passed as --commands, e.g. config/editor/commands_bench_heavy.json")
    run.add_argument("--worn", action="store_true", help="headset worn, launch confirmed by the user: skip the proximity bypass and its restore")
    run.add_argument("--gpu-metrics", action="store_true", help="run ovrgpuprofiler for the measured window")
    run.add_argument("--gpu-metric-ids", default="", help="ovrgpuprofiler metric ids (comma separated); overrides memory-bank/local/quest.md")
    run.add_argument("--repeat", type=int, default=1, help="number of runs (one launch each)")
    run.add_argument("--push", action="store_true", help="copy the local --bench / --scene files over the device's writable copies first")
    run.add_argument("--timeout", type=float, default=600.0, help="seconds to wait for the result line per run")
    run.add_argument("--bench-to-device", action="store_true", help="adb push the benchmark file to the app's external files directory and pass that path (works on a release APK)")
    run.add_argument("--editor-args", default="", help="extra editor arguments, e.g. --no-scene")
    run.add_argument("--mcp", action="append", default=[], metavar="TOOL[=JSON]", help="MCP call made during the warmup (repeatable)")
    run.add_argument("--load-scene", default="", help="device path of a scene file: load_scene, wait idle, set_headset_scene during the warmup")
    run.add_argument("--mcp-measure", action="append", default=[], metavar="TOOL[=JSON]", help="MCP call made in the measured window, reply stored in the result (repeatable)")
    run.add_argument("--mcp-port", type=int, default=3743, help="host port forwarded to the device's MCP port")
    run.set_defaults(func=cmd_run)

    compare = sub.add_parser("compare", help="compare two result files (or repeat summaries)")
    compare.add_argument("a")
    compare.add_argument("b")
    compare.set_defaults(func=cmd_compare)

    args = parser.parse_args()
    if (args.command == "run") and (args.repeat < 1):
        parser.error("--repeat must be >= 1")
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
