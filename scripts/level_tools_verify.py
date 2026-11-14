#!/usr/bin/env python3
"""Verify level kit pieces over MCP (doc/editor/level_tools.md "Verification").

Launches a headless editor (ERHE_AI_DRIVER=1, stopped by its own pid), creates
an empty scene and checks over MCP:

- level_get_kits lists the 3 kits and the 14 shapes of the starter set;
- the 14 shapes placed once each at their defaults in one call (the "catalog
  row") are 14 pieces whose world_aabb matches the shape's default size, whose
  parameters echo the catalog defaults, and whose materials are the palette
  materials of the shape's slots; a piece has a static physics body, the stairs
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
  bounds, the node keeps id, name, transform and materials, the body stays
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
  (editor.scene trace) of the heaviest shape are printed;
- spatial queries (doc/editor/level_tools.md "Spatial queries"), in a scene of
  its own (closed afterwards): level_get_support_report over a ground slab, a
  block resting on it, one stacked on that, a floating block and one over
  nothing, with node_ids and a tolerance; level_snap_to_surface keeps the
  resting block under the stacked one; level_get_region_contents with boxes
  and spheres; level_get_height_map cell by cell and its text picture, rays
  from inside a solid, rays ending at min y, refused arguments and an
  oversized grid. In the walkable level after the rebuild a height map strip
  has the upper slab's and the floor's walkable cells on two islands. The
  overlap report (scene closed afterwards): known-depth pairs, touching and
  apart blocks, a sunk block, two triangle mesh stairs, tolerance, tags,
  node_ids and refused arguments. The level check (scene closed afterwards):
  exactly one finding of each kind once the navigation mesh is built, then
  none after the fixes and a rebuild. In the window scene the Level Kit
  window's Support Report, Select Unsupported, Region Contents (a row click
  selects), Height Map, Overlap Report with Exclude Tags and Level Check (a
  row click selects the pieces); a capture_screenshot of the window is written
  and its path printed.
- layout patterns (doc/editor/level_tools.md "Kit layout nodes"), in a scene of
  their own (closed afterwards): a contiguous line of walls from the default
  step, an open arc keeping the node's rotation, creation as one undo entry
  that undo removes and redo restores, surface snap per element on a ground
  with a raised slab, from_piece_id replacing a parented piece in one entry
  whose undo gives the piece back exactly, and refused calls. In the window
  scene the Level Kit window's Layout section: Make Layout from the selected
  piece (one entry, the piece replaced, undo restoring it), every row kind
  (count and gap modes, line, grid, ring and polyline rows, points added,
  moved and removed, items added from a piece, edited, re-materialed and
  removed, snap and seed rows) as one entry each, a refused edit, Unlock, an
  edit while unlocked, Lock and Refresh; a capture_screenshot of the section
  is written and its path printed.
- groups (doc/editor/level_tools.md "Groups"), in a scene of their own
  (closed afterwards): level_group_nodes at the base center of the bounds
  with world transforms kept, undo and redo; set_node_transform on the group
  moves its pieces; level_duplicate_group copies full pieces sharing meshes
  and collision shapes, a copy's parameter change leaves the original alone;
  level_mirror_group with known positions and rotations, the rock listed as
  not exact, twice the original; nesting; level_ungroup; delete_nodes and
  undo; a pasted stairs piece's triangle mesh shape; refusals; save and load
  keep the groups. In the window scene the Level Kit window's Group section:
  Group Selected, Duplicate, Mirror X and Ungroup; a capture_screenshot of
  the section is written and its path printed.
- material slots (doc/editor/level_tools.md "Materials"), in a scene of their
  own (closed afterwards): the catalog's slots, defaults and alternatives; a
  placed tree has two primitives with Kit Bark and Kit Foliage; per-slot
  materials at placement, one undo entry with the palette materials it adds;
  support, overlap and snap of trees on a ground slab; level_set_piece_material
  as one undo entry keeping the other slot, no entry when unchanged; a
  parameter change, a layout from the piece, a new layout with item materials, a
  duplicate, a mirror and a paste keep the assignments; refused arguments;
  save and reopen keep them. In the window scene the Level Kit window's slot
  material rows: the preview's and the placed tree's materials, the selected
  tree's bark row as one undo entry, a live segments drag keeping them; a
  capture_screenshot of the rows is written and its path printed.

- kit layout nodes (doc/editor/level_tools.md "Kit layout nodes"), in a scene
  of their own (closed afterwards): each pattern kind and the gap count mode
  at known positions and counts, two items, snap settings, per-element seeds,
  value changes regenerating as one undo entry, moves, the selection
  redirect, 15 edit paths refusing a locked child, unlock, edits while
  unlocked, lock with undo and redo, a duplicated and mirrored group holding
  a layout, refused calls, save and reopen.

The run restores config/editor/editor_settings.json and desktop_windows.json,
which the window's checkboxes and its visibility rewrite.

Usage: python3 scripts/level_tools_verify.py [--editor <binary>] [--image <png path>] [--surface-image <png path>]
       [--walkable-image <png path>] [--window-image <png path>] [--properties-image <png path>] [--queries-image <png path>]
       [--layout-image <png path>] [--group-image <png path>] [--only-walkable] [--only-transform] [--only-layouts] [--only-window-layout]
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
DEFAULT_QUERIES_IMAGE = os.path.join("logs", "level_tools_spatial_queries.png")
DEFAULT_LAYOUT_IMAGE = os.path.join("logs", "level_tools_layout.png")
DEFAULT_GROUP_IMAGE = os.path.join("logs", "level_tools_group.png")
DEFAULT_MATERIALS_IMAGE = os.path.join("logs", "level_tools_material_slots.png")
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

    material_problems = [f"{p['shape']}: {p['materials']}" for p in placed
                         if p["materials"] != {slot["slot"]: slot["default_material"] for slot in by_id[p["shape"]]["material_slots"]}]
    check("every catalog row piece has the default palette material of each of its shape's slots", not material_problems,
          "; ".join(material_problems) if material_problems else ", ".join(sorted({m for p in placed for m in p["materials"].values()})))
    materials = {m["name"] for m in editor.call("get_scene_materials", {"scene_name": scene}).get("materials", [])}
    palette = {slot["default_material"] for s in shapes for slot in s["material_slots"]}
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
          close(after["rotation_xyzw"], before["rotation_xyzw"]) and (after["materials"] == before["materials"]),
          f"{after['name']} {fmt3(after['position'])} {after['materials']}")
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
    check_walkable_height_map(editor, scene, seed_point, upper_center, floor_top, upper_top)
    return scene


def expected_height_text(rows, columns, cell, min_x, min_z, height_at):
    """The text picture of write_height_text for a known height function (None: no hit)."""
    heights = [[height_at(min_x + (c + 0.5) * cell, min_z + (r + 0.5) * cell) for c in range(columns)] for r in range(rows)]
    hits = [h for row in heights for h in row if h is not None]
    low, high = min(hits), max(hits)
    def digit(h):
        if h is None:
            return "."
        if high <= low:
            return "0"
        return str(int(math.floor(((h - low) / (high - low)) * 9.0 + 0.5)))
    return heights, ["".join(digit(h) for h in row) for row in heights]


def check_spatial_queries(editor):
    """D7.1, D7.2, D7.4: region contents, height map and support report on a known-answer scene."""
    scene = create_scene(editor)
    reply = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [0.0, 0.0, 0.0], "name": "ground", "parameters": {"width": 8.0, "depth": 8.0}},
        {"shape": "basic.block", "position": [0.0, 0.2, 0.0], "name": "resting block"},
        {"shape": "basic.block", "position": [0.0, 1.2, 0.0], "name": "stacked block"},
        {"shape": "basic.block", "position": [3.0, 2.0, 3.0], "name": "floating block"},
        {"shape": "basic.block", "position": [20.0, 0.0, 20.0], "name": "block over nothing"},
    ]})
    editor.wait_idle()
    ground, resting, stacked, floating, lost = reply["pieces"]
    ground_top = ground["world_aabb"]["max"][1]
    check("the spatial query scene: the ground's top at 0.2, a block resting on it and one stacked on that",
          (abs(ground_top - 0.2) < TOLERANCE) and (abs(resting["world_aabb"]["min"][1] - ground_top) < TOLERANCE) and
          (abs(stacked["world_aabb"]["min"][1] - resting["world_aabb"]["max"][1]) < TOLERANCE),
          f"ground top {ground_top:.4f}, resting {fmt3(resting['world_aabb']['min'])}, stacked {fmt3(stacked['world_aabb']['min'])}")

    # Support report (D7.4).
    report = editor.call("level_get_support_report", {"scene_name": scene})
    by_id = {n["id"]: n for n in report["nodes"]}
    expected_unsupported = sorted([ground["id"], floating["id"], lost["id"]])
    check("level_get_support_report over every kit piece: the ground, the floating block and the block over nothing rest on nothing",
          report["all_kit_pieces"] and (report["count"] == 5) and (sorted(report["unsupported_ids"]) == expected_unsupported) and
          (report["unsupported_count"] == 3) and (abs(report["tolerance"] - 0.01) < 1.0e-6),
          f"unsupported {report['unsupported_ids']}, expected {expected_unsupported}, count {report['count']}")
    check("the resting block's distance is 0, the stacked block rests on it (pieces support pieces)",
          by_id[resting["id"]]["supported"] and (abs(by_id[resting["id"]]["distance"]) < TOLERANCE) and
          by_id[stacked["id"]]["supported"] and (abs(by_id[stacked["id"]]["distance"]) < TOLERANCE),
          f"resting {json.dumps(by_id[resting['id']])}, stacked {json.dumps(by_id[stacked['id']])}")
    check("the floating block finds the ground 1.8 m under its base; the block over nothing finds no surface",
          by_id[floating["id"]]["surface_found"] and (abs(by_id[floating["id"]]["distance"] - (2.0 - ground_top)) < TOLERANCE) and
          (not by_id[lost["id"]]["surface_found"]) and (by_id[lost["id"]]["distance"] is None),
          f"floating {json.dumps(by_id[floating['id']])}, lost {json.dumps(by_id[lost['id']])}")
    snap = editor.call("level_snap_to_surface", {"scene_name": scene, "node_ids": [resting["id"]], "query_only": True})["nodes"][0]
    check("level_snap_to_surface on the resting block, which carries the stacked block, keeps it on the ground (the stacked block's bottom is no surface)",
          snap["surface_found"] and (abs(snap["distance"]) < TOLERANCE) and close(snap["position"], [0.0, ground_top, 0.0]),
          f"{fmt3(snap['position'])} distance {snap['distance']:.4f}")
    wide = editor.call("level_get_support_report", {"scene_name": scene, "node_ids": [floating["id"], resting["id"]], "tolerance": 2.0})
    check("level_get_support_report with node_ids and tolerance 2 m: only the listed nodes, the floating block counts as supported",
          (not wide["all_kit_pieces"]) and (wide["count"] == 2) and (wide["unsupported_ids"] == []) and ([n["id"] for n in wide["nodes"]] == [floating["id"], resting["id"]]),
          json.dumps(wide)[:300])
    for label, arguments in [
        ("a zero tolerance", {"scene_name": scene, "tolerance": 0.0}),
        ("a node that is not a mesh", {"scene_name": scene, "node_ids": [999999]}),
        ("an unknown argument", {"scene_name": scene, "direction": [0, -1, 0]}),
    ]:
        error = call_error(editor, "level_get_support_report", arguments)
        check(f"level_get_support_report refuses {label}", error is not None, str(error))

    # Region contents (D7.1).
    def region_ids(arguments):
        answer = editor.call("level_get_region_contents", dict({"scene_name": scene}, **arguments))
        return answer, [n["id"] for n in answer["nodes"]]
    answer, ids = region_ids({"box": {"min": [-0.6, 0.3, -0.6], "max": [0.6, 1.0, 0.6]}})
    check("a box inside the resting block's height holds only the resting block (not the ground below, not the stacked block above)",
          ids == [resting["id"]], f"{ids}")
    entry = answer["nodes"][0] if answer["nodes"] else {}
    check("a region entry carries kit_piece, shape, transform and world bounds",
          entry.get("kit_piece") and (entry.get("shape") == "basic.block") and close(entry.get("position", [9, 9, 9]), [0.0, ground_top, 0.0]) and
          close(entry.get("rotation_xyzw", [9, 9, 9, 9]), [0.0, 0.0, 0.0, 1.0]) and close(entry.get("world_aabb", {}).get("max", [9, 9, 9]), resting["world_aabb"]["max"]) and
          ("box" in answer),
          json.dumps(entry)[:300])
    _, ids = region_ids({"box": {"min": [-0.5, 0.0, -0.5], "max": [0.5, 1.5, 0.5]}})
    check("a box through the ground and both blocks' height holds the ground, the resting and the stacked block",
          ids == sorted([ground["id"], resting["id"], stacked["id"]]), f"{ids}")
    _, ids = region_ids({"sphere": {"center": [3.0, 3.5, 3.0], "radius": 0.6}})
    check("a sphere touching the floating block's top holds only it", ids == [floating["id"]], f"{ids}")
    _, ids = region_ids({"sphere": {"center": [20.0, 1.7, 20.0], "radius": 0.6}})
    check("a sphere 0.7 m above the far block holds nothing", ids == [], f"{ids}")
    for label, arguments in [
        ("both a box and a sphere", {"scene_name": scene, "box": {"min": [0, 0, 0], "max": [1, 1, 1]}, "sphere": {"center": [0, 0, 0], "radius": 1}}),
        ("neither a box nor a sphere", {"scene_name": scene}),
        ("a box with min above max", {"scene_name": scene, "box": {"min": [1, 0, 0], "max": [0, 1, 1]}}),
        ("a sphere with a negative radius", {"scene_name": scene, "sphere": {"center": [0, 0, 0], "radius": -1}}),
    ]:
        error = call_error(editor, "level_get_region_contents", arguments)
        check(f"level_get_region_contents refuses {label}", error is not None, str(error))

    # Height map (D7.2): the ground spans x and z -4..4; the stacked block's top is at 2.2 over -0.5..0.5.
    cell = 0.5
    min_x, min_z, max_x, max_z = -2.0, -2.0, 5.0, 2.0
    def height_at(x, z):
        if (-0.5 < x < 0.5) and (-0.5 < z < 0.5):
            return stacked["world_aabb"]["max"][1]
        if (-4.0 < x < 4.0) and (-4.0 < z < 4.0):
            return ground_top
        return None
    columns, rows = int(round((max_x - min_x) / cell)), int(round((max_z - min_z) / cell))
    expected, expected_text = expected_height_text(rows, columns, cell, min_x, min_z, height_at)
    hm = editor.call("level_get_height_map", {"scene_name": scene, "min": [min_x, -1.0, min_z], "max": [max_x, 5.0, max_z], "cell_size": cell, "text": True})
    got = hm.get("heights", [])
    mismatches = [(r, c, got[r][c], expected[r][c]) for r in range(rows) for c in range(columns)
                  if (r < len(got)) and (c < len(got[r])) and
                  ((got[r][c] is None) != (expected[r][c] is None) or ((got[r][c] is not None) and abs(got[r][c] - expected[r][c]) > TOLERANCE))]
    check(f"level_get_height_map: {columns} x {rows} cells of {cell} m, the stacked block's top over the ground, no hit past the ground's edge",
          (hm.get("columns") == columns) and (hm.get("rows") == rows) and (len(got) == rows) and (not mismatches) and
          (hm.get("hit_count") == sum(1 for row in expected for h in row if h is not None)),
          f"{hm.get('columns')} x {hm.get('rows')}, mismatches {mismatches[:4]}")
    check("the height map's text picture maps the ground to 0, the block's top to 9 and marks cells without a hit with '.'",
          hm.get("height_text") == expected_text, f"\n" + "\n".join(hm.get("height_text", [])) + "\nexpected\n" + "\n".join(expected_text))
    print("height map text picture:\n" + "\n".join(hm.get("height_text", [])), flush=True)
    check("a height map without a navigation mesh reports navigation_available false and no walkable rows",
          (hm.get("navigation_available") is False) and ("walkable" not in hm) and ("island_text" not in hm), "")
    inside = editor.call("level_get_height_map", {"scene_name": scene, "min": [2.75, -1.0, 2.75], "max": [3.25, 2.5, 3.25], "cell_size": 0.25})
    check("rays start at max y: from inside the floating block they pass its bottom face (seen from behind) and find the ground",
          (inside["columns"] == 2) and (inside["rows"] == 2) and
          all((h is not None) and (abs(h - ground_top) < TOLERANCE) for row in inside["heights"] for h in row) and ("height_text" not in inside),
          json.dumps(inside["heights"]))
    below = editor.call("level_get_height_map", {"scene_name": scene, "min": [2.75, 0.5, 2.75], "max": [3.25, 1.5, 3.25], "cell_size": 0.25})
    check("rays end at min y: a region between the ground and the floating block has no hit", below["hit_count"] == 0, json.dumps(below["heights"]))
    error = call_error(editor, "level_get_height_map", {"scene_name": scene, "min": [0, 0, 0], "max": [100, 1, 100], "cell_size": 0.1})
    check("level_get_height_map refuses an oversized grid naming the cell count and the limit",
          (error is not None) and ("1000 x 1000" in error) and ("65536" in error), str(error))
    for label, arguments in [
        ("a missing cell_size", {"scene_name": scene, "min": [0, 0, 0], "max": [1, 1, 1]}),
        ("a zero cell_size", {"scene_name": scene, "min": [0, 0, 0], "max": [1, 1, 1], "cell_size": 0}),
        ("min above max in x", {"scene_name": scene, "min": [2, 0, 0], "max": [1, 1, 1], "cell_size": 0.5}),
    ]:
        error = call_error(editor, "level_get_height_map", arguments)
        check(f"level_get_height_map refuses {label}", error is not None, str(error))
    # Left open, it moves the Transform checks' scene to a wide viewport where X drags land short (level_tools_implementation.md step 20).
    editor.close(scene)


def check_overlap_report(editor):
    """D7.5: known-answer pairs, tolerance, tag exclusion, node_ids, two triangle meshes and refused arguments."""
    scene = create_scene(editor)
    reply = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [0.0, 0.0, 0.0], "name": "ground", "parameters": {"width": 8.0, "depth": 8.0}},
        {"shape": "basic.block", "position": [0.0, 0.2, 0.0], "name": "block a"},
        {"shape": "basic.block", "position": [0.75, 0.2, 0.0], "name": "block b"},     # 0.25 m into block a
        {"shape": "basic.block", "position": [1.75, 0.2, 0.0], "name": "block c"},     # touches block b
        {"shape": "basic.block", "position": [-3.0, 0.2, -3.0], "name": "block d"},    # apart
        {"shape": "basic.block", "position": [-3.0, 0.0, 3.0], "name": "sunk block"},  # 0.2 m into the ground
        {"shape": "basic.stairs", "position": [10.0, 0.2, 0.0], "name": "stairs a"},
        {"shape": "basic.stairs", "position": [10.5, 0.2, 0.0], "name": "stairs b"},   # two triangle meshes 0.5 m into each other
    ]})
    editor.wait_idle()
    ground, block_a, block_b, block_c, block_d, sunk, stairs_a, stairs_b = reply["pieces"]
    editor.call("add_tags", {"scene_name": scene, "ids": [ground["id"]], "tags": ["ground"]})
    editor.call("add_tags", {"scene_name": scene, "ids": [block_a["id"]], "tags": ["skip"]})
    editor.wait_idle()

    def pairs_of(report):
        return {tuple(o["ids"]): o for o in report["overlaps"]}

    report = editor.call("level_get_overlap_report", {"scene_name": scene})
    pairs = pairs_of(report)
    ab = (min(block_a["id"], block_b["id"]), max(block_a["id"], block_b["id"]))
    gs = (min(ground["id"], sunk["id"]), max(ground["id"], sunk["id"]))
    st = (min(stairs_a["id"], stairs_b["id"]), max(stairs_a["id"], stairs_b["id"]))
    check("level_get_overlap_report over every kit piece: blocks a and b, the sunk block and the ground, the two stairs; not the touching or the apart blocks",
          report["all_kit_pieces"] and (report["count"] == 8) and (report["tested_count"] == 8) and (set(pairs) == {ab, gs, st}) and
          (report["overlap_count"] == 3) and (abs(report["tolerance"] - 0.01) < 1.0e-6),
          f"pairs {sorted(pairs)}, expected {sorted([ab, gs, st])}, bounds pairs {report.get('bounds_pair_count')}")
    entry = pairs.get(ab, {})
    check("blocks a and b interpenetrate by 0.25 m (exact, the blocks' own shapes) around the center of their common bounds",
          (abs(entry.get("depth", 0.0) - 0.25) < 0.005) and (entry.get("measure") == "exact") and
          close(entry.get("position", [9, 9, 9]), [0.375, 0.7, 0.0], 0.01) and (entry.get("names") == [block_a["name"], block_b["name"]]),
          json.dumps(entry))
    entry = pairs.get(gs, {})
    check("the sunk block is 0.2 m into the ground", (abs(entry.get("depth", 0.0) - 0.2) < 0.005) and (entry.get("measure") == "exact"), json.dumps(entry))
    entry = pairs.get(st, {})
    check("two triangle mesh stairs are measured against each other's convex hull (the backend collides no mesh pair), at most their 0.5 m bounds overlap",
          (entry.get("measure") == "convex_hull") and (0.01 < entry.get("depth", 0.0) <= 0.5 + 1.0e-3), json.dumps(entry))
    check("the bounds pass keeps only pairs whose bounds overlap by more than the tolerance (the touching and resting pieces drop out there)",
          report["bounds_pair_count"] == 3, f"bounds pairs {report['bounds_pair_count']}")
    excluded = editor.call("level_get_overlap_report", {"scene_name": scene, "exclude_tags": ["ground", "skip"]})
    check("exclude_tags ['ground', 'skip'] leaves out the ground and block a: only the stairs pair remains",
          (set(pairs_of(excluded)) == {st}) and (sorted(excluded["excluded_ids"]) == sorted([ground["id"], block_a["id"]])) and
          (excluded["tested_count"] == 6) and (excluded["exclude_tags"] == ["ground", "skip"]),
          f"pairs {sorted(pairs_of(excluded))}, excluded {excluded['excluded_ids']}")
    wide = editor.call("level_get_overlap_report", {"scene_name": scene, "tolerance": 0.3, "node_ids": [block_a["id"], block_b["id"], block_c["id"], block_d["id"]]})
    check("with tolerance 0.3 m over the four blocks nothing overlaps (0.25 m is within it)",
          (not wide["all_kit_pieces"]) and (wide["count"] == 4) and (wide["overlap_count"] == 0) and (wide["bounds_pair_count"] == 0), json.dumps(wide)[:300])
    listed = editor.call("level_get_overlap_report", {"scene_name": scene, "node_ids": [block_b["id"], block_c["id"], block_d["id"]]})
    check("over blocks b, c and d alone (touching and apart) nothing overlaps", listed["overlap_count"] == 0, json.dumps(listed)[:300])
    for label, arguments in [
        ("a zero tolerance", {"scene_name": scene, "tolerance": 0.0}),
        ("exclude_tags that is not an array", {"scene_name": scene, "exclude_tags": "ground"}),
        ("an empty tag", {"scene_name": scene, "exclude_tags": [""]}),
        ("a node that is not a mesh", {"scene_name": scene, "node_ids": [999999]}),
        ("a node listed twice", {"scene_name": scene, "node_ids": [block_a["id"], block_a["id"]]}),
        ("an unknown argument", {"scene_name": scene, "depth": 1.0}),
    ]:
        error = call_error(editor, "level_get_overlap_report", arguments)
        check(f"level_get_overlap_report refuses {label}", error is not None, str(error))
    editor.close(scene)


def check_level_check(editor):
    """D7.6: a scene with one finding of each kind, then the same scene fixed with none."""
    scene = create_scene(editor)
    reply = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [0.0, 0.0, 0.0], "name": "ground", "parameters": {"width": 8.0, "depth": 8.0}},
        {"shape": "basic.block", "position": [3.0, 4.0, 3.0], "name": "floating block"},
        {"shape": "basic.block", "position": [2.0, 0.2, -2.0], "name": "overlap a"},
        {"shape": "basic.block", "position": [2.5, 0.2, -2.0], "name": "overlap b"},
        {"shape": "architecture.pillar", "position": [-2.5, 0.2, -2.5], "name": "pillar"},
        {"shape": "basic.floor_slab", "position": [-2.5, 3.2, -2.5], "name": "platform", "parameters": {"width": 3.0, "depth": 3.0}},
        {"shape": "architecture.door_wall", "position": [0.0, 0.2, 3.0], "name": "low door", "parameters": {"derive": [], "opening_height": 1.9}},
    ]})
    editor.wait_idle()
    ground, floating, overlap_a, overlap_b, pillar, platform, door = reply["pieces"]
    editor.call("add_tags", {"scene_name": scene, "ids": [ground["id"]], "tags": ["ground"]})
    editor.wait_idle()
    check("the level check scene: the platform rests on the pillar at 3.4 m",
          abs(platform["world_aabb"]["max"][1] - 3.4) < TOLERANCE and abs(pillar["world_aabb"]["max"][1] - 3.2) < TOLERANCE,
          f"platform {fmt3(platform['world_aabb']['max'])}, pillar {fmt3(pillar['world_aabb']['max'])}")

    def by_kind(result):
        kinds = {}
        for finding in result["findings"]:
            kinds.setdefault(finding["kind"], []).append(finding)
        return kinds

    plain = editor.call("level_check", {"scene_name": scene})
    check("level_check without a navigation mesh says so and checks no island; the ground, which rests on nothing, is unsupported too",
          (plain["navigation_available"] is False) and (plain["counts"]["unreachable_island"] == 0) and (plain["counts"]["unsupported"] == 2) and
          (plain["piece_count"] == 7) and (plain["excluded_count"] == 0),
          json.dumps({k: plain.get(k) for k in ("navigation_available", "counts", "piece_count")}))

    seed_point = [0.0, 0.25, 0.0]
    node = editor.call("create_node", {"scene_name": scene, "name": "seed", "position": seed_point})
    editor.wait_idle()
    editor.call("navigation_set_seed", {"scene_name": scene, "node_id": node.get("node_id", node.get("id"))})
    editor.set_export_settings(scene, {})
    editor.wait_idle()
    job = build_navigation(editor, scene)
    check("the level check scene's navigation mesh builds", job.get("state") == "done", f"{job.get('state')} {job.get('error', '')}")

    result = editor.call("level_check", {"scene_name": scene, "exclude_tags": ["ground"]})
    kinds = by_kind(result)
    counts = result["counts"]
    check("level_check with exclude_tags ['ground'] finds exactly one finding of each kind (unsupported, overlap, unreachable island, unwalkable parameters)",
          (counts == {"unsupported": 1, "overlap": 1, "unreachable_island": 1, "unwalkable_parameters": 1, "unreadable_piece": 0}) and
          (result["finding_count"] == 4) and result["navigation_available"] and (not result["navigation_stale"]) and (result["excluded_count"] == 1),
          json.dumps({k: result.get(k) for k in ("counts", "navigation_available", "navigation_stale", "excluded_count", "island_count")}))
    print("level check findings:\n" + "\n".join(f"  {f['kind']}: {f['detail']}" for f in result["findings"]), flush=True)
    unsupported = (kinds.get("unsupported") or [{}])[0]
    check("the unsupported finding is the floating block at its base center, 3.8 m over the ground",
          (unsupported.get("ids") == [floating["id"]]) and (abs((unsupported.get("distance") or 0.0) - 3.8) < 0.01) and
          close(unsupported.get("position", [9, 9, 9]), [3.0, 4.0, 3.0], 0.01),
          json.dumps(unsupported))
    overlap = (kinds.get("overlap") or [{}])[0]
    check("the overlap finding names both blocks with a depth of 0.5 m",
          (overlap.get("ids") == sorted([overlap_a["id"], overlap_b["id"]])) and (abs(overlap.get("depth", 0.0) - 0.5) < 0.005) and
          (overlap.get("measure") == "exact"),
          json.dumps(overlap))
    island = (kinds.get("unreachable_island") or [{}])[0]
    point = island.get("position", [9, 9, 9])
    check("the unreachable island lies on the platform (the piece under its point), larger than 1 m2",
          (island.get("ids") == [platform["id"]]) and (island.get("area", 0.0) > 1.0) and (abs(point[1] - 3.4) < 0.3) and
          (-4.0 <= point[0] <= -1.0) and (-4.0 <= point[2] <= -1.0),
          json.dumps(island))
    walk = (kinds.get("unwalkable_parameters") or [{}])[0]
    check("the R2.10 finding is the door wall whose opening the agent does not fit (agent of the navigation settings)",
          (walk.get("ids") == [door["id"]]) and walk.get("detail") and close(walk.get("position", [9, 9, 9]), [0.0, 0.2, 3.0]) and
          (abs(result["agent"]["height"] - 1.8) < 1.0e-6),
          json.dumps(walk))
    small = editor.call("level_check", {"scene_name": scene, "exclude_tags": ["ground"], "min_island_area": 1000.0})
    check("min_island_area 1000 m2 leaves the island out", (small["counts"]["unreachable_island"] == 0) and (small["finding_count"] == 3),
          json.dumps(small["counts"]))
    for label, arguments in [
        ("a negative min_island_area", {"scene_name": scene, "min_island_area": -1.0}),
        ("a zero support_tolerance", {"scene_name": scene, "support_tolerance": 0.0}),
        ("a zero overlap_tolerance", {"scene_name": scene, "overlap_tolerance": 0.0}),
        ("exclude_tags that is not an array", {"scene_name": scene, "exclude_tags": "ground"}),
        ("an unknown argument", {"scene_name": scene, "node_ids": [ground["id"]]}),
    ]:
        error = call_error(editor, "level_check", arguments)
        check(f"level_check refuses {label}", error is not None, str(error))

    # Fix each finding; the moves make the navigation mesh stale until the rebuild.
    place_at(editor, scene, floating["id"], [3.0, 0.2, 3.0])
    place_at(editor, scene, overlap_b["id"], [3.5, 0.2, -2.0])
    editor.call("level_set_piece_parameters", {"scene_name": scene, "node_id": door["id"], "parameters": {"opening_height": 2.2}})
    platform_seed = editor.call("create_node", {"scene_name": scene, "name": "platform seed", "position": [-2.5, 3.45, -2.5]})
    editor.wait_idle()
    editor.call("navigation_set_seed", {"scene_name": scene, "node_id": platform_seed.get("node_id", platform_seed.get("id"))})
    editor.wait_idle()
    stale = editor.call("level_check", {"scene_name": scene, "exclude_tags": ["ground"]})
    check("after the fixes and before the rebuild level_check reports the navigation mesh stale",
          stale["navigation_available"] and stale["navigation_stale"] and (stale["counts"]["overlap"] == 0) and (stale["counts"]["unsupported"] == 0),
          json.dumps({k: stale.get(k) for k in ("navigation_stale", "counts")}))
    job = build_navigation(editor, scene)
    clean = editor.call("level_check", {"scene_name": scene, "exclude_tags": ["ground"]})
    check("the fixed scene, rebuilt with a seed on the platform, has no finding",
          (job.get("state") == "done") and (clean["finding_count"] == 0) and clean["navigation_available"] and (not clean["navigation_stale"]),
          json.dumps({k: clean.get(k) for k in ("finding_count", "counts", "navigation_stale")}) + " " +
          "; ".join(f"{f['kind']}: {f['detail']}" for f in clean["findings"]))
    # Left open, it would change the viewport layout of the later gizmo drag checks (level_tools_implementation.md step 20).
    editor.close(scene)


def yaw_xyzw(degrees):
    half = math.radians(degrees) * 0.5
    return [0.0, math.sin(half), 0.0, math.cos(half)]


def run_poses(run):
    return [(p["position"], p["rotation_xyzw"]) for p in run["pieces"]]


def poses_match(run, expected, tolerance=TOLERANCE):
    """expected: (position, rotation_xyzw) per placed piece, in order."""
    got = run_poses(run)
    return (len(got) == len(expected)) and all(close(g[0], e[0], tolerance) and same_rotation(g[1], e[1], 1.0e-4) for g, e in zip(got, expected))


def describe_run(run):
    return "; ".join(f"{fmt3(p['position'])} {fmt3(p['rotation_xyzw'])}" for p in run.get("pieces", []))


def check_layout_patterns(editor):
    """D3.9: kit layout node patterns beyond check_layouts, creation undo and redo, surface snap per element, from_piece_id, refusals (scene closed afterwards)."""
    scene = create_scene(editor)
    identity = [0.0, 0.0, 0.0, 1.0]

    def create(arguments):
        return editor.call("level_create_layout", dict({"scene_name": scene}, **arguments))

    def layout_of(layout_id):
        return layouts_by_id(editor, scene)[layout_id]

    block = [{"shape": "basic.block"}]
    walls = create({"position": [0.0, 0.0, -20.0], "name": "wall run", "items": [{"shape": "architecture.wall"}], "pattern": {"kind": "line", "count": 3}})
    boxes = [p["world_aabb"] for p in pieces(editor, scene) if p["id"] in {c["id"] for c in walls["children"]}]
    boxes.sort(key=lambda b: b["min"][0])
    check("a line without a step steps by the wall's width (4 m): the walls touch end to end, named after the shape and element number",
          (abs(walls["pattern"]["step"] - 4.0) < TOLERANCE) and (len(boxes) == 3) and
          all(abs(boxes[i]["max"][0] - boxes[i + 1]["min"][0]) < TOLERANCE for i in range(2)) and
          ([c["name"] for c in walls["children"]] == ["Wall 1", "Wall 2", "Wall 3"]),
          f"step {walls['pattern']['step']}, boxes {[(b['min'][0], b['max'][0]) for b in boxes]}, names {[c['name'] for c in walls['children']]}")
    shared = {p["brush_id"] for p in pieces(editor, scene) if p["id"] in {c["id"] for c in walls["children"]}}
    check("the walls of the run share one mesh", len(shared) == 1, f"{shared}")
    arc = create({"position": [50.0, 0.0, 10.0], "items": block, "pattern": {"kind": "ring", "count": 3, "radius": 1.0, "arc_degrees": 180.0, "facing": "start"}})
    check("an arc of 180 degrees with 3 pieces has pieces at both ends; facing start keeps the node's rotation",
          positions_match(child_positions(arc), [[50.0, 0.0, 10.0], [51.0, 0.0, 9.0], [50.0, 0.0, 8.0]]) and
          all(same_rotation(c["rotation_xyzw"], identity) for c in arc["children"]), f"{[fmt3(p) for p in child_positions(arc)]}")

    # Creation is one undo entry: undo removes the node and every piece, redo gives them back at their transforms.
    depth = undo_depth(editor)
    before_ids = {p["id"] for p in pieces(editor, scene)}
    run = create({"position": [70.0, 0.0, 0.0], "name": "corner", "items": block, "pattern": {"kind": "polyline", "count": 4, "points": [[4.0, 0.0, 0.0], [4.0, 0.0, 2.0]]}})
    run_ids = {c["id"] for c in run["children"]}
    editor.wait_idle()
    check("level_create_layout is one undo entry", undo_depth(editor) == depth + 1, f"{undo_depth(editor)} vs {depth + 1}")
    editor.call("undo")
    editor.wait_idle()
    after_undo = {p["id"] for p in pieces(editor, scene)}
    check("one undo removes the layout node and its whole run",
          (after_undo == before_ids) and (run["id"] not in layouts_by_id(editor, scene)) and (run["id"] not in scene_tree(editor, scene)),
          f"{len(after_undo)} pieces")
    editor.call("redo")
    editor.wait_idle()
    redone = layouts_by_id(editor, scene).get(run["id"])
    check("redo gives the layout node and its run back at their transforms",
          (redone is not None) and ({c["id"] for c in redone["children"]} == run_ids) and positions_match(child_positions(redone), child_positions(run)),
          f"{redone and redone['child_count']}")

    # Surface snap per element on uneven ground: a ground slab (top 0.2) and a raised slab over x 0 - 4 (top 0.4).
    ground = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [0.0, 0.0, 5.0], "name": "layout ground", "parameters": {"width": 12.0, "depth": 4.0}},
        {"shape": "basic.floor_slab", "position": [2.0, 0.2, 5.0], "name": "layout step", "parameters": {"width": 4.0, "depth": 2.0}},
    ]})["pieces"]
    editor.wait_idle()
    low, high = ground[0]["world_aabb"]["max"][1], ground[1]["world_aabb"]["max"][1]
    rested = create({"position": [-3.0, 3.0, 5.0], "items": block, "surface_snap": True, "pattern": {"kind": "line", "count": 4, "step": 2.0}})
    check("with surface_snap each element of a line rests on the ground under it: two on the ground, two on the raised slab",
          positions_match(child_positions(rested), [[-3.0, low, 5.0], [-1.0, low, 5.0], [1.0, high, 5.0], [3.0, high, 5.0]]),
          f"low {low:.3f} high {high:.3f}: {[fmt3(p) for p in child_positions(rested)]}")

    # from_piece_id: the piece is the item, the node takes its place, the piece is replaced by element 0 in one entry.
    group = editor.call("level_group_nodes", {"scene_name": scene, "node_ids": [ground[0]["id"]], "name": "source parent"})
    editor.wait_idle()
    kits = editor.call("level_get_kits", {"scene_name": scene})
    block_slot = next(s for s in kits["shapes"] if s["id"] == "basic.block")["material_slots"][0]
    alternative = block_slot["alternatives"][0]
    source = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.block", "position": [0.0, 0.0, 30.0], "rotation_xyzw": yaw_xyzw(90.0), "name": "source", "parent_id": group["id"],
         "parameters": {"width": 2.0}, "materials": {block_slot["slot"]: alternative}}]})["pieces"][0]
    editor.wait_idle()
    siblings_before = [n["id"] for n in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"] if n.get("parent_id") == group["id"]]
    source_before = piece_by_id(editor, scene, source["id"])
    depth = undo_depth(editor)
    made = create({"from_piece_id": source["id"], "pattern": {"kind": "line", "count": 3}})
    editor.wait_idle()
    check("level_create_layout from_piece_id: 3 pieces along the piece's yawed x axis by its 2 m width, its values and material, under its parent",
          positions_match(child_positions(made), [[0.0, 0.0, 30.0 - 2.0 * i] for i in range(3)]) and
          all(same_rotation(c["rotation_xyzw"], yaw_xyzw(90.0)) for c in made["children"]) and
          all((c["parameters_text"] == source_before["parameters_text"]) and (c["materials"] == source_before["materials"]) for c in made["children"]) and
          (scene_tree(editor, scene)[made["id"]]["parent_id"] == group["id"]) and (made["name"] == "source Layout") and
          (made["replaced_piece_id"] == source["id"]),
          f"{[fmt3(p) for p in child_positions(made)]}, {made['name']}")
    check("the piece is gone, replaced by element 0 (not left beside the layout), as one undo entry",
          (source["id"] not in scene_tree(editor, scene)) and (undo_depth(editor) == depth + 1), f"depth {undo_depth(editor)} vs {depth + 1}")
    editor.call("undo")
    editor.wait_idle()
    restored = piece_by_id(editor, scene, source["id"]) if source["id"] in scene_tree(editor, scene) else None
    siblings_after = [n["id"] for n in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"] if n.get("parent_id") == group["id"]]
    check("undo gives the original piece back exactly (id, name, parent and place, transform, values, materials) and removes the layout",
          (restored is not None) and (siblings_after == siblings_before) and (made["id"] not in scene_tree(editor, scene)) and
          all(restored[k] == source_before[k] for k in ("name", "parameters_text", "materials", "brush_id")) and
          close(restored["position"], source_before["position"]) and same_rotation(restored["rotation_xyzw"], source_before["rotation_xyzw"]) and
          restored["physics"]["has_body"],
          f"{siblings_after} vs {siblings_before}")
    editor.call("redo")
    editor.wait_idle()
    check("redo makes the layout again in the piece's place", (made["id"] in layouts_by_id(editor, scene)) and (source["id"] not in scene_tree(editor, scene)), "")
    editor.call("undo")
    editor.wait_idle()

    count = len(pieces(editor, scene))
    depth = undo_depth(editor)
    for label, arguments, needle in [
        ("a grid of 40 x 40", {"items": block, "pattern": {"kind": "grid", "count_x": 40, "count_z": 40}}, "1600"),
        ("a zero step with more than one element", {"items": block, "pattern": {"kind": "line", "count": 2, "step": 0.0}}, "step"),
        ("a ring of radius 0", {"items": block, "pattern": {"kind": "ring", "count": 3, "radius": 0.0}}, "radius"),
        ("an arc of 400 degrees", {"items": block, "pattern": {"kind": "ring", "count": 3, "radius": 1.0, "arc_degrees": 400.0}}, "arc"),
        ("an unknown facing", {"items": block, "pattern": {"kind": "ring", "count": 3, "radius": 1.0, "facing": "inward"}}, "facing"),
        ("an empty path", {"items": block, "pattern": {"kind": "polyline", "points": []}}, "path"),
        ("a gap path of more than 1024 pieces", {"items": block, "pattern": {"kind": "polyline", "count_mode": "gap", "points": [[2000.0, 0.0, 0.0]]}}, "1024"),
        ("an unknown argument", {"items": block, "pattern": {"kind": "line"}, "count": 2}, "count"),
        ("a value out of range", {"items": [{"shape": "basic.block", "parameters": {"width": 100.0}}], "pattern": {"kind": "line"}}, "width"),
        ("from_piece_id with items", {"from_piece_id": source["id"], "items": block, "pattern": {"kind": "line"}}, "not both"),
        ("from_piece_id with a position", {"from_piece_id": source["id"], "position": [0.0, 0.0, 0.0], "pattern": {"kind": "line"}}, "not both"),
        ("a from_piece_id naming no node", {"from_piece_id": source["id"] + 100000, "pattern": {"kind": "line"}}, "no mesh node"),
        ("a from_piece_id naming a node that is no piece", {"from_piece_id": group["id"], "pattern": {"kind": "line"}}, "no mesh node"),
        ("a from_piece_id of a locked layout's child", {"from_piece_id": rested["children"][0]["id"], "pattern": {"kind": "line"}}, "locked kit layout"),
    ]:
        error = call_error(editor, "level_create_layout", dict({"scene_name": scene}, **arguments))
        check(f"level_create_layout refuses {label}", (error is not None) and (needle in error), str(error))
    editor.wait_idle()
    check("the refused calls placed nothing and recorded no undo entry",
          (len(pieces(editor, scene)) == count) and (undo_depth(editor) == depth) and (source["id"] in scene_tree(editor, scene)),
          f"{len(pieces(editor, scene))} vs {count}, depth {undo_depth(editor)} vs {depth}")
    # Left open, it moves the Transform checks' scene to a wide viewport (level_tools_implementation.md step 20).
    editor.close(scene)


def layouts_by_id(editor, scene):
    return {layout["id"]: layout for layout in editor.call("level_get_layouts", {"scene_name": scene})["layouts"]}


def child_positions(layout):
    return [c["position"] for c in layout["children"]]


def child_snapshot(layout):
    """Ids, names, world transforms, records and materials of a layout's children, in order."""
    return [(c["id"], c["name"], tuple(round(x, 4) for x in c["position"]), tuple(round(x, 4) for x in c["rotation_xyzw"]),
             c.get("parameters_text"), json.dumps(c.get("materials"), sort_keys=True)) for c in layout["children"]]


