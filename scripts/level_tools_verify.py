#!/usr/bin/env python3
"""Verify level kit pieces over MCP (doc/editor/level_tools.md "Verification").

Launches a headless editor (ERHE_AI_DRIVER=1, stopped by its own pid), creates
an empty scene and checks over MCP:

- level_get_kits lists the 3 kits and the 14 shapes of the starter set;
- the 14 shapes placed once each at their defaults in one call (the "catalog
  row") are 14 pieces whose world_aabb matches the shape's default size, whose
  parameters echo the catalog defaults, and whose material is the palette
  material of the shape's slot; a piece has a static physics body, the stairs
  a triangle mesh shape, the block a convex hull; a render_scene_image of the
  row is written and its path printed;
- a batch of pieces: two of equal values share one mesh (one brush, the brush's
  primitive), two of different values do not, and equal values in a later call
  reuse the catalog row's mesh; the reply echoes the given parameters over the
  defaults;
- undo removes the batch in one step and redo restores it;
- a value outside its range fails the call naming the piece index, the
  parameter and its range, and places nothing; so do an unknown key in a
  piece, an unknown top-level key, an unknown parameter and an unknown shape;
- snap "module" rounds positions to the module;
- after set_scene_settings level.module / wall_height, the catalog defaults and
  placed pieces follow the new lengths, and snap rounds to the new module;
- level_set_piece_parameters changes a stair's rise as one undo entry: the new
  bounds, the node keeps id, name, transform and material, the body stays
  static with the new triangle mesh shape, a second piece sharing the old mesh
  is unaffected; undo and redo; an out-of-range value and a node that is not a
  piece fail and change nothing; cached_brush_count falls back after
  clear_undo_history for a brush no piece uses;
- surface placement: a block placed above a floor slab with surface_snap rests
  on the slab's top; a yawed bush placed above a wedge with surface_orient takes
  the wedge's slope normal as its up axis and keeps its heading (the only change
  is the tilt about the horizontal axis); orient + snap on a yawed block does
  the same; level_snap_to_surface on a raised block answers query_only without
  moving it, then moves it in one undo entry that one undo takes back; a block
  hanging over a slab's edge rests on the slab (corner rays); a placement over
  empty space reports surface_found false and stays where it was given; a
  block placed sunk at y 0 over a slab rises onto it (a placed one through
  level_snap_to_surface, with a negative distance), a bush placed inside a
  hill where the slope is lower than the bush ends on its surface, and a block
  under a roof higher than the block rests on the floor. A
  render_scene_image of the surface placements is written and its path printed;
- the walkable level, in a scene of its own: a floor slab, stairs at their defaults
  and an upper slab whose top and front edge meet the stairs' top (from the
  catalog's default_bounds), a seed node on the floor and export settings;
  navigation_build: the upper slab is reachable, a complete path climbs from the
  floor to it, both are one island; step_rise_limit 0.5 m (above
  agent_max_climb) makes the mesh stale ("geometry changed") and after a
  rebuild no path joins them. A render_scene_image with the navigation mesh
  ("all") is written and its path printed;
- persistence (doc/editor/level_tools.md "Persistence"): the catalog scene and the
  walkable level are saved with save_scene to a temporary directory, closed and
  loaded again with load_scene: every piece keeps its record, name and bounds,
  every piece follows its kit (follows_kit), equal values share one mesh and
  different values do not, the static bodies are back; the saved file holds
  each piece as a node with an ordinary glTF mesh (R8.2); a parameter change of
  a reloaded piece works and undoes; a placement after the reload reuses the
  palette material (the material count stays); a hand-edited copy with one
  piece's shape id unknown and another's parameter out of range loads with
  those two keeping their meshes, follows_kit false and a problem text;
- the Level Kit window and the Brush tool's kit mode, driven through the UI
  tools: the window opens from the Window menu and has no Place button;
  choosing the Block row makes the Brush tool the active tool in kit mode; with
  the pointer over a slab the preview is the module-rounded hover point resting
  on the slab, and a click with the insert binding places a piece at exactly
  the preview's position and rotation, one undo entry, not selected; a second
  click elsewhere shares the mesh; hovering a placed block stacks the next one
  on it; the rotate binding turns the preview and the next piece by 90 degrees
  about Y; with Orient To Surface the preview and the piece over a wedge take
  the slope normal; editing the width row changes the preview's bounds and the
  next piece; undo removes one piece per step; a palette brush chosen with
  set_tool_asset leaves kit mode and places face-aligned on the slab's top
  face; a hotbar key choosing another tool leaves kit mode; a selected stairs
  piece shows its parameter rows. A capture_screenshot with the preview over
  the slab is written and its path printed;
- the Properties window with the stairs piece selected: its Kit Piece group
  has rows rise, width and step_rise_limit, the raw Parameters row is absent
  outside developer mode, get_item_properties still lists the raw text; typing
  1 into the rise row gives the stairs a 1 m rise as one undo entry, undo gives
  1.5 m back. A capture_screenshot of it is written and its path printed.
- live parameter drags (D3.6): a held mouse_drag of the stairs' rise row shows
  new bounds mid-drag with record and undo depth unchanged, the release records
  one entry and undo restores the bounds; a drag back to the start, a typed
  out-of-range rise, Escape in a typed rise, deselecting mid-drag and undo
  mid-drag record no entry of the drag; a held drag of the Level Kit window's
  width row changes the placement preview without an entry; the cached brush
  count returns to its start after clear_undo_history; the preview step times
  (editor.scene trace) of the heaviest shape are printed.

The run restores config/editor/editor_settings.json and desktop_windows.json,
which the window's checkboxes and its visibility rewrite.

Usage: python3 scripts/level_tools_verify.py [--editor <binary>] [--image <png path>] [--surface-image <png path>]
       [--walkable-image <png path>] [--window-image <png path>] [--properties-image <png path>] [--only-walkable]
(default editor: build_ninja_linux_vulkan_headless/bin/editor when built, else build/Ninja_Vulkan_Debug/bin/editor)
"""

import argparse
import json
import math
import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prepared_asset_verify as pav  # noqa: E402
from prepared_asset_verify import Editor, check  # noqa: E402
from mcp_viewport_isolation import isolate_viewport  # noqa: E402

HEADLESS_EDITOR = "build_ninja_linux_vulkan_headless/bin/editor"
DEBUG_EDITOR    = "build/Ninja_Vulkan_Debug/bin/editor"
DEFAULT_IMAGE   = os.path.join("logs", "level_tools_catalog_row.png")
DEFAULT_SURFACE_IMAGE = os.path.join("logs", "level_tools_surface_placement.png")
DEFAULT_WALKABLE_IMAGE = os.path.join("logs", "level_tools_walkable_level.png")
DEFAULT_WINDOW_IMAGE = os.path.join("logs", "level_tools_level_kit_window.png")
DEFAULT_PROPERTIES_IMAGE = os.path.join("logs", "level_tools_properties.png")
DEFAULT_ROCK_IMAGE = os.path.join("logs", "level_tools_rock_forms.png")
DEFAULT_TRANSFORM_IMAGE = os.path.join("logs", "level_tools_surface_settings.png")
WINDOW          = "Level Kit"
IMAGE_WIDTH     = 1600
IMAGE_HEIGHT    = 600
TOLERANCE       = 1.0e-3
RESTORED_CONFIG_FILES = [os.path.join("config", "editor", "editor_settings.json"), os.path.join("config", "editor", "desktop_windows.json")]

# Default sizes (x, y, z) at module 1 m and wall height 3 m, from the definitions in src/editor/level/.
EXACT_DEFAULT_SIZES = {
    "basic.block":            (1.0, 1.0, 1.0),
    "basic.wedge":            (1.0, 1.0, 2.0),
    "basic.cylinder":         (1.0, 1.0, 1.0),
    "basic.stairs":           (1.0, 3.0, 4.5),   # 15 steps of 0.2 m, treads 0.3 m
    "basic.floor_slab":       (2.0, 0.2, 2.0),
    "architecture.wall":      (4.0, 3.0, 0.25),
    "architecture.door_wall": (4.0, 3.0, 0.25),
    "architecture.pillar":    (0.5, 3.0, 0.5),
    "architecture.arch":      (2.6, 2.7, 0.5),   # span 2 + 2 x 0.3, rise 2.4 + 0.3
}
# Seeded shapes: nominal sizes, which the jitter moves by up to this share.
NOMINAL_DEFAULT_SIZES = {
    "nature.rock":      (1.0, 0.75, 1.0),
    "nature.land_mass": (4.0, 1.0, 4.0),
    "nature.hill":      (6.0, 1.0, 6.0),
    "nature.tree":      (2.0, 3.6, 2.0),
    "nature.bush":      (1.0, 0.6, 1.0),
}
NOMINAL_SHARE = 0.35


def default_editor():
    return HEADLESS_EDITOR if os.path.isfile(os.path.join(pav.REPO_ROOT, HEADLESS_EDITOR)) else DEBUG_EDITOR


def size_of(aabb):
    return [aabb["max"][i] - aabb["min"][i] for i in range(3)]


def close(a, b, tolerance=TOLERANCE):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def fmt3(v):
    return "[" + ", ".join(f"{x:.3f}" for x in v) + "]"


def call_error(editor, tool, arguments):
    """The error text of a call expected to fail, or None when it succeeds."""
    try:
        editor.call(tool, arguments)
    except RuntimeError as error:
        return str(error)
    return None


def pieces(editor, scene):
    return editor.call("level_get_pieces", {"scene_name": scene})["pieces"]


def undo_depth(editor):
    return len(editor.call("get_undo_redo_stack").get("undo", []))


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


def check_catalog(editor, scene):
    catalog = editor.call("level_get_kits", {"scene_name": scene})
    shapes = catalog["shapes"]
    check("level_get_kits lists 14 shapes", (catalog["shape_count"] == 14) and (len(shapes) == 14), f"{catalog['shape_count']} shapes")
    check("level_get_kits lists the basic, architecture and nature kits with 5, 4 and 5 shapes",
          [(k["name"], len(k["shapes"])) for k in catalog["kits"]] == [("basic", 5), ("architecture", 4), ("nature", 5)],
          json.dumps([(k["name"], len(k["shapes"])) for k in catalog["kits"]]))
    check("level_get_kits reports the default level lengths (module 1 m, wall height 3 m)",
          close([catalog["level"]["module"], catalog["level"]["wall_height"]], [1.0, 3.0]), json.dumps(catalog["level"]))
    every_field = all(all(key in shape for key in ("id", "kind", "description", "tags", "parameters", "default_size",
                                                   "material_slots", "connectors", "normal_style", "triangle_budget", "collision"))
                      for shape in shapes)
    check("every catalog shape carries its id, kind, description, tags, parameters, default size, material slots, connectors, "
          "normal style, triangle budget and collision", every_field)
    return catalog


def default_parameters(shape):
    return {parameter["name"]: parameter["default"] for parameter in shape["parameters"]}


def echoes_defaults(shape, parameters):
    """The reply's parameters are the catalog defaults, the default mask's values inside their derive ranges."""
    if parameters.get("derive") != shape["default_derive"]:
        return False
    for parameter in shape["parameters"]:
        value = parameters.get(parameter["name"])
        if parameter["default_derived"]:
            if ("derive_min" in parameter) and not (parameter["derive_min"] - TOLERANCE <= value <= parameter["derive_max"] + TOLERANCE):
                return False
        elif value != parameter["default"]:
            return False
    return True


def check_catalog_row(editor, scene, catalog):
    """The 14 shapes at their defaults in one row along X; returns (row pieces, row center x, row length)."""
    shapes = catalog["shapes"]
    gap = 1.0
    cursor = 0.0
    requests = []
    for shape in shapes:
        size = shape["default_size"]
        bounds = shape["default_bounds"]
        # The origin is the base center; seeded shapes sit their bounds around it only roughly.
        x = cursor - bounds["min"][0]
        requests.append({"shape": shape["id"], "position": [x, 0.0, 0.0], "name": f"row {shape['id']}"})
        cursor += size[0] + gap
    length = cursor - gap
    depth_before = undo_depth(editor)
    reply = editor.call("level_place_pieces", {"scene_name": scene, "pieces": requests})
    editor.wait_idle()
    placed = reply["pieces"]
    check("the catalog row places 14 pieces in one call and one undo entry",
          (reply["count"] == 14) and (undo_depth(editor) == depth_before + 1), f"count {reply['count']}, undo {depth_before} -> {undo_depth(editor)}")

    by_id = {shape["id"]: shape for shape in shapes}
    size_problems = []
    for piece in placed:
        size = size_of(piece["world_aabb"])
        catalog_size = by_id[piece["shape"]]["default_size"]
        if not close(size, catalog_size):
            size_problems.append(f"{piece['shape']} {fmt3(size)} vs catalog {fmt3(catalog_size)}")
        if piece["shape"] in EXACT_DEFAULT_SIZES:
            if not close(size, EXACT_DEFAULT_SIZES[piece["shape"]]):
                size_problems.append(f"{piece['shape']} {fmt3(size)} vs expected {fmt3(EXACT_DEFAULT_SIZES[piece['shape']])}")
        elif not by_id[piece["shape"]]["default_derive"]:
            # A shape with a default mask has derived sizes; its catalog default_size is the check.
            nominal = NOMINAL_DEFAULT_SIZES[piece["shape"]]
            if not all(abs(s - n) <= NOMINAL_SHARE * n for s, n in zip(size, nominal)):
                size_problems.append(f"{piece['shape']} {fmt3(size)} vs nominal {fmt3(nominal)}")
    check("every catalog row piece's world_aabb matches its shape's default size (exact sizes for the unseeded shapes)",
          not size_problems, "; ".join(size_problems) if size_problems else f"{len(placed)} pieces")

    on_ground = [p["shape"] for p in placed if abs(p["world_aabb"]["min"][1]) > TOLERANCE]
    check("every catalog row piece stands on the ground (world_aabb min y 0)", not on_ground, ", ".join(on_ground))

    echo_problems = [p["shape"] for p in placed if not echoes_defaults(by_id[p["shape"]], p["parameters"])]
    check("the reply's parameters echo the catalog defaults", not echo_problems, ", ".join(echo_problems))

    material_problems = [f"{p['shape']}: {p['material']}" for p in placed
                         if p["material"] != by_id[p["shape"]]["material_slots"][0]["default_material"]]
    check("every catalog row piece has the palette material of its shape's slot", not material_problems,
          "; ".join(material_problems) if material_problems else ", ".join(sorted({p["material"] for p in placed})))
    materials = {m["name"] for m in editor.call("get_scene_materials", {"scene_name": scene}).get("materials", [])}
    palette = {s["material_slots"][0]["default_material"] for s in shapes}
    check("the palette materials the row uses are in the scene's content library", palette <= materials, f"{sorted(palette - materials)} missing")

    listed = {p["id"]: p for p in pieces(editor, scene)}
    row_ids = [p["id"] for p in placed]
    check("level_get_pieces lists the 14 pieces with their shape and record", all((i in listed) for i in row_ids) and
          all(listed[p["id"]]["shape"] == p["shape"] and listed[p["id"]]["parameters"] == p["parameters"] for p in placed))
    stairs = next(listed[p["id"]] for p in placed if p["shape"] == "basic.stairs")
    block  = next(listed[p["id"]] for p in placed if p["shape"] == "basic.block")
    check("a piece has a static physics body (stairs)",
          stairs["physics"]["has_body"] and (stairs["physics"]["motion_mode"] == "static"), json.dumps(stairs["physics"]))
    check("the stairs collide as a triangle mesh and the block as a convex hull",
          ("mesh" in stairs["physics"]["collision_shape"].lower()) and ("convex" in block["physics"]["collision_shape"].lower()),
          f"stairs {stairs['physics']['collision_shape']}, block {block['physics']['collision_shape']}")
    details = editor.call("get_node_details", {"scene_name": scene, "node_id": block["id"]})
    physics = details.get("physics") or details.get("node_physics") or {}
    check("get_node_details reports the block's static body", json.dumps(physics).find("static") >= 0, json.dumps(physics)[:200])
    return placed, length


def render_row(editor, scene, length, image_path):
    center = 0.5 * length
    camera = {"eye": [center, 6.0, 29.0], "target": [center, 1.0, 0.0], "fov_y_degrees": 32.0, "near": 0.1, "far": 200.0}
    os.makedirs(os.path.dirname(image_path), exist_ok=True)
    reply = editor.call("render_scene_image", {"scene": scene, "camera": camera, "width": IMAGE_WIDTH, "height": IMAGE_HEIGHT,
                                               "output": "png", "path": image_path})
    written = os.path.isfile(image_path)
    check(f"render_scene_image of the catalog row is written ({IMAGE_WIDTH} x {IMAGE_HEIGHT})",
          written and (reply.get("width") == IMAGE_WIDTH) and (reply.get("height") == IMAGE_HEIGHT), image_path)
    print(f"catalog row image: {image_path} ({IMAGE_WIDTH} x {IMAGE_HEIGHT})", flush=True)


