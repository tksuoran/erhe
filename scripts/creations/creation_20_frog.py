#!/usr/bin/env python3
"""Creation 20: Frog - a sitting frog as an EDITABLE GEOMETRY GRAPH.

A green frog sitting on a lily pad in a dark pond, built the same way as
the dolphin (creation 19): every body part is a coarse box cage ->
subdivide -> lattice FFD chain, appendages are posed by Transform nodes
(quaternion rotation mode), then Join -> ONE Boolean union -> final
subdivide -> smooth_normals -> Output. Drag any lattice point in the
node editor and the whole frog re-evaluates.

Detail pass (v2, ~25 graph parts instead of 11):

  Body:        9-station spindle with arched back, throat bulge, blunt snout
  Eyes (x2):   CC'd cube domes; gold iris + slit pupil are scene spheres
  Hind legs:   articulated thigh (hip->knee) + shank (knee->ankle) + webbed
               foot + 3 toes with bulb tips, per side
  Front legs:  foreleg aligned shoulder->wrist + foot + 3 toes, per side
  All parts -> Join -> Boolean(union) -> subdivide -> smooth_normals -> Out

Limbs are aligned to authored anchor SEGMENTS (hip/knee/ankle,
shoulder/wrist) with align-+X quaternions - see the skill's
geometry_graph_sculpt.md exact-landing recipe. Nostrils and tympanum
discs are probed onto the actual evaluated surface with
`closest_points`. The skin is a procedural mottle (fbm -> green
colorize + wart speckle, soft light) bound to the skin material's
base_color slot with wrap=repeat over the graph body's rough-but-
coherent UV tiles. Lily pads get their signature radial slit via a CSG
box difference (the scaled pooled instance goes private, documented
behavior), and a dragonfly hovers in the frog's gaze.

NOTE --only is not supported: the frog is one geometry graph and graph
assets cannot be recreated under the same name. Iterate with a full
`--reuse` rebuild or live via geometry_graph_set_parameter / the node
editor.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import (  # noqa: E402
    Creation, standard_args, reframe, fail_soft,
    axis_angle_quaternion, align_y_quaternion, quat_mul, quat_rotate,
    v_add, v_scale, v_sub, v_length,
)


def v_norm(v):
    return v_scale(v, 1.0 / v_length(v))


def align_x_quaternion(direction):
    """Quaternion rotating local +X onto the given unit direction."""
    d = v_norm(direction)
    dot = max(-1.0, min(1.0, d[0]))
    axis = [0.0, -d[2], d[1]]  # cross(+X, d)
    length = math.sqrt(axis[1] * axis[1] + axis[2] * axis[2])
    if length < 1.0e-6:
        axis = [0.0, 0.0, 1.0]
    else:
        axis = [0.0, axis[1] / length, axis[2] / length]
    return axis_angle_quaternion(axis, math.acos(dot))


def grad(stops, interpolation=1):
    return {"interpolation": interpolation,
            "stops": [{"pos": p, "color": list(c)} for p, c in stops]}


SHOTS = [
    ("",       [1.9, 1.1, 2.3], [0.0, 0.35, 0.0]),
    ("_side",  [0.1, 0.7, 2.6], [0.0, 0.35, 0.0]),
    ("_front", [2.5, 0.9, 0.3], [0.0, 0.35, 0.0]),
    ("_top",   [0.5, 3.2, 0.9], [0.0, 0.3, 0.0]),
    ("_close", [1.25, 0.85, 1.05], [0.1, 0.45, 0.0]),
    ("_leg",   [-1.15, 0.75, 1.55], [-0.28, 0.28, 0.33]),
]

BASE = "logs/creations/frog"
GLB = "res/editor/scenes/creations/frog.glb"
GRAPH_NAME = "Frog Graph"
SKIN_GRAPH = "Frog Skin Mottle"


# ------------------------------------------------------------- lattice tables
# Flat float array in lattice_offset_index order:
# index = i + (dx+1) * (j + (dy+1) * k), 3 floats per control point.

def offset_index(divisions, i, j, k):
    return i + (divisions[0] + 1) * (j + (divisions[1] + 1) * k)


def make_flat(divisions):
    count = (divisions[0] + 1) * (divisions[1] + 1) * (divisions[2] + 1)
    return [0.0] * (count * 3)


def put(flat, divisions, i, j, k, dx, dy, dz):
    base = offset_index(divisions, i, j, k) * 3
    flat[base + 0] = dx
    flat[base + 1] = dy
    flat[base + 2] = dz


def spindle_offsets(divisions, half, keep, shift_x, shift_y,
                    top_taper=0.0, belly_taper=0.0, belly_drop=None):
    """Body along X: per-station (i) squeeze of Y/Z toward the centerline
    (keep[i] = surviving fraction), axial cap shifts and dorsoventral
    shifts; top/belly rows get extra beam (Z) squeeze for the oval
    cross-section; belly_drop[i] pushes the belly row down (throat/
    dewlap bulge)."""
    di, dj, dk = divisions
    flat = make_flat(divisions)
    for i in range(di + 1):
        squeeze = 1.0 - keep[i]
        drop = belly_drop[i] if belly_drop else 0.0
        for j in range(dj + 1):
            y = (2.0 * j / dj - 1.0) * half[1]
            extra = top_taper if j == dj else (belly_taper if j == 0 else 0.0)
            sz = squeeze + (1.0 - squeeze) * extra
            dy_extra = -drop if j == 0 else 0.0
            for k in range(dk + 1):
                z = (2.0 * k / dk - 1.0) * half[2]
                put(flat, divisions, i, j, k,
                    shift_x[i], -y * squeeze + shift_y[i] + dy_extra, -z * sz)
    return flat


def paddle_offsets(divisions, half, sweep, keep_chord, keep_thick):
    """One-sided flat paddle (chord X, span +Z root -> tip, thin Y): per
    span station a backward sweep + chord/thickness squeeze. The webbed
    foot widens mid-span (keep_chord peaks at 1.0) then rounds off."""
    di, dj, dk = divisions
    flat = make_flat(divisions)
    for k in range(dk + 1):
        sx = 1.0 - keep_chord[k]
        sy = 1.0 - keep_thick[k]
        for i in range(di + 1):
            x = (2.0 * i / di - 1.0) * half[0]
            for j in range(dj + 1):
                y = (2.0 * j / dj - 1.0) * half[1]
                put(flat, divisions, i, j, k, -x * sx + sweep[k], -y * sy, 0.0)
    return flat


# ---------------------------------------------------------------------- frog

def part_chain(g, size, subdivisions, lattice_divisions, offsets, iterations=1):
    """box -> subdivide -> lattice; returns the lattice node id. Explicit
    cage at the box bounds: subdivision shrinks the mesh inside it,
    auto-fit would rescale the sculpt."""
    box = g.add("box", {"size": size, "subdivisions": subdivisions,
                        "power": 1.0})
    subdivide = g.add("subdivide", {"mode": 0, "iterations": iterations})
    lattice = g.add("lattice", {
        "auto_fit": False,
        "cage_min": [-0.5 * v for v in size],
        "cage_max": [0.5 * v for v in size],
        "divisions": lattice_divisions,
        "interpolation": 1,  # bezier FFD - globally smooth
        "show_cage": False,
        "offsets": offsets,
    })
    g.chain([box, subdivide, lattice])
    return lattice


def segment_part(g, tools, thickness, keep, anchor_a, anchor_b,
                 over_a=0.06, over_b=0.05):
    """Spindle limb segment aligned to the anchor_a -> anchor_b segment
    (align-+X) and EXTENDED past both anchors by over_a/over_b: the
    squeezed, CC-shrunk caps must stay buried inside the joint masses -
    segments that merely touch their anchors tip-to-tip read as
    disconnected. Appends its Transform to tools."""
    direction = v_norm(v_sub(anchor_b, anchor_a))
    a = v_add(anchor_a, v_scale(direction, -over_a))
    b = v_add(anchor_b, v_scale(direction, over_b))
    size = [v_length(v_sub(b, a)), thickness[0], thickness[1]]
    stations = len(keep) - 1
    part = part_chain(
        g, size, [1, 0, 0], [stations, 1, 1],
        spindle_offsets(
            [stations, 1, 1], [0.5 * v for v in size],
            keep=keep,
            shift_x=[0.02] + [0.0] * (stations - 1) + [-0.02],
            shift_y=[0.0] * (stations + 1)))
    pose = g.add("transform", {
        "translation": v_scale(v_add(a, b), 0.5),
        "rotation_mode": 1,
        "rotation_quaternion": align_x_quaternion(direction),
    })
    g.link(part, pose)
    tools.append(pose)
    return pose


def joint_ball(g, tools, center, size):
    """CC'd cube sphere welding two limb segments at a joint."""
    ball_box = g.add("box", {"size": [size, size, size],
                             "subdivisions": [0, 0, 0], "power": 1.0})
    ball_cc = g.add("subdivide", {"mode": 0, "iterations": 2})
    ball_pose = g.add("transform", {"translation": list(center)})
    g.chain([ball_box, ball_cc, ball_pose])
    tools.append(ball_pose)


