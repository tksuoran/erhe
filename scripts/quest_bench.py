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
        --perfetto   takes a Perfetto trace with the metavr CLI (`metavr perf
                     capture --mode custom --gpu-render-stage --gpu-metrics`;
                     `metavr` on PATH, else ~/.metavr/bin/metavr) of
                     --perfetto-seconds (default 4), starting --perfetto-delay
                     seconds (default 3) after the "scene settled" line, and
                     stores in host.perfetto the trace path and its summary
                     (queried with `metavr perf query`): the GPU surfaces
                     (render passes as the Adreno driver reports them) grouped
                     by configuration with per-surface medians of duration,
                     binning, per-bin rendering and compositor preemption, and
                     the mean / min / max of every Adreno counter. Refused
                     together with --gpu-metrics (both read the counters).
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
        --mcp-after-load TOOL[=JSON]  (repeatable) calls made after the
                     scene load (right after the --mcp calls without
                     --load-scene). In every JSON argument the string $SCENE
                     is replaced by the loaded scene's name.
        --mcp-measure TOOL[=JSON]  (repeatable) calls made once the measured
                     window started (reset_composition_pass_stats first, then
                     a 3 s wait); the replies are stored in the result's
                     host.mcp_measure.
        --config FILE  a bench configuration (scripts/quest_bench_configs/):
                     name, description and the lists mcp, mcp_after_load and
                     mcp_measure (strings of the TOOL[=JSON] form above), made
                     before the command line's calls of the same kind. The
                     name and description are stored in host.config.

        Whenever MCP is used, the configuration in effect is stored in the
        result's host.config_state: the warmup replies of set_graphics_preset
        (the last one the calls made, else one without arguments, which
        re-applies the preset in effect), set_culling_classification,
        set_occlusion_culling and set_light_cluster_binning without
        arguments (pure queries), and, in the measured window,
        get_light_cluster_stats and get_scene_lights of the loaded scene
        (the light shading in effect and the shaded / shadow-mapped lights).

        Quest state (host.device_state): adb snapshots before the launch, at
        the start of the measured window and at the result line (CPU clocks
        and limits per cpufreq policy, GPU clock and busy %, thermal zone
        temperatures, the thermal service's status and HAL temperatures,
        battery), and a sampler every --sample-interval seconds through the
        measured window (clocks, GPU busy %, thermal zones, /proc/stat CPU
        usage), summarized per value (median / min / max).

        --record [DOC]  appends a run record (doc/reference/quest_bench_runs.md
                     by default) for the runs of this invocation.

    py -3 scripts/quest_bench.py record [--doc DOC] run.json [run.json ...]
        Appends the run record of stored per-run results (one invocation's
        repeats) to the record document.

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
import shutil
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


# --- Perfetto capture (metavr) ----------------------------------------------

# Per GPU surface (render pass as the Adreno driver reports it): its
# configuration and, summed over its children, the binning pass, the per-bin
# rendering and the compositor's preemptions.
PERFETTO_SURFACE_SQL = (
    "SELECT s.dur, "
    "EXTRACT_ARG(s.arg_set_id, 'width') AS width, EXTRACT_ARG(s.arg_set_id, 'height') AS height, "
    "EXTRACT_ARG(s.arg_set_id, 'colorBPP') AS color_bpp, EXTRACT_ARG(s.arg_set_id, 'depthBPP') AS depth_bpp, "
    "EXTRACT_ARG(s.arg_set_id, 'MSAA') AS msaa, EXTRACT_ARG(s.arg_set_id, 'renderMode') AS render_mode, "
    "EXTRACT_ARG(s.arg_set_id, 'numberOfBins') AS bins, EXTRACT_ARG(s.arg_set_id, 'numRenderedBins') AS rendered_bins, "
    "(SELECT COALESCE(SUM(c.dur), 0) FROM slice c WHERE c.parent_id = s.id AND c.name = 'Binning') AS binning, "
    "(SELECT COALESCE(SUM(r.dur), 0) FROM slice c JOIN slice r ON r.parent_id = c.id WHERE c.parent_id = s.id AND r.name = 'Render') AS render, "
    "(SELECT COALESCE(SUM(c.dur), 0) FROM slice c WHERE c.parent_id = s.id AND c.name = 'Preempt') + "
    "(SELECT COALESCE(SUM(r.dur), 0) FROM slice c JOIN slice r ON r.parent_id = c.id WHERE c.parent_id = s.id AND r.name = 'Preempt') AS preempt "
    "FROM slice s JOIN track t ON s.track_id = t.id WHERE t.type = 'gpu_track' AND s.depth = 0"
)
PERFETTO_COUNTER_SQL = (
    "SELECT t.name AS name, AVG(c.value) AS mean, MIN(c.value) AS min, MAX(c.value) AS max, COUNT(*) AS samples "
    "FROM counter c JOIN counter_track t ON c.track_id = t.id GROUP BY t.name ORDER BY t.name"
)


def metavr_path() -> str:
    found = shutil.which("metavr")
    if found:
        return found
    return str(Path.home() / ".metavr" / "bin" / "metavr")


