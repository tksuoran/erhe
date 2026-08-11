#!/usr/bin/env python3
"""Creation 18: two smooth fish, sculpted procedurally.

FISH ONE (generic orange fish) - the BODY is a geometry graph (box cage,
subdivisions [2, 1, 0] -> one level of Catmull-Clark -> bezier lattice FFD ->
one more subdivision level): seven lattice stations squeeze the cage toward
the centerline (hard tail pinch, belly at mid-forward, converging snout with
a slight droop) and a per-height-row extra beam squeeze rounds the
cross-section into a fish oval. The TAIL is a lattice-fanned thin box forked
with a CSG cylinder cut; the DORSAL fin is a lattice-raked thin box; paired
PECTORAL / PELVIC fins and the ANAL fin share one pooled sweep-blade brush.
Eyes are placed by probing the actual body surface (geometry_query closest
points). The body SKIN is two live procedural texture graphs bound to the
Fish Body material: a quincunx scale-scallop height field (soft circle dome
tiled twice with a half-cell stagger, merged with lighten = max) drives a
colorized albedo with a soft-light fbm mottle, and the same field through
normal_map gives per-scale relief.

FISH TWO (Synchiropus splendidus, mandarin dragonet) - same body-graph
machinery with its own cage: rounder cross-section (dragonets are not
laterally compressed), broad flattened head with a blunt face, hard pinch at
the tail base. The TAIL is a rounded fan (convex trailing edge - no fork
cut); a tall raked first-dorsal SAIL plus a low second dorsal; oversized
rounded PELVIC fans (the perching fans), pectoral fans and an anal fin from
a pooled sweep-blade brush; protruding frog-like eyes probed onto the TOP of
the head. The skin is scaleless: an fbm field colorized through a gradient
whose tight orange stops cut labyrinth bands with dark rims out of the blue
base (the classic contour-band read of the mandarin maze), plus a faint
normal for slime relief.

    py -3 scripts/creations/creation_18_fish.py [--reuse] [--reframe GLB]
                                                [--only Fish|Mandarin]
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

# ------------------------------------------------------- mandarin body graph
# Synchiropus splendidus. References: side view (body plan + maze pattern),
# front views (broad flattened head, eyes on TOP, round cross-section),
# line art (silhouette: two dorsals, rounded fan tail, big pelvic fans).
# Dragonets are NOT laterally compressed: beam ~ height, blunt face,
# hard pinch at the tail base only.
M_SIZE = [1.5, 0.55, 0.60]
M_SUBDIVISIONS = [2, 1, 0]
M_DIVISIONS = [6, 2, 2]
M_SQUEEZE_Y = [0.78, 0.58, 0.32, 0.10, 0.04, 0.12, 0.52]
M_SQUEEZE_Z = [0.88, 0.68, 0.38, 0.12, 0.04, 0.08, 0.42]
# Nearly flat back line - the head is flattened, not arched.
M_SHIFT_Y = [0.00, 0.01, 0.02, 0.01, 0.00, -0.01, -0.05]
M_SHIFT_X = [0.06, 0.02, 0.00, 0.00, 0.00, -0.04, -0.14]
# Belly flat-ish (they perch on it), back gently rounded.
M_EDGE_TAPER = [0.30, 0.00, 0.42]

M_POS = [1.4, 0.45, 1.9]  # hovering just above the seabed, like the refs


# ------------------------------------------------- mandarin version registry
# Quality iterations: every iteration ADDS a new, more detailed mandarin next
# to the previous ones (old versions stay in the scene for comparison). Each
# version has its own suffix - node names, materials, and graph-asset names
# all carry it, because create_graph_mesh / create_graph_texture reject
# duplicate names (assets cannot be rebuilt in place).
def _mandarin_v1_config():
    """Version 1, frozen exactly as first landed."""
    return {
        "suffix": "",
        "pos": list(M_POS),
        "size": list(M_SIZE),
        "divisions": list(M_DIVISIONS),
        "squeeze_y": list(M_SQUEEZE_Y),
        "squeeze_z": list(M_SQUEEZE_Z),
        "shift_y": list(M_SHIFT_Y),
        "shift_x": list(M_SHIFT_X),
        "edge_taper": list(M_EDGE_TAPER),
        "post_subdiv": 1,
        "skin": "v1",
        "tail": {"w": 0.55, "h": 0.62, "t": 0.028, "attach": 0.17, "rim": False},
        "sail": {"w": 0.34, "h": 0.44, "t": 0.02, "rake": 0.24, "filament": False},
        "dorsal2": {"w": 0.30, "h": 0.20, "t": 0.018},
        "pectoral_scale": 0.9,
        "pelvic_scale": 1.35, "pelvic_pitch": 48.0, "pelvic_roll": 30.0,
        "anal_scale": 0.9,
        "eye": "single",
    }


def _mandarin_v2_config():
    """V2: goby silhouette (9 stations - narrow caudal peduncle, fuller mid,
    widest head), smoother surface (2 post-subdiv levels), blue-dominant
    elongated maze + yellow speckle, blue-rimmed tail, ring+pupil eyes,
    taller sail with trailing filament, bigger flatter perching fans."""
    cfg = _mandarin_v1_config()
    cfg.update({
        "suffix": " V2",
        "pos": [2.9, 0.45, 1.9],
        "size": [1.6, 0.55, 0.62],
        "divisions": [8, 2, 2],
        "squeeze_y": [0.80, 0.66, 0.45, 0.24, 0.10, 0.04, 0.06, 0.16, 0.55],
        "squeeze_z": [0.90, 0.76, 0.52, 0.26, 0.10, 0.04, 0.02, 0.06, 0.45],
        "shift_y":   [0.00, 0.01, 0.02, 0.02, 0.01, 0.00, -0.01, -0.02, -0.06],
        "shift_x":   [0.07, 0.03, 0.00, 0.00, 0.00, 0.00, 0.00, -0.04, -0.16],
        "edge_taper": [0.28, 0.00, 0.40],
        "post_subdiv": 2,
        "skin": "v2",
        "tail": {"w": 0.58, "h": 0.64, "t": 0.028, "attach": 0.18, "rim": True},
        "sail": {"w": 0.36, "h": 0.50, "t": 0.02, "rake": 0.30, "filament": True},
        "pelvic_scale": 1.5, "pelvic_pitch": 42.0, "pelvic_roll": 45.0,
        "eye": "ring",
    })
    return cfg


def _mandarin_v3_config():
    """V3: vibrant color pass (user direction: side-view reference colors).
    Layered albedo composition (see mandarin_v3_albedo), saturated fin/tail/
    eye materials, wet-sheen roughness; head slimmed slightly from V2's bulb."""
    cfg = _mandarin_v2_config()
    cfg.update({
        "suffix": " V3",
        "pos": [4.4, 0.45, 1.9],
        "skin": "v3",
        "squeeze_y": [0.80, 0.66, 0.45, 0.24, 0.10, 0.05, 0.10, 0.24, 0.58],
        "squeeze_z": [0.90, 0.76, 0.52, 0.26, 0.10, 0.05, 0.06, 0.12, 0.48],
        # Deeper attach: the slimmer V3 peduncle left the V2-depth fan
        # floating off the body tip.
        "tail": {"w": 0.58, "h": 0.64, "t": 0.028, "attach": 0.30, "rim": True},
        "fin_color": [0.02, 0.32, 1.00],
        "tail_color": [1.00, 0.42, 0.02],
        "eye_ring_color": [0.90, 0.20, 0.05],
        "body_roughness": 0.24,
    })
    return cfg


