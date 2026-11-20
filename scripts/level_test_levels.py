#!/usr/bin/env python3
"""Build three test levels with the level kit and save each as a scene.

The levels exercise the level kit features through the editor's MCP server
(doc/editor/level_tools.md "MCP"):

- castle_courtyard: a closed segments layout of walls with gates and a second
  one of battlements on top, towers with roofs joined by connectors, a
  colonnade (a line layout of columns carrying joined entablatures), a raised
  keep reached by a spline stairs layout, a railing path layout, a ring of
  lamp posts, grid and line layouts of props, a guard post group duplicated
  and mirrored, a road segments layout, and surface-snapped scatters and a
  tree ring on hills outside the walls.
- harbour_cove: a beach of shore tiles (line layout) sloping into a water
  surface, a quay (line layout) carrying moorings joined to its top, a pier
  chain joined by connectors, boats, a buoy scatter, sea stacks and a rock arch,
  a headland with a surface-snapped lighthouse, palms, shells and plants
  scattered onto the sand by surface snap, a cliff path layout behind the
  beach and a harbour yard.
- hillside_ruins: a plateau with ruins (a segments layout of broken walls, a
  grid of cracked floor snapped onto it, placeholder blocks replaced by broken
  pillars), a stairs layout curving up to it, a stepping-stone path layout and
  torii snapped over a hill, a stream crossed by a joined bridge chain,
  bamboo and boulder scatters, and a workshop yard of joined pipes, an antenna
  on a container, a ladder on a scaffold and machine elements meshed on a
  table.

Each level is built in a new scene, checked with level_check (the counts are
printed), rendered to logs/level_test_levels/<level>_<view>.png, saved to
res/editor/scenes/level_tests/<level>.glb and loaded back to compare its kit
pieces and layouts with the built scene.

Usage:
    python3 scripts/level_test_levels.py [--editor <build>/bin/Debug/editor] [--only <level>]
                                         [--port <port>] [--no-save]

Without --port the script launches the headless editor (ERHE_DETERMINISTIC_CLOCK=1,
ERHE_DEFAULT_LAYOUT=1) and stops it at the end; with --port it builds in the
editor listening there and leaves it running.
"""

import argparse
import json
import math
import os
import sys
import time

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(SCRIPTS)
sys.path.insert(0, SCRIPTS)
sys.path.insert(0, os.path.join(SCRIPTS, "creations"))

from erhe_mcp import McpClient  # noqa: E402
from common import look_at_quaternion  # noqa: E402

DEFAULT_EDITOR = os.path.join("build_xcode_metal_headless", "bin", "Debug", "editor")
IMAGE_DIR = os.path.join(REPO, "logs", "level_test_levels")
SCENE_DIR = os.path.join(REPO, "res", "editor", "scenes", "level_tests")
IMAGE_WIDTH = 1600
IMAGE_HEIGHT = 900


def yaw(degrees):
    half = math.radians(degrees) * 0.5
    return [0.0, math.sin(half), 0.0, math.cos(half)]


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz]


def pitch(degrees):
    half = math.radians(degrees) * 0.5
    return [math.sin(half), 0.0, 0.0, math.cos(half)]


def corner(x, y, z):
    return {"position": [float(x), float(y), float(z)], "kind": "corner"}


def smooth(x, y, z):
    return {"position": [float(x), float(y), float(z)], "kind": "smooth"}