def metavr(*args: str, timeout: float) -> subprocess.CompletedProcess:
    return subprocess.run([metavr_path(), *args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)


def summarize_perfetto(session: str) -> dict:
    """Surfaces grouped by configuration (medians per surface, ms) and the
    GPU counters (mean / min / max over the capture)."""
    surfaces = metavr("--json", "perf", "query", session, PERFETTO_SURFACE_SQL, timeout=300)
    counters = metavr("--json", "perf", "query", session, PERFETTO_COUNTER_SQL, timeout=300)
    if (surfaces.returncode != 0) or (counters.returncode != 0):
        raise RuntimeError(f"metavr perf query {session} failed: {surfaces.stderr.strip()} {counters.stderr.strip()}")
    groups = {}
    for row in json.loads(surfaces.stdout):
        key = (f"{row['width']}x{row['height']} color {row['color_bpp']} bpp depth {row['depth_bpp']} bpp "
               f"MSAA {row['msaa']} {row['render_mode']} bins {row['bins']}")
        groups.setdefault(key, []).append(row)
    surface_summary = []
    for key, rows in groups.items():
        def median_ms(field: str) -> float:
            return round(statistics.median(float(r[field] or 0) for r in rows) / 1e6, 3)
        surface_summary.append({
            "surface":       key,
            "count":         len(rows),
            "total_ms":      round(sum(float(r["dur"]) for r in rows) / 1e6, 3),
            "duration_ms":   median_ms("dur"),
            "binning_ms":    median_ms("binning"),
            "render_ms":     median_ms("render"),
            "preempt_ms":    median_ms("preempt"),
            "rendered_bins": statistics.median(float(r["rendered_bins"] or 0) for r in rows),
        })
    surface_summary.sort(key=lambda s: s["total_ms"], reverse=True)
    counter_summary = {
        row["name"]: {"mean": row["mean"], "min": row["min"], "max": row["max"], "samples": row["samples"]}
        for row in json.loads(counters.stdout)
    }
    return {"surfaces": surface_summary, "counters": counter_summary}


class Perfetto_capture:
    """metavr perf capture (custom mode: GPU render stages + GPU counters)
    of seconds, started delay seconds into the measured window."""

    def __init__(self, session: str, seconds: float, delay: float):
        self.session = session
        self.seconds = seconds
        self.delay = delay
        self.result = {}
        self.thread = None

    def start(self) -> None:
        def body():
            time.sleep(self.delay)
            capture = metavr("perf", "capture", "--mode", "custom", "--gpu-render-stage", "--gpu-metrics",
                             "--app", PKG, "--duration", str(int(self.seconds * 1000)), "-o", self.session,
                             timeout=self.seconds + 180)
            if capture.returncode != 0:
                self.result = {"session": self.session, "error": (capture.stdout + capture.stderr).strip()[-2000:]}
                return
            path = re.search(r"Path:\s*(\S+)", capture.stdout)
            self.result = {"session": self.session, "trace_path": path.group(1) if path else "",
                           "delay_s": self.delay, "seconds": self.seconds}
            try:
                self.result.update(summarize_perfetto(self.session))
            except (RuntimeError, ValueError, KeyError) as error:
                self.result["error"] = str(error)
        self.thread = threading.Thread(target=body, daemon=True)
        self.thread.start()

    def join(self) -> dict:
        if self.thread is None:
            return {}
        self.thread.join(timeout=self.delay + self.seconds + 600)
        return self.result


# --- Quest state (clocks, thermals, usage) ---------------------------------

# One device shell command line printing key=value lines; a value the shell
# user cannot read prints empty and is dropped.
DEVICE_SYSFS_SCRIPT = "; ".join([
    "for p in /sys/devices/system/cpu/cpufreq/policy*; do n=${p##*policy}; "
    "echo \"cpufreq.$n.cpus=$(cat $p/related_cpus 2>/dev/null)\"; "
    "echo \"cpufreq.$n.cur_khz=$(cat $p/scaling_cur_freq 2>/dev/null)\"; "
    "echo \"cpufreq.$n.max_khz=$(cat $p/scaling_max_freq 2>/dev/null)\"; "
    "echo \"cpufreq.$n.hw_max_khz=$(cat $p/cpuinfo_max_freq 2>/dev/null)\"; done",
    "for f in gpuclk max_gpuclk gpu_busy_percentage thermal_pwrlevel devfreq/cur_freq devfreq/max_freq; do "
    "echo \"kgsl.$f=$(cat /sys/class/kgsl/kgsl-3d0/$f 2>/dev/null)\"; done",
    "for z in /sys/class/thermal/thermal_zone*; do "
    "echo \"tz.$(cat $z/type 2>/dev/null)=$(cat $z/temp 2>/dev/null)\"; done",
    "grep '^cpu' /proc/stat | while read l; do echo \"stat.${l%% *}=${l#* }\"; done",
])

# android.os.Temperature types (dumpsys thermalservice mType).
THERMAL_TYPES = {
    0: "cpu", 1: "gpu", 2: "battery", 3: "skin", 4: "usb_port", 5: "power_amplifier",
    6: "bcl_voltage", 7: "bcl_current", 8: "bcl_percentage", 9: "npu",
}
HAL_TEMPERATURE_RE = re.compile(r"Temperature\{mValue=([-0-9.eE]+), mType=(-?\d+), mName=([^,]*), mStatus=(-?\d+)\}")


def read_device_sysfs() -> dict:
    """Clocks, GPU busy, thermal zones and /proc/stat counters as a flat dict."""
    text = adb_shell(DEVICE_SYSFS_SCRIPT, timeout=30).stdout
    values = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        value = value.strip()
        if (not separator) or (not value):
            continue
        key = key.strip()
        if key.startswith("stat."):
            values[key] = [int(v) for v in value.split()]
            continue
        # Thermal zone types repeat on some devices: keep each reading.
        base, index = key, 1
        while key in values:
            index += 1
            key = f"{base}#{index}"
        values[key] = value
    return values


def read_thermal_service() -> dict:
    text = adb_shell("dumpsys thermalservice", timeout=30).stdout
    status = re.search(r"Thermal Status:\s*(-?\d+)", text)
    # The HAL section lists the current temperatures ("Current temperatures
    # from HAL:"); the cached section above it repeats older values.
    hal_text = text.split("Current temperatures from HAL:", 1)[-1]
    hal_text = hal_text.split("Current cooling devices from HAL:", 1)[0]
    temperatures = []
    for match in HAL_TEMPERATURE_RE.finditer(hal_text):
        temperatures.append({
            "value_c": float(match.group(1)),
            "type":    THERMAL_TYPES.get(int(match.group(2)), match.group(2)),
            "name":    match.group(3),
            "status":  int(match.group(4)),
        })
    max_by_type = {}
    for temperature in temperatures:
        kind = temperature["type"]
        max_by_type[kind] = max(max_by_type.get(kind, -1000.0), temperature["value_c"])
    return {
        "thermal_status": int(status.group(1)) if status else None,
        "hal_max_c_by_type": max_by_type,
        "hal_temperatures": temperatures,
    }


def read_battery() -> dict:
    text = adb_shell("dumpsys battery", timeout=30).stdout
    values = {}
    for key in ("level", "temperature", "status", "AC powered", "USB powered"):
        match = re.search(rf"^\s*{re.escape(key)}:\s*(\S+)", text, re.MULTILINE)
        if match:
            values[key.replace(" ", "_").lower()] = match.group(1)
    if "temperature" in values:
        values["temperature_c"] = int(values.pop("temperature")) / 10.0  # tenths of a degree
    return values


def device_snapshot() -> dict:
    """Full Quest state: sysfs values, thermal service, battery."""
    snapshot = {"time": datetime.datetime.now().isoformat(timespec="seconds")}
    for key, reader in (("sysfs", read_device_sysfs), ("thermal", read_thermal_service), ("battery", read_battery)):
        try:
            snapshot[key] = reader()
        except Exception as error:  # a missing reading is recorded, not fatal
            snapshot[key] = {"error": str(error)}
    return snapshot


def cpu_usage_percent(a: list, b: list) -> float | None:
    """Busy % between two /proc/stat rows (user nice system idle iowait ...)."""
    if (not a) or (not b) or (len(a) < 5) or (len(b) < 5):
        return None
    total = sum(b) - sum(a)
    idle = (b[3] + b[4]) - (a[3] + a[4])
    return (100.0 * (total - idle) / total) if total > 0 else None


def stats_of(values: list) -> dict:
    return {"median": statistics.median(values), "min": min(values), "max": max(values), "samples": len(values)}


def summarize_device_samples(samples: list) -> dict:
    """Per value median / min / max over the measured window's samples."""
    series = {}
    for sample in samples:
        for key, value in sample.items():
            if key.startswith("stat.") or key.endswith(".cpus"):
                continue
            try:
                number = float(str(value).split()[0])  # "97 %" -> 97
            except (TypeError, ValueError, IndexError):
                continue
            series.setdefault(key, []).append(number)
    summary = {key: stats_of(values) for key, values in series.items()}
    # CPU usage from consecutive /proc/stat samples, and over the whole window.
    for key in sorted({k for sample in samples for k in sample if k.startswith("stat.")}):
        rows = [sample.get(key) for sample in samples if sample.get(key)]
        usages = [u for u in (cpu_usage_percent(rows[i], rows[i + 1]) for i in range(len(rows) - 1)) if u is not None]
        if usages:
            summary[f"usage.{key[len('stat.'):]}"] = stats_of(usages)
        if len(rows) >= 2:
            whole = cpu_usage_percent(rows[0], rows[-1])
            if whole is not None:
                summary.setdefault(f"usage.{key[len('stat.'):]}", {})["window"] = whole
    if samples:
        summary["cpufreq_policies"] = {
            key.split(".")[1]: value for key, value in samples[0].items() if key.startswith("cpufreq.") and key.endswith(".cpus")
        }
    return summary


class Device_sampler:
    """Samples the Quest's sysfs values every interval until stopped."""

    def __init__(self, interval_s: float):
        self.interval_s = interval_s
        self.samples = []
        self.stop_event = threading.Event()
        self.thread = None

    def start(self) -> None:
        def body():
            while not self.stop_event.is_set():
                try:
                    self.samples.append(read_device_sysfs())
                except Exception as error:
                    print(f"device sample failed: {error}", file=sys.stderr)
                self.stop_event.wait(self.interval_s)
        self.thread = threading.Thread(target=body, daemon=True)
        self.thread.start()

    def stop(self) -> dict:
        if self.thread is None:
            return {}
        self.stop_event.set()
        self.thread.join(timeout=60)
        return summarize_device_samples(self.samples)


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
    return bool(args.mcp or args.load_scene or args.mcp_after_load or args.mcp_measure)


def substitute_scene(value, scene_name: str):
    """Replaces $SCENE in every string of an MCP argument value."""
    if isinstance(value, str):
        if ("$SCENE" in value) and (not scene_name):
            raise RuntimeError(f"MCP argument '{value}' names $SCENE but no scene was loaded (--load-scene)")
        return value.replace("$SCENE", scene_name)
    if isinstance(value, list):
        return [substitute_scene(v, scene_name) for v in value]
    if isinstance(value, dict):
        return {k: substitute_scene(v, scene_name) for k, v in value.items()}
    return value


def make_mcp_calls(port: int, calls: list, scene_name: str, run_index: int) -> list:
    replies = []
    for call in calls:
        tool, arguments = parse_mcp_call(call)
        arguments = substitute_scene(arguments, scene_name)
        reply = mcp_call(port, tool, arguments)
        print(f"[{run_index}] MCP {tool}: {json.dumps(reply)[:300]}")
        if isinstance(reply, dict) and reply.get("isError"):
            raise RuntimeError(f"MCP {tool} failed: {json.dumps(reply)[:500]}")
        replies.append({"tool": tool, "arguments": arguments, "reply": reply})
    return replies


def warmup_config_state(port: int, warmup_replies: list) -> dict:
    """The configuration in effect after the warmup calls (pure queries,
    except a set_graphics_preset without arguments when no call made one)."""
    presets = [r["reply"] for r in warmup_replies if r["tool"] == "set_graphics_preset"]
    return {
        "graphics_preset":        presets[-1] if presets else mcp_call(port, "set_graphics_preset", {}),
        "culling_classification": mcp_call(port, "set_culling_classification", {}),
        "occlusion_culling":      mcp_call(port, "set_occlusion_culling", {}),
        "light_cluster_binning":  mcp_call(port, "set_light_cluster_binning", {}),
    }


def measure_config_state(port: int, scene_name: str) -> dict:
    """The light shading and lights of the frames being measured."""
    state = {"light_cluster_stats": mcp_call(port, "get_light_cluster_stats", {})}
    if scene_name:
        state["scene_lights"] = mcp_call(port, "get_scene_lights", {"scene_name": scene_name})
    return state


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
    perfetto = None
    sampler = None
    forwarded = False
    mcp_host = {}
    device_state = {}
    scene_name = ""
    started_at = datetime.datetime.now().isoformat(timespec="seconds")
    try:
        adb_shell(f"am force-stop {PKG}")
        adb_shell(f"rm -f {RESULT_DIR}/benchmark_{name}.json")
        device_state["before_launch"] = device_snapshot()
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
        measure_done = not uses_mcp(args)
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
                    if sampler is None:
                        sampler = Device_sampler(args.sample_interval)
                        sampler.start()
                        device_state["measure_start"] = device_snapshot()
                if args.gpu_metrics and (profiler is None) and (f"Benchmark {name}: scene settled" in line):
                    profiler = Gpu_profiler(gpu_metric_ids(args.gpu_metric_ids), measure_seconds)
                    profiler.start()
                if args.perfetto and (perfetto is None) and (f"Benchmark {name}: scene settled" in line):
                    perfetto = Perfetto_capture(f"{stamp}_{name}_{run_index}", args.perfetto_seconds, args.perfetto_delay)
                    perfetto.start()
                if f"Benchmark result {name}:" in line:
                    outcome = "result"
                    print(f"[{run_index}] {line[line.index('Benchmark result'):]}")
                    if sampler is not None:
                        device_state["measure_window"] = sampler.stop()
                        sampler = None
                    device_state["at_result"] = device_snapshot()
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
                mcp_host["warmup"] = make_mcp_calls(args.mcp_port, args.mcp, scene_name, run_index)
                if args.load_scene:
                    mcp_host["load_scene"] = mcp_load_scene(args.mcp_port, args.load_scene, args.timeout)
                    scene_name = mcp_host["load_scene"]["scene_name"]
                    print(f"[{run_index}] MCP load_scene: {json.dumps(mcp_host['load_scene'])}")
                mcp_host["after_load"] = make_mcp_calls(args.mcp_port, args.mcp_after_load, scene_name, run_index)
                mcp_host["config_state"] = warmup_config_state(args.mcp_port, mcp_host["warmup"] + mcp_host["after_load"])
                warmup_done = True
                continue  # lines that arrived during the calls are read next pass
            if settled and (not measure_done):
                mcp_call(args.mcp_port, "reset_composition_pass_stats")
                time.sleep(3.0)
                mcp_host["measure"] = make_mcp_calls(args.mcp_port, args.mcp_measure, scene_name, run_index)
                mcp_host["config_state"].update(measure_config_state(args.mcp_port, scene_name))
                print(f"[{run_index}] MCP measure calls done ({len(args.mcp_measure)} + configuration state)")
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
            "started_at": started_at,
            "stored_as": f"{stamp}_{name}_{run_index}.json",
            "config": args.config_info,
            "worn": bool(args.worn),
            "scene": args.scene or "",
            "capture": str(capture_path),
            "gpu_metrics": profiler.join() if profiler is not None else {},
            "perfetto": perfetto.join() if perfetto is not None else {},
            "editor_args": editor_args,
            "mcp_warmup": mcp_host.get("warmup", []),
            "load_scene": mcp_host.get("load_scene", {}),
            "mcp_after_load": mcp_host.get("after_load", []),
            "mcp_measure": mcp_host.get("measure", []),
            "config_state": mcp_host.get("config_state", {}),
            "device_state": device_state,
        }
        RUNS_DIR.mkdir(parents=True, exist_ok=True)
        out_path = RUNS_DIR / f"{stamp}_{name}_{run_index}.json"
        with open(out_path, "w", encoding="utf-8", newline="") as f:  # write_text(newline=) needs Python 3.10
            f.write(json.dumps(result, indent=2))
        print(f"[{run_index}] stored {out_path}")
        return result
    finally:
        if sampler is not None:
            sampler.stop()
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


