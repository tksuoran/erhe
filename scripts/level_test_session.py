#!/usr/bin/env python3
"""Launch an editor for a hands-on level tools test and build the test level in it.

See doc/agents/editor_runs.md "Handing the editor to the user".

Usage:
    python3 scripts/level_test_session.py --editor <build>/bin/Debug/editor [--build <build tree>]

Every step is checked and the script stops with a message at the first one
that fails: the config reset, the build, the launch (the MCP port is read
for the launched pid only), each placed piece (the beach corner's palms of
each species, leaf plants of each form, palm clumps and palm shrubs snapped
onto the shore's sand; spires in the water, basalt columns, a dune and a
ground patch),
each join of the cultural corners (a classical colonnade carrying its
entablatures, a medieval wall run with battlements between two towers, torii
along a path, a roof on a walled room), each layout's child count,
the viewport that shows the level, and the connectors the viewport draws.
The editor keeps running for the user; its pid is printed at the end.
"""

import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import mcp_call  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parent.parent
LOG = REPO / "logs" / "log.txt"
FLOATING_WINDOWS = ["PVS", "LOD", "Light Mixer"]


class Step_failure(Exception):
    pass


def require(condition, message):
    if not condition:
        raise Step_failure(message)


def run(command, what):
    result = subprocess.run(command, cwd=REPO, capture_output=True, text=True)
    require(result.returncode == 0, f"{what} failed:\n{(result.stdout + result.stderr)[-2000:]}")
    return result.stdout


def running_editors():
    result = subprocess.run(["pgrep", "-lf", "bin/.*/editor"], capture_output=True, text=True)
    return [line for line in result.stdout.splitlines() if line.strip()]


def build(tree):
    # A new source file reaches an Xcode project only through a configure run (doc/agents/macos.md).
    run(["cmake", str(tree)], f"cmake {tree}")
    output = subprocess.run(
        ["cmake", "--build", str(tree), "--target", "editor", "--config", "Debug"],
        cwd=REPO, capture_output=True, text=True
    )
    text = output.stdout + output.stderr
    errors = [line for line in text.splitlines() if (" error" in line) or ("Undefined symbols" in line)]
    require(output.returncode == 0, "the editor build failed:\n" + "\n".join(errors[-20:]))


def launch(editor):
    process = subprocess.Popen(
        [str(editor)], cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
    )
    pattern = re.compile(rf"MCP server: listening on 127\.0\.0\.1:(\d+) \(pid {process.pid}\b")
    deadline = time.monotonic() + 180.0
    port = None
    while (port is None) and (time.monotonic() < deadline):
        require(process.poll() is None, f"the editor exited during startup (code {process.returncode}); see logs/log.txt")
        if LOG.is_file():
            for line in LOG.read_text(encoding="utf-8", errors="replace").splitlines():
                match = pattern.search(line)
                if match:
                    port = int(match.group(1))
        if port is None:
            time.sleep(0.5)
    require(port is not None, f"no 'MCP server: listening' line for pid {process.pid} in logs/log.txt")
    return process, port


class Editor:
    def __init__(self, process, port):
        self.process = process
        self.port = port

    def call(self, tool, arguments=None):
        require(self.process.poll() is None, f"the editor exited (code {self.process.returncode}) before {tool}")
        payload = mcp_call.rpc(self.port, "tools/call", {"name": tool, "arguments": arguments or {}})
        require("error" not in payload, f"{tool}: {json.dumps(payload.get('error'))}")
        result = payload.get("result", {})
        content = result.get("content")
        text = content[0]["text"] if (isinstance(content, list) and content and ("text" in content[0])) else json.dumps(result)
        require(not result.get("isError", False), f"{tool} refused: {text[:600]}")
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            raise Step_failure(f"{tool} answered text that is not JSON: {text[:600]}")

    def wait_ready(self):
        deadline = time.monotonic() + 240.0
        while time.monotonic() < deadline:
            require(self.process.poll() is None, f"the editor exited (code {self.process.returncode}) while starting")
            try:
                self.call("list_scenes")
                return
            except (OSError, Step_failure):
                time.sleep(0.5)
        raise Step_failure("the editor did not answer MCP calls within 240 s")


def create_scene(editor):
    before = {scene["name"] for scene in editor.call("list_scenes")["scenes"]}
    editor.call("create_scene")
    deadline = time.monotonic() + 60.0
    while time.monotonic() < deadline:
        added = sorted({scene["name"] for scene in editor.call("list_scenes")["scenes"]} - before)
        if added:
            return added[0]
    raise Step_failure("create_scene made no new scene within 60 s")


