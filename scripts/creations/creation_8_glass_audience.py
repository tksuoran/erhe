#!/usr/bin/env python3
"""Creation 8: The Glass Audience.

A dark stage room divided in two by a clear glass wall. Two thrones face
each other through the glass: one stands empty, the other seats a human
skeleton built from primitives, hands resting on the armrests. Above the
glass line a lamp swings on a pendulum joint (the Ragdoll Rumble rig: a
dynamic rod constrained to the world, lamp head and a spot light riding
it as stripped-physics children), sweeping its light cone across both
halves. Physics is left running so the lamp keeps swinging live.
Showcases: transmissive glass material, primitive figure posing,
pendulum joint reuse, a physics-driven moving light.
"""

import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, axis_angle_quaternion, quat_mul, standard_args  # noqa: E402


ROT_X_90 = axis_angle_quaternion([1.0, 0.0, 0.0], math.pi / 2.0)


def rotated_shape(c, shape, name, position, rotation_xyzw, **kwargs):
    result = c.shape(shape, name, position, **kwargs)
    node_id = result.get("node_id") if isinstance(result, dict) else None
    if node_id is not None and rotation_xyzw is not None:
        c.move_node_id(node_id, rotation_xyzw=rotation_xyzw)
    return node_id


def build_throne(c, s, tag, m):
    """Throne at z = 2.5*s, facing the glass wall (toward z = 0)."""
    z = 2.5 * s
    c.shape("box", f"Dais {tag}", [0.0, 0.09, z], size=[1.7, 0.18, 1.7],
            material_name=m["wood"])
    c.shape("box", f"Throne Base {tag}", [0.0, 0.36, z], size=[0.66, 0.36, 0.56],
            material_name=m["wood"])
    c.shape("box", f"Throne Seat {tag}", [0.0, 0.58, z], size=[0.72, 0.10, 0.62],
            material_name=m["cushion"])
    c.shape("box", f"Throne Back {tag}", [0.0, 1.12, z + 0.36 * s],
            size=[0.72, 1.40, 0.12], material_name=m["wood"])
    c.shape("box", f"Throne Back Pad {tag}", [0.0, 1.10, z + 0.28 * s],
            size=[0.52, 1.00, 0.06], material_name=m["cushion"])
    for side in (-1.0, 1.0):
        c.shape("uv_sphere", f"Throne Finial {tag} {side:+.0f}",
                [side * 0.33, 1.88, z + 0.36 * s], radius=0.055,
                slice_count=12, stack_count=8, material_name=m["gold"])
        c.shape("box", f"Armrest Post {tag} {side:+.0f}",
                [side * 0.36, 0.75, z - 0.16 * s], size=[0.08, 0.34, 0.08],
                material_name=m["wood"])
        c.shape("box", f"Armrest {tag} {side:+.0f}",
                [side * 0.36, 0.93, z - 0.02 * s], size=[0.10, 0.06, 0.52],
                material_name=m["wood"])