class Level:
    """One level scene; pieces and layouts are remembered by name."""

    def __init__(self, client, scene):
        self.client = client
        self.scene = scene
        self.ids = {}
        self.layouts = {}
        shapes = self.call("level_get_kits", {"scene_name": scene})["shapes"]
        self.default_derive = {shape["id"]: shape.get("default_derive", []) for shape in shapes}

    def call(self, tool, arguments=None):
        return self.client.call(tool, arguments or {})

    def with_derive(self, entry):
        """A given value of a parameter the shape derives from the seed by default takes it out of the mask."""
        parameters = entry.get("parameters")
        if parameters and ("derive" not in parameters):
            mask = self.default_derive.get(entry["shape"], [])
            if any(name in parameters for name in mask):
                entry = dict(entry, parameters=dict(parameters, derive=[name for name in mask if name not in parameters]))
        return entry

    def place(self, pieces, **options):
        pieces = [self.with_derive(piece) for piece in pieces]
        arguments = {"scene_name": self.scene, "pieces": pieces}
        arguments.update(options)
        placed = self.call("level_place_pieces", arguments)["pieces"]
        if len(placed) != len(pieces):
            raise RuntimeError(f"placed {len(placed)} of {len(pieces)} pieces")
        for piece in placed:
            self.ids[piece["name"]] = piece["id"]
        return placed

    def piece(self, shape, name, position, rotation_xyzw=None, parameters=None, materials=None, **extra):
        piece = {"shape": shape, "name": name, "position": [float(v) for v in position]}
        if rotation_xyzw is not None:
            piece["rotation_xyzw"] = rotation_xyzw
        if parameters is not None:
            piece["parameters"] = parameters
        if materials is not None:
            piece["materials"] = materials
        piece.update(extra)
        return piece

    def join(self, shape, name, connector, to, to_connector, parameters=None, materials=None, gap=None):
        to_id = to if isinstance(to, int) else self.ids[to]
        connect = {"connector": connector, "to_piece_id": to_id, "to_connector": to_connector}
        if gap is not None:
            connect["gap"] = gap
        piece = {"shape": shape, "name": name, "connect": connect}
        if parameters is not None:
            piece["parameters"] = parameters
        if materials is not None:
            piece["materials"] = materials
        return self.place([piece])[0]

    def layout(self, name, position=None, rotation_xyzw=None, pattern=None, items=None, expect=None, **options):
        arguments = {"scene_name": self.scene, "name": name, "pattern": pattern or {}, "locked": True}
        if position is not None:
            arguments["position"] = [float(v) for v in position]
        if rotation_xyzw is not None:
            arguments["rotation_xyzw"] = rotation_xyzw
        if items is not None:
            arguments["items"] = [self.with_derive(item) for item in items]
        arguments.update(options)
        reply = self.call("level_create_layout", arguments)
        children = reply.get("children", [])
        if (expect is not None) and (len(children) != expect):
            raise RuntimeError(f"layout '{name}' has {len(children)} pieces, expected {expect}: {json.dumps(reply.get('run'))}")
        if not children:
            raise RuntimeError(f"layout '{name}' made no pieces: {json.dumps(reply.get('run'))}")
        self.layouts[name] = reply
        return reply

    def ground(self, name, center, size_x, size_z, top, material, thickness=0.2, tiles_x=2, tiles_z=2):
        """A grid layout of floor slab tiles (a slab is at most 64 m) whose tops are at height top."""
        tile_x = size_x / tiles_x
        tile_z = size_z / tiles_z
        start = [center[0] - 0.5 * size_x + 0.5 * tile_x, top - thickness, center[1] - 0.5 * size_z + 0.5 * tile_z]
        return self.layout(name, position=start, pattern={"kind": "grid", "count_x": tiles_x, "count_z": tiles_z,
                                                          "spacing_x": tile_x, "spacing_z": tile_z},
                           items=[{"shape": "basic.floor_slab", "parameters": {"width": tile_x, "depth": tile_z, "thickness": thickness},
                                   "materials": {"floor": material}}], expect=tiles_x * tiles_z)

    def height_at(self, x, z):
        """Height of the topmost surface at (x, z), read from a one-cell height map."""
        reply = self.call("level_get_height_map", {
            "scene_name": self.scene, "min": [x - 0.05, -50.0, z - 0.05], "max": [x + 0.05, 50.0, z + 0.05], "cell_size": 0.1
        })
        heights = [h for row in reply["heights"] for h in row if h is not None]
        if not heights:
            raise RuntimeError(f"no surface at ({x}, {z})")
        return max(heights)

    def group(self, names, name):
        return self.call("level_group_nodes", {"scene_name": self.scene, "node_ids": [self.ids[n] for n in names], "name": name})

    # Look

    def environment(self, sun_color, sun_intensity, sun_pitch, sun_yaw, ambient, shadow_range, fill=None):
        self.call("set_scene_settings", {
            "scene_name": self.scene, "ambient_light": ambient, "merge": True,
            "settings": {"sky": {"_version": 3, "enabled": True, "mode": 1}, "grid": {"_version": 3, "visible": False}}
        })
        self.call("create_light", {"scene_name": self.scene, "type": "directional", "name": "Sun",
                                   "position": [0.0, 40.0, 0.0], "color": sun_color, "intensity": sun_intensity})
        self.call("set_node_transform", {"scene_name": self.scene, "node_name": "Sun",
                                         "rotation_xyzw": quat_mul(yaw(sun_yaw), pitch(sun_pitch))})
        if fill is not None:
            self.call("create_light", dict({"scene_name": self.scene, "type": "point", "name": "Sky Fill", "cast_shadow": False}, **fill))
        cameras = self.call("get_scene_cameras", {"scene_name": self.scene})["cameras"]
        self.call("edit_camera", {"scene_name": self.scene, "camera_id": cameras[0]["id"],
                                  "shadow_range": float(shadow_range), "z_far": 600.0})

    def place_camera(self, eye, target):
        cameras = self.call("get_scene_cameras", {"scene_name": self.scene})["cameras"]
        name = cameras[0].get("node") or cameras[0].get("name")
        self.call("set_node_transform", {"scene_name": self.scene, "node_name": name,
                                         "translation": eye, "rotation_xyzw": look_at_quaternion(eye, target)})

    def render(self, level_name, views):
        os.makedirs(IMAGE_DIR, exist_ok=True)
        paths = []
        for suffix, eye, target in views:
            path = os.path.join(IMAGE_DIR, f"{level_name}_{suffix}.png")
            camera = {"eye": eye, "target": target, "fov_y_degrees": 50.0, "near": 0.1, "far": 600.0}
            self.call("render_scene_image", {"scene": self.scene, "camera": camera, "width": IMAGE_WIDTH,
                                             "height": IMAGE_HEIGHT, "output": "png", "path": path})
            paths.append(path)
        return paths


# --- Level 1: castle courtyard ----------------------------------------------------------

