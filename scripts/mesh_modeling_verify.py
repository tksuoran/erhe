#!/usr/bin/env python3
"""Verify the mesh modeling selection commands (doc/plans/mesh_modeling.md
section 4.2, doc/editor/mesh_component_selection.md) over MCP.

Launches a headless editor (ERHE_AI_DRIVER=1; ERHE_FIXED_DT_MS=16.667 unless
the environment sets it), creates a box and checks the
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
Inset (section 4.8, doc/editor/mesh_modeling.md) is checked through
inset_mesh_faces on the plain box (one facet with an inset vertex position,
two adjacent facets, every facet, individual on two facets), each with the
selection afterwards and one undo step, and through the I key in a viewport
(move + Enter: one "Inset" undo entry; move + Escape: unchanged, no entry;
the I and E option keys re-running the topology step).
Bevel (section 4.9 M13a and the segments of M13b, doc/editor/mesh_modeling.md)
is checked through bevel_mesh_edges on the plain box (one edge with the edge
quad selected and its vertices at the offset, every edge, width on one edge
with the quad width, one edge with three segments, every edge with two
segments), each with one undo step, and through Ctrl+B in a viewport (move +
Enter: one "Bevel" undo entry; W and L re-running the topology step, then
Escape: unchanged, no entry; S and two wheel steps to three segments, then
Enter: the three segment counts and one "Bevel" entry).
Split, rip and separate (catalog M9) are checked on the plain box:
split_mesh_components of one face and of one edge (edge split), rip_mesh_vertices
of one vertex and of one edge, separate_mesh_selection of one face (the new
node's place in the hierarchy, its counts, undo and redo), each with undo, and
the Y, V and P keys in a viewport.
Knife (section 4.7, doc/editor/mesh_modeling.md) is checked through
knife_cut_mesh on the plain box (two edge midpoints across the top face, a
vertex to vertex diagonal, three points over two faces with and without cut
through), each with the selection afterwards and one undo step, and through
the K key in a viewport (click, move, click, Enter: one "Knife" undo entry;
click, Escape: unchanged, no entry; click, click, Ctrl+Z, Enter: no cut).
Fill and connect vertex path (section 4.10, catalog M15, M16) are checked on
the plain box: fill_mesh_selection of a deleted face's four vertices (the new
face selected), a nothing-to-fill pair, connect_mesh_vertices of two vertices
of one face and of two antipodal vertices (the cutting plane path), each with
undo, and the F and J keys in a viewport (one undo entry each; F in object
mode queues nothing).
Bridge edge loops (section 4.10, catalog M14) is checked through
bridge_mesh_loops: the plain box's top and bottom faces bridge nothing (each
bridge quad would repeat a side face), and on a box with one interior plane
along y the top and bottom faces are deleted and their rims bridged through
the inside (plain, with one cut, and merged at factor 0.25), each with undo.
The Delete key during a G slide deletes nothing (the component commands
decline while a modal component edit runs).
Flip, recalculate normals and smooth vertices (catalog M10) are checked on the
plain box: flip_mesh_facets of one face (its winding normal, read back through
get_mesh_attribute_values, reversed; the counts and the selection kept),
recalculate_mesh_normals outside on the box with two flipped faces (face mode
and object mode; every face outward) and inside, each with undo; the Shift+N
key recalculates (no frame node) and N alone still creates a frame node with
the mesh counts unchanged; smooth_mesh_vertices of one vertex of the
Catmull-Clark box with factor 1 lands it on its neighbours' average, with undo.
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
    # The fixed-dt editor clock (doc/editor/time.md): the clicks, double
    # clicks and gestures below mean the same at any frame rate.
    env.setdefault("ERHE_FIXED_DT_MS", "16.667")
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
        self.call("advance_frames", {"frames": frames})

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

    # Delete during the slide: the component commands decline while a modal
    # component edit runs, so nothing is deleted and no entry is queued.
    start_g_slide("Delete during a G slide")
    e.key("delete", [])
    e.advance(2)
    wait_idle(e)
    expect("Delete during a G slide deletes nothing", geometry_counts(e, CC_BOX), base)
    expect("Delete during a G slide queues no operation", undo_count(e), before_undo)
    e.key("escape", [])
    wait_idle(e)
    restored = vertex_positions(e, CC_BOX, loop_vertices)
    check_true("Delete during a G slide: Escape still cancels the slide", all(near(restored[v], p[v], 0.0) for v in loop_vertices))
    expect("Delete during a G slide: no undo entry after Escape", undo_count(e), before_undo)

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


def run_inset(e):
    """Inset (doc/plans/mesh_modeling.md section 4.8,
    doc/editor/mesh_modeling.md): inset_mesh_faces and the I key."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    base = (8, 12, 6)
    expect("inset: plain box at its base counts", geometry_counts(e, BOX), base)
    facets = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": BOX, "domain": "facet", "indices": list(range(base[2]))
    })["elements"]
    facet_vertices = {element["index"]: element["vertices"] for element in facets}
    first = facet_vertices[0]
    adjacent = None
    for facet, vertices in facet_vertices.items():
        if (facet != 0) and (len(set(vertices) & set(first)) == 2):
            adjacent = facet
            break
    check_true("the box has a facet adjacent to facet 0", adjacent is not None)
    p = vertex_positions(e, BOX, range(base[0]))

    def inset(facet_list, **kwargs):
        e.call("set_mesh_component_mode", {"mode": "face"})
        select_on(e, BOX, facets=facet_list)
        result = e.call("inset_mesh_faces", kwargs)
        wait_idle(e)
        return result

    before_undo = undo_count(e)

    # One facet, thickness 0.25 (even offset): 4 inset vertices, 4 rim quads.
    result = inset([0], thickness=0.25)
    expect("inset_mesh_faces one facet: changed", result.get("changed"), True)
    expect("inset one facet -> (12, 20, 10)", geometry_counts(e, BOX), (12, 20, 10))
    selected = entry_of(e.call("get_mesh_component_selection"), BOX)["facets"]
    expect("inset one facet: 1 selected facet", len(selected), 1)
    inset_facet = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": BOX, "domain": "facet", "indices": selected
    })["elements"][0]["vertices"]
    check_true("inset one facet: the selected facet is the inset one (its vertices are new)",
               all(v >= base[0] for v in inset_facet), str(inset_facet))
    # Corner c of a rectangle moves 0.25 along each of its two edges.
    c = p[first[0]]
    n1 = p[first[1]]
    n2 = p[first[-1]]
    expected = [c[i] + 0.25 * (((n1[i] - c[i]) / distance(n1, c)) + ((n2[i] - c[i]) / distance(n2, c))) for i in range(3)]
    new_positions = vertex_positions(e, BOX, range(base[0], base[0] + 4))
    check_true("inset one facet: the inset vertex of the facet's first corner at 0.25 from both edges",
               any(near(q, expected) for q in new_positions.values()),
               f"expected {expected}, got {list(new_positions.values())}")
    expect("inset one facet: one undo entry", undo_count(e), before_undo + 1)
    expect("inset one facet: the undo entry is the inset", e.call("get_undo_redo_stack")["undo"][-1]["description"], "Inset")
    undo_and_check(e, "inset one facet", BOX, base)
    expect("inset one facet: one undo step removes it", undo_count(e), before_undo)

    # Two adjacent facets: one region, the shared edge interior.
    if adjacent is not None:
        inset([0, adjacent], thickness=0.25)
        expect("inset two adjacent facets -> (14, 24, 12)", geometry_counts(e, BOX), (14, 24, 12))
        expect("inset two adjacent facets: 2 selected facets",
               len(entry_of(e.call("get_mesh_component_selection"), BOX)["facets"]), 2)
        undo_and_check(e, "inset two adjacent facets", BOX, base)

        # Individual: each facet on its own.
        inset([0, adjacent], thickness=0.25, individual=True)
        expect("inset individual two facets -> (16, 28, 14)", geometry_counts(e, BOX), (16, 28, 14))
        undo_and_check(e, "inset individual two facets", BOX, base)

    # Every facet: a closed region has no boundary edges.
    result = inset(list(range(base[2])), thickness=0.25)
    expect("inset every facet: not changed", result.get("changed"), False)
    expect("inset every facet: the box unchanged", geometry_counts(e, BOX), base)
    expect("inset every facet: no undo entry", undo_count(e), before_undo)

    # The I key in a viewport: I over the selection, move, Enter.
    e.call("clear_mesh_component_selection")
    viewport = place_in_front_of_camera(e, BOX)
    x, y = left_of_centre(viewport)
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, BOX, facets=[0])
    before_undo = undo_count(e)
    e.key("i", [])
    expect("I: the inset topology is in place", geometry_counts(e, BOX), (12, 20, 10))
    try:
        e.call("undo")
        check_true("undo over MCP during the inset is refused", False, "no error")
    except RuntimeError as error:
        check_true("undo over MCP during the inset is refused", True, str(error))
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x + 30.0, "y": y, "frame": 0}]})
    e.advance(3)
    e.key("enter", [])
    wait_idle(e)
    expect("I, move, Enter -> (12, 20, 10)", geometry_counts(e, BOX), (12, 20, 10))
    expect("I, move, Enter: one undo entry", undo_count(e), before_undo + 1)
    expect("I, move, Enter: the undo entry is the inset", e.call("get_undo_redo_stack")["undo"][-1]["description"], "Inset")
    expect("I, move, Enter: 1 selected facet",
           len(entry_of(e.call("get_mesh_component_selection"), BOX)["facets"]), 1)
    moved = vertex_positions(e, BOX, range(base[0], base[0] + 4))
    corners = [p[v] for v in first]
    check_true("I, move, Enter: the inset vertices moved off the facet's corners",
               all(min(distance(q, corner) for corner in corners) > 1e-4 for q in moved.values()),
               str(list(moved.values())))
    undo_and_check(e, "I, move, Enter", BOX, base)

    # I, move, Escape: nothing changes, nothing is queued.
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, BOX, facets=[0])
    before_undo = undo_count(e)
    e.key("i", [])
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x + 30.0, "y": y, "frame": 0}]})
    e.advance(3)
    e.key("escape", [])
    wait_idle(e)
    expect("I, move, Escape: the box unchanged", geometry_counts(e, BOX), base)
    expect("I, move, Escape: no undo entry", undo_count(e), before_undo)
    expect("I, move, Escape: the facet selected again",
           entry_of(e.call("get_mesh_component_selection"), BOX)["facets"], [0])
    restored = vertex_positions(e, BOX, range(base[0]))
    check_true("I, move, Escape: positions back", all(near(restored[v], p[v], 0.0) for v in range(base[0])))

    # The modal option keys re-run the topology step: I on two adjacent
    # facets (one region), I again toggles individual, E even offset.
    if adjacent is not None:
        select_on(e, BOX, facets=[0, adjacent])
        e.key("i", [])
        expect("I on two adjacent facets -> (14, 24, 12)", geometry_counts(e, BOX), (14, 24, 12))
        e.key("i", [])
        expect("I, I (individual) -> (16, 28, 14)", geometry_counts(e, BOX), (16, 28, 14))
        e.key("e", [])
        expect("I, I, E (even offset off) -> (16, 28, 14)", geometry_counts(e, BOX), (16, 28, 14))
        e.key("escape", [])
        wait_idle(e)
        expect("I, I, E, Escape: the box unchanged", geometry_counts(e, BOX), base)
        expect("I, I, E, Escape: no undo entry", undo_count(e), before_undo)

    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": BOX,
                                  "translation": [0.0, 1.0, 0.0], "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]})
    e.advance(2)


