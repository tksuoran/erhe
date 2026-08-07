#!/usr/bin/env python3
"""Creation 9: Sandbox Afternoon.

A children's sandbox on a summer lawn: a wooden plank frame filled with
procedurally textured sand, a bucket-molded sand castle with towers and
crenellated walls, scattered toys (beach ball, bucket + spade, toy dump
truck, stacked blocks) and, shading it all, an old oak tree grown from a
stochastic L-system (string rewriting + 3D turtle; branch segments are
tapered cones, tips gather into clustered leaf canopies).
Showcases: L-system geometry from primitives, texture graph sand,
shape composition, warm afternoon lighting.
"""

import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, axis_angle_quaternion, standard_args  # noqa: E402


SAND_TOP = 0.30          # world y of the sand surface
BOX_HALF = 2.5           # sandbox outer half-extent


# --------------------------------------------------------------------- math

def v_add(a, b):
    return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]


def v_scale(a, s):
    return [a[0] * s, a[1] * s, a[2] * s]


def v_cross(a, b):
    return [a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]]


def v_norm(a):
    length = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) or 1.0
    return [a[0] / length, a[1] / length, a[2] / length]


def v_rotate(v, axis, angle):
    """Rodrigues rotation of v about unit axis."""
    c, s = math.cos(angle), math.sin(angle)
    k = axis
    kv = v_cross(k, v)
    kkv = v_cross(k, kv)
    return [v[i] + s * kv[i] + (1.0 - c) * kkv[i] for i in range(3)]


def align_y_quaternion(direction):
    """Quaternion [x,y,z,w] rotating +Y onto direction (unit)."""
    d = v_norm(direction)
    dot = max(-1.0, min(1.0, d[1]))
    if dot > 0.99999:
        return None                      # already +Y
    if dot < -0.99999:
        return [1.0, 0.0, 0.0, 0.0]      # 180 deg about X
    axis = v_norm(v_cross([0.0, 1.0, 0.0], d))
    return axis_angle_quaternion(axis, math.acos(dot))


# ------------------------------------------------------------ sand texture

def grad(stops, interpolation=1):
    return {"interpolation": interpolation,
            "stops": [{"pos": p, "color": list(c)} for p, c in stops]}


def sand_graph(c):
    g = c.texture_graph("Sand")
    ripples = g.add("fbm", {"noise": 1, "scale_x": 9.0, "scale_y": 4.0, "iterations": 5.0})
    grains = g.add("noise", {"size": 8, "density": 0.5})
    mix = g.add("math", {"op": 0, "clamp": True})
    color = g.add("colorize", {"gradient": grad([
        (0.00, [0.52, 0.41, 0.26, 1.0]),
        (0.45, [0.72, 0.60, 0.40, 1.0]),
        (0.75, [0.83, 0.72, 0.51, 1.0]),
        (1.00, [0.92, 0.83, 0.62, 1.0])])})
    out = g.add("output", {"name": "Sand", "size": 1024})
    g.link(ripples, mix)
    g.link(grains, mix, dst_slot=1)
    g.link(mix, color)
    g.link(color, out, dst_slot=2)
    return "Sand"


# ----------------------------------------------------------------- L-system

def expand_lsystem(rng, iterations=5):
    """Stochastic oak rule: an apex grows a segment then 1-3 pitched,
    rolled daughter apices. Remaining apices become leaf clusters."""
    s = "A"
    for _ in range(iterations):
        out = []
        for ch in s:
            if ch != "A":
                out.append(ch)
                continue
            r = rng.random()
            if r < 0.55:
                out.append("F[&A]/[&A]/[&A]")
            elif r < 0.90:
                out.append("F[&A]/[&A]")
            else:
                out.append("F/[&A]")     # crooked single continuation
        s = "".join(out)
    return s.replace("A", "L")