# --- run record (doc/reference/quest_bench_runs.md) ---------------------------

RECORD_DOC = REPO_ROOT / "doc" / "reference" / "quest_bench_runs.md"


def fmt_range(values: list, digits: int = 1, scale: float = 1.0) -> str:
    """median [min..max] of values (one value: just it); '-' when empty."""
    values = [v * scale for v in values if v is not None]
    if not values:
        return "-"
    median = statistics.median(values)
    if len(values) == 1 or (min(values) == max(values)):
        return f"{median:.{digits}f}"
    return f"{median:.{digits}f} [{min(values):.{digits}f}..{max(values):.{digits}f}]"


def window_stat(device_state: dict, key: str) -> dict:
    return device_state.get("measure_window", {}).get(key, {})


def hal_max(snapshot: dict, kind: str):
    return snapshot.get("thermal", {}).get("hal_max_c_by_type", {}).get(kind)


def cpufreq_text(device_state: dict) -> str:
    """Per cpufreq policy: cores, median current clock [min..max] and the
    scaling cap, in MHz, over the measured window."""
    window = device_state.get("measure_window", {})
    parts = []
    for policy, cpus in sorted(window.get("cpufreq_policies", {}).items(), key=lambda item: int(item[0])):
        current = window.get(f"cpufreq.{policy}.cur_khz", {})
        cap = window.get(f"cpufreq.{policy}.max_khz", {})
        hw_max = window.get(f"cpufreq.{policy}.hw_max_khz", {})
        if not current:
            continue
        text = f"cpu {cpus}: {current['median'] / 1000:.0f}"
        if current["min"] != current["max"]:
            text += f" [{current['min'] / 1000:.0f}..{current['max'] / 1000:.0f}]"
        if cap:
            text += f" cap {cap['min'] / 1000:.0f}"
        if hw_max:
            text += f"/{hw_max['median'] / 1000:.0f}"
        parts.append(text)
    return "; ".join(parts) if parts else "-"


