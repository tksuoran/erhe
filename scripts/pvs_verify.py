#!/usr/bin/env python3
"""Gate of the potentially visible set bake (doc/plans/pvs.md "Verification").

First version: the sampled bake on the wall scene. Launches an editor
(ERHE_AI_DRIVER=1, the default startup scene) from the repo root, waits for
its MCP server, builds the wall scene of scripts/occlusion_wall_check.py (its
box() helper and positions are imported: a 30 x 12 m wall at z = -2, six 1 m
boxes behind it, four 0.4 m boxes in front of it, all static), and runs
pvs_bake method=sampled over a 12 x 4 x 8 m region in front of the wall with
2 m cells, polling get_pvs_bake_status until it is done. Asserts:

- get_pvs_status reports state ready and cells_with_pvs == cell_count;
- in every cell (get_pvs_cell) the wall is in the PVS and none of the six
  occlusion_behind_* boxes is;
- a second identical bake gives the same bits_hash;
- the consumer (doc/plans/pvs.md "Runtime consumption"), with the viewport
  camera in a cell in front of the wall looking at it, for each culling
  classification mode (gpu, then cpu): get_pvs_status reports the viewport
  view with culled == 6 (the boxes behind the wall, which no viewpoint of the
  sampled bake saw) and frame_culling_active; get_cull_classification_check
  (gpu mode, on a device with the GPU path) reports 0 mismatches with equal
  CPU and GPU PVS culled counts; the viewport region of capture_screenshot and
  a render_scene_image from the camera's pose are byte-identical with
  pvs_culling on and off;
- after save_scene and load_scene of the scene, the loaded scene's
  get_pvs_status reports ready with the same bits_hash, column_count and
  cells_with_pvs;
- moving a box in front of the wall makes the database stale: the next
  frames publish the rule off and the viewport view culls nothing.

Prints the grid, the bake times and the hashes, then a PASS / FAIL line.
Quits the editor at the end, deletes the saved scene file and restores the
editor config files a run may write.

Usage: python3 scripts/pvs_verify.py [--editor build_xcode_metal/bin/Debug/editor]
                                     [--scene "Default Scene"] [--samples-per-cell 9]
                                     [--modes gpu,cpu]
"""

import argparse
import hashlib
import os
import sys
import time

from mcp_viewport_isolation import isolate_viewport
from occlusion_wall_check import BEHIND, FRONT, box, mcp
from visible_entries_check import Config_backup, launch_editor, stop_editor

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

WALL_NAME    = "occlusion_wall"
BEHIND_NAMES = [f"occlusion_behind_{i}" for i in range(len(BEHIND))]
FRONT_NAMES  = [f"occlusion_front_{i}" for i in range(len(FRONT))]
# 12 x 4 x 8 m in front of the wall (its front face is at z = -1.8).
REGION_MIN   = [-6.0, 0.2, -1.5]
REGION_MAX   = [ 6.0, 4.2,  6.5]
CELL_SIZE    = 2.0
SAVE_PATH    = os.path.join("logs", "pvs_verify_scene.glb")
# The consumer's camera: inside the region (cell (3, 0, 3)), looking down
# -Z at the wall with the identity rotation.
CONSUMER_EYE = [0.5, 1.3, 5.0]
SHOT_DIR     = os.path.join("logs", "pvs_verify_shots")


def bake(port, scene, samples_per_cell, timeout_s):
    started = mcp(port, "pvs_bake", {
        "scene_name": scene, "method": "sampled", "cell_size": CELL_SIZE,
        "region_min": REGION_MIN, "region_max": REGION_MAX, "samples_per_cell": samples_per_cell
    })
    if not isinstance(started, dict) or started.get("state") != "running":
        raise RuntimeError(f"pvs_bake: {str(started)[:500]}")
    deadline = time.monotonic() + timeout_s
    while True:
        status = mcp(port, "get_pvs_bake_status", {})
        if not isinstance(status, dict):
            raise RuntimeError(f"get_pvs_bake_status: {str(status)[:500]}")
        if status["state"] != "running":
            break
        if time.monotonic() > deadline:
            mcp(port, "pvs_cancel", {})
            raise RuntimeError(f"the bake did not finish in {timeout_s} s: {status}")
        time.sleep(1.0)
    if status["state"] != "done":
        raise RuntimeError(f"the bake ended {status['state']}: {status.get('error', '')}")
    return started, status


def pvs_status(port, scene):
    status = mcp(port, "get_pvs_status", {"scene_name": scene})
    if not isinstance(status, dict):
        raise RuntimeError(f"get_pvs_status: {str(status)[:500]}")
    return status


