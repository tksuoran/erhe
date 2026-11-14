#!/usr/bin/env python3
"""Verify curve prims over MCP (doc/editor/curves.md "Verification").

Launches a headless editor (ERHE_AI_DRIVER=1, stopped by its own pid), creates
an empty scene and checks:

- create_curve makes one Curve prim in one undo entry; undo removes it, redo
  brings it back; the UI paths (the scene.create_new_curve command of
  Create > Curve, create_child_prim type curve) make the default curve;
- get_curve / get_curves read back what was given, in the prim's frame and in
  world space;
- set_curve replaces points and closure in one undo entry (none for an
  unchanged curve); undo and redo restore exactly; curve_set_point_kind applies
  the kind-change handle rule and undoes exactly; malformed arguments are
  refused and change nothing;
- evaluate_curve against known answers: a straight polyline (positions,
  tangent, up-keeping frame, clamping), an L-shaped polyline (the corner takes
  the leaving span, the end the arriving one), the cubic quarter-circle
  approximation (its length against a double-precision integration, its
  midpoint), a closed square wrapping both ways;
- a moved and turned prim evaluates in world space; a move of its parent too;
- duplicate, copy and paste and delete with undo keep the path;
- a glTF save and reopen reproduces every curve bit for bit (the file carries
  ERHE_curve);
- the change notification: one Curve_changed_message per change (set_curve,
  its undo, a move of the curve, a move of its parent), none without one;
- the Properties window's Curve rows: Closed and a typed point position, each
  one undo entry;
- a capture_screenshot of a selected curve with its points and handles is
  written (logs/curve_selected.png) and its path printed;
- (section lattice) the lattice tool's viewport editing: a click picks a
  control point, a gizmo X drag moves it, one undo entry, undo restores;
- (section viewport) the curve tool: picking points and handle ends by viewport
  clicks, gizmo axis / plane / Ctrl-snapped drags, Escape cancel, handle drags
  with and without Alt, a two-point move, transform_selection, Ctrl+click
  insert keeping the shape, Delete and its refusal, the kind and closed
  commands, the Properties At End button, curve_add_point / curve_remove_points
  / select_curve_points and their refusals, the change message count of a
  drag, the selection clearing, and a screenshot of a selected point with its
  handles (logs/curve_point_selection.png).

Scripted gizmo drags land about 1 px short (doc/plans/level_tools_implementation.md
step 20); the drag checks allow GIZMO_SHORTFALL_PX of the pointer travel and
print the measured shortfall.

The run restores config/editor/editor_settings.json, desktop_windows.json,
user_state.json and desktop_window_imgui_host_imgui.ini. The viewport cameras
are placed from the viewport's real size (frame_camera), so the checks do not
depend on the user's window layout.

- (section level) on a level built over MCP (a ground slab and the path
  layouts "Fence" and "Stepping Stones", each its own curve node carrying the
  layout values, R5.12.8): selecting a piece selects the layout node, the
  curve tool's target; a selected curve in the default style differs from
  the unselected view in many pixels (logs/curve_level_selected.png); after a
  gizmo drag of a point (the locked layout following) the point stays selected
  and a second drag needs no click; mid-drag (button held) the curve, its
  length in get_curve and level_get_layouts and the layouts' children follow,
  the piece count growing and shrinking, with no entry, and a screenshot shows
  them (logs/curve_level_mid_drag.png); Escape restores the children, node
  ids, cached brush count, navigation staleness, selection and undo depth
  exactly; the release records one entry equal to the last step, undo and
  redo exact; surface-snapped stones rest on a hill mid-drag; an unlocked
  layout does not follow; a curve width typed in the Settings window changes
  the drawing at once, and a magenta curve color and width written into
  editor_settings.json draw at the next start (logs/curve_level_style.png;
  an AI-driven run saves no config file).

Usage: python3 scripts/curve_verify.py [--editor <binary>] [--image <png path>] [--keep] [--sections base,lattice,viewport,level]
(default editor: build_ninja_linux_vulkan_headless/bin/editor when built, else build/Ninja_Vulkan_Debug/bin/editor)
"""

import argparse
import json
import math
import os
import shutil
import struct
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prepared_asset_verify as pav  # noqa: E402
from prepared_asset_verify import Editor, check  # noqa: E402
from mcp_viewport_isolation import isolate_viewport  # noqa: E402
import level_tools_verify as ltv  # noqa: E402 - the viewport projection and gizmo drag helpers

HEADLESS_EDITOR = "build_ninja_linux_vulkan_headless/bin/editor"
DEBUG_EDITOR    = "build/Ninja_Vulkan_Debug/bin/editor"
DEFAULT_IMAGE   = os.path.join("logs", "curve_selected.png")
POINTS_IMAGE    = os.path.join("logs", "curve_point_selection.png")
RESTORED_CONFIG_FILES = [os.path.join("config", "editor", name) for name in ("editor_settings.json", "desktop_windows.json", "user_state.json", "desktop_window_imgui_host_imgui.ini")]
TOLERANCE = 1.0e-5
KAPPA = 0.5522847498307936  # 4/3 (sqrt(2) - 1): the cubic quarter circle


def default_editor():
    return HEADLESS_EDITOR if os.path.isfile(os.path.join(pav.REPO_ROOT, HEADLESS_EDITOR)) else DEBUG_EDITOR


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def close(a, b, tolerance=TOLERANCE):
    if isinstance(a, (list, tuple)):
        return (len(a) == len(b)) and all(abs(x - y) <= tolerance for x, y in zip(a, b))
    return abs(a - b) <= tolerance


def point(position, kind="corner", in_handle=(0.0, 0.0, 0.0), out_handle=(0.0, 0.0, 0.0)):
    return {"position": list(position), "in": list(in_handle), "out": list(out_handle), "kind": kind}


def as_f32(points):
    """The points as the editor stores them: every number rounded to float32."""
    return [{"position": [f32(x) for x in p["position"]], "in": [f32(x) for x in p.get("in", (0, 0, 0))],
             "out": [f32(x) for x in p.get("out", (0, 0, 0))], "kind": p.get("kind", "corner")} for p in points]


def local_points(curve):
    return [{"position": p["position"], "in": p["in"], "out": p["out"], "kind": p["kind"]} for p in curve["points"]]


def undo_depth(editor):
    return len(editor.call("get_undo_redo_stack").get("undo", []))


def call_error(editor, tool, arguments):
    try:
        editor.call(tool, arguments)
    except RuntimeError as error:
        return str(error)
    return None


def create_scene(editor):
    before = set(editor.scene_names())
    editor.call("create_scene")
    deadline = time.monotonic() + 60.0
    while time.monotonic() < deadline:
        added = sorted(set(editor.scene_names()) - before)
        if added:
            editor.wait_idle()
            return added[0]
        time.sleep(0.1)
    raise RuntimeError("create_scene made no scene")


def curves(editor, scene):
    return editor.call("get_curves", {"scene_name": scene})


def get_curve(editor, scene, curve_id, world=False):
    return editor.call("get_curve", {"scene_name": scene, "curve_id": curve_id, "world": world})


def evaluate(editor, scene, curve_id, distances, **extra):
    arguments = {"scene_name": scene, "curve_id": curve_id, "distances": distances}
    arguments.update(extra)
    return editor.call("evaluate_curve", arguments)


def message_count(editor, scene):
    return curves(editor, scene)["change_message_count"]


def settle(editor, frames=3):
    """A few editor frames after an edit, so the frame's curve changes are flushed."""
    editor.wait_idle()
    for _ in range(frames):
        editor.call("get_async_status")


def quarter_arc_length():
    """Arc length of the cubic quarter-circle Bezier by composite Simpson in double precision."""
    p0, p1, p2, p3 = (1.0, 0.0), (1.0, KAPPA), (KAPPA, 1.0), (0.0, 1.0)

    def speed(t):
        u = 1.0 - t
        dx = 3 * u * u * (p1[0] - p0[0]) + 6 * u * t * (p2[0] - p1[0]) + 3 * t * t * (p3[0] - p2[0])
        dy = 3 * u * u * (p1[1] - p0[1]) + 6 * u * t * (p2[1] - p1[1]) + 3 * t * t * (p3[1] - p2[1])
        return math.hypot(dx, dy)
    n = 20000
    h = 1.0 / n
    total = speed(0.0) + speed(1.0)
    for i in range(1, n):
        total += (4 if (i % 2) else 2) * speed(i * h)
    return total * h / 3.0


def check_create_and_read(editor, scene):
    depth = undo_depth(editor)
    given = [point((0.0, 0.0, 0.0)), point((3.0, 0.0, 0.0)), point((3.0, 0.0, 4.0))]
    created = editor.call("create_curve", {"scene_name": scene, "name": "L curve", "points": given})
    editor.wait_idle()
    check("create_curve makes one curve in one undo entry", undo_depth(editor) == depth + 1, f"undo {depth} -> {undo_depth(editor)}")
    check("create_curve reply: length 7, point distances [0, 3, 7], open, 3 points, 2 spans",
          close(created["length"], 7.0) and close(created["point_distances"], [0.0, 3.0, 7.0]) and (created["closed"] is False)
          and (created["point_count"] == 3) and (created["span_count"] == 2), json.dumps(created)[:400])
    read = get_curve(editor, scene, created["id"])
    check("get_curve reads back the given points in the prim's frame", local_points(read) == as_f32(given), json.dumps(read["points"]))
    listed = curves(editor, scene)
    check("get_curves lists the curve", [c["id"] for c in listed["curves"]] == [created["id"]] and listed["count"] == 1, json.dumps(listed)[:300])
    nodes = {n["id"]: n for n in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]}
    check("the scene lists the curve prim, of type Curve", (created["id"] in nodes) and (created["type"] == "Curve"), created["type"])

    editor.call("undo")
    editor.wait_idle()
    check("undo of create_curve removes the curve", curves(editor, scene)["count"] == 0)
    editor.call("redo")
    editor.wait_idle()
    again = curves(editor, scene)
    check("redo of create_curve brings the same curve back", [c["id"] for c in again["curves"]] == [created["id"]])
    check("the restored curve has the same points", local_points(get_curve(editor, scene, created["id"])) == as_f32(given))
    return created["id"], created["parent_id"]


def check_ui_create(editor, scene, root_id):
    before = {c["id"] for c in curves(editor, scene)["curves"]}
    depth = undo_depth(editor)
    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.call("scene.create_new_curve")
    editor.wait_idle()
    after = [c for c in curves(editor, scene)["curves"] if c["id"] not in before]
    check("the scene.create_new_curve command (Create > Curve) makes one default curve in one undo entry",
          (len(after) == 1) and (undo_depth(editor) == depth + 1), f"{len(after)} new, undo {depth} -> {undo_depth(editor)}")
    made = []
    if after:
        default = get_curve(editor, scene, after[0]["id"])
        expected = as_f32([point((0.0, 0.0, 0.0), "smooth", (-0.25, 0.0, 0.25), (0.25, 0.0, -0.25)),
                           point((1.0, 0.0, 0.0), "smooth", (-0.25, 0.0, -0.25), (0.25, 0.0, 0.25))])
        check("the default curve: two smooth points 1 m apart along +X arcing towards -Z", local_points(default) == expected, json.dumps(default["points"]))
        made.append(after[0]["id"])
    before = {c["id"] for c in curves(editor, scene)["curves"]}
    editor.call("create_child_prim", {"scene_name": scene, "type": "curve", "node_id": root_id})
    editor.wait_idle()
    after = [c for c in curves(editor, scene)["curves"] if c["id"] not in before]
    check("create_child_prim type curve (the Hierarchy Create menu catalog) makes one curve", len(after) == 1, json.dumps(after)[:200])
    made += [c["id"] for c in after]
    if made:
        editor.call("delete_nodes", {"scene_name": scene, "ids": made})
        editor.wait_idle()


