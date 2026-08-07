#!/usr/bin/env python3
"""Creation 6: Robot Roll Call.

A dark showroom with three homage robots on lit pedestals, each built
entirely from parametric shapes so the silhouette carries the character:
an astromech droid (dome head, twin shoulder legs, blue panels), a
trash-compactor bot (boxy body on treads, binocular eyes), and a
wise-cracking bending unit (cylinder body, dome head, antenna, visor).
Showcases: shape composition into characters, per-shape rotation,
material styling, staged museum lighting.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, axis_angle_quaternion, standard_args  # noqa: E402


def rotated_shape(c, shape, name, position, rotation_xyzw, **kwargs):
    result = c.shape(shape, name, position, **kwargs)
    node_id = result.get("node_id") if isinstance(result, dict) else None
    if node_id is not None and rotation_xyzw is not None:
        c.move_node_id(node_id, rotation_xyzw=rotation_xyzw)
    return node_id


def build_astromech(c, x, y0, m):
    """Astro droid: white cylinder body, silver dome, twin shoulder legs."""
    # Body cylinder (cone with equal radii, base at node origin, +Y up).
    c.shape("cone", "Astro Body", [x, y0 + 0.25, 0.0], height=1.0,
            bottom_radius=0.5, top_radius=0.5, slice_count=28,
            material_name=m["white"])
    # Dome: sphere centered exactly at the body top rim.
    c.shape("uv_sphere", "Astro Dome", [x, y0 + 1.25, 0.0], radius=0.5,
            slice_count=28, stack_count=18, material_name=m["silver"])
    # Dome eye + panels.
    c.shape("uv_sphere", "Astro Eye", [x, y0 + 1.52, 0.42], radius=0.075,
            slice_count=12, stack_count=8, material_name=m["dark"])
    c.shape("box", "Astro Dome Panel", [x - 0.17, y0 + 1.56, 0.38],
            size=[0.14, 0.09, 0.06], material_name=m["blue"])
    # Blue body panels, slightly proud of the cylinder front.
    c.shape("box", "Astro Panel A", [x, y0 + 1.02, 0.48],
            size=[0.22, 0.24, 0.06], material_name=m["blue"])
    c.shape("box", "Astro Panel B", [x, y0 + 0.66, 0.48],
            size=[0.30, 0.18, 0.06], material_name=m["blue"])
    c.shape("box", "Astro Vent", [x, y0 + 0.42, 0.48],
            size=[0.26, 0.12, 0.05], material_name=m["dark"])
    # Legs + shoulders + feet.
    for side in (-1.0, 1.0):
        lx = x + side * 0.63
        c.shape("box", f"Astro Leg {side:+.0f}", [lx, y0 + 0.72, -0.02],
                size=[0.26, 0.95, 0.35], material_name=m["white"])
        # Shoulder disc: cylinder rotated to lie along X, embedded in the leg.
        q = axis_angle_quaternion([0.0, 0.0, 1.0], -side * math.pi / 2.0)
        rotated_shape(c, "cone", f"Astro Shoulder {side:+.0f}",
                      [x + side * 0.50, y0 + 1.06, -0.02], q, height=0.17,
                      bottom_radius=0.24, top_radius=0.24, slice_count=20,
                      material_name=m["blue"])
        c.shape("box", f"Astro Foot {side:+.0f}", [lx, y0 + 0.11, 0.04],
                size=[0.32, 0.22, 0.58], material_name=m["white"])
    # Third center foot, peeking forward.
    c.shape("box", "Astro Center Foot", [x, y0 + 0.10, 0.28],
            size=[0.24, 0.20, 0.44], material_name=m["white"])


def build_compactor(c, x, y0, m):
    """Compactor bot: rusty cube on treads, periscope neck, binocular eyes."""
    for side in (-1.0, 1.0):
        c.shape("box", f"Compactor Tread {side:+.0f}",
                [x + side * 0.56, y0 + 0.23, 0.0],
                size=[0.34, 0.46, 1.05], material_name=m["tread"])
    c.shape("box", "Compactor Body", [x, y0 + 0.72, 0.0],
            size=[0.85, 0.80, 0.85], material_name=m["rust"])
    # Front hatch detail.
    c.shape("box", "Compactor Hatch", [x, y0 + 0.62, 0.42],
            size=[0.55, 0.40, 0.06], material_name=m["tread"])
    # Stubby arms on the body sides.
    for side in (-1.0, 1.0):
        c.shape("box", f"Compactor Arm {side:+.0f}",
                [x + side * 0.50, y0 + 0.86, 0.30],
                size=[0.11, 0.14, 0.62], material_name=m["rust"])
        c.shape("box", f"Compactor Hand {side:+.0f}",
                [x + side * 0.50, y0 + 0.84, 0.64],
                size=[0.16, 0.06, 0.16], material_name=m["tread"])
    # Neck leaning slightly forward, then the binocular head.
    q_neck = axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(14.0))
    rotated_shape(c, "box", "Compactor Neck", [x, y0 + 1.32, -0.08], q_neck,
                  size=[0.10, 0.46, 0.10], material_name=m["tread"])
    q_eye = axis_angle_quaternion([1.0, 0.0, 0.0], math.pi / 2.0)
    for side in (-1.0, 1.0):
        rotated_shape(c, "capsule", f"Compactor Eye {side:+.0f}",
                      [x + side * 0.135, y0 + 1.58, 0.06], q_eye,
                      length=0.26, bottom_radius=0.115, top_radius=0.115,
                      slice_count=18, material_name=m["silver"])
        c.shape("uv_sphere", f"Compactor Lens {side:+.0f}",
                [x + side * 0.135, y0 + 1.58, 0.26], radius=0.085,
                slice_count=12, stack_count=8, material_name=m["dark"])


def build_bender(c, x, y0, m):
    """Bending unit: gray cylinder body, dome head, visor eyes, antenna."""
    for side in (-1.0, 1.0):
        lx = x + side * 0.16
        c.shape("cone", f"Bender Foot {side:+.0f}", [lx, y0, 0.0],
                height=0.10, bottom_radius=0.17, top_radius=0.09,
                slice_count=16, material_name=m["gray"])
        c.shape("cone", f"Bender Leg {side:+.0f}", [lx, y0 + 0.10, 0.0],
                height=0.42, bottom_radius=0.055, top_radius=0.055,
                slice_count=12, material_name=m["gray"])
    # Body: slightly tapered cylinder.
    c.shape("cone", "Bender Body", [x, y0 + 0.52, 0.0], height=0.78,
            bottom_radius=0.35, top_radius=0.30, slice_count=26,
            material_name=m["gray"])
    c.shape("box", "Bender Door", [x, y0 + 0.86, 0.29],
            size=[0.30, 0.34, 0.05], material_name=m["gray"])
    # Arms: hanging capsules.
    for side in (-1.0, 1.0):
        c.shape("capsule", f"Bender Arm {side:+.0f}",
                [x + side * 0.43, y0 + 0.92, 0.0], length=0.46,
                bottom_radius=0.055, top_radius=0.055, slice_count=12,
                material_name=m["gray"])
        c.shape("uv_sphere", f"Bender Hand {side:+.0f}",
                [x + side * 0.43, y0 + 0.60, 0.0], radius=0.085,
                slice_count=12, stack_count=8, material_name=m["gray"])
    # Head: neck cylinder + dome + visor with two eyes + antenna ball.
    c.shape("cone", "Bender Head", [x, y0 + 1.30, 0.0], height=0.40,
            bottom_radius=0.19, top_radius=0.19, slice_count=22,
            material_name=m["gray"])
    c.shape("uv_sphere", "Bender Head Dome", [x, y0 + 1.70, 0.0], radius=0.19,
            slice_count=22, stack_count=14, material_name=m["gray"])
    c.shape("box", "Bender Visor", [x, y0 + 1.55, 0.13],
            size=[0.31, 0.11, 0.14], material_name=m["silver"])
    for side in (-1.0, 1.0):
        c.shape("uv_sphere", f"Bender Eye {side:+.0f}",
                [x + side * 0.075, y0 + 1.55, 0.21], radius=0.052,
                slice_count=10, stack_count=8, material_name=m["glow"])
    c.shape("box", "Bender Mouth", [x, y0 + 1.36, 0.16],
            size=[0.18, 0.09, 0.06], material_name=m["dark"])
    c.shape("cone", "Bender Antenna", [x, y0 + 1.88, 0.0], height=0.17,
            bottom_radius=0.022, top_radius=0.012, slice_count=8,
            material_name=m["gray"])
    c.shape("uv_sphere", "Bender Antenna Ball", [x, y0 + 2.07, 0.0],
            radius=0.045, slice_count=10, stack_count=8,
            material_name=m["silver"])


def main():
    args = standard_args("Robot Roll Call")
    c = Creation("Robot Roll Call", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    c.ambience(ambient=[0.045, 0.045, 0.065],
               clear_color=[0.012, 0.012, 0.02, 1.0], grid=False, sky=False)

    def mat(**edits):
        return c.make_material(clear_textures=True, **edits)

    m = {
        "floor":  mat(base_color=[0.045, 0.045, 0.055], roughness=0.3, metallic=0.35, reflectance=0.9),
        "wall":   mat(base_color=[0.07, 0.075, 0.095], roughness=0.92, metallic=0.0),
        "plinth": mat(base_color=[0.13, 0.13, 0.145], roughness=0.45, metallic=0.6),
        "white":  mat(base_color=[0.85, 0.86, 0.88], roughness=0.35, metallic=0.1),
        "blue":   mat(base_color=[0.05, 0.22, 0.65], roughness=0.3, metallic=0.25),
        "silver": mat(base_name="Silver", roughness=0.22, metallic=1.0),
        "dark":   mat(base_color=[0.03, 0.03, 0.035], roughness=0.5, metallic=0.2),
        "rust":   mat(base_color=[0.68, 0.47, 0.11], roughness=0.75, metallic=0.15),
        "tread":  mat(base_color=[0.055, 0.05, 0.045], roughness=0.9, metallic=0.0),
        "gray":   mat(base_color=[0.46, 0.51, 0.49], roughness=0.32, metallic=0.85),
        "glow":   mat(base_color=[0.9, 0.8, 0.35], roughness=0.4, metallic=0.0,
                      emissive=[2.2, 1.8, 0.5]),
    }

    # Showroom: glossy floor + back wall.
    c.shape("box", "Showroom Floor", [0.0, -0.25, 0.0], size=[30.0, 0.5, 18.0],
            material_name=m["floor"])
    c.shape("box", "Showroom Wall", [0.0, 4.0, -3.6], size=[30.0, 9.0, 0.4],
            material_name=m["wall"])
    c.shape("box", "Showroom Side Wall", [-11.0, 4.0, 2.0], size=[0.4, 9.0, 14.0],
            material_name=m["wall"])

    y0 = 0.22
    stations = [
        (-2.9, build_astromech, [0.35, 0.55, 1.0]),
        (0.0, build_compactor, [1.0, 0.65, 0.3]),
        (2.9, build_bender, [0.4, 1.0, 0.55]),
    ]
    for x, builder, accent in stations:
        c.shape("cone", f"Pedestal {x:+.1f}", [x, 0.0, 0.0], height=y0,
                bottom_radius=1.15, top_radius=1.05, slice_count=32,
                material_name=m["plinth"])
        builder(c, x, y0, m)
        # Museum accent light behind each robot + a warm key in front.
        c.light("point", f"Accent {x:+.1f}", [x - 0.8, 2.6, -2.0], accent,
                80.0, range=8.0, cast_shadow=False)
        c.light("point", f"Key {x:+.1f}", [x + 0.9, 3.1, 2.7],
                [1.0, 0.95, 0.85], 110.0, range=12.0, cast_shadow=False)

    # One shadow-casting main light so the robots ground themselves.
    c.light("point", "House Light", [0.0, 5.5, 4.5], [0.95, 0.95, 1.0], 240.0,
            range=25.0, cast_shadow=True)

    c.settle()
    c.place_camera([4.6, 2.5, 6.4], [0.0, 1.05, -0.2])
    c.screenshot("logs/creations/robot_roll_call.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/robot_roll_call.glb")
    print("Robot Roll Call complete.")


if __name__ == "__main__":
    main()