def viewport_crop_hash(path, viewport):
    """MD5 of the raw RGBA pixels of the viewport's rectangle of a
    capture_screenshot (the ImGui window around it differs in the frame time
    text). The rectangle is in the screenshot's pixels when the capture is
    the viewport rectangle's size times the display scale."""
    from PIL import Image
    image = Image.open(path).convert("RGBA")
    scale_x = image.width  / float(viewport["window_width"])  if viewport.get("window_width")  else 1.0
    scale_y = image.height / float(viewport["window_height"]) if viewport.get("window_height") else 1.0
    box_px = (
        int(round(viewport["x"] * scale_x)), int(round(viewport["y"] * scale_y)),
        int(round((viewport["x"] + viewport["width"]) * scale_x)), int(round((viewport["y"] + viewport["height"]) * scale_y))
    )
    crop = image.crop(box_px)
    return hashlib.md5(crop.tobytes()).hexdigest(), box_px


def file_hash(path):
    with open(path, "rb") as handle:
        return hashlib.md5(handle.read()).hexdigest()


def viewport_entry(port, title):
    reply = mcp(port, "get_viewports", {})
    for v in reply.get("viewports", []) if isinstance(reply, dict) else []:
        if v.get("title") == title:
            return v
    raise RuntimeError(f"get_viewports: no viewport {title!r}")


def pvs_view(status, view_name):
    for v in status.get("views", []):
        if v.get("view") == view_name:
            return v
    return None


def consumer(port, scene, viewport, mode, failures):
    """One classification mode of the consumer gate (doc/plans/pvs.md
    "Runtime consumption")."""
    camera = viewport["camera"]
    title  = viewport["title"]
    print(mcp(port, "set_culling_classification", {"mode": mode}))
    mcp(port, "set_pvs_culling", {"enabled": True})
    time.sleep(2.0)
    status = pvs_status(port, scene)
    views  = status.get("views", [])
    print(f"[{mode}] pvs status: state {status.get('state')}, culling_enabled {status.get('culling_enabled')}, frame_culling_active {status.get('frame_culling_active')}, camera {status.get('camera')}, views {views}")
    if not status.get("frame_culling_active"):
        failures.append(f"[{mode}] frame_culling_active false")
    if len(views) != 1:
        failures.append(f"[{mode}] expected one viewport view, got {views}")
    elif views[0].get("culled") != len(BEHIND_NAMES):
        failures.append(f"[{mode}] viewport view culled {views[0].get('culled')} != {len(BEHIND_NAMES)}")

    try:
        check = mcp(port, "get_cull_classification_check", {"scene_name": scene})
    except RuntimeError as error:
        check = str(error)
    if isinstance(check, dict):
        print(
            f"[{mode}] classification check: frame {check.get('frame_index')}, views {check.get('view_count')}, entries {check.get('entry_count')}, "
            f"mismatch_count {check.get('mismatch_count')}, pvs_mismatch_count {check.get('pvs_mismatch_count')}, "
            f"pvs_culled cpu {check.get('pvs_culled_cpu_count')} gpu {check.get('pvs_culled_gpu_count')}, "
            f"command_mismatch_count {check.get('command_mismatch_count')}"
        )
        if check.get("mismatch_count") != 0:
            failures.append(f"[{mode}] classification check mismatch_count {check.get('mismatch_count')}")
        if check.get("command_mismatch_count") != 0:
            failures.append(f"[{mode}] classification check command_mismatch_count {check.get('command_mismatch_count')}")
        if check.get("pvs_culled_cpu_count") != check.get("pvs_culled_gpu_count"):
            failures.append(f"[{mode}] PVS culled cpu {check.get('pvs_culled_cpu_count')} != gpu {check.get('pvs_culled_gpu_count')}")
        if check.get("pvs_culled_cpu_count", 0) < len(BEHIND_NAMES):
            failures.append(f"[{mode}] PVS culled {check.get('pvs_culled_cpu_count')} < {len(BEHIND_NAMES)}")
    else:
        # The check needs the GPU path (culling_classification gpu on a
        # device with it): not applicable in cpu mode or on MoltenVK.
        print(f"[{mode}] classification check not applicable: {str(check).strip()[:200]}")

    # Images: the viewport (the view with the PVS rule) and a scene image
    # from the camera's pose (its own view, without the rule), on and off.
    os.makedirs(os.path.join(REPO, SHOT_DIR), exist_ok=True)
    hashes = {}
    render_hashes = {}
    for enabled in (True, False):
        mcp(port, "set_pvs_culling", {"enabled": enabled})
        time.sleep(1.5)
        tag  = "on" if enabled else "off"
        shot = os.path.join(SHOT_DIR, f"{mode}_{tag}_viewport.png")
        mcp(port, "capture_screenshot", {"path": shot})
        entry = viewport_entry(port, title)
        hashes[tag], box_px = viewport_crop_hash(os.path.join(REPO, shot), entry)
        render = os.path.join(SHOT_DIR, f"{mode}_{tag}_render.png")
        mcp(port, "render_scene_image", {
            "scene": scene,
            "camera": {"eye": CONSUMER_EYE, "target": [CONSUMER_EYE[0], CONSUMER_EYE[1], CONSUMER_EYE[2] - 10.0], "up": [0, 1, 0], "fov_y_degrees": 50, "near": 0.05, "far": 200},
            "width": 640, "height": 400, "output": "png", "path": render
        })
        render_hashes[tag] = file_hash(os.path.join(REPO, render))
        if enabled:
            on_status = pvs_status(port, scene)
            on_view = pvs_view(on_status, views[0]["view"]) if views else None
            print(f"[{mode}] at the 'on' capture: view {on_view}")
    print(f"[{mode}] viewport crop {box_px} md5 on {hashes['on']} off {hashes['off']}; render_scene_image md5 on {render_hashes['on']} off {render_hashes['off']}")
    if hashes["on"] != hashes["off"]:
        failures.append(f"[{mode}] viewport image differs with pvs_culling on / off")
    if render_hashes["on"] != render_hashes["off"]:
        failures.append(f"[{mode}] render_scene_image differs with pvs_culling on / off")
    mcp(port, "set_pvs_culling", {"enabled": True})