def check_set_curve(editor, scene, curve_id):
    before = local_points(get_curve(editor, scene, curve_id))
    depth = undo_depth(editor)
    given = [point((0.1, 0.2, 0.3), "free", (-0.5, 0.0, 0.0), (0.0, 0.0, 0.7)),
             point((2.0, -0.0, 1.0), "smooth", (-0.25, 0.0, -0.125), (0.5, 0.0, 0.25)),
             point((4.0, 1.0, 1.0 / 3.0))]
    reply = editor.call("set_curve", {"scene_name": scene, "curve_id": curve_id, "points": given, "closed": True})
    editor.wait_idle()
    after = get_curve(editor, scene, curve_id)
    check("set_curve replaces points and closure in one undo entry",
          reply["changed"] and (undo_depth(editor) == depth + 1) and after["closed"] and (local_points(after) == as_f32(given)),
          f"undo {depth} -> {undo_depth(editor)}, closed {after['closed']}, {json.dumps(after['points'])[:300]}")
    same = editor.call("set_curve", {"scene_name": scene, "curve_id": curve_id, "points": given})
    editor.wait_idle()
    check("set_curve with the curve's own values records nothing", (not same["changed"]) and (undo_depth(editor) == depth + 1))
    editor.call("undo")
    editor.wait_idle()
    undone = get_curve(editor, scene, curve_id)
    check("undo of set_curve restores the points, handles, kinds and closure exactly", (local_points(undone) == before) and (undone["closed"] is False))
    editor.call("redo")
    editor.wait_idle()
    redone = get_curve(editor, scene, curve_id)
    check("redo of set_curve applies it again exactly", (local_points(redone) == as_f32(given)) and redone["closed"])

    # The kind change rule: from corner gives Catmull-Rom handles (a third of half the neighbor difference).
    depth = undo_depth(editor)
    kind = editor.call("curve_set_point_kind", {"scene_name": scene, "curve_id": curve_id, "index": 2, "kind": "smooth"})
    editor.wait_idle()
    p = kind["points"]
    # Closed: neighbors of point 2 are points 1 and 0; tangent (p0 - p1) / 2, handles +-1/3 of it.
    t = [(f32(0.1) - 2.0) / 2.0, (f32(0.2) - 0.0) / 2.0, (f32(0.3) - 1.0) / 2.0]
    check("curve_set_point_kind corner -> smooth gives Catmull-Rom handles in one undo entry",
          kind["changed"] and (undo_depth(editor) == depth + 1) and (p[2]["kind"] == "smooth")
          and close(p[2]["out"], [x / 3.0 for x in t], 1e-6) and close(p[2]["in"], [-x / 3.0 for x in t], 1e-6),
          json.dumps(p[2]))
    editor.call("undo")
    editor.wait_idle()
    check("undo of curve_set_point_kind restores the corner with its zero handles", local_points(get_curve(editor, scene, curve_id)) == as_f32(given))

    depth = undo_depth(editor)
    state = local_points(get_curve(editor, scene, curve_id))
    refusals = [
        ("one point", {"points": [point((0, 0, 0))]}),
        ("an unknown point key", {"points": [point((0, 0, 0)), dict(point((1, 0, 0)), handle=[0, 0, 0])]}),
        ("an unknown kind", {"points": [point((0, 0, 0)), point((1, 0, 0), "pointy")]}),
        ("a two-component position", {"points": [point((0, 0, 0)), {"position": [1, 0]}]}),
        ("a non-boolean closed", {"closed": 1}),
        ("an unknown argument", {"closure": True}),
        ("no points and no closed", {}),
    ]
    for label, extra in refusals:
        arguments = {"scene_name": scene, "curve_id": curve_id}
        arguments.update(extra)
        error = call_error(editor, "set_curve", arguments)
        check(f"set_curve refuses {label}", error is not None, str(error))
    check("a refused set_curve changes nothing and records nothing",
          (undo_depth(editor) == depth) and (local_points(get_curve(editor, scene, curve_id)) == state))
    check("curve_set_point_kind refuses an index past the end",
          call_error(editor, "curve_set_point_kind", {"scene_name": scene, "curve_id": curve_id, "index": 3, "kind": "free"}) is not None)


def check_evaluation(editor, scene):
    straight = editor.call("create_curve", {"scene_name": scene, "name": "straight", "points": [point((0, 0, 0)), point((10, 0, 0))]})
    editor.wait_idle()
    e = evaluate(editor, scene, straight["id"], [2.5, -1.0, 20.0])
    s0, s1, s2 = e["samples"]
    check("straight polyline: length 10, position at 2.5, tangent +X, up +Y, side -Z",
          close(e["length"], 10.0) and close(s0["position"], [2.5, 0, 0]) and close(s0["tangent"], [1, 0, 0])
          and close(s0["up"], [0, 1, 0]) and close(s0["side"], [0, 0, -1]), json.dumps(s0))
    check("straight polyline: distances clamp to the ends of an open curve",
          close(s1["position"], [0, 0, 0]) and close(s2["position"], [10, 0, 0]) and close(s1["normalized_distance"], 0.0)
          and close(s2["normalized_distance"], 10.0), json.dumps([s1, s2]))

    l_shape = editor.call("create_curve", {"scene_name": scene, "name": "L", "points": [point((0, 0, 0)), point((3, 0, 0)), point((3, 0, 4))]})
    editor.wait_idle()
    e = evaluate(editor, scene, l_shape["id"], [2.999, 3.0, 5.0, 7.0])
    a, b, c, d = e["samples"]
    check("L polyline: before the corner the tangent is +X, at the corner the leaving +Z",
          close(a["tangent"], [1, 0, 0]) and close(b["tangent"], [0, 0, 1]) and close(b["position"], [3, 0, 0]), json.dumps([a, b]))
    check("L polyline: at 5 the point (3, 0, 2); the end takes the arriving span",
          close(c["position"], [3, 0, 2]) and close(d["position"], [3, 0, 4]) and close(d["tangent"], [0, 0, 1]), json.dumps([c, d]))
    check("L polyline: the frame keeps up +Y and side = cross(up, tangent) on the second segment",
          close(c["up"], [0, 1, 0]) and close(c["side"], [1, 0, 0]), json.dumps(c))

    arc = editor.call("create_curve", {"scene_name": scene, "name": "arc", "points": [
        point((1, 0, 0), "free", (0, 0, 0), (0, 0, KAPPA)),
        point((0, 0, 1), "free", (KAPPA, 0, 0), (0, 0, 0))]})
    editor.wait_idle()
    reference = quarter_arc_length()
    length = arc["length"]
    check("quarter arc: length within 1e-6 relative of the double-precision integral (doc/erhe/math.md accuracy)",
          abs(length - reference) <= 1.0e-6 * reference, f"{length!r} vs {reference!r}")
    check("quarter arc: the approximation is within 3e-4 of pi / 2", abs(reference - math.pi / 2) < 3e-4, f"{reference}")
    e = evaluate(editor, scene, arc["id"], [0.5 * length, 0.0, length])
    mid, start, end = e["samples"]
    h = math.sqrt(0.5)
    check("quarter arc: the midpoint by distance is (sqrt(1/2), 0, sqrt(1/2)) with tangent (-1, 0, 1) / sqrt(2)",
          close(mid["position"], [h, 0, h], 2e-5) and close(mid["tangent"], [-h, 0, h], 2e-5), json.dumps(mid))
    check("quarter arc: the ends have tangents +Z and -X", close(start["tangent"], [0, 0, 1]) and close(end["tangent"], [-1, 0, 0]), json.dumps([start, end]))

    square = editor.call("create_curve", {"scene_name": scene, "name": "square", "closed": True,
                                          "points": [point((0, 0, 0)), point((2, 0, 0)), point((2, 0, 2)), point((0, 0, 2))]})
    editor.wait_idle()
    e = evaluate(editor, scene, square["id"], [9.0, -1.0, 8.0, 5.0])
    w1, w2, w3, w4 = e["samples"]
    check("closed square: length 8; 9 wraps to 1, -1 to 7, 8 to 0",
          close(e["length"], 8.0) and close(w1["position"], [1, 0, 0]) and close(w2["position"], [0, 0, 1])
          and close(w3["position"], [0, 0, 0]) and close(w2["normalized_distance"], 7.0) and close(w3["tangent"], [1, 0, 0]),
          json.dumps(e["samples"]))
    check("closed square: at 5 the point (1, 0, 2) heading -X", close(w4["position"], [1, 0, 2]) and close(w4["tangent"], [-1, 0, 0]), json.dumps(w4))
    return straight["id"], l_shape["id"], arc["id"], square["id"]


def rotate_y(v, degrees):
    r = math.radians(degrees)
    c, s = math.cos(r), math.sin(r)
    return [c * v[0] + s * v[2], v[1], -s * v[0] + c * v[2]]


def check_world_space(editor, scene, l_id):
    q = [0.0, math.sin(math.radians(45.0)), 0.0, math.cos(math.radians(45.0))]  # 90 degrees about +Y
    depth = undo_depth(editor)
    editor.call("set_node_transform", {"scene_name": scene, "node_id": l_id, "space": "world", "translation": [5.0, 1.0, 0.0], "rotation_xyzw": q})
    editor.wait_idle()
    e = evaluate(editor, scene, l_id, [5.0, 1.0])
    s5, s1 = e["samples"]
    expected5 = [x + t for x, t in zip(rotate_y([3, 0, 2], 90.0), [5.0, 1.0, 0.0])]
    check("a moved and turned curve evaluates in world space (position and tangent)",
          close(s5["position"], expected5, 1e-4) and close(s5["tangent"], rotate_y([0, 0, 1], 90.0), 1e-5)
          and close(s5["local_position"], [3, 0, 2]) and close(s1["tangent"], rotate_y([1, 0, 0], 90.0), 1e-5),
          json.dumps(s5))
    world = get_curve(editor, scene, l_id, world=True)
    check("get_curve world gives the points in world space",
          close(world["points"][2]["world_position"], [x + t for x, t in zip(rotate_y([3, 0, 4], 90.0), [5.0, 1.0, 0.0])], 1e-4),
          json.dumps(world["points"][2]))

    # A parent's move carries the curve.
    parent = editor.call("create_node", {"scene_name": scene, "name": "curve parent"})
    editor.wait_idle()
    child = editor.call("create_curve", {"scene_name": scene, "name": "child curve", "parent_id": parent["node_id"],
                                         "points": [point((0, 0, 0)), point((1, 0, 0))]})
    editor.wait_idle()
    editor.call("set_node_transform", {"scene_name": scene, "node_id": parent["node_id"], "space": "world", "translation": [0.0, 0.0, -3.0]})
    editor.wait_idle()
    e = evaluate(editor, scene, child["id"], [0.5])
    check("a move of the parent moves the curve's world-space evaluation", close(e["samples"][0]["position"], [0.5, 0.0, -3.0], 1e-5), json.dumps(e["samples"][0]))
    # Undo the parent move, the child curve, the parent and the L curve move.
    while undo_depth(editor) > depth:
        editor.call("undo")
        editor.wait_idle()
    e = evaluate(editor, scene, l_id, [5.0])
    check("undo of the move restores the world-space evaluation", close(e["samples"][0]["position"], [3, 0, 2], 1e-5), json.dumps(e["samples"][0]))