MANDARIN_VERSIONS = [_mandarin_v1_config(), _mandarin_v2_config(),
                     _mandarin_v3_config()]

# ---------------------------------------------------------------- skin graphs
# Scale scallops per UV tile (the body's box-cage UVs survive as a few large
# coherent tiles, so the pattern repeats per tile). 16 read as moire stripes
# at fish scale; 6 reads as scales.
SCALE_CELLS = 6.0
ALBEDO_GRAPH = "Fish Scales Albedo"
NORMAL_GRAPH = "Fish Scales Normal"
M_ALBEDO_GRAPH = "Mandarin Maze Albedo"
M_NORMAL_GRAPH = "Mandarin Maze Normal"


def lattice_offsets(size, divisions, squeeze_y, squeeze_z, shift_y, shift_x,
                    edge_taper):
    """Flat offsets array in lattice_offset_index order:
    index = i + (nx) * (j + (ny) * k)."""
    dx, dy, dz = divisions
    counts = (dx + 1, dy + 1, dz + 1)
    half = [size[0] / 2.0, size[1] / 2.0, size[2] / 2.0]
    offsets = [0.0] * (counts[0] * counts[1] * counts[2] * 3)
    for k in range(counts[2]):
        z = -half[2] + 2.0 * half[2] * k / dz
        for j in range(counts[1]):
            y = -half[1] + 2.0 * half[1] * j / dy
            for i in range(counts[0]):
                index = 3 * (i + counts[0] * (j + counts[1] * k))
                z_squeeze = squeeze_z[i] + (1.0 - squeeze_z[i]) * edge_taper[j]
                offsets[index + 0] = shift_x[i]
                offsets[index + 1] = -y * squeeze_y[i] + shift_y[i]
                offsets[index + 2] = -z * z_squeeze
    return offsets


def body_lattice_offsets():
    return lattice_offsets(BODY_SIZE, DIVISIONS, SQUEEZE_Y, SQUEEZE_Z,
                           SHIFT_Y, SHIFT_X, EDGE_TAPER)


