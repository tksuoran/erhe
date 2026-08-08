#!/usr/bin/env python3
"""Creation 13: Windswept Glade.

The Forest Glade (creation 10) rebuilt with live physics in the
vegetation and a wind field over the scene: every tree sways from its
trunk, every fern frond and every wildflower stem bends from its base -
all driven by six-dof rest-pose motor joints (position target 0 =
authored pose, max_force = yield) plus the per-scene wind system
(Physics_config v2 + Node_physics wind_receptivity).
Showcases: rest-pose motor joints as plant spines, the wind force field
(traveling gusts - plants a wavelength apart move out of phase),
one-body-per-plant physics LOD (the whole canopy rides its trunk node),
and motion_mode="none" visual parts that neither collide nor cost
bodies.

Physics rig pattern (per swaying element):
- visual parts are created with motion_mode="none" (no rigid body at
  all - a static child body would grind against the dynamic spine),
- the spine node (trunk / frond rachis segment 0 / stem segment 0)
  gets create_physics_body shape="auto" (hull of its own mesh),
  gravity_factor 0, explicit mass, wind_receptivity,
- a coincident anchor child at the spine base joints it to the WORLD
  with shared rest-pose motor settings (linear + angular-Y locked,
  angular X/Z limited, position-target-0 drives),
- everything is built with the simulation DISABLED so the joints
  capture the authored pose as the rest pose, then physics + wind are
  enabled and the bodies woken.
"""

import math
import os
import random
import sys
import time

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

# Rest-pose motor joint settings shared per plant scale. Linear XYZ and
# angular Y locked, angular X/Z limited; drives spring back to the pose
# captured at joint creation. Trees are stiff and heavy, fronds loose,
# flower stems looser still.
SWAY_SETTINGS = {
    "tree_sway":  {"range": 0.12, "stiffness": 300.0, "damping": 30.0, "max_force": 600.0},
    # Frond dynamics: only the spine hull carries mass, so a light spine +
    # stiff spring rings at several Hz (jittery). A heavier spine and softer
    # spring put the natural frequency ~1.5 Hz; body angular damping kills
    # the residual ring. The same sizing rule (aim omega = sqrt(k / I) at
    # 1-2 Hz via explicit mass) applies to every plant scale below.
    "frond_sway":   {"range": 0.35, "stiffness": 1.5,   "damping": 0.25, "max_force": 8.0},
    "stem_sway":    {"range": 0.50, "stiffness": 0.8,   "damping": 0.08, "max_force": 3.0},
    "grass_sway":   {"range": 0.60, "stiffness": 0.3,   "damping": 0.03, "max_force": 2.0},
    "curtain_sway": {"range": 0.60, "stiffness": 0.5,   "damping": 0.06, "max_force": 5.0},
}


def make_sway_settings(c):
    for name, p in SWAY_SETTINGS.items():
        c.joint_settings(
            name,
            limits=[
                {"linear_axes": [True, True, True], "angular_axes": [False, False, False], "min": 0.0, "max": 0.0},
                {"linear_axes": [False, False, False], "angular_axes": [False, True, False], "min": 0.0, "max": 0.0},
                {"linear_axes": [False, False, False], "angular_axes": [True, False, False], "min": -p["range"], "max": p["range"]},
                {"linear_axes": [False, False, False], "angular_axes": [False, False, True], "min": -p["range"], "max": p["range"]},
            ],
            drives=[
                {"type": "angular", "axis": 0, "stiffness": p["stiffness"], "damping": p["damping"], "max_force": p["max_force"], "position_target": 0.0},
                {"type": "angular", "axis": 2, "stiffness": p["stiffness"], "damping": p["damping"], "max_force": p["max_force"], "position_target": 0.0},
            ],
        )


def rig_sway(c, sway_jobs):
    """Turn each collected spine node into a wind-receptive dynamic body
    jointed to the world at its base. Run AFTER settle() with the
    simulation disabled, so the joints capture the authored rest pose."""
    for tag, node_id, base_pos, settings, mass, receptivity, ang_damp in sway_jobs:
        c.body(node_id, shape="auto", motion_mode="dynamic", mass=mass,
               gravity_factor=0.0, angular_damping=ang_damp,
               linear_damping=0.05, wind_receptivity=receptivity)
        anchor_id = c.anchor(f"{tag} Sway Anchor", node_id, base_pos)
        c.joint(anchor_id, settings_name=settings)
    print(f"rigged {len(sway_jobs)} sway spines")


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


