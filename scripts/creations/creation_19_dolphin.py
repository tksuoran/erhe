#!/usr/bin/env python3
"""Creation 19: Dolphin.

A bottlenose dolphin leaping over open water, modeled entirely with the
box -> catmull_clark -> lattice_deform part pipeline, welded with ONE
CSG union pass and smoothed with a final catmull_clark. No textures and
no texture coordinates (generate_texcoords=False on every subdivision) -
the shape itself carries the anatomy.

Parts (all watertight boxes, so every stage stays a closed manifold):
  - Trunk: fusiform station-squeeze spindle (max girth ~1/3 behind the
    head, rising tail peduncle, rounded caps via axial shifts, oval
    cross-section from an extra top-row beam taper).
  - Rostrum: short tapered beak, set slightly below the body axis so the
    melon overhangs it.
  - Dorsal fin: falcate blade - backward sweep + chord/thickness taper
    increasing with height.
  - Pectoral flippers (pair): swept blades built axis-aligned, then
    posed down-out-back with align_y_quaternion.
  - Tail flukes: one z-spanning blade, tips swept back into the crescent
    with chord taper and a slight upward dihedral.

Anatomy directed by lattice station tables; bezier FFD smooths interior
stations, so caps are pinched hard (skill: geometry_graph_sculpt.md).
CSG composes in world space: parts are placed in their final relative
pose BEFORE the union, then the whole subtree is posed via the root
group (translate + nose-up pitch) for the leap.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import (  # noqa: E402
    Creation, standard_args, reframe, fail_soft,
    axis_angle_quaternion, quat_mul, quat_rotate, v_add, v_scale,
)

SHOTS = [
    ("",       [3.4, 2.5, 4.2], [0.0, 1.5, 0.0]),
    ("_side",  [0.0, 1.6, 5.2], [0.0, 1.5, 0.0]),
    ("_front", [4.2, 2.4, 2.8], [0.5, 1.5, 0.0]),
    ("_top",   [0.6, 6.2, 1.4], [0.0, 1.4, 0.0]),
]

BASE = "logs/creations/dolphin"
GLB = "res/editor/scenes/creations/dolphin.glb"


# ------------------------------------------------------------- lattice tables

def spindle_offsets(divisions, half, keep, shift_x, shift_y,
                    top_taper=0.0, belly_taper=0.0):
    """Offsets for a body along X: per-station (i) squeeze of Y/Z toward
    the centerline (keep[i] = fraction of the section that survives),
    axial cap shifts (shift_x) and dorsoventral shifts (shift_y).
    top_taper/belly_taper add extra beam (Z) squeeze on the top/bottom
    control rows so the cross-section reads oval instead of boxy."""
    di, dj, dk = divisions
    offsets = []
    for i in range(di + 1):
        squeeze = 1.0 - keep[i]
        for j in range(dj + 1):
            y = (2.0 * j / dj - 1.0) * half[1]
            extra = top_taper if j == dj else (belly_taper if j == 0 else 0.0)
            sz = squeeze + (1.0 - squeeze) * extra
            for k in range(dk + 1):
                z = (2.0 * k / dk - 1.0) * half[2]
                offsets.append([i, j, k,
                                shift_x[i], -y * squeeze + shift_y[i], -z * sz])
    return offsets


def blade_offsets(divisions, half, sweep, keep_chord, keep_thick):
    """Offsets for an upright fin blade (chord along X, span along Y,
    thin in Z): per height row (j) a backward sweep, a chord squeeze
    toward the chord center and a thickness squeeze - the falcate fin."""
    di, dj, dk = divisions
    offsets = []
    for j in range(dj + 1):
        sx = 1.0 - keep_chord[j]
        sz = 1.0 - keep_thick[j]
        for i in range(di + 1):
            x = (2.0 * i / di - 1.0) * half[0]
            for k in range(dk + 1):
                z = (2.0 * k / dk - 1.0) * half[2]
                offsets.append([i, j, k, -x * sx + sweep[j], 0.0, -z * sz])
    return offsets


def fluke_offsets(divisions, half, sweep, keep_chord, keep_thick, lift):
    """Offsets for the horizontal tail flukes (chord along X, span along
    Z, thin in Y): tables are indexed by distance from the span center
    (m = |k - center|), sweeping the tips back into the crescent."""
    di, dj, dk = divisions
    center = dk // 2
    offsets = []
    for k in range(dk + 1):
        m = abs(k - center)
        sx = 1.0 - keep_chord[m]
        sy = 1.0 - keep_thick[m]
        for i in range(di + 1):
            x = (2.0 * i / di - 1.0) * half[0]
            for j in range(dj + 1):
                y = (2.0 * j / dj - 1.0) * half[1]
                offsets.append([i, j, k,
                                -x * sx + sweep[m], -y * sy + lift[m], 0.0])
    return offsets


def paddle_offsets(divisions, half, sweep, keep_chord, keep_thick):
    """Offsets for a one-sided flat paddle (chord along X, span along +Z
    from root k=0 to tip k=dk, thin in Y): per span station a backward
    sweep, a chord squeeze and a thickness squeeze - the pectoral
    flipper, built flat-horizontal and posed afterwards."""
    di, dj, dk = divisions
    offsets = []
    for k in range(dk + 1):
        sx = 1.0 - keep_chord[k]
        sy = 1.0 - keep_thick[k]
        for i in range(di + 1):
            x = (2.0 * i / di - 1.0) * half[0]
            for j in range(dj + 1):
                y = (2.0 * j / dj - 1.0) * half[1]
                offsets.append([i, j, k, -x * sx + sweep[k], -y * sy, 0.0])
    return offsets


# ------------------------------------------------------------------- dolphin

def node_id_of(c, result, name):
    if isinstance(result, dict) and result.get("node_id"):
        return int(result["node_id"])
    node = c.node_by_name(name)
    if node is None:
        raise RuntimeError(f"part '{name}' did not appear")
    return int(node["id"])


def build_dolphin(c, skin):
    """Box -> catmull_clark -> lattice per part, placed in final relative
    pose, ONE union pass, final catmull_clark. Returns the root group id."""
    root = c.group("Dolphin", [0.0, 0.0, 0.0])

    def part(name, position, size, steps, rotation_xyzw=None):
        result = c.shape("box", name, position, size=size, steps=steps,
                         material_name=skin, motion_mode="none", reuse=False,
                         parent_node_id=root, rotation_xyzw=rotation_xyzw)
        return node_id_of(c, result, name)

    def subdivide(node_id, levels=1):
        for _ in range(levels):
            c.mutate("catmull_clark", {"scene_name": c.scene,
                                       "node_id": node_id,
                                       "generate_texcoords": False})

    # Trunk: 2.4 m spindle, +X = nose. Stations i=0 (tail) .. 6 (head).
    body_half = [1.2, 0.30, 0.25]
    # Two pre-union subdivision levels per part: the CSG re-triangulates
    # the whole target, and Catmull-Clark of triangles weaves the
    # alternating diagonals into visible bumps - FINER input triangles
    # push that weave below visibility. (The Laplacian `smooth` op was
    # tried for this and EXPLODES geometry - broken, do not use.)
    body = part("Trunk", [0.0, 0.0, 0.0], [2.4, 0.60, 0.50], [10, 4, 4])
    subdivide(body, 2)
    c.lattice_deform(body, spindle_offsets(
        [6, 2, 2], body_half,
        keep=[0.24, 0.44, 0.70, 0.93, 1.00, 0.86, 0.45],
        shift_x=[0.18, 0.0, 0.0, 0.0, 0.0, 0.0, -0.26],
        shift_y=[0.12, 0.07, 0.0, 0.0, 0.0, 0.02, 0.03],
        top_taper=0.28, belly_taper=0.16),
        divisions=[6, 2, 2],
        cage_min=[-v for v in body_half], cage_max=body_half, wait=False)

    # Rostrum: a short blunt beak, slightly below the axis (melon
    # overhang). Iteration 1's 0.46 m spike read as a marlin bill.
    ros_half = [0.18, 0.075, 0.085]
    rostrum = part("Rostrum", [1.04, -0.055, 0.0], [0.36, 0.15, 0.17], [4, 2, 2])
    subdivide(rostrum, 2)
    c.lattice_deform(rostrum, spindle_offsets(
        [3, 1, 1], ros_half,
        keep=[1.0, 0.80, 0.60, 0.42],
        shift_x=[0.0, 0.0, 0.0, -0.05],
        shift_y=[0.0, 0.0, -0.01, -0.02]),
        divisions=[3, 1, 1],
        cage_min=[-v for v in ros_half], cage_max=ros_half, wait=False)

    # Dorsal fin: falcate blade over the mid-back.
    dor_half = [0.26, 0.23, 0.0275]
    dorsal = part("Dorsal Fin", [0.05, 0.44, 0.0], [0.52, 0.46, 0.055], [4, 4, 1])
    subdivide(dorsal, 2)
    c.lattice_deform(dorsal, blade_offsets(
        [2, 3, 1], dor_half,
        sweep=[0.0, -0.05, -0.15, -0.26],
        keep_chord=[1.0, 0.78, 0.55, 0.40],
        keep_thick=[1.0, 0.80, 0.58, 0.42]),
        divisions=[2, 3, 1],
        cage_min=[-v for v in dor_half], cage_max=dor_half, wait=False)

    # Pectoral flippers: flat-horizontal paddles (chord X, span +Z root
    # -> tip, thin Y), then posed: droop about X brings the tip down-out,
    # yaw about Y sweeps it back. The lattice acts in MESH-LOCAL space,
    # so posing at creation is safe. Iteration 1's span-up blades +
    # align_y_quaternion read as vertical daggers from the front.
    flip_half = [0.17, 0.03, 0.19]
    flippers = []
    for name, side in (("Flipper L", 1.0), ("Flipper R", -1.0)):
        # The mesh tip is always local +Z; the paddle is mirror-symmetric
        # in local Y, so the right side is a pure rotation: droop 128
        # degrees about X sends +Z down-and-right (52 sends it
        # down-and-left), and the yaw sign follows the side.
        droop_deg = 42.0 if side > 0.0 else 132.0
        droop = axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(droop_deg))
        yaw = axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(-35.0 * side))
        rotation = quat_mul(yaw, droop)
        tip_dir = quat_rotate(rotation, [0.0, 0.0, 1.0])
        shoulder = [0.62, -0.10, 0.17 * side]
        center = v_add(shoulder, v_scale(tip_dir, flip_half[2] - 0.05))
        flipper = part(name, center, [0.34, 0.06, 0.38], [3, 1, 4],
                       rotation_xyzw=rotation)
        subdivide(flipper, 2)
        c.lattice_deform(flipper, paddle_offsets(
            [2, 1, 3], flip_half,
            sweep=[0.0, -0.03, -0.10, -0.18],
            keep_chord=[1.0, 0.82, 0.62, 0.42],
            keep_thick=[1.0, 0.85, 0.65, 0.45]),
            divisions=[2, 1, 3],
            cage_min=[-v for v in flip_half], cage_max=flip_half, wait=False)
        flippers.append(flipper)

    # Tail flukes: one crescent blade across the peduncle (which the
    # trunk lattice raised to y ~ +0.10).
    flu_half = [0.22, 0.04, 0.55]
    flukes = part("Flukes", [-1.10, 0.10, 0.0], [0.44, 0.08, 1.10], [4, 1, 8])
    subdivide(flukes, 2)
    c.lattice_deform(flukes, fluke_offsets(
        [2, 1, 6], flu_half,
        sweep=[0.0, -0.04, -0.15, -0.34],
        keep_chord=[1.0, 0.90, 0.68, 0.42],
        keep_thick=[1.0, 0.90, 0.72, 0.55],
        lift=[0.0, 0.01, 0.03, 0.06]),
        divisions=[2, 1, 6],
        cage_min=[-v for v in flu_half], cage_max=flu_half, wait=False)

    # One settle for all queued subdivision + lattice ops, then weld all
    # appendages into the trunk in a SINGLE union pass (world-composed;
    # sequential passes leave sliver-triangle shading artifacts).
    c.settle()
    c.csg(body, [rostrum, dorsal] + flippers + [flukes], "union")

    # The extra catmull_clark: fair the union seams on the merged solid.
    subdivide(body)
    # The MCP catmull_clark leaves the merged mesh FLAT-shaded (the parts
    # read smooth only because lattice_deform's regenerate_attributes had
    # rebuilt their vertex normals). A zero-offset lattice pass moves
    # nothing but regenerates smooth normals over the whole solid.
    c.lattice_deform(body, [[0, 0, 0, 0.0, 0.0, 0.0]], divisions=[1, 1, 1],
                     wait=False)
    c.settle()

    # Leap pose: whole subtree up over the water, nose pitched up.
    c.set_node_transform("Dolphin", translation=[0.0, 1.5, 0.0],
                         rotation_xyzw=axis_angle_quaternion(
                             [0.0, 0.0, 1.0], math.radians(22.0)))
    return root


# ---------------------------------------------------------------------- main

def main():
    args = standard_args("Dolphin")
    if reframe(args, "Dolphin", BASE, SHOTS):
        return
    only = args.only
    c = Creation("Dolphin", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe,
                 reuse=args.reuse or bool(only), keep_scenes=bool(only))
    if only:
        scene = c.attach_scene()
        print(f"attached scene: {scene} (rebuilding only '{only}')")
    else:
        scene = c.new_scene()
        print(f"scene: {scene}")

    with fail_soft(c, BASE):
        if not only:
            c.ambience(ambient=[0.14, 0.15, 0.18],
                       clear_color=[0.55, 0.66, 0.78, 1.0], grid=False,
                       sky={"_version": 3, "enabled": True, "mode": 1})

        skin = c.ensure_material("dolphin skin", base_color=[0.46, 0.52, 0.58],
                                 roughness=0.42, metallic=0.0)
        water = c.ensure_material("sea water", base_color=[0.02, 0.15, 0.28],
                                  roughness=0.12, metallic=0.0,
                                  blending_mode="alpha_blend", opacity=0.85)

        if not only:
            # Lights + shadow range FIRST (skill rule).
            c.light("directional", "Sun", [0.0, 25.0, 0.0],
                    [1.0, 0.96, 0.90], 3.0)
            qx = axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(-134.0))
            qy = axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(30.0))
            c.set_node_transform("Sun", rotation_xyzw=quat_mul(qy, qx))
            c.light("point", "Sky Fill", [-10.0, 12.0, 14.0],
                    [0.62, 0.70, 0.85], 140.0, range=60.0, cast_shadow=False)
            # Tight shadow fit: a 3 m subject at range 60 gave blocky
            # shadows + acne speckles on the back. 44 m sea diag = 31 m
            # stays inside the 48 m range (skill: whole ground inside).
            c.shadow_range(48.0, z_far=300.0)
            c.shape("box", "Sea", [0.0, -0.04, 0.0], size=[44.0, 0.08, 44.0],
                    material_name=water, motion_mode="none")

        if only:
            if only != "Dolphin":
                raise SystemExit(f"--only '{only}' unknown; objects: ['Dolphin']")
            c.delete_nodes(names=["Dolphin"])
        build_dolphin(c, skin)

        # Clear the selection so no gizmo / node label / hotbar row rides
        # into the screenshots (skill: select_items ids=[] before shots).
        c.clear_selection()
        c.screenshot_views(BASE, SHOTS)
        if not args.no_save:
            c.save(GLB)
    print("Dolphin complete.")


if __name__ == "__main__":
    main()