def gpu_clock_text(device_state: dict) -> str:
    for key in ("kgsl.gpuclk", "kgsl.devfreq/cur_freq"):
        stats = window_stat(device_state, key)
        if stats:
            text = f"{stats['median'] / 1e6:.0f}"
            if stats["min"] != stats["max"]:
                text += f" [{stats['min'] / 1e6:.0f}..{stats['max'] / 1e6:.0f}]"
            cap = window_stat(device_state, "kgsl.max_gpuclk") or window_stat(device_state, "kgsl.devfreq/max_freq")
            if cap:
                text += f" cap {cap['min'] / 1e6:.0f}"
            return text
    return "-"


def temperature_text(device_state: dict, kind: str) -> str:
    begin = hal_max(device_state.get("measure_start", {}), kind)
    end = hal_max(device_state.get("at_result", {}), kind)
    if (begin is None) and (end is None):
        return "-"
    return f"{f'{begin:.1f}' if begin is not None else '-'} -> {f'{end:.1f}' if end is not None else '-'}"


def lights_text(scene_lights) -> str:
    lights = scene_lights.get("lights", []) if isinstance(scene_lights, dict) else (scene_lights if isinstance(scene_lights, list) else [])
    if not lights:
        return "-"
    counts = {}
    for light in lights:
        entry = counts.setdefault(light.get("type", "?"), [0, 0, 0, 0])
        entry[0] += 1
        entry[1] += 1 if light.get("shaded") else 0
        entry[2] += 1 if light.get("shadow_mapped") else 0
        # shadow_cached: the shadow map cache kept the light's map (a 2D
        # layer, or the cube faces bit mask) from an earlier frame.
        entry[3] += 1 if (light.get("shadow_mapped") and light.get("shadow_cached")) else 0
    return "; ".join(
        f"{kind} {shaded}/{total} shaded, {mapped} shadow-mapped ({cached} cached)"
        for kind, (total, shaded, mapped, cached) in sorted(counts.items())
    ) + " (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn)"


