#!/usr/bin/env python3
"""Creation 11: Monster Portal Island.

A tropical island - a sandy dome rising out of translucent turquoise
water - where a large triangular stone portal (triangle = 3-slice cone
prisms: dark frame + emissive magenta core) has opened on the beach.
Through it came three one-eyed monsters: two walkers (a squat purple
blob and a tall green cyclops) and a hovering teal flyer, all built
from squashed spheres, capsules and cones with big single eyes. Curved
palm trees (chained tilted trunk cones + drooping frond blades) and a
starfish complete the postcard.
Showcases: alpha-blended water, triangular prisms from low-slice cones,
character composition from primitives, emissive + point-light accents.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, axis_angle_quaternion, quat_mul, standard_args  # noqa: E402


ISLAND_R = 8.0
ISLAND_H = 0.12          # y scale of the island dome sphere


def island_y(x, z):
    """Sand height at (x, z) on the dome, slightly sunk."""
    r_sq = ISLAND_R * ISLAND_R - x * x - z * z
    return ISLAND_H * math.sqrt(max(0.0, r_sq)) - 0.03


# --------------------------------------------------------------------- math

def v_add(a, b):
    return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]


def v_scale(a, s):
    return [a[0] * s, a[1] * s, a[2] * s]


def v_norm(a):
    length = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) or 1.0
    return [a[0] / length, a[1] / length, a[2] / length]


def align_y_quaternion(direction):
    """Quaternion [x,y,z,w] rotating +Y onto direction."""
    d = v_norm(direction)
    dot = max(-1.0, min(1.0, d[1]))
    if dot > 0.99999:
        return None
    if dot < -0.99999:
        return [1.0, 0.0, 0.0, 0.0]
    axis = v_norm([d[2], 0.0, -d[0]])        # cross((0,1,0), d)
    half = math.acos(dot) / 2.0
    s = math.sin(half)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half)]


# ------------------------------------------------------------------ helpers

def scaled_sphere(c, name, position, radius, scale, material,
                  slices=14, stacks=10, rotation=None):
    """Sphere with non-uniform scale; scale FIRST, then optional rotation
    so the elongation follows the rotated local axes."""
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


def aligned_cone(c, name, position, direction, height, r_bottom, r_top,
                 material, slices=12):
    """Cone whose base ends up at `position` with its axis along
    `direction` (rotation pivots at the node origin = cone base)."""
    rotation = align_y_quaternion(direction)
    result = c.shape("cone", name, position, height=height,
                     bottom_radius=r_bottom, top_radius=r_top,
                     slice_count=slices, material_name=material)
    if rotation is not None:
        node_id = result.get("node_id") if isinstance(result, dict) else None
        if node_id is not None:
            c.move_node_id(node_id, rotation_xyzw=rotation)
    return result


# ----------------------------------------------------------------- flora

def palm_tree(c, tag, x, z, lean, m):
    """Curved palm: chained tilted trunk cones, drooping frond blades,
    coconut cluster. lean = unit-ish horizontal lean direction."""
    pos = [x, island_y(x, z) - 0.05, z]
    direction = v_norm([lean[0] * 0.22, 1.0, lean[1] * 0.22])
    seg_len = 0.60
    for i in range(6):
        radius = 0.13 - 0.012 * i
        aligned_cone(c, f"{tag} Trunk {i}", list(pos), direction, seg_len,
                     radius, radius - 0.010, m["bark"], slices=10)
        pos = v_add(pos, v_scale(direction, seg_len * 0.85))
        # progressive curve: lean gently more with every segment
        direction = v_norm(v_add(direction, [lean[0] * 0.09, -0.02, lean[1] * 0.09]))
    crown = list(pos)
    for i in range(7):
        a = 2.0 * math.pi * i / 7.0 + 0.35
        droop = -0.32 - 0.10 * (i % 2)
        frond_dir = v_norm([math.cos(a), droop, math.sin(a)])
        length = 1.55
        center = v_add(crown, v_scale(frond_dir, length * 0.45))
        scaled_sphere(c, f"{tag} Frond {i}", center, length * 0.5,
                      [0.24, 1.0, 0.10], m["frond"], slices=10, stacks=8,
                      rotation=align_y_quaternion(frond_dir))
    for i, (dx, dz) in enumerate([(0.14, 0.05), (-0.10, 0.12), (0.02, -0.15)]):
        c.shape("uv_sphere", f"{tag} Coconut {i}",
                [crown[0] + dx, crown[1] - 0.12, crown[2] + dz],
                radius=0.11, slice_count=10, stack_count=8,
                material_name=m["bark"])


def starfish(c, x, z, m):
    y = island_y(x, z) + 0.03
    for i in range(5):
        a = 2.0 * math.pi * i / 5.0
        arm_dir = [math.cos(a), 0.0, math.sin(a)]
        scaled_sphere(c, f"Starfish Arm {i}",
                      [x + arm_dir[0] * 0.11, y, z + arm_dir[2] * 0.11],
                      0.10, [0.55, 0.22, 1.4], m["coral"], slices=8, stacks=6,
                      rotation=[0.0, math.sin(-a / 2.0), 0.0, math.cos(a / 2.0)])
    c.shape("uv_sphere", "Starfish Core", [x, y, z], radius=0.07,
            slice_count=8, stack_count=6, material_name=m["coral"])


# ----------------------------------------------------------------- portal

def portal(c, x, z, m):
    """Large triangular portal: two 3-slice cone prisms stood upright
    (rotate +Y onto +Z, then roll so a vertex points up); the emissive
    core prism is thinner and pokes through the stone frame."""
    y = island_y(x, z)
    q_stand = axis_angle_quaternion([1.0, 0.0, 0.0], math.pi / 2.0)
    # Roll so a triangle vertex points up (empirically the un-rolled prism
    # renders vertex-right; +90 deg more puts it vertex-up).
    q_up = quat_mul(axis_angle_quaternion([0.0, 0.0, 1.0], math.pi), q_stand)
    center_h = 2.1
    for name, radius, depth, material in [
        ("Portal Frame", 2.5, 0.55, m["stone"]),
        ("Portal Core", 1.65, 0.70, m["glow"]),
    ]:
        # cone base sits at -depth/2 along the (rotated) axis
        result = c.shape("cone", name, [x, y + center_h, z + depth / 2.0],
                         height=depth, bottom_radius=radius, top_radius=radius,
                         slice_count=3, material_name=material)
        node_id = result.get("node_id") if isinstance(result, dict) else None
        if node_id is not None:
            c.move_node_id(node_id, rotation_xyzw=q_up)
    # Rubble at the portal feet.
    for i, (dx, dz, size) in enumerate([(-1.9, 0.5, 0.30), (2.0, 0.4, 0.24),
                                        (-1.3, 0.9, 0.17)]):
        px, pz = x + dx, z + dz
        scaled_sphere(c, f"Portal Rubble {i}", [px, island_y(px, pz) + size * 0.3, pz],
                      size, [1.2, 0.6, 1.0], m["stone"], slices=10, stacks=8)
    c.light("point", "Portal Glow", [x, y + center_h, z + 1.4],
            [0.85, 0.25, 1.0], 90.0, range=7.0, cast_shadow=False)


# ---------------------------------------------------------------- monsters

def eye(c, tag, center, forward, size, m):
    """One big eye: white ball on the body surface + dark pupil."""
    f = v_norm(forward)
    scaled_sphere(c, f"{tag} Eye", v_add(center, v_scale(f, size * 0.72)),
                  size * 0.42, [1.0, 1.0, 0.55], m["white"],
                  rotation=align_y_quaternion(f))
    c.shape("uv_sphere", f"{tag} Pupil", v_add(center, v_scale(f, size * 0.98)),
            radius=size * 0.16, slice_count=10, stack_count=8,
            material_name=m["dark"])


def walker_blob(c, tag, x, z, face, m):
    """Squat purple blob cyclops: dome body, stumpy legs, horns."""
    y0 = island_y(x, z)
    body = [x, y0 + 0.68, z]
    scaled_sphere(c, f"{tag} Body", body, 0.52, [1.0, 1.12, 0.95], m["purple"],
                  slices=18, stacks=13)
    f = v_norm([face[0], 0.0, face[1]])
    side = [-f[2], 0.0, f[0]]
    for s in (-1.0, 1.0):
        leg = v_add([x, y0 + 0.18, z], v_scale(side, s * 0.26))
        c.shape("capsule", f"{tag} Leg {s:+.0f}", leg, length=0.30,
                bottom_radius=0.10, top_radius=0.10, slice_count=10,
                material_name=m["purple"])
        foot = v_add([x, y0 + 0.05, z], v_scale(side, s * 0.28))
        scaled_sphere(c, f"{tag} Foot {s:+.0f}", v_add(foot, v_scale(f, 0.06)),
                      0.13, [1.1, 0.55, 1.35], m["purple"])
        arm_dir = v_norm(v_add(v_scale(side, s), [0.0, -0.8, 0.0]))
        arm = v_add(v_add(body, v_scale(side, s * 0.48)), [0.0, 0.05, 0.0])
        scaled_sphere(c, f"{tag} Arm {s:+.0f}", v_add(arm, v_scale(arm_dir, 0.18)),
                      0.20, [0.45, 1.0, 0.45], m["purple"],
                      rotation=align_y_quaternion(arm_dir))
        horn_dir = v_norm(v_add(v_scale(side, s * 0.5), [0.0, 1.0, 0.0]))
        aligned_cone(c, f"{tag} Horn {s:+.0f}",
                     v_add(body, v_add(v_scale(side, s * 0.30), [0.0, 0.44, 0.0])),
                     horn_dir, 0.28, 0.07, 0.015, m["white"], slices=8)
    eye(c, tag, v_add(body, [0.0, 0.14, 0.0]), f, 0.52, m)


def walker_tall(c, tag, x, z, face, m):
    """Tall green cyclops: egg body on long legs, hanging arms, antenna."""
    y0 = island_y(x, z)
    body = [x, y0 + 1.05, z]
    scaled_sphere(c, f"{tag} Body", body, 0.46, [0.88, 1.5, 0.82], m["green"],
                  slices=18, stacks=13)
    f = v_norm([face[0], 0.0, face[1]])
    side = [-f[2], 0.0, f[0]]
    for s in (-1.0, 1.0):
        leg = v_add([x, y0 + 0.22, z], v_scale(side, s * 0.20))
        c.shape("capsule", f"{tag} Leg {s:+.0f}", leg, length=0.44,
                bottom_radius=0.085, top_radius=0.085, slice_count=10,
                material_name=m["green"])
        scaled_sphere(c, f"{tag} Foot {s:+.0f}",
                      v_add(v_add([x, y0 + 0.06, z], v_scale(side, s * 0.22)),
                            v_scale(f, 0.08)),
                      0.12, [1.1, 0.5, 1.4], m["green"])
        # Arms angled out from the shoulder so they don't read as legs.
        arm_dir = v_norm([side[0] * s * 0.45, -1.0, side[2] * s * 0.45])
        shoulder = v_add(body, v_scale(side, s * 0.42))
        arm_c = v_add(shoulder, v_scale(arm_dir, 0.30))
        result = c.shape("capsule", f"{tag} Arm {s:+.0f}", arm_c, length=0.55,
                         bottom_radius=0.065, top_radius=0.065,
                         slice_count=10, material_name=m["green"])
        node_id = result.get("node_id") if isinstance(result, dict) else None
        if node_id is not None:
            rotation = align_y_quaternion(arm_dir)
            if rotation is not None:
                c.move_node_id(node_id, rotation_xyzw=rotation)
        scaled_sphere(c, f"{tag} Hand {s:+.0f}", v_add(shoulder, v_scale(arm_dir, 0.62)),
                      0.10, [1.0, 1.0, 1.0], m["green"])
    eye(c, tag, v_add(body, [0.0, 0.30, 0.0]), f, 0.46, m)
    aligned_cone(c, f"{tag} Antenna", v_add(body, [0.0, 0.62, 0.0]),
                 [0.12, 1.0, 0.0], 0.34, 0.03, 0.012, m["green"], slices=8)
    c.shape("uv_sphere", f"{tag} Antenna Tip",
            v_add(body, [0.06, 0.98, 0.0]), radius=0.06,
            slice_count=8, stack_count=6, material_name=m["coral"])


def flyer(c, tag, x, y, z, face, m):
    """Hovering teal one-eye: round body, swept wing blades, tail fin."""
    body = [x, y, z]
    scaled_sphere(c, f"{tag} Body", body, 0.36, [1.0, 0.9, 1.05], m["teal"],
                  slices=16, stacks=12)
    f = v_norm([face[0], 0.0, face[1]])
    side = [-f[2], 0.0, f[0]]
    for s in (-1.0, 1.0):
        wing_dir = v_norm(v_add(v_scale(side, s), [0.0, 0.35, 0.0]))
        center = v_add(body, v_scale(wing_dir, 0.62))
        scaled_sphere(c, f"{tag} Wing {s:+.0f}", center, 0.55,
                      [0.30, 1.0, 0.09], m["teal"], slices=10, stacks=8,
                      rotation=align_y_quaternion(wing_dir))
    tail_dir = v_norm(v_add(v_scale(f, -1.0), [0.0, 0.25, 0.0]))
    aligned_cone(c, f"{tag} Tail", v_add(body, v_scale(f, -0.30)),
                 tail_dir, 0.42, 0.09, 0.015, m["teal"], slices=8)
    eye(c, tag, body, f, 0.40, m)


# --------------------------------------------------------------------- main

def main():
    args = standard_args("Monster Portal Island")
    c = Creation("Monster Portal Island", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    c.ambience(ambient=[0.24, 0.26, 0.30],
               clear_color=[0.45, 0.65, 0.85, 1.0], grid=False,
               sky={"_version": 3, "enabled": True, "mode": 1})

    def mat(**edits):
        return c.make_material(clear_textures=True, **edits)

    m = {
        "sand":   mat(base_color=[0.90, 0.80, 0.58], roughness=1.0, metallic=0.0),
        "water":  mat(base_color=[0.02, 0.40, 0.56], roughness=0.10, metallic=0.0,
                      opacity=0.70, blending_mode="alpha_blend"),
        "bark":   mat(base_color=[0.42, 0.28, 0.14], roughness=0.9, metallic=0.0),
        "frond":  mat(base_color=[0.10, 0.42, 0.12], roughness=0.8, metallic=0.0),
        "stone":  mat(base_color=[0.20, 0.19, 0.23], roughness=0.85, metallic=0.0),
        "glow":   mat(base_color=[0.55, 0.10, 0.75], roughness=0.4, metallic=0.0,
                      emissive=[2.6, 0.7, 3.4]),
        "purple": mat(base_color=[0.48, 0.18, 0.60], roughness=0.55, metallic=0.0),
        "green":  mat(base_color=[0.22, 0.52, 0.16], roughness=0.55, metallic=0.0),
        "teal":   mat(base_color=[0.10, 0.52, 0.55], roughness=0.45, metallic=0.0),
        "white":  mat(base_color=[0.93, 0.93, 0.90], roughness=0.35, metallic=0.0),
        "dark":   mat(base_color=[0.04, 0.04, 0.05], roughness=0.4, metallic=0.0),
        "coral":  mat(base_color=[0.90, 0.40, 0.22], roughness=0.7, metallic=0.0),
    }

    # Sea: sand bed below, translucent water sheet, island dome above.
    c.shape("box", "Seabed", [0.0, -0.45, 0.0], size=[60.0, 0.5, 60.0],
            material_name=m["sand"])
    scaled_sphere(c, "Island", [0.0, 0.0, 0.0], ISLAND_R,
                  [1.0, ISLAND_H, 1.0], m["sand"], slices=28, stacks=20)
    c.shape("box", "Sea", [0.0, 0.16, 0.0], size=[60.0, 0.38, 60.0],
            material_name=m["water"])

    # The triangular portal on the back beach.
    portal(c, 0.0, -2.6, m)

    # Palms leaning out over the water.
    palm_tree(c, "Palm A", 3.6, -0.4, (0.8, 0.5), m)
    palm_tree(c, "Palm B", -3.8, 0.8, (-0.9, 0.3), m)

    # The one-eyed welcoming committee.
    walker_blob(c, "Blob", -1.7, 1.6, (0.55, 0.85), m)
    walker_tall(c, "Tall", 1.5, 1.1, (0.30, 0.95), m)
    flyer(c, "Flyer", -0.2, 3.4, 0.6, (0.35, 0.95), m)

    starfish(c, 2.6, 2.9, m)

    # Tropical sun + portal already adds its magenta accent.
    c.light("directional", "Tropical Sun", [0.0, 12.0, 0.0],
            [1.0, 0.95, 0.82], 2.8)
    sun = c.node_by_name("Tropical Sun")
    if sun is not None:
        pitch = math.radians(-128.0)
        yaw = math.radians(35.0)
        q = quat_mul([0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)],
                     [math.sin(pitch / 2), 0.0, 0.0, math.cos(pitch / 2)])
        c.select(sun["id"])
        c.mutate("transform_selection", {"space": "global", "rotation_xyzw": q})
        c.clear_selection()
    c.light("point", "Lagoon Fill", [5.0, 4.0, 7.0], [0.6, 0.8, 0.9], 70.0,
            range=25.0, cast_shadow=False)

    c.settle()
    c.place_camera([5.6, 3.0, 9.4], [0.0, 1.5, -0.8])
    c.screenshot("logs/creations/monster_portal_island.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/monster_portal_island.glb")
    print("Monster Portal Island complete.")


if __name__ == "__main__":
    main()