def positions_match(actual, expected, tolerance=TOLERANCE):
    return (len(actual) == len(expected)) and all(close(a, e, tolerance) for a, e in zip(actual, expected))


def check_layouts(editor, directory):
    """D3.9: kit layout nodes - patterns and count modes with known answers, items, snap, seeds, regeneration, lock, refusals, persistence (scenes closed afterwards)."""
    scene = create_scene(editor)
    cache_start = cached_brush_count(editor, scene)

    def create(arguments):
        return editor.call("level_create_layout", dict({"scene_name": scene}, **arguments))

    def layout_of(layout_id):
        return layouts_by_id(editor, scene)[layout_id]

    block = [{"shape": "basic.block"}]

    # Each pattern kind with known answers, in the node's frame (element 0 at its origin).
    depth = undo_depth(editor)
    line = create({"name": "line layout", "position": [10.0, 0.0, 10.0], "rotation_xyzw": yaw_xyzw(90.0), "items": block,
                   "pattern": {"kind": "line", "count": 4, "step": 2.0}})
    editor.wait_idle()
    check("level_create_layout makes a locked line of 4 blocks 2 m apart along the yawed node's x axis (world -z), one undo entry",
          line["locked"] and positions_match(child_positions(line), [[10.0, 0.0, 10.0 - 2.0 * i] for i in range(4)]) and
          all(same_rotation(c["rotation_xyzw"], yaw_xyzw(90.0)) for c in line["children"]) and
          ([c["name"] for c in line["children"]] == ["Block 1", "Block 2", "Block 3", "Block 4"]) and
          (line["run"]["count"] == 4) and (undo_depth(editor) == depth + 1),
          f"{[fmt3(p) for p in child_positions(line)]}, depth {undo_depth(editor)} vs {depth + 1}")
    tree = scene_tree(editor, scene)
    check("the layout node is an Xform holding the pieces as its children", (tree[line["id"]]["type"] == "Xform") and
          all(tree[c["id"]]["parent_id"] == line["id"] for c in line["children"]), "")
    grid = create({"position": [30.0, 0.0, 0.0], "items": block, "pattern": {"kind": "grid", "count_x": 3, "count_z": 2, "spacing_x": 2.0, "spacing_z": 1.5}})
    check("a 3 x 2 grid steps x fastest by 2 m, then z by 1.5 m",
          positions_match(child_positions(grid), [[30.0 + 2.0 * ix, 0.0, 1.5 * iz] for iz in range(2) for ix in range(3)]), f"{[fmt3(p) for p in child_positions(grid)]}")
    ring = create({"position": [50.0, 0.0, 0.0], "items": block, "pattern": {"kind": "ring", "count": 4, "radius": 2.0}})
    check("a full ring of 4 at radius 2 m about a center 2 m behind the node, each piece facing outward",
          positions_match(child_positions(ring), [[50.0, 0.0, 0.0], [52.0, 0.0, -2.0], [50.0, 0.0, -4.0], [48.0, 0.0, -2.0]]) and
          same_rotation(ring["children"][1]["rotation_xyzw"], yaw_xyzw(90.0)), f"{[fmt3(p) for p in child_positions(ring)]}")
    path = create({"position": [70.0, 0.0, 0.0], "items": block, "pattern": {"kind": "polyline", "count": 8, "points": [[4.0, 0.0, 0.0], [4.0, 0.0, 3.0]]}})
    check("a polyline in count mode spreads 8 pieces evenly over its 7 m, both ends holding one, the corner piece turned to the second segment",
          positions_match(child_positions(path), [[70.0 + i, 0.0, 0.0] for i in range(5)] + [[74.0, 0.0, float(i)] for i in range(1, 4)]) and
          same_rotation(path["children"][4]["rotation_xyzw"], yaw_xyzw(-90.0)) and (abs(path["run"]["space"] - 7.0) < TOLERANCE),
          f"{[fmt3(p) for p in child_positions(path)]}")

    # Gap mode (R5.12.7): the space and the free distance between neighbours determine the count.
    gap_line = create({"position": [0.0, 0.0, 40.0], "items": block, "pattern": {"kind": "line", "count_mode": "gap", "length": 7.0, "gap": 0.5}})
    check("a gap-mode line of 7 m with a 0.5 m gap between 1 m blocks holds 5 pieces, the remainder spread over the gaps (1.75 m steps)",
          positions_match(child_positions(gap_line), [[1.75 * i, 0.0, 40.0] for i in range(5)]) and (abs(gap_line["run"]["step"] - 1.75) < TOLERANCE),
          f"{[fmt3(p) for p in child_positions(gap_line)]}, run {gap_line['run']}")
    gap_ring = create({"position": [0.0, 0.0, 60.0], "items": block, "pattern": {"kind": "ring", "count_mode": "gap", "radius": 4.0, "gap": 1.0}})
    check("a gap-mode full ring of radius 4 with a 1 m gap between 1 m blocks closes with 12 pieces 30 degrees apart",
          (gap_ring["child_count"] == 12) and same_rotation(gap_ring["children"][3]["rotation_xyzw"], yaw_xyzw(90.0)) and
          close(gap_ring["children"][3]["position"], [4.0, 0.0, 56.0]), f"{gap_ring['child_count']} pieces, run {gap_ring['run']}")
    gap_path = create({"position": [0.0, 0.0, 80.0], "items": [{"shape": "basic.block", "parameters": {"width": 1.5}}],
                       "pattern": {"kind": "polyline", "count_mode": "gap", "gap": 0.5, "points": [[4.0, 0.0, 0.0], [4.0, 0.0, 3.0]]}})
    check("a gap-mode polyline of 7 m with a 0.5 m gap between 1.5 m blocks holds 4 pieces 2.333 m apart along the path, the last at its end",
          (gap_path["child_count"] == 4) and close(child_positions(gap_path)[3], [4.0, 0.0, 83.0]) and close(child_positions(gap_path)[1], [7.0 / 3.0, 0.0, 80.0]),
          f"{[fmt3(p) for p in child_positions(gap_path)]}")
    depth = undo_depth(editor)
    before_gap = child_snapshot(layout_of(gap_line["id"]))
    changed = editor.call("level_set_layout", {"scene_name": scene, "layout_id": gap_line["id"], "pattern": {"length": 9.0}})
    editor.wait_idle()
    check("changing the gap-mode line's length to 9 m regenerates 7 pieces 1.5 m apart as one undo entry, element 0 kept",
          (changed["child_count"] == 7) and (undo_depth(editor) == depth + 1) and close(child_positions(changed)[6], [9.0, 0.0, 40.0]) and
          (changed["children"][0]["id"] == before_gap[0][0]) and (changed["kept_count"] >= 1),
          f"{changed['child_count']} pieces, kept {changed['kept_count']}, depth {undo_depth(editor)} vs {depth + 1}")
    changed = editor.call("level_set_layout", {"scene_name": scene, "layout_id": gap_line["id"], "pattern": {"gap": 1.0}})
    check("a 1 m gap over 9 m holds 5 pieces (2 m steps, 2.25 m spread)", changed["child_count"] == 5, f"{changed['child_count']}")
    changed = editor.call("level_set_layout", {"scene_name": scene, "layout_id": gap_line["id"], "items": [{"shape": "basic.block", "parameters": {"width": 2.0}}]})
    check("a wider item (2 m) changes the count: 9 m at 3 m steps holds 4 pieces", changed["child_count"] == 4, f"{changed['child_count']}")
    editor.wait_idle()
    for _ in range(3):
        editor.call("undo")
    editor.wait_idle()
    check("undo of the three value changes gives the gap-mode line its first children back exactly",
          child_snapshot(layout_of(gap_line["id"])) == before_gap, "")

    # Several items: element i takes item i modulo the item count; materials per item.
    kits = editor.call("level_get_kits", {"scene_name": scene})
    block_slot = next(s for s in kits["shapes"] if s["id"] == "basic.block")["material_slots"][0]
    alternative = block_slot["alternatives"][0] if block_slot["alternatives"] else block_slot["default_material"]
    two = create({"position": [0.0, 0.0, -20.0], "items": [{"shape": "basic.block", "materials": {block_slot["slot"]: alternative}}, {"shape": "basic.cylinder"}],
                  "pattern": {"kind": "line", "count": 5, "step": 2.0}})
    check("two items alternate: block, cylinder, block, cylinder, block, the blocks with the item's slot material",
          ([c["shape"] for c in two["children"]] == ["basic.block", "basic.cylinder", "basic.block", "basic.cylinder", "basic.block"]) and
          all(c["materials"][block_slot["slot"]] == alternative for c in two["children"][0::2]),
          f"{[c['shape'] for c in two['children']]}, {[c['materials'] for c in two['children'][0::2]]}")

    # Snap settings: module snap rounds each element; surface snap rests each on the ground.
    snapped = create({"position": [100.3, 0.0, 0.2], "items": block, "snap": "module", "pattern": {"kind": "line", "count": 2, "step": 1.6}})
    check("module snap rounds each element's position to the module", positions_match(child_positions(snapped), [[100.0, 0.0, 0.0], [102.0, 0.0, 0.0]]),
          f"{[fmt3(p) for p in child_positions(snapped)]}")
    ground = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [120.0, 0.0, 0.0], "name": "layout ground", "parameters": {"width": 12.0, "depth": 4.0}}]})["pieces"][0]
    editor.wait_idle()
    top = ground["world_aabb"]["max"][1]
    rested = create({"name": "rested layout", "position": [116.0, 2.0, 0.0], "items": block, "surface_snap": True, "pattern": {"kind": "line", "count": 3, "step": 1.0}})
    check("surface snap rests each element on the ground slab", all(abs(p[1] - top) < TOLERANCE for p in child_positions(rested)),
          f"top {top:.3f}: {[fmt3(p) for p in child_positions(rested)]}")

    # Random seeds with the item's mask: differing derived parameters, stable across regeneration.
    rocks = create({"position": [0.0, 0.0, -40.0], "items": [{"shape": "nature.rock"}], "random_seeds": True, "seed": 5, "pattern": {"kind": "line", "count": 3, "step": 3.0}})
    texts = [c["parameters_text"] for c in rocks["children"]]
    check("per-element seeds give each rock its own derived parameters", len(set(texts)) == 3, "\n".join(texts))
    depth = undo_depth(editor)
    refreshed = editor.call("level_refresh_layout", {"scene_name": scene, "layout_id": rocks["id"]})
    editor.wait_idle()
    check("level_refresh_layout of an unchanged locked layout keeps every child and records no entry",
          (not refreshed["changed"]) and (child_snapshot(refreshed) == child_snapshot(rocks)) and (undo_depth(editor) == depth), "")
    grown = editor.call("level_set_layout", {"scene_name": scene, "layout_id": rocks["id"], "pattern": {"count": 4}})
    check("a fourth rock keeps the first three (same ids and parameters) and adds one with its own seed",
          ([c["id"] for c in grown["children"][:3]] == [c["id"] for c in rocks["children"]]) and
          ([c["parameters_text"] for c in grown["children"][:3]] == texts) and (grown["children"][3]["parameters_text"] not in texts), "")
    reseeded = editor.call("level_set_layout", {"scene_name": scene, "layout_id": rocks["id"], "seed": 6})
    check("another layout seed gives other derived parameters", [c["parameters_text"] for c in reseeded["children"][:3]] != texts, "")

    # Value changes of a locked layout: one entry each, undo gives the previous children back.
    before_line = child_snapshot(layout_of(line["id"]))
    depth = undo_depth(editor)
    changed = editor.call("level_set_layout", {"scene_name": scene, "layout_id": line["id"], "pattern": {"count": 3, "step": 3.0}})
    editor.wait_idle()
    check("changing count and step regenerates the line (3 pieces 3 m apart) as one undo entry, element 0 kept",
          positions_match(child_positions(changed), [[10.0, 0.0, 10.0 - 3.0 * i] for i in range(3)]) and (undo_depth(editor) == depth + 1) and
          (changed["children"][0]["id"] == before_line[0][0]), f"{[fmt3(p) for p in child_positions(changed)]}")
    editor.call("undo")
    editor.wait_idle()
    check("undo of the change gives the line its previous children back (ids, transforms, records, materials)",
          child_snapshot(layout_of(line["id"])) == before_line, "")
    editor.call("redo")
    editor.wait_idle()
    check("redo makes the 3-piece line again", layout_of(line["id"])["child_count"] == 3, "")
    editor.call("undo")
    editor.wait_idle()

    # Moving the node moves the run; with a surface setting the move rests the pieces again in the same entry.
    depth = undo_depth(editor)
    editor.call("set_node_transform", {"scene_name": scene, "node_id": line["id"], "translation": [15.0, 0.0, 10.0]})
    editor.wait_idle()
    moved = layout_of(line["id"])
    check("set_node_transform on a locked layout moves every child with it, one undo entry, children kept",
          positions_match(child_positions(moved), [[15.0, 0.0, 10.0 - 2.0 * i] for i in range(4)]) and (undo_depth(editor) == depth + 1) and
          ([c["id"] for c in moved["children"]] == [c[0] for c in before_line]), f"{[fmt3(p) for p in child_positions(moved)]}")
    editor.call("undo")
    editor.wait_idle()
    before_rested = child_snapshot(layout_of(rested["id"]))
    depth = undo_depth(editor)
    editor.call("set_node_transform", {"scene_name": scene, "node_id": rested["id"], "translation": [119.0, 3.0, 0.0]})
    editor.wait_idle()
    moved = layout_of(rested["id"])
    check("moving a surface-snapped layout rests its pieces on the ground again at the new place, in the move's one undo entry",
          positions_match(child_positions(moved), [[119.0 + i, top, 0.0] for i in range(3)]) and (undo_depth(editor) == depth + 1),
          f"{[fmt3(p) for p in child_positions(moved)]}, depth {undo_depth(editor)} vs {depth + 1}")
    editor.call("undo")
    editor.wait_idle()
    check("undo of that move gives the node and its pieces back", child_snapshot(layout_of(rested["id"])) == before_rested, "")

    # Selection: a child of a locked layout selects the layout node; a Transform tool edit moves the layout.
    child = line["children"][1]
    editor.call("select_items", {"scene_name": scene, "ids": [child["id"]]})
    editor.wait_idle()
    selection = editor.call("get_selection")
    check("selecting a child of a locked layout selects the layout node",
          ([i["id"] for i in selection["items"]] == [line["id"]]) and (selection.get("active_item", {}).get("id") == line["id"]), f"{selection}")
    editor.call("transform_selection", {"translation": [10.0, 0.0, 11.0]})
    editor.wait_idle()
    moved = layout_of(line["id"])
    check("transform_selection after that moves the layout node and the whole run, not the child alone",
          close(moved["position"], [10.0, 0.0, 11.0]) and close(moved["children"][1]["position"], [10.0, 0.0, 9.0]), f"{fmt3(moved['position'])}")
    editor.call("undo")
    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.wait_idle()

    # Every covered edit path refuses a child of a locked layout, naming the node, and changes nothing.
    other = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [{"shape": "basic.block", "position": [0.0, 0.0, -60.0], "name": "free block"}]})["pieces"][0]
    editor.wait_idle()
    editor.call("clipboard_copy_nodes", {"scene_name": scene, "node_ids": [other["id"]]})
    snapshot = child_snapshot(layout_of(line["id"]))
    depth = undo_depth(editor)
    for tool, label, arguments in [
        ("set_node_transform", "a transform", {"node_id": child["id"], "translation": [0.0, 5.0, 0.0]}),
        ("level_set_piece_parameters", "a parameter change", {"node_id": child["id"], "parameters": {"width": 2.0}}),
        ("level_set_piece_material", "a material change", {"node_id": child["id"], "slot": block_slot["slot"], "material": alternative}),
        ("assign_mesh_material", "a primitive material", {"mesh_id": child["id"], "primitive_index": 0, "material_name": alternative}),
        ("level_snap_to_surface", "a surface snap", {"node_ids": [child["id"]]}),
        ("level_group_nodes", "grouping it", {"node_ids": [child["id"]]}),
        ("level_ungroup", "an ungroup", {"group_id": child["id"]}),
        ("level_duplicate_group", "duplicating it", {"group_id": child["id"], "offset": [1.0, 0.0, 0.0]}),
        ("level_mirror_group", "mirroring it", {"group_id": child["id"], "axis": "x"}),
        ("reparent_item", "reparenting it out", {"item_id": child["id"], "parent_id": scene_tree(editor, scene)[line["id"]]["parent_id"]}),
        ("reparent_item", "reparenting a piece into the layout", {"item_id": other["id"], "parent_id": line["id"]}),
        ("delete_nodes", "deleting it", {"ids": [child["id"]]}),
        ("clipboard_paste", "pasting into the layout", {"parent_id": line["id"]}),
        ("level_place_pieces", "placing into the layout", {"pieces": [{"shape": "basic.block", "parent_id": line["id"]}]}),
        ("set_item_property", "a property write", {"item_id": child["id"], "property": "name", "value": "renamed"}),
    ]:
        error = call_error(editor, tool, dict({"scene_name": scene}, **arguments))
        check(f"{tool} refuses {label} of a locked layout's child, naming the layout node",
              (error is not None) and ("line layout" in error) and ("locked kit layout" in error), str(error))
    editor.wait_idle()
    check("the refused edits changed nothing and recorded no undo entry",
          (child_snapshot(layout_of(line["id"])) == snapshot) and (undo_depth(editor) == depth) and (scene_tree(editor, scene)[other["id"]]["parent_id"] != line["id"]),
          f"depth {undo_depth(editor)} vs {depth}")

    # The remaining id-addressed edit tools refuse a locked layout's child too, naming the layout node.
    rock_child = layout_of(rocks["id"])["children"][1]
    editor.call("select_items", {"scene_name": scene, "ids": [other["id"], line["id"]], "active": line["id"]})
    editor.wait_idle()
    for tool, label, arguments, layout_name in [
        ("set_item_flags", "a flag", {"ids": [child["id"]], "flags": ["no_transform_update"]}, "line layout"),
        ("add_tags", "a tag", {"ids": [child["id"]], "tags": ["layout test"]}, "line layout"),
        ("remove_tags", "a tag removal", {"ids": [child["id"]], "tags": ["layout test"]}, "line layout"),
        ("lock_items", "lock_edit", {"ids": [child["id"]]}, "line layout"),
        ("unlock_items", "unlock", {"ids": [child["id"]]}, "line layout"),
        ("apply_physics_force", "a force", {"node_id": child["id"], "force": [0.0, 10.0, 0.0]}, "line layout"),
        ("physics_drag", "a physics drag", {"node_id": child["id"], "translation": [0.0, 1.0, 0.0]}, "line layout"),
        ("navigation_set_seed", "a navigation seed", {"node_id": child["id"], "seed": True}, "line layout"),
        ("catmull_clark", "a geometry operation", {"node_id": child["id"]}, "line layout"),
        ("csg", "consuming it as a CSG tool", {"operation": "union", "node_id": other["id"], "tool_node_id": child["id"]}, "line layout"),
        ("select_mesh_components", "a component selection", {"node_id": child["id"], "facets": [0]}, "line layout"),
        ("generate_lods", "an LOD chain", {"node_id": child["id"]}, "line layout"),
        ("merge_static_subtree", "a merge of the layout", {"node_id": line["id"]}, "line layout"),
        ("attach_selection_to_active", "attaching a piece under the layout", {}, "line layout"),
        ("animation_create_key", "keying it", {"nodes": [rock_child["name"]]}, "Kit Layout"),
    ]:
        error = call_error(editor, tool, dict({"scene_name": scene}, **arguments))
        check(f"{tool} refuses {label} of a locked layout's child, naming the layout node",
              (error is not None) and (layout_name in error) and ("locked kit layout" in error), str(error))
    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.wait_idle()
    check("those refusals changed nothing and recorded no undo entry",
          (child_snapshot(layout_of(line["id"])) == snapshot) and (undo_depth(editor) == depth) and
          (scene_tree(editor, scene)[other["id"]]["parent_id"] != line["id"]) and (len(layout_of(rocks["id"])["children"]) == 4),
          f"depth {undo_depth(editor)} vs {depth}")

    # Raw Kit_layout values: a write on a locked node regenerates in its one entry; one its values cannot take is refused.
    pattern_object = json.loads(layout_of(line["id"])["pattern_text"])
    pattern_object["count"] = 2
    depth = undo_depth(editor)
    editor.call("set_item_property", {"scene_name": scene, "item_id": line["id"], "property": "Kit_layout.pattern",
                                      "value": json.dumps(pattern_object, sort_keys=True, separators=(",", ":"))})
    editor.wait_idle()
    raw = layout_of(line["id"])
    check("set_item_property of Kit_layout.pattern on a locked layout regenerates its children (2) as one undo entry",
          (raw["child_count"] == 2) and (raw["pattern"]["count"] == 2) and (undo_depth(editor) == depth + 1) and
          (raw["children"][0]["id"] == snapshot[0][0]), f"{raw['child_count']} children, depth {undo_depth(editor)} vs {depth + 1}")
    editor.call("undo")
    editor.wait_idle()
    check("its undo gives the values and the children back", child_snapshot(layout_of(line["id"])) == snapshot, "")
    pattern_object["count"] = 0
    for label, arguments, needle in [
        ("a pattern its values cannot take", {"property": "Kit_layout.pattern", "value": json.dumps(pattern_object, sort_keys=True, separators=(",", ":"))}, "level_set_layout"),
        ("an expression", {"property": "Kit_layout.seed", "expression": "1 + 1"}, "level_set_layout"),
        ("a pattern text that does not read", {"property": "Kit_layout.pattern", "value": "{}"}, "level_set_layout"),
    ]:
        error = call_error(editor, "set_item_property", dict({"scene_name": scene, "item_id": line["id"]}, **arguments))
        check(f"set_item_property refuses {label} of a locked layout, naming level_set_layout", (error is not None) and (needle in error), str(error))
    editor.wait_idle()
    check("the refused raw writes changed nothing", (child_snapshot(layout_of(line["id"])) == snapshot) and (undo_depth(editor) == depth), "")
    depth = undo_depth(editor)
    editor.call("set_item_property", {"scene_name": scene, "item_id": line["id"], "property": "Kit_layout.locked", "value": "false"})
    editor.wait_idle()
    check("a raw write of Kit_layout.locked false unlocks as level_set_layout_lock does, changing no child, one entry",
          (not layout_of(line["id"])["locked"]) and (child_snapshot(layout_of(line["id"])) == snapshot) and (undo_depth(editor) == depth + 1), "")
    editor.call("set_item_property", {"scene_name": scene, "item_id": line["id"], "property": "Kit_layout.locked", "value": "true"})
    editor.wait_idle()
    relocked_raw = layout_of(line["id"])
    check("a raw write of Kit_layout.locked true locks as level_set_layout_lock does: every child replaced, one entry",
          relocked_raw["locked"] and (relocked_raw["child_count"] == 4) and not ({c["id"] for c in relocked_raw["children"]} & {c[0] for c in snapshot}) and
          (undo_depth(editor) == depth + 2), f"depth {undo_depth(editor)} vs {depth + 2}")
    editor.call("undo")
    editor.call("undo")
    editor.wait_idle()
    check("undoing both gives the locked layout its children back", layout_of(line["id"])["locked"] and (child_snapshot(layout_of(line["id"])) == snapshot), "")

    # Unlock: one entry, no child changes; unlocked children are edited directly; values change alone.
    depth = undo_depth(editor)
    unlocked = editor.call("level_set_layout_lock", {"scene_name": scene, "layout_id": line["id"], "locked": False})
    editor.wait_idle()
    check("unlocking is one undo entry and changes no child (ids, transforms, records, materials)",
          (not unlocked["locked"]) and (child_snapshot(unlocked) == snapshot) and (undo_depth(editor) == depth + 1), "")
    editor.call("set_node_transform", {"scene_name": scene, "node_id": child["id"], "translation": [12.0, 1.0, 8.0]})
    editor.call("level_set_piece_parameters", {"scene_name": scene, "node_id": line["children"][2]["id"], "parameters": {"width": 1.5}})
    editor.call("level_set_piece_material", {"scene_name": scene, "node_id": line["children"][0]["id"], "slot": block_slot["slot"], "material": alternative})
    editor.wait_idle()
    editor.call("delete_nodes", {"scene_name": scene, "ids": [line["children"][3]["id"]]})
    editor.wait_idle()
    edited = layout_of(line["id"])
    check("unlocked children are edited directly: moved, re-parametrized, re-materialed and deleted",
          (edited["child_count"] == 3) and close(edited["children"][1]["position"], [12.0, 1.0, 8.0]) and
          ("0x1.8p+0" in edited["children"][2]["parameters_text"]) and (edited["children"][0]["materials"][block_slot["slot"]] == alternative),
          f"{edited['child_count']} children")
    edited_snapshot = child_snapshot(edited)
    changed = editor.call("level_set_layout", {"scene_name": scene, "layout_id": line["id"], "pattern": {"count": 6}})
    editor.wait_idle()
    check("level_set_layout on an unlocked layout changes its values and leaves its children alone",
          (changed["pattern"]["count"] == 6) and (child_snapshot(changed) == edited_snapshot) and (changed["run"]["count"] == 6), "")
    cache_before_lock = cached_brush_count(editor, scene)

    # Lock: discards the edits as one entry; undo gives the edited children back exactly; redo locks again.
    depth = undo_depth(editor)
    locked = editor.call("level_set_layout_lock", {"scene_name": scene, "layout_id": line["id"], "locked": True})
    editor.wait_idle()
    edited_ids = {c[0] for c in edited_snapshot}
    check("locking discards the edits: 6 fresh children at the pattern's poses, one undo entry",
          locked["locked"] and positions_match(child_positions(locked), [[10.0, 0.0, 10.0 - 2.0 * i] for i in range(6)]) and
          not ({c["id"] for c in locked["children"]} & edited_ids) and (undo_depth(editor) == depth + 1) and
          all(c["parameters_text"] == locked["children"][0]["parameters_text"] for c in locked["children"]),
          f"{[fmt3(p) for p in child_positions(locked)]}")
    locked_ids = [c["id"] for c in locked["children"]]
    editor.call("undo")
    editor.wait_idle()
    restored = layout_of(line["id"])
    check("undo of the lock gives the edited children back exactly and unlocks", (not restored["locked"]) and (child_snapshot(restored) == edited_snapshot), "")
    check("the restored children have their static bodies", all(p["physics"]["has_body"] for p in pieces(editor, scene) if p["id"] in edited_ids), "")
    editor.call("redo")
    editor.wait_idle()
    relocked = layout_of(line["id"])
    check("redo locks again with the generated children", relocked["locked"] and ([c["id"] for c in relocked["children"]] == locked_ids), "")
    check("the generated children follow their kit and have static bodies",
          all(p["follows_kit"] and p["physics"]["has_body"] for p in pieces(editor, scene) if p["id"] in set(locked_ids)), "")
    print(f"cached brushes: start {cache_start}, before lock {cache_before_lock}, after redo {cached_brush_count(editor, scene)}", flush=True)

    # Mirror and duplicate of a group holding a locked layout: the layout rides as a unit, the copy is a layout with its own children.
    group = editor.call("level_group_nodes", {"scene_name": scene, "node_ids": [two["id"]], "name": "layout group"})
    editor.wait_idle()
    copy = editor.call("level_duplicate_group", {"scene_name": scene, "group_id": group["id"], "offset": [0.0, 0.0, -10.0]})
    editor.wait_idle()
    layouts = layouts_by_id(editor, scene)
    tree = scene_tree(editor, scene)
    copies = [l for l in layouts.values() if tree[l["id"]]["parent_id"] == copy["id"]]
    check("duplicating a group with a locked layout gives a locked layout copy with its own children at the offset",
          (len(copies) == 1) and copies[0]["locked"] and (copies[0]["child_count"] == 5) and
          not ({c["id"] for c in copies[0]["children"]} & {c["id"] for c in two["children"]}) and
          close(copies[0]["children"][0]["position"], [two["children"][0]["position"][0], 0.0, two["children"][0]["position"][2] - 10.0]),
          f"{len(copies)} copies")
    mirrored = editor.call("level_mirror_group", {"scene_name": scene, "group_id": group["id"], "axis": "x"})
    check("mirroring the group moves the locked layout as a unit, reported as not exact, its children not listed",
          (two["id"] in mirrored["not_exact_ids"]) and not ({n["id"] for n in mirrored["nodes"]} & {c["id"] for c in two["children"]}), f"{mirrored['not_exact_ids']}")
    editor.call("undo")
    editor.wait_idle()

    # The level check and the overlap report read layout children as pieces.
    report = editor.call("level_check", {"scene_name": scene})
    check("level_check runs over a scene with kit layouts", isinstance(report, dict), "")

    # Refused arguments record nothing.
    free_id = unlocked_free(editor, scene, create)
    editor.wait_idle()
    depth = undo_depth(editor)
    for tool, label, arguments, needle in [
        ("level_create_layout", "no items", {"pattern": {"kind": "line"}}, "items"),
        ("level_create_layout", "no pattern", {"items": block}, "pattern"),
        ("level_create_layout", "an unknown kind", {"items": block, "pattern": {"kind": "spiral"}}, "kind"),
        ("level_create_layout", "an unknown count mode", {"items": block, "pattern": {"kind": "line", "count_mode": "fill"}}, "count_mode"),
        ("level_create_layout", "a count of 0", {"items": block, "pattern": {"kind": "line", "count": 0}}, "count"),
        ("level_create_layout", "a gap line over 1024 pieces", {"items": block, "pattern": {"kind": "line", "count_mode": "gap", "length": 5000.0}}, "1024"),
        ("level_create_layout", "a gap leaving no step", {"items": block, "pattern": {"kind": "line", "count_mode": "gap", "gap": -1.0}}, "gap"),
        ("level_create_layout", "an unknown shape", {"items": [{"shape": "basic.blob"}], "pattern": {"kind": "line"}}, "basic.blob"),
        ("level_create_layout", "an unknown pattern key", {"items": block, "pattern": {"kind": "line", "spacing": 1.0}}, "spacing"),
        ("level_create_layout", "a parent inside a locked layout", {"items": block, "pattern": {"kind": "line"}, "parent_id": line["id"]}, "locked kit layout"),
        ("level_set_layout", "a lock change", {"layout_id": line["id"], "locked": False}, "locked"),
        ("level_set_layout", "a node that is not a layout", {"layout_id": other["id"]}, "not a kit layout"),
        ("level_set_layout_lock", "no lock value", {"layout_id": line["id"]}, "locked"),
        ("level_refresh_layout", "an unlocked layout", {"layout_id": free_id}, "unlocked"),
    ]:
        error = call_error(editor, tool, dict({"scene_name": scene}, **arguments))
        check(f"{tool} refuses {label}", (error is not None) and (needle in error), str(error))
    editor.wait_idle()
    check("the refused layout calls recorded no undo entry", undo_depth(editor) == depth, f"{undo_depth(editor)} vs {depth}")

    # Save and reopen: values, lock states and children come back.
    before_layouts = layouts_by_id(editor, scene)
    path = os.path.join(directory, "layout_level.gltf")
    before_pieces, materials, name = save_close_load(editor, scene, path)
    after_layouts = list(editor.call("level_get_layouts", {"scene_name": name})["layouts"])

    def layout_key(layout):
        return (layout["name"], layout["locked"], layout["pattern_text"], json.dumps([i["parameters_text"] for i in layout["items"]]),
                tuple((c["name"], tuple(round(x, 3) for x in c["position"]), c.get("parameters_text")) for c in layout["children"]))

    check("save and reopen keep every layout's values, lock state and children",
          sorted(map(layout_key, before_layouts.values())) == sorted(map(layout_key, after_layouts)),
          f"{len(after_layouts)} layouts after, {len(before_layouts)} before")
    check("the reopened layout children follow their kit", all(p["follows_kit"] for p in pieces(editor, name)), "")
    reopened_line = next(l for l in after_layouts if l["name"] == "line layout")
    changed = editor.call("level_set_layout", {"scene_name": name, "layout_id": reopened_line["id"], "pattern": {"count": 2}})
    check("a reopened locked layout regenerates on a value change", changed["child_count"] == 2, f"{changed['child_count']}")
    editor.call("clear_undo_history")
    editor.wait_idle()
    editor.close(name)
    print(f"cached brushes after closing the layout scenes and clearing the undo history: {cached_brush_count_any(editor)} (start {cache_start})", flush=True)