def distance_to_line(p, a, b):
    d = [b[i] - a[i] for i in range(3)]
    length = math.sqrt(sum(c * c for c in d))
    d = [c / length for c in d]
    w = [p[i] - a[i] for i in range(3)]
    t = sum(w[i] * d[i] for i in range(3))
    return math.sqrt(sum((w[i] - t * d[i]) ** 2 for i in range(3)))


def run_bevel(e):
    """Bevel (doc/plans/mesh_modeling.md section 4.9 M13a, M13b segments,
    doc/editor/mesh_modeling.md): bevel_mesh_edges and the Ctrl+B key."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    base = (8, 12, 6)
    expect("bevel: plain box at its base counts", geometry_counts(e, BOX), base)
    first = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": BOX, "domain": "facet", "indices": [0]
    })["elements"][0]["vertices"]
    edge = [first[0], first[1]]
    p = vertex_positions(e, BOX, range(base[0]))
    a = p[edge[0]]
    b = p[edge[1]]

    def bevel(select_all=False, **kwargs):
        e.call("set_mesh_component_mode", {"mode": "edge"})
        if select_all:
            e.call("select_all_mesh_components", {"scene_name": e.scene, "node_name": BOX})
        else:
            select_on(e, BOX, edges=[edge])
        result = e.call("bevel_mesh_edges", kwargs)
        wait_idle(e)
        return result

    def selected_quad():
        facets = entry_of(e.call("get_mesh_component_selection"), BOX)["facets"]
        if len(facets) != 1:
            return None, facets
        vertices = e.call("get_mesh_attribute_values", {
            "scene_name": e.scene, "node_name": BOX, "domain": "facet", "indices": facets
        })["elements"][0]["vertices"]
        return vertex_positions(e, BOX, vertices), facets

    before_undo = undo_count(e)

    # One edge, offset 0.25: each end becomes two vertices, the edge a quad.
    result = bevel(amount=0.25)
    expect("bevel_mesh_edges one edge: changed", result.get("changed"), True)
    expect("bevel one edge -> (10, 15, 7)", geometry_counts(e, BOX), (10, 15, 7))
    quad, facets = selected_quad()
    check_true("bevel one edge: the edge quad is the one selected facet", quad is not None, str(facets))
    if quad is not None:
        check_true("bevel one edge: the quad's vertices are 0.25 from the beveled edge",
                   len(quad) == 4 and all(abs(distance_to_line(q, a, b) - 0.25) < 1e-4 for q in quad.values()),
                   str(list(quad.values())))
    expect("bevel one edge: one undo entry", undo_count(e), before_undo + 1)
    expect("bevel one edge: the undo entry is the bevel", e.call("get_undo_redo_stack")["undo"][-1]["description"], "Bevel")
    undo_and_check(e, "bevel one edge", BOX, base)
    expect("bevel one edge: one undo step removes it", undo_count(e), before_undo)

    # Every edge: the classic beveled cube.
    result = bevel(select_all=True, amount=0.25)
    expect("bevel every edge -> (24, 48, 26)", geometry_counts(e, BOX), (24, 48, 26))
    expect("bevel every edge: 12 edge facets", result.get("edge_facets"), 12)
    expect("bevel every edge: 8 vertex facets", result.get("vertex_facets"), 8)
    undo_and_check(e, "bevel every edge", BOX, base)

    # Width 0.25 on one edge (a right angle): the quad is 0.25 wide at each end.
    bevel(amount=0.25, offset_type="width")
    expect("bevel width one edge -> (10, 15, 7)", geometry_counts(e, BOX), (10, 15, 7))
    quad, facets = selected_quad()
    if quad is None:
        check_true("bevel width: the edge quad is selected", False, str(facets))
    else:
        near_a = [q for q in quad.values() if distance(q, a) < distance(q, b)]
        check_true("bevel width: the quad is 0.25 wide at the edge's end",
                   len(near_a) == 2 and abs(distance(near_a[0], near_a[1]) - 0.25) < 1e-4,
                   str(near_a))
    undo_and_check(e, "bevel width one edge", BOX, base)

    # One edge, three segments: two three-valent ends with one beveled edge
    # each, so no vertex patch: 10 + 2 * 2 vertices, 6 + 3 facets, 12 + 3 * 3
    # edges; the strip is three quads.
    result = bevel(amount=0.25, segments=3)
    expect("bevel one edge 3 segments -> (14, 21, 9)", geometry_counts(e, BOX), (14, 21, 9))
    expect("bevel one edge 3 segments: 3 edge facets", result.get("edge_facets"), 3)
    expect("bevel one edge 3 segments: 1 beveled edge", result.get("beveled_edges"), 1)
    expect("bevel one edge 3 segments: no vertex facets", result.get("vertex_facets"), 0)
    expect("bevel one edge 3 segments: one undo entry", undo_count(e), before_undo + 1)
    undo_and_check(e, "bevel one edge 3 segments", BOX, base)

    # Every edge, two segments: the cutoff patch at every corner (a centre
    # triangle and three corner triangles): 48 vertices, 108 edges, 62 facets.
    result = bevel(select_all=True, amount=0.25, segments=2)
    expect("bevel every edge 2 segments -> (48, 108, 62)", geometry_counts(e, BOX), (48, 108, 62))
    expect("bevel every edge 2 segments: 24 edge facets", result.get("edge_facets"), 24)
    expect("bevel every edge 2 segments: 32 vertex facets", result.get("vertex_facets"), 32)
    undo_and_check(e, "bevel every edge 2 segments", BOX, base)
    expect("bevel: no undo entries left", undo_count(e), before_undo)

    # The Ctrl+B key in a viewport: Ctrl+B over the selection, move, Enter.
    e.call("clear_mesh_component_selection")
    viewport = place_in_front_of_camera(e, BOX)
    x, y = left_of_centre(viewport)
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    e.call("set_mesh_component_mode", {"mode": "edge"})
    select_on(e, BOX, edges=[edge])
    before_undo = undo_count(e)
    e.key("b", ["ctrl"])
    expect("Ctrl+B: the bevel topology is in place", geometry_counts(e, BOX), (10, 15, 7))
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x + 30.0, "y": y, "frame": 0}]})
    e.advance(3)
    e.key("enter", [])
    wait_idle(e)
    expect("Ctrl+B, move, Enter -> (10, 15, 7)", geometry_counts(e, BOX), (10, 15, 7))
    expect("Ctrl+B, move, Enter: one undo entry", undo_count(e), before_undo + 1)
    expect("Ctrl+B, move, Enter: the undo entry is the bevel", e.call("get_undo_redo_stack")["undo"][-1]["description"], "Bevel")
    quad, facets = selected_quad()
    if quad is None:
        check_true("Ctrl+B, move, Enter: the edge quad is selected", False, str(facets))
    else:
        check_true("Ctrl+B, move, Enter: the quad's vertices moved off the edge",
                   all(distance_to_line(q, a, b) > 1e-4 for q in quad.values()), str(list(quad.values())))
    undo_and_check(e, "Ctrl+B, move, Enter", BOX, base)

    # Ctrl+B, move, W, L, Escape: nothing changes, nothing is queued.
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    e.call("set_mesh_component_mode", {"mode": "edge"})
    select_on(e, BOX, edges=[edge])
    before_undo = undo_count(e)
    e.key("b", ["ctrl"])
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x + 30.0, "y": y, "frame": 0}]})
    e.advance(3)
    e.key("w", [])
    expect("Ctrl+B, W (width): the bevel topology is in place", geometry_counts(e, BOX), (10, 15, 7))
    e.key("l", [])
    expect("Ctrl+B, W, L (loop slide off): the bevel topology is in place", geometry_counts(e, BOX), (10, 15, 7))
    e.key("escape", [])
    wait_idle(e)
    expect("Ctrl+B, move, Escape: the box unchanged", geometry_counts(e, BOX), base)
    expect("Ctrl+B, move, Escape: no undo entry", undo_count(e), before_undo)
    restored = vertex_positions(e, BOX, range(base[0]))
    check_true("Ctrl+B, move, Escape: positions back", all(near(restored[v], p[v], 0.0) for v in range(base[0])))

    # Ctrl+B, move, S, wheel, wheel, Enter: three segments, one entry.
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    e.call("set_mesh_component_mode", {"mode": "edge"})
    select_on(e, BOX, edges=[edge])
    before_undo = undo_count(e)
    e.key("b", ["ctrl"])
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x + 30.0, "y": y, "frame": 0}]})
    e.advance(3)
    e.key("s", [])
    e.call("mouse_wheel", {"x": x + 30.0, "y": y, "dy": 1.0})
    e.advance(2)
    expect("Ctrl+B, S, wheel: two segments in place", geometry_counts(e, BOX), (12, 18, 8))
    e.call("mouse_wheel", {"x": x + 30.0, "y": y, "dy": 1.0})
    e.advance(2)
    expect("Ctrl+B, S, wheel, wheel: three segments in place", geometry_counts(e, BOX), (14, 21, 9))
    e.key("enter", [])
    wait_idle(e)
    expect("Ctrl+B, S, wheel x2, Enter -> (14, 21, 9)", geometry_counts(e, BOX), (14, 21, 9))
    expect("Ctrl+B, S, wheel x2, Enter: one undo entry", undo_count(e), before_undo + 1)
    expect("Ctrl+B, S, wheel x2, Enter: the undo entry is the bevel", e.call("get_undo_redo_stack")["undo"][-1]["description"], "Bevel")
    moved = [v for v in vertex_positions(e, BOX, range(14)).values() if distance_to_line(v, a, b) > 1e-4]
    check_true("Ctrl+B, S, wheel x2, Enter: the new vertices moved off the edge", len(moved) == 14, str(len(moved)))
    undo_and_check(e, "Ctrl+B, S, wheel x2, Enter", BOX, base)

    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": BOX,
                                  "translation": [0.0, 1.0, 0.0], "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]})
    e.advance(2)


def scene_node_order(e):
    """(parent id, name) of every node, in scene tree walk order."""
    nodes = e.call("get_scene_nodes", {"scene_name": e.scene})["nodes"]
    return [(node.get("parent_id"), node["name"]) for node in nodes]


def run_split_rip_separate(e):
    """Split (Y), rip (V) and separate (P), doc/plans/mesh_modeling.md catalog M9."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    base = (8, 12, 6)
    expect("split / rip / separate: plain box at its base counts", geometry_counts(e, BOX), base)

    # Split one face: its 4 vertices and 4 edges duplicate.
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, BOX, facets=[0])
    e.call("split_mesh_components")
    wait_idle(e)
    expect("split one face -> (12, 16, 6)", geometry_counts(e, BOX), (12, 16, 6))
    expect("split one face: the face stays selected",
           entry_of(e.call("get_mesh_component_selection"), BOX)["facets"], [0])
    undo_and_check(e, "split one face", BOX, base)

    # Edge mode without a complete face: edge split. Each endpoint (a corner
    # of valence 3) tears along one more edge: +2 vertices, +3 edges.
    select_on(e, BOX, facets=[0])
    seed_edge = entry_of(e.call("get_mesh_component_selection"), BOX)["edges"][0]
    e.call("set_mesh_component_mode", {"mode": "edge"})
    select_on(e, BOX, edges=[seed_edge])
    e.call("split_mesh_components")
    wait_idle(e)
    expect("edge split of one edge -> (10, 15, 6)", geometry_counts(e, BOX), (10, 15, 6))
    expect("edge split: one edge selected (the torn side's copy)",
           len(entry_of(e.call("get_mesh_component_selection"), BOX)["edges"]), 1)
    undo_and_check(e, "edge split", BOX, base)

    # Rip one vertex (vertex mode, no selected edge): it tears along its two
    # edges nearest to the direction.
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, BOX, vertices=[0])
    result = e.call("rip_mesh_vertices", {"direction": [1.0, 1.0, 1.0]})
    expect("rip_mesh_vertices echoes the direction", result.get("direction"), [1.0, 1.0, 1.0])
    wait_idle(e)
    expect("rip one vertex along two edges -> (9, 14, 6)", geometry_counts(e, BOX), (9, 14, 6))
    expect("rip one vertex: one ripped vertex selected",
           len(entry_of(e.call("get_mesh_component_selection"), BOX)["vertices"]), 1)
    undo_and_check(e, "rip one vertex", BOX, base)

    # Rip one edge (edge mode): same tear as the edge split.
    e.call("set_mesh_component_mode", {"mode": "edge"})
    select_on(e, BOX, edges=[seed_edge])
    e.call("rip_mesh_vertices")
    wait_idle(e)
    expect("rip one edge -> (10, 15, 6)", geometry_counts(e, BOX), (10, 15, 6))
    undo_and_check(e, "rip one edge", BOX, base)

    # Rip is refused in face mode.
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, BOX, facets=[0])
    try:
        e.call("rip_mesh_vertices")
        check_true("rip in face mode is refused", False, "no error")
    except RuntimeError:
        check_true("rip in face mode is refused", True)

    # Separate one face into a new node right after the box.
    order_before = scene_node_order(e)
    undo_before = undo_count(e)
    select_on(e, BOX, facets=[0])
    result = e.call("separate_mesh_selection")
    new_name = result.get("node_name", "")
    expect("separate_mesh_selection names the new node", new_name, f"{BOX} separated")
    wait_idle(e)
    expect("separate: one undo entry", undo_count(e), undo_before + 1)
    expect("separate: the original -> (8, 12, 5)", geometry_counts(e, BOX), (8, 12, 5))
    expect("separate: the new node -> (4, 4, 1)", geometry_counts(e, new_name), (4, 4, 1))
    order = scene_node_order(e)
    box_entry = next((entry for entry in order if entry[1] == BOX), None)
    new_index = next((i for i, entry in enumerate(order) if entry[1] == new_name), None)
    box_index = order.index(box_entry) if box_entry is not None else None
    check_true("separate: the new node is right after the original, same parent",
               (new_index is not None) and (box_index is not None) and (new_index == box_index + 1)
               and (order[new_index][0] == box_entry[0]), str(order))
    check_true("separate: the other nodes keep their order",
               [entry for entry in order if entry[1] != new_name] == order_before, str(order))
    expect("separate: the original's component selection is empty",
           counts_of(e.call("get_mesh_component_selection"), BOX), (0, 0, 0))
    e.call("undo")
    wait_idle(e)
    expect("separate undo: the original -> (8, 12, 6)", geometry_counts(e, BOX), base)
    expect("separate undo: the new node is gone, the order restored", scene_node_order(e), order_before)
    e.call("redo")
    wait_idle(e)
    expect("separate redo: the original -> (8, 12, 5)", geometry_counts(e, BOX), (8, 12, 5))
    expect("separate redo: the new node -> (4, 4, 1)", geometry_counts(e, new_name), (4, 4, 1))
    expect("separate redo: the new node is back at its place", scene_node_order(e), order)
    e.call("undo")
    wait_idle(e)
    expect("separate undo after redo: the order restored", scene_node_order(e), order_before)

    # The keys, with the pointer over the box in a viewport.
    e.call("clear_mesh_component_selection")
    viewport = place_in_front_of_camera(e, BOX)
    x, y = left_of_centre(viewport)
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, BOX, facets=[0])
    e.key("y", [])
    wait_idle(e)
    expect("Y in face mode splits the face -> (12, 16, 6)", geometry_counts(e, BOX), (12, 16, 6))
    undo_and_check(e, "Y", BOX, base)
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, BOX, vertices=[0])
    e.key("v", [])
    wait_idle(e)
    expect("V in vertex mode rips the vertex -> (9, 14, 6)", geometry_counts(e, BOX), (9, 14, 6))
    undo_and_check(e, "V", BOX, base)
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, BOX, facets=[0])
    e.key("p", [])
    wait_idle(e)
    expect("P in face mode separates the face -> (8, 12, 5)", geometry_counts(e, BOX), (8, 12, 5))
    undo_and_check(e, "P", BOX, base)
    expect("P undo: the order restored", scene_node_order(e), order_before)

    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": BOX,
                                  "translation": [0.0, 1.0, 0.0], "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]})
    e.advance(2)


