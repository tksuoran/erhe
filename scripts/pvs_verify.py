#!/usr/bin/env python3
"""Gate of the potentially visible set bake (doc/plans/pvs.md "Verification").

Section "sampled": the sampled bake on the wall scene. Launches an editor
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

Section "wall" (after "sampled", in the same editor): the disocclusion bake
(pvs_bake method=disocclusion) of the same grid, with a 6 m box (inside of
which a camera sees through its back faces) and a box beyond its far wall
added far behind the region. Asserts the wall expectations: every cell
keeps the wall and the four boxes in front of it; the cells keeping a box
behind the wall are counted and printed but are not a failure (12 of 48 at
the PVS step 5 commit: false positives, future work, doc/plans/pvs.md
"Open"); then the zero-misses check (below) over every cell; then a
one-cell bake inside the 6 m box keeps the box and the box beyond its far
wall.

Sections "heavy" (the heavy bench scene, config/editor/commands_bench_heavy.json)
and "bistro" (res/editor/assets/bistro_lights_compressed, region at street
height): each in its own editor, a disocclusion bake with 4 m cells over the
content bounds, then the zero-misses check over 20 random cells; bistro also
compares viewport and render_scene_image images with pvs_culling on and off
from street-level poses inside the grid.

The zero-misses check: per cell, M random viewpoints inside the cell (a
viewpoint inside a solid, from which an axis ray first hits a back face, is
redrawn: the bake's free point rule) with random orientations; every tracked
entry (a database column, get_pvs_cell all_columns) that get_visible_entries
sees must be in the cell's PVS (a cell without a PVS keeps everything).
Reports the misses (the gate: 0) and the false-positive ratio, PVS size over
the size of the union of the sampled visible sets, over the checked cells.

Prints the grid, the bake times, the GPU pass times, the database bytes and
the hashes, then a PASS / FAIL line. Quits each editor at the end, deletes the
saved scene file and restores the editor config files a run may write.

Usage: python3 scripts/pvs_verify.py [--editor build_xcode_metal/bin/Debug/editor]
                                     [--scene "Default Scene"] [--samples-per-cell 9]
                                     [--modes gpu,cpu] [--sections sampled,wall,heavy,bistro]
                                     [--viewpoints 12] [--face-resolution 1024]
"""

import argparse
import hashlib
import json
import math
import os
import random
import re
import subprocess
import sys
import time

from mcp_call import rpc
from mcp_viewport_isolation import isolate_viewport
from occlusion_wall_check import BEHIND, BEYOND, FRONT, ROOM, box, mcp
from visible_entries_check import LOG_PATH, Config_backup, launch_editor, stop_editor

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
    hide_pvs_window(port)
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
    hide_pvs_window(port)
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


# ---- the disocclusion bake (doc/plans/pvs.md "Build order" step 5) ----

ROOM_NAME   = "occlusion_room"
BEYOND_NAME = "occlusion_beyond"
# One 2 m cell inside the 6 m box (centered at ROOM, z 37 .. 43).
ROOM_REGION_MIN = [ROOM[0] - 1.0, ROOM[1] - 1.0, ROOM[2] - 1.0]
ROOM_REGION_MAX = [ROOM[0] + 1.0, ROOM[1] + 1.0, ROOM[2] + 1.0]
VISIBLE_WIDTH  = 1024
VISIBLE_HEIGHT = 640
AXES = [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]]


def call(port, tool, arguments):
    """In-process MCP call (scripts/mcp_call.py rpc): the zero-misses check
    makes thousands of calls. Returns the decoded JSON reply or raises."""
    payload = rpc(port, "tools/call", {"name": tool, "arguments": arguments})
    if "error" in payload:
        raise RuntimeError(f"{tool}: {payload['error']}")
    result = payload.get("result", {})
    content = result.get("content")
    text = content[0]["text"] if (isinstance(content, list) and content and "text" in content[0]) else json.dumps(result)
    if result.get("isError", False):
        raise RuntimeError(f"{tool}: {text[:500]}")
    return json.loads(text) if text.strip().startswith("{") else text


