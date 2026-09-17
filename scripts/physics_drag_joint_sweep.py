#!/usr/bin/env python3
"""Regression sweep: a physics drag never pulls a jointed body's joint apart.

Drives a RUNNING editor over its MCP server (this script launches no editor
and closes none). For every case it rebuilds the Newton's cradle of
scripts/creations/creation_21_newtons_cradle.py (--reuse --scene-only
--no-save, which CLOSES the editor's open scenes first) so each drag starts
from a cradle at rest: an undamped cradle keeps the energy of every earlier
release, and a leftover swing lets balls collide in ways that belong to an
earlier case, not to that case's drag.

Each case drags one ball through one tool with release=false, holds the drag
(--hold-s), releases it and watches the row (--after-s):
  - drag_selection (the Transform tool's gizmo drag path): seven translations
    (slow / fast far pulls, out of the swing plane, up into slack, down,
    the end ball pushed into the row, a fast diagonal) and a 90 degree
    rotation;
  - physics_drag (the Physics tool's right-drag path), grabbed off center at
    ball center + (0.047, 0.017, 0.054): the same seven translations and the
    reported drag of Ball 5 to world (0.87, 0.36, -0.22) over 60 frames.
A sampler thread reads get_scene_nodes continuously from the drag start to the
end of the after-release window, composes the world positions of every
"Ball N Hinge" (under the ball) and "Ball N Pivot" (under the frame) from the
parent-local transforms, and records the worst hinge-to-pivot distance of any
ball; the table also shows how far the dragged ball got from its start
during the hold (a drag that did not move the ball proves nothing; a ball
pulled straight down or up along its thread legitimately stays put). A case
fails when that distance exceeds --threshold-mm during the drag
and hold, or after the release. Exit code 0 = every case passed, 1 = a case
exceeded the threshold, 2 = the sweep could not run.

The physics backend is not reported over MCP, so pass --box3d for a Box3D
editor (the flag picks the cradle's ball gap, see creation 21); --jolt is the
default. When logs/log.txt names a different backend, a warning is printed.

Launching a headless editor for it (repo root as working directory):
    build_vs2026_vulkan_headless/bin/Debug/editor.exe --commands config/editor/commands_empty.json
    (build_vs2026_vulkan_headless_box3d/... for Box3D, then pass --box3d)
with ERHE_AI_DRIVER=1 in the environment; wait until logs/log.txt contains
"completed frame 12", then run:
    py -3 scripts/physics_drag_joint_sweep.py [--box3d] [--port 3743]
"""

import argparse
import math
import os
import subprocess
import sys
import threading
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts", "creations"))
from erhe_mcp import McpClient  # noqa: E402
from common import Creation  # noqa: E402

CRADLE_SCRIPT = os.path.join(REPO_ROOT, "scripts", "creations", "creation_21_newtons_cradle.py")
BALL_COUNT = 5
GRAB_OFFSET = (0.047, 0.017, 0.054)

TRANSLATIONS = [
    ("far +X 2 m slow",      "Ball 3", {"translation": [2.0, 0.0, 0.0],  "frames": 60}),
    ("far +X 2 m fast",      "Ball 3", {"translation": [2.0, 0.0, 0.0],  "frames": 2}),
    ("out of plane +Z 1 m",  "Ball 3", {"translation": [0.0, 0.0, 1.0],  "frames": 30}),
    ("up +Y 1 m (slack)",    "Ball 3", {"translation": [0.0, 1.0, 0.0],  "frames": 30}),
    ("down -Y 0.5 m",        "Ball 3", {"translation": [0.0, -0.5, 0.0], "frames": 30}),
    ("end ball into row -X", "Ball 5", {"translation": [-0.6, 0.0, 0.0], "frames": 10}),
    ("diagonal far fast",    "Ball 5", {"translation": [1.5, 1.0, 1.5],  "frames": 3}),
]

CASES = (
    [("drag_selection", label, ball, drag) for label, ball, drag in TRANSLATIONS]
    + [("drag_selection", "rotate 90 about X", "Ball 3",
        {"rotation_axis": [1.0, 0.0, 0.0], "rotation_angle_deg": 90.0, "frames": 20})]
    + [("physics_drag", label, ball, drag) for label, ball, drag in TRANSLATIONS]
    + [("physics_drag", "reported drag Ball 5", "Ball 5",
        {"target": [0.87, 0.36, -0.22], "frames": 60})]
)


# ------------------------------------------------------------ world transforms

def quat_to_mat3(q):
    x, y, z, w = q
    return [
        [1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w),       2.0 * (x * z + y * w)],
        [2.0 * (x * y + z * w),       1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)],
        [2.0 * (x * z - y * w),       2.0 * (y * z + x * w),       1.0 - 2.0 * (x * x + y * y)],
    ]