# -------------------------------------------------------------------- camera
# (suffix, eye, target) world-space shots; hoisted for --reframe.
def _mandarin_shots():
    """One close-up per mandarin version, framed relative to its position."""
    shots = []
    for cfg in MANDARIN_VERSIONS:
        tag = ("mandarin" + cfg["suffix"]).strip().lower().replace(" ", "_")
        p = cfg["pos"]
        shots.append((tag, [p[0] + 1.3, p[1] + 0.7, p[2] + 1.8],
                      [p[0] - 0.05, p[1] + 0.05, p[2] - 0.05]))
    return shots


SHOTS = [
    ("side",    [0.3, 1.9, 6.4], [0.2, 1.2, 0.4]),
    ("quarter", [4.0, 2.6, 4.8], [0.3, 1.2, 0.4]),
    ("front",   [5.2, 1.5, 2.2], [0.3, 1.2, 0.5]),
    ("top",     [0.6, 6.8, 1.8], [0.4, 1.0, 0.6]),
] + _mandarin_shots()


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


def mandarin_maze_albedo(g, name, fbm_params, stops, speckle=None):
    """fbm -> colorize contour bands; optional speckle layer dict:
    {'params': fbm params, 'blend_type': N, 'amount': A, 'stops': gradient}
    (stops colorizes the speckle before blending; None keeps it grayscale)."""
    maze = g.add("fbm", fbm_params)
    bands = g.add("colorize", {"gradient": grad(stops)})
    g.link(maze, bands)
    top = bands
    if speckle is not None:
        dots = g.add("fbm", speckle["params"])
        if speckle.get("stops"):
            layer = g.add("colorize", {"gradient": grad(speckle["stops"])})
            g.link(dots, layer)
        else:
            layer = g.add("ensure_rgba")
            g.link(dots, layer)
        mix = g.add("blend", {"blend_type": speckle["blend_type"],
                              "amount": speckle["amount"]})
        g.link(layer, mix)           # s1: speckle layer
        g.link(top, mix, dst_slot=1)  # s2: maze base
        top = mix
    out = g.add("output", {"name": name, "size": 1024})
    g.link(top, out, dst_slot=2)     # rgba slot


def mandarin_v3_albedo(g, name):
    """V3: vibrant layered composition (side-view reference). One shared fbm
    field feeds a VIVID blue colorize (base) and an alpha-masked orange-band
    colorize (labyrinth stripes with navy rims) composited with blend normal
    - the blend node multiplies opacity by s1.alpha, so the colorize alpha
    stops ARE the stripe mask. A linear-gradient node (alpha ramp along u)
    tints the head zone green-teal, voronoi distance-to-center makes
    scattered yellow spots, and the whole composite goes through warp driven
    by a low-frequency fbm so every line wiggles organically."""
    navy = [0.01, 0.04, 0.30, 1.0]
    field = g.add("fbm", {"noise": 1, "scale_x": 4.5, "scale_y": 3.0,
                          "iterations": 4.0})
    base = g.add("colorize", {"gradient": grad([
        (0.00, [0.00, 0.02, 0.22, 1.0]),   # deep navy crevice
        (0.25, [0.01, 0.22, 0.90, 1.0]),   # cobalt
        (0.50, [0.02, 0.42, 1.00, 1.0]),   # vivid blue
        (0.72, [0.05, 0.65, 1.00, 1.0]),   # azure
        (0.88, [0.15, 0.85, 1.00, 1.0]),   # cyan
        (1.00, [0.55, 0.98, 1.00, 1.0])])})
    g.link(field, base)
    orange = g.add("colorize", {"gradient": grad([
        (0.00, [0.0, 0.0, 0.0, 0.0]),
        (0.36, [0.0, 0.0, 0.0, 0.0]),
        (0.40, navy),                      # rim
        (0.43, [1.00, 0.42, 0.02, 1.0]),   # band 1
        (0.50, [1.00, 0.58, 0.08, 1.0]),
        (0.53, navy),                      # rim
        (0.57, [0.0, 0.0, 0.0, 0.0]),
        (0.68, [0.0, 0.0, 0.0, 0.0]),
        (0.71, navy),                      # rim
        (0.74, [1.00, 0.45, 0.03, 1.0]),   # band 2
        (0.80, [1.00, 0.50, 0.05, 1.0]),
        (0.83, navy),                      # rim
        (0.87, [0.0, 0.0, 0.0, 0.0]),
        (1.00, [0.0, 0.0, 0.0, 0.0])])})
    g.link(field, orange)
    comp1 = g.add("blend", {"blend_type": 0, "amount": 1.0})  # normal
    g.link(orange, comp1)            # s1: masked orange bands
    g.link(base, comp1, dst_slot=1)  # s2: blue base
    # Head-zone green tint: linear gradient along u, alpha 0 over most of the
    # tile. Box-cage tile orientation varies, so this is an experiment - if a
    # tile flips u the tint lands at the wrong end of that tile.
    head = g.add("gradient", {"rotate": 0.0, "repeat": 1.0, "gradient": grad([
        (0.00, [0.0, 0.0, 0.0, 0.0]),
        (0.70, [0.0, 0.0, 0.0, 0.0]),
        (0.85, [0.25, 0.75, 0.45, 0.45]),
        (1.00, [0.50, 0.85, 0.35, 0.65])])})
    comp2 = g.add("blend", {"blend_type": 0, "amount": 1.0})
    g.link(head, comp2)
    g.link(comp1, comp2, dst_slot=1)
    # Yellow spots: voronoi output 0 = intensity-scaled distance to the cell
    # center (0 at each center), randomness 1 scatters organically.
    spots_src = g.add("voronoi", {"scale_x": 12.0, "scale_y": 12.0,
                                  "randomness": 1.0, "intensity": 0.75})
    spots = g.add("colorize", {"gradient": grad([
        (0.00, [1.00, 0.90, 0.30, 1.0]),
        (0.10, [1.00, 0.85, 0.25, 1.0]),
        (0.18, [0.0, 0.0, 0.0, 0.0]),
        (1.00, [0.0, 0.0, 0.0, 0.0])])})
    g.link(spots_src, spots)
    comp3 = g.add("blend", {"blend_type": 0, "amount": 0.9})
    g.link(spots, comp3)
    g.link(comp2, comp3, dst_slot=1)
    wiggle = g.add("fbm", {"noise": 1, "scale_x": 1.6, "scale_y": 1.6,
                           "iterations": 2.0})
    warped = g.add("warp", {"amount": 0.06})
    g.link(comp3, warped)                # in
    g.link(wiggle, warped, dst_slot=1)   # d: displacement height
    out = g.add("output", {"name": name, "size": 1024})
    g.link(warped, out, dst_slot=2)      # rgba slot