def build_castle_courtyard(level):
    level.environment(sun_color=[1.0, 0.92, 0.8], sun_intensity=3.0, sun_pitch=-128.0, sun_yaw=30.0,
                      ambient=[0.16, 0.17, 0.2], shadow_range=90.0)

    level.ground("ground", [0.0, 0.0], 80.0, 80.0, 0.0, "Kit Grass")
    level.place([
        level.piece("basic.floor_slab", "courtyard paving", [0, 0, 0], parameters={"width": 19.6, "depth": 19.6, "thickness": 0.05}),
    ])

    # The curtain wall: one closed curve of 20 points, 4 m apart, a gate in the middle of each side.
    half = 10.0
    side = [-10.0, -6.0, -2.0, 2.0, 6.0]
    wall_points = ([corner(x, 0, half) for x in side] + [corner(half, 0, -z) for z in side]
                   + [corner(-x, 0, -half) for x in side] + [corner(-half, 0, z) for z in side])
    wall = {"shape": "architecture.wall"}
    gate = {"shape": "architecture.door_wall"}
    level.layout("curtain wall", position=[0, 0, 0], pattern={"kind": "segments"}, points=wall_points, closed=True,
                 items=[wall, wall, gate, wall, wall], expect=20)
    level.layout("wall battlements", position=[0, 3.0, 0], pattern={"kind": "segments"}, points=wall_points, closed=True,
                 items=[{"shape": "architecture.battlement", "parameters": {"length": 4.0}}], expect=20)

    # Corner towers: two round ones with battlement tops, two square ones carrying joined pyramid roofs.
    level.place([
        level.piece("architecture.tower", "tower north-west", [-half, 0, -half], parameters={"height": 6.0}),
        level.piece("architecture.tower", "tower south-east", [half, 0, half], yaw(180.0), parameters={"height": 6.0}),
        level.piece("architecture.tower", "tower north-east", [half, 0, -half], yaw(-90.0),
                    parameters={"profile": "square", "height": 6.0, "top": "flat"}),
        level.piece("architecture.tower", "tower south-west", [-half, 0, half], yaw(90.0),
                    parameters={"profile": "square", "height": 6.0, "top": "flat"}),
    ])
    for tower in ["tower north-east", "tower south-west"]:
        level.join("architecture.roof", f"{tower} roof", "base", tower, "top",
                   parameters={"form": "pyramid", "width": 3.6, "depth": 3.6, "pitch": 45.0})

    # The keep: a slab on a grid of pillars, reached by a spline staircase that curves up to it.
    level.layout("keep pillars", position=[-3.0, 0.05, -8.0], pattern={"kind": "grid", "count_x": 3, "count_z": 2,
                                                                         "spacing_x": 3.0, "spacing_z": 3.0},
                 items=[{"shape": "architecture.pillar", "parameters": {"height": 2.95}}], expect=6)
    level.place([level.piece("basic.floor_slab", "keep floor", [0, 3.0, -6.5],
                             parameters={"width": 8.0, "depth": 4.0, "thickness": 0.3})])
    level.layout("keep stairs", position=[-7.5, 0.05, 2.0], pattern={"kind": "stairs", "stairs": {"width": 1.6}},
                 points=[smooth(0, 0, 0), smooth(0.5, 1.2, -3.5), smooth(2.0, 2.6, -6.5), smooth(3.5, 3.25, -8.5)],
                 expect=1)
    level.layout("keep railing", position=[-3.0, 3.3, -4.6], pattern={"kind": "path", "count": 4},
                 points=[corner(0, 0, 0), corner(6.0, 0, 0)],
                 items=[{"shape": "architecture.railing"}], expect=4)
    level.place([
        level.piece("architecture.stone_lantern", "keep lantern west", [-3.4, 3.3, -7.8]),
        level.piece("architecture.stone_lantern", "keep lantern east", [3.4, 3.3, -7.8]),
    ])

    # A colonnade on the east side: a line of columns, each carrying an entablature joined to its top.
    level.place([level.piece("basic.floor_slab", "stylobate", [6.5, 0.05, 1.0],
                             parameters={"width": 2.4, "depth": 10.4, "thickness": 0.3}, materials={"floor": "Kit Stone"})])
    colonnade = level.layout("colonnade", position=[6.5, 0.35, -3.0], rotation_xyzw=yaw(-90.0),
                             pattern={"kind": "line", "count": 5, "step": 2.0},
                             items=[{"shape": "architecture.column"}], expect=5)
    for i, child in enumerate(colonnade["children"]):
        level.join("architecture.entablature", f"entablature {i + 1}", "base", child["id"], "top")

    # The square: a well with a ring of lamp posts, market stalls along the west wall, a grid of crates and barrels.
    level.place([level.piece("prop.well", "well", [0, 0.05, 2.0])])
    level.layout("well lamps", position=[0, 0.05, 2.0 + 3.5], pattern={"kind": "ring", "count": 4, "radius": 3.5},
                 items=[{"shape": "light.lamp_post", "parameters": {"head": "lantern"}}], expect=4)
    level.layout("market", position=[-8.2, 0.05, 3.2], rotation_xyzw=yaw(-90.0), pattern={"kind": "line", "count": 3, "step": 2.4},
                 items=[{"shape": "prop.market_stall", "parameters": {"awning": awning}, "materials": {"cloth": cloth}}
                        for awning, cloth in [("peaked", "Kit Red Cloth"), ("sloped", "Kit Blue Cloth"), ("flat", "Kit Cloth")]],
                 expect=3)
    level.layout("supplies", position=[4.0, 0.05, 6.0], pattern={"kind": "grid", "count_x": 3, "count_z": 2,
                                                                  "spacing_x": 1.1, "spacing_z": 1.1},
                 items=[{"shape": "prop.crate"}, {"shape": "prop.barrel"}], expect=6)
    level.place([
        level.piece("prop.cart", "cart", [-4.0, 0.05, 6.5], yaw(25.0)),
        level.piece("prop.table", "table", [2.0, 0.05, -2.0]),
        level.piece("prop.pot", "pot 1", [1.2, 0.05, -2.9]),
        level.piece("prop.pot", "pot 2", [2.8, 0.05, -2.9], parameters={"seed": 2}),
    ])

    # A guard post inside the south gate, grouped, duplicated to the other side and mirrored.
    level.place([
        level.piece("light.fire", "brazier", [-3.2, 0.05, 8.6]),
        level.piece("prop.crate", "guard crate", [-4.4, 0.05, 8.9]),
        level.piece("prop.barrel", "guard barrel", [-4.4, 0.05, 7.8]),
        level.piece("prop.signpost", "guard sign", [-3.2, 0.05, 7.4], yaw(-20.0)),
    ])
    guard = level.group(["brazier", "guard crate", "guard barrel", "guard sign"], "guard post west")
    copy = level.call("level_duplicate_group", {"scene_name": level.scene, "group_id": guard["id"],
                                                "position": [-guard["position"][0], guard["position"][1], guard["position"][2]],
                                                "name": "guard post east"})
    level.call("level_mirror_group", {"scene_name": level.scene, "group_id": copy["id"], "axis": "x"})

    # Outside: a road from the south gate lined with lamp posts, hills with a tree ring and scatters.
    road_points = [corner(0, 0, 10.4), corner(1.0, 0, 16.0), corner(4.0, 0, 22.0), corner(9.0, 0, 27.0), corner(15.0, 0, 30.0)]
    level.layout("road", position=[0, 0, 0], pattern={"kind": "segments"}, points=road_points,
                 items=[{"shape": "basic.floor_slab", "parameters": {"width": 2.0, "depth": 3.0, "thickness": 0.06},
                         "materials": {"floor": "Kit Earth"}}], expect=4)
    level.layout("road lamps", position=[2.0, 0, 11.0], pattern={"kind": "path", "count_mode": "gap", "gap": 4.0},
                 points=[smooth(0, 0, 0), smooth(1.0, 0, 5.0), smooth(4.0, 0, 11.0), smooth(9.5, 0, 15.5)],
                 items=[{"shape": "light.lamp_post", "parameters": {"head": "hood"}}])
    level.place([
        level.piece("nature.hill", "west hill", [-24, 0, 12], parameters={"radius": 8.0, "height": 2.5}),
        level.piece("nature.hill", "east hill", [24, 0, -8], parameters={"radius": 9.0, "height": 3.0, "seed": 3}),
        level.piece("nature.hill", "north hill", [-6, 0, -26], parameters={"radius": 10.0, "height": 2.0, "seed": 5}),
    ])
    level.layout("east hill trees", position=[24, 6, -8 + 5.5], pattern={"kind": "ring", "count": 9, "radius": 5.5},
                 items=[{"shape": "nature.tree"}, {"shape": "nature.tree", "materials": {"foliage": "Kit Autumn Foliage"}}],
                 surface_snap=True, surface_forward_distance=8.0, surface_backward_distance=8.0, per_piece_seed=True, expect=9)
    level.layout("west meadow", position=[-24, 5, 12], pattern={"kind": "scatter", "scatter_mode": "count", "count": 40,
                                                                "region": "disc", "region_radius": 9.0, "min_spacing": 0.5},
                 items=[{"shape": "nature.grass_tuft", "weight": 5}, {"shape": "nature.bush", "weight": 2},
                        {"shape": "nature.rock"}, {"shape": "nature.tree"}],
                 surface_snap=True, surface_forward_distance=8.0, surface_backward_distance=8.0, per_piece_seed=True)
    level.layout("north wood", position=[-6, 5, -27], pattern={"kind": "scatter", "scatter_mode": "count", "count": 18,
                                                               "region": "rectangle", "size_x": 24.0, "size_z": 9.0, "min_spacing": 1.5},
                 items=[{"shape": "nature.tree", "weight": 3}, {"shape": "nature.blocky_tree"}, {"shape": "nature.bush", "weight": 2}],
                 surface_snap=True, surface_forward_distance=8.0, surface_backward_distance=8.0, per_piece_seed=True)

    eye = [30.0, 24.0, 36.0]
    level.place_camera(eye, [0.0, 1.0, 0.0])
    return [
        ("overview", eye, [0.0, 1.0, 0.0]),
        ("courtyard", [-6.0, 7.5, 9.0], [2.0, 1.5, -4.0]),
        ("gate", [3.0, 2.2, 24.0], [0.0, 2.5, 8.0]),
    ]


