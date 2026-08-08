#!/usr/bin/env python3
"""Creation 12: UAP Hangar.

A wide, dim hangar with five classic UAP silhouettes hovering in their
own light pools: a TR-3B black triangle (flat 3-slice cone slab with
three corner glows + center ring), a classic domed flying saucer with a
rim of running lights, a featureless white Tic Tac, the bell-shaped
Sports Model, and a translucent orb with a dark cube inscribed inside
(cube-in-sphere report). Emissive ceiling strips + per-craft spots.
Showcases: recognizable silhouettes from minimal primitives, emissive
materials, alpha-blended shells, staged dark-interior lighting.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, axis_angle_quaternion, standard_args  # noqa: E402


def scaled_sphere(c, name, position, radius, scale, material,
                  slices=18, stacks=13, rotation=None):
    result = c.shape("uv_sphere", name, position, radius=radius,
                     slice_count=slices, stack_count=stacks,
                     material_name=material)
    node_id = result.get("node_id") if isinstance(result, dict) else None
    if node_id is None:
        return None
    if scale != [1.0, 1.0, 1.0]:
        c.move_node_id(node_id, scale=scale)
    if rotation is not None:
        c.move_node_id(node_id, rotation_xyzw=rotation)
    return node_id


# ------------------------------------------------------------------- crafts

def tr3b(c, x, y, z, m):
    """TR-3B: flat black triangle slab, three corner glows, center ring.
    A 3-slice cone lies flat by default (axis = +Y); vertices sit at
    0 / 120 / 240 degrees in the XZ plane."""
    c.shape("cone", "TR-3B Hull", [x, y, z], height=0.45,
            bottom_radius=3.2, top_radius=2.9, slice_count=3,
            material_name=m["black"])
    for i in range(3):
        a = math.radians(i * 120.0)
        c.shape("uv_sphere", f"TR-3B Corner Glow {i}",
                [x + 2.05 * math.cos(a), y - 0.06, z + 2.05 * math.sin(a)],
                radius=0.34, slice_count=14, stack_count=10,
                material_name=m["glow"])
    scaled_sphere(c, "TR-3B Center Glow", [x, y - 0.05, z], 0.55,
                  [1.0, 0.35, 1.0], m["glow"])
    c.light("point", "TR-3B Underglow", [x, y - 1.2, z],
            [1.0, 0.55, 0.15], 60.0, range=6.0, cast_shadow=False)


def saucer(c, x, y, z, m):
    """Classic domed flying saucer with rim running lights."""
    scaled_sphere(c, "Saucer Hull", [x, y, z], 2.0, [1.0, 0.20, 1.0],
                  m["silver"], slices=26, stacks=16)
    scaled_sphere(c, "Saucer Dome", [x, y + 0.28, z], 0.75, [1.0, 0.75, 1.0],
                  m["shell"], slices=20, stacks=14)
    for i in range(8):
        a = 2.0 * math.pi * i / 8.0
        c.shape("uv_sphere", f"Saucer Rim Light {i}",
                [x + 1.55 * math.cos(a), y + 0.05, z + 1.55 * math.sin(a)],
                radius=0.11, slice_count=10, stack_count=8,
                material_name=m["glow_cyan"])
    c.light("point", "Saucer Underglow", [x, y - 1.0, z],
            [0.3, 0.9, 1.0], 50.0, range=5.0, cast_shadow=False)


def tic_tac(c, x, y, z, m):
    """Featureless white lozenge, hovering horizontally."""
    result = c.shape("capsule", "Tic Tac", [x, y, z], length=2.2,
                     bottom_radius=0.62, top_radius=0.62, slice_count=22,
                     material_name=m["white"])
    node_id = result.get("node_id") if isinstance(result, dict) else None
    if node_id is not None:
        c.move_node_id(node_id, rotation_xyzw=axis_angle_quaternion(
            [0.0, 0.0, 1.0], math.pi / 2.0))


def sports_model(c, x, y, z, m):
    """Bell-shaped Sports Model: wide brim disc, tapering waist, cap."""
    scaled_sphere(c, "Sports Brim", [x, y, z], 1.7, [1.0, 0.22, 1.0],
                  m["gray"], slices=26, stacks=16)
    c.shape("cone", "Sports Waist", [x, y + 0.10, z], height=1.05,
            bottom_radius=1.30, top_radius=0.52, slice_count=24,
            material_name=m["gray"])
    scaled_sphere(c, "Sports Cap", [x, y + 1.12, z], 0.54, [1.0, 0.62, 1.0],
                  m["gray"], slices=18, stacks=12)
    for i in range(3):
        a = 2.0 * math.pi * i / 3.0 + 0.4
        c.shape("uv_sphere", f"Sports Porthole {i}",
                [x + 0.78 * math.cos(a), y + 0.52, z + 0.78 * math.sin(a)],
                radius=0.09, slice_count=10, stack_count=8,
                material_name=m["glow"])


def orb(c, x, y, z, m):
    """Translucent sphere with a dark cube inscribed (corners poking out)."""
    c.shape("box", "Orb Cube", [x, y, z], size=[1.20, 1.20, 1.20],
            material_name=m["black"])
    c.shape("uv_sphere", "Orb Shell", [x, y, z], radius=1.02,
            slice_count=24, stack_count=17, material_name=m["shell"])
    c.light("point", "Orb Glow", [x, y - 0.9, z], [0.7, 0.8, 1.0],
            35.0, range=4.0, cast_shadow=False)


# --------------------------------------------------------------------- main

def main():
    args = standard_args("UAP Hangar")
    c = Creation("UAP Hangar", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    c.ambience(ambient=[0.11, 0.12, 0.15],
               clear_color=[0.01, 0.012, 0.02, 1.0], grid=False, sky=False)

    def mat(**edits):
        return c.make_material(clear_textures=True, **edits)

    m = {
        "floor":     mat(base_color=[0.16, 0.16, 0.17], roughness=0.35, metallic=0.2,
                         reflectance=0.7),
        "wall":      mat(base_color=[0.12, 0.13, 0.15], roughness=0.9, metallic=0.0),
        "dark":      mat(base_color=[0.05, 0.05, 0.06], roughness=0.8, metallic=0.2),
        "black":     mat(base_color=[0.015, 0.015, 0.02], roughness=0.45, metallic=0.6),
        "silver":    mat(base_name="Silver", roughness=0.18, metallic=1.0),
        "gray":      mat(base_color=[0.35, 0.36, 0.38], roughness=0.3, metallic=0.9),
        "white":     mat(base_color=[0.92, 0.92, 0.90], roughness=0.25, metallic=0.0),
        "shell":     mat(base_color=[0.55, 0.75, 0.9], roughness=0.15, metallic=0.0,
                         opacity=0.30, blending_mode="alpha_blend"),
        "glow":      mat(base_color=[1.0, 0.6, 0.15], roughness=0.4, metallic=0.0,
                         emissive=[3.0, 1.6, 0.4]),
        "glow_cyan": mat(base_color=[0.3, 0.9, 1.0], roughness=0.4, metallic=0.0,
                         emissive=[0.8, 2.4, 2.8]),
        "strip":     mat(base_color=[0.9, 0.92, 1.0], roughness=0.5, metallic=0.0,
                         emissive=[2.2, 2.3, 2.6]),
    }

    # Hangar shell: floor, back + side walls, ceiling with girders.
    c.shape("box", "Hangar Floor", [0.0, -0.25, 0.0], size=[42.0, 0.5, 26.0],
            material_name=m["floor"])
    c.shape("box", "Hangar Back Wall", [0.0, 5.5, -13.0], size=[42.0, 11.0, 0.5],
            material_name=m["wall"])
    for s in (-1.0, 1.0):
        c.shape("box", f"Hangar Side Wall {s:+.0f}", [s * 21.0, 5.5, 0.0],
                size=[0.5, 11.0, 26.0], material_name=m["wall"])
    c.shape("box", "Hangar Ceiling", [0.0, 11.0, 0.0], size=[42.0, 0.5, 26.0],
            material_name=m["wall"])
    for i, gx in enumerate((-14.0, -7.0, 0.0, 7.0, 14.0)):
        c.shape("box", f"Girder {i}", [gx, 10.4, 0.0], size=[0.6, 0.8, 26.0],
                material_name=m["dark"])
    for i, gx in enumerate((-10.5, -3.5, 3.5, 10.5)):
        c.shape("box", f"Light Strip {i}", [gx, 10.6, 0.0],
                size=[0.35, 0.15, 20.0], material_name=m["strip"])

    # The fleet, widely spaced, each hovering in its own pool of light.
    fleet = [
        ("TR-3B",  tr3b,          -9.0, 3.4, -4.5, [1.0, 0.8, 0.6]),
        ("Saucer", saucer,        -3.0, 2.8, -7.0, [0.7, 0.9, 1.0]),
        ("TicTac", tic_tac,        1.5, 2.6, -1.0, [0.9, 0.9, 1.0]),
        ("Sports", sports_model,   7.0, 2.1, -5.5, [0.8, 0.85, 1.0]),
        ("Orb",    orb,            9.8, 2.8, -0.8, [0.7, 0.8, 1.0]),
    ]
    for name, builder, x, y, z, tint in fleet:
        builder(c, x, y, z, m)
        intensity = 550.0 if name == "TR-3B" else 380.0
        c.light("point", f"{name} Spot", [x, 8.8, z + 1.5], tint, intensity,
                range=16.0, cast_shadow=False)

    # One shadow-casting key so the fleet grounds against the floor.
    c.light("point", "Hangar Key", [2.0, 9.5, 9.0], [0.9, 0.92, 1.0],
            650.0, range=50.0, cast_shadow=True)

    c.settle()
    c.place_camera([13.5, 7.0, 15.0], [-1.5, 2.2, -3.5])
    c.screenshot("logs/creations/uap_hangar.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/uap_hangar.glb")
    print("UAP Hangar complete.")


if __name__ == "__main__":
    main()