def grow_tree(c, tag, base, species, bark, leaf_materials, rng, sway_jobs):
    """Stochastic L-system tree, graph mirroring the bracket structure.
    Every part is motion_mode="none"; the trunk is the sway spine (the
    whole branch/canopy subtree rides its node), collected in sway_jobs."""
    p = species
    root = c.group(tag, base)
    result = c.shape("cone", f"{tag} Trunk", base, height=p["trunk_h"],
                     bottom_radius=p["trunk_r"][0], top_radius=p["trunk_r"][1],
                     slice_count=14, material_name=bark, parent_node_id=root,
                     motion_mode="none")
    trunk_id = result.get("node_id") if isinstance(result, dict) else None
    parent = trunk_id if trunk_id is not None else root
    if trunk_id is not None:
        sway_jobs.append((tag, trunk_id, list(base), "tree_sway", 25.0, 6.0, 0.8))

    leaves = []                         # (tip position, carrying branch id)
    seg_count = 0
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
            result = c.shape("cone", f"{tag} Branch {seg_count}", list(pos),
                             height=length, bottom_radius=radius,
                             top_radius=max(0.025, radius * 0.7),
                             slice_count=8, material_name=bark,
                             parent_node_id=parent, motion_mode="none")
            node_id = result.get("node_id") if isinstance(result, dict) else None
            rotation = align_y_quaternion(heading)
            if node_id is not None:
                if rotation is not None:
                    c.move_node_id(node_id, rotation_xyzw=rotation)
                parent = node_id
            seg_count += 1
            pos = v_add(pos, v_scale(heading, length))
        elif ch == "&":
            angle = math.radians(rng.uniform(*p["pitch"]))
            heading = v_norm(v_rotate(heading, left, angle))
        elif ch == "/":
            angle = math.radians(rng.uniform(95.0, 145.0))
            left = v_norm(v_rotate(left, heading, angle))
        elif ch == "[":
            stack.append((list(pos), list(heading), list(left), depth, parent))
            depth += 1
        elif ch == "]":
            pos, heading, left, depth, parent = stack.pop()
        elif ch == "L":
            leaves.append((v_add(pos, v_scale(heading, 0.12)), parent))

    clusters = []                       # [centroid, count, contributor counts]
    merge_sq = p["cluster_r"] * p["cluster_r"]
    for point, carrier in leaves:
        for cluster in clusters:
            center, count, carriers = cluster
            d = [point[j] - center[j] for j in range(3)]
            if d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < merge_sq:
                cluster[1] = count + 1
                cluster[0] = [center[j] + d[j] / cluster[1] for j in range(3)]
                carriers[carrier] = carriers.get(carrier, 0) + 1
                break
        else:
            clusters.append([list(point), 1, {carrier: 1}])
    base_r, per, cap = p["leaf_r"]
    for i, (center, count, carriers) in enumerate(clusters):
        radius = min(base_r + per * math.sqrt(count), cap)
        jitter = [rng.uniform(-0.06, 0.06) for _ in range(3)]
        carrier = max(carriers.items(), key=lambda kv: kv[1])[0]
        c.shape("uv_sphere", f"{tag} Canopy {i}", v_add(center, jitter),
                radius=radius, slice_count=12, stack_count=9,
                material_name=leaf_materials[i % len(leaf_materials)],
                parent_node_id=carrier, motion_mode="none")
    print(f"{tag}: {seg_count} segments, {len(clusters)} canopy clusters")


# ------------------------------------------------------------- undergrowth

def scaled_sphere(c, name, position, radius, scale, material, slices=14, stacks=10,
                  parent=None, motion_mode="static"):
    result = c.shape("uv_sphere", name, position, radius=radius,
                     slice_count=slices, stack_count=stacks,
                     material_name=material, parent_node_id=parent,
                     motion_mode=motion_mode)
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
                  size * 0.82, [1.05, 0.32, 1.0], m["moss"],
                  parent=node_id if node_id is not None else root)