def normalize3(v):
    length = math.sqrt(sum(c * c for c in v))
    return [c / length for c in v]


def cross3(a, b):
    return [(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])]


def dot3(a, b):
    return sum(x * y for x, y in zip(a, b))


def orthographic_view(center, direction, half_size, size=1000.0):
    """A knife_cut_mesh view object: orthographic along direction, centred
    on center (world), half_size world units to each viewport edge."""
    f = normalize3(direction)
    up = [0.0, 1.0, 0.0]
    if abs(dot3(f, up)) > 0.99:
        up = [0.0, 0.0, -1.0]
    r = normalize3(cross3(f, up))
    u = cross3(r, f)
    rows = [
        [r[0] / half_size, r[1] / half_size, r[2] / half_size, -dot3(r, center) / half_size],
        [u[0] / half_size, u[1] / half_size, u[2] / half_size, -dot3(u, center) / half_size],
        [0.01 * f[0], 0.01 * f[1], 0.01 * f[2], -0.01 * dot3(f, center)],
        [0.0, 0.0, 0.0, 1.0]
    ]
    clip_from_world = [rows[row][column] for column in range(4) for row in range(4)]
    eye = [center[i] - (10.0 * f[i]) for i in range(3)]
    return {"eye": eye, "direction": f, "perspective": False,
            "viewport_width": size, "viewport_height": size, "clip_from_world": clip_from_world}


