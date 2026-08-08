#!/usr/bin/env python3
"""Creation 10: Forest Glade.

A small sunlit forest clearing: a ring of L-system trees in two species
(gnarled oaks and slim pale birches, same stochastic string-rewriting +
3D-turtle interpreter with per-species parameters), L-system wildflowers
(segmented stems, bracketed leaves, side-bloom stalks, petal-ring
blooms), L-system ferns (each frond a turtle-drawn rachis of tapering
segments drooping under gravity, with paired pinna blades at every node
and a tip leaflet), dome bushes, mossy boulders, fly-agaric mushroom
clusters and a fallen mossy log with its stump.
Showcases: parametric L-system vegetation at three scales (trees,
ferns, flowers), non-uniform node scaling (rocks / moss / petals /
pinnae), seeded scatter composition, morning-forest lighting.
Every plant / boulder / prop is one subtree under its own group node
(create_shape parent_node_id + world positions), so the scene hierarchy
reads: Oak A, Bush 3, Fern 1, Fallen Tree (with its mushrooms nested).
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
    leaf tips greedily clustered into canopy spheres. All parts are
    children of one group node so the scene graph has one root per tree."""
    p = species
    root = c.group(tag, base)
    c.shape("cone", f"{tag} Trunk", base, height=p["trunk_h"],
            bottom_radius=p["trunk_r"][0], top_radius=p["trunk_r"][1],
            slice_count=14, material_name=bark, parent_node_id=root)

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
                         slice_count=8, material_name=bark, parent_node_id=root)
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
                material_name=leaf_materials[i % len(leaf_materials)],
                parent_node_id=root)
    print(f"{tag}: {len(segments)} segments, {len(clusters)} canopy clusters")


# ------------------------------------------------------------- undergrowth

def scaled_sphere(c, name, position, radius, scale, material, slices=14, stacks=10,
                  parent=None):
    result = c.shape("uv_sphere", name, position, radius=radius,
                     slice_count=slices, stack_count=stacks,
                     material_name=material, parent_node_id=parent)
    node_id = result.get("node_id") if isinstance(result, dict) else None
    if node_id is not None and scale != [1.0, 1.0, 1.0]:
        c.move_node_id(node_id, scale=scale)
    return node_id


def bush(c, tag, x, z, size, materials, rng):
    """Dome bush: 3-4 overlapping squashed spheres under one group node."""
    root = c.group(tag, [x, 0.0, z])
    blobs = rng.randint(3, 4)
    for i in range(blobs):
        a = rng.uniform(0.0, 2.0 * math.pi)
        d = rng.uniform(0.0, 0.30) * size
        r = size * rng.uniform(0.55, 0.80)
        scaled_sphere(c, f"{tag} Blob {i}",
                      [x + d * math.cos(a), r * 0.55, z + d * math.sin(a)],
                      r, [1.0, 0.72, 1.0], materials[i % len(materials)],
                      parent=root)


def mossy_rock(c, tag, x, z, size, yaw, m, rng):
    """Boulder: squashed sphere sunk into the lawn + flattened moss cap,
    both under one group node."""
    root = c.group(tag, [x, 0.0, z])
    node_id = scaled_sphere(c, f"{tag} Rock", [x, size * 0.26, z], size,
                            [1.0 + rng.uniform(-0.15, 0.25), 0.50, 1.0], m["rock"],
                            parent=root)
    if node_id is not None:
        c.move_node_id(node_id, rotation_xyzw=[0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)])
    scaled_sphere(c, f"{tag} Moss", [x + size * 0.12, size * 0.42, z - size * 0.08],
                  size * 0.82, [1.05, 0.32, 1.0], m["moss"], parent=root)