def check_duplicate_copy_delete(editor, scene, arc_id):
    path = local_points(get_curve(editor, scene, arc_id))
    before = {c["id"] for c in curves(editor, scene)["curves"]}
    editor.call("select_items", {"scene_name": scene, "ids": [arc_id]})
    editor.wait_idle()
    editor.call("Selection.duplicate")
    editor.wait_idle()
    added = [c for c in curves(editor, scene)["curves"] if c["id"] not in before]
    check("duplicate of a selected curve makes one curve with the same path",
          (len(added) == 1) and (local_points(get_curve(editor, scene, added[0]["id"])) == path), json.dumps(added)[:300])
    if added:
        editor.call("undo")
        editor.wait_idle()
        check("undo of the duplicate removes it", added[0]["id"] not in {c["id"] for c in curves(editor, scene)["curves"]})

    editor.call("clipboard_copy_nodes", {"scene_name": scene, "node_ids": [arc_id]})
    editor.call("clipboard_paste", {"scene_name": scene})
    editor.wait_idle()
    pasted = [c for c in curves(editor, scene)["curves"] if c["id"] not in before]
    check("copy and paste of a curve makes one curve with the same path",
          (len(pasted) == 1) and (local_points(get_curve(editor, scene, pasted[0]["id"])) == path), json.dumps(pasted)[:300])
    if pasted:
        editor.call("undo")
        editor.wait_idle()

    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.call("delete_nodes", {"scene_name": scene, "ids": [arc_id]})
    editor.wait_idle()
    check("delete removes the curve", arc_id not in {c["id"] for c in curves(editor, scene)["curves"]})
    editor.call("undo")
    editor.wait_idle()
    check("undo of the delete brings the curve back with its path",
          (arc_id in {c["id"] for c in curves(editor, scene)["curves"]}) and (local_points(get_curve(editor, scene, arc_id)) == path))


def check_notification(editor, scene, square_id):
    settle(editor)
    count0 = message_count(editor, scene)
    settle(editor, 5)
    check("no Curve_changed_message without a change", message_count(editor, scene) == count0, f"{count0} -> {message_count(editor, scene)}")

    def one_message(label, action):
        settle(editor)
        before = message_count(editor, scene)
        action()
        settle(editor)
        after = message_count(editor, scene)
        check(f"{label}: exactly one Curve_changed_message", after == before + 1, f"{before} -> {after}")

    square = get_curve(editor, scene, square_id)
    moved = local_points(square)
    moved[0]["position"] = [0.0, 0.5, 0.0]
    one_message("set_curve", lambda: editor.call("set_curve", {"scene_name": scene, "curve_id": square_id, "points": moved}))
    one_message("undo of set_curve", lambda: editor.call("undo"))
    one_message("redo of set_curve", lambda: editor.call("redo"))
    one_message("a move of the curve", lambda: editor.call("set_node_transform", {"scene_name": scene, "node_id": square_id, "space": "world", "translation": [0.0, 0.0, 5.0]}))
    one_message("undo of the move", lambda: editor.call("undo"))
    editor.call("undo")
    editor.wait_idle()
    reply = get_curve(editor, scene, square_id)
    check("get_curve reports the curve's revision and the scene's message count", ("revision" in reply) and (reply["change_message_count"] >= count0 + 5))


def check_persistence(editor, scene, work):
    before = {c["id"]: get_curve(editor, scene, c["id"]) for c in curves(editor, scene)["curves"]}
    by_name = {c["name"]: c for c in before.values()}
    path = os.path.join(work, "curves.gltf")
    editor.save(scene, path)
    with open(path, encoding="utf-8") as handle:
        document = json.load(handle)
    carried = [n for n in document.get("nodes", []) if "ERHE_curve" in n.get("extensions", {})]
    check("the saved glTF lists ERHE_curve and carries one node per curve",
          ("ERHE_curve" in document.get("extensionsUsed", [])) and (len(carried) == len(before)) and ("ERHE_curve" not in document.get("extensionsRequired", [])),
          f"{len(carried)} nodes vs {len(before)} curves")
    editor.close(scene)
    reopened = editor.load(path)
    after = {c["name"]: get_curve(editor, reopened, c["id"]) for c in curves(editor, reopened)["curves"]}
    same = (set(after) == set(by_name)) and all(
        (local_points(after[name]) == local_points(by_name[name])) and (after[name]["closed"] == by_name[name]["closed"])
        and (after[name]["length"] == by_name[name]["length"]) and close(after[name]["world_position"], by_name[name]["world_position"], 0.0)
        for name in by_name)
    check("a glTF save and reopen reproduces every curve bit for bit (points, closure, length, world position)", same,
          json.dumps({name: local_points(c) for name, c in after.items()})[:600])
    return reopened


def find_items(editor, window, label=None):
    arguments = {"window": window, "limit": 2000, "visible_only": False}
    if label is not None:
        arguments["label_contains"] = label
    return editor.call("get_imgui_items", arguments).get("items", [])


def check_properties(editor, scene):
    made = editor.call("create_curve", {"scene_name": scene, "name": "properties curve", "points": [point((0, 0, 0)), point((1, 0, 0)), point((1, 0, 1))]})
    editor.wait_idle()
    curve_id = made["id"]
    editor.call("select_items", {"scene_name": scene, "ids": [curve_id]})
    window = "Properties"
    editor.call("set_window_visibility", {"title": window, "visible": True, "focus": True})
    editor.wait_idle()
    header = next((i for i in find_items(editor, window, "Curve") if i.get("display_label") == "Curve"), None)
    if (header is not None) and not header.get("status", {}).get("opened"):
        editor.call("imgui_scroll_to_item", {"window": window, "id": header["id"]})
        editor.call("imgui_click", {"window": window, "id": header["id"]})
        editor.wait_idle()
    items = find_items(editor, window)
    labels = [i.get("display_label") for i in items]
    # Text rows (Points, Length) submit no item; a widget's item carries its row label.
    check("the Properties window shows the Curve group with its Closed row and the point groups with Position and Kind",
          all(label in labels for label in ("Curve", "Closed", "Point 0", "Point 2", "Position.x", "Position.z", "Kind")),
          json.dumps(sorted(set(str(x) for x in labels)))[:600])

    checkbox = next((i for i in items if i.get("display_label") == "Closed"), None)
    if checkbox is None:
        check("the Closed row has a checkbox", False)
    else:
        depth = undo_depth(editor)
        editor.call("imgui_scroll_to_item", {"window": window, "id": checkbox["id"]})
        editor.call("imgui_click", {"window": window, "id": checkbox["id"]})
        editor.wait_idle()
        check("a click on Closed closes the curve in one undo entry",
              get_curve(editor, scene, curve_id)["closed"] and (undo_depth(editor) == depth + 1), f"undo {depth} -> {undo_depth(editor)}")

    # Point 0 Position x: ctrl+click to type a value.
    items = find_items(editor, window)
    drags = sorted((i for i in items if i.get("display_label") == "Position.x"), key=lambda i: i["center_y"])
    if not drags:
        check("the point rows have a Position row", False)
        return curve_id
    depth = undo_depth(editor)
    editor.call("imgui_scroll_to_item", {"window": window, "id": drags[0]["id"]})
    editor.call("imgui_click", {"window": window, "id": drags[0]["id"], "modifiers": ["ctrl"]})
    editor.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
    editor.call("type_text", {"text": "0.75"})
    editor.call("key_press", {"key": "enter"})
    editor.wait_idle()
    read = get_curve(editor, scene, curve_id)
    check("a typed Point 0 Position x sets the point in one undo entry",
          close(read["points"][0]["position"], [0.75, 0.0, 0.0], 1e-6) and (undo_depth(editor) == depth + 1),
          f"position {read['points'][0]['position']}, undo {depth} -> {undo_depth(editor)}")
    editor.call("undo")
    editor.wait_idle()
    check("undo of the typed position restores the point", close(get_curve(editor, scene, curve_id)["points"][0]["position"], [0.0, 0.0, 0.0], 0.0))
    return curve_id


def capture_selected(editor, scene, image_path):
    shown = editor.call("create_curve", {"scene_name": scene, "name": "shown curve", "position": [0.0, 0.3, 0.0], "points": [
        point((-0.7, 0.0, 0.0), "smooth", (-0.2, 0.0, 0.2), (0.2, 0.0, -0.2)),
        point((0.0, 0.25, -0.2), "free", (-0.25, 0.0, 0.0), (0.2, 0.2, 0.0)),
        point((0.45, 0.0, 0.1)),
        point((0.7, 0.3, 0.35), "smooth", (0.0, -0.2, -0.2), (0.0, 0.2, 0.2))]})
    editor.wait_idle()
    editor.call("select_items", {"scene_name": scene, "ids": [shown["id"]]})
    editor.wait_idle()
    try:
        isolate_viewport(editor.call, scene_name=scene)
    except RuntimeError as error:
        print(f"viewport isolation: {error}", flush=True)
    settle(editor, 5)
    absolute = image_path if os.path.isabs(image_path) else os.path.join(pav.REPO_ROOT, image_path)
    os.makedirs(os.path.dirname(absolute), exist_ok=True)
    reply = editor.call("capture_screenshot", {"path": absolute})
    written = reply.get("path", absolute)
    check("capture_screenshot of the selected curve is written", os.path.isfile(written), json.dumps(reply)[:200])
    print(f"selected curve image: {written}", flush=True)


# Viewport editing

def point_at(viewport, world):
    return ltv.window_point(viewport, world)


def set_camera(editor, scene, position, target):
    """Points the scene's camera from position at target (no roll; +Y up)."""
    camera = editor.call("get_scene_cameras", {"scene_name": scene})["cameras"][0]
    d = [target[i] - position[i] for i in range(3)]
    yaw = math.atan2(-d[0], -d[2])
    pitch = math.atan2(d[1], math.hypot(d[0], d[2]))
    qy = [0.0, math.sin(0.5 * yaw), 0.0, math.cos(0.5 * yaw)]
    qx = [math.sin(0.5 * pitch), 0.0, 0.0, math.cos(0.5 * pitch)]
    # q = qy * qx
    q = [qy[3] * qx[0] + qy[0] * qx[3] + qy[1] * qx[2] - qy[2] * qx[1],
         qy[3] * qx[1] - qy[0] * qx[2] + qy[1] * qx[3] + qy[2] * qx[0],
         qy[3] * qx[2] + qy[0] * qx[1] - qy[1] * qx[0] + qy[2] * qx[3],
         qy[3] * qx[3] - qy[0] * qx[0] - qy[1] * qx[1] - qy[2] * qx[2]]
    editor.call("set_node_transform", {"scene_name": scene, "node_id": camera["id"], "space": "world", "translation": list(position), "rotation_xyzw": q})
    editor.wait_idle()


def viewport_of(editor, scene):
    isolate_viewport(editor.call, scene_name=scene)
    editor.wait_idle()
    return next(v for v in editor.call("get_viewports")["viewports"] if v.get("scene") == scene)


# The points a section clicks or drags stay this far in from the viewport's edges (ndc), leaving the rest for the
# gizmo's arrows and the drags' travel, whatever the viewport's size and aspect (the user's window layout).
FRAME_NDC = 0.55


def ndc_of(viewport, world):
    """The normalized device coordinates of a world point in the viewport, None behind the camera."""
    m = viewport["camera_world_from_camera"]
    right, up, back, origin = m[0:3], m[4:7], m[8:11], m[12:15]
    d = [world[i] - origin[i] for i in range(3)]
    depth = -sum(d[i] * back[i] for i in range(3))
    if depth <= 0.0:
        return None
    tan_y = math.tan(0.5 * viewport["camera_fov_y"])
    tan_x = tan_y * viewport["width"] / viewport["height"]
    return (sum(d[i] * right[i] for i in range(3)) / (depth * tan_x), sum(d[i] * up[i] for i in range(3)) / (depth * tan_y))