def stale_check(port, scene, failures):
    """Moving a baked entry makes the database stale (doc/plans/pvs.md D4)."""
    # The scene's viewport again (the reload may have opened another), with
    # the consumer camera: it culls the six boxes before the move.
    viewport = isolate_viewport(lambda tool, arguments: mcp(port, tool, arguments), scene_name=scene)
    mcp(port, "set_node_transform", {"scene_name": scene, "node_name": viewport["camera"], "translation": CONSUMER_EYE, "rotation_xyzw": [0, 0, 0, 1], "space": "world"})
    time.sleep(1.5)
    before = pvs_status(port, scene).get("views", [])
    print(f"stale: before the move, views {before}")
    if (len(before) != 1) or (before[0].get("culled") != len(BEHIND_NAMES)):
        failures.append(f"before the stale mark: views {before}, expected one view culling {len(BEHIND_NAMES)}")
    mcp(port, "set_node_transform", {"scene_name": scene, "node_name": FRONT_NAMES[0], "translation": [FRONT[0][0] + 0.25, FRONT[0][1], FRONT[0][2]], "space": "world"})
    time.sleep(1.5)
    status = pvs_status(port, scene)
    views = status.get("views", [])
    print(f"stale: state {status.get('state')}, frame_culling_active {status.get('frame_culling_active')}, views {views}")
    if status.get("state") != "stale":
        failures.append(f"after moving {FRONT_NAMES[0]}: state {status.get('state')}, expected stale")
    if status.get("frame_culling_active"):
        failures.append("after the stale mark: frame_culling_active still true")
    if (len(views) != 1) or (views[0].get("culled") != 0):
        failures.append(f"after the stale mark: views {views}, expected one view culling 0")


def scene_names(port):
    reply = mcp(port, "list_scenes", {})
    return [s.get("name", "") for s in reply.get("scenes", [])] if isinstance(reply, dict) else []