def check_layout_tree(editor):
    """D3.9 in the item tree: a locked layout's child row offers no edit, drops onto and from it change nothing (scene closed afterwards)."""
    scene = create_scene(editor)
    layout = editor.call("level_create_layout", {"scene_name": scene, "name": "tree layout", "items": [{"shape": "basic.block"}],
                                                 "pattern": {"kind": "line", "count": 2}})
    free = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.block", "position": [0.0, 0.0, 5.0], "name": "tree free"},
        {"shape": "basic.block", "position": [3.0, 0.0, 5.0], "name": "tree other"}]})["pieces"]
    editor.wait_idle()
    editor.call("clipboard_copy_nodes", {"scene_name": scene, "node_ids": [free[0]["id"]]})

    def items(label, window=None):
        arguments = {"limit": 400, "visible_only": False, "label_contains": label}
        if window is not None:
            arguments["window"] = window
        return editor.call("get_imgui_items", arguments).get("items", [])

    rows = [i for i in items("tree layout") if i["label"] == "tree layout"]
    # The scene's hierarchy window may be a docked tab behind another one: bring each one forward until the row shows.
    hierarchies = [w["display_name"] for w in editor.call("get_imgui_windows").get("windows", []) if w.get("display_name", "").startswith("Scene Hierarchy")]
    for name in hierarchies:
        if rows:
            break
        editor.call("imgui_click", {"label": "Window"})
        editor.call("imgui_click", {"window": "Window###Menu_00", "label": name})
        time.sleep(0.3)
        rows = [i for i in items("tree layout") if i["label"] == "tree layout"]
    if not rows:
        check("the item tree shows the layout's row", False, "no row")
        editor.close(scene)
        return
    tree = rows[0]["window"]

    def row(label):
        return next((i for i in items(label, tree) if i["label"] == label), None)

    def menu(label):
        found = [i for i in items(label) if (i["label"] == label) and i["window"].startswith("##Popup")]
        return found[0] if found else None

    def open_menu(label):
        target = row(label)
        editor.call("mouse_click", {"x": target["center_x"], "y": target["center_y"], "button": "right"})
        time.sleep(0.2)

    def close_menu():
        editor.call("key_press", {"key": "escape"})
        time.sleep(0.2)

    layout_row = row("tree layout")
    editor.call("mouse_click", {"x": layout_row["x"] + 10.0, "y": layout_row["center_y"]})
    time.sleep(0.2)
    child = layout["children"][0]
    before = child_snapshot(layouts_by_id(editor, scene)[layout["id"]])
    depth = undo_depth(editor)

    open_menu(child["name"])
    entries = {label: menu(label) for label in ["Rename", "Cut", "Duplicate", "Paste", "Create", "Delete", "Locked by its kit layout"]}
    check("the item tree's context menu of a locked layout's child disables Rename, Cut, Duplicate, Paste and Create and shows the lock instead of the node edits",
          all((entries[label] is not None) and entries[label]["status"].get("disabled") for label in ["Rename", "Cut", "Duplicate", "Paste", "Create", "Locked by its kit layout"]),
          f"{ {label: (entry or {}).get('status', {}).get('disabled') for label, entry in entries.items()} }")
    delete = entries["Delete"]
    if delete is not None:
        editor.call("mouse_click", {"x": delete["center_x"], "y": delete["center_y"]})
        time.sleep(0.2)
    else:
        close_menu()
    editor.wait_idle()
    check("the item tree's Delete on a locked layout's child deletes nothing and records no entry",
          (child_snapshot(layouts_by_id(editor, scene)[layout["id"]]) == before) and (undo_depth(editor) == depth), f"depth {undo_depth(editor)} vs {depth}")

    open_menu("tree layout")
    paste, create, rename = menu("Paste"), menu("Create"), menu("Rename")
    check("on the locked layout's own row Paste and Create are disabled (it makes its own children) while Rename is offered",
          (paste is not None) and paste["status"].get("disabled") and (create is not None) and create["status"].get("disabled") and
          (rename is not None) and not rename["status"].get("disabled"), "")
    close_menu()
    open_menu("tree other")
    paste = menu("Paste")
    check("on a free piece's row Paste is offered (the clipboard holds a piece)", (paste is not None) and not paste["status"].get("disabled"), "")
    close_menu()

    def drag(source_label, target_label):
        source, target = row(source_label), row(target_label)
        editor.call("mouse_drag", {"from": [source["center_x"], source["center_y"]], "to": [target["center_x"], target["center_y"]], "frames": 12})
        time.sleep(0.3)
        editor.wait_idle()

    root_id = scene_tree(editor, scene)[layout["id"]]["parent_id"]
    drag("tree free", "tree other")
    moved = scene_tree(editor, scene)[free[0]["id"]]["parent_id"] == free[1]["id"]
    check("control: dragging a free piece's row onto another free piece reparents it", moved, "")
    if moved:
        editor.call("undo")
        editor.wait_idle()
    depth = undo_depth(editor)
    drag(child["name"], "tree other")
    drag("tree free", "tree layout")
    tree_now = scene_tree(editor, scene)
    check("dragging a locked layout's child out and a piece onto the locked layout node change nothing",
          (tree_now[child["id"]]["parent_id"] == layout["id"]) and (tree_now[free[0]["id"]]["parent_id"] == root_id) and (undo_depth(editor) == depth) and
          (child_snapshot(layouts_by_id(editor, scene)[layout["id"]]) == before), f"depth {undo_depth(editor)} vs {depth}")
    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.close(scene)


