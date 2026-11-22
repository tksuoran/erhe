#!/usr/bin/env python3
"""Detail blend weight: headless checks of doc/plans/detail_blend_weight.md.

Groups (doc/plans/detail_blend_weight_design.md BD9.1); without an --only-*
flag every group runs:

  --only-material  the material values (B1.1, B2.1 - B2.3) and the detail_blend
                   texture slot (B2.2): defaults, set through edit_material and
                   create_material, read back through get_material_details and
                   get_item_properties, one undo entry per edit, the Properties
                   window's "Detail Blend" group, a scene save and load (a plain
                   texture and a Graph Texture in the slot), read back again; the
                   Graph Texture under Textures/Graph Textures, its bake in no Scene
                   Hierarchy row and no Height Texture picker entry, a pick and a drop
                   of the Graph Texture on the row, the pick through save, close, load.
  --only-paint     channel painting (B7.1 - B7.3, B7.6): fill_vertex_channel per channel,
                   paint_vertex_channel strokes (mix, subtract, add) equal to a CPU model of
                   the channel brush within 1e-5 with the other channels bit-identical,
                   invert_vertex_channel, one undo entry each and undo restoring, refusals.
  --only-shader    the "Detail Blend Weight" debug mode (B5, B8.3) through render_scene_image
                   (linear, rgba32f): weight 0 black and 1 white with a height texture bound,
                   height influence 0 the plain weight, the checker boundary of a weight gradient
                   at transition width 0 equal to the CPU formula of the measured weight and
                   height outside a one-pixel band, detail blend off black, the surface noise
                   (not constant; world and node space differ once the node moves) and B1.3
                   (no vertex color tint on a detail blend material outside the debug mode).
  --only-tool      the Paint tool's Brush mode (B7, BD7.2 - BD7.8, BD8.5) through the input MCP tools:
                   the settings tools, a left drag painting the weight channel only as one undo entry
                   (live before the release), undo, Escape mid-drag, the hovered material's weight
                   channel, the Show Detail Weight switch in a capture (live mid-drag), Shift + wheel.

    py -3 scripts/detail_blend_verify.py [--editor <path>] [--cwd <dir>] [--only-material] [--only-paint] [--only-shader] [--only-tool]

Launches the headless editor (prepared_asset_verify.Editor: deterministic
clock, default layout) and stops it, by its pid, at the end. One PASS/FAIL
line per check, exit code 1 when any check fails.
"""

import argparse
import math
import os
import shutil
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import prepared_asset_verify as pav  # noqa: E402
from prepared_asset_verify import Editor, check  # noqa: E402

DEFAULT_EDITOR = "build_vs2026_vulkan_headless/bin/Debug/editor.exe" if sys.platform == "win32" else "build_ninja_linux_vulkan_headless/bin/editor"
TOLERANCE = 1.0e-5

# The defaults of doc/plans/detail_blend_weight_design.md BD1.1.
DEFAULTS = {
    "detail_blend":                  False,
    "detail_blend_weight_channel":   "r",
    "detail_blend_height_channel":   "r",
    "detail_blend_height_influence": 1.0,
    "detail_blend_transition_width": 0.2,
    "detail_blend_noise_amount":     0.0,
    "detail_blend_noise_scale":      1.0,
    "detail_blend_noise_space":      "node",
}
# Values that differ from every default.
VALUES = {
    "detail_blend":                  True,
    "detail_blend_weight_channel":   "g",
    "detail_blend_height_channel":   "a",
    "detail_blend_height_influence": 0.75,
    "detail_blend_transition_width": 0.05,
    "detail_blend_noise_amount":     0.25,
    "detail_blend_noise_scale":      4.0,
    "detail_blend_noise_space":      "world",
}
SLOT_EDIT = {"rotation": 0.5, "offset": [0.25, -0.125], "scale": [4.0, 2.0], "wrap": "mirrored_repeat"}


def same(a, b):
    if isinstance(a, float) or isinstance(b, float):
        return (a is not None) and (b is not None) and abs(float(a) - float(b)) <= TOLERANCE
    if isinstance(a, list) and isinstance(b, list):
        return (len(a) == len(b)) and all(same(x, y) for x, y in zip(a, b))
    return a == b


def values_of(details):
    return {key: details.get(key) for key in DEFAULTS}


def mismatches(actual, expected):
    return [f"{key}: {actual.get(key)} != {value}" for key, value in expected.items() if not same(actual.get(key), value)]


class Session:
    def __init__(self, editor: Editor, work_dir: str) -> None:
        self.e = editor
        self.work_dir = work_dir
        self.scene = None

    def details(self, material, scene=None):
        return self.e.call("get_material_details", {"scene_name": scene or self.scene, "material_name": material})

    def undo_depth(self):
        return len(self.e.call("get_undo_redo_stack")["undo"])


