#!/usr/bin/env python3
"""Concurrency soak for the brush geometry preparation queue.

doc/plans/deferred_brush_geometry.md, Verification / "Concurrency": start the
editor with ERHE_DEBUG_VALIDATE_GEOMETRY=1 and, right after frame 12, request
every palette brush (tier 2, worker preparation) and immediately place ten
different brushes (tier 1, preparation on the calling thread) while the queue
is busy. The two tiers then race for the same brushes, which is exactly the
path the per-brush mutex and the slot state machine guard.

A run passes when the editor exits with code 0, its log holds no
`MESH CORRUPT` line (geometry validation) and no error line beyond the two
known startup ones, and every brush reaches `ready` or `failed`.

Usage:
    py -3 scripts/brush_geometry_queue_soak.py --runs 20
"""

import argparse
import os
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from erhe_mcp import McpClient, wait_for_server  # noqa: E402


# Error lines every startup emits today; they predate this queue and are not
# what the soak is looking for (see doc/plans/deferred_brush_geometry.md and
# the "startup-log-error" notes in the memory bank).
KNOWN_ERROR_SUBSTRINGS = (
    "property 'mass': value rejected by validate callback",
    "property 'lightmapped': object is sealed",
)

PLACEMENT_COUNT = 10


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def read_log_tail(log_path: str, offset: int) -> str:
    if not os.path.isfile(log_path):
        return ""
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        size = os.fstat(f.fileno()).st_size
        # The editor truncates the log at startup, so an offset past the end
        # means the file was rewritten and the whole of it is this run's.
        f.seek(offset if offset <= size else 0)
        return f.read()


def check_log(text: str, problems: list[str]) -> None:
    for line in text.splitlines():
        if "MESH CORRUPT" in line:
            problems.append("MESH CORRUPT: " + line.strip())
        elif "[E]" in line and not any(known in line for known in KNOWN_ERROR_SUBSTRINGS):
            problems.append("error line: " + line.strip())


def run_once(run_index: int, editor_path: str, repo_root: str, timeout_s: float) -> bool:
    port = find_free_port()
    log_path = os.path.join(repo_root, "logs", "log.txt")
    log_offset = os.path.getsize(log_path) if os.path.isfile(log_path) else 0

    environment = dict(os.environ)
    environment["ERHE_AI_DRIVER"] = "1"
    environment["ERHE_DEBUG_VALIDATE_GEOMETRY"] = "1"
    environment["ERHE_MCP_PORT"] = str(port)

    process = subprocess.Popen(
        [editor_path],
        cwd=repo_root,
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    problems: list[str] = []
    try:
        client = McpClient(port=port)
        wait_for_server(client, timeout_s)
        scene = client.call("list_scenes")["scenes"][0]["name"]

        states = client.call("get_brush_geometry_states", {"scene_name": scene})["states"]
        unprepared = [entry["name"] for entry in states if entry["state"] == "unprepared"]

        # Tier 2 first: the whole palette goes to the queue and the workers
        # start on it immediately.
        client.call("request_brush_geometry", {"scene_name": scene})

        # Tier 1 while the queue is busy: ten brushes the workers are also
        # holding requests for.
        placements = unprepared[:PLACEMENT_COUNT]
        for index, name in enumerate(placements):
            client.call(
                "place_brush",
                {
                    "scene_name": scene,
                    "brush_name": name,
                    "position": [3.0 * index, 5.0, 0.0],
                    "motion_mode": "none",
                },
            )

        deadline = time.monotonic() + timeout_s
        while True:
            counts = client.call("get_brush_geometry_states", {"scene_name": scene})["counts"]
            if (counts["unprepared"] == 0) and (counts["queued"] == 0) and (counts["preparing"] == 0):
                break
            if time.monotonic() >= deadline:
                problems.append(f"brushes did not finish preparing: {counts}")
                break
            time.sleep(0.2)

        if counts["failed"] != 0:
            problems.append(f"{counts['failed']} brush(es) failed to prepare")
        if len(placements) != PLACEMENT_COUNT:
            problems.append(f"only {len(placements)} unprepared brushes to place")

        client.call("request_exit")
    except Exception as error:  # noqa: BLE001 - any failure fails the run
        problems.append(f"{type(error).__name__}: {error}")

    try:
        exit_code = process.wait(timeout=timeout_s)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        problems.append("editor did not exit; killed")
        exit_code = None

    if exit_code not in (0, None):
        problems.append(f"editor exit code {exit_code}")

    check_log(read_log_tail(log_path, log_offset), problems)

    if problems:
        print(f"run {run_index}: FAIL")
        for problem in problems:
            print(f"    {problem}")
        return False
    print(f"run {run_index}: pass (exit 0, no MESH CORRUPT, no new error lines)")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runs", type=int, default=20, help="number of editor startups (default 20)")
    parser.add_argument(
        "--build-dir",
        default="build_vs2026_vulkan_headless",
        help="build tree holding the editor (default build_vs2026_vulkan_headless)",
    )
    parser.add_argument("--config", default="Debug", help="multi-config build type (default Debug)")
    parser.add_argument("--timeout", type=float, default=180.0, help="per-phase timeout in seconds (default 180)")
    arguments = parser.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    candidates = [
        os.path.join(repo_root, arguments.build_dir, "bin", arguments.config, "editor.exe"),
        os.path.join(repo_root, arguments.build_dir, "bin", "editor.exe"),
        os.path.join(repo_root, arguments.build_dir, "bin", arguments.config, "editor"),
        os.path.join(repo_root, arguments.build_dir, "bin", "editor"),
    ]
    editor_path = next((path for path in candidates if os.path.isfile(path)), "")
    if not editor_path:
        print(f"editor not found under {arguments.build_dir}; looked at:")
        for candidate in candidates:
            print(f"    {candidate}")
        return 1

    print(f"editor: {editor_path}")
    passed = 0
    for run_index in range(1, arguments.runs + 1):
        if run_once(run_index, editor_path, repo_root, arguments.timeout):
            passed += 1
    print(f"\n{passed}/{arguments.runs} runs passed")
    return 0 if passed == arguments.runs else 1


if __name__ == "__main__":
    sys.exit(main())