def unlocked_free(editor, scene, create):
    """A fresh unlocked layout's id (one undo entry)."""
    return create({"position": [0.0, 0.0, -100.0], "items": [{"shape": "basic.block"}], "locked": False, "pattern": {"kind": "line", "count": 1}})["id"]


def cached_brush_count_any(editor):
    names = editor.scene_names()
    return cached_brush_count(editor, names[0]) if names else -1


def item_id(editor, label, index=0):
    """The ImGui id of the index-th Level Kit window item with the label, clipped items counted."""
    matches = [i for i in imgui_items(editor) if i.get("display_label") == label]
    return matches[index]["id"]


def click_item(editor, label, index=0, modifiers=None):
    """Scrolls to the item and clicks it by id (an index of imgui_click counts visible items only)."""
    target = item_id(editor, label, index)
    editor.call("imgui_scroll_to_item", {"window": WINDOW, "id": target})
    arguments = {"window": WINDOW, "id": target}
    if modifiers:
        arguments["modifiers"] = modifiers
    return editor.call("imgui_click", arguments)


def type_into(editor, label, text, index=0):
    """Ctrl+click a drag row of the Level Kit window, type the value and enter it."""
    click_item(editor, label, index, ["ctrl"])
    editor.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
    editor.call("type_text", {"text": text})
    editor.call("key_press", {"key": "enter"})
    editor.wait_idle()