# --- Level 2: harbour cove ------------------------------------------------------------

def build_harbour_cove(level):
    level.environment(sun_color=[1.0, 0.88, 0.7], sun_intensity=3.0, sun_pitch=-132.0, sun_yaw=200.0,
                      ambient=[0.17, 0.18, 0.22], shadow_range=100.0)

    # Land at y 1 behind the beach and the quay; the sea bed at y -2; the water surface at y 0.5.
    level.ground("land", [0.0, -19.0], 76.0, 32.0, 1.0, "Kit Grass", tiles_z=1)
    level.ground("sea bed", [0.0, 16.0], 76.0, 38.0, -2.0, "Kit Sand", tiles_z=1)
    # The water reaches past the sea bed, so no sea bed edge shows beyond it.
    level.place([level.piece("nature.water", "sea west", [-20.0, 0.5, 17.5], parameters={"width": 40.0, "depth": 41.0}),
                 level.piece("nature.water", "sea east", [20.0, 0.5, 17.5], parameters={"width": 40.0, "depth": 41.0})])

    # The beach: a line of shore tiles from the land down into the water.
    level.layout("beach", position=[-34.0, 0, 0.0], pattern={"kind": "line", "count": 9, "step": 4.0},
                 items=[{"shape": "nature.shore", "parameters": {"seed": s}} for s in (1, 2, 3)], expect=9)
    # Under the shallow water the beach continues as a slope down to the sea bed.
    level.layout("beach shelf", position=[-34.0, -2.0, 6.0], pattern={"kind": "line", "count": 9, "step": 4.0},
                 items=[{"shape": "nature.shore", "parameters": {"rise": 2.0, "seed": 4}}], expect=9)

    # The quay: a line of quay walls standing on the sea bed, moorings joined to every other top.
    quay = level.layout("quay", position=[1.0, -2.0, -2.5], pattern={"kind": "line", "count": 15, "step": 2.0},
                        items=[{"shape": "harbour.quay_wall"}], expect=15)
    for i, child in enumerate(quay["children"][1::3]):
        level.join("harbour.mooring", f"mooring {i + 1}", "base", child["id"], "top")
    level.layout("quay lamps", position=[4.0, 1.0, -4.0], pattern={"kind": "line", "count": 5, "step": 6.0},
                 items=[{"shape": "light.lamp_post", "parameters": {"head": "globe", "arms": 1}}], expect=5)

    # The pier: six sections joined end to start, running out to sea from the quay.
    level.place([level.piece("harbour.pier", "pier 1", [17.0, -2.0, -1.0], yaw(-90.0), parameters={"braces": "cross"})])
    for i in range(2, 7):
        level.join("harbour.pier", f"pier {i}", "start", f"pier {i - 1}", "end", parameters={"braces": "cross", "seed": i})
    level.join("light.lantern", "pier lantern", "base", "pier 6", "right")

    # Boats and buoys on the water.
    level.place([
        level.piece("harbour.boat", "rowboat", [13.5, 0.2, 6.0], yaw(-80.0), parameters={"form": "rowboat"}),
        level.piece("harbour.boat", "skiff", [21.5, 0.15, 4.5], yaw(-100.0), parameters={"form": "skiff", "length": 5.0}),
        level.piece("harbour.boat", "barge", [28.0, 0.1, 6.0], yaw(-90.0), parameters={"form": "barge", "length": 8.0, "beam": 2.6}),
        level.piece("harbour.boat", "beached boat", [-14.0, 0.75, -0.5], yaw(30.0), parameters={"form": "rowboat", "heel": 12.0}),
    ])
    level.layout("buoys", position=[6.0, 0.1, 24.0], pattern={"kind": "scatter", "scatter_mode": "count", "count": 6,
                                                               "region": "rectangle", "size_x": 40.0, "size_z": 8.0, "min_spacing": 4.0},
                 items=[{"shape": "harbour.buoy"}])

    # Rocks in the sea: stacks, an arch, and a headland carrying the lighthouse.
    level.place([
        level.piece("nature.rock_spire", "sea stack", [-20.0, -2.0, 22.0], parameters={"form": "stack", "height": 9.0}),
        level.piece("nature.rock_spire", "pinnacle", [-12.0, -2.0, 28.0], parameters={"form": "pinnacle", "height": 7.0, "seed": 3}),
        level.piece("nature.rock_arch", "sea arch", [-27.0, -2.0, 15.0], yaw(30.0)),
        level.piece("nature.land_mass", "headland", [-34.0, -2.0, 24.0], parameters={"footprint": 12.0, "height": 4.5, "erosion": 0.3}),
        level.piece("nature.boulder_cluster", "headland boulders", [-28.5, -2.0, 23.0], parameters={"radius": 2.5}),
    ])
    level.place([level.piece("harbour.lighthouse", "lighthouse", [-34.0, 8.0, 24.0], parameters={"gallery": "rail"},
                             surface_snap=True)], surface_max_distance=8.0)

    # The beach plants, snapped down onto the sand.
    level.layout("beach palms", position=[-17.0, 4.0, -2.0], pattern={"kind": "scatter", "scatter_mode": "count", "count": 6,
                                                                       "region": "rectangle", "size_x": 32.0, "size_z": 4.0,
                                                                       "min_spacing": 0.0},
                 items=[{"shape": "nature.palm", "parameters": {"species": species}}
                        for species in ["coconut", "date", "royal", "fan", "bottle"]],
                 surface_snap=True, surface_forward_distance=6.0, surface_backward_distance=6.0, per_piece_seed=True)
    level.layout("beach shells", position=[-17.0, 2.0, 1.5], pattern={"kind": "scatter", "scatter_mode": "count", "count": 16,
                                                                       "region": "rectangle", "size_x": 30.0, "size_z": 2.0,
                                                                       "min_spacing": 0.6},
                 items=[{"shape": "nature.shell"}, {"shape": "nature.succulent"}, {"shape": "nature.rock"}],
                 surface_snap=True, surface_forward_distance=4.0, surface_backward_distance=4.0, per_piece_seed=True)
    level.layout("dune grass", position=[-17.0, 3.0, -5.0], pattern={"kind": "scatter", "scatter_mode": "count", "count": 24,
                                                                      "region": "rectangle", "size_x": 32.0, "size_z": 3.0,
                                                                      "min_spacing": 0.6},
                 items=[{"shape": "nature.grass_tuft", "weight": 3}, {"shape": "nature.palm_shrub"},
                        {"shape": "nature.leaf_plant"}],
                 surface_snap=True, surface_forward_distance=4.0, surface_backward_distance=4.0, per_piece_seed=True)

    # The cliffs behind the beach: a path layout along a curve, each cliff tangent to it.
    level.layout("cliffs", position=[-36.0, 1.0, -12.0], pattern={"kind": "path", "count": 9},
                 points=[smooth(0, 0, 0), smooth(11.0, 0, -4.0), smooth(22.0, 0, 0.0), smooth(32.0, 0, -3.0)],
                 items=[{"shape": "nature.cliff", "parameters": {"strata": strata, "height": 5.0}}
                        for strata in ["layered", "massive", "blocky"]])

    # The harbour yard behind the quay.
    level.place([
        level.piece("harbour.crane", "crane", [10.0, 1.0, -5.5], yaw(180.0)),
        level.piece("industrial.container", "container", [27.0, 1.0, -9.0], materials={"metal": "Kit Rust"}),
        level.piece("harbour.cargo", "cargo loose", [14.0, 1.0, -4.8]),
    ])
    level.layout("cargo stack", position=[19.0, 1.0, -8.0], pattern={"kind": "grid", "count_x": 3, "count_z": 2,
                                                                      "spacing_x": 1.3, "spacing_z": 1.3},
                 items=[{"shape": "harbour.cargo"}, {"shape": "prop.barrel"}, {"shape": "prop.crate"}], expect=6)
    level.layout("fish market", position=[4.0, 1.0, -9.0], pattern={"kind": "line", "count": 3, "step": 2.6},
                 items=[{"shape": "prop.market_stall", "parameters": {"awning": "sloped"}, "materials": {"cloth": cloth}}
                        for cloth in ["Kit Blue Cloth", "Kit Cloth", "Kit Red Cloth"]], expect=3)
    level.layout("bunting", position=[3.0, 1.0, -7.0], pattern={"kind": "line", "count": 3, "step": 4.0},
                 items=[{"shape": "light.garland"}], expect=3)

    eye = [2.0, 36.0, 60.0]
    level.place_camera(eye, [-3.0, 0.0, 2.0])
    return [
        ("overview", eye, [-3.0, 0.0, 2.0]),
        ("headland", [-14.0, 8.0, 38.0], [-30.0, 4.0, 20.0]),
        ("beach", [-6.0, 4.0, 16.0], [-20.0, 1.5, -2.0]),
        ("quay", [30.0, 6.0, 14.0], [14.0, 0.5, -2.0]),
    ]