def check_sharing_and_undo(editor, scene, row):
    row_block = next(p for p in row if p["shape"] == "basic.block")
    requests = [
        {"shape": "basic.block", "position": [0.0, 0.0, -8.0], "name": "block a"},
        {"shape": "basic.block", "position": [2.0, 0.0, -8.0], "name": "block b"},
        {"shape": "basic.block", "position": [4.0, 0.0, -8.0], "name": "block c", "parameters": {"width": 2.0}},
        {"shape": "basic.stairs", "position": [8.0, 0.0, -8.0], "name": "stairs", "parameters": {"rise": 1.5, "width": 2.0}},
    ]
    count_before = len(pieces(editor, scene))
    depth_before = undo_depth(editor)
    reply = editor.call("level_place_pieces", {"scene_name": scene, "pieces": requests})
    editor.wait_idle()
    a, b, c, stairs = reply["pieces"]
    check("a batch of 4 pieces is placed as one undo entry",
          (reply["count"] == 4) and (len(pieces(editor, scene)) == count_before + 4) and (undo_depth(editor) == depth_before + 1),
          f"count {reply['count']}, undo {depth_before} -> {undo_depth(editor)}")
    check("the reply echoes the given parameters over the defaults",
          (c["parameters"] == {"width": 2.0, "height": 1.0, "depth": 1.0, "seed": 1, "derive": []}) and
          (stairs["parameters"] == {"rise": 1.5, "width": 2.0, "step_rise_limit": stairs["parameters"]["step_rise_limit"], "seed": 1, "derive": []}) and
          (abs(stairs["parameters"]["step_rise_limit"] - 0.2) < TOLERANCE),
          json.dumps([c["parameters"], stairs["parameters"]]))
    check("two pieces of equal values share one mesh (one brush, one mesh key)",
          (a["brush_id"] == b["brush_id"]) and (a["mesh_key"] == b["mesh_key"]), f"{a['brush_id']} {b['brush_id']}")
    check("two pieces of different values do not share a mesh",
          (a["brush_id"] != c["brush_id"]) and (a["mesh_key"] != c["mesh_key"]), f"{a['brush_id']} {c['brush_id']}")
    check("equal values in a later call reuse the catalog row's mesh", a["brush_id"] == row_block["brush_id"],
          f"{a['brush_id']} vs row {row_block['brush_id']}")
    listed = {p["id"]: p for p in pieces(editor, scene)}
    check("level_get_pieces: each batch piece's mesh is its brush's primitive, a and b share the brush",
          all(listed[p["id"]].get("shares_brush_primitive") for p in (a, b, c, stairs)) and
          (listed[a["id"]]["brush_id"] == listed[b["id"]]["brush_id"]))
    check("the stairs of rise 1.5 m are 1.5 m high (8 steps), 2 m wide and 2.25 m deep",
          close(size_of(stairs["world_aabb"]), [2.0, 1.5, 2.25]), fmt3(size_of(stairs["world_aabb"])))
    check("a piece reports the triangle count of its mesh (block 12)", a["triangle_count"] == 12, str(a["triangle_count"]))

    editor.call("undo")
    editor.wait_idle()
    after_undo = pieces(editor, scene)
    check("one undo removes the whole batch", (len(after_undo) == count_before) and not any(p["id"] in (a["id"], b["id"], c["id"], stairs["id"]) for p in after_undo),
          f"{len(after_undo)} pieces, {count_before} before the batch")
    editor.call("redo")
    editor.wait_idle()
    after_redo = {p["id"] for p in pieces(editor, scene)}
    check("redo restores the batch", all(p["id"] in after_redo for p in (a, b, c, stairs)) and (len(after_redo) == count_before + 4),
          f"{len(after_redo)} pieces")


def check_rejections(editor, scene):
    count_before = len(pieces(editor, scene))
    good = {"shape": "basic.block", "position": [0.0, 0.0, -14.0]}

    error = call_error(editor, "level_place_pieces", {"scene_name": scene, "pieces": [good, {"shape": "basic.block", "parameters": {"width": 100.0}}]})
    check("a value outside its range fails naming the piece index, the parameter and its range",
          (error is not None) and ("pieces[1]" in error) and ("'width'" in error) and ("0.05 - 64" in error), error or "succeeded")
    error = call_error(editor, "level_place_pieces", {"scene_name": scene, "pieces": [good, {"shape": "basic.block", "colour": "red"}]})
    check("an unknown key in a piece fails naming the piece and the key", (error is not None) and ("pieces[1]" in error) and ("colour" in error), error or "succeeded")
    error = call_error(editor, "level_place_pieces", {"scene_name": scene, "pieces": [good], "snapping": "module"})
    check("an unknown top-level key fails naming the key", (error is not None) and ("snapping" in error), error or "succeeded")
    error = call_error(editor, "level_place_pieces", {"scene_name": scene, "pieces": [{"shape": "basic.block", "parameters": {"size": 2.0}}]})
    check("an unknown parameter fails naming it", (error is not None) and ("'size'" in error), error or "succeeded")
    error = call_error(editor, "level_place_pieces", {"scene_name": scene, "pieces": [{"shape": "basic.blob"}]})
    check("an unknown shape fails naming it", (error is not None) and ("basic.blob" in error), error or "succeeded")
    error = call_error(editor, "level_place_pieces", {"scene_name": scene, "pieces": [{"shape": "basic.block", "rotation_xyzw": [0.0, 0.0, 0.0, 2.0]}]})
    check("a scaled rotation quaternion is refused", (error is not None) and ("rotation_xyzw" in error), error or "succeeded")
    editor.wait_idle()
    check("a failing call places nothing", len(pieces(editor, scene)) == count_before, f"{len(pieces(editor, scene))} vs {count_before}")


def check_snap(editor, scene, module, given, expected, label):
    reply = editor.call("level_place_pieces", {"scene_name": scene, "snap": "module",
                                               "pieces": [{"shape": "basic.block", "position": given, "name": f"snap {label}"}]})
    editor.wait_idle()
    position = reply["pieces"][0]["position"]
    check(f"snap module rounds {given} to {expected} (module {module} m)", close(position, expected), fmt3(position))


def check_level_settings(editor, scene):
    editor.call("set_scene_settings", {"scene_name": scene, "merge": True,
                                       "settings": {"_version": 4, "level": {"_version": 1, "module": 2.0, "wall_height": 4.0}}})
    editor.wait_idle()
    catalog = editor.call("level_get_kits", {"scene_name": scene})
    by_id = {shape["id"]: shape for shape in catalog["shapes"]}
    check("level_get_kits follows set_scene_settings level (module 2 m, wall height 4 m)",
          close([catalog["level"]["module"], catalog["level"]["wall_height"]], [2.0, 4.0]) and
          (default_parameters(by_id["basic.block"])["width"] == 2.0) and (default_parameters(by_id["basic.stairs"])["rise"] == 4.0),
          json.dumps(catalog["level"]))
    reply = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.block",       "position": [0.0, 0.0, 14.0]},
        {"shape": "architecture.wall", "position": [6.0, 0.0, 14.0]},
    ]})
    editor.wait_idle()
    block, wall = reply["pieces"]
    check("a piece placed after the change takes the new defaults (block 2 m, wall 8 x 4 m)",
          close(size_of(block["world_aabb"]), [2.0, 2.0, 2.0]) and close(size_of(wall["world_aabb"]), [8.0, 4.0, 0.25]),
          f"block {fmt3(size_of(block['world_aabb']))}, wall {fmt3(size_of(wall['world_aabb']))}")
    check_snap(editor, scene, 2.0, [3.1, 0.4, 0.9], [4.0, 0.0, 0.0], "module 2")


def cached_brush_count(editor, scene):
    return editor.call("level_get_kits", {"scene_name": scene})["cached_brush_count"]


def piece_by_id(editor, scene, node_id):
    return next((p for p in pieces(editor, scene) if p["id"] == node_id), None)


def check_parameter_changes(editor, scene):
    reply = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.stairs", "position": [0.0, 0.0, -20.0], "name": "stairs to change", "parameters": {"rise": 1.5, "width": 2.0},
         "rotation_xyzw": [0.0, math.sin(math.radians(15.0)), 0.0, math.cos(math.radians(15.0))]},
        {"shape": "basic.stairs", "position": [4.0, 0.0, -20.0], "name": "stairs that stay", "parameters": {"rise": 1.5, "width": 2.0}},
    ]})
    editor.wait_idle()
    changed, kept = reply["pieces"]
    before = piece_by_id(editor, scene, changed["id"])
    kept_before = piece_by_id(editor, scene, kept["id"])
    count_before = cached_brush_count(editor, scene)
    depth_before = undo_depth(editor)

    result = editor.call("level_set_piece_parameters", {"scene_name": scene, "node_id": changed["id"], "parameters": {"rise": 2.0}})
    editor.wait_idle()
    check("level_set_piece_parameters is one undo entry", undo_depth(editor) == depth_before + 1, f"undo {depth_before} -> {undo_depth(editor)}")
    check("the reply merges the given rise over the piece's values (width 2 kept)",
          result["changed"] and (result["parameters"]["rise"] == 2.0) and (result["parameters"]["width"] == 2.0), json.dumps(result["parameters"]))
    after = piece_by_id(editor, scene, changed["id"])
    check("the reply's world_aabb is the piece's, and the stairs of rise 2 m are 2 m high",
          close(size_of(result["world_aabb"]), size_of(after["world_aabb"])) and close([after["world_aabb"]["max"][1] - after["world_aabb"]["min"][1]], [2.0]),
          fmt3(size_of(after["world_aabb"])))
    check("the changed piece keeps its id, name, position, rotation and material",
          (after["name"] == before["name"]) and close(after["position"], before["position"]) and
          close(after["rotation_xyzw"], before["rotation_xyzw"]) and (after["material"] == before["material"]),
          f"{after['name']} {fmt3(after['position'])} {after['material']}")
    check("the changed piece's record, mesh key and brush follow the new values",
          (after["parameters"]["rise"] == 2.0) and (after["mesh_key"] == result["mesh_key"]) and (after["brush_id"] == result["brush_id"]) and
          (after["brush_id"] != before["brush_id"]) and after["shares_brush_primitive"],
          f"brush {before['brush_id']} -> {after['brush_id']}")
    check("the changed piece's body stays static with a new triangle mesh shape",
          after["physics"]["has_body"] and (after["physics"]["motion_mode"] == "static") and ("mesh" in after["physics"]["collision_shape"].lower()),
          json.dumps(after["physics"]))
    kept_after = piece_by_id(editor, scene, kept["id"])
    check("a second piece sharing the old mesh is unaffected",
          (kept_after["brush_id"] == kept_before["brush_id"]) and (kept_after["parameters"] == kept_before["parameters"]) and
          close(size_of(kept_after["world_aabb"]), size_of(kept_before["world_aabb"])),
          f"brush {kept_after['brush_id']}, size {fmt3(size_of(kept_after['world_aabb']))}")
    check("the cache holds one more brush for the new values", cached_brush_count(editor, scene) == count_before + 1,
          f"{count_before} -> {cached_brush_count(editor, scene)}")

    editor.call("undo")
    editor.wait_idle()
    undone = piece_by_id(editor, scene, changed["id"])
    check("one undo restores the rise, the bounds and the shared mesh",
          (undone["parameters"]["rise"] == 1.5) and (undone["brush_id"] == kept_before["brush_id"]) and
          close(undone["world_aabb"]["min"], before["world_aabb"]["min"]) and close(undone["world_aabb"]["max"], before["world_aabb"]["max"]) and
          undone["shares_brush_primitive"] and (undone["physics"]["collision_shape"] == before["physics"]["collision_shape"]),
          f"rise {undone['parameters']['rise']}, brush {undone['brush_id']}")
    editor.call("redo")
    editor.wait_idle()
    redone = piece_by_id(editor, scene, changed["id"])
    check("redo applies the change again", (redone["parameters"]["rise"] == 2.0) and (redone["brush_id"] == result["brush_id"]),
          f"rise {redone['parameters']['rise']}, brush {redone['brush_id']}")

    depth = undo_depth(editor)
    error = call_error(editor, "level_set_piece_parameters", {"scene_name": scene, "node_id": changed["id"], "parameters": {"rise": 100.0}})
    editor.wait_idle()
    unchanged = piece_by_id(editor, scene, changed["id"])
    check("an out-of-range change fails naming the parameter and its range and leaves the piece unchanged",
          (error is not None) and ("'rise'" in error) and (unchanged["parameters_text"] == redone["parameters_text"]) and
          (unchanged["brush_id"] == redone["brush_id"]) and (undo_depth(editor) == depth), error or "succeeded")
    node = editor.call("create_node", {"scene_name": scene, "name": "not a piece"})
    editor.wait_idle()
    node_id = node.get("node_id", node.get("id"))
    error = call_error(editor, "level_set_piece_parameters", {"scene_name": scene, "node_id": node_id, "parameters": {"rise": 1.0}})
    check("a node that is not a piece is an error", (error is not None) and ("not a kit piece" in error), error or "succeeded")

    # A brush only the redo history holds goes with the history.
    count = cached_brush_count(editor, scene)
    editor.call("level_set_piece_parameters", {"scene_name": scene, "node_id": changed["id"], "parameters": {"rise": 2.5}})
    editor.wait_idle()
    count_set = cached_brush_count(editor, scene)
    editor.call("undo")
    editor.wait_idle()
    count_undone = cached_brush_count(editor, scene)
    editor.call("clear_undo_history")
    editor.wait_idle()
    count_cleared = cached_brush_count(editor, scene)
    check("cached_brush_count: +1 for rise 2.5, kept while the redo entry holds it, back after clear_undo_history",
          (count_set == count + 1) and (count_undone == count + 1) and (count_cleared == count),
          f"{count} -> {count_set} -> undo {count_undone} -> cleared {count_cleared}")


def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz]


def q_conj(q):
    return [-q[0], -q[1], -q[2], q[3]]


def q_rotate(q, v):
    return q_mul(q_mul(q, [v[0], v[1], v[2], 0.0]), q_conj(q))[:3]


def q_yaw(degrees):
    half = math.radians(degrees) * 0.5
    return [0.0, math.sin(half), 0.0, math.cos(half)]


def normalize(v):
    length = math.sqrt(sum(x * x for x in v))
    return [x / length for x in v]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def tilt_only(before, after, normal, tolerance=2.0e-3):
    """True when after = r * before with r the shortest rotation from before's up axis to normal (the heading kept)."""
    up = q_rotate(before, [0.0, 1.0, 0.0])
    r = q_mul(after, q_conj(before))
    if r[3] < 0.0:
        r = [-x for x in r]
    expected_axis = normalize(cross(up, normal))
    expected_angle = math.acos(max(-1.0, min(1.0, dot(up, normal))))
    angle = 2.0 * math.acos(max(-1.0, min(1.0, r[3])))
    axis = normalize(r[:3]) if angle > 1.0e-6 else expected_axis
    return (abs(angle - expected_angle) <= tolerance) and close(axis, expected_axis, tolerance)


