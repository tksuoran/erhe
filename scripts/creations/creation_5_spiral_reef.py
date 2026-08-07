#!/usr/bin/env python3
"""Creation 5: The Spiral Reef.

A double golden-angle helix of organic forms rising out of a shallow sea:
tori, spheres and capsules shrink as they climb, colored along a coral
hue ramp. Direct MCP geometry operations sculpt them after placement -
catmull_clark smooths every third form, chamfer facets another third,
Laplacian smooth melts the rest - so no two forms keep their parametric
look. Showcases: direct geometry operations on selections, procedural
placement, materials.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, hsv_to_rgb, standard_args  # noqa: E402


COUNT = 44
STEP = math.radians(26.0)   # regular step -> readable double-coil
BASE_R = 6.5
TOP_Y = 10.0


def main():
    args = standard_args("The Spiral Reef")
    c = Creation("Spiral Reef", port=args.port)
    scene = c.new_scene()
    print(f"scene: {scene}")

    c.ambience(ambient=[0.11, 0.14, 0.16], clear_color=[0.05, 0.14, 0.18, 1.0])

    sea = c.make_material(base_color=[0.04, 0.18, 0.22], roughness=0.05,
                          metallic=0.0, reflectance=1.0, opacity=0.55)
    sand = c.make_material(base_color=[0.55, 0.48, 0.33], roughness=1.0)
    c.shape("box", "Sea Floor", [0.0, -0.6, 0.0], size=[30.0, 0.4, 30.0], material_name=sand)
    c.shape("box", "Sea Surface", [0.0, -0.15, 0.0], size=[30.0, 0.1, 30.0],
            material_name=sea)

    # Central spire the helix wraps around.
    spire = c.make_material(base_color=[0.35, 0.30, 0.28], roughness=0.85)
    c.shape("cone", "Spire", [0.0, 0.0, 0.0], height=TOP_Y + 2.0,
            bottom_radius=1.0, top_radius=0.12, slice_count=24,
            material_name=spire)

    created = []   # (node name, op kind)
    shapes = ["torus", "uv_sphere", "capsule"]
    for i in range(COUNT):
        t = i / (COUNT - 1)
        angle = i * STEP
        radius = BASE_R * (1.0 - 0.72 * t)
        x = radius * math.cos(angle)
        z = radius * math.sin(angle)
        y = 0.3 + TOP_Y * (t ** 1.15)
        size = 1.15 * (1.0 - 0.6 * t)

        hue = 0.98 - 0.55 * t  # magenta-coral down to teal
        rgb = hsv_to_rgb(hue % 1.0, 0.65, 0.85)
        mat = c.make_material(base_color=list(rgb), roughness=0.35,
                              metallic=0.15, reflectance=0.6)

        kind = shapes[i % 3]
        name = f"Reef {i:02d}"
        if kind == "torus":
            c.shape("torus", name, [x, y, z], major_radius=0.55 * size,
                    minor_radius=0.22 * size, major_steps=18, minor_steps=12,
                    material_name=mat)
        elif kind == "uv_sphere":
            c.shape("uv_sphere", name, [x, y, z], radius=0.5 * size,
                    slice_count=14, stack_count=9, material_name=mat)
        else:
            c.shape("capsule", name, [x, y, z], length=0.9 * size,
                    bottom_radius=0.28 * size, top_radius=0.18 * size,
                    slice_count=12, material_name=mat)
        created.append((name, i % 3))

    c.settle()

    # Sculpt with direct geometry operations, batched per op over selections.
    def ids_for(op_index):
        wanted = {name for name, k in created if k == op_index}
        return [n["id"] for n in c.nodes() if n.get("name") in wanted]

    # NOTE: chamfer on uv_sphere crashes the editor (bug logged 2026-08-07);
    # remesh gives the spheres an organic refaceted look instead.
    cc_ids = ids_for(0)
    if cc_ids:
        c.select(cc_ids)
        c.mutate("catmull_clark")
        c.settle()
    rm_ids = ids_for(1)
    if rm_ids:
        c.select(rm_ids)
        c.mutate("remesh", {"target_vertex_count": 500, "anisotropy": 0.04})
        c.settle()
    sm_ids = ids_for(2)
    if sm_ids:
        c.select(sm_ids)
        c.mutate("smooth", {"iterations": 6, "strength": 0.7})
        c.settle()
    c.clear_selection()

    # Lighting: sunny shafts + underwater bounce.
    c.light("directional", "Sun Shaft", [0.0, 25.0, 0.0], [1.0, 0.95, 0.8], 2.6)
    c.light("point", "Reef Glow", [0.0, 6.0, 0.0], [0.4, 0.9, 0.8], 300.0, range=30.0,
            cast_shadow=False)
    c.light("point", "Coral Warm", [6.0, 3.0, 6.0], [1.0, 0.6, 0.4], 220.0, range=28.0,
            cast_shadow=False)
    c.light("point", "Violet Rim", [-6.0, 8.0, -6.0], [0.6, 0.4, 1.0], 180.0, range=30.0,
            cast_shadow=False)

    c.settle()
    c.place_camera([15.5, 9.5, 15.5], [0.0, 4.6, 0.0])
    c.screenshot("logs/creations/spiral_reef.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/spiral_reef.glb")
    print("Spiral Reef complete.")


if __name__ == "__main__":
    main()
