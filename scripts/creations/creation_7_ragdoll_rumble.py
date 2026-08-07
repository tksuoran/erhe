#!/usr/bin/env python3
"""Creation 7: Ragdoll Rumble.

A golden protocol-droid homage stands at attention (one silver shin, of
course) while a wrecking ball hangs from a gantry behind it. Every body
part is a dynamic rigid body laced together with physics joints: ball
shoulders / hips / neck, hinge elbows / knees, a tight waist, and the
wrecking ball swings on a pendulum joint anchored to the world. The
script freezes physics while it rigs the ragdoll, takes a standing
portrait, then enables the simulation and lets the pendulum turn the
droid into a heap of gold - and captures the aftermath.
Showcases: physics joints (create_physics_joint_settings limits +
anchor-node pivots), dynamic bodies, staged simulation control.
"""

import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, axis_angle_quaternion, standard_args  # noqa: E402


def dyn_shape(c, shape, name, position, **kwargs):
    result = c.shape(shape, name, position, motion_mode="dynamic", **kwargs)
    node_id = result.get("node_id") if isinstance(result, dict) else None
    if node_id is None:
        raise RuntimeError(f"create_shape '{name}' returned no node_id: {result}")
    return node_id


def main():
    args = standard_args("Ragdoll Rumble")
    c = Creation("Ragdoll Rumble", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    # Rig with the simulation frozen; enable it only for the finale.
    c.set_physics(False)

    c.ambience(ambient=[0.05, 0.05, 0.07],
               clear_color=[0.015, 0.015, 0.025, 1.0], grid=False, sky=False)

    # clear_textures: several stock metals carry patterned textures that
    # survive a base_color edit and read strongly on box-density UVs.
    gold = c.make_material(base_color=[1.0, 0.72, 0.29], roughness=0.32,
                           metallic=1.0, clear_textures=True)
    silver = c.make_material(base_name="Silver", base_color=[0.95, 0.95, 0.97],
                             roughness=0.22, metallic=1.0, clear_textures=True)
    steel = c.make_material(base_color=[0.09, 0.09, 0.11], roughness=0.55,
                            metallic=0.7, clear_textures=True)
    floor = c.make_material(base_color=[0.05, 0.05, 0.06], roughness=0.25,
                            metallic=0.3, reflectance=0.85, clear_textures=True)
    glow = c.make_material(base_color=[0.9, 0.75, 0.3], roughness=0.4,
                           metallic=0.0, emissive=[4.0, 3.0, 0.8], clear_textures=True)
    ball_mat = c.make_material(base_color=[0.32, 0.31, 0.33], roughness=0.4,
                               metallic=1.0, clear_textures=True)

    # Stage.
    c.shape("box", "Stage Floor", [0.0, -0.25, 0.0], size=[18.0, 0.5, 14.0],
            material_name=floor)

    # Gantry (static) carrying the pendulum pivot.
    for side in (-1.0, 1.0):
        c.shape("cone", f"Gantry Pillar {side:+.0f}", [side * 1.75, 0.0, 0.0],
                height=4.15, bottom_radius=0.10, top_radius=0.10,
                slice_count=14, material_name=steel)
        c.shape("cone", f"Gantry Base {side:+.0f}", [side * 1.75, 0.0, 0.0],
                height=0.18, bottom_radius=0.30, top_radius=0.12,
                slice_count=14, material_name=steel)
    c.shape("box", "Gantry Beam", [0.0, 4.25, 0.0], size=[3.9, 0.2, 0.2],
            material_name=steel)

    # ------------------------------------------------------------- ragdoll
    # Golden droid, facing +Z (toward the wrecking ball's swing plane).
    parts = {}
    parts["pelvis"] = dyn_shape(c, "box", "Droid Pelvis", [0.0, 1.02, 0.0],
                                size=[0.36, 0.22, 0.24], material_name=gold)
    parts["torso"] = dyn_shape(c, "box", "Droid Torso", [0.0, 1.44, 0.0],
                               size=[0.46, 0.52, 0.28], material_name=gold)
    parts["head"] = dyn_shape(c, "uv_sphere", "Droid Head", [0.0, 1.92, 0.0],
                              radius=0.17, slice_count=22, stack_count=14,
                              material_name=gold)
    # Glowing eyes: parented to the head, rigid bodies stripped so they are
    # pure visuals riding the head body.
    for side in (-1.0, 1.0):
        result = c.shape("uv_sphere", f"Droid Eye {side:+.0f}",
                         [side * 0.065, 1.95, 0.165], radius=0.045,
                         slice_count=10, stack_count=8, material_name=glow,
                         parent_node_id=parts["head"])
        eye_id = result.get("node_id") if isinstance(result, dict) else None
        if eye_id is not None:
            c.strip_physics(eye_id)

    for side in (-1.0, 1.0):
        s = f"{side:+.0f}"
        ax = side * 0.34
        parts[f"upper_arm{s}"] = dyn_shape(
            c, "capsule", f"Droid Upper Arm {s}", [ax, 1.50, 0.0],
            length=0.20, bottom_radius=0.065, top_radius=0.065,
            slice_count=14, material_name=gold)
        parts[f"lower_arm{s}"] = dyn_shape(
            c, "capsule", f"Droid Lower Arm {s}", [ax, 1.15, 0.0],
            length=0.18, bottom_radius=0.055, top_radius=0.055,
            slice_count=14, material_name=gold)
        parts[f"hand{s}"] = dyn_shape(
            c, "uv_sphere", f"Droid Hand {s}", [ax, 0.93, 0.0], radius=0.07,
            slice_count=12, stack_count=8, material_name=gold)
        lx = side * 0.115
        parts[f"upper_leg{s}"] = dyn_shape(
            c, "capsule", f"Droid Upper Leg {s}", [lx, 0.72, 0.0],
            length=0.26, bottom_radius=0.08, top_radius=0.08,
            slice_count=14, material_name=gold)
        # The right shin is the salvaged silver one.
        shin_mat = silver if side > 0 else gold
        parts[f"lower_leg{s}"] = dyn_shape(
            c, "capsule", f"Droid Lower Leg {s}", [lx, 0.32, 0.0],
            length=0.24, bottom_radius=0.07, top_radius=0.07,
            slice_count=14, material_name=shin_mat)
        parts[f"foot{s}"] = dyn_shape(
            c, "box", f"Droid Foot {s}", [lx, 0.065, 0.06],
            size=[0.16, 0.11, 0.30], material_name=gold)

    c.settle()

    # ------------------------------------------------------ joint settings
    lock_linear = {"linear_axes": [True, True, True], "min": 0.0, "max": 0.0}
    c.joint_settings("Ball Tight", [
        lock_linear,
        {"angular_axes": [True, True, True], "min": -0.35, "max": 0.35},
    ])
    c.joint_settings("Ball Wide", [
        lock_linear,
        {"angular_axes": [True, True, True], "min": -1.25, "max": 1.25},
    ])
    c.joint_settings("Hinge", [
        lock_linear,
        {"angular_axes": [False, True, True], "min": 0.0, "max": 0.0},
        {"angular_axes": [True, False, False], "min": -2.1, "max": 2.1},
    ])
    c.joint_settings("Locked", [
        lock_linear,
        {"angular_axes": [True, True, True], "min": 0.0, "max": 0.0},
    ])
    c.joint_settings("Pendulum", [
        lock_linear,
        {"angular_axes": [False, True, True], "min": 0.0, "max": 0.0},
        {"angular_axes": [True, False, False], "min": -2.6, "max": 2.6},
    ])

    # ------------------------------------------------------------- joints
    # Coincident anchor child nodes on both bodies give each joint a clean
    # pivot (create_physics_joint captures frames from the node transforms).
    def link(child, parent, pivot, settings):
        a = c.anchor(f"{child} pivot", parts[child], pivot)
        b = c.anchor(f"{child}<->{parent}", parts[parent], pivot)
        c.settle(deadline_s=60.0)  # anchor inserts execute on the next frame
        c.joint(a, connected_node_id=b, settings_name=settings)

    link("torso", "pelvis", [0.0, 1.155, 0.0], "Ball Tight")
    link("head", "torso", [0.0, 1.725, 0.0], "Ball Tight")
    for side in (-1.0, 1.0):
        s = f"{side:+.0f}"
        ax = side * 0.34
        lx = side * 0.115
        link(f"upper_arm{s}", "torso", [ax, 1.66, 0.0], "Ball Wide")
        link(f"lower_arm{s}", f"upper_arm{s}", [ax, 1.315, 0.0], "Hinge")
        link(f"hand{s}", f"lower_arm{s}", [ax, 1.00, 0.0], "Ball Tight")
        link(f"upper_leg{s}", "pelvis", [lx, 0.93, 0.0], "Ball Wide")
        link(f"lower_leg{s}", f"upper_leg{s}", [lx, 0.51, 0.0], "Hinge")
        link(f"foot{s}", f"lower_leg{s}", [lx, 0.12, 0.0], "Ball Tight")

    # ------------------------------------------------------ wrecking ball
    # Pendulum pivot under the beam; rod + ball start pulled back 68 deg.
    pivot = [0.0, 4.15, 0.0]
    theta = math.radians(68.0)
    d = [0.0, -math.cos(theta), math.sin(theta)]  # pivot -> ball direction
    rod_center = [pivot[i] + d[i] * 1.28 for i in range(3)]
    ball_center = [pivot[i] + d[i] * 2.55 for i in range(3)]

    rod_id = dyn_shape(c, "capsule", "Wrecking Rod", rod_center,
                       length=2.30, bottom_radius=0.045, top_radius=0.045,
                       slice_count=10, material_name=steel)
    c.move_node_id(rod_id, rotation_xyzw=axis_angle_quaternion([1.0, 0.0, 0.0], -theta))
    ball_id = dyn_shape(c, "uv_sphere", "Wrecking Ball", ball_center,
                        radius=0.40, slice_count=24, stack_count=16,
                        material_name=ball_mat)
    c.settle()

    rod_top = c.anchor("rod pivot", rod_id, pivot)
    ball_anchor = c.anchor("ball pivot", ball_id, ball_center)
    rod_tip = c.anchor("rod tip", rod_id, ball_center)
    c.settle(deadline_s=60.0)
    c.joint(rod_top, settings_name="Pendulum")  # constrained to the world
    c.joint(ball_anchor, connected_node_id=rod_tip, settings_name="Locked")

    # ------------------------------------------------------------ lighting
    c.light("point", "Key", [3.5, 4.0, 4.0], [1.0, 0.95, 0.85], 320.0,
            range=18.0, cast_shadow=True)
    c.light("point", "Cool Fill", [-4.0, 2.5, 2.0], [0.35, 0.45, 0.8], 160.0,
            range=16.0, cast_shadow=False)
    c.light("point", "Rim", [0.0, 3.0, -4.0], [0.9, 0.6, 0.3], 180.0,
            range=14.0, cast_shadow=False)

    c.settle()
    # Side-on view: the whole pendulum arc and the droid in profile.
    c.place_camera([6.8, 3.1, 2.6], [0.0, 2.0, 1.0])
    c.screenshot("logs/creations/ragdoll_rumble_standing.png")

    # --------------------------------------------------------- the finale
    print("Releasing the wrecking ball...")
    c.set_physics(True)
    c.wake_physics()
    time.sleep(8.0)
    c.set_physics(False)  # freeze the aftermath pose
    c.screenshot("logs/creations/ragdoll_rumble_aftermath.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/ragdoll_rumble.glb")
    print("Ragdoll Rumble complete (physics left disabled).")


if __name__ == "__main__":
    main()
