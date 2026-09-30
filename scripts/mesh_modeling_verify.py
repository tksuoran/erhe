#!/usr/bin/env python3
"""Verify the mesh modeling selection commands (doc/plans/mesh_modeling.md
section 4.2, doc/editor/mesh_component_selection.md) over MCP.

Launches a headless editor (ERHE_AI_DRIVER=1), creates a box and checks the
flush rules, the mode conversions (flush and expand), invert, select all,
select none, select linked and the vertex / edge mode region (box and brush)
select against the box's known counts (8 vertices, 12 edges, 6 facets). Loop
and ring select are checked on the box, a torus and a one-sided rectangle
(an open mesh), through select_mesh_loop and through Alt / Ctrl+Alt clicks.
Delete and dissolve (section 4.3) are checked on a box subdivided once with
Catmull-Clark and a box with subdivided flat sides: each operation through its
MCP tool and the Delete / Ctrl+X keys, each followed by undo, comparing
get_mesh_geometry_info counts. Merge (section 4.4) is checked on the
Catmull-Clark box (at center, collapse, at position with the survivor's
position read back, the M key) and merge by distance on the plain box, each followed by
undo. Subdivide edges (section 4.5) is checked on the plain box: two opposite
edges of one face with one and two cuts, and every edge with one cut (grid
fill), each with the selection afterwards (the inner edges) and undo. Edge
and vertex slide (section 4.6) are checked on the Catmull-Clark box: the
middle edge loop slid halfway to either side, even and even + flipped, the
corner texcoords re-interpolated at the slid positions, a refused selection,
a vertex slid onto a neighbour, each followed by undo, and the G key slide in
a viewport cancelled (MCP cancel_component_edit and Escape, positions back and
no undo entry) and confirmed (Enter, one undo entry). Loop cut (section 4.5,
doc/editor/mesh_modeling.md) is checked through loop_cut_mesh on the plain box
(one and two cuts, a slide factor), a ring closing across a quad on the
Catmull-Clark box and the single edge case on an octahedron, each with the
selection afterwards and one undo step, and through Ctrl+R in a viewport:
Escape in the preview, wheel + click + move + click, and Escape in the slide.
Operations whose result has no facet (merge by distance of the whole
box, delete of every face) are checked to leave an empty mesh the editor keeps
rendering, followed by undo. The editor's stderr (where a crash stack goes) is written to
logs/editor_stderr.txt.

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
STDERR_PATH = os.path.join(REPO_ROOT, "logs", "editor_stderr.txt")
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
    # The cpptrace crash stack goes to stderr; keep it for the post-mortem.
    stderr_file = open(STDERR_PATH, "w", encoding="utf-8")
    process = subprocess.Popen([exe], cwd=REPO_ROOT, env=env, creationflags=flags,
                               stdout=subprocess.DEVNULL, stderr=stderr_file)
    stderr_file.close()
    deadline = time.monotonic() + 180.0
    pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+)")
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"editor exited with {process.returncode} during startup; see logs/log.txt and logs/editor_stderr.txt")
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


TORUS = "mm_torus"
RECTANGLE = "mm_rectangle"


def entry_of(selection, node_name):
    """(vertices, edges, facets) lists of node_name's live entry."""
    for entry in selection.get("entries", []):
        if (entry.get("node_name") == node_name) and entry.get("live", False):
            return entry
    return {"vertices": [], "edges": [], "facets": []}


def counts_of(selection, node_name):
    entry = entry_of(selection, node_name)
    return (len(entry["vertices"]), len(entry["edges"]), len(entry["facets"]))


def place_in_front_of_camera(e, node_name, distance=2.0):
    """Move node_name in front of the first viewport's camera, camera-aligned."""
    viewport = e.call("get_viewports")["viewports"][0]
    m = viewport["camera_world_from_camera"]
    position = [m[12], m[13], m[14]]
    forward = [-m[8], -m[9], -m[10]]
    length = math.sqrt(sum(c * c for c in forward))
    translation = [position[i] + (forward[i] / length) * distance for i in range(3)]
    e.call("set_node_transform", {
        "scene_name": e.scene, "node_name": node_name,
        "translation": translation, "rotation_xyzw": quaternion_from_columns(m)
    })
    e.advance(2)
    return viewport


def left_of_centre(viewport):
    """A window point left of the viewport centre, inside a shape placed by
    place_in_front_of_camera, so the nearest edge to it is the shape's left
    edge."""
    x = viewport["x"] + (viewport["width"] * 0.5) - (viewport["height"] * 0.02)
    y = viewport["y"] + (viewport["height"] * 0.5)
    return x, y


def check_pick(e, viewport, node_name, x, y):
    """The click point hovers node_name (pick_at takes viewport pixels)."""
    pick = e.call("pick_at", {"x": x - viewport["x"], "y": y - viewport["y"]})
    node = pick.get("nearest", {}).get("node")
    check_true(f"the click point hovers {node_name}", node == node_name, f"hovers {node}")