def supported_text(reply: dict) -> str:
    """The device support flag of a culling setting's reply (a gpu mode
    falls back to the CPU path where the device lacks it)."""
    if "gpu_supported" not in reply:
        return ""
    return " (gpu supported)" if reply["gpu_supported"] else " (gpu NOT supported: CPU path)"


def light_shading_in_effect(cluster_stats) -> str:
    if not isinstance(cluster_stats, dict):
        return "-"
    views = [v for v in cluster_stats.get("views", []) if v.get("rendered_last_frame")]
    shading = sorted({f"{v.get('light_shading', '?')} ({v.get('scene_view', '?')})" for v in views})
    return ", ".join(shading) if shading else "-"


# Adreno counters shown in the run record (all are stored in host.perfetto).
PERFETTO_RECORD_COUNTERS = (
    "% Time Shading Fragments", "% Time Shading Vertices", "% Shaders Busy", "% Shader ALU Capacity Utilized",
    "% Vertex Fetch Stall", "% Texture Fetch Stall", "% Stalled on System Memory", "ALU / Fragment", "ALU / Vertex",
    "Fragments Shaded / Second", "Vertices Shaded / Second", "Pre-clipped Polygons/Second", "Average Polygon Area",
    "% Prims Trivially Rejected", "Textures / Fragment", "Textures / Vertex", "GPU Frequency",
)