def toe(g, tools, root, direction, length, thickness):
    """Slim toe spindle with a bulbed tip, root buried in the foot/paddle."""
    d = v_norm(direction)
    part = part_chain(
        g, [length, thickness, thickness], [1, 0, 0], [3, 1, 1],
        spindle_offsets(
            [3, 1, 1], [0.5 * length, 0.5 * thickness, 0.5 * thickness],
            keep=[0.75, 1.0, 0.72, 0.95],  # dip then bulb = toe pad tip
            shift_x=[0.02, 0.0, 0.0, -0.01],
            shift_y=[0.0, 0.0, 0.0, 0.0]))
    center = v_add(root, v_scale(d, 0.5 * length - 0.02))
    pose = g.add("transform", {
        "translation": center,
        "rotation_mode": 1,
        "rotation_quaternion": align_x_quaternion(d),
    })
    g.link(part, pose)
    tools.append(pose)


def yaw_about_y(direction, degrees):
    return quat_rotate(
        axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(degrees)),
        direction)


def build_frog(c, skin):
    """Build the frog geometry graph, bind it to a scene node, sit it on
    the lily pad. Returns the bound node's name."""
    g = c.geometry_graph(GRAPH_NAME)

    # Body (torso + head merged): 0.95 m along X, +X = snout. Nine
    # stations i=0 (rump) .. 8 (snout): raised rump, arched back
    # sloping over the shoulders, wide blunt snout; the belly row drops
    # under the head for the throat.
    body_size = [0.95, 0.44, 0.66]
    body = part_chain(
        g, body_size, [3, 1, 1], [8, 3, 3],
        iterations=2,
        offsets=spindle_offsets(
            [8, 3, 3], [0.5 * v for v in body_size],
            keep=[0.40, 0.78, 0.97, 1.00, 0.97, 0.93, 0.87, 0.74, 0.50],
            shift_x=[0.11, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.10],
            shift_y=[0.03, 0.09, 0.12, 0.11, 0.08, 0.06, 0.05, 0.05, 0.05],
            belly_drop=[0.0, 0.0, 0.0, 0.0, 0.0, 0.01, 0.03, 0.05, 0.02],
            top_taper=0.22, belly_taper=0.05))

    tools = []

    # Eye bulges: CC'd cubes on the skull corners (iris/pupil spheres are
    # scene nodes placed later - they keep their own materials).
    for side in (1.0, -1.0):
        eye_box = g.add("box", {"size": [0.21, 0.19, 0.19],
                                "subdivisions": [0, 0, 0], "power": 1.0})
        eye_cc = g.add("subdivide", {"mode": 0, "iterations": 2})
        eye_pose = g.add("transform", {
            "translation": [0.24, 0.24, 0.16 * side]})
        g.chain([eye_box, eye_cc, eye_pose])
        tools.append(eye_pose)

    # Hind legs: articulated fold. Hip high on the flank, knee swept
    # back-out-up, ankle tucked forward-down - thigh and shank are
    # spindles aligned to the hip->knee and knee->ankle segments.
    for side in (1.0, -1.0):
        hip = [-0.16, 0.00, 0.20 * side]
        knee = [-0.44, 0.06, 0.335 * side]
        ankle = [-0.28, -0.185, 0.38 * side]
        segment_part(g, tools, [0.27, 0.22],
                     [0.60, 1.0, 0.95, 0.65], hip, knee,
                     over_a=0.10, over_b=0.05)
        joint_ball(g, tools, knee, 0.15)
        segment_part(g, tools, [0.14, 0.13],
                     [0.72, 1.0, 0.90, 0.62], knee, ankle,
                     over_a=0.08, over_b=0.05)
        joint_ball(g, tools, ankle, 0.11)

        # Webbed foot: paddle splayed forward-outward, root buried in
        # the ankle ball.
        foot_size = [0.24, 0.05, 0.40]
        foot_half = [0.5 * v for v in foot_size]
        foot = part_chain(
            g, foot_size, [1, 0, 2], [2, 1, 3],
            paddle_offsets(
                [2, 1, 3], foot_half,
                sweep=[0.0, -0.02, -0.05, -0.10],
                keep_chord=[0.55, 0.80, 1.0, 0.62],
                keep_thick=[1.0, 0.85, 0.65, 0.45]))
        theta = math.atan2(0.88, 0.48 * side)
        rotation = axis_angle_quaternion([0.0, 1.0, 0.0], theta)
        tip_dir = quat_rotate(rotation, [0.0, 0.0, 1.0])
        center = v_add(ankle, v_scale(tip_dir, foot_half[2] - 0.08))
        foot_pose = g.add("transform", {
            "translation": center,
            "rotation_mode": 1,
            "rotation_quaternion": rotation,
        })
        g.link(foot, foot_pose)
        tools.append(foot_pose)

        # Three toes rooted INSIDE the paddle (mid-plane, near mid-span)
        # so the union welds them, poking past the paddle tip with a
        # slight droop onto the pad.
        for fan_deg in (-20.0, 0.0, 20.0):
            fan_dir = yaw_about_y(tip_dir, fan_deg)
            root = v_add([ankle[0], -0.19, ankle[2]],
                         v_scale([fan_dir[0], 0.0, fan_dir[2]], 0.13))
            toe(g, tools, root, [fan_dir[0], -0.05, fan_dir[2]],
                0.26, 0.05)

    # Front legs: foreleg aligned to the shoulder->wrist segment so arm,
    # wrist, foot and toes land exactly (exact-landing recipe).
    ffoot_size = [0.14, 0.04, 0.22]
    ffoot_half = [0.5 * v for v in ffoot_size]
    for side in (1.0, -1.0):
        shoulder = [0.26, 0.02, 0.17 * side]
        wrist = [0.40, -0.205, 0.235 * side]
        segment_part(g, tools, [0.11, 0.11],
                     [0.90, 1.0, 0.90, 0.75], shoulder, wrist,
                     over_a=0.08, over_b=0.03)
        joint_ball(g, tools, wrist, 0.09)

        ffoot = part_chain(
            g, ffoot_size, [1, 0, 1], [2, 1, 2],
            paddle_offsets(
                [2, 1, 2], ffoot_half,
                sweep=[0.0, -0.02, -0.05],
                keep_chord=[0.62, 1.0, 0.60],
                keep_thick=[1.0, 0.75, 0.50]))
        theta = math.atan2(0.94, 0.22 * side)
        foot_rotation = axis_angle_quaternion([0.0, 1.0, 0.0], theta)
        tip_dir = quat_rotate(foot_rotation, [0.0, 0.0, 1.0])
        center = v_add(wrist, v_scale(tip_dir, ffoot_half[2] - 0.05))
        ffoot_pose = g.add("transform", {
            "translation": center,
            "rotation_mode": 1,
            "rotation_quaternion": foot_rotation,
        })
        g.link(ffoot, ffoot_pose)
        tools.append(ffoot_pose)

        # Three small front toes rooted inside the paddle, splayed on
        # the pad.
        for fan_deg in (-24.0, 0.0, 24.0):
            fan_dir = yaw_about_y(tip_dir, fan_deg)
            root = v_add([wrist[0], -0.205, wrist[2]],
                         v_scale([fan_dir[0], 0.0, fan_dir[2]], 0.05))
            toe(g, tools, root, [fan_dir[0], -0.02, fan_dir[2]],
                0.15, 0.038)

    # Join merges the posed parts into ONE tool solid, a single boolean
    # union welds them into the body, the extra subdivide fairs the
    # union seams and smooth_normals fixes the shading.
    join = g.add("join")
    for tool in tools:
        g.link(tool, join)
    boolean = g.add("boolean", {"operation": 0})  # union
    g.link(body, boolean, dst_slot=0)             # A: body
    g.link(join, boolean, dst_slot=1)             # B: merged parts
    final = g.add("subdivide", {"mode": 0, "iterations": 1})
    normals = g.add("smooth_normals")
    out = g.add("output", {"material": skin})
    g.chain([boolean, final, normals, out])
    c.call("get_geometry_graph")  # evaluation barrier

    # Sit on the lily pad: pad top is at y ~= 0.11 at the center, a bit
    # lower where the feet land; the feet/wrists sit at local y -0.205.
    frog = c.bind_node_mesh("Frog", GRAPH_NAME)
    c.set_node_transform(frog, translation=[0.0, 0.32, 0.0])
    return frog


