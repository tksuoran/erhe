#!/usr/bin/env python3
"""Summarize a Tracy capture of the editor: hot zones, GPU zones and the
render-thread phase overlap (doc/editor/render_thread.md section 3.4).

Runs tracy-csvexport (built from the Tracy checkout, capture/csvexport) on a
.tracy file and reports:
  - the top CPU zones by total time (count, mean, median, p90 per call, and
    the thread the zone ran on),
  - the top GPU zones the same way (--gpu),
  - the per-frame phase picture: the `frame` tick, the main-thread phases
    (frame_overlap_phase = M1, frame_prepare_phase = M2, frame_imgui_phase =
    M3, join = the main thread blocked on the render thread in the serial
    schedule, kick = the handoff, which with frame pipelining
    (ERHE_FRAME_PIPELINING=1) holds the wait for the previous frame's R) and
    the render thread's frame_render_phase (R), with how much of R the main
    thread was busy (overlapped; of that, how much in M2) versus blocked in
    join or kick, and how much of the tick the render thread was idle.

Example:
  python3 scripts/tracy_zone_summary.py <run_dir>/capture.tracy --top 40
"""

import argparse
import shutil
import statistics
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

SEP = "|"

PHASE_ZONES = ["frame", "frame_overlap_phase", "frame_prepare_phase", "frame_imgui_phase", "join", "kick", "frame_render_phase", "wait_for_draw_data"]
MAIN_WORK_ZONES = ["frame_overlap_phase", "frame_prepare_phase", "frame_imgui_phase"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture", help=".tracy file")
    parser.add_argument("--csvexport", default="", help="path to tracy-csvexport")
    parser.add_argument("--top", type=int, default=30, help="zones to list")
    parser.add_argument("--gpu", action="store_true", help="also list GPU zones")
    parser.add_argument("--skip-frames", type=int, default=180, help="frames dropped as warm-up in the phase analysis")
    parser.add_argument("--filter", default="", help="only list zones whose name contains this")
    parser.add_argument("--min-count", type=int, default=10, help="hide zones with fewer calls (the whole-run zones)")
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
        if len(parts) != len(header):
            continue
        rows.append(dict(zip(header, parts)))
    return rows


def short_file(path: str) -> str:
    marker = "/src/"
    index = path.find(marker)
    return path[index + len(marker):] if index >= 0 else path


def percentile(values: list, fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round(fraction * (len(ordered) - 1))))
    return ordered[index]


def zone_table(rows: list, top: int, name_filter: str, title: str, min_count: int) -> None:
    groups = defaultdict(list)
    threads = defaultdict(set)
    for row in rows:
        key = (row["name"], short_file(row.get("src_file", "")), row.get("src_line", ""))
        if name_filter and name_filter not in row["name"]:
            continue
        groups[key].append(int(row.get("exec_time_ns") or row.get("GPU execution time") or 0))
        threads[key].add(row.get("thread", "gpu"))
    total_all = sum(sum(v) for v in groups.values())
    ranked = sorted(((k, v) for k, v in groups.items() if len(v) >= min_count), key=lambda item: sum(item[1]), reverse=True)[:top]
    print(f"\n{title} (top {len(ranked)} by total time; us per call)")
    print(f"{'zone':48} {'file:line':52} {'thread':8} {'count':>7} {'mean':>9} {'median':>9} {'p90':>9} {'total%':>7}")
    for (name, file, line), values in ranked:
        total = sum(values)
        print(
            f"{name[:48]:48} {(file + ':' + line)[:52]:52} {','.join(sorted(threads[(name, file, line)]))[:8]:8} "
            f"{len(values):7d} {statistics.fmean(values) / 1000:9.1f} {statistics.median(values) / 1000:9.1f} "
            f"{percentile(values, 0.9) / 1000:9.1f} {100.0 * total / total_all if total_all else 0:7.2f}"
        )


def intervals_of(rows: list, name: str) -> list:
    result = []
    for row in rows:
        if row["name"] == name:
            start = int(row["ns_since_start"])
            result.append((start, start + int(row["exec_time_ns"])))
    result.sort()
    return result