def bake_disocclusion(port, scene, arguments, timeout_s):
    request = dict({"scene_name": scene, "method": "disocclusion"}, **arguments)
    started = call(port, "pvs_bake", request)
    if not isinstance(started, dict) or started.get("state") != "running":
        raise RuntimeError(f"pvs_bake: {str(started)[:500]}")
    deadline = time.monotonic() + timeout_s
    last_print = time.monotonic()
    while True:
        status = call(port, "get_pvs_bake_status", {})
        if status["state"] != "running":
            break
        if time.monotonic() > deadline:
            call(port, "pvs_cancel", {})
            raise RuntimeError(f"the disocclusion bake did not finish in {timeout_s} s: {status}")
        if (time.monotonic() - last_print) > 30.0:
            print(f"  bake: {status['cells_done']}/{status['cell_count']} cells, {status['seconds_elapsed']:.0f} s, about {status['seconds_remaining']:.0f} s left")
            last_print = time.monotonic()
        time.sleep(1.0)
    if status["state"] != "done":
        raise RuntimeError(f"the disocclusion bake ended {status['state']}: {status.get('error', '')}")
    return started, status


def report_bake(label, started, done, status):
    gpu = done.get("gpu_ms", {})
    print(
        f"[{label}] disocclusion bake: {started['cell_counts']} = {started['cell_count']} cells of {started['cell_size']} m from {started['grid_origin']}, "
        f"{started['column_count']} columns; {done['seconds_elapsed']:.2f} s ({1000.0 * done['seconds_elapsed'] / max(1, started['cell_count']):.1f} ms/cell); "
        f"cells_with_pvs {status.get('cells_with_pvs')}, without sample point {done.get('cells_without_sample_point')}, sample_point_test {done.get('sample_point_test')}, "
        f"unmatched {done.get('unmatched_entries')}; "
        f"database bytes {status.get('bits_bytes', status.get('bytes'))}, bits_hash {status.get('bits_hash')}"
    )
    print(
        f"[{label}] GPU ms of a cell: cell {gpu.get('cell', 0):.3f}, depth pass {gpu.get('depth_pass', 0):.3f}, id pass {gpu.get('id_pass', 0):.3f}, "
        f"tiles {gpu.get('tiles', 0):.3f}, layer loop {gpu.get('layer_loop', 0):.3f}, map {gpu.get('map', 0):.3f}, vote {gpu.get('vote', 0):.3f}"
    )


def cell_reply(port, scene, cell, all_columns=False):
    reply = call(port, "get_pvs_cell", {"scene_name": scene, "cell": cell, "all_columns": all_columns})
    if not isinstance(reply, dict) or "columns" not in reply:
        raise RuntimeError(f"get_pvs_cell {cell}: {str(reply)[:500]}")
    return reply


def in_pvs_names(reply):
    """None for a cell without a PVS (everything visible)."""
    if not reply["has_pvs"]:
        return None
    return {c["node_name"] for c in reply["columns"]}


def wall_expectations(port, scene, cell_count, failures):
    behind_kept = []
    front_lost  = []
    sizes       = []
    without     = 0
    for cell in range(cell_count):
        names = in_pvs_names(cell_reply(port, scene, cell))
        if names is None:
            without += 1
            behind_kept.append((cell, "no PVS"))
            continue
        sizes.append(len(names))
        kept = sorted(names & set(BEHIND_NAMES))
        if kept:
            behind_kept.append((cell, kept))
        lost = sorted(({WALL_NAME} | set(FRONT_NAMES)) - names)
        if lost:
            front_lost.append((cell, lost))
    print(f"[wall] expectations: PVS size {min(sizes) if sizes else 0}..{max(sizes) if sizes else 0}, cells without a PVS {without}, cells keeping a box behind the wall {len(behind_kept)}, cells losing the wall or a front box {len(front_lost)}")
    if behind_kept:
        # Reported, not a failure: the cells next to the wall keep boxes
        # behind it (a false positive, future work: doc/plans/pvs.md "Open").
        print(f"[wall] boxes behind the wall kept in cells {behind_kept[:6]} (reported, not a failure)")
    if front_lost:
        failures.append(f"[wall] wall or front boxes missing in cells {front_lost[:6]}")


def hide_pvs_window(port):
    """The viewport crops compare the viewport rectangle: a PVS window left
    open in the local ImGui ini would float over it (with its hover state)."""
    try:
        mcp(port, "set_window_visibility", {"title": "PVS", "visible": False})
    except RuntimeError as error:
        print(f"hide PVS window: {error}")