def local_matrix(node):
    """3x4 affine [R*S | T] of a get_scene_nodes entry (parent from node)."""
    r = quat_to_mat3(node.get("rotation_xyzw", [0.0, 0.0, 0.0, 1.0]))
    s = node.get("scale", [1.0, 1.0, 1.0])
    t = node.get("position", [0.0, 0.0, 0.0])
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)]


def compose(a, b):
    """a * b of two 3x4 affine matrices."""
    out = []
    for i in range(3):
        row = [sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)]
        row.append(sum(a[i][k] * b[k][3] for k in range(3)) + a[i][3])
        out.append(row)
    return out


def world_positions(nodes, names):
    """World positions of the named nodes, composed up each parent chain.
    A prim without a transform (a Scope) contributes identity."""
    by_id = {node["id"]: node for node in nodes}
    by_name = {node["name"]: node for node in nodes}
    result = {}
    for name in names:
        node = by_name.get(name)
        if node is None:
            raise RuntimeError(f"node '{name}' not found in the scene")
        matrix = local_matrix(node) if "position" in node else None
        parent_id = node.get("parent_id")
        while parent_id is not None and parent_id in by_id:
            parent = by_id[parent_id]
            if "position" in parent:
                parent_matrix = local_matrix(parent)
                matrix = parent_matrix if matrix is None else compose(parent_matrix, matrix)
            parent_id = parent.get("parent_id")
        result[name] = [matrix[0][3], matrix[1][3], matrix[2][3]] if matrix is not None else [0.0, 0.0, 0.0]
    return result


def measure(nodes, ball):
    """(worst hinge-to-pivot distance, the ball it belongs to) and the world
    position of the dragged ball."""
    names = [ball]
    for index in range(1, BALL_COUNT + 1):
        names += [f"Ball {index} Hinge", f"Ball {index} Pivot"]
    positions = world_positions(nodes, names)
    worst = (0.0, "")
    for index in range(1, BALL_COUNT + 1):
        distance = math.dist(positions[f"Ball {index} Hinge"], positions[f"Ball {index} Pivot"])
        if distance > worst[0]:
            worst = (distance, f"Ball {index}")
    return worst, positions[ball]


# ------------------------------------------------------------------- sampling

class Sampler:
    """Reads get_scene_nodes in a loop on its own MCP connection and keeps the
    worst hinge separation per phase ('hold' until the release call returns,
    then 'after'), and how far the dragged ball got from where it started."""

    def __init__(self, port, scene, ball, ball_start):
        self.client = McpClient(port)
        self.scene = scene
        self.ball = ball
        self.ball_start = ball_start
        self.moved = 0.0
        self.phase = "hold"
        self.worst = {"hold": (0.0, ""), "after": (0.0, "")}
        self.samples = {"hold": 0, "after": 0}
        self.errors = 0
        self.first_error = ""
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join()

    def _run(self):
        while not self._stop.is_set():
            phase = self.phase
            try:
                nodes = self.client.call("get_scene_nodes", {"scene_name": self.scene}).get("nodes", [])
                sample, ball_position = measure(nodes, self.ball)
            except Exception as error:  # noqa: BLE001 - a busy server skips one sample
                self.errors += 1
                if not self.first_error:
                    self.first_error = str(error)
                time.sleep(0.02)
                continue
            self.samples[phase] += 1
            if sample[0] > self.worst[phase][0]:
                self.worst[phase] = sample
            if phase == "hold":
                self.moved = max(self.moved, math.dist(ball_position, self.ball_start))


# ---------------------------------------------------------------------- cases

def build_cradle(port, backend):
    command = [sys.executable, CRADLE_SCRIPT, "--reuse", "--scene-only", "--no-save",
               "--port", str(port), f"--{backend}"]
    completed = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(f"creation_21 failed ({completed.returncode}):\n{completed.stdout}\n{completed.stderr}")