def run_loop_select_mcp(e):
    """select_mesh_loop on the box and a torus (doc/plans/mesh_modeling.md
    section 4.2)."""
    def loop(node_name, edge, kind, action="replace", **kwargs):
        args = {"scene_name": e.scene, "node_name": node_name, "edge": edge, "kind": kind, "action": action}
        args.update(kwargs)
        return e.call("select_mesh_loop", args)

    # A cube edge: the edges of facet 0 give valid seeds.
    e.select(mode="face", facets=[0])
    seed = entry_of(e.call("get_mesh_component_selection"), BOX)["edges"][0]
    e.call("set_mesh_component_mode", {"mode": "edge"})
    selection = loop(BOX, seed, "edge_loop")
    expect("box edge loop from a cube edge -> the edge alone (valence-3 corners)",
           (selection["walked"], counts_of(selection, BOX)), (1, (2, 1, 0)))
    selection = loop(BOX, seed, "edge_ring")
    expect("box edge ring from a cube edge -> 4 edges", (selection["walked"], counts_of(selection, BOX)[1]), (4, 4))
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    selection = loop(BOX, seed, "edge_ring")
    expect("box edge ring in vertex mode selects the ring's 8 vertices (flush derives every edge)",
           counts_of(selection, BOX), (8, 12, 6))
    e.call("set_mesh_component_mode", {"mode": "face"})
    selection = loop(BOX, seed, "face_loop")
    expect("box face loop -> 4 facets (flushing to 8 vertices, 12 edges)",
           (selection["walked"], counts_of(selection, BOX)), (4, (8, 12, 4)))
    try:
        loop(BOX, seed, "edge_loop")
        check_true("edge_loop in face mode is refused", False, "no error")
    except RuntimeError:
        check_true("edge_loop in face mode is refused", True)
    e.call("clear_mesh_component_selection")

    # Torus: all quads, valence-4 vertices, closed loops and rings.
    major, minor = 12, 8
    e.call("create_shape", {"scene_name": e.scene, "shape": "torus", "name": TORUS,
                            "major_steps": major, "minor_steps": minor, "motion_mode": "none"})
    e.advance(4)
    info = e.call("get_mesh_geometry_info", {"scene_name": e.scene, "node_name": TORUS})
    counts = info.get("counts", info)
    torus_ok = (counts.get("vertices") == major * minor) and (counts.get("facets") == major * minor)
    check_true("torus is welded: 96 vertices, 96 facets", torus_ok, f"got {counts}")
    if torus_ok:
        e.call("set_mesh_component_mode", {"mode": "face"})
        e.call("select_mesh_components", {"scene_name": e.scene, "node_name": TORUS, "facets": [0]})
        seed = entry_of(e.call("get_mesh_component_selection"), TORUS)["edges"][0]
        e.call("set_mesh_component_mode", {"mode": "edge"})
        selection = loop(TORUS, seed, "edge_loop")
        loop_count = selection["walked"]
        loop_edges = entry_of(selection, TORUS)["edges"]
        check_true("torus edge loop is a closed circle (8 or 12 edges)", loop_count in (major, minor), f"got {loop_count}")
        selection = loop(TORUS, seed, "edge_ring")
        ring_count = selection["walked"]
        expect("torus edge ring crosses the other circle direction (loop + ring = 20)", loop_count + ring_count, major + minor)
        # A ring edge sharing no vertex with the seed lies on a parallel,
        # disjoint loop.
        ring_edges = entry_of(selection, TORUS)["edges"]
        other = next(edge for edge in ring_edges if not (set(edge) & set(seed)))
        selection = loop(TORUS, seed, "edge_loop")
        expect("torus loop (replace) -> loop edges", counts_of(selection, TORUS)[1], loop_count)
        selection = loop(TORUS, other, "edge_loop", action="extend")
        expect("torus extend with a parallel loop -> twice the edges", counts_of(selection, TORUS)[1], 2 * loop_count)
        selection = loop(TORUS, other, "edge_loop", action="deselect")
        expect("torus deselect the parallel loop -> back to the first loop",
               sorted(map(tuple, entry_of(selection, TORUS)["edges"])), sorted(map(tuple, loop_edges)))
        e.call("set_mesh_component_mode", {"mode": "face"})
        selection = loop(TORUS, seed, "face_loop")
        check_true("torus face loop is a closed band (8 or 12 facets)", selection["walked"] in (major, minor),
                   f"got {selection['walked']}")
        e.call("clear_mesh_component_selection")
    # Out of the way of the click tests.
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": TORUS, "translation": [0.0, -100.0, 0.0]})
    e.advance(2)


def run_loop_select_clicks(e):
    """Alt+click / Ctrl+Alt+click / Shift on the box in front of the camera,
    then the boundary cycle on a one-sided rectangle."""
    viewport = place_in_front_of_camera(e, BOX)
    x, y = left_of_centre(viewport)
    check_pick(e, viewport, BOX, x, y)

    def click(modifiers):
        e.call("mouse_click", {"x": x, "y": y, "modifiers": modifiers})
        e.advance(2)
        return e.call("get_mesh_component_selection")

    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "edge"})
    expect("Alt+click on a cube edge (edge mode) -> the edge alone", counts_of(click(["menu"]), BOX), (2, 1, 0))
    expect("Ctrl+Alt+click on a cube edge -> the 4-edge ring", counts_of(click(["ctrl", "menu"]), BOX)[1], 4)
    expect("Shift+Ctrl+Alt+click on the selected ring -> deselects it", counts_of(click(["shift", "ctrl", "menu"]), BOX)[1], 0)
    expect("Shift+Ctrl+Alt+click again -> extends with the ring", counts_of(click(["shift", "ctrl", "menu"]), BOX)[1], 4)
    e.call("set_mesh_component_mode", {"mode": "face"})
    e.call("clear_mesh_component_selection")
    expect("Alt+click in face mode -> the 4-facet face loop", counts_of(click(["menu"]), BOX)[2], 4)
    expect("Ctrl+Alt+click in face mode -> the face loop too", counts_of(click(["ctrl", "menu"]), BOX)[2], 4)
    e.call("clear_mesh_component_selection")

    # An open mesh: a one-sided rectangle, 4 boundary edges with valence-2
    # corners. Box out of the way first.
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": BOX, "translation": [0.0, 100.0, 0.0]})
    e.call("create_shape", {"scene_name": e.scene, "shape": "rectangle", "name": RECTANGLE,
                            "back_face": False, "motion_mode": "none"})
    e.advance(4)
    viewport = place_in_front_of_camera(e, RECTANGLE)
    x, y = left_of_centre(viewport)
    check_pick(e, viewport, RECTANGLE, x, y)
    e.call("set_mesh_component_mode", {"mode": "edge"})
    selection = click(["menu"])
    expect("Alt+click on a boundary edge -> stops at the outer corners (1 edge)", counts_of(selection, RECTANGLE)[1], 1)
    seed = entry_of(selection, RECTANGLE)["edges"][0] if counts_of(selection, RECTANGLE)[1] == 1 else [0, 1]
    expect("Alt+click again on the selected boundary loop -> the whole boundary (4 edges)",
           counts_of(click(["menu"]), RECTANGLE)[1], 4)
    expect("Alt+click once more -> back to the loop (1 edge)", counts_of(click(["menu"]), RECTANGLE)[1], 1)
    selection = e.call("select_mesh_loop", {"scene_name": e.scene, "node_name": RECTANGLE, "edge": seed, "kind": "boundary_loop"})
    expect("select_mesh_loop boundary_loop on the rectangle -> 4 edges", (selection["walked"], counts_of(selection, RECTANGLE)[1]), (4, 4))
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})


def run_transform_in_component_mode(e):
    """A node transform while a mesh component mode is active and the box is
    object-selected with no component selected (the gizmo is owned by the
    empty component selection, so the node-touched refresh must not run
    against the emptied node entries)."""
    e.call("clear_mesh_component_selection")
    e.call("select_items", {"scene_name": e.scene, "paths": [BOX]})
    e.advance()
    e.call("set_mesh_component_mode", {"mode": "face"})
    e.advance()
    state = e.call("get_transform_state")
    expect("face mode, box object-selected, no facets -> gizmo unanchored, no node entries",
           (state["component_mode"], state["selected_node_count"]), (False, 0))
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": BOX, "translation": [0.0, 1.0, 0.0]})
    e.advance(2)
    selection = e.call("get_mesh_component_selection")
    expect("set_node_transform in face mode keeps the editor alive (empty component selection)",
           (selection.get("mode"), e.counts(selection)), ("face", (0, 0, 0)))
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.advance()
    expect("back to object mode -> the node gizmo drives the box again",
           e.call("get_transform_state")["selected_node_count"], 1)
    e.call("select_items", {"scene_name": e.scene, "paths": []})
    e.advance()


CC_BOX = "mm_cc_box"
FLAT_BOX = "mm_flat_box"