def random_orientation(rng):
    # A uniform random unit quaternion (Shoemake).
    u1, u2, u3 = rng.random(), rng.random(), rng.random()
    a = math.sqrt(1.0 - u1)
    b = math.sqrt(u1)
    return [a * math.sin(2.0 * math.pi * u2), a * math.cos(2.0 * math.pi * u2), b * math.sin(2.0 * math.pi * u3), b * math.cos(2.0 * math.pi * u3)]


def is_free_point(port, scene, point):
    """The bake's free point rule: no axis ray from the point first hits a
    back face (the point is not inside a closed surface)."""
    for axis in AXES:
        hit = call(port, "raycast", {"scene_name": scene, "origin": point, "direction": axis, "max_distance": 10000.0, "mask": 1})
        if isinstance(hit, dict) and hit.get("hit"):
            normal = hit["normal"]
            if (normal[0] * axis[0] + normal[1] * axis[1] + normal[2] * axis[2]) > 0.0:
                return False
    return True


def misses_check(port, scene, label, cells, viewpoints, rng, failures):
    """The zero-misses gate over the given cells."""
    first = cell_reply(port, scene, cells[0], all_columns=True)
    tracked = {(c["node_uid"], c["primitive_index"]) for c in first["all_columns"]}
    total_misses = 0
    missed       = {}
    pvs_total    = 0
    union_total  = 0
    redrawn      = 0
    checked      = 0
    started      = time.monotonic()
    for index, cell in enumerate(cells):
        reply = cell_reply(port, scene, cell)
        pvs = {(c["node_uid"], c["primitive_index"]) for c in reply["columns"]} if reply["has_pvs"] else None
        cell_min = reply["cell_min"]
        cell_max = reply["cell_max"]
        union = set()
        drawn = 0
        attempts = 0
        while (drawn < viewpoints) and (attempts < (viewpoints * 8)):
            attempts += 1
            point = [cell_min[i] + (0.001 + 0.998 * rng.random()) * (cell_max[i] - cell_min[i]) for i in range(3)]
            if not is_free_point(port, scene, point):
                redrawn += 1
                continue
            drawn += 1
            visible = call(port, "get_visible_entries", {
                "scene_name": scene, "position": point, "orientation": random_orientation(rng),
                "width": VISIBLE_WIDTH, "height": VISIBLE_HEIGHT
            })
            for e in visible["entries"]:
                key = (e["node_uid"], e["primitive_index"])
                if key not in tracked:
                    continue
                union.add(key)
                if (pvs is not None) and (key not in pvs):
                    total_misses += 1
                    missed.setdefault((cell, e["node_name"], e["primitive_index"], e["node_uid"]), []).append([round(v, 3) for v in point])
        checked += drawn
        if pvs is not None:
            pvs_total   += len(pvs)
            union_total += len(union)
        if ((index + 1) % 10) == 0:
            print(f"  [{label}] {index + 1}/{len(cells)} cells, {checked} viewpoints, misses {total_misses}, {time.monotonic() - started:.0f} s")
    ratio = (pvs_total / union_total) if union_total > 0 else float("nan")
    print(
        f"[{label}] zero-misses check: {len(cells)} cells, {checked} viewpoints ({redrawn} redrawn inside solids), {len(tracked)} tracked entries, "
        f"misses {total_misses}; false-positive ratio (PVS size / union of the samples) {ratio:.3f} ({pvs_total} / {union_total})"
    )
    for key, points in list(missed.items())[:10]:
        print(f"  [{label}] miss: cell {key[0]} entry {key[1]} primitive {key[2]} from {points[:3]}")
    if total_misses != 0:
        failures.append(f"[{label}] {total_misses} misses")
    return ratio, [(key[0], key[3], key[2], key[1]) for key in missed.keys()]