def frame_camera(editor, scene, position, target, keep, limit=FRAME_NDC, min_px_per_metre=0.0, tries=12):
    """set_camera from position at target, moved back along the view line until every keep point projects within
    limit of the isolated viewport's center; returns the viewport, or raises naming the viewport's size. A section
    whose picks need a scale (a handle end clear of the gizmo's arrow) names it in min_px_per_metre."""
    eye = list(position)
    worst = None
    viewport = None
    for _ in range(tries):
        set_camera(editor, scene, eye, target)
        viewport = viewport_of(editor, scene)
        coordinates = [ndc_of(viewport, point) for point in keep]
        worst = max((max(abs(c[0]), abs(c[1])) if c is not None else math.inf) for c in coordinates)
        if worst <= limit:
            distance = math.sqrt(sum((eye[i] - target[i]) ** 2 for i in range(3)))
            px_per_metre = 0.5 * viewport["height"] / (distance * math.tan(0.5 * viewport["camera_fov_y"]))
            if px_per_metre < min_px_per_metre:
                raise RuntimeError(
                    f"viewport '{viewport.get('title')}' {viewport.get('width')}x{viewport.get('height')} px is too small: framing "
                    f"{len(keep)} points within ndc {limit} gives {px_per_metre:.0f} px/m, the section needs {min_px_per_metre:.0f}"
                )
            return viewport
        eye = [target[i] + 1.25 * (eye[i] - target[i]) for i in range(3)]
    raise RuntimeError(
        f"cannot frame {len(keep)} points within ndc {limit} in viewport '{viewport and viewport.get('title')}' "
        f"{viewport and viewport.get('width')}x{viewport and viewport.get('height')} px (worst {worst:.2f} at eye {[round(e, 2) for e in eye]})"
    )


def anchor(editor):
    return editor.call("get_transform_state")["anchor_frame"]["translation"]


def choose_move_tool(editor, viewport):
    x = viewport["x"] + 0.5 * viewport["width"]
    y = viewport["y"] + 0.9 * viewport["height"]
    editor.call("mouse_wheel", {"x": x, "y": y, "dy": 0.0})
    editor.call("key_press", {"key": ltv.binding_input(editor, "Hotbar.activate_slot_1", "key")})
    editor.wait_idle()


TRANSLATE_Z = (5, 6)
GIZMO_SHORTFALL_PX = 1.5  # scripted gizmo drags land about 1 px short (doc/plans/level_tools_implementation.md step 20)


def gizmo_drag(editor, viewport, values, delta, hold=False):
    """ltv.gizmo_drag, returning the pointer travel in pixels per metre of delta."""
    handle = ltv.gizmo_handle(editor, viewport, values)
    if handle is None:
        raise RuntimeError(f"no gizmo handle {values} on screen")
    start = point_at(viewport, handle["world"])
    to = point_at(viewport, [handle["world"][i] + delta[i] for i in range(3)])
    ltv.gizmo_drag(editor, viewport, values, delta, hold)
    return math.hypot(to[0] - start[0], to[1] - start[1]) / math.sqrt(sum(d * d for d in delta))


def drag_tolerance(px_per_metre):
    return GIZMO_SHORTFALL_PX / px_per_metre


def report_shortfall(label, moved, expected, px_per_metre):
    print(f"{label}: moved {moved:.6f} of {expected:.6f} m = {(expected - moved) * px_per_metre:.2f} px short at {px_per_metre:.1f} px/m", flush=True)


def lattice_offsets(editor, lattice_id):
    graph = editor.call("get_geometry_graph")
    node = next(n for n in graph["nodes"] if n["id"] == lattice_id)
    return node["parameters"]["offsets"]


def check_lattice_viewport(editor):
    """The lattice tool's viewport editing: click picks a control point, a gizmo X drag moves it, one undo entry."""
    scene = create_scene(editor)
    editor.call("set_active_scene", {"scene_name": scene})
    editor.call("create_graph_mesh", {"name": "lattice graph", "scene_name": scene})
    ids = {t: editor.call("geometry_graph_add_node", {"type": t})["id"] for t in ("box", "lattice", "output")}
    for a, b in (("box", "lattice"), ("lattice", "output")):
        editor.call("geometry_graph_connect", {"source_node_id": ids[a], "source_slot": 0, "sink_node_id": ids[b], "sink_slot": 0})
    editor.call("geometry_graph_set_parameter", {"node_id": ids["lattice"], "parameters": {"auto_fit": False, "cage_min": [-1, -1, -1], "cage_max": [1, 1, 1], "divisions": [1, 1, 1]}})
    editor.call("get_geometry_graph")
    editor.call("create_node", {"name": "lattice node", "scene_name": scene})
    editor.wait_idle()
    editor.call("set_node_graph_mesh", {"scene_name": scene, "node_name": "lattice node", "graph_mesh": "lattice graph"})
    editor.call("geometry_graph_set_display_flags", {"node_id": ids["lattice"], "display": True})
    editor.call("get_geometry_graph")
    # The cage, and point (1, 1, 1) as far as its two 0.5 m drags take it.
    cage = [[x, y, z] for x in (-1.0, 1.0) for y in (-1.0, 1.0) for z in (-1.0, 1.0)] + [[2.0, 1.0, 1.0]]
    viewport = frame_camera(editor, scene, [4.0, 3.0, 6.0], [0.5, 0.0, 0.0], cage)
    choose_move_tool(editor, viewport)
    settle(editor)
    state = editor.call("get_transform_state")
    check("lattice: a designated, bound lattice owns the gizmo, anchored at its selected point (0, 0, 0) = cage min",
          state["component_mode"] and close(state["anchor_frame"]["translation"], [-1, -1, -1], 1e-5), json.dumps(state)[:300])

    # Point (1, 1, 1) is index 7 of the 2x2x2 points; offsets are 3 floats per point.
    x, y = point_at(viewport, [1.0, 1.0, 1.0])
    depth = undo_depth(editor)
    editor.call("mouse_click", {"x": x + 4.0, "y": y - 3.0})
    editor.wait_idle()
    settle(editor)
    check("lattice: a click 5 px off point (1, 1, 1) selects it (the gizmo moves there), no undo entry",
          close(anchor(editor), [1, 1, 1], 1e-5) and (undo_depth(editor) == depth), f"{anchor(editor)}, undo {depth} -> {undo_depth(editor)}")
    editor.call("mouse_click", {"x": viewport["x"] + 30.0, "y": viewport["y"] + 30.0})
    editor.wait_idle()
    settle(editor)
    check("lattice: a click on empty space keeps the selected control point", close(anchor(editor), [1, 1, 1], 1e-5), f"{anchor(editor)}")

    px_per_metre = gizmo_drag(editor, viewport, ltv.TRANSLATE_X, [0.5, 0.0, 0.0])
    editor.call("get_geometry_graph")
    settle(editor)
    offsets = lattice_offsets(editor, ids["lattice"])
    moved = offsets[21:24]
    others = offsets[:21]
    report_shortfall("lattice gizmo X drag", moved[0], 0.5, px_per_metre)
    check(f"lattice: a gizmo X drag of 0.5 moves point (1, 1, 1) along X only (within {GIZMO_SHORTFALL_PX} px), the others stay, one undo entry",
          close(moved, [0.5, 0.0, 0.0], drag_tolerance(px_per_metre)) and (moved[1] == 0.0) and (moved[2] == 0.0) and all(v == 0.0 for v in others) and (undo_depth(editor) == depth + 1),
          f"{moved}, undo {depth} -> {undo_depth(editor)}")
    check("lattice: the gizmo follows the moved point", close(anchor(editor), [1.0 + moved[0], 1.0 + moved[1], 1.0 + moved[2]], 1e-5), f"{anchor(editor)}")
    # A second drag without a new click moves the same point again (the gizmo stays on it).
    gizmo_drag(editor, viewport, ltv.TRANSLATE_X, [0.5, 0.0, 0.0])
    editor.call("get_geometry_graph")
    settle(editor)
    again = lattice_offsets(editor, ids["lattice"])[21:24]
    check("lattice: after the drag the point stays selected: a second X drag without a click moves it again, a second undo entry",
          editor.call("get_transform_state")["component_mode"] and (again[0] > moved[0] + 0.3) and (undo_depth(editor) == depth + 2),
          f"{moved} -> {again}, undo {depth} -> {undo_depth(editor)}")
    editor.call("undo")
    editor.call("undo")
    editor.call("get_geometry_graph")
    settle(editor)
    check("lattice: undo restores every offset to zero", all(v == 0.0 for v in lattice_offsets(editor, ids["lattice"])))
    editor.call("geometry_graph_set_display_flags", {"node_id": ids["lattice"], "display": False})
    editor.call("get_geometry_graph")
    settle(editor)
    check("lattice: clearing the designation returns the gizmo to the node selection", not editor.call("get_transform_state")["component_mode"])
    editor.call("close_scene", {"scene_name": scene})
    editor.wait_idle()



def selection_of(editor):
    return editor.call("get_curve_point_selection")


def world_points(editor, scene, curve_id):
    return get_curve(editor, scene, curve_id, world=True)["points"]


def sample_world(editor, scene, curve_id, count=101):
    length = get_curve(editor, scene, curve_id)["length"]
    reply = evaluate(editor, scene, curve_id, [length * i / (count - 1) for i in range(count)])
    return length, [sample["position"] for sample in reply["samples"]]


def same_shape(before, after, tolerance=1.0e-5):
    """The largest distance between positions at equal distances, and whether the lengths agree."""
    (length_a, a), (length_b, b) = before, after
    worst = max(math.dist(p, q) for p, q in zip(a, b))
    return (abs(length_a - length_b) <= tolerance) and (worst <= tolerance), worst, length_a, length_b


def click_world(editor, viewport, world, modifiers=None):
    x, y = point_at(viewport, world)
    arguments = {"x": x, "y": y}
    if modifiers:
        arguments["modifiers"] = modifiers
    editor.call("mouse_click", arguments)
    editor.wait_idle()
    settle(editor)


