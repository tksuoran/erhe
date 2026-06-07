#!/usr/bin/env python3
"""Automated idle-memory growth check for the erhe editor.

Launches the editor in an isolated working directory (so the user's
config/ is never touched), lets it idle, samples per-process memory
counters at a fixed interval, then fits a linear slope to decide whether
memory grows steadily ("leak") or stays flat.

Sampled per tick:
  - /proc/<pid>/status        : VmRSS, VmSwap, Threads
  - /proc/<pid>/smaps_rollup  : Rss, Pss, Anonymous, Private_Dirty
  - /proc/<pid>/fdinfo/<drm>  : drm-memory-vram / gtt / cpu (amdgpu, deduped
                                by drm-client-id) -> attributes GPU-driver
                                growth separately from host heap growth
  - /proc/<pid>/stat          : utime+stime (CPU activity proxy; confirms
                                the editor is actually rendering, not idle)

At start and end a full /proc/<pid>/smaps snapshot is aggregated by
mapping name, and the report prints the biggest per-mapping growers --
first-level attribution (heap vs anon mmap vs driver vs file mappings)
before reaching for allocation-site tools.

The editor is terminated with SIGTERM, which it handles as a clean quit
(SDL_EVENT_QUIT -> window close). Use --quit-after-frames for
frame-exact runs (the editor then exits by itself and SIGTERM is only a
fallback); frame-exact runs are what you want when comparing two runs of
different lengths with allocation tracing.

Examples:
  python3 scripts/idle_memory_check.py
  python3 scripts/idle_memory_check.py --duration 600 --label overnight
  python3 scripts/idle_memory_check.py --set 'erhe_graphics.json:vulkan.vulkan_validation_layers=false' --label no_validation
  python3 scripts/idle_memory_check.py --quit-after-frames 20000 --duration 0 --label frames20k

Exit codes: 0 = flat, 1 = steady growth detected, 2 = error.
"""

import argparse
import csv
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
READINESS_MARKER = "Init: editor ready, entering main loop"

# Metrics used for the leak verdict (host memory). GPU counters are
# reported but tracked separately so a driver-side growth is named as such.
VERDICT_METRICS = ("anonymous_kib", "rss_kib")
GPU_METRICS = ("drm_vram_kib", "drm_gtt_kib", "drm_cpu_kib")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build_ninja_linux_vulkan", help="build directory containing src/editor/editor")
    parser.add_argument("--duration", type=float, default=300.0, help="sampling window in seconds after settle (0 = until editor exits by itself)")
    parser.add_argument("--settle", type=float, default=20.0, help="seconds after readiness before samples count toward the fit")
    parser.add_argument("--interval", type=float, default=1.0, help="sample period in seconds")
    parser.add_argument("--readiness-timeout", type=float, default=120.0, help="max seconds to wait for the readiness log marker")
    parser.add_argument("--label", default="run", help="label used in the run directory name")
    parser.add_argument("--set", dest="overrides", action="append", default=[],
                        metavar="FILE.json:dotted.path=JSON_VALUE",
                        help="override a value in a copied config file, e.g. 'erhe_graphics.json:vulkan.vulkan_validation_layers=false'")
    parser.add_argument("--quit-after-frames", type=int, default=0,
                        help="set editor_settings.json quit_after_frames in the run config (frame-exact run length)")
    parser.add_argument("--threshold-kib-per-min", type=float, default=100.0, help="growth slope above this is reported as a leak")
    parser.add_argument("--min-total-kib", type=float, default=4096.0, help="total growth below this is never reported as a leak")
    parser.add_argument("--wrapper", default="", help="command prefix to wrap the editor invocation, e.g. 'heaptrack' (split on spaces)")
    parser.add_argument("--keep-going", action="store_true", help="do not abort sampling if the editor exits early; report what was collected")
    parser.add_argument("--runs-root", default="", help="directory to place run dirs in (default: <build-dir>/idle_mem)")
    return parser.parse_args()


