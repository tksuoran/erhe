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

A second sequence checks the per-light caster test: two shadowed point
lights A and B whose ranges do not overlap, each with a caster beside it.
A drag beside A, a caster added and removed beside B, a caster moved from
B's reach to A's and a caster removed beside A must redraw only the faces of
the lights whose reach the caster's old or new bounds are in
(get_scene_lights shadow_cube_face_draw_counts): every face of the other
light is kept. Selecting the caster beside A (a flag the caster filter does
not read) must keep every face of both. A skinned figure imported beside B,
kept static, keeps every face of both, and a joint rotation and a vertex
drag on it redraw only B's faces. Last, the shadow fit cache: a static
frame reuses every shadow-mapped light's fit results (get_scene_lights
shadow_fit_reused / shadow_fit_serial), moving one light refits that light
only, and captures with the fit cache off are identical. The captures with the cache on and off must again be
identical.

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
# A skinned (and animated, not playing) glTF: the skinned caster steps.
SKINNED_GLB = "res/editor/assets/RiggedFigure/RiggedFigure.glb"


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

    def compare(tag):
        """Captures with the cache on, then off; returns (mismatch, without_cache path)."""
        with_cache = os.path.join(out_dir, f"{tag}_cache.png")
        call("capture_screenshot", path=with_cache)
        call("set_shadow_map_cache_enabled", enabled=False)
        time.sleep(args.settle_s)
        without_cache = os.path.join(out_dir, f"{tag}_no_cache.png")
        call("capture_screenshot", path=without_cache)
        return pixel_diff(with_cache, without_cache), without_cache

    for tag, edit in steps:
        call("set_shadow_map_cache_enabled", enabled=True)
        time.sleep(args.settle_s * 2.0)
        edit()
        time.sleep(args.settle_s)
        lights = call("get_scene_lights", scene_name=scene).get("lights", [])
        kept = sum(bin(int(light.get("shadow_cube_cached_faces") or 0)).count("1") + int(light.get("shadow_cached") or 0) for light in lights)
        mismatch, without_cache = compare(tag)
        changed = pixel_diff(previous, without_cache) if previous else None
        print(f"{tag:14s} cache vs no cache: {mismatch} differing pixels; changed against the previous step: {changed}; passes kept: {kept}")
        if mismatch != 0:
            failed.append(tag)
        previous = without_cache

    # Per-light caster test. A and B: point lights 3 m apart with range 1.2,
    # each with a box caster beside it (below and to the side: the faces
    # the box reaches are named in the reply).
    light_a = [-1.5, 0.9, 0.0]
    light_b = [1.5, 0.9, 0.0]
    for name, position in (("Point A", light_a), ("Point B", light_b)):
        call("create_light", scene_name=scene, type="point", name=name, position=position, range=1.2, intensity=3.0)

    def beside(light, dx, dy, dz):
        return [light[0] + dx, light[1] + dy, light[2] + dz]

    for name, position in (("Box A", beside(light_a, 0.3, -0.6, 0.1)), ("Box B", beside(light_b, -0.3, -0.6, 0.1))):
        call("create_shape", scene_name=scene, shape="box", name=name, size=[0.2, 0.2, 0.2], position=position, motion_mode="none")
    time.sleep(args.settle_s * 4.0)

    def face_counts():
        lights = call("get_scene_lights", scene_name=scene).get("lights", [])
        counts = {}
        for light in lights:
            if light.get("name") in ("Point A", "Point B"):
                counts[light["name"]] = (light.get("shadow_cube_face_draw_counts"), light.get("shadow_mapped"))
        return counts

    def drag(node_name, start, end, frames=6):
        for i in range(1, frames + 1):
            t = i / frames
            position = [start[k] + ((end[k] - start[k]) * t) for k in range(3)]
            call("set_node_transform", scene_name=scene, node_name=node_name, space="world", translation=position, rotation_xyzw=identity)

    def wait_until_idle(timeout_s=60.0):
        start = time.time()
        time.sleep(0.5)
        while (time.time() - start) < timeout_s:
            status = call("get_async_status")
            if isinstance(status, dict) and (status.get("asset_loads", 0) + status.get("queued_operations", 0) + status.get("pending_scene_commits", 0)) == 0:
                return
            time.sleep(0.25)
        print("async work did not settle")

    def import_skinned_beside_b():
        call("import_gltf", scene_name=scene, path=SKINNED_GLB)
        wait_until_idle()
        call("set_node_transform", scene_name=scene, node_name="RiggedFigure.glb", space="world",
             translation=beside(light_b, 0.5, -0.9, 0.0), rotation_xyzw=identity, scale=[0.5, 0.5, 0.5])

    def drag_skinned_vertices():
        # A vertex drag on the skinned mesh: the dragged vertices' joint
        # boxes grow with them during the drag, the commit rebuilds the
        # primitive.
        call("select_mesh_components", scene_name=scene, node_name="Proxy", mode="vertex", vertices=[0, 1, 2, 3])
        for i in range(1, 6):
            call("transform_selection", space="global", translation=[0.0, 0.0, 0.02 * i], end_edit=(i == 5))
        wait_until_idle()
        call("set_mesh_component_mode", mode="object")

    box_a_start = beside(light_a, 0.3, -0.6, 0.1)
    box_b_start = beside(light_b, -0.3, -0.6, 0.1)
    # (tag, edit, the lights whose faces may redraw, the lights whose faces must all be kept)
    # The removals take the last caster of the list first (Box C), then Box A
    # after Box B moved beside A: removing an entry moves the list's last
    # entry into its place (a swap-remove), which redraws the passes the
    # moved entry reaches (its draw order changed), so Box B is beside A when
    # Box A goes.
    light_steps = [
        ("drag_beside_a",   lambda: drag("Box A", box_a_start, beside(light_a, 0.3, -0.6, -0.2)),                     ["Point A"], ["Point B"]),
        ("select_beside_a", lambda: call("select_items", scene_name=scene, paths=["Box A"]),                          [], ["Point A", "Point B"]),
        ("add_beside_b",    lambda: call("create_shape", scene_name=scene, shape="box", name="Box C", size=[0.2, 0.2, 0.2],
                                         position=beside(light_b, 0.2, 0.6, -0.2), motion_mode="none"),                 ["Point B"], ["Point A"]),
        ("remove_beside_b", lambda: call("delete_nodes", scene_name=scene, names=["Box C"]),                           ["Point B"], ["Point A"]),
        ("move_b_to_a",     lambda: call("set_node_transform", scene_name=scene, node_name="Box B", space="world",
                                         translation=beside(light_a, -0.3, -0.6, 0.1), rotation_xyzw=identity),        ["Point A", "Point B"], []),
        ("remove_beside_a", lambda: call("delete_nodes", scene_name=scene, names=["Box A"]),                           ["Point A"], ["Point B"]),
        # A skinned caster beside B: its entries are never culled, so its
        # posed bounds decide the faces it reaches. Imported (the frames
        # before its geometry carries per-joint bounds name no bounds, so
        # every pass may redraw), left static (every face of both kept),
        # then posed through a joint.
        ("skinned_add",     import_skinned_beside_b,                                                                   ["Point B"], []),
        ("skinned_static",  lambda: None,                                                                              [], ["Point A", "Point B"]),
        ("skinned_pose",    lambda: call("set_node_transform", scene_name=scene, node_name="arm_joint_L_2", space="local",
                                         translation=[0.0, 0.2445, 0.0], rotation_xyzw=[0.0, 0.0, 0.38268343, 0.92387953]), ["Point B"], ["Point A"]),
        ("skinned_vertex_drag", drag_skinned_vertices,                                                                 ["Point B"], ["Point A"]),
    ]
    for tag, edit, may_redraw, must_keep in light_steps:
        call("set_shadow_map_cache_enabled", enabled=True)
        time.sleep(args.settle_s * 2.0)
        before = face_counts()
        edit()
        time.sleep(args.settle_s * 2.0)
        after = face_counts()
        redrawn = {}
        ok = True
        for name in ("Point A", "Point B"):
            if (name not in before) or (name not in after) or (not before[name][1]) or (before[name][0] is None) or (after[name][0] is None):
                print(f"{tag}: {name} is not a shadow-mapped point light with draw counts: {before.get(name)} / {after.get(name)}")
                ok = False
                continue
            redrawn[name] = [face for face in range(6) if after[name][0][face] != before[name][0][face]]
        if ok:
            for name in must_keep:
                if redrawn[name]:
                    print(f"{tag}: faces {redrawn[name]} of {name} redrawn, expected all kept")
                    ok = False
            for name in may_redraw:
                if not redrawn[name]:
                    print(f"{tag}: no face of {name} redrawn, expected the reached faces to redraw")
                    ok = False
        mismatch, without_cache = compare(tag)
        changed = pixel_diff(previous, without_cache) if previous else None
        print(f"{tag:14s} cache vs no cache: {mismatch} differing pixels; changed against the previous step: {changed}; faces redrawn: {redrawn}")
        if (mismatch != 0) or not ok:
            failed.append(tag)
        previous = without_cache

    # Shadow fit cache: in a static frame every shadow-mapped light reuses
    # its caster-derived limits (shadow_fit_reused, shadow_fit_serial stable);
    # moving one light refits that light only; captures equal those with
    # the fit cache off.
    def fit_state():
        lights = call("get_scene_lights", scene_name=scene).get("lights", [])
        return {light["name"]: (light.get("shadow_fit_reused"), light.get("shadow_fit_serial")) for light in lights if light.get("shadow_mapped")}

    def compare_fit(tag):
        with_cache = os.path.join(out_dir, f"{tag}_fit_cache.png")
        call("capture_screenshot", path=with_cache)
        call("set_shadow_fit_cache_enabled", enabled=False)
        time.sleep(args.settle_s)
        without_cache = os.path.join(out_dir, f"{tag}_no_fit_cache.png")
        call("capture_screenshot", path=without_cache)
        call("set_shadow_fit_cache_enabled", enabled=True)
        return pixel_diff(with_cache, without_cache)

    call("set_shadow_map_cache_enabled", enabled=True)
    time.sleep(args.settle_s * 2.0)
    before = fit_state()
    time.sleep(args.settle_s)
    after = fit_state()
    ok = bool(before) and all(after[name][0] is True for name in after) and (before == after)
    mismatch = compare_fit("fit_static")
    print(f"fit_static     fit cache vs no fit cache: {mismatch} differing pixels; lights reused: {sum(1 for v in after.values() if v[0])}/{len(after)}; serials stable: {before == after}")
    if (mismatch != 0) or not ok:
        failed.append("fit_static")

    time.sleep(args.settle_s * 2.0)
    before = fit_state()
    call("set_node_transform", scene_name=scene, node_name="Point A", space="world", translation=beside(light_a, 0.05, 0.0, 0.0), rotation_xyzw=identity)
    time.sleep(args.settle_s * 2.0)
    after = fit_state()
    refit = sorted(name for name in after if (name in before) and (after[name][1] != before[name][1]))
    ok = (refit == ["Point A"])
    mismatch = compare_fit("fit_move_point_a")
    print(f"fit_move_point_a fit cache vs no fit cache: {mismatch} differing pixels; refitted: {refit}")
    if (mismatch != 0) or not ok:
        failed.append("fit_move_point_a")

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