def check_curve_viewport(editor, image_path):
    """Curve point editing in the viewport (doc/editor/curves.md "Viewport editing")."""
    scene = create_scene(editor)
    editor.call("set_active_scene", {"scene_name": scene})
    given = [point((0.0, 0.0, 0.0)),
             point((2.0, 0.0, 0.0), "smooth", (-0.5, 0.0, 0.0), (0.5, 0.0, 0.0)),
             point((3.0, 0.0, -1.0))]
    made = editor.call("create_curve", {"scene_name": scene, "name": "edit curve", "points": given})
    editor.wait_idle()
    curve_id = made["id"]
    # The curve's points inside the viewport, at a scale where a 0.5 m handle end lies clear of the gizmo's arrow.
    viewport = frame_camera(editor, scene, [1.5, 3.5, 4.5], [1.5, 0.0, -0.3], [[0.0, 0.0, 0.0], [2.0, 0.0, 0.0], [3.0, 0.0, -1.0]],
                            limit=0.92, min_px_per_metre=280.0)
    choose_move_tool(editor, viewport)
    editor.call("select_items", {"scene_name": scene, "ids": [curve_id]})
    editor.wait_idle()
    settle(editor)
    first = selection_of(editor)
    check("curve tool: selecting the curve makes it the tool's target with no points selected, the gizmo on the node",
          (first["curve"] is not None) and (first["curve"]["id"] == curve_id) and (first["points"] == []) and not editor.call("get_transform_state")["component_mode"],
          json.dumps(first))

    # Pick a point by a click at its projected position.
    depth = undo_depth(editor)
    click_world(editor, viewport, [2.0, 0.0, 0.0])
    picked = selection_of(editor)
    check("curve tool: a click at point 1's projection selects it, no undo entry, the curve stays selected",
          ([(p["index"], p["part"]) for p in picked["points"]] == [(1, "point")]) and (undo_depth(editor) == depth)
          and (picked["curve"] is not None), json.dumps(picked))
    state = editor.call("get_transform_state")
    check("curve tool: the gizmo is anchored at the selected point", state["component_mode"] and close(state["anchor_frame"]["translation"], [2.0, 0.0, 0.0], 1e-5), json.dumps(state["anchor_frame"]))

    # A gizmo X drag moves it by the known amount; the handles ride along; one undo entry; sane message count.
    before = local_points(get_curve(editor, scene, curve_id))
    settle(editor)
    count0 = message_count(editor, scene)
    px_per_metre = gizmo_drag(editor, viewport, ltv.TRANSLATE_X, [0.5, 0.0, 0.0])
    settle(editor)
    count1 = message_count(editor, scene)
    after = local_points(get_curve(editor, scene, curve_id))
    moved = after[1]["position"][0] - 2.0
    report_shortfall("curve point gizmo X drag", moved, 0.5, px_per_metre)
    check(f"curve tool: a gizmo X drag of 0.5 moves point 1 along X only (within {GIZMO_SHORTFALL_PX} px), its handles ride along, one undo entry",
          close(after[1]["position"], [2.5, 0.0, 0.0], drag_tolerance(px_per_metre)) and (after[1]["position"][1] == 0.0) and (after[1]["position"][2] == 0.0)
          and (after[1]["in"] == before[1]["in"]) and (after[1]["out"] == before[1]["out"]) and (after[0] == before[0]) and (after[2] == before[2])
          and (undo_depth(editor) == depth + 1), f"{json.dumps(after[1])}, undo {depth} -> {undo_depth(editor)}")
    check("curve tool: the drag sends at most one Curve_changed_message per moved frame (10 move frames and the record), none while held still",
          1 <= (count1 - count0) <= 11, f"{count0} -> {count1}")
    settle(editor, 5)
    check("curve tool: no Curve_changed_message while the selected point rests", message_count(editor, scene) == count1, f"{count1} -> {message_count(editor, scene)}")
    check("curve tool: the gizmo follows the moved point", close(anchor(editor), after[1]["position"], 1e-5), f"{anchor(editor)}")
    kept = selection_of(editor)
    check("curve tool: after the drag point 1 stays selected and the gizmo stays on it",
          ([(p["index"], p["part"]) for p in kept["points"]] == [(1, "point")]) and kept.get("gizmo_on_points", False), json.dumps(kept))
    gizmo_drag(editor, viewport, ltv.TRANSLATE_X, [0.5, 0.0, 0.0])
    settle(editor)
    again = local_points(get_curve(editor, scene, curve_id))[1]["position"]
    check("curve tool: a second X drag without a click moves point 1 again, a second undo entry",
          (again[0] > after[1]["position"][0] + 0.3) and (undo_depth(editor) == depth + 2), f"{after[1]['position']} -> {again}, undo {depth} -> {undo_depth(editor)}")
    editor.call("undo")
    editor.call("undo")
    editor.wait_idle()
    check("curve tool: undo of the two drags restores the curve exactly", local_points(get_curve(editor, scene, curve_id)) == before)
    settle(editor)

    # A plane constraint (the XZ handle) keeps Y.
    gizmo_drag(editor, viewport, ltv.TRANSLATE_XZ, [0.3, 0.0, 0.4])
    plane = local_points(get_curve(editor, scene, curve_id))[1]["position"]
    check("curve tool: a gizmo XZ plane drag moves point 1 in the plane (Y stays 0 within 1e-5: the pointer ray meets the plane)", (abs(plane[1]) <= 1e-5) and close(plane, [2.3, 0.0, 0.4], 0.02), json.dumps(plane))
    editor.call("undo")
    editor.wait_idle()
    settle(editor)

    # Grid snap: Ctrl held during the drag snaps with the Move tool's snap value (0.1).
    gizmo_drag_with(editor, viewport, ltv.TRANSLATE_X, [0.37, 0.0, 0.0], ["ctrl"])
    snapped = local_points(get_curve(editor, scene, curve_id))[1]["position"]
    check("curve tool: a Ctrl gizmo X drag of 0.37 snaps point 1 to x = 2.4 (snap 0.1, start on the grid)", close(snapped, [2.4, 0.0, 0.0], 1e-5), json.dumps(snapped))
    editor.call("undo")
    editor.wait_idle()
    settle(editor)

    # Cancel: Escape during a held drag restores the points, no undo entry.
    depth = undo_depth(editor)
    gizmo_drag(editor, viewport, ltv.TRANSLATE_X, [0.5, 0.0, 0.0], hold=True)
    held = local_points(get_curve(editor, scene, curve_id))[1]["position"]
    editor.call("key_press", {"key": "escape"})
    editor.wait_idle()
    editor.call("mouse_release", {})
    editor.wait_idle()
    settle(editor)
    check("curve tool: Escape during a held gizmo drag restores the curve and records nothing",
          (held[0] > 2.1) and (local_points(get_curve(editor, scene, curve_id)) == before) and (undo_depth(editor) == depth),
          f"held {held}, undo {depth} -> {undo_depth(editor)}")

    # Handle drags: a smooth point keeps its handles aligned; Alt frees the dragged side.
    click_world(editor, viewport, [2.5, 0.0, 0.0])
    handle_pick = selection_of(editor)
    check("curve tool: a click at the out handle end selects the handle", [(p["index"], p["part"]) for p in handle_pick["points"]] == [(1, "out")], json.dumps(handle_pick))
    depth = undo_depth(editor)
    gizmo_drag(editor, viewport, TRANSLATE_Z, [0.0, 0.0, -0.5])
    aligned = local_points(get_curve(editor, scene, curve_id))[1]
    out = aligned["out"]
    expected_in = [-0.5 * c / math.hypot(*out) for c in out]
    check("curve tool: dragging a smooth point's out handle along Z turns the in handle opposite, keeping its length; the point stays smooth; one undo entry",
          (aligned["kind"] == "smooth") and close(out, [0.5, 0.0, -0.5], 0.02) and close(aligned["in"], expected_in, 1e-5) and (aligned["position"] == before[1]["position"])
          and (undo_depth(editor) == depth + 1), json.dumps(aligned))
    editor.call("undo")
    editor.wait_idle()
    settle(editor)
    gizmo_drag_with(editor, viewport, TRANSLATE_Z, [0.0, 0.0, -0.5], ["menu"])
    freed = local_points(get_curve(editor, scene, curve_id))[1]
    check("curve tool: with Alt the drag moves the out handle alone and the point becomes free; one undo entry",
          (freed["kind"] == "free") and close(freed["out"], [0.5, 0.0, -0.5], 0.02) and (freed["in"] == before[1]["in"]) and (undo_depth(editor) == depth + 1),
          json.dumps(freed))
    editor.call("undo")
    editor.wait_idle()
    check("curve tool: undo of the Alt drag restores the smooth point exactly", local_points(get_curve(editor, scene, curve_id)) == before)
    settle(editor)

    # Multi-select: a click and a Ctrl+click select two points; the gizmo moves both.
    click_world(editor, viewport, [0.0, 0.0, 0.0])
    click_world(editor, viewport, [3.0, 0.0, -1.0], ["ctrl"])
    two = selection_of(editor)
    check("curve tool: a click and a Ctrl+click select points 0 and 2", [(p["index"], p["part"]) for p in two["points"]] == [(0, "point"), (2, "point")], json.dumps(two))
    check("curve tool: the gizmo sits at the centroid of the selected points", close(anchor(editor), [1.5, 0.0, -0.5], 1e-5), f"{anchor(editor)}")
    depth = undo_depth(editor)
    px_per_metre = gizmo_drag(editor, viewport, ltv.TRANSLATE_Y, [0.0, 0.5, 0.0])
    both = local_points(get_curve(editor, scene, curve_id))
    rise = both[0]["position"][1]
    report_shortfall("curve two-point gizmo Y drag", rise, 0.5, px_per_metre)
    check(f"curve tool: a gizmo Y drag moves both selected points by the same amount (0.5 within {GIZMO_SHORTFALL_PX} px), point 1 stays, one undo entry",
          close(rise, 0.5, drag_tolerance(px_per_metre)) and (both[2]["position"][1] == rise) and (both[1] == before[1]) and (undo_depth(editor) == depth + 1),
          json.dumps([both[0]["position"], both[2]["position"]]))
    editor.call("undo")
    editor.wait_idle()
    settle(editor)

    # transform_selection drives the same path: an absolute anchor translation.
    editor.call("select_curve_points", {"scene_name": scene, "curve_id": curve_id, "points": [{"index": 2}]})
    settle(editor)
    depth = undo_depth(editor)
    editor.call("transform_selection", {"translation": [3.0, 1.0, -1.0]})
    editor.wait_idle()
    check("curve tool: transform_selection with a selected point moves it to the translation, one undo entry",
          close(local_points(get_curve(editor, scene, curve_id))[2]["position"], [3.0, 1.0, -1.0], 1e-5) and (undo_depth(editor) == depth + 1))
    editor.call("undo")
    editor.wait_idle()

    # Insert on the curve: Ctrl+click on the curve between the points splits the span, the shape unchanged.
    editor.call("select_curve_points", {"scene_name": scene, "curve_id": curve_id, "points": []})
    settle(editor)
    shape = sample_world(editor, scene, curve_id)
    place = evaluate(editor, scene, curve_id, [2.6])["samples"][0]["position"]
    depth = undo_depth(editor)
    click_world(editor, viewport, place, ["ctrl"])
    inserted = get_curve(editor, scene, curve_id)
    same, worst, la, lb = same_shape(shape, sample_world(editor, scene, curve_id))
    check("curve tool: Ctrl+click on the curve inserts one point in one undo entry, selected, and the sampled shape is unchanged within 1e-5",
          (inserted["point_count"] == 4) and same and (undo_depth(editor) == depth + 1)
          and ([(p["index"], p["part"]) for p in selection_of(editor)["points"]] == [(2, "point")]),
          f"{inserted['point_count']} points, worst {worst:.3g}, length {la} -> {lb}, undo {depth} -> {undo_depth(editor)}")
    print(f"curve tool: Ctrl+click insert: shape worst deviation {worst:.3g}, new point {json.dumps(inserted['points'][2])}", flush=True)
    editor.call("undo")
    editor.wait_idle()
    check("curve tool: undo of the insert restores the curve exactly", local_points(get_curve(editor, scene, curve_id)) == before)

    # Remove with the Delete key; kinds and closure through the commands.
    editor.call("select_curve_points", {"scene_name": scene, "curve_id": curve_id, "points": [{"index": 1}]})
    hover_x, hover_y = point_at(viewport, [1.0, 0.0, 1.0])
    editor.call("mouse_wheel", {"x": hover_x, "y": hover_y, "dy": 0.0})
    depth = undo_depth(editor)
    editor.call("key_press", {"key": "delete"})
    editor.wait_idle()
    removed = get_curve(editor, scene, curve_id)
    nodes = {n["id"] for n in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]}
    check("curve tool: Delete removes the selected point (not the curve node), one undo entry, the point selection clears",
          (removed["point_count"] == 2) and (curve_id in nodes) and (undo_depth(editor) == depth + 1) and (selection_of(editor)["points"] == []),
          f"{removed['point_count']} points, undo {depth} -> {undo_depth(editor)}")
    editor.call("select_curve_points", {"scene_name": scene, "curve_id": curve_id, "points": [{"index": 0}]})
    editor.call("mouse_wheel", {"x": hover_x, "y": hover_y, "dy": 0.0})
    depth = undo_depth(editor)
    editor.call("key_press", {"key": "delete"})
    editor.wait_idle()
    check("curve tool: Delete with two points left is refused (a curve keeps two points), nothing recorded",
          (get_curve(editor, scene, curve_id)["point_count"] == 2) and (undo_depth(editor) == depth) and ("a curve keeps at least 2 points" in pav.read_log()),
          f"undo {depth} -> {undo_depth(editor)}")
    editor.call("undo")
    editor.wait_idle()
    check("curve tool: undo of the removal restores the curve exactly", local_points(get_curve(editor, scene, curve_id)) == before)

    editor.call("select_curve_points", {"scene_name": scene, "curve_id": curve_id, "points": [{"index": 0}, {"index": 1, "part": "in"}]})
    depth = undo_depth(editor)
    editor.call("Curve.set_kind_free")
    editor.wait_idle()
    kinds = [p["kind"] for p in local_points(get_curve(editor, scene, curve_id))]
    check("curve tool: Curve.set_kind_free makes the points with a selected part free (corner 0 gains Catmull-Rom handles), one undo entry",
          (kinds == ["free", "free", "corner"]) and (undo_depth(editor) == depth + 1), json.dumps(kinds))
    editor.call("undo")
    editor.wait_idle()
    editor.call("Curve.toggle_closed")
    editor.wait_idle()
    check("curve tool: Curve.toggle_closed closes the curve, one undo entry", get_curve(editor, scene, curve_id)["closed"] and (undo_depth(editor) == depth + 1))
    editor.call("undo")
    editor.wait_idle()
    check("curve tool: undo of the kind change and the closing restores the curve exactly",
          (local_points(get_curve(editor, scene, curve_id)) == before) and not get_curve(editor, scene, curve_id)["closed"])

    # The Properties window's curve tool rows.
    editor.call("select_curve_points", {"scene_name": scene, "curve_id": curve_id, "points": [{"index": 2}]})
    editor.call("set_window_visibility", {"title": "Properties", "visible": True, "focus": True})
    editor.wait_idle()
    settle(editor)
    header = next((i for i in find_items(editor, "Properties", "Curve") if i.get("display_label") == "Curve"), None)
    if (header is not None) and not header.get("status", {}).get("opened"):
        editor.call("imgui_scroll_to_item", {"window": "Properties", "id": header["id"]})
        editor.call("imgui_click", {"window": "Properties", "id": header["id"]})
        editor.wait_idle()
    # The row's second button: a row's widgets carry the row label with a component suffix.
    button = next((i for i in find_items(editor, "Properties", "Add Point") if i.get("display_label") == "Add Point.y"), None)
    if button is None:
        check("the Properties window's Curve group shows the Add Point row with At End", False)
    else:
        depth = undo_depth(editor)
        editor.call("imgui_scroll_to_item", {"window": "Properties", "id": button["id"]})
        editor.call("imgui_click", {"window": "Properties", "id": button["id"]})
        editor.wait_idle()
        settle(editor)
        extended = get_curve(editor, scene, curve_id)
        check("a click on the Properties At End button adds a point at the end in one undo entry and selects it",
              (extended["point_count"] == 4) and (undo_depth(editor) == depth + 1)
              and ([(p["index"], p["part"]) for p in selection_of(editor)["points"]] == [(3, "point")]),
              f"{extended['point_count']} points, undo {depth} -> {undo_depth(editor)}")
        editor.call("undo")
        editor.wait_idle()

    # MCP point edits.
    shape = sample_world(editor, scene, curve_id)
    depth = undo_depth(editor)
    added = editor.call("curve_add_point", {"scene_name": scene, "curve_id": curve_id, "at": "curve", "distance": 1.0})
    same, worst, la, lb = same_shape(shape, sample_world(editor, scene, curve_id))
    check("curve_add_point at curve distance 1 splits the cubic first span (point 1 gives it a handle) with a smooth point, shape unchanged within 1e-5, one undo entry",
          (added["index"] == 1) and (added["points"][1]["kind"] == "smooth") and same and (undo_depth(editor) == depth + 1),
          f"{json.dumps(added['points'][:3])}, worst {worst:.3g}")
    check("the corner start of the split span becomes free with a zero in handle (its other side stays straight)",
          (added["points"][0]["kind"] == "free") and (added["points"][0]["in"] == [0.0, 0.0, 0.0]), json.dumps(added["points"][0]))
    editor.call("undo")
    editor.wait_idle()
    polyline = editor.call("create_curve", {"scene_name": scene, "name": "polyline", "points": [point((0, 0, 2)), point((2, 0, 2)), point((2, 0, 3))]})
    editor.wait_idle()
    poly_shape = sample_world(editor, scene, polyline["id"])
    split = editor.call("curve_add_point", {"scene_name": scene, "curve_id": polyline["id"], "at": "curve", "distance": 0.5})
    same, worst, la, lb = same_shape(poly_shape, sample_world(editor, scene, polyline["id"]))
    check("curve_add_point on a line span gives a corner and two line spans, the shape unchanged",
          (split["index"] == 1) and (split["points"][1]["kind"] == "corner") and close(split["points"][1]["position"], [0.5, 0.0, 2.0], 1e-6)
          and all(p["kind"] == "corner" for p in split["points"]) and same, f"{json.dumps(split['points'])}, worst {worst:.3g}")
    editor.call("delete_nodes", {"scene_name": scene, "ids": [polyline["id"]]})
    editor.wait_idle()
    depth = undo_depth(editor)
    world_place = evaluate(editor, scene, curve_id, [2.75])["samples"][0]["position"]
    nearest = editor.call("curve_add_point", {"scene_name": scene, "curve_id": curve_id, "at": "curve", "position": world_place})
    same, worst, la, lb = same_shape(shape, sample_world(editor, scene, curve_id))
    check("curve_add_point at curve position splits the cubic span at the nearest place (the point is there), a smooth point, shape unchanged",
          (nearest["points"][2]["kind"] == "smooth") and close(nearest["points"][2]["position"], world_place, 1e-4) and same
          and (nearest["points"][3]["kind"] == "free"), f"{json.dumps(nearest['points'][1:4])}, worst {worst:.3g}")
    editor.call("undo")
    editor.wait_idle()
    length = get_curve(editor, scene, curve_id)["length"]
    end_tangent = evaluate(editor, scene, curve_id, [length])["samples"][0]["local_tangent"]
    end = editor.call("curve_add_point", {"scene_name": scene, "curve_id": curve_id, "at": "end"})
    chord = math.dist([2.0, 0.0, 0.0], [3.0, 0.0, -1.0])
    expected_end = [3.0 + chord * end_tangent[0], chord * end_tangent[1], -1.0 + chord * end_tangent[2]]  # along the end tangent by the end span's chord
    check("curve_add_point at end extends a corner end along its tangent by the end span's chord, as a corner",
          (end["point_count"] == 4) and close(end["points"][3]["position"], expected_end, 1e-5) and (end["points"][3]["kind"] == "corner"), json.dumps(end["points"][3]))
    editor.call("undo")
    editor.wait_idle()
    start = editor.call("curve_add_point", {"scene_name": scene, "curve_id": curve_id, "at": "start", "point": point((-1.0, 0.5, 0.0), "smooth", (-0.2, 0.0, 0.0), (0.2, 0.0, 0.0))})
    check("curve_add_point at start with explicit point values puts them first exactly",
          (start["index"] == 0) and (local_points(start)[0] == as_f32([point((-1.0, 0.5, 0.0), "smooth", (-0.2, 0.0, 0.0), (0.2, 0.0, 0.0))])[0]), json.dumps(start["points"][0]))
    editor.call("undo")
    editor.wait_idle()
    depth = undo_depth(editor)
    remove = editor.call("curve_remove_points", {"scene_name": scene, "curve_id": curve_id, "indices": [1]})
    check("curve_remove_points removes point 1 in one undo entry", (remove["point_count"] == 2) and (undo_depth(editor) == depth + 1))
    editor.call("undo")
    editor.wait_idle()
    check("undo of curve_remove_points restores the curve exactly", local_points(get_curve(editor, scene, curve_id)) == before)

    depth = undo_depth(editor)
    refusals = [
        ("curve_remove_points", "leaving one point", {"indices": [0, 1]}),
        ("curve_remove_points", "an index twice", {"indices": [1, 1]}),
        ("curve_remove_points", "an index past the end", {"indices": [3]}),
        ("curve_add_point", "a place at a control point", {"at": "curve", "distance": 0.0}),
        ("curve_add_point", "an unknown 'at'", {"at": "middle"}),
        ("curve_add_point", "'point' with at curve", {"at": "curve", "distance": 1.0, "point": point((0, 0, 0))}),
        ("curve_add_point", "both distance and position", {"at": "curve", "distance": 1.0, "position": [0, 0, 0]}),
        ("curve_add_point", "an unknown argument", {"at": "end", "where": 1}),
        ("select_curve_points", "a handle of a corner", {"points": [{"index": 0, "part": "in"}]}),
        ("select_curve_points", "an index past the end", {"points": [{"index": 3}]}),
        ("select_curve_points", "an unknown part", {"points": [{"index": 1, "part": "handle"}]}),
    ]
    for tool, label, extra in refusals:
        arguments = {"scene_name": scene, "curve_id": curve_id}
        arguments.update(extra)
        error = call_error(editor, tool, arguments)
        check(f"{tool} refuses {label}", error is not None, str(error))
    editor.call("set_curve", {"scene_name": scene, "curve_id": curve_id, "closed": True})
    editor.wait_idle()
    check("curve_add_point refuses at end of a closed curve", call_error(editor, "curve_add_point", {"scene_name": scene, "curve_id": curve_id, "at": "end"}) is not None)
    editor.call("undo")
    editor.wait_idle()
    check("the refusals changed nothing and recorded nothing", (undo_depth(editor) == depth) and (local_points(get_curve(editor, scene, curve_id)) == before),
          f"undo {depth} -> {undo_depth(editor)}")

    # The screenshot: point 1 with both handle ends selected, highlighted over the Curves visualization.
    editor.call("select_curve_points", {"scene_name": scene, "curve_id": curve_id, "points": [{"index": 1}, {"index": 1, "part": "in"}, {"index": 1, "part": "out"}]})
    hover(editor, viewport, [3.0, 0.0, -1.0])
    settle(editor, 5)
    absolute = image_path if os.path.isabs(image_path) else os.path.join(pav.REPO_ROOT, image_path)
    os.makedirs(os.path.dirname(absolute), exist_ok=True)
    reply = editor.call("capture_screenshot", {"path": absolute})
    written = reply.get("path", absolute)
    check("capture_screenshot of the curve with a selected point and its handles is written", os.path.isfile(written), json.dumps(reply)[:200])
    print(f"curve point selection image: {written}", flush=True)

    # The tool's selection clears when the curve is deselected or deleted.
    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.wait_idle()
    check("curve tool: deselecting the curve clears the tool's target and points", selection_of(editor) == {"curve": None, "points": []}, json.dumps(selection_of(editor)))
    editor.call("select_curve_points", {"scene_name": scene, "curve_id": curve_id, "points": [{"index": 1}]})
    editor.call("delete_nodes", {"scene_name": scene, "ids": [curve_id]})
    editor.wait_idle()
    check("curve tool: deleting the curve clears the tool's target", selection_of(editor)["curve"] is None, json.dumps(selection_of(editor)))
    editor.call("undo")
    editor.wait_idle()
    editor.call("select_items", {"scene_name": scene, "ids": [curve_id]})
    editor.wait_idle()
    check("curve tool: selecting the restored curve starts with no points selected", selection_of(editor)["points"] == [], json.dumps(selection_of(editor)))
    editor.call("close_scene", {"scene_name": scene})
    editor.wait_idle()