def build_skeleton(c, s, m):
    """Seated skeleton on the throne at z = 2.5*s, hands on the armrests."""
    z = 2.5 * s
    bone = m["bone"]
    # Pelvis + spine + neck.
    c.shape("box", "Skel Pelvis", [0.0, 0.68, z + 0.02 * s],
            size=[0.26, 0.12, 0.20], material_name=bone)
    c.shape("capsule", "Skel Spine", [0.0, 0.95, z + 0.06 * s],
            length=0.42, bottom_radius=0.045, top_radius=0.045,
            slice_count=10, material_name=bone)
    c.shape("capsule", "Skel Neck", [0.0, 1.27, z + 0.04 * s],
            length=0.10, bottom_radius=0.025, top_radius=0.025,
            slice_count=8, material_name=bone)
    # Ribcage: flattened torus hoops around the spine.
    for i in range(4):
        y = 1.22 - 0.066 * i
        major = 0.155 - 0.012 * i
        rib = c.shape("torus", f"Skel Rib {i}", [0.0, y, z + 0.05 * s],
                      major_radius=major, minor_radius=0.016,
                      major_steps=18, minor_steps=6, material_name=bone)
        rib_id = rib.get("node_id") if isinstance(rib, dict) else None
        if rib_id is not None:
            c.move_node_id(rib_id, scale=[1.0, 1.0, 0.8])
    # Skull with eye sockets and jaw, facing the glass.
    c.shape("uv_sphere", "Skel Skull", [0.0, 1.43, z + 0.03 * s], radius=0.105,
            slice_count=18, stack_count=12, material_name=bone)
    for side in (-1.0, 1.0):
        c.shape("uv_sphere", f"Skel Eye {side:+.0f}",
                [side * 0.038, 1.45, z - 0.065 * s], radius=0.026,
                slice_count=8, stack_count=6, material_name=m["dark"])
    c.shape("box", "Skel Jaw", [0.0, 1.335, z - 0.045 * s],
            size=[0.075, 0.04, 0.07], material_name=bone)
    for side in (-1.0, 1.0):
        # Shoulder + upper arm hanging to the elbow.
        c.shape("uv_sphere", f"Skel Shoulder {side:+.0f}",
                [side * 0.20, 1.20, z + 0.05 * s], radius=0.04,
                slice_count=8, stack_count=6, material_name=bone)
        c.shape("capsule", f"Skel Upper Arm {side:+.0f}",
                [side * 0.27, 1.06, z + 0.03 * s], length=0.20,
                bottom_radius=0.028, top_radius=0.028, slice_count=8,
                material_name=bone)
        # Forearm horizontal along the armrest, hand on its front end.
        rotated_shape(c, "capsule", f"Skel Forearm {side:+.0f}",
                      [side * 0.35, 0.985, z - 0.13 * s], ROT_X_90,
                      length=0.24, bottom_radius=0.026, top_radius=0.026,
                      slice_count=8, material_name=bone)
        c.shape("box", f"Skel Hand {side:+.0f}",
                [side * 0.35, 0.985, z - 0.30 * s], size=[0.06, 0.045, 0.12],
                material_name=bone)
        # Thigh forward, shin down, foot flat.
        rotated_shape(c, "capsule", f"Skel Thigh {side:+.0f}",
                      [side * 0.11, 0.64, z - 0.24 * s], ROT_X_90,
                      length=0.30, bottom_radius=0.048, top_radius=0.048,
                      slice_count=10, material_name=bone)
        c.shape("capsule", f"Skel Shin {side:+.0f}",
                [side * 0.12, 0.34, z - 0.48 * s], length=0.38,
                bottom_radius=0.032, top_radius=0.032, slice_count=8,
                material_name=bone)
        c.shape("box", f"Skel Foot {side:+.0f}",
                [side * 0.12, 0.05, z - 0.56 * s], size=[0.08, 0.06, 0.22],
                material_name=bone)