def run_wall(port, scene, args, failures):
    box(port, scene, ROOM_NAME,   ROOM,   [6.0, 6.0, 6.0])
    box(port, scene, BEYOND_NAME, BEYOND, [1.0, 1.0, 1.0])
    time.sleep(1.0)
    parameters = {
        "cell_size": CELL_SIZE, "region_min": REGION_MIN, "region_max": REGION_MAX,
        "face_resolution": args.face_resolution, "face_fov_degrees": args.face_fov
    }
    started, done = bake_disocclusion(port, scene, parameters, timeout_s=3600.0)
    status = pvs_status(port, scene)
    report_bake("wall", started, done, status)
    if status.get("state") != "ready":
        failures.append(f"[wall] state {status.get('state')} after the disocclusion bake")
    if status.get("bake", {}).get("method") != "disocclusion":
        failures.append(f"[wall] bake method {status.get('bake', {}).get('method')}")
    wall_expectations(port, scene, started["cell_count"], failures)
    misses_check(port, scene, "wall", list(range(started["cell_count"])), args.viewpoints, random.Random(20261030), failures)
    if args.trace:
        # The first cell that keeps a box behind the wall: the box's tile
        # column across the wall.
        for cell in range(started["cell_count"]):
            reply = cell_reply(port, scene, cell)
            uids = {c["node_name"]: c["node_uid"] for c in reply["columns"]}
            names = [n for n in BEHIND_NAMES if n in uids]
            if names:
                trace_cell(port, scene, "wall", reply, {"cell_size": CELL_SIZE, "region_min": REGION_MIN, "region_max": REGION_MAX, "face_resolution": args.face_resolution, "face_fov_degrees": args.face_fov},
                           [(uids[n], 0, n) for n in names], print_columns=True)
                break

    # Inside the 6 m box: its walls are back faces from inside, so the box
    # and the box beyond its far wall stay visible.
    parameters = {"cell_size": 2.0, "region_min": ROOM_REGION_MIN, "region_max": ROOM_REGION_MAX, "face_resolution": args.face_resolution, "face_fov_degrees": args.face_fov}
    started_room, done_room = bake_disocclusion(port, scene, parameters, timeout_s=600.0)
    reply = cell_reply(port, scene, 0)
    names = in_pvs_names(reply)
    print(f"[wall] inside the 6 m box: has_pvs {reply['has_pvs']}, pvs_size {reply['pvs_size']}, cells without a sample point {done_room.get('cells_without_sample_point')}")
    if (names is not None) and not ({ROOM_NAME, BEYOND_NAME} <= names):
        failures.append(f"[wall] inside the 6 m box the PVS lacks {sorted({ROOM_NAME, BEYOND_NAME} - names)}")


def launch_with(editor, extra_args):
    """launch_editor() with extra editor arguments (--commands)."""
    exe = editor if os.path.isabs(editor) else os.path.join(REPO, editor)
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    time.sleep(3.0)  # a previous editor's last log writes
    open(LOG_PATH, "w").close()
    env = dict(os.environ, ERHE_AI_DRIVER="1")
    process = subprocess.Popen([exe] + extra_args, cwd=REPO, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+) \(pid (\d+)")
    deadline = time.monotonic() + 300.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"editor exited with {process.returncode} during startup; see logs/log.txt")
        try:
            with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
        except OSError:
            text = ""
        for match in pattern.finditer(text):
            if (int(match.group(2)) == process.pid) and ("Main loop: completed frame 12" in text):
                return process, int(match.group(1))
        time.sleep(1.0)
    # No fallback to a default port: an answer there could come from another
    # editor, and the gate would drive the wrong one.
    process.kill()
    raise RuntimeError("editor did not become ready (no MCP listening line of this pid in logs/log.txt)")


def content_bounds(port, scene, cell_size):
    """The static entries' bounds, as a bake without a region sees them
    (started and cancelled at once: the region defaults to them)."""
    started = call(port, "pvs_bake", {"scene_name": scene, "method": "disocclusion", "cell_size": cell_size, "face_resolution": 64, "tile_size": 8, "layer_count": 8})
    call(port, "pvs_cancel", {})
    time.sleep(2.0)  # the cancelled bake's node retires
    origin = started["grid_origin"]
    counts = started["cell_counts"]
    return origin, [origin[i] + counts[i] * started["cell_size"] for i in range(3)]


def run_large(port, scene, label, region_min, region_max, args, failures, rng):
    parameters = {"cell_size": 4.0, "region_min": region_min, "region_max": region_max, "face_resolution": args.face_resolution, "face_fov_degrees": args.face_fov}
    started, done = bake_disocclusion(port, scene, parameters, timeout_s=4 * 3600.0)
    status = pvs_status(port, scene)
    report_bake(label, started, done, status)
    if status.get("state") != "ready":
        failures.append(f"[{label}] state {status.get('state')} after the bake")
    cell_count = started["cell_count"]
    cells = sorted(rng.sample(range(cell_count), min(20, cell_count)))
    _, missed = misses_check(port, scene, label, cells, args.viewpoints, rng, failures)
    return started, missed