def hover(editor, viewport, world):
    x, y = point_at(viewport, world)
    editor.call("mouse_wheel", {"x": x, "y": y, "dy": 0.0})
    editor.wait_idle()


def gizmo_drag_with(editor, viewport, values, delta, modifiers):
    """A released gizmo drag with modifier keys held for the whole gesture."""
    handle = ltv.gizmo_handle(editor, viewport, values)
    if handle is None:
        raise RuntimeError(f"no gizmo handle {values} on screen")
    start = point_at(viewport, handle["world"])
    probe = editor.call("get_transform_handles", {"viewport": viewport["title"], "probe_points": [[start[0], start[1]]]})["probes"][0]
    if probe.get("handle_value") != handle["handle_value"]:
        start = (handle["x"], handle["y"])
    to = point_at(viewport, [handle["world"][i] + delta[i] for i in range(3)])
    editor.call("mouse_drag", {"from": [start[0], start[1]], "to": [to[0], to[1]], "frames": 10, "hold": True, "modifiers": modifiers})
    editor.wait_idle()
    editor.call("mouse_release", {})
    editor.wait_idle()
    settle(editor)


# A small level built over MCP: a ground slab and two path layouts, each its own curve node (R5.12.8): "Fence", 12
# pillars in gap mode along a corner curve, and "Stepping Stones", surface-snapped rocks in gap mode along a smooth curve.