M_RIM = [0.01, 0.09, 0.34, 1.0]  # dark navy rim around every orange band

MANDARIN_SKINS = {
    # v1: symmetric fbm, roughly balanced orange/blue, soft-light speckle.
    "v1": {
        "fbm": {"noise": 1, "scale_x": 3.0, "scale_y": 3.0, "iterations": 4.0},
        "stops": [
            (0.00, [0.02, 0.07, 0.30, 1.0]),   # deep blue shadow
            (0.18, [0.05, 0.28, 0.80, 1.0]),   # body blue
            (0.30, M_RIM),
            (0.34, [0.98, 0.44, 0.05, 1.0]),   # orange band 1
            (0.44, [1.00, 0.52, 0.10, 1.0]),
            (0.48, M_RIM),
            (0.56, [0.06, 0.34, 0.86, 1.0]),   # blue between the bands
            (0.62, M_RIM),
            (0.66, [1.00, 0.55, 0.10, 1.0]),   # orange band 2
            (0.74, [0.96, 0.44, 0.06, 1.0]),
            (0.78, M_RIM),
            (0.88, [0.05, 0.30, 0.82, 1.0]),
            (1.00, [0.25, 0.72, 0.62, 1.0])],  # green-cyan face highlight
        "speckle": {"params": {"noise": 1, "scale_x": 9.0, "scale_y": 9.0,
                               "iterations": 3.0},
                    "blend_type": 6, "amount": 0.18, "stops": None},
        "normal_amount": 0.25,
    },
    # v2: blue-DOMINANT (the real fish is a blue fish with orange lines, not
    # a 50/50 mix), anisotropic fbm stretches the maze along the body, and a
    # lighten-blended yellow speckle adds the face/back dot texture.
    "v2": {
        "fbm": {"noise": 1, "scale_x": 4.5, "scale_y": 3.0, "iterations": 4.0},
        "stops": [
            (0.00, [0.01, 0.05, 0.24, 1.0]),   # deep blue shadow
            (0.26, [0.05, 0.30, 0.84, 1.0]),   # body blue
            (0.40, M_RIM),
            (0.435, [1.00, 0.50, 0.08, 1.0]),  # orange band 1 (narrow)
            (0.50, [1.00, 0.54, 0.10, 1.0]),
            (0.535, M_RIM),
            (0.66, [0.06, 0.34, 0.88, 1.0]),   # wide blue between bands
            (0.72, M_RIM),
            (0.75, [0.98, 0.46, 0.06, 1.0]),   # orange band 2 (narrow)
            (0.80, [0.96, 0.44, 0.06, 1.0]),
            (0.83, M_RIM),
            (0.92, [0.05, 0.30, 0.82, 1.0]),
            (1.00, [0.20, 0.70, 0.60, 1.0])],  # green-cyan face highlight
        "speckle": {"params": {"noise": 1, "scale_x": 14.0, "scale_y": 14.0,
                               "iterations": 3.0},
                    "blend_type": 9, "amount": 0.35,  # lighten = max
                    "stops": [
                        (0.00, [0.0, 0.0, 0.0, 1.0]),
                        (0.72, [0.0, 0.0, 0.0, 1.0]),
                        (0.82, [0.85, 0.72, 0.20, 1.0]),
                        (1.00, [1.00, 0.88, 0.38, 1.0])]},
        "normal_amount": 0.20,
    },
    # v3: layered vibrant composition - see mandarin_v3_albedo.
    "v3": {
        "albedo_fn": mandarin_v3_albedo,
        "fbm": {"noise": 1, "scale_x": 4.5, "scale_y": 3.0, "iterations": 4.0},
        "normal_amount": 0.15,
    },
}


