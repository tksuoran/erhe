#!/usr/bin/env python3
"""Record a Perfetto system trace around a command (an editor run) and
summarize it: per-CPU frequency, CPU utilization and the process's threads.

Uses the Perfetto prebuilts `tracebox` and `trace_processor` (the launcher
scripts from https://get.perfetto.dev/, which fetch the binaries on first
use), found through --perfetto-dir, ERHE_PERFETTO_DIR or ~/perfetto.

Without root the trace holds the sys_stats data source (CPU times and
cpufreq per CPU, meminfo) and process_stats (every process and thread with
its RSS); that is enough to see which cores ran hot and how busy the
machine was during a run. With --ftrace (needs root: /sys/kernel/tracing)
it adds sched_switch, cpu_frequency and cpu_idle events, which give the
per-thread scheduling timeline the Perfetto UI shows as CPU tracks.

Examples:
  python3 scripts/perfetto_system_trace.py -o run.pftrace -- \
      python3 scripts/render_thread_bench.py --build-dir build/Ninja_Vulkan_Release --scene draws --configs overlap
  python3 scripts/perfetto_system_trace.py --summarize run.pftrace
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

CONFIG_BASE = """buffers: {{ size_kb: 131072 fill_policy: RING_BUFFER }}
data_sources: {{ config {{ name: "linux.sys_stats" sys_stats_config {{
    stat_period_ms: 100 stat_counters: STAT_CPU_TIMES stat_counters: STAT_FORK_COUNT
    meminfo_period_ms: 1000 meminfo_counters: MEMINFO_MEM_AVAILABLE
    cpufreq_period_ms: 100 }} }} }}
data_sources: {{ config {{ name: "linux.process_stats" process_stats_config {{
    scan_all_processes_on_start: true proc_stats_poll_ms: 500 }} }} }}
{ftrace}
duration_ms: {duration_ms}
"""

CONFIG_FTRACE = """data_sources: { config { name: "linux.ftrace" ftrace_config {
    ftrace_events: "sched/sched_switch" ftrace_events: "sched/sched_waking"
    ftrace_events: "power/cpu_frequency" ftrace_events: "power/cpu_idle" } } }
"""

SUMMARY_SQL = """
select 'cpufreq' as what, cpu, min(value)/1000 as min_mhz, avg(value)/1000 as avg_mhz, max(value)/1000 as max_mhz
from counter c join cpu_counter_track t on c.track_id = t.id where t.name = 'cpufreq' group by cpu order by cpu;
"""

UTIL_SQL = """
with per_cpu as (
  select t.cpu as cpu, t.name as name, max(c.value) - min(c.value) as delta_ns
  from counter c join cpu_counter_track t on c.track_id = t.id
  where t.name like 'cpu.times.%' group by t.cpu, t.name
),
tot as (select cpu, sum(delta_ns) as total from per_cpu group by cpu),
idle as (select cpu, delta_ns as idle from per_cpu where name = 'cpu.times.idle_ns')
select tot.cpu, round(100.0 * (tot.total - idle.idle) / tot.total, 1) as busy_percent
from tot join idle using(cpu) order by tot.cpu;
"""

THREADS_SQL = """
select p.name as process, t.name as thread, t.tid from thread t join process p using(upid)
where p.name like '%editor%' order by t.tid;
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", nargs="*", help="command to run inside the trace (after --)")
    parser.add_argument("-o", "--output", default="system.pftrace", help="trace file to write")
    parser.add_argument("--duration", type=float, default=0.0, help="seconds to trace when no command is given")
    parser.add_argument("--ftrace", action="store_true", help="add the ftrace scheduling events (needs root)")
    parser.add_argument("--perfetto-dir", default=os.environ.get("ERHE_PERFETTO_DIR", str(Path.home() / "perfetto")))
    parser.add_argument("--summarize", default="", help="only summarize this existing trace")
    return parser.parse_args()


def run_query(trace_processor: Path, trace: str, sql: str) -> str:
    with tempfile.NamedTemporaryFile("w", suffix=".sql", delete=False) as handle:
        handle.write(sql)
        sql_path = handle.name
    try:
        out = subprocess.run([sys.executable, str(trace_processor), "-q", sql_path, trace], capture_output=True, text=True, timeout=600)
    finally:
        os.unlink(sql_path)
    lines = [line for line in out.stdout.splitlines() if line and not line.startswith("[") and "Loading trace" not in line and "%" not in line]
    return "\n".join(lines)


def summarize(trace_processor: Path, trace: str) -> None:
    print("\nCPU frequency during the trace (MHz):")
    print(run_query(trace_processor, trace, SUMMARY_SQL))
    print("\nCPU busy time (percent of the trace):")
    print(run_query(trace_processor, trace, UTIL_SQL))
    print("\neditor threads seen:")
    print(run_query(trace_processor, trace, THREADS_SQL))


def main() -> int:
    args = parse_args()
    perfetto_dir = Path(args.perfetto_dir)
    tracebox = perfetto_dir / "tracebox"
    trace_processor = perfetto_dir / "trace_processor"
    if args.summarize:
        summarize(trace_processor, args.summarize)
        return 0
    if not tracebox.is_file():
        print(f"error: {tracebox} not found (curl -LO https://get.perfetto.dev/tracebox)", file=sys.stderr)
        return 2
    duration_s = args.duration if args.duration > 0 else 3600.0
    config = CONFIG_BASE.format(ftrace=CONFIG_FTRACE if args.ftrace else "", duration_ms=int(duration_s * 1000))
    with tempfile.NamedTemporaryFile("w", suffix=".cfg", delete=False) as handle:
        handle.write(config)
        config_path = handle.name
    output = Path(args.output).resolve()
    tracer = subprocess.Popen([sys.executable, str(tracebox), "-c", config_path, "--txt", "-o", str(output)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    returncode = 0
    try:
        if args.command:
            returncode = subprocess.run(args.command).returncode
        else:
            tracer.wait()
    finally:
        if tracer.poll() is None:
            tracer.send_signal(2)  # SIGINT: perfetto stops and writes the trace
            try:
                tracer.wait(timeout=60)
            except subprocess.TimeoutExpired:
                tracer.kill()
        os.unlink(config_path)
    print(f"trace: {output} ({output.stat().st_size if output.exists() else 0} bytes)")
    if output.exists() and trace_processor.is_file():
        summarize(trace_processor, str(output))
    return returncode


if __name__ == "__main__":
    sys.exit(main())
