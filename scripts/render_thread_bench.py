#!/usr/bin/env python3
"""Unpaced frame-time measurement of the editor's render thread configurations.

doc/editor/render_thread.md section 3.4: the same scene in the
configurations `inline` (ERHE_RENDER_THREAD=0), `dedicated`
(ERHE_RENDER_THREAD=1 ERHE_IMGUI_OVERLAP=0), `overlap` (ERHE_RENDER_THREAD=1
ERHE_IMGUI_OVERLAP=1), all three with ERHE_FRAME_PIPELINING=0 (the serial
schedule: the main thread joins R before M2), and `pipelined` (the overlap
plus ERHE_FRAME_PIPELINING=1: the kick joins the previous frame's R, so M1,
M2 and M3 of frame N+1 run while R renders frame N), with vsync and frame pacing forced off and no MCP polling; the editor quits by itself after
--frames frames and the `Phase timing` lines of logs/log.txt are averaged
over the steady state (the first --skip lines are dropped).

Every run gets an isolated working directory (copy of config/ with the
overrides applied, res/ symlinked, shared spirv cache), the same way
scripts/idle_memory_check.py launches the editor, so the user's config/ is
never touched. `--tracy` starts tracy-capture once each run is ready and saves
<run_dir>/capture.tracy (the editor must be built with
ERHE_PROFILE_LIBRARY=tracy; the Tracy client records while the capture is
connected).

Examples:
  python3 scripts/render_thread_bench.py --build-dir build/Ninja_Vulkan_Debug
  python3 scripts/render_thread_bench.py --build-dir build/Ninja_Vulkan_Release \
      --scene heavy --configs overlap --tracy --frames 1200
  # CPU-bound on an integrated GPU: small window and viewport
  python3 scripts/render_thread_bench.py --build-dir build/Ninja_Vulkan_Release \
      --scene draws --configs inline,dedicated,overlap,pipelined --gpu intel \
      --set 'window.json:size=[960,540]' --viewport-size 320x200 --frames 720

With pipelining the phase log's join_wait is the wait inside the kick for
the previous frame's R; full tick still is the main thread's tick.
"""

import argparse
from collections import defaultdict
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))
from idle_memory_check import apply_override  # noqa: E402

READINESS_MARKER = "Main loop: completed frame 12"

SCENES = {
    "default": "config/editor/commands.json",
    "heavy":   "config/editor/commands_bench_heavy.json",
    "draws":   "config/editor/commands_bench_draws.json",
}

# The serial configurations set ERHE_FRAME_PIPELINING=0 explicitly: the
# pipelining defaults on with a dedicated render thread (9.3.3).
CONFIGS = {
    "inline":    {"ERHE_RENDER_THREAD": "0", "ERHE_FRAME_PIPELINING": "0"},
    "dedicated": {"ERHE_RENDER_THREAD": "1", "ERHE_IMGUI_OVERLAP": "0", "ERHE_FRAME_PIPELINING": "0"},
    "overlap":   {"ERHE_RENDER_THREAD": "1", "ERHE_IMGUI_OVERLAP": "1", "ERHE_FRAME_PIPELINING": "0"},
    "pipelined": {"ERHE_RENDER_THREAD": "1", "ERHE_IMGUI_OVERLAP": "1", "ERHE_FRAME_PIPELINING": "1"},
}

# The log line written by Editor::log_phase_timing(); every "<name> <float>"
# pair is captured by PAIR_RE, the nested names are unique.
PHASE_LINE_RE = re.compile(r"Phase timing \(us/frame, avg of (\d+) frames\): (.*)$")
PAIR_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_ ]*?) (-?\d+\.\d+)")

