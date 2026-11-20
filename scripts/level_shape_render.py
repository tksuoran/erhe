#!/usr/bin/env python3
"""Render kit shapes, each alone on its ground pad, one image per shape and seed.

See doc/editor/level_tools.md "Shape images". Every shape that level_get_kits
lists (or those --only selects) stands at its default parameter values on a
ground pad of its own; a seeded shape stands three times, at seeds 1, 3 and 17.
The pads run toward -Z. Each piece is rendered from the same three-quarter view
for every piece of its size class into logs/shape_render_<id>.png (a seeded
shape: logs/shape_render_<id>_<seed>.png).

Usage:
    python3 scripts/level_shape_render.py [--editor <editor binary>] [--only <id or id prefix>]
                                          [--parameters <json object>] [--tag <name>] [--detail <x,y,z,distance>]
                                          [--azimuth <degrees>] [--elevation <degrees>]
                                          [--sun <intensity>] [--ambient <level>] [--light-shadows]
                                          [--save <scene path>] [--keep-open]

--only names one shape (machine.cam, not machine.camshaft) or an id prefix (basic.).
--parameters gives every piece these values instead of its defaults (with --only
naming the shape they belong to); --tag ends the image names with _<name>, so such
a run keeps the default images. --detail renders a close-up instead: the camera
looks at the point x,y,z in the piece's own frame from the given distance.
--azimuth and --elevation turn the camera. --sun adds a directional light of that
intensity and --ambient sets the scene's ambient light to that grey level: a shape
that carries lights of its own is otherwise lit by them alone. --light-shadows
turns shadow casting on for every light node the pieces carry
(doc/editor/level_tools.md "Lights on pieces"). --save writes the scene (a .gltf
or .glb path) so the windowed editor can open it; --keep-open leaves the launched
editor running with the scene and prints its pid.
"""

import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prepared_asset_verify as pav  # noqa: E402
from prepared_asset_verify import Editor  # noqa: E402
import level_tools_verify as ltv  # noqa: E402

EDITOR_CANDIDATES = ("build_xcode_metal_headless/bin/Debug/editor", ltv.HEADLESS_EDITOR, ltv.DEBUG_EDITOR)
SEEDS = (1, 3, 17)
# Half the largest extent of a piece and its pad margin, in meters: every piece of one class is rendered by the same camera.
SIZE_CLASSES = (1.0, 1.5, 2.25, 3.5, 5.0, 7.5, 11.0, 16.0, 24.0, 36.0, 54.0, 80.0, 120.0)
PAD_MARGIN = 0.5
PAD_THICKNESS = 0.1
PAD_MATERIAL = "Kit Dark Rock"
PAD_TILE_MAX = 64.0  # the floor slab's largest width and depth
IMAGE_WIDTH = 1600
IMAGE_HEIGHT = 900
ELEVATION_DEGREES = 24.0
AZIMUTH_DEGREES = 28.0
FOV_Y_DEGREES = 36.0


def default_editor():
    for candidate in EDITOR_CANDIDATES:
        if os.path.isfile(os.path.join(pav.REPO_ROOT, candidate)):
            return candidate
    return EDITOR_CANDIDATES[0]


def size_class(half_extent):
    return next((c for c in SIZE_CLASSES if half_extent <= c), SIZE_CLASSES[-1])


def local_box(piece):
    return [[a - b for a, b in zip(piece["world_aabb"][end], piece["position"])] for end in ("min", "max")]


def list_rows(editor, scene, only):
    """(shape, seed) per row, in catalog order."""
    shapes = editor.call("level_get_kits", {"scene_name": scene})["shapes"]
    ids = {shape["id"] for shape in shapes}

    def selected(shape_id):
        # An id names that shape alone (machine.cam, not machine.camshaft); anything else is a prefix.
        return (shape_id == only) if (only in ids) else shape_id.startswith(only)

    rows = []
    for shape in shapes:
        if not selected(shape["id"]):
            continue
        for seed in (SEEDS if shape["seeded"] else SEEDS[:1]):
            rows.append({"shape": shape["id"], "seed": seed, "seeded": shape["seeded"]})
    return rows


def parameters_of(row, parameters):
    return dict(parameters, seed=row["seed"])