def check_surface_lift(editor, scene):
    """Rays start one piece height above the base, so a sunk piece rises; a roof above that height is not a surface."""
    z = 52.0
    base = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [0.0, 0.0, z], "name": "slab for a sunk block"},
        {"shape": "basic.floor_slab", "position": [6.0, 0.0, z], "name": "slab for a sunk placed block"},
        {"shape": "nature.hill",      "position": [16.0, 0.0, z], "name": "hill"},
        {"shape": "basic.floor_slab", "position": [28.0, 0.0, z], "name": "floor under a roof"},
        {"shape": "basic.floor_slab", "position": [28.0, 6.0, z], "name": "roof"},
    ]})
    editor.wait_idle()
    slab, slab2, hill, floor, roof = base["pieces"]
    slab_top = slab["world_aabb"]["max"][1]
    # A spot on the hill's slope lower than the bush is tall: the bush rises at most its own height.
    bush_height = next(s for s in editor.call("level_get_kits", {"scene_name": scene})["shapes"] if s["id"] == "nature.bush")["default_size"][1]
    bush_xz = None
    hill_hit = {}
    for step in range(64):
        x = 16.0 + 0.25 * step
        hit = editor.call("raycast", {"scene_name": scene, "origin": [x, 30.0, z], "direction": [0.0, -1.0, 0.0], "mask": 1})
        if hit.get("hit") and (0.1 < hit["position"][1] < 0.5 * bush_height):
            bush_xz = [x, z]
            hill_hit = hit
            break
    check("the hill has a slope point lower than half the bush's height", bush_xz is not None, f"bush height {bush_height:.3f}")
    if bush_xz is None:
        return
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.block", "position": [0.0, 0.0, z], "name": "sunk block", "surface_snap": True},
        {"shape": "basic.block", "position": [6.0, 0.0, z], "name": "sunk placed block"},
        {"shape": "nature.bush", "position": [bush_xz[0], 0.0, bush_xz[1]], "name": "bush in the hill", "surface_orient": True},
        {"shape": "basic.block", "position": [28.0, 1.0, z], "name": "block under the roof"},
    ]})
    editor.wait_idle()
    sunk, sunk_placed, bush, roofed = placed["pieces"]
    check("a block placed at y 0 over a slab with surface_snap rises onto the slab's top",
          sunk["surface_found"] and (abs(sunk["world_aabb"]["min"][1] - slab_top) < TOLERANCE), f"base {sunk['world_aabb']['min'][1]:.4f}, slab top {slab_top:.4f}")
    snapped = editor.call("level_snap_to_surface", {"scene_name": scene, "node_ids": [sunk_placed["id"], roofed["id"]]})
    editor.wait_idle()
    sunk_answer, roofed_answer = snapped["nodes"]
    check("level_snap_to_surface lifts a sunk placed block onto the slab with a negative distance",
          sunk_answer["surface_found"] and close(sunk_answer["position"], [6.0, slab_top, z]) and (abs(sunk_answer["distance"] + slab_top) < TOLERANCE),
          f"{fmt3(sunk_answer['position'])} distance {sunk_answer['distance']:.4f}")
    floor_top = floor["world_aabb"]["max"][1]
    roofed_after = piece_by_id(editor, scene, roofed["id"])
    check("a block under a roof more than one piece height above its base rests on the floor",
          roofed_answer["surface_found"] and (abs(roofed_after["world_aabb"]["min"][1] - floor_top) < TOLERANCE) and
          (roofed_after["world_aabb"]["max"][1] < roof["world_aabb"]["min"][1]),
          f"base {roofed_after['world_aabb']['min'][1]:.4f}, floor top {floor_top:.4f}, roof bottom {roof['world_aabb']['min'][1]:.4f}")
    bush_up = q_rotate(bush["rotation_xyzw"], [0.0, 1.0, 0.0])
    check("a bush placed at ground height inside a hill (slope lower than the bush) with surface_orient ends on the hill's surface, up axis on its normal",
          hill_hit.get("hit") and bush["surface_found"] and (abs(bush["position"][1] - hill_hit["position"][1]) < 0.05) and
          close(bush_up, normalize(hill_hit["normal"]), 0.05),
          f"bush {fmt3(bush['position'])} up {fmt3(bush_up)}, hill hit {fmt3(hill_hit.get('position', [0, 0, 0]))} normal {fmt3(normalize(hill_hit.get('normal', [0, 1, 0])))}")


def check_surface_placement(editor, scene, image_path):
    z = 40.0
    probe = editor.call("raycast", {"scene_name": scene, "origin": [200.0, 50.0, 200.0], "direction": [0.0, -1.0, 0.0], "mask": 1})
    check("the scene has no surface under the empty-space probe (200, *, 200)", not probe.get("hit"), json.dumps(probe)[:200])

    base = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [0.0, 0.0, z], "name": "slab"},
        {"shape": "basic.wedge",      "position": [6.0, 0.0, z], "name": "wedge"},
        {"shape": "basic.floor_slab", "position": [12.0, 0.0, z], "name": "slab for the tool"},
        {"shape": "basic.wedge",      "position": [18.0, 0.0, z], "name": "wedge for orient and snap"},
        {"shape": "basic.floor_slab", "position": [24.0, 0.0, z], "name": "slab with an overhang"},
    ]})
    editor.wait_idle()
    slab, wedge, tool_slab, wedge2, edge_slab = base["pieces"]
    slab_top = slab["world_aabb"]["max"][1]
    hit = editor.call("raycast", {"scene_name": scene, "origin": [6.0, 5.0, z], "direction": [0.0, -1.0, 0.0], "mask": 1})
    slope_normal = normalize(hit["normal"])
    check("the wedge's slope normal (raycast) is (0, 2, 1) / sqrt(5)", close(slope_normal, normalize([0.0, 2.0, 1.0])), fmt3(slope_normal))

    yaw = q_yaw(30.0)
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.block", "position": [0.0, 3.0, z], "name": "block on slab", "surface_snap": True},
        {"shape": "nature.bush", "position": [6.0, 3.0, z], "name": "bush on wedge", "rotation_xyzw": yaw, "surface_orient": True},
        {"shape": "basic.block", "position": [18.0, 3.0, z], "name": "yawed block on wedge", "rotation_xyzw": yaw,
         "surface_snap": True, "surface_orient": True},
        {"shape": "basic.block", "position": [200.0, 5.0, 200.0], "name": "block over nothing", "surface_snap": True},
    ]})
    editor.wait_idle()
    block, bush, yawed, lost = placed["pieces"]
    check("surface_snap rests the block's base on the slab's top",
          block["surface_found"] and close([block["world_aabb"]["min"][1], block["position"][1]], [slab_top, slab_top]),
          f"base {block['world_aabb']['min'][1]:.4f}, slab top {slab_top:.4f}")
    bush_up = q_rotate(bush["rotation_xyzw"], [0.0, 1.0, 0.0])
    check("surface_orient gives the bush the wedge's slope normal as its up axis", bush["surface_found"] and close(bush_up, slope_normal),
          f"up {fmt3(bush_up)}, normal {fmt3(slope_normal)}")
    check("surface_orient keeps the bush's heading (the only change is the tilt about the horizontal axis)",
          tilt_only(yaw, bush["rotation_xyzw"], slope_normal), json.dumps(bush["rotation_xyzw"]))
    middle = 0.5 * wedge["world_aabb"]["max"][1]
    check(f"the bush's base center rests on the slope (y {middle:.2f} at the wedge's middle)", abs(bush["position"][1] - middle) < 0.05,
          fmt3(bush["position"]))
    yawed_up = q_rotate(yawed["rotation_xyzw"], [0.0, 1.0, 0.0])
    check("orient + snap on a yawed block: up axis = slope normal and the yaw is kept",
          yawed["surface_found"] and close(yawed_up, slope_normal) and tilt_only(yaw, yawed["rotation_xyzw"], slope_normal),
          f"up {fmt3(yawed_up)}, rotation {json.dumps(yawed['rotation_xyzw'])}")
    check("a placement over empty space reports surface_found false and stays at the given point",
          (not lost["surface_found"]) and close(lost["position"], [200.0, 5.0, 200.0]), fmt3(lost["position"]))

    raised = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.block", "position": [12.0, 3.0, z], "name": "raised block"},
        {"shape": "basic.block", "position": [25.3, 3.0, z], "name": "block over the edge"},
    ]})
    editor.wait_idle()
    raised_block, overhang = raised["pieces"]
    depth = undo_depth(editor)
    query = editor.call("level_snap_to_surface", {"scene_name": scene, "node_ids": [raised_block["id"]], "query_only": True})
    editor.wait_idle()
    answer = query["nodes"][0]
    still = piece_by_id(editor, scene, raised_block["id"])
    check("level_snap_to_surface query_only answers the resting position and moves nothing",
          answer["surface_found"] and close(answer["position"], [12.0, slab_top, z]) and (abs(answer["distance"] - (3.0 - slab_top)) < TOLERANCE) and
          close(still["position"], [12.0, 3.0, z]) and (undo_depth(editor) == depth) and (query["moved_count"] == 0),
          f"answer {fmt3(answer['position'])} distance {answer['distance']:.4f}, node at {fmt3(still['position'])}")
    moved = editor.call("level_snap_to_surface", {"scene_name": scene, "node_ids": [raised_block["id"], overhang["id"]]})
    editor.wait_idle()
    rested = piece_by_id(editor, scene, raised_block["id"])
    edge = piece_by_id(editor, scene, overhang["id"])
    check("level_snap_to_surface moves both blocks as one undo entry",
          (moved["moved_count"] == 2) and (undo_depth(editor) == depth + 1) and close(rested["position"], [12.0, slab_top, z]),
          f"moved {moved['moved_count']}, undo {depth} -> {undo_depth(editor)}, block at {fmt3(rested['position'])}")
    check("a block hanging over the slab's edge rests on the slab (corner rays)",
          close([edge["world_aabb"]["min"][1]], [slab_top]) and close(edge["position"], [25.3, slab_top, z]), fmt3(edge["position"]))
    editor.call("undo")
    editor.wait_idle()
    back = piece_by_id(editor, scene, raised_block["id"])
    back_edge = piece_by_id(editor, scene, overhang["id"])
    check("one undo takes both blocks back up", close(back["position"], [12.0, 3.0, z]) and close(back_edge["position"], [25.3, 3.0, z]),
          f"{fmt3(back['position'])}, {fmt3(back_edge['position'])}")
    editor.call("redo")
    editor.wait_idle()

    check_surface_lift(editor, scene)

    camera = {"eye": [12.0, 7.0, z + 16.0], "target": [12.0, 0.5, z], "fov_y_degrees": 50.0, "near": 0.1, "far": 200.0}
    os.makedirs(os.path.dirname(image_path), exist_ok=True)
    reply = editor.call("render_scene_image", {"scene": scene, "camera": camera, "width": IMAGE_WIDTH, "height": IMAGE_HEIGHT,
                                               "output": "png", "path": image_path})
    check(f"render_scene_image of the surface placements is written ({IMAGE_WIDTH} x {IMAGE_HEIGHT})",
          os.path.isfile(image_path) and (reply.get("width") == IMAGE_WIDTH), image_path)
    print(f"surface placement image: {image_path} ({IMAGE_WIDTH} x {IMAGE_HEIGHT})", flush=True)


def build_navigation(editor, scene, timeout=600.0):
    """navigation_build polled to its end through get_save_status; the job's last state."""
    job_id = editor.call("navigation_build", {"scene_name": scene})["job_id"]
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        job = editor.call("get_save_status", {"job_id": job_id})
        if job.get("state") in ("done", "failed"):
            editor.wait_idle()
            return job
        time.sleep(0.05)
    raise RuntimeError(f"navigation_build job {job_id} did not finish")


def check_walkable_level(editor, image_path):
    """A floor slab, stairs at their defaults and an upper slab, built into a navigation mesh (doc/editor/level_tools.md "Walkable area reads")."""
    scene = create_scene(editor)
    shapes = {shape["id"]: shape for shape in editor.call("level_get_kits", {"scene_name": scene})["shapes"]}
    stairs_bounds = shapes["basic.stairs"]["default_bounds"]
    rise       = stairs_bounds["max"][1] - stairs_bounds["min"][1]
    front_z    = stairs_bounds["max"][2]  # the stairs rise toward -z from their front edge
    back_z     = stairs_bounds["min"][2]
    thickness  = 0.2
    upper_size = 6.0
    floor_top  = thickness
    upper_top  = floor_top + rise
    upper_center = [0.0, upper_top, back_z - 0.5 * upper_size]
    reply = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [0.0, 0.0, 2.0], "name": "floor",
         "parameters": {"width": 12.0, "depth": 16.0, "thickness": thickness}},
        {"shape": "basic.stairs", "position": [0.0, floor_top, 0.0], "name": "stairs"},
        {"shape": "basic.floor_slab", "position": [0.0, upper_top - thickness, upper_center[2]], "name": "upper floor",
         "parameters": {"width": upper_size, "depth": upper_size, "thickness": thickness}},
    ]})
    editor.wait_idle()
    floor, stairs, upper = reply["pieces"]
    check("the walkable level: the stairs' top meets the upper slab's top and front edge",
          (abs(stairs["world_aabb"]["max"][1] - upper["world_aabb"]["max"][1]) < TOLERANCE) and
          (abs(stairs["world_aabb"]["min"][2] - upper["world_aabb"]["max"][2]) < TOLERANCE),
          f"stairs {fmt3(stairs['world_aabb']['min'])} .. {fmt3(stairs['world_aabb']['max'])}, upper {fmt3(upper['world_aabb']['min'])} .. {fmt3(upper['world_aabb']['max'])}")
    seed_point = [0.0, floor_top + 0.05, front_z + 3.0]
    node = editor.call("create_node", {"scene_name": scene, "name": "seed", "position": seed_point})
    editor.wait_idle()
    editor.call("navigation_set_seed", {"scene_name": scene, "node_id": node.get("node_id", node.get("id"))})
    editor.set_export_settings(scene, {})
    editor.wait_idle()

    job = build_navigation(editor, scene)
    status = editor.call("get_navigation_status", {"scene_name": scene})
    check("the walkable level's navigation mesh builds from the three pieces",
          (job.get("state") == "done") and (status.get("input_mesh_count") == 3) and not status.get("stale"),
          f"{job.get('state')} {job.get('error', '')} meshes {status.get('input_mesh_count')} - {status.get('status_text')}")
    upper_point = editor.call("navigation_find_point", {"scene_name": scene, "position": upper_center})
    check("the upper slab is reachable (navigation_find_point on its top)",
          upper_point.get("found") and upper_point.get("reachable") and (abs(upper_point["point"][1] - upper_top) < 0.3), json.dumps(upper_point))
    path = editor.call("navigation_find_path", {"scene_name": scene, "start": seed_point, "end": upper_center})
    climb = [corner[1] for corner in path.get("corners", [])]
    check("a path from the floor up the stairs to the upper slab exists",
          path.get("found") and path.get("complete") and climb and (max(climb) > upper_top - 0.3) and (path["length"] >= abs(upper_center[2] - seed_point[2])),
          f"found {path.get('found')}, complete {path.get('complete')}, length {path.get('length')}, {len(climb)} corners, reason {path.get('reason')}")
    summary = editor.call("get_navigation_summary", {"scene_name": scene})
    check("the floor and the upper slab are one reachable island",
          (summary.get("reachable_island_count") == 1) and (upper_point.get("island") == path.get("start", {}).get("island")),
          f"islands {summary.get('island_count')}, reachable {summary.get('reachable_island_count')}")

    os.makedirs(os.path.dirname(image_path), exist_ok=True)
    camera = {"eye": [11.0, 9.0, 9.0], "target": [0.0, 1.5, -2.5], "fov_y_degrees": 45.0, "near": 0.1, "far": 200.0}
    image = editor.call("render_scene_image", {"scene": scene, "camera": camera, "width": IMAGE_WIDTH, "height": IMAGE_HEIGHT,
                                               "output": "png", "path": image_path, "navigation_mesh": "all"})
    check(f"render_scene_image of the walkable level with the navigation mesh is written ({IMAGE_WIDTH} x {IMAGE_HEIGHT})",
          os.path.isfile(image_path) and (image.get("width") == IMAGE_WIDTH), image_path)
    print(f"walkable level image: {image_path} ({IMAGE_WIDTH} x {IMAGE_HEIGHT})", flush=True)

    max_climb = 0.4  # the default agent_max_climb of the export settings added above
    editor.call("level_set_piece_parameters", {"scene_name": scene, "node_id": stairs["id"], "parameters": {"step_rise_limit": 0.5}})
    editor.wait_idle()
    raised = piece_by_id(editor, scene, stairs["id"])
    stale = editor.call("get_navigation_status", {"scene_name": scene})
    check(f"raising the step rise above agent_max_climb ({max_climb} m) keeps the stairs' run and makes the navigation mesh stale",
          close(raised["world_aabb"]["min"], stairs["world_aabb"]["min"]) and close(raised["world_aabb"]["max"], stairs["world_aabb"]["max"]) and
          stale.get("stale") and (stale.get("stale_reasons") == ["geometry changed"]),
          f"stale {stale.get('stale')} {stale.get('stale_reasons')}")
    job = build_navigation(editor, scene)
    path = editor.call("navigation_find_path", {"scene_name": scene, "start": seed_point, "end": upper_center})
    upper_point = editor.call("navigation_find_point", {"scene_name": scene, "position": upper_center})
    check("after the rebuild no path joins the floor and the upper slab, which is not reachable",
          (job.get("state") == "done") and (not path.get("found")) and (path.get("reason") == "different islands") and
          upper_point.get("found") and not upper_point.get("reachable"),
          f"{job.get('state')}, path found {path.get('found')} reason {path.get('reason')}, upper {json.dumps(upper_point)[:200]}")
    return scene


def piece_key(piece):
    """What a piece is, independent of its node id: name, record and rounded world bounds."""
    bounds = tuple(round(x, 2) + 0.0 for x in piece["world_aabb"]["min"] + piece["world_aabb"]["max"]) if piece.get("world_aabb") else None
    return (piece["name"], piece["shape"], piece["parameters_text"], bounds)


def material_count(editor, scene):
    return len(editor.call("get_scene_materials", {"scene_name": scene}).get("materials", []))


def check_saved_file(label, path):
    """R8.2: every piece node of the saved glTF file references an ordinary mesh with positions."""
    with open(path, "r", encoding="utf-8") as file:
        document = json.load(file)
    piece_nodes = [n for n in document.get("nodes", [])
                   if "Kit_piece.shape" in n.get("extensions", {}).get("ERHE_node", {}).get("properties", {})]
    meshes = document.get("meshes", [])
    plain = [n for n in piece_nodes if ("mesh" in n) and (n["mesh"] < len(meshes)) and
             all("POSITION" in primitive.get("attributes", {}) for primitive in meshes[n["mesh"]].get("primitives", [])) and
             meshes[n["mesh"]].get("primitives")]
    check(f"{label}: the saved file holds every piece as a node with an ordinary glTF mesh (R8.2)",
          piece_nodes and (len(plain) == len(piece_nodes)), f"{len(plain)} of {len(piece_nodes)} piece nodes")
    return document


