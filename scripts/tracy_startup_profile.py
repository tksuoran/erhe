#!/usr/bin/env python3
"""Capture the editor's startup with Tracy and report zone times.

Needs an editor built with -DERHE_PROFILE_LIBRARY=tracy -DERHE_TRACY_ON_DEMAND=OFF
(with on-demand ON, Tracy drops everything recorded before the capture tool
connects, which is the startup). For every run the script starts
tracy-capture first, launches the editor with ERHE_AI_DRIVER=1 from the repo
root, waits for "Main loop: completed frame 12" in logs/log.txt, asks the
editor to exit over MCP, and exports the capture with tracy-csvexport -u.

Tool locations come from --tracy-dir or the ERHE_TRACY_DIR environment
variable (the directory holding tracy-capture and tracy-csvexport).

Examples:
  py -3 scripts/tracy_startup_profile.py --runs 3
  py -3 scripts/tracy_startup_profile.py --runs 3 --zone editor::Scene_builder::make_brushes --zone editor::prewarm_all
  py -3 scripts/tracy_startup_profile.py --within editor::Scene_builder::make_brushes --zone erhe::geometry::Geometry::process
  py -3 scripts/tracy_startup_profile.py --timeline 40
"""

import argparse
import csv
import os
import re
import subprocess
import sys
import time
from collections import defaultdict

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
READY_LINE = "Main loop: completed frame 12"


def find_tool(tracy_dir, name):
    for candidate in (name, name + ".exe"):
        path = os.path.join(tracy_dir, candidate)
        if os.path.isfile(path):
            return path
    sys.exit(f"{name} not found in '{tracy_dir}' (use --tracy-dir or ERHE_TRACY_DIR)")