def build_skin_graph(c):
    """Procedural mottled frog skin: fbm -> green colorize base with a
    wart-speckle noise layer soft-lit on top. Bound to the skin
    material's base_color slot (wrap=repeat - graph-body UV tiles run
    past [0,1])."""
    g = c.texture_graph(SKIN_GRAPH)
    # Low-frequency fbm only: a high-frequency wart-noise layer aliased
    # into dirty dark speckle where the unioned body's UV tiles compress.
    field = g.add("fbm", {"noise": 1, "scale_x": 3.5, "scale_y": 3.5,
                          "iterations": 4.0})
    base = g.add("colorize", {"gradient": grad([
        (0.00, [0.11, 0.28, 0.08, 1.0]),   # deep moss shadow
        (0.35, [0.16, 0.38, 0.11, 1.0]),
        (0.65, [0.23, 0.49, 0.15, 1.0]),   # body green
        (0.85, [0.33, 0.57, 0.19, 1.0]),
        (1.00, [0.45, 0.63, 0.24, 1.0])])})  # highlight green
    g.link(field, base)
    out = g.add("output", {"name": SKIN_GRAPH, "size": 1024})
    g.link(base, out, dst_slot=2)    # rgba slot


def decorate_frog(c, frog, materials):
    """Scene-node details on the evaluated body: two-tone eyes, probed
    nostrils and tympanum discs (single-material graph output - anything
    needing its own material lives outside the union)."""
    iris_gold, eye_black, tympanum_mat = materials
    for side in (1.0, -1.0):
        # Eye: gold iris dome + horizontal slit pupil layered along the
        # gaze direction on the skin bulge (bulge center local
        # [0.24, 0.24, +-0.16], frog node at y 0.32).
        eye_center = [0.24, 0.56, 0.16 * side]
        gaze = v_norm([0.72, 0.30, 0.30 * side])
        c.shape("uv_sphere", f"Iris {'L' if side > 0 else 'R'}",
                v_add(eye_center, v_scale(gaze, 0.052)),
                radius=0.050, slice_count=20, stack_count=12,
                material_name=iris_gold, motion_mode="none")
        c.shape("uv_sphere", f"Pupil {'L' if side > 0 else 'R'}",
                v_add(eye_center, v_scale(gaze, 0.085)),
                radius=1.0, slice_count=16, stack_count=10,
                material_name=eye_black, motion_mode="none",
                scale=[0.034, 0.017, 0.044])

    # Nostrils + tympanum probed onto the actual surface (guessed
    # offsets miss - the subdivided skin sits inside the cage).
    probes = c.closest_points(
        [[0.56, 0.44, 0.055], [0.56, 0.44, -0.055],    # nostrils
         [0.13, 0.46, 0.45], [0.13, 0.46, -0.45]],     # tympani
        node_name=frog)
    for index, side in enumerate((1.0, -1.0)):
        hit = probes[index].get("position", [0.50, 0.42, 0.05 * side])
        c.shape("uv_sphere", f"Nostril {'L' if side > 0 else 'R'}", hit,
                radius=0.012, slice_count=10, stack_count=6,
                material_name=eye_black, motion_mode="none")
    for index, side in enumerate((1.0, -1.0)):
        probe = probes[2 + index]
        hit = probe.get("position", [0.13, 0.42, 0.30 * side])
        normal = probe.get("normal", [0.0, 0.0, side])
        c.shape("uv_sphere", f"Tympanum {'L' if side > 0 else 'R'}",
                v_add(hit, v_scale(normal, 0.004)),
                radius=1.0, slice_count=14, stack_count=8,
                material_name=tympanum_mat, motion_mode="none",
                rotation_xyzw=align_y_quaternion(normal),
                scale=[0.055, 0.012, 0.055])


