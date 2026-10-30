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
- after save_scene and load_scene of the scene, the loaded scene's
  get_pvs_status reports ready with the same bits_hash, column_count and
  cells_with_pvs.

Prints the grid, the bake times and the hashes, then a PASS / FAIL line.
Quits the editor at the end, deletes the saved scene file and restores the
editor config files a run may write.

Usage: python3 scripts/pvs_verify.py [--editor build_xcode_metal/bin/Debug/editor]
                                     [--scene "Default Scene"] [--samples-per-cell 9]
"""

import argparse
import os
import sys
import time

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


def scene_names(port):
    reply = mcp(port, "list_scenes", {})
    return [s.get("name", "") for s in reply.get("scenes", [])] if isinstance(reply, dict) else []


def run(port, scene, samples_per_cell):
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
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", default=os.path.join("build_xcode_metal", "bin", "Debug", "editor"))
    parser.add_argument("--scene", default="Default Scene")
    parser.add_argument("--samples-per-cell", type=int, default=9)
    args = parser.parse_args()

    backup = Config_backup()
    process = None
    port = None
    failures = ["did not run"]
    try:
        process, port = launch_editor(args.editor)
        print(f"editor pid {process.pid} on port {port}")
        failures = run(port, args.scene, args.samples_per_cell)
    finally:
        if process is not None:
            stop_editor(port, process)
        backup.restore()
    print("PASS" if not failures else "FAIL: " + "; ".join(failures))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