def trace_cell(port, scene, label, reply, parameters, entries, print_columns=False):
    """A traced bake of only the cell `reply` (get_pvs_cell) of the full
    bake's grid (`parameters` carry the full bake's region, so the cell's
    depth range and sample point are the full bake's: the trace reproduces
    its cell bit for bit), with the bake trace of entries [(node_uid,
    primitive_index, name)]; returns the trace and prints it. Replaces the
    scene's database."""
    request = dict(parameters)
    request.update({"trace_cell": reply["cell"], "trace_only": True,
                    "trace_entries": [[uid, prim] for uid, prim, _ in entries]})
    _, done = bake_disocclusion(port, scene, request, timeout_s=600.0)
    trace = done.get("trace", {})
    names = {(uid, prim): name for uid, prim, name in entries}
    for index, column in enumerate(trace.get("columns", [])):
        name = names.get((column["node_uid"], column["primitive_index"]), "?")
        print(f"  [{label}] trace cell {reply['cell']} {name}: in_cell_pvs {column['in_cell_pvs']}")
        for face in trace.get("faces", []):
            e = face["entries"][index]
            print(
                f"    face {face['face']} s{face['sample']}: fragments {e['fragments']} samples {e['samples']} core {e['core_samples']} "
                f"disoccluded {e['disoccluded_samples']} in_words {e['in_column_words']} vote {e['bounds_vote']} near {e['near']} in_face {e['in_face']} "
                f"rect {e['rectangle']} layers {e['layers']}"
            )
            if print_columns and e["in_face"]:
                lo, hi = e["layers"]
                column_text = " ".join(f"{l}:{'C' if c[0] else 'o'}{'d' if c[1] else ''}" for l, c in enumerate(e["tile_column"]) if l <= hi)
                print(f"      tile {e['tile']} per layer (C closed / o open, d disoccluded): {column_text}")
    return trace


def classify(trace, index):
    """A: no fragment on any face. C: no visible part of the entry is in a
    disoccluded chunk of any face (closure / cones). E: a face's bounds vote
    is true but the cell's PVS lacks the entry (readback, mapping). V: a
    disoccluded sample of the entry exists but no face's bounds vote is
    true (vote bug). "in": the one-cell bake includes it. The sample counts
    (samples, core_samples, disoccluded_samples) need the id pass, which a
    trace bake runs."""
    faces = [face["entries"][index] for face in trace.get("faces", [])]
    column = trace["columns"][index]
    if column["in_cell_pvs"]:
        return "in"
    if any(e["bounds_vote"] for e in faces):
        return "E"
    if any(e["disoccluded_samples"] > 0 for e in faces):
        return "V"
    if all(e["fragments"] == 0 for e in faces):
        return "A"
    return "C"


def trace_misses(port, scene, label, missed, parameters, limit):
    """Classifies up to `limit` missed (cell, column) pairs into the buckets
    of classify()."""
    by_cell = {}
    for cell, uid, prim, name in missed:
        by_cell.setdefault(cell, [])
        if (uid, prim, name) not in by_cell[cell]:
            by_cell[cell].append((uid, prim, name))
    # The cells' replies before any trace: a one-cell trace bake replaces
    # the scene's database.
    replies = {}
    count = 0
    for cell, entries in by_cell.items():
        if count >= limit:
            break
        replies[cell] = cell_reply(port, scene, cell)
        count += len(entries)
    buckets = {}
    traced = 0
    for cell, entries in by_cell.items():
        if (traced >= limit) or (cell not in replies):
            break
        entries = entries[:max(1, limit - traced)]
        trace = trace_cell(port, scene, label, replies[cell], parameters, entries)
        for index, column in enumerate(trace.get("columns", [])):
            bucket = classify(trace, index)
            buckets[bucket] = buckets.get(bucket, 0) + 1
            traced += 1
    print(f"[{label}] miss buckets over {traced} traced pairs: {dict(sorted(buckets.items()))}")
    return buckets


def wait_for_scene(port, name, timeout_s):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        names = scene_names(port)
        if name in names:
            return
        time.sleep(1.0)
    raise RuntimeError(f"scene {name!r} did not appear")


HEAVY_Y = [0.2, 4.2]


