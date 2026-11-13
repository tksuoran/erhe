#!/usr/bin/env python3
"""Verify Build Navigation Mesh (doc/editor/navigation.md "Job", "Staleness", "MCP").

Writes a hand-made glTF scene into a scratch directory, opens it in an editor
this script launches (ERHE_AI_DRIVER=1, stopped by its own pid), adds export
settings, marks a seed on the floor and checks over MCP:

- without export settings, and without a seed, navigation_build is refused;
- the reachable area is within 5 % of the hand-computed one (see EXPECTED_REACHABLE_AREA);
- the mirrored and scaled ramp is walkable and reachable, the platform's polygons
  are present and not reachable, the seed result names a polygon,
  get_navigation_mesh lists as many polygons as the status counts;
- three nodes sharing one mesh read one primitive shape;
- the job leaves the undo stack and the node tree unchanged;
- a second build (after navigation_clear) gives the same polygons;
- every staleness reason (geometry: a move, a delete, a hide of a mesh and of its
  parent, the show of a mesh hidden at build time; settings; seeds: the
  flag and a move) is raised by its edit and ended by its undo (a settings edit
  by setting the value back);
- a seed search extent edit is not "settings changed", and the build that follows
  reruns the flood without a rebuild (build_count unchanged, flood_count + 1);
- a seed 50 m above the scene fails the build with the seed's reason and keeps the installed mesh;
- the visualization (doc/editor/navigation.md "Visualization"): the "Navigation Mesh" node is a
  render proxy, not content, with its reachable and unreachable meshes; render_scene_image from above
  with navigation_mesh all differs from off over most of the floor, reachable differs from all over
  the platform only; the id pass (get_visible_entries), a
  ray and a save (the glTF file) leave it out; a build with it present reads the same input (the
  flood alone runs); navigation_clear removes it; navigation_set_visualization sets the viewports;
  closing the scene leaves nothing alive;
- the walkable area reads: get_navigation_summary lists two islands (the floor with the ramp, reachable; the
  platform's top, not) largest first with bounds and points, matching get_navigation_status's counts;
  navigation_find_point by position and by node, over the platform and out of reach (and in wider search_extents);
  navigation_find_path around the wall and up the ramp (found, complete, corners and length agree), to the platform
  (different islands) and off the mesh (no polygon near end); a stale mesh answers with stale true;
- get_save_status reports the stage of a running build (informational);
- saving (doc/editor/navigation.md "Saving"), on the scene saved as an erhe scene in the work directory: a
  build stores the item (made, key, bytes) and the record, an unchanged build finds it (same key, not made);
  a save, close and load_scene installs it with the same totals, seeds, seed results and polygons, current,
  with its visualization and no build; a flood alone stores a new item in place of the old one (one item in
  the editor asset); an edit saved without a build (a mesh moved, a build setting, the seed unmarked) makes
  the reopened mesh stale with its reason; Clear removes the item and the record and a reopen installs
  nothing; drops of unreferenced and of all items keep the recorded item; no navigation material is listed.

Usage: python3 scripts/navigation_verify.py [--editor <binary>] [--keep]
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
from prepared_asset_verify import Editor, Gltf_builder, check  # noqa: E402

# Hand-computed reachable area (defaults: cell 0.15 m, agent_radius 0.3 m = 2 cells, climb 0.4 m, height 1.8 m).
# A walkable border with no walkable neighbour is a ledge to Recast (its ledge filter takes a missing
# neighbour as a drop), so it loses one cell before the erosion: e = r + 0.15 = 0.45 there; an
# obstacle's walls stand on the floor and erode it by r = 0.3 alone.
# Floor 20 x 20 m at y 0, its border a ledge: (20 - 2 x 0.45)^2                                = 364.81
# Every obstacle removes its footprint grown by r (A + P r + pi r^2):
#   wall 10 x 0.4 x 3 m (its 0.4 m inside erodes away, its top too):  4 + 20.8 r + pi r^2 = 10.52
#   platform 4 x 4 x 1 m (floor inside has 1 m < 1.8 m clearance):   16 + 16 r + pi r^2 = 21.08
#   three pillars 1 x 1 x 2.5 m (their 0.4 m insides are below region_min_size): 3 x 2.48 = 7.45
#   ramp 6 x 3 m rising 1 m (y = 0.4 at 3.6 m from its high end): its sides above the climb
#   are ledges, eroded e on the ramp and r on the floor beside it, and its high end
#   likewise: 2 x 3.6 x (0.45 + 0.3) + 3 x 0.45 + 3.6 x 0.3 + corners ~ 8.0; the sloped
#   surface is 1.4 % larger than its footprint: + 0.2
# 364.81 - 10.52 - 21.08 - 7.45 - 8.0 + 0.2 = 317.96
EXPECTED_REACHABLE_AREA = 317.96

FLOOR    = (-10.0, -10.0, 10.0, 10.0)
WALL     = ((-5.0, 0.0, 6.0), (5.0, 3.0, 6.4))
PLATFORM = ((3.0, 0.0, -7.0), (7.0, 1.0, -3.0))
PILLARS  = ((-6.0, -6.0), (-3.0, -6.0), (0.0, -6.0))
RAMP_TRANSLATION = (2.0, 0.0, 1.0)
RAMP_SCALE       = (-6.0, 2.0, 3.0)  # mirrored: world x from 2 down to -4, height 0 to 1, z from 1 to 4
SEED  = (-8.0, 0.05, -1.0)


def box(lo, hi):
    """Closed box, outward normals, glTF counter-clockwise front faces."""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    faces = [
        ((0, 1, 0),  [(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)]),
        ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
        ((0, 0, -1), [(x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)]),
        ((0, 0, 1),  [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
        ((-1, 0, 0), [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]),
        ((1, 0, 0),  [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)])
    ]
    positions, normals, texcoords, indices = [], [], [], []
    for normal, corners in faces:
        base = len(positions)
        positions += corners
        normals += [normal] * 4
        texcoords += [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, texcoords, indices


def quad_up(corners):
    """One quad whose (a, b, c) normal points up."""
    return corners, [(0.0, 1.0, 0.0)] * 4, [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)], [0, 1, 2, 0, 2, 3]


def write_scene(directory):
    builder = Gltf_builder()
    x0, z0, x1, z1 = FLOOR
    floor = quad_up([(x0, 0.0, z0), (x0, 0.0, z1), (x1, 0.0, z1), (x1, 0.0, z0)])
    # Unit ramp in its node's space: x 0..1, rising 0.5, z 0..1; the node mirrors and scales it.
    ramp_corners = [(0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.5, 1.0), (1.0, 0.5, 0.0)]
    ramp_normal  = (-0.5 / (1.25 ** 0.5), 1.0 / (1.25 ** 0.5), 0.0)
    ramp = (ramp_corners, [ramp_normal] * 4, [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)], [0, 1, 2, 0, 2, 3])
    meshes = [
        {"name": "floor",    "primitives": [builder.primitive(floor, 0)]},
        {"name": "wall",     "primitives": [builder.primitive(box(*WALL), 0)]},
        {"name": "platform", "primitives": [builder.primitive(box(*PLATFORM), 0)]},
        {"name": "pillar",   "primitives": [builder.primitive(box((-0.5, 0.0, -0.5), (0.5, 2.5, 0.5)), 0)]},
        {"name": "ramp",     "primitives": [builder.primitive(ramp, 0)]}
    ]
    nodes = [
        {"name": "floor",    "mesh": 0},
        {"name": "wall",     "mesh": 1},
        {"name": "deck",     "children": [8]},
        {"name": "pillar_a", "mesh": 3, "translation": [PILLARS[0][0], 0.0, PILLARS[0][1]]},
        {"name": "pillar_b", "mesh": 3, "translation": [PILLARS[1][0], 0.0, PILLARS[1][1]]},
        {"name": "pillar_c", "mesh": 3, "translation": [PILLARS[2][0], 0.0, PILLARS[2][1]]},
        {"name": "ramp",     "mesh": 4, "translation": list(RAMP_TRANSLATION), "scale": list(RAMP_SCALE)},
        {"name": "seed",     "translation": list(SEED)},
        {"name": "platform", "mesh": 2}
    ]
    document = {
        "asset": {"version": "2.0", "generator": "navigation_verify.py"},
        "scene": 0, "scenes": [{"nodes": list(range(len(nodes) - 1))}],  # the platform is a child of "deck"
        "nodes": nodes, "meshes": meshes,
        "materials": [{"name": "grey", "pbrMetallicRoughness": {"baseColorFactor": [0.6, 0.6, 0.6, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.8}}],
        "accessors": builder.accessors, "bufferViews": builder.views
    }
    binary = builder.padded_binary()
    document["buffers"] = [{"uri": "navigation_scene.bin", "byteLength": len(binary)}]
    with open(os.path.join(directory, "navigation_scene.bin"), "wb") as handle:
        handle.write(binary)
    path = os.path.join(directory, "navigation_scene.gltf")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle)
    return path


class Navigation:
    def __init__(self, editor, scene):
        self.editor = editor
        self.scene  = scene
        self.stages = set()

    def status(self):
        return self.editor.call("get_navigation_status", {"scene_name": self.scene})

    def mesh(self, reachable_only=False):
        return self.editor.call("get_navigation_mesh", {"scene_name": self.scene, "reachable_only": reachable_only, "max_polygons": 100000})

    def build(self, timeout=600.0):
        """navigation_build polled to its end through get_save_status; the job's last state."""
        job_id = self.editor.call("navigation_build", {"scene_name": self.scene})["job_id"]
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            job = self.editor.call("get_save_status", {"job_id": job_id})
            if "progress" in job:
                self.stages.add(job["progress"].get("stage", ""))
            if job.get("state") in ("done", "failed"):
                self.editor.wait_idle()
                return job
            time.sleep(0.02)
        raise RuntimeError(f"navigation_build job {job_id} did not finish")

    def refusal(self):
        return self.editor.refusal("navigation_build", {"scene_name": self.scene})

    def set_navigation(self, fields):
        self.editor.set_export_settings(self.scene, {"navigation": fields})