def measure(editor, scene, rows, parameters):
    """The local bounds of every row's piece, read from a placement that is undone."""
    requests = [{"shape": row["shape"], "position": [0.0, 0.0, 0.0], "parameters": parameters_of(row, parameters)} for row in rows]
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": requests})["pieces"]
    editor.wait_idle()
    for row, piece in zip(rows, placed):
        row["box"] = local_box(piece)
    editor.call("undo")
    editor.wait_idle()


def lay_out(rows):
    """Each row's piece position, pad and camera; the rows run toward -Z, far enough apart that a camera sees its row alone."""
    z = 0.0
    previous_class = None
    for row in rows:
        box = row["box"]
        row["x"] = -0.5 * (box[0][0] + box[1][0])  # the bounds centered on the pad
        min_x = row["x"] + box[0][0]
        max_x = row["x"] + box[1][0]
        min_z = box[0][2]
        max_z = box[1][2]
        half_extent = 0.5 * max(max_x - min_x, max_z - min_z, box[1][1]) + PAD_MARGIN
        row["class"] = size_class(half_extent)
        if previous_class is not None:
            z -= (3.0 * max(previous_class, row["class"])) + 2.0
        previous_class = row["class"]
        row["z"] = z
        row["pad"] = {
            "width": (max_x - min_x) + (2.0 * PAD_MARGIN), "depth": (max_z - min_z) + (2.0 * PAD_MARGIN),
            "position": [0.5 * (min_x + max_x), -PAD_THICKNESS, z + (0.5 * (min_z + max_z))],
        }
        row["center"] = [0.5 * (min_x + max_x), 0.0, z + (0.5 * (min_z + max_z))]


def camera_of(row, view):
    """The camera of the row's size class, relative to the row's center on the ground; a detail view looks at its own point."""
    c = row["class"]
    distance = 4.2 * c
    el = math.radians(view["elevation"])
    az = math.radians(view["azimuth"])
    target = [row["center"][0], 0.45 * c, row["center"][2]]
    near = 0.05 * c
    if view["detail"] is not None:
        x, y, z, distance = view["detail"]
        target = [row["x"] + x, y, row["z"] + z]
        near = 0.05 * distance
    eye = [target[0] + (distance * math.cos(el) * math.sin(az)), target[1] + (distance * math.sin(el)), target[2] + (distance * math.cos(el) * math.cos(az))]
    # The far plane ends the view behind the row, so the rows beyond it stay out of the image.
    return {"eye": eye, "target": target, "fov_y_degrees": FOV_Y_DEGREES, "near": near, "far": distance + (1.6 * c)}


def pad_tiles(pad, name):
    """The pad as equal floor slabs, each within the slab's size range."""
    columns = max(1, math.ceil(pad["width"] / PAD_TILE_MAX))
    tile_rows = max(1, math.ceil(pad["depth"] / PAD_TILE_MAX))
    width = pad["width"] / columns
    depth = pad["depth"] / tile_rows
    tiles = []
    for i in range(columns):
        for j in range(tile_rows):
            x = pad["position"][0] - (0.5 * pad["width"]) + ((i + 0.5) * width)
            z = pad["position"][2] - (0.5 * pad["depth"]) + ((j + 0.5) * depth)
            tiles.append({"shape": "basic.floor_slab", "position": [x, pad["position"][1], z], "name": name,
                          "parameters": {"width": width, "depth": depth, "thickness": PAD_THICKNESS}, "materials": {"floor": PAD_MATERIAL}})
    return tiles


def build(editor, scene, rows, parameters):
    requests = []
    for row in rows:
        label = f"{row['shape']} seed {row['seed']}" if row["seeded"] else row["shape"]
        requests.extend(pad_tiles(row["pad"], f"pad {label}"))
        requests.append({"shape": row["shape"], "position": [row["x"], 0.0, row["z"]], "name": label, "parameters": parameters_of(row, parameters)})
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": requests})["pieces"]
    editor.wait_idle()
    return placed