def grow_oak(c, base, bark, leaf_materials, rng):
    """Interpret the L-system with a 3D turtle; every F is a tapered cone
    segment, every L a leaf point (clustered into canopy spheres)."""
    trunk_h = 1.3
    c.shape("cone", "Oak Trunk", base, height=trunk_h, bottom_radius=0.38,
            top_radius=0.24, slice_count=18, material_name=bark)
    # Root flares.
    for i in range(4):
        a = i * math.pi / 2.0 + 0.5
        direction = v_norm([math.cos(a), 0.55, math.sin(a)])
        seg_shape(c, f"Oak Root {i}", base, direction, 0.55, 0.17, 0.04, bark)

    segments = []
    leaves = []
    pos = v_add(base, [0.0, trunk_h, 0.0])
    heading = v_norm([0.14, 0.98, 0.09])
    left = v_norm(v_cross([0.0, 1.0, 0.0], heading)) if abs(heading[1]) < 0.999 else [1.0, 0.0, 0.0]
    left = v_norm(v_cross(heading, v_cross(left, heading)))
    up = v_cross(heading, left)
    depth = 0
    stack = []

    for ch in expand_lsystem(rng):
        if ch == "F":
            # gnarl: small random pitch + roll before drawing
            wob = math.radians(rng.uniform(-9.0, 9.0))
            heading = v_norm(v_rotate(heading, left, wob))
            up = v_norm(v_cross(heading, left))
            length = 1.05 * (0.72 ** depth) * rng.uniform(0.85, 1.15)
            radius = max(0.035, 0.22 * (0.62 ** depth))
            segments.append((list(pos), list(heading), length, radius,
                             max(0.03, radius * 0.7)))
            pos = v_add(pos, v_scale(heading, length))
        elif ch == "&":
            angle = math.radians(rng.uniform(24.0, 44.0))
            heading = v_norm(v_rotate(heading, left, angle))
            up = v_norm(v_cross(heading, left))
        elif ch == "/":
            angle = math.radians(rng.uniform(95.0, 145.0))
            left = v_norm(v_rotate(left, heading, angle))
            up = v_cross(heading, left)
        elif ch == "[":
            stack.append((list(pos), list(heading), list(left), list(up), depth))
            depth += 1
        elif ch == "]":
            pos, heading, left, up, depth = stack.pop()
        elif ch == "L":
            leaves.append(v_add(pos, v_scale(heading, 0.15)))

    print(f"oak: {len(segments)} branch segments, {len(leaves)} leaf tips")
    for i, (start, direction, length, rb, rt) in enumerate(segments):
        seg_shape(c, f"Oak Branch {i}", start, direction, length, rb, rt, bark)

    # Greedy-cluster the leaf tips into canopy blobs.
    clusters = []                        # [centroid, count]
    for point in leaves:
        for cluster in clusters:
            center, count = cluster
            dx = point[0] - center[0]
            dy = point[1] - center[1]
            dz = point[2] - center[2]
            if dx * dx + dy * dy + dz * dz < 0.8 * 0.8:
                cluster[1] = count + 1
                cluster[0] = [center[j] + (point[j] - center[j]) / cluster[1]
                              for j in range(3)]
                break
        else:
            clusters.append([list(point), 1])
    print(f"oak: {len(clusters)} leaf clusters")
    for i, (center, count) in enumerate(clusters):
        radius = min(0.42 + 0.14 * math.sqrt(count), 0.95)
        jitter = [rng.uniform(-0.08, 0.08) for _ in range(3)]
        c.shape("uv_sphere", f"Oak Canopy {i}", v_add(center, jitter),
                radius=radius, slice_count=14, stack_count=10,
                material_name=leaf_materials[i % len(leaf_materials)])


def seg_shape(c, name, start, direction, length, r_bottom, r_top, material):
    """A branch segment: cone with its base at start, +Y aligned to direction."""
    result = c.shape("cone", name, start, height=length,
                     bottom_radius=r_bottom, top_radius=r_top,
                     slice_count=10, material_name=material)
    rotation = align_y_quaternion(direction)
    if rotation is not None:
        node_id = result.get("node_id") if isinstance(result, dict) else None
        if node_id is not None:
            c.move_node_id(node_id, rotation_xyzw=rotation)


# -------------------------------------------------------------- sand castle