def build_dragonfly(c, position):
    """A small dragonfly hovering in the frog's gaze: slim two-segment
    body, dark head, two pairs of translucent swept wings."""
    body_mat = c.ensure_material("dragonfly", base_color=[0.05, 0.42, 0.48],
                                 roughness=0.3, metallic=0.1)
    head_mat = c.ensure_material("dragonfly head", base_color=[0.03, 0.03, 0.04],
                                 roughness=0.25, metallic=0.0)
    wing_mat = c.ensure_material("dragonfly wing", base_color=[0.9, 0.95, 1.0],
                                 roughness=0.15, metallic=0.0,
                                 blending_mode="alpha_blend", opacity=0.45)
    root = c.group("Dragonfly", position)
    c.shape("uv_sphere", "Dragonfly Thorax", position,
            radius=1.0, slice_count=12, stack_count=8,
            material_name=body_mat, motion_mode="none",
            scale=[0.075, 0.017, 0.017], parent_node_id=root)
    c.shape("uv_sphere", "Dragonfly Tail",
            v_add(position, [-0.115, 0.004, 0.0]),
            radius=1.0, slice_count=12, stack_count=8,
            material_name=body_mat, motion_mode="none",
            scale=[0.085, 0.009, 0.009], parent_node_id=root)
    c.shape("uv_sphere", "Dragonfly Head",
            v_add(position, [0.075, 0.006, 0.0]),
            radius=0.014, slice_count=12, stack_count=8,
            material_name=head_mat, motion_mode="none", parent_node_id=root)
    # Two wing pairs, swept slightly back, near-horizontal. Wing long
    # axis is local +Z; yaw about Y maps +Z to [sin, 0, cos], so
    # theta = atan2(dir.x, dir.z).
    for pair, sweep_deg, attach_x in (("A", 18.0, 0.015), ("B", 42.0, -0.02)):
        for side in (1.0, -1.0):
            a = math.radians(sweep_deg)
            wing_dir = v_norm([-math.sin(a), 0.08, math.cos(a) * side])
            center = v_add(v_add(position, [attach_x, 0.014, 0.0]),
                           v_scale(wing_dir, 0.080))
            c.shape("box", f"Dragonfly Wing {pair}{'L' if side > 0 else 'R'}",
                    center, size=[0.042, 0.0025, 0.165],
                    material_name=wing_mat, motion_mode="none",
                    rotation_xyzw=axis_angle_quaternion(
                        [0.0, 1.0, 0.0],
                        math.atan2(wing_dir[0], wing_dir[2])),
                    parent_node_id=root)