def wait_idle(e, tries=600):
    """Advance frames until no async mesh operation is pending, running or
    queued on the operation stack."""
    keys = ["pending", "running", "queued_operations", "pending_scene_commits"]
    for _ in range(tries):
        e.advance(1)
        status = e.call("get_async_status")
        if all(int(status.get(key, 0)) == 0 for key in keys):
            e.advance(1)
            return True
        time.sleep(0.05)
    return False


def geometry_counts(e, node_name):
    """(vertices, edges, facets) of node_name's first primitive."""
    info = e.call("get_mesh_geometry_info", {"scene_name": e.scene, "node_name": node_name})
    counts = info.get("counts", info)
    return (counts["vertices"], counts["edges"], counts["facets"])


def select_on(e, node_name, **kwargs):
    args = {"scene_name": e.scene, "node_name": node_name}
    args.update(kwargs)
    return e.call("select_mesh_components", args)


def undo_and_check(e, label, node_name, expected):
    e.call("undo")
    wait_idle(e)
    expect(f"{label}: undo restores the counts", geometry_counts(e, node_name), expected)


def run_delete_dissolve(e):
    """Delete and dissolve (doc/plans/mesh_modeling.md section 4.3)."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("create_shape", {"scene_name": e.scene, "shape": "box", "name": CC_BOX, "steps": [0, 0, 0], "motion_mode": "none"})
    e.advance(4)
    e.call("catmull_clark", {"scene_name": e.scene, "node_name": CC_BOX})
    wait_idle(e)
    base = geometry_counts(e, CC_BOX)
    expect("Catmull-Clark box -> 26 vertices, 48 edges, 24 facets", base, (26, 48, 24))
    v, ed, f = base

    # Delete faces (face mode, MCP, context by mode).
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, CC_BOX, facets=[0])
    result = e.call("delete_mesh_components")
    expect("delete_mesh_components in face mode -> context by mode", result.get("queued"), True)
    wait_idle(e)
    expect("delete one facet -> facets minus one, edges and vertices kept (all still used)",
           geometry_counts(e, CC_BOX), (v, ed, f - 1))
    undo_and_check(e, "delete faces", CC_BOX, base)

    # A context whose mode does not match is refused.
    select_on(e, CC_BOX, facets=[0])
    try:
        e.call("delete_mesh_components", {"context": "vertices"})
        check_true("delete context vertices in face mode is refused", False, "no error")
    except RuntimeError:
        check_true("delete context vertices in face mode is refused", True)

    # Dissolve one interior edge (edge mode): its two quads join into a hexagon.
    seed = entry_of(e.call("get_mesh_component_selection"), CC_BOX)["edges"][0]
    e.call("set_mesh_component_mode", {"mode": "edge"})
    select_on(e, CC_BOX, edges=[seed])
    e.call("dissolve_mesh_components", {"kind": "edges", "dissolve_vertices": False})
    wait_idle(e)
    expect("dissolve one edge (dissolve_vertices off) -> one hexagon: facets minus one, edges minus one",
           geometry_counts(e, CC_BOX), (v, ed - 1, f - 1))
    undo_and_check(e, "dissolve edges", CC_BOX, base)

    # Dissolve one vertex (vertex mode): its facets join. Its valence comes
    # from an expand to edge mode.
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, CC_BOX, vertices=[0])
    e.call("set_mesh_component_mode", {"mode": "edge", "conversion": "expand"})
    valence = counts_of(e.call("get_mesh_component_selection"), CC_BOX)[1]
    check_true("vertex 0 has valence 3 or 4", valence in (3, 4), f"got {valence}")
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, CC_BOX, vertices=[0])
    e.call("dissolve_mesh_components", {})
    wait_idle(e)
    expect(f"dissolve a valence-{valence} vertex -> its {valence} facets join, it and its edges go",
           geometry_counts(e, CC_BOX), (v - 1, ed - valence, f - (valence - 1)))
    undo_and_check(e, "dissolve vertices", CC_BOX, base)

    # Dissolve faces: two adjacent facets (those of the seed edge) join.
    e.call("set_mesh_component_mode", {"mode": "edge"})
    select_on(e, CC_BOX, edges=[seed])
    e.call("set_mesh_component_mode", {"mode": "face", "conversion": "expand"})
    facets = entry_of(e.call("get_mesh_component_selection"), CC_BOX)["facets"]
    expect("expand from the seed edge -> its 2 facets", len(facets), 2)
    e.call("dissolve_mesh_components", {"kind": "faces"})
    wait_idle(e)
    expect("dissolve two adjacent facets -> facets minus one, edges minus one",
           geometry_counts(e, CC_BOX), (v, ed - 1, f - 1))
    undo_and_check(e, "dissolve faces", CC_BOX, base)

    # The keys, with the pointer over a viewport (and the box in front of the
    # camera, so the keys reach the viewport).
    viewport = place_in_front_of_camera(e, CC_BOX, distance=5.0)
    x = viewport["x"] + (viewport["width"] / 2.0)
    y = viewport["y"] + (viewport["height"] / 2.0)
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    # Object-selected too: Selection.delete / Selection.cut would remove the
    # node if they consumed the keys.
    e.call("select_items", {"scene_name": e.scene, "paths": [CC_BOX]})
    e.advance()
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, CC_BOX, facets=[0])
    e.key("delete", [])
    wait_idle(e)
    expect("Delete key in face mode deletes the facet, not the node",
           geometry_counts(e, CC_BOX), (v, ed, f - 1))
    undo_and_check(e, "Delete key", CC_BOX, base)
    e.call("set_mesh_component_mode", {"mode": "edge"})
    select_on(e, CC_BOX, edges=[seed])
    e.key("x", ["ctrl"])
    wait_idle(e)
    after = geometry_counts(e, CC_BOX)
    check_true("Ctrl+X in edge mode dissolves the edge (facets minus one)", after[2] == f - 1, f"got {after}")
    undo_and_check(e, "Ctrl+X", CC_BOX, base)
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("select_items", {"scene_name": e.scene, "paths": []})
    e.advance()
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": CC_BOX, "translation": [0.0, -200.0, 0.0]})
    e.advance(2)

    # Limited dissolve: the plain box is unchanged (90 degree dihedrals); a box
    # with subdivided flat sides goes back to the plain box.
    e.call("dissolve_limited", {"scene_name": e.scene, "node_name": BOX})
    wait_idle(e)
    expect("limited dissolve on the plain box changes nothing", geometry_counts(e, BOX), (8, 12, 6))
    e.call("create_shape", {"scene_name": e.scene, "shape": "box", "name": FLAT_BOX, "steps": [1, 1, 1], "motion_mode": "none"})
    e.advance(4)
    flat = geometry_counts(e, FLAT_BOX)
    check_true("stepped box has subdivided flat sides", flat[2] > 6, f"got {flat}")
    e.call("dissolve_limited", {"scene_name": e.scene, "node_name": FLAT_BOX})
    wait_idle(e)
    expect("limited dissolve on the stepped box -> the plain box (8, 12, 6)", geometry_counts(e, FLAT_BOX), (8, 12, 6))
    undo_and_check(e, "limited dissolve", FLAT_BOX, flat)


def vertex_position(e, node_name, vertex):
    elements = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": node_name, "domain": "vertex", "indices": [vertex]
    })["elements"]
    position = elements[0]["position"]
    if isinstance(position, dict):
        return [position["x"], position["y"], position["z"]]
    return list(position)


def run_merge(e):
    """Merge (doc/plans/mesh_modeling.md section 4.4)."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    base = geometry_counts(e, CC_BOX)
    expect("merge: Catmull-Clark box at its base counts", base, (26, 48, 24))
    v, ed, f = base

    # Two adjacent vertices: the endpoints of an edge of facet 0.
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, CC_BOX, facets=[0])
    seed = entry_of(e.call("get_mesh_component_selection"), CC_BOX)["edges"][0]
    pair = sorted(seed)
    # Merging an edge's two endpoints collapses the edge; its two quads become
    # triangles, so the facet count stays.
    merged = (v - 1, ed - 1, f)

    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, CC_BOX, vertices=pair)
    result = e.call("merge_mesh_vertices", {})
    expect("merge_mesh_vertices default type -> at_center", result.get("type"), "at_center")
    wait_idle(e)
    expect("merge at center of two adjacent vertices -> (v-1, e-1, f)", geometry_counts(e, CC_BOX), merged)
    after = entry_of(e.call("get_mesh_component_selection"), CC_BOX)
    expect("merge at center: the survivor is the selection", after["vertices"], [pair[0]])
    undo_and_check(e, "merge at center", CC_BOX, base)

    e.call("set_mesh_component_mode", {"mode": "edge"})
    select_on(e, CC_BOX, edges=[seed])
    e.call("merge_mesh_vertices", {"type": "collapse"})
    wait_idle(e)
    expect("collapse one edge -> (v-1, e-1, f)", geometry_counts(e, CC_BOX), merged)
    undo_and_check(e, "collapse", CC_BOX, base)

    target = [0.125, 0.25, 0.375]
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, CC_BOX, vertices=pair)
    e.call("merge_mesh_vertices", {"type": "at_position", "position": target})
    wait_idle(e)
    expect("merge at position -> (v-1, e-1, f)", geometry_counts(e, CC_BOX), merged)
    position = vertex_position(e, CC_BOX, pair[0])
    check_true("merge at position: the survivor is at the position",
               all(abs(a - b) < 1e-5 for a, b in zip(position, target)), f"got {position}")
    undo_and_check(e, "merge at position", CC_BOX, base)

    # at_position without a position is refused.
    select_on(e, CC_BOX, vertices=pair)
    try:
        e.call("merge_mesh_vertices", {"type": "at_position"})
        check_true("merge at_position without position is refused", False, "no error")
    except RuntimeError:
        check_true("merge at_position without position is refused", True)

    # The M key (merge at center), with the pointer over a viewport.
    viewport = place_in_front_of_camera(e, CC_BOX, distance=5.0)
    x = viewport["x"] + (viewport["width"] / 2.0)
    y = viewport["y"] + (viewport["height"] / 2.0)
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    select_on(e, CC_BOX, vertices=pair)
    e.key("m", [])
    wait_idle(e)
    expect("M key in vertex mode merges at center -> (v-1, e-1, f)", geometry_counts(e, CC_BOX), merged)
    undo_and_check(e, "M key", CC_BOX, base)
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": CC_BOX, "translation": [0.0, -200.0, 0.0]})
    e.advance(2)

    # Merge by distance on the whole plain box: nothing is closer than 1e-4.
    e.call("merge_mesh_by_distance", {"scene_name": e.scene, "node_name": BOX})
    wait_idle(e)
    expect("merge by distance 1e-4 on the plain box changes nothing", geometry_counts(e, BOX), (8, 12, 6))

    # The four vertices of one box face, with a threshold above the face
    # diagonal: they form one cluster and collapse to one vertex. The face
    # goes, its four neighbours become triangles, the four side edges stay:
    # (8 - 3, 12 - 4, 6 - 1). A threshold that merges the whole box is
    # checked by run_empty_results().
    face_vertices = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": BOX, "domain": "facet", "indices": [0]
    })["elements"][0]["vertices"]
    corners = [vertex_position(e, BOX, vertex) for vertex in face_vertices]
    face_diagonal = max(sum((a - b) ** 2 for a, b in zip(p, q)) ** 0.5 for p in corners for q in corners)
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, BOX, vertices=face_vertices)
    e.call("merge_mesh_by_distance", {"scene_name": e.scene, "node_name": BOX, "threshold": face_diagonal * 1.01})
    wait_idle(e)
    expect("merge by distance of one face's vertices above its diagonal -> (5, 8, 5)", geometry_counts(e, BOX), (5, 8, 5))
    undo_and_check(e, "merge by distance", BOX, (8, 12, 6))
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})