def save_close_load(editor, scene, path):
    before = pieces(editor, scene)
    materials = material_count(editor, scene)
    editor.save(scene, path)
    editor.close(scene)
    name = editor.load(path)
    return before, materials, name


def check_reloaded(label, editor, before, materials_before, name):
    after = pieces(editor, name)
    check(f"{label}: every piece is back with its name, record and bounds",
          sorted(map(piece_key, before)) == sorted(map(piece_key, after)),
          f"{len(after)} pieces after, {len(before)} before; first differences "
          f"{sorted(set(map(piece_key, after)) ^ set(map(piece_key, before)))[:3]}")
    not_following = [f"{p['name']}: {p.get('problem')}" for p in after if not p.get("follows_kit")]
    check(f"{label}: every reloaded piece follows its kit and its mesh is the kit brush's",
          (not not_following) and all(p.get("shares_brush_primitive") for p in after), "; ".join(not_following[:5]))
    brushes = {}
    for piece in after:
        brushes.setdefault((piece["shape"], piece["parameters_text"]), set()).add(piece.get("brush_id"))
    shared = all(len(ids) == 1 for ids in brushes.values())
    distinct = len({next(iter(ids)) for ids in brushes.values()}) == len(brushes)
    groups = sum(1 for p in after) - len(brushes)
    check(f"{label}: equal values share one mesh and different values do not ({groups} pieces share an earlier piece's mesh)",
          shared and distinct and (groups > 0 or label == "walkable level"), f"{len(brushes)} value sets")
    bodies = [p["name"] for p in after if not (p["physics"]["has_body"] and p["physics"]["motion_mode"] == "static")]
    check(f"{label}: every reloaded piece has its static body", not bodies, ", ".join(bodies[:5]))
    check(f"{label}: the palette materials are back by name, none added",
          material_count(editor, name) <= materials_before and all(p["material"] for p in after),
          f"{material_count(editor, name)} materials, {materials_before} before the save")
    return after


def check_persistence(editor, catalog_scene, walkable_scene, directory):
    """Save, close and load the kit levels (doc/editor/level_tools.md "Persistence")."""
    if catalog_scene is not None:
        catalog_path = os.path.join(directory, "catalog_level.gltf")
        before, materials, name = save_close_load(editor, catalog_scene, catalog_path)
        check_saved_file("catalog level", catalog_path)
        check_reloaded("catalog level", editor, before, materials, name)
        editor.close(name)

    walkable_path = os.path.join(directory, "walkable_level.gltf")
    before, materials, name = save_close_load(editor, walkable_scene, walkable_path)
    document = check_saved_file("walkable level", walkable_path)
    after = check_reloaded("walkable level", editor, before, materials, name)

    stairs = next(p for p in after if p["shape"] == "basic.stairs")
    depth = undo_depth(editor)
    result = editor.call("level_set_piece_parameters", {"scene_name": name, "node_id": stairs["id"], "parameters": {"rise": 2.0}})
    editor.wait_idle()
    changed = piece_by_id(editor, name, stairs["id"])
    check("a reloaded piece takes a parameter change as one undo entry",
          result["changed"] and (undo_depth(editor) == depth + 1) and changed["follows_kit"] and
          (abs(size_of(changed["world_aabb"])[1] - 2.0) < TOLERANCE),
          f"height {size_of(changed['world_aabb'])[1]:.3f}, undo {depth} -> {undo_depth(editor)}")
    editor.call("undo")
    editor.wait_idle()
    undone = piece_by_id(editor, name, stairs["id"])
    check("undo gives the reloaded piece its loaded values, bounds and shared kit mesh back",
          (undone["parameters_text"] == stairs["parameters_text"]) and (undone["brush_id"] == stairs["brush_id"]) and
          close(undone["world_aabb"]["max"], stairs["world_aabb"]["max"]) and undone["follows_kit"],
          f"brush {undone['brush_id']} vs {stairs['brush_id']}")

    materials = material_count(editor, name)
    placed = editor.call("level_place_pieces", {"scene_name": name, "pieces": [{"shape": "basic.block", "position": [4.0, 0.2, 4.0]}]})
    editor.wait_idle()
    check("a placement after the reload reuses the palette material found by name (material count unchanged)",
          (material_count(editor, name) == materials) and (placed["pieces"][0]["material"] == stairs["material"]),
          f"{materials} -> {material_count(editor, name)}, material {placed['pieces'][0]['material']}")
    editor.close(name)

    # A copy whose stairs name an unknown shape and whose upper floor has a width out of range.
    edited = json.loads(json.dumps(document))
    edited_names = {"stairs": None, "upper floor": None}
    for node in edited.get("nodes", []):
        erhe_node = node.get("extensions", {}).get("ERHE_node", {})
        for key in ("properties", "mesh_properties"):
            properties = erhe_node.get(key, {})
            if "Kit_piece.shape" not in properties:
                continue
            if node.get("name") == "stairs":
                properties["Kit_piece.shape"] = "basic.no_such_shape"
                edited_names["stairs"] = node["name"]
            elif node.get("name") == "upper floor":
                record = json.loads(properties["Kit_piece.parameters"])
                record["width"] = "0x1p+10"
                properties["Kit_piece.parameters"] = json.dumps(record, separators=(",", ":"))
                edited_names["upper floor"] = node["name"]
    edited.get("extensions", {}).get("ERHE_scene", {}).pop("editor_asset", None)
    edited_path = os.path.join(directory, "walkable_level_edited.gltf")
    with open(edited_path, "w", encoding="utf-8") as file:
        json.dump(edited, file)
    check("the hand-edited copy names an unknown shape on the stairs and a width of 1024 m on the upper floor",
          all(edited_names.values()), json.dumps(edited_names))
    edited_name = editor.load(edited_path)
    loaded = {p["name"]: p for p in pieces(editor, edited_name)}
    before_by_name = {p["name"]: p for p in after}
    problems = {n: loaded.get(n, {}) for n in ("stairs", "upper floor")}
    check("the edited pieces load keeping their saved meshes (bounds as saved), follows_kit false, with a problem text",
          all(p and (not p.get("follows_kit")) and p.get("problem") and
              close(p["world_aabb"]["min"], before_by_name[n]["world_aabb"]["min"]) and
              close(p["world_aabb"]["max"], before_by_name[n]["world_aabb"]["max"]) for n, p in problems.items()),
          "; ".join(f"{n}: follows {p.get('follows_kit')}, problem '{p.get('problem')}'" for n, p in problems.items()))
    check("the unknown shape and the out-of-range width are named in the problem texts",
          ("basic.no_such_shape" in problems["stairs"].get("problem", "")) and ("'width'" in problems["upper floor"].get("problem", "")),
          json.dumps({n: p.get("problem") for n, p in problems.items()}))
    check("the edited copy's untouched piece follows its kit", loaded.get("floor", {}).get("follows_kit") is True,
          json.dumps(loaded.get("floor", {}).get("problem")))
    log_text = pav.read_log()
    check("the load names each piece that keeps its loaded mesh in one log line",
          ("Kit piece 'stairs'" in log_text) and ("Kit piece 'upper floor'" in log_text), "")
    editor.close(edited_name)


def imgui_items(editor, label=None):
    arguments = {"window": WINDOW, "limit": 400, "visible_only": False}
    if label is not None:
        arguments["label_contains"] = label
    return editor.call("get_imgui_items", arguments).get("items", [])


def click(editor, label, window=WINDOW):
    if window is not None:
        editor.call("imgui_scroll_to_item", {"window": window, "label": label})
        return editor.call("imgui_click", {"window": window, "label": label})
    return editor.call("imgui_click", {"label": label})


def is_checked(editor, label):
    item = next((i for i in imgui_items(editor, label) if i.get("display_label") == label), None)
    if item is None:
        raise RuntimeError(f"no checkbox {label!r} in the {WINDOW} window")
    return bool(item.get("status", {}).get("checked"))


def set_checkbox(editor, label, checked):
    if is_checked(editor, label) != checked:
        click(editor, label)
    return is_checked(editor, label) == checked


def preview(editor):
    return editor.call("level_get_placement_preview", {})


def wait_preview(editor, predicate, timeout=10.0):
    """Polls the kit mode preview until predicate holds; returns the last reply."""
    deadline = time.monotonic() + timeout
    reply = preview(editor)
    while (not predicate(reply)) and (time.monotonic() < deadline):
        time.sleep(0.1)
        reply = preview(editor)
    return reply


def window_point(viewport, point):
    """The editor window pixel a world point projects to, from the camera frame get_viewports reports."""
    m = viewport["camera_world_from_camera"]
    right, up, back, origin = m[0:3], m[4:7], m[8:11], m[12:15]
    d = [point[i] - origin[i] for i in range(3)]
    depth = -dot(d, back)
    tan_y = math.tan(0.5 * viewport["camera_fov_y"])
    tan_x = tan_y * viewport["width"] / viewport["height"]
    ndc_x = dot(d, right) / (depth * tan_x)
    ndc_y = dot(d, up) / (depth * tan_y)
    if (depth <= 0.0) or (abs(ndc_x) > 0.95) or (abs(ndc_y) > 0.95):
        raise RuntimeError(f"{fmt3(point)} is outside the viewport (ndc {ndc_x:.2f}, {ndc_y:.2f})")
    return (viewport["x"] + (0.5 + 0.5 * ndc_x) * viewport["width"],
            viewport["y"] + (0.5 - 0.5 * ndc_y) * viewport["height"])


def hover(editor, viewport, point):
    """Moves the pointer over the world point (a wheel turn of zero moves and settles without zooming)."""
    x, y = window_point(viewport, point)
    editor.call("mouse_wheel", {"x": x, "y": y, "dy": 0.0})
    return x, y


def binding_input(editor, command, kind):
    """The input of the first effective binding of the command with the given kind (key / mouse_button)."""
    for entry in editor.call("list_input_bindings").get("commands", []):
        if entry.get("command") == command:
            for binding in entry.get("bindings", []):
                parts = binding.split(":")
                if parts[0] == kind and len(parts) > 1:
                    return parts[1].split("+")[-1]
    raise RuntimeError(f"no {kind} binding of {command}")


def same_rotation(a, b, tolerance=1.0e-4):
    return close(a, b, tolerance) or close(a, [-c for c in b], tolerance)


def new_pieces(editor, scene, known_ids):
    return [p for p in pieces(editor, scene) if p["id"] not in known_ids]


MASK64 = (1 << 64) - 1
SEED_MAX = 1000000


def kit_random_next_u32(state):
    """Kit_random::next_u32() (splitmix64), returning (value, state)."""
    state = (state + 0x9e3779b97f4a7c15) & MASK64
    z = state
    z = ((z ^ (z >> 30)) * 0xbf58476d1ce4e5b9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94d049bb133111eb) & MASK64
    z = z ^ (z >> 31)
    return z >> 32, state


def kit_next_seed(seed):
    """editor::kit_next_seed(): the seed after a seed, as New Seed Per Piece and the seed row's New button take it."""
    state = (seed & 0xffffffff) ^ 0x6a09e667f3bcc909
    name_hash = 0xcbf29ce484222325
    for c in b"next_seed":
        name_hash = ((name_hash ^ c) * 0x100000001b3) & MASK64
    state ^= name_hash
    value, _ = kit_random_next_u32(state)
    next_seed = value % (SEED_MAX + 1)
    return next_seed if next_seed != seed else (next_seed + 1) % (SEED_MAX + 1)


def check_derive_catalog(catalog):
    """D1.8 and D4.5 in the catalog: every shape has a seed, derive fields per parameter, the default masks."""
    by_id = {shape["id"]: shape for shape in catalog["shapes"]}
    problems = []
    for shape in catalog["shapes"]:
        names = [p["name"] for p in shape["parameters"]]
        if "seed" not in names:
            problems.append(f"{shape['id']} has no seed")
        for parameter in shape["parameters"]:
            if parameter.get("derive") not in ("fixed", "derivable") or ("default_derived" not in parameter):
                problems.append(f"{shape['id']}.{parameter['name']} lacks derive / default_derived")
            if (parameter.get("derive") == "derivable") and (parameter["type"] != "enum") and not (parameter["min"] <= parameter["derive_min"] <= parameter["derive_max"] <= parameter["max"]):
                problems.append(f"{shape['id']}.{parameter['name']} derive range {parameter.get('derive_min')} - {parameter.get('derive_max')} outside {parameter['min']} - {parameter['max']}")
        if shape["default_derive"] != [p["name"] for p in shape["parameters"] if p["default_derived"]]:
            problems.append(f"{shape['id']} default_derive {shape['default_derive']}")
    check("every catalog shape has a seed and per parameter derive, default_derived and a derive range inside the valid range",
          not problems, "; ".join(problems[:5]))
    rock = {p["name"]: p for p in by_id["nature.rock"]["parameters"]}
    stairs = {p["name"]: p for p in by_id["basic.stairs"]["parameters"]}
    block = {p["name"]: p for p in by_id["basic.block"]["parameters"]}
    check("the rock's form (blob, angular, worn, slab) and bevels (0 - 2) derive, its size is derived by default within 0.6 - 1.5 m",
          (rock["form"]["derive"] == "derivable") and (rock["form"]["values"] == ["blob", "angular", "worn", "slab"]) and
          (rock["bevels"]["derive"] == "derivable") and (rock["bevels"]["min"] == 0) and (rock["bevels"]["max"] == 2) and
          rock["size"]["default_derived"] and close([rock["size"]["derive_min"], rock["size"]["derive_max"]], [0.6, 1.5]) and
          (by_id["nature.rock"]["default_derive"] == ["size"]), json.dumps({k: {f: v.get(f) for f in ("derive", "default_derived", "derive_min", "derive_max")} for k, v in rock.items()}))
    check("a block's width derives within 0.5 - 3 modules (meters at module 1), the stairs' step_rise_limit and every seed are fixed, basic shapes mask nothing",
          close([block["width"]["derive_min"], block["width"]["derive_max"]], [0.5, 3.0]) and (stairs["step_rise_limit"]["derive"] == "fixed") and
          all(p["derive"] == "fixed" for s in catalog["shapes"] for p in s["parameters"] if p["name"] == "seed") and
          all(not by_id[i]["default_derive"] for i in ("basic.block", "basic.wedge", "basic.cylinder", "basic.stairs", "basic.floor_slab")),
          json.dumps(block["width"]))


ROCK_FORMS = ["blob", "angular", "worn", "slab"]
ROCK_IMAGE_WIDTH  = 1600
ROCK_IMAGE_HEIGHT = 1000