def mushroom(c, tag, x, z, height, m, rng, y0=0.0, parent=None):
    """Fly agaric, fully nested: group > stem > cap > speckles."""
    root = c.group(tag, [x, y0, z], parent_node_id=parent)
    cap_r = height * 0.62
    result = c.shape("cone", f"{tag} Stem", [x, y0, z], height=height,
                     bottom_radius=height * 0.30, top_radius=height * 0.20,
                     slice_count=10, material_name=m["cream"], parent_node_id=root)
    stem_id = result.get("node_id") if isinstance(result, dict) else None
    cap_id = scaled_sphere(c, f"{tag} Cap", [x, y0 + height * 1.02, z], cap_r,
                           [1.0, 0.60, 1.0], m["red"], slices=14, stacks=10,
                           parent=stem_id if stem_id is not None else root)
    dot_parent = cap_id if cap_id is not None else root
    for i in range(3):
        a = rng.uniform(0.0, 2.0 * math.pi)
        d = cap_r * rng.uniform(0.35, 0.75)
        c.shape("uv_sphere", f"{tag} Dot {i}",
                [x + d * math.cos(a), y0 + height * 1.02 + cap_r * 0.42, z + d * math.sin(a)],
                radius=cap_r * 0.16, slice_count=8, stack_count=6,
                material_name=m["cream"], parent_node_id=dot_parent)


def blade(c, name, position, radius, scale, direction, material, parent=None):
    """Elongated squashed sphere aligned along direction (leaf blades,
    petals, cut faces); scale FIRST, then rotate. Pure visual."""
    result = c.shape("uv_sphere", name, position, radius=radius,
                     slice_count=10, stack_count=8, material_name=material,
                     parent_node_id=parent, motion_mode="none")
    node_id = result.get("node_id") if isinstance(result, dict) else None
    if node_id is None:
        return
    c.move_node_id(node_id, scale=scale)
    rotation = align_y_quaternion(direction)
    if rotation is not None:
        c.move_node_id(node_id, rotation_xyzw=rotation)


def expand_flower(rng, iterations=3):
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


def flower(c, tag, x, z, color, m, rng, sway_jobs,
           petal_count=5, petal_len=1.35, bloom_r=0.032, iterations=3):
    """L-system wildflower; stem segment 0 is the sway spine (the whole
    plant above it is motion_mode="none" and rides its node). Variety
    comes from geometry - petal count / length, bloom size, stem
    iterations - since the 12-material pool is already fully claimed."""
    base = [x, 0.03, z]
    root = c.group(tag, [x, 0.0, z])
    parent = root
    pos = list(base)
    heading = v_norm([rng.uniform(-0.12, 0.12), 1.0, rng.uniform(-0.12, 0.12)])
    left = v_norm(v_cross([0.0, 1.0, 0.0], heading)) if abs(heading[1]) < 0.999 else [1.0, 0.0, 0.0]
    stack = []
    seg = 0

    for ch in expand_flower(rng, iterations):
        if ch == "F":
            wob = math.radians(rng.uniform(-7.0, 7.0))
            heading = v_norm(v_rotate(heading, left, wob))
            length = rng.uniform(0.09, 0.14)
            result = c.shape("cone", f"{tag} Stem {seg}", list(pos),
                             height=length, bottom_radius=0.013,
                             top_radius=0.010, slice_count=6,
                             material_name=m["leaf2"], parent_node_id=parent,
                             motion_mode="none")
            rotation = align_y_quaternion(heading)
            node_id = result.get("node_id") if isinstance(result, dict) else None
            if node_id is not None:
                if rotation is not None:
                    c.move_node_id(node_id, rotation_xyzw=rotation)
                if seg == 0:
                    sway_jobs.append((tag, node_id, list(pos), "stem_sway", 0.03, 0.35, 0.10))
                parent = node_id
            pos = v_add(pos, v_scale(heading, length))
            seg += 1
        elif ch == "L":
            leaf_dir = v_norm(v_rotate(heading, left, math.radians(rng.uniform(55.0, 75.0))))
            blade(c, f"{tag} Leaf {seg}", v_add(pos, v_scale(leaf_dir, 0.05)),
                  0.05, [0.50, 1.6, 0.22], leaf_dir, m["leaf2"], parent=parent)
        elif ch == "&":
            heading = v_norm(v_rotate(heading, left, math.radians(rng.uniform(35.0, 55.0))))
        elif ch == "/":
            left = v_norm(v_rotate(left, heading, math.radians(rng.uniform(110.0, 150.0))))
        elif ch == "[":
            stack.append((list(pos), list(heading), list(left), parent))
        elif ch == "]":
            pos, heading, left, parent = stack.pop()
        elif ch == "K":
            center_color = m["cream"] if color == "yellow" else m["yellow"]
            result = c.shape("uv_sphere", f"{tag} Center {seg}",
                             v_add(pos, v_scale(heading, 0.02)), radius=bloom_r,
                             slice_count=8, stack_count=6, material_name=center_color,
                             parent_node_id=parent, motion_mode="none")
            center_id = result.get("node_id") if isinstance(result, dict) else None
            petal_parent = center_id if center_id is not None else parent
            step = 360.0 / petal_count
            for i in range(petal_count):
                ring = v_rotate(left, heading, math.radians(i * step + rng.uniform(-8.0, 8.0)))
                petal_dir = v_norm(v_add(ring, v_scale(heading, 0.45)))
                blade(c, f"{tag} Petal {seg}.{i}",
                      v_add(pos, v_scale(petal_dir, 0.055)),
                      0.045, [0.55, petal_len, 0.30], petal_dir, m[color],
                      parent=petal_parent)
            seg += 1