def build_level(editor, scene):
    shapes = {shape["id"] for shape in editor.call("level_get_kits", {"scene_name": scene})["shapes"]}
    pieces = [
        {"shape": "basic.floor_slab", "position": [0, -0.2, 0], "name": "ground",
         "parameters": {"width": 60.0, "depth": 60.0, "thickness": 0.2}},
        {"shape": "nature.hill", "position": [14, 0, -10], "name": "hill"},
        {"shape": "architecture.wall", "position": [-4, 0, 6], "name": "wall a"},
        {"shape": "architecture.wall", "position": [1.5, 0, 6], "name": "wall b (drag me to wall a)"},
        {"shape": "architecture.wall", "position": [-4, 0, 10], "name": "wall c"},
        {"shape": "architecture.door_wall", "position": [3, 0, 10], "name": "door wall"},
        {"shape": "basic.stairs", "position": [-6, 0, 0], "name": "stairs"},
        {"shape": "basic.floor_slab", "position": [-6, 0, -6], "name": "platform slab"},
        {"shape": "architecture.pillar", "position": [-2, 0, -6], "name": "pillar"},
        {"shape": "architecture.arch", "position": [2, 0, 0], "name": "arch"},
        {"shape": "basic.block", "position": [6, 0, 4], "name": "block 1"},
        {"shape": "basic.block", "position": [8.5, 0, 4], "name": "block 2"},
        {"shape": "basic.block", "position": [11, 0, 4], "name": "block 3"},
        {"shape": "basic.block", "position": [6, 2, 0], "name": "hovering block"},
        {"shape": "nature.rock", "position": [9, 0, 0], "name": "rock"},
        # The beach corner: a cliff behind three shore tiles that slope into a water surface.
        {"shape": "nature.cliff", "position": [-24, 0, 15.5], "name": "cliff"},
        {"shape": "nature.shore", "position": [-28, 0, 22], "name": "shore a"},
        {"shape": "nature.shore", "position": [-24, 0, 22], "name": "shore b"},
        {"shape": "nature.shore", "position": [-20, 0, 22], "name": "shore c"},
        {"shape": "nature.water", "position": [-24, 0.5, 28], "name": "water", "parameters": {"width": 16.0, "depth": 12.0}},
        # The rock corner: spires standing in the water, basalt columns at the shore's end, a dune and a ground patch inland.
        {"shape": "nature.rock_spire", "position": [-28, 0, 29], "name": "sea stack", "parameters": {"form": "stack", "height": 8.0, "derive": ["radius", "lean"]}},
        {"shape": "nature.rock_spire", "position": [-21, 0, 31], "name": "pinnacle", "parameters": {"form": "pinnacle", "height": 6.0, "derive": ["radius", "lean"]}},
        {"shape": "nature.basalt_columns", "position": [-15, 0, 23], "name": "basalt columns", "parameters": {"radius": 2.5, "derive": ["height_spread"]}},
        {"shape": "nature.dune", "position": [-24, 0, 6], "name": "dune"},
        {"shape": "nature.ground_patch", "position": [-16, 0, 10], "name": "ground patch"},
        # A small walled room for the roof (joined below).
        {"shape": "architecture.door_wall", "position": [22, 0, 22], "name": "room front"},
        {"shape": "architecture.wall", "position": [22, 0, 18], "name": "room back", "rotation_xyzw": [0, 1, 0, 0]},
        {"shape": "architecture.wall", "position": [20, 0, 20], "name": "room left", "rotation_xyzw": [0, -0.7071068, 0, 0.7071068]},
        {"shape": "architecture.wall", "position": [24, 0, 20], "name": "room right", "rotation_xyzw": [0, 0.7071068, 0, 0.7071068]},
    ]
    missing = sorted({piece["shape"] for piece in pieces} - shapes)
    require(not missing, f"the level names shapes level_get_kits does not list: {missing}")
    placed = editor.call("level_place_pieces", {"scene_name": scene, "pieces": pieces})["pieces"]
    require(len(placed) == len(pieces), f"placed {len(placed)} of {len(pieces)} pieces")
    ids = {piece["name"]: piece["id"] for piece in placed}

    # A palm of each species on the shore's berm, snapped down onto the sand.
    palms = [
        {"shape": "nature.palm", "position": [-29 + 2.5 * i, 2.5, 19.7], "name": f"{species} palm",
         "parameters": {"species": species}, "surface_snap": True}
        for i, species in enumerate(["coconut", "date", "royal", "fan", "bottle"])
    ]
    placed_palms = editor.call("level_place_pieces", {"scene_name": scene, "surface_max_distance": 3.0, "pieces": palms})["pieces"]
    require(len(placed_palms) == len(palms), f"placed {len(placed_palms)} of {len(palms)} palms")
    require(all(palm["surface_found"] for palm in placed_palms), "a palm found no sand under it")
    ids.update({palm["name"]: palm["id"] for palm in placed_palms})

    # Leaf plants of each form between the palms, snapped onto the berm.
    plants = [
        {"shape": "nature.leaf_plant", "position": [-27.75 + 2.5 * i, 2.5, 19.3], "name": f"{form} leaf plant",
         "parameters": {"form": form}, "surface_snap": True}
        for i, form in enumerate(["banana", "taro", "strelitzia", "fern", "tree_fern"])
    ]
    placed_plants = editor.call("level_place_pieces", {"scene_name": scene, "surface_max_distance": 3.0, "pieces": plants})["pieces"]
    require(len(placed_plants) == len(plants), f"placed {len(placed_plants)} of {len(plants)} leaf plants")
    require(all(plant["surface_found"] for plant in placed_plants), "a leaf plant found no surface under it")
    ids.update({plant["name"]: plant["id"] for plant in placed_plants})

    # Palm clumps and palm shrubs of each species on the berm behind them.
    understory = [
        {"shape": "nature.palm_clump", "position": [-28.5 + 5.5 * i, 2.5, 20.3 + 1.5 * i], "name": f"{species} palm clump",
         "parameters": {"species": species}, "surface_snap": True}
        for i, species in enumerate(["areca", "nipa"])
    ] + [
        {"shape": "nature.palm_shrub", "position": [-26.5 + 2.5 * i, 2.5, 20.3], "name": f"{species} palm shrub",
         "parameters": {"species": species}, "surface_snap": True}
        for i, species in enumerate(["palmetto", "cycad", "seedling"])
    ]
    placed_understory = editor.call("level_place_pieces", {"scene_name": scene, "surface_max_distance": 3.0, "pieces": understory})["pieces"]
    require(len(placed_understory) == len(understory), f"placed {len(placed_understory)} of {len(understory)} palm clumps and shrubs")
    require(all(piece["surface_found"] for piece in placed_understory),
            "a palm clump or shrub found no sand under it: " + ", ".join(f"{p['shape']} {p['surface_found']} {p['position']}" for p in placed_understory))
    ids.update({piece["name"]: piece["id"] for piece in placed_understory})

    # The cultural corners, joined by their connectors (doc/editor/level_kit_shapes.md "Column" .. "Tower").
    def join(piece, to_name, connector, to_connector):
        joined = editor.call("level_place_pieces", {"scene_name": scene, "pieces": [dict(piece, connect={
            "connector": connector, "to_piece_id": ids[to_name], "to_connector": to_connector})]})["pieces"]
        require(len(joined) == 1, f"'{piece['name']}' was not joined to '{to_name}'")
        ids[piece["name"]] = joined[0]["id"]

    corners = [
        {"shape": "basic.floor_slab", "position": [-20, 0, -22], "name": "stylobate", "parameters": {"width": 10.0, "depth": 4.0, "thickness": 0.3}},
        {"shape": "basic.floor_slab", "position": [0, 0, -21], "name": "torii path", "parameters": {"width": 2.6, "depth": 14.0, "thickness": 0.05}},
        {"shape": "architecture.tower", "position": [16, 0, -24], "name": "round tower"},
    ] + [
        {"shape": "architecture.column", "position": [-23 + 2 * i, 0.3, -22], "name": f"column {i + 1}"} for i in range(4)
    ] + [
        {"shape": "architecture.torii", "position": [0, 0.05, -16 - 3 * i], "name": f"torii {i + 1}", "materials": {"wood": "Kit Vermilion"}} for i in range(4)
    ]
    placed_corners = editor.call("level_place_pieces", {"scene_name": scene, "pieces": corners})["pieces"]
    require(len(placed_corners) == len(corners), f"placed {len(placed_corners)} of {len(corners)} cultural pieces")
    ids.update({piece["name"]: piece["id"] for piece in placed_corners})
    for i in range(4):
        join({"shape": "architecture.entablature", "name": f"entablature {i + 1}"}, f"column {i + 1}", "base", "top")
    join({"shape": "architecture.wall", "name": "castle wall a"}, "round tower", "start", "right")
    join({"shape": "architecture.wall", "name": "castle wall b"}, "castle wall a", "start", "end")
    join({"shape": "architecture.battlement", "name": "battlement a", "parameters": {"length": 4.0}}, "castle wall a", "base", "top")
    join({"shape": "architecture.battlement", "name": "battlement b", "parameters": {"length": 4.0}}, "castle wall b", "base", "top")
    join({"shape": "architecture.tower", "name": "square tower", "parameters": {"profile": "square"}}, "castle wall b", "left", "end")
    join({"shape": "architecture.roof", "name": "roof"}, "room front", "front", "top")

    # The layouts come after the ground and the hill, which their surface snap puts them on.
    layouts = [
        ("tree ring on the hill", [14, 0, -10], {"kind": "ring", "count": 8, "radius": 2.5},
         [{"shape": "nature.tree"}], {"surface_snap": True, "per_piece_seed": True}, 8),
        ("rock scatter", [14, 0, 12],
         {"kind": "scatter", "scatter_mode": "count", "count": 30, "region": "rectangle",
          "size_x": 14.0, "size_z": 8.0, "min_spacing": 0.4},
         [{"shape": "nature.rock"}, {"shape": "nature.bush"}], {"surface_snap": True, "per_piece_seed": True}, 30),
        ("pillar line", [-12, 0, -14], {"kind": "line", "count": 6}, [{"shape": "architecture.pillar"}], {}, 6),
    ]
    for name, position, pattern, items, options, count in layouts:
        arguments = {"scene_name": scene, "name": name, "position": position, "pattern": pattern, "items": items, "locked": True}
        arguments.update(options)
        children = editor.call("level_create_layout", arguments).get("children", [])
        require(len(children) == count, f"layout '{name}' has {len(children)} pieces, expected {count}")
    ring = next(layout for layout in editor.call("level_get_layouts", {"scene_name": scene})["layouts"] if layout["name"] == "tree ring on the hill")
    hill_trees = [child["position"][1] for child in ring["children"]]
    require(max(hill_trees) > 0.1, "no tree of the ring rests on the hill (the ring was generated without a hill under it)")
    return ids


