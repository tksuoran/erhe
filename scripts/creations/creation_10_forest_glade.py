#!/usr/bin/env python3
"""Creation 10: Forest Glade.

A small sunlit forest clearing: a ring of L-system trees in two species
(gnarled oaks and slim pale birches, same stochastic string-rewriting +
3D-turtle interpreter with per-species parameters), dome bushes, mossy
boulders, fly-agaric mushroom clusters and a scatter of wildflowers on
the glade floor.
Showcases: parametric L-system vegetation, non-uniform node scaling
(squashed spheres as rocks / moss / mushroom caps), seeded scatter
composition, morning-forest lighting.
"""

import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, standard_args  # noqa: E402


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
    kv = v_cross(axis, v)
    kkv = v_cross(axis, kv)
    return [v[i] + s * kv[i] + (1.0 - c) * kkv[i] for i in range(3)]


def align_y_quaternion(direction):
    """Quaternion [x,y,z,w] rotating +Y onto direction."""
    d = v_norm(direction)
    dot = max(-1.0, min(1.0, d[1]))
    if dot > 0.99999:
        return None
    if dot < -0.99999:
        return [1.0, 0.0, 0.0, 0.0]
    axis = v_norm(v_cross([0.0, 1.0, 0.0], d))
    half = math.acos(dot) / 2.0
    s = math.sin(half)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half)]


# ----------------------------------------------------------------- L-system

OAK = {
    "iterations": 4,
    "weights": (0.55, 0.90),         # <w0: 3 branches, <w1: 2, else 1
    "trunk_h": 1.2, "trunk_r": (0.34, 0.22),
    "seg_len": 0.95, "len_falloff": 0.72,
    "seg_r": 0.20, "r_falloff": 0.60,
    "pitch": (26.0, 46.0), "wobble": 9.0,
    "tilt": [0.14, 0.98, 0.09],
    "cluster_r": 0.75, "leaf_r": (0.40, 0.13, 0.90),
}

BIRCH = {
    "iterations": 4,
    "weights": (0.30, 0.85),
    "trunk_h": 1.6, "trunk_r": (0.16, 0.12),
    "seg_len": 0.90, "len_falloff": 0.76,
    "seg_r": 0.10, "r_falloff": 0.62,
    "pitch": (18.0, 32.0), "wobble": 6.0,
    "tilt": [0.05, 0.995, 0.03],
    "cluster_r": 0.65, "leaf_r": (0.32, 0.11, 0.70),
}


def expand_lsystem(rng, iterations, weights):
    s = "A"
    for _ in range(iterations):
        out = []
        for ch in s:
            if ch != "A":
                out.append(ch)
                continue
            r = rng.random()
            if r < weights[0]:
                out.append("F[&A]/[&A]/[&A]")
            elif r < weights[1]:
                out.append("F[&A]/[&A]")
            else:
                out.append("F/[&A]")
        s = "".join(out)
    return s.replace("A", "L")


def grow_tree(c, tag, base, species, bark, leaf_materials, rng):
    """Stochastic L-system tree: trunk cone, turtle-driven branch cones,
    leaf tips greedily clustered into canopy spheres."""
    p = species
    c.shape("cone", f"{tag} Trunk", base, height=p["trunk_h"],
            bottom_radius=p["trunk_r"][0], top_radius=p["trunk_r"][1],
            slice_count=14, material_name=bark)

    segments = []
    leaves = []
    pos = v_add(base, [0.0, p["trunk_h"], 0.0])
    heading = v_norm(p["tilt"])
    left = v_norm(v_cross([0.0, 1.0, 0.0], heading)) if abs(heading[1]) < 0.999 else [1.0, 0.0, 0.0]
    left = v_norm(v_cross(heading, v_cross(left, heading)))
    depth = 0
    stack = []

    for ch in expand_lsystem(rng, p["iterations"], p["weights"]):
        if ch == "F":
            wob = math.radians(rng.uniform(-p["wobble"], p["wobble"]))
            heading = v_norm(v_rotate(heading, left, wob))
            length = p["seg_len"] * (p["len_falloff"] ** depth) * rng.uniform(0.85, 1.15)
            radius = max(0.03, p["seg_r"] * (p["r_falloff"] ** depth))
            segments.append((list(pos), list(heading), length, radius))
            pos = v_add(pos, v_scale(heading, length))
        elif ch == "&":
            angle = math.radians(rng.uniform(*p["pitch"]))
            heading = v_norm(v_rotate(heading, left, angle))
        elif ch == "/":
            angle = math.radians(rng.uniform(95.0, 145.0))
            left = v_norm(v_rotate(left, heading, angle))
        elif ch == "[":
            stack.append((list(pos), list(heading), list(left), depth))
            depth += 1
        elif ch == "]":
            pos, heading, left, depth = stack.pop()
        elif ch == "L":
            leaves.append(v_add(pos, v_scale(heading, 0.12)))

    for i, (start, direction, length, radius) in enumerate(segments):
        result = c.shape("cone", f"{tag} Branch {i}", start, height=length,
                         bottom_radius=radius, top_radius=max(0.025, radius * 0.7),
                         slice_count=8, material_name=bark)
        rotation = align_y_quaternion(direction)
        if rotation is not None:
            node_id = result.get("node_id") if isinstance(result, dict) else None
            if node_id is not None:
                c.move_node_id(node_id, rotation_xyzw=rotation)

    clusters = []
    merge_sq = p["cluster_r"] * p["cluster_r"]
    for point in leaves:
        for cluster in clusters:
            center, count = cluster
            d = [point[j] - center[j] for j in range(3)]
            if d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < merge_sq:
                cluster[1] = count + 1
                cluster[0] = [center[j] + d[j] / cluster[1] for j in range(3)]
                break
        else:
            clusters.append([list(point), 1])
    base_r, per, cap = p["leaf_r"]
    for i, (center, count) in enumerate(clusters):
        radius = min(base_r + per * math.sqrt(count), cap)
        jitter = [rng.uniform(-0.06, 0.06) for _ in range(3)]
        c.shape("uv_sphere", f"{tag} Canopy {i}", v_add(center, jitter),
                radius=radius, slice_count=12, stack_count=9,
                material_name=leaf_materials[i % len(leaf_materials)])
    print(f"{tag}: {len(segments)} segments, {len(clusters)} canopy clusters")