def read_log(log_path):
    try:
        with open(log_path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def capture(editor, capture_exe, out_path, timeout_s):
    log_path = os.path.join(REPO_ROOT, "logs", "log.txt")
    if os.path.exists(log_path):
        os.remove(log_path)
    if os.path.exists(out_path):
        os.remove(out_path)
    creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    capture_process = subprocess.Popen(
        [capture_exe, "-o", out_path, "-f"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    time.sleep(1.0)
    env = dict(os.environ, ERHE_AI_DRIVER="1")
    start = time.time()
    editor_process = subprocess.Popen(
        [editor], cwd=REPO_ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        creationflags=creation_flags
    )
    ready_s = None
    text = ""
    while ((time.time() - start) < timeout_s) and (editor_process.poll() is None):
        text = read_log(log_path)
        if READY_LINE in text:
            ready_s = time.time() - start
            break
        time.sleep(0.05)
    if ready_s is None:
        print(f"editor did not reach frame 12 (exit code {editor_process.poll()}); see logs/log.txt")
    time.sleep(2.0)
    port = re.search(r"MCP server: listening on 127\.0\.0\.1:(\d+)", read_log(log_path))
    if (editor_process.poll() is None) and port:
        subprocess.run(
            [sys.executable, os.path.join(REPO_ROOT, "scripts", "mcp_call.py"), "request_exit"],
            cwd=REPO_ROOT, env=dict(env, ERHE_MCP_PORT=port.group(1)), stdout=subprocess.DEVNULL
        )
    try:
        editor_process.wait(60)
    except subprocess.TimeoutExpired:
        editor_process.kill()
    try:
        capture_process.wait(120)
    except subprocess.TimeoutExpired:
        capture_process.kill()
    return ready_s


def load_events(csvexport_exe, tracy_path):
    result = subprocess.run([csvexport_exe, "-u", tracy_path], capture_output=True, text=True, errors="replace")
    rows = []
    for row in csv.DictReader(result.stdout.splitlines()):
        if row.get("ns_since_start", "").isdigit():
            rows.append((row["name"], int(row["ns_since_start"]), int(row["exec_time_ns"]), row["thread"]))
    return rows


def frame_12_end_ms(rows):
    ticks = sorted((r for r in rows if r[0] == "editor::Editor::tick"), key=lambda r: r[1])
    if len(ticks) < 12:
        return None
    return (ticks[11][1] + ticks[11][2]) / 1e6


def report_zones(rows, zones, within):
    begin, end = 0, 1 << 62
    if within:
        parent = next((r for r in rows if r[0] == within), None)
        if parent is None:
            print(f"  zone '{within}' not in capture")
            return
        begin, end = parent[1], parent[1] + parent[2]
        print(f"  within {within}: {parent[2] / 1e6:.0f} ms")
    stats = defaultdict(lambda: [0, 0, 0])
    for name, start, duration, _thread in rows:
        if (name in zones) and (begin <= start <= end):
            entry = stats[name]
            entry[0] += duration
            entry[1] += 1
            entry[2] = max(entry[2], duration)
    for name in zones:
        total, count, longest = stats[name]
        print(f"  {name}: total {total / 1e6:.1f} ms  n={count}  longest {longest / 1e6:.1f} ms")


def report_timeline(rows, threshold_ms):
    run = next((r for r in rows if r[0] == "editor::run_editor"), None)
    end_ms = frame_12_end_ms(rows)
    if (run is None) or (end_ms is None):
        return
    main = sorted(
        (r for r in rows if (r[3] == run[3]) and (r[1] < end_ms * 1e6)), key=lambda r: (r[1], -r[2])
    )
    stack = []
    for name, start, duration, _thread in main:
        while stack and (stack[-1] <= start):
            stack.pop()
        if duration >= (threshold_ms * 1e6):
            print(f"  {start / 1e6:8.0f} {duration / 1e6:8.0f} ms  {'  ' * len(stack)}{name}")
        stack.append(start + duration)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build_ninja_win_vulkan", help="build tree (relative to the repo root)")
    parser.add_argument("--editor", default=None, help="editor executable (default: <build-dir>/bin/editor[.exe])")
    parser.add_argument("--tracy-dir", default=os.environ.get("ERHE_TRACY_DIR", ""), help="directory of tracy-capture / tracy-csvexport")
    parser.add_argument("--out-dir", default=os.path.join(REPO_ROOT, "logs"), help="where the .tracy files go")
    parser.add_argument("--label", default="startup", help="file name prefix of the captures")
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=600.0, help="seconds to wait for frame 12")
    parser.add_argument("--zone", action="append", default=[], help="zone name to report (repeatable)")
    parser.add_argument("--within", default=None, help="only count --zone events that start inside the first zone of this name")
    parser.add_argument("--timeline", type=float, default=None, metavar="MS", help="print the main-thread zone tree up to frame 12, zones of at least MS")
    args = parser.parse_args()

    if not args.tracy_dir:
        sys.exit("give --tracy-dir or set ERHE_TRACY_DIR")
    capture_exe = find_tool(args.tracy_dir, "tracy-capture")
    csvexport_exe = find_tool(args.tracy_dir, "tracy-csvexport")
    editor = args.editor
    if editor is None:
        bin_dir = os.path.join(REPO_ROOT, args.build_dir, "bin")
        editor = next(
            (p for p in (os.path.join(bin_dir, "editor.exe"), os.path.join(bin_dir, "editor")) if os.path.isfile(p)),
            None
        )
        if editor is None:
            sys.exit(f"no editor executable in '{bin_dir}' (use --editor)")

    os.makedirs(args.out_dir, exist_ok=True)
    for run_index in range(1, args.runs + 1):
        tracy_path = os.path.join(args.out_dir, f"{args.label}_{run_index}.tracy")
        ready_s = capture(editor, capture_exe, tracy_path, args.timeout)
        rows = load_events(csvexport_exe, tracy_path)
        end_ms = frame_12_end_ms(rows)
        ready_text = f"{ready_s:.2f} s" if ready_s is not None else "n/a"
        end_text = f"{end_ms:.0f} ms" if end_ms is not None else "n/a"
        print(f"run {run_index}: ready (wall) {ready_text}, frame 12 done at {end_text} in the capture, {tracy_path}")
        if args.zone:
            report_zones(rows, args.zone, args.within)
        if args.timeline is not None:
            report_timeline(rows, args.timeline)


if __name__ == "__main__":
    main()