def mushroom(c, tag, x, z, height, m, rng, y0=0.0, parent=None):
    """Fly agaric: cream stem, squashed red cap, white speckles, grouped
    under one node (which may itself be a child, e.g. of the fallen log)."""
    root = c.group(tag, [x, y0, z], parent_node_id=parent)
    cap_r = height * 0.62
    c.shape("cone", f"{tag} Stem", [x, y0, z], height=height,
            bottom_radius=height * 0.30, top_radius=height * 0.20,
            slice_count=10, material_name=m["cream"], parent_node_id=root)
    scaled_sphere(c, f"{tag} Cap", [x, y0 + height * 1.02, z], cap_r,
                  [1.0, 0.60, 1.0], m["red"], slices=14, stacks=10, parent=root)
    for i in range(3):
        a = rng.uniform(0.0, 2.0 * math.pi)
        d = cap_r * rng.uniform(0.35, 0.75)
        c.shape("uv_sphere", f"{tag} Dot {i}",
                [x + d * math.cos(a), y0 + height * 1.02 + cap_r * 0.42, z + d * math.sin(a)],
                radius=cap_r * 0.16, slice_count=8, stack_count=6,
                material_name=m["cream"], parent_node_id=root)


def blade(c, name, position, radius, scale, direction, material, parent=None):
    """Elongated squashed sphere aligned so its long (local Y) axis points
    along direction: leaf blades, petals, cut faces. Scale FIRST, then
    rotate (each transform_selection call is a separate edit)."""
    result = c.shape("uv_sphere", name, position, radius=radius,
                     slice_count=10, stack_count=8, material_name=material,
                     parent_node_id=parent)
    node_id = result.get("node_id") if isinstance(result, dict) else None
    if node_id is None:
        return
    c.move_node_id(node_id, scale=scale)
    rotation = align_y_quaternion(direction)
    if rotation is not None:
        c.move_node_id(node_id, rotation_xyzw=rotation)


def expand_flower(rng, iterations=3):
    """Flower L-system: a stem apex grows a segment then a bracketed leaf,
    a bracketed side-bloom stalk, or nothing; every surviving apex blooms."""
    s = "A"
    for _ in range(iterations):
        out = []
        for ch in s:
            if ch != "A":
                out.append(ch)
                continue
            r = rng.random()
            if r < 0.50:
                out.append("F[L]/A")
            elif r < 0.75:
                out.append("F[&FK]/A")
            else:
                out.append("FA")
        s = "".join(out)
    return s.replace("A", "K")


def flower(c, tag, x, z, color, m, rng):
    """L-system wildflower: turtle-drawn stem segments, leaf blades at
    the bracketed Ls, petal-ring blooms (center + 5 aligned petals) at
    every K. One group node per plant."""
    root = c.group(tag, [x, 0.0, z])
    pos = [x, 0.0, z]
    heading = v_norm([rng.uniform(-0.12, 0.12), 1.0, rng.uniform(-0.12, 0.12)])
    left = v_norm(v_cross([0.0, 1.0, 0.0], heading)) if abs(heading[1]) < 0.999 else [1.0, 0.0, 0.0]
    stack = []
    seg = 0

    for ch in expand_flower(rng):
        if ch == "F":
            wob = math.radians(rng.uniform(-7.0, 7.0))
            heading = v_norm(v_rotate(heading, left, wob))
            length = rng.uniform(0.09, 0.14)
            result = c.shape("cone", f"{tag} Stem {seg}", list(pos),
                             height=length, bottom_radius=0.013,
                             top_radius=0.010, slice_count=6,
                             material_name=m["leaf2"], parent_node_id=root)
            rotation = align_y_quaternion(heading)
            if rotation is not None:
                node_id = result.get("node_id") if isinstance(result, dict) else None
                if node_id is not None:
                    c.move_node_id(node_id, rotation_xyzw=rotation)
            pos = v_add(pos, v_scale(heading, length))
            seg += 1
        elif ch == "L":
            leaf_dir = v_norm(v_rotate(heading, left, math.radians(rng.uniform(55.0, 75.0))))
            blade(c, f"{tag} Leaf {seg}", v_add(pos, v_scale(leaf_dir, 0.05)),
                  0.05, [0.50, 1.6, 0.22], leaf_dir, m["leaf2"], parent=root)
        elif ch == "&":
            heading = v_norm(v_rotate(heading, left, math.radians(rng.uniform(35.0, 55.0))))
        elif ch == "/":
            left = v_norm(v_rotate(left, heading, math.radians(rng.uniform(110.0, 150.0))))
        elif ch == "[":
            stack.append((list(pos), list(heading), list(left)))
        elif ch == "]":
            pos, heading, left = stack.pop()
        elif ch == "K":
            c.shape("uv_sphere", f"{tag} Center {seg}",
                    v_add(pos, v_scale(heading, 0.02)), radius=0.032,
                    slice_count=8, stack_count=6, material_name=m["yellow"],
                    parent_node_id=root)
            for i in range(5):
                ring = v_rotate(left, heading, math.radians(i * 72.0 + rng.uniform(-8.0, 8.0)))
                petal_dir = v_norm(v_add(ring, v_scale(heading, 0.45)))
                blade(c, f"{tag} Petal {seg}.{i}",
                      v_add(pos, v_scale(petal_dir, 0.055)),
                      0.045, [0.55, 1.35, 0.30], petal_dir, m[color], parent=root)
            seg += 1