def build_mandarin_skin(c, cfg):
    """Two Graph_texture assets per version (see MANDARIN_SKINS). Dragonets
    are scaleless, so the normal is only a faint fbm slime relief."""
    sfx = cfg["suffix"]
    albedo_name = f"Mandarin{sfx} Maze Albedo"
    normal_name = f"Mandarin{sfx} Maze Normal"
    skin = MANDARIN_SKINS[cfg["skin"]]
    listing = c.call("get_graph_textures", {"scene_name": c.scene})
    existing = {entry.get("name") for entry in listing.get("graph_textures", [])}
    if albedo_name not in existing:
        g = c.texture_graph(albedo_name)
        if "albedo_fn" in skin:
            skin["albedo_fn"](g, albedo_name)
        else:
            mandarin_maze_albedo(g, albedo_name, skin["fbm"], skin["stops"],
                                 skin["speckle"])
    if normal_name not in existing:
        g = c.texture_graph(normal_name)
        height = g.add("fbm", skin["fbm"])
        normal = g.add("normal_map", {"amount": skin["normal_amount"], "size": 10})
        g.link(height, normal)
        out = g.add("output", {"name": normal_name, "size": 1024})
        g.link(normal, out, dst_slot=1)  # rgb slot
    return albedo_name, normal_name


def build_mandarin(c, cfg=None):
    cfg = cfg or MANDARIN_VERSIONS[0]
    sfx = cfg["suffix"]
    # White base-color factor: the maze albedo graph carries the pattern.
    body_mat = c.ensure_material(f"Mandarin{sfx} Body",
                                 base_color=[1.0, 1.0, 1.0],
                                 roughness=cfg.get("body_roughness", 0.28),
                                 metallic=0.02)
    c.mutate("edit_material", {"scene_name": c.scene, "material_name": body_mat,
                               "base_color": [1.0, 1.0, 1.0]})
    albedo_name, normal_name = build_mandarin_skin(c, cfg)
    c.bind_material_texture(body_mat, albedo_name, slot="base_color", wrap="repeat")
    c.bind_material_texture(body_mat, normal_name, slot="normal", wrap="repeat")
    # ------------------------------------------------------------- body graph
    # Reuse an existing body-graph asset (rebuilds after a fin/pose-only fix:
    # graph assets cannot be created twice, and the body is unchanged).
    size = cfg["size"]
    graph_listing = c.call("get_graph_meshes", {"scene_name": c.scene})
    existing_graphs = {entry.get("name")
                       for entry in graph_listing.get("graph_meshes", [])}
    if f"Mandarin{sfx} Body Graph" in existing_graphs:
        body = c.bind_node_mesh(f"Mandarin{sfx}", f"Mandarin{sfx} Body Graph")
        pos = cfg["pos"]
        c.move_node(body, translation=pos)
        c.settle()
        return _mandarin_attachments(c, cfg, body)
    g = c.geometry_graph(f"Mandarin{sfx} Body Graph")
    box = g.add("box", {"size": size, "subdivisions": M_SUBDIVISIONS, "power": 1.0})
    subdivide_pre = g.add("subdivide", {"mode": 0, "iterations": 1})
    lattice = g.add("lattice", {
        "auto_fit": False,
        "cage_min": [-0.5 * v for v in size],
        "cage_max": [0.5 * v for v in size],
        "divisions": cfg["divisions"],
        "interpolation": 1,  # bezier FFD
        "show_cage": False,
        "offsets": lattice_offsets(size, cfg["divisions"], cfg["squeeze_y"],
                                   cfg["squeeze_z"], cfg["shift_y"],
                                   cfg["shift_x"], cfg["edge_taper"]),
    })
    subdivide_post = g.add("subdivide", {"mode": 0,
                                         "iterations": cfg["post_subdiv"]})
    out = g.add("output", {"material": body_mat})
    g.chain([box, subdivide_pre, lattice, subdivide_post, out])
    c.call("get_geometry_graph")  # evaluation barrier

    body = c.bind_node_mesh(f"Mandarin{sfx}", f"Mandarin{sfx} Body Graph")
    c.move_node(body, translation=cfg["pos"])
    c.settle()
    return _mandarin_attachments(c, cfg, body)