def expand_fern(rng, iterations=6):
    s = "A"
    for _ in range(iterations):
        out = []
        for ch in s:
            if ch != "A":
                out.append(ch)
                continue
            r = rng.random()
            if r < 0.88:
                out.append("F[+L][-L]A")
            else:
                out.append("FA")
        s = "".join(out)
    return s.replace("A", "L")


def compound_pinna(c, name, pos, direction, length, parent, materials, rng):
    """Second L-system level: midrib + pinnule pair + tip pinnule."""
    d = v_norm(direction)
    midrib_len = length * 2.1
    result = c.shape("cone", f"{name} Midrib", list(pos), height=midrib_len,
                     bottom_radius=length * 0.09, top_radius=length * 0.05,
                     slice_count=6, material_name=materials[1],
                     parent_node_id=parent, motion_mode="none")
    node_id = result.get("node_id") if isinstance(result, dict) else None
    rotation = align_y_quaternion(d)
    if node_id is not None and rotation is not None:
        c.move_node_id(node_id, rotation_xyzw=rotation)
    mid_parent = node_id if node_id is not None else parent
    sperp = v_cross(d, [0.0, 1.0, 0.0])
    if abs(sperp[0]) + abs(sperp[1]) + abs(sperp[2]) < 1e-4:
        sperp = [1.0, 0.0, 0.0]
    sperp = v_norm(sperp)
    mid_point = v_add(pos, v_scale(d, midrib_len * 0.5))
    plen = length * 0.62
    for s in (-1.0, 1.0):
        pdir = v_norm(v_add(v_scale(sperp, s), v_scale(d, 0.5)))
        blade(c, f"{name} Pinnule {s:+.0f}", v_add(mid_point, v_scale(pdir, plen * 0.8)),
              plen, [0.45, 1.5, 0.18], pdir, materials[0], parent=mid_parent)
    blade(c, f"{name} Pinnule Tip", v_add(pos, v_scale(d, midrib_len * 0.95)),
          plen * 0.9, [0.45, 1.5, 0.18], d, materials[0], parent=mid_parent)


def fern(c, tag, x, z, size, m, rng, sway_jobs):
    """Two-level L-system fern rosette. Each frond's first rachis segment
    is a sway spine; the rest of the frond rides it as visual children."""
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
        parent = root
        for ch in expand_fern(rng):
            if ch == "F":
                seg = size * 0.20 * (0.92 ** t) * rng.uniform(0.9, 1.1)
                radius = size * 0.018 * (1.0 - 0.07 * t)
                result = c.shape("cone", f"{tag} Rachis {i}.{t}", list(pos),
                                 height=seg, bottom_radius=radius,
                                 top_radius=radius * 0.8, slice_count=6,
                                 material_name=m["leaf2"], parent_node_id=parent,
                                 motion_mode="none")
                rotation = align_y_quaternion(heading)
                node_id = result.get("node_id") if isinstance(result, dict) else None
                if node_id is not None:
                    if rotation is not None:
                        c.move_node_id(node_id, rotation_xyzw=rotation)
                    if t == 0:
                        sway_jobs.append((f"{tag} Frond {i}", node_id, list(pos),
                                          "frond_sway", 0.8, 0.22, 0.8))
                    parent = node_id
                pos = v_add(pos, v_scale(heading, seg * 0.95))
                # gravity droop: pitch outward-down a little more each step
                heading = v_norm(v_add(heading, [out_dir[0] * 0.10, -0.12, out_dir[2] * 0.10]))
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
                length = size * 0.16 * (1.0 - 0.075 * t) * rng.uniform(0.85, 1.15)
                mats = (m["leaf"] if (t + pending) % 2 else m["leaf2"], m["leaf2"])
                if t <= 1:
                    compound_pinna(c, f"{tag} Pinna {i}.{t}.{pending:+d}",
                                   pos, pinna_dir, length, parent, mats, rng)
                else:
                    blade(c, f"{tag} Pinna {i}.{t}.{pending:+d}",
                          v_add(pos, v_scale(pinna_dir, length * 0.9)),
                          length, [0.32, 1.55, 0.13], pinna_dir, mats[0],
                          parent=parent)
                pending = 0