def check_rock_forms_and_derive(editor, directory, image_path):
    """D1.9 rock forms and D1.8 derive from seed over MCP, in a scene of their own, and the rock image for the reviewer."""
    scene = create_scene(editor)
    requests = [{"shape": "basic.floor_slab", "position": [0.0, -0.2, -1.0], "name": "rock floor", "parameters": {"width": 14.0, "depth": 10.0}}]
    grid = []
    for column, form in enumerate(ROCK_FORMS):
        for bevels in range(3):
            if (form == "blob") and (bevels > 0):
                continue
            grid.append((form, bevels))
            requests.append({"shape": "nature.rock", "position": [-4.5 + (3.0 * column), 0.0, -1.8 * bevels], "name": f"rock {form} {bevels}",
                             "parameters": {"form": form, "bevels": bevels, "seed": 7, "size": 1.2, "derive": []}})
    # Eight consecutive seeds as New Seed Per Piece gives them, with form, size, roughness and bevels derived.
    seeds = [11]
    while len(seeds) < 8:
        seeds.append(kit_next_seed(seeds[-1]))
    variety_mask = ["form", "size", "roughness", "bevels"]
    for i, seed in enumerate(seeds):
        requests.append({"shape": "nature.rock", "position": [-5.6 + (1.6 * i), 0.0, 2.4], "name": f"rock seed {seed}",
                         "parameters": {"seed": seed, "derive": variety_mask}})
    reply = editor.call("level_place_pieces", {"scene_name": scene, "pieces": requests})
    editor.wait_idle()
    rocks = reply["pieces"][1:1 + len(grid)]
    variety = reply["pieces"][1 + len(grid):]
    problems = [f"{p['name']}: {p['triangle_count']} triangles, min y {p['world_aabb']['min'][1]:.4f}" for p in rocks + variety
                if (p["triangle_count"] > 500) or (abs(p["world_aabb"]["min"][1]) > TOLERANCE) or (p["triangle_count"] < 4)]
    check("every rock form at 0, 1 and 2 bevels and eight seeded rocks are placed, within 500 triangles, resting on their flat base",
          (len(rocks) == len(grid)) and (len(variety) == 8) and not problems, "; ".join(problems))
    counts = {p["name"]: p["triangle_count"] for p in rocks}
    print("rock triangle counts (seed 7, size 1.2): " + ", ".join(f"{name[5:]} {count}" for name, count in counts.items()), flush=True)
    check("bevel passes add facets to every hull form (0 < 1 < 2 bevels), the blob has no bevels",
          all(counts[f"rock {f} 0"] < counts[f"rock {f} 1"] < counts[f"rock {f} 2"] for f in ("angular", "worn", "slab")),
          json.dumps(counts))
    listed = {p["id"]: p for p in pieces(editor, scene)}
    rock_physics = listed[rocks[1]["id"]]["physics"]
    check("a rock's collision is a convex hull and its pieces follow the kit",
          ("convex" in rock_physics["collision_shape"].lower()) and all(listed[p["id"]]["follows_kit"] for p in rocks + variety), json.dumps(rock_physics))
    check("the eight seeded rocks derive their masked parameters: derive echoes the mask, values in their ranges, several forms",
          all(p["parameters"]["derive"] == variety_mask and (0.6 - TOLERANCE <= p["parameters"]["size"] <= 1.5 + TOLERANCE) and
              (0.1 - TOLERANCE <= p["parameters"]["roughness"] <= 0.6 + TOLERANCE) for p in variety) and
          (len({p["parameters"]["form"] for p in variety}) >= 2) and (len({p["mesh_key"] for p in variety}) == 8),
          json.dumps([(p["parameters"]["form"], round(p["parameters"]["size"], 3), p["parameters"]["bevels"]) for p in variety]))

    camera = {"eye": [0.0, 6.5, 9.5], "target": [0.0, 0.0, -0.4], "fov_y_degrees": 50.0, "near": 0.1, "far": 100.0}
    os.makedirs(os.path.dirname(image_path), exist_ok=True)
    image = editor.call("render_scene_image", {"scene": scene, "camera": camera, "width": ROCK_IMAGE_WIDTH, "height": ROCK_IMAGE_HEIGHT,
                                               "output": "png", "path": image_path})
    check(f"render_scene_image of the rock forms is written ({ROCK_IMAGE_WIDTH} x {ROCK_IMAGE_HEIGHT})",
          os.path.isfile(image_path) and (image.get("width") == ROCK_IMAGE_WIDTH), image_path)
    print(f"rock forms image: {image_path} (columns blob, angular, worn, slab; rows 0, 1, 2 bevels front to back; front row seeds {seeds})", flush=True)

    # The default mask: the size derives from the seed; equal seed and mask give one mesh.
    reply = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "nature.rock", "position": [6.0, 0.0, -4.0], "name": "masked a", "parameters": {"seed": 21}},
        {"shape": "nature.rock", "position": [6.0, 0.0, -2.0], "name": "masked b", "parameters": {"seed": 21}},
        {"shape": "nature.rock", "position": [6.0, 0.0,  0.0], "name": "masked c", "parameters": {"seed": 22}},
    ]})
    editor.wait_idle()
    a, b, c = reply["pieces"]
    check("a masked placement derives the size from the seed (derive [size], size within 0.6 - 1.5 m on a 0.05 step)",
          (a["parameters"]["derive"] == ["size"]) and (0.6 - TOLERANCE <= a["parameters"]["size"] <= 1.5 + TOLERANCE) and
          (abs(round(a["parameters"]["size"] / 0.05) * 0.05 - a["parameters"]["size"]) < TOLERANCE), json.dumps(a["parameters"]))
    check("two pieces of equal seed and mask are identical and share one mesh",
          (a["parameters"] == b["parameters"]) and (a["brush_id"] == b["brush_id"]) and (a["mesh_key"] == b["mesh_key"]),
          f"{a['brush_id']} {b['brush_id']}")
    check("another seed gives another piece", (c["mesh_key"] != a["mesh_key"]) and (c["parameters"]["seed"] == 22), c["mesh_key"])

    error = call_error(editor, "level_place_pieces", {"scene_name": scene, "pieces": [{"shape": "nature.rock", "parameters": {"size": 1.0}}]})
    check("a value given for a masked parameter is an error naming it", (error is not None) and ("'size'" in error) and ("derived" in error), error or "succeeded")
    error = call_error(editor, "level_place_pieces", {"scene_name": scene, "pieces": [{"shape": "basic.stairs", "parameters": {"derive": ["step_rise_limit"]}}]})
    check("deriving a fixed parameter (stairs step_rise_limit) is an error naming it", (error is not None) and ("step_rise_limit" in error), error or "succeeded")
    explicit = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "nature.rock", "position": [7.5, 0.0, -4.0], "name": "explicit", "parameters": {"seed": 21, "size": 1.0, "derive": []}}]})["pieces"][0]
    editor.wait_idle()
    check("derive [] gives the size explicitly", (explicit["parameters"]["derive"] == []) and (explicit["parameters"]["size"] == 1.0),
          json.dumps(explicit["parameters"]))

    # A seed change derives the masked values again, equal to a fresh placement of that seed.
    depth = undo_depth(editor)
    result = editor.call("level_set_piece_parameters", {"scene_name": scene, "node_id": a["id"], "parameters": {"seed": 22}})
    editor.wait_idle()
    check("a seed change re-derives the masked size (as a placement of that seed) in one undo entry",
          (result["parameters"]["size"] == c["parameters"]["size"]) and (result["mesh_key"] == c["mesh_key"]) and (undo_depth(editor) == depth + 1),
          f"{result['parameters']} vs {c['parameters']}")
    error = call_error(editor, "level_set_piece_parameters", {"scene_name": scene, "node_id": a["id"], "parameters": {"size": 1.0}})
    check("level_set_piece_parameters refuses a value for a masked parameter", (error is not None) and ("'size'" in error), error or "succeeded")
    unmasked = editor.call("level_set_piece_parameters", {"scene_name": scene, "node_id": a["id"], "parameters": {"derive": [], "size": 1.25}})
    editor.wait_idle()
    check("level_set_piece_parameters with derive [] takes an explicit size", (unmasked["parameters"]["derive"] == []) and (unmasked["parameters"]["size"] == 1.25),
          json.dumps(unmasked["parameters"]))

    # A record from before the seed and the mask (no migration): the piece does not follow the kit, before and after a reload.
    old = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [{"shape": "nature.rock", "position": [7.5, 0.0, 0.0], "name": "old record"}]})["pieces"][0]
    editor.wait_idle()
    old_text = '{"size":"0x1p+0","roughness":"0x1.333334p-2","seed":1,"segments":7}'
    editor.call("set_item_property", {"item_id": old["id"], "property": "Kit_piece.parameters", "value": old_text})
    editor.wait_idle()
    piece = piece_by_id(editor, scene, old["id"])
    check("a piece whose record has the old form (no form, bevels or derive) does not follow the kit and names the problem",
          (piece is not None) and (not piece["follows_kit"]) and ("segments" in piece["problem"] or "form" in piece["problem"]),
          json.dumps({k: piece.get(k) for k in ("follows_kit", "problem")}) if piece else "missing")
    path = os.path.join(directory, "rock_level.gltf")
    editor.save(scene, path)
    editor.close(scene)
    name = editor.load(path)
    reloaded = {p["name"]: p for p in pieces(editor, name)}
    old_reloaded = reloaded.get("old record", {})
    check("after a reload the old-form record keeps its loaded mesh (follows_kit false) and every other rock follows the kit",
          (old_reloaded.get("follows_kit") is False) and old_reloaded.get("problem") and
          all(p["follows_kit"] for n, p in reloaded.items() if n != "old record"),
          json.dumps({k: old_reloaded.get(k) for k in ("follows_kit", "problem")}))
    check("reloaded masked rocks keep their saved values and mask", reloaded.get("masked b", {}).get("parameters") == b["parameters"],
          json.dumps(reloaded.get("masked b", {}).get("parameters")))
    editor.close(name)


def check_from_seed_toggle(editor, scene):
    """D4.5: the Properties window's from-seed toggles; toggling derives the value as one undo entry."""
    properties = "Properties"
    rock = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "nature.rock", "position": [3.0, 0.2, 3.0], "name": "toggle rock", "parameters": {"seed": 31}}]})["pieces"][0]
    editor.wait_idle()
    editor.call("select_items", {"scene_name": scene, "ids": [rock["id"]]})
    editor.wait_idle()

    def item(label):
        items = editor.call("get_imgui_items", {"window": properties, "limit": 1000, "visible_only": False, "label_contains": label}).get("items", [])
        return next((i for i in items if i.get("display_label") == label), None)

    size_toggle = item("size.from_seed")
    roughness_toggle = item("roughness.from_seed")
    new_seed = item("seed.new")
    check("the Properties window shows the from-seed toggles (size on, roughness off) and the seed's New button",
          (size_toggle is not None) and size_toggle.get("status", {}).get("checked") and (roughness_toggle is not None) and
          not roughness_toggle.get("status", {}).get("checked") and (new_seed is not None) and (item("seed.from_seed") is None),
          json.dumps([size_toggle, roughness_toggle, new_seed])[:400])
    before = piece_by_id(editor, scene, rock["id"])
    depth = undo_depth(editor)
    editor.call("imgui_scroll_to_item", {"window": properties, "label": "roughness.from_seed"})
    editor.call("imgui_click", {"window": properties, "label": "roughness.from_seed"})
    editor.wait_idle()
    after = piece_by_id(editor, scene, rock["id"])
    roughness = after["parameters"]["roughness"]
    check("clicking roughness's toggle derives it from the seed (within 0.1 - 0.6) as one undo entry",
          (after["parameters"]["derive"] == ["size", "roughness"]) and (0.1 - TOLERANCE <= roughness <= 0.6 + TOLERANCE) and
          (undo_depth(editor) == depth + 1) and after["follows_kit"] and (item("roughness.from_seed") or {}).get("status", {}).get("checked"),
          f"{after['parameters']}, undo {undo_depth(editor) - depth}")
    editor.call("undo")
    editor.wait_idle()
    undone = piece_by_id(editor, scene, rock["id"])
    check("undo gives the explicit roughness back", undone["parameters"] == before["parameters"], json.dumps(undone["parameters"]))
    editor.call("imgui_scroll_to_item", {"window": properties, "label": "seed.new"})
    editor.call("imgui_click", {"window": properties, "label": "seed.new"})
    editor.wait_idle()
    renewed = piece_by_id(editor, scene, rock["id"])
    check("the seed's New button takes the next seed and derives the masked size again, one undo entry",
          (renewed["parameters"]["seed"] == kit_next_seed(31)) and (undo_depth(editor) == depth + 1) and (renewed["mesh_key"] != before["mesh_key"]),
          f"seed {renewed['parameters']['seed']} (expected {kit_next_seed(31)}), undo {undo_depth(editor) - depth}")
    editor.call("undo")
    editor.wait_idle()