def polygon_bounds(polygon):
    xs = [v[0] for v in polygon["vertices"]]
    ys = [v[1] for v in polygon["vertices"]]
    zs = [v[2] for v in polygon["vertices"]]
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def scene_nodes(editor, scene):
    return editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]


def derived_names(nodes):
    """Names of the render proxies and the prims below them (the navigation visualization)."""
    names = {node.get("name") for node in nodes if node.get("render_proxy")}
    grown = True
    while grown:
        below = {node.get("name") for node in nodes if node.get("parent") in names} - names
        grown = bool(below)
        names |= below
    return names


def node_tree(editor, scene):
    """The authored node tree: the derived render proxies left out."""
    nodes = scene_nodes(editor, scene)
    derived = derived_names(nodes)
    return sorted((node.get("name"), node.get("parent"), node.get("world_from_node_hex"))
                  for node in nodes if node.get("name") not in derived)


# The visualization images: straight down from 30 m over the scene centre, up = -z (image top is -z).
VIEW_HEIGHT = 30.0
VIEW_FOV_Y  = 50.0
VIEW_SIZE   = 320


def view_pixel(x, y, z):
    """The image pixel (column, row) of a world point in the visualization view."""
    half = math.tan(math.radians(VIEW_FOV_Y) / 2.0) * (VIEW_HEIGHT - y)
    return ((x / half) + 1.0) * 0.5 * VIEW_SIZE, ((z / half) + 1.0) * 0.5 * VIEW_SIZE


def view_rect(lo, hi, y, margin):
    (c0, r0), (c1, r1) = view_pixel(lo[0], y, lo[1]), view_pixel(hi[0], y, hi[1])
    return (min(c0, c1) - margin, min(r0, r1) - margin, max(c0, c1) + margin, max(r0, r1) + margin)


