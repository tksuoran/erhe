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
- a coincident anchor child at the spine base joints it to its CARRIER
  (the plant's root group / trunk / branch, which gets a tiny static
  sensor body) with shared rest-pose motor settings (linear + angular-Y
  locked, angular X/Z limited, position-target-0 drives) - never to the
  world, so a plant can be moved as one object,
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
from common import (  # noqa: E402
    Creation, standard_args, align_y_quaternion, probe_tilt, quat_mul,
    v_add, v_scale, v_cross, v_norm, v_rotate,
)
from lsystem_trees import grow_tree  # noqa: E402  (moved to the shared module)


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
    jointed to its CARRIER (the plant part it hangs off: the plant's root
    group, a trunk or a branch) - never to the world. World anchors pinned
    sway parts in world space, so moving a plant left its foliage floating
    behind (2026-08-08); with carrier joints the constraint frames are
    body-relative and the whole plant moves as one object. The carrier gets
    a tiny static sensor body (constraint target only - sensors collide
    with nothing). Run AFTER settle() with the simulation disabled, so the
    joints capture the authored rest pose."""
    carriers_with_body = set()
    for tag, node_id, base_pos, settings, mass, receptivity, ang_damp, carrier_id in sway_jobs:
        if carrier_id not in carriers_with_body:
            c.body(carrier_id, shape="sphere", radius=0.05,
                   motion_mode="static", is_trigger=True)
            carriers_with_body.add(carrier_id)
        c.body(node_id, shape="auto", motion_mode="dynamic", mass=mass,
               gravity_factor=0.0, angular_damping=ang_damp,
               linear_damping=0.05, wind_receptivity=receptivity)
        anchor_id = c.anchor(f"{tag} Sway Anchor", node_id, base_pos)
        c.joint(anchor_id, connected_node_id=carrier_id, settings_name=settings)
    print(f"rigged {len(sway_jobs)} sway spines on {len(carriers_with_body)} carriers")



# ------------------------------------------------------------- undergrowth

def scaled_sphere(c, name, position, radius, scale, material, slices=14, stacks=10,
                  parent=None, motion_mode="static", rotation_xyzw=None):
    result = c.shape("uv_sphere", name, position, radius=radius,
                     rotation_xyzw=rotation_xyzw,
                     scale=scale if scale != [1.0, 1.0, 1.0] else None,
                     slice_count=slices, stack_count=stacks,
                     material_name=material, parent_node_id=parent,
                     motion_mode=motion_mode)
    return result.get("node_id") if isinstance(result, dict) else None


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
                            parent=root,
                            rotation_xyzw=[0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)])
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


def blade(c, name, position, radius, scale, direction, material, parent=None,
          batch=None):
    """Elongated squashed sphere aligned along direction (leaf blades,
    petals, cut faces). Unit-sphere instance: EVERY blade in the scene
    shares one brush; radius and the anisotropic stretch fold into the
    node scale (T*R*S, same result as the old radius + scale). Blades are
    always leaves, so no pose node is needed. Pure visual. Pass batch to
    collect into a PartBatch instead of placing immediately."""
    (batch if batch is not None else c).part(
        "uv_sphere", name, position,
        rotation_xyzw=align_y_quaternion(direction),
        radii=[radius * scale[0], radius * scale[1], radius * scale[2]],
        slice_count=10, stack_count=8, material_name=material,
        parent_node_id=parent)


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
    batch = c.part_batch()  # everything except stem segment 0 (the spine)
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
            if seg == 0:
                # Sway spine: keeps REAL geometry (create_physics_body
                # shape="auto" hulls the node's own mesh; a unit-scaled
                # instance would hull at unit size).
                result = c.shape("cone", f"{tag} Stem {seg}", list(pos),
                                 rotation_xyzw=align_y_quaternion(heading),
                                 height=length, bottom_radius=0.013,
                                 top_radius=0.010, slice_count=6,
                                 material_name=m["leaf2"], parent_node_id=parent,
                                 motion_mode="none")
                node_id = result.get("node_id") if isinstance(result, dict) else None
                if node_id is not None:
                    sway_jobs.append((tag, node_id, list(pos), "stem_sway", 0.03, 0.35, 0.10, root))
                    parent = node_id
            else:
                parent = batch.part("cone", f"{tag} Stem {seg}", list(pos),
                                    rotation_xyzw=align_y_quaternion(heading),
                                    height=length, bottom_radius=0.013,
                                    top_radius=0.010, slice_count=6,
                                    material_name=m["leaf2"], parent_node_id=parent,
                                    as_parent=True)
            pos = v_add(pos, v_scale(heading, length))
            seg += 1
        elif ch == "L":
            leaf_dir = v_norm(v_rotate(heading, left, math.radians(rng.uniform(55.0, 75.0))))
            blade(c, f"{tag} Leaf {seg}", v_add(pos, v_scale(leaf_dir, 0.05)),
                  0.05, [0.50, 1.6, 0.22], leaf_dir, m["leaf2"], parent=parent,
                  batch=batch)
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
            petal_parent = batch.part("uv_sphere", f"{tag} Center {seg}",
                                      v_add(pos, v_scale(heading, 0.02)), radius=bloom_r,
                                      slice_count=8, stack_count=6, material_name=center_color,
                                      parent_node_id=parent, as_parent=True)
            step = 360.0 / petal_count
            for i in range(petal_count):
                ring = v_rotate(left, heading, math.radians(i * step + rng.uniform(-8.0, 8.0)))
                petal_dir = v_norm(v_add(ring, v_scale(heading, 0.45)))
                blade(c, f"{tag} Petal {seg}.{i}",
                      v_add(pos, v_scale(petal_dir, 0.055)),
                      0.045, [0.55, petal_len, 0.30], petal_dir, m[color],
                      parent=petal_parent, batch=batch)
            seg += 1
    batch.flush()


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


def compound_pinna(c, name, pos, direction, length, parent, materials, rng,
                   batch=None):
    """Second L-system level: midrib + pinnule pair + tip pinnule."""
    d = v_norm(direction)
    midrib_len = length * 2.1
    if batch is not None:
        mid_parent = batch.part("cone", f"{name} Midrib", list(pos), height=midrib_len,
                                rotation_xyzw=align_y_quaternion(d),
                                bottom_radius=length * 0.09, top_radius=length * 0.05,
                                slice_count=6, material_name=materials[1],
                                parent_node_id=parent, as_parent=True)
    else:
        result = c.part("cone", f"{name} Midrib", list(pos), height=midrib_len,
                        rotation_xyzw=align_y_quaternion(d),
                        bottom_radius=length * 0.09, top_radius=length * 0.05,
                        slice_count=6, material_name=materials[1],
                        parent_node_id=parent, as_parent=True)
        node_id = result.get("node_id")
        mid_parent = node_id if node_id is not None else parent
    sperp = v_cross(d, [0.0, 1.0, 0.0])
    if abs(sperp[0]) + abs(sperp[1]) + abs(sperp[2]) < 1e-4:
        sperp = [1.0, 0.0, 0.0]
    sperp = v_norm(sperp)
    mid_point = v_add(pos, v_scale(d, midrib_len * 0.5))
    plen = length * 0.62
    # A blade's half-extent along its direction is radius * scale_y; center
    # each pinnule one half-extent (minus a hair of embed) from its
    # attachment point so it barely touches instead of poking through.
    side_half = plen * 1.5
    for s in (-1.0, 1.0):
        pdir = v_norm(v_add(v_scale(sperp, s), v_scale(d, 0.5)))
        blade(c, f"{name} Pinnule {s:+.0f}", v_add(mid_point, v_scale(pdir, side_half * 0.92)),
              plen, [0.45, 1.5, 0.18], pdir, materials[0], parent=mid_parent,
              batch=batch)
    tip_half = plen * 0.9 * 1.5
    blade(c, f"{name} Pinnule Tip", v_add(pos, v_scale(d, midrib_len * 0.98 + tip_half * 0.9)),
          plen * 0.9, [0.45, 1.5, 0.18], d, materials[0], parent=mid_parent,
          batch=batch)


def fern(c, tag, x, z, size, m, rng, sway_jobs):
    """Two-level L-system fern rosette. Each frond's first rachis segment
    is a sway spine; the rest of the frond rides it as visual children."""
    root = c.group(tag, [x, 0.0, z])
    fronds = 5
    base = [x, 0.04, z]
    batch = c.part_batch()  # everything except the 5 spine segments
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
                if t == 0:
                    # Sway spine: real geometry (shape="auto" body hull).
                    result = c.shape("cone", f"{tag} Rachis {i}.{t}", list(pos),
                                     rotation_xyzw=align_y_quaternion(heading),
                                     height=seg, bottom_radius=radius,
                                     top_radius=radius * 0.8, slice_count=6,
                                     material_name=m["leaf2"], parent_node_id=parent,
                                     motion_mode="none")
                    node_id = result.get("node_id") if isinstance(result, dict) else None
                    if node_id is not None:
                        sway_jobs.append((f"{tag} Frond {i}", node_id, list(pos),
                                          "frond_sway", 0.8, 0.22, 0.8, root))
                        parent = node_id
                else:
                    parent = batch.part("cone", f"{tag} Rachis {i}.{t}", list(pos),
                                        rotation_xyzw=align_y_quaternion(heading),
                                        height=seg, bottom_radius=radius,
                                        top_radius=radius * 0.8, slice_count=6,
                                        material_name=m["leaf2"], parent_node_id=parent,
                                        as_parent=True)
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
                                   pos, pinna_dir, length, parent, mats, rng,
                                   batch=batch)
                else:
                    # Center one half-extent (radius * scale_y, minus a hair
                    # of embed) out along the pinna direction: the blade
                    # barely touches the rachis instead of poking through it.
                    blade(c, f"{tag} Pinna {i}.{t}.{pending:+d}",
                          v_add(pos, v_scale(pinna_dir, length * 1.55 * 0.92)),
                          length, [0.32, 1.55, 0.13], pinna_dir, mats[0],
                          parent=parent, batch=batch)
                pending = 0
    batch.flush()


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
    sway_jobs.append((tag, spine_id, base, "grass_sway", 0.10, 0.12, 0.4, root))
    batch = c.part_batch()
    for i in range(rng.randint(3, 5)):
        a = rng.uniform(0.0, 2.0 * math.pi)
        tilt = math.radians(rng.uniform(14.0, 38.0))
        d = v_norm([math.sin(tilt) * math.cos(a), math.cos(tilt), math.sin(tilt) * math.sin(a)])
        batch.part("cone", f"{tag} Blade {i}", base,
                   rotation_xyzw=align_y_quaternion(d),
                   height=h * rng.uniform(0.65, 1.0), bottom_radius=0.010,
                   top_radius=0.001, slice_count=3,
                   material_name=m["leaf" if i % 2 else "leaf2"],
                   parent_node_id=spine_id)
    batch.flush()


def willow(c, tag, base, m, rng, sway_jobs, scale=1.0):
    """Weeping willow: static-looking trunk + arching branches (all
    motion_mode="none"), with a swinging leaf CURTAIN at each branch tip.
    Each curtain root is a small dynamic body jointed to a coincident
    anchor on its branch - NOT to the world - so the curtains swing in
    the wind while staying attached to the tree. The long hanging
    strands are elongated blade spheres riding the curtain root.

    scale is the tree's age: an ancient willow (scale >= 1.5) is
    proportionally bigger, droops its branches lower, and grows a second
    lower ring of branch curtains; curtain body mass scales with scale^2
    and receptivity with scale, so the big tree's curtains swing slower."""
    x, y, z = base
    ancient = scale >= 1.5
    root = c.group(tag, base)
    trunk_h = 2.3 * scale
    # Batch 1: trunk + crowns + branches. Curtain roots are sway spines
    # (real geometry, and their strands need the real branch ids), so they
    # come after the flush; their foliage is batch 2.
    batch = c.part_batch()
    trunk_handle = batch.part("cone", f"{tag} Trunk", base, height=trunk_h,
                              bottom_radius=0.30 * scale, top_radius=0.20 * scale,
                              slice_count=12, material_name=m["bark"],
                              parent_node_id=root, as_parent=True)
    # Leafy crown mass so the tree does not read as bare branches.
    for i in range(3):
        ca = 2.0 * math.pi * i / 3.0 + rng.uniform(-0.4, 0.4)
        batch.part("uv_sphere", f"{tag} Crown {i}",
                   [x + 0.45 * scale * math.cos(ca), y + trunk_h + 0.35 * scale,
                    z + 0.45 * scale * math.sin(ca)],
                   radius=rng.uniform(0.5, 0.65) * scale, slice_count=12, stack_count=9,
                   material_name=m["leaf" if i % 2 else "leaf2"],
                   parent_node_id=trunk_handle)

    # Branch rings: (attach height fraction, branch count, upward pitch).
    # The ancient tree hangs a second, droopier ring below the crown.
    rings = [(1.0, 6, 0.55 if not ancient else 0.40)]
    if ancient:
        rings.append((0.68, 4, 0.25))
    branch_specs = []  # (pose-node handle, yaw, branch_dir, branch_len, ring_base)
    for ring_frac, ring_branches, ring_up in rings:
        ring_base = [x, y + trunk_h * ring_frac, z]
        for i in range(ring_branches):
            a = 2.0 * math.pi * i / ring_branches + rng.uniform(-0.25, 0.25)
            out_dir = [math.cos(a), 0.0, math.sin(a)]
            branch_dir = v_norm([out_dir[0], ring_up, out_dir[2]])
            branch_len = rng.uniform(1.1, 1.5) * scale
            # Pose-node part: the carrier id rig_sway gets is the rigid
            # pose node (it grows a sensor body + curtain children).
            handle = batch.part("cone", f"{tag} Branch {len(branch_specs)}", ring_base,
                                rotation_xyzw=align_y_quaternion(branch_dir),
                                height=branch_len, bottom_radius=0.09 * scale,
                                top_radius=0.05 * scale, slice_count=8,
                                material_name=m["bark"], parent_node_id=trunk_handle,
                                as_parent=True)
            branch_specs.append((handle, a, branch_dir, branch_len, ring_base))
    batch.flush()

    foliage = c.part_batch()
    for curtain_index, (handle, a, branch_dir, branch_len, ring_base) in enumerate(branch_specs):
        carrier = handle.node_id if handle.node_id is not None else root
        tip = v_add(ring_base, v_scale(branch_dir, branch_len))

        # Curtain root: small stub at the branch tip; its hull is the
        # dynamic body all strands of this curtain ride.
        result = c.shape("cone", f"{tag} Curtain {curtain_index}", tip,
                         height=0.18 * scale, bottom_radius=0.05 * scale,
                         top_radius=0.03 * scale, slice_count=6,
                         material_name=m["bark"], parent_node_id=carrier,
                         motion_mode="none")
        curtain_id = result.get("node_id") if isinstance(result, dict) else None
        if curtain_id is None:
            continue
        # Static inner strands along the branch (inner foliage barely
        # moves), then the swinging tip curtain.
        for s in range(3):
            frac = 0.35 + 0.2 * s
            sp = v_add(ring_base, v_scale(branch_dir, branch_len * frac))
            sa = a + rng.uniform(-0.8, 0.8)
            sd = v_norm([math.cos(sa) * 0.22, -1.0, math.sin(sa) * 0.22])
            length = rng.uniform(0.7, 1.0) * scale
            blade(c, f"{tag} Inner {curtain_index}.{s}",
                  v_add(sp, v_scale(sd, length * 0.5)),
                  0.22 * (scale ** 0.7), [0.18, length * 2.3 / (scale ** 0.7), 0.12], sd,
                  m["leaf" if s % 2 else "leaf2"], parent=carrier, batch=foliage)
        strands = rng.randint(8, 10)
        for s in range(strands):
            sa = a + rng.uniform(-0.9, 0.9)
            sd = v_norm([math.cos(sa) * 0.30, -1.0, math.sin(sa) * 0.30])
            length = rng.uniform(0.9, 1.4) * scale
            blade(c, f"{tag} Strand {curtain_index}.{s}",
                  v_add(tip, v_scale(sd, length * 0.5)),
                  0.22 * (scale ** 0.7), [0.18, length * 2.3 / (scale ** 0.7), 0.12], sd,
                  m["leaf" if s % 2 else "leaf2"], parent=curtain_id, batch=foliage)
        # Anchored to the BRANCH (rig_sway gives it a sensor carrier
        # body), matching the docstring: the curtain stays attached when
        # the tree is moved. (A world anchor here once left curtains
        # floating behind a moved willow.)
        sway_jobs.append((f"{tag} Curtain {curtain_index}", curtain_id, list(tip),
                          "curtain_sway", 0.6 * scale * scale, 0.35 * scale, 0.5,
                          carrier))
    foliage.flush()