def perfetto_markdown(results: list) -> list:
    """Per run: the surface with the most GPU time (the eye pass) and the
    selected counters of its --perfetto capture."""
    captured = [(index, r["host"]["perfetto"]) for index, r in enumerate(results)
                if r.get("host", {}).get("perfetto", {}).get("surfaces")]
    if not captured:
        return []
    lines = ["Perfetto capture (metavr perf capture, GPU render stages + Adreno counters, in the measured window); "
             "the surface with the most GPU time, medians per surface (ms):", "",
             "| Run | Surface | Surfaces | Duration | Binning | Bin rendering | Preempted | Rendered bins | Trace |",
             "|---|---|---|---|---|---|---|---|---|"]
    for index, perfetto in captured:
        top = perfetto["surfaces"][0]
        lines.append(f"| {index} | {top['surface']} | {top['count']} | {top['duration_ms']:.2f} | {top['binning_ms']:.2f} | "
                     f"{top['render_ms']:.2f} | {top['preempt_ms']:.2f} | {top['rendered_bins']:g} | `{perfetto.get('session', '')}` |")
    lines.append("")
    lines.append("| Counter (mean over the capture) | " + " | ".join(f"Run {index}" for index, _ in captured) + " |")
    lines.append("|---|" + "---|" * len(captured))
    for counter in PERFETTO_RECORD_COUNTERS:
        cells = []
        for _, perfetto in captured:
            stats = perfetto.get("counters", {}).get(counter)
            cells.append("-" if stats is None else f"{stats['mean']:.4g}")
        lines.append(f"| {counter} | " + " | ".join(cells) + " |")
    lines.append("")
    return lines


