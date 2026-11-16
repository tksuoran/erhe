#!/usr/bin/env python3
"""Build the comparison scene of the recreated kit shapes and render one image per pair.

See doc/editor/level_tools.md "Recreated shapes". Every shape that level_get_kits
lists with `recreates` stands beside its original at the default parameter
values: one row per pair on its own ground pad, the original on the left (-X),
the recreated shape on the right (+X), a fixed gap between their bounds. A
seeded shape makes three rows, at seeds 1, 3 and 17. The rows run toward -Z.

Each pair is rendered from the same three-quarter view for every pair of its
size class into logs/shape_compare_<original id>.png (a seeded shape:
logs/shape_compare_<original id>_<seed>.png).

Usage:
    python3 scripts/level_shape_compare.py [--editor <editor binary>] [--only <id prefix>] [--gap <meters>]
                                           [--save <scene path>] [--keep-open]

--save writes the scene (a .gltf or .glb path) so the windowed editor can open it;
--keep-open leaves the launched editor running with the scene and prints its pid.
"""

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prepared_asset_verify as pav  # noqa: E402
from prepared_asset_verify import Editor  # noqa: E402
import level_tools_verify as ltv  # noqa: E402

EDITOR_CANDIDATES = ("build_xcode_metal_headless/bin/Debug/editor", ltv.HEADLESS_EDITOR, ltv.DEBUG_EDITOR)
SEEDS = (1, 3, 17)
# Half the largest extent of a pair with its gap, in meters: every pair of one class is rendered by the same camera.
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


def list_pairs(editor, scene, only):
    """(original, recreated, seed) per row: the catalog's recreated shapes in catalog order."""
    shapes = editor.call("level_get_kits", {"scene_name": scene})["shapes"]
    by_id = {shape["id"]: shape for shape in shapes}
    rows = []
    for shape in shapes:
        original_id = shape["recreates"]
        if (not original_id) or (original_id not in by_id) or not original_id.startswith(only):
            continue
        for seed in (SEEDS if shape["seeded"] else SEEDS[:1]):
            rows.append({"original": original_id, "recreated": shape["id"], "seed": seed, "seeded": shape["seeded"]})
    return rows


def measure(editor, scene, rows):
    """The local bounds of every row's two pieces, read from a placement that is undone."""
    requests = []
    for row in rows:
        for key in ("original", "recreated"):
            requests.append({"shape": row[key], "position": [0.0, 0.0, 0.0], "parameters": {"seed": row["seed"]}})
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": requests})["pieces"]
    editor.wait_idle()
    for index, row in enumerate(rows):
        row["original_box"] = local_box(placed[2 * index])
        row["recreated_box"] = local_box(placed[(2 * index) + 1])
    editor.call("undo")
    editor.wait_idle()


def lay_out(rows, gap):
    """Each row's piece positions, pad and camera; the rows run toward -Z, far enough apart that a camera sees its row alone."""
    z = 0.0
    previous_class = None
    for row in rows:
        a = row["original_box"]
        b = row["recreated_box"]
        row["original_x"] = (-0.5 * gap) - a[1][0]
        row["recreated_x"] = (0.5 * gap) - b[0][0]
        min_x = row["original_x"] + a[0][0]
        max_x = row["recreated_x"] + b[1][0]
        min_z = min(a[0][2], b[0][2])
        max_z = max(a[1][2], b[1][2])
        height = max(a[1][1], b[1][1])
        half_extent = 0.5 * max(max_x - min_x, max_z - min_z, height) + PAD_MARGIN
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


def camera_of(row):
    """The camera of the row's size class, relative to the row's center on the ground."""
    c = row["class"]
    distance = 4.2 * c
    el = math.radians(ELEVATION_DEGREES)
    az = math.radians(AZIMUTH_DEGREES)
    target = [row["center"][0], 0.45 * c, row["center"][2]]
    eye = [target[0] + (distance * math.cos(el) * math.sin(az)), target[1] + (distance * math.sin(el)), target[2] + (distance * math.cos(el) * math.cos(az))]
    # The far plane ends the view behind the row, so the rows beyond it stay out of the image.
    return {"eye": eye, "target": target, "fov_y_degrees": FOV_Y_DEGREES, "near": 0.05 * c, "far": distance + (1.6 * c)}


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


def build(editor, scene, rows):
    requests = []
    for row in rows:
        label = f"{row['original']} seed {row['seed']}" if row["seeded"] else row["original"]
        requests.extend(pad_tiles(row["pad"], f"pad {label}"))
        requests.append({"shape": row["original"], "position": [row["original_x"], 0.0, row["z"]], "name": f"{label} original",
                         "parameters": {"seed": row["seed"]}})
        requests.append({"shape": row["recreated"], "position": [row["recreated_x"], 0.0, row["z"]], "name": f"{label} recreated",
                         "parameters": {"seed": row["seed"]}})
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": requests})["pieces"]
    editor.wait_idle()
    return placed


def render(editor, scene, row):
    name = f"shape_compare_{row['original']}" + (f"_{row['seed']}" if row["seeded"] else "") + ".png"
    path = os.path.join(pav.REPO_ROOT, "logs", name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.isfile(path):
        os.remove(path)
    reply = editor.call("render_scene_image", {"scene": scene, "camera": camera_of(row), "width": IMAGE_WIDTH, "height": IMAGE_HEIGHT,
                                               "output": "png", "path": path})
    return path if (os.path.isfile(path) and (reply.get("width") == IMAGE_WIDTH)) else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=default_editor(), help="editor binary (relative to the repository root or absolute)")
    parser.add_argument("--only", default="", help="compare only the originals whose id starts with this (for example basic. or nature.rock)")
    parser.add_argument("--gap", type=float, default=1.0, help="meters between the two shapes' bounds")
    parser.add_argument("--save", default="", help="save the comparison scene to this .gltf or .glb path")
    parser.add_argument("--keep-open", action="store_true", help="leave the launched editor running with the scene")
    args = parser.parse_args()

    failures = []
    editor = None
    try:
        editor = Editor(args.editor)
        scene = ltv.create_scene(editor)
        rows = list_pairs(editor, scene, args.only)
        if not rows:
            raise RuntimeError(f"level_get_kits lists no recreated shape whose original starts with '{args.only}'")
        measure(editor, scene, rows)
        lay_out(rows, args.gap)
        placed = build(editor, scene, rows)
        if len(placed) < 3 * len(rows):
            failures.append(f"placed {len(placed)} pieces for {len(rows)} rows")
        print(f"scene '{scene}': {len(rows)} rows, original at -X, recreated at +X, {args.gap} m apart; the rows run toward -Z")
        for row in rows:
            path = render(editor, scene, row)
            label = f"{row['original']} | {row['recreated']}" + (f" seed {row['seed']}" if row["seeded"] else "")
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