def run_subdivide_edges(e):
    """Subdivide edges (doc/plans/mesh_modeling.md section 4.5) on the plain box."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    base = (8, 12, 6)
    expect("subdivide: plain box at its base counts", geometry_counts(e, BOX), base)
    face_vertices = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": BOX, "domain": "facet", "indices": [0]
    })["elements"][0]["vertices"]
    opposite = [sorted([face_vertices[0], face_vertices[1]]), sorted([face_vertices[2], face_vertices[3]])]

    # Two opposite edges of one face, one cut: the face becomes two quads
    # joined by one inner edge, its two neighbours get a boundary vertex:
    # (8 + 2, 12 + 2 + 1, 6 + 1).
    for cuts, expected in ((1, (10, 15, 7)), (2, (12, 18, 8))):
        e.call("set_mesh_component_mode", {"mode": "edge"})
        select_on(e, BOX, edges=opposite)
        result = e.call("subdivide_mesh_edges", {"scene_name": e.scene, "node_name": BOX, "cuts": cuts})
        expect(f"subdivide_mesh_edges cuts {cuts} queued", result.get("queued"), True)
        wait_idle(e)
        expect(f"subdivide two opposite edges, cuts {cuts} -> {expected}", geometry_counts(e, BOX), expected)
        edges = entry_of(e.call("get_mesh_component_selection"), BOX)["edges"]
        expect(f"subdivide cuts {cuts}: the selection is the {cuts} inner edge(s)", len(edges), cuts)
        check_true(f"subdivide cuts {cuts}: the inner edges join new vertices",
                   all(min(edge) >= 8 for edge in edges), str(edges))
        undo_and_check(e, f"subdivide two opposite edges, cuts {cuts}", BOX, base)

    # Every edge, one cut: each face becomes a 3 x 3 vertex grid of four
    # quads around a new centre vertex: (8 + 12 + 6, 24 + 6 * 4, 6 * 4).
    e.call("set_mesh_component_mode", {"mode": "edge"})
    e.call("select_all_mesh_components", {"scene_name": e.scene, "node_name": BOX})
    e.call("subdivide_mesh_edges", {"scene_name": e.scene, "node_name": BOX})
    wait_idle(e)
    expect("subdivide every edge, cuts 1 -> (26, 48, 24)", geometry_counts(e, BOX), (26, 48, 24))
    edges = entry_of(e.call("get_mesh_component_selection"), BOX)["edges"]
    expect("subdivide every edge: the selection is the 24 inner edges", len(edges), 24)
    undo_and_check(e, "subdivide every edge", BOX, base)

    # Without a live selection the tool is refused.
    e.call("clear_mesh_component_selection")
    try:
        e.call("subdivide_mesh_edges", {"scene_name": e.scene, "node_name": BOX})
        check_true("subdivide without a selection is refused", False, "no error")
    except RuntimeError:
        check_true("subdivide without a selection is refused", True)
    e.call("set_mesh_component_mode", {"mode": "object"})


def vertex_positions(e, node_name, vertices):
    """{vertex: [x, y, z]} (mesh-local) of node_name's first primitive."""
    elements = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": node_name, "domain": "vertex", "indices": list(vertices)
    })["elements"]
    result = {}
    for element in elements:
        position = element["position"]
        if isinstance(position, dict):
            position = [position["x"], position["y"], position["z"]]
        result[element["index"]] = list(position)
    return result


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def near(a, b, tolerance=1e-4):
    return distance(a, b) <= tolerance


