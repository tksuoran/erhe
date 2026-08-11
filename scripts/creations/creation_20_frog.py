#!/usr/bin/env python3
"""Creation 20: Frog - a sitting frog as an EDITABLE GEOMETRY GRAPH.

A green frog sitting on a lily pad in a dark pond, built the same way as
the dolphin (creation 19): every body part is a coarse box cage ->
subdivide -> lattice FFD chain, appendages are posed by Transform nodes
(quaternion rotation mode), then Join -> ONE Boolean union -> final
subdivide -> smooth_normals -> Output. Drag any lattice point in the
node editor and the whole frog re-evaluates.

  Body:        box -> subdivide(2) -> lattice -------------------> Boolean.A
  Eyes (x2):   box -> subdivide(2) -> transform ---------------+
  Thighs (x2): box -> subdivide(2) -> lattice -> transform ----+-> Join -> Boolean.B
  Feet (x2):   box -> subdivide(1) -> lattice -> transform ----+
  Arms (x2):   box -> subdivide(2) -> lattice -> transform ----+
  Front feet:  box -> subdivide(1) -> lattice -> transform ----+   (x2)
  Boolean(union) -> subdivide(1) -> smooth_normals -> Output

ROUNDNESS RULE (geometry_graph_sculpt.md): coarse box subdivisions -
often 0 per axis - and a single CC iteration per part read round; dense
cages read square. Caps are pinched hard because bezier FFD softens
interior stations but applies endpoints exactly.

The frog's crouch lives in the lattice tables (arched back, raised
rump) and the appendage Transform quaternions (folded thighs hugging
the flanks, webbed feet splayed forward, straight forelegs propping the
chest). Black pupils are two small scene-node spheres placed on the eye
bulges - they are not part of the union so they keep their own dark
material.

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
    axis_angle_quaternion, quat_mul, quat_rotate, v_add, v_scale,
    v_sub, v_length,
)


def align_x_quaternion(direction):
    """Quaternion rotating local +X onto the given unit direction."""
    d = v_scale(direction, 1.0 / v_length(direction))
    dot = max(-1.0, min(1.0, d[0]))
    axis = [0.0, -d[2], d[1]]  # cross(+X, d)
    length = math.sqrt(axis[1] * axis[1] + axis[2] * axis[2])
    if length < 1.0e-6:
        axis = [0.0, 0.0, 1.0]
    else:
        axis = [0.0, axis[1] / length, axis[2] / length]
    return axis_angle_quaternion(axis, math.acos(dot))

SHOTS = [
    ("",       [1.9, 1.1, 2.3], [0.0, 0.35, 0.0]),
    ("_side",  [0.1, 0.7, 2.6], [0.0, 0.35, 0.0]),
    ("_front", [2.5, 0.9, 0.3], [0.0, 0.35, 0.0]),
    ("_top",   [0.5, 3.2, 0.9], [0.0, 0.3, 0.0]),
]

BASE = "logs/creations/frog"
GLB = "res/editor/scenes/creations/frog.glb"
GRAPH_NAME = "Frog Graph"


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
                    top_taper=0.0, belly_taper=0.0):
    """Body along X: per-station (i) squeeze of Y/Z toward the centerline
    (keep[i] = surviving fraction), axial cap shifts and dorsoventral
    shifts; top/belly rows get extra beam (Z) squeeze for the oval
    cross-section."""
    di, dj, dk = divisions
    flat = make_flat(divisions)
    for i in range(di + 1):
        squeeze = 1.0 - keep[i]
        for j in range(dj + 1):
            y = (2.0 * j / dj - 1.0) * half[1]
            extra = top_taper if j == dj else (belly_taper if j == 0 else 0.0)
            sz = squeeze + (1.0 - squeeze) * extra
            for k in range(dk + 1):
                z = (2.0 * k / dk - 1.0) * half[2]
                put(flat, divisions, i, j, k,
                    shift_x[i], -y * squeeze + shift_y[i], -z * sz)
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


def build_frog(c, skin):
    """Build the frog geometry graph, bind it to a scene node, sit it on
    the lily pad. Returns the bound node's name."""
    g = c.geometry_graph(GRAPH_NAME)

    # Body (torso + head merged, frogs have no neck): 0.95 m along X,
    # +X = snout. Stations i=0 (rump) .. 6 (snout tip): big raised rump,
    # arched back sloping down to a blunt wide snout.
    body_size = [0.95, 0.44, 0.66]
    body = part_chain(
        g, body_size, [2, 0, 0], [6, 2, 2],
        iterations=2,
        offsets=spindle_offsets(
            [6, 2, 2], [0.5 * v for v in body_size],
            keep=[0.42, 0.88, 1.00, 0.96, 0.86, 0.76, 0.52],
            shift_x=[0.12, 0.0, 0.0, 0.0, 0.0, 0.0, -0.08],
            shift_y=[0.02, 0.08, 0.10, 0.08, 0.05, 0.04, 0.04],
            top_taper=0.20, belly_taper=0.04))

    tools = []

    # Eye bulges: two spheres-from-boxes on top of the head, wide apart
    # (frog eyes sit on the skull corners). No lattice needed - a single
    # CC'd cube is already the dome; the union trims the buried half.
    for side in (1.0, -1.0):
        eye_box = g.add("box", {"size": [0.19, 0.17, 0.17],
                                "subdivisions": [0, 0, 0], "power": 1.0})
        eye_cc = g.add("subdivide", {"mode": 0, "iterations": 2})
        eye_pose = g.add("transform", {
            "translation": [0.24, 0.235, 0.155 * side]})
        g.chain([eye_box, eye_cc, eye_pose])
        tools.append(eye_pose)

    # Folded hind legs: one bean-shaped mass per side hugging the rear
    # flank (thigh + shank read as one lump on a crouched frog), long
    # axis along the body, bulging up and out.
    thigh_size = [0.56, 0.30, 0.24]
    for side in (1.0, -1.0):
        thigh = part_chain(
            g, thigh_size, [1, 0, 0], [3, 1, 1],
            spindle_offsets(
                [3, 1, 1], [0.5 * v for v in thigh_size],
                keep=[0.45, 1.0, 0.95, 0.55],
                shift_x=[0.06, 0.0, 0.0, -0.05],
                shift_y=[0.0, 0.02, 0.01, -0.02]))
        yaw = axis_angle_quaternion([0.0, 1.0, 0.0],
                                    math.radians(-12.0 * side))
        thigh_pose = g.add("transform", {
            "translation": [-0.28, 0.0, 0.30 * side],
            "rotation_mode": 1,
            "rotation_quaternion": yaw,
        })
        g.link(thigh, thigh_pose)
        tools.append(thigh_pose)

    # Webbed hind feet: flat paddles splayed forward-outward beside the
    # body, ankle at the folded leg. Paddle tip is local +Z; rotating +Z
    # about Y by theta gives [sin, 0, cos], so theta = atan2(fwd, out).
    foot_size = [0.24, 0.05, 0.42]
    foot_half = [0.5 * v for v in foot_size]
    for side in (1.0, -1.0):
        foot = part_chain(
            g, foot_size, [1, 0, 2], [2, 1, 3],
            paddle_offsets(
                [2, 1, 3], foot_half,
                sweep=[0.0, -0.02, -0.05, -0.10],
                keep_chord=[0.45, 0.75, 1.0, 0.62],
                keep_thick=[1.0, 0.85, 0.65, 0.45]))
        theta = math.atan2(0.88, 0.48 * side)
        rotation = axis_angle_quaternion([0.0, 1.0, 0.0], theta)
        tip_dir = quat_rotate(rotation, [0.0, 0.0, 1.0])
        # Ankle tucked up under the thigh lump so the union welds foot
        # to leg (iteration 2 had the paddle floating below the thigh).
        ankle = [-0.27, -0.185, 0.33 * side]
        center = v_add(ankle, v_scale(tip_dir, foot_half[2] - 0.04))
        foot_pose = g.add("transform", {
            "translation": center,
            "rotation_mode": 1,
            "rotation_quaternion": rotation,
        })
        g.link(foot, foot_pose)
        tools.append(foot_pose)

    # Front legs: straight props from the chest down to the pad. The arm
    # is built along X and ALIGNED to the shoulder->wrist segment, so the
    # forearm, wrist and front foot connect exactly (iteration 1 had the
    # arm tip buried in the pad and the foot floating apart from it).
    arm_size = [0.36, 0.11, 0.11]
    ffoot_size = [0.14, 0.04, 0.22]
    ffoot_half = [0.5 * v for v in ffoot_size]
    for side in (1.0, -1.0):
        shoulder = [0.26, 0.02, 0.17 * side]
        wrist = [0.40, -0.205, 0.235 * side]
        arm_dir = v_sub(wrist, shoulder)
        arm_dir = v_scale(arm_dir, 1.0 / v_length(arm_dir))
        arm = part_chain(
            g, arm_size, [1, 0, 0], [3, 1, 1],
            spindle_offsets(
                [3, 1, 1], [0.5 * v for v in arm_size],
                keep=[0.90, 1.0, 0.90, 0.72],
                shift_x=[0.02, 0.0, 0.0, -0.02],
                shift_y=[0.0, 0.0, 0.0, 0.0]))
        rotation = align_x_quaternion(arm_dir)
        # Root end embeds in the chest; +X tip lands on the wrist.
        center = v_add(wrist, v_scale(arm_dir, -0.5 * arm_size[0] + 0.05))
        arm_pose = g.add("transform", {
            "translation": center,
            "rotation_mode": 1,
            "rotation_quaternion": rotation,
        })
        g.link(arm, arm_pose)
        tools.append(arm_pose)

        # Front foot: small paddle forward from the wrist, resting on
        # the pad.
        ffoot = part_chain(
            g, ffoot_size, [1, 0, 1], [2, 1, 2],
            paddle_offsets(
                [2, 1, 2], ffoot_half,
                sweep=[0.0, -0.02, -0.05],
                keep_chord=[0.55, 1.0, 0.60],
                keep_thick=[1.0, 0.75, 0.50]))
        theta = math.atan2(0.94, 0.22 * side)
        foot_rotation = axis_angle_quaternion([0.0, 1.0, 0.0], theta)
        tip_dir = quat_rotate(foot_rotation, [0.0, 0.0, 1.0])
        center = v_add(wrist, v_scale(tip_dir, ffoot_half[2] - 0.03))
        ffoot_pose = g.add("transform", {
            "translation": center,
            "rotation_mode": 1,
            "rotation_quaternion": foot_rotation,
        })
        g.link(ffoot, ffoot_pose)
        tools.append(ffoot_pose)

    # Join merges the posed appendages into ONE tool solid, a single
    # boolean union welds them into the body, the extra subdivide fairs
    # the union seams and smooth_normals fixes the shading.
    join = g.add("join")
    for tool in tools:
        g.link(tool, join)
    boolean = g.add("boolean", {"operation": 0})  # union
    g.link(body, boolean, dst_slot=0)             # A: body
    g.link(join, boolean, dst_slot=1)             # B: merged appendages
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

        skin = c.ensure_material("frog skin", base_color=[0.23, 0.46, 0.16],
                                 roughness=0.52, metallic=0.0)
        pad_green = c.ensure_material("lily pad", base_color=[0.10, 0.34, 0.12],
                                      roughness=0.42, metallic=0.0)
        # Mid roughness: at 0.08 the sun/sky sheen washed the dark base
        # color out to pale slate; 0.35 lets the deep teal read.
        water = c.ensure_material("pond water", base_color=[0.02, 0.11, 0.09],
                                  roughness=0.35, metallic=0.0,
                                  blending_mode="alpha_blend", opacity=0.94)
        eye_black = c.ensure_material("eye black", base_color=[0.02, 0.02, 0.02],
                                      roughness=0.18, metallic=0.0)
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
        c.shadow_range(30.0, z_far=300.0)

        # Pond: thin translucent slab; it must not cast a shadow
        # (dolphin lesson - the surface darkened everything below it).
        pond = c.shape("box", "Pond", [0.0, -0.04, 0.0],
                       size=[26.0, 0.08, 26.0],
                       material_name=water, motion_mode="none")
        c.mutate("set_item_flags", {
            "scene_name": c.scene, "ids": [int(pond["node_id"])],
            "flags": ["shadow_cast"], "enabled": False,
        })

        # Lily pads: squashed spheres read as round pads (no new shape
        # types). The frog's pad is the big one at the origin.
        pads = [
            ([0.0, 0.055, 0.0], [1.05, 0.055, 1.05]),
            ([1.9, 0.045, -1.1], [0.72, 0.045, 0.72]),
            ([-1.6, 0.045, 1.3], [0.62, 0.045, 0.62]),
            ([-2.3, 0.045, -1.7], [0.80, 0.045, 0.80]),
            ([1.3, 0.045, 1.9], [0.52, 0.045, 0.52]),
        ]
        for index, (position, radii) in enumerate(pads):
            c.shape("uv_sphere", f"Lily Pad {index + 1}", position,
                    radius=1.0, slice_count=24, stack_count=12,
                    material_name=pad_green, motion_mode="none",
                    scale=radii)

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
            # the base, so the tip shifts by h*sin(lean) in -X... +X for
            # negative lean; a scaled sphere avoids new shape types).
            head_h = 0.80 * height
            head_x = x - head_h * math.sin(math.radians(lean))
            c.shape("uv_sphere", f"Cattail {index + 1}",
                    [head_x, head_h * math.cos(math.radians(lean)), z],
                    radius=1.0, slice_count=12, stack_count=8,
                    material_name=cattail_brown, motion_mode="none",
                    scale=[0.085, 0.24, 0.085])

        build_frog(c, skin)

        # Pupils: small dark spheres on the eye bulges (scene nodes, not
        # part of the union, so they keep their own material). Eye bulge
        # centers in world = frog local + frog translation [0, 0.34, 0].
        for side in (1.0, -1.0):
            c.shape("uv_sphere", f"Pupil {'L' if side > 0 else 'R'}",
                    [0.315, 0.60, 0.165 * side],
                    radius=0.038, slice_count=16, stack_count=8,
                    material_name=eye_black, motion_mode="none")

        c.clear_selection()
        c.screenshot_views(BASE, SHOTS)
        if not args.no_save:
            c.save(GLB)
    print("Frog complete.")


if __name__ == "__main__":
    main()
