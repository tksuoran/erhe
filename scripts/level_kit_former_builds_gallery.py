#!/usr/bin/env python3
"""Build a gallery scene of the level kit shapes that have a former build.

Each shape whose build was fixed keeps its former build as "<id>_old"
(doc/editor/level_kit_shapes.md "Former builds"). The gallery places every such
pair side by side in a row of its own, the former build on the left of its view
and the current build on the right, at the values that show the fix, renders a close view of each
pair to logs/level_kit_former_builds/former_builds_gallery_<pair>.png and saves the scene to
res/editor/scenes/level_tests/former_builds_gallery.glb.

Usage:
    python3 scripts/level_kit_former_builds_gallery.py [--editor <build>/bin/editor] [--port <port>] [--no-save]

Without --port the script launches the headless editor (ERHE_DETERMINISTIC_CLOCK=1,
ERHE_DEFAULT_LAYOUT=1) and stops it at the end; with --port it builds in the
editor listening there and leaves it running.
"""

import argparse
import os
import sys

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(SCRIPTS)
sys.path.insert(0, SCRIPTS)
sys.path.insert(0, os.path.join(SCRIPTS, "creations"))

import level_test_levels as levels  # noqa: E402
from erhe_mcp import McpClient  # noqa: E402

DEFAULT_EDITOR = os.path.join("build_ninja_linux_vulkan_headless", "bin", "editor")
IMAGE_DIR = os.path.join(REPO, "logs", "level_kit_former_builds")
SCENE_PATH = os.path.join(REPO, "res", "editor", "scenes", "level_tests", "former_builds_gallery.glb")

# (pair name, shape id, parameters, spacing: the pair's centers apart in meters, height: the taller one's in meters)
PAIRS = [
    ("palm",                "nature.palm",       {"species": "coconut"},  7.0, 12.0),
    ("palm_date",           "nature.palm",       {"species": "date"},     7.0, 12.0),
    ("palm_clump",          "nature.palm_clump", {"species": "areca"},    4.0, 4.5),
    ("quay_wall",           "harbour.quay_wall", {"batter": 10.0},        3.0, 3.2),
    ("market_stall_peaked", "prop.market_stall", {"awning": "peaked"},    3.0, 2.5),
    ("market_stall_sloped", "prop.market_stall", {"awning": "sloped"},    3.0, 2.5),
    ("well_gable",          "prop.well",         {"roof": "gable"},       3.0, 3.0),
    ("fire_campfire",       "light.fire",        {"form": "campfire"},    2.0, 1.4),
    ("vent_duct_elbow",     "industrial.vent",   {"form": "duct_elbow"},  1.5, 1.5),
    ("boat_rowboat",        "harbour.boat",      {"form": "rowboat"},     3.0, 1.2),
    ("boat_skiff",          "harbour.boat",      {"form": "skiff"},       3.0, 1.2),
    ("spring_extension",    "machine.spring",    {"form": "extension"},   0.5, 0.3),
    ("spring_torsion",      "machine.spring",    {"form": "torsion"},     0.8, 0.5),
]


def build_gallery(level):
    level.environment(sun_color=[1.0, 0.95, 0.85], sun_intensity=3.0, sun_pitch=-125.0, sun_yaw=35.0,
                      ambient=[0.2, 0.21, 0.24], shadow_range=60.0)
    # Each pair in a row of its own along Z, seen from -Z at an angle, so the rows already placed stay behind the
    # view: the former build at +X shows on the left. The boats lie along X: they pair along Z, the former one at +Z.
    z = 0.0
    views = []
    for name, shape, parameters, spacing, height in PAIRS:
        along_z = shape == "harbour.boat"
        old_position = [0.0, 0.0, z + (0.5 * spacing)] if along_z else [0.5 * spacing, 0.0, z]
        new_position = [0.0, 0.0, z - (0.5 * spacing)] if along_z else [-0.5 * spacing, 0.0, z]
        level.place([
            level.piece(shape + "_old", f"{name} (old)", old_position, parameters=parameters),
            level.piece(shape,          f"{name} (new)", new_position, parameters=parameters),
        ])
        size = max(2.0 * spacing, height, 8.0 if along_z else 0.0)
        target = [0.0, 0.5 * height, z]
        if along_z:
            eye = [1.2 * size, (0.5 * height) + (0.5 * size), z - (0.3 * size)]
        else:
            eye = [0.7 * size, (0.5 * height) + (0.45 * size), z - (1.1 * size)]
        views.append((name, eye, target))
        z += max(spacing, 4.0) + 6.0
    level.ground("ground", [0.0, 0.5 * z], 60.0, z + 30.0, 0.0, "Kit Grass", tiles_x=2, tiles_z=max(2, int((z + 30.0) / 40.0) + 1))
    views.append(("overview", [40.0, 30.0, -20.0], [0.0, 1.5, 0.5 * z]))
    return views


def main():
    parser = argparse.ArgumentParser(description="Build the gallery of level kit shapes beside their former builds")
    parser.add_argument("--editor", default=DEFAULT_EDITOR, help="editor binary to launch (headless build)")
    parser.add_argument("--port", type=int, default=None, help="build in the editor listening on this MCP port instead")
    parser.add_argument("--no-save", action="store_true", help="build and render without saving")
    args = parser.parse_args()

    levels.IMAGE_DIR = IMAGE_DIR
    levels.SCENE_DIR = os.path.dirname(SCENE_PATH)
    editor = None
    os.chdir(REPO)
    if args.port is not None:
        client = McpClient(args.port)
    else:
        import prepared_asset_verify as pav
        editor = pav.Editor(args.editor)
        client = editor.client
    try:
        ok = levels.build(client, "former_builds_gallery", build_gallery, not args.no_save)
    finally:
        if editor is not None:
            editor.stop()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
