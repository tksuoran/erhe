#!/usr/bin/env python3
"""Creation 19: Dolphin - as an EDITABLE GEOMETRY GRAPH.

A bottlenose dolphin leaping over open water. Originally built with
baked scene ops (box -> catmull_clark -> lattice_deform -> csg union ->
extra CC); migrated to a single live geometry graph so every stage
stays editable - drag a lattice control point and the whole dolphin
re-evaluates:

  Trunk:    box -> subdivide(2) -> lattice ------------------> Boolean.A
  Rostrum:  box -> subdivide(2) -> lattice -> transform -+
  Dorsal:   box -> subdivide(2) -> lattice -> transform -+-> Join -> Boolean.B
  Flippers: box -> subdivide(2) -> lattice -> transform -+   (x2, QUATERNION
  Flukes:   box -> subdivide(2) -> lattice -> transform -+    rotation mode)
  Boolean(union) -> subdivide(1) -> Output

This is the same single-pass multi-tool union the scene op ran: the csg
tool merges all tools into ONE rhs geometry then runs one boolean, and
the graph's Join node is exactly that merge. Two subdivision levels per
part BEFORE the union keep the CSG's triangulation fine enough that the
post-union Catmull-Clark does not weave it into visible bumps; the
final subdivide also regenerates smooth vertex normals, so the baked
version's zero-offset-lattice normals pass is unnecessary here.

The flipper transforms use the Transform node's quaternion rotation
mode (rotation_mode 1 + rotation_quaternion [x,y,z,w], added for this
migration) so the droop/yaw pose math is shared verbatim with the
baked version's create_shape quaternions.

No textures and no texture coordinate requirements; the shape carries
the anatomy. Lattice tables are authored per part (station-squeeze
spindles, swept blades, the fluke crescent); bezier FFD smooths
interior stations, so caps are pinched hard (geometry_graph_sculpt.md).

NOTE --only is not supported: geometry-graph assets cannot be recreated
under the same name (create_graph_mesh rejects duplicates), so iterate
with a full `--reuse` rebuild (one graph evaluation - it is fast) or
live via geometry_graph_set_parameter / the node editor UI.
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
GRAPH_NAME = "Dolphin Graph"


# ------------------------------------------------------------- lattice tables
# The graph Lattice node takes a FLAT float array in lattice_offset_index
# order: index = i + (dx+1) * (j + (dy+1) * k), 3 floats per control point.

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


def blade_offsets(divisions, half, sweep, keep_chord, keep_thick):
    """Upright fin blade (chord X, span Y, thin Z): per height row (j) a
    backward sweep, a chord squeeze and a thickness squeeze - the
    falcate dorsal fin."""
    di, dj, dk = divisions
    flat = make_flat(divisions)
    for j in range(dj + 1):
        sx = 1.0 - keep_chord[j]
        sz = 1.0 - keep_thick[j]
        for i in range(di + 1):
            x = (2.0 * i / di - 1.0) * half[0]
            for k in range(dk + 1):
                z = (2.0 * k / dk - 1.0) * half[2]
                put(flat, divisions, i, j, k, -x * sx + sweep[j], 0.0, -z * sz)
    return flat


def fluke_offsets(divisions, half, sweep, keep_chord, keep_thick, lift):
    """Horizontal tail flukes (chord X, span Z, thin Y): tables indexed
    by distance from the span center, sweeping the tips back into the
    crescent with a slight upward dihedral."""
    di, dj, dk = divisions
    center = dk // 2
    flat = make_flat(divisions)
    for k in range(dk + 1):
        m = abs(k - center)
        sx = 1.0 - keep_chord[m]
        sy = 1.0 - keep_thick[m]
        for i in range(di + 1):
            x = (2.0 * i / di - 1.0) * half[0]
            for j in range(dj + 1):
                y = (2.0 * j / dj - 1.0) * half[1]
                put(flat, divisions, i, j, k,
                    -x * sx + sweep[m], -y * sy + lift[m], 0.0)
    return flat


def paddle_offsets(divisions, half, sweep, keep_chord, keep_thick):
    """One-sided flat paddle (chord X, span +Z root -> tip, thin Y): per
    span station a backward sweep + chord/thickness squeeze - the
    pectoral flipper, built flat and posed by its Transform node."""
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


# ------------------------------------------------------------------- dolphin

def part_chain(g, size, subdivisions, lattice_divisions, offsets):
    """box -> subdivide(2) -> lattice; returns the lattice node id (the
    chain's output end). Explicit cage at the box bounds: subdivision
    shrinks the mesh inside it, auto-fit would rescale the sculpt."""
    box = g.add("box", {"size": size, "subdivisions": subdivisions,
                        "power": 1.0})
    subdivide = g.add("subdivide", {"mode": 0, "iterations": 2})
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


def build_dolphin(c, skin):
    """Build the dolphin geometry graph, bind it to a scene node, pose
    the leap. Returns the bound node's name."""
    g = c.geometry_graph(GRAPH_NAME)

    # Trunk: 2.4 m spindle, +X = nose. Stations i=0 (tail) .. 6 (head).
    trunk_size = [2.4, 0.60, 0.50]
    trunk = part_chain(
        g, trunk_size, [10, 4, 4], [6, 2, 2],
        spindle_offsets(
            [6, 2, 2], [0.5 * v for v in trunk_size],
            keep=[0.24, 0.44, 0.70, 0.93, 1.00, 0.86, 0.45],
            shift_x=[0.18, 0.0, 0.0, 0.0, 0.0, 0.0, -0.26],
            shift_y=[0.12, 0.07, 0.0, 0.0, 0.0, 0.02, 0.03],
            top_taper=0.28, belly_taper=0.16))

    tools = []

    # Rostrum: short blunt beak, slightly below the axis (melon overhang).
    ros_size = [0.36, 0.15, 0.17]
    rostrum = part_chain(
        g, ros_size, [4, 2, 2], [3, 1, 1],
        spindle_offsets(
            [3, 1, 1], [0.5 * v for v in ros_size],
            keep=[1.0, 0.80, 0.60, 0.42],
            shift_x=[0.0, 0.0, 0.0, -0.05],
            shift_y=[0.0, 0.0, -0.01, -0.02]))
    rostrum_pose = g.add("transform", {"translation": [1.04, -0.055, 0.0]})
    g.link(rostrum, rostrum_pose)
    tools.append(rostrum_pose)

    # Dorsal fin: falcate blade over the mid-back.
    dor_size = [0.52, 0.46, 0.055]
    dorsal = part_chain(
        g, dor_size, [4, 4, 1], [2, 3, 1],
        blade_offsets(
            [2, 3, 1], [0.5 * v for v in dor_size],
            sweep=[0.0, -0.05, -0.15, -0.26],
            keep_chord=[1.0, 0.78, 0.55, 0.40],
            keep_thick=[1.0, 0.80, 0.58, 0.42]))
    dorsal_pose = g.add("transform", {"translation": [0.05, 0.44, 0.0]})
    g.link(dorsal, dorsal_pose)
    tools.append(dorsal_pose)

    # Pectoral flippers: flat paddles posed down-out-back with the
    # Transform node's QUATERNION rotation mode. The mesh tip is always
    # local +Z and the paddle is mirror-symmetric in local Y, so the
    # right side is a pure rotation: droop 42 deg about X on the left
    # pairs with 138 deg on the right, yaw sign follows the side.
    flip_size = [0.34, 0.06, 0.38]
    flip_half = [0.5 * v for v in flip_size]
    for side in (1.0, -1.0):
        droop_deg = 42.0 if side > 0.0 else 138.0
        droop = axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(droop_deg))
        yaw = axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(-35.0 * side))
        rotation = quat_mul(yaw, droop)
        tip_dir = quat_rotate(rotation, [0.0, 0.0, 1.0])
        shoulder = [0.62, -0.10, 0.17 * side]
        center = v_add(shoulder, v_scale(tip_dir, flip_half[2] - 0.05))
        flipper = part_chain(
            g, flip_size, [3, 1, 4], [2, 1, 3],
            paddle_offsets(
                [2, 1, 3], flip_half,
                sweep=[0.0, -0.03, -0.10, -0.18],
                keep_chord=[1.0, 0.82, 0.62, 0.42],
                keep_thick=[1.0, 0.85, 0.65, 0.45]))
        flipper_pose = g.add("transform", {
            "translation": center,
            "rotation_mode": 1,  # quaternion
            "rotation_quaternion": rotation,
        })
        g.link(flipper, flipper_pose)
        tools.append(flipper_pose)

    # Tail flukes: one crescent blade across the raised peduncle.
    flu_size = [0.44, 0.08, 1.10]
    flukes = part_chain(
        g, flu_size, [4, 1, 8], [2, 1, 6],
        fluke_offsets(
            [2, 1, 6], [0.5 * v for v in flu_size],
            sweep=[0.0, -0.04, -0.15, -0.34],
            keep_chord=[1.0, 0.90, 0.68, 0.42],
            keep_thick=[1.0, 0.90, 0.72, 0.55],
            lift=[0.0, 0.01, 0.03, 0.06]))
    flukes_pose = g.add("transform", {"translation": [-1.10, 0.10, 0.0]})
    g.link(flukes, flukes_pose)
    tools.append(flukes_pose)

    # Join merges the posed appendages into ONE tool solid (the same
    # merge the scene csg op does for tool_node_ids), then a single
    # boolean union welds them into the trunk, and the extra subdivide
    # fairs the union seams + regenerates smooth vertex normals.
    join = g.add("join")
    for tool in tools:
        g.link(tool, join)
    boolean = g.add("boolean", {"operation": 0})  # union
    g.link(trunk, boolean, dst_slot=0)            # A: trunk
    g.link(join, boolean, dst_slot=1)             # B: merged appendages
    final = g.add("subdivide", {"mode": 0, "iterations": 1})
    out = g.add("output", {"material": skin})
    g.chain([boolean, final, out])
    c.call("get_geometry_graph")  # evaluation barrier

    # Leap pose on the bound scene node: up over the water, nose pitched up.
    dolphin = c.bind_node_mesh("Dolphin", GRAPH_NAME)
    c.set_node_transform(dolphin, translation=[0.0, 1.5, 0.0],
                         rotation_xyzw=axis_angle_quaternion(
                             [0.0, 0.0, 1.0], math.radians(22.0)))
    return dolphin