def run(port, scene, samples_per_cell, modes):
    failures = []
    box(port, scene, WALL_NAME, [0.0, 3.0, -2.0], [30.0, 12.0, 0.4])
    for name, p in zip(BEHIND_NAMES, BEHIND):
        box(port, scene, name, p, [1.0, 1.0, 1.0])
    for name, p in zip(FRONT_NAMES, FRONT):
        box(port, scene, name, p, [0.4, 0.4, 0.4])
    time.sleep(1.0)  # the inserts land on the next frames

    started, done = bake(port, scene, samples_per_cell, timeout_s=1800.0)
    print(
        f"bake 1: {started['cell_counts']} = {started['cell_count']} cells of {started['cell_size']} m from {started['grid_origin']}, "
        f"{started['column_count']} columns, {started['capture_count']} captures, {started['uids_assigned']} uids assigned; "
        f"{done['seconds_elapsed']:.2f} s, unmatched entries {done['unmatched_entries']}"
    )
    status = pvs_status(port, scene)
    cell_count = status.get("cell_count", 0)
    if status.get("state") != "ready":
        failures.append(f"state {status.get('state')} after the bake")
    if status.get("cells_with_pvs") != cell_count:
        failures.append(f"cells_with_pvs {status.get('cells_with_pvs')} != cell_count {cell_count}")
    if status.get("bake", {}).get("method") != "sampled":
        failures.append(f"bake method {status.get('bake', {}).get('method')}")
    print(f"status: state {status.get('state')}, cells_with_pvs {status.get('cells_with_pvs')}/{cell_count}, columns {status.get('column_count')}, bits_hash {status.get('bits_hash')}")

    wall_missing = []
    behind_seen  = []
    sizes        = []
    for cell in range(cell_count):
        reply = mcp(port, "get_pvs_cell", {"scene_name": scene, "cell": cell})
        if not isinstance(reply, dict) or "columns" not in reply:
            raise RuntimeError(f"get_pvs_cell {cell}: {str(reply)[:500]}")
        names = {c["node_name"] for c in reply["columns"]}
        sizes.append(reply["pvs_size"])
        if WALL_NAME not in names:
            wall_missing.append(cell)
        seen = sorted(names & set(BEHIND_NAMES))
        if seen:
            behind_seen.append((cell, seen))
    if wall_missing:
        failures.append(f"the wall is missing in cells {wall_missing}")
    if behind_seen:
        failures.append(f"boxes behind the wall in cells {behind_seen[:6]}")
    print(f"cells: PVS size {min(sizes)}..{max(sizes)} of {status.get('column_count')} columns; wall missing in {len(wall_missing)}, behind boxes seen in {len(behind_seen)}")

    _, done_2 = bake(port, scene, samples_per_cell, timeout_s=1800.0)
    status_2 = pvs_status(port, scene)
    print(f"bake 2: {done_2['seconds_elapsed']:.2f} s, bits_hash {status_2.get('bits_hash')}")
    if status_2.get("bits_hash") != status.get("bits_hash"):
        failures.append(f"second bake bits_hash {status_2.get('bits_hash')} != {status.get('bits_hash')}")

    # The consumer: the viewport camera in a cell in front of the wall.
    viewport = isolate_viewport(lambda tool, arguments: mcp(port, tool, arguments), scene_name=scene)
    mcp(port, "set_node_transform", {"scene_name": scene, "node_name": viewport["camera"], "translation": CONSUMER_EYE, "rotation_xyzw": [0, 0, 0, 1], "space": "world"})
    for mode in modes:
        consumer(port, scene, viewport, mode, failures)

    save_path = os.path.join(REPO, SAVE_PATH)
    if os.path.isfile(save_path):
        os.remove(save_path)
    job = mcp(port, "save_scene", {"scene_name": scene, "path": SAVE_PATH})
    if not isinstance(job, dict) or "job_id" not in job:
        raise RuntimeError(f"save_scene: {str(job)[:500]}")
    deadline = time.monotonic() + 300.0
    while True:
        save = mcp(port, "get_save_status", {"job_id": job["job_id"]})
        if isinstance(save, dict) and save.get("state") in ("done", "failed"):
            break
        if time.monotonic() > deadline:
            raise RuntimeError(f"save did not finish: {save}")
        time.sleep(0.2)
    if save.get("state") != "done":
        raise RuntimeError(f"save failed: {save.get('error', save)}")
    before = set(scene_names(port))
    mcp(port, "load_scene", {"path": SAVE_PATH})
    loaded = None
    deadline = time.monotonic() + 120.0
    while (loaded is None) and (time.monotonic() < deadline):
        added = sorted(set(scene_names(port)) - before)
        if added:
            loaded = added[0]
        else:
            time.sleep(0.5)
    if loaded is None:
        raise RuntimeError("load_scene: no new scene appeared")
    reloaded = pvs_status(port, loaded)
    print(
        f"reload '{loaded}': state {reloaded.get('state')}, bits_hash {reloaded.get('bits_hash')}, "
        f"columns {reloaded.get('column_count')}, cells_with_pvs {reloaded.get('cells_with_pvs')}"
    )
    if reloaded.get("state") != "ready":
        failures.append(f"reloaded state {reloaded.get('state')}")
    for key in ("bits_hash", "column_count", "cells_with_pvs"):
        if reloaded.get(key) != status_2.get(key):
            failures.append(f"reloaded {key} {reloaded.get(key)} != {status_2.get(key)}")
    if os.path.isfile(save_path):
        os.remove(save_path)

    stale_check(port, scene, failures)
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", default=os.path.join("build_xcode_metal", "bin", "Debug", "editor"))
    parser.add_argument("--scene", default="Default Scene")
    parser.add_argument("--samples-per-cell", type=int, default=9)
    parser.add_argument("--modes", default="gpu,cpu", help="culling classification modes of the consumer section")
    args = parser.parse_args()

    backup = Config_backup()
    process = None
    port = None
    failures = ["did not run"]
    try:
        process, port = launch_editor(args.editor)
        print(f"editor pid {process.pid} on port {port}")
        failures = run(port, args.scene, args.samples_per_cell, [m for m in args.modes.split(",") if m])
    finally:
        if process is not None:
            stop_editor(port, process)
        backup.restore()
    print("PASS" if not failures else "FAIL: " + "; ".join(failures))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