# ------------------------------------------------------------- undergrowth

def scaled_sphere(c, name, position, radius, scale, material, slices=14, stacks=10):
    result = c.shape("uv_sphere", name, position, radius=radius,
                     slice_count=slices, stack_count=stacks,
                     material_name=material)
    node_id = result.get("node_id") if isinstance(result, dict) else None
    if node_id is not None and scale != [1.0, 1.0, 1.0]:
        c.move_node_id(node_id, scale=scale)
    return node_id


def bush(c, tag, x, z, size, materials, rng):
    """Dome bush: 3-4 overlapping squashed spheres."""
    blobs = rng.randint(3, 4)
    for i in range(blobs):
        a = rng.uniform(0.0, 2.0 * math.pi)
        d = rng.uniform(0.0, 0.30) * size
        r = size * rng.uniform(0.55, 0.80)
        scaled_sphere(c, f"{tag} Blob {i}",
                      [x + d * math.cos(a), r * 0.55, z + d * math.sin(a)],
                      r, [1.0, 0.72, 1.0], materials[i % len(materials)])


def mossy_rock(c, tag, x, z, size, yaw, m, rng):
    """Boulder: squashed sphere sunk into the lawn + flattened moss cap."""
    node_id = scaled_sphere(c, f"{tag} Rock", [x, size * 0.26, z], size,
                            [1.0 + rng.uniform(-0.15, 0.25), 0.50, 1.0], m["rock"])
    if node_id is not None:
        c.move_node_id(node_id, rotation_xyzw=[0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)])
    scaled_sphere(c, f"{tag} Moss", [x + size * 0.12, size * 0.42, z - size * 0.08],
                  size * 0.82, [1.05, 0.32, 1.0], m["moss"])


def mushroom(c, tag, x, z, height, m, rng):
    """Fly agaric: cream stem, squashed red cap, white speckles."""
    cap_r = height * 0.62
    c.shape("cone", f"{tag} Stem", [x, 0.0, z], height=height,
            bottom_radius=height * 0.30, top_radius=height * 0.20,
            slice_count=10, material_name=m["cream"])
    scaled_sphere(c, f"{tag} Cap", [x, height * 1.02, z], cap_r,
                  [1.0, 0.60, 1.0], m["red"], slices=14, stacks=10)
    for i in range(3):
        a = rng.uniform(0.0, 2.0 * math.pi)
        d = cap_r * rng.uniform(0.35, 0.75)
        c.shape("uv_sphere", f"{tag} Dot {i}",
                [x + d * math.cos(a), height * 1.02 + cap_r * 0.42, z + d * math.sin(a)],
                radius=cap_r * 0.16, slice_count=8, stack_count=6,
                material_name=m["cream"])


def flower(c, tag, x, z, color, m, rng):
    """Wildflower: thin stem cone + colored head sphere."""
    h = rng.uniform(0.18, 0.32)
    c.shape("cone", f"{tag} Stem", [x, 0.0, z], height=h,
            bottom_radius=0.014, top_radius=0.010, slice_count=6,
            material_name=m["leaf2"])
    c.shape("uv_sphere", f"{tag} Head", [x, h + 0.035, z],
            radius=rng.uniform(0.04, 0.06), slice_count=10, stack_count=8,
            material_name=m[color])


# --------------------------------------------------------------------- main