def render(editor, scene, row, view):
    name = f"shape_render_{row['shape']}" + (f"_{row['seed']}" if row["seeded"] else "") + (f"_{view['tag']}" if view["tag"] else "") + ".png"
    path = os.path.join(pav.REPO_ROOT, "logs", name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.isfile(path):
        os.remove(path)
    reply = editor.call("render_scene_image", {"scene": scene, "camera": camera_of(row, view), "width": IMAGE_WIDTH, "height": IMAGE_HEIGHT,
                                               "output": "png", "path": path})
    return path if (os.path.isfile(path) and (reply.get("width") == IMAGE_WIDTH)) else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=default_editor(), help="editor binary (relative to the repository root or absolute)")
    parser.add_argument("--only", default="", help="render only this shape, or the shapes whose id starts with this (for example basic.)")
    parser.add_argument("--parameters", default="{}", help="a JSON object of parameter values for every piece")
    parser.add_argument("--tag", default="", help="ends the image names with _<tag>")
    parser.add_argument("--detail", default="", help="x,y,z,distance: a close-up of that point of the piece")
    parser.add_argument("--azimuth", type=float, default=AZIMUTH_DEGREES, help="camera azimuth in degrees from +Z toward +X")
    parser.add_argument("--elevation", type=float, default=ELEVATION_DEGREES, help="camera elevation in degrees")
    parser.add_argument("--sun", type=float, default=0.0, help="intensity of a directional light added to the scene (0: none)")
    parser.add_argument("--ambient", type=float, default=-1.0, help="grey level of the scene's ambient light (negative: left as it is)")
    parser.add_argument("--light-shadows", action="store_true", help="turn shadow casting on for the pieces' light nodes")
    parser.add_argument("--save", default="", help="save the comparison scene to this .gltf or .glb path")
    parser.add_argument("--keep-open", action="store_true", help="leave the launched editor running with the scene")
    args = parser.parse_args()

    failures = []
    editor = None
    parameters = json.loads(args.parameters)
    view = {"tag": args.tag, "azimuth": args.azimuth, "elevation": args.elevation,
            "detail": [float(v) for v in args.detail.split(",")] if args.detail else None}
    try:
        editor = Editor(args.editor)
        scene = ltv.create_scene(editor)
        rows = list_rows(editor, scene, args.only)
        if not rows:
            raise RuntimeError(f"level_get_kits lists no shape whose id is or starts with '{args.only}'")
        measure(editor, scene, rows, parameters)
        lay_out(rows)
        placed = build(editor, scene, rows, parameters)
        if len(placed) < (2 * len(rows)):
            failures.append(f"placed {len(placed)} pieces for {len(rows)} rows")
        if args.sun > 0.0:
            editor.call("create_light", {"scene_name": scene, "type": "directional", "name": "Sun", "intensity": args.sun,
                                         "position": [3.0, 8.0, 6.0], "cast_shadow": False})
            editor.wait_idle()
        if args.light_shadows:
            for piece in editor.call("level_get_pieces", {"scene_name": scene})["pieces"]:
                for light in piece.get("lights", []):
                    editor.call("set_item_property", {"item_id": light["id"], "property": "cast_shadow", "value": True})
            editor.wait_idle()
        if args.ambient >= 0.0:
            editor.call("set_scene_settings", {"scene_name": scene, "ambient_light": [args.ambient, args.ambient, args.ambient]})
            editor.wait_idle()
        print(f"scene '{scene}': {len(rows)} rows, each piece on its own pad; the rows run toward -Z")
        for row in rows:
            path = render(editor, scene, row, view)
            label = row["shape"] + (f" seed {row['seed']}" if row["seeded"] else "")
            if path is None:
                failures.append(f"no image for {label}")
            print(f"{label}: row z {row['z']:.1f}, size class {row['class']} m, {path}", flush=True)
        if args.save:
            path = args.save if os.path.isabs(args.save) else os.path.join(pav.REPO_ROOT, args.save)
            editor.save(scene, path)
            print(f"saved {path}")
    except Exception as error:  # noqa: BLE001 - a failed step fails the run
        failures.append(f"{type(error).__name__}: {error}")
    finally:
        if (editor is not None) and args.keep_open and not failures:
            print(f"the editor keeps running: pid {editor.process.pid}; stop it by that pid when done")
        else:
            for launched in pav.LAUNCHED:
                launched.stop()
    for failure in failures:
        print(f"FAIL: {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
