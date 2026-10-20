"""Summarize the frame pacing records of a running editor (or a saved dump).

    py -3 scripts/frame_pacing_summary.py [--port N] [--count N] [--save out.json]
    py -3 scripts/frame_pacing_summary.py --json frames.json

Fetches get_frame_pacing_frames through scripts/mcp_call.py and prints the
median and p95 of, per frame:

  loop             cpu_slot_begin delta (the main loop period)
  pacer_wait       pacer_wait_end - pacer_wait_begin (tier S sleep, tier W release gate)
  acquire          acquire_end - acquire_begin (swapchain image / drawable wait)
  fence_wait       fence_wait_ms
  present cadence  present_request_time delta
  achieved cadence achieved_present_time delta (only where feedback exists)

The records and their fields are described in doc/frame_pacing/inputs.md.
Run it on a settled editor without other MCP traffic during the window the
records cover (the default count is the recorder's 120 newest frames).
"""

import argparse
import json
import pathlib
import statistics
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent


def fetch_frames(port, count):
    command = [sys.executable, str(REPO_ROOT / "scripts" / "mcp_call.py"), "get_frame_pacing_frames"]
    if count is not None:
        command.append(json.dumps({"count": count}))
    if port is not None:
        command += ["--port", str(port)]
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        sys.exit(f"mcp_call.py failed: {completed.stderr.strip() or completed.stdout.strip()}")
    return json.loads(completed.stdout)


def deltas_ms(frames, field):
    values = [f[field] for f in frames]
    return [(b - a) * 1000.0 for a, b in zip(values, values[1:]) if (a > 0.0) and (b > 0.0)]


def spans_ms(frames, begin, end):
    return [(f[end] - f[begin]) * 1000.0 for f in frames if (f[end] > 0.0) and (f[begin] > 0.0)]


def summarize(name, values):
    if not values:
        print(f"{name:17s} (no samples)")
        return
    ordered = sorted(values)
    p95 = ordered[max(0, int(len(ordered) * 0.95) - 1)]
    print(f"{name:17s} median {statistics.median(ordered):7.2f} ms  p95 {p95:7.2f} ms  ({len(ordered)} samples)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=None, help="MCP port of the editor (default: mcp_call.py's)")
    parser.add_argument("--count", type=int, default=None, help="number of newest frames to fetch")
    parser.add_argument("--json", default=None, help="read a saved get_frame_pacing_frames result instead of fetching")
    parser.add_argument("--save", default=None, help="also write the fetched result to this file")
    args = parser.parse_args()

    if args.json is not None:
        with open(args.json, encoding="utf-8") as f:
            result = json.load(f)
    else:
        result = fetch_frames(args.port, args.count)
        if args.save is not None:
            with open(args.save, "w", encoding="utf-8", newline="") as f:
                f.write(json.dumps(result, indent=1) + "\n")
    frames = result.get("frames", [])
    if len(frames) < 2:
        sys.exit("fewer than two frame records")

    print(f"frames {frames[0]['frame_id']}..{frames[-1]['frame_id']} ({len(frames)})")
    summarize("loop",             deltas_ms(frames, "cpu_slot_begin"))
    summarize("pacer_wait",       spans_ms(frames, "pacer_wait_begin", "pacer_wait_end"))
    summarize("acquire",          spans_ms(frames, "acquire_begin", "acquire_end"))
    summarize("fence_wait",       [f["fence_wait_ms"] for f in frames])
    summarize("present cadence",  deltas_ms(frames, "present_request_time"))
    summarize("achieved cadence", deltas_ms(frames, "achieved_present_time"))


if __name__ == "__main__":
    main()