def main():
    args = standard_args("The Glass Audience")
    c = Creation("The Glass Audience", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    c.set_physics(False)
    c.ambience(ambient=[0.035, 0.035, 0.05],
               clear_color=[0.01, 0.01, 0.018, 1.0], grid=False, sky=False)

    m = {
        "floor":   c.make_material(base_color=[0.05, 0.05, 0.06], roughness=0.3,
                                   metallic=0.3, reflectance=0.85, clear_textures=True),
        "wall":    c.make_material(base_color=[0.075, 0.08, 0.10], roughness=0.92,
                                   metallic=0.0, clear_textures=True),
        "steel":   c.make_material(base_color=[0.10, 0.10, 0.12], roughness=0.5,
                                   metallic=0.7, clear_textures=True),
        "wood":    c.make_material(base_color=[0.11, 0.065, 0.04], roughness=0.65,
                                   metallic=0.0, clear_textures=True),
        "cushion": c.make_material(base_color=[0.34, 0.03, 0.05], roughness=0.85,
                                   metallic=0.0, clear_textures=True),
        "gold":    c.make_material(base_color=[1.0, 0.72, 0.29], roughness=0.35,
                                   metallic=1.0, clear_textures=True),
        "bone":    c.make_material(base_color=[0.85, 0.82, 0.72], roughness=0.6,
                                   metallic=0.0, clear_textures=True),
        "dark":    c.make_material(base_color=[0.02, 0.02, 0.025], roughness=0.6,
                                   metallic=0.0, clear_textures=True),
        # blending_mode alpha_blend is what makes the raster path translucent
        # (opacity alone renders opaque); transmission covers the ray tracer.
        "glass":   c.make_material(base_color=[0.62, 0.80, 0.85], roughness=0.05,
                                   metallic=0.0, reflectance=1.0, opacity=0.16,
                                   transmission=0.9, ior=1.5,
                                   blending_mode="alpha_blend", clear_textures=True),
        "glow":    c.make_material(base_color=[1.0, 0.9, 0.6], roughness=0.4,
                                   metallic=0.0, emissive=[6.0, 5.0, 3.0],
                                   clear_textures=True),
    }

    # ------------------------------------------------------------ the room
    c.shape("box", "Stage Floor", [0.0, -0.25, 0.0], size=[14.0, 0.5, 11.0],
            material_name=m["floor"])
    c.shape("box", "End Wall A", [0.0, 2.5, 5.6], size=[14.0, 5.0, 0.4],
            material_name=m["wall"])
    c.shape("box", "End Wall B", [0.0, 2.5, -5.6], size=[14.0, 5.0, 0.4],
            material_name=m["wall"])
    c.shape("box", "Back Wall", [-7.2, 2.5, 0.0], size=[0.4, 5.0, 11.6],
            material_name=m["wall"])

    # Glass wall dividing the room, with a steel frame.
    c.shape("box", "Glass Wall", [0.0, 1.8, 0.0], size=[10.0, 3.6, 0.06],
            material_name=m["glass"])
    c.shape("box", "Glass Frame Top", [0.0, 3.66, 0.0], size=[10.4, 0.12, 0.14],
            material_name=m["steel"])
    for side in (-1.0, 1.0):
        c.shape("box", f"Glass Frame Side {side:+.0f}", [side * 5.06, 1.8, 0.0],
                size=[0.12, 3.72, 0.14], material_name=m["steel"])

    # Gantry above the glass line for the swinging lamp.
    for side in (-1.0, 1.0):
        c.shape("cone", f"Lamp Pillar {side:+.0f}", [side * 5.3, 0.0, 0.0],
                height=4.05, bottom_radius=0.09, top_radius=0.09,
                slice_count=14, material_name=m["steel"])
    c.shape("box", "Lamp Beam", [0.0, 4.12, 0.0], size=[11.0, 0.16, 0.16],
            material_name=m["steel"])

    # ------------------------------------------------------------- thrones
    build_throne(c, +1.0, "Occupied", m)
    build_throne(c, -1.0, "Empty", m)
    build_skeleton(c, +1.0, m)
    c.settle()

    # ------------------------------------------------- the swinging lamp
    # Same rig as Ragdoll Rumble: a dynamic rod on a pendulum joint to the
    # world; shade, bulb and the spot light ride the rod as pure visuals.
    pivot = [0.0, 4.02, 0.0]
    theta = math.radians(38.0)
    d = [math.sin(theta), -math.cos(theta), 0.0]  # pivot -> lamp direction
    q_rod = axis_angle_quaternion([0.0, 0.0, 1.0], theta)

    rod = c.shape("capsule", "Lamp Rod",
                  [pivot[i] + d[i] * 0.73 for i in range(3)],
                  length=1.40, bottom_radius=0.025, top_radius=0.025,
                  slice_count=10, motion_mode="dynamic",
                  material_name=m["steel"])
    rod_id = rod["node_id"]
    c.move_node_id(rod_id, rotation_xyzw=q_rod)

    shade = c.shape("cone", "Lamp Shade",
                    [pivot[i] + d[i] * 1.52 for i in range(3)],
                    height=0.24, bottom_radius=0.26, top_radius=0.06,
                    slice_count=20, use_bottom=False, motion_mode="static",
                    material_name=m["steel"], parent_node_id=rod_id)
    shade_id = shade.get("node_id") if isinstance(shade, dict) else None
    if shade_id is not None:
        c.move_node_id(shade_id, rotation_xyzw=q_rod)
        c.strip_physics(shade_id)
    bulb = c.shape("uv_sphere", "Lamp Bulb",
                   [pivot[i] + d[i] * 1.50 for i in range(3)], radius=0.055,
                   slice_count=12, stack_count=8, motion_mode="static",
                   material_name=m["glow"], parent_node_id=rod_id)
    bulb_id = bulb.get("node_id") if isinstance(bulb, dict) else None
    if bulb_id is not None:
        c.strip_physics(bulb_id)

    # Spot light under the shade, aimed along the rod, riding it.
    c.light("spot", "Swinging Cone", [pivot[i] + d[i] * 1.56 for i in range(3)],
            [1.0, 0.92, 0.7], 380.0, range=14.0, cast_shadow=True,
            inner_spot_angle=0.35, outer_spot_angle=0.62)
    c.settle()
    light_node = c.node_by_name("Swinging Cone")
    if light_node is None:
        raise RuntimeError("spot light node not found")
    c.mutate("reparent_node", {
        "scene_name": c.scene, "node_id": light_node["id"],
        "parent_node_id": rod_id,
    })
    # Point the cone down the rod: rest aim is -Y, tilted with the rod.
    q_down = quat_mul(q_rod, axis_angle_quaternion([1.0, 0.0, 0.0], -math.pi / 2.0))
    c.move_node_id(light_node["id"], rotation_xyzw=q_down)

    c.joint_settings("Lamp Pendulum", [
        {"linear_axes": [True, True, True], "min": 0.0, "max": 0.0},
        {"angular_axes": [True, True, False], "min": 0.0, "max": 0.0},
        {"angular_axes": [False, False, True], "min": -2.5, "max": 2.5},
    ])
    rod_top = c.anchor("lamp pivot", rod_id, pivot)
    c.settle(deadline_s=60.0)
    c.joint(rod_top, settings_name="Lamp Pendulum")  # constrained to the world

    # ------------------------------------------------------------ lighting
    # Quiet fills; the swinging spot is the main actor.
    c.light("point", "Warm Side", [2.5, 3.2, 3.8], [1.0, 0.75, 0.5], 90.0,
            range=10.0, cast_shadow=False)
    c.light("point", "Cold Side", [2.5, 3.2, -3.8], [0.4, 0.5, 0.85], 90.0,
            range=10.0, cast_shadow=False)
    c.light("point", "Rim", [-5.0, 3.0, 0.0], [0.5, 0.55, 0.7], 70.0,
            range=12.0, cast_shadow=False)

    c.settle()
    # Diagonal view through the glass: skeleton throne near, empty one beyond.
    c.place_camera([6.6, 2.9, 5.6], [-1.2, 1.0, -1.4])
    c.screenshot("logs/creations/glass_audience_still.png")

    # Let the lamp swing and keep it swinging.
    print("Releasing the lamp...")
    c.set_physics(True)
    c.wake_physics()
    time.sleep(1.2)
    c.screenshot("logs/creations/glass_audience_swing.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/glass_audience.glb")
    print("The Glass Audience complete (lamp left swinging, physics ON).")


if __name__ == "__main__":
    main()