# --- Level 3: hillside ruins ------------------------------------------------------------

def build_hillside_ruins(level):
    level.environment(sun_color=[1.0, 0.95, 0.86], sun_intensity=3.0, sun_pitch=-125.0, sun_yaw=150.0,
                      ambient=[0.17, 0.18, 0.2], shadow_range=100.0)

    level.ground("ground", [0.0, 0.0], 80.0, 80.0, 0.0, "Kit Grass")
    level.place([
        level.piece("nature.land_mass", "plateau", [12.0, 0, -10.0], parameters={"footprint": 16.0, "height": 3.0, "erosion": 0.15}),
        level.piece("nature.hill", "path hill", [-10.0, 0, 8.0], parameters={"radius": 7.0, "height": 1.6, "seed": 2}),
        level.piece("nature.hill", "far hill", [-22.0, 0, -22.0], parameters={"radius": 10.0, "height": 3.5, "seed": 4}),
        level.piece("nature.ground_patch", "worn ground", [-14.0, 0, -6.0]),
    ])

    # The ruins on the plateau: the top height is read from the scene.
    top = level.height_at(12.0, -10.0)
    level.place([level.piece("architecture.stepped_platform", "temple base", [12.0, top - 0.25, -11.0],
                             parameters={"width": 9.0, "depth": 7.0, "tiers": 3})])
    deck = level.height_at(12.0, -11.0)
    level.layout("temple floor", position=[9.0, deck + 1.0, -13.0], pattern={"kind": "grid", "count_x": 4, "count_z": 3,
                                                                             "spacing_x": 2.0, "spacing_z": 2.0},
                 items=[{"shape": "ruin.cracked_floor", "parameters": {"seed": s}} for s in (1, 2, 3)],
                 surface_snap=True, surface_forward_distance=2.0, surface_backward_distance=2.0, per_piece_seed=True, expect=12)
    floor = level.height_at(12.0, -11.0)
    level.layout("temple walls", position=[0, 0, 0], pattern={"kind": "segments"},
                 points=[corner(8.0, floor, -8.0), corner(8.0, floor, -14.0), corner(16.0, floor, -14.0), corner(16.0, floor, -8.0)],
                 items=[{"shape": "ruin.broken_wall", "parameters": {"break": brk, "seed": s}}
                        for brk, s in [("stepped", 1), ("jagged", 2), ("slope", 3)]], expect=3)
    # Placeholder blocks where the colonnade stood, replaced by broken pillars.
    placeholders = [f"placeholder {i + 1}" for i in range(4)]
    level.place([level.piece("basic.block", name, [9.0 + 2.0 * i, floor, -8.6],
                             parameters={"width": 0.6, "height": 1.0, "depth": 0.6}) for i, name in enumerate(placeholders)])
    replaced = level.call("level_replace_pieces", {"scene_name": level.scene, "node_ids": [level.ids[n] for n in placeholders],
                                                   "shape": "ruin.broken_pillar"})
    if replaced["replaced_count"] != len(placeholders):
        raise RuntimeError(f"replaced {replaced['replaced_count']} of {len(placeholders)} placeholders")
    level.place([
        level.piece("ruin.broken_arch", "temple gate", [12.0, floor, -7.6]),
        level.piece("architecture.column", "standing column", [15.0, floor, -12.5]),
    ])
    level.join("architecture.entablature", "fallen lintel rest", "base", "standing column", "top")
    level.layout("temple rubble", position=[12.0, floor + 2.0, -11.0], pattern={"kind": "scatter", "scatter_mode": "count",
                                                                                 "count": 10, "region": "rectangle",
                                                                                 "size_x": 7.0, "size_z": 5.0, "min_spacing": 0.8},
                 items=[{"shape": "ruin.rubble"}], surface_snap=True, surface_forward_distance=3.0, surface_backward_distance=3.0, per_piece_seed=True)

    # A staircase that curves up the plateau's west flank to the temple base.
    level.layout("plateau stairs", position=[-1.0, 0.02, -2.0], pattern={"kind": "stairs", "stairs": {"width": 1.8}},
                 points=[smooth(0, 0, 0), smooth(2.5, 0.8, -1.5), smooth(4.5, 2.0, -5.0), smooth(6.8, top, -8.0)], expect=1)

    # A stepping-stone path over the hill to the stairs, with torii along it.
    path_points = [smooth(0, 0, 0), smooth(6.0, 0, -3.0), smooth(12.0, 0, -8.0), smooth(16.0, 0, -14.0), smooth(20.0, 0, -15.5)]
    level.layout("stepping stones", position=[-21.0, 4.0, 13.5], pattern={"kind": "path", "count_mode": "gap", "gap": 0.35},
                 points=path_points, items=[{"shape": "nature.stepping_stone"}],
                 surface_snap=True, surface_forward_distance=6.0, surface_backward_distance=6.0, per_piece_seed=True)
    level.layout("torii", position=[-21.0, 4.0, 13.5], pattern={"kind": "path", "count": 4},
                 points=path_points[:4], items=[{"shape": "architecture.torii", "materials": {"wood": "Kit Vermilion"}}],
                 surface_snap=True, surface_forward_distance=6.0, surface_backward_distance=6.0, expect=4)

    # A stream crossed by a stone bridge of three joined arched decks.
    level.place([
        level.piece("nature.water", "stream", [-2.0, 0.02, 24.0], yaw(90.0), parameters={"width": 30.0, "depth": 4.0}),
        level.piece("architecture.bridge_deck", "bridge 1", [-6.0, 0, 24.0],
                    parameters={"supports": "arched", "parapet": "solid", "height": 1.4}),
    ])
    level.join("architecture.bridge_deck", "bridge 2", "start", "bridge 1", "end",
               parameters={"supports": "arched", "parapet": "solid", "height": 1.4, "seed": 2})
    level.join("architecture.bridge_deck", "bridge 3", "start", "bridge 2", "end",
               parameters={"supports": "arched", "parapet": "solid", "height": 1.4, "seed": 3})

    # Vegetation: a bamboo grove, boulders, trees ringing the far hill and grass everywhere.
    level.layout("bamboo grove", position=[-26.0, 4.0, -2.0], pattern={"kind": "scatter", "scatter_mode": "count", "count": 8,
                                                                        "region": "disc", "region_radius": 6.0, "min_spacing": 2.0},
                 items=[{"shape": "nature.bamboo"}], surface_snap=True, surface_forward_distance=6.0, surface_backward_distance=6.0, per_piece_seed=True)
    level.layout("boulders", position=[24.0, 4.0, 12.0], pattern={"kind": "scatter", "scatter_mode": "count", "count": 6,
                                                                   "region": "rectangle", "size_x": 14.0, "size_z": 12.0, "min_spacing": 2.0},
                 items=[{"shape": "nature.boulder_cluster", "parameters": {"form": form}} for form in ["angular", "worn", "mixed"]],
                 surface_snap=True, surface_forward_distance=6.0, surface_backward_distance=6.0, per_piece_seed=True)
    level.layout("far hill trees", position=[-22.0, 8.0, -22.0 + 7.0], pattern={"kind": "ring", "count": 10, "radius": 7.0},
                 items=[{"shape": "nature.tree"}, {"shape": "nature.blocky_tree"}],
                 surface_snap=True, surface_forward_distance=10.0, surface_backward_distance=10.0, per_piece_seed=True, expect=10)
    level.layout("meadow", position=[0.0, 6.0, 4.0], pattern={"kind": "scatter", "scatter_mode": "count", "count": 50,
                                                               "region": "rectangle", "size_x": 50.0, "size_z": 30.0, "min_spacing": 1.0},
                 items=[{"shape": "nature.grass_tuft", "weight": 6}, {"shape": "nature.bush", "weight": 2}, {"shape": "nature.rock"}],
                 surface_snap=True, surface_forward_distance=8.0, surface_backward_distance=8.0, per_piece_seed=True)

    # The workshop yard: a joined pipe run, tanks, a container with an antenna, a scaffold with a ladder leaning on it.
    level.place([
        level.piece("basic.floor_slab", "yard", [22.0, 0, 20.0], parameters={"width": 14.0, "depth": 10.0, "thickness": 0.1},
                    materials={"floor": "Kit Stone"}),
        level.piece("industrial.tank", "tank 1", [17.0, 0.1, 17.0]),
        level.piece("industrial.tank", "tank 2", [17.0, 0.1, 20.0], parameters={"seed": 2}),
        level.piece("industrial.pipe", "pipe 1", [19.5, 1.5, 17.0], parameters={"flanges": "ends"}),
    ])
    level.join("industrial.pipe", "pipe 2", "start", "pipe 1", "end", parameters={"form": "elbow", "flanges": "ends"})
    level.join("industrial.pipe", "pipe 3", "start", "pipe 2", "end", parameters={"flanges": "ends", "length": 3.0})
    level.join("industrial.pipe", "pipe 4", "start", "pipe 3", "end", parameters={"form": "tee", "flanges": "ends"})
    level.place([
        level.piece("industrial.container", "workshop container", [26.0, 0.1, 23.0], yaw(90.0)),
        level.piece("traversal.scaffold", "scaffold", [26.0, 0.1, 16.5]),
        level.piece("industrial.vent", "vent", [22.0, 0.1, 24.0]),
        level.piece("prop.table", "workbench", [21.0, 0.1, 21.5], parameters={"width": 2.0, "depth": 1.0}),
    ])
    level.join("industrial.antenna", "antenna", "base", "workshop container", "top")
    # The ladder leans on the scaffold's front, its top at the first deck (2.4 m).
    level.place([level.piece("traversal.ladder", "scaffold ladder", [26.4, 0.1, 17.0 + 2.4 * math.tan(math.radians(10.0)) + 0.06],
                             parameters={"height": 2.4})])

    # Machine elements on the workbench: two gears meshed, a bolt through a washer and a nut.
    bench_top = level.height_at(21.0, 21.5)
    level.place([level.piece("machine.gear", "gear 1", [20.6, bench_top, 21.5], parameters={"teeth": 24, "body": "spokes"})])
    level.join("machine.gear", "gear 2", "mesh", "gear 1", "mesh", parameters={"teeth": 12, "body": "web"})
    level.place([
        level.piece("machine.bolt", "bolt", [21.6, bench_top, 21.3]),
        level.piece("machine.spring", "spring", [21.6, bench_top, 21.8]),
        level.piece("machine.bearing", "bearing", [21.9, bench_top, 21.6]),
    ])
    level.join("machine.nut", "nut", "bolt", "bolt", "nut")

    eye = [30.0, 22.0, 40.0]
    level.place_camera(eye, [2.0, 1.0, -2.0])
    return [
        ("overview", eye, [2.0, 1.0, -2.0]),
        ("ruins", [2.0, 9.0, 2.0], [12.0, 3.0, -10.0]),
        ("yard", [12.0, 4.0, 30.0], [22.0, 1.0, 19.0]),
    ]