def check_window_layout(editor, scene, image_path):
    """D4.9: the Level Kit window's Layout section - Make Layout from a piece, every row kind, items, lock, refresh, refusals."""
    slab = next(p for p in pieces(editor, scene) if p["name"] == "slab")
    top = slab["world_aabb"]["max"][1]
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.block", "position": [-3.0, top, -1.0], "name": "layout source"},
        {"shape": "basic.wedge", "position": [3.0, top, 3.0], "name": "layout wedge"},
    ]})["pieces"]
    editor.wait_idle()
    source, wedge = placed
    source_before = piece_by_id(editor, scene, source["id"])
    start_depth = undo_depth(editor)
    module_was, surface_was = is_checked(editor, "Snap To Module"), is_checked(editor, "Snap To Surface")
    settings_ok = set_checkbox(editor, "Snap To Module", False) and set_checkbox(editor, "Snap To Surface", False)
    editor.call("select_items", {"scene_name": scene, "ids": [source["id"]]})
    editor.wait_idle()
    labels = {i.get("display_label") or "" for i in imgui_items(editor)}
    wanted = {"Layout", "Line", "Grid", "Ring", "Polyline", "By Count", "By Gap", "Count", "Direction.x", "Step", "Make Layout"}
    check("with a kit piece selected the Layout section shows the pattern rows and Make Layout", settings_ok and (wanted <= labels), f"missing {sorted(wanted - labels)}")

    def layout():
        layouts = list(layouts_by_id(editor, scene).values())
        return layouts[0] if len(layouts) == 1 else None

    def entry(label, action, predicate, describe=lambda l: ""):
        depth = undo_depth(editor)
        action()
        editor.wait_idle()
        current = layout()
        ok = (current is not None) and predicate(current) and (undo_depth(editor) == depth + 1)
        check(label, ok, f"depth {undo_depth(editor)} vs {depth + 1}; {describe(current) if current else 'no layout'}")
        return current

    # Make Layout: a line of 3 from the piece, one entry, the piece replaced by element 0, the node selected.
    click(editor, "Line")
    type_into(editor, "Count", "3")
    made = entry("Make Layout with a line of 3 makes a locked layout of 3 blocks 1 m apart in the piece's place as one undo entry",
                 lambda: click(editor, "Make Layout"),
                 lambda l: l["locked"] and positions_match(child_positions(l), [[-3.0 + i, top, -1.0] for i in range(3)]) and
                           (l["name"] == "layout source Layout") and (l["children"][0]["parameters_text"] == source_before["parameters_text"]),
                 lambda l: f"{[fmt3(p) for p in child_positions(l)]} {l['name']}")
    if made is None:
        set_checkbox(editor, "Snap To Module", module_was)
        set_checkbox(editor, "Snap To Surface", surface_was)
        return
    selection = editor.call("get_selection")
    check("the selected piece is replaced by element 0 (not left beside the layout) and the layout node is selected",
          (source["id"] not in pieces_by_id(editor, scene)) and ([i["id"] for i in selection["items"]] == [made["id"]]), f"{selection}")
    editor.call("undo")
    editor.wait_idle()
    restored = pieces_by_id(editor, scene).get(source["id"])
    check("undo of Make Layout gives the original piece back exactly and removes the layout",
          (restored is not None) and (layout() is None) and
          all(restored[k] == source_before[k] for k in ("name", "parameters_text", "materials", "brush_id")) and close(restored["position"], source_before["position"]),
          f"{restored and restored['name']}")
    editor.call("redo")
    editor.wait_idle()
    layout_id = made["id"]
    editor.call("select_items", {"scene_name": scene, "ids": [layout_id]})
    editor.wait_idle()
    labels = {i.get("display_label") or "" for i in imgui_items(editor)}
    wanted = {"Layout", "Line", "Grid", "Ring", "Polyline", "By Count", "By Gap", "Count", "Direction.x", "Step", "Module Snap", "Surface Snap",
              "Surface Orient", "Random Seeds", "Layout Seed", "Lock", "Unlock", "Refresh", "Remove Item", "Add Shape Item", "Add Piece Item", "stone material"}
    check("with the layout node selected the section shows every value row, the item's rows and Lock, Unlock and Refresh", wanted <= labels, f"missing {sorted(wanted - labels)}")
    os.makedirs(os.path.dirname(image_path), exist_ok=True)
    editor.call("imgui_scroll_to_item", {"window": WINDOW, "label": "Layout Seed", "align": "center"})
    editor.wait_idle()
    editor.call("capture_screenshot", {"path": image_path})
    check("capture_screenshot of the Level Kit window's Layout section is written", os.path.isfile(image_path), image_path)
    print(f"layout window screenshot: {image_path}", flush=True)

    # Each row kind edits and regenerates as one undo entry.
    entry("typing Count 4 regenerates 4 pieces as one undo entry", lambda: type_into(editor, "Count", "4"), lambda l: l["child_count"] == 4)
    entry("typing Step 2 spaces them 2 m apart as one undo entry", lambda: type_into(editor, "Step", "2"),
          lambda l: positions_match(child_positions(l), [[-3.0 + 2.0 * i, top, -1.0] for i in range(4)]), lambda l: f"{[fmt3(p) for p in child_positions(l)]}")
    entry("By Gap switches the line to gap mode as one undo entry", lambda: click(editor, "By Gap"), lambda l: l["pattern"]["count_mode"] == "gap")
    labels = {i.get("display_label") or "" for i in imgui_items(editor)}
    check("gap mode shows the Gap and Length rows in place of Count and Step", ({"Gap", "Length"} <= labels) and not ({"Count", "Step"} & labels), f"{sorted(labels & {'Gap', 'Length', 'Count', 'Step'})}")
    entry("typing Length 5 makes the count from the space: 5 m at a 0 m gap between 1 m blocks holds 6 pieces", lambda: type_into(editor, "Length", "5"),
          lambda l: l["child_count"] == 6, lambda l: f"{l['child_count']} run {l['run']}")
    entry("typing Gap 0.5 gives 4 pieces (1.5 m steps spread to 1.667 m)", lambda: type_into(editor, "Gap", "0.5"),
          lambda l: (l["child_count"] == 4) and (abs(l["run"]["step"] - 5.0 / 3.0) < TOLERANCE), lambda l: f"{l['child_count']} run {l['run']}")
    entry("By Count switches back to count mode as one undo entry", lambda: click(editor, "By Count"), lambda l: (l["pattern"]["count_mode"] == "count") and (l["child_count"] == 4))
    entry("typing a Direction component changes the line's direction as one undo entry", lambda: type_into(editor, "Direction.z", "1"),
          lambda l: l["pattern"]["direction"] != [1.0, 0.0, 0.0], lambda l: f"{l['pattern']['direction']}")
    entry("Grid makes a 3 x 3 grid of 9 pieces as one undo entry", lambda: click(editor, "Grid"), lambda l: (l["pattern"]["kind"] == "grid") and (l["child_count"] == 9))
    entry("typing Count X 2 gives 6 pieces", lambda: type_into(editor, "Count X", "2"), lambda l: l["child_count"] == 6)
    entry("typing Spacing Z 2 spaces the rows 2 m apart", lambda: type_into(editor, "Spacing Z", "2"),
          lambda l: abs(l["children"][2]["position"][2] - l["children"][0]["position"][2] - 2.0) < TOLERANCE, lambda l: f"{[fmt3(p) for p in child_positions(l)]}")
    entry("Ring makes a ring of 4 as one undo entry", lambda: click(editor, "Ring"), lambda l: (l["pattern"]["kind"] == "ring") and (l["child_count"] == 4))
    entry("typing Radius 3 changes the ring's radius as one undo entry", lambda: type_into(editor, "Radius", "3"),
          lambda l: (abs(l["pattern"]["radius"] - 3.0) < TOLERANCE) and close(l["children"][2]["position"], [-3.0, top, -7.0]), lambda l: f"{[fmt3(p) for p in child_positions(l)]}")
    entry("typing Arc 180 makes an open arc as one undo entry", lambda: type_into(editor, "Arc", "180"), lambda l: abs(l["pattern"]["arc_degrees"] - 180.0) < TOLERANCE)
    entry("Face Outward off keeps the node's rotation for every piece, one undo entry", lambda: click(editor, "Face Outward"),
          lambda l: (l["pattern"]["facing"] == "start") and all(same_rotation(c["rotation_xyzw"], [0.0, 0.0, 0.0, 1.0]) for c in l["children"]))
    entry("Polyline lays the pieces along the path as one undo entry", lambda: click(editor, "Polyline"),
          lambda l: (l["pattern"]["kind"] == "polyline") and (len(l["pattern"]["points"]) == 1) and (l["child_count"] == 4))
    entry("Add Point adds a point after the last as one undo entry", lambda: click(editor, "Add Point"), lambda l: len(l["pattern"]["points"]) == 2,
          lambda l: f"{l['pattern']['points']}")
    entry("typing into the second Point row moves that point as one undo entry", lambda: type_into(editor, "Point.x", "2", 1),
          lambda l: (len(l["pattern"]["points"]) == 2) and (l["pattern"]["points"][1] != [6.0, 0.0, 0.0]), lambda l: f"{l['pattern']['points']}")
    entry("Remove Point removes the first point as one undo entry", lambda: click_item(editor, "Remove Point", 0),
          lambda l: len(l["pattern"]["points"]) == 1, lambda l: f"{l['pattern']['points']}")
    entry("Line again as one undo entry", lambda: click(editor, "Line"), lambda l: l["pattern"]["kind"] == "line")

    # Items: add the selected piece, edit an item's parameter and slot material, remove an item.
    editor.call("select_items", {"scene_name": scene, "ids": [layout_id, wedge["id"]], "active": layout_id})
    editor.wait_idle()
    entry("Add Piece Item adds the selected wedge as a second item: the pieces alternate block, wedge, one undo entry", lambda: click(editor, "Add Piece Item"),
          lambda l: (len(l["items"]) == 2) and ([c["shape"] for c in l["children"]] == ["basic.block", "basic.wedge", "basic.block", "basic.wedge"]),
          lambda l: f"{[c['shape'] for c in l['children']]}")
    editor.call("select_items", {"scene_name": scene, "ids": [layout_id]})
    editor.wait_idle()
    # The first item's width row is the second last one down the window (the second item, the wedge, has one too).
    width_rows = sorted((i for i in imgui_items(editor) if i.get("display_label") == "width"), key=lambda i: i["center_y"])
    block_width = width_rows[-2]["index"] if len(width_rows) >= 2 else 0
    entry("typing 2 into the first item's width row gives its pieces width 2 as one undo entry", lambda: type_into(editor, "width", "2", block_width),
          lambda l: (l["items"][0]["parameters"]["width"] == 2.0) and (l["children"][0]["parameters"]["width"] == 2.0),
          lambda l: f"{[i['parameters'] for i in l['items']]}, {len(width_rows)} width rows")
    kits = editor.call("level_get_kits", {"scene_name": scene})
    block_slot = next(s for s in kits["shapes"] if s["id"] == "basic.block")["material_slots"][0]
    alternative = block_slot["alternatives"][0]
    wedge_slots = [x["slot"] for x in next(s for s in kits["shapes"] if s["id"] == "basic.wedge")["material_slots"]]
    slot_rows = sorted((i for i in imgui_items(editor) if i.get("display_label") == f"{block_slot['slot']} material"), key=lambda i: i["center_y"])
    block_slot_row = slot_rows[-2 if block_slot["slot"] in wedge_slots else -1]["index"]

    def choose_material():
        click_item(editor, f"{block_slot['slot']} material", block_slot_row)
        editor.call("imgui_click", {"label": alternative})

    entry(f"choosing {alternative} in the first item's {block_slot['slot']} material combo gives its pieces that material as one undo entry", choose_material,
          lambda l: (l["children"][0]["materials"][block_slot["slot"]] == alternative) and (l["items"][0]["materials"][block_slot["slot"]] == alternative),
          lambda l: f"{l['children'][0]['materials']}")
    entry("Remove Item on the second item leaves the blocks alone, one undo entry", lambda: click_item(editor, "Remove Item", 1),
          lambda l: (len(l["items"]) == 1) and all(c["shape"] == "basic.block" for c in l["children"]))

    # Snap and seed rows.
    entry("Module Snap on is one undo entry", lambda: click(editor, "Module Snap"), lambda l: l["snap"] == "module")
    entry("Surface Snap on rests the pieces on what is under them, one undo entry", lambda: click(editor, "Surface Snap"),
          lambda l: l["surface_snap"] and all(c["position"][1] >= (top - TOLERANCE) for c in l["children"]),
          lambda l: f"top {top:.3f}: {[fmt3(p) for p in child_positions(l)]}")
    entry("Surface Orient on is one undo entry", lambda: click(editor, "Surface Orient"), lambda l: l["surface_orient"])
    entry("Random Seeds on is one undo entry", lambda: click(editor, "Random Seeds"), lambda l: l["random_seeds"])
    entry("typing Layout Seed 7 is one undo entry", lambda: type_into(editor, "Layout Seed", "7"), lambda l: l["seed"] == 7)

    # A refused edit: a zero step with four pieces; its message shows in the section and nothing is recorded.
    before = child_snapshot(layout())
    depth = undo_depth(editor)
    type_into(editor, "Step", "0")
    editor.wait_idle()
    log_text = pav.read_log()
    check("a refused row edit (Step 0 for 4 pieces) records nothing, keeps the children and shows its refusal (logged with the section's message)",
          (undo_depth(editor) == depth) and (child_snapshot(layout()) == before) and (abs(layout()["pattern"]["step"] - 2.0) < TOLERANCE) and
          ("Level Kit: layout" in log_text) and ("step" in log_text.split("Level Kit: layout")[-1].splitlines()[0]),
          f"depth {undo_depth(editor)} vs {depth}, step {layout()['pattern']['step']}")

    # Unlock, an edit while unlocked, Lock and Refresh.
    unlocked = entry("Unlock is one undo entry and changes no child", lambda: click(editor, "Unlock"),
                     lambda l: (not l["locked"]) and (child_snapshot(l) == before))
    if unlocked is not None:
        entry("typing Count 2 on the unlocked layout changes its value and leaves its children", lambda: type_into(editor, "Count", "2"),
              lambda l: (l["pattern"]["count"] == 2) and (child_snapshot(l) == before))
        relocked = entry("Lock replaces every child with the 2 its values make, one undo entry", lambda: click(editor, "Lock"),
                         lambda l: l["locked"] and (l["child_count"] == 2) and not ({c["id"] for c in l["children"]} & {c[0] for c in before}))
        if relocked is not None:
            depth = undo_depth(editor)
            click(editor, "Refresh")
            editor.wait_idle()
            check("Refresh of a layout whose children match records no entry", (undo_depth(editor) == depth) and (child_snapshot(layout()) == child_snapshot(relocked)), "")
    states = {i.get("display_label"): i.get("status", {}).get("disabled") for i in imgui_items(editor) if i.get("display_label") in ("Lock", "Unlock", "Refresh")}
    check("locked, Lock is disabled and Unlock and Refresh are enabled", states == {"Lock": True, "Unlock": False, "Refresh": False}, f"{states}")

    # The window scene goes back to how it was for the next checks.
    while undo_depth(editor) > start_depth:
        editor.call("undo")
        editor.wait_idle()
    check("undoing every entry gives the window scene its piece back", (layout() is None) and (source["id"] in pieces_by_id(editor, scene)), "")
    set_checkbox(editor, "Snap To Module", module_was)
    set_checkbox(editor, "Snap To Surface", surface_was)
    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.wait_idle()


def check_properties_layout_write(editor, scene):
    """D3.9: a Properties row write of a Kit_layout value the node cannot take is taken back and records no undo entry (developer mode)."""
    slab = next(p for p in pieces(editor, scene) if p["name"] == "slab")
    top = slab["world_aabb"]["max"][1]
    made = editor.call("level_create_layout", {"scene_name": scene, "name": "properties layout", "position": [-3.0, top, 3.0],
                                               "items": [{"shape": "basic.block"}], "pattern": {"kind": "line", "count": 2}})
    editor.wait_idle()
    editor.call("select_items", {"scene_name": scene, "ids": [made["id"]]})
    properties = "Properties"
    editor.call("set_window_visibility", {"title": properties, "visible": True, "focus": True})
    editor.wait_idle()

    def items(label=None):
        arguments = {"window": properties, "limit": 1000, "visible_only": False}
        if label is not None:
            arguments["label_contains"] = label
        return editor.call("get_imgui_items", arguments).get("items", [])

    header = next((i for i in items("Kit Layout") if i.get("display_label") == "Kit Layout"), None)
    if (header is not None) and not header.get("status", {}).get("opened"):
        editor.call("imgui_scroll_to_item", {"window": properties, "label": "Kit Layout"})
        editor.call("imgui_click", {"window": properties, "label": "Kit Layout"})
        editor.wait_idle()
    # The attached row's text field (developer mode shows the raw Kit_layout rows).
    pattern_label = "* Pattern.x"
    row = next((i for i in items("Pattern") if i.get("display_label") == pattern_label), None)
    if row is None:
        print("Properties Kit Layout rows not shown (developer mode off): the Properties write check is skipped", flush=True)
    else:
        before = layouts_by_id(editor, scene)[made["id"]]
        depth = undo_depth(editor)
        editor.call("imgui_scroll_to_item", {"window": properties, "label": pattern_label})
        editor.call("imgui_click", {"window": properties, "label": pattern_label})
        editor.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
        editor.call("type_text", {"text": "{}"})
        editor.call("key_press", {"key": "enter"})
        editor.wait_idle()
        after = layouts_by_id(editor, scene)[made["id"]]
        check("a Properties Pattern row write its layout cannot take is taken back: no undo entry, values and children unchanged",
              (undo_depth(editor) == depth) and (after["pattern_text"] == before["pattern_text"]) and (child_snapshot(after) == child_snapshot(before)),
              f"depth {undo_depth(editor)} vs {depth}, pattern {after['pattern_text'][:80]}")
    editor.call("undo")
    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.wait_idle()


def scene_tree(editor, scene):
    """Every prim of the scene by id: name, type and parent_id."""
    return {n["id"]: n for n in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]}


def pieces_by_id(editor, scene):
    return {p["id"]: p for p in pieces(editor, scene)}


def reflect_point(p, origin, axis):
    return [(2.0 * origin[i] - p[i]) if i == axis else p[i] for i in range(3)]