def apply_override(config_dir: Path, spec: str) -> None:
    file_part, assign = spec.split(":", 1)
    dotted, raw_value = assign.split("=", 1)
    try:
        value = json.loads(raw_value)
    except json.JSONDecodeError:
        value = raw_value  # bare string, e.g. logger level names
    path = config_dir / "editor" / file_part
    data = json.loads(path.read_text())
    # Keys themselves may contain dots (logger names in logging.json), so
    # descend by the longest dotted prefix that exists as a key.
    node = data
    parts = dotted.split(".")
    i = 0
    while True:
        for j in range(len(parts), i, -1):
            candidate = ".".join(parts[i:j])
            if isinstance(node, dict) and candidate in node:
                if j == len(parts):
                    node[candidate] = value
                    path.write_text(json.dumps(data, indent=4) + "\n")
                    return
                node = node[candidate]
                i = j
                break
        else:
            raise KeyError(f"{file_part}: key path '{dotted}' not found (typo?)")


def make_run_dir(args: argparse.Namespace) -> Path:
    runs_root = Path(args.runs_root) if args.runs_root else (REPO_ROOT / args.build_dir / "idle_mem")
    runs_root.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = runs_root / f"{stamp}_{args.label}"
    run_dir.mkdir()

    # Isolated config copy; res/ shared read-only via symlink; spirv cache
    # shared across runs so only the first run pays shader compilation.
    shutil.copytree(REPO_ROOT / "config", run_dir / "config")
    (run_dir / "res").symlink_to(REPO_ROOT / "res")
    (run_dir / "logs").mkdir()
    shared_spirv = runs_root / "spirv_cache_shared"
    shared_spirv.mkdir(exist_ok=True)
    (run_dir / "spirv_cache").symlink_to(shared_spirv)

    overrides = list(args.overrides)
    if args.quit_after_frames > 0:
        overrides.append(f"editor_settings.json:quit_after_frames={args.quit_after_frames}")
    for spec in overrides:
        apply_override(run_dir / "config", spec)
    (run_dir / "overrides.txt").write_text("\n".join(overrides) + "\n")
    return run_dir


def read_proc_file(pid: int, name: str) -> str:
    try:
        return (Path(f"/proc/{pid}") / name).read_text()
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        return ""


def parse_kib_fields(text: str, wanted: dict) -> dict:
    out = {}
    for line in text.splitlines():
        key, _, rest = line.partition(":")
        if key in wanted:
            parts = rest.split()
            if parts and parts[-1] == "kB":
                out[wanted[key]] = int(parts[0])
            elif parts:
                out[wanted[key]] = int(parts[0])
    return out


def sample_drm_fdinfo(pid: int) -> dict:
    """Sum per-client DRM memory counters, deduplicated by drm-client-id."""
    fd_dir = Path(f"/proc/{pid}/fd")
    totals = {"drm_vram_kib": 0, "drm_gtt_kib": 0, "drm_cpu_kib": 0}
    seen_clients = set()
    found_any = False
    try:
        fds = list(fd_dir.iterdir())
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        return {}
    for fd in fds:
        try:
            target = os.readlink(fd)
        except OSError:
            continue
        if "/dev/dri/" not in target:
            continue
        info = read_proc_file(pid, f"fdinfo/{fd.name}")
        client_id = None
        fields = {}
        for line in info.splitlines():
            key, _, rest = line.partition(":")
            rest = rest.strip()
            if key == "drm-client-id":
                client_id = rest
            elif key in ("drm-memory-vram", "drm-memory-gtt", "drm-memory-cpu"):
                value, _, unit = rest.partition(" ")
                kib = int(value)
                if unit.strip() == "MiB":
                    kib *= 1024
                fields["drm_" + key.rsplit("-", 1)[1] + "_kib"] = kib
        if not fields:
            continue
        if client_id is not None:
            if client_id in seen_clients:
                continue
            seen_clients.add(client_id)
        found_any = True
        for key, value in fields.items():
            totals[key] += value
    return totals if found_any else {}