def main():
    args = standard_args("Forest Glade")
    c = Creation("Forest Glade", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    c.ambience(ambient=[0.16, 0.20, 0.16],
               clear_color=[0.45, 0.62, 0.85, 1.0], grid=False,
               sky={"_version": 3, "enabled": True, "mode": 1})

    def mat(**edits):
        return c.make_material(clear_textures=True, **edits)

    m = {
        "grass":  mat(base_color=[0.14, 0.28, 0.08], roughness=1.0, metallic=0.0),
        "bark":   mat(base_color=[0.26, 0.17, 0.09], roughness=0.95, metallic=0.0),
        "birch":  mat(base_color=[0.82, 0.80, 0.72], roughness=0.8, metallic=0.0),
        "leaf":   mat(base_color=[0.12, 0.28, 0.06], roughness=0.9, metallic=0.0),
        "leaf2":  mat(base_color=[0.20, 0.36, 0.08], roughness=0.9, metallic=0.0),
        "rock":   mat(base_color=[0.38, 0.38, 0.40], roughness=0.9, metallic=0.0),
        "moss":   mat(base_color=[0.16, 0.33, 0.10], roughness=1.0, metallic=0.0),
        "red":    mat(base_color=[0.72, 0.08, 0.05], roughness=0.5, metallic=0.0),
        "cream":  mat(base_color=[0.90, 0.87, 0.78], roughness=0.7, metallic=0.0),
        "pink":   mat(base_color=[0.85, 0.30, 0.55], roughness=0.5, metallic=0.0),
        "yellow": mat(base_color=[0.92, 0.78, 0.10], roughness=0.5, metallic=0.0),
        "sky_blue": mat(base_color=[0.45, 0.60, 0.90], roughness=0.5, metallic=0.0),
    }

    rng = random.Random(7)

    c.shape("box", "Forest Floor", [0.0, -0.25, 0.0], size=[60.0, 0.5, 60.0],
            material_name=m["grass"])

    # Ring of trees around the clearing: oaks + birches.
    trees = [
        ("Oak A",   OAK,   [-3.6, 0.0, -3.4]),
        ("Oak B",   OAK,   [4.2, 0.0, -3.8]),
        ("Oak C",   OAK,   [-4.6, 0.0, 2.6]),
        ("Birch A", BIRCH, [1.2, 0.0, -5.2]),
        ("Birch B", BIRCH, [5.6, 0.0, -1.6]),
    ]
    for tag, species, base in trees:
        bark = m["birch"] if species is BIRCH else m["bark"]
        grow_tree(c, tag, base, species, bark, [m["leaf"], m["leaf2"]], rng)

    # Bushes filling the gaps between trunks.
    for i, (x, z, size) in enumerate([(-2.2, -4.6, 0.8), (2.6, -5.0, 0.7),
                                      (-5.2, 0.2, 0.9), (5.4, -0.6, 0.8),
                                      (-3.0, 3.8, 0.7), (2.0, 4.6, 0.9)]):
        bush(c, f"Bush {i}", x, z, size, [m["leaf"], m["leaf2"]], rng)

    # Mossy boulders.
    for i, (x, z, size) in enumerate([(-1.6, 1.8, 0.55), (1.9, 0.7, 0.75),
                                      (0.4, -2.6, 0.45), (3.2, 3.6, 0.5)]):
        mossy_rock(c, f"Boulder {i}", x, z, size, rng.uniform(0.0, math.pi), m, rng)

    # Fly agaric clusters at a boulder and an oak foot.
    for i, (x, z) in enumerate([(2.45, 1.25), (2.7, 0.9), (2.2, 0.55),
                                (-3.1, -2.9), (-3.5, -2.5),
                                (0.15, -2.15)]):
        mushroom(c, f"Mushroom {i}", x, z, rng.uniform(0.10, 0.18), m, rng)

    # Wildflower scatter across the open glade.
    colors = ["pink", "yellow", "cream", "sky_blue"]
    for i in range(16):
        a = rng.uniform(0.0, 2.0 * math.pi)
        d = rng.uniform(0.4, 3.4)
        flower(c, f"Flower {i}", d * math.cos(a), d * math.sin(a),
               colors[i % len(colors)], m, rng)

    # Morning sun slanting through the trees + soft green bounce.
    c.light("directional", "Morning Sun", [0.0, 12.0, 0.0],
            [1.0, 0.92, 0.75], 2.6)
    sun = c.node_by_name("Morning Sun")
    if sun is not None:
        pitch = math.radians(-142.0)
        yaw = math.radians(-55.0)
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
    c.light("point", "Green Bounce", [0.0, 3.5, 0.0], [0.55, 0.75, 0.45],
            45.0, range=20.0, cast_shadow=False)

    c.settle()
    c.place_camera([6.8, 3.2, 7.6], [0.0, 1.3, -0.8])
    c.screenshot("logs/creations/forest_glade.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/forest_glade.glb")
    print("Forest Glade complete.")


if __name__ == "__main__":
    main()