def expand_fern(rng, iterations=4):
    """Frond L-system: the rachis apex grows a segment bearing a pinna
    pair (usually), a single left or right pinna otherwise; the surviving
    apex becomes the tip leaflet."""
    s = "A"
    for _ in range(iterations):
        out = []
        for ch in s:
            if ch != "A":
                out.append(ch)
                continue
            r = rng.random()
            if r < 0.70:
                out.append("F[+L][-L]A")
            elif r < 0.85:
                out.append("F[+L]A")
            else:
                out.append("F[-L]A")
        s = "".join(out)
    return s.replace("A", "L")


def fern(c, tag, x, z, size, m, rng):
    """L-system fern rosette: each frond is a turtle-drawn rachis of
    tapering cone segments that droops a little more with every step,
    with paired pinna blades at the bracketed nodes and a tip leaflet.
    One group node per fern."""
    root = c.group(tag, [x, 0.0, z])
    fronds = 5
    base = [x, 0.04, z]
    for i in range(fronds):
        a = 2.0 * math.pi * i / fronds + rng.uniform(-0.3, 0.3)
        out_dir = [math.cos(a), 0.0, math.sin(a)]
        side = [-out_dir[2], 0.0, out_dir[0]]
        heading = v_norm([out_dir[0] * 0.65, 1.0, out_dir[2] * 0.65])
        pos = list(base)
        t = 0
        pending = 0
        for ch in expand_fern(rng):
            if ch == "F":
                seg = size * 0.30 * (0.88 ** t) * rng.uniform(0.9, 1.1)
                radius = size * 0.020 * (1.0 - 0.10 * t)
                result = c.shape("cone", f"{tag} Rachis {i}.{t}", list(pos),
                                 height=seg, bottom_radius=radius,
                                 top_radius=radius * 0.8, slice_count=6,
                                 material_name=m["leaf2"], parent_node_id=root)
                rotation = align_y_quaternion(heading)
                if rotation is not None:
                    node_id = result.get("node_id") if isinstance(result, dict) else None
                    if node_id is not None:
                        c.move_node_id(node_id, rotation_xyzw=rotation)
                pos = v_add(pos, v_scale(heading, seg * 0.95))
                # gravity droop: pitch outward-down a little more each step
                heading = v_norm(v_add(heading, [out_dir[0] * 0.15, -0.18, out_dir[2] * 0.15]))
                t += 1
            elif ch == "+":
                pending = 1
            elif ch == "-":
                pending = -1
            elif ch == "L":
                if pending == 0:
                    pinna_dir = heading
                else:
                    pinna_dir = v_norm(v_add(v_scale(side, float(pending)),
                                             v_scale(heading, 0.55)))
                length = size * 0.15 * (1.0 - 0.11 * t) * rng.uniform(0.85, 1.15)
                blade(c, f"{tag} Pinna {i}.{t}.{pending:+d}",
                      v_add(pos, v_scale(pinna_dir, length * 0.9)),
                      length, [0.42, 1.55, 0.16], pinna_dir,
                      m["leaf"] if (t + pending) % 2 else m["leaf2"],
                      parent=root)
                pending = 0


