#!/usr/bin/env python3
"""Verify the mesh modeling selection commands (doc/plans/mesh_modeling.md
section 4.2, doc/editor/mesh_component_selection.md) over MCP.

Launches a headless editor (ERHE_AI_DRIVER=1), creates a box and checks the
flush rules, the mode conversions (flush and expand), invert, select all,
select none, select linked and the vertex / edge mode region (box and brush)
select against the box's known counts (8 vertices, 12 edges, 6 facets).

Usage:
    py -3 scripts/mesh_modeling_verify.py [--editor <path to editor.exe>]

Exit code 0 when every check passes; otherwise the failing checks are named.
"""

import argparse
import math
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from erhe_mcp import McpClient, check_true, report  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_PATH = os.path.join(REPO_ROOT, "logs", "log.txt")
DEFAULT_EDITOR = os.path.join("build_vs2026_vulkan_headless", "bin", "Debug", "editor.exe")
BOX = "mm_box"


def launch_editor(editor_exe):
    exe = editor_exe if os.path.isabs(editor_exe) else os.path.join(REPO_ROOT, editor_exe)
    if not os.path.isfile(exe):
        raise RuntimeError(f"no editor at {exe}")
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    open(LOG_PATH, "w").close()
    env = dict(os.environ, ERHE_AI_DRIVER="1")
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    process = subprocess.Popen([exe], cwd=REPO_ROOT, env=env, creationflags=flags,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 180.0
    pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+)")
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"editor exited with {process.returncode} during startup; see logs/log.txt")
        try:
            with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            match = pattern.search(text)
            if match and ("Main loop: completed frame 12" in text):
                return process, int(match.group(1))
        except OSError:
            pass
        time.sleep(1.0)
    process.kill()
    raise RuntimeError("editor did not become ready")


class Editor:
    def __init__(self, client, scene):
        self.c = client
        self.scene = scene

    def call(self, tool, args=None):
        return self.c.call(tool, args or {})

    def advance(self, frames=2):
        for _ in range(frames):
            self.call("advance_time", {"seconds": 0.016})

    def key(self, key, modifiers):
        self.call("key_press", {"key": key, "modifiers": modifiers})
        self.advance(2)

    def select(self, **kwargs):
        args = {"scene_name": self.scene, "node_name": BOX}
        args.update(kwargs)
        return self.call("select_mesh_components", args)

    def counts(self, selection=None):
        """(vertices, edges, facets) of the box's entry in the selection."""
        if selection is None:
            selection = self.call("get_mesh_component_selection")
        for entry in selection.get("entries", []):
            if (entry.get("node_name") == BOX) and entry.get("live", False):
                return (len(entry["vertices"]), len(entry["edges"]), len(entry["facets"]))
        return (0, 0, 0)


def expect(label, actual, expected):
    check_true(label, actual == expected, f"got {actual}, expected {expected}")


def quaternion_from_columns(m):
    """Unit quaternion [x, y, z, w] of the rotation part of a column-major 4x4."""
    # r[row][column]
    r = [[m[(column * 4) + row] for column in range(3)] for row in range(3)]
    for column in range(3):
        length = math.sqrt(sum(r[row][column] ** 2 for row in range(3)))
        for row in range(3):
            r[row][column] /= length
    trace = r[0][0] + r[1][1] + r[2][2]
    if trace > 0.0:
        k = 0.5 / math.sqrt(trace + 1.0)
        q = [(r[2][1] - r[1][2]) * k, (r[0][2] - r[2][0]) * k, (r[1][0] - r[0][1]) * k, 0.25 / k]
    elif (r[0][0] > r[1][1]) and (r[0][0] > r[2][2]):
        k = 2.0 * math.sqrt(1.0 + r[0][0] - r[1][1] - r[2][2])
        q = [0.25 * k, (r[0][1] + r[1][0]) / k, (r[0][2] + r[2][0]) / k, (r[2][1] - r[1][2]) / k]
    elif r[1][1] > r[2][2]:
        k = 2.0 * math.sqrt(1.0 + r[1][1] - r[0][0] - r[2][2])
        q = [(r[0][1] + r[1][0]) / k, 0.25 * k, (r[1][2] + r[2][1]) / k, (r[0][2] - r[2][0]) / k]
    else:
        k = 2.0 * math.sqrt(1.0 + r[2][2] - r[0][0] - r[1][1])
        q = [(r[0][2] + r[2][0]) / k, (r[1][2] + r[2][1]) / k, 0.25 * k, (r[1][0] - r[0][1]) / k]
    length = math.sqrt(sum(c * c for c in q))
    return [c / length for c in q]