def take_sample(pid: int, t_now: float) -> dict:
    status = read_proc_file(pid, "status")
    rollup = read_proc_file(pid, "smaps_rollup")
    stat = read_proc_file(pid, "stat")
    if not status or not rollup:
        return {}
    sample = {"t": round(t_now, 2)}
    sample.update(parse_kib_fields(status, {"VmRSS": "rss_kib", "VmSwap": "swap_kib", "Threads": "threads"}))
    sample.update(parse_kib_fields(rollup, {
        "Pss": "pss_kib", "Anonymous": "anonymous_kib", "Private_Dirty": "private_dirty_kib", "Shared_Clean": "shared_clean_kib",
    }))
    if stat:
        fields = stat.rsplit(")", 1)[1].split()
        sample["cpu_ticks"] = int(fields[11]) + int(fields[12])  # utime + stime
    sample.update(sample_drm_fdinfo(pid))
    return sample


def snapshot_smaps_by_mapping(pid: int) -> dict:
    """Aggregate Rss/Private_Dirty/Anonymous per mapping name."""
    text = read_proc_file(pid, "smaps")
    result = {}
    current = None
    for line in text.splitlines():
        if line and (line[0].isdigit() or (line[0] in "abcdef")):
            head, _, rest = line.partition(" ")
            if "-" in head and len(head.split("-")) == 2:
                parts = line.split(None, 5)
                name = parts[5] if len(parts) > 5 else "[anon]"
                current = result.setdefault(name, {"rss_kib": 0, "private_dirty_kib": 0, "anonymous_kib": 0, "count": 0})
                current["count"] += 1
                continue
        if current is None:
            continue
        key, _, rest = line.partition(":")
        if key in ("Rss", "Private_Dirty", "Anonymous"):
            parts = rest.split()
            if parts and parts[-1] == "kB":
                mapped = {"Rss": "rss_kib", "Private_Dirty": "private_dirty_kib", "Anonymous": "anonymous_kib"}[key]
                current[mapped] += int(parts[0])
    return result


