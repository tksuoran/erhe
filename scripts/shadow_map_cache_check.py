#!/usr/bin/env python3
"""Image gate of the shadow map cache (doc/erhe/shadows.md "Shadow map cache").

Launches the windowed editor on its default scene (ERHE_AI_DRIVER=1, from the
repo root), adds a shadow-casting spot and point light, then runs a sequence
of edits: a static frame, a caster move, a spot move, a point light move, a
spot cone change, a move of the viewport camera (the directional fits
follow it) and a second caster move. After each edit, with the cache on, the
viewport is captured; the cache is then switched off (every pass drawn) and
captured again. The two captures must be identical (the bottom status bar
rows, which show frame times, are excluded). Each step also reports how many
pixels changed against the previous step, so a step that changes nothing is
visible as such, and which passes the cache kept.

Needs Pillow and numpy. Example:
  python3 scripts/shadow_map_cache_check.py --editor build_xcode_metal/bin/Release/editor
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bistro_frame_bench as bench  # noqa: E402  (mcp(), wait_for_ready(), scene_names())

STATUS_BAR_ROWS = 24


def pixel_diff(path_a, path_b, ignore_bottom=STATUS_BAR_ROWS):
    a = np.asarray(Image.open(path_a).convert("RGB"))
    b = np.asarray(Image.open(path_b).convert("RGB"))
    if a.shape != b.shape:
        return -1
    rows = a.shape[0] - ignore_bottom
    return int(np.any(a[:rows] != b[:rows], axis=2).sum())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True, help="windowed editor executable (repo-root relative or absolute)")
    parser.add_argument("--out", default="", help="directory for the captures (default: a temporary directory)")
    parser.add_argument("--settle-s", type=float, default=0.5, help="seconds to wait after each edit / cache switch")
    args = parser.parse_args()

    out_dir = args.out or tempfile.mkdtemp(prefix="shadow_map_cache_")
    os.makedirs(out_dir, exist_ok=True)
    log_path = os.path.join(bench.REPO_ROOT, "logs", "log.txt")
    if os.path.exists(log_path):
        os.remove(log_path)  # wait_for_ready() reads the MCP port from the new log
    editor = args.editor if os.path.isabs(args.editor) else os.path.join(bench.REPO_ROOT, args.editor)
    process = subprocess.Popen([editor], cwd=bench.REPO_ROOT, env=dict(os.environ, ERHE_AI_DRIVER="1"),
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    port, _ = bench.wait_for_ready(process, 600.0)
    if port is None:
        sys.exit("editor did not become ready; see logs/log.txt")
    time.sleep(2.0)

    def call(tool, **arguments):
        reply = bench.mcp(port, tool, arguments)
        if (not isinstance(reply, dict)) or reply.get("isError"):
            print(f"{tool}: {str(reply)[:300]}")
        return reply

    scene = bench.scene_names(port)[0]
    down = [-0.70710678, 0.0, 0.0, 0.70710678]
    call("create_light", scene_name=scene, type="spot", name="Spot T", position=[0.0, 2.0, 0.0], range=10.0,
         intensity=20.0, outer_spot_angle=0.9, inner_spot_angle=0.6)
    call("create_light", scene_name=scene, type="point", name="Point T", position=[0.5, 1.0, 1.2], range=8.0, intensity=10.0)
    call("set_node_transform", scene_name=scene, node_name="Spot T", space="world", translation=[0.0, 2.0, 0.0], rotation_xyzw=down)
    nodes = call("get_scene_nodes", scene_name=scene)["nodes"]
    camera_name = call("get_viewports")["viewports"][0]["camera"]
    camera = next(node for node in nodes if node.get("name") == camera_name)
    cp = camera["position"]
    identity = [0.0, 0.0, 0.0, 1.0]
    steps = [
        ("static",        lambda: None),
        ("move_caster",   lambda: call("set_node_transform", scene_name=scene, node_name="octahedron", space="world", translation=[0.3, 0.251, 0.484], rotation_xyzw=identity)),
        ("move_spot",     lambda: call("set_node_transform", scene_name=scene, node_name="Spot T", space="world", translation=[0.4, 2.0, -0.2], rotation_xyzw=down)),
        ("move_point",    lambda: call("set_node_transform", scene_name=scene, node_name="Point T", space="world", translation=[-0.4, 0.9, 1.0], rotation_xyzw=identity)),
        ("spot_angle",    lambda: call("edit_light", scene_name=scene, light_name="Spot T", outer_spot_angle=0.6, inner_spot_angle=0.4)),
        ("move_camera",   lambda: call("set_node_transform", scene_name=scene, node_id=camera["id"], space="world", translation=[cp[0] + 0.7, cp[1] + 0.2, cp[2] - 0.4], rotation_xyzw=camera["rotation_xyzw"])),
        ("move_caster_2", lambda: call("set_node_transform", scene_name=scene, node_name="tetrahedron", space="world", translation=[0.6, 0.084, 0.8], rotation_xyzw=identity)),
    ]

    failed = []
    previous = None
    for tag, edit in steps:
        call("set_shadow_map_cache_enabled", enabled=True)
        time.sleep(args.settle_s * 2.0)
        edit()
        time.sleep(args.settle_s)
        lights = call("get_scene_lights", scene_name=scene).get("lights", [])
        kept = sum(bin(int(light.get("shadow_cube_cached_faces") or 0)).count("1") + int(light.get("shadow_cached") or 0) for light in lights)
        with_cache = os.path.join(out_dir, f"{tag}_cache.png")
        call("capture_screenshot", path=with_cache)
        call("set_shadow_map_cache_enabled", enabled=False)
        time.sleep(args.settle_s)
        without_cache = os.path.join(out_dir, f"{tag}_no_cache.png")
        call("capture_screenshot", path=without_cache)
        mismatch = pixel_diff(with_cache, without_cache)
        changed = pixel_diff(previous, without_cache) if previous else None
        print(f"{tag:14s} cache vs no cache: {mismatch} differing pixels; changed against the previous step: {changed}; passes kept: {kept}")
        if mismatch != 0:
            failed.append(tag)
        previous = without_cache

    call("request_exit")
    try:
        process.wait(120)
    except subprocess.TimeoutExpired:
        process.kill()
    print(f"captures in {out_dir}")
    print("PASS" if not failed else f"FAIL: {', '.join(failed)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