def grass_tuft(c, tag, x, z, size, m, rng, sway_jobs):
    """Grass tuft: a central spine blade (the sway body) with 3-5 fanned
    blade cones riding it as visual children. Whole tuft bends together."""
    root = c.group(tag, [x, 0.0, z])
    base = [x, 0.02, z]
    h = size * rng.uniform(0.9, 1.2)
    result = c.shape("cone", f"{tag} Spine", base, height=h,
                     bottom_radius=0.012, top_radius=0.001, slice_count=3,
                     material_name=m["leaf2"], parent_node_id=root,
                     motion_mode="none")
    spine_id = result.get("node_id") if isinstance(result, dict) else None
    if spine_id is None:
        return
    sway_jobs.append((tag, spine_id, base, "grass_sway", 0.10, 0.12, 0.4))
    for i in range(rng.randint(3, 5)):
        a = rng.uniform(0.0, 2.0 * math.pi)
        tilt = math.radians(rng.uniform(14.0, 38.0))
        d = v_norm([math.sin(tilt) * math.cos(a), math.cos(tilt), math.sin(tilt) * math.sin(a)])
        result = c.shape("cone", f"{tag} Blade {i}", base,
                         height=h * rng.uniform(0.65, 1.0), bottom_radius=0.010,
                         top_radius=0.001, slice_count=3,
                         material_name=m["leaf" if i % 2 else "leaf2"],
                         parent_node_id=spine_id, motion_mode="none")
        node_id = result.get("node_id") if isinstance(result, dict) else None
        rotation = align_y_quaternion(d)
        if node_id is not None and rotation is not None:
            c.move_node_id(node_id, rotation_xyzw=rotation)


def willow(c, tag, base, m, rng, sway_jobs):
    """Weeping willow: static-looking trunk + arching branches (all
    motion_mode="none"), with a swinging leaf CURTAIN at each branch tip.
    Each curtain root is a small dynamic body jointed to a coincident
    anchor on its branch - NOT to the world - so the curtains swing in
    the wind while staying attached to the tree. The long hanging
    strands are elongated blade spheres riding the curtain root."""
    x, y, z = base
    root = c.group(tag, base)
    trunk_h = 2.3
    result = c.shape("cone", f"{tag} Trunk", base, height=trunk_h,
                     bottom_radius=0.30, top_radius=0.20, slice_count=12,
                     material_name=m["bark"], parent_node_id=root,
                     motion_mode="none")
    trunk_id = result.get("node_id") if isinstance(result, dict) else None
    on_trunk = trunk_id if trunk_id is not None else root
    top = [x, y + trunk_h, z]
    # Leafy crown mass so the tree does not read as bare branches.
    for i in range(3):
        ca = 2.0 * math.pi * i / 3.0 + rng.uniform(-0.4, 0.4)
        c.shape("uv_sphere", f"{tag} Crown {i}",
                [x + 0.45 * math.cos(ca), y + trunk_h + 0.35, z + 0.45 * math.sin(ca)],
                radius=rng.uniform(0.5, 0.65), slice_count=12, stack_count=9,
                material_name=m["leaf" if i % 2 else "leaf2"],
                parent_node_id=on_trunk, motion_mode="none")
    curtain_count = 6
    for i in range(curtain_count):
        a = 2.0 * math.pi * i / curtain_count + rng.uniform(-0.25, 0.25)
        out_dir = [math.cos(a), 0.0, math.sin(a)]
        branch_dir = v_norm([out_dir[0], 0.55, out_dir[2]])
        branch_len = rng.uniform(1.1, 1.5)
        result = c.shape("cone", f"{tag} Branch {i}", top, height=branch_len,
                         bottom_radius=0.09, top_radius=0.05, slice_count=8,
                         material_name=m["bark"], parent_node_id=on_trunk,
                         motion_mode="none")
        branch_id = result.get("node_id") if isinstance(result, dict) else None
        rotation = align_y_quaternion(branch_dir)
        if branch_id is not None and rotation is not None:
            c.move_node_id(branch_id, rotation_xyzw=rotation)
        carrier = branch_id if branch_id is not None else on_trunk
        tip = v_add(top, v_scale(branch_dir, branch_len))

        # Curtain root: small stub at the branch tip; its hull is the
        # dynamic body all strands of this curtain ride.
        result = c.shape("cone", f"{tag} Curtain {i}", tip, height=0.18,
                         bottom_radius=0.05, top_radius=0.03, slice_count=6,
                         material_name=m["bark"], parent_node_id=carrier,
                         motion_mode="none")
        curtain_id = result.get("node_id") if isinstance(result, dict) else None
        if curtain_id is None:
            continue
        # Static inner strands along the branch (inner foliage barely
        # moves), then the swinging tip curtain.
        for s in range(3):
            frac = 0.35 + 0.2 * s
            sp = v_add(top, v_scale(branch_dir, branch_len * frac))
            sa = a + rng.uniform(-0.8, 0.8)
            sd = v_norm([math.cos(sa) * 0.22, -1.0, math.sin(sa) * 0.22])
            length = rng.uniform(0.7, 1.0)
            blade(c, f"{tag} Inner {i}.{s}",
                  v_add(sp, v_scale(sd, length * 0.5)),
                  0.22, [0.18, length * 2.3, 0.12], sd,
                  m["leaf" if s % 2 else "leaf2"], parent=carrier)
        strands = rng.randint(8, 10)
        for s in range(strands):
            sa = a + rng.uniform(-0.9, 0.9)
            sd = v_norm([math.cos(sa) * 0.30, -1.0, math.sin(sa) * 0.30])
            length = rng.uniform(0.9, 1.4)
            blade(c, f"{tag} Strand {i}.{s}",
                  v_add(tip, v_scale(sd, length * 0.5)),
                  0.22, [0.18, length * 2.3, 0.12], sd,
                  m["leaf" if s % 2 else "leaf2"], parent=curtain_id)
        # World-anchored at the branch tip: the willow's own parts are all
        # motion_mode="none" (the tree never moves), so a world anchor at
        # the tip is exactly a branch anchor, with fewer calls.
        sway_jobs.append((f"{tag} Curtain {i}", curtain_id, list(tip),
                          "curtain_sway", 0.6, 0.35, 0.5))


