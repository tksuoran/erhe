#!/usr/bin/env python3
"""Gate of the raster occlusion test (doc/plans/occlusion_culling.md).

Drives a running editor (ERHE_AI_DRIVER=1, the default startup scene) over
MCP: places a wall in front of the viewport camera, N boxes behind it, M
boxes in front of it and one small box that crosses the camera's near plane,
turns on the gpu culling classification and occlusion culling, and reads
get_occlusion_check. Passes when every box behind the wall is reported
occluded, no box in front (nor the near-plane box) is, and no entry that
crosses the near plane is left unmarked.

Usage: python3 scripts/occlusion_wall_check.py [--port N] [--scene "Default Scene"]
"""

import argparse
import json
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

EYE         = [0.0, 1.3, 8.0]
Z_NEAR      = 1.0
BEHIND      = [[x, 1.3, -6.0] for x in (-3.0, -1.8, -0.6, 0.6, 1.8, 3.0)]
FRONT       = [[x, 1.3,  3.0] for x in (-1.5, -0.5, 0.5, 1.5)]
NEAR_BOX    = [0.3, 1.05, EYE[2] - Z_NEAR]


def mcp(port, tool, args):
    result = subprocess.run(
        [sys.executable, os.path.join(REPO, "scripts", "mcp_call.py"), tool, json.dumps(args), "--port", str(port)],
        capture_output=True, text=True, cwd=REPO
    )
    if result.returncode != 0:
        raise RuntimeError(f"{tool} failed: {result.stdout} {result.stderr}")
    return json.loads(result.stdout) if result.stdout.strip().startswith("{") else result.stdout


def box(port, scene, name, position, size):
    # Static bodies: with physics enabled a dynamic box would fall and the
    # wall would be pushed out of the floor, moving what the gate measures.
    mcp(port, "create_shape", {"scene_name": scene, "shape": "box", "name": name, "size": size, "position": position, "motion_mode": "none"})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=3743)
    parser.add_argument("--scene", default="Default Scene")
    args = parser.parse_args()
    port, scene = args.port, args.scene

    viewport = mcp(port, "get_viewports", {})["viewports"][0]
    camera = viewport["camera"]
    # The camera looks down -Z with the identity rotation.
    mcp(port, "set_node_transform", {"scene_name": scene, "node_name": camera, "translation": EYE, "rotation_xyzw": [0, 0, 0, 1], "space": "world"})
    mcp(port, "edit_camera", {"scene_name": scene, "camera_name": camera, "z_near": Z_NEAR})
    box(port, scene, "occlusion_wall", [0.0, 3.0, -2.0], [30.0, 12.0, 0.4])
    for i, p in enumerate(BEHIND):
        box(port, scene, f"occlusion_behind_{i}", p, [1.0, 1.0, 1.0])
    for i, p in enumerate(FRONT):
        box(port, scene, f"occlusion_front_{i}", p, [0.4, 0.4, 0.4])
    box(port, scene, "occlusion_near", NEAR_BOX, [0.2, 0.2, 0.2])
    print(mcp(port, "set_culling_classification", {"mode": "gpu"}))
    print(mcp(port, "set_occlusion_culling", {"enabled": True}))
    time.sleep(2.0)

    check = mcp(port, "get_occlusion_check", {"scene_name": scene})
    print(json.dumps(check, indent=2))
    view = next(v for v in check["views"] if v["scene_view"] == viewport.get("scene_view", v["scene_view"]))
    occluded = {e["name"] for e in view["first_occluded"]}
    behind   = {f"occlusion_behind_{i}" for i in range(len(BEHIND))}
    visible  = {f"occlusion_front_{i}" for i in range(len(FRONT))} | {"occlusion_near", "occlusion_wall"}
    failures = []
    if view["occluded"] > 16:
        failures.append(f"more occluded entries ({view['occluded']}) than reported; widen the report")
    if not behind <= occluded:
        failures.append(f"not occluded: {sorted(behind - occluded)}")
    if occluded & visible:
        failures.append(f"wrongly occluded: {sorted(occluded & visible)}")
    if view["near_plane"] < 1:
        failures.append("no entry crosses the near plane")
    if view["near_plane_occluded"] != 0:
        failures.append(f"near plane entries not marked: {view['near_plane_occluded']}")
    print(f"occluded {view['occluded']} tested {view['tested']} visible {view['visible']} untested {view['untested']} near_plane {view['near_plane']} near_plane_occluded {view['near_plane_occluded']}")
    print("PASS" if not failures else "FAIL: " + "; ".join(failures))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