LEVEL_IMAGE = os.path.join("logs", "curve_level_selected.png")
LEVEL_MID_DRAG_IMAGE = os.path.join("logs", "curve_level_mid_drag.png")
LEVEL_STYLE_IMAGE = os.path.join("logs", "curve_level_style.png")
LOUD_COLOR = [1.0, 0.0, 1.0, 1.0]
LOUD_WIDTH = -9.0


def absolute_path(path):
    return path if os.path.isabs(path) else os.path.join(pav.REPO_ROOT, path)


def load_level(editor):
    scene = create_scene(editor)
    editor.call("set_active_scene", {"scene_name": scene})
    editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [0.0, -0.2, 0.0], "name": "ground", "parameters": {"width": 40.0, "depth": 40.0, "thickness": 0.2}}]})
    editor.wait_idle()
    editor.call("level_create_layout", {"scene_name": scene, "name": "Fence", "position": [-10.0, 0.0, -5.0],
                                        "items": [{"shape": "architecture.pillar", "parameters": {"height": 0.4}}],
                                        "pattern": {"kind": "path", "count_mode": "gap", "gap": 0.5},
                                        "points": [{"position": [0.0, 0.0, 0.0]}, {"position": [6.0, 0.0, 0.0]}, {"position": [6.0, 0.0, 4.0]}, {"position": [7.0, 0.0, 4.0]}]})
    editor.wait_idle()
    editor.call("level_create_layout", {"scene_name": scene, "name": "Stepping Stones", "position": [-10.0, 0.0, 6.0], "surface_snap": True,
                                        "items": [{"shape": "nature.rock", "parameters": {"seed": 3}}],
                                        "pattern": {"kind": "path", "count_mode": "gap", "gap": 0.4},
                                        "points": [{"position": [0.0, 0.0, 0.0], "kind": "smooth", "out": [2.0, 0.0, 0.0]},
                                                   {"position": [5.0, 0.0, 2.0], "kind": "smooth", "in": [-1.5, 0.0, 0.0], "out": [1.5, 0.0, 0.0]},
                                                   {"position": [10.0, 0.0, 0.0], "kind": "smooth", "in": [-2.0, 0.0, 0.0]}]})
    editor.wait_idle()
    return scene


def level_layouts(editor, scene):
    return {layout["name"]: layout for layout in editor.call("level_get_layouts", {"scene_name": scene})["layouts"]}


def children_of(layout):
    return [(c["id"], c["position"], c["rotation_xyzw"]) for c in layout["children"]]


def curve_by_name(editor, scene, name):
    return next(c for c in editor.call("get_curves", {"scene_name": scene})["curves"] if c["name"] == name)


def capture(editor, path):
    absolute = absolute_path(path)
    os.makedirs(os.path.dirname(absolute), exist_ok=True)
    reply = editor.call("capture_screenshot", {"path": absolute})
    return reply.get("path", absolute)


def loud_pixel_count(path):
    """Pixels close to the loud magenta of the style check."""
    _, _, pixels = pav.decode_png(path)
    return sum(1 for i in range(0, len(pixels), 4) if (pixels[i] > 200) and (pixels[i + 1] < 60) and (pixels[i + 2] > 200))