def corner_texcoord(e, node_name, corner):
    element = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": node_name, "domain": "corner", "indices": [corner]
    })["elements"][0]
    value = element.get("attributes", {}).get("corner_texcoord_0")
    if (not isinstance(value, dict)) or (not value.get("present", False)):
        return None
    return list(value["value"])


def undo_count(e):
    return len(e.call("get_undo_redo_stack")["undo"])


def run_slide(e):
    """Edge slide and vertex slide (doc/plans/mesh_modeling.md section 4.6,
    doc/editor/transform.md "Scalar edits") on the Catmull-Clark box."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("set_node_transform", {
        "scene_name": e.scene, "node_name": CC_BOX,
        "translation": [0.0, -200.0, 0.0], "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
    })
    e.advance(2)
    base = geometry_counts(e, CC_BOX)
    expect("slide: Catmull-Clark box at its base counts", base, (26, 48, 24))
    p = vertex_positions(e, CC_BOX, range(base[0]))

    # The middle edge loop: the edges whose two vertices lie at y = 0.
    e.call("set_mesh_component_mode", {"mode": "edge"})
    e.call("select_all_mesh_components", {"scene_name": e.scene, "node_name": CC_BOX})
    edges = entry_of(e.call("get_mesh_component_selection"), CC_BOX)["edges"]
    middle = sorted(sorted(edge) for edge in edges if abs(p[edge[0]][1]) < 1e-5 and abs(p[edge[1]][1]) < 1e-5)
    expect("the box's middle loop has 8 edges", len(middle), 8)
    loop_vertices = sorted({v for edge in middle for v in edge})
    expect("the middle loop has 8 vertices", len(loop_vertices), 8)
    # Each loop vertex's rails: its neighbours above and below the loop.
    rail = {}
    for v in loop_vertices:
        neighbours = [edge[0] if edge[1] == v else edge[1] for edge in edges if v in edge]
        up = [n for n in neighbours if p[n][1] > 1e-5]
        down = [n for n in neighbours if p[n][1] < -1e-5]
        expect(f"loop vertex {v} has one rail up and one down", (len(up), len(down)), (1, 1))
        rail[v] = {"up": up[0], "down": down[0]}

    def select_loop():
        e.call("set_mesh_component_mode", {"mode": "edge"})
        e.call("select_mesh_loop", {"scene_name": e.scene, "node_name": CC_BOX, "edge": middle[0], "kind": "edge_loop"})
        loop = sorted(sorted(edge) for edge in entry_of(e.call("get_mesh_component_selection"), CC_BOX)["edges"])
        return loop

    expect("select_mesh_loop from a middle edge -> the 8 middle edges", select_loop(), middle)

    def slide(label, **kwargs):
        args = {"kind": "edge"}
        args.update(kwargs)
        result = e.call("slide_mesh_components", args)
        wait_idle(e)
        expect(f"{label}: topology unchanged", geometry_counts(e, CC_BOX), base)
        return result, vertex_positions(e, CC_BOX, loop_vertices)

    def undo_slide(label):
        e.call("undo")
        wait_idle(e)
        after = vertex_positions(e, CC_BOX, loop_vertices)
        check_true(f"{label}: undo restores the positions", all(near(after[v], p[v], 1e-6) for v in loop_vertices),
                   str({v: after[v] for v in loop_vertices[:2]}))

    # Factor 0.5: every loop vertex halfway toward one rail side, all on the same side.
    result, after = slide("edge slide 0.5", factor=0.5)
    expect("edge slide 0.5: 8 slide vertices in 1 loop, 8 moved",
           (result.get("slide_vertices"), result.get("loops"), result.get("moved_vertices")), (8, 1, 8))
    side = "up" if after[loop_vertices[0]][1] > 0.0 else "down"
    other = "down" if side == "up" else "up"
    midpoints = {v: [(a + b) * 0.5 for a, b in zip(p[v], p[rail[v][side]])] for v in loop_vertices}
    check_true(f"edge slide 0.5: every loop vertex at the midpoint of its {side} rail",
               all(near(after[v], midpoints[v]) for v in loop_vertices),
               f"vertex {loop_vertices[0]} at {after[loop_vertices[0]]}, expected {midpoints[loop_vertices[0]]}")

    # Correct UVs: a slid corner of a facet on the slid side takes the
    # texcoord of its new position, halfway along the facet's edge.
    v = loop_vertices[0]
    target = rail[v][side]
    facet_corners = None
    for facet in range(base[2]):
        element = e.call("get_mesh_attribute_values", {
            "scene_name": e.scene, "node_name": CC_BOX, "domain": "facet", "indices": [facet]
        })["elements"][0]
        if (v in element["vertices"]) and (target in element["vertices"]):
            facet_corners = dict(zip(element["vertices"], element["corners"]))
            break
    check_true("a facet holds the loop vertex and its rail end", facet_corners is not None)
    if facet_corners is not None:
        uv_slid = corner_texcoord(e, CC_BOX, facet_corners[v])
        uv_end = corner_texcoord(e, CC_BOX, facet_corners[target])
        undo_slide("edge slide 0.5")
        uv_start = corner_texcoord(e, CC_BOX, facet_corners[v])
        check_true("the Catmull-Clark box has corner texcoords", uv_start is not None and uv_slid is not None)
        if (uv_start is not None) and (uv_slid is not None):
            expected_uv = [(a + b) * 0.5 for a, b in zip(uv_start, uv_end)]
            check_true("edge slide 0.5: the slid corner's texcoord is re-interpolated (halfway along the facet edge)",
                       near(uv_slid, expected_uv, 1e-4), f"got {uv_slid}, expected {expected_uv} (start {uv_start})")
            check_true("undo restores the corner texcoord", near(corner_texcoord(e, CC_BOX, facet_corners[v]), uv_start, 1e-6))
    else:
        undo_slide("edge slide 0.5")

    # Factor -0.5: the other side.
    select_loop()
    _, after = slide("edge slide -0.5", factor=-0.5)
    midpoints = {v: [(a + b) * 0.5 for a, b in zip(p[v], p[rail[v][other]])] for v in loop_vertices}
    check_true(f"edge slide -0.5: every loop vertex at the midpoint of its {other} rail",
               all(near(after[v], midpoints[v]) for v in loop_vertices),
               f"vertex {loop_vertices[0]} at {after[loop_vertices[0]]}, expected {midpoints[loop_vertices[0]]}")
    undo_slide("edge slide -0.5")

    # Even: every vertex moves the same distance (the rails differ in length).
    rail_lengths = sorted(round(distance(p[v], p[rail[v][side]]), 5) for v in loop_vertices)
    check_true("the loop's rails differ in length (even is observable)", rail_lengths[0] != rail_lengths[-1], str(rail_lengths))
    select_loop()
    _, after = slide("edge slide 0.5 even", factor=0.5, even=True)
    moved = [distance(after[v], p[v]) for v in loop_vertices]
    check_true("edge slide 0.5 even: every loop vertex moved the same distance",
               (max(moved) - min(moved)) < 1e-4 and min(moved) > 1e-3, str([round(m, 5) for m in moved]))
    undo_slide("edge slide even")

    # Even + flipped: every vertex ends the same distance from its rail end.
    select_loop()
    _, after = slide("edge slide 0.5 even flipped", factor=0.5, even=True, flipped=True)
    to_end = [distance(after[v], p[rail[v][side]]) for v in loop_vertices]
    check_true("edge slide 0.5 even flipped: every loop vertex ends the same distance from its rail end",
               (max(to_end) - min(to_end)) < 1e-4, str([round(d, 5) for d in to_end]))
    undo_slide("edge slide even flipped")

    # Refused: three selected edges at one vertex.
    corner = min(range(base[0]), key=lambda i: -(p[i][0] + p[i][1] + p[i][2]))
    corner_edges = [edge for edge in edges if corner in edge]
    select_on(e, CC_BOX, edges=corner_edges)
    try:
        e.call("slide_mesh_components", {"kind": "edge", "factor": 0.5})
        check_true("edge slide with a vertex on 3 selected edges is refused", False, "no error")
    except RuntimeError:
        check_true("edge slide with a vertex on 3 selected edges is refused", True)

    # Vertex slide: one vertex onto a neighbour.
    v = loop_vertices[0]
    n = rail[v]["up"]
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, CC_BOX, vertices=[v])
    direction = [b - a for a, b in zip(p[v], p[n])]
    result = e.call("slide_mesh_components", {"kind": "vertex", "factor": 1.0, "direction": direction})
    wait_idle(e)
    expect("vertex slide: 1 slide vertex, 1 moved", (result.get("slide_vertices"), result.get("moved_vertices")), (1, 1))
    position = vertex_positions(e, CC_BOX, [v])[v]
    check_true("vertex slide factor 1 lands on the neighbour", near(position, p[n]), f"got {position}, expected {p[n]}")
    e.call("undo")
    wait_idle(e)
    check_true("vertex slide: undo restores the vertex", near(vertex_positions(e, CC_BOX, [v])[v], p[v], 1e-6))

    # Transform mode edge_slide: the gizmo / numeric translation drives the
    # slide - the translation projected on the first slide vertex's rail is
    # the factor, so every loop vertex moves the same fraction of its rail.
    e.call("set_transform_mode", {"mode": "edge_slide"})
    select_loop()
    e.advance(2)
    anchor = e.call("get_transform_state")["anchor_frame"]["translation"]
    e.call("transform_selection", {"translation": [anchor[0], anchor[1] + 0.05, anchor[2]]})
    wait_idle(e)
    after = vertex_positions(e, CC_BOX, loop_vertices)
    fractions = []
    off_rail = 0.0
    for v in loop_vertices:
        rail_vector = [b - a for a, b in zip(p[v], p[rail[v]["up"]])]
        moved = [b - a for a, b in zip(p[v], after[v])]
        t = sum(a * b for a, b in zip(moved, rail_vector)) / sum(c * c for c in rail_vector)
        fractions.append(t)
        off_rail = max(off_rail, distance(moved, [t * c for c in rail_vector]))
    check_true("transform mode edge_slide: an upward translation slides every loop vertex the same fraction up its rail",
               (min(fractions) > 1e-3) and ((max(fractions) - min(fractions)) < 1e-3) and (off_rail < 1e-4),
               f"fractions {[round(t, 5) for t in fractions]}, off rail {off_rail:.6f}")
    expect("transform mode edge_slide: the undo entry is the edge slide", e.call("get_undo_redo_stack")["undo"][-1]["description"], "Edge Slide")
    expect("transform mode edge_slide: topology unchanged", geometry_counts(e, CC_BOX), base)
    undo_slide("transform mode edge_slide")
    e.call("set_transform_mode", {"mode": "move"})

    # The G key in a viewport: live slide, cancelled and confirmed.
    viewport = place_in_front_of_camera(e, CC_BOX, distance=5.0)
    x = viewport["x"] + (viewport["width"] / 2.0)
    y = viewport["y"] + (viewport["height"] / 2.0)
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)

    def start_g_slide(label):
        select_loop()
        e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
        e.advance(2)
        e.key("g", [])
        e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y + 30.0, "frame": 0}]})
        e.advance(3)
        live = vertex_positions(e, CC_BOX, loop_vertices)
        largest = max(distance(live[v], p[v]) for v in loop_vertices)
        check_true(f"{label}: the G slide moves the loop with the pointer", largest > 1e-5,
                   f"largest displacement {largest:.5f}")

    before_undo = undo_count(e)
    start_g_slide("cancel_component_edit")
    result = e.call("cancel_component_edit")
    e.advance(2)
    expect("cancel_component_edit cancels the running slide", result.get("cancelled"), True)
    restored = vertex_positions(e, CC_BOX, loop_vertices)
    check_true("cancel_component_edit: positions back at the start", all(near(restored[v], p[v], 0.0) for v in loop_vertices),
               str({v: restored[v] for v in loop_vertices[:2]}))
    wait_idle(e)
    expect("cancel_component_edit: no undo entry", undo_count(e), before_undo)
    expect("cancel_component_edit with nothing active -> cancelled false", e.call("cancel_component_edit").get("cancelled"), False)

    start_g_slide("Escape")
    try:
        e.call("undo")
        check_true("undo over MCP during a G slide is refused", False, "no error")
    except RuntimeError as error:
        check_true("undo over MCP during a G slide is refused", "slide" in str(error), str(error))
    expect("the refused undo leaves the undo count", undo_count(e), before_undo)
    e.key("escape", [])
    wait_idle(e)
    restored = vertex_positions(e, CC_BOX, loop_vertices)
    check_true("Escape: positions back at the start", all(near(restored[v], p[v], 0.0) for v in loop_vertices))
    expect("Escape: no undo entry", undo_count(e), before_undo)

    start_g_slide("Enter")
    live = vertex_positions(e, CC_BOX, loop_vertices)
    e.key("enter", [])
    wait_idle(e)
    expect("Enter confirms the G slide: one undo entry", undo_count(e), before_undo + 1)
    expect("the undo entry is the edge slide", e.call("get_undo_redo_stack")["undo"][-1]["description"], "Edge Slide")
    committed = vertex_positions(e, CC_BOX, loop_vertices)
    check_true("Enter keeps the slid positions", all(near(committed[v], live[v], 1e-5) for v in loop_vertices))
    expect("G slide: topology unchanged", geometry_counts(e, CC_BOX), base)
    undo_slide("G slide")

    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": CC_BOX, "translation": [0.0, -200.0, 0.0]})
    e.advance(2)


OCTAHEDRON = "mm_octahedron"


def run_loop_cut(e):
    """Loop cut (doc/plans/mesh_modeling.md section 4.5,
    doc/editor/mesh_modeling.md): loop_cut_mesh and the Ctrl+R gesture."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    base = (8, 12, 6)
    expect("loop cut: plain box at its base counts", geometry_counts(e, BOX), base)
    p = vertex_positions(e, BOX, range(base[0]))
    y_min = min(v[1] for v in p.values())
    y_max = max(v[1] for v in p.values())
    vertical = None
    for a in range(base[0]):
        for b in range(a + 1, base[0]):
            if near([p[a][0], p[a][2]], [p[b][0], p[b][2]], 1e-6) and abs(p[a][1] - p[b][1]) > 1e-3:
                vertical = [a, b]
                break
        if vertical is not None:
            break
    check_true("the box has a vertical edge", vertical is not None)
    if vertical is None:
        return

    def loop_cut(node_name, edge, **kwargs):
        args = {"scene_name": e.scene, "node_name": node_name, "edge": edge}
        args.update(kwargs)
        result = e.call("loop_cut_mesh", args)
        wait_idle(e)
        return result

    def new_vertex_heights():
        after = vertex_positions(e, BOX, range(8, geometry_counts(e, BOX)[0]))
        return [(v[1] - y_min) / (y_max - y_min) for v in after.values()]

    # Cuts 1 from a vertical edge: the ring is the 4 vertical edges (closed),
    # one horizontal loop around the sides: (8 + 4, 12 + 4 + 4, 6 + 4).
    e.call("set_mesh_component_mode", {"mode": "face"})
    before_undo = undo_count(e)
    result = loop_cut(BOX, vertical)
    expect("loop_cut_mesh from a vertical box edge: a closed ring of 4",
           (result.get("ring_length"), result.get("ring_closed")), (4, True))
    expect("loop_cut_mesh cuts 1 -> (12, 20, 10)", geometry_counts(e, BOX), (12, 20, 10))
    selection = e.call("get_mesh_component_selection")
    expect("loop cut switches to edge mode", selection.get("mode"), "edge")
    edges = entry_of(selection, BOX)["edges"]
    expect("loop cut cuts 1: the selection is the 4 inner edges", len(edges), 4)
    check_true("loop cut cuts 1: the inner edges join new vertices", all(min(edge) >= 8 for edge in edges), str(edges))
    heights = new_vertex_heights()
    check_true("loop cut factor 0: the new loop at half height", all(abs(h - 0.5) < 1e-4 for h in heights), str(heights))
    expect("loop cut: one undo entry", undo_count(e), before_undo + 1)
    expect("loop cut: the undo entry is the loop cut", e.call("get_undo_redo_stack")["undo"][-1]["description"], "Loop Cut")
    undo_and_check(e, "loop cut cuts 1", BOX, base)
    expect("loop cut: one undo step removes it", undo_count(e), before_undo)

    # Cuts 2: two loops, (8 + 8, 12 + 8 + 8, 6 + 8).
    loop_cut(BOX, vertical, cuts=2)
    expect("loop_cut_mesh cuts 2 -> (16, 28, 14)", geometry_counts(e, BOX), (16, 28, 14))
    expect("loop cut cuts 2: the selection is the 8 inner edges",
           len(entry_of(e.call("get_mesh_component_selection"), BOX)["edges"]), 8)
    undo_and_check(e, "loop cut cuts 2", BOX, base)

    # Factor 0.5: the new loop slides halfway to one rail end, 3/4 (or 1/4)
    # of the height; every loop vertex at the same height.
    result = loop_cut(BOX, vertical, factor=0.5)
    expect("loop cut factor 0.5: 4 slide vertices in 1 loop, 4 moved",
           (result.get("slide_vertices"), result.get("loops"), result.get("moved_vertices")), (4, 1, 4))
    heights = new_vertex_heights()
    check_true("loop cut factor 0.5: the new loop at 3/4 (or 1/4) of the height",
               (len(heights) == 4) and all(abs(abs(h - 0.5) - 0.25) < 1e-4 for h in heights) and (max(heights) - min(heights) < 1e-4),
               str(heights))
    expect("loop cut factor 0.5: cut and slide are one undo entry", undo_count(e), before_undo + 1)
    undo_and_check(e, "loop cut factor 0.5", BOX, base)

    # The Catmull-Clark box: the ring from a middle edge closes across a quad
    # around the box (8 edges): (26 + 8, 48 + 16, 24 + 8).
    cc_base = geometry_counts(e, CC_BOX)
    expect("loop cut: Catmull-Clark box at its base counts", cc_base, (26, 48, 24))
    cc_p = vertex_positions(e, CC_BOX, range(cc_base[0]))
    e.call("select_all_mesh_components", {"scene_name": e.scene, "node_name": CC_BOX})
    cc_edges = entry_of(e.call("get_mesh_component_selection"), CC_BOX)["edges"]
    middle = [edge for edge in cc_edges if abs(cc_p[edge[0]][1]) < 1e-5 and abs(cc_p[edge[1]][1]) < 1e-5]
    e.call("clear_mesh_component_selection")
    check_true("the Catmull-Clark box has a middle edge", len(middle) > 0)
    if len(middle) > 0:
        result = loop_cut(CC_BOX, middle[0])
        expect("loop_cut_mesh on the Catmull-Clark box: a closed ring of 8",
               (result.get("ring_length"), result.get("ring_closed")), (8, True))
        expect("loop cut on the Catmull-Clark box -> (34, 64, 32)", geometry_counts(e, CC_BOX), (34, 64, 32))
        expect("loop cut on the Catmull-Clark box: the selection is the 8 inner edges",
               len(entry_of(e.call("get_mesh_component_selection"), CC_BOX)["edges"]), 8)
        undo_and_check(e, "loop cut on the Catmull-Clark box", CC_BOX, cc_base)

    # A seed without a quad facet (an all-triangle octahedron): the edge
    # alone is cut, its two triangles fan: (6 + 1, 12 + 3, 8 + 2).
    e.call("create_shape", {"scene_name": e.scene, "shape": "regular_polyhedron", "kind": "octahedron",
                            "name": OCTAHEDRON, "motion_mode": "none"})
    e.advance(4)
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": OCTAHEDRON, "translation": [0.0, 300.0, 0.0]})
    e.advance(2)
    tri_base = geometry_counts(e, OCTAHEDRON)
    expect("loop cut: octahedron at its base counts", tri_base, (6, 12, 8))
    tri_face = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": OCTAHEDRON, "domain": "facet", "indices": [0]
    })["elements"][0]["vertices"]
    result = loop_cut(OCTAHEDRON, [tri_face[0], tri_face[1]], factor=0.5)
    expect("loop cut of a triangle edge: the ring is the edge alone, no inner edges, no slide",
           (result.get("ring_length"), result.get("inner_edges"), result.get("slide_vertices")), (1, 0, 0))
    expect("loop cut of a triangle edge -> (7, 15, 10)", geometry_counts(e, OCTAHEDRON), (7, 15, 10))
    expect("loop cut of a triangle edge selects nothing",
           counts_of(e.call("get_mesh_component_selection"), OCTAHEDRON), (0, 0, 0))
    undo_and_check(e, "loop cut of a triangle edge", OCTAHEDRON, tri_base)

    # Ctrl+R in a viewport, over the box's left (vertical) edge.
    e.call("clear_mesh_component_selection")
    viewport = place_in_front_of_camera(e, BOX)
    x, y = left_of_centre(viewport)
    check_pick(e, viewport, BOX, x, y)
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    e.call("set_mesh_component_mode", {"mode": "face"})
    before_undo = undo_count(e)

    e.key("r", ["ctrl"])
    e.key("escape", [])
    wait_idle(e)
    expect("Ctrl+R, Escape in the preview: the box unchanged", geometry_counts(e, BOX), base)
    expect("Ctrl+R, Escape in the preview: no undo entry", undo_count(e), before_undo)
    expect("Ctrl+R, Escape in the preview: face mode kept", e.call("get_mesh_component_selection").get("mode"), "face")

    e.key("r", ["ctrl"])
    e.call("mouse_wheel", {"x": x, "y": y, "dy": 1.0})
    e.advance(2)
    e.call("mouse_click", {"x": x, "y": y})
    e.advance(3)
    expect("Ctrl+R, wheel, click: two loops cut", geometry_counts(e, BOX), (16, 28, 14))
    try:
        e.call("undo")
        check_true("undo over MCP during the loop cut slide is refused", False, "no error")
    except RuntimeError as error:
        check_true("undo over MCP during the loop cut slide is refused", "slide" in str(error), str(error))
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y + 20.0, "frame": 0}]})
    e.advance(3)
    e.call("mouse_click", {"x": x, "y": y + 20.0})
    wait_idle(e)
    expect("Ctrl+R, wheel, click, move, click: two loops cut", geometry_counts(e, BOX), (16, 28, 14))
    expect("Ctrl+R gesture: one undo entry", undo_count(e), before_undo + 1)
    expect("Ctrl+R gesture: the undo entry is the loop cut", e.call("get_undo_redo_stack")["undo"][-1]["description"], "Loop Cut")
    heights = new_vertex_heights()
    check_true("Ctrl+R gesture: the loops slid with the pointer",
               (len(heights) == 8) and any((abs(h - (1.0 / 3.0)) > 1e-3) and (abs(h - (2.0 / 3.0)) > 1e-3) for h in heights),
               str([round(h, 4) for h in heights]))
    undo_and_check(e, "Ctrl+R gesture", BOX, base)
    expect("Ctrl+R gesture: one undo step removes it", undo_count(e), before_undo)

    e.call("set_mesh_component_mode", {"mode": "face"})
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    e.key("r", ["ctrl"])
    e.call("mouse_click", {"x": x, "y": y})
    e.advance(3)
    expect("Ctrl+R, click: the loop is cut while sliding", geometry_counts(e, BOX), (12, 20, 10))
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y + 20.0, "frame": 0}]})
    e.advance(3)
    e.key("escape", [])
    wait_idle(e)
    expect("Escape in the loop cut slide: the box unchanged", geometry_counts(e, BOX), base)
    expect("Escape in the loop cut slide: no undo entry", undo_count(e), before_undo)
    expect("Escape in the loop cut slide: face mode back", e.call("get_mesh_component_selection").get("mode"), "face")
    restored = vertex_positions(e, BOX, range(base[0]))
    check_true("Escape in the loop cut slide: positions back", all(near(restored[v], p[v], 0.0) for v in range(base[0])))

    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": BOX,
                                  "translation": [0.0, 1.0, 0.0], "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]})
    e.advance(2)