COLUMNS = [
    ("full tick", "full tick"),
    ("M1", "M1"),
    ("frame_start_wait", "frame_start_wait"),
    ("M2", "M2"),
    ("slot_acquire", "slot_acquire"),
    ("imgui", "imgui"),
    ("M3", "M3"),
    ("R", "R"),
    ("swapchain", "swapchain"),
    ("draw_data_wait", "draw_data_wait"),
    ("join_wait", "join_wait"),
    ("kick", "kick"),
    ("join", "join"),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build/Ninja_Vulkan_Debug", help="build tree containing bin/editor (or bin/<config>/editor)")
    parser.add_argument("--config", default="Release", help="configuration subdirectory of bin/ for multi-config trees (Xcode, Visual Studio)")
    parser.add_argument("--scene", choices=sorted(SCENES), default="default", help="startup commands file to load")
    parser.add_argument("--configs", default="inline,dedicated,overlap", help="comma-separated subset of inline,dedicated,overlap,pipelined")
    parser.add_argument("--frames", type=int, default=720, help="frames to run before the editor quits by itself")
    parser.add_argument("--skip", type=int, default=3, help="phase log lines (60 frames each) dropped as warm-up")
    parser.add_argument("--repeat", type=int, default=1, help="runs per configuration; medians are reported")
    parser.add_argument("--tracy", action="store_true", help="record a Tracy capture of every run (tracy-capture on PATH or --tracy-capture)")
    parser.add_argument("--tracy-capture", default="", help="path to the tracy-capture executable")
    parser.add_argument("--keep-vsync", action="store_true", help="do not force vsync and frame pacing off (display-paced run)")
    parser.add_argument("--set", dest="overrides", action="append", default=[], help="extra config override 'file.json:dotted.path=value' (idle_memory_check.py syntax)")
    parser.add_argument("--wrapper", default="", help="command prefix for the editor, e.g. 'taskset -c 0-7' (split on spaces)")
    parser.add_argument("--sample-cpus", action="store_true", help="sample which CPU each editor thread runs on (per-thread histogram in the summary)")
    parser.add_argument("--viewport-size", default="", help="WxH: float the first viewport window at this size (edits the copied ImGui ini) to cut GPU work")
    parser.add_argument("--gpu", default="", help="Vulkan ICD to select, e.g. nvidia or intel (Linux), moltenvk or kosmickrisp (macOS), or a path to an ICD json: sets VK_DRIVER_FILES and labels the runs")
    parser.add_argument("--label", default="", help="label added to the run directory names")
    parser.add_argument("--runs-root", default="", help="where run directories go (default <build-dir>/render_thread_bench)")
    parser.add_argument("--readiness-timeout", type=float, default=300.0)
    parser.add_argument("--run-timeout", type=float, default=900.0, help="seconds to wait for the editor to quit by itself")
    parser.add_argument("--spirv-optimization", choices=("on", "off"), default="on", help="ERHE_SPIRV_OPTIMIZATION for the editor: profiling runs use optimized SPIR-V (Graphics_config::spirv_optimization; default on)")
    return parser.parse_args()


def make_run_dir(args: argparse.Namespace, runs_root: Path, name: str, extra_overrides: list) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = runs_root / f"{stamp}_{name}"
    run_dir.mkdir()
    shutil.copytree(REPO_ROOT / "config", run_dir / "config")
    (run_dir / "res").symlink_to(REPO_ROOT / "res")
    (run_dir / "logs").mkdir()
    shared_spirv = runs_root / "spirv_cache_shared"
    shared_spirv.mkdir(exist_ok=True)
    (run_dir / "spirv_cache").symlink_to(shared_spirv)
    overrides = list(args.overrides) + extra_overrides
    for spec in overrides:
        apply_override(run_dir / "config", spec)
    if args.viewport_size:
        width, height = (int(v) for v in args.viewport_size.lower().split("x"))
        ini = run_dir / "config" / "editor" / "desktop_window_imgui_host_imgui.ini"
        # The ini is gitignored: a clone that never ran the windowed editor
        # has none, and the entry alone is a valid ini.
        text = ini.read_text() if ini.is_file() else ""
        entry = f"[Window][Viewport_window 1]\nPos=8,28\nSize={width},{height}\nCollapsed=0\n"
        # Drop the existing section (up to the next one) and append the entry.
        out = []
        skipping = False
        for line in text.split("\n"):
            if line.startswith("[Window][Viewport_window 1]"):
                skipping = True
                continue
            if skipping and line.startswith("["):
                skipping = False
            if not skipping:
                out.append(line)
        ini.write_text("\n".join(out).rstrip("\n") + "\n\n" + entry)
    (run_dir / "overrides.txt").write_text("\n".join(overrides) + "\n")
    return run_dir


def parse_phase_lines(log_text: str) -> list:
    rows = []
    for line in log_text.splitlines():
        match = PHASE_LINE_RE.search(line)
        if match is None:
            continue
        row = {"frames": int(match.group(1))}
        for name, value in PAIR_RE.findall(match.group(2)):
            row[name.strip()] = float(value)
        rows.append(row)
    return rows


def steady_state(rows: list, skip: int) -> dict:
    used = rows[skip:] if len(rows) > skip else rows[-1:]
    if not used:
        return {}
    result = {"lines": len(used)}
    for key in used[0]:
        if key == "frames":
            continue
        result[key] = statistics.fmean(row[key] for row in used if key in row)
    return result


def sample_thread_cpus(pid: int, histogram: dict) -> None:
    """Record the CPU each thread of `pid` last ran on (field 39 of /proc/<pid>/task/<tid>/stat)."""
    task_dir = Path("/proc") / str(pid) / "task"
    try:
        tids = list(task_dir.iterdir())
    except OSError:
        return
    for tid in tids:
        try:
            stat = (tid / "stat").read_text()
        except OSError:
            continue
        name_end = stat.rfind(")")
        name = stat[stat.find("(") + 1:name_end]
        fields = stat[name_end + 2:].split()
        if len(fields) > 36:
            histogram[name][int(fields[36])] += 1


ICD_DIRS = [Path("/usr/share/vulkan/icd.d"), Path("/usr/local/share/vulkan/icd.d")]


def find_icd(gpu: str) -> str:
    """The ICD json for --gpu: a path as given, else the one <gpu>_icd.json (case-insensitive, any prefix) in ICD_DIRS."""
    if gpu.endswith(".json"):
        return gpu
    matches = [f for d in ICD_DIRS if d.is_dir() for f in sorted(d.iterdir()) if f.name.lower().endswith(f"{gpu.lower()}_icd.json")]
    if len(matches) != 1:
        raise SystemExit(f"--gpu {gpu}: expected one matching ICD in {', '.join(str(d) for d in ICD_DIRS)}, found {[str(m) for m in matches]}")
    return str(matches[0])


def find_editor(args: argparse.Namespace) -> Path:
    single = REPO_ROOT / args.build_dir / "bin" / "editor"
    if single.is_file():
        return single
    return REPO_ROOT / args.build_dir / "bin" / args.config / "editor"


def find_tracy_capture(args: argparse.Namespace) -> str:
    if args.tracy_capture:
        return args.tracy_capture
    found = shutil.which("tracy-capture")
    if found:
        return found
    home_build = Path.home() / "git" / "tracy" / "capture" / "build" / "tracy-capture"
    if home_build.is_file():
        return str(home_build)
    raise SystemExit("tracy-capture not found; pass --tracy-capture")


def run_once(args: argparse.Namespace, runs_root: Path, config_name: str, index: int) -> dict:
    name = f"{args.scene}_{config_name}_{index}" + (f"_{args.gpu}" if args.gpu else "") + (f"_vp{args.viewport_size}" if args.viewport_size else "") + (f"_{args.label}" if args.label else "")
    overrides = [f"editor_settings.json:quit_after_frames={args.frames}"]
    if not args.keep_vsync:
        overrides.append("erhe_graphics.json:force_disable_vsync=true")
        # The frame pacer (tier S slop servo when the device lacks present
        # timing) would pace the frame start; an unpaced run turns it off
        # (force_disable_vsync alone also resolves the tier to off).
        overrides.append("erhe_graphics.json:frame_pacing_tier=off")
    run_dir = make_run_dir(args, runs_root, name, overrides)
    editor = find_editor(args)
    env = dict(os.environ)
    env["ERHE_AI_DRIVER"] = "1"
    env["ERHE_SPIRV_OPTIMIZATION"] = "1" if args.spirv_optimization == "on" else "0"
    if args.gpu:
        env["VK_DRIVER_FILES"] = find_icd(args.gpu)
    env.update(CONFIGS[config_name])

    cmd = (args.wrapper.split() if args.wrapper else []) + [str(editor), "--commands", SCENES[args.scene]]
    stdout_log = (run_dir / "editor_stdout.log").open("wb")
    t0 = time.monotonic()
    proc = subprocess.Popen(cmd, cwd=run_dir, env=env, stdout=stdout_log, stderr=subprocess.STDOUT, start_new_session=True)
    print(f"[{name}] pid {proc.pid} in {run_dir}", flush=True)

    log_file = run_dir / "logs" / "log.txt"
    ready = False
    while time.monotonic() - t0 < args.readiness_timeout:
        if proc.poll() is not None:
            break
        if log_file.is_file() and READINESS_MARKER in log_file.read_text(errors="replace"):
            ready = True
            break
        time.sleep(0.5)
    if not ready and proc.poll() is None:
        os.killpg(proc.pid, signal.SIGKILL)
        raise SystemExit(f"[{name}] readiness marker not seen; killed")

    # The capture connects after the readiness marker: connecting while
    # the Tracy client is still starting up (macOS) ends in "disconnected
    # during the initial connection handshake". The client records on
    # demand, so the capture starts at frame 12; the warm-up skip covers it.
    capture = None
    if args.tracy and ready:
        capture_exe = find_tracy_capture(args)
        capture_log = (run_dir / "tracy_capture.log").open("wb")
        capture = subprocess.Popen(
            [capture_exe, "-o", str(run_dir / "capture.tracy"), "-a", "127.0.0.1", "-f"],
            stdout=capture_log, stderr=subprocess.STDOUT
        )

    cpu_samples = defaultdict(lambda: defaultdict(int))
    deadline = time.monotonic() + args.run_timeout
    while proc.poll() is None and time.monotonic() < deadline:
        if args.sample_cpus:
            sample_thread_cpus(proc.pid, cpu_samples)
        time.sleep(0.2)
    try:
        proc.wait(timeout=max(0.0, deadline - time.monotonic()))
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        raise SystemExit(f"[{name}] editor did not quit after {args.frames} frames; killed")
    if capture is not None:
        try:
            capture.wait(timeout=120)
        except subprocess.TimeoutExpired:
            capture.kill()
            print(f"[{name}] tracy-capture did not finish; killed", flush=True)

    rows = parse_phase_lines(log_file.read_text(errors="replace"))
    summary = steady_state(rows, args.skip)
    if args.sample_cpus:
        summary["thread_cpus"] = {name: dict(sorted(hist.items())) for name, hist in cpu_samples.items()}
        for name, hist in sorted(cpu_samples.items()):
            total = sum(hist.values())
            top = sorted(hist.items(), key=lambda item: item[1], reverse=True)[:6]
            print(f"[{name}] thread '{name}': " + ", ".join(f"cpu{cpu} {100 * count // total}%" for cpu, count in top), flush=True)
    summary["exit_code"] = proc.returncode
    summary["run_dir"] = str(run_dir)
    summary["phase_lines"] = len(rows)
    (run_dir / "summary.json").write_text(json.dumps({"rows": rows, "steady_state": summary}, indent=2) + "\n")
    print(f"[{name}] exit {proc.returncode}, {len(rows)} phase lines, full tick {summary.get('full tick', float('nan')):.1f} us", flush=True)
    return summary


def median_summary(summaries: list) -> dict:
    keys = set()
    for summary in summaries:
        keys.update(k for k, v in summary.items() if isinstance(v, float))
    result = {}
    for key in keys:
        values = [s[key] for s in summaries if key in s]
        result[key] = statistics.median(values)
    result["runs"] = [s["run_dir"] for s in summaries]
    return result


def format_table(results: dict) -> str:
    configs = list(results)
    lines = ["| Term (us/frame) | " + " | ".join(configs) + " |", "|---|" + "---|" * len(configs)]
    for label, key in COLUMNS:
        cells = []
        for config in configs:
            value = results[config].get(key)
            cells.append("-" if value is None else f"{value:.1f}")
        lines.append(f"| {label} | " + " | ".join(cells) + " |")
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    editor = find_editor(args)
    if not editor.is_file():
        print(f"error: editor binary not found: {editor}", file=sys.stderr)
        return 2
    configs = [c.strip() for c in args.configs.split(",") if c.strip()]
    for config in configs:
        if config not in CONFIGS:
            print(f"error: unknown config '{config}'", file=sys.stderr)
            return 2
    runs_root = Path(args.runs_root) if args.runs_root else (REPO_ROOT / args.build_dir / "render_thread_bench")
    runs_root.mkdir(parents=True, exist_ok=True)

    results = {}
    for config in configs:
        summaries = [run_once(args, runs_root, config, index) for index in range(args.repeat)]
        results[config] = median_summary(summaries)

    table = format_table(results)
    header = f"build {args.build_dir}, gpu {args.gpu or 'default'}, scene {args.scene}, {args.frames} frames, skip {args.skip} lines, repeat {args.repeat}, vsync {'kept' if args.keep_vsync else 'off'}"
    print()
    print(header)
    print(table)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out = runs_root / f"{stamp}_{args.scene}{('_' + args.gpu) if args.gpu else ''}_results.md"
    out.write_text(header + "\n\n" + table + "\n\n" + json.dumps(results, indent=2) + "\n")
    print(f"written {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
