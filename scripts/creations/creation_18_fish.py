#!/usr/bin/env python3
"""Creation 18: a smooth fish, sculpted procedurally.

The BODY is a geometry graph (box cage, subdivisions [2, 1, 0] -> one level
of Catmull-Clark -> bezier lattice FFD -> one more subdivision level): seven
lattice stations squeeze the cage toward the centerline (hard tail pinch,
belly at mid-forward, converging snout with a slight droop) and a
per-height-row extra beam squeeze rounds the cross-section into a fish oval.
The TAIL is a lattice-fanned thin box forked with a CSG cylinder cut; the
DORSAL fin is a lattice-raked thin box; paired PECTORAL / PELVIC fins and the
ANAL fin share one pooled sweep-blade brush. Eyes are placed by probing the
actual body surface (geometry_query closest points).

The body SKIN is two live procedural texture graphs bound to the Fish Body
material: a quincunx scale-scallop height field (soft circle dome tiled twice
with a half-cell stagger, merged with lighten = max) drives a colorized
albedo with a soft-light fbm mottle, and the same field through normal_map
gives per-scale relief.

    py -3 scripts/creations/creation_18_fish.py [--reuse] [--reframe GLB]
                                                [--only Fish]
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
from common import (  # noqa: E402
    axis_angle_quaternion, quat_mul, v_add,
)

TITLE = "Fish"
BASE = "logs/creations/fish"
SAVE_GLB = "res/editor/scenes/creations/fish.glb"

# ----------------------------------------------------------------- body graph
# Body axes: +X nose, -X tail, Y up, Z beam (laterally compressed).
BODY_SIZE = [3.0, 1.3, 0.72]
# Interior subdivision planes per axis (0 = corner vertices only).
BODY_SUBDIVISIONS = [2, 1, 0]
DIVISIONS = [6, 2, 2]  # 7 x 3 x 3 control points

# Per-station squeeze toward the centerline, i = 0 (tail) .. 6 (nose), as a
# fraction of the cage half-extent removed at that station. Bezier FFD
# smooths the interior stations heavily; endpoints apply exactly.
SQUEEZE_Y = [0.72, 0.60, 0.36, 0.08, 0.00, 0.42, 0.90]
SQUEEZE_Z = [0.94, 0.80, 0.52, 0.15, 0.05, 0.50, 0.88]
# Dorsal arc + nose droop.
SHIFT_Y = [0.00, 0.02, 0.03, 0.02, 0.00, -0.06, -0.16]
# Axial pull toward the body center at the caps, so subdivision rounds the
# nose/tail instead of leaving a flat swirl-pole face.
SHIFT_X = [0.10, 0.04, 0.00, 0.00, 0.00, -0.08, -0.28]
# Extra beam squeeze per height row (j = belly, mid, back), fraction of the
# remaining half-beam: narrows back + belly into a fish oval cross-section.
EDGE_TAPER = [0.45, 0.00, 0.78]

FISH_POS = [0.0, 1.5, 0.0]  # world position of the body node

# ---------------------------------------------------------------- skin graphs
# Scale scallops per UV tile (the body's box-cage UVs survive as a few large
# coherent tiles, so the pattern repeats per tile). 16 read as moire stripes
# at fish scale; 6 reads as scales.
SCALE_CELLS = 6.0
ALBEDO_GRAPH = "Fish Scales Albedo"
NORMAL_GRAPH = "Fish Scales Normal"


def body_lattice_offsets():
    """Flat offsets array in lattice_offset_index order:
    index = i + (nx) * (j + (ny) * k)."""
    dx, dy, dz = DIVISIONS
    counts = (dx + 1, dy + 1, dz + 1)
    half = [BODY_SIZE[0] / 2.0, BODY_SIZE[1] / 2.0, BODY_SIZE[2] / 2.0]
    offsets = [0.0] * (counts[0] * counts[1] * counts[2] * 3)
    for k in range(counts[2]):
        z = -half[2] + 2.0 * half[2] * k / dz
        for j in range(counts[1]):
            y = -half[1] + 2.0 * half[1] * j / dy
            for i in range(counts[0]):
                index = 3 * (i + counts[0] * (j + counts[1] * k))
                z_squeeze = SQUEEZE_Z[i] + (1.0 - SQUEEZE_Z[i]) * EDGE_TAPER[j]
                offsets[index + 0] = SHIFT_X[i]
                offsets[index + 1] = -y * SQUEEZE_Y[i] + SHIFT_Y[i]
                offsets[index + 2] = -z * z_squeeze
    return offsets


# -------------------------------------------------------------------- camera
# (suffix, eye, target) world-space shots; hoisted for --reframe.
SHOTS = [
    ("side",    [0.3, 1.9, 5.6],  [0.0, 1.4, 0.0]),
    ("quarter", [3.6, 2.6, 4.2],  [0.1, 1.4, 0.0]),
    ("front",   [4.6, 1.5, 1.6],  [0.0, 1.45, 0.0]),
    ("top",     [0.4, 6.4, 1.6],  [0.0, 1.4, 0.0]),
]


def build_scene_setup(c):
    c.ambience(ambient=[0.18, 0.24, 0.30],
               clear_color=[0.05, 0.12, 0.20],
               sky={"_version": 3, "enabled": True, "mode": 1}, grid=False)
    c.shadow_range(40.0)
    c.light("directional", "Sun", [0.0, 10.0, 0.0], [1.0, 0.96, 0.88], 2.6)
    q = quat_mul(
        axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(40.0)),
        axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(-132.0)))
    c.set_node_transform("Sun", rotation_xyzw=q)
    c.light("directional", "Fill", [0.0, 6.0, 0.0], [0.5, 0.7, 0.9], 0.7,
            cast_shadow=False)
    qf = quat_mul(
        axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(-130.0)),
        axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(-150.0)))
    c.set_node_transform("Fill", rotation_xyzw=qf)
    # Sandy seabed slab (thin so the far edge reads as a hairline).
    sand = c.ensure_material("Seabed Sand", base_color=[0.62, 0.56, 0.42],
                             roughness=0.95, metallic=0.0)
    seabed = c.shape("box", "Seabed", [0.0, -0.04, 0.0], size=[46.0, 0.08, 46.0],
                     material_name=sand)
    # The ground only RECEIVES shadows; its own cast just draws dark bands at
    # the shadow-fit edge.
    seabed_id = seabed.get("node_id") if isinstance(seabed, dict) else None
    if seabed_id is not None:
        c.mutate("set_item_flags", {"scene_name": c.scene, "ids": [seabed_id],
                                    "flags": ["shadow_cast"], "enabled": False})


def grad(stops, interpolation=1):
    return {"interpolation": interpolation,
            "stops": [{"pos": p, "color": list(col)} for p, col in stops]}


def scallop_height(g):
    """Quincunx scale scallop height field: one soft circle dome (edge 1.0 =
    full radial falloff) tiled twice with repeat transforms, the second grid
    staggered by half a cell, merged with lighten (= max). Returns the
    grayscale node id."""
    dome = g.add("shape", {"shape": 0, "radius": 0.95, "edge": 1.0})
    rgba = g.add("ensure_rgba")
    g.link(dome, rgba)
    grids = []
    for stagger in (0.0, 0.5):
        tile = g.add("transform", {
            "translate_x": stagger / SCALE_CELLS,
            "translate_y": stagger / SCALE_CELLS,
            "scale_x": 1.0 / SCALE_CELLS,
            "scale_y": 1.0 / SCALE_CELLS,
            "repeat": True,
        })
        g.link(rgba, tile)
        grids.append(tile)
    merge = g.add("blend", {"blend_type": 9, "amount": 1.0})  # lighten = max
    g.link(grids[0], merge)
    g.link(grids[1], merge, dst_slot=1)
    grey = g.add("greyscale", {"mode": 4})  # max - undoes the vec3 spread
    g.link(merge, grey)
    return grey


def build_skin_graphs(c):
    """Two Graph_texture assets (one baked output each): scallop -> colorize
    albedo and scallop -> normal_map. Skipped when they already exist
    (--only reruns against a populated library)."""
    listing = c.call("get_graph_textures", {"scene_name": c.scene})
    existing = {entry.get("name") for entry in listing.get("graph_textures", [])}
    if ALBEDO_GRAPH not in existing:
        g = c.texture_graph(ALBEDO_GRAPH)
        height = scallop_height(g)
        color = g.add("colorize", {"gradient": grad([
            (0.00, [0.28, 0.08, 0.02, 1.0]),   # crevice between scales
            (0.45, [0.85, 0.34, 0.08, 1.0]),
            (0.85, [1.00, 0.52, 0.14, 1.0]),   # body orange
            (1.00, [1.00, 0.78, 0.34, 1.0])])})  # scale-center sheen
        g.link(height, color)
        mottle = g.add("fbm", {"noise": 1, "scale_x": 4.0, "scale_y": 4.0,
                               "iterations": 5.0})
        mottle_rgba = g.add("ensure_rgba")
        g.link(mottle, mottle_rgba)
        mix = g.add("blend", {"blend_type": 6, "amount": 0.20})  # soft light
        g.link(mottle_rgba, mix)         # s1: mottle layer
        g.link(color, mix, dst_slot=1)   # s2: scale albedo base
        out = g.add("output", {"name": ALBEDO_GRAPH, "size": 1024})
        g.link(mix, out, dst_slot=2)     # rgba slot
    if NORMAL_GRAPH not in existing:
        g = c.texture_graph(NORMAL_GRAPH)
        height = scallop_height(g)
        normal = g.add("normal_map", {"amount": 0.7, "size": 10})
        g.link(height, normal)
        out = g.add("output", {"name": NORMAL_GRAPH, "size": 1024})
        g.link(normal, out, dst_slot=1)  # rgb slot


def build_fish(c):
    # White base-color factor: the scales albedo graph carries the orange.
    body_mat = c.ensure_material("Fish Body", base_color=[1.0, 1.0, 1.0],
                                 roughness=0.38, metallic=0.05)
    # ensure_material returns a pre-existing material unchanged (--only path);
    # the factor must be white or it tints the albedo texture.
    c.mutate("edit_material", {"scene_name": c.scene, "material_name": body_mat,
                               "base_color": [1.0, 1.0, 1.0]})
    build_skin_graphs(c)
    c.bind_material_texture(body_mat, ALBEDO_GRAPH, slot="base_color", wrap="repeat")
    c.bind_material_texture(body_mat, NORMAL_GRAPH, slot="normal", wrap="repeat")
    fin_mat = c.ensure_material("Fish Fin", base_color=[0.80, 0.30, 0.13],
                                roughness=0.5, metallic=0.0,
                                blending_mode="alpha_blend", opacity=0.95)
    eye_mat = c.ensure_material("Fish Eye", base_color=[0.03, 0.03, 0.035],
                                roughness=0.2, metallic=0.0)

    # ------------------------------------------------------------- body graph
    g = c.geometry_graph("Fish Body Graph")
    box = g.add("box", {"size": BODY_SIZE, "subdivisions": BODY_SUBDIVISIONS, "power": 1.0})
    subdivide_pre = g.add("subdivide", {"mode": 0, "iterations": 1})
    lattice = g.add("lattice", {
        # Explicit cage at the box bounds: the offsets are authored in this
        # frame, and one Catmull-Clark level shrinks the mesh inside it, so
        # auto-fit would rescale the sculpt unpredictably.
        "auto_fit": False,
        "cage_min": [-0.5 * v for v in BODY_SIZE],
        "cage_max": [0.5 * v for v in BODY_SIZE],
        "divisions": DIVISIONS,
        "interpolation": 1,  # bezier FFD - globally smooth
        "show_cage": False,
        "offsets": body_lattice_offsets(),
    })
    subdivide_post = g.add("subdivide", {"mode": 0, "iterations": 1})
    out = g.add("output", {"material": body_mat})
    g.chain([box, subdivide_pre, lattice, subdivide_post, out])
    c.call("get_geometry_graph")  # evaluation barrier

    body = c.bind_node_mesh("Fish", "Fish Body Graph")
    c.move_node(body, translation=FISH_POS)
    c.settle()
    body_id = c.node_by_name(body)["id"]

    # Live body bounds drive every attachment point.
    aabb_min, aabb_max = c.subtree_world_aabb(body)
    tail_x = aabb_min[0]           # rearmost body point
    nose_x = aabb_max[0]
    top_y = aabb_max[1]
    mid_y = (aabb_min[1] + aabb_max[1]) * 0.5

    # ------------------------------------------------------------- tail fin
    # Thin box fanned with a lattice, then a cylinder CSG cut forks it.
    tail_w, tail_h, tail_t = 1.05, 1.35, 0.045
    tail_center = [tail_x - tail_w / 2.0 + 0.34, mid_y, 0.0]
    c.shape("box", "Tail Fin", tail_center, size=[tail_w, tail_h, tail_t],
            steps=[6, 6, 1], material_name=fin_mat, reuse=False,
            parent_node_id=body_id, motion_mode="none")
    # Fan: pinch the attach column (i=2, +X side) hard, mid column halfway;
    # sweep the outer rear corners slightly backward for lobes.
    fan = []
    for j in range(3):
        for k in range(2):
            y = -1.0 + j  # -1, 0, +1 row factor
            fan.append([2, j, k, 0.0, -y * 0.72 * (tail_h / 2.0), 0.0])
            fan.append([1, j, k, 0.0, -y * 0.26 * (tail_h / 2.0), 0.0])
            if j != 1:
                fan.append([0, j, k, -0.16, 0.0, 0.0])
    c.lattice_deform("Tail Fin", fan, divisions=[2, 2, 1],
                     interpolation="bezier", wait=True)
    for _ in range(2):  # one level per call
        c.mutate("catmull_clark", {"scene_name": c.scene, "node_name": "Tail Fin"})
    c.settle()
    # Fork: subtract a beam-spanning cylinder (equal-radius capped cone laid
    # along Z) whose surface dips into the trailing edge.
    fork_r = 0.62
    fork = c.shape("cone", "Tail Fork Cut",
                   [tail_center[0] - tail_w / 2.0 - fork_r * 0.78, mid_y, -0.2],
                   height=0.4, bottom_radius=fork_r, top_radius=fork_r,
                   slice_count=48, reuse=False, motion_mode="none",
                   rotation_xyzw=axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(90.0)))
    c.csg("Tail Fin", fork.get("node_id"), operation="difference", wait=True)

    # ------------------------------------------------------------ dorsal fin
    dorsal_w, dorsal_h, dorsal_t = 0.95, 0.5, 0.032
    # Probe the actual back line under the fin base - the back arcs, so the
    # AABB top only holds at the hump.
    back = c.closest_points(
        [[-0.45, top_y + 0.5, 0.0], [0.0, top_y + 0.5, 0.0], [0.45, top_y + 0.5, 0.0]],
        node_name=body)
    back_y = min(p.get("position", [0.0, top_y, 0.0])[1] for p in back)
    dorsal_center = [0.0, back_y + dorsal_h / 2.0 - 0.16, 0.0]
    c.shape("box", "Dorsal Fin", dorsal_center,
            size=[dorsal_w, dorsal_h, dorsal_t], steps=[6, 4, 1],
            material_name=fin_mat, reuse=False, parent_node_id=body_id,
            motion_mode="none")
    # Rake the top backward, taper the top edge thickness, drop the rear top
    # corner (shark-fin profile).
    rake = []
    for k in range(2):
        for i in range(3):
            x = -1.0 + i
            rake.append([i, 2, k, -0.36 + x * -0.05, -0.14 if i == 0 else 0.0, 0.0])
        rake.append([2, 1, k, -0.08, 0.0, 0.0])
    c.lattice_deform("Dorsal Fin", rake, divisions=[2, 2, 1],
                     interpolation="bezier", wait=True)
    for _ in range(2):  # one level per call
        c.mutate("catmull_clark", {"scene_name": c.scene, "node_name": "Dorsal Fin"})
    c.settle()

    # ------------------------------------------- paired fins: sweep blades
    # One pooled blade brush; per-instance pose + node scale.
    blade_profile = [[0.0, -0.05], [0.5, -0.02], [1.0, 0.0],
                     [0.5, 0.02], [0.0, 0.05], [-0.5, 0.02], [-1.0, 0.0],
                     [-0.5, -0.02]]
    blade_profile = [[0.13 * px, 0.36 * py] for px, py in blade_profile]

    def blade(name, position, yaw_deg, pitch_deg, roll_deg, scale):
        q = quat_mul(
            axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(yaw_deg)),
            quat_mul(
                axis_angle_quaternion([0.0, 0.0, 1.0], math.radians(pitch_deg)),
                axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(roll_deg))))
        c.shape("sweep", name, position,
                profile=blade_profile,
                spine=[[0.0, 0.0, 0.0], [0.14, -0.02, 0.0],
                       [0.3, -0.06, 0.0], [0.42, -0.12, 0.0]],
                spine_steps=10,
                taper=[[0.0, 0.75], [0.35, 1.0], [1.0, 0.0]],
                material_name=fin_mat, rotation_xyzw=q, scale=scale,
                parent_node_id=body_id, motion_mode="none")

    # Probe the actual flank surface for pectoral anchors.
    flank = c.closest_points(
        [[0.55, mid_y + 0.05, 0.5], [0.55, mid_y + 0.05, -0.5]],
        node_name=body)
    for side, probe in zip((1.0, -1.0), flank):
        p = probe.get("position", [0.55, mid_y, side * 0.3])
        p = v_add(p, [0.0, 0.0, side * 0.035])
        blade(f"Pectoral Fin {'L' if side > 0 else 'R'}", p,
              yaw_deg=180.0 + side * 30.0, pitch_deg=24.0, roll_deg=side * 42.0,
              scale=[1.0, 1.0, 1.0])
    # Pelvic pair (below, slightly behind pectorals) + anal fin.
    belly = c.closest_points(
        [[-0.1, aabb_min[1] + 0.1, 0.15], [-0.1, aabb_min[1] + 0.1, -0.15],
         [-0.7, aabb_min[1] + 0.12, 0.0]],
        node_name=body)
    for side, probe in zip((1.0, -1.0), belly[:2]):
        p = probe.get("position", [-0.1, mid_y - 0.5, side * 0.12])
        p = v_add(p, [0.0, -0.05, side * 0.015])
        blade(f"Pelvic Fin {'L' if side > 0 else 'R'}", p,
              yaw_deg=180.0 + side * 10.0, pitch_deg=62.0, roll_deg=0.0,
              scale=[0.7, 0.7, 0.7])
    anal_p = v_add(belly[2].get("position", [-0.7, mid_y - 0.5, 0.0]), [0.0, -0.05, 0.0])
    blade("Anal Fin", anal_p, yaw_deg=180.0, pitch_deg=66.0, roll_deg=0.0,
          scale=[0.8, 0.8, 0.8])

    # ------------------------------------------------------------------ eyes
    eyes = c.closest_points(
        [[nose_x - 0.42, mid_y + 0.16, 0.5], [nose_x - 0.42, mid_y + 0.16, -0.5]],
        node_name=body)
    for side, probe in zip((1.0, -1.0), eyes):
        hit = probe.get("position")
        normal = probe.get("normal", [0.0, 0.0, side])
        p = v_add(hit, [-0.025 * n for n in normal]) if hit else [1.0, mid_y + 0.16, side * 0.2]
        c.shape("uv_sphere", f"Eye {'L' if side > 0 else 'R'}", p,
                radius=0.055, slice_count=20, stack_count=12,
                material_name=eye_mat, parent_node_id=body_id,
                motion_mode="none")

    c.settle()
    return body


def main():
    args = common.standard_args("creation 18: smooth fish")
    if common.reframe(args, TITLE, BASE, SHOTS):
        return
    c = common.Creation(TITLE, port=args.port, pause_s=args.pause,
                        editor_exe=args.editor_exe,
                        reuse=args.reuse or bool(args.only),
                        keep_scenes=args.keep_scenes or bool(args.only))
    with common.fail_soft(c, BASE, failed_glb=None):
        if args.only:
            c.attach_scene()
            c.delete_nodes(names=[args.only])
            build_fish(c)
        else:
            c.new_scene()
            build_scene_setup(c)
            build_fish(c)
        common.hierarchy_report(c, "fish hierarchy")
        c.screenshot_views(BASE, SHOTS)
        if not args.no_save:
            c.save(SAVE_GLB)
    print("fish creation complete.")


if __name__ == "__main__":
    main()