def reflect_aabb(aabb, origin, axis):
    a = reflect_point(aabb["min"], origin, axis)
    b = reflect_point(aabb["max"], origin, axis)
    return {"min": [min(a[i], b[i]) for i in range(3)], "max": [max(a[i], b[i]) for i in range(3)]}


def aabb_close(a, b, tolerance=TOLERANCE):
    return close(a["min"], b["min"], tolerance) and close(a["max"], b["max"], tolerance)


def union_aabb(boxes):
    return {"min": [min(b["min"][i] for b in boxes) for i in range(3)], "max": [max(b["max"][i] for b in boxes) for i in range(3)]}


def base_center(aabb):
    return [0.5 * (aabb["min"][0] + aabb["max"][0]), aabb["min"][1], 0.5 * (aabb["min"][2] + aabb["max"][2])]


def check_groups(editor, directory):
    """D3.8: group, ungroup, move, delete, duplicate and mirror with known answers, undo and redo, nesting, persistence and refusals (scene closed afterwards)."""
    scene = create_scene(editor)
    identity = [0.0, 0.0, 0.0, 1.0]
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.block",  "position": [2.0, 0.0, 1.0],  "name": "g block", "parameters": {"width": 2.0}},
        {"shape": "basic.wedge",  "position": [-1.0, 0.0, 3.0], "name": "g wedge"},
        {"shape": "nature.rock",  "position": [0.0, 0.0, -1.0], "name": "g rock"},
        {"shape": "basic.stairs", "position": [4.0, 0.0, -2.0], "name": "g stairs", "parameters": {"rise": 1.0}},
        {"shape": "basic.block",  "position": [8.0, 0.0, 8.0],  "name": "g outside"},
    ]})["pieces"]
    editor.wait_idle()
    block, wedge, rock, stairs, outside = placed
    members = [block, wedge, rock, stairs]
    member_ids = [p["id"] for p in members]
    before = pieces_by_id(editor, scene)
    root_id = scene_tree(editor, scene)[block["id"]]["parent_id"]

    # Group: one undo entry, the group at the base center of the bounds, every piece under it at its world transform.
    depth = undo_depth(editor)
    group = editor.call("level_group_nodes", {"scene_name": scene, "node_ids": member_ids, "name": "test group"})
    editor.wait_idle()
    expected_origin = base_center(union_aabb([before[i]["world_aabb"] for i in member_ids]))
    tree = scene_tree(editor, scene)
    after = pieces_by_id(editor, scene)
    check("level_group_nodes puts the pieces under a new group node at the base center of their combined world bounds, one undo entry",
          (undo_depth(editor) == depth + 1) and close(group["position"], expected_origin) and (group["name"] == "test group") and
          all(tree[i]["parent_id"] == group["id"] for i in member_ids) and (group["piece_count"] == 4) and (group["count"] == 5) and
          (tree[group["id"]]["type"] == "Xform"),
          f"position {fmt3(group['position'])} vs {fmt3(expected_origin)}, depth {undo_depth(editor)} vs {depth + 1}")
    check("grouping keeps every piece's world transform and bounds",
          all(close(after[i]["position"], before[i]["position"]) and same_rotation(after[i]["rotation_xyzw"], before[i]["rotation_xyzw"]) and
              aabb_close(after[i]["world_aabb"], before[i]["world_aabb"]) for i in member_ids), "")
    group_id = group["id"]
    editor.call("undo")
    editor.wait_idle()
    tree = scene_tree(editor, scene)
    after = pieces_by_id(editor, scene)
    check("undo of the group takes the group node out and gives the pieces their parent and world transforms back",
          (group_id not in tree) and all(tree[i]["parent_id"] == root_id for i in member_ids) and
          all(close(after[i]["position"], before[i]["position"]) for i in member_ids), "")
    editor.call("redo")
    editor.wait_idle()
    tree = scene_tree(editor, scene)
    after = pieces_by_id(editor, scene)
    check("redo of the group gives the same group node and the pieces under it at their world transforms",
          (group_id in tree) and all(tree[i]["parent_id"] == group_id for i in member_ids) and
          all(close(after[i]["position"], before[i]["position"]) for i in member_ids), "")

    # Move the group as a unit with the existing transform tool: one entry, every piece moves with it.
    depth = undo_depth(editor)
    editor.call("set_node_transform", {"scene_name": scene, "node_id": group_id, "translation": [group["position"][i] + d for i, d in enumerate([10.0, 0.0, 0.0])]})
    editor.wait_idle()
    moved = pieces_by_id(editor, scene)
    check("set_node_transform on the group node moves every piece by the same offset as one undo entry, bodies kept",
          (undo_depth(editor) == depth + 1) and
          all(close(moved[i]["position"], [before[i]["position"][0] + 10.0] + before[i]["position"][1:]) and moved[i]["physics"]["has_body"] for i in member_ids) and
          close(moved[outside["id"]]["position"], before[outside["id"]]["position"]),
          f"{[fmt3(moved[i]['position']) for i in member_ids]}")
    editor.call("undo")
    editor.wait_idle()
    moved = pieces_by_id(editor, scene)
    check("undo of the move gives every piece its place back", all(close(moved[i]["position"], before[i]["position"]) for i in member_ids), "")

    # Duplicate: full pieces sharing the meshes and collision shapes of the originals.
    depth = undo_depth(editor)
    count = len(before)
    copy = editor.call("level_duplicate_group", {"scene_name": scene, "group_id": group_id, "offset": [0.0, 0.0, 10.0]})
    editor.wait_idle()
    now = pieces_by_id(editor, scene)
    copies = {p["name"]: now[p["id"]] for p in copy["nodes"] if p["kind"] == "piece"}
    originals = {p["name"]: now[p["id"]] for p in members}
    check("level_duplicate_group copies the group with its 4 pieces as one undo entry, named '<name> Copy', offset by [0, 0, 10]",
          (undo_depth(editor) == depth + 1) and (len(now) == count + 4) and (copy["piece_count"] == 4) and (copy["name"] == "test group Copy") and
          close(copy["position"], [group["position"][0], group["position"][1], group["position"][2] + 10.0]) and (set(copies) == set(originals)),
          f"{len(now)} pieces vs {count + 4}, {copy['name']}, {fmt3(copy['position'])}")
    check("every copied piece is a full piece: its record, the original's shared mesh, follows its kit, a static body with the original's collision shape",
          all((c["parameters_text"] == originals[n]["parameters_text"]) and (c["brush_id"] == originals[n]["brush_id"]) and c["follows_kit"] and
              c["shares_brush_primitive"] and c["physics"]["has_body"] and (c["physics"]["motion_mode"] == "static") and
              (c["physics"]["collision_shape"] == originals[n]["physics"]["collision_shape"]) and
              close(c["position"], [originals[n]["position"][0], originals[n]["position"][1], originals[n]["position"][2] + 10.0])
              for n, c in copies.items()),
          "; ".join(f"{n}: {c['physics']['collision_shape']} vs {originals[n]['physics']['collision_shape']}" for n, c in copies.items()))
    block_copy = copies["g block"]
    changed = editor.call("level_set_piece_parameters", {"scene_name": scene, "node_id": block_copy["id"], "parameters": {"width": 3.0}})
    editor.wait_idle()
    now = pieces_by_id(editor, scene)
    check("level_set_piece_parameters on a copied piece changes the copy alone",
          changed["changed"] and (abs(size_of(now[block_copy["id"]]["world_aabb"])[0] - 3.0) < TOLERANCE) and
          (abs(size_of(now[block["id"]]["world_aabb"])[0] - 2.0) < TOLERANCE) and (now[block["id"]]["brush_id"] == block["brush_id"]),
          f"copy width {size_of(now[block_copy['id']]['world_aabb'])[0]:.3f}, original {size_of(now[block['id']]['world_aabb'])[0]:.3f}")
    editor.call("undo")
    editor.call("undo")
    editor.wait_idle()
    check("undo takes the copy out", len(pieces(editor, scene)) == count, f"{len(pieces(editor, scene))} vs {count}")
    editor.call("redo")
    editor.wait_idle()
    now = pieces_by_id(editor, scene)
    check("redo puts the copied pieces back with their records, shared meshes and bodies",
          all((c["id"] in now) and now[c["id"]]["follows_kit"] and now[c["id"]]["physics"]["has_body"] and
              (now[c["id"]]["physics"]["collision_shape"] == c["physics"]["collision_shape"]) for c in copies.values()), "")
    editor.call("undo")
    editor.wait_idle()

    # Mirror across the group's z axis: known positions and rotations, the asymmetric rock flagged.
    origin = group["position"]
    before = pieces_by_id(editor, scene)
    depth = undo_depth(editor)
    mirrored = editor.call("level_mirror_group", {"scene_name": scene, "group_id": group_id, "axis": "z"})
    editor.wait_idle()
    now = pieces_by_id(editor, scene)
    expected_rotations = {block["id"]: identity, wedge["id"]: yaw_xyzw(180.0), rock["id"]: yaw_xyzw(180.0), stairs["id"]: yaw_xyzw(180.0)}
    check("level_mirror_group z reflects every piece's position across the group's z plane, one undo entry",
          (undo_depth(editor) == depth + 1) and (mirrored["count"] == 4) and close(mirrored["plane_normal"], [0.0, 0.0, 1.0]) and
          all(close(now[i]["position"], reflect_point(before[i]["position"], origin, 2)) for i in member_ids),
          "; ".join(f"{now[i]['name']} {fmt3(now[i]['position'])}" for i in member_ids))
    check("the block (planes x, z) keeps its rotation; the wedge and stairs (plane x) and the rock turn 180 degrees about y: no negative scale",
          all(same_rotation(now[i]["rotation_xyzw"], expected_rotations[i]) for i in member_ids),
          "; ".join(f"{now[i]['name']} {fmt3(now[i]['rotation_xyzw'])}" for i in member_ids))
    check("the symmetric pieces show their exact mirror image (world bounds reflected); the rock is listed as not exact",
          all(aabb_close(now[i]["world_aabb"], reflect_aabb(before[i]["world_aabb"], origin, 2)) for i in (block["id"], wedge["id"], stairs["id"])) and
          (mirrored["not_exact_ids"] == [rock["id"]]) and (mirrored["not_exact_count"] == 1),
          f"not exact {mirrored['not_exact_ids']}")
    check("mirrored pieces stay kit pieces with their records, shared meshes and static bodies",
          all(now[i]["follows_kit"] and now[i]["physics"]["has_body"] and (now[i]["parameters_text"] == before[i]["parameters_text"]) for i in member_ids), "")
    editor.call("level_mirror_group", {"scene_name": scene, "group_id": group_id, "axis": "z"})
    editor.wait_idle()
    now = pieces_by_id(editor, scene)
    check("mirrored twice, every piece is back at its original position, rotation and bounds",
          all(close(now[i]["position"], before[i]["position"]) and same_rotation(now[i]["rotation_xyzw"], before[i]["rotation_xyzw"]) and
              aabb_close(now[i]["world_aabb"], before[i]["world_aabb"]) for i in member_ids), "")
    mirrored_x = editor.call("level_mirror_group", {"scene_name": scene, "group_id": group_id, "axis": "x"})
    editor.wait_idle()
    now = pieces_by_id(editor, scene)
    check("level_mirror_group x reflects across the group's x plane: plane-x shapes keep their rotation, the block's bounds are reflected",
          all(close(now[i]["position"], reflect_point(before[i]["position"], origin, 0)) and same_rotation(now[i]["rotation_xyzw"], identity) for i in member_ids) and
          aabb_close(now[block["id"]]["world_aabb"], reflect_aabb(before[block["id"]]["world_aabb"], origin, 0)) and (mirrored_x["not_exact_ids"] == [rock["id"]]),
          "")
    for _ in range(3):
        editor.call("undo")
    editor.wait_idle()
    now = pieces_by_id(editor, scene)
    check("three undos give the pieces their transforms from before the mirrors",
          all(close(now[i]["position"], before[i]["position"]) and same_rotation(now[i]["rotation_xyzw"], before[i]["rotation_xyzw"]) for i in member_ids), "")

    # Nesting: a group of a group and a piece; a node given with its ancestor goes with the ancestor.
    outer = editor.call("level_group_nodes", {"scene_name": scene, "node_ids": [group_id, outside["id"], block["id"]], "name": "outer"})
    editor.wait_idle()
    tree = scene_tree(editor, scene)
    check("a group of the group, a piece and a piece inside the group nests the group; the inner piece stays in its group",
          (tree[group_id]["parent_id"] == outer["id"]) and (tree[outside["id"]]["parent_id"] == outer["id"]) and
          (tree[block["id"]]["parent_id"] == group_id) and (outer["piece_count"] == 5) and (outer["count"] == 7),
          f"count {outer['count']}, pieces {outer['piece_count']}")
    nested_copy = editor.call("level_duplicate_group", {"scene_name": scene, "group_id": outer["id"], "position": [30.0, 0.0, 0.0], "name": "outer copy"})
    editor.wait_idle()
    tree = scene_tree(editor, scene)
    inner_copies = [n for n in nested_copy["nodes"] if n["kind"] == "group" and n["id"] != nested_copy["id"]]
    check("duplicating the outer group copies the nested group with its pieces, at the given position",
          (nested_copy["piece_count"] == 5) and (len(inner_copies) == 1) and (tree[inner_copies[0]["id"]]["parent_id"] == nested_copy["id"]) and
          close(nested_copy["position"], [30.0, 0.0, 0.0]) and (nested_copy["name"] == "outer copy"),
          f"{nested_copy['piece_count']} pieces, {len(inner_copies)} inner groups")
    editor.call("undo")
    editor.wait_idle()
    before = pieces_by_id(editor, scene)
    depth = undo_depth(editor)
    ungrouped = editor.call("level_ungroup", {"scene_name": scene, "group_id": outer["id"]})
    editor.wait_idle()
    tree = scene_tree(editor, scene)
    now = pieces_by_id(editor, scene)
    check("level_ungroup moves the outer group's children to its parent, world transforms kept, removes the group node, one undo entry",
          (undo_depth(editor) == depth + 1) and (outer["id"] not in tree) and (ungrouped["child_count"] == 2) and
          (tree[group_id]["parent_id"] == root_id) and (tree[outside["id"]]["parent_id"] == root_id) and
          all(close(now[i]["position"], before[i]["position"]) for i in before), "")
    editor.call("undo")
    editor.wait_idle()
    tree = scene_tree(editor, scene)
    check("undo of the ungroup gives the group node and its children back", (outer["id"] in tree) and (tree[group_id]["parent_id"] == outer["id"]), "")
    editor.call("undo")
    editor.wait_idle()

    # Delete the group with the existing recursive delete; undo gives full pieces back.
    stairs_shape = pieces_by_id(editor, scene)[stairs["id"]]["physics"]["collision_shape"]
    count = len(pieces(editor, scene))
    depth = undo_depth(editor)
    editor.call("delete_nodes", {"scene_name": scene, "ids": [group_id]})
    editor.wait_idle()
    check("delete_nodes on the group deletes it with its 4 pieces as one undo entry",
          (len(pieces(editor, scene)) == count - 4) and (undo_depth(editor) == depth + 1) and (group_id not in scene_tree(editor, scene)), "")
    editor.call("undo")
    editor.wait_idle()
    now = pieces_by_id(editor, scene)
    tree = scene_tree(editor, scene)
    check("undo of the delete gives the pieces back under the group with their records, shared meshes, bodies and collision shapes",
          all((i in now) and (tree[i]["parent_id"] == group_id) and now[i]["follows_kit"] and now[i]["physics"]["has_body"] for i in member_ids) and
          (now[stairs["id"]]["physics"]["collision_shape"] == stairs_shape),
          f"stairs shape {now.get(stairs['id'], {}).get('physics', {}).get('collision_shape')} vs {stairs_shape}")

    # A pasted copy of a triangle mesh piece gets its brush's shape (the paste path the duplicate shares).
    known = set(now)
    editor.call("clipboard_copy_nodes", {"scene_name": scene, "node_ids": [stairs["id"]]})
    editor.call("clipboard_paste", {"scene_name": scene})
    editor.wait_idle()
    pasted = [p for p in pieces(editor, scene) if p["id"] not in known]
    check("a pasted kit piece shares the original's mesh and collision shape (a triangle mesh for the stairs) and follows its kit",
          (len(pasted) == 1) and (pasted[0]["brush_id"] == stairs["brush_id"]) and pasted[0]["follows_kit"] and
          (pasted[0]["physics"]["collision_shape"] == stairs_shape) and pasted[0]["physics"]["has_body"],
          f"{[(p['name'], p['physics']['collision_shape']) for p in pasted]} vs {stairs_shape}")
    editor.call("undo")
    editor.wait_idle()

    # Refusals record nothing.
    empty = editor.call("create_node", {"scene_name": scene, "name": "empty group"})
    editor.wait_idle()
    count = len(pieces(editor, scene))
    depth = undo_depth(editor)
    for tool, label, arguments, needle in [
        ("level_group_nodes", "no nodes", {"node_ids": []}, "node_ids"),
        ("level_group_nodes", "a node listed twice", {"node_ids": [wedge["id"], wedge["id"]]}, "twice"),
        ("level_group_nodes", "an unknown node", {"node_ids": [outside["id"] + 100000]}, "no node"),
        ("level_group_nodes", "the scene root", {"node_ids": [root_id]}, "scene root"),
        ("level_group_nodes", "an unknown argument", {"node_ids": [wedge["id"]], "parent_id": 0}, "parent_id"),
        ("level_group_nodes", "an empty name", {"node_ids": [wedge["id"]], "name": ""}, "name"),
        ("level_ungroup", "a piece", {"group_id": wedge["id"]}, "not a group node"),
        ("level_ungroup", "no group_id", {}, "group_id"),
        ("level_duplicate_group", "both offset and position", {"group_id": group_id, "offset": [1.0, 0.0, 0.0], "position": [0.0, 0.0, 0.0]}, "exactly one"),
        ("level_duplicate_group", "neither offset nor position", {"group_id": group_id}, "exactly one"),
        ("level_duplicate_group", "a bad offset", {"group_id": group_id, "offset": [1.0, 0.0]}, "offset"),
        ("level_duplicate_group", "the scene root", {"group_id": root_id, "offset": [1.0, 0.0, 0.0]}, "scene root"),
        ("level_mirror_group", "an unknown axis", {"group_id": group_id, "axis": "w"}, "axis"),
        ("level_mirror_group", "a node without child nodes", {"group_id": empty["node_id"], "axis": "x"}, "no child nodes"),
    ]:
        error = call_error(editor, tool, dict({"scene_name": scene}, **arguments))
        check(f"{tool} refuses {label}", (error is not None) and (needle in error), str(error))
    editor.wait_idle()
    check("the refused group calls changed nothing and recorded no undo entry",
          (len(pieces(editor, scene)) == count) and (undo_depth(editor) == depth), f"depth {undo_depth(editor)} vs {depth}")

    # Persistence: the hierarchy and the pieces come back, a copy sharing the originals' meshes again.
    editor.call("level_duplicate_group", {"scene_name": scene, "group_id": group_id, "offset": [0.0, 0.0, 10.0]})
    editor.wait_idle()
    path = os.path.join(directory, "group_level.gltf")
    before_pieces, materials, name = save_close_load(editor, scene, path)
    check_reloaded("group level", editor, before_pieces, materials, name)
    tree = scene_tree(editor, name)
    by_name = {n["name"]: n for n in tree.values()}
    group_children = {n["name"] for n in tree.values() if n.get("parent_id") == by_name.get("test group", {}).get("id")}
    copy_children = {n["name"] for n in tree.values() if n.get("parent_id") == by_name.get("test group Copy", {}).get("id")}
    check("group level: the group node and its copy are back, each with its four pieces as children",
          ("test group" in by_name) and ("test group Copy" in by_name) and (by_name["test group"]["type"] == "Xform") and
          (group_children == {p["name"] for p in members}) and (copy_children == {p["name"] for p in members}),
          f"{sorted(group_children)} / {sorted(copy_children)}")
    reloaded_group = by_name["test group"]["id"]
    reloaded = editor.call("level_mirror_group", {"scene_name": name, "group_id": reloaded_group, "axis": "z"})
    editor.wait_idle()
    check("group level: a reloaded group mirrors like the original", (reloaded["count"] == 4) and (reloaded["not_exact_count"] == 1), "")
    # Left open, it moves the Transform checks' scene to a wide viewport (level_tools_implementation.md step 20).
    editor.close(name)