def record_markdown(results: list) -> str:
    first = results[0]
    host = first.get("host", {})
    identity = first.get("identity", {})
    config = host.get("config", {})
    state = host.get("config_state", {})
    preset = state.get("graphics_preset", {}) if isinstance(state.get("graphics_preset"), dict) else {}
    started = host.get("started_at", "").replace("T", " ")[:16]
    hashes = sorted({r.get("input_hash", "") for r in results})
    stored = [r.get("host", {}).get("stored_as", "") for r in results]
    values = [headline(r) for r in results]

    def column(key):
        return [v.get(key) for v in values if key in v]

    lines = []
    lines.append(f"## {started} - {first.get('name', '?')}, {config.get('name', 'no configuration file')}")
    lines.append("")
    if config.get("description"):
        lines.append(config["description"] + ".")
        lines.append("")
    load = host.get("load_scene", {})
    lines.append(f"- Runs: {len(results)} x {first.get('measure_frames', '?')} measured frames "
                 f"(warmup {first.get('warmup_frames', '?')}), display {first.get('refresh', {}).get('display_refresh_hz', '?')} Hz, "
                 f"input_hash {', '.join(hashes)}; results {', '.join(f'`{name}`' for name in stored if name)} "
                 "(`memory-bank/local/quest_runs/`, per machine).")
    lines.append(f"- Build: {identity.get('version', '?')}; {identity.get('device_model', '?')}, OS {identity.get('os_build', '?')}; "
                 f"performance levels CPU {identity.get('cpu_performance_level', '?')} / GPU {identity.get('gpu_performance_level', '?')}.")
    lines.append(f"- Launch: `{host.get('editor_args', '')}`" + (
        f"; scene `{os.path.basename(load.get('path', ''))}` (listed {load.get('listed_s', '?')} s, idle {load.get('idle_s', '?')} s)" if load else "") + ".")
    calls = [c["tool"] for c in host.get("mcp_warmup", []) + host.get("mcp_after_load", [])]
    if calls:
        lines.append(f"- Configuration calls: {', '.join(calls)} (arguments in the result files' `host` block).")
    lines.append("")
    lines.append("| Configuration | |")
    lines.append("|---|---|")
    culling = state.get("culling_classification", {})
    occlusion = state.get("occlusion_culling", {})
    binning = state.get("light_cluster_binning", {})
    rows = [
        ("Light shading (setting / in effect)", f"{preset.get('light_shading', '-')} / {light_shading_in_effect(state.get('light_cluster_stats'))}"),
        ("Lights", lights_text(state.get("scene_lights"))),
        ("Light limits shadow+unshadowed", "directional {}+{}, spot {}+{}, point {}+{}".format(
            preset.get("directional_shadow_light_count", "-"), preset.get("directional_unshadowed_light_count", "-"),
            preset.get("spot_shadow_light_count", "-"), preset.get("spot_unshadowed_light_count", "-"),
            preset.get("point_shadow_light_count", "-"), preset.get("point_unshadowed_light_count", "-"))),
        ("Shadows", "{}; {} px, point {} px, {} ({} bits), {}, {}, {}, {}".format(
            "on" if preset.get("shadow_enable") else ("off" if preset else "-"),
            preset.get("shadow_resolution", "-"), preset.get("point_shadow_resolution", "-"),
            preset.get("shadow_map_format", "-"), preset.get("shadow_depth_bits", "-"),
            preset.get("shadow_technique", "-"), preset.get("shadow_filter", "-"),
            preset.get("shadow_bias", "-"), preset.get("shadow_cull_mode", "-"))),
        ("Light cluster binning", binning.get("mode", "-") if isinstance(binning, dict) else "-"),
        ("Culling classification", (culling.get("mode", "-") + supported_text(culling)) if isinstance(culling, dict) else "-"),
        ("Occlusion culling", (("on" if occlusion.get("enabled") else "off") + supported_text(occlusion)) if isinstance(occlusion, dict) and ("enabled" in occlusion) else "-"),
        ("Draw lists", ("on" if preset.get("use_draw_lists") else "off") if "use_draw_lists" in preset else "-"),
        ("Specular anti-aliasing", preset.get("specular_anti_aliasing", "-")),
        ("Preset", preset.get("preset_name", "-")),
    ]
    for label, text in rows:
        lines.append(f"| {label} | {text} |")
    lines.append("")
    lines.append("| Result (median [min..max] of the runs' medians) | |")
    lines.append("|---|---|")
    result_rows = [
        ("Frames per second", fmt_range(column("frames_per_second"), 2)),
        ("Frame wall time (ms)", fmt_range(column("wall_us"), 2, 0.001)),
        ("M2 / R / M3 (ms)", " / ".join(fmt_range(column(t), 2, 0.001) for t in ("m2_us", "r_us", "m3_us"))),
        ("App GPU frame time, XR counter (ms)", fmt_range(column("xr:app/gpu_frametime"), 2)),
        ("GPU utilization, XR counter (%)", fmt_range(column("xr:device/gpu_utilization"), 1)),
        ("CPU utilization average / worst, XR counter (%)", fmt_range(column("xr:device/cpu_utilization_average"), 1) + " / " + fmt_range(column("xr:device/cpu_utilization_worst"), 1)),
    ]
    gpu_pass_keys = [k for k in dict.fromkeys(k for v in values for k in v) if k.startswith("gpu_pass:")]
    for key in gpu_pass_keys:
        result_rows.append((f"GPU pass {key[len('gpu_pass:'):]} (ms)", fmt_range(column(key), 3, 0.001)))
    for label, text in result_rows:
        lines.append(f"| {label} | {text} |")
    lines.append("")
    lines.extend(perfetto_markdown(results))
    lines.append("Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; "
                 "clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).")
    lines.append("")
    lines.append("| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|---|")
    for index, result in enumerate(results):
        device = result.get("host", {}).get("device_state", {})
        ident = result.get("identity", {})
        start = device.get("measure_start", {}).get("thermal", {}).get("thermal_status")
        end = device.get("at_result", {}).get("thermal", {}).get("thermal_status")
        editor_status = f"{ident.get('thermal_status_begin', '-')} -> {ident.get('thermal_status_end', '-')}, step-downs {ident.get('thermal_step_downs_in_window', '-')}"
        battery = device.get("at_result", {}).get("battery", {})
        battery_text = f"{battery.get('temperature_c', '-')}, {battery.get('level', '-')} %" if battery else "-"
        busy = window_stat(device, "kgsl.gpu_busy_percentage")
        usage = window_stat(device, "usage.cpu")
        lines.append("| {} | {:.2f} | {} -> {}; {} | {} | {} | {} | {} | {} | {} | {} | {} |".format(
            index, result.get("frames_per_second", 0.0),
            start if start is not None else "-", end if end is not None else "-", editor_status,
            temperature_text(device, "cpu"), temperature_text(device, "gpu"), temperature_text(device, "skin"),
            battery_text, cpufreq_text(device), gpu_clock_text(device),
            fmt_range([busy["median"]], 0) if busy else "-",
            f"{usage['window']:.1f}" if usage and ("window" in usage) else "-"))
    lines.append("")
    return "\n".join(lines) + "\n"