def find_boundary(e, viewport, inside, outside, node_name, steps=12):
    """The window point where the segment from inside (over node_name) to
    outside (off it) leaves node_name, by bisection with pick_at."""
    def hits(point):
        pick = e.call("pick_at", {"x": point[0] - viewport["x"], "y": point[1] - viewport["y"]})
        return (pick.get("nearest") or {}).get("node") == node_name
    if (not hits(inside)) or hits(outside):
        return None
    for _ in range(steps):
        middle = [0.5 * (inside[0] + outside[0]), 0.5 * (inside[1] + outside[1])]
        if hits(middle):
            inside = middle
        else:
            outside = middle
    return inside


def run_knife(e):
    """Knife (doc/plans/mesh_modeling.md section 4.7,
    doc/editor/mesh_modeling.md): knife_cut_mesh and the K gesture."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": BOX,
                                  "translation": [0.0, 1.0, 0.0], "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]})
    e.advance(2)
    base = (8, 12, 6)
    expect("knife: plain box at its base counts", geometry_counts(e, BOX), base)
    p = vertex_positions(e, BOX, range(base[0]))
    y_max = max(v[1] for v in p.values())
    y_min = min(v[1] for v in p.values())
    z_max = max(v[2] for v in p.values())
    z_min = min(v[2] for v in p.values())
    extent = max(max(abs(c) for c in v) for v in p.values())
    top = [v for v in range(base[0]) if abs(p[v][1] - y_max) < 1e-6]
    bottom = [v for v in range(base[0]) if abs(p[v][1] - y_min) < 1e-6]

    def edge_along_x(vertices, z):
        pair = [v for v in vertices if abs(p[v][2] - z) < 1e-6]
        return pair if len(pair) == 2 else None

    top_back = edge_along_x(top, z_min)
    top_front = edge_along_x(top, z_max)
    bottom_front = edge_along_x(bottom, z_max)
    check_true("the box has the top back, top front and bottom front edges",
               (top_back is not None) and (top_front is not None) and (bottom_front is not None))
    if (top_back is None) or (top_front is None) or (bottom_front is None):
        return
    diagonal_end = None
    for v in top:
        if (abs(p[v][0] - p[top[0]][0]) > 1e-6) and (abs(p[v][2] - p[top[0]][2]) > 1e-6):
            diagonal_end = v
    check_true("the box top has a diagonal", diagonal_end is not None)

    centre = [0.0, 1.0, 0.0]
    top_view = orthographic_view(centre, [0.0, -1.0, 0.0], 2.0 * extent)
    slanted_view = orthographic_view(centre, [0.0, -1.0, -2.0], 2.0 * extent)

    def knife(points, view, **kwargs):
        args = {"scene_name": e.scene, "node_name": BOX, "points": points, "view": view}
        args.update(kwargs)
        result = e.call("knife_cut_mesh", args)
        wait_idle(e)
        return result

    e.call("set_mesh_component_mode", {"mode": "face"})
    before_undo = undo_count(e)

    # Two edge midpoints across the top face: one cut edge, the top face in
    # two and its two edges split: (8 + 2, 12 + 3, 6 + 1).
    result = knife([{"snap": "edge", "edge": top_back}, {"snap": "edge", "edge": top_front}], top_view)
    expect("knife_cut_mesh edge to edge across the top: changed, 2 cut vertices, 1 cut edge",
           (result.get("changed"), result.get("cut_vertices"), result.get("cut_edges")), (True, 2, 1))
    expect("knife edge to edge -> (10, 15, 7)", geometry_counts(e, BOX), (10, 15, 7))
    selection = e.call("get_mesh_component_selection")
    expect("knife switches to edge mode", selection.get("mode"), "edge")
    expect("knife edge to edge: the cut edge is the selection", len(entry_of(selection, BOX)["edges"]), 1)
    expect("knife edge to edge: one undo entry", undo_count(e), before_undo + 1)
    expect("knife edge to edge: the undo entry is the knife", e.call("get_undo_redo_stack")["undo"][-1]["description"], "Knife")
    undo_and_check(e, "knife edge to edge", BOX, base)
    expect("knife edge to edge: one undo step removes it", undo_count(e), before_undo)

    # Vertex to vertex across the top diagonal: (8, 12 + 1, 6 + 1).
    if diagonal_end is not None:
        knife([{"snap": "vertex", "vertex": top[0]}, {"snap": "vertex", "vertex": diagonal_end}], top_view)
        expect("knife vertex to vertex across the top diagonal -> (8, 13, 7)", geometry_counts(e, BOX), (8, 13, 7))
        undo_and_check(e, "knife vertex to vertex", BOX, base)

    # Three points over the top and the front face, seen from the front and
    # above: two cut edges, three edge splits: (8 + 3, 12 + 5, 6 + 2). The
    # bottom back edge lies behind the front face under the second segment;
    # without cut through it is not cut.
    three = [{"snap": "edge", "edge": top_back}, {"snap": "edge", "edge": top_front}, {"snap": "edge", "edge": bottom_front}]
    result = knife(three, slanted_view)
    expect("knife three points over two faces: 2 cut edges", result.get("cut_edges"), 2)
    expect("knife three points over two faces -> (11, 17, 8)", geometry_counts(e, BOX), (11, 17, 8))
    undo_and_check(e, "knife three points", BOX, base)
    # Cut through: the bottom back edge is cut too, and the bottom face with
    # it: (8 + 4, 12 + 7, 6 + 3).
    result = knife(three, slanted_view, cut_through=True)
    expect("knife three points, cut through: 3 cut edges", result.get("cut_edges"), 3)
    expect("knife three points, cut through -> (12, 19, 9)", geometry_counts(e, BOX), (12, 19, 9))
    undo_and_check(e, "knife three points, cut through", BOX, base)
    expect("knife_cut_mesh: every cut undone", undo_count(e), before_undo)

    # The K gesture in a viewport: the box faces the camera, scaled to half
    # its size so its sides lie well inside the viewport; the points are 3 px
    # inside its top and bottom edges (within the snap radius), found with
    # pick_at.
    e.call("clear_mesh_component_selection")
    viewport = place_in_front_of_camera(e, BOX)
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": BOX, "scale": [0.5, 0.5, 0.5]})
    e.advance(2)
    # A vertical scan right of the viewport centre (where the pointer
    # hovers the viewport in the headless layout).
    x = viewport["x"] + (viewport["width"] * 0.5) + (viewport["height"] * 0.02)
    centre_y = viewport["y"] + (viewport["height"] * 0.5)
    top = find_boundary(e, viewport, [x, centre_y], [x, viewport["y"] + 1.0], BOX)
    bottom = find_boundary(e, viewport, [x, centre_y], [x, viewport["y"] + viewport["height"] - 1.0], BOX)
    check_true("the box's top and bottom edges found on screen", (top is not None) and (bottom is not None), f"{top}, {bottom}")
    if (top is None) or (bottom is None):
        return
    y0 = top[1] + 3.0
    y1 = bottom[1] - 3.0

    def move(y):
        e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
        e.advance(3)

    # pick_at probes the view without the pointer; leave the viewport and
    # come back so the pointer hovers it again.
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": viewport["x"] - 20.0, "y": centre_y, "frame": 0}]})
    e.advance(3)
    move(y0)
    e.call("set_mesh_component_mode", {"mode": "face"})
    before_undo = undo_count(e)

    # K, click, move, click, Enter: one cut across the front face.
    e.key("k", [])
    e.call("mouse_click", {"x": x, "y": y0})
    e.advance(2)
    try:
        e.call("undo")
        check_true("undo over MCP during the knife is refused", False, "no error")
    except RuntimeError as error:
        check_true("undo over MCP during the knife is refused", "knife" in str(error), str(error))
    move(y1)
    e.call("mouse_click", {"x": x, "y": y1})
    e.advance(2)
    expect("K, click, click: nothing cut before the confirm", geometry_counts(e, BOX), base)
    e.key("enter", [])
    wait_idle(e)
    expect("K, click, move, click, Enter -> (10, 15, 7)", geometry_counts(e, BOX), (10, 15, 7))
    expect("K gesture: one undo entry", undo_count(e), before_undo + 1)
    expect("K gesture: the undo entry is the knife", e.call("get_undo_redo_stack")["undo"][-1]["description"], "Knife")
    selection = e.call("get_mesh_component_selection")
    expect("K gesture: edge mode with the cut edge selected",
           (selection.get("mode"), len(entry_of(selection, BOX)["edges"])), ("edge", 1))
    undo_and_check(e, "K gesture", BOX, base)
    expect("K gesture: one undo step removes it", undo_count(e), before_undo)

    # K, click, Escape: nothing changes, nothing is queued.
    e.call("set_mesh_component_mode", {"mode": "face"})
    move(y0)
    e.key("k", [])
    e.call("mouse_click", {"x": x, "y": y0})
    e.advance(2)
    e.key("escape", [])
    wait_idle(e)
    expect("K, click, Escape: the box unchanged", geometry_counts(e, BOX), base)
    expect("K, click, Escape: no undo entry", undo_count(e), before_undo)
    expect("K, click, Escape: face mode kept", e.call("get_mesh_component_selection").get("mode"), "face")

    # K with a live edge selection, then Y: the knife's axis lock, not a
    # split (mode-dispatching commands decline while a modal gesture runs).
    select_on(e, BOX, facets=[0])
    seed_edge = entry_of(e.call("get_mesh_component_selection"), BOX)["edges"][0]
    e.call("set_mesh_component_mode", {"mode": "edge"})
    select_on(e, BOX, edges=[seed_edge])
    move(y0)
    e.key("k", [])
    e.key("y", [])
    wait_idle(e)
    expect("K, Y with an edge selection: no split", geometry_counts(e, BOX), base)
    expect("K, Y with an edge selection: no undo entry", undo_count(e), before_undo)
    try:
        e.call("undo")
        check_true("K, Y: the knife stays active", False, "undo was accepted")
    except RuntimeError as error:
        check_true("K, Y: the knife stays active", "knife" in str(error), str(error))
    e.key("escape", [])
    wait_idle(e)
    expect("K, Y, Escape: the box unchanged", geometry_counts(e, BOX), base)
    expect("K, Y, Escape: no undo entry", undo_count(e), before_undo)
    e.call("set_mesh_component_mode", {"mode": "face"})

    # K, click, click, Ctrl+Z, Enter: the one point left makes no cut.
    move(y0)
    e.key("k", [])
    e.call("mouse_click", {"x": x, "y": y0})
    e.advance(2)
    move(y1)
    e.call("mouse_click", {"x": x, "y": y1})
    e.advance(2)
    e.key("z", ["ctrl"])
    e.key("enter", [])
    wait_idle(e)
    expect("K, click, click, Ctrl+Z, Enter: the box unchanged", geometry_counts(e, BOX), base)
    expect("K, click, click, Ctrl+Z, Enter: no undo entry", undo_count(e), before_undo)

    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": BOX, "translation": [0.0, 1.0, 0.0],
                                  "rotation_xyzw": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]})
    e.advance(2)


def run_fill_connect(e):
    """Fill (F) and connect vertex path (J), doc/plans/mesh_modeling.md
    section 4.10 (catalog M15, M16), on the plain box."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    base = (8, 12, 6)
    expect("fill / connect: plain box at its base counts", geometry_counts(e, BOX), base)

    # Face 0's vertices, from the face mode flush.
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, BOX, facets=[0])
    face_vertices = entry_of(e.call("get_mesh_component_selection"), BOX)["vertices"]
    expect("face 0 has four vertices", len(face_vertices), 4)
    p = vertex_positions(e, BOX, range(base[0]))

    def differing_axes(a, b):
        return sum(1 for i in range(3) if abs(p[a][i] - p[b][i]) > 1e-6)

    first = face_vertices[0]
    opposite = next(v for v in face_vertices if differing_axes(first, v) == 2)
    antipode = next(v for v in range(base[0]) if differing_axes(first, v) == 3)

    def delete_face_0():
        e.call("set_mesh_component_mode", {"mode": "face"})
        select_on(e, BOX, facets=[0])
        e.call("delete_mesh_components")
        wait_idle(e)
        expect("delete face 0 -> (8, 12, 5)", geometry_counts(e, BOX), (8, 12, 5))

    # Fill the hole from its four boundary vertices.
    delete_face_0()
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, BOX, vertices=face_vertices)
    e.call("fill_mesh_selection")
    wait_idle(e)
    expect("fill the hole's four vertices -> (8, 12, 6)", geometry_counts(e, BOX), base)
    expect("fill: the new face is selected",
           len(entry_of(e.call("get_mesh_component_selection"), BOX)["facets"]), 1)
    undo_and_check(e, "fill", BOX, (8, 12, 5))
    undo_and_check(e, "delete face 0", BOX, base)

    # Nothing to fill: two opposite corners of the closed box.
    select_on(e, BOX, vertices=[first, antipode])
    e.call("fill_mesh_selection")
    wait_idle(e)
    expect("fill two opposite corners of the closed box: nothing to fill", geometry_counts(e, BOX), base)
    undo_and_check(e, "fill nothing", BOX, base)

    # Connect two opposite vertices of one face: one split.
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, BOX, vertices=[first, opposite])
    e.call("connect_mesh_vertices")
    wait_idle(e)
    expect("connect two opposite vertices of a face -> (8, 13, 7)", geometry_counts(e, BOX), (8, 13, 7))
    expect("connect: the new edge is selected",
           len(entry_of(e.call("get_mesh_component_selection"), BOX)["edges"]), 1)
    undo_and_check(e, "connect", BOX, base)

    # Connect two antipodal vertices (no shared face): their vertex normals
    # cancel, so the cutting plane contains the least aligned axis; it holds
    # four corners, and the shortest path runs along one existing edge and
    # one face diagonal - one face split.
    select_on(e, BOX, vertices=[first, antipode])
    e.call("connect_mesh_vertices")
    wait_idle(e)
    expect("connect antipodal vertices (pair path) -> (8, 13, 7)", geometry_counts(e, BOX), (8, 13, 7))
    undo_and_check(e, "connect pair", BOX, base)

    # Connect is refused in face mode.
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, BOX, facets=[0])
    try:
        e.call("connect_mesh_vertices")
        check_true("connect in face mode is refused", False, "no error")
    except RuntimeError:
        check_true("connect in face mode is refused", True)

    # The keys, with the pointer over the box in a viewport.
    e.call("clear_mesh_component_selection")
    viewport = place_in_front_of_camera(e, BOX)
    x, y = left_of_centre(viewport)
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    delete_face_0()
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, BOX, vertices=face_vertices)
    before_undo = undo_count(e)
    e.key("f", [])
    wait_idle(e)
    expect("F in vertex mode fills the hole -> (8, 12, 6)", geometry_counts(e, BOX), base)
    expect("F: one undo entry", undo_count(e), before_undo + 1)
    undo_and_check(e, "F", BOX, (8, 12, 5))
    undo_and_check(e, "delete face 0 (keys)", BOX, base)
    select_on(e, BOX, vertices=[first, opposite])
    before_undo = undo_count(e)
    e.key("j", [])
    wait_idle(e)
    expect("J in vertex mode connects the two vertices -> (8, 13, 7)", geometry_counts(e, BOX), (8, 13, 7))
    expect("J: one undo entry", undo_count(e), before_undo + 1)
    undo_and_check(e, "J", BOX, base)

    # Object mode: F falls through to the fly camera's frame command.
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    before_undo = undo_count(e)
    e.key("f", [])
    wait_idle(e)
    expect("F in object mode changes no geometry", geometry_counts(e, BOX), base)
    expect("F in object mode queues no operation", undo_count(e), before_undo)

    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": BOX, "translation": [0.0, 1.0, 0.0],
                                  "rotation_xyzw": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]})
    e.advance(2)


