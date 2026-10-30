#!/usr/bin/env python3
"""Gate of the MCP tool get_visible_entries (doc/plans/pvs.md "Verification").

Launches an editor (ERHE_AI_DRIVER=1, the default startup scene) from the repo
root, waits for its MCP server, builds the wall scene of
scripts/occlusion_wall_check.py (its box() helper and positions are imported:
a 30 x 12 m wall, six 1 m boxes behind it, four 0.4 m boxes in front of it,
all static), places the scene's viewport camera at the occlusion gate's eye
looking down -Z and calls get_visible_entries from that camera's pose ten
times. Passes when the ten replies list the same entries with the same pixel
counts, the wall and the four front boxes are in the set and the six boxes
behind the wall are not, and, from a pose behind the wall looking back at it,
the six boxes are in the set. Material cases (the id pass draws what the
color pass draws): from the front pose an alpha-tested box whose opacity is
under its cutoff everywhere is not in the set and one whose opacity is above
it is; from the back pose two one-sided rectangles (front face only, facing
+Z, away from the camera) show their back faces: the double-sided one (its
material's double_sided property) is in the set, the single-sided one is not.
Prints the counts. Quits the editor at the end and restores the editor config
files a run may write.

This is the first piece of the plan's scripts/pvs_verify.py.

Usage: python3 scripts/visible_entries_check.py [--editor build_xcode_metal/bin/Debug/editor]
                                                [--scene "Default Scene"] [--repeats 10]
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

from occlusion_wall_check import BEHIND, EYE, FRONT, box, mcp
from mcp_viewport_isolation import isolate_viewport

REPO     = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_PATH = os.path.join(REPO, "logs", "log.txt")

# The editor config files a run may write (window layout, settings).
CONFIG_FILES = [
    "config/editor/desktop_window_imgui_host_imgui.ini",
    "config/editor/desktop_windows.json",
    "config/editor/editor_settings.json",
    "config/editor/erhe_graphics.json",
    "config/editor/default_viewport_config.json",
]

WALL_NAME    = "occlusion_wall"
BEHIND_NAMES = [f"occlusion_behind_{i}" for i in range(len(BEHIND))]
FRONT_NAMES  = [f"occlusion_front_{i}" for i in range(len(FRONT))]
# Behind the wall, looking back at it (+Z) over the six boxes.
BACK_EYE     = [0.0, 1.3, -12.0]
BACK_TARGET  = [0.0, 1.3, 0.0]
# Alpha-tested boxes seen from the front pose, clear of the front boxes.
ALPHA_HIDDEN  = "visible_alpha_test_discarded"
ALPHA_SHOWN   = "visible_alpha_test_kept"
# One-sided rectangles facing +Z between the back pose and the boxes behind
# the wall, above the boxes' silhouettes: the back pose sees their back faces.
DOUBLE_SIDED  = "visible_double_sided_rectangle"
SINGLE_SIDED  = "visible_single_sided_rectangle"


class Config_backup:
    def __init__(self):
        self.directory = tempfile.mkdtemp(prefix="visible_entries_check_")
        self.saved = []
        for relative in CONFIG_FILES:
            source = os.path.join(REPO, relative)
            if os.path.isfile(source):
                target = os.path.join(self.directory, relative.replace("/", "__"))
                shutil.copyfile(source, target)
                self.saved.append((relative, target))

    def restore(self):
        for relative, target in self.saved:
            destination = os.path.join(REPO, relative)
            with open(target, "rb") as handle:
                data = handle.read()
            current = None
            if os.path.isfile(destination):
                with open(destination, "rb") as handle:
                    current = handle.read()
            if current != data:
                with open(destination, "wb") as handle:
                    handle.write(data)
                print(f"restored {relative}")
        shutil.rmtree(self.directory, ignore_errors=True)


def launch_editor(editor):
    exe = editor if os.path.isabs(editor) else os.path.join(REPO, editor)
    if not os.path.isfile(exe):
        raise RuntimeError(f"no editor at {exe}")
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    open(LOG_PATH, "w").close()
    env = dict(os.environ, ERHE_AI_DRIVER="1")
    process = subprocess.Popen([exe], cwd=REPO, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+) \(pid (\d+)")
    deadline = time.monotonic() + 240.0
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
    process.kill()
    raise RuntimeError("editor did not become ready")


def stop_editor(port, process):
    try:
        mcp(port, "request_exit", {})
    except Exception:  # noqa: BLE001 - the editor may already be gone
        pass
    try:
        process.wait(timeout=60.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10.0)


def visible(port, scene, pose):
    reply = mcp(port, "get_visible_entries", dict({"scene_name": scene}, **pose))
    if not isinstance(reply, dict) or "entries" not in reply:
        raise RuntimeError(f"get_visible_entries: {str(reply)[:500]}")
    return reply


def entry_key(entry):
    return (entry["node_uid"], entry["node_name"], entry["primitive_index"], entry["pixels"])


def run(port, scene, repeats):
    failures = []
    viewport = isolate_viewport(lambda tool, arguments: mcp(port, tool, arguments), scene_name=scene)
    camera = viewport["camera"]
    # The camera looks down -Z with the identity rotation (occlusion_wall_check.py).
    mcp(port, "set_node_transform", {"scene_name": scene, "node_name": camera, "translation": EYE, "rotation_xyzw": [0, 0, 0, 1], "space": "world"})
    box(port, scene, WALL_NAME, [0.0, 3.0, -2.0], [30.0, 12.0, 0.4])
    for name, p in zip(BEHIND_NAMES, BEHIND):
        box(port, scene, name, p, [1.0, 1.0, 1.0])
    for name, p in zip(FRONT_NAMES, FRONT):
        box(port, scene, name, p, [0.4, 0.4, 0.4])

    # Alpha test: opacity 0 with cutoff 0.5 discards every fragment in the
    # color pass, opacity 1 none.
    mcp(port, "create_material", {"scene_name": scene, "name": "visible_alpha_discard", "blending_mode": "alpha_test", "opacity": 0.0, "alpha_cutoff": 0.5})
    mcp(port, "create_material", {"scene_name": scene, "name": "visible_alpha_keep",    "blending_mode": "alpha_test", "opacity": 1.0, "alpha_cutoff": 0.5})
    mcp(port, "create_shape", {"scene_name": scene, "shape": "box", "name": ALPHA_HIDDEN, "size": [0.4, 0.4, 0.4], "position": [-1.2, 2.0, 5.0], "motion_mode": "none", "material_name": "visible_alpha_discard"})
    mcp(port, "create_shape", {"scene_name": scene, "shape": "box", "name": ALPHA_SHOWN,  "size": [0.4, 0.4, 0.4], "position": [ 1.2, 2.0, 5.0], "motion_mode": "none", "material_name": "visible_alpha_keep"})
    # Double-sided: the material's double_sided property.
    mcp(port, "create_material", {"scene_name": scene, "name": "visible_double_sided"})
    mcp(port, "set_item_property", {"scene_name": scene, "item_name": "visible_double_sided", "property": "double_sided", "value": True})
    mcp(port, "create_material", {"scene_name": scene, "name": "visible_single_sided"})
    for name, x, material in ((DOUBLE_SIDED, 1.5, "visible_double_sided"), (SINGLE_SIDED, -1.5, "visible_single_sided")):
        mcp(port, "create_shape", {
            "scene_name": scene, "shape": "rectangle", "name": name, "width": 0.8, "height": 0.6,
            "front_face": True, "back_face": False, "position": [x, 2.5, -9.0], "motion_mode": "none", "material_name": material
        })
    time.sleep(1.0) # the property write is applied on the next frame

    # The viewport camera's pose, as set above.
    front_pose = {"position": EYE, "orientation": [0.0, 0.0, 0.0, 1.0]}
    replies = [visible(port, scene, front_pose) for _ in range(repeats)]
    first = replies[0]
    first_set = [entry_key(e) for e in first["entries"]]
    for i, reply in enumerate(replies[1:], start=2):
        keys = [entry_key(e) for e in reply["entries"]]
        if keys != first_set:
            failures.append(f"front call {i} differs from call 1: {sorted(set(keys) ^ set(first_set))[:8]}")
        if reply["covered_pixels"] != first["covered_pixels"]:
            failures.append(f"front call {i} covered_pixels {reply['covered_pixels']} != {first['covered_pixels']}")
    frames = [r["frame_index"] for r in replies]
    names = {e["node_name"] for e in first["entries"]}
    expected_in  = {WALL_NAME} | set(FRONT_NAMES)
    expected_out = set(BEHIND_NAMES)
    if not expected_in <= names:
        failures.append(f"front: missing {sorted(expected_in - names)}")
    if names & expected_out:
        failures.append(f"front: behind the wall but seen {sorted(names & expected_out)}")
    if ALPHA_SHOWN not in names:
        failures.append(f"front: alpha-tested {ALPHA_SHOWN} (opacity above the cutoff) missing")
    if ALPHA_HIDDEN in names:
        failures.append(f"front: alpha-tested {ALPHA_HIDDEN} (opacity under the cutoff) seen")
    print(
        f"front: {repeats} calls, frames {frames[0]}..{frames[-1]}, {first['width']} x {first['height']}, "
        f"entries {first['entry_count']}, covered_pixels {first['covered_pixels']}, unresolved_pixels {first['unresolved_pixels']}"
    )
    for e in first["entries"]:
        print(f"  {e['node_name']!r} uid {e['node_uid']!r} primitive {e['primitive_index']} pixels {e['pixels']}")

    back = visible(port, scene, {"position": BACK_EYE, "target": BACK_TARGET})
    back_names = {e["node_name"] for e in back["entries"]}
    if not expected_out <= back_names:
        failures.append(f"back: missing {sorted(expected_out - back_names)}")
    if DOUBLE_SIDED not in back_names:
        failures.append(f"back: {DOUBLE_SIDED} (back face of a double-sided material) missing")
    if SINGLE_SIDED in back_names:
        failures.append(f"back: {SINGLE_SIDED} (back face of a single-sided material) seen")
    print(
        f"back: entries {back['entry_count']}, covered_pixels {back['covered_pixels']}, "
        f"unresolved_pixels {back['unresolved_pixels']}, behind boxes seen {len(expected_out & back_names)}/{len(expected_out)}"
    )
    print(
        f"materials: {ALPHA_SHOWN} {'in' if ALPHA_SHOWN in names else 'OUT'}, {ALPHA_HIDDEN} {'IN' if ALPHA_HIDDEN in names else 'out'}, "
        f"{DOUBLE_SIDED} {'in' if DOUBLE_SIDED in back_names else 'OUT'}, {SINGLE_SIDED} {'IN' if SINGLE_SIDED in back_names else 'out'}"
    )
    for e in back["entries"]:
        print(f"  {e['node_name']!r} uid {e['node_uid']!r} primitive {e['primitive_index']} pixels {e['pixels']}")
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", default=os.path.join("build_xcode_metal", "bin", "Debug", "editor"))
    parser.add_argument("--scene", default="Default Scene")
    parser.add_argument("--repeats", type=int, default=10)
    args = parser.parse_args()

    backup = Config_backup()
    process = None
    port = None
    try:
        process, port = launch_editor(args.editor)
        print(f"editor pid {process.pid} on port {port}")
        failures = run(port, args.scene, args.repeats)
    finally:
        if process is not None:
            stop_editor(port, process)
        backup.restore()
    print("PASS" if not failures else "FAIL: " + "; ".join(failures))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