# ---------------------------------------------------------------------- main

def main():
    args = standard_args("Frog")
    if reframe(args, "Frog", BASE, SHOTS):
        return
    if args.only:
        raise SystemExit(
            "--only is not supported: the frog is one geometry graph and "
            "graph assets cannot be recreated under the same name. Use a "
            "full --reuse rebuild, or edit live via "
            "geometry_graph_set_parameter / the node editor.")
    c = Creation("Frog", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    with fail_soft(c, BASE):
        c.ambience(ambient=[0.13, 0.16, 0.13],
                   clear_color=[0.45, 0.58, 0.52, 1.0], grid=False,
                   sky={"_version": 3, "enabled": True, "mode": 1})

        # Near-white base color: the mottle graph carries the green.
        skin = c.ensure_material("frog skin", base_color=[0.95, 1.0, 0.90],
                                 roughness=0.50, metallic=0.0)
        build_skin_graph(c)
        c.bind_material_texture(skin, SKIN_GRAPH, slot="base_color",
                                wrap="repeat")
        pad_green = c.ensure_material("lily pad", base_color=[0.10, 0.34, 0.12],
                                      roughness=0.42, metallic=0.0)
        # Mid roughness: at 0.08 the sun/sky sheen washed the dark base
        # color out to pale slate; 0.35 lets the deep teal read.
        water = c.ensure_material("pond water", base_color=[0.02, 0.11, 0.09],
                                  roughness=0.35, metallic=0.0,
                                  blending_mode="alpha_blend", opacity=0.94)
        eye_black = c.ensure_material("eye black", base_color=[0.02, 0.02, 0.02],
                                      roughness=0.18, metallic=0.0)
        iris_gold = c.ensure_material("iris gold", base_color=[0.85, 0.60, 0.12],
                                      roughness=0.35, metallic=0.15)
        tympanum_mat = c.ensure_material("tympanum",
                                         base_color=[0.15, 0.30, 0.11],
                                         roughness=0.65, metallic=0.0)
        reed_green = c.ensure_material("reed", base_color=[0.18, 0.36, 0.14],
                                       roughness=0.6, metallic=0.0)
        cattail_brown = c.ensure_material("cattail", base_color=[0.30, 0.17, 0.08],
                                          roughness=0.8, metallic=0.0)

        # Lights + shadow range FIRST (skill rule). Pond is 26 m: half
        # diagonal ~18.4 m stays inside range 30.
        c.light("directional", "Sun", [0.0, 25.0, 0.0],
                [1.0, 0.97, 0.88], 3.0)
        qx = axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(-134.0))
        qy = axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(35.0))
        c.set_node_transform("Sun", rotation_xyzw=quat_mul(qy, qx))
        c.light("point", "Sky Fill", [-8.0, 9.0, 10.0],
                [0.60, 0.72, 0.66], 110.0, range=45.0, cast_shadow=False)
        # Tight fit: range 30 over a 26 m pond speckled the pads with
        # shadow acne. 20 m pond half-diagonal 14.1 m fits range 16.
        c.shadow_range(16.0, z_far=300.0)

        # Pond: thin translucent slab; it must not cast a shadow
        # (dolphin lesson - the surface darkened everything below it).
        pond = c.shape("box", "Pond", [0.0, -0.04, 0.0],
                       size=[20.0, 0.08, 20.0],
                       material_name=water, motion_mode="none")
        c.mutate("set_item_flags", {
            "scene_name": c.scene, "ids": [int(pond["node_id"])],
            "flags": ["shadow_cast"], "enabled": False,
        })

        # Lily pads: squashed spheres with the signature radial slit cut
        # by a thin CSG box (the scaled pooled instance goes private -
        # documented behavior). notch_deg aims each slit differently.
        pads = [
            ([0.0, 0.055, 0.0], [1.05, 0.055, 1.05], 40.0),
            ([1.9, 0.045, -1.1], [0.72, 0.045, 0.72], 160.0),
            ([-1.6, 0.045, 1.3], [0.62, 0.045, 0.62], 250.0),
            ([-2.3, 0.045, -1.7], [0.80, 0.045, 0.80], 320.0),
            ([1.3, 0.045, 1.9], [0.52, 0.045, 0.52], 100.0),
        ]
        notch_jobs = []
        for index, (position, radii, notch_deg) in enumerate(pads):
            pad = c.shape("uv_sphere", f"Lily Pad {index + 1}", position,
                          radius=1.0, slice_count=24, stack_count=12,
                          material_name=pad_green, motion_mode="none",
                          scale=radii)
            phi = math.radians(notch_deg)
            direction = [math.cos(phi), 0.0, math.sin(phi)]
            r = radii[0]
            notch = c.shape(
                "box", f"Pad Notch {index + 1}",
                v_add(position, v_scale(direction, 0.60 * r)),
                size=[0.95 * r, 0.3, max(0.05, 0.09 * r)],
                motion_mode="none", reuse=False,
                rotation_xyzw=axis_angle_quaternion(
                    [0.0, 1.0, 0.0], -phi))
            notch_jobs.append((pad["node_id"], notch["node_id"]))
        for pad_id, notch_id in notch_jobs:
            c.csg(int(pad_id), int(notch_id), operation="difference",
                  wait=False)
        c.settle()

        # A cattail cluster off to the side (not directly behind the
        # frog's head) plus one lone reed on the right, for depth.
        for index, (x, z, height, lean) in enumerate([
                (-3.6, -0.4, 2.3, 4.0), (-3.3, -0.9, 1.9, -5.0),
                (-4.0, -0.1, 2.6, 2.0), (2.9, -2.6, 2.1, -3.0)]):
            rotation = axis_angle_quaternion([0.0, 0.0, 1.0],
                                             math.radians(lean))
            c.shape("cone", f"Reed {index + 1}", [x, 0.0, z],
                    height=height, bottom_radius=0.045, top_radius=0.012,
                    slice_count=10, material_name=reed_green,
                    motion_mode="none", rotation_xyzw=rotation)
            # Brown cattail head near the tip (lean rotates about Z at
            # the base, so the tip shifts in X; a scaled sphere avoids
            # new shape types).
            head_h = 0.80 * height
            head_x = x - head_h * math.sin(math.radians(lean))
            c.shape("uv_sphere", f"Cattail {index + 1}",
                    [head_x, head_h * math.cos(math.radians(lean)), z],
                    radius=1.0, slice_count=12, stack_count=8,
                    material_name=cattail_brown, motion_mode="none",
                    scale=[0.085, 0.24, 0.085])

        frog = build_frog(c, skin)
        decorate_frog(c, frog, (iris_gold, eye_black, tympanum_mat))
        build_dragonfly(c, [0.85, 0.80, 0.55])

        c.clear_selection()
        c.screenshot_views(BASE, SHOTS)
        if not args.no_save:
            c.save(GLB)
    print("Frog complete.")


if __name__ == "__main__":
    main()