BRIDGE_BOX = "mm_bridge_box"


def top_and_bottom_faces(e, node_name, facet_count):
    """The two facets with the highest and lowest mean vertex y."""
    p = vertex_positions(e, node_name, range(geometry_counts(e, node_name)[0]))
    mean_y = {}
    e.call("set_mesh_component_mode", {"mode": "face"})
    for facet in range(facet_count):
        select_on(e, node_name, facets=[facet])
        vertices = entry_of(e.call("get_mesh_component_selection"), node_name)["vertices"]
        mean_y[facet] = sum(p[v][1] for v in vertices) / len(vertices)
    ordered = sorted(mean_y, key=lambda facet: mean_y[facet])
    return [ordered[0], ordered[-1]]


def run_bridge(e):
    """Bridge edge loops, doc/plans/mesh_modeling.md section 4.10 (catalog
    M14)."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    base = (8, 12, 6)
    expect("bridge: plain box at its base counts", geometry_counts(e, BOX), base)

    # The plain box: each bridge quad between the top and bottom rims would
    # repeat a side face, so nothing is bridged and the box stays as it was.
    faces = top_and_bottom_faces(e, BOX, base[2])
    select_on(e, BOX, facets=faces)
    before_undo = undo_count(e)
    e.call("bridge_mesh_loops")
    wait_idle(e)
    expect("bridge the plain box's top and bottom faces: nothing bridged", geometry_counts(e, BOX), base)
    if undo_count(e) > before_undo:
        undo_and_check(e, "bridge nothing", BOX, base)

    # A box with one interior plane along y: 12 vertices, 20 edges, 10 faces.
    e.call("create_shape", {"scene_name": e.scene, "shape": "box", "name": BRIDGE_BOX, "steps": [0, 1, 0], "motion_mode": "none"})
    e.advance(4)
    e.call("set_node_transform", {"scene_name": e.scene, "node_name": BRIDGE_BOX, "translation": [0.0, 400.0, 0.0]})
    e.advance(2)
    tall = (12, 20, 10)
    expect("bridge: box with one interior y plane", geometry_counts(e, BRIDGE_BOX), tall)
    faces = top_and_bottom_faces(e, BRIDGE_BOX, tall[2])

    def bridge(label, expected, arguments, selected_facets):
        e.call("set_mesh_component_mode", {"mode": "face"})
        select_on(e, BRIDGE_BOX, facets=faces)
        before = undo_count(e)
        result = e.call("bridge_mesh_loops", arguments)
        expect(f"{label}: queued", result.get("queued"), True)
        wait_idle(e)
        expect(f"{label} -> {expected}", geometry_counts(e, BRIDGE_BOX), expected)
        expect(f"{label}: one undo entry", undo_count(e), before + 1)
        if selected_facets is not None:
            expect(f"{label}: the bridge faces are selected",
                   len(entry_of(e.call("get_mesh_component_selection"), BRIDGE_BOX)["facets"]), selected_facets)
        undo_and_check(e, label, BRIDGE_BOX, tall)

    # The caps go, their rims are bridged through the inside: 4 rungs, 4 quads.
    bridge("bridge the top and bottom faces", (12, 24, 12), {}, 4)
    # One cut: 4 rung midpoints, each bridge quad split in two.
    bridge("bridge with one cut", (16, 32, 16), {"cuts": 1}, 8)
    # Merge at 0.25: the rims weld (12 - 4 vertices); the upper and lower side
    # quads of each column then share one vertex set and one of each pair goes.
    bridge("bridge with merge at 0.25", (8, 12, 4), {"merge": True, "merge_factor": 0.25}, None)

    # Bad arguments are refused.
    select_on(e, BRIDGE_BOX, facets=faces)
    try:
        e.call("bridge_mesh_loops", {"connection": "spiral"})
        check_true("bridge_mesh_loops refuses an unknown connection", False, "no error")
    except RuntimeError:
        check_true("bridge_mesh_loops refuses an unknown connection", True)

    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})


def facet_winding(e, node_name, facet):
    """(Newell normal, centre, corner normals) of one facet, read back through
    get_mesh_attribute_values in the facet's corner order."""
    element = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": node_name, "domain": "facet", "indices": [facet]
    })["elements"][0]
    vertices = element["vertices"]
    p = vertex_positions(e, node_name, vertices)
    points = [p[v] for v in vertices]
    normal = [0.0, 0.0, 0.0]
    for i, a in enumerate(points):
        b = points[(i + 1) % len(points)]
        normal[0] += (a[1] - b[1]) * (a[2] + b[2])
        normal[1] += (a[2] - b[2]) * (a[0] + b[0])
        normal[2] += (a[0] - b[0]) * (a[1] + b[1])
    centre = [sum(point[k] for point in points) / len(points) for k in range(3)]
    corners = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": node_name, "domain": "corner", "indices": element["corners"]
    })["elements"]
    corner_normals = []
    for corner in corners:
        value = corner.get("attributes", {}).get("corner_normal")
        if isinstance(value, dict) and value.get("present", False):
            corner_normals.append(list(value["value"]))
    return normalize3(normal), centre, corner_normals