def fallen_log(c, m, rng):
    """Fallen mossy tree (static set dressing, unchanged from creation 10)."""
    start = [-2.7, 0.26, 3.0]
    d = v_norm([2.9, 0.02, 1.1])
    length = 3.1
    root = c.group("Fallen Tree", [start[0], 0.0, start[2]])
    result = c.shape("cone", "Fallen Log", start, height=length,
                     bottom_radius=0.27, top_radius=0.19, slice_count=14,
                     material_name=m["bark"], parent_node_id=root)
    log_id = result.get("node_id") if isinstance(result, dict) else None
    if log_id is not None:
        c.move_node_id(log_id, rotation_xyzw=align_y_quaternion(d))
    on_log = log_id if log_id is not None else root
    end = v_add(start, v_scale(d, length))
    blade(c, "Log Cut Face", end, 0.19, [1.0, 0.16, 1.0], d, m["cream"],
          parent=on_log)
    stump = v_add(start, v_scale(d, -0.8))
    result = c.shape("cone", "Log Stump", [stump[0], 0.0, stump[2]], height=0.5,
                     bottom_radius=0.30, top_radius=0.27, slice_count=14,
                     material_name=m["bark"], parent_node_id=root)
    stump_id = result.get("node_id") if isinstance(result, dict) else None
    scaled_sphere(c, "Stump Cut Face", [stump[0], 0.5, stump[2]], 0.26,
                  [1.0, 0.14, 1.0], m["cream"], slices=12, stacks=8,
                  parent=stump_id if stump_id is not None else root)
    for i, f in enumerate((0.9, 1.7, 2.4)):
        p = v_add(start, v_scale(d, f))
        stub_dir = v_norm([rng.uniform(-0.4, 0.4), 1.0, rng.uniform(-0.5, 0.1)])
        result = c.shape("cone", f"Log Stub {i}", [p[0], p[1] + 0.12, p[2]],
                         height=rng.uniform(0.30, 0.45), bottom_radius=0.06,
                         top_radius=0.025, slice_count=8, material_name=m["bark"],
                         parent_node_id=on_log)
        node_id = result.get("node_id") if isinstance(result, dict) else None
        if node_id is not None:
            c.move_node_id(node_id, rotation_xyzw=align_y_quaternion(stub_dir))
    for i, f in enumerate((0.5, 1.4, 2.2)):
        p = v_add(start, v_scale(d, f))
        scaled_sphere(c, f"Log Moss {i}", [p[0], p[1] + 0.18, p[2]],
                      0.22, [1.4, 0.30, 0.85], m["moss"], slices=10, stacks=8,
                      parent=on_log)
    log_top = start[1] + 0.20
    for i, f in enumerate((1.15, 1.95)):
        p = v_add(start, v_scale(d, f))
        mushroom(c, f"Log Mushroom {i}", p[0] + 0.06, p[2] - 0.06,
                 rng.uniform(0.09, 0.13), m, rng, y0=log_top, parent=on_log)