def render_view(editor, scene, path, mode):
    camera = {"eye": [0.0, VIEW_HEIGHT, 0.0], "target": [0.0, 0.0, 0.0], "up": [0.0, 0.0, -1.0],
              "fov_y_degrees": VIEW_FOV_Y, "near": 0.1, "far": 100.0}
    return editor.call("render_scene_image", {"scene": scene, "camera": camera, "width": VIEW_SIZE, "height": VIEW_SIZE,
                                              "output": "png", "path": path, "navigation_mesh": mode})


def differing_pixels(path_a, path_b):
    """(column, row) of every pixel that differs by more than 8/255 in a channel (bloom spreads a few levels)."""
    wa, _, a = pav.decode_png(path_a)
    _, _, b = pav.decode_png(path_b)
    result = []
    for i in range(0, len(a), 4):
        if max(abs(a[i + c] - b[i + c]) for c in range(3)) > 8:
            pixel = i // 4
            result.append((pixel % wa, pixel // wa))
    return result


def inside(pixel, rect):
    return (rect[0] <= pixel[0] <= rect[2]) and (rect[1] <= pixel[1] <= rect[3])


def check_visualization(editor, navigation, scene, work, status):
    """Step 5 of doc/plans/navigation_mesh.md: the "Navigation Mesh" node, its images and what leaves it out."""
    nodes = scene_nodes(editor, scene)
    by_name = {node.get("name"): node for node in nodes}
    group = by_name.get("Navigation Mesh")
    meshes = [name for name in ("Navigation Mesh Reachable", "Navigation Mesh Unreachable")
              if (name in by_name) and (by_name[name].get("parent") == "Navigation Mesh")]
    check("the build installs the 'Navigation Mesh' node: a render proxy, not content, with its reachable and unreachable meshes",
          (group is not None) and group.get("render_proxy") and not group.get("content") and (len(meshes) == 2)
          and all(by_name[name].get("render_proxy") and not by_name[name].get("content") for name in meshes),
          json.dumps({name: by_name.get(name) for name in ["Navigation Mesh"] + meshes})[:600])

    images = {}
    for mode in ("off", "all", "reachable"):
        images[mode] = os.path.join(work, f"navigation_{mode}.png")
        render_view(editor, scene, images[mode], mode)
    floor_rect    = view_rect((FLOOR[0], FLOOR[1]), (FLOOR[2], FLOOR[3]), 0.0, 0.0)
    (px0, _, pz0), (px1, py1, pz1) = PLATFORM
    platform_rect = view_rect((px0, pz0), (px1, pz1), py1, 3.0)
    floor_pixels  = (floor_rect[2] - floor_rect[0]) * (floor_rect[3] - floor_rect[1])
    all_vs_off    = [p for p in differing_pixels(images["all"], images["off"]) if inside(p, floor_rect)]
    check("render_scene_image navigation_mesh all differs from off over most of the floor",
          len(all_vs_off) >= 0.5 * floor_pixels,
          f"{len(all_vs_off)} of {floor_pixels:.0f} floor pixels differ ({images['all']})")
    reachable_vs_all = differing_pixels(images["reachable"], images["all"])
    outside = [p for p in reachable_vs_all if not inside(p, platform_rect)]
    platform_pixels = (platform_rect[2] - platform_rect[0] - 6.0) * (platform_rect[3] - platform_rect[1] - 6.0)
    check("reachable differs from all over the platform only (its polygons are the unreachable ones)",
          (len(outside) == 0) and (len(reachable_vs_all) >= 0.3 * platform_pixels),
          f"{len(reachable_vs_all)} pixels differ, {len(outside)} outside the platform {[round(v) for v in platform_rect]}, first outside {outside[:5]}")
    print(f"INFO: visualization images {images['off']}, {images['all']}, {images['reachable']}", flush=True)

    entries = editor.call("get_visible_entries", {"scene_name": scene, "position": [0.0, VIEW_HEIGHT, 0.0], "target": [0.0, 0.0, 0.0],
                                                  "up": [0.0, 0.0, -1.0], "width": VIEW_SIZE, "height": VIEW_SIZE})
    entry_names = sorted({entry.get("node_name", entry.get("mesh_name", "")) for entry in entries.get("entries", [])})
    check("the id pass sees no navigation mesh (get_visible_entries)",
          (len(entry_names) > 0) and not any(name.startswith("Navigation") for name in entry_names), f"entries {entry_names}")

    hit = editor.call("raycast", {"scene_name": scene, "origin": [-8.0, 20.0, 1.0], "direction": [0.0, -1.0, 0.0]})
    check("a ray from above hits the floor, not the navigation mesh", hit.get("hit") and (hit.get("node_name") == "floor"), json.dumps(hit)[:300])

    saved = os.path.join(work, "navigation_saved.gltf")
    editor.save(scene, saved)
    document = pav.gltf_document(saved)
    saved_names = [node.get("name", "") for node in document.get("nodes", [])] + \
                  [mesh.get("name", "") for mesh in document.get("meshes", [])] + \
                  [material.get("name", "") for material in document.get("materials", [])]
    check("the saved glTF holds no navigation visualization node, mesh or material",
          ("floor" in saved_names) and not any(name.startswith("Navigation") for name in saved_names),
          f"{[name for name in saved_names if name.startswith('Navigation')]}")

    job = navigation.build()
    again = navigation.status()
    check("a build with the visualization present reads the same seven input meshes, so the flood alone runs",
          (job.get("state") == "done") and (again.get("input_mesh_count") == status.get("input_mesh_count") == 7)
          and (again.get("build_count") == status.get("build_count")) and (again.get("flood_count") == status.get("flood_count") + 1),
          f"meshes {again.get('input_mesh_count')}, build_count {status.get('build_count')} -> {again.get('build_count')}, flood_count {status.get('flood_count')} -> {again.get('flood_count')}")
    check("the flood installs the visualization again", "Navigation Mesh" in {node.get("name") for node in scene_nodes(editor, scene)})

    viewports = editor.call("navigation_set_visualization", {"mode": "all"}).get("viewports", [])
    check("navigation_set_visualization sets the viewports' Navigation Mesh mode",
          (len(viewports) > 0) and all(v.get("mode") == "all" for v in viewports), json.dumps(viewports)[:300])
    editor.call("navigation_set_visualization", {"mode": "reachable"})


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def check_reads(editor, navigation, scene, status):
    """The walkable area reads (doc/editor/navigation.md "Islands", "MCP"): summary, nearest point, path."""
    tool_names = editor.client.tool_names()
    check("the read tools are listed", {"get_navigation_summary", "navigation_find_point", "navigation_find_path"} <= tool_names)
    summary = editor.call("get_navigation_summary", {"scene_name": scene})
    islands = summary.get("islands", [])
    print(f"INFO: islands {json.dumps(islands)}", flush=True)
    check("the summary: two islands (the floor with the ramp, the platform's top), one reachable, as the status counts",
          summary.get("available") and not summary.get("stale") and (summary.get("island_count") == 2) and (summary.get("reachable_island_count") == 1)
          and (status.get("island_count") == 2) and (status.get("reachable_island_count") == 1) and (len(islands) == 2) and not summary.get("truncated"),
          f"summary {summary.get('island_count')} / {summary.get('reachable_island_count')}, status {status.get('island_count')} / {status.get('reachable_island_count')}")
    check("the summary lists the islands largest first, their areas adding up to the total, no failed seed",
          all(islands[i]["area"] >= islands[i + 1]["area"] for i in range(len(islands) - 1))
          and (abs(sum(i["area"] for i in islands) - summary.get("total_area", 0.0)) < 0.01)
          and (abs(summary.get("reachable_area", 0.0) - status.get("reachable_area", -1.0)) < 1.0e-6) and (summary.get("failed_seeds") == []),
          f"areas {[round(i['area'], 2) for i in islands]}, total {summary.get('total_area')}, failed seeds {summary.get('failed_seeds')}")
    (px0, _, pz0), (px1, py1, pz1) = PLATFORM
    platform = next((i for i in islands if not i["reachable"]), None)
    check("the platform island: unreachable, its bounds over the platform's top, its point on it",
          (platform is not None) and (px0 <= platform["bounds_min"][0]) and (platform["bounds_max"][0] <= px1)
          and (pz0 <= platform["bounds_min"][2]) and (platform["bounds_max"][2] <= pz1)
          and (abs(platform["bounds_min"][1] - py1) < 0.3) and (abs(platform["bounds_max"][1] - py1) < 0.3)
          and (px0 < platform["point"][0] < px1) and (pz0 < platform["point"][2] < pz1),
          json.dumps(platform))
    floor_island = next((i for i in islands if i["reachable"]), None)
    check("the reachable island spans the floor and the ramp (height range 0 to about 1 m)",
          (floor_island is not None) and (floor_island["bounds_min"][1] < 0.3) and (0.7 < floor_island["bounds_max"][1] < 1.3),
          json.dumps(floor_island))

    point = editor.call("navigation_find_point", {"scene_name": scene, "position": [-8.0, 1.0, 2.0]})
    check("navigation_find_point snaps a point 1 m above the floor onto it: reachable, on the floor island, distance as reported",
          point.get("found") and point.get("reachable") and (abs(point["point"][1]) < 0.3) and (abs(point["point"][0] + 8.0) < 1.0e-3)
          and (abs(point["distance"] - distance(point["point"], [-8.0, 1.0, 2.0])) < 1.0e-4)
          and (floor_island is not None) and (point.get("island") == floor_island["id"]) and (point.get("search_extents") == [1.0, 3.0, 1.0]),
          json.dumps(point))
    by_node = editor.call("navigation_find_point", {"scene_name": scene, "node": "seed"})
    check("navigation_find_point of the seed node: its origin, found and reachable",
          by_node.get("found") and by_node.get("reachable") and (by_node.get("node") == "seed") and (distance(by_node["position"], SEED) < 1.0e-4),
          json.dumps(by_node))
    on_platform = editor.call("navigation_find_point", {"scene_name": scene, "position": [5.0, 1.5, -5.0]})
    check("navigation_find_point over the platform: found, not reachable, on the platform island",
          on_platform.get("found") and not on_platform.get("reachable") and (platform is not None) and (on_platform.get("island") == platform["id"]),
          json.dumps(on_platform))
    nothing = editor.call("navigation_find_point", {"scene_name": scene, "position": [-8.0, 20.0, 2.0]})
    wide = editor.call("navigation_find_point", {"scene_name": scene, "position": [-8.0, 20.0, 2.0], "search_extents": [1.0, 25.0, 1.0]})
    check("navigation_find_point 20 m up finds nothing in the default extents and the floor with search_extents 25 m high",
          (not nothing.get("found")) and wide.get("found") and (wide.get("search_extents") == [1.0, 25.0, 1.0]),
          f"{json.dumps(nothing)} / {json.dumps(wide)}")
    error = editor.refusal("navigation_find_point", {"scene_name": scene, "position": [0.0, 0.0, 0.0], "node": "seed"})
    check("navigation_find_point with both position and node is an error", error is not None, error or "accepted")

    start, end = [-8.0, 0.0, -1.0], [8.0, 0.0, 8.0]
    path = editor.call("navigation_find_path", {"scene_name": scene, "start": start, "end": end})
    corners = path.get("corners", [])
    check("navigation_find_path around the wall: found, complete, from start to end, longer than the straight line",
          path.get("found") and path.get("complete") and (len(corners) >= 3) and (distance(corners[0], path["start"]["point"]) < 1.0e-4)
          and (distance(corners[-1], path["end"]["point"]) < 1.0e-4) and (path["length"] > distance(start, end))
          and (abs(path["length"] - sum(distance(corners[i], corners[i + 1]) for i in range(len(corners) - 1))) < 1.0e-3),
          f"found {path.get('found')}, complete {path.get('complete')}, length {path.get('length')}, {len(corners)} corners")
    ramp_path = editor.call("navigation_find_path", {"scene_name": scene, "start": start, "end": [-3.0, 0.9, 2.5]})
    check("navigation_find_path up the ramp: found and complete",
          ramp_path.get("found") and ramp_path.get("complete") and (ramp_path["end"]["point"][1] > 0.5), json.dumps(ramp_path)[:400])
    blocked = editor.call("navigation_find_path", {"scene_name": scene, "start": start, "end": [5.0, 1.0, -5.0]})
    check("navigation_find_path to the platform: not found, reason 'different islands', both ends snapped",
          (not blocked.get("found")) and (blocked.get("reason") == "different islands") and blocked["start"]["found"] and blocked["end"]["found"]
          and (blocked.get("corners") == []),
          json.dumps(blocked)[:400])
    off = editor.call("navigation_find_path", {"scene_name": scene, "start": start, "end": [50.0, 0.0, 0.0]})
    check("navigation_find_path to a point off the mesh: reason 'no polygon near end'",
          (not off.get("found")) and (off.get("reason") == "no polygon near end"), json.dumps(off)[:300])

    # A stale mesh still answers, saying so.
    editor.call("set_node_transform", {"scene_name": scene, "node_name": "pillar_a", "space": "local", "translation": [-6.0, 0.0, -7.0]})
    editor.wait_idle()
    stale_point = editor.call("navigation_find_point", {"scene_name": scene, "position": [-8.0, 1.0, 2.0]})
    stale_path  = editor.call("navigation_find_path", {"scene_name": scene, "start": start, "end": end})
    stale_summary = editor.call("get_navigation_summary", {"scene_name": scene})
    editor.call("undo")
    editor.wait_idle()
    check("a stale mesh still answers the reads with stale true",
          stale_point.get("stale") and stale_point.get("found") and stale_path.get("stale") and stale_path.get("found") and stale_summary.get("stale"),
          f"point {stale_point.get('stale')}, path {stale_path.get('stale')}, summary {stale_summary.get('stale')}")
    check("the undo of the move makes the reads current again", not editor.call("get_navigation_summary", {"scene_name": scene}).get("stale"))


def undo_stack(editor):
    return json.dumps(editor.call("get_undo_redo_stack"), sort_keys=True)


def stale_reasons(navigation):
    return navigation.status().get("stale_reasons", [])


def check_staleness(navigation, label, reason, edit, undo):
    before = stale_reasons(navigation)
    edit()
    navigation.editor.wait_idle()
    raised = stale_reasons(navigation)
    undo()
    navigation.editor.wait_idle()
    ended = stale_reasons(navigation)
    check(f"staleness: {label} raises '{reason}' and its undo ends it",
          (before == []) and (raised == [reason]) and (ended == []),
          f"before {before}, after the edit {raised}, after the undo {ended}")


def run(editor, work):
    scene = editor.open_foreign(write_scene(work))
    navigation = Navigation(editor, scene)
    tool_names = editor.client.tool_names()
    check("the navigation tools are listed", {"navigation_build", "navigation_clear", "get_navigation_mesh"} <= tool_names)

    refusal = navigation.refusal()
    check("without export settings the build is refused", (refusal is not None) and ("no export settings" in refusal), refusal or "accepted")
    editor.set_export_settings(scene, {})
    refusal = navigation.refusal()
    expected = f"Build Navigation Mesh of '{scene}' refused: the scene has no navigation seed - mark a node (Properties > Navigation Seed)"
    check("without a seed the build is refused with the stated message", (refusal is not None) and (expected in refusal), refusal or "accepted")
    editor.call("navigation_set_seed", {"scene_name": scene, "node": "seed"})

    tree_before  = node_tree(editor, scene)
    stack_before = undo_stack(editor)
    job = navigation.build()
    check("the build is done", job.get("state") == "done", json.dumps(job)[:400])
    check("the build changes neither the node tree nor the undo stack",
          (node_tree(editor, scene) == tree_before) and (undo_stack(editor) == stack_before))
    status = navigation.status()
    check("the status is available and current", status.get("available") and not status.get("stale") and not status.get("pending"), json.dumps(status)[:300])
    check("seven input meshes, the three pillars share one shape (five shapes), none skipped",
          (status.get("input_mesh_count") == 7) and (status.get("input_shape_count") == 5) and (status.get("skipped_mesh_count") == 0),
          f"meshes {status.get('input_mesh_count')}, shapes {status.get('input_shape_count')}, skipped {status.get('skipped_mesh_count')}")
    reachable_area = status.get("reachable_area", 0.0)
    check("the reachable area is within 5 % of the hand-computed one",
          abs(reachable_area - EXPECTED_REACHABLE_AREA) <= (0.05 * EXPECTED_REACHABLE_AREA),
          f"{reachable_area:.2f} m2, hand-computed {EXPECTED_REACHABLE_AREA:.2f} m2 ({100.0 * (reachable_area / EXPECTED_REACHABLE_AREA - 1.0):+.1f} %)")
    seeds = status.get("built_seeds", [])
    result = seeds[0].get("result", {}) if seeds else {}
    check("the seed result names a polygon", (len(seeds) == 1) and result.get("found") and isinstance(result.get("polygon"), int), json.dumps(seeds))

    mesh = navigation.mesh()
    polygons = mesh.get("polygons", [])
    check("get_navigation_mesh lists as many polygons as the status counts",
          (mesh.get("polygon_count") == status.get("polygon_count")) and (len(polygons) == status.get("polygon_count")) and not mesh.get("truncated"),
          f"mesh {mesh.get('polygon_count')} / {len(polygons)}, status {status.get('polygon_count')}")
    (px0, _, pz0), (px1, py1, pz1) = PLATFORM
    platform = [p for p in polygons
                if all(abs(v[1] - py1) < 0.25 for v in p["vertices"])
                and polygon_bounds(p)[0][0] >= px0 - 0.1 and polygon_bounds(p)[1][0] <= px1 + 0.1
                and polygon_bounds(p)[0][2] >= pz0 - 0.1 and polygon_bounds(p)[1][2] <= pz1 + 0.1]
    check("the platform's polygons are present and not reachable",
          (len(platform) > 0) and not any(p["reachable"] for p in platform),
          f"{len(platform)} polygons, {sum(p['area'] for p in platform):.2f} m2, {sum(1 for p in platform if p['reachable'])} reachable")
    ramp = [p for p in polygons if p["reachable"]
            and (sum(v[1] for v in p["vertices"]) / len(p["vertices"])) > 0.5
            and polygon_bounds(p)[0][0] >= -4.5 and polygon_bounds(p)[1][0] <= 2.5
            and polygon_bounds(p)[0][2] >= 0.5 and polygon_bounds(p)[1][2] <= 4.5]
    check("the mirrored and scaled ramp is walkable and reachable (its winding follows the mirror)", len(ramp) > 0,
          f"{len(ramp)} reachable polygons above 0.5 m on the ramp")
    reachable_mesh = navigation.mesh(reachable_only=True)
    check("get_navigation_mesh reachable_only lists the reachable polygons",
          reachable_mesh.get("returned_count") == status.get("reachable_polygon_count"),
          f"{reachable_mesh.get('returned_count')} vs {status.get('reachable_polygon_count')}")

    check_reads(editor, navigation, scene, status)
    check_visualization(editor, navigation, scene, work, status)

    # A second build of the same scene: clear, then build again.
    first = json.dumps(polygons, sort_keys=True)
    cleared = editor.call("navigation_clear", {"scene_name": scene})
    check("navigation_clear removes the mesh and its visualization node",
          cleared.get("cleared") and not navigation.status().get("available")
          and ("Navigation Mesh" not in {node.get("name") for node in scene_nodes(editor, scene)}))
    job = navigation.build()
    second = json.dumps(navigation.mesh().get("polygons", []), sort_keys=True)
    check("a second build gives the same polygons", (job.get("state") == "done") and (first == second))
    built = navigation.status()

    # Staleness: each reason raised by its edit and ended by its undo.
    check_staleness(navigation, "moving an input mesh node", "geometry changed",
                    lambda: editor.call("set_node_transform", {"scene_name": scene, "node_name": "pillar_a", "space": "local", "translation": [-6.0, 0.0, -7.0]}),
                    lambda: editor.call("undo"))
    check_staleness(navigation, "deleting an input mesh node", "geometry changed",
                    lambda: editor.call("delete_nodes", {"scene_name": scene, "names": ["pillar_b"]}),
                    lambda: editor.call("undo"))
    check_staleness(navigation, "a build setting edit", "settings changed",
                    lambda: navigation.set_navigation({"cell_size": 0.2}),
                    lambda: navigation.set_navigation({"cell_size": 0.15}))
    check_staleness(navigation, "unmarking the seed", "seeds changed",
                    lambda: editor.call("navigation_set_seed", {"scene_name": scene, "node": "seed", "seed": False}),
                    lambda: editor.call("undo"))
    check_staleness(navigation, "moving the seed", "seeds changed",
                    lambda: editor.call("set_node_transform", {"scene_name": scene, "node_name": "seed", "space": "local", "translation": [-8.0, 0.05, 1.0]}),
                    lambda: editor.call("undo"))

    # Visibility is an input of the static set: a hide, its own or an ancestor's, is "geometry changed".
    hide = lambda name: editor.call("set_item_property", {"scene_name": scene, "item_name": name, "property": "visible", "value": False})  # noqa: E731
    check_staleness(navigation, "hiding an input mesh", "geometry changed", lambda: hide("wall"), lambda: editor.call("undo"))
    check_staleness(navigation, "hiding an input mesh's parent", "geometry changed", lambda: hide("deck"), lambda: editor.call("undo"))

    # A seed search extent edit is no build setting: the next build reruns the flood alone.
    navigation.set_navigation({"seed_search_radius": 2.0})
    after_edit = navigation.status()
    check("a seed search extent edit is not 'settings changed'", after_edit.get("stale_reasons") == [], json.dumps(after_edit.get("stale_reasons")))
    job = navigation.build()
    flooded = navigation.status()
    check("the build after a search extent edit reruns the flood without a rebuild",
          (job.get("state") == "done") and (flooded.get("build_count") == built.get("build_count")) and (flooded.get("flood_count") == built.get("flood_count") + 1)
          and (flooded.get("reachable_area") == built.get("reachable_area")),
          f"build_count {built.get('build_count')} -> {flooded.get('build_count')}, flood_count {built.get('flood_count')} -> {flooded.get('flood_count')}")
    navigation.set_navigation({"seed_search_radius": 1.0})

    # A seed 50 m above the scene: the build fails with the seed's reason and the installed mesh stays.
    editor.call("set_node_transform", {"scene_name": scene, "node_name": "seed", "space": "local", "translation": [SEED[0], 50.0, SEED[2]]})
    before_failure = navigation.status()
    job = navigation.build()
    after_failure = navigation.status()
    check("a seed 50 m above the scene fails the build with the seed's reason",
          (job.get("state") == "failed") and ("no polygon within" in job.get("error", "")) and ("'seed'" in job.get("error", "")),
          job.get("error", json.dumps(job)[:300]))
    check("the failed build keeps the installed mesh",
          after_failure.get("available") and all(after_failure.get(k) == before_failure.get(k)
                                                 for k in ("polygon_count", "reachable_area", "build_count", "flood_count")),
          f"polygons {after_failure.get('polygon_count')}, reachable {after_failure.get('reachable_area')}")
    editor.call("undo")
    editor.wait_idle()

    # A mesh hidden at build time is no input: showing it is "geometry changed", hiding it again ends it.
    hide("wall")
    editor.wait_idle()
    job = navigation.build()
    hidden_build = navigation.status()
    check("a build with the wall hidden reads six input meshes and is current",
          (job.get("state") == "done") and (hidden_build.get("input_mesh_count") == 6) and not hidden_build.get("stale"),
          f"meshes {hidden_build.get('input_mesh_count')}, stale_reasons {hidden_build.get('stale_reasons')}")
    check_staleness(navigation, "showing a mesh hidden at build time", "geometry changed",
                    lambda: editor.call("undo"), lambda: editor.call("redo"))
    editor.call("undo")
    editor.wait_idle()

    # The stage of a running build, from a larger build (a finer cell size); informational.
    navigation.set_navigation({"cell_size": 0.03, "cell_height": 0.03})
    job = navigation.build()
    navigation.set_navigation({"cell_size": 0.15, "cell_height": 0.10})
    seen = sorted(s for s in navigation.stages if s)
    print(f"INFO: get_save_status stages seen while builds ran: {seen if seen else 'none'} (fine build {job.get('state')}, {job.get('save_ms', 0.0):.0f} ms)", flush=True)
    check("get_save_status reported a stage while a build ran (or the builds were too short to see one)", True,
          f"stages {seen}" if seen else "not observed")

    # Closing the scene releases the visualization with everything else (the scene-close leak watchdog).
    editor.close(scene)
    deadline = time.monotonic() + 60.0
    marker = f"of closed scene '{scene}'"
    while (time.monotonic() < deadline) and not any((marker in line) and ("scene-close check" in line) for line in pav.read_log().splitlines()):
        time.sleep(0.2)
    leaks = [line for line in pav.read_log().splitlines() if ("scene-close leak" in line) and (scene in line)]
    released = any((marker in line) and ("scene-close check" in line) for line in pav.read_log().splitlines())
    check("closing the scene leaves nothing of it alive", released and not leaks, (leaks[:3] if leaks else "no all-released line") if not (released and not leaks) else "")


KIND_NAVIGATION_MESH = 9


def navigation_items(store_path):
    """{key: payload bytes} of the navigation mesh items of an editor asset, and its navigation.key record."""
    items, records = pav.read_store(store_path)
    return {key: len(value[4]) for key, value in items.items() if value[0] == KIND_NAVIGATION_MESH}, records.get("navigation.key", "")


def material_names(editor, scene):
    return [material.get("name", "") for material in editor.call("get_scene_materials", {"scene_name": scene}).get("materials", [])]


def summary(status):
    keys = ("available", "stale", "stale_reasons", "polygon_count", "reachable_polygon_count", "total_area", "reachable_area",
            "tile_count", "input_mesh_count", "input_shape_count", "input_triangle_count", "skipped_mesh_count", "built_seeds")
    return {key: status.get(key) for key in keys}


def reopen(editor, scene, path):
    editor.close(scene)
    return editor.load(path)


def check_saving(editor, work):
    """Step 8 of doc/plans/navigation_mesh.md: the item, the record, the open."""
    directory = os.path.join(work, "saving")
    os.makedirs(directory, exist_ok=True)
    path  = os.path.join(directory, "navigation_saving.glb")
    store = os.path.join(directory, "navigation_saving.erhe_editor")
    foreign = editor.open_foreign(write_scene(work))
    editor.set_export_settings(foreign, {})
    editor.call("navigation_set_seed", {"scene_name": foreign, "node": "seed"})
    editor.save(foreign, path)
    scene = reopen(editor, foreign, path)
    navigation = Navigation(editor, scene)
    status = navigation.status()
    check("saving: the saved scene opens with its seed and without a navigation mesh or item",
          (status.get("seed_count") == 1) and not status.get("available") and (status.get("item") is None), json.dumps(status)[:300])
    check("saving: no navigation material is listed before a build", not any(n.startswith("Navigation") for n in material_names(editor, scene)))

    job = navigation.build()
    built = navigation.status()
    item = built.get("item") or {}
    items, record = navigation_items(store)
    check("saving: a build stores the item: made, with its key and bytes, the one navigation mesh item the record names",
          (job.get("state") == "done") and item.get("made") and (len(item.get("key", "")) == 32) and (item.get("bytes", 0) > 0)
          and (items == {item.get("key"): item.get("bytes")}) and (record == item.get("key")),
          f"item {item}, editor asset items {items}, record '{record}'")
    names = material_names(editor, scene)
    check("saving: no navigation material is listed with the visualization installed (get_scene_materials)",
          ("grey" in names) and not any(n.startswith("Navigation") for n in names), f"{names}")

    navigation.build()
    again = navigation.status().get("item") or {}
    check("saving: a build of the unchanged scene finds the same item (same key, not made)",
          (again.get("key") == item.get("key")) and (again.get("made") is False), f"{item} -> {again}")

    built   = navigation.status()
    polygons = navigation.mesh().get("polygons", [])
    editor.save(scene, path)
    scene = reopen(editor, scene, path)
    navigation = Navigation(editor, scene)
    opened = navigation.status()
    check("saving: the reopened scene shows the same totals, seeds and seed results, current, without a build",
          (summary(opened) == summary(built)) and opened.get("available") and not opened.get("stale")
          and (opened.get("build_count") == 0) and (opened.get("flood_count") == 0),
          f"opened {json.dumps(summary(opened))[:400]}, built {json.dumps(summary(built))[:400]}, build_count {opened.get('build_count')}")
    check("saving: the reopened item is the stored one, not made", (opened.get("item") or {}).get("key") == item.get("key"), json.dumps(opened.get("item")))
    check("saving: the reopened scene has the visualization node", "Navigation Mesh" in {node.get("name") for node in scene_nodes(editor, scene)})
    check("saving: get_navigation_mesh of the reopened scene gives the same polygons",
          json.dumps(navigation.mesh().get("polygons", []), sort_keys=True) == json.dumps(polygons, sort_keys=True))

    # A flood alone (the seed moved within the reachable area) stores a new item in place of the old one.
    editor.call("set_node_transform", {"scene_name": scene, "node_name": "seed", "space": "local", "translation": [SEED[0], SEED[1], 0.0]})
    editor.wait_idle()
    job = navigation.build()
    flooded = navigation.status()
    new_item = flooded.get("item") or {}
    items, record = navigation_items(store)
    prepared = editor.status(scene)
    kind = pav.status_kind(prepared, "navigation_mesh")
    check("saving: a flood alone stores a new item and removes the old one: one navigation mesh item in the editor asset",
          (job.get("state") == "done") and (flooded.get("build_count") == 0) and (flooded.get("flood_count") == 1)
          and new_item.get("made") and (new_item.get("key") != item.get("key")) and (items == {new_item.get("key"): new_item.get("bytes")})
          and (record == new_item.get("key")),
          f"build_count {flooded.get('build_count')}, flood_count {flooded.get('flood_count')}, item {new_item}, editor asset {items}, record '{record}'")
    check("saving: the prepared status counts the recorded item as asked (present), not unreferenced",
          (kind.get("present") == 1) and (kind.get("stored_count") == 1) and (kind.get("unreferenced_count", 0) == 0), json.dumps(kind))

    # An edit saved without a build makes the reopened mesh stale with its reason.
    def stale_after_reopen(label, reason, edit):
        nonlocal scene, navigation
        edit()
        editor.wait_idle()
        editor.save(scene, path)
        scene = reopen(editor, scene, path)
        navigation = Navigation(editor, scene)
        reopened = navigation.status()
        check(f"saving: {label}, saved without a build, makes the reopened mesh stale: '{reason}'",
              reopened.get("available") and (reopened.get("stale_reasons") == [reason]) and (reopened.get("build_count") == 0),
              f"available {reopened.get('available')}, stale_reasons {reopened.get('stale_reasons')}")

    stale_after_reopen("a moved mesh", "geometry changed",
                       lambda: editor.call("set_node_transform", {"scene_name": scene, "node_name": "pillar_a", "space": "local", "translation": [-6.0, 0.0, -7.0]}))
    job = navigation.build()
    check("saving: the build after the reopen makes it current", (job.get("state") == "done") and not navigation.status().get("stale"))
    stale_after_reopen("a build setting edit", "settings changed", lambda: navigation.set_navigation({"cell_size": 0.2}))
    navigation.set_navigation({"cell_size": 0.15})
    editor.wait_idle()
    check("saving: setting the build setting back ends 'settings changed'", navigation.status().get("stale_reasons") == [],
          json.dumps(navigation.status().get("stale_reasons")))
    stale_after_reopen("unmarking the seed", "seeds changed", lambda: editor.call("navigation_set_seed", {"scene_name": scene, "node": "seed", "seed": False}))
    editor.call("navigation_set_seed", {"scene_name": scene, "node": "seed"})
    editor.wait_idle()
    check("saving: marking the saved seed node again ends 'seeds changed'", navigation.status().get("stale_reasons") == [],
          json.dumps(navigation.status().get("stale_reasons")))

    # Drops keep the item the record names.
    recorded = (navigation.status().get("item") or {}).get("key")
    for drop in ("unreferenced", "all"):
        editor.status(scene, drop=drop)
        items, record = navigation_items(store)
        check(f"saving: a drop of {drop} items keeps the recorded navigation mesh item",
              (recorded is not None) and (items.keys() == {recorded}) and (record == recorded), f"recorded {recorded}, editor asset {items}, record '{record}'")

    # Clear removes the item and the record; a reopen installs nothing.
    editor.call("navigation_clear", {"scene_name": scene})
    editor.wait_idle()
    items, record = navigation_items(store)
    cleared = navigation.status()
    check("saving: Clear removes the item and the record", (items == {}) and (record == "") and (cleared.get("item") is None) and not cleared.get("available"),
          f"editor asset {items}, record '{record}', item {cleared.get('item')}")
    editor.save(scene, path)
    scene = reopen(editor, scene, path)
    navigation = Navigation(editor, scene)
    check("saving: after Clear a reopen installs nothing", not navigation.status().get("available")
          and ("Navigation Mesh" not in {node.get("name") for node in scene_nodes(editor, scene)}))
    editor.close(scene)


HEADLESS_EDITOR = "build_ninja_linux_vulkan_headless/bin/editor"
DEBUG_EDITOR    = "build/Ninja_Vulkan_Debug/bin/editor"


def default_editor():
    return HEADLESS_EDITOR if os.path.isfile(os.path.join(pav.REPO_ROOT, HEADLESS_EDITOR)) else DEBUG_EDITOR


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=default_editor(), help="editor binary (relative to the repository root or absolute); "
                        "default: the Linux headless tree's editor when it is built, else the Linux Debug tree's")
    parser.add_argument("--keep", action="store_true", help="keep the work directory")
    args = parser.parse_args()

    start = time.monotonic()
    work = os.path.realpath(tempfile.mkdtemp(prefix="navigation_verify_"))
    print(f"work directory {work}", flush=True)
    editor = None
    try:
        editor = Editor(args.editor)
        run(editor, work)
        check_saving(editor, work)
    except Exception as error:  # noqa: BLE001 - a failed step is a failed check
        check("run completed without an error", False, f"{type(error).__name__}: {error}")
    finally:
        for launched in pav.LAUNCHED:
            launched.stop()
        if args.keep:
            print(f"kept {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)
    failed = [label for label, ok in pav.RESULTS if not ok]
    print(f"\n{len(pav.RESULTS) - len(failed)}/{len(pav.RESULTS)} checks passed, {len(failed)} failed in {time.monotonic() - start:.0f} s")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