def fallen_log(c, m, rng):
    """Fallen mossy tree (static set dressing, unchanged from creation 10)."""
    start = [-2.7, 0.26, 3.0]
    d = v_norm([2.9, 0.02, 1.1])
    length = 3.1
    root = c.group("Fallen Tree", [start[0], 0.0, start[2]])
    result = c.shape("cone", "Fallen Log", start, height=length,
                     rotation_xyzw=align_y_quaternion(d),
                     bottom_radius=0.27, top_radius=0.19, slice_count=14,
                     material_name=m["bark"], parent_node_id=root)
    log_id = result.get("node_id") if isinstance(result, dict) else None
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
        c.shape("cone", f"Log Stub {i}", [p[0], p[1] + 0.12, p[2]],
                rotation_xyzw=align_y_quaternion(stub_dir),
                height=rng.uniform(0.30, 0.45), bottom_radius=0.06,
                top_radius=0.025, slice_count=8, material_name=m["bark"],
                parent_node_id=on_log)
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

    # Lights first, so the scene is lit from the first shape onward when
    # watching a windowed build.
    c.light("directional", "Morning Sun", [0.0, 12.0, 0.0],
            [1.0, 0.92, 0.75], 2.6)
    pitch = math.radians(-142.0)
    yaw = math.radians(-55.0)
    qy = [0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)]
    qx = [math.sin(pitch / 2), 0.0, 0.0, math.cos(pitch / 2)]
    # set_node_transform retries while the light's insert is pending: no
    # settle + node_by_name dance, and the selection is never touched.
    c.set_node_transform("Morning Sun", rotation_xyzw=quat_mul(qy, qx))
    c.light("point", "Green Bounce", [0.0, 3.5, 0.0], [0.55, 0.75, 0.45],
            45.0, range=20.0, cast_shadow=False)
    c.shadow_range(70.0)  # cover the whole tree ring + floor horizon

    c.shape("box", "Forest Floor", [0.0, -0.25, 0.0], size=[60.0, 0.5, 60.0],
            material_name=m["grass"])

    # Two rings of trees around the clearing (bases lifted 0.05 so the
    # trunk hulls clear the floor instead of grinding on it). The outer
    # ring keeps the +X/+Z camera approach clear (eye [6.8, 3.2, 7.6]).
    # Ages span sapling (0.6) to ancient (1.5): each affects size,
    # branching depth/density, gnarl and canopy - no two trees alike.
    trees = [
        ("Oak A",   OAK,   [-3.6, 0.05, -3.4], 1.0),
        ("Oak B",   OAK,   [4.2, 0.05, -3.8],  1.45),
        ("Oak C",   OAK,   [-4.6, 0.05, 2.6],  0.85),
        ("Birch A", BIRCH, [1.2, 0.05, -5.2],  1.0),
        ("Birch B", BIRCH, [5.6, 0.05, -1.6],  0.65),
        ("Oak D",   OAK,   [-7.6, 0.05, -6.8], 1.35),
        ("Oak E",   OAK,   [8.6, 0.05, -5.8],  1.0),
        ("Oak F",   OAK,   [-8.8, 0.05, 3.6],  0.7),
        ("Oak G",   OAK,   [0.2, 0.05, -9.2],  1.15),
        ("Oak H",   OAK,   [-3.4, 0.05, 6.8],  0.9),
        ("Birch C", BIRCH, [2.6, 0.05, -8.6],  1.3),
        ("Birch D", BIRCH, [9.2, 0.05, -1.2],  0.95),
        ("Birch E", BIRCH, [-9.0, 0.05, -1.8], 0.6),
        ("Birch F", BIRCH, [6.4, 0.05, -7.8],  1.1),
        ("Birch G", BIRCH, [-6.0, 0.05, 6.6],  0.8),
    ]
    for tag, species, base, age in trees:
        bark = m["birch"] if species is BIRCH else m["bark"]
        grow_tree(c, tag, base, species, bark, [m["leaf"], m["leaf2"]], rng,
                  sway_jobs, age=age)

    for i, (x, z, size) in enumerate([(-2.2, -4.6, 0.8), (2.6, -5.0, 0.7),
                                      (-5.2, 0.2, 0.9), (5.4, -0.6, 0.8),
                                      (-3.0, 3.8, 0.7), (2.0, 4.6, 0.9)]):
        bush(c, f"Bush {i}", x, z, size, [m["leaf"], m["leaf2"]], rng)

    boulders = [(-1.6, 1.8, 0.55), (1.9, 0.7, 0.75),
                (0.4, -2.6, 0.45), (3.2, 3.6, 0.5)]
    for i, (x, z, size) in enumerate(boulders):
        mossy_rock(c, f"Boulder {i}", x, z, size, rng.uniform(0.0, math.pi), m, rng)

    # Boulder keep-out: the rocks are static colliders, so a flower or grass
    # tuft spawned inside one locks its sway hull in permanent contact
    # (constant wild impulses). Margin covers the rock's horizontal stretch
    # (up to 1.25x size) plus a sway hull's reach.
    def clear_of_boulders(px, pz, margin=0.45):
        return all(
            (px - bx) ** 2 + (pz - bz) ** 2 > (bs * 1.25 + margin) ** 2
            for bx, bz, bs in boulders
        )

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
        for _ in range(40):  # resample until clear of the boulders
            a = rng.uniform(0.0, 2.0 * math.pi)
            d = rng.uniform(0.5, 3.2)
            fx, fz = d * math.cos(a), d * math.sin(a)
            if clear_of_boulders(fx, fz):
                break
        flower(c, f"Flower {i}", fx, fz,
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
            for _ in range(40):  # resample until clear of the boulders
                gx = cx + rng.uniform(-0.45, 0.45)
                gz = cz + rng.uniform(-0.45, 0.45)
                if clear_of_boulders(gx, gz, margin=0.25):
                    break
            grass_tuft(c, f"Grass {tuft_index}", gx, gz,
                       rng.uniform(0.24, 0.36), m, rng, sway_jobs)
            tuft_index += 1

    # Two weeping willows: A at the clearing's left edge, curtains
    # overhanging the meadow (prominent but ~12 m from the camera eye
    # [6.8, 3.2, 7.6] - close-up strands read as screen-filling slabs);
    # B an ANCIENT giant at twice the size, towering over the back ring
    # with a second, lower ring of branch curtains.
    willow(c, "Willow A", [-3.2, 0.05, 1.4], m, rng, sway_jobs)
    willow(c, "Willow B", [4.6, 0.05, -6.2], m, rng, sway_jobs, scale=2.0)

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

    probe_tilt(c, ["Oak A Trunk", "Fern 0 Rachis 0.0", "Flower 0 Stem 0",
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
