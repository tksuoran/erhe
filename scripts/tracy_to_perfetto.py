#!/usr/bin/env python3
"""Convert a Tracy capture of the editor into a Perfetto-loadable trace.

Writes the Chrome JSON trace event format (`traceEvents`), which the Perfetto
UI (ui.perfetto.dev) and trace_processor open directly: one process, a
track per Tracy thread for the CPU zones, a "GPU" track for the GPU zones,
and the frame marks as instant events. Use it to view a capture next to a
system trace, or to query it with trace_processor SQL.

Thread names come from the editor: the Tracy thread index of the main
thread is the one that runs `frame_prepare_phase`, the render thread's the
one that runs `frame_render_phase` (`ERHE_PROFILE_THREAD_NAME`); the others
keep their index.

Example:
  python3 scripts/tracy_to_perfetto.py <run_dir>/capture.tracy -o capture.json
  python3 ~/perfetto/trace_processor -q query.sql capture.json
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

SEP = "|"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture", help=".tracy file")
    parser.add_argument("-o", "--output", default="", help="output .json (default: next to the capture)")
    parser.add_argument("--csvexport", default="", help="path to tracy-csvexport")
    parser.add_argument("--min-us", type=float, default=0.0, help="drop CPU zones shorter than this (keeps the file small)")
    return parser.parse_args()


def find_csvexport(args: argparse.Namespace) -> str:
    if args.csvexport:
        return args.csvexport
    found = shutil.which("tracy-csvexport")
    if found:
        return found
    home_build = Path.home() / "git" / "tracy" / "csvexport" / "build" / "tracy-csvexport"
    if home_build.is_file():
        return str(home_build)
    raise SystemExit("tracy-csvexport not found; pass --csvexport")


def export(csvexport: str, capture: str, extra: list) -> list:
    out = subprocess.run([csvexport, "-s", SEP, *extra, capture], capture_output=True, text=True, check=True)
    lines = out.stdout.splitlines()
    if not lines:
        return []
    header = lines[0].split(SEP)
    rows = []
    for line in lines[1:]:
        parts = line.split(SEP)
        if len(parts) == len(header):
            rows.append(dict(zip(header, parts)))
    return rows


def short_file(path: str) -> str:
    marker = "/src/"
    index = path.find(marker)
    return path[index + len(marker):] if index >= 0 else path


def main() -> int:
    args = parse_args()
    csvexport = find_csvexport(args)
    cpu_rows = export(csvexport, args.capture, ["-u"])
    gpu_rows = export(csvexport, args.capture, ["-g"])
    if not cpu_rows:
        print("no zones exported", file=sys.stderr)
        return 1

    thread_names = {}
    for row in cpu_rows:
        if row["name"] == "frame_prepare_phase":
            thread_names[row["thread"]] = "Main thread"
        elif row["name"] == "frame_render_phase":
            thread_names.setdefault(row["thread"], "Render thread")
    for row in cpu_rows:
        if row["name"] == "frame_render_phase" and thread_names.get(row["thread"]) == "Main thread":
            pass  # inline mode: the main thread renders too

    events = []
    pid = 1
    thread_ids = {}
    for row in cpu_rows:
        tid_key = row["thread"]
        if tid_key not in thread_ids:
            thread_ids[tid_key] = 100 + len(thread_ids)
            events.append({"ph": "M", "pid": pid, "tid": thread_ids[tid_key], "name": "thread_name", "args": {"name": thread_names.get(tid_key, f"Tracy thread {tid_key}")}})
        duration_us = int(row["exec_time_ns"]) / 1000.0
        if duration_us < args.min_us:
            continue
        events.append({
            "ph": "X", "pid": pid, "tid": thread_ids[tid_key], "name": row["name"],
            "cat": "cpu", "ts": int(row["ns_since_start"]) / 1000.0, "dur": duration_us,
            "args": {"src": f"{short_file(row['src_file'])}:{row['src_line']}"},
        })
        if row["name"] == "frame":
            events.append({"ph": "i", "pid": pid, "tid": thread_ids[tid_key], "name": "frame", "cat": "frame", "ts": int(row["ns_since_start"]) / 1000.0, "s": "p"})

    gpu_tid = 900
    if gpu_rows:
        events.append({"ph": "M", "pid": pid, "tid": gpu_tid, "name": "thread_name", "args": {"name": "GPU"}})
        for row in gpu_rows:
            events.append({
                "ph": "X", "pid": pid, "tid": gpu_tid, "name": row["name"], "cat": "gpu",
                "ts": int(row["Time from start of program"]) / 1000.0,
                "dur": int(row["GPU execution time"]) / 1000.0,
                "args": {"src": short_file(row.get("src_file", ""))},
            })
    events.append({"ph": "M", "pid": pid, "tid": 0, "name": "process_name", "args": {"name": "editor (Tracy)"}})

    output = Path(args.output) if args.output else Path(args.capture).with_suffix(".json")
    output.write_text(json.dumps({"traceEvents": events, "displayTimeUnit": "ms"}))
    print(f"{len(events)} events -> {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