def run_empty_results(e, process):
    """Operations whose result has no facet: the mesh keeps an empty
    primitive that renders and raytraces nothing (doc/erhe/primitive.md
    "Empty primitives"), the editor keeps running, and undo restores the box."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    expect("empty results: plain box at its base counts", geometry_counts(e, BOX), (8, 12, 6))
    e.call("select_items", {"scene_name": e.scene, "paths": [BOX]})
    e.advance()

    def check_alive_and_rendering(label):
        check_true(f"{label}: editor alive", process.poll() is None, f"exit code {process.poll()}")
        shot = e.call("capture_screenshot", {})
        check_true(f"{label}: capture_screenshot works", int(shot.get("width", 0)) > 0, str(shot))
        image = e.call("render_scene_image", {
            "camera": {"eye": [0.0, 2.0, 8.0], "target": [0.0, 0.0, 0.0]},
            "width": 128, "height": 128, "path": "logs/mesh_modeling_empty_result.png"
        })
        check_true(f"{label}: render_scene_image works", int(image.get("width", 0)) == 128, str(image)[:200])

    # Merge by distance of the whole box with a threshold above its diagonal:
    # every vertex joins one cluster, every facet and edge degenerates.
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    e.call("select_all_mesh_components")
    e.call("merge_mesh_by_distance", {"scene_name": e.scene, "node_name": BOX, "threshold": 2.5})
    wait_idle(e)
    expect("merge by distance of the whole box -> (1, 0, 0)", geometry_counts(e, BOX), (1, 0, 0))
    check_alive_and_rendering("merge to a lone vertex")
    undo_and_check(e, "merge to a lone vertex", BOX, (8, 12, 6))

    # Delete every face: the now unused vertices and edges go with them.
    e.call("set_mesh_component_mode", {"mode": "face"})
    e.call("select_all_mesh_components")
    e.call("delete_mesh_components", {"context": "faces"})
    wait_idle(e)
    expect("delete every face -> (0, 0, 0)", geometry_counts(e, BOX), (0, 0, 0))
    check_alive_and_rendering("delete every face")
    undo_and_check(e, "delete every face", BOX, (8, 12, 6))
    check_alive_and_rendering("after undo")

    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})


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

    run_loop_select_mcp(e)
    run_loop_select_clicks(e)

    # It moves the box.
    run_transform_in_component_mode(e)

    run_delete_dissolve(e)

    run_merge(e)

    run_subdivide_edges(e)

    run_slide(e)

    run_loop_cut(e)


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
        run_empty_results(e, process)
    finally:
        process.kill()
    return report()


if __name__ == "__main__":
    sys.exit(main())