def append_record(results: list, doc_path: Path) -> None:
    if not doc_path.exists():
        raise RuntimeError(f"record document {doc_path} missing")
    text = doc_path.read_text(encoding="utf-8")
    if not text.endswith("\n"):
        text += "\n"
    doc_path.write_text(text + "\n" + record_markdown(results), encoding="utf-8", newline="")
    print(f"recorded in {doc_path}")


def load_bench_config(args) -> None:
    """Merges a --config file's calls in front of the command line's."""
    args.config_info = {}
    if not args.config:
        return
    path = REPO_ROOT / args.config
    config = json.loads(path.read_text(encoding="utf-8"))
    for key, attribute in (("mcp", "mcp"), ("mcp_after_load", "mcp_after_load"), ("mcp_measure", "mcp_measure")):
        setattr(args, attribute, list(config.get(key, [])) + getattr(args, attribute))
    args.config_info = {
        "path": args.config.replace("\\", "/"),
        "name": config.get("name", path.stem),
        "description": config.get("description", ""),
    }


def cmd_run(args) -> int:
    bench_path = REPO_ROOT / args.bench
    bench = json.loads(bench_path.read_text(encoding="utf-8"))
    load_bench_config(args)
    if args.perfetto and args.gpu_metrics:
        print("--perfetto and --gpu-metrics both read the Adreno counters: pass one of them")
        return 2
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
        with open(out_path, "w", encoding="utf-8", newline="") as f:  # write_text(newline=) needs Python 3.10
            f.write(json.dumps(summary, indent=2))
        print_repeat(summary)
        print(f"stored {out_path}")
    else:
        values = headline(results[0])
        for key, value in values.items():
            print(f"{key:<44} {value:>14.3f}")
    if args.record:
        append_record(results, Path(args.record))
    return 0


def cmd_record(args) -> int:
    results = []
    for path in map(Path, args.results):
        result = json.loads(path.read_text(encoding="utf-8"))
        # Results stored before the host block named them: the file name
        # (<YYYYmmdd_HHMMSS>_<name>_<run>.json) gives both.
        host = result.setdefault("host", {})
        host.setdefault("stored_as", path.name)
        stamp = re.match(r"^(\d{8}_\d{6})_", path.name)
        if stamp and not host.get("started_at"):
            host["started_at"] = datetime.datetime.strptime(stamp.group(1), "%Y%m%d_%H%M%S").isoformat(timespec="seconds")
        results.append(result)
    append_record(results, Path(args.doc))
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
    run.add_argument("--perfetto", action="store_true", help="metavr perf capture (GPU render stages + GPU counters) in the measured window")
    run.add_argument("--perfetto-seconds", type=float, default=4.0, help="length of the --perfetto capture")
    run.add_argument("--perfetto-delay", type=float, default=3.0, help="seconds into the measured window the --perfetto capture starts")
    run.add_argument("--repeat", type=int, default=1, help="number of runs (one launch each)")
    run.add_argument("--push", action="store_true", help="copy the local --bench / --scene files over the device's writable copies first")
    run.add_argument("--timeout", type=float, default=600.0, help="seconds to wait for the result line per run")
    run.add_argument("--bench-to-device", action="store_true", help="adb push the benchmark file to the app's external files directory and pass that path (works on a release APK)")
    run.add_argument("--editor-args", default="", help="extra editor arguments, e.g. --no-scene")
    run.add_argument("--mcp", action="append", default=[], metavar="TOOL[=JSON]", help="MCP call made during the warmup (repeatable)")
    run.add_argument("--load-scene", default="", help="device path of a scene file: load_scene, wait idle, set_headset_scene during the warmup")
    run.add_argument("--mcp-after-load", action="append", default=[], metavar="TOOL[=JSON]", help="MCP call made after the scene load; $SCENE is replaced by the scene name (repeatable)")
    run.add_argument("--mcp-measure", action="append", default=[], metavar="TOOL[=JSON]", help="MCP call made in the measured window, reply stored in the result (repeatable)")
    run.add_argument("--config", default="", help="bench configuration file (scripts/quest_bench_configs/), repo-relative")
    run.add_argument("--sample-interval", type=float, default=2.0, help="seconds between Quest state samples in the measured window")
    run.add_argument("--record", nargs="?", const=str(RECORD_DOC), default="", metavar="DOC", help="append the run record to DOC (default doc/reference/quest_bench_runs.md)")
    run.add_argument("--mcp-port", type=int, default=3743, help="host port forwarded to the device's MCP port")
    run.set_defaults(func=cmd_run)

    record = sub.add_parser("record", help="append the run record of stored per-run results to the record document")
    record.add_argument("results", nargs="+", help="per-run result files of one invocation")
    record.add_argument("--doc", default=str(RECORD_DOC), help="record document (default doc/reference/quest_bench_runs.md)")
    record.set_defaults(func=cmd_record)

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