def _mandarin_attachments(c, cfg, body):
    """Everything attached to the body: tail fan (+rim), dorsals, blade fins,
    eyes. Separate from build_mandarin so a rebuild that only changes
    attachments can reuse the existing body-graph asset."""
    sfx = cfg["suffix"]
    pos = cfg["pos"]
    # Fins: blue membranes; tail: orange with its blue rim as GEOMETRY (fins
    # have no texcoords, so rims cannot come from textures).
    fin_mat = c.ensure_material(f"Mandarin{sfx} Fin",
                                base_color=cfg.get("fin_color", [0.10, 0.32, 0.92]),
                                roughness=0.45, metallic=0.0,
                                blending_mode="alpha_blend", opacity=0.92)
    tail_mat = c.ensure_material(f"Mandarin{sfx} Tail",
                                 base_color=cfg.get("tail_color", [0.96, 0.45, 0.08]),
                                 roughness=0.5, metallic=0.0,
                                 blending_mode="alpha_blend", opacity=0.95)
    eye_mat = c.ensure_material(f"Mandarin{sfx} Eye",
                                base_color=[0.30, 0.06, 0.04],
                                roughness=0.25, metallic=0.0)
    body_id = c.node_by_name(body)["id"]

    aabb_min, aabb_max = c.subtree_world_aabb(body)
    tail_x = aabb_min[0]
    nose_x = aabb_max[0]
    top_y = aabb_max[1]
    mid_y = (aabb_min[1] + aabb_max[1]) * 0.5
    z0 = pos[2]

    # -------------------------------------------------------------- tail fan
    # Rounded fan, NOT forked: pinch the attach column, and bow the middle of
    # the trailing edge rearward so Catmull-Clark rounds it convex (line-art
    # silhouette). Optional rim: a slightly larger, thinner BLUE fan sitting
    # in the same plane - it pokes out past the orange fan's outline all
    # around, reading as the blue edge of the refs.
    tail = cfg["tail"]
    tail_w, tail_h, tail_t = tail["w"], tail["h"], tail["t"]
    tail_center = [tail_x - tail_w / 2.0 + tail["attach"], mid_y, z0]

    def fan_deform(node_name, height, bow):
        fan = []
        for j in range(3):
            for k in range(2):
                y = -1.0 + j  # -1, 0, +1 row factor
                fan.append([2, j, k, 0.0, -y * 0.80 * (height / 2.0), 0.0])
                fan.append([1, j, k, 0.0, -y * 0.30 * (height / 2.0), 0.0])
                if j == 1:
                    fan.append([0, j, k, -bow, 0.0, 0.0])  # convex edge
        c.lattice_deform(node_name, fan, divisions=[2, 2, 1],
                         interpolation="bezier", wait=True)
        for _ in range(2):  # one level per call
            c.mutate("catmull_clark", {"scene_name": c.scene,
                                       "node_name": node_name})

    c.shape("box", f"Mandarin{sfx} Tail Fin", tail_center,
            size=[tail_w, tail_h, tail_t], steps=[6, 6, 1],
            material_name=tail_mat, reuse=False, parent_node_id=body_id,
            motion_mode="none")
    fan_deform(f"Mandarin{sfx} Tail Fin", tail_h, 0.10)
    if tail.get("rim"):
        rim_w, rim_h = tail_w + 0.06, tail_h + 0.10
        rim_center = [tail_center[0] - 0.025, mid_y, z0]
        c.shape("box", f"Mandarin{sfx} Tail Rim", rim_center,
                size=[rim_w, rim_h, 0.014], steps=[6, 6, 1],
                material_name=fin_mat, reuse=False, parent_node_id=body_id,
                motion_mode="none")
        fan_deform(f"Mandarin{sfx} Tail Rim", rim_h, 0.12)
    c.settle()

    # ------------------------------------------------- dorsal sail + 2nd fin
    # Probe the actual back line at both fin bases.
    back = c.closest_points(
        [[pos[0] + 0.10, top_y + 0.4, z0], [pos[0] + 0.25, top_y + 0.4, z0],
         [pos[0] - 0.35, top_y + 0.4, z0], [pos[0] - 0.50, top_y + 0.4, z0]],
        node_name=body)
    sail_y = min(p.get("position", [0.0, top_y, z0])[1] for p in back[:2])
    rear_y = min(p.get("position", [0.0, top_y, z0])[1] for p in back[2:])
    # Tall raked first-dorsal sail just behind the head.
    sail = cfg["sail"]
    sail_w, sail_h, sail_t = sail["w"], sail["h"], sail["t"]
    sail_center = [pos[0] + 0.16, sail_y + sail_h / 2.0 - 0.08, z0]
    c.shape("box", f"Mandarin{sfx} Dorsal Sail", sail_center,
            size=[sail_w, sail_h, sail_t], steps=[4, 4, 1],
            material_name=fin_mat, reuse=False, parent_node_id=body_id,
            motion_mode="none")
    rake = []
    for k in range(2):
        for i in range(3):
            x = -1.0 + i
            rake.append([i, 2, k, -sail["rake"] + x * -0.03,
                         -0.10 if i == 0 else 0.0, 0.0])
        rake.append([2, 1, k, -0.05, 0.0, 0.0])
    c.lattice_deform(f"Mandarin{sfx} Dorsal Sail", rake, divisions=[2, 2, 1],
                     interpolation="bezier", wait=True)
    for _ in range(2):
        c.mutate("catmull_clark", {"scene_name": c.scene,
                                   "node_name": f"Mandarin{sfx} Dorsal Sail"})
    # Low second dorsal near the tail base.
    d2 = cfg["dorsal2"]
    d2_center = [pos[0] - 0.42, rear_y + d2["h"] / 2.0 - 0.05, z0]
    c.shape("box", f"Mandarin{sfx} Dorsal Fin", d2_center,
            size=[d2["w"], d2["h"], d2["t"]], steps=[4, 3, 1],
            material_name=fin_mat, reuse=False, parent_node_id=body_id,
            motion_mode="none")
    rake2 = []
    for k in range(2):
        for i in range(3):
            rake2.append([i, 2, k, -0.10, 0.0, 0.0])
    c.lattice_deform(f"Mandarin{sfx} Dorsal Fin", rake2, divisions=[2, 2, 1],
                     interpolation="bezier", wait=True)
    for _ in range(2):
        c.mutate("catmull_clark", {"scene_name": c.scene,
                                   "node_name": f"Mandarin{sfx} Dorsal Fin"})
    c.settle()

    # ------------------------------------------------ fan fins: sweep blades
    # Broader, shorter blade than fish one - mandarin fans are round paddles.
    blade_profile = [[0.0, -0.05], [0.5, -0.02], [1.0, 0.0],
                     [0.5, 0.02], [0.0, 0.05], [-0.5, 0.02], [-1.0, 0.0],
                     [-0.5, -0.02]]
    blade_profile = [[0.16 * px, 0.30 * py] for px, py in blade_profile]

    def blade(name, position, yaw_deg, pitch_deg, roll_deg, scale):
        q = quat_mul(
            axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(yaw_deg)),
            quat_mul(
                axis_angle_quaternion([0.0, 0.0, 1.0], math.radians(pitch_deg)),
                axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(roll_deg))))
        c.shape("sweep", name, position,
                profile=blade_profile,
                spine=[[0.0, 0.0, 0.0], [0.10, -0.02, 0.0],
                       [0.20, -0.05, 0.0], [0.28, -0.10, 0.0]],
                spine_steps=10,
                taper=[[0.0, 0.8], [0.45, 1.0], [1.0, 0.15]],
                material_name=fin_mat, rotation_xyzw=q, scale=scale,
                parent_node_id=body_id, motion_mode="none")

    # Trailing filament on the sail's rear top corner (side view + line art).
    if sail.get("filament"):
        fil_p = [sail_center[0] - sail_w * 0.30,
                 sail_center[1] + sail_h * 0.42, z0]
        blade(f"Mandarin{sfx} Dorsal Filament", fil_p,
              yaw_deg=180.0, pitch_deg=-32.0, roll_deg=0.0,
              scale=[0.5, 0.35, 0.35])

    # Pectoral fans on the flanks behind the head.
    flank = c.closest_points(
        [[pos[0] + 0.30, mid_y + 0.03, z0 + 0.5],
         [pos[0] + 0.30, mid_y + 0.03, z0 - 0.5]],
        node_name=body)
    ps = cfg["pectoral_scale"]
    for side, probe in zip((1.0, -1.0), flank):
        p = probe.get("position", [pos[0] + 0.30, mid_y, z0 + side * 0.25])
        p = v_add(p, [0.0, 0.0, side * 0.02])
        blade(f"Mandarin{sfx} Pectoral Fin {'L' if side > 0 else 'R'}", p,
              yaw_deg=180.0 + side * 35.0, pitch_deg=12.0, roll_deg=side * 55.0,
              scale=[ps, ps, ps])
    # Oversized pelvic perching fans below the head + anal fin at the rear.
    belly = c.closest_points(
        [[pos[0] + 0.32, aabb_min[1] + 0.06, z0 + 0.12],
         [pos[0] + 0.32, aabb_min[1] + 0.06, z0 - 0.12],
         [pos[0] - 0.40, aabb_min[1] + 0.06, z0]],
        node_name=body)
    vs = cfg["pelvic_scale"]
    for side, probe in zip((1.0, -1.0), belly[:2]):
        p = probe.get("position", [pos[0] + 0.32, mid_y - 0.2, z0 + side * 0.1])
        p = v_add(p, [0.0, -0.03, side * 0.02])
        blade(f"Mandarin{sfx} Pelvic Fin {'L' if side > 0 else 'R'}", p,
              yaw_deg=180.0 + side * 20.0, pitch_deg=cfg["pelvic_pitch"],
              roll_deg=side * cfg["pelvic_roll"], scale=[vs, vs, vs])
    anal_p = v_add(belly[2].get("position", [pos[0] - 0.40, mid_y - 0.2, z0]),
                   [0.0, -0.03, 0.0])
    a_s = cfg["anal_scale"]
    blade(f"Mandarin{sfx} Anal Fin", anal_p, yaw_deg=180.0, pitch_deg=64.0,
          roll_deg=0.0, scale=[a_s, a_s, a_s])

    # ------------------------------------------------------------------ eyes
    # Frog-like eyes sit on TOP of the head (front-view refs), protruding -
    # probe the upper head surface and push the sphere slightly OUT along the
    # normal instead of embedding it. "ring" style: orange-red ring sphere
    # with a smaller near-black pupil pushed further out.
    eyes = c.closest_points(
        [[nose_x - 0.26, top_y + 0.4, z0 + 0.3],
         [nose_x - 0.26, top_y + 0.4, z0 - 0.3]],
        node_name=body)
    if cfg["eye"] == "ring":
        ring_mat = c.ensure_material(f"Mandarin{sfx} Eye Ring",
                                     base_color=cfg.get("eye_ring_color",
                                                        [0.80, 0.28, 0.10]),
                                     roughness=0.35, metallic=0.0)
        pupil_mat = c.ensure_material(f"Mandarin{sfx} Eye Pupil",
                                      base_color=[0.02, 0.02, 0.03],
                                      roughness=0.15, metallic=0.0)
    for side, probe in zip((1.0, -1.0), eyes):
        hit = probe.get("position")
        normal = probe.get("normal", [0.0, 0.7, side * 0.7])
        tag = "L" if side > 0 else "R"
        if hit is None:
            hit = [nose_x - 0.26, top_y - 0.05, z0 + side * 0.12]
            normal = [0.0, 0.7, side * 0.7]
        if cfg["eye"] == "ring":
            ring_p = v_add(hit, [0.014 * n for n in normal])
            c.shape("uv_sphere", f"Mandarin{sfx} Eye {tag}", ring_p,
                    radius=0.048, slice_count=20, stack_count=12,
                    material_name=ring_mat, parent_node_id=body_id,
                    motion_mode="none")
            pupil_p = v_add(hit, [0.040 * n for n in normal])
            c.shape("uv_sphere", f"Mandarin{sfx} Pupil {tag}", pupil_p,
                    radius=0.026, slice_count=16, stack_count=10,
                    material_name=pupil_mat, parent_node_id=body_id,
                    motion_mode="none")
        else:
            p = v_add(hit, [0.012 * n for n in normal])
            c.shape("uv_sphere", f"Mandarin{sfx} Eye {tag}", p,
                    radius=0.042, slice_count=20, stack_count=12,
                    material_name=eye_mat, parent_node_id=body_id,
                    motion_mode="none")

    c.settle()
    return body