def check_level_kit_window(editor, image_path):
    """The Level Kit window and the Brush tool's kit mode, driven through the UI tools (doc/agents/mcp_ui_driving.md)."""
    scene = create_scene(editor)
    fixtures = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [0.0, 0.0, 0.0], "name": "slab", "parameters": {"width": 8.0, "depth": 8.0}},
        {"shape": "basic.wedge", "position": [2.0, 0.2, -2.0], "name": "wedge"}
    ]})["pieces"]
    editor.wait_idle()
    slab = fixtures[0]
    slab_top = slab["world_aabb"]["max"][1]
    known = {p["id"] for p in fixtures}
    camera = editor.call("get_scene_cameras", {"scene_name": scene})["cameras"][0]
    pitch = math.atan2(9.0, 12.0)
    editor.call("set_node_transform", {"scene_name": scene, "node_id": camera["id"], "translation": [0.0, 9.0, 12.0],
                                       "rotation_xyzw": [-math.sin(0.5 * pitch), 0.0, 0.0, math.cos(0.5 * pitch)]})
    isolate_viewport(editor.call, scene_name=scene)
    editor.wait_idle()
    viewport = next(v for v in editor.call("get_viewports")["viewports"] if v.get("scene") == scene)
    insert_button = binding_input(editor, "Brush_tool.insert", "mouse_button")
    rotate_key = binding_input(editor, "Brush_tool.rotate_cw", "key")
    # The window acts on the last hovered viewport.
    hover(editor, viewport, [0.0, slab_top, 0.0])

    def shown_window():
        windows = editor.call("get_imgui_windows").get("windows", [])
        return next((w for w in windows if (w.get("display_name") == WINDOW) and w.get("active") and not w.get("hidden")), None)

    # The window may already be open from the working copy's window state.
    if shown_window() is None:
        editor.call("imgui_click", {"label": "Window"})
        editor.call("imgui_click", {"window": "Window###Menu_00", "label": WINDOW})
    shown = shown_window()
    check("the Level Kit window is open (from the Window menu when it was closed; get_imgui_windows)",
          (shown is not None) and shown.get("active") and not shown.get("hidden"), json.dumps(shown)[:300])
    labels = {i.get("display_label") or "" for i in imgui_items(editor)}
    expected = {"Module", "Wall Height", "Block", "Straight stairs", "Wall", "Rock", "Snap To Module", "Snap To Surface",
                "Orient To Surface", "New Seed Per Piece", "Rotation Step", "Snap Selected To Surface", "Orient Selected To Surface"}
    check("the window lists the level settings, the shapes, the placement settings and the selection actions, and no Place button",
          (expected <= labels) and ("Place" not in labels), f"missing {sorted(expected - labels)}")
    settings_ok = (set_checkbox(editor, "Snap To Module", True) and set_checkbox(editor, "Snap To Surface", True)
                   and set_checkbox(editor, "Orient To Surface", False) and set_checkbox(editor, "New Seed Per Piece", False))
    if preview(editor).get("rotation_step_degrees") != 90:
        click(editor, "Rotation Step")
        editor.call("imgui_click", {"label": "90 deg"})
    check("the placement settings are module snap, surface snap, no orient and a 90 degree rotation step",
          settings_ok and (preview(editor).get("rotation_step_degrees") == 90), json.dumps(preview(editor))[:200])
    check("before a shape is chosen the Brush tool is not in kit mode", not preview(editor).get("active"), "")

    click(editor, "Block")
    state = wait_preview(editor, lambda r: r.get("active"))
    block_rows = {i.get("display_label") or "" for i in imgui_items(editor)}
    check("choosing the Block row enters kit mode: the Brush tool is the active tool with the block's kit brush",
          state.get("active") and state.get("tool_active") and (state.get("shape") == "basic.block"), json.dumps(state)[:300])
    check("the Block row shows its parameter rows and Reset To Defaults",
          {"width", "height", "depth", "Reset To Defaults"} <= block_rows, f"{sorted(block_rows)[:40]}")

    # Module snap and surface snap over the slab.
    target = [-2.1, slab_top, 0.9]
    hover(editor, viewport, target)
    state = wait_preview(editor, lambda r: r.get("visible") and close(r.get("hover_position") or [9, 9, 9], target, 0.15))
    hovered = state.get("hover_position") or [0.0, 0.0, 0.0]
    expected_position = [round(hovered[0]), slab_top, round(hovered[2])]
    check("the pointer over the slab hovers the slab where it was aimed",
          close(hovered, target, 0.15), f"hover {fmt3(hovered)}, aimed {fmt3(target)}")
    check("the preview over the slab is the module-rounded hover point with its base on the slab top",
          state.get("visible") and state.get("surface_found") and close(state.get("position", [9, 9, 9]), expected_position, 1.0e-4)
          and close(state.get("rotation_xyzw", [9, 9, 9, 9]), [0.0, 0.0, 0.0, 1.0], 1.0e-5),
          f"{json.dumps(state)[:400]}")
    os.makedirs(os.path.dirname(image_path), exist_ok=True)
    reply = editor.call("capture_screenshot", {"path": image_path})
    check("capture_screenshot of the editor with the kit preview over the slab is written", os.path.isfile(image_path), json.dumps(reply)[:200])
    print(f"level kit preview screenshot: {image_path}", flush=True)

    # Alt (menu) + right press belongs to the camera's tumble, so it places nothing.
    depth = undo_depth(editor)
    x, y = window_point(viewport, target)
    editor.call("mouse_click", {"x": x, "y": y, "button": insert_button, "modifiers": ["menu"]})
    editor.wait_idle()
    added = new_pieces(editor, scene, known)
    check("a right click with Alt held places nothing and records no undo entry",
          (len(added) == 0) and (undo_depth(editor) == depth), f"added {len(added)}, undo {undo_depth(editor) - depth}")
    hover(editor, viewport, target)
    state = wait_preview(editor, lambda r: r.get("visible") and close(r.get("hover_position") or [9, 9, 9], target, 0.15))

    placed = []

    def place_here(label, aim, point_state):
        depth = undo_depth(editor)
        x, y = window_point(viewport, aim)
        editor.call("mouse_click", {"x": x, "y": y, "button": insert_button})
        editor.wait_idle()
        added = new_pieces(editor, scene, known | {p["id"] for p in placed})
        piece = added[0] if len(added) == 1 else None
        ok = (piece is not None) and (undo_depth(editor) == depth + 1) and point_state.get("visible")
        if ok:
            ok = close(piece["position"], point_state["position"], 1.0e-4) and same_rotation(piece["rotation_xyzw"], point_state["rotation_xyzw"])
        check(f"{label}: a click places one piece, one undo entry, at the preview's position and rotation",
              ok, f"added {[(p['shape'], fmt3(p['position']), p['rotation_xyzw']) for p in added]}, preview {fmt3(point_state.get('position', [0, 0, 0]))} "
                  f"{point_state.get('rotation_xyzw')}, undo {undo_depth(editor) - depth}")
        if piece is not None:
            placed.append(piece)
        return piece

    selection_before = editor.call("get_selection")
    first = place_here("module and surface snap", target, state)
    selection_after = editor.call("get_selection")
    check("the placed piece is not selected (the Brush tool's insert keeps the selection)",
          (first is not None) and (json.dumps(selection_before, sort_keys=True) == json.dumps(selection_after, sort_keys=True)),
          f"{json.dumps(selection_after)[:200]}")
    after_click = wait_preview(editor, lambda r: r.get("active"))
    check("the Brush tool stays in kit mode after a placement", after_click.get("active") and after_click.get("tool_active"), "")

    second_aim = [0.1, slab_top, 2.1]
    hover(editor, viewport, second_aim)
    state = wait_preview(editor, lambda r: r.get("visible") and close(r.get("hover_position") or [9, 9, 9], second_aim, 0.15))
    second = place_here("a second click elsewhere", second_aim, state)
    check("the second piece shares the first one's mesh (one kit brush)",
          (first is not None) and (second is not None) and (first.get("brush_id") == second.get("brush_id")),
          f"{first and first.get('brush_id')} vs {second and second.get('brush_id')}")

    # Stacking: the placed block is a surface for the next one.
    first_position = first["position"] if first else [0.0, slab_top, 0.0]
    stack_aim = [first_position[0] + 0.1, first_position[1] + 1.0, first_position[2] + 0.1]
    hover(editor, viewport, stack_aim)
    state = wait_preview(editor, lambda r: r.get("visible") and close(r.get("hover_position") or [9, 9, 9], stack_aim, 0.15))
    check("hovering the top of a placed block puts the preview on top of it",
          close(state.get("position", [9, 9, 9]), [first_position[0], first_position[1] + 1.0, first_position[2]], 1.0e-4),
          f"preview {fmt3(state.get('position', [0, 0, 0]))}, block {fmt3(first_position)}")
    place_here("stacking", stack_aim, state)

    # A rotate step turns the preview and the next piece by 90 degrees about the up axis.
    rotate_aim = [-2.1, slab_top, -2.1]
    hover(editor, viewport, rotate_aim)
    editor.call("key_press", {"key": rotate_key})
    state = wait_preview(editor, lambda r: r.get("visible") and (r.get("yaw_steps") != 0) and close(r.get("hover_position") or [9, 9, 9], rotate_aim, 0.15))
    q = state.get("rotation_xyzw", [0.0, 0.0, 0.0, 1.0])
    half = math.sqrt(0.5)
    check(f"the rotate binding ({rotate_key}) turns the preview by 90 degrees about Y",
          (abs(q[0]) < 1.0e-5) and (abs(q[2]) < 1.0e-5) and (abs(abs(q[1]) - half) < 1.0e-5) and (abs(abs(q[3]) - half) < 1.0e-5),
          f"yaw_steps {state.get('yaw_steps')}, rotation {q}")
    place_here("after a rotate step", rotate_aim, state)

    # Orient on the wedge: the preview and the piece take the slope normal as their up axis.
    oriented = set_checkbox(editor, "Orient To Surface", True)
    shared = editor.call("get_surface_snap_settings", {})
    check("the Level Kit window's surface checkboxes are the shared pair (get_surface_snap_settings: snap and orient on)",
          shared.get("snap") and shared.get("orient"), json.dumps(shared))
    wedge_aim = [2.0, slab_top + 0.5, -2.0]
    hover(editor, viewport, wedge_aim)
    state = wait_preview(editor, lambda r: r.get("visible") and close(r.get("hover_position") or [9, 9, 9], wedge_aim, 0.2)
                         and abs(q_rotate(r.get("rotation_xyzw", [0, 0, 0, 1]), [0.0, 1.0, 0.0])[1] - 1.0) > 1.0e-3)
    slope_normal = normalize([0.0, 2.0, 1.0])
    up = q_rotate(state.get("rotation_xyzw", [0.0, 0.0, 0.0, 1.0]), [0.0, 1.0, 0.0])
    check("with Orient To Surface the preview over the wedge takes the slope normal as its up axis",
          oriented and state.get("surface_found") and close(up, slope_normal, 2.0e-3), f"up {fmt3(up)}, slope normal {fmt3(slope_normal)}")
    place_here("orient on the wedge", wedge_aim, state)
    set_checkbox(editor, "Orient To Surface", False)

    # A parameter row edit hands the Brush tool the brush of the new values.
    before_key = preview(editor).get("mesh_key")
    editor.call("imgui_scroll_to_item", {"window": WINDOW, "label": "width"})
    editor.call("imgui_click", {"window": WINDOW, "label": "width", "index": 0, "modifiers": ["ctrl"]})
    editor.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
    editor.call("type_text", {"text": "2"})
    editor.call("key_press", {"key": "enter"})
    wide_aim = [0.1, slab_top, -0.1]
    hover(editor, viewport, wide_aim)
    state = wait_preview(editor, lambda r: r.get("visible") and (r.get("mesh_key") != before_key)
                         and close(r.get("hover_position") or [9, 9, 9], wide_aim, 0.15))
    bounds = state.get("bounds") or {"min": [0, 0, 0], "max": [0, 0, 0]}
    check("editing the width row gives the preview the bounds of the new values",
          close(size_of(bounds), [2.0, 1.0, 1.0]) and (state.get("parameters", {}).get("width") == 2.0), json.dumps(state)[:300])
    wide = place_here("after a parameter change", wide_aim, state)
    check("the piece placed after the parameter change has the new size",
          (wide is not None) and close(sorted(size_of(wide["world_aabb"])), sorted(size_of(state.get("world_aabb", bounds)))) and
          (wide.get("parameters", {}).get("width") == 2.0),
          f"{wide and size_of(wide['world_aabb'])}")

    # New Seed Per Piece: each click takes the next seed, so two clicks give two different rocks.
    click(editor, "Rock")
    state = wait_preview(editor, lambda r: r.get("active") and (r.get("shape") == "nature.rock"))
    per_piece = set_checkbox(editor, "New Seed Per Piece", True)
    rock_aim = [2.5, slab_top, 2.5]
    hover(editor, viewport, rock_aim)
    state = wait_preview(editor, lambda r: r.get("visible") and (r.get("shape") == "nature.rock") and close(r.get("hover_position") or [9, 9, 9], rock_aim, 0.15))
    first_seed = state.get("parameters", {}).get("seed")
    rock_a = place_here("New Seed Per Piece, first rock", rock_aim, state)
    rock_aim = [-2.5, slab_top, 2.5]
    hover(editor, viewport, rock_aim)
    state = wait_preview(editor, lambda r: r.get("visible") and close(r.get("hover_position") or [9, 9, 9], rock_aim, 0.15)
                         and (r.get("parameters", {}).get("seed") != first_seed))
    rock_b = place_here("New Seed Per Piece, second rock", rock_aim, state)
    after_rocks = wait_preview(editor, lambda r: r.get("parameters", {}).get("seed") not in (None, first_seed, state.get("parameters", {}).get("seed")))
    check("New Seed Per Piece: two clicks place two different rocks of consecutive seeds, and the preview takes the next seed",
          per_piece and (rock_a is not None) and (rock_b is not None) and (rock_a["mesh_key"] != rock_b["mesh_key"]) and
          (rock_b["parameters"]["seed"] == kit_next_seed(rock_a["parameters"]["seed"])) and
          (after_rocks.get("parameters", {}).get("seed") == kit_next_seed(rock_b["parameters"]["seed"])),
          f"seeds {rock_a and rock_a['parameters']['seed']} {rock_b and rock_b['parameters']['seed']} preview {after_rocks.get('parameters', {}).get('seed')}")
    window_seed = next((i for i in imgui_items(editor, "seed") if i.get("display_label") == "seed"), None)
    check("the Level Kit window's seed row shows the tool's next seed", (window_seed is not None), json.dumps(window_seed)[:200])
    set_checkbox(editor, "New Seed Per Piece", False)

    # Undo removes one piece per step.
    steps_ok = True
    for i in range(len(placed)):
        before = len(pieces(editor, scene))
        editor.call("undo")
        editor.wait_idle()
        steps_ok = steps_ok and (len(pieces(editor, scene)) == before - 1)
    check(f"undo removes the {len(placed)} placed pieces one per step", steps_ok and (len(pieces(editor, scene)) == len(fixtures)),
          f"{len(pieces(editor, scene))} pieces left")

    # Choosing a palette brush leaves kit mode; the ordinary Brush tool places face-aligned as before.
    brushes = editor.call("get_scene_brushes", {"scene_name": scene}).get("brushes", [])
    palette = next((b for b in brushes if b.get("name") == "Cube"), brushes[0] if brushes else None)
    editor.call("set_tool_asset", {"tool": "brush", "scene_name": scene, "name": palette["name"]})
    state = wait_preview(editor, lambda r: not r.get("active"))
    check(f"choosing a palette brush ({palette['name']}) leaves kit mode", (not state.get("active")) and state.get("tool_active"), json.dumps(state)[:200])
    nodes_before = {n["id"] for n in editor.call("get_scene_nodes", {"scene_name": scene}).get("nodes", [])}
    face_aim = [-2.1, slab_top, 2.1]
    x, y = hover(editor, viewport, face_aim)
    wait_preview(editor, lambda r: close(r.get("hover_position") or [9, 9, 9], face_aim, 0.15))
    editor.call("mouse_click", {"x": x, "y": y, "button": insert_button})
    editor.wait_idle()
    # The preview's own nodes are named brush-<brush name>; the Hotbar's quad nodes, which follow the hovered view, are not content.
    added = [n for n in editor.call("get_scene_nodes", {"scene_name": scene}).get("nodes", [])
             if (n["id"] not in nodes_before) and n.get("content") and not n.get("name", "").startswith("brush-")]
    details = editor.call("get_node_details", {"scene_name": scene, "node_id": added[0]["id"]}) if len(added) == 1 else {}
    aabb = details.get("world_aabb") or details.get("mesh", {}).get("world_aabb") or {"min": [0, 9, 0], "max": [0, 9, 0]}
    check("a palette brush placed by a click on the slab's top face rests face-aligned on it (no kit piece record)",
          (len(added) == 1) and (abs(aabb["min"][1] - slab_top) < TOLERANCE) and (len(pieces(editor, scene)) == len(fixtures)),
          f"added {[n.get('name') for n in added]}, world_aabb {json.dumps(aabb)}")
    editor.call("undo")
    editor.wait_idle()

    # Choosing another tool leaves kit mode.
    click(editor, "Block")
    state = wait_preview(editor, lambda r: r.get("active"))
    left = False
    hover(editor, viewport, [0.0, slab_top, 0.0])
    for key in "123456789":
        editor.call("key_press", {"key": key})
        state = wait_preview(editor, lambda r: not r.get("tool_active"), timeout=1.0)
        if not state.get("tool_active"):
            left = not state.get("active")
            break
    check("choosing another tool (a hotbar slot key) leaves kit mode", left, json.dumps(state)[:200])

    stairs = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.stairs", "position": [-3.0, slab_top, 0.0], "name": "window stairs", "parameters": {"rise": 1.5}}
    ]})["pieces"][0]
    editor.wait_idle()
    editor.call("select_items", {"scene_name": scene, "ids": [stairs["id"]]})
    editor.wait_idle()
    rows = [i for i in imgui_items(editor) if i.get("display_label") in ("rise", "width", "step_rise_limit")]
    check("a selected stairs piece shows its parameter rows (rise, width, step_rise_limit) in the Selection section",
          {i.get("display_label") for i in rows} >= {"rise", "width", "step_rise_limit"}, f"{len(rows)} rows")
    return scene, stairs


def check_properties_rows(editor, scene, stairs, image_path):
    """The selected stairs piece's "Kit Piece" group in the Properties window: one row per parameter."""
    properties = "Properties"
    editor.call("set_window_visibility", {"title": properties, "visible": True, "focus": True})
    editor.wait_idle()

    def items(label=None):
        arguments = {"window": properties, "limit": 1000, "visible_only": False}
        if label is not None:
            arguments["label_contains"] = label
        return editor.call("get_imgui_items", arguments).get("items", [])

    # The group's fold state is the user's; open it when it is folded.
    header = next((i for i in items("Kit Piece") if i.get("display_label") == "Kit Piece"), None)
    if (header is not None) and not header.get("status", {}).get("opened"):
        editor.call("imgui_scroll_to_item", {"window": properties, "label": "Kit Piece"})
        editor.call("imgui_click", {"window": properties, "label": "Kit Piece"})
        editor.wait_idle()
    labels = {i.get("display_label") or "" for i in items()}
    # Developer mode shows the developer-only rows, the record's raw Shape and Parameters among them.
    developer_mode = "Mesh Raytrace" in labels
    check("the Properties window shows a Kit Piece group with one row per stairs parameter",
          {"Kit Piece", "rise", "width", "step_rise_limit"} <= labels, f"missing {sorted({'Kit Piece', 'rise', 'width', 'step_rise_limit'} - labels)}")
    check("the record's raw Parameters row (canonical JSON) is not shown outside developer mode",
          developer_mode or ("Parameters" not in labels), f"developer mode {developer_mode}")
    reply = editor.call("get_item_properties", {"item_id": stairs["id"]})
    raw = [p for p in reply.get("properties", []) if str(p.get("name", "")).endswith("parameters")]
    check("MCP get_item_properties still lists the raw Kit_piece.parameters text",
          bool(raw) and ("rise" in str(raw[0].get("value", ""))), json.dumps(raw)[:300])

    before = next(p for p in pieces(editor, scene) if p["id"] == stairs["id"])
    depth = undo_depth(editor)
    editor.call("imgui_scroll_to_item", {"window": properties, "label": "rise"})
    editor.call("imgui_click", {"window": properties, "label": "rise", "modifiers": ["ctrl"]})
    editor.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
    editor.call("type_text", {"text": "1"})
    editor.call("key_press", {"key": "enter"})
    editor.wait_idle()
    after = next(p for p in pieces(editor, scene) if p["id"] == stairs["id"])
    height_before = size_of(before["world_aabb"])[1]
    height_after = size_of(after["world_aabb"])[1]
    check("typing 1 into the Properties rise row gives the stairs a 1 m rise as one undo entry",
          (abs(height_after - 1.0) < TOLERANCE) and (abs(height_before - 1.5) < TOLERANCE) and (undo_depth(editor) == depth + 1)
          and (after.get("parameters", {}).get("rise") == 1.0),
          f"height {height_before:.3f} -> {height_after:.3f}, undo {undo_depth(editor) - depth}, {after.get('parameters')}")
    editor.call("imgui_scroll_to_item", {"window": properties, "label": "step_rise_limit", "align": "center"})
    os.makedirs(os.path.dirname(image_path), exist_ok=True)
    reply = editor.call("capture_screenshot", {"path": image_path})
    check("capture_screenshot of the Properties window with the stairs piece selected is written", os.path.isfile(image_path), json.dumps(reply)[:200])
    print(f"properties kit piece screenshot: {image_path}", flush=True)
    editor.call("undo")
    editor.wait_idle()
    undone = next(p for p in pieces(editor, scene) if p["id"] == stairs["id"])
    check("undo gives the stairs back their 1.5 m rise", abs(size_of(undone["world_aabb"])[1] - 1.5) < TOLERANCE,
          f"{size_of(undone['world_aabb'])}")


def item_rect(editor, window, label, index=0):
    editor.call("imgui_scroll_to_item", {"window": window, "label": label, "index": index})
    return editor.call("get_imgui_item_rect", {"window": window, "label": label, "index": index})


def piece_height(editor, scene, node_id):
    piece = next(p for p in pieces(editor, scene) if p["id"] == node_id)
    return size_of(piece["world_aabb"])[1], piece


def preview_step_times(log_text):
    """The milliseconds of the 'preview step' trace lines of Level_kits::preview_parameters()."""
    times = []
    for line in log_text.splitlines():
        if "preview step" in line and line.rstrip().endswith(" ms"):
            try:
                times.append(float(line.rstrip()[:-3].rsplit(" ", 1)[-1]))
            except ValueError:
                pass
    return times


def drag_row(editor, window, label, pixels, index=0):
    """Presses on a row's center and drags it by pixels with the button held; returns the center."""
    rect = item_rect(editor, window, label, index)
    x, y = rect["center_x"], rect["center_y"]
    editor.call("mouse_drag", {"from": [x, y], "to": [x + pixels, y], "frames": 10, "hold": True})
    return x, y