LEVELS = {
    "castle_courtyard": build_castle_courtyard,
    "harbour_cove": build_harbour_cove,
    "hillside_ruins": build_hillside_ruins,
}


# --- Scenes, saving and loading back -----------------------------------------------

def scene_names(client):
    return [scene["name"] for scene in client.call("list_scenes")["scenes"]]


def wait_frames(client, frames=1):
    client.call("wait_frames", {"frames": frames})


def wait_idle(client, timeout=600.0):
    deadline = time.monotonic() + timeout
    settled_count = 0
    while time.monotonic() < deadline:
        status = client.call("get_async_status")
        settled = status.get("idle", False) and all(
            status.get(field, 0) == 0 for field in ("pending_scene_commits", "queued_operations", "asset_loads"))
        settled_count = (settled_count + 1) if settled else 0
        if settled_count >= 3:
            return
    raise RuntimeError("the editor did not settle")


def new_scene(client):
    before = set(scene_names(client))
    client.call("create_scene")
    for _ in range(600):
        added = sorted(set(scene_names(client)) - before)
        if added:
            wait_idle(client)
            return added[0]
        wait_frames(client)
    raise RuntimeError("create_scene made no scene")


def summary(client, scene):
    pieces = client.call("level_get_pieces", {"scene_name": scene})["pieces"]
    layouts = client.call("level_get_layouts", {"scene_name": scene})["layouts"]
    shapes = sorted({piece["shape"] for piece in pieces})
    problems = [piece for piece in pieces if piece.get("problem")] + [layout for layout in layouts if layout.get("problem")]
    return {"pieces": len(pieces), "layouts": len(layouts), "shapes": shapes, "problems": len(problems)}