# ---------------------------------------------------------------------- main

def main():
    args = standard_args("Dolphin")
    if reframe(args, "Dolphin", BASE, SHOTS):
        return
    if args.only:
        raise SystemExit(
            "--only is not supported: the dolphin is one geometry graph and "
            "graph assets cannot be recreated under the same name. Use a "
            "full --reuse rebuild, or edit live via "
            "geometry_graph_set_parameter / the node editor.")
    c = Creation("Dolphin", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    with fail_soft(c, BASE):
        c.ambience(ambient=[0.14, 0.15, 0.18],
                   clear_color=[0.55, 0.66, 0.78, 1.0], grid=False,
                   sky={"_version": 3, "enabled": True, "mode": 1})

        skin = c.ensure_material("dolphin skin", base_color=[0.46, 0.52, 0.58],
                                 roughness=0.42, metallic=0.0)
        water = c.ensure_material("sea water", base_color=[0.02, 0.15, 0.28],
                                  roughness=0.12, metallic=0.0,
                                  blending_mode="alpha_blend", opacity=0.85)

        # Lights + shadow range FIRST (skill rule).
        c.light("directional", "Sun", [0.0, 25.0, 0.0],
                [1.0, 0.96, 0.90], 3.0)
        qx = axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(-134.0))
        qy = axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(30.0))
        c.set_node_transform("Sun", rotation_xyzw=quat_mul(qy, qx))
        c.light("point", "Sky Fill", [-10.0, 12.0, 14.0],
                [0.62, 0.70, 0.85], 140.0, range=60.0, cast_shadow=False)
        # Tight shadow fit: a 3 m subject at range 60 gave blocky shadows
        # + acne speckles. 44 m sea diag = 31 m stays inside range 48.
        c.shadow_range(48.0, z_far=300.0)
        c.shape("box", "Sea", [0.0, -0.04, 0.0], size=[44.0, 0.08, 44.0],
                material_name=water, motion_mode="none")

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
