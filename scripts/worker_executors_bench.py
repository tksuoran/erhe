#!/usr/bin/env python3
"""Kit thumbnail burst frame times (doc/plans/worker_executors.md W8, build step 1).

    python3 scripts/worker_executors_bench.py --editor <tree>/bin/editor
        [--cwd <dir>] [--out logs/worker_executors_bench.json] [--rest-frames 300] [--label today]

Launches the editor (ERHE_AI_DRIVER=1, ERHE_DEFAULT_LAYOUT=1, the wall clock:
ERHE_DETERMINISTIC_CLOCK=0) with the repo root, or --cwd (a directory with
its own config/, res/ and logs/), as working directory and
collects the main loop's tick durations through MCP get_frame_ticks, one call
per frame, so the series is gap-free. Phases:

1. startup: waits for get_async_status idle in three consecutive frames. The
   startup time is the end of the first tick from which every tick reads the
   scene load idle (the "first interactive frame" of W8.4), in seconds since
   the editor process started.
2. rest: --rest-frames ticks of the startup scene at rest.
3. burst: shows the Level Kit window (set_window_visibility) and requests
   every thumbnail (level_get_kit_thumbnails request_all), then collects ticks
   until no thumbnail is none or queued. The burst is the ticks from the one
   that served the request to the one that served the reply reporting all
   done; its duration is the sum of their durations.
4. after: --rest-frames ticks with the window open and every thumbnail made.

Reports median, 99th percentile (nearest rank) and longest tick per phase,
the burst duration, the thumbnail count and the executors of the run (MCP
get_executor_status, read once after startup) and the log's background apply
warnings and late mesh upload errors, prints one summary line and writes
everything to --out. Asks the editor to exit (request_exit) and stops
it by the pid it launched when it does not. Results: doc/reference/worker_executors_bench.md.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import signal
import statistics
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from erhe_mcp import McpClient  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK_DIR  = REPO_ROOT
LOG_PATH  = os.path.join(WORK_DIR, "logs", "log.txt")
WINDOW    = "Level Kit"


def read_log():
    try:
        with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return ""


def stats(durations):
    if not durations:
        return {"count": 0}
    ordered = sorted(durations)
    p99_index = max(0, math.ceil(0.99 * len(ordered)) - 1)
    return {
        "count":     len(ordered),
        "median_ms": statistics.median(ordered),
        "p99_ms":    ordered[p99_index],
        "max_ms":    ordered[-1],
        "mean_ms":   statistics.fmean(ordered),
    }


class Bench_editor:
    """The editor this script launched; stopped by its own pid."""

    def __init__(self, exe, timeout_s):
        self.exe = exe if os.path.isabs(exe) else os.path.join(REPO_ROOT, exe)
        if not os.path.isfile(self.exe):
            raise RuntimeError(f"no editor at {self.exe}")
        os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
        open(LOG_PATH, "w").close()
        env = dict(os.environ, ERHE_AI_DRIVER="1", ERHE_DEFAULT_LAYOUT="1", ERHE_DETERMINISTIC_CLOCK="0")
        flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        self.process = subprocess.Popen([self.exe], cwd=WORK_DIR, env=env, creationflags=flags,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.ticks = {}       # tick -> (end_s, duration_ms, scene_idle)
        self.latest_tick = 0
        self.gaps = 0
        pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+) \(pid (\d+)")
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"editor exited with {self.process.returncode} during startup; see logs/log.txt")
            ports = [int(m.group(1)) for m in pattern.finditer(read_log()) if int(m.group(2)) == self.process.pid]
            if ports:
                self.client = McpClient(ports[0])
                return
            # Before the MCP server answers the log is the only report: read it again once the process
            # has run a while longer, or at once when it exits.
            try:
                self.process.wait(timeout=0.1)
            except subprocess.TimeoutExpired:
                pass
        self.stop()
        raise RuntimeError("the editor's MCP server did not start")

    def call(self, tool, arguments=None):
        return self.client.call(tool, arguments or {})

    def collect(self):
        """One get_frame_ticks call (one frame): adds the ticks since the previous call."""
        reply = self.call("get_frame_ticks", {"after_tick": self.latest_tick, "max_count": 8192})
        if reply["oldest_tick"] > self.latest_tick + 1:
            self.gaps += 1
        for tick, end_s, duration_ms, scene_idle in reply["ticks"]:
            self.ticks[tick] = (end_s, duration_ms, scene_idle)
        self.latest_tick = max(self.latest_tick, reply["latest_tick"])
        return reply

    def wait_idle(self, timeout_s):
        """Returns once get_async_status reads idle in three consecutive frames."""
        deadline = time.monotonic() + timeout_s
        idle_count = 0
        while time.monotonic() < deadline:
            status = self.call("get_async_status")
            idle_count = (idle_count + 1) if status.get("idle", False) else 0
            self.collect()
            if idle_count >= 3:
                return
        raise RuntimeError("editor not idle")

    def collect_frames(self, count):
        first = self.latest_tick + 1
        while self.latest_tick < first + count - 1:
            self.collect()
        return first, self.latest_tick

    def durations(self, first, last):
        return [self.ticks[t][1] for t in range(first, last + 1) if t in self.ticks]

    def stop(self):
        if self.process.poll() is not None:
            return
        try:
            self.call("request_exit")
        except Exception:  # noqa: BLE001 - the editor may already be gone; the pid decides below
            pass
        try:
            self.process.wait(120)
        except subprocess.TimeoutExpired:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(60)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


def startup_time(editor, idle_tick):
    """End of the first tick from which every tick through idle_tick reads the scene load idle."""
    first_idle = idle_tick
    tick = idle_tick
    while (tick - 1) in editor.ticks and editor.ticks[tick - 1][2] == 1:
        tick -= 1
        first_idle = tick
    if 1 not in editor.ticks:
        return None, first_idle
    return editor.ticks[first_idle][0], first_idle


def thumbnails_done(reply):
    return (reply["none_count"] == 0) and (reply["queued_count"] == 0) and (reply["building_count"] == 0)


def run(args):
    editor = Bench_editor(args.editor, args.timeout)
    try:
        editor.wait_idle(args.timeout)
        idle_tick = editor.latest_tick
        startup_s, first_idle_tick = startup_time(editor, idle_tick)
        # The run's configuration: worker counts and priorities of both executors.
        executors = editor.call("get_executor_status")["executors"]

        rest_first, rest_last = editor.collect_frames(args.rest_frames)

        scenes = editor.call("list_scenes")["scenes"]
        if not scenes:
            raise RuntimeError("the editor has no startup scene")
        scene_name = scenes[0]["name"]
        burst_first = editor.collect()["latest_tick"] + 1
        editor.call("set_window_visibility", {"title": WINDOW, "visible": True, "focus": True})
        reply = editor.call("level_get_kit_thumbnails", {"request_all": True, "scene_name": scene_name})
        shape_count = reply["shape_count"]
        # The thumbnails reply lists every shape: read it again only every few frames while many shapes remain, so its
        # cost stays out of most burst frames.
        while not thumbnails_done(reply):
            for _ in range(max(1, (reply["queued_count"] + reply["building_count"]) // 4)):
                editor.collect()
            reply = editor.call("level_get_kit_thumbnails")
        burst_last = editor.collect()["latest_tick"]
        failed = reply["failed_count"]
        windows = editor.call("get_imgui_windows").get("windows", [])
        window_shown = any((w.get("display_name") == WINDOW) and not w.get("hidden", True) and not w.get("collapsed", False)
                           for w in windows)

        after_first, after_last = editor.collect_frames(args.rest_frames)

        burst_durations = editor.durations(burst_first, burst_last)
        result = {
            "label":          args.label,
            "editor":         os.path.relpath(editor.exe, REPO_ROOT),
            "cwd":            "repo root" if WORK_DIR == REPO_ROOT else os.path.basename(WORK_DIR),
            "scene":          scene_name,
            "startup_s":      startup_s,
            "startup_tick":   first_idle_tick,
            "first_tick_s":   editor.ticks[1][0] if 1 in editor.ticks else None,
            "rest":           dict(stats(editor.durations(rest_first, rest_last)), first_tick=rest_first, last_tick=rest_last),
            "burst":          dict(stats(burst_durations), first_tick=burst_first, last_tick=burst_last,
                                   duration_s=sum(burst_durations) / 1000.0),
            "after":          dict(stats(editor.durations(after_first, after_last)), first_tick=after_first, last_tick=after_last),
            "thumbnails":     shape_count,
            "failed":         failed,
            "window_shown":   window_shown,
            "gaps":           editor.gaps,
            "executors":      executors,
        }
    finally:
        editor.stop()
    # The log lines a burst is read against (doc/plans/worker_executors.md W4.1).
    lines = read_log().splitlines()
    result["background_apply_warnings"] = [line for line in lines if "Background apply took" in line]
    result["late_mesh_uploads"]         = [line for line in lines if "after the frame's mesh flush" in line]
    return result


def summary(result):
    def phase(name):
        p = result[name]
        return f"{name} median {p['median_ms']:.2f} p99 {p['p99_ms']:.2f} max {p['max_ms']:.2f} ms ({p['count']} ticks)"
    startup = "n/a" if result["startup_s"] is None else f"{result['startup_s']:.2f} s"
    return (f"{result['label']}: startup {startup}; {phase('rest')}; {phase('burst')}, "
            f"duration {result['burst']['duration_s']:.2f} s, {result['thumbnails']} thumbnails "
            f"({result['failed']} failed, window shown {result['window_shown']}); {phase('after')}; gaps {result['gaps']}; "
            f"background apply warnings {len(result['background_apply_warnings'])}, late mesh uploads {len(result['late_mesh_uploads'])}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True, help="editor executable (relative to the repo root or absolute)")
    parser.add_argument("--cwd", default=None, help="working directory of the editor: its config/, res/ and logs/ (default: the repo root)")
    parser.add_argument("--out", default=os.path.join("logs", "worker_executors_bench.json"), help="result JSON path")
    parser.add_argument("--rest-frames", type=int, default=300, help="ticks recorded at rest before and after the burst")
    parser.add_argument("--label", default="today", help="name of the run in the result")
    parser.add_argument("--timeout", type=float, default=600.0, help="seconds to wait for startup and idle")
    args = parser.parse_args()
    if args.cwd is not None:
        global WORK_DIR, LOG_PATH
        WORK_DIR = os.path.abspath(args.cwd)
        LOG_PATH = os.path.join(WORK_DIR, "logs", "log.txt")

    result = run(args)
    out = args.out if os.path.isabs(args.out) else os.path.join(REPO_ROOT, args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
        handle.write("\n")
    print(summary(result), flush=True)
    print(f"written: {out}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