def run_case(c, args, tool, label, ball, drag):
    build_cradle(args.port, args.backend)
    c.attach_scene()
    c.settle()
    nodes = c.nodes()
    baseline, ball_start = measure(nodes, ball)
    ball_node = next((node for node in nodes if node["name"] == ball and node["type"] == "Mesh"), None)
    if ball_node is None:
        raise RuntimeError(f"'{ball}' mesh not found")

    request = dict(drag)
    request["release"] = False
    if tool == "drag_selection":
        c.select([ball_node["id"]])
    else:
        center = world_positions(nodes, [ball])[ball]
        request["scene_name"] = c.scene
        request["node_name"] = ball
        request["grab_point"] = [center[i] + GRAB_OFFSET[i] for i in range(3)]

    sampler = Sampler(args.port, c.scene, ball, ball_start)
    sampler.start()
    started = time.perf_counter()
    try:
        c.mutate(tool, request)
        time.sleep(args.hold_s)
        c.mutate(tool, {"action": "release"})
        hold_seconds = time.perf_counter() - started
        sampler.phase = "after"
        if tool == "drag_selection":
            c.clear_selection()
        time.sleep(args.after_s)
        after_seconds = time.perf_counter() - started - hold_seconds
    finally:
        sampler.stop()
    return {
        "tool": tool, "label": label, "ball": ball,
        "baseline": baseline, "moved": sampler.moved,
        "hold": sampler.worst["hold"], "after": sampler.worst["after"],
        "hold_rate": sampler.samples["hold"] / max(hold_seconds, 1e-6),
        "after_rate": sampler.samples["after"] / max(after_seconds, 1e-6),
        "samples": sampler.samples["hold"] + sampler.samples["after"],
        "errors": sampler.errors, "first_error": sampler.first_error,
    }


def check_backend(backend):
    log_path = os.path.join(REPO_ROOT, "logs", "log.txt")
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as log:
            head = log.read(200000)
    except OSError:
        return
    logged = "box3d" if "box3d physics backend initialized" in head else "jolt"
    if logged != backend:
        print(f"warning: logs/log.txt names the {logged} backend but --{backend} is in effect; "
              f"pass --{logged} if that log is this editor's", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=int(os.environ.get("ERHE_MCP_PORT", "3743")))
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--jolt", dest="backend", action="store_const", const="jolt",
                         help="the editor runs the Jolt backend (default)")
    backend.add_argument("--box3d", dest="backend", action="store_const", const="box3d",
                         help="the editor runs the Box3D backend")
    parser.set_defaults(backend="jolt")
    parser.add_argument("--threshold-mm", type=float, default=1.0,
                        help="largest allowed hinge-to-pivot distance (default 1.0)")
    parser.add_argument("--hold-s", type=float, default=1.0, help="seconds a drag is held (default 1.0)")
    parser.add_argument("--after-s", type=float, default=2.5,
                        help="seconds watched after the release (default 2.5)")
    parser.add_argument("--only", default=None,
                        help="run only the cases whose tool or label contains this text")
    args = parser.parse_args()

    check_backend(args.backend)
    cases = [case for case in CASES
             if (args.only is None) or (args.only in case[0]) or (args.only in case[1])]
    if not cases:
        print(f"no case matches --only '{args.only}'")
        return 2

    try:
        c = Creation("physics drag joint sweep", port=args.port, reuse=True, keep_scenes=True,
                     manage_windows=False)
    except Exception as error:  # noqa: BLE001
        print(f"no editor MCP server on port {args.port}: {error}")
        return 2
    rows = []
    for tool, label, ball, drag in cases:
        try:
            row = run_case(c, args, tool, label, ball, drag)
        except Exception as error:  # noqa: BLE001 - report and stop the sweep
            print(f"{tool} '{label}': sweep failed: {error}", flush=True)
            return 2
        rows.append(row)
        print(f"{tool:14s} {label:22s} hold {1000.0 * row['hold'][0]:7.3f} mm ({row['hold'][1] or '-'})"
              f"  after {1000.0 * row['after'][0]:7.3f} mm ({row['after'][1] or '-'})"
              f"  ball moved {row['moved']:.3f} m  {row['samples']} samples", flush=True)

    threshold = args.threshold_mm / 1000.0
    print()
    print(f"backend {args.backend}, threshold {args.threshold_mm:.3f} mm, hold {args.hold_s} s, after release {args.after_s} s")
    print(f"{'tool':14s} {'case':22s} {'moved m':>7s} {'start mm':>8s} {'hold mm':>8s} {'ball':6s} {'after mm':>8s} {'ball':6s} {'Hz hold/after':>13s}  result")
    failed = 0
    for row in rows:
        passed = (row["hold"][0] <= threshold) and (row["after"][0] <= threshold)
        failed += 0 if passed else 1
        print(f"{row['tool']:14s} {row['label']:22s} {row['moved']:7.3f} {1000.0 * row['baseline'][0]:8.3f}"
              f" {1000.0 * row['hold'][0]:8.3f} {row['hold'][1] or '-':6s}"
              f" {1000.0 * row['after'][0]:8.3f} {row['after'][1] or '-':6s}"
              f" {row['hold_rate']:6.1f}/{row['after_rate']:<6.1f}  {'pass' if passed else 'FAIL'}"
              + (f"  ({row['errors']} sample errors: {row['first_error'][:80]})" if row["errors"] else ""))
    print()
    print(f"{len(rows) - failed} of {len(rows)} cases within {args.threshold_mm:.3f} mm")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