def run_region_select(e):
    """Vertex / edge mode box and brush select through debug_region_select.

    The box is placed in front of the first viewport's camera with the
    camera's orientation, so its local -X vertices (one face) project to the
    left half of the viewport and its +X vertices to the right half.
    """
    viewport = e.call("get_viewports")["viewports"][0]
    m = viewport["camera_world_from_camera"]
    position = [m[12], m[13], m[14]]
    forward = [-m[8], -m[9], -m[10]]
    length = math.sqrt(sum(c * c for c in forward))
    distance = 5.0
    translation = [position[i] + (forward[i] / length) * distance for i in range(3)]
    e.call("set_node_transform", {
        "scene_name": e.scene, "node_name": BOX,
        "translation": translation, "rotation_xyzw": quaternion_from_columns(m)
    })
    e.advance(2)

    width = int(viewport["width"])
    height = int(viewport["height"])
    whole = {"x": 0, "y": 0, "width": width, "height": height}
    left_half = {"x": 0, "y": 0, "width": width // 2, "height": height}

    def region(mode, rect, **kwargs):
        args = {"mode": mode}
        args.update(rect)
        args.update(kwargs)
        e.call("debug_region_select", args)
        e.advance(2)
        return e.counts()

    e.call("clear_mesh_component_selection")
    expect("vertex mode box over the viewport -> 8 vertices, 12 edges, 6 facets", region("vertex", whole), (8, 12, 6))
    expect("vertex mode box over the left half -> 4 vertices, 4 edges, 1 facet", region("vertex", left_half), (4, 4, 1))
    expect("edge mode box over the viewport -> 12 edges", region("edge", whole), (8, 12, 6))
    expect("edge mode box over the left half -> 4 edges (both endpoints inside)", region("edge", left_half), (4, 4, 1))
    region("vertex", whole)
    expect("vertex mode Ctrl box (subtract) of the left half -> 4 vertices remain",
           region("vertex", left_half, replace=False, subtract=True), (4, 4, 1))
    region("edge", whole)
    expect("edge mode Ctrl box (subtract) of the left half -> the other 8 edges remain",
           region("edge", left_half, replace=False, subtract=True)[1], 8)
    e.call("clear_mesh_component_selection")
    expect("vertex mode Shift box (extend) of the left half onto nothing -> 4 vertices",
           region("vertex", left_half, replace=False), (4, 4, 1))
    brush = {"x": 0, "y": 0, "width": width, "height": height}
    expect("vertex mode brush covering the viewport -> 8 vertices",
           region("vertex", brush, is_brush=True, brush_radius=float(max(width, height)))[0], 8)
    expect("vertex mode brush of radius 1 at the viewport centre -> nothing (box vertices lie off-centre)",
           region("vertex", brush, is_brush=True, brush_radius=1.0), (0, 0, 0))
    e.call("clear_mesh_component_selection")


def run(e):
    # First, while nothing is object-selected: it moves the box.
    run_region_select(e)

    # Flush: a facet selects its edges and vertices.
    e.select(mode="face", facets=[0])
    expect("face mode, facet 0 -> 4 vertices, 4 edges, 1 facet", e.counts(), (4, 4, 1))

    # Mode switch (flush): vertex mode keeps the vertices, derives the rest.
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    expect("to vertex mode (flush) -> 4 vertices, 4 edges, 1 facet", e.counts(), (4, 4, 1))

    # Mode switch (expand) going up: every edge touching the one vertex.
    e.select(mode="vertex", vertices=[0])
    expect("vertex mode, vertex 0 -> 1 vertex, 0 edges, 0 facets", e.counts(), (1, 0, 0))
    e.call("set_mesh_component_mode", {"mode": "edge", "conversion": "expand"})
    vertices, edges, facets = e.counts()
    expect("to edge mode (expand) from one vertex -> 3 edges", edges, 3)
    expect("expanded edges select their 4 vertices, no facet", (vertices, facets), (4, 0))

    # Mode switch (expand) going down: only elements surrounded by the selection.
    e.select(mode="face", facets=[0])
    e.call("set_mesh_component_mode", {"mode": "vertex", "conversion": "expand"})
    expect("face -> vertex (expand) keeps no vertex of a lone facet", e.counts(), (0, 0, 0))

    # Invert in face mode.
    e.select(mode="face", facets=[0])
    selection = e.call("invert_mesh_selection")
    expect("invert in face mode -> 5 facets", e.counts(selection)[2], 5)
    expect("inverted facets flush to 8 vertices, 12 edges", e.counts(selection)[0:2], (8, 12))

    # Select all / none.
    selection = e.call("select_all_mesh_components", {"scene_name": e.scene, "node_name": BOX})
    expect("select all (node target) -> 8 vertices, 12 edges, 6 facets", e.counts(selection), (8, 12, 6))
    e.call("clear_mesh_component_selection")
    e.call("select_items", {"scene_name": e.scene, "paths": [BOX]})
    e.advance()
    selection = e.call("select_all_mesh_components")
    expect("select all (object selection target) -> 6 facets", e.counts(selection)[2], 6)
    e.call("clear_mesh_component_selection")
    expect("select none -> nothing selected", e.counts(), (0, 0, 0))

    # Select linked from one vertex.
    e.select(mode="vertex", vertices=[0])
    selection = e.call("select_linked_mesh_components", {"from_selection": True})
    expect("select linked from one vertex -> 8 vertices, 12 edges, 6 facets", e.counts(selection), (8, 12, 6))

    # Select linked from a seed list (the 'under the cursor' form), face mode.
    e.select(mode="face", facets=[])
    selection = e.call("select_linked_mesh_components", {"scene_name": e.scene, "node_name": BOX, "vertices": [0]})
    expect("select linked from a seed vertex in face mode -> 6 facets", e.counts(selection), (8, 12, 6))

    e.call("clear_mesh_component_selection")

    # The keys, with the pointer over a viewport (keyboard commands need it
    # there, and it is where the fly camera's plain A would also match).
    viewport = e.call("get_viewports")["viewports"][0]
    x = viewport["x"] + (viewport["width"] / 2.0)
    y = viewport["y"] + (viewport["height"] / 2.0)
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    e.call("set_mesh_component_mode", {"mode": "face"})
    e.key("a", ["ctrl"])
    expect("Ctrl+A selects all (not the fly camera's A)", e.counts(), (8, 12, 6))
    e.key("a", ["menu"])
    expect("Alt+A selects none", e.counts(), (0, 0, 0))
    e.select(mode="face", facets=[0])
    e.key("i", ["ctrl"])
    expect("Ctrl+I inverts", e.counts()[2], 5)
    e.select(mode="face", facets=[0])
    e.key("l", ["ctrl"])
    expect("Ctrl+L selects linked from the selection", e.counts(), (8, 12, 6))

    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=DEFAULT_EDITOR)
    args = parser.parse_args()

    process, port = launch_editor(args.editor)
    try:
        client = McpClient(port)
        scene = client.call("list_scenes")["scenes"][0]["name"]
        e = Editor(client, scene)
        e.call("create_shape", {"scene_name": scene, "shape": "box", "name": BOX, "steps": [0, 0, 0], "motion_mode": "none"})
        e.advance(4)
        run(e)
    finally:
        process.kill()
    return report()


if __name__ == "__main__":
    sys.exit(main())