def run_heavy(args, failures):
    process, port = launch_with(args.editor, ["--commands", "config/editor/commands_bench_heavy.json"])
    try:
        scene = "Bench Heavy"
        wait_for_scene(port, scene, 300.0)
        time.sleep(5.0)
        # The bench instances carry dynamic bodies (not in the static set,
        # doc/plans/pvs.md D3): make every body static so the bake has the
        # ~1500 instances as columns, and turn physics off.
        try:
            call(port, "toggle_physics", {"enabled": False})
        except RuntimeError as error:
            print(f"[heavy] toggle_physics: {error}")
        nodes = call(port, "get_scene_nodes", {"scene_name": scene})
        node_list = nodes.get("nodes", []) if isinstance(nodes, dict) else []
        made_static = 0
        for node in node_list:
            try:
                call(port, "edit_physics_body", {"scene_name": scene, "node_id": node["id"], "motion_mode": "static"})
                made_static += 1
            except RuntimeError as error:
                if "Node has no rigid body" not in str(error):
                    raise
                # a node without a rigid body
        print(f"[heavy] {made_static} of {len(node_list)} nodes' bodies made static")
        time.sleep(3.0)
        bounds_min, bounds_max = content_bounds(port, scene, 4.0)
        print(f"[heavy] content bounds {bounds_min} .. {bounds_max}")
        # Above the floor slab (y -1 .. 0): a viewpoint inside it sees
        # through its back faces, and the raytrace scene does not hold the
        # floor, so the free point rule cannot reject such viewpoints.
        region_min = [bounds_min[0], HEAVY_Y[0], bounds_min[2]]
        region_max = [bounds_max[0], HEAVY_Y[1], bounds_max[2]]
        _, missed = run_large(port, scene, "heavy", region_min, region_max, args, failures, random.Random(4242))
        if args.trace and missed:
            trace_misses(port, scene, "heavy", missed, {"cell_size": 4.0, "region_min": region_min, "region_max": region_max, "face_resolution": args.face_resolution, "face_fov_degrees": args.face_fov}, 20)
    finally:
        stop_editor(port, process)


BISTRO_PATH  = "res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf"
BISTRO_SCENE = "bistro_lights_c.gltf"
# The street-height slab of the region (meters above the street at y 0).
BISTRO_Y     = [0.2, 4.2]


def bistro_images(port, scene, cells, failures):
    """Viewport and render_scene_image with pvs_culling on and off from
    street-level poses inside the grid (the viewport's view has the rule)."""
    viewport = isolate_viewport(lambda tool, arguments: mcp(port, tool, arguments), scene_name=scene)
    hide_pvs_window(port)
    os.makedirs(os.path.join(REPO, SHOT_DIR), exist_ok=True)
    for index, cell in enumerate(cells):
        reply = cell_reply(port, scene, cell)
        center = [0.5 * (reply["cell_min"][i] + reply["cell_max"][i]) for i in range(3)]
        yaw = 0.25 * math.pi * index
        rotation = [0.0, math.sin(0.5 * yaw), 0.0, math.cos(0.5 * yaw)]
        mcp(port, "set_node_transform", {"scene_name": scene, "node_name": viewport["camera"], "translation": center, "rotation_xyzw": rotation, "space": "world"})
        forward = [-math.sin(yaw), 0.0, -math.cos(yaw)]
        target = [center[i] + 10.0 * forward[i] for i in range(3)]
        hashes = {}
        render_hashes = {}
        culled = None
        for enabled in (True, False):
            mcp(port, "set_pvs_culling", {"enabled": enabled})
            time.sleep(1.5)
            tag  = "on" if enabled else "off"
            shot = os.path.join(SHOT_DIR, f"bistro_{index}_{tag}_viewport.png")
            mcp(port, "capture_screenshot", {"path": shot})
            entry = viewport_entry(port, viewport["title"])
            hashes[tag], _ = viewport_crop_hash(os.path.join(REPO, shot), entry)
            render = os.path.join(SHOT_DIR, f"bistro_{index}_{tag}_render.png")
            mcp(port, "render_scene_image", {
                "scene": scene, "camera": {"eye": center, "target": target, "up": [0, 1, 0], "fov_y_degrees": 60, "near": 0.1, "far": 500},
                "width": 960, "height": 540, "output": "png", "path": render
            })
            render_hashes[tag] = file_hash(os.path.join(REPO, render))
            if enabled:
                views = pvs_status(port, scene).get("views", [])
                culled = views[0] if views else None
        print(f"[bistro] pose {index} (cell {cell}): PVS rule {culled}; viewport md5 on {hashes['on']} off {hashes['off']}; render_scene_image md5 on {render_hashes['on']} off {render_hashes['off']}")
        if hashes["on"] != hashes["off"]:
            failures.append(f"[bistro] pose {index}: viewport image differs with pvs_culling on / off")
        if render_hashes["on"] != render_hashes["off"]:
            failures.append(f"[bistro] pose {index}: render_scene_image differs with pvs_culling on / off")
    mcp(port, "set_pvs_culling", {"enabled": True})