def check_live_drags(editor, scene, stairs, catalog_scene, message_image_path=None):
    """D3.6: parameter drags show at once, commit one undo entry at release, cancel without one."""
    properties = "Properties"
    editor.call("set_log_levels", {"loggers": {"editor.scene": "trace"}})
    editor.call("select_items", {"scene_name": scene, "ids": [stairs["id"]]})
    editor.wait_idle()
    editor.call("clear_undo_history")
    editor.wait_idle()
    count_start = cached_brush_count(editor, scene)
    height_start, start = piece_height(editor, scene, stairs["id"])
    depth = undo_depth(editor)

    x, y = drag_row(editor, properties, "rise", 60.0)
    editor.wait_idle()
    height_mid, mid = piece_height(editor, scene, stairs["id"])
    check("mid-drag the stairs show the dragged rise while the record and the undo depth stay",
          (height_mid > height_start + 0.1) and (undo_depth(editor) == depth) and (mid["parameters"]["rise"] == start["parameters"]["rise"]),
          f"height {height_start:.3f} -> {height_mid:.3f}, undo {undo_depth(editor) - depth}, record rise {mid['parameters']['rise']}")
    editor.call("mouse_release", {})
    editor.wait_idle()
    height_end, end = piece_height(editor, scene, stairs["id"])
    check("the release records one undo entry with the final rise in the record and the mesh",
          (undo_depth(editor) == depth + 1) and (abs(end["parameters"]["rise"] - height_end) < TOLERANCE) and end.get("follows_kit"),
          f"undo {undo_depth(editor) - depth}, record rise {end['parameters']['rise']}, height {height_end:.3f}, follows_kit {end.get('follows_kit')}")
    editor.call("undo")
    editor.wait_idle()
    height_undone, undone = piece_height(editor, scene, stairs["id"])
    check("one undo gives the stairs their starting rise, mesh and record back",
          (abs(height_undone - height_start) < TOLERANCE) and (undone["parameters"] == start["parameters"]) and undone.get("follows_kit"),
          f"height {height_undone:.3f}, start {height_start:.3f}")

    # A drag that ends where it began records nothing.
    depth = undo_depth(editor)
    x, y = drag_row(editor, properties, "rise", 60.0)
    editor.call("inject_input_events", {"events": [
        {"type": "mouse_move", "frame": i, "x": x + 60.0 - 12.0 * (i + 1), "y": y} for i in range(5)
    ]})
    editor.wait_idle()
    editor.call("mouse_release", {})
    editor.wait_idle()
    height_back, back = piece_height(editor, scene, stairs["id"])
    check("a drag that ends back at the starting value records no undo entry and shows the starting mesh",
          (undo_depth(editor) == depth) and (abs(height_back - height_start) < TOLERANCE) and (back["parameters"] == start["parameters"]),
          f"undo {undo_depth(editor) - depth}, height {height_back:.3f}, rise {back['parameters']['rise']}")

    # A typed value outside the range shows the message and changes nothing.
    editor.call("imgui_scroll_to_item", {"window": properties, "label": "rise"})
    editor.call("imgui_click", {"window": properties, "label": "rise", "modifiers": ["ctrl"]})
    editor.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
    editor.call("type_text", {"text": "100"})
    editor.call("key_press", {"key": "enter"})
    editor.wait_idle()
    height_typed, typed = piece_height(editor, scene, stairs["id"])
    check("a typed rise of 100 m (outside 0.1 - 16) changes nothing",
          (undo_depth(editor) == depth) and (abs(height_typed - height_start) < TOLERANCE) and (typed["parameters"] == start["parameters"]),
          f"undo {undo_depth(editor) - depth}, height {height_typed:.3f}")
    if message_image_path is not None:
        editor.call("imgui_scroll_to_item", {"window": properties, "label": "rise", "align": "center"})
        editor.call("capture_screenshot", {"path": message_image_path})
        print(f"refused typed value screenshot: {message_image_path}", flush=True)
    # The rows read the record again on a selection change.
    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.call("select_items", {"scene_name": scene, "ids": [stairs["id"]]})
    editor.wait_idle()

    # Deselecting the piece mid-drag gives it its starting mesh back without an entry.
    x, y = drag_row(editor, properties, "rise", 60.0)
    editor.wait_idle()
    height_mid, _ = piece_height(editor, scene, stairs["id"])
    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.wait_idle()
    editor.call("mouse_release", {})
    editor.wait_idle()
    height_cancelled, cancelled = piece_height(editor, scene, stairs["id"])
    check("deselecting the piece mid-drag restores its starting mesh with no undo entry",
          (height_mid > height_start + 0.1) and (abs(height_cancelled - height_start) < TOLERANCE) and (undo_depth(editor) == depth)
          and (cancelled["parameters"] == start["parameters"]) and cancelled.get("follows_kit"),
          f"mid {height_mid:.3f}, after {height_cancelled:.3f}, undo {undo_depth(editor) - depth}")

    # Escape in a typed value gives the piece its starting mesh back without an entry.
    editor.call("select_items", {"scene_name": scene, "ids": [stairs["id"]]})
    editor.wait_idle()
    depth = undo_depth(editor)
    editor.call("imgui_scroll_to_item", {"window": properties, "label": "rise"})
    editor.call("imgui_click", {"window": properties, "label": "rise", "modifiers": ["ctrl"]})
    editor.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
    editor.call("type_text", {"text": "5"})
    editor.wait_idle()
    editor.call("key_press", {"key": "escape"})
    editor.wait_idle()
    height_escaped, escaped = piece_height(editor, scene, stairs["id"])
    check("Escape in a typed rise leaves the starting mesh and record with no undo entry",
          (abs(height_escaped - height_start) < TOLERANCE) and (undo_depth(editor) == depth) and (escaped["parameters"] == start["parameters"]),
          f"height {height_escaped:.3f}, undo {undo_depth(editor) - depth}")

    # Undo pressed mid-drag ends the preview first, then undoes the last entry; the release records nothing.
    editor.call("level_set_piece_parameters", {"scene_name": scene, "node_id": stairs["id"], "parameters": {"rise": 2.0}})
    editor.wait_idle()
    depth = undo_depth(editor)
    drag_row(editor, properties, "rise", 60.0)
    editor.wait_idle()
    height_mid, _ = piece_height(editor, scene, stairs["id"])
    editor.call("undo")
    editor.wait_idle()
    height_undone, undone = piece_height(editor, scene, stairs["id"])
    editor.call("mouse_release", {})
    editor.wait_idle()
    height_released, released = piece_height(editor, scene, stairs["id"])
    check("undo mid-drag ends the preview and undoes the rise 2 change; the release records nothing",
          (height_mid > 2.1) and (abs(height_undone - height_start) < TOLERANCE) and (undone["parameters"] == start["parameters"])
          and (abs(height_released - height_start) < TOLERANCE) and (undo_depth(editor) == depth - 1) and released.get("follows_kit"),
          f"mid {height_mid:.3f}, undone {height_undone:.3f}, released {height_released:.3f}, undo {undo_depth(editor) - depth}")

    # The shape being placed follows a held drag of its row at once, without an undo entry.
    click(editor, "Block")
    state = wait_preview(editor, lambda r: r.get("active") and (r.get("parameters", {}).get("width") == 1.0))
    depth = undo_depth(editor)
    bounds_start = size_of(state["bounds"])
    drag_row(editor, WINDOW, "width", 60.0, index=0)
    mid_state = wait_preview(editor, lambda r: size_of(r.get("bounds") or state["bounds"])[0] > bounds_start[0] + 0.1)
    check("mid-drag the kit brush of the shape being placed has the dragged width",
          (size_of(mid_state["bounds"])[0] > bounds_start[0] + 0.1) and (undo_depth(editor) == depth),
          f"width {bounds_start[0]:.3f} -> {size_of(mid_state['bounds'])[0]:.3f}, undo {undo_depth(editor) - depth}")
    editor.call("mouse_release", {})
    editor.wait_idle()
    final_state = preview(editor)
    check("the release leaves the brush of the final width and records nothing",
          (abs(size_of(final_state["bounds"])[0] - final_state["parameters"]["width"]) < TOLERANCE) and (undo_depth(editor) == depth),
          f"bounds {fmt3(size_of(final_state['bounds']))}, width {final_state['parameters']['width']}")
    click(editor, "Reset To Defaults")
    wait_preview(editor, lambda r: r.get("parameters", {}).get("width") == 1.0)
    brushes = editor.call("get_scene_brushes", {"scene_name": scene}).get("brushes", [])
    editor.call("set_tool_asset", {"tool": "brush", "scene_name": scene, "name": brushes[0]["name"]})
    editor.wait_idle()
    editor.call("clear_undo_history")
    editor.wait_idle()
    count_end = cached_brush_count(editor, scene)
    check("the cached brush count returns to its starting value after the undo history is cleared",
          count_end == count_start, f"{count_start} -> {count_end}")
    # Timing: after the count check, which a selected and then undone placement would disturb (it keeps
    # its brush alive past clear_undo_history; not a parameter drag effect).
    times = preview_step_times(pav.read_log())
    # The heaviest starter shape at its defaults, dragged the same way.
    shapes = catalog_scene["shapes"]
    heaviest = max(shapes, key=lambda shape: shape.get("triangle_budget", 0))
    heavy = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [{"shape": heaviest["id"], "position": [3.0, 0.2, 3.0]}]})["pieces"][0]
    editor.wait_idle()
    editor.call("select_items", {"scene_name": scene, "ids": [heavy["id"]]})
    editor.wait_idle()
    first_float = next(p["name"] for p in heaviest["parameters"] if (p.get("type") == "float") and not p.get("default_derived"))
    before_heavy = len(preview_step_times(pav.read_log()))
    drag_row(editor, properties, first_float, 40.0)
    editor.wait_idle()
    editor.call("mouse_release", {})
    editor.wait_idle()
    heavy_times = preview_step_times(pav.read_log())[before_heavy:]
    check(f"dragging {heaviest['id']} ({heaviest.get('triangle_budget')} triangle budget) row {first_float} previews live",
          bool(heavy_times), f"{len(heavy_times)} steps")
    print(f"preview step: stairs {len(times)} steps, max {max(times) if times else 0:.3f} ms; "
          f"{heaviest['id']} {len(heavy_times)} steps, median {sorted(heavy_times)[len(heavy_times) // 2] if heavy_times else 0:.3f} ms, "
          f"max {max(heavy_times) if heavy_times else 0:.3f} ms", flush=True)
    editor.call("undo")
    editor.wait_idle()
    editor.call("undo")
    editor.wait_idle()

    editor.call("set_log_levels", {"loggers": {"editor.scene": "info"}})


# --- Surface settings in the toolbar and the Transform tool (doc/editor/level_tools.md "Surface settings") ---

TRANSFORM_WINDOW = "Transform"


def open_window(editor, name):
    """Shows the window from the Window menu unless it is shown; True when it is."""
    def shown():
        windows = editor.call("get_imgui_windows").get("windows", [])
        return any((w.get("display_name") == name) and w.get("active") and not w.get("hidden") for w in windows)
    if not shown():
        editor.call("imgui_click", {"label": "Window"})
        editor.call("imgui_click", {"window": "Window###Menu_00", "label": name})
    deadline = time.monotonic() + 5.0
    while (not shown()) and (time.monotonic() < deadline):
        time.sleep(0.1)
    return shown()


def surface_sites(editor):
    """Per setting the state the three UI sites show: {'Snap To Surface': {'kit': .., 'move': .., 'toolbar': ..}, ...}."""
    sites = {}
    for label in ("Snap To Surface", "Orient To Surface"):
        items = editor.call("get_imgui_items", {"label_contains": label, "limit": 50, "visible_only": False}).get("items", [])
        found = {}
        for item in items:
            if item.get("display_label") != label:
                continue
            window = item.get("window", "")
            site = "kit" if window.startswith(WINDOW) else "move" if window.startswith(TRANSFORM_WINDOW) else "toolbar"
            found[site] = {"checked": bool(item.get("status", {}).get("checked")), "id": item.get("id"), "window": window}
        sites[label] = found
    return sites


def sites_agree(sites, label, value):
    found = sites.get(label, {})
    return (set(found.keys()) == {"kit", "move", "toolbar"}) and all(entry["checked"] == value for entry in found.values())


def click_site(editor, label, site):
    entry = surface_sites(editor)[label][site]
    editor.call("imgui_click", {"window": entry["window"], "id": entry["id"]})
    editor.wait_idle()


def gizmo_handle(editor, viewport, values):
    """The first shown gizmo handle of the viewport whose handle_value is in values."""
    handles = editor.call("get_transform_handles", {"viewport": viewport["title"]})
    return next((h for h in handles.get("handles", []) if (h.get("handle_value") in values) and ("world" in h)), None)


def gizmo_drag(editor, viewport, values, delta, hold=False):
    """Presses on a translate handle and pulls the pointer to where the grab point plus delta projects."""
    handle = gizmo_handle(editor, viewport, values)
    if handle is None:
        handles = editor.call("get_transform_handles", {"viewport": viewport["title"]})
        raise RuntimeError(f"no gizmo handle {values} on screen: {json.dumps(handles)[:600]}")
    # The grab point's own projection, so the pointer's travel maps to exactly delta along the handle.
    start = window_point(viewport, handle["world"])
    probe = editor.call("get_transform_handles", {"viewport": viewport["title"], "probe_points": [[start[0], start[1]]]})["probes"][0]
    if probe.get("handle_value") != handle["handle_value"]:
        start = (handle["x"], handle["y"])
    to = window_point(viewport, [handle["world"][i] + delta[i] for i in range(3)])
    # Held, so the drag takes the last move before the release (a release in the same frame does not).
    editor.call("mouse_drag", {"from": [start[0], start[1]], "to": [to[0], to[1]], "frames": 10, "hold": True})
    editor.wait_idle()
    if not hold:
        editor.call("mouse_release", {})
        editor.wait_idle()


TRANSLATE_X  = (1, 2)
TRANSLATE_Y  = (3, 4)
TRANSLATE_XZ = (8,)


def select(editor, scene, ids):
    editor.call("select_items", {"scene_name": scene, "ids": ids})
    editor.wait_idle()


def place_at(editor, scene, node_id, position, rotation=(0.0, 0.0, 0.0, 1.0)):
    editor.call("set_node_transform", {"scene_name": scene, "node_id": node_id, "translation": list(position), "rotation_xyzw": list(rotation)})
    editor.wait_idle()


def surface_step_times(log_text, entries):
    times = []
    marker = f"surface step: {entries} entries"
    for line in log_text.splitlines():
        if marker in line and line.rstrip().endswith(" ms"):
            try:
                times.append(float(line.rstrip()[:-3].rsplit(" ", 1)[-1]))
            except ValueError:
                pass
    return times