def node_mesh(editor, scene, node_id):
    details = editor.call("get_node_details", {"scene_name": scene, "node_id": node_id})
    return details.get("mesh") or {}


TREE_DEFAULTS = {"bark": "Kit Bark", "foliage": "Kit Foliage"}


def check_material_slots(editor, directory):
    """D1.11, D3.4: material slots per shape, placement, set_slot_material and what keeps them (scene closed afterwards)."""
    scene = create_scene(editor)
    catalog = editor.call("level_get_kits", {"scene_name": scene})
    by_id = {shape["id"]: shape for shape in catalog["shapes"]}
    palette = {m["name"] for m in catalog["palette"]}
    tree_slots = by_id["nature.tree"]["material_slots"]
    check("the catalog lists the tree's slots bark and foliage with their defaults Kit Bark and Kit Foliage and alternatives",
          ([(s["slot"], s["default_material"]) for s in tree_slots] == list(TREE_DEFAULTS.items())) and
          all(s["alternatives"] and set(s["alternatives"]) <= palette for s in tree_slots), json.dumps(tree_slots))
    slot_problems = [f"{shape['id']}.{slot['slot']}" for shape in catalog["shapes"] for slot in shape["material_slots"]
                     if (slot["default_material"] not in palette) or not slot["alternatives"] or not set(slot["alternatives"]) <= palette
                     or (slot["default_material"] in slot["alternatives"])]
    check("every catalog slot has a palette default and palette alternatives; the palette holds stone, wood, plaster, foliage, bark, earth and grass",
          (not slot_problems) and ({f"Kit {n}" for n in ("Stone", "Wood", "Plaster", "Foliage", "Bark", "Earth", "Grass")} <= palette),
          f"{slot_problems} palette {sorted(palette)}")

    # Placement: a ground for the support checks (pieces of one call are no surfaces for each other), defaults, per-slot materials.
    ground = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.floor_slab", "position": [0.0, 0.0, 0.0], "name": "m ground", "parameters": {"width": 16.0, "depth": 16.0}}]})["pieces"][0]
    editor.wait_idle()
    materials_start = material_count(editor, scene)
    depth = undo_depth(editor)
    placed = editor.call("level_place_pieces", {"scene_name": scene, "surface_snap": True, "pieces": [
        {"shape": "nature.tree", "position": [0.0, 2.0, 0.0], "name": "m tree"},
        {"shape": "nature.tree", "position": [4.0, 2.0, 0.0], "name": "m chosen", "materials": {"bark": "Kit Wood", "foliage": "Kit Autumn Foliage"}},
        {"shape": "basic.block", "position": [-4.0, 2.0, 0.0], "name": "m block", "materials": {"stone": "Kit Rock"}},
    ]})["pieces"]
    editor.wait_idle()
    tree, chosen, block = placed
    top = ground["world_aabb"]["max"][1]
    mesh = node_mesh(editor, scene, tree["id"])
    check("a placed tree is one piece with two mesh primitives, Kit Bark and Kit Foliage, its mesh the kit brush's",
          (tree["materials"] == TREE_DEFAULTS) and (mesh.get("primitive_count") == 2) and (mesh.get("materials") == ["Kit Bark", "Kit Foliage"]) and
          piece_by_id(editor, scene, tree["id"]).get("shares_brush_primitive") and piece_by_id(editor, scene, tree["id"]).get("follows_kit") and
          (tree["triangle_count"] == 68),
          f"{tree['materials']} {json.dumps({k: mesh.get(k) for k in ('primitive_count', 'materials')})} triangles {tree['triangle_count']}")
    check("per-slot materials at placement: the chosen tree has Kit Wood bark and Kit Autumn Foliage, the block Kit Rock, all in one undo entry",
          (chosen["materials"] == {"bark": "Kit Wood", "foliage": "Kit Autumn Foliage"}) and (block["materials"] == {"stone": "Kit Rock"}) and
          (node_mesh(editor, scene, chosen["id"]).get("materials") == ["Kit Wood", "Kit Autumn Foliage"]) and (undo_depth(editor) == depth + 1) and
          (chosen["brush_id"] == tree["brush_id"]),
          f"{chosen['materials']} {block['materials']} undo {undo_depth(editor) - depth}")
    materials_placed = material_count(editor, scene)
    editor.call("undo")
    editor.wait_idle()
    undone_count = material_count(editor, scene)
    editor.call("redo")
    editor.wait_idle()
    check("undo of the placement takes out the palette materials it added (Bark, Foliage, Wood, Autumn Foliage, Rock) and redo gives them back",
          (undone_count == materials_start) and (materials_placed == materials_start + 5) and (material_count(editor, scene) == materials_placed),
          f"{materials_start} -> {materials_placed}, undo {undone_count}, redo {material_count(editor, scene)}")

    # Support, overlap and snap for a tree on the ground.
    check("the trees rest on the ground slab (surface_snap; a triangle mesh body)",
          all(abs(p["position"][1] - top) < TOLERANCE and p["surface_found"] for p in (tree, chosen)) and
          ("mesh" in piece_by_id(editor, scene, tree["id"])["physics"]["collision_shape"].lower()),
          f"{fmt3(tree['position'])} {fmt3(chosen['position'])} top {top:.3f}")
    support = editor.call("level_get_support_report", {"scene_name": scene, "node_ids": [tree["id"], chosen["id"]]})
    check("the support report finds both trees supported", support["unsupported_count"] == 0, json.dumps(support)[:300])
    overlap = editor.call("level_get_overlap_report", {"scene_name": scene, "node_ids": [ground["id"], tree["id"], chosen["id"]]})
    check("the overlap report finds no overlap between the trees and the ground they rest on", overlap["overlap_count"] == 0, json.dumps(overlap)[:300])
    editor.call("set_node_transform", {"scene_name": scene, "node_id": chosen["id"], "translation": [4.0, top + 1.0, 0.0]})
    editor.wait_idle()
    snap = editor.call("level_snap_to_surface", {"scene_name": scene, "node_ids": [chosen["id"]], "query_only": True})
    check("level_snap_to_surface (query_only) finds the raised tree 1 m above the ground",
          snap["nodes"][0]["surface_found"] and abs(snap["nodes"][0]["distance"] - 1.0) < 1.0e-3, json.dumps(snap["nodes"][0]))
    editor.call("undo")
    editor.wait_idle()

    # One slot on a placed piece: one undo entry, the other slot kept.
    depth = undo_depth(editor)
    reply = editor.call("level_set_piece_material", {"scene_name": scene, "node_id": tree["id"], "slot": "foliage", "material": "Kit Autumn Foliage"})
    editor.wait_idle()
    after = piece_by_id(editor, scene, tree["id"])
    check("level_set_piece_material sets the tree's foliage as one undo entry and keeps its bark",
          reply["changed"] and (after["materials"] == {"bark": "Kit Bark", "foliage": "Kit Autumn Foliage"}) and (undo_depth(editor) == depth + 1) and
          (reply["previous_material"] == "Kit Foliage") and after["follows_kit"],
          f"{after['materials']} undo {undo_depth(editor) - depth}")
    reply = editor.call("level_set_piece_material", {"scene_name": scene, "node_id": tree["id"], "slot": "foliage", "material": "Kit Autumn Foliage"})
    editor.wait_idle()
    check("setting the slot's current material again changes nothing and records no undo entry",
          (not reply["changed"]) and (undo_depth(editor) == depth + 1), json.dumps(reply)[:200])
    count = material_count(editor, scene)
    reply = editor.call("level_set_piece_material", {"scene_name": scene, "node_id": tree["id"], "slot": "bark", "material": "Kit Plaster"})
    editor.wait_idle()
    added = material_count(editor, scene)
    editor.call("undo")
    editor.wait_idle()
    undone = piece_by_id(editor, scene, tree["id"])
    check("a palette material the scene does not hold yet is added with the slot change in one undo entry, which undo takes back",
          (added == count + 1) and (material_count(editor, scene) == count) and
          (undone["materials"] == {"bark": "Kit Bark", "foliage": "Kit Autumn Foliage"}),
          f"materials {count} -> {added} -> {material_count(editor, scene)}, {undone['materials']}")

    # A parameter change keeps the assignments.
    reply = editor.call("level_set_piece_parameters", {"scene_name": scene, "node_id": tree["id"], "parameters": {"segments": 12}})
    editor.wait_idle()
    changed = piece_by_id(editor, scene, tree["id"])
    check("a parameter change of the tree keeps the material of each slot",
          reply["changed"] and (reply["materials"] == changed["materials"] == {"bark": "Kit Bark", "foliage": "Kit Autumn Foliage"}) and
          changed["follows_kit"] and (changed["mesh_key"] != tree["mesh_key"]),
          f"{changed['materials']} {changed['mesh_key'][:60]}")
    editor.call("undo")
    editor.wait_idle()

    # A layout made from the piece and a new layout with item materials.
    made = editor.call("level_create_layout", {"scene_name": scene, "from_piece_id": chosen["id"], "pattern": {"kind": "line", "count": 3, "direction": [0.0, 0.0, 1.0], "step": 3.0}})
    editor.wait_idle()
    check("a layout made from the chosen tree gives each of its pieces the tree's slot materials",
          (made["child_count"] == 3) and all(c["materials"] == chosen["materials"] for c in made["children"]), json.dumps([c["materials"] for c in made["children"]]))
    editor.call("undo")
    editor.wait_idle()
    run = editor.call("level_create_layout", {"scene_name": scene, "position": [-6.0, top, 4.0], "items": [{"shape": "nature.tree", "materials": {"foliage": "Kit Grass"}}],
                                              "pattern": {"kind": "line", "count": 2, "step": 3.0}})
    editor.wait_idle()
    check("a new tree layout with item materials {foliage: Kit Grass} keeps the default bark",
          all(c["materials"] == {"bark": "Kit Bark", "foliage": "Kit Grass"} for c in run["children"]), json.dumps([c["materials"] for c in run["children"]]))

    # Duplicate, mirror and paste keep them.
    copy = editor.call("level_duplicate_group", {"scene_name": scene, "group_id": chosen["id"], "offset": [0.0, 0.0, -4.0]})
    editor.wait_idle()
    copy_piece = piece_by_id(editor, scene, copy["id"])
    check("a duplicate of the chosen tree keeps its slot materials, mesh and collision shape",
          (copy_piece["materials"] == chosen["materials"]) and (copy_piece["brush_id"] == chosen["brush_id"]) and copy_piece["follows_kit"] and
          (copy_piece["physics"]["collision_shape"] == piece_by_id(editor, scene, chosen["id"])["physics"]["collision_shape"]),
          f"{copy_piece['materials']} {copy_piece['physics']['collision_shape']}")
    group = editor.call("level_group_nodes", {"scene_name": scene, "node_ids": [chosen["id"], copy["id"]], "name": "m trees"})
    editor.wait_idle()
    editor.call("level_mirror_group", {"scene_name": scene, "group_id": group["id"], "axis": "x"})
    editor.wait_idle()
    mirrored = pieces_by_id(editor, scene)
    check("a mirrored group of trees keeps each tree's slot materials",
          all(mirrored[i]["materials"] == chosen["materials"] and mirrored[i]["follows_kit"] for i in (chosen["id"], copy["id"])), "")
    known = set(pieces_by_id(editor, scene))
    editor.call("clipboard_copy_nodes", {"scene_name": scene, "node_ids": [chosen["id"]]})
    editor.call("clipboard_paste", {"scene_name": scene})
    editor.wait_idle()
    pasted = [p for p in pieces(editor, scene) if p["id"] not in known]
    check("a pasted tree keeps its slot materials, mesh and triangle mesh collision shape",
          (len(pasted) == 1) and (pasted[0]["materials"] == chosen["materials"]) and pasted[0]["follows_kit"] and
          (pasted[0]["physics"]["collision_shape"] == mirrored[chosen["id"]]["physics"]["collision_shape"]) and pasted[0]["physics"]["has_body"],
          json.dumps([(p["materials"], p["physics"]) for p in pasted]))

    # Refusals change nothing.
    count = len(pieces(editor, scene))
    depth = undo_depth(editor)
    for tool, label, arguments, needle in [
        ("level_place_pieces", "an unknown slot", {"pieces": [{"shape": "nature.tree", "materials": {"leaves": "Kit Foliage"}}]}, "no material slot"),
        ("level_place_pieces", "an unknown material", {"pieces": [{"shape": "nature.tree", "materials": {"bark": "No Such Material"}}]}, "no material"),
        ("level_place_pieces", "materials that are not an object", {"pieces": [{"shape": "nature.tree", "materials": ["Kit Bark"]}]}, "materials"),
        ("level_place_pieces", "the single material key", {"pieces": [{"shape": "nature.tree", "material": "Kit Bark"}]}, "material"),
        ("level_create_layout", "items with from_piece_id", {"from_piece_id": tree["id"], "items": [{"shape": "nature.tree", "materials": {"bark": "Kit Wood"}}], "pattern": {"kind": "line"}}, "not both"),
        ("level_set_piece_material", "an unknown slot", {"node_id": tree["id"], "slot": "stone", "material": "Kit Stone"}, "no material slot"),
        ("level_set_piece_material", "an unknown material", {"node_id": tree["id"], "slot": "bark", "material": "No Such Material"}, "no material"),
        ("level_set_piece_material", "no slot", {"node_id": tree["id"], "material": "Kit Stone"}, "slot"),
        ("level_set_piece_material", "a node that is not a piece", {"node_id": group["id"], "slot": "bark", "material": "Kit Wood"}, "not a kit piece"),
        ("level_set_piece_material", "an unknown argument", {"node_id": tree["id"], "slot": "bark", "material": "Kit Wood", "index": 0}, "index"),
    ]:
        error = call_error(editor, tool, dict({"scene_name": scene}, **arguments))
        check(f"{tool} refuses {label}", (error is not None) and (needle in error), str(error))
    editor.wait_idle()
    check("the refused material calls changed nothing and recorded no undo entry",
          (len(pieces(editor, scene)) == count) and (undo_depth(editor) == depth), f"depth {undo_depth(editor)} vs {depth}")

    # Save and reopen.
    before = {p["name"]: p["materials"] for p in pieces(editor, scene)}
    path = os.path.join(directory, "material_slots_level.gltf")
    before_pieces, materials, name = save_close_load(editor, scene, path)
    check_reloaded("material slots level", editor, before_pieces, materials, name)
    after = {p["name"]: p["materials"] for p in pieces(editor, name)}
    check("material slots level: every piece's slot materials are back by name after save and reopen",
          after == before, f"{[(n, before.get(n), after.get(n)) for n in sorted(set(before) | set(after)) if before.get(n) != after.get(n)][:4]}")
    # Left open, it moves the Transform checks' scene to a wide viewport (level_tools_implementation.md step 20).
    editor.close(name)


def check_window_materials(editor, scene, image_path):
    """The Level Kit window's slot material combos: for the shape to place (preview and placement) and for the selected piece."""
    slab = next(p for p in pieces(editor, scene) if p["name"] == "slab")
    slab_top = slab["world_aabb"]["max"][1]
    click(editor, "Tree")
    state = wait_preview(editor, lambda r: r.get("active") and (r.get("shape") == "nature.tree"))
    labels = {i.get("display_label") or "" for i in imgui_items(editor)}
    check("choosing the Tree row shows its bark material and foliage material rows; the preview takes the slot defaults",
          ({"bark material", "foliage material"} <= labels) and (state.get("materials") == TREE_DEFAULTS), json.dumps(state.get("materials")))
    click(editor, "foliage material")
    editor.call("imgui_click", {"label": "Kit Autumn Foliage"})
    editor.wait_idle()
    state = wait_preview(editor, lambda r: (r.get("materials") or {}).get("foliage") == "Kit Autumn Foliage")
    check("choosing Kit Autumn Foliage in the foliage material row gives the preview that foliage and keeps the bark",
          state.get("materials") == {"bark": "Kit Bark", "foliage": "Kit Autumn Foliage"}, json.dumps(state.get("materials")))

    viewport = next(v for v in editor.call("get_viewports")["viewports"] if v.get("scene") == scene)
    insert_button = binding_input(editor, "Brush_tool.insert", "mouse_button")
    aim = [-2.5, slab_top, 2.5]
    hover(editor, viewport, aim)
    state = wait_preview(editor, lambda r: r.get("visible") and (r.get("shape") == "nature.tree"))
    known = set(pieces_by_id(editor, scene))
    depth = undo_depth(editor)
    x, y = window_point(viewport, aim)
    editor.call("mouse_click", {"x": x, "y": y, "button": insert_button})
    editor.wait_idle()
    added = new_pieces(editor, scene, known)
    placed = added[0] if len(added) == 1 else None
    check("a click places the tree with the window's slot materials, one undo entry",
          (placed is not None) and (placed["materials"] == {"bark": "Kit Bark", "foliage": "Kit Autumn Foliage"}) and (undo_depth(editor) == depth + 1),
          f"{[(p['shape'], p['materials']) for p in added]} undo {undo_depth(editor) - depth}")
    for key in "123456789":
        editor.call("key_press", {"key": key})
        if not wait_preview(editor, lambda r: not r.get("tool_active"), timeout=1.0).get("tool_active"):
            break
    if placed is None:
        return

    # The selected piece's combos: the second bark material row of the window (the first is the shape to place).
    editor.call("select_items", {"scene_name": scene, "ids": [placed["id"]]})
    editor.wait_idle()
    rows = [i for i in imgui_items(editor) if i.get("display_label") == "bark material"]
    check("a selected tree shows its own bark material and foliage material rows under its parameter rows", len(rows) == 2, f"{len(rows)} bark rows")
    depth = undo_depth(editor)
    editor.call("imgui_scroll_to_item", {"window": WINDOW, "label": "bark material", "index": 1})
    editor.call("imgui_click", {"window": WINDOW, "label": "bark material", "index": 1})
    editor.call("imgui_click", {"label": "Kit Wood"})
    editor.wait_idle()
    after = piece_by_id(editor, scene, placed["id"])
    check("choosing Kit Wood in the selected tree's bark row sets its bark as one undo entry and keeps its foliage",
          (after["materials"] == {"bark": "Kit Wood", "foliage": "Kit Autumn Foliage"}) and (undo_depth(editor) == depth + 1),
          f"{after['materials']} undo {undo_depth(editor) - depth}")
    os.makedirs(os.path.dirname(image_path), exist_ok=True)
    editor.call("imgui_scroll_to_item", {"window": WINDOW, "label": "foliage material", "index": 1, "align": "center"})
    editor.wait_idle()
    editor.call("capture_screenshot", {"path": image_path})
    check("capture_screenshot of the Level Kit window's slot material rows is written", os.path.isfile(image_path), image_path)
    print(f"material slots window screenshot: {image_path}", flush=True)

    # A live drag of the tree's segments row in the Properties window keeps the slot materials, mid-drag and after.
    properties = "Properties"
    editor.call("set_window_visibility", {"title": properties, "visible": True, "focus": True})
    editor.wait_idle()
    header = next((i for i in editor.call("get_imgui_items", {"window": properties, "limit": 1000, "visible_only": False, "label_contains": "Kit Piece"}).get("items", [])
                   if i.get("display_label") == "Kit Piece"), None)
    if (header is not None) and not header.get("status", {}).get("opened"):
        editor.call("imgui_scroll_to_item", {"window": properties, "label": "Kit Piece"})
        editor.call("imgui_click", {"window": properties, "label": "Kit Piece"})
        editor.wait_idle()
    facets_start = node_mesh(editor, scene, placed["id"]).get("facet_count")
    depth = undo_depth(editor)
    drag_row(editor, properties, "segments", 60.0)
    editor.wait_idle()
    mid = node_mesh(editor, scene, placed["id"])
    mid_piece = piece_by_id(editor, scene, placed["id"])
    check("mid-drag of the tree's segments row the preview mesh changes and both slots keep their materials",
          (mid.get("facet_count") != facets_start) and (mid.get("materials") == ["Kit Wood", "Kit Autumn Foliage"]) and (undo_depth(editor) == depth) and
          (mid_piece["materials"] == after["materials"]),
          f"facets {facets_start} -> {mid.get('facet_count')}, {mid.get('materials')}, undo {undo_depth(editor) - depth}")
    editor.call("mouse_release", {})
    editor.wait_idle()
    end = piece_by_id(editor, scene, placed["id"])
    check("the release records one entry; the tree follows its kit with both slot materials kept",
          (undo_depth(editor) == depth + 1) and end["follows_kit"] and (end["materials"] == after["materials"]) and
          (end["parameters"]["segments"] != after["parameters"]["segments"]),
          f"undo {undo_depth(editor) - depth}, segments {after['parameters']['segments']} -> {end['parameters']['segments']}, {end['materials']}")
    editor.call("undo")
    editor.call("undo")
    editor.call("undo")
    editor.wait_idle()
    check("three undos take the drag, the bark choice and the placed tree back", placed["id"] not in pieces_by_id(editor, scene), "")
    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.wait_idle()