def build_castle(c, cx, cz, m):
    y0 = SAND_TOP
    # Packed-sand base mound.
    c.shape("cone", "Castle Mound", [cx, y0 - 0.02, cz], height=0.18,
            bottom_radius=1.25, top_radius=1.02, slice_count=26,
            material_name=m["sand"])
    top = y0 + 0.15
    # Central keep + red roof.
    c.shape("cone", "Castle Keep", [cx, top, cz], height=0.55,
            bottom_radius=0.38, top_radius=0.34, slice_count=22,
            material_name=m["sand"])
    c.shape("cone", "Keep Roof", [cx, top + 0.55, cz], height=0.34,
            bottom_radius=0.42, top_radius=0.02, slice_count=18,
            material_name=m["red"])
    # Corner towers + roofs, molded-bucket look (slight taper).
    t = 0.72
    for i, (dx, dz) in enumerate([(-t, -t), (t, -t), (t, t), (-t, t)]):
        c.shape("cone", f"Tower {i}", [cx + dx, top, cz + dz], height=0.38,
                bottom_radius=0.21, top_radius=0.18, slice_count=18,
                material_name=m["sand"])
        c.shape("cone", f"Tower Roof {i}", [cx + dx, top + 0.38, cz + dz],
                height=0.22, bottom_radius=0.24, top_radius=0.015,
                slice_count=14, material_name=m["red"])
    # Curtain walls between towers, with merlons.
    wall_y = top + 0.12
    walls = [
        ([cx, wall_y, cz - t], [1.15, 0.24, 0.13], True),
        ([cx, wall_y, cz + t], [1.15, 0.24, 0.13], True),
        ([cx - t, wall_y, cz], [0.13, 0.24, 1.15], False),
        ([cx + t, wall_y, cz], [0.13, 0.24, 1.15], False),
    ]
    for w, (center, size, along_x) in enumerate(walls):
        c.shape("box", f"Wall {w}", center, size=size, material_name=m["sand"])
        for k in (-1, 0, 1):
            offset = k * 0.38
            mx = center[0] + (offset if along_x else 0.0)
            mz = center[2] + (0.0 if along_x else offset)
            c.shape("box", f"Merlon {w}.{k + 1}", [mx, wall_y + 0.16, mz],
                    size=[0.09, 0.09, 0.09], material_name=m["sand"])
    # Gate on the front (+Z) wall.
    c.shape("box", "Castle Gate", [cx, top + 0.07, cz + t + 0.04],
            size=[0.22, 0.18, 0.10], material_name=m["dark"])
    # Two loose bucket-molded mounds nearby.
    c.shape("cone", "Mold A", [cx + 1.5, y0, cz + 0.6], height=0.22,
            bottom_radius=0.26, top_radius=0.20, slice_count=16,
            material_name=m["sand"])
    c.shape("cone", "Mold B", [cx + 1.15, y0, cz + 1.15], height=0.20,
            bottom_radius=0.22, top_radius=0.17, slice_count=16,
            material_name=m["sand"])


# --------------------------------------------------------------------- toys

def build_toys(c, m):
    # Beach ball, glossy.
    c.shape("uv_sphere", "Beach Ball", [1.55, SAND_TOP + 0.27, 1.35],
            radius=0.28, slice_count=24, stack_count=16,
            material_name=m["red"])
    # Upright yellow bucket next to the castle.
    c.shape("cone", "Toy Bucket", [0.6, SAND_TOP, 1.5], height=0.28,
            bottom_radius=0.16, top_radius=0.22, slice_count=18,
            material_name=m["yellow"])
    # Spade lying flat: capsule handle along X + blade box.
    q_flat = axis_angle_quaternion([0.0, 0.0, 1.0], math.pi / 2.0)
    result = c.shape("capsule", "Spade Handle", [-0.1, SAND_TOP + 0.045, 1.8],
                     length=0.44, bottom_radius=0.035, top_radius=0.035,
                     slice_count=10, material_name=m["blue"])
    node_id = result.get("node_id") if isinstance(result, dict) else None
    if node_id is not None:
        c.move_node_id(node_id, rotation_xyzw=q_flat)
    c.shape("box", "Spade Blade", [-0.42, SAND_TOP + 0.03, 1.8],
            size=[0.26, 0.05, 0.20], material_name=m["blue"])
    # Toy dump truck.
    tx, tz = 1.55, -1.35
    c.shape("box", "Truck Chassis", [tx, SAND_TOP + 0.16, tz],
            size=[0.30, 0.10, 0.80], material_name=m["dark"])
    c.shape("box", "Truck Cab", [tx, SAND_TOP + 0.35, tz + 0.26],
            size=[0.30, 0.26, 0.28], material_name=m["blue"])
    c.shape("box", "Truck Bed", [tx, SAND_TOP + 0.31, tz - 0.18],
            size=[0.32, 0.18, 0.42], material_name=m["yellow"])
    q_wheel = axis_angle_quaternion([0.0, 0.0, 1.0], math.pi / 2.0)
    for i, (sx, sz) in enumerate([(-1, 0.24), (1, 0.24), (-1, -0.26), (1, -0.26)]):
        result = c.shape("cone", f"Truck Wheel {i}",
                         [tx + sx * 0.21, SAND_TOP + 0.11, tz + sz],
                         height=0.07, bottom_radius=0.10, top_radius=0.10,
                         slice_count=14, material_name=m["dark"])
        node_id = result.get("node_id") if isinstance(result, dict) else None
        if node_id is not None:
            c.move_node_id(node_id, rotation_xyzw=q_wheel)
    # Stacked wooden blocks in a corner.
    for i, (color, dy, dx) in enumerate([("red", 0.11, 0.0), ("blue", 0.33, 0.03),
                                         ("yellow", 0.55, -0.02)]):
        c.shape("box", f"Block {i}", [-1.75 + dx, SAND_TOP + dy, 1.6],
                size=[0.22, 0.22, 0.22], material_name=m[color])


# --------------------------------------------------------------------- main

