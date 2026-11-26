#!/usr/bin/env python3
"""Build a gallery scene of the level kit shapes that have a former build.

Each shape whose build was fixed keeps its former build as "<id>_old"
(doc/editor/level_kit_shapes.md "Former builds"). The gallery places every such
pair side by side, the former build on the left (-X) and the current build on
the right (+X), at the values that show the fix, renders a close view of each
pair to logs/level_kit_former_builds/<pair>.png and saves the scene to
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

# (pair name, shape id, parameters, extent in meters: the pair's spacing and the close view's distance, view height share)
PAIRS = [
    ("palm",                "nature.palm",       {"species": "coconut"},                         8.0, 0.75),
    ("palm_date",           "nature.palm",       {"species": "date"},                            8.0, 0.75),
    ("palm_clump",          "nature.palm_clump", {"species": "areca"},                           4.0, 0.6),
    ("quay_wall",           "harbour.quay_wall", {"batter": 10.0},                               3.0, 0.6),
    ("market_stall_peaked", "prop.market_stall", {"awning": "peaked"},                           3.0, 0.7),
    ("market_stall_sloped", "prop.market_stall", {"awning": "sloped"},                           3.0, 0.7),
    ("well_gable",          "prop.well",         {"roof": "gable"},                              2.5, 0.8),
    ("fire_campfire",       "light.fire",        {"form": "campfire"},                           2.0, 0.6),
    ("vent_duct_elbow",     "industrial.vent",   {"form": "duct_elbow"},                         1.5, 0.6),
    ("boat_rowboat",        "harbour.boat",      {"form": "rowboat"},                            8.0, 0.4),
    ("boat_skiff",          "harbour.boat",      {"form": "skiff"},                              8.0, 0.4),
    ("spring_extension",    "machine.spring",    {"form": "extension"},                          0.6, 0.5),
    ("spring_torsion",      "machine.spring",    {"form": "torsion"},                            0.8, 0.5),
]


def build_gallery(level):
    level.environment(sun_color=[1.0, 0.95, 0.85], sun_intensity=3.0, sun_pitch=-125.0, sun_yaw=35.0,
                      ambient=[0.2, 0.21, 0.24], shadow_range=60.0)
    x = 0.0
    views = []
    for name, shape, parameters, extent, view_height in PAIRS:
        center = x + extent
        level.place([
            level.piece(shape + "_old", f"{name} (old)", [center - (0.5 * extent), 0.0, 0.0], parameters=parameters),
            level.piece(shape,          f"{name} (new)", [center + (0.5 * extent), 0.0, 0.0], parameters=parameters),
        ])
        target = [center, view_height * extent * 0.8, 0.0]
        eye = [center + (0.25 * extent), target[1] + (0.6 * extent), 1.7 * extent]
        views.append((name, eye, target))
        x += 2.0 * extent + 1.0
    level.ground("ground", [0.5 * x, 0.0], x + 20.0, 40.0, 0.0, "Kit Grass", tiles_x=max(2, int(x / 40.0) + 1))
    overview_target = [0.5 * x, 1.5, 0.0]
    views.append(("overview", [0.5 * x, 0.35 * x, 0.55 * x], overview_target))
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