def fallen_log(c, m, rng):
    """Fallen mossy tree: horizontal tapered trunk, cut-face discs, its
    stump, stub branches, moss patches and a pair of log-top mushrooms,
    all under one 'Fallen Tree' group node."""
    start = [-2.7, 0.26, 3.0]
    d = v_norm([2.9, 0.02, 1.1])
    length = 3.1
    root = c.group("Fallen Tree", [start[0], 0.0, start[2]])
    result = c.shape("cone", "Fallen Log", start, height=length,
                     bottom_radius=0.27, top_radius=0.19, slice_count=14,
                     material_name=m["bark"], parent_node_id=root)
    node_id = result.get("node_id") if isinstance(result, dict) else None
    if node_id is not None:
        c.move_node_id(node_id, rotation_xyzw=align_y_quaternion(d))
    end = v_add(start, v_scale(d, length))
    blade(c, "Log Cut Face", end, 0.19, [1.0, 0.16, 1.0], d, m["cream"],
          parent=root)
    # Stump the log broke off from, with a pale cut face.
    stump = v_add(start, v_scale(d, -0.8))
    c.shape("cone", "Log Stump", [stump[0], 0.0, stump[2]], height=0.5,
            bottom_radius=0.30, top_radius=0.27, slice_count=14,
            material_name=m["bark"], parent_node_id=root)
    scaled_sphere(c, "Stump Cut Face", [stump[0], 0.5, stump[2]], 0.26,
                  [1.0, 0.14, 1.0], m["cream"], slices=12, stacks=8,
                  parent=root)
    # Stub branches poking up from the log.
    for i, f in enumerate((0.9, 1.7, 2.4)):
        p = v_add(start, v_scale(d, f))
        stub_dir = v_norm([rng.uniform(-0.4, 0.4), 1.0, rng.uniform(-0.5, 0.1)])
        result = c.shape("cone", f"Log Stub {i}", [p[0], p[1] + 0.12, p[2]],
                         height=rng.uniform(0.30, 0.45), bottom_radius=0.06,
                         top_radius=0.025, slice_count=8, material_name=m["bark"],
                         parent_node_id=root)
        node_id = result.get("node_id") if isinstance(result, dict) else None
        if node_id is not None:
            c.move_node_id(node_id, rotation_xyzw=align_y_quaternion(stub_dir))
    # Moss saddles along the top.
    for i, f in enumerate((0.5, 1.4, 2.2)):
        p = v_add(start, v_scale(d, f))
        scaled_sphere(c, f"Log Moss {i}", [p[0], p[1] + 0.18, p[2]],
                      0.22, [1.4, 0.30, 0.85], m["moss"], slices=10, stacks=8,
                      parent=root)
    # Mushrooms colonizing the log (grouped under the fallen tree).
    log_top = start[1] + 0.20
    for i, f in enumerate((1.15, 1.95)):
        p = v_add(start, v_scale(d, f))
        mushroom(c, f"Log Mushroom {i}", p[0] + 0.06, p[2] - 0.06,
                 rng.uniform(0.09, 0.13), m, rng, y0=log_top, parent=root)


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

    # Fallen mossy tree along the front edge of the clearing.
    fallen_log(c, m, rng)

    # L-system fern rosettes in the tree shade.
    for i, (x, z, size) in enumerate([(-2.6, -1.8, 1.15), (3.3, -2.4, 1.0),
                                      (0.9, 2.6, 0.9)]):
        fern(c, f"Fern {i}", x, z, size, m, rng)

    # L-system wildflowers across the open glade.
    colors = ["pink", "yellow", "cream", "sky_blue"]
    for i in range(8):
        a = rng.uniform(0.0, 2.0 * math.pi)
        d = rng.uniform(0.5, 2.8)
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