# ---------------------------------------------------------------- sway probe

def probe_sway(c, node_names, seconds=6.0, interval=0.25):
    """Sample world tilt (deg from upright) of the named nodes and print a
    small table - numeric proof the wind is moving the plants. Roughness
    (mean |second difference|) exposes high-frequency ringing that the
    range alone hides: smooth sway ~ a fraction of a degree, jitter >> 1."""
    steps = max(1, int(seconds / interval))
    series = {name: [] for name in node_names}
    for _ in range(steps):
        time.sleep(interval)
        for name in node_names:
            details = c.call("get_node_details", {"scene_name": c.scene, "node_name": name})
            qx, qy, qz, qw = details["world_transform"]["rotation_xyzw"]
            y_up = 1.0 - 2.0 * (qx * qx + qz * qz)
            series[name].append(math.degrees(math.acos(max(-1.0, min(1.0, y_up)))))
    for name, tilts in series.items():
        lo, hi = min(tilts), max(tilts)
        roughness = 0.0
        if len(tilts) > 2:
            roughness = sum(abs(tilts[i + 1] - 2.0 * tilts[i] + tilts[i - 1])
                            for i in range(1, len(tilts) - 1)) / (len(tilts) - 2)
        print(f"sway {name}: {' '.join(f'{t:5.1f}' for t in tilts)}  "
              f"(range {hi - lo:.1f} deg, roughness {roughness:.2f})")
    return series


# --------------------------------------------------------------------- main