def outward_facets(e, node_name):
    """{facet: True when its winding normal points away from the mesh centre}."""
    vertex_count, _, facet_count = geometry_counts(e, node_name)
    p = vertex_positions(e, node_name, range(vertex_count))
    mesh_centre = [sum(point[k] for point in p.values()) / len(p) for k in range(3)]
    result = {}
    for facet in range(facet_count):
        normal, centre, _ = facet_winding(e, node_name, facet)
        result[facet] = dot3(normal, [centre[k] - mesh_centre[k] for k in range(3)]) > 0.0
    return result


def frame_node_count(e):
    return sum(1 for node in e.call("get_scene_nodes", {"scene_name": e.scene})["nodes"] if node["name"] == "frame node")


def run_normals_smooth(e):
    """Flip, recalculate normals and smooth vertices, doc/plans/mesh_modeling.md
    catalog M10."""
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    base = (8, 12, 6)
    all_out = {facet: True for facet in range(6)}
    expect("normals: plain box at its base counts", geometry_counts(e, BOX), base)
    expect("normals: every face of the plain box faces outward", outward_facets(e, BOX), all_out)

    # Flip one face.
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, BOX, facets=[0])
    before_normal, _, _ = facet_winding(e, BOX, 0)
    before = undo_count(e)
    expect("flip one face: queued", e.call("flip_mesh_facets").get("queued"), True)
    wait_idle(e)
    after_normal, _, corner_normals = facet_winding(e, BOX, 0)
    expect("flip one face: its normal reverses", round(dot3(before_normal, after_normal), 4), -1.0)
    for i, corner_normal in enumerate(corner_normals):
        check_true(f"flip one face: corner normal {i} follows the new winding", dot3(corner_normal, after_normal) > 0.99, str(corner_normal))
    expect("flip one face: counts kept (one edge per vertex pair)", geometry_counts(e, BOX), base)
    expect("flip one face: one undo entry", undo_count(e), before + 1)
    expect("flip one face: the face stays selected", entry_of(e.call("get_mesh_component_selection"), BOX)["facets"], [0])
    outward = outward_facets(e, BOX)
    expect("flip one face: only face 0 faces inward", outward, {**all_out, 0: False})
    undo_and_check(e, "flip one face", BOX, base)
    expect("flip one face: undo restores the winding", outward_facets(e, BOX), all_out)

    # Recalculate outside on a box with two flipped faces, face mode.
    def flip_two():
        e.call("set_mesh_component_mode", {"mode": "face"})
        select_on(e, BOX, facets=[0, 2])
        e.call("flip_mesh_facets")
        wait_idle(e)
        expect("recalculate: two faces flipped", outward_facets(e, BOX), {**all_out, 0: False, 2: False})

    flip_two()
    select_on(e, BOX, facets=list(range(6)))
    before = undo_count(e)
    expect("recalculate outside (face mode): queued", e.call("recalculate_mesh_normals", {"side": "outside"}).get("queued"), True)
    wait_idle(e)
    expect("recalculate outside (face mode): every face outward", outward_facets(e, BOX), all_out)
    expect("recalculate outside (face mode): one undo entry", undo_count(e), before + 1)
    expect("recalculate outside (face mode): counts kept", geometry_counts(e, BOX), base)
    undo_and_check(e, "recalculate outside (face mode)", BOX, base)

    # Inside, face mode.
    select_on(e, BOX, facets=list(range(6)))
    e.call("recalculate_mesh_normals", {"side": "inside"})
    wait_idle(e)
    expect("recalculate inside: every face inward", outward_facets(e, BOX), {facet: False for facet in range(6)})
    undo_and_check(e, "recalculate inside", BOX, base)

    # Object mode: the whole of the selected mesh.
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.call("recalculate_mesh_normals", {"scene_name": e.scene, "node_name": BOX})
    wait_idle(e)
    expect("recalculate outside (object mode, node target): every face outward", outward_facets(e, BOX), all_out)
    undo_and_check(e, "recalculate outside (object mode)", BOX, base)
    expect("recalculate: undo returns to the two flipped faces", outward_facets(e, BOX), {**all_out, 0: False, 2: False})

    # The keys, with the pointer over a viewport.
    viewport = e.call("get_viewports")["viewports"][0]
    x = viewport["x"] + (viewport["width"] / 2.0)
    y = viewport["y"] + (viewport["height"] / 2.0)
    e.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})
    e.advance(3)
    e.call("set_mesh_component_mode", {"mode": "face"})
    select_on(e, BOX, facets=list(range(6)))
    frames = frame_node_count(e)
    before = undo_count(e)
    e.key("n", ["shift"])
    wait_idle(e)
    expect("Shift+N: every face outward", outward_facets(e, BOX), all_out)
    expect("Shift+N: one undo entry", undo_count(e), before + 1)
    expect("Shift+N: no frame node", frame_node_count(e), frames)
    undo_and_check(e, "Shift+N", BOX, base)

    # Undo the two-face flip.
    undo_and_check(e, "flip two faces", BOX, base)
    expect("normals: undo returns the plain box", outward_facets(e, BOX), all_out)

    # N alone still creates a frame node; the mesh is untouched.
    e.call("clear_mesh_component_selection")
    e.call("set_mesh_component_mode", {"mode": "object"})
    frames = frame_node_count(e)
    e.key("n", [])
    wait_idle(e)
    expect("N: a frame node is created", frame_node_count(e), frames + 1)
    expect("N: the box counts are unchanged", geometry_counts(e, BOX), base)
    e.call("undo")
    wait_idle(e)
    expect("N: undo removes the frame node", frame_node_count(e), frames)

    # Smooth one vertex of the Catmull-Clark box with factor 1: it lands on
    # the average of its edge-connected neighbours.
    vertex_count, _, facet_count = geometry_counts(e, CC_BOX)
    facets = e.call("get_mesh_attribute_values", {
        "scene_name": e.scene, "node_name": CC_BOX, "domain": "facet", "indices": list(range(facet_count))
    })["elements"]
    vertex = 0
    neighbours = set()
    for element in facets:
        vertices = element["vertices"]
        for i, v in enumerate(vertices):
            if v == vertex:
                neighbours.add(vertices[i - 1])
                neighbours.add(vertices[(i + 1) % len(vertices)])
    p = vertex_positions(e, CC_BOX, [vertex] + sorted(neighbours))
    average = [sum(p[n][k] for n in neighbours) / len(neighbours) for k in range(3)]
    base_cc = geometry_counts(e, CC_BOX)
    e.call("set_mesh_component_mode", {"mode": "vertex"})
    select_on(e, CC_BOX, vertices=[vertex])
    before = undo_count(e)
    result = e.call("smooth_mesh_vertices", {"factor": 1.0})
    expect("smooth one vertex: queued", result.get("queued"), True)
    wait_idle(e)
    moved = vertex_position(e, CC_BOX, vertex)
    check_true("smooth one vertex with factor 1 lands on its neighbours' average", near(moved, average), f"{moved} vs {average}")
    expect("smooth one vertex: counts kept", geometry_counts(e, CC_BOX), base_cc)
    expect("smooth one vertex: one undo entry", undo_count(e), before + 1)
    expect("smooth one vertex: the vertex stays selected", entry_of(e.call("get_mesh_component_selection"), CC_BOX)["vertices"], [vertex])
    e.call("undo")
    wait_idle(e)
    check_true("smooth one vertex: undo restores the position", near(vertex_position(e, CC_BOX, vertex), p[vertex]), str(vertex_position(e, CC_BOX, vertex)))

    # Bad arguments are refused.
    select_on(e, CC_BOX, vertices=[vertex])
    try:
        e.call("smooth_mesh_vertices", {"factor": 2.0})
        check_true("smooth_mesh_vertices refuses factor 2", False, "no error")
    except RuntimeError:
        check_true("smooth_mesh_vertices refuses factor 2", True)
    try:
        e.call("recalculate_mesh_normals", {"side": "sideways"})
        check_true("recalculate_mesh_normals refuses an unknown side", False, "no error")
    except RuntimeError:
        check_true("recalculate_mesh_normals refuses an unknown side", True)

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

    run_inset(e)

    run_bevel(e)

    run_split_rip_separate(e)

    run_knife(e)

    run_fill_connect(e)

    run_bridge(e)

    run_normals_smooth(e)


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