def main():
    args = standard_args("Sandbox Afternoon")
    c = Creation("Sandbox Afternoon", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    # Bright summer afternoon.
    c.ambience(ambient=[0.22, 0.24, 0.28],
               clear_color=[0.45, 0.62, 0.85, 1.0], grid=False,
               sky={"_version": 3, "enabled": True, "mode": 1})

    def mat(**edits):
        return c.make_material(clear_textures=True, **edits)

    sand_texture = sand_graph(c)
    m = {
        "grass":  mat(base_color=[0.16, 0.30, 0.09], roughness=1.0, metallic=0.0),
        "sand":   mat(base_color=[0.88, 0.78, 0.58], roughness=1.0, metallic=0.0),
        "wood":   mat(base_color=[0.40, 0.26, 0.13], roughness=0.85, metallic=0.0),
        "red":    mat(base_color=[0.75, 0.09, 0.06], roughness=0.30, metallic=0.0),
        "yellow": mat(base_color=[0.92, 0.72, 0.07], roughness=0.40, metallic=0.0),
        "blue":   mat(base_color=[0.08, 0.24, 0.70], roughness=0.35, metallic=0.0),
        "dark":   mat(base_color=[0.05, 0.05, 0.05], roughness=0.6, metallic=0.1),
        "bark":   mat(base_color=[0.28, 0.19, 0.10], roughness=0.95, metallic=0.0),
        "leaf":   mat(base_color=[0.13, 0.30, 0.07], roughness=0.9, metallic=0.0),
        "leaf2":  mat(base_color=[0.21, 0.38, 0.09], roughness=0.9, metallic=0.0),
    }
    c.bind_material_texture(m["sand"], sand_texture, slot="base_color")

    # Lawn + sandbox frame + sand fill.
    c.shape("box", "Lawn", [0.0, -0.25, 0.0], size=[28.0, 0.5, 28.0],
            material_name=m["grass"])
    plank_h, plank_w = 0.42, 0.30
    inner = BOX_HALF - plank_w
    c.shape("box", "Frame North", [0.0, plank_h / 2, -BOX_HALF + plank_w / 2],
            size=[2 * BOX_HALF, plank_h, plank_w], material_name=m["wood"])
    c.shape("box", "Frame South", [0.0, plank_h / 2, BOX_HALF - plank_w / 2],
            size=[2 * BOX_HALF, plank_h, plank_w], material_name=m["wood"])
    c.shape("box", "Frame West", [-BOX_HALF + plank_w / 2, plank_h / 2, 0.0],
            size=[plank_w, plank_h, 2 * inner], material_name=m["wood"])
    c.shape("box", "Frame East", [BOX_HALF - plank_w / 2, plank_h / 2, 0.0],
            size=[plank_w, plank_h, 2 * inner], material_name=m["wood"])
    c.shape("box", "Sand Fill", [0.0, SAND_TOP / 2, 0.0],
            size=[2 * inner, SAND_TOP, 2 * inner], material_name=m["sand"])

    build_castle(c, -0.9, -0.7, m)
    build_toys(c, m)
    grow_oak(c, [4.3, 0.0, -3.2], m["bark"], [m["leaf"], m["leaf2"]],
             random.Random(11))

    # Warm afternoon sun (shadow caster) + cool sky fill.
    c.light("directional", "Afternoon Sun", [0.0, 12.0, 0.0],
            [1.0, 0.93, 0.80], 2.8)
    sun = c.node_by_name("Afternoon Sun")
    if sun is not None:
        pitch = math.radians(-134.0)     # sun elevation ~45 deg
        yaw = math.radians(40.0)
        qy = [0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)]
        qx = [math.sin(pitch / 2), 0.0, 0.0, math.cos(pitch / 2)]
        q = [
            qy[3] * qx[0] + qy[0] * qx[3] + qy[1] * qx[2] - qy[2] * qx[1],
            qy[3] * qx[1] - qy[0] * qx[2] + qy[1] * qx[3] + qy[2] * qx[0],
            qy[3] * qx[2] + qy[0] * qx[1] - qy[1] * qx[0] + qy[2] * qx[3],
            qy[3] * qx[3] - qy[0] * qx[0] - qy[1] * qx[1] - qy[2] * qx[2],
        ]
        c.select(sun["id"])
        c.mutate("transform_selection", {"space": "global", "rotation_xyzw": q})
        c.clear_selection()
    c.light("point", "Sky Fill", [-6.0, 7.0, 6.0], [0.55, 0.65, 0.9], 160.0,
            range=30.0, cast_shadow=False)

    c.settle()
    c.place_camera([7.0, 4.0, 8.6], [1.4, 1.6, -1.2])
    c.screenshot("logs/creations/sandbox_afternoon.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/sandbox_afternoon.glb")
    print("Sandbox Afternoon complete.")


if __name__ == "__main__":
    main()