def check_transform_surface(editor, image_path):
    """D4.6: the shared surface pair in three UI sites, and the Transform tool's drags resting moved meshes."""
    editor.call("set_surface_snap_settings", {"snap": False, "orient": False})
    scene = create_scene(editor)
    fixtures = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [-2.0, 0.0, 0.0], "name": "slab", "parameters": {"width": 4.0, "depth": 4.0}},
        {"shape": "basic.wedge", "position": [3.0, 0.0, 0.0], "name": "wedge", "parameters": {"width": 2.0}},
        {"shape": "basic.block", "position": [-1.5, 1.5, 0.0], "name": "mover"},
        {"shape": "basic.block", "position": [2.5, 2.0, 0.0], "name": "mate"},
        {"shape": "basic.block", "position": [-3.0, 2.0, -1.5], "name": "parent"},
        {"shape": "basic.block", "position": [-3.0, 3.0, -1.5], "name": "child", "parent_index": 4},
    ]})["pieces"]
    editor.wait_idle()
    slab, wedge, mover, mate, parent, child = fixtures
    slab_top = slab["world_aabb"]["max"][1]
    slope_normal = normalize([0.0, 2.0, 1.0])
    camera = editor.call("get_scene_cameras", {"scene_name": scene})["cameras"][0]
    pitch = math.atan2(9.0, 12.0)
    editor.call("set_node_transform", {"scene_name": scene, "node_id": camera["id"], "translation": [0.0, 9.0, 12.0],
                                       "rotation_xyzw": [-math.sin(0.5 * pitch), 0.0, 0.0, math.cos(0.5 * pitch)]})
    isolate_viewport(editor.call, scene_name=scene)
    editor.wait_idle()
    viewport = next(v for v in editor.call("get_viewports")["viewports"] if v.get("scene") == scene)
    hover(editor, viewport, [0.0, slab_top, 0.0])
    kit_open = open_window(editor, WINDOW)
    transform_open = open_window(editor, TRANSFORM_WINDOW)
    check("the Level Kit and Transform windows are shown", kit_open and transform_open, "")

    # Every site shows the pair; toggling any one of them shows in all three and in get_surface_snap_settings.
    sites = surface_sites(editor)
    check("Snap To Surface and Orient To Surface show in the Level Kit window, the Transform window's Move rows and the viewport toolbar, all off",
          sites_agree(sites, "Snap To Surface", False) and sites_agree(sites, "Orient To Surface", False), json.dumps(sites))
    for label, site, key in (("Snap To Surface", "toolbar", "snap"), ("Snap To Surface", "move", "snap"), ("Snap To Surface", "kit", "snap"),
                             ("Orient To Surface", "toolbar", "orient"), ("Orient To Surface", "kit", "orient"), ("Orient To Surface", "move", "orient")):
        before = editor.call("get_surface_snap_settings", {})[key]
        click_site(editor, label, site)
        after = editor.call("get_surface_snap_settings", {})[key]
        check(f"a click on the {site} {label} toggles the shared setting and every site shows it",
              (after == (not before)) and sites_agree(surface_sites(editor), label, after), f"{before} -> {after}: {json.dumps(surface_sites(editor)[label])}")
    final = editor.call("get_surface_snap_settings", {})
    check("after three toggles of each setting the pair is on", final["snap"] and final["orient"], json.dumps(final))
    editor.call("set_surface_snap_settings", {"snap": False, "orient": False})

    # Both off: a gizmo X drag is the tool's own result. The Move tool (hotbar slot 1) shows the gizmo.
    select(editor, scene, [mover["id"]])
    hover(editor, viewport, [0.0, slab_top, 0.0])
    editor.call("key_press", {"key": binding_input(editor, "Hotbar.activate_slot_1", "key")})
    editor.wait_idle()
    check("with the Move tool chosen (hotbar slot 1) the gizmo shows its X arrow", gizmo_handle(editor, viewport, TRANSLATE_X) is not None, "")
    depth = undo_depth(editor)
    gizmo_drag(editor, viewport, TRANSLATE_X, [1.0, 0.0, 0.0])
    moved = piece_by_id(editor, scene, mover["id"])
    check("with both settings off a gizmo X drag moves the block exactly as before (no surface step)",
          close(moved["position"], [-0.5, 1.5, 0.0], 0.02) and (abs(moved["position"][1] - 1.5) < 1.0e-5) and (undo_depth(editor) == depth + 1),
          f"{fmt3(moved['position'])}, undo {undo_depth(editor) - depth}")
    editor.call("undo")
    editor.wait_idle()

    # Snap on (toolbar button): the X drag rests the raised block on the slab, one undo entry.
    click_site(editor, "Snap To Surface", "toolbar")
    depth = undo_depth(editor)
    gizmo_drag(editor, viewport, TRANSLATE_X, [1.0, 0.0, 0.0])
    moved = piece_by_id(editor, scene, mover["id"])
    check("with Snap To Surface a gizmo X drag of a raised block rests its base on the slab, one undo entry",
          close(moved["position"], [-0.5, slab_top, 0.0], 0.02) and (abs(moved["world_aabb"]["min"][1] - slab_top) < TOLERANCE) and (undo_depth(editor) == depth + 1),
          f"{fmt3(moved['position'])}, undo {undo_depth(editor) - depth}")
    handles = editor.call("get_transform_handles", {"viewport": viewport["title"]})
    check("the gizmo anchor follows the rested block", close(handles["anchor"]["world"], moved["position"], 0.02),
          f"anchor {fmt3(handles['anchor']['world'])}, block {fmt3(moved['position'])}")

    # Held across the slab's edge: mid-drag the block hangs over the edge resting on the slab.
    depth = undo_depth(editor)
    gizmo_drag(editor, viewport, TRANSLATE_X, [0.8, 0.0, 0.0], hold=True)
    mid = piece_by_id(editor, scene, mover["id"])
    check("mid-drag over the slab's edge the block rests on the slab (corner rays), no entry yet",
          close(mid["position"], [0.3, slab_top, 0.0], 0.02) and (undo_depth(editor) == depth), f"{fmt3(mid['position'])}, undo {undo_depth(editor) - depth}")
    editor.call("mouse_release", {})
    editor.wait_idle()
    check("the release records one undo entry", undo_depth(editor) == depth + 1, f"undo {undo_depth(editor) - depth}")

    # Over the wedge with snap only: the base rests on the highest corner hit.
    rest = piece_by_id(editor, scene, mover["id"])
    gizmo_drag(editor, viewport, TRANSLATE_X, [2.7, 0.0, 0.0])
    on_wedge = piece_by_id(editor, scene, mover["id"])
    check("a drag onto the wedge with snap rests the block on the wedge's highest point under it, upright",
          close(on_wedge["position"], [3.0, 0.75, 0.0], 0.02) and same_rotation(on_wedge["rotation_xyzw"], [0.0, 0.0, 0.0, 1.0]),
          f"{fmt3(on_wedge['position'])} {on_wedge['rotation_xyzw']}")
    editor.call("undo")
    editor.wait_idle()

    # Orient on (Transform window row): the drag onto the wedge tilts the block; undo gives position and rotation back.
    click_site(editor, "Orient To Surface", "move")
    depth = undo_depth(editor)
    gizmo_drag(editor, viewport, TRANSLATE_X, [2.7, 0.0, 0.0])
    tilted = piece_by_id(editor, scene, mover["id"])
    up = q_rotate(tilted["rotation_xyzw"], [0.0, 1.0, 0.0])
    check("with Orient To Surface the drag onto the wedge gives the block the slope normal as up axis, heading kept, base center on the slope",
          close(up, slope_normal, 2.0e-3) and tilt_only([0.0, 0.0, 0.0, 1.0], tilted["rotation_xyzw"], slope_normal) and
          close(tilted["position"], [3.0, 0.5, 0.0], 0.02) and (undo_depth(editor) == depth + 1),
          f"{fmt3(tilted['position'])} up {fmt3(up)}, undo {undo_depth(editor) - depth}")
    editor.call("undo")
    editor.wait_idle()
    back = piece_by_id(editor, scene, mover["id"])
    check("undo after the orient drag restores the position and the rotation",
          close(back["position"], rest["position"], 1.0e-4) and same_rotation(back["rotation_xyzw"], rest["rotation_xyzw"]),
          f"{fmt3(back['position'])} {back['rotation_xyzw']}")

    # The Y handle alone lifts the block off the surface.
    gizmo_drag(editor, viewport, TRANSLATE_Y, [0.0, 1.0, 0.0])
    lifted = piece_by_id(editor, scene, mover["id"])
    check("a Y-axis drag with both settings on lifts the block off the slab, upright",
          close(lifted["position"], [rest["position"][0], slab_top + 1.0, 0.0], 0.02) and same_rotation(lifted["rotation_xyzw"], [0.0, 0.0, 0.0, 1.0]),
          fmt3(lifted["position"]))

    # A plane handle snaps: the XZ drag brings the lifted block back down onto the slab.
    click_site(editor, "Orient To Surface", "toolbar")
    gizmo_drag(editor, viewport, TRANSLATE_XZ, [-1.0, 0.0, 0.5])
    planar = piece_by_id(editor, scene, mover["id"])
    check("an XZ plane drag with snap rests the block on the slab",
          close(planar["position"], [rest["position"][0] - 1.0, slab_top, 0.5], 0.02), fmt3(planar["position"]))

    # Two pieces: each rests on its own surface and neither stands on the other.
    place_at(editor, scene, mover["id"], [-1.5, 1.0, 1.0])
    place_at(editor, scene, mate["id"], [2.5, 2.0, 0.0])
    select(editor, scene, [mover["id"], mate["id"]])
    depth = undo_depth(editor)
    gizmo_drag(editor, viewport, TRANSLATE_X, [0.5, 0.0, 0.0])
    a = piece_by_id(editor, scene, mover["id"])
    b = piece_by_id(editor, scene, mate["id"])
    check("a two-piece drag rests one block on the slab and the other on the wedge, one undo entry",
          close(a["position"], [-1.0, slab_top, 1.0], 0.02) and close(b["position"], [3.0, 0.75, 0.0], 0.02) and (undo_depth(editor) == depth + 1),
          f"{fmt3(a['position'])}, {fmt3(b['position'])}, undo {undo_depth(editor) - depth}")

    # A parent with its child, both selected: the parent rests, the child moves rigidly with it.
    select(editor, scene, [parent["id"], child["id"]])
    p0 = piece_by_id(editor, scene, parent["id"])
    c0 = piece_by_id(editor, scene, child["id"])
    gizmo_drag(editor, viewport, TRANSLATE_X, [0.5, 0.0, 0.0])
    p1 = piece_by_id(editor, scene, parent["id"])
    c1 = piece_by_id(editor, scene, child["id"])
    offset0 = [c0["position"][i] - p0["position"][i] for i in range(3)]
    offset1 = [c1["position"][i] - p1["position"][i] for i in range(3)]
    check("a parent dragged with its child rests on the slab and the child keeps its offset (not snapped onto the parent)",
          close(p1["position"], [-2.5, slab_top, -1.5], 0.02) and close(offset0, offset1, 1.0e-4), f"parent {fmt3(p1['position'])}, offset {fmt3(offset0)} -> {fmt3(offset1)}")

    # transform_selection takes its own arguments: the UI setting is on, yet without them nothing rests.
    select(editor, scene, [mover["id"]])
    reply = editor.call("transform_selection", {"translation": [-1.5, 1.5, -1.0]})
    editor.wait_idle()
    plain = piece_by_id(editor, scene, mover["id"])
    check("transform_selection without surface arguments does not snap although the UI setting is on, and echoes them off",
          close(plain["position"], [-1.5, 1.5, -1.0], 1.0e-4) and (reply.get("surface_snap") is False) and (reply.get("surface_orient") is False),
          f"{fmt3(plain['position'])} {json.dumps({k: reply.get(k) for k in ('surface_snap', 'surface_orient', 'surface_found_count')})}")
    depth = undo_depth(editor)
    reply = editor.call("transform_selection", {"translation": [-1.5, 1.5, -1.0], "surface_snap": True})
    editor.wait_idle()
    snapped = piece_by_id(editor, scene, mover["id"])
    check("transform_selection with surface_snap rests the moved block on the slab, one undo entry",
          close(snapped["position"], [-1.5, slab_top, -1.0], 1.0e-4) and reply.get("surface_snap") and (reply.get("surface_found_count") == 1) and
          reply["nodes"][0].get("surface_found") and (undo_depth(editor) == depth + 1),
          f"{fmt3(snapped['position'])} undo {undo_depth(editor) - depth}")
    error = call_error(editor, "drag_selection", {"rotation_axis": [0, 1, 0], "rotation_angle_deg": 10, "surface_snap": True})
    check("drag_selection refuses surface options on a rotate drag", (error is not None) and ("translate" in error), str(error))

    # Per-step cost: 50 blocks over a large slab, one drag_selection with surface_snap.
    big = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [0.0, 0.0, 40.0], "name": "big slab", "parameters": {"width": 16.0, "depth": 16.0}}
    ] + [{"shape": "basic.block", "position": [-6.0 + 1.5 * (i % 10), 2.0, 34.0 + 2.0 * (i // 10)], "name": f"timed {i}"} for i in range(50)]})["pieces"]
    editor.wait_idle()
    editor.call("set_log_levels", {"loggers": {"editor.transform_tool": "trace"}})
    select(editor, scene, [p["id"] for p in big[1:]])
    reply = editor.call("drag_selection", {"translation": [0.5, 0.0, 0.0], "frames": 20, "surface_snap": True})
    editor.wait_idle()
    editor.call("set_log_levels", {"loggers": {"editor.transform_tool": "info"}})
    rested = sum(1 for n in reply.get("nodes", []) if n.get("surface_found"))
    times = surface_step_times(pav.read_log(), 50)
    median = sorted(times)[len(times) // 2] if times else -1.0
    check("drag_selection with surface_snap rests all 50 blocks and echoes the options",
          (rested == 50) and reply.get("surface_snap") and (reply.get("surface_orient") is False), f"{rested} rested")
    check("the surface step of a 50-piece selection is timed", len(times) >= 20, f"{len(times)} steps")
    print(f"surface step, 50 pieces: median {median:.3f} ms, max {max(times) if times else -1.0:.3f} ms over {len(times)} steps", flush=True)

    # Screenshot of the toolbar buttons and the Move rows with both settings on.
    select(editor, scene, [mover["id"]])
    os.makedirs(os.path.dirname(image_path), exist_ok=True)
    editor.call("capture_screenshot", {"path": image_path})
    check("capture_screenshot of the toolbar buttons and the Move rows is written", os.path.isfile(image_path), image_path)
    print(f"surface settings screenshot: {image_path}", flush=True)
    editor.call("set_surface_snap_settings", {"snap": False, "orient": False})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=default_editor(), help="editor binary (relative to the repository root or absolute)")
    parser.add_argument("--image", default=DEFAULT_IMAGE, help="catalog row PNG (relative to the repository root or absolute)")
    parser.add_argument("--surface-image", default=DEFAULT_SURFACE_IMAGE, help="surface placement PNG (relative to the repository root or absolute)")
    parser.add_argument("--walkable-image", default=DEFAULT_WALKABLE_IMAGE, help="walkable level PNG (relative to the repository root or absolute)")
    parser.add_argument("--window-image", default=DEFAULT_WINDOW_IMAGE, help="Level Kit window screenshot PNG (relative to the repository root or absolute)")
    parser.add_argument("--properties-image", default=DEFAULT_PROPERTIES_IMAGE, help="Properties window screenshot PNG (relative to the repository root or absolute)")
    parser.add_argument("--rock-image", default=DEFAULT_ROCK_IMAGE, help="rock forms PNG (relative to the repository root or absolute)")
    parser.add_argument("--transform-image", default=DEFAULT_TRANSFORM_IMAGE, help="surface settings screenshot PNG (relative to the repository root or absolute)")
    parser.add_argument("--only-walkable", action="store_true", help="run the walkable level, persistence and window checks alone")
    parser.add_argument("--only-transform", action="store_true", help="run the surface settings and Transform tool checks alone")
    args = parser.parse_args()
    image_path = args.image if os.path.isabs(args.image) else os.path.join(pav.REPO_ROOT, args.image)
    surface_image_path = args.surface_image if os.path.isabs(args.surface_image) else os.path.join(pav.REPO_ROOT, args.surface_image)
    walkable_image_path = args.walkable_image if os.path.isabs(args.walkable_image) else os.path.join(pav.REPO_ROOT, args.walkable_image)
    window_image_path = args.window_image if os.path.isabs(args.window_image) else os.path.join(pav.REPO_ROOT, args.window_image)
    properties_image_path = args.properties_image if os.path.isabs(args.properties_image) else os.path.join(pav.REPO_ROOT, args.properties_image)
    rock_image_path = args.rock_image if os.path.isabs(args.rock_image) else os.path.join(pav.REPO_ROOT, args.rock_image)
    transform_image_path = args.transform_image if os.path.isabs(args.transform_image) else os.path.join(pav.REPO_ROOT, args.transform_image)
    work = tempfile.mkdtemp(prefix="level_tools_verify_")
    # The window's checkboxes and its visibility rewrite these; the run gives them back as they were.
    restored = {}
    for name in RESTORED_CONFIG_FILES:
        path = os.path.join(pav.REPO_ROOT, name)
        if os.path.isfile(path):
            with open(path, "rb") as f:
                restored[path] = f.read()

    start = time.monotonic()
    try:
        editor = Editor(args.editor)
        scene = None
        if args.only_transform:
            check_transform_surface(editor, transform_image_path)
        else:
            if not args.only_walkable:
                scene = create_scene(editor)
                print(f"scene {scene}", flush=True)
                catalog = check_catalog(editor, scene)
                check_derive_catalog(catalog)
                row, length = check_catalog_row(editor, scene, catalog)
                render_row(editor, scene, length, image_path)
                check_sharing_and_undo(editor, scene, row)
                check_rejections(editor, scene)
                check_snap(editor, scene, 1.0, [1.3, 0.2, -2.6], [1.0, 0.0, -3.0], "module 1")
                check_level_settings(editor, scene)
                check_parameter_changes(editor, scene)
                check_surface_placement(editor, scene, surface_image_path)
                check_rock_forms_and_derive(editor, work, rock_image_path)
            walkable_scene = check_walkable_level(editor, walkable_image_path)
            check_persistence(editor, scene, walkable_scene, work)
            window_scene, stairs = check_level_kit_window(editor, window_image_path)
            check_properties_rows(editor, window_scene, stairs, properties_image_path)
            check_from_seed_toggle(editor, window_scene)
            check_live_drags(editor, window_scene, stairs, editor.call("level_get_kits", {"scene_name": window_scene}))
            check_transform_surface(editor, transform_image_path)
    except Exception as error:  # noqa: BLE001 - a failed step is a failed check
        check("run completed without an error", False, f"{type(error).__name__}: {error}")
    finally:
        for launched in pav.LAUNCHED:
            launched.stop()
        shutil.rmtree(work, ignore_errors=True)
        for path, content in restored.items():
            with open(path, "rb") as f:
                current = f.read()
            if current != content:
                with open(path, "wb") as f:
                    f.write(content)
    failed = [label for label, ok in pav.RESULTS if not ok]
    print(f"\n{len(pav.RESULTS) - len(failed)}/{len(pav.RESULTS)} checks passed, {len(failed)} failed in {time.monotonic() - start:.0f} s")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