def main():
    args = common.standard_args("creation 18: smooth fish + mandarin dragonet")
    if common.reframe(args, TITLE, BASE, SHOTS):
        return
    c = common.Creation(TITLE, port=args.port, pause_s=args.pause,
                        editor_exe=args.editor_exe,
                        reuse=args.reuse or bool(args.only),
                        keep_scenes=args.keep_scenes or bool(args.only))
    builders = {"Fish": build_fish}
    for version_cfg in MANDARIN_VERSIONS:
        name = f"Mandarin{version_cfg['suffix']}"
        builders[name] = (lambda cc, cfg=version_cfg: build_mandarin(cc, cfg))
    with common.fail_soft(c, BASE, failed_glb=None):
        if args.only:
            builder = builders.get(args.only)
            if builder is None:
                raise SystemExit(
                    f"--only must be one of {sorted(builders)}, got '{args.only}'")
            c.attach_scene()
            # First build of a NEW version has nothing to delete.
            if c.node_by_name(args.only) is not None:
                c.delete_nodes(names=[args.only])
            builder(c)
        else:
            c.new_scene()
            build_scene_setup(c)
            build_fish(c)
            for version_cfg in MANDARIN_VERSIONS:
                build_mandarin(c, version_cfg)
        common.hierarchy_report(c, "fish hierarchy")
        c.screenshot_views(BASE, SHOTS)
        if not args.no_save:
            c.save(SAVE_GLB)
    print("fish creation complete.")


if __name__ == "__main__":
    main()
