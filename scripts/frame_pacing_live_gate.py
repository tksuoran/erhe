"""Live frame pacing gate: fresh editor launches, settled samples, one table.

    python3 scripts/frame_pacing_live_gate.py --editor build_xcode_metal/bin/Debug/editor
        [--runs 5] [--settle 15] [--windows 3] [--env ERHE_RENDER_THREAD=0 ...]
        [--graphics key=value ...] [--pin-min-vsyncs N] [--out DIR]

Each run launches the editor (ERHE_AI_DRIVER=1, repo root as working
directory), waits for the MCP server, waits --settle seconds, fetches
get_frame_pacing_status and --windows x get_frame_pacing_frames (the
recorder's 120 newest frames each, 1.2 s apart), asks the editor to exit and
waits for it. --graphics overrides keys of config/editor/erhe_graphics.json
for the batch (the file is restored afterwards); values are JSON
(frame_pacing_tier=\\"s\\", force_disable_vsync=true). --pin-min-vsyncs sets
the pacer's cadence floor through set_frame_pacing_min_vsyncs after the
settle and waits another --settle before sampling.

Per run it prints: the pacer cadence N, refreshes per displayed frame
(achieved present deltas in refresh periods), release-to-display latency
(achieved present minus pacer_wait_end, or cpu_slot_begin), the presenting
command buffer's GPU span (GPUEndTime - GPUStartTime on Metal) and its share
beyond one refresh period, the GPU span by start phase within the refresh
period (8 bins; phase 0 = a display time), the deadline offset estimate, and
whether the log reported undisplayed drawables (an occluded window gives no
presentation feedback). The editor window must be visible on the display; on macOS the script holds
a caffeinate assertion (display and system awake) for the batch.

Measurement background and the plan this serves: doc/plans/frame_pacing_metal.md.
"""

import argparse
import collections
import json
import os
import pathlib
import statistics
import subprocess
import sys
import time
import urllib.request

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
GRAPHICS_CONFIG = REPO_ROOT / "config" / "editor" / "erhe_graphics.json"
LOG_PATH = REPO_ROOT / "logs" / "log.txt"


def mcp(port, *args):
    command = [sys.executable, str(REPO_ROOT / "scripts" / "mcp_call.py"), *args, "--port", str(port)]
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"mcp_call.py {args[0]} failed: {completed.stderr.strip() or completed.stdout.strip()}")
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError:
        return completed.stdout.strip() # tools that answer with plain text (request_exit)


def wait_health(port, timeout_s):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=1.0) as response:
                if response.status == 200:
                    return True
        except OSError:
            pass
        time.sleep(0.5)
    return False


def quantile(values, q):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * q))]


def analyze(frames, status):
    frames = sorted({f["frame_id"]: f for f in frames}.values(), key=lambda f: f["frame_id"])
    pacer = status.get("pacer", {})
    period = pacer.get("grid_period") or (status.get("display_refresh_period_ms", 0.0) / 1000.0)
    result = {"N": pacer.get("vsyncs_per_frame"), "deadline_offset_ms": pacer.get("deadline_offset_ms"), "thermal": status.get("thermal_state")}
    achieved = [(f["frame_id"], f["achieved_present_time"]) for f in frames if f["achieved_present_time"] > 0.0]
    refreshes = collections.Counter()
    for (id_a, t_a), (id_b, t_b) in zip(achieved, achieved[1:]):
        if id_b == id_a + 1 and period > 0.0:
            refreshes[round((t_b - t_a) / period)] += 1
    result["refreshes"] = dict(sorted(refreshes.items()))
    latency = []
    for f in frames:
        start = f["pacer_wait_end"] if f["pacer_wait_end"] > 0.0 else f["cpu_slot_begin"]
        if (f["achieved_present_time"] > 0.0) and (start > 0.0):
            latency.append((f["achieved_present_time"] - start) * 1000.0)
    if latency:
        result["latency_ms"] = (statistics.median(latency), quantile(latency, 0.95))
    spans = [((f["gpu_end"] - f["gpu_begin"]) * 1000.0, f["gpu_begin"]) for f in frames if f["gpu_end"] > f["gpu_begin"] > 0.0]
    clocks = [f.get("gpu_clock_ratio", 0.0) for f in frames if f.get("gpu_clock_ratio", 0.0) > 0.0]
    if clocks:
        result["gpu_clock"] = statistics.median(clocks)
    cpu_clocks = [f.get("cpu_clock_ratio", 0.0) for f in frames if f.get("cpu_clock_ratio", 0.0) > 0.0]
    if cpu_clocks:
        result["cpu_clock"] = statistics.median(cpu_clocks)
    if spans and period > 0.0:
        values = [s for s, _ in spans]
        result["gpu_span_ms"] = (statistics.median(values), quantile(values, 0.9), max(values))
        result["gpu_beyond_period_pct"] = 100.0 * sum(1 for s in values if s > period * 1000.0) / len(values)
        if achieved:
            reference = achieved[0][1]
            bins = collections.defaultdict(list)
            for span, begin in spans:
                bins[int((((begin - reference) / period) % 1.0) * 8)].append(span)
            result["span_by_phase"] = {
                f"{b / 8:.3f}": (len(v), statistics.median(v), sum(1 for s in v if s > period * 1000.0))
                for b, v in sorted(bins.items())
            }
    return result