def main():
    args = standard_args("Windswept Glade")
    c = Creation("Windswept Glade", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    # Build with the simulation OFF: the sway joints must capture the
    # authored pose as the rest pose (see the skill's bendy-plant recipe).
    c.set_physics(False)

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
    sway_jobs = []

    c.shape("box", "Forest Floor", [0.0, -0.25, 0.0], size=[60.0, 0.5, 60.0],
            material_name=m["grass"])

    # Two rings of trees around the clearing (bases lifted 0.05 so the
    # trunk hulls clear the floor instead of grinding on it). The outer
    # ring keeps the +X/+Z camera approach clear (eye [6.8, 3.2, 7.6]).
    trees = [
        ("Oak A",   OAK,   [-3.6, 0.05, -3.4]),
        ("Oak B",   OAK,   [4.2, 0.05, -3.8]),
        ("Oak C",   OAK,   [-4.6, 0.05, 2.6]),
        ("Birch A", BIRCH, [1.2, 0.05, -5.2]),
        ("Birch B", BIRCH, [5.6, 0.05, -1.6]),
        ("Oak D",   OAK,   [-7.6, 0.05, -6.8]),
        ("Oak E",   OAK,   [8.6, 0.05, -5.8]),
        ("Oak F",   OAK,   [-8.8, 0.05, 3.6]),
        ("Oak G",   OAK,   [0.2, 0.05, -9.2]),
        ("Oak H",   OAK,   [-3.4, 0.05, 6.8]),
        ("Birch C", BIRCH, [2.6, 0.05, -8.6]),
        ("Birch D", BIRCH, [9.2, 0.05, -1.2]),
        ("Birch E", BIRCH, [-9.0, 0.05, -1.8]),
        ("Birch F", BIRCH, [6.4, 0.05, -7.8]),
        ("Birch G", BIRCH, [-6.0, 0.05, 6.6]),
    ]
    for tag, species, base in trees:
        bark = m["birch"] if species is BIRCH else m["bark"]
        grow_tree(c, tag, base, species, bark, [m["leaf"], m["leaf2"]], rng, sway_jobs)

    for i, (x, z, size) in enumerate([(-2.2, -4.6, 0.8), (2.6, -5.0, 0.7),
                                      (-5.2, 0.2, 0.9), (5.4, -0.6, 0.8),
                                      (-3.0, 3.8, 0.7), (2.0, 4.6, 0.9)]):
        bush(c, f"Bush {i}", x, z, size, [m["leaf"], m["leaf2"]], rng)

    for i, (x, z, size) in enumerate([(-1.6, 1.8, 0.55), (1.9, 0.7, 0.75),
                                      (0.4, -2.6, 0.45), (3.2, 3.6, 0.5)]):
        mossy_rock(c, f"Boulder {i}", x, z, size, rng.uniform(0.0, math.pi), m, rng)

    for i, (x, z) in enumerate([(2.45, 1.25), (2.7, 0.9), (2.2, 0.55),
                                (-3.1, -2.9), (-3.5, -2.5),
                                (0.15, -2.15)]):
        mushroom(c, f"Mushroom {i}", x, z, rng.uniform(0.10, 0.18), m, rng)

    fallen_log(c, m, rng)

    # Fern colonies: three clusters of three rosettes each, varied sizes.
    fern_clusters = [
        [(-2.6, -1.8, 1.15), (-3.3, -1.2, 0.85), (-1.9, -2.5, 0.95)],
        [(3.3, -2.4, 1.0),   (4.0, -1.9, 0.8),   (2.7, -3.1, 0.9)],
        [(0.9, 2.6, 0.9),    (1.7, 3.1, 0.75),   (0.2, 3.3, 0.8)],
    ]
    fern_index = 0
    for cluster in fern_clusters:
        for x, z, size in cluster:
            fern(c, f"Fern {fern_index}", x, z, size, m, rng, sway_jobs)
            fern_index += 1

    # Wildflower meadow: 20 flowers in 5 colors; variety from geometry
    # (petal count / length, bloom size, stem height) - the material pool
    # is fully claimed, so no new colors.
    colors = ["pink", "yellow", "cream", "sky_blue", "red"]
    for i in range(20):
        a = rng.uniform(0.0, 2.0 * math.pi)
        d = rng.uniform(0.5, 3.2)
        flower(c, f"Flower {i}", d * math.cos(a), d * math.sin(a),
               colors[i % len(colors)], m, rng, sway_jobs,
               petal_count=rng.randint(4, 7),
               petal_len=rng.uniform(1.1, 1.7),
               bloom_r=rng.uniform(0.024, 0.042),
               iterations=rng.randint(2, 4))

    # Grass colonies: clusters of physics tufts across the clearing.
    grass_clusters = [(1.5, 1.5), (-1.0, 0.6), (-0.4, -1.4),
                      (2.4, -0.8), (0.3, 3.6), (-2.8, 2.2)]
    tuft_index = 0
    for cx, cz in grass_clusters:
        for _ in range(3):
            gx = cx + rng.uniform(-0.45, 0.45)
            gz = cz + rng.uniform(-0.45, 0.45)
            grass_tuft(c, f"Grass {tuft_index}", gx, gz,
                       rng.uniform(0.24, 0.36), m, rng, sway_jobs)
            tuft_index += 1

    # Two weeping willows: A at the clearing's left edge, curtains
    # overhanging the meadow (prominent but ~12 m from the camera eye
    # [6.8, 3.2, 7.6] - close-up strands read as screen-filling slabs);
    # B in the back ring as a depth element.
    willow(c, "Willow A", [-3.2, 0.05, 1.4], m, rng, sway_jobs)
    willow(c, "Willow B", [4.6, 0.05, -6.2], m, rng, sway_jobs)

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

    # ------------------------------------------------------- physics + wind
    c.settle()
    make_sway_settings(c)
    rig_sway(c, sway_jobs)
    c.settle()

    c.wind(enabled=True, direction=[1.0, 0.0, 0.35], speed=3.0,
           gust_amplitude=2.2, gust_frequency=0.4, turbulence=0.45,
           wavelength=9.0)
    c.set_physics(True)
    c.wake_physics()

    probe_sway(c, ["Oak A Trunk", "Fern 0 Rachis 0.0", "Flower 0 Stem 0",
                   "Grass 0 Spine", "Willow A Curtain 0"])

    c.place_camera([6.8, 3.2, 7.6], [0.0, 1.3, -0.8])
    c.screenshot("logs/creations/windswept_glade.png")
    time.sleep(3.0)
    c.screenshot("logs/creations/windswept_glade_gust.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/windswept_glade.glb")
    print("Windswept Glade complete.")


if __name__ == "__main__":
    main()
