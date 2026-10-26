#!/usr/bin/env python3
"""Gate of the raster occlusion test in the headset's multiview pass
(doc/plans/occlusion_culling.md "Headset"), on a Quest over adb.

The headset counterpart of scripts/occlusion_wall_check.py. Two benchmark
runs of the installed APK (scripts/quest_bench.py: off-head, head pose fixed
by the benchmark file's camera path, MCP forwarded), each starting with
--no-scene; during the warmup the boxes are created over MCP in the empty
scene and the gpu culling classification and occlusion culling are turned
on; in the measured window get_occlusion_check reads the headset view.

- wall: the head at EYE looking down -Z; a 30 x 12 m wall, six boxes behind
  it, four in front of it, one small box crossing the eyes' near region.
  Passes when every box behind the wall is occluded, no box in front (nor
  the near box, nor the wall) is, no entry reaching the occlusion near plane
  is left unmarked, and the classification culls exactly the occluded
  entries (static scene and pose).
- inside: the head inside a 6 m box with a box beyond its far wall: neither
  is occluded (the box reaches the near plane: visible untested).

Usage: py -3 scripts/quest_occlusion_wall_check.py [--scene "Scene 1"]
"""

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import quest_bench as qb

EYE      = [0.0, 1.3, 8.0]
BEHIND   = [[x, 1.3, -6.0] for x in (-3.0, -1.8, -0.6, 0.6, 1.8, 3.0)]
FRONT    = [[x, 1.3,  3.0] for x in (-1.5, -0.5, 0.5, 1.5)]
# Crosses the eyes' near region (the eye cameras' near plane is 0.03 m).
NEAR_BOX = [0.05, 1.25, EYE[2] - 0.1]
ROOM     = [0.0, 1.3, 40.0]
BEYOND   = [0.0, 1.3, 34.0]
BENCH_DIR = qb.REPO_ROOT / "logs" / "quest_occlusion"


def write_bench(name: str, position: list) -> str:
    BENCH_DIR.mkdir(parents=True, exist_ok=True)
    bench = {
        "name": name,
        # Long enough for the MCP calls of the warmup (the settle that
        # starts the measured window waits only for work already queued).
        "warmup_frames": 1200,
        "measure_frames": 600,
        "frame_dt": 0.013888889,
        "settle_timeout_frames": 3600,
        "passthrough": False,
        "camera_path": {"type": "keyframes", "frames": [{"frame": 0, "position": position, "yaw_pitch_roll_deg": [0.0, 0.0, 0.0]}]}
    }
    path = BENCH_DIR / f"benchmark_{name}.json"
    path.write_text(json.dumps(bench, indent=4), encoding="utf-8")
    return str(path.relative_to(qb.REPO_ROOT)).replace("\\", "/")


def box_call(scene: str, name: str, position: list, size: list) -> str:
    # Static bodies (as occlusion_wall_check.py): nothing moves.
    return "create_shape=" + json.dumps({"scene_name": scene, "shape": "box", "name": name, "size": size, "position": position, "motion_mode": "none"})


def run(name: str, position: list, boxes: list, scene: str) -> dict:
    bench_path = write_bench(name, position)
    args = argparse.Namespace(
        bench=bench_path, scene="", worn=False, gpu_metrics=False, gpu_metric_ids="", repeat=1, push=False,
        timeout=300.0, bench_to_device=True, editor_args="--no-scene",
        mcp=["list_scenes"] + boxes + [
            'set_culling_classification={"mode": "gpu"}',
            'set_occlusion_culling={"enabled": true}'
        ],
        load_scene="",
        mcp_measure=["get_occlusion_check=" + json.dumps({"scene_name": scene})],
        mcp_port=3743
    )
    args.device_bench = qb.push_bench_to_device(bench_path)
    bench = json.loads((qb.REPO_ROOT / bench_path).read_text(encoding="utf-8"))
    result = qb.run_once(args, bench, 0, time.strftime("%Y%m%d_%H%M%S") + "_occlusion")
    scenes = result["host"]["mcp_warmup"][0]["reply"]
    print(f"{name}: scenes {json.dumps(scenes)[:200]}")
    check = result["host"]["mcp_measure"][0]["reply"]
    if not isinstance(check, dict):
        raise RuntimeError(f"{name}: get_occlusion_check failed: {check}")
    views = [view for view in check.get("views", []) if view.get("scene_view") == "Headset"]
    if len(views) != 1:
        raise RuntimeError(f"{name}: expected one Headset occlusion view, got {[view.get('scene_view') for view in check.get('views', [])]}")
    return views[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scene", default="Scene 1", help="the --no-scene start's empty scene")
    args = parser.parse_args()
    scene = args.scene
    failures = []

    boxes = [box_call(scene, "occlusion_wall", [0.0, 3.0, -2.0], [30.0, 12.0, 0.4])]
    boxes += [box_call(scene, f"occlusion_behind_{i}", p, [1.0, 1.0, 1.0]) for i, p in enumerate(BEHIND)]
    boxes += [box_call(scene, f"occlusion_front_{i}", p, [0.4, 0.4, 0.4]) for i, p in enumerate(FRONT)]
    boxes += [box_call(scene, "occlusion_near", NEAR_BOX, [0.1, 0.1, 0.1])]
    view = run("occlusion_wall", EYE, boxes, scene)
    occluded = {entry["name"] for entry in view["first_occluded"]}
    behind   = {f"occlusion_behind_{i}" for i in range(len(BEHIND))}
    visible  = {f"occlusion_front_{i}" for i in range(len(FRONT))} | {"occlusion_near", "occlusion_wall"}
    if view["occluded"] > 16:
        failures.append(f"more occluded entries ({view['occluded']}) than reported")
    if not behind <= occluded:
        failures.append(f"not occluded: {sorted(behind - occluded)}")
    if occluded & visible:
        failures.append(f"wrongly occluded: {sorted(occluded & visible)}")
    if view.get("culled", 0) != view["occluded"]:
        failures.append(f"culled {view.get('culled')} != occluded {view['occluded']}")
    if view["near_plane"] < 1:
        failures.append("no entry reaches the occlusion near plane")
    if view["near_plane_occluded"] != 0:
        failures.append(f"near plane entries not marked: {view['near_plane_occluded']}")
    print(f"wall: culled {view.get('culled')} occluded {view['occluded']} tested {view['tested']} visible {view['visible']} untested {view['untested']} near_plane {view['near_plane']} near_plane_occluded {view['near_plane_occluded']} occluded names {sorted(occluded)}")

    boxes = [box_call(scene, "occlusion_room", ROOM, [6.0, 6.0, 6.0]), box_call(scene, "occlusion_beyond", BEYOND, [1.0, 1.0, 1.0])]
    view = run("occlusion_inside", ROOM, boxes, scene)
    occluded = {entry["name"] for entry in view["first_occluded"]}
    for name in ("occlusion_room", "occlusion_beyond"):
        if name in occluded:
            failures.append(f"inside case: {name} occluded")
    if view["near_plane"] < 1:
        failures.append("inside case: the room does not reach the near plane")
    if view["near_plane_occluded"] != 0:
        failures.append(f"inside case: near plane entries not marked: {view['near_plane_occluded']}")
    print(f"inside: culled {view.get('culled')} occluded {view['occluded']} tested {view['tested']} untested {view['untested']} near_plane {view['near_plane']} near_plane_occluded {view['near_plane_occluded']} occluded names {sorted(occluded)}")
    print("PASS" if not failures else "FAIL: " + "; ".join(failures))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