def run_bistro(args, failures):
    process, port = launch_with(args.editor, [])
    try:
        # One open, then wait until the scene is listed.
        call(port, "open_scene", {"path": BISTRO_PATH})
        deadline = time.monotonic() + 300.0
        while BISTRO_SCENE not in scene_names(port):
            if time.monotonic() > deadline:
                raise RuntimeError(f"open_scene: {BISTRO_SCENE} did not appear")
            time.sleep(1.0)
        # The open is listed before its content is: wait until a bake sees
        # the static set settle.
        time.sleep(60.0)
        bounds_min, bounds_max = content_bounds(port, BISTRO_SCENE, 4.0)
        print(f"[bistro] content bounds {bounds_min} .. {bounds_max}")
        region_min = [bounds_min[0], BISTRO_Y[0], bounds_min[2]]
        region_max = [bounds_max[0], BISTRO_Y[1], bounds_max[2]]
        rng = random.Random(1234)
        started, missed = run_large(port, BISTRO_SCENE, "bistro", region_min, region_max, args, failures, rng)
        cells = sorted(rng.sample(range(started["cell_count"]), min(4, started["cell_count"])))
        bistro_images(port, BISTRO_SCENE, cells, failures)
        if args.trace and missed:
            trace_misses(port, BISTRO_SCENE, "bistro", missed, {"cell_size": 4.0, "region_min": region_min, "region_max": region_max, "face_resolution": args.face_resolution, "face_fov_degrees": args.face_fov}, 20)
    finally:
        stop_editor(port, process)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", default=os.path.join("build_xcode_metal", "bin", "Debug", "editor"))
    parser.add_argument("--scene", default="Default Scene")
    parser.add_argument("--samples-per-cell", type=int, default=9)
    parser.add_argument("--modes", default="gpu,cpu", help="culling classification modes of the consumer section")
    parser.add_argument("--sections", default="sampled,wall,heavy,bistro", help="sampled, wall (needs sampled's scene), heavy, bistro")
    parser.add_argument("--viewpoints", type=int, default=12, help="random viewpoints per cell of the zero-misses check")
    parser.add_argument("--face-resolution", type=int, default=1024, help="disocclusion bake face resolution")
    parser.add_argument("--face-fov", type=float, default=120.0, help="disocclusion bake face field of view, degrees")
    parser.add_argument("--trace", action="store_true", help="trace the misses (bake trace, buckets of classify()) and a wall cell")
    args = parser.parse_args()
    sections = [s for s in args.sections.split(",") if s]

    backup = Config_backup()
    failures = []
    try:
        if ("sampled" in sections) or ("wall" in sections):
            process = None
            port = None
            try:
                process, port = launch_editor(args.editor)
                print(f"editor pid {process.pid} on port {port}")
                if "sampled" in sections:
                    failures += run(port, args.scene, args.samples_per_cell, [m for m in args.modes.split(",") if m])
                else:
                    box(port, args.scene, WALL_NAME, [0.0, 3.0, -2.0], [30.0, 12.0, 0.4])
                    for name, p in zip(BEHIND_NAMES, BEHIND):
                        box(port, args.scene, name, p, [1.0, 1.0, 1.0])
                    for name, p in zip(FRONT_NAMES, FRONT):
                        box(port, args.scene, name, p, [0.4, 0.4, 0.4])
                if "wall" in sections:
                    run_wall(port, args.scene, args, failures)
            finally:
                if process is not None:
                    stop_editor(port, process)
        if "heavy" in sections:
            run_heavy(args, failures)
        if "bistro" in sections:
            run_bistro(args, failures)
    except Exception as error:  # noqa: BLE001 - report and fail
        failures.append(f"exception: {error}")
    finally:
        backup.restore()
    print("PASS" if not failures else "FAIL: " + "; ".join(failures))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