def check_window_group(editor, scene, image_path):
    """The Level Kit window's Group section: Group Selected, Duplicate, Mirror X and Ungroup on the selected nodes and group."""
    slab = next(p for p in pieces(editor, scene) if p["name"] == "slab")
    top = slab["world_aabb"]["max"][1]
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.block", "position": [-4.0, top, -3.0], "name": "wg a"},
        {"shape": "basic.wedge", "position": [-2.0, top, -3.0], "name": "wg b"},
    ]})["pieces"]
    editor.wait_idle()
    ids = [p["id"] for p in placed]
    depth_start = undo_depth(editor)
    editor.call("select_items", {"scene_name": scene, "ids": ids})
    editor.wait_idle()
    wanted = ["Group", "Group Name", "Group Selected"]
    missing = [label for label in wanted if not any((i.get("display_label") == label) for i in imgui_items(editor, label))]
    check("the Level Kit window shows the Group section with Group Name and Group Selected", not missing, f"missing {missing}")
    editor.call("imgui_scroll_to_item", {"window": WINDOW, "label": "Group Name"})
    editor.call("imgui_click", {"window": WINDOW, "label": "Group Name"})
    editor.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
    editor.call("type_text", {"text": "window group"})
    editor.call("key_press", {"key": "enter"})
    editor.wait_idle()
    click(editor, "Group Selected")
    editor.wait_idle()
    tree = scene_tree(editor, scene)
    group = next((n for n in tree.values() if n["name"] == "window group"), None)
    selection = editor.call("get_selection")
    check("Group Selected puts the selected pieces under a new group named in Group Name, one undo entry, and selects the group",
          (group is not None) and all(tree[i]["parent_id"] == group["id"] for i in ids) and (undo_depth(editor) == depth_start + 1) and
          (selection.get("active_item", {}).get("id") == (group or {}).get("id")),
          f"group {group and group['id']}, active {selection.get('active_item')}")
    if group is None:
        return
    # Duplicate Offset is a DragFloat3, whose label is no item of its own; the Duplicate check below uses its default.
    wanted = ["Ungroup", "Duplicate", "Mirror X", "Mirror Z"]
    missing = [label for label in wanted if not any((i.get("display_label") == label) for i in imgui_items(editor, label))]
    check("with the group selected the section shows Ungroup, Duplicate, Mirror X and Mirror Z", not missing, f"missing {missing}")
    before = pieces_by_id(editor, scene)
    width = 3.0  # block at x -4 (width 1) to wedge at x -2 (width 1): -4.5 .. -1.5
    click(editor, "Duplicate")
    editor.wait_idle()
    now = pieces_by_id(editor, scene)
    copies = sorted((p for i, p in now.items() if i not in before), key=lambda p: p["name"])
    check("Duplicate copies the group by its width along x as one undo entry, the copies full pieces sharing the originals' meshes",
          (len(copies) == 2) and all(close(c["position"], [before[o]["position"][0] + width] + before[o]["position"][1:]) and
                                     (c["brush_id"] == before[o]["brush_id"]) and c["physics"]["has_body"] for c, o in zip(copies, ids)) and
          (undo_depth(editor) == depth_start + 2),
          "; ".join(f"{c['name']} {fmt3(c['position'])}" for c in copies))
    copy_group = editor.call("get_selection").get("active_item", {})
    copy_origin = editor.call("get_node_details", {"scene_name": scene, "node_id": copy_group.get("id")})["world_transform"]["translation"]
    click(editor, "Mirror X")
    editor.wait_idle()
    mirrored = pieces_by_id(editor, scene)
    check("Mirror X reflects the selected copy's pieces across its x plane, one undo entry",
          all(close(mirrored[c["id"]]["position"], reflect_point(c["position"], copy_origin, 0)) for c in copies) and (undo_depth(editor) == depth_start + 3),
          "; ".join(f"{mirrored[c['id']]['name']} {fmt3(mirrored[c['id']]['position'])}" for c in copies))
    os.makedirs(os.path.dirname(image_path), exist_ok=True)
    editor.call("imgui_scroll_to_item", {"window": WINDOW, "label": "Mirror X", "align": "center"})
    editor.wait_idle()
    editor.call("capture_screenshot", {"path": image_path})
    check("capture_screenshot of the Level Kit window's Group section is written", os.path.isfile(image_path), image_path)
    print(f"group window screenshot: {image_path}", flush=True)
    click(editor, "Ungroup")
    editor.wait_idle()
    tree = scene_tree(editor, scene)
    now = pieces_by_id(editor, scene)
    check("Ungroup removes the copy's group node, its pieces keep their world transforms under the scene root, one undo entry",
          (copy_group.get("id") not in tree) and all(tree[c["id"]]["parent_id"] == slab_parent(tree, slab) for c in copies) and
          all(close(now[c["id"]]["position"], mirrored[c["id"]]["position"]) for c in copies) and (undo_depth(editor) == depth_start + 4), "")
    for _ in range(4):
        editor.call("undo")
    editor.wait_idle()
    tree = scene_tree(editor, scene)
    check("four undos leave the window scene with the two pieces and no group", (group["id"] not in tree) and all(i in tree for i in ids), "")
    editor.call("delete_nodes", {"scene_name": scene, "ids": ids})
    editor.call("select_items", {"scene_name": scene, "ids": []})
    editor.wait_idle()


def slab_parent(tree, slab):
    return tree[slab["id"]]["parent_id"]


def check_walkable_height_map(editor, scene, seed_point, upper_center, floor_top, upper_top):
    """D7.2 with a navigation mesh: walkable flags and island ids along a strip from the upper slab to the floor."""
    cell = 0.5
    min_z = upper_center[2] - 0.5
    max_z = seed_point[2] + 0.5
    hm = editor.call("level_get_height_map", {"scene_name": scene, "min": [-0.5, -1.0, min_z], "max": [0.5, upper_top + 2.0, max_z],
                                              "cell_size": cell, "text": True})
    upper = editor.call("navigation_find_point", {"scene_name": scene, "position": upper_center})
    floor = editor.call("navigation_find_point", {"scene_name": scene, "position": seed_point})
    first, last = 0, hm["rows"] - 1
    check("a height map with a navigation mesh reports navigation_available, walkable rows, island rows and the island picture",
          hm.get("navigation_available") and (not hm.get("navigation_stale")) and (len(hm.get("walkable", [])) == hm["rows"]) and
          (len(hm.get("island_text", [])) == hm["rows"]), json.dumps({k: hm.get(k) for k in ("navigation_available", "navigation_stale", "walkable_count")}))
    check("the strip's first row is the upper slab: its top height, walkable, the upper slab's island",
          all(abs(h - upper_top) < 0.01 for h in hm["heights"][first]) and all(hm["walkable"][first]) and
          all(i == upper.get("island") for i in hm["islands"][first]),
          f"heights {hm['heights'][first]}, walkable {hm['walkable'][first]}, islands {hm['islands'][first]}, upper island {upper.get('island')}")
    check("the strip's last row is the floor: its top height, walkable, the floor's island, another island than the upper slab's",
          all(abs(h - floor_top) < 0.01 for h in hm["heights"][last]) and all(hm["walkable"][last]) and
          all(i == floor.get("island") for i in hm["islands"][last]) and (floor.get("island") != upper.get("island")),
          f"heights {hm['heights'][last]}, walkable {hm['walkable'][last]}, islands {hm['islands'][last]}, floor island {floor.get('island')}")
    print("walkable strip island picture:\n" + "\n".join(hm.get("island_text", [])), flush=True)


def check_window_queries(editor, scene, image_path):
    """The Level Kit window's Support Report, Region Contents and Height Map buttons over the selection."""
    slab = next(p for p in pieces(editor, scene) if p["name"] == "slab")
    slab_top = slab["world_aabb"]["max"][1]
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.block", "position": [-3.0, slab_top, 3.0], "name": "query resting block"},
        {"shape": "basic.block", "position": [-3.0, slab_top + 2.0, -3.0], "name": "query floating block"},
        {"shape": "basic.block", "position": [slab["world_aabb"]["max"][0] + 0.2, slab_top, 0.0], "name": "query edge block"},
    ]})["pieces"]
    editor.wait_idle()
    resting, floating, edge = placed
    viewport = next(v for v in editor.call("get_viewports")["viewports"] if v.get("scene") == scene)
    hover(editor, viewport, [0.0, slab_top, 0.0])
    labels = {i.get("display_label") or "" for i in imgui_items(editor)}
    check("the Level Kit window shows the Spatial Queries buttons",
          {"Spatial Queries", "Support Report", "Region Contents", "Height Map"} <= labels, f"missing {sorted({'Support Report', 'Region Contents', 'Height Map'} - labels)}")

    def selected_ids():
        editor.wait_idle()
        return sorted(i["id"] for i in editor.call("get_selection").get("items", []))

    editor.call("select_items", {"scene_name": scene, "ids": [resting["id"], floating["id"]]})
    editor.wait_idle()
    click(editor, "Support Report")
    editor.wait_idle()
    click(editor, "Select Unsupported")
    check("Support Report over the two selected blocks, then Select Unsupported, selects the floating block alone",
          selected_ids() == [floating["id"]], f"{selected_ids()}")

    editor.call("select_items", {"scene_name": scene, "ids": [resting["id"]]})
    editor.wait_idle()
    click(editor, "Support Report")
    editor.wait_idle()
    report_labels = {i.get("display_label") or "" for i in imgui_items(editor)}
    check("Support Report over the resting block alone lists nothing unsupported (no Select Unsupported button, no entry)",
          ("Select Unsupported" not in report_labels) and ("query floating block" not in report_labels), "")
    click(editor, "Region Contents")
    editor.wait_idle()
    region_labels = {i.get("display_label") or "" for i in imgui_items(editor)}
    check("Region Contents over the resting block lists it and the slab it touches, not the floating block",
          ("query resting block" in region_labels) and ("query floating block" not in region_labels), "")
    editor.call("select_items", {"scene_name": scene, "ids": [floating["id"]]})
    editor.wait_idle()
    click(editor, "query resting block")
    check("clicking a Region Contents entry selects that mesh", selected_ids() == [resting["id"]], f"{selected_ids()}")

    # Over the block hanging past the slab's edge the grown bounds have cells on the slab, on the block and past both.
    editor.call("select_items", {"scene_name": scene, "ids": [edge["id"]]})
    editor.wait_idle()
    click(editor, "Height Map")
    editor.wait_idle()
    hm = editor.call("level_get_height_map", {"scene_name": scene, "min": [edge["world_aabb"]["min"][0] - 1.0, edge["world_aabb"]["min"][1] - 1.0, -1.5],
                                              "max": [edge["world_aabb"]["max"][0] + 1.0, edge["world_aabb"]["max"][1] + 1.0, 1.5], "cell_size": 1.0, "text": True})
    picture = "".join(hm.get("height_text", []))
    check("the window's height map region (the edge block's bounds grown by one module) has hit and no-hit cells",
          ("." in picture) and any(c.isdigit() for c in picture), "\n" + "\n".join(hm.get("height_text", [])))
    check_window_overlaps(editor, scene, slab_top, selected_ids)

    os.makedirs(os.path.dirname(image_path), exist_ok=True)
    editor.call("imgui_scroll_to_item", {"window": WINDOW, "label": "Level Check", "align": "center"})
    editor.wait_idle()
    editor.call("capture_screenshot", {"path": image_path})
    check("capture_screenshot of the Level Kit window's spatial query results is written", os.path.isfile(image_path), image_path)
    print(f"spatial queries window screenshot: {image_path}", flush=True)


def check_window_overlaps(editor, scene, slab_top, selected_ids):
    """The Level Kit window's Overlap Report, Exclude Tags and Level Check; a row click selects the row's pieces."""
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [
        {"shape": "basic.block", "position": [2.0, slab_top, 3.0], "name": "query overlap a"},
        {"shape": "basic.block", "position": [2.4, slab_top, 3.0], "name": "query overlap b"},
    ]})["pieces"]
    editor.wait_idle()
    first, second = placed
    pair = sorted([first["id"], second["id"]])
    labels = {i.get("display_label") or "" for i in imgui_items(editor)}
    check("the Level Kit window shows Exclude Tags, Overlap Report and Level Check",
          {"Exclude Tags", "Overlap Report", "Level Check"} <= labels, f"missing {sorted({'Exclude Tags', 'Overlap Report', 'Level Check'} - labels)}")
    editor.call("select_items", {"scene_name": scene, "ids": pair})
    editor.wait_idle()
    click(editor, "Overlap Report")
    editor.wait_idle()
    rows = [i.get("display_label") for i in imgui_items(editor, "query overlap a - query overlap b")]
    check("Overlap Report over the two selected blocks lists them with their 0.6 m depth",
          rows == ["query overlap a - query overlap b: 0.600 m"], f"{rows}")
    editor.call("select_items", {"scene_name": scene, "ids": [first["id"]]})
    editor.wait_idle()
    if rows:
        click(editor, rows[0])
    check("clicking the overlap row selects both blocks", selected_ids() == pair, f"{selected_ids()}")

    editor.call("add_tags", {"scene_name": scene, "ids": [first["id"]], "tags": ["window skip"]})
    editor.call("imgui_scroll_to_item", {"window": WINDOW, "label": "Exclude Tags"})
    editor.call("imgui_click", {"window": WINDOW, "label": "Exclude Tags"})
    editor.call("type_text", {"text": "window skip"})
    editor.call("key_press", {"key": "enter"})
    editor.wait_idle()
    click(editor, "Overlap Report")
    editor.wait_idle()
    rows = [i.get("display_label") for i in imgui_items(editor, "query overlap a - query overlap b")]
    check("with Exclude Tags 'window skip' (block a carries it) the Overlap Report lists nothing", rows == [], f"rows {rows}")
    editor.call("imgui_click", {"window": WINDOW, "label": "Exclude Tags"})
    editor.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
    editor.call("key_press", {"key": "backspace"})
    editor.call("key_press", {"key": "enter"})
    editor.wait_idle()
    click(editor, "Overlap Report")
    editor.wait_idle()
    rows = [i.get("display_label") for i in imgui_items(editor, "query overlap a - query overlap b")]
    check("with Exclude Tags cleared the Overlap Report lists the pair again", len(rows) == 1, f"rows {rows}")

    click(editor, "Level Check")
    editor.wait_idle()
    expected = editor.call("level_check", {"scene_name": scene})
    finding_rows = [i.get("display_label") or "" for i in imgui_items(editor) if (i.get("display_label") or "").split(":")[0] in expected["counts"]]
    overlap_rows = [label for label in finding_rows if label.startswith("overlap: query overlap a and query overlap b")]
    check("Level Check lists the findings level_check returns, the two blocks' overlap among them",
          (len(finding_rows) == expected["finding_count"]) and (len(overlap_rows) == 1), f"{len(finding_rows)} rows, {expected['finding_count']} findings: {finding_rows[:8]}")
    editor.call("select_items", {"scene_name": scene, "ids": [second["id"]]})
    editor.wait_idle()
    if overlap_rows:
        click(editor, overlap_rows[0])
    check("clicking a Level Check row selects its pieces", selected_ids() == pair, f"{selected_ids()}")


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
          material_count(editor, name) <= materials_before and all(p["materials"] and all(p["materials"].values()) for p in after),
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
          (material_count(editor, name) == materials) and (placed["pieces"][0]["materials"] == stairs["materials"]),
          f"{materials} -> {material_count(editor, name)}, materials {placed['pieces'][0]['materials']}")
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
    # The item may sit outside its window's scrolled view, where it is not submitted.
    editor.call("imgui_scroll_to_item", {"window": entry["window"], "id": entry["id"], "align": "nearest"})
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
    parser.add_argument("--queries-image", default=DEFAULT_QUERIES_IMAGE, help="spatial queries window screenshot PNG (relative to the repository root or absolute)")
    parser.add_argument("--layout-image", default=DEFAULT_LAYOUT_IMAGE, help="Layout section screenshot PNG (relative to the repository root or absolute)")
    parser.add_argument("--group-image", default=DEFAULT_GROUP_IMAGE, help="Group section screenshot PNG (relative to the repository root or absolute)")
    parser.add_argument("--materials-image", default=DEFAULT_MATERIALS_IMAGE, help="slot material rows screenshot PNG (relative to the repository root or absolute)")
    parser.add_argument("--only-walkable", action="store_true", help="run the walkable level, persistence and window checks alone")
    parser.add_argument("--only-transform", action="store_true", help="run the surface settings and Transform tool checks alone")
    parser.add_argument("--only-layouts", action="store_true", help="run the kit layout node checks alone")
    parser.add_argument("--only-window-layout", action="store_true", help="run the Level Kit window checks up to its Layout section alone")
    args = parser.parse_args()
    image_path = args.image if os.path.isabs(args.image) else os.path.join(pav.REPO_ROOT, args.image)
    surface_image_path = args.surface_image if os.path.isabs(args.surface_image) else os.path.join(pav.REPO_ROOT, args.surface_image)
    walkable_image_path = args.walkable_image if os.path.isabs(args.walkable_image) else os.path.join(pav.REPO_ROOT, args.walkable_image)
    window_image_path = args.window_image if os.path.isabs(args.window_image) else os.path.join(pav.REPO_ROOT, args.window_image)
    properties_image_path = args.properties_image if os.path.isabs(args.properties_image) else os.path.join(pav.REPO_ROOT, args.properties_image)
    rock_image_path = args.rock_image if os.path.isabs(args.rock_image) else os.path.join(pav.REPO_ROOT, args.rock_image)
    queries_image_path = args.queries_image if os.path.isabs(args.queries_image) else os.path.join(pav.REPO_ROOT, args.queries_image)
    layout_image_path = args.layout_image if os.path.isabs(args.layout_image) else os.path.join(pav.REPO_ROOT, args.layout_image)
    group_image_path = args.group_image if os.path.isabs(args.group_image) else os.path.join(pav.REPO_ROOT, args.group_image)
    materials_image_path = args.materials_image if os.path.isabs(args.materials_image) else os.path.join(pav.REPO_ROOT, args.materials_image)
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
        elif args.only_layouts:
            check_layout_patterns(editor)
            check_layouts(editor, work)
            check_layout_tree(editor)
        elif args.only_window_layout:
            window_scene, _ = check_level_kit_window(editor, window_image_path)
            check_window_layout(editor, window_scene, layout_image_path)
            check_properties_layout_write(editor, window_scene)
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
                check_spatial_queries(editor)
                check_overlap_report(editor)
                check_level_check(editor)
                check_layout_patterns(editor)
                check_groups(editor, work)
                check_material_slots(editor, work)
                check_layouts(editor, work)
                check_layout_tree(editor)
            walkable_scene = check_walkable_level(editor, walkable_image_path)
            check_persistence(editor, scene, walkable_scene, work)
            window_scene, stairs = check_level_kit_window(editor, window_image_path)
            check_properties_rows(editor, window_scene, stairs, properties_image_path)
            check_from_seed_toggle(editor, window_scene)
            check_live_drags(editor, window_scene, stairs, editor.call("level_get_kits", {"scene_name": window_scene}))
            check_window_queries(editor, window_scene, queries_image_path)
            check_window_layout(editor, window_scene, layout_image_path)
            check_properties_layout_write(editor, window_scene)
            check_window_group(editor, window_scene, group_image_path)
            check_window_materials(editor, window_scene, materials_image_path)
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