def linear_fit(points: list) -> dict:
    """Least squares + Theil-Sen slope. points = [(t, value)], t seconds."""
    n = len(points)
    if n < 8:
        return {"n": n, "lsq_slope_kib_per_min": 0.0, "theil_sen_kib_per_min": 0.0, "r2": 0.0, "total_delta_kib": 0.0}
    ts = [p[0] for p in points]
    vs = [p[1] for p in points]
    mean_t = sum(ts) / n
    mean_v = sum(vs) / n
    var_t = sum((t - mean_t) ** 2 for t in ts)
    cov = sum((t - mean_t) * (v - mean_v) for t, v in points)
    lsq = cov / var_t if var_t > 0 else 0.0
    ss_tot = sum((v - mean_v) ** 2 for v in vs)
    ss_res = sum((v - (mean_v + lsq * (t - mean_t))) ** 2 for t, v in points)
    r2 = (1.0 - ss_res / ss_tot) if ss_tot > 0 else 0.0
    # Theil-Sen over a decimated set (full pairwise is O(n^2)).
    step = max(1, n // 200)
    dec = points[::step]
    slopes = []
    for i in range(len(dec)):
        for j in range(i + 1, len(dec)):
            dt = dec[j][0] - dec[i][0]
            if dt > 0:
                slopes.append((dec[j][1] - dec[i][1]) / dt)
    slopes.sort()
    theil = slopes[len(slopes) // 2] if slopes else 0.0
    return {
        "n": n,
        "lsq_slope_kib_per_min": round(lsq * 60.0, 2),
        "theil_sen_kib_per_min": round(theil * 60.0, 2),
        "r2": round(r2, 4),
        "total_delta_kib": round(vs[-1] - vs[0], 1),
        "first_kib": vs[0],
        "last_kib": vs[-1],
    }


def main() -> int:
    args = parse_args()
    editor = REPO_ROOT / args.build_dir / "src" / "editor" / "editor"
    if not editor.is_file():
        print(f"error: editor binary not found: {editor}", file=sys.stderr)
        return 2

    run_dir = make_run_dir(args)
    print(f"run dir: {run_dir}")

    cmd = (args.wrapper.split() if args.wrapper else []) + [str(editor)]
    stdout_log = (run_dir / "editor_stdout.log").open("wb")
    proc = subprocess.Popen(cmd, cwd=run_dir, stdout=stdout_log, stderr=subprocess.STDOUT, start_new_session=True)
    print(f"editor pid: {proc.pid} (cmd: {' '.join(cmd)})")

    # With a wrapper (e.g. heaptrack) the editor is a child of the wrapper;
    # find the actual editor pid for /proc sampling.
    sample_pid = proc.pid
    log_file = run_dir / "logs" / "log.txt"

    def find_editor_pid() -> int:
        if not args.wrapper:
            return proc.pid
        try:
            out = subprocess.run(["pgrep", "-n", "-f", str(editor)], capture_output=True, text=True, timeout=5)
            if out.stdout.strip():
                return int(out.stdout.strip().splitlines()[-1])
        except Exception:
            pass
        return proc.pid

    # Wait for readiness marker.
    t0 = time.monotonic()
    ready_time = None
    while time.monotonic() - t0 < args.readiness_timeout:
        if proc.poll() is not None:
            print(f"error: editor exited during startup (code {proc.returncode}); see {run_dir}/editor_stdout.log and logs/log.txt", file=sys.stderr)
            return 2
        if log_file.is_file() and READINESS_MARKER in log_file.read_text(errors="replace"):
            ready_time = time.monotonic()
            break
        time.sleep(0.5)
    if ready_time is None:
        print("error: readiness marker not seen in time; killing", file=sys.stderr)
        os.killpg(proc.pid, signal.SIGKILL)
        return 2
    sample_pid = find_editor_pid()
    print(f"ready after {ready_time - t0:.1f}s; sampling pid {sample_pid} "
          f"(settle {args.settle:.0f}s, window {args.duration:.0f}s, interval {args.interval}s)")

    smaps_begin = None
    samples = []
    end_after = None if args.duration <= 0 else ready_time + args.settle + args.duration
    exited_by_itself = False
    while True:
        now = time.monotonic()
        if proc.poll() is not None:
            exited_by_itself = True
            break
        if end_after is not None and now >= end_after:
            break
        sample = take_sample(sample_pid, now - ready_time)
        if sample:
            sample["phase"] = "settle" if (now - ready_time) < args.settle else "measure"
            samples.append(sample)
            if smaps_begin is None and sample["phase"] == "measure":
                smaps_begin = snapshot_smaps_by_mapping(sample_pid)
        elif not args.keep_going:
            time.sleep(0.2)
            if proc.poll() is not None:
                exited_by_itself = True
                break
        time.sleep(max(0.0, args.interval - (time.monotonic() - now)))

    smaps_end = snapshot_smaps_by_mapping(sample_pid) if proc.poll() is None else {}

    # Shut down: SIGTERM -> clean quit (SDL_EVENT_QUIT); SIGKILL fallback.
    shutdown = "self-exit"
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=30)
            shutdown = "sigterm-clean"
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
            shutdown = "sigkill"
    stdout_log.close()
    print(f"editor stopped ({shutdown}, exit code {proc.returncode})")

    # Persist samples.
    measure = [s for s in samples if s["phase"] == "measure"]
    all_keys = ["t", "phase"] + sorted({k for s in samples for k in s} - {"t", "phase"})
    with (run_dir / "samples.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=all_keys)
        writer.writeheader()
        writer.writerows(samples)

    # Fit + verdict.
    fits = {}
    for metric in VERDICT_METRICS + GPU_METRICS + ("private_dirty_kib", "pss_kib"):
        points = [(s["t"], s[metric]) for s in measure if metric in s]
        if points:
            fits[metric] = linear_fit(points)

    def grows(metric: str) -> bool:
        fit = fits.get(metric)
        if not fit or fit["n"] < 8:
            return False
        return (fit["theil_sen_kib_per_min"] >= args.threshold_kib_per_min
                and fit["total_delta_kib"] >= args.min_total_kib)

    host_leak = any(grows(m) for m in VERDICT_METRICS)
    gpu_leak = any(grows(m) for m in GPU_METRICS)

    # CPU activity sanity: if the editor was not actually burning CPU it was
    # probably not rendering (e.g. fully occluded) and the run is suspect.
    cpu_note = ""
    cpu_points = [(s["t"], s["cpu_ticks"]) for s in measure if "cpu_ticks" in s]
    if len(cpu_points) >= 2:
        hz = os.sysconf("SC_CLK_TCK")
        dt = cpu_points[-1][0] - cpu_points[0][0]
        cpu_util = (cpu_points[-1][1] - cpu_points[0][1]) / hz / dt if dt > 0 else 0.0
        cpu_note = f"{cpu_util:.2f} cores avg"
        if cpu_util < 0.05:
            cpu_note += " (WARNING: editor nearly idle on CPU; was it rendering?)"

    # Per-mapping attribution diff.
    mapping_growth = []
    if smaps_begin and smaps_end:
        names = set(smaps_begin) | set(smaps_end)
        for name in names:
            before = smaps_begin.get(name, {}).get("rss_kib", 0)
            after = smaps_end.get(name, {}).get("rss_kib", 0)
            if after - before != 0:
                mapping_growth.append((after - before, name, before, after))
        mapping_growth.sort(reverse=True)

    summary = {
        "run_dir": str(run_dir),
        "cmd": cmd,
        "shutdown": shutdown,
        "exit_code": proc.returncode,
        "ready_seconds": round(ready_time - t0, 1),
        "samples_measure": len(measure),
        "duration_measured_s": round(measure[-1]["t"] - measure[0]["t"], 1) if len(measure) >= 2 else 0.0,
        "cpu": cpu_note,
        "threshold_kib_per_min": args.threshold_kib_per_min,
        "min_total_kib": args.min_total_kib,
        "fits": fits,
        "host_leak": host_leak,
        "gpu_leak": gpu_leak,
        "top_mapping_growth": [
            {"delta_kib": d, "name": n, "before_kib": b, "after_kib": a} for d, n, b, a in mapping_growth[:15]
        ],
    }
    (run_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    print()
    print(f"=== idle memory report ({len(measure)} samples over {summary['duration_measured_s']:.0f}s, cpu {cpu_note}) ===")
    for metric in VERDICT_METRICS + GPU_METRICS + ("private_dirty_kib",):
        fit = fits.get(metric)
        if not fit:
            continue
        flag = " <-- GROWING" if grows(metric) else ""
        print(f"  {metric:20s} {fit['first_kib']/1024.0:9.1f} -> {fit['last_kib']/1024.0:9.1f} MiB   "
              f"slope {fit['theil_sen_kib_per_min']:9.1f} KiB/min (lsq {fit['lsq_slope_kib_per_min']:9.1f}, r2 {fit['r2']:.3f}){flag}")
    if mapping_growth:
        print("  top mapping growth (RSS):")
        for delta, name, before, after in mapping_growth[:8]:
            print(f"    {delta:+10d} KiB  {name}  ({before} -> {after})")
    verdict = "LEAK (host)" if host_leak else ("LEAK (gpu driver)" if gpu_leak else "FLAT")
    print(f"  verdict: {verdict}")
    print(f"  details: {run_dir}/summary.json, samples.csv")
    return 1 if (host_leak or gpu_leak) else 0


if __name__ == "__main__":
    sys.exit(main())