def print_run(index, label, result, undisplayed):
    latency = result.get("latency_ms")
    span = result.get("gpu_span_ms")
    print(
        f"run {index} {label}: N={result['N']} refreshes={result['refreshes']} "
        + (f"latency med {latency[0]:.1f} p95 {latency[1]:.1f} ms " if latency else "latency - ")
        + (f"gpu span med {span[0]:.2f} p90 {span[1]:.2f} max {span[2]:.2f} ms ({result['gpu_beyond_period_pct']:.0f}% > T) " if span else "")
        + (f"dpe {result['deadline_offset_ms']:.2f} ms" if result.get("deadline_offset_ms") is not None else "")
        + (f" thermal {result['thermal']}" if result.get("thermal") else "")
        + (f" gpu clock {result['gpu_clock'] * 100.0:.0f}%" if result.get("gpu_clock") else "")
        + (f" cpu clock {result['cpu_clock'] * 100.0:.0f}%" if result.get("cpu_clock") else "")
        + ("  [UNDISPLAYED DRAWABLES - window occluded?]" if undisplayed else "")
    )
    phases = result.get("span_by_phase")
    if phases:
        print("    span by start phase (n, median ms, > T): " + "  ".join(f"{p}:{n}/{m:.1f}/{b}" for p, (n, m, b) in phases.items()))


def fetch_history(port, count):
    # The capture is unbounded: frames from the first captured one on.
    history = []
    first = 0
    while len(history) < count:
        chunk = min(2000, count - len(history))
        reply = mcp(port, "get_frame_pacing_frames", json.dumps({"first_frame_id": first, "count": chunk}))
        frames = reply.get("frames", [])
        if not frames:
            break
        history += frames
        first = frames[-1]["frame_id"] + 1
    return history


def print_transitions(run, history):
    # Cadence changes with the GPU clock ratio of the frames around them:
    # an upshift preceded by a GPU pre-warm shows the clock rising at N.
    changes = []
    previous = None
    for index, frame in enumerate(history):
        n = frame.get("planned_vsyncs")
        if (previous is not None) and (n != previous):
            before = [round(f.get("gpu_clock_ratio", 0.0), 2) for f in history[max(0, index - 8):index]]
            after = history[index:index + 8]
            after_clock = [round(f.get("gpu_clock_ratio", 0.0), 2) for f in after]
            after_refr = [f.get("actual_vsyncs") for f in after]
            changes.append(f"  frame {frame['frame_id']}: N {previous}->{n} clock before {before} after {after_clock} refreshes after {after_refr}")
        previous = n
    print(f"run {run}: {len(changes)} cadence change(s) in the first {len(history)} frames")
    for line in changes[:12]:
        print(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True, help="editor executable (relative to the repo root or absolute)")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--settle", type=float, default=15.0)
    parser.add_argument("--windows", type=int, default=3)
    parser.add_argument("--port", type=int, default=3743)
    parser.add_argument("--env", action="append", default=[], help="KEY=VALUE for the editor environment")
    parser.add_argument("--graphics", action="append", default=[], help="key=json-value override of erhe_graphics.json")
    parser.add_argument("--pin-min-vsyncs", type=int, default=None)
    parser.add_argument("--history", type=int, default=0, help="also fetch the first N captured frames (startup history) and print the cadence transitions")
    parser.add_argument("--label", default="")
    parser.add_argument("--out", default=None, help="directory for the raw JSON of each run")
    args = parser.parse_args()

    if sys.platform == "darwin":
        # Keep the display and the system awake for the batch (released when
        # this script exits): a sleeping display leaves no presentation
        # feedback, and the lock screen that follows occludes the window.
        subprocess.Popen(["caffeinate", "-d", "-i", "-w", str(os.getpid())])

    editor = pathlib.Path(args.editor)
    if not editor.is_absolute():
        editor = REPO_ROOT / editor
    env = dict(os.environ)
    env["ERHE_AI_DRIVER"] = "1"
    for item in args.env:
        key, _, value = item.partition("=")
        env[key] = value

    original_config = GRAPHICS_CONFIG.read_text(encoding="utf-8") if args.graphics else None
    if args.graphics:
        config = json.loads(original_config)
        for item in args.graphics:
            key, _, value = item.partition("=")
            config[key] = json.loads(value)
        GRAPHICS_CONFIG.write_text(json.dumps(config, indent=4) + "\n", encoding="utf-8")
    out_dir = pathlib.Path(args.out) if args.out else None
    if out_dir:
        out_dir.mkdir(parents=True, exist_ok=True)
    try:
        for run in range(1, args.runs + 1):
            process = subprocess.Popen([str(editor)], cwd=str(REPO_ROOT), env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                if not wait_health(args.port, 120.0):
                    print(f"run {run}: MCP server did not come up")
                    continue
                time.sleep(args.settle)
                if args.pin_min_vsyncs is not None:
                    mcp(args.port, "set_frame_pacing_min_vsyncs", json.dumps({"min_vsyncs": args.pin_min_vsyncs}))
                    time.sleep(args.settle)
                status = mcp(args.port, "get_frame_pacing_status")
                frames = []
                for window in range(args.windows):
                    if window > 0:
                        time.sleep(1.2)
                    frames += mcp(args.port, "get_frame_pacing_frames").get("frames", [])
                undisplayed = "were not displayed" in LOG_PATH.read_text(encoding="utf-8", errors="replace")
                result = analyze(frames, status)
                print_run(run, args.label, result, undisplayed)
                history = fetch_history(args.port, args.history) if (args.history > 0) else []
                if history:
                    print_transitions(run, history)
                if out_dir:
                    (out_dir / f"run_{run}.json").write_text(json.dumps({"status": status, "frames": frames, "history": history}, indent=1) + "\n", encoding="utf-8")
                mcp(args.port, "request_exit")
            finally:
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    finally:
        if original_config is not None:
            GRAPHICS_CONFIG.write_text(original_config, encoding="utf-8")


if __name__ == "__main__":
    main()