def save(client, scene, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    job = client.call("save_scene", {"scene_name": scene, "path": path, "set_as_source": True})
    deadline = time.monotonic() + 600.0
    while time.monotonic() < deadline:
        status = client.call("get_save_status", {"job_id": job["job_id"]})
        if status["state"] == "done":
            return status
        if status["state"] == "failed":
            raise RuntimeError(f"saving {scene} failed: {status.get('error')}")
        wait_frames(client)
    raise RuntimeError(f"saving {scene} did not finish")


def load_back(client, path):
    before = set(scene_names(client))
    client.call("load_scene", {"path": path})
    for _ in range(3000):
        added = sorted(set(scene_names(client)) - before)
        if added:
            wait_idle(client)
            return added[0]
        wait_frames(client)
    raise RuntimeError(f"load_scene {path} made no scene")


def build(client, name, builder, save_scene):
    scene = new_scene(client)
    client.call("set_active_scene", {"scene_name": scene})
    level = Level(client, scene)
    started = time.monotonic()
    views = builder(level)
    wait_idle(client)
    check = client.call("level_check", {"scene_name": scene})
    built = summary(client, scene)
    images = level.render(name, views)
    print(f"{name}: scene '{scene}', {built['pieces']} kit pieces in {len(built['shapes'])} shapes, "
          f"{built['layouts']} layouts, built in {time.monotonic() - started:.1f} s", flush=True)
    print(f"  level_check: {check['finding_count']} findings {json.dumps(check.get('counts', {}))}", flush=True)
    unsupported = sorted({name for finding in check.get("findings", []) if finding["kind"] == "unsupported" for name in finding["names"]})
    if unsupported:
        print(f"  unsupported: {', '.join(unsupported)}", flush=True)
    for image in images:
        print(f"  image: {image}", flush=True)
    if built["problems"]:
        raise RuntimeError(f"{name}: {built['problems']} pieces or layouts report a problem")
    if not save_scene:
        return True
    path = os.path.join(SCENE_DIR, f"{name}.glb")
    status = save(client, scene, path)
    print(f"  saved: {path} ({status.get('file_bytes', 0) / 1.0e6:.1f} MB)", flush=True)
    loaded_scene = load_back(client, path)
    loaded = summary(client, loaded_scene)
    same = (loaded["pieces"] == built["pieces"]) and (loaded["layouts"] == built["layouts"]) and (loaded["shapes"] == built["shapes"])
    print(f"  loaded back as '{loaded_scene}': {loaded['pieces']} kit pieces, {loaded['layouts']} layouts, "
          f"{loaded['problems']} problems -> {'same' if (same and not loaded['problems']) else 'DIFFERENT'}", flush=True)
    client.call("close_scene", {"scene_name": loaded_scene})
    wait_idle(client)
    return same and (loaded["problems"] == 0)


def main():
    parser = argparse.ArgumentParser(description="Build three level kit test levels and save each as a scene")
    parser.add_argument("--editor", default=DEFAULT_EDITOR, help="editor binary to launch (headless build)")
    parser.add_argument("--port", type=int, default=None, help="build in the editor listening on this MCP port instead")
    parser.add_argument("--only", choices=sorted(LEVELS), action="append", help="build only this level (repeatable)")
    parser.add_argument("--no-save", action="store_true", help="build and render without saving")
    args = parser.parse_args()

    editor = None
    os.chdir(REPO)
    if args.port is not None:
        client = McpClient(args.port)
    else:
        import prepared_asset_verify as pav
        editor = pav.Editor(args.editor)
        client = editor.client
    ok = True
    try:
        for name in (args.only or list(LEVELS)):
            ok = build(client, name, LEVELS[name], not args.no_save) and ok
    finally:
        if editor is not None:
            editor.stop()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