def look_at_curve(editor, scene, curve_name, height=14.0, back=16.0, index=None):
    curve = curve_by_name(editor, scene, curve_name)
    points = get_curve(editor, scene, curve["id"], world=True)["points"]
    middle = points[(len(points) // 2) if (index is None) else index]["world_position"]
    # Every point of the curve, with room around them for the gizmo and the section's drags.
    frame_camera(editor, scene, [middle[0], middle[1] + height, middle[2] + back], middle, [p["world_position"] for p in points], limit=0.75)
    return curve["id"], points


def residue_state(editor, scene, layout_name):
    layout = level_layouts(editor, scene)[layout_name]
    nodes = sorted(n["id"] for n in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"])
    return {
        "children":   children_of(layout),
        "nodes":      nodes,
        "brushes":    ltv.cached_brush_count(editor, scene),
        "navigation": editor.call("get_navigation_status", {"scene_name": scene}).get("stale"),
        "selection":  [i["id"] for i in editor.call("get_selection")["items"]],
        "undo":       undo_depth(editor),
    }


def check_level_drag(editor, scene, viewport, curve_name, layout_name, point_index, delta, label, count_change):
    """A held gizmo drag of one curve point: the layout follows mid-drag, Escape restores it exactly, a release records one entry."""
    curve_id, points = look_at_curve(editor, scene, curve_name, index=point_index)
    viewport = viewport_of(editor, scene)
    # Selecting a piece of the locked layout selects the layout node, the curve the tool edits (R5.12.8).
    piece_id = level_layouts(editor, scene)[layout_name]["children"][0]["id"]
    editor.call("select_items", {"scene_name": scene, "ids": [piece_id]})
    editor.wait_idle()
    settle(editor)
    selected = [i["id"] for i in editor.call("get_selection")["items"]]
    target = selection_of(editor).get("curve")
    check(f"{label}: selecting a piece selects the one layout node, which is the curve tool's target",
          (selected == [curve_id]) and (target is not None) and (target.get("id") == curve_id), f"{selected}, target {target}")
    click_world(editor, viewport, points[point_index]["world_position"])
    before = residue_state(editor, scene, layout_name)
    before_layout = level_layouts(editor, scene)[layout_name]
    before_length = get_curve(editor, scene, curve_id)["length"]

    gizmo_drag(editor, viewport, ltv.TRANSLATE_X, delta, hold=True)
    settle(editor)
    mid_curve = get_curve(editor, scene, curve_id)
    mid_layout = level_layouts(editor, scene)[layout_name]
    mid_count = mid_layout["child_count"]
    check(f"{label}: mid-drag get_curve reports the moved point and the new length; level_get_layouts' curve length and run space equal it",
          (abs(mid_curve["points"][point_index]["position"][0] - points[point_index]["position"][0]) > 0.5)
          and (mid_curve["length"] != before_length) and (abs(mid_layout["curve"]["length"] - mid_curve["length"]) < 1.0e-4) and (abs(mid_layout["run"]["space"] - mid_curve["length"]) < 1.0e-4),
          f"length {before_length} -> {mid_curve['length']}, layout {mid_layout['curve']['length']}, run {mid_layout['run']}")
    check(f"{label}: mid-drag the layout's children follow the curve (count {before_layout['child_count']} -> {mid_count}, {count_change}) with no undo entry",
          (children_of(mid_layout) != before["children"]) and (undo_depth(editor) == before["undo"])
          and (((count_change == "grows") and (mid_count > before_layout["child_count"])) or ((count_change == "shrinks") and (mid_count < before_layout["child_count"])) or ((count_change == "same") and (mid_count == before_layout["child_count"]))),
          f"count {before_layout['child_count']} -> {mid_count}, undo {before['undo']} -> {undo_depth(editor)}")
    editor.call("key_press", {"key": "escape"})
    editor.wait_idle()
    editor.call("mouse_release", {})
    editor.wait_idle()
    settle(editor)
    after_cancel = residue_state(editor, scene, layout_name)
    check(f"{label}: Escape mid-drag restores the curve and the children exactly (ids, transforms), the node ids, cached brush count, navigation staleness, selection and undo depth",
          (after_cancel == before) and (get_curve(editor, scene, curve_id)["length"] == before_length),
          json.dumps({k: (before[k], after_cancel[k]) for k in before if before[k] != after_cancel[k]})[:600])

    # The release: one entry equal to the last mid-drag step; undo and redo exact.
    click_world(editor, viewport, points[point_index]["world_position"])
    gizmo_drag(editor, viewport, ltv.TRANSLATE_X, delta, hold=True)
    settle(editor)
    held = level_layouts(editor, scene)[layout_name]
    editor.call("mouse_release", {})
    editor.wait_idle()
    settle(editor)
    released = level_layouts(editor, scene)[layout_name]
    check(f"{label}: the release records one undo entry and its pieces stand where the last mid-drag step showed them",
          (undo_depth(editor) == before["undo"] + 1) and ([c[1:] for c in children_of(released)] == [c[1:] for c in children_of(held)]),
          f"undo {before['undo']} -> {undo_depth(editor)}, {released['child_count']} vs {held['child_count']} children")
    kept = selection_of(editor)
    check(f"{label}: after the release the point stays selected and the gizmo on it",
          ([(p["index"], p["part"]) for p in kept["points"]] == [(point_index, "point")]) and kept.get("gizmo_on_points", False), json.dumps(kept)[:300])
    editor.call("undo")
    editor.wait_idle()
    settle(editor)
    undone = residue_state(editor, scene, layout_name)
    check(f"{label}: undo restores the curve and the children exactly (the same nodes)",
          (undone["children"] == before["children"]) and (get_curve(editor, scene, curve_id)["length"] == before_length), "")
    editor.call("redo")
    editor.wait_idle()
    settle(editor)
    check(f"{label}: redo gives the released children again (poses)",
          [c[1:] for c in children_of(level_layouts(editor, scene)[layout_name])] == [c[1:] for c in children_of(released)], "")
    editor.call("undo")
    editor.wait_idle()
    settle(editor)
    return curve_id, points


def check_level(editor):
    """Curves on a small level of path layouts: default drawing, point selection after drags, live layouts, the style settings UI."""
    # The earlier sections' scenes and window states are no part of these checks.
    editor.call("reset_editor_state")
    editor.wait_idle()
    scene = load_level(editor)
    curve_id, points = look_at_curve(editor, scene, "Fence")
    viewport = viewport_of(editor, scene)
    choose_move_tool(editor, viewport)
    editor.call("select_items", {"scene_name": scene, "ids": []})
    settle(editor, 5)
    unselected = capture(editor, os.path.join("logs", "curve_level_unselected.png"))
    editor.call("select_items", {"scene_name": scene, "ids": [curve_id]})
    settle(editor, 5)
    selected = capture(editor, LEVEL_IMAGE)
    _, differing, total = pav.compare_images(unselected, selected)
    print(f"selected curve on the test level (default style): {selected}, {differing} of {total} pixels differ from the unselected view", flush=True)
    check("the selected Fence in the default style differs from the unselected view in many pixels (a 4 px line)", differing > 3000, f"{differing}")

    # Two drags of the same point without a new click, with the locked Fence following.
    click_world(editor, viewport, points[1]["world_position"])
    depth = undo_depth(editor)
    gizmo_drag(editor, viewport, ltv.TRANSLATE_X, [0.5, 0.0, 0.0])
    settle(editor)
    first = get_curve(editor, scene, curve_id)["points"][1]["position"]
    kept = selection_of(editor)
    check("level: after a drag of Fence point 1 (its locked pieces following) the point stays selected and the gizmo on it",
          ([(p["index"], p["part"]) for p in kept["points"]] == [(1, "point")]) and kept.get("gizmo_on_points", False), json.dumps(kept)[:300])
    gizmo_drag(editor, viewport, ltv.TRANSLATE_X, [0.5, 0.0, 0.0])
    settle(editor)
    second = get_curve(editor, scene, curve_id)["points"][1]["position"]
    check("level: a second drag without a click moves the point again; two undo entries",
          (second[0] > first[0] + 0.3) and (undo_depth(editor) == depth + 2), f"{first} -> {second}, undo {depth} -> {undo_depth(editor)}")
    editor.call("undo")
    editor.call("undo")
    editor.wait_idle()
    settle(editor)

    # The mid-drag screenshot: the curve and the fence posts moved with the held point.
    click_world(editor, viewport, points[1]["world_position"])
    gizmo_drag(editor, viewport, ltv.TRANSLATE_X, [3.0, 0.0, 0.0], hold=True)
    settle(editor, 5)
    mid_image = capture(editor, LEVEL_MID_DRAG_IMAGE)
    editor.call("mouse_release", {})
    editor.wait_idle()
    settle(editor, 5)
    released_image = capture(editor, os.path.join("logs", "curve_level_released.png"))
    _, mid_vs_before, _ = pav.compare_images(selected, mid_image)
    _, mid_vs_release, _ = pav.compare_images(mid_image, released_image)
    print(f"mid-drag image: {mid_image}; {mid_vs_before} pixels differ from before the drag, {mid_vs_release} from after the release", flush=True)
    check("level: the mid-drag screenshot shows the moved curve and pieces (differs from before the drag; nearly equals the released view)",
          (mid_vs_before > 2000) and (mid_vs_release < mid_vs_before // 4), f"{mid_vs_before} vs {mid_vs_release}")
    editor.call("undo")
    editor.wait_idle()
    settle(editor)

    # Live layouts: count grows (Fence, gap 0) and shrinks (Stepping Stones, gap mode, surface snap) mid-drag.
    check_level_drag(editor, scene, viewport, "Fence", "Fence", 1, [2.0, 0.0, 0.0], "level Fence", "grows")
    stone_id, stone_points = look_at_curve(editor, scene, "Stepping Stones")
    toward_start = -2.5 if stone_points[-1]["world_position"][0] > stone_points[0]["world_position"][0] else 2.5
    check_level_drag(editor, scene, viewport, "Stepping Stones", "Stepping Stones", len(stone_points) - 1, [toward_start, 0.0, 0.0], "level Stepping Stones", "shrinks")

    # Surface snap mid-drag: a hill under the middle of the path; the held step's stones rest on it as the release's do.
    middle = stone_points[len(stone_points) // 2]["world_position"]
    hill = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [{"shape": "nature.hill", "position": [middle[0], 0.0, middle[2]], "name": "verify hill",
                                                                       "parameters": {"radius": 3.0, "height": 0.3, "derive": []}}]})
    editor.wait_idle()
    settle(editor)
    look_at_curve(editor, scene, "Stepping Stones", index=0)
    viewport = viewport_of(editor, scene)
    editor.call("select_items", {"scene_name": scene, "ids": [stone_id]})
    settle(editor)
    click_world(editor, viewport, stone_points[0]["world_position"])
    gizmo_drag(editor, viewport, ltv.TRANSLATE_X, [0.4, 0.0, 0.0], hold=True)
    settle(editor)
    held = level_layouts(editor, scene)["Stepping Stones"]
    lifted = [c["position"][1] for c in held["children"] if c["position"][1] > 0.1]
    editor.call("mouse_release", {})
    editor.wait_idle()
    settle(editor)
    released = level_layouts(editor, scene)["Stepping Stones"]
    check("level: mid-drag the surface-snapped stones over the hill rest on it (some rise), at the poses the release records",
          bool(lifted) and ([c["position"] for c in released["children"]] == [c["position"] for c in held["children"]]),
          f"{len(lifted)} lifted stones, heights {[round(h, 3) for h in lifted][:6]}")
    editor.call("undo")
    editor.wait_idle()
    editor.call("delete_nodes", {"scene_name": scene, "ids": [hill["pieces"][0]["id"]]})
    editor.wait_idle()
    settle(editor)

    # An unlocked layout does not follow.
    fence = level_layouts(editor, scene)["Fence"]
    editor.call("level_set_layout_lock", {"scene_name": scene, "layout_id": fence["id"], "locked": False})
    editor.wait_idle()
    curve_id, points = look_at_curve(editor, scene, "Fence")
    viewport = viewport_of(editor, scene)
    editor.call("select_items", {"scene_name": scene, "ids": [curve_id]})
    settle(editor)
    click_world(editor, viewport, points[1]["world_position"])
    gizmo_drag(editor, viewport, ltv.TRANSLATE_X, [2.0, 0.0, 0.0], hold=True)
    settle(editor)
    held_unlocked = children_of(level_layouts(editor, scene)["Fence"])
    editor.call("mouse_release", {})
    editor.wait_idle()
    settle(editor)
    check("level: an unlocked Fence keeps its children mid-drag and after the release",
          (held_unlocked == children_of(fence)) and (children_of(level_layouts(editor, scene)["Fence"]) == children_of(fence)), "")
    editor.call("undo")
    editor.call("undo")
    editor.wait_idle()
    settle(editor)

    # The style settings UI: typing the selected curve width changes the drawn pixels at once; the file keeps it at exit.
    editor.call("select_items", {"scene_name": scene, "ids": [curve_id]})
    settle(editor, 5)
    before_style = capture(editor, os.path.join("logs", "curve_level_before_style.png"))
    editor.call("set_window_visibility", {"title": "Settings", "visible": True, "focus": True})
    editor.wait_idle()
    for header in ("Editor Settings", "Debug Visualizations Style", "Curves"):
        item = next((i for i in find_items(editor, "Settings", header) if i.get("display_label") == header), None)
        if (item is not None) and not item.get("status", {}).get("opened"):
            editor.call("imgui_scroll_to_item", {"window": "Settings", "id": item["id"]})
            editor.call("imgui_click", {"window": "Settings", "id": item["id"]})
            editor.wait_idle()
    # The row's second widget is its width field; the Curves group comes before Point Handles' Selected row.
    widths = sorted((i for i in find_items(editor, "Settings", "Selected") if i.get("display_label") == "Selected.y"), key=lambda i: i.get("center_y", 0))
    width_item = widths[0] if widths else None
    if width_item is None:
        check("the Settings window shows the Curves > Selected width field", False, json.dumps([(i.get("display_label"), i.get("type")) for i in widths])[:400])
        return
    editor.call("imgui_scroll_to_item", {"window": "Settings", "id": width_item["id"]})
    editor.call("imgui_click", {"window": "Settings", "id": width_item["id"], "modifiers": ["ctrl"]})
    editor.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
    editor.call("type_text", {"text": f"{LOUD_WIDTH:.1f}"})
    editor.call("key_press", {"key": "enter"})
    editor.wait_idle()
    editor.call("set_window_visibility", {"title": "Settings", "visible": False})
    settle(editor, 5)
    after_style = capture(editor, os.path.join("logs", "curve_level_after_style.png"))
    _, differing, _ = pav.compare_images(before_style, after_style)
    check(f"a width of {LOUD_WIDTH} px typed in Settings > Debug Visualizations Style > Curves > Selected changes the drawn curve at once", differing > 1000, f"{differing} pixels differ")
    editor.call("close_scene", {"scene_name": scene})
    editor.wait_idle()


def check_style_file(editor_path):
    """A loud curve color and width written into editor_settings.json (style version 10) draw at the next start.

    An AI-driven run writes no config file (doc/agents/editor_runs.md), so the save at exit is the codegen
    serializer shared by every settings field, not exercised here; the restore below puts the user's file back."""
    settings_path = os.path.join(pav.REPO_ROOT, "config", "editor", "editor_settings.json")
    with open(settings_path) as handle:
        settings = json.load(handle)
    style = settings.get("debug_visualizations_style", {})
    style["_version"]             = 10
    style["curve_selected_color"] = LOUD_COLOR
    style["curve_selected_width"] = LOUD_WIDTH
    settings["debug_visualizations_style"] = style
    with open(settings_path, "w") as handle:
        json.dump(settings, handle, indent=4)
    editor = Editor(editor_path)
    scene = load_level(editor)
    curve_id, _ = look_at_curve(editor, scene, "Fence")
    viewport_of(editor, scene)
    editor.call("select_items", {"scene_name": scene, "ids": [curve_id]})
    settle(editor, 5)
    loud = capture(editor, LEVEL_STYLE_IMAGE)
    editor.call("select_items", {"scene_name": scene, "ids": []})
    settle(editor, 5)
    plain = capture(editor, os.path.join("logs", "curve_level_style_unselected.png"))
    count, count_plain = loud_pixel_count(loud), loud_pixel_count(plain)
    print(f"loud style image: {loud}: {count} magenta pixels selected, {count_plain} unselected", flush=True)
    check(f"a magenta curve_selected_color and width {LOUD_WIDTH} in editor_settings.json draw the selected curve magenta at the next start", (count > 3000) and (count_plain < 100), f"{count} vs {count_plain}")
    editor.stop()


def run(editor, work, image_path, sections):
    if "lattice" in sections:
        check_lattice_viewport(editor)
    if "viewport" in sections:
        check_curve_viewport(editor, POINTS_IMAGE)
    if "base" in sections:
        run_base(editor, work, image_path)
    if "level" in sections:
        check_level(editor)


def run_base(editor, work, image_path):
    scene = create_scene(editor)
    l_id, root_id = check_create_and_read(editor, scene)
    check_ui_create(editor, scene, root_id)
    check_set_curve(editor, scene, l_id)
    editor.call("set_curve", {"scene_name": scene, "curve_id": l_id, "closed": False,
                              "points": [point((0, 0, 0)), point((3, 0, 0)), point((3, 0, 4))]})
    editor.wait_idle()
    straight_id, l_id2, arc_id, square_id = check_evaluation(editor, scene)
    check_world_space(editor, scene, l_id2)
    check_duplicate_copy_delete(editor, scene, arc_id)
    check_notification(editor, scene, square_id)
    scene = check_persistence(editor, scene, work)
    check_properties(editor, scene)
    capture_selected(editor, scene, image_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=default_editor(), help="editor binary (relative to the repository root or absolute)")
    parser.add_argument("--image", default=DEFAULT_IMAGE, help="where the selected-curve screenshot is written")
    parser.add_argument("--keep", action="store_true", help="keep the work directory")
    parser.add_argument("--sections", default="base,lattice,viewport,level", help="comma-separated sections to run (base, lattice, viewport, level)")
    args = parser.parse_args()

    restored = {}
    for name in RESTORED_CONFIG_FILES:
        path = os.path.join(pav.REPO_ROOT, name)
        if os.path.isfile(path):
            with open(path, "rb") as handle:
                restored[path] = handle.read()

    start = time.monotonic()
    work = os.path.realpath(tempfile.mkdtemp(prefix="curve_verify_"))
    print(f"work directory {work}", flush=True)
    try:
        editor = Editor(args.editor)
        sections = set(args.sections.split(","))
        run(editor, work, args.image, sections)
        if "level" in sections:
            # The settings file is written at exit; one editor per log, so this one stops first.
            editor.stop()
            check_style_file(args.editor)
    except Exception as error:  # noqa: BLE001 - a failed step is a failed check
        check("run completed without an error", False, f"{type(error).__name__}: {error}")
    finally:
        for launched in pav.LAUNCHED:
            launched.stop()
        for path, content in restored.items():
            with open(path, "wb") as handle:
                handle.write(content)
        if args.keep:
            print(f"kept {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)
    failed = [label for label, ok in pav.RESULTS if not ok]
    print(f"\n{len(pav.RESULTS) - len(failed)}/{len(pav.RESULTS)} checks passed, {len(failed)} failed in {time.monotonic() - start:.0f} s")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