def show_level(editor, scene, ids):
    for title in FLOATING_WINDOWS:
        editor.call("set_window_visibility", {"title": title, "visible": False})
    editor.call("set_window_visibility", {"title": "Level Kit", "visible": True})
    viewports = editor.call("get_viewports")["viewports"]
    ours = [viewport for viewport in viewports if viewport.get("scene") == scene]
    require(ours, f"no viewport shows '{scene}': {[viewport.get('scene') for viewport in viewports]}")
    title = ours[0]["title"]
    editor.call("set_window_visibility", {"title": title, "visible": True, "focus": True})
    camera = next((node for node in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"] if node["name"] == "Camera"), None)
    require(camera is not None, f"'{scene}' has no node named Camera")
    editor.call("set_node_transform", {
        "scene_name": scene, "node_id": camera["id"], "translation": [4, 22, 34], "rotation_xyzw": [-0.3007, 0, 0, 0.9537]
    })
    editor.call("set_surface_snap_settings", {"snap": False, "orient": False, "connector_snap_distance": 0.5})
    editor.call("select_items", {"scene_name": scene, "ids": [ids["wall a"], ids["wall b (drag me to wall a)"]]})
    # The indicators of the selection are drawn from the next frames on: read them until they show.
    deadline = time.monotonic() + 60.0
    count = 0
    while (count == 0) and (time.monotonic() < deadline):
        drawn = editor.call("level_get_connector_indicators", {"viewport": title})
        count = len(drawn.get("connectors", drawn.get("indicators", [])))
    require(count > 0, f"the viewport '{title}' draws no connector of the two selected walls: {json.dumps(drawn)[:400]}")
    return title, count


def main():
    parser = argparse.ArgumentParser(description="Launch an editor and build the level tools test level in it")
    parser.add_argument("--editor", required=True, help="editor binary, e.g. build_xcode_metal/bin/Debug/editor")
    parser.add_argument("--build", default=None, help="build tree to configure and build before the launch")
    parser.add_argument("--screenshot", default=None, help="write a screenshot of the prepared editor to this path")
    parser.add_argument("--keep-config", action="store_true", help="leave config/ as it is instead of resetting it to HEAD")
    args = parser.parse_args()

    process = None
    try:
        editors = running_editors()
        require(not editors, "an editor is already running; ask the user to close it first:\n" + "\n".join(editors))
        if not args.keep_config:
            run(["git", "checkout", "--", "config/"], "git checkout -- config/")
        if args.build is not None:
            build(REPO / args.build)
        editor_path = (REPO / args.editor) if not os.path.isabs(args.editor) else pathlib.Path(args.editor)
        require(editor_path.is_file(), f"no editor binary at {editor_path}")
        process, port = launch(editor_path)
        editor = Editor(process, port)
        editor.wait_ready()
        scene = create_scene(editor)
        editor.call("set_active_scene", {"scene_name": scene})
        ids = build_level(editor, scene)
        title, connector_count = show_level(editor, scene, ids)
        if args.screenshot is not None:
            editor.call("capture_screenshot", {"path": args.screenshot})
        print(f"ready: pid {process.pid}, MCP port {port}, scene '{scene}' in viewport '{title}', "
              f"{len(ids)} pieces, 3 layouts, {connector_count} connectors drawn")
        return 0
    except Step_failure as failure:
        print(f"FAILED: {failure}", file=sys.stderr)
        if process is not None:
            print(f"the editor (pid {process.pid}) was left running", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