def write_source_scene(directory):
    """detail_source.gltf: one box whose material samples checker.png, so the texture is in the content library."""
    builder = pav.Gltf_builder()
    checker = [[c for x in range(8) for c in ((255, 255, 255, 255) if ((x // 2 + y // 2) % 2) == 0 else (0, 0, 0, 255))] for y in range(8)]
    with open(os.path.join(directory, "checker.png"), "wb") as handle:
        handle.write(pav.png_bytes(8, 8, checker))
    document = {
        "asset": {"version": "2.0", "generator": "detail_blend_verify.py"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Detail Box", "mesh": 0}],
        "meshes": [{"name": "box", "primitives": [builder.primitive(pav.make_box(0.5, 0.5, 0.5), 0)]}],
        "materials": [{"name": "Checker", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9986, "wrapS": 10497, "wrapT": 10497}],
        "images": [{"uri": "checker.png"}],
        "accessors": builder.accessors, "bufferViews": builder.views
    }
    binary = builder.padded_binary()
    document["buffers"] = [{"uri": "detail_source.bin", "byteLength": len(binary)}]
    with open(os.path.join(directory, "detail_source.bin"), "wb") as handle:
        handle.write(binary)
    path = os.path.join(directory, "detail_source.gltf")
    with open(path, "w", encoding="utf-8") as handle:
        pav.json.dump(document, handle)
    return path


def check_slot(label, slot, texture_name=None, graph_texture_name=None, transform_kept=True):
    if texture_name is not None:
        check(f"{label}: the slot holds texture '{texture_name}'", slot.get("texture_name") == texture_name, str(slot.get("texture_name")))
    if graph_texture_name is not None:
        check(f"{label}: the slot holds Graph Texture '{graph_texture_name}'", slot.get("graph_texture_name") == graph_texture_name,
              str(slot.get("graph_texture_name")))
    if not transform_kept:
        return
    transform = {"rotation": slot.get("rotation"), "offset": slot.get("offset"), "scale": slot.get("scale")}
    expected  = {"rotation": SLOT_EDIT["rotation"], "offset": SLOT_EDIT["offset"], "scale": SLOT_EDIT["scale"]}
    problems = mismatches(transform, expected)
    check(f"{label}: the slot keeps its UV transform", not problems, "; ".join(problems))


BAKE_NAME = "Height Bake"  # the output node's texture name: the Graph Texture's internal bake


def imgui_items(e, **arguments):
    arguments.setdefault("visible_only", False)
    arguments.setdefault("limit", 3000)
    return e.call("get_imgui_items", arguments).get("items", [])


def window_with(e, label, prefix):
    """The ImGui window, named with prefix, that submits an item with this label (None when none does)."""
    for _ in range(60):
        for item in imgui_items(e, label_contains=label):
            if (item.get("label") == label) and str(item.get("window", "")).startswith(prefix):
                return item["window"]
        e.wait_frames(1)
    return None


def properties_window_of(e, scene, material):
    """Opens the Properties window on a material, with its "Detail Blend" group open; its window name."""
    e.call("open_properties_window", {"scene_name": scene, "material": material})
    e.wait_frames(3)
    for _ in range(4):
        items = [i for i in imgui_items(e) if str(i.get("window", "")).startswith("Properties")]
        # A row with a local value is labelled "* <label>".
        picks = [i for i in items if i.get("label") in ("Height Texture.pick", "* Height Texture.pick")]
        if picks:
            e.call("imgui_scroll_to_item", {"window": picks[0]["window"], "id": picks[0]["id"]})
            e.wait_frames(2)
            return picks[0]["window"]
        groups = [i["window"] for i in items if i.get("label") == "Detail Blend"]
        if not groups:
            e.wait_frames(1)
            continue
        e.call("imgui_scroll_to_item", {"window": groups[0], "label": "Detail Blend"})
        e.wait_frames(2)
        e.call("imgui_click", {"window": groups[0], "label": "Detail Blend"})
        e.wait_frames(3)
    return None


def open_picker(e, window):
    """Opens the Height Texture picker popup; its entries (label, id)."""
    pick = [i for i in imgui_items(e, window=window) if i.get("label") in ("Height Texture.pick", "* Height Texture.pick")]
    e.call("imgui_click", {"window": window, "id": pick[0]["id"]})
    e.wait_frames(3)
    return [(i.get("label"), i.get("id")) for i in imgui_items(e, visible_only=True) if str(i.get("window", "")).startswith("##Popup")]


def open_tree_row(e, window, label):
    """Opens a Scene Hierarchy row by a click on its arrow."""
    rows = [i for i in imgui_items(e, window=window, label_contains=label) if i.get("label") == label]
    if not rows or rows[0]["status"].get("opened"):
        return
    rect = e.call("get_imgui_item_rect", {"window": window, "label": label})
    point = [rect["x"] + 8, rect["center_y"]]
    e.call("mouse_drag", {"from": point, "to": point, "frames": 1})
    e.wait_frames(3)


def group_material(s: Session):
    print("\n== --only-material: values and the detail_blend slot ==")
    e = s.e
    source = write_source_scene(s.work_dir)
    s.scene = e.open_foreign(source)
    check("setup: the source scene loads", s.scene is not None, str(s.scene))
    textures = e.call("get_scene_textures", {"scene_name": s.scene}).get("textures", [])
    texture_names = [t.get("name") for t in textures]
    check("setup: the checker texture is in the content library", len(texture_names) >= 1, str(texture_names))
    if not texture_names:
        return
    texture_name = texture_names[0]

    # 1. Defaults on a new material
    e.call("create_material", {"scene_name": s.scene, "name": "Detail Defaults"})
    e.wait_idle()
    defaults = s.details("Detail Defaults")
    problems = mismatches(values_of(defaults), DEFAULTS)
    check("1.1 a new material has the default detail blend values", not problems, "; ".join(problems))
    slot = defaults.get("texture_samplers", {}).get("detail_blend")
    check("1.2 get_material_details lists the detail_blend slot, empty", (slot is not None) and (slot.get("texture_id") is None), str(slot))

    # 2. edit_material: every value and the slot, one undo entry
    depth = s.undo_depth()
    arguments = {"scene_name": s.scene, "material_name": "Detail Defaults",
                 "texture_samplers": {"detail_blend": dict(SLOT_EDIT, texture=texture_name)}}
    arguments.update(VALUES)
    reply = e.call("edit_material", arguments)
    e.wait_idle()
    check("2.1 edit_material reports a change", reply.get("changed") is True, str(reply.get("applied")))
    edited = s.details("Detail Defaults")
    problems = mismatches(values_of(edited), VALUES)
    check("2.2 get_material_details reads the edited values back", not problems, "; ".join(problems))
    check_slot("2.3", edited["texture_samplers"]["detail_blend"], texture_name=texture_name)
    check("2.4 the edit is one undo entry", s.undo_depth() == depth + 1, f"{depth} -> {s.undo_depth()}")
    e.call("undo")
    e.wait_idle()
    undone = s.details("Detail Defaults")
    problems = mismatches(values_of(undone), DEFAULTS)
    check("2.5 undo restores the defaults", not problems, "; ".join(problems))
    check("2.6 undo empties the slot", undone["texture_samplers"]["detail_blend"].get("texture_id") is None)
    e.call("redo")
    e.wait_idle()
    problems = mismatches(values_of(s.details("Detail Defaults")), VALUES)
    check("2.7 redo applies the values again", not problems, "; ".join(problems))

    refusal = e.refusal("edit_material", {"scene_name": s.scene, "material_name": "Detail Defaults", "detail_blend_weight_channel": "none"})
    check("2.8 a weight channel outside r, g, b, a is refused", "must be one of" in str(refusal), str(refusal)[:200])

    # 3. The registered properties (the Properties window rows and the generic property tools)
    properties = e.call("get_item_properties", {"scene_name": s.scene, "item_name": "Detail Defaults"}).get("properties", [])
    by_name = {p.get("name"): p for p in properties}
    expected = list(DEFAULTS) + ["detail_blend_texture", "detail_blend_texture_uv_scale", "detail_blend_texture_wrap_u"]
    missing = [name for name in expected if name not in by_name]
    check("3.1 the material registers the detail blend properties and the slot", not missing, ", ".join(missing))
    check("3.2 the edited values are local", all(by_name.get(name, {}).get("source") == "local" for name in VALUES),
          ", ".join(f"{name}={by_name.get(name, {}).get('source')}" for name in VALUES))
    depth = s.undo_depth()
    e.call("set_item_property", {"scene_name": s.scene, "item_name": "Detail Defaults", "property": "detail_blend_transition_width", "value": "0.5"})
    e.wait_idle()
    check("3.3 set_item_property writes a detail value", same(s.details("Detail Defaults").get("detail_blend_transition_width"), 0.5))
    check("3.4 the property write is one undo entry", s.undo_depth() == depth + 1, f"{depth} -> {s.undo_depth()}")
    e.call("undo")
    e.wait_idle()
    check("3.5 undo restores it", same(s.details("Detail Defaults").get("detail_blend_transition_width"), VALUES["detail_blend_transition_width"]))

    e.call("open_properties_window", {"scene_name": s.scene, "material": "Detail Defaults"})
    found = []
    for _ in range(60):
        found = e.call("get_imgui_items", {"label_contains": "Detail Blend", "visible_only": False, "limit": 50}).get("items", [])
        if found:
            break
        e.wait_frames(1)
    check("3.6 the Properties window shows a 'Detail Blend' group", bool(found), ", ".join(sorted({str(i.get("label")) for i in found}))[:200])

    # 4. create_material carries the values; a Graph Texture in the slot
    arguments = {"scene_name": s.scene, "name": "Detail Graph"}
    arguments.update(VALUES)
    e.call("create_material", arguments)
    e.wait_idle()
    problems = mismatches(values_of(s.details("Detail Graph")), VALUES)
    check("4.1 create_material sets the detail values", not problems, "; ".join(problems))
    created = e.call("create_graph_texture", {"scene_name": s.scene, "name": "Detail Height"})
    e.wait_idle()
    check("4.2 a Graph Texture is created", bool(created), str(created)[:200])
    bound = e.call("set_material_texture_source", {"scene_name": s.scene, "material_name": "Detail Graph", "graph_texture": "Detail Height", "slot": "detail_blend"})
    e.wait_idle()
    check("4.3 set_material_texture_source accepts the detail_blend slot", bool(bound) and bound.get("bound"), str(bound)[:200])
    e.call("edit_material", {"scene_name": s.scene, "material_name": "Detail Graph", "texture_samplers": {"detail_blend": SLOT_EDIT}})
    e.wait_idle()
    check_slot("4.4", s.details("Detail Graph")["texture_samplers"]["detail_blend"], graph_texture_name="Detail Height")


    # 6. The Height Texture row in the Properties window: a Graph Texture is the item, its bake is internal
    e.call("set_texture_graph_target", {"scene_name": s.scene, "graph_texture": "Detail Height"})
    noise  = e.call("texture_graph_add_node", {"type": "perlin"})
    output = e.call("texture_graph_add_node", {"type": "output"})
    e.call("texture_graph_set_parameter", {"node_id": output["id"], "parameters": {"name": BAKE_NAME}})
    e.call("texture_graph_connect", {"source_node_id": noise["id"], "sink_node_id": output["id"]})
    e.wait_idle()
    baked = False
    for _ in range(120):
        listed = e.call("get_graph_textures", {"scene_name": s.scene}).get("graph_textures", [])
        baked = any((g.get("name") == "Detail Height") and g.get("has_output") for g in listed)
        if baked:
            break
        e.wait_frames(1)
    check("6.1 the Graph Texture bakes its output", baked)
    nodes = e.call("get_scene_nodes", {"scene_name": s.scene}).get("nodes", [])
    by_id = {n.get("id"): n for n in nodes}
    graph_node = next((n for n in nodes if n.get("name") == "Detail Height"), {})
    folder = by_id.get(graph_node.get("parent_id"), {})
    check("6.2 the Graph Texture is listed under Textures/Graph Textures",
          (folder.get("name") == "Graph Textures") and (by_id.get(folder.get("parent_id"), {}).get("name") == "Textures"),
          f"{graph_node.get('parent')} < {folder.get('parent')}")
    tree = window_with(e, "Textures", "Scene Hierarchy")
    if tree is not None:
        open_tree_row(e, tree, "Textures")
        open_tree_row(e, tree, "Graph Textures")
    rows = [i.get("label") for i in imgui_items(e, window=tree)] if tree is not None else []
    check("6.3 the Scene Hierarchy lists the Graph Texture", "Detail Height" in rows, str(tree))
    check("6.4 the Scene Hierarchy lists no bake", BAKE_NAME not in rows)
    e.call("create_material", {"scene_name": s.scene, "name": "Detail UI", "detail_blend": True})
    e.call("create_material", {"scene_name": s.scene, "name": "Detail Drop", "detail_blend": True})
    e.wait_idle()
    window = properties_window_of(e, s.scene, "Detail UI")
    check("6.5 the Properties window shows the Height Texture row", window is not None)
    if window is not None:
        entries = open_picker(e, window)
        labels = [label for label, _ in entries]
        check("6.6 the Height Texture picker offers the Graph Texture", "Detail Height" in labels, str(labels))
        check("6.7 the Height Texture picker offers no bake", BAKE_NAME not in labels, str(labels))
        graph_entry = [item_id for label, item_id in entries if label == "Detail Height"]
        if graph_entry:
            e.call("imgui_click", {"id": graph_entry[0]})
            e.wait_idle()
        else:
            e.call("key_press", {"key": "escape"})
        check_slot("6.8 picked", s.details("Detail UI")["texture_samplers"]["detail_blend"], graph_texture_name="Detail Height", transform_kept=False)
    window = properties_window_of(e, s.scene, "Detail Drop")
    if (window is not None) and (tree is not None):
        source = e.call("get_imgui_item_rect", {"window": tree, "label": "Detail Height"})
        target = e.call("get_imgui_item_rect", {"window": window, "label": "Height Texture"})
        e.call("mouse_drag", {"from": [source["center_x"], source["center_y"]], "to": [target["center_x"], target["center_y"]], "frames": 12})
        e.wait_idle()
    check_slot("6.9 dropped from the Scene Hierarchy", s.details("Detail Drop")["texture_samplers"]["detail_blend"],
               graph_texture_name="Detail Height", transform_kept=False)

    # 5. Save and load: the box carries the plain-texture material, the graph material rides its binding
    assigned = e.call("assign_mesh_material", {"scene_name": s.scene, "mesh_name": "Detail Box", "material_name": "Detail Defaults"})
    e.wait_idle()
    check("5.1 the edited material is assigned to the box", bool(assigned), str(assigned)[:200])
    saved_path = os.path.join(s.work_dir, "detail_saved.glb")
    e.save(s.scene, saved_path)
    check("5.2 the scene saves", os.path.isfile(saved_path), saved_path)
    loaded = e.load(saved_path)
    check("5.3 the saved scene loads", loaded is not None, str(loaded))
    reloaded = s.details("Detail Defaults", loaded)
    problems = mismatches(values_of(reloaded), VALUES)
    check("5.4 the reloaded material keeps the detail values", not problems, "; ".join(problems))
    reloaded_slot = reloaded.get("texture_samplers", {}).get("detail_blend", {})
    check("5.5 the reloaded material's slot holds a texture", reloaded_slot.get("texture_id") is not None, str(reloaded_slot))
    check_slot("5.6", reloaded_slot)
    graph = s.details("Detail Graph", loaded)
    problems = mismatches(values_of(graph), VALUES)
    check("5.7 the reloaded Graph Texture material keeps the detail values", not problems, "; ".join(problems))
    # The Graph Texture binding record carries the texture and the sampler of a slot, not its UV transform (every slot alike).
    check_slot("5.8", graph.get("texture_samplers", {}).get("detail_blend", {}), graph_texture_name="Detail Height", transform_kept=False)
    defaults_kept = s.details("Detail Defaults", loaded)
    check("5.9 the reload is the same values a second time", values_of(defaults_kept) == values_of(reloaded))

    e.close(loaded)

    # 7. The Properties window's pick survives the editor's save, a close and a load of a new scene
    ui_path = os.path.join(s.work_dir, "detail_ui.glb")
    e.save(s.scene, ui_path)
    e.close(s.scene)
    reopened = e.load(ui_path)
    check("7.1 the saved scene loads", reopened is not None, str(reopened))
    check_slot("7.2 the reloaded picked slot", s.details("Detail UI", reopened)["texture_samplers"]["detail_blend"],
               graph_texture_name="Detail Height", transform_kept=False)
    window = properties_window_of(e, reopened, "Detail UI")
    labels = [label for label, _ in open_picker(e, window)] if window is not None else []
    e.call("key_press", {"key": "escape"})
    check("7.3 after the load the picker offers the Graph Texture and no bake",
          ("Detail Height" in labels) and (BAKE_NAME not in labels), str(labels))
    e.close(reopened)


# --- --only-paint ------------------------------------------------------------------------------

GRID_CELLS       = 6       # GRID_CELLS x GRID_CELLS quads over 1 x 1 m in the node's xz plane
GRID_TRANSLATION = (1.0, 0.5, -0.25)
GRID_SCALE       = 2.0
CHANNELS         = "rgba"
FILL_VALUES      = (0.2, 0.4, 0.6, 0.8)


def write_grid_scene(directory):
    """detail_grid.gltf: one grid mesh whose node is translated and scaled, so the brush runs in world space."""
    builder = pav.Gltf_builder()
    positions, normals, texcoords, indices = [], [], [], []
    for j in range(GRID_CELLS + 1):
        for i in range(GRID_CELLS + 1):
            positions.append((i / GRID_CELLS - 0.5, 0.0, j / GRID_CELLS - 0.5))
            normals.append((0.0, 1.0, 0.0))
            texcoords.append((i / GRID_CELLS, j / GRID_CELLS))
    for j in range(GRID_CELLS):
        for i in range(GRID_CELLS):
            a = j * (GRID_CELLS + 1) + i
            b = a + GRID_CELLS + 1
            indices += [a, b, a + 1, a + 1, b, b + 1]
    document = {
        "asset": {"version": "2.0", "generator": "detail_blend_verify.py"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Detail Grid", "mesh": 0, "translation": list(GRID_TRANSLATION), "scale": [GRID_SCALE] * 3}],
        "meshes": [{"name": "grid", "primitives": [builder.primitive((positions, normals, texcoords, indices), 0)]}],
        "materials": [{"name": "Grid"}],
        "accessors": builder.accessors, "bufferViews": builder.views
    }
    binary = builder.padded_binary()
    document["buffers"] = [{"uri": "detail_grid.bin", "byteLength": len(binary)}]
    with open(os.path.join(directory, "detail_grid.bin"), "wb") as handle:
        handle.write(binary)
    path = os.path.join(directory, "detail_grid.gltf")
    with open(path, "w", encoding="utf-8") as handle:
        pav.json.dump(document, handle)
    return path


def falloff(distance, radius):
    """The channel brush falloff (doc/editor/tools.md "Channel painting"): 3p^2 - 2p^3, p = 1 - distance / radius."""
    if radius <= 0.0 or distance >= radius:
        return 0.0
    p = 1.0 - distance / radius
    return p * p * (3.0 - 2.0 * p)


def model_stroke(before, positions, centers, radius, strength, target, blend):
    """The CPU model of apply_dab over one stroke: per vertex the greatest alpha, blended from the value before."""
    result = list(before)
    for vertex, position in enumerate(positions):
        alpha = 0.0
        for center in centers:
            alpha = max(alpha, min(1.0, falloff(math.dist(position, center), radius) * strength))
        if alpha <= 0.0:
            continue
        base = before[vertex]
        if blend == "mix":
            value = target * alpha + base * (1.0 - alpha)
        elif blend == "add":
            value = base + target * alpha
        else:
            value = base - target * alpha
        result[vertex] = min(1.0, max(0.0, value))
    return result


class Grid:
    def __init__(self, s: Session, node):
        self.s = s
        self.node = node
        info = s.e.call("get_mesh_geometry_info", {"scene_name": s.scene, "node_name": node})
        self.vertex_count = info["counts"]["vertices"]
        self.corner_count = info["counts"]["corners"]
        vertices = s.e.call("get_mesh_attribute_values", {"scene_name": s.scene, "node_name": node, "domain": "vertex",
                                                          "indices": list(range(self.vertex_count)), "attributes": []})["elements"]
        self.world = [tuple(GRID_TRANSLATION[k] + GRID_SCALE * v["position"][k] for k in range(3)) for v in vertices]
        corners = self.read_elements()
        self.corner_vertex = [c["vertex"] for c in corners]

    def read_elements(self):
        return self.s.e.call("get_mesh_attribute_values", {"scene_name": self.s.scene, "node_name": self.node, "domain": "corner",
                                                           "indices": list(range(self.corner_count)), "attributes": ["corner_color_0"]})["elements"]

    def colors(self):
        """corner_color_0 per corner; None for a corner without one."""
        result = []
        for element in self.read_elements():
            value = element["attributes"].get("corner_color_0", {})
            result.append(value.get("value") if value.get("present") else None)
        return result

    def vertex_values(self, colors, channel):
        """The value of each vertex in the channel: that of its first corner (the brush's rule)."""
        values = [None] * self.vertex_count
        for corner, vertex in enumerate(self.corner_vertex):
            if values[vertex] is None and colors[corner] is not None:
                values[vertex] = colors[corner][channel]
        return values


def colors_close(a, b):
    return (a is not None) and (b is not None) and all(abs(x - y) <= TOLERANCE for x, y in zip(a, b))


def group_paint(s: Session):
    print("\n== --only-paint: channel stroke, fill and invert ==")
    e = s.e
    s.scene = e.open_foreign(write_grid_scene(s.work_dir))
    check("setup: the grid scene loads", s.scene is not None, str(s.scene))
    node = "Detail Grid"
    grid = Grid(s, node)
    check("setup: the grid has a Geometry with shared vertices", (grid.vertex_count == (GRID_CELLS + 1) ** 2) and (grid.corner_count > grid.vertex_count),
          f"{grid.vertex_count} vertices, {grid.corner_count} corners")

    # 1. Fill each channel with its own value: one undo entry each
    for channel, value in zip(CHANNELS, FILL_VALUES):
        depth = s.undo_depth()
        reply = e.call("fill_vertex_channel", {"scene_name": s.scene, "node_names": [node], "channel": channel, "value": value})
        e.wait_idle()
        check(f"1.{channel} fill_vertex_channel {channel} = {value} is one undo entry", reply.get("queued") and (s.undo_depth() == depth + 1),
              f"{depth} -> {s.undo_depth()}")
    filled = grid.colors()
    check("1.1 every corner holds the four fill values", all(colors_close(c, FILL_VALUES) for c in filled),
          str(next((c for c in filled if not colors_close(c, FILL_VALUES)), "")))
    e.call("undo")
    e.wait_idle()
    undone = grid.colors()
    expected = list(FILL_VALUES[:3]) + [1.0]
    check("1.2 undo of the alpha fill restores alpha (white before) and keeps r, g, b", all(colors_close(c, expected) for c in undone),
          str(next((c for c in undone if not colors_close(c, expected)), "")))
    e.call("redo")
    e.wait_idle()
    check("1.3 redo fills alpha again", all(colors_close(c, FILL_VALUES) for c in grid.colors()))

    # 2. A stroke equals the CPU model; the other channels are untouched
    strokes = [
        {"channel": "r", "centers": [[1.0, 0.5, -0.25], [1.4, 0.5, -0.25], [1.4, 0.5, 0.15]], "radius": 0.6, "strength": 0.8, "target": 1.0, "blend": "mix"},
        {"channel": "g", "centers": [[0.4, 0.5, -0.6], [0.6, 0.5, -0.6]], "radius": 0.5, "strength": 1.0, "target": 0.3, "blend": "subtract"},
        {"channel": "b", "centers": [[1.6, 0.5, 0.5]], "radius": 0.7, "strength": 0.5, "target": 0.5, "blend": "add"},
    ]
    for index, stroke in enumerate(strokes, start=1):
        channel = CHANNELS.index(stroke["channel"])
        label = f"2.{index} {stroke['blend']} stroke on {stroke['channel']}"
        before = grid.colors()
        before_values = grid.vertex_values(before, channel)
        expected_values = model_stroke(before_values, grid.world, stroke["centers"], stroke["radius"], stroke["strength"], stroke["target"], stroke["blend"])
        depth = s.undo_depth()
        arguments = dict(stroke, scene_name=s.scene, node_name=node, primitive_index=0)
        reply = e.call("paint_vertex_channel", arguments)
        e.wait_idle()
        check(f"{label}: one undo entry", reply.get("queued") and (s.undo_depth() == depth + 1), f"{depth} -> {s.undo_depth()}")
        after = grid.colors()
        changed = sum(1 for a, b in zip(before_values, grid.vertex_values(after, channel)) if a != b)
        check(f"{label}: changed_vertices counts the vertices whose value changed", (changed > 0) and (reply.get("changed_vertices") == changed),
              f"{reply.get('changed_vertices')} vs {changed}")
        worst = 0.0
        others_kept = True
        for corner, vertex in enumerate(grid.corner_vertex):
            worst = max(worst, abs(after[corner][channel] - expected_values[vertex]))
            others_kept = others_kept and all(after[corner][k] == before[corner][k] for k in range(4) if k != channel)
        check(f"{label}: every corner equals the CPU model within {TOLERANCE}", worst <= TOLERANCE, f"worst {worst:.3g}")
        check(f"{label}: the other three channels are bit-identical", others_kept)
        e.call("undo")
        e.wait_idle()
        restored = grid.colors()
        check(f"{label}: undo restores every corner", all(a == b for a, b in zip(restored, before)))
        e.call("redo")
        e.wait_idle()
        check(f"{label}: redo applies the stroke again", all(colors_close(a, b) for a, b in zip(grid.colors(), after)))

    # 3. Invert
    before = grid.colors()
    depth = s.undo_depth()
    reply = e.call("invert_vertex_channel", {"scene_name": s.scene, "node_names": [node], "channel": "r"})
    e.wait_idle()
    check("3.1 invert_vertex_channel is one undo entry", reply.get("queued") and (s.undo_depth() == depth + 1), f"{depth} -> {s.undo_depth()}")
    inverted = grid.colors()
    check("3.2 r becomes 1 - r on every corner, g, b, a untouched",
          all(abs(a[0] - (1.0 - b[0])) <= TOLERANCE and a[1:] == b[1:] for a, b in zip(inverted, before)))
    e.call("undo")
    e.wait_idle()
    check("3.3 undo restores the inverted channel", all(colors_close(a, b) for a, b in zip(grid.colors(), before)))

    # 4. Refusals
    base = {"scene_name": s.scene, "node_name": node, "centers": [[1.0, 0.5, -0.25]], "radius": 0.5}
    refusal = e.refusal("paint_vertex_channel", dict(base, channel="x"))
    check("4.1 a channel outside r, g, b, a is refused", "channel must be one of" in str(refusal), str(refusal)[:200])
    refusal = e.refusal("paint_vertex_channel", dict(base, channel="r", radius=0.0))
    check("4.2 a radius of 0 is refused", "radius" in str(refusal), str(refusal)[:200])
    refusal = e.refusal("paint_vertex_channel", dict(base, channel="r", primitive_index=3))
    check("4.3 a primitive index out of range is refused", "out of range" in str(refusal), str(refusal)[:200])
    refusal = e.refusal("fill_vertex_channel", {"scene_name": s.scene, "node_names": ["No Such Node"], "channel": "r", "value": 0.5})
    check("4.4 fill of a missing node is refused", "not found" in str(refusal), str(refusal)[:200])
    refusal = e.refusal("fill_vertex_channel", {"scene_name": s.scene, "node_names": [node], "channel": "r", "value": 1.5})
    check("4.5 a fill value outside [0, 1] is refused", "value" in str(refusal), str(refusal)[:200])

    e.close(s.scene)


# --- --only-shader -----------------------------------------------------------------------------

PLANE_CELLS   = 15     # weights i / 15 are exact in the 8-bit vertex color of the optimized variant
PLANE_HALF    = 1.0    # the plane spans [-1, 1] in x and z, node at the origin
IMAGE_SIZE    = 160
DEBUG_DETAIL  = 38     # Shader_debug::detail_blend_weight
DEBUG_WORLD   = 36     # Shader_debug::world_position
CAMERA        = {"eye": [0.0, 2.2, 0.0], "target": [0.0, 0.0, 0.0], "up": [0.0, 0.0, -1.0], "fov_y_degrees": 60.0, "near": 0.1, "far": 10.0}
HEIGHT_LEVELS = (64, 192)


def write_plane_scene(directory):
    """detail_plane.gltf: one plane of PLANE_CELLS^2 quads, UVs 0..1, and a material whose occlusion
    slot samples the checker height.png (a linear texture in the content library)."""
    builder = pav.Gltf_builder()
    height = [[c for x in range(8) for c in ((HEIGHT_LEVELS[(x // 2 + y // 2) % 2],) * 3 + (255,))] for y in range(8)]
    with open(os.path.join(directory, "height.png"), "wb") as handle:
        handle.write(pav.png_bytes(8, 8, height))
    positions, normals, texcoords, indices = [], [], [], []
    for j in range(PLANE_CELLS + 1):
        for i in range(PLANE_CELLS + 1):
            u, v = i / PLANE_CELLS, j / PLANE_CELLS
            positions.append(((2.0 * u - 1.0) * PLANE_HALF, 0.0, (2.0 * v - 1.0) * PLANE_HALF))
            normals.append((0.0, 1.0, 0.0))
            texcoords.append((u, v))
    for j in range(PLANE_CELLS):
        for i in range(PLANE_CELLS):
            a = j * (PLANE_CELLS + 1) + i
            b = a + PLANE_CELLS + 1
            indices += [a, b, a + 1, a + 1, b, b + 1]
    document = {
        "asset": {"version": "2.0", "generator": "detail_blend_verify.py"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Detail Plane", "mesh": 0}],
        "meshes": [{"name": "plane", "primitives": [builder.primitive((positions, normals, texcoords, indices), 0)]}],
        "materials": [{"name": "Height Source", "occlusionTexture": {"index": 0}}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 10497, "wrapT": 10497}],
        "images": [{"uri": "height.png"}],
        "accessors": builder.accessors, "bufferViews": builder.views
    }
    binary = builder.padded_binary()
    document["buffers"] = [{"uri": "detail_plane.bin", "byteLength": len(binary)}]
    with open(os.path.join(directory, "detail_plane.bin"), "wb") as handle:
        handle.write(binary)
    path = os.path.join(directory, "detail_plane.gltf")
    with open(path, "w", encoding="utf-8") as handle:
        pav.json.dump(document, handle)
    return path


def read_pfm(path):
    """(width, height, rows) of an RGB float PFM; rows[y][x] = (r, g, b), y = 0 the top row."""
    with open(path, "rb") as handle:
        data = handle.read()
    parts = data.split(b"\n", 3)
    if parts[0].strip() != b"PF":
        raise RuntimeError(f"{path} is not an RGB PFM")
    width, height = (int(t) for t in parts[1].split())
    scale = float(parts[2])
    endian = "<" if scale < 0.0 else ">"
    floats = struct.unpack(f"{endian}{width * height * 3}f", parts[3][:width * height * 12])
    rows = []
    for y in range(height):
        row_base = (height - 1 - y) * width * 3
        rows.append([floats[row_base + 3 * x:row_base + 3 * x + 3] for x in range(width)])
    return width, height, rows


def cpu_detail_blend(w, h, influence, width, pixel_width):
    """erhe::math::detail_blend() (doc/erhe/math.md "Detail blend")."""
    s = max(width, pixel_width, 1.0e-4)
    d = min(1.0, max(0.0, ((h - 1.0 + w) / s) + w))
    return w + ((d - w) * influence)


class Shader_session:
    def __init__(self, s: Session, node):
        self.s = s
        self.node = node
        self.index = 0

    def render(self, shader_debug, output="linear"):
        """The image of CAMERA as rows of (r, g, b); a debug render's uncovered pixels are NaN."""
        self.index += 1
        path = os.path.join(self.s.work_dir, f"shader_{self.index}.{'pfm' if output == 'linear' else 'png'}")
        arguments = {"scene": self.s.scene, "camera": CAMERA, "width": IMAGE_SIZE, "height": IMAGE_SIZE,
                     "output": output, "path": path, "shader_debug": shader_debug, "msaa_samples": 0}
        if output == "linear":
            arguments["color_format"] = "rgba32f"
        self.s.e.call("render_scene_image", arguments)
        return read_pfm(path)[2] if output == "linear" else None

    def edit(self, material, **values):
        self.s.e.call("edit_material", dict(values, scene_name=self.s.scene, material_name=material))
        self.s.e.wait_idle()

    def fill(self, value, channel="r"):
        self.s.e.call("fill_vertex_channel", {"scene_name": self.s.scene, "node_names": [self.node], "channel": channel, "value": value})
        self.s.e.wait_idle()

    def assign(self, material):
        self.s.e.call("assign_mesh_material", {"scene_name": self.s.scene, "mesh_name": self.node, "material_name": material})
        self.s.e.wait_idle()


def covered(rows):
    return [(x, y) for y, row in enumerate(rows) for x, value in enumerate(row) if not math.isnan(value[0])]


def group_shader(s: Session):
    print("\n== --only-shader: the detail weight debug mode and the vertex color factor ==")
    e = s.e
    s.scene = e.open_foreign(write_plane_scene(s.work_dir))
    check("setup: the plane scene loads", s.scene is not None, str(s.scene))
    node = "Detail Plane"
    textures = e.call("get_scene_textures", {"scene_name": s.scene}).get("textures", [])
    check("setup: the height texture is in the content library", len(textures) >= 1, str(textures)[:200])
    if not textures:
        e.close(s.scene)
        return
    t = Shader_session(s, node)
    e.call("create_material", {"scene_name": s.scene, "name": "Detail Height", "detail_blend": True,
                               "detail_blend_height_influence": 1.0, "detail_blend_transition_width": 1.0})
    e.wait_idle()
    t.edit("Detail Height", texture_samplers={"detail_blend": {"texture": textures[0]["name"], "min_filter": "nearest", "mag_filter": "nearest"}})
    t.assign("Detail Height")

    # The world position of every pixel: the gradient's expected weight is (x + 1) / 2.
    world = t.render(DEBUG_WORLD)
    pixels = covered(world)
    check("setup: the plane covers the image center", len(pixels) > (IMAGE_SIZE * IMAGE_SIZE) // 3, f"{len(pixels)} pixels")

    # 1. Fill 0 renders black, fill 1 renders white, with the height texture bound
    t.fill(0.0)
    image = t.render(DEBUG_DETAIL)
    values = [image[y][x][0] for x, y in pixels]
    check("1.1 weight 0 renders black", max(values) <= 1.0e-6, f"max {max(values):.3g}")
    t.fill(1.0)
    image = t.render(DEBUG_DETAIL)
    values = [image[y][x][0] for x, y in pixels]
    check("1.2 weight 1 renders white", min(values) >= 1.0 - 1.0e-6, f"min {min(values):.6g}")

    # 2. Height influence 0 renders the plain weight: a gradient along x, set per corner
    grid = Grid(s, node)
    vertices = e.call("get_mesh_attribute_values", {"scene_name": s.scene, "node_name": node, "domain": "vertex",
                                                    "indices": list(range(grid.vertex_count)), "attributes": []})["elements"]
    vertex_weight = [round((v["position"][0] + PLANE_HALF) / (2.0 * PLANE_HALF) * PLANE_CELLS) / PLANE_CELLS for v in vertices]
    e.call("set_mesh_attribute_values", {"scene_name": s.scene, "node_name": node, "attribute": "corner_color_0",
                                         "elements": list(range(grid.corner_count)),
                                         "values": [[vertex_weight[vertex], 1.0, 1.0, 1.0] for vertex in grid.corner_vertex]})
    e.wait_idle()
    t.edit("Detail Height", detail_blend_height_influence=0.0)
    image = t.render(DEBUG_DETAIL)
    plain = {(x, y): image[y][x][0] for x, y in pixels}
    worst = max(abs(plain[(x, y)] - (world[y][x][0] + PLANE_HALF) / (2.0 * PLANE_HALF)) for x, y in pixels)
    check("2.1 height influence 0 renders the plain weight (x + 1) / 2", worst <= 2.0e-3, f"worst {worst:.3g}")

    # 3. The height read back: weight 1/2 at transition width 1 renders the height itself
    t.edit("Detail Height", detail_blend_height_influence=1.0)
    t.fill(0.5)
    image = t.render(DEBUG_DETAIL)
    half = image
    t.edit("Detail Height", detail_blend_height_influence=0.0)
    image = t.render(DEBUG_DETAIL)
    heights = {(x, y): half[y][x][0] + 1.0 - 2.0 * image[y][x][0] for x, y in pixels}
    levels = sorted({round(h, 2) for h in heights.values()})
    check("3.1 the height texture shows its two checker levels", len(levels) == 2, str(levels))

    # 4. The gradient at transition width 0: the boundary lies at h = 1 - w within one pixel
    e.call("set_mesh_attribute_values", {"scene_name": s.scene, "node_name": node, "attribute": "corner_color_0",
                                         "elements": list(range(grid.corner_count)),
                                         "values": [[vertex_weight[vertex], 1.0, 1.0, 1.0] for vertex in grid.corner_vertex]})
    e.wait_idle()
    t.edit("Detail Height", detail_blend_height_influence=1.0, detail_blend_transition_width=0.0)
    image = t.render(DEBUG_DETAIL)
    level = {p: (0 if h < 0.5 * (levels[0] + levels[-1]) else 1) for p, h in heights.items()}
    sign = {p: (heights[p] - 1.0 + plain[p]) > 0.0 for p in pixels}
    outside, band, ones = 0, 0, 0
    worst_pixel = None
    for x, y in pixels:
        neighbours = [(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)]
        if any((p not in sign) or (sign[p] != sign[(x, y)]) or (level[p] != level[(x, y)]) for p in neighbours):
            band += 1
            continue
        expected = cpu_detail_blend(plain[(x, y)], heights[(x, y)], 1.0, 0.0, 0.0)
        ones += 1 if expected >= 0.5 else 0
        if abs(image[y][x][0] - expected) > 0.01:
            outside += 1
            worst_pixel = worst_pixel or (x, y, image[y][x][0], expected)
    check("4.1 every pixel off the boundary band equals the CPU formula of its measured weight and height", outside == 0,
          f"{outside} differ, first {worst_pixel}")
    check("4.2 the image holds both sides of a boundary that follows the checker", (ones > 100) and ((len(pixels) - band - ones) > 100) and (band > 0),
          f"{ones} ones, {len(pixels) - band - ones} zeros, {band} band pixels")

    # 5. Detail blend off renders black
    t.edit("Detail Height", detail_blend=False)
    image = t.render(DEBUG_DETAIL)
    values = [image[y][x][0] for x, y in pixels]
    check("5.1 a material with detail blend off renders black", max(values) <= 1.0e-6, f"max {max(values):.3g}")

    # 6. Surface noise without a texture: not constant, node space moves with the node
    e.call("create_material", {"scene_name": s.scene, "name": "Detail Noise", "detail_blend": True, "detail_blend_transition_width": 1.0,
                               "detail_blend_noise_amount": 1.0, "detail_blend_noise_scale": 3.0, "detail_blend_noise_space": "node"})
    e.wait_idle()
    t.assign("Detail Noise")
    t.fill(0.5)
    node_image = t.render(DEBUG_DETAIL)
    noise = [node_image[y][x][0] for x, y in pixels]
    mean = sum(noise) / len(noise)
    deviation = math.sqrt(sum((v - mean) ** 2 for v in noise) / len(noise))
    check("6.1 noise amount 1 with no texture is not constant", deviation > 0.05, f"deviation {deviation:.3g}")
    t.edit("Detail Noise", detail_blend_noise_space="world")
    world_image = t.render(DEBUG_DETAIL)
    same_at_origin = max(abs(world_image[y][x][0] - node_image[y][x][0]) for x, y in pixels)
    check("6.2 at the origin the world and node space noise agree", same_at_origin <= 1.0e-3, f"worst {same_at_origin:.3g}")
    e.call("set_node_transform", {"scene_name": s.scene, "node_name": node, "translation": [0.31, 0.0, 0.17]})
    e.wait_idle()
    world_moved = t.render(DEBUG_DETAIL)
    t.edit("Detail Noise", detail_blend_noise_space="node")
    node_moved = t.render(DEBUG_DETAIL)
    both = [(x, y) for x, y in pixels if not (math.isnan(world_moved[y][x][0]) or math.isnan(node_moved[y][x][0]))]
    difference = sum(abs(world_moved[y][x][0] - node_moved[y][x][0]) for x, y in both) / max(1, len(both))
    check("6.3 after the node moves, the world and node space noise differ", difference > 0.05, f"mean difference {difference:.3g}")
    e.call("set_node_transform", {"scene_name": s.scene, "node_name": node, "translation": [0.0, 0.0, 0.0]})
    e.wait_idle()

    # 7. B1.3: outside the debug mode a detail blend material is not tinted by its weight channel
    e.call("create_material", {"scene_name": s.scene, "name": "Detail Tint", "bxdf_model": "unlit", "base_color": [1.0, 1.0, 1.0]})
    e.wait_idle()
    t.assign("Detail Tint")
    t.fill(0.0)
    center = [(x, y) for x, y in pixels if (abs(x - IMAGE_SIZE // 2) < 8) and (abs(y - IMAGE_SIZE // 2) < 8)]

    def red_and_green(rows):
        return (sum(rows[y][x][0] for x, y in center) / len(center), sum(rows[y][x][1] for x, y in center) / len(center))

    red, green = red_and_green(t.render(0))
    check("7.1 detail blend off: the painted channel tints the base color", (red <= 0.01) and (green >= 0.5), f"r {red:.3g} g {green:.3g}")
    t.edit("Detail Tint", detail_blend=True)
    red, green = red_and_green(t.render(0))
    check("7.2 detail blend on: the weight channel does not tint the base color", (abs(red - green) <= 0.01) and (green >= 0.5), f"r {red:.3g} g {green:.3g}")

    log = pav.read_log()
    shader_errors = [line for line in log.splitlines() if ("] [E] [" in line) and any(k in line.lower() for k in ("glsl", "program", "shader"))]
    check("8.1 no shader compile or link error in the log", not shader_errors, "; ".join(shader_errors[:3])[:300])
    e.close(s.scene)


# --- --only-tool -------------------------------------------------------------------------------

TOOL_CENTER  = (1.0, 0.5, -0.25)  # the middle of the grid of write_grid_scene() in world space
TOOL_RADIUS  = 0.4
TOOL_SAMPLE  = (0.8, 0.5, -0.18)  # a capture's sample point: inside the stroke, clear of the edge lines and the brush circles
TOOL_PARK    = (1.8, 0.5, 0.55)   # where the pointer rests for a capture without a stroke
TOOL_SETTING_KEYS = ("paint_mode", "paints", "radius", "strength", "target", "blend", "channel")


def brush_state(e):
    return e.call("get_paint_brush_settings")


def wait_brush(e, predicate, frames=240):
    """The get_paint_brush_settings reply once predicate holds, read again after each editor frame."""
    reply = brush_state(e)
    for _ in range(frames):
        if predicate(reply):
            return reply
        e.wait_frames()
        reply = brush_state(e)
    return reply


def screen_mean(path, x, y, half=4):
    """The mean of r, g, b over the (2 half + 1)^2 pixel box about window pixel (x, y) of a capture_screenshot PNG."""
    width, height, pixels = pav.decode_png(path)
    total, count = 0.0, 0
    for py_ in range(max(0, y - half), min(height, y + half + 1)):
        for px in range(max(0, x - half), min(width, x + half + 1)):
            offset = 4 * (py_ * width + px)
            total += (pixels[offset] + pixels[offset + 1] + pixels[offset + 2]) / 3.0
            count += 1
    return total / max(1, count)


def changed_channels(before, after):
    """The set of channel indices that differ on any corner."""
    return {k for a, b in zip(before, after) for k in range(4) if a[k] != b[k]}


def group_tool(s: Session):
    print("\n== --only-tool: the Paint tool's Brush mode through the input drivers ==")
    e = s.e
    saved = brush_state(e)
    s.scene = e.open_foreign(write_grid_scene(s.work_dir))
    check("setup: the grid scene loads", s.scene is not None, str(s.scene))
    try:
        tool_checks(s)
    finally:
        for button in e.call("get_input_state").get("buttons", []):
            e.call("mouse_release", {"button": button})
        if brush_state(e).get("stroke_active"):
            e.call("key_press", {"key": "escape"})
        e.call("set_active_tool", {"tool": None})
        e.wait_idle()
        e.call("set_paint_brush_settings", {key: saved[key] for key in TOOL_SETTING_KEYS})
        if s.scene in e.scene_names():
            e.close(s.scene)


def tool_checks(s: Session):
    import curve_verify as cv  # noqa: E402 - the camera framing and the viewport isolation
    import level_tools_verify as ltv  # noqa: E402 - the projection and window helpers
    e = s.e
    node = "Detail Grid"
    grid = Grid(s, node)
    for name, values in (("Tool Plain", {}), ("Tool Detail", {"detail_blend": True, "detail_blend_weight_channel": "g"})):
        e.call("create_material", dict(values, scene_name=s.scene, name=name))
    e.wait_idle()
    e.call("assign_mesh_material", {"scene_name": s.scene, "mesh_name": node, "material_name": "Tool Plain"})
    for channel in "rg":
        e.call("fill_vertex_channel", {"scene_name": s.scene, "node_names": [node], "channel": channel, "value": 0.0})
    e.wait_idle()

    # 1. The settings tools: explicit arguments, every one checked before anything changes
    reply = e.call("set_paint_brush_settings", {"paint_mode": "brush", "paints": "weight", "radius": TOOL_RADIUS, "strength": 1.0,
                                                "target": 1.0, "blend": "mix", "channel": "r"})
    check("1.1 set_paint_brush_settings writes the values and get_paint_brush_settings reads them back",
          (reply.get("paint_mode") == "brush") and (reply.get("paints") == "weight") and same(reply.get("radius"), TOOL_RADIUS)
          and (reply.get("blend") == "mix") and (reply.get("channel") == "r") and (brush_state(s.e) == reply), json_text(reply))
    refusals = [e.refusal("set_paint_brush_settings", arguments) for arguments in
                ({"size": 1.0}, {"radius": 500.0, "channel": "g"}, {"channel": "x"}, {"paints": "height"}, {"target": 2.0})]
    check("1.2 an unknown argument and values out of range are refused, and nothing changes",
          all(refusals) and (brush_state(e) == reply), "; ".join(str(r)[:80] for r in refusals))

    e.call("set_active_tool", {"tool": "paint"})
    keep = [[x, 0.5, z] for x in (0.0, 2.0) for z in (-1.25, 0.75)]
    viewport = cv.frame_camera(e, s.scene, [1.0, 3.5, 2.0], list(TOOL_CENTER), keep)

    def at(point):
        x, y = ltv.window_point(viewport, point)
        return [float(round(x)), float(round(y))]

    def aim(point):
        x, y = at(point)
        e.call("mouse_wheel", {"x": x, "y": y, "dy": 0.0})
        return wait_brush(e, lambda r: (r.get("brush_center") is not None) and (math.dist(r["brush_center"], point) < 0.05))

    state = aim(TOOL_CENTER)
    check("1.3 with the tool active in Brush mode the brush is the hovered surface point",
          state.get("tool_active") and (state.get("brush_center") is not None) and (math.dist(state["brush_center"], TOOL_CENTER) < 0.05), json_text(state))
    check("1.4 a material with detail blend off paints the tool's channel",
          state.get("weight_channel", {}).get("source") == "tool" and state["weight_channel"].get("channel") == "r", json_text(state.get("weight_channel")))

    start, end = [0.75, 0.5, -0.25], [1.25, 0.5, -0.25]

    def held_stroke():
        """Presses at start, drags to end and holds; returns the state once the stroke has changed a vertex."""
        aim(start)
        e.call("mouse_drag", {"from": at(start), "to": at(end), "frames": 4, "hold": True})
        return wait_brush(e, lambda r: r.get("stroke_active") and (r.get("changed_vertices", 0) > 0))

    # 2. A stroke paints the weight channel only, live, as one undo entry; undo restores
    before = grid.colors()
    depth = s.undo_depth()
    held = held_stroke()
    live = grid.colors()
    check("2.1 a left drag opens a stroke that changes vertices", held.get("stroke_active") and held.get("changed_vertices", 0) > 0, json_text(held))
    check("2.2 a read before the release already shows the changed weights (r only)", changed_channels(before, live) == {0},
          str(changed_channels(before, live)))
    e.call("mouse_release", {})
    done = wait_brush(e, lambda r: (not r.get("stroke_active")) and (r.get("last_stroke", {}).get("outcome") == "ended"))
    e.wait_idle()
    after = grid.colors()
    last = done.get("last_stroke", {})
    check("2.3 the release ends the stroke as one undo entry", last.get("recorded") and (s.undo_depth() == depth + 1), f"{json_text(last)}; {depth} -> {s.undo_depth()}")
    check("2.4 the stroke changed the r channel only (g, b, a bit-identical)", (changed_channels(before, after) == {0}) and (last.get("channels") == ["r"]),
          str(changed_channels(before, after)))
    painted = [c[0] for c in after]
    check("2.5 the corners near the stroke reach the target, those beyond the radius keep 0", (max(painted) >= 0.99) and (min(painted) == 0.0),
          f"max {max(painted):.3g} min {min(painted):.3g}")
    e.call("undo")
    e.wait_idle()
    check("2.6 undo restores every corner", grid.colors() == before)

    # 3. Escape during a stroke restores the values and adds no undo entry
    depth = s.undo_depth()
    held = held_stroke()
    changed_before_escape = changed_channels(before, grid.colors())
    e.call("key_press", {"key": "escape"})
    cancelled = wait_brush(e, lambda r: (not r.get("stroke_active")) and (r.get("last_stroke", {}).get("outcome") == "cancelled"))
    restored = grid.colors()
    e.call("mouse_release", {})
    e.wait_idle()
    check("3.1 Escape mid-drag cancels the stroke that had changed values", held.get("stroke_active") and (changed_before_escape == {0})
          and (cancelled.get("last_stroke", {}).get("outcome") == "cancelled"), json_text(cancelled.get("last_stroke")))
    check("3.2 the cancel writes the values before the stroke back", restored == before)
    check("3.3 the cancelled drag adds no undo entry, its release paints nothing", (s.undo_depth() == depth) and (grid.colors() == before),
          f"{depth} -> {s.undo_depth()}")

    # 4. The hovered material's weight channel when its detail blend is on (B7.4)
    e.call("assign_mesh_material", {"scene_name": s.scene, "mesh_name": node, "material_name": "Tool Detail"})
    e.wait_idle()
    aim(end)
    state = aim(TOOL_CENTER)
    check("4.1 a material with detail blend on gives its weight channel (g) over the tool's (r)",
          (state.get("weight_channel", {}).get("source") == "material") and (state["weight_channel"].get("channel") == "g"), json_text(state.get("weight_channel")))
    before = grid.colors()
    depth = s.undo_depth()
    held_stroke()
    e.call("mouse_release", {})
    done = wait_brush(e, lambda r: (not r.get("stroke_active")) and (r.get("last_stroke", {}).get("outcome") == "ended"))
    e.wait_idle()
    check("4.2 the stroke paints g only, one undo entry", (changed_channels(before, grid.colors()) == {1}) and (done["last_stroke"].get("channels") == ["g"])
          and (s.undo_depth() == depth + 1), f"{changed_channels(before, grid.colors())}, {json_text(done.get('last_stroke'))}")
    e.call("undo")
    e.wait_idle()

    # 5. The Show Detail Weight switch sets the hovered viewport's debug mode; the debug mode shows a stroke live (B5.3)
    pixel = [int(v) for v in at(TOOL_SAMPLE)]
    shot = [0]

    def capture():
        shot[0] += 1
        path = os.path.join(s.work_dir, f"tool_{shot[0]}.png")
        e.call("capture_screenshot", {"path": path})
        return screen_mean(path, pixel[0], pixel[1])

    aim(TOOL_PARK)
    lit = capture()
    window = "Tool Properties"
    shown = ltv.open_window(e, window)
    e.call("imgui_click", {"window": window, "label": "Show Detail Weight"})
    on = wait_brush(e, lambda r: r.get("detail_weight_view") is not None)
    aim(TOOL_PARK)
    e.wait_frames(2)
    debug_zero = capture()
    check("5.1 the switch sets the last hovered viewport to the Detail Blend Weight mode", shown and (on.get("detail_weight_view") is not None), json_text(on.get("detail_weight_view")))
    check("5.2 the capture changes: weight 0 shows black where the lit view did not", (debug_zero < 8.0) and (lit > debug_zero + 20.0),
          f"lit {lit:.1f}, debug {debug_zero:.1f}")
    held_stroke()
    e.wait_frames(3)
    debug_live = capture()
    e.call("mouse_release", {})
    wait_brush(e, lambda r: not r.get("stroke_active"))
    e.wait_idle()
    check("5.3 mid-drag the debug view already shows the painted weight", debug_live > debug_zero + 100.0, f"before {debug_zero:.1f}, mid-drag {debug_live:.1f}")
    e.call("undo")
    e.wait_idle()
    e.call("imgui_click", {"window": window, "label": "Show Detail Weight"})
    off = wait_brush(e, lambda r: r.get("detail_weight_view") is None)
    aim(TOOL_PARK)
    e.wait_frames(2)
    back = capture()
    check("5.4 the switch again gives the viewport its mode back", (off.get("detail_weight_view") is None) and (abs(back - lit) < 4.0),
          f"lit {lit:.1f}, after {back:.1f}")
    e.call("imgui_click", {"window": window, "label": "Show Detail Weight"})
    wait_brush(e, lambda r: r.get("detail_weight_view") is not None)
    e.call("set_active_tool", {"tool": None})
    e.wait_idle()
    check("5.5 deactivating the tool gives the viewport its mode back", brush_state(e).get("detail_weight_view") is None)
    e.call("set_active_tool", {"tool": "paint"})
    if not shown:
        e.call("set_window_visibility", {"title": window, "visible": False})

    # 6. Shift + wheel sets the radius; the plain wheel does not
    x, y = at(TOOL_CENTER)
    aim(TOOL_CENTER)
    radius = brush_state(e)["radius"]
    e.call("mouse_wheel", {"x": x, "y": y, "dy": 1.0, "modifiers": ["shift"]})
    grown = brush_state(e)["radius"]
    e.call("mouse_wheel", {"x": x, "y": y, "dy": 1.0})
    zoomed = brush_state(e)["radius"]
    check("6.1 Shift + wheel grows the radius by the wheel step", abs(grown - radius * 1.1) < 1.0e-4, f"{radius} -> {grown}")
    check("6.2 the plain wheel leaves the radius", zoomed == grown, f"{grown} -> {zoomed}")
    check("6.3 the radius reads back through get_paint_brush_settings after the wheel", same(brush_state(e)["radius"], grown))


def json_text(value):
    return pav.json.dumps(value)[:300]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=DEFAULT_EDITOR, help=f"editor executable (default {DEFAULT_EDITOR})")
    parser.add_argument("--cwd", default=None, help="working directory of the editor: its config/, res/ and logs/ (default: the repository root)")
    parser.add_argument("--only-material", action="store_true", help="run only the material values and slot group")
    parser.add_argument("--only-paint", action="store_true", help="run only the channel painting group")
    parser.add_argument("--only-shader", action="store_true", help="run only the shader group")
    parser.add_argument("--only-tool", action="store_true", help="run only the Paint tool Brush mode group")
    args = parser.parse_args()
    if args.cwd is not None:
        args.editor = args.editor if os.path.isabs(args.editor) else os.path.join(pav.REPO_ROOT, args.editor)
        pav.REPO_ROOT = os.path.abspath(args.cwd)
        pav.LOG_PATH  = os.path.join(pav.REPO_ROOT, "logs", "log.txt")

    groups = [("material", args.only_material, group_material), ("paint", args.only_paint, group_paint), ("shader", args.only_shader, group_shader),
              ("tool", args.only_tool, group_tool)]
    selected = [g for g in groups if g[1]] or groups

    work_dir = tempfile.mkdtemp(prefix="detail_blend_verify_")
    editor = Editor(args.editor)
    try:
        editor.call("reset_editor_state")
        editor.wait_idle()
        for _name, _flag, run in selected:
            run(Session(editor, work_dir))
    finally:
        editor.stop()
        shutil.rmtree(work_dir, ignore_errors=True)
    failed = [label for label, ok in pav.RESULTS if not ok]
    print(f"\n{len(pav.RESULTS) - len(failed)}/{len(pav.RESULTS)} checks passed")
    for label in failed:
        print(f"  FAILED: {label}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