def overlap(a: tuple, intervals: list) -> int:
    """Total length of `intervals` inside interval `a` (intervals sorted, non-overlapping)."""
    total = 0
    for start, end in intervals:
        if end <= a[0]:
            continue
        if start >= a[1]:
            break
        total += min(end, a[1]) - max(start, a[0])
    return total


def phase_report(rows: list, skip_frames: int) -> None:
    present = {name: intervals_of(rows, name) for name in PHASE_ZONES}
    frames = present["frame"][skip_frames:]
    if not frames:
        print("\nno `frame` zones; is this an editor capture?")
        return
    renders = present["frame_render_phase"]
    main_work = sorted(sum((present[name] for name in MAIN_WORK_ZONES), []))
    # The main thread blocks on R in `join` (serial schedule) or inside
    # `kick` (frame pipelining); in the serial schedule the kick is only the
    # handoff bookkeeping.
    joins = sorted(present["join"] + present["kick"])
    prepares = present["frame_prepare_phase"]
    threads = {}
    for row in rows:
        if row["name"] in PHASE_ZONES:
            threads.setdefault(row["name"], row.get("thread", "?"))
    dedicated = threads.get("frame_render_phase") != threads.get("frame_prepare_phase")

    print(f"\nphase picture over {len(frames)} frames (after skipping {skip_frames}); render thread {'dedicated' if dedicated else 'inline'}")
    per_name = {}
    for name in PHASE_ZONES:
        values = [end - start for start, end in present[name][skip_frames:]] if name == "frame" else [end - start for start, end in present[name]]
        if values:
            per_name[name] = values
            print(f"  {name:22} n={len(values):6d} mean {statistics.fmean(values) / 1000:9.1f} us  median {statistics.median(values) / 1000:9.1f}  p90 {percentile(values, 0.9) / 1000:9.1f}  max {max(values) / 1000:9.1f}")

    if not renders:
        return
    # For every R interval inside the analysed frames: how much main-thread
    # work overlapped it and how much of it the main thread spent in join.
    first_start = frames[0][0]
    renders_used = [r for r in renders if r[0] >= first_start]
    overlapped = [overlap(r, main_work) for r in renders_used]
    in_prepare = [overlap(r, prepares) for r in renders_used]
    joined = [overlap(r, joins) for r in renders_used]
    r_lengths = [end - start for start, end in renders_used]
    if r_lengths:
        print(f"  R while main busy  mean {statistics.fmean(overlapped) / 1000:9.1f} us ({100.0 * sum(overlapped) / sum(r_lengths):5.1f}% of R)")
        print(f"  R while main in M2 mean {statistics.fmean(in_prepare) / 1000:9.1f} us ({100.0 * sum(in_prepare) / sum(r_lengths):5.1f}% of R)")
        print(f"  R while main joins mean {statistics.fmean(joined) / 1000:9.1f} us ({100.0 * sum(joined) / sum(r_lengths):5.1f}% of R; join + kick)")
    # Render thread idle: tick time not covered by R.
    renders_sorted = sorted(renders_used)
    idle = [ (end - start) - overlap((start, end), renders_sorted) for start, end in frames ]
    print(f"  R idle per tick    mean {statistics.fmean(idle) / 1000:9.1f} us ({100.0 * sum(idle) / sum(end - start for start, end in frames):5.1f}% of tick)")


def main() -> int:
    args = parse_args()
    csvexport = find_csvexport(args)
    rows = export(csvexport, args.capture, ["-u"])
    if not rows:
        print("no zones exported", file=sys.stderr)
        return 1
    zone_table(rows, args.top, args.filter, "CPU zones", args.min_count)
    if args.gpu:
        gpu_rows = export(csvexport, args.capture, ["-g"])
        if gpu_rows:
            zone_table(gpu_rows, args.top, args.filter, "GPU zones", args.min_count)
        else:
            print("\nno GPU zones in capture")
    phase_report(rows, args.skip_frames)
    return 0


if __name__ == "__main__":
    sys.exit(main())
