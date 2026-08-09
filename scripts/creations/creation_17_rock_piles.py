#!/usr/bin/env python3
"""Creation 17: Rockfall - piles of rocks settled by real physics.

Rocks are CONVEX HULLS of jittered fibonacci-sphere point clouds: a few
seeded archetypes (angular shard, chunky block, flat slab, river-worn
round) are shared as content-library brushes and instanced at QUANTIZED
bake scales (collision follows a number bake scale; the multiplicative
scale ladder keeps the per-brush primitive count small while the size
distribution spans 0.2 m pebbles to 2 m boulders, power-law like real
talus). Hero outcrop boulders are private hulls run through the chamfer
op for beveled facets.

Piles form by dropping the rocks under gravity: each pile is pre-heaped
with a sphere-drop packing (largest first, every rock rested on the
heap below it), then the simulation runs DETERMINISTICALLY through the
advance_time MCP tool (manual simulation clock, 2026-08-09) - exactly
N simulated seconds regardless of wall clock, window visibility or
frame rate - and the aftermath is frozen with toggle_physics.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import (  # noqa: E402
    Creation, standard_args, reframe, fail_soft, align_y_quaternion, quat_mul,
    power_law_size, quantize_scale, rand_quat, tilt_yaw_quat, sim,
    v_add, v_norm, v_scale,
)

import random  # noqa: E402

SHOTS = [
    ("",        [14.0, 5.2, 10.0],  [-0.5, 1.0, -3.0]),
    ("_talus",  [5.8, 2.4, 6.8],    [0.0, 1.0, 0.0]),
    ("_cairn",  [9.4, 1.6, -0.6],   [6.5, 1.1, -3.4]),
    ("_low",    [-6.5, 0.9, 8.5],   [-2.0, 1.2, -4.0]),
    ("_cactus", [-3.0, 2.8, 6.5],   [-10.0, 2.8, -1.0]),
    ("_agave",  [-6.2, 1.5, 5.8],   [-8.5, 0.7, 3.0]),
]

ROCK_DENSITY = 2600.0  # kg/m3, granite-ish


# ------------------------------------------------------------------ geometry

def rock_points(rng, count, elong, jitter):
    """Point cloud for one rock archetype: fibonacci-sphere directions,
    per-point radial jitter, anisotropic squash - the convex hull of this
    reads as an angular rock at low counts and river-worn at high counts.
    Rounded to 4 decimals so the shape pool key is stable."""
    golden = math.pi * (3.0 - math.sqrt(5.0))
    points = []
    for i in range(count):
        y = 1.0 - 2.0 * (i + 0.5) / count
        ring = math.sqrt(max(0.0, 1.0 - y * y))
        theta = golden * i
        radius = 0.5 * (1.0 + rng.uniform(-jitter, jitter))
        points.append([
            round(radius * elong[0] * ring * math.cos(theta), 4),
            round(radius * elong[1] * y, 4),
            round(radius * elong[2] * ring * math.sin(theta), 4),
        ])
    return points


def make_archetypes(rng):
    """Nominal-1m rock archetypes; instances pick one + a bake scale."""
    return {
        "shard":  {"points": rock_points(rng, 9,  (1.0, 0.62, 0.48), 0.30), "volume": 0.10},
        "angular": {"points": rock_points(rng, 12, (1.0, 0.80, 0.66), 0.22), "volume": 0.16},
        "block":  {"points": rock_points(rng, 10, (1.0, 0.70, 0.55), 0.26), "volume": 0.12},
        "chunky": {"points": rock_points(rng, 18, (1.0, 0.85, 0.72), 0.15), "volume": 0.21},
        "slab":   {"points": rock_points(rng, 14, (1.0, 0.42, 0.78), 0.12), "volume": 0.11},
        "worn":   {"points": rock_points(rng, 22, (1.0, 0.75, 0.85), 0.13), "volume": 0.20},
        # 30 evenly-jittered points read as a perfect geodesic ball - keep
        # river-worn rocks at 24 points with real jitter.
        "round":  {"points": rock_points(rng, 24, (1.0, 0.88, 0.80), 0.14), "volume": 0.24},
    }


# power_law_size / quantize_scale / rand_quat / tilt_yaw_quat / sim were
# promoted to common.py (2026-08-09) - import, never re-derive.

# ---------------------------------------------------------------- pile logic

def heap_positions(rng, sizes, center, radius):
    """Sphere-drop pre-heap: largest first, each rock rests on the heap
    below (treating placed rocks as spheres with a 0.85 contact factor).
    The heap is already cone-shaped when physics takes over, so settling
    compacts it gently instead of exploding an interpenetrating cloud."""
    placed = []  # (x, y, z, r)
    out = []
    for s in sorted(sizes, reverse=True):
        r = 0.5 * s
        best = None
        for _ in range(48):
            ang = rng.uniform(0.0, 2.0 * math.pi)
            rad = radius * math.sqrt(rng.random())
            x = center[0] + rad * math.cos(ang)
            z = center[2] + rad * math.sin(ang)
            y = r * 0.9 + 0.03
            for px, py, pz, pr in placed:
                d2 = (x - px) ** 2 + (z - pz) ** 2
                touch = (r + pr) * 0.85
                if d2 < touch * touch:
                    y = max(y, py + math.sqrt(touch * touch - d2))
            if best is None or y < best[1]:
                best = (x, y, z)
        x, y, z = best
        placed.append((x, y, z, r))
        out.append((s, [x, y + 0.05, z]))
    return out


class RockYard:
    """Shared rock fabrication: archetype brushes + friction bookkeeping."""

    def __init__(self, c, rng, materials):
        self.c = c
        self.rng = rng
        self.materials = materials
        self.archetypes = make_archetypes(rng)
        self.body_ids = []
        self.chamfer_ids = []
        self.counter = 0

    LARGE_ARCHETYPES = ("shard", "angular", "block", "chunky")

    def rock(self, prefix, position, size, parent_id, archetype=None,
             rotation=None, dynamic=True, material=None, friction=0.9):
        # Boulders read best angular (worn/round silhouettes at 1.5 m look
        # like dropped geodesic balls); rounded shapes stay in the small end.
        pool = self.LARGE_ARCHETYPES if size > 1.0 else list(self.archetypes)
        arch_name = archetype or self.rng.choice(pool)
        arch = self.archetypes[arch_name]
        scale = quantize_scale(size)
        mass = ROCK_DENSITY * arch["volume"] * scale ** 3
        self.counter += 1
        result = self.c.shape(
            "convex_hull", f"{prefix} rock {self.counter}", position,
            points=arch["points"], scale=scale,
            rotation_xyzw=rotation or rand_quat(self.rng),
            motion_mode="dynamic" if dynamic else "static",
            mass=round(mass, 2) if dynamic else None,
            parent_node_id=parent_id,
            material_name=material or self.rng.choice(self.materials))
        node_id = result.get("node_id") if isinstance(result, dict) else None
        if dynamic and node_id:
            self.body_ids.append((node_id, friction))
        if node_id and scale >= 0.45:
            self.chamfer_ids.append(node_id)
        return node_id

    def apply_rock_friction(self):
        """Rocks pile instead of scattering: high friction, dead bounce,
        angular damping so boulders stop rolling. One batch per 40 rocks."""
        calls = [{"tool": "edit_physics_body", "arguments": {
            "scene_name": self.c.scene, "node_id": node_id,
            "friction": friction, "restitution": 0.02,
            "angular_damping": 0.35, "linear_damping": 0.05,
        }} for node_id, friction in self.body_ids]
        for i in range(0, len(calls), 40):
            self.c.batch(calls[i:i + 40])
        self.body_ids = []

    def apply_chamfer(self, bevel_ratio=0.22):
        """Beveled facet rims on every rock big enough to show them
        (>= 0.45 m). Runs AFTER the settle: the chamfer op replaces the
        instance's mesh primitives (the instance silently goes private -
        deliberate; small rocks and pebbles keep sharing brushes), the
        physics attachment and pose survive, and the frozen bodies keep
        their slightly-proud original collision hulls (invisible). One
        node_ids batch per 50 rocks, one settle at the end."""
        ids = self.chamfer_ids
        self.chamfer_ids = []
        for i in range(0, len(ids), 50):
            self.c.mutate("chamfer", {
                "scene_name": self.c.scene, "node_ids": ids[i:i + 50],
                "bevel_ratio": bevel_ratio})
        self.c.settle()


# --------------------------------------------------------------- the objects

def build_talus(c, yard, rng):
    """The main pile: a talus cone of ~80 mixed rocks, 0.24-1.9 m."""
    root = c.group("Talus", [0.0, 0.0, 0.0])
    sizes = [power_law_size(rng, 0.24, 1.9) for _ in range(80)]
    for s, pos in heap_positions(rng, sizes, [0.0, 0.0, 0.0], 2.9):
        yard.rock("Talus", pos, s, root)


def stack_cairn(c, yard, rng):
    """A built cairn, stacked the way a person builds one: one flat slab
    per course, with a short DETERMINISTIC settle (advance_time manual
    ticks) between courses so each stone is at rest before the next lands
    on its MEASURED pose (the stack is followed as it drifts). Dropping a
    whole pre-stacked tower at once collapsed twice - iteration 1's tall
    slabs scattered, iteration 2's near-resting stack still slumped to two
    courses. Runs inside settle_rocks' manual-time window, physics ON."""
    center = [6.5, 0.0, -3.4]
    root = c.group("Cairn", center)
    flat = rock_points(rng, 15, (1.0, 0.30, 0.86), 0.09)
    yard.archetypes["cairn_flat"] = {"points": flat, "volume": 0.085}
    top_x, top_y, top_z = center[0], 0.0, center[2]
    for i in range(11):
        s = 1.25 - 0.068 * i + rng.uniform(-0.02, 0.02)
        half = 0.5 * s * 0.30
        # The simulation is deterministic in TIME (manual clock) but not
        # bit-identical across runs (Jolt threading), so a course can still
        # slide off on an unlucky run - verify each stone stayed on top and
        # LAY IT AGAIN if it fell (a person building a cairn does the same).
        for attempt in range(3):
            pos = [top_x + rng.uniform(-0.03, 0.03), top_y + half + 0.01,
                   top_z + rng.uniform(-0.03, 0.03)]
            node_id = yard.rock(f"Cairn c{i}", pos, s, root,
                                archetype="cairn_flat",
                                rotation=tilt_yaw_quat(rng, 2.0), friction=1.5)
            yard.apply_rock_friction()
            name = f"Cairn c{i} rock {yard.counter}"
            # Wake the whole cairn stack, not just the laid stone: the
            # courses below must stay cooperative (compact under the new
            # load) or stones slide off - a per-stone wake slumped the
            # tower. The rest of the scene stays settled.
            c.wake_physics(node_name="Cairn")
            sim(c, 1.2)
            position, _rotation = c.node_world_pose(name)
            if position[1] > top_y - half:
                break
            # The discarded stone's id must leave the chamfer list too or
            # the post-settle chamfer batch fails on a missing node.
            c.delete_nodes(names=[name])
            yard.chamfer_ids = [i_ for i_ in yard.chamfer_ids if i_ != node_id]
        top_x, top_y, top_z = position[0], position[1] + half, position[2]


def build_outcrop(c, yard, rng):
    """Half-buried hero boulders: private (reuse=False) hulls run through
    the chamfer op - beveled facet rims the instanced rocks don't have -
    with a dynamic scree apron dropped against their feet."""
    center = [-5.8, 0.0, -5.5]
    root = c.group("Outcrop", center)
    heroes = [
        ([-6.8, 0.85, -6.4], 3.2, 0.35),
        ([-4.6, 0.65, -4.6], 2.4, 0.30),
        ([-7.6, 0.45, -3.8], 1.6, 0.28),
    ]
    hero_ids = []
    for index, (pos, size, bevel) in enumerate(heroes):
        result = c.shape(
            "convex_hull", f"Outcrop boulder {index + 1}", pos,
            points=rock_points(rng, 16, (1.0, 0.78, 0.68), 0.2),
            scale=size, rotation_xyzw=rand_quat(rng), reuse=False,
            material_name=yard.materials[index % len(yard.materials)])
        if isinstance(result, dict) and result.get("node_id"):
            hero_ids.append(result["node_id"])
    for node_id, (_, _, bevel) in zip(hero_ids, heroes):
        c.mutate("chamfer", {"scene_name": c.scene, "node_id": node_id,
                             "bevel_ratio": bevel})
    c.settle()
    sizes = [power_law_size(rng, 0.2, 0.8) for _ in range(26)]
    for s, pos in heap_positions(rng, sizes, [center[0], 0.0, center[2] + 1.6], 2.2):
        yard.rock("Outcrop", pos, s, root)


def build_dunes(c, yard, rng):
    """Smooth sand mounds. Presentation research (2026-08-09): a
    high-tessellation uv_sphere (slice 48 / stack 24, smooth vertex
    normals) instanced with node TRS scale [rx, h, rz], yawed and sunk
    ~35% below grade gives a perfectly smooth mound whose skirt melts
    into the ground - and every mound shares ONE pooled brush. The
    alternatives all show facets or creases at silhouette scale:
    lattice-deformed stepped boxes crease on the coarse control grid,
    catmull_clark / smooth on hulls stays polygonal at the rim.
    motion_mode none (no collision): the rocks settle on the flat
    ground and the drifts lap AGAINST the pile skirts, reading as sand
    accumulated around the stones."""
    root = c.group("Dunes", [0.0, 0.0, 0.0])

    def mound(name, x, z, rx, h, rz, yaw_deg, sink=0.42):
        yaw = math.radians(yaw_deg)
        c.shape("uv_sphere", name, [x, -sink * h, z],
                radius=1.0, slice_count=48, stack_count=24,
                motion_mode="none", scale=[rx, h, rz],
                rotation_xyzw=[0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)],
                material_name="dune sand", parent_node_id=root)

    # Big wind-aligned dunes in the middle distance (long axes share a
    # NW-SE heading so the field reads as one wind regime). Less sunk
    # than the drifts so they keep a real crest height.
    mound("Dune west",      -17.0,   3.0, 10.0, 2.2, 4.8, -24.0, sink=0.30)
    mound("Dune north",       7.0, -19.0, 13.0, 2.6, 5.8, -32.0, sink=0.30)
    mound("Dune far east",   24.0,  -6.0,  8.0, 1.8, 4.0, -18.0, sink=0.30)
    mound("Dune south",      -4.0,  16.0,  9.0, 1.9, 4.2, -28.0, sink=0.30)
    # Low drifts lapping the pile skirts.
    mound("Talus drift E",    4.6,   2.6,  2.6, 0.34, 1.6, -30.0)
    mound("Talus drift W",   -3.6,  -1.2,  2.6, 0.30, 1.5, -22.0)
    mound("Cairn drift",      7.8,  -2.2,  2.2, 0.26, 1.3, -30.0)
    mound("Outcrop drift",   -6.6,  -3.2,  2.8, 0.34, 1.6, -26.0)
    mound("Field drift",      1.0,  -8.5,  3.4, 0.34, 1.9, -30.0)


def build_cacti(c, yard, rng):
    """Desert cacti. Shape research (2026-08-09): saguaro trunk and arms
    are smooth CAPSULES - the arm's elbow-then-up curve is a 3-segment
    chain (out-and-up, steeper, vertical) with align_y_quaternion, radii
    matched so the smooth caps read as one limb; barrel cacti and
    prickly-pear pads are squashed uv_spheres (pads [rx, ry, 0.09-thin]).
    Real saguaro RIBS were considered and rejected: convex hulls cannot
    go concave, per-rib CSG grooves cost a boolean pass per rib, and at
    scene scale a matte green material reads right without them.
    Capsule/sphere parameters are QUANTIZED so the shape pool shares
    brushes across cacti. All parts motion_mode none (no bodies - the
    piles' settle never touches them)."""
    greens = [
        c.ensure_material("cactus green", base_color=[0.24, 0.38, 0.18],
                          roughness=0.85, metallic=0.0),
        c.ensure_material("cactus sage",  base_color=[0.30, 0.43, 0.26],
                          roughness=0.85, metallic=0.0),
    ]
    bloom = c.ensure_material("cactus bloom", base_color=[0.95, 0.90, 0.75],
                              roughness=0.7, metallic=0.0)
    fruit = c.ensure_material("cactus fruit", base_color=[0.72, 0.12, 0.30],
                              roughness=0.7, metallic=0.0)
    root = c.group("Cacti", [0.0, 0.0, 0.0])

    def q2(v, step):  # quantize a geometry parameter for brush pooling
        return max(step, round(v / step) * step)

    def limb(name, base, direction, length, radius, material):
        # create_shape capsule takes bottom_radius / top_radius (a plain
        # 'radius' argument is silently ignored - geometry capsules are
        # tapered-capable, unlike the physics-shape schema).
        direction = v_norm(direction)
        length = q2(length, 0.1)
        r = q2(radius, 0.02)
        center = v_add(base, v_scale(direction, 0.5 * length))
        c.shape("capsule", name, center, bottom_radius=r, top_radius=r,
                length=length, rotation_xyzw=align_y_quaternion(direction),
                motion_mode="none", material_name=material,
                parent_node_id=root)
        return v_add(base, v_scale(direction, length))

    def saguaro(name, x, z, s, arm_specs, material):
        trunk_len = q2(2.6 * s, 0.1)
        trunk_r = q2(0.26 * s, 0.02)
        c.shape("capsule", f"{name} trunk", [x, 0.5 * trunk_len, z],
                bottom_radius=trunk_r, top_radius=trunk_r, length=trunk_len,
                motion_mode="none", material_name=material,
                parent_node_id=root)
        c.shape("uv_sphere", f"{name} crown", [x, trunk_len + 0.1 * s, z],
                radius=0.06, scale=s, motion_mode="none",
                material_name=bloom, parent_node_id=root)
        for index, (height_frac, azimuth_deg) in enumerate(arm_specs):
            az = math.radians(azimuth_deg)
            out = [math.cos(az), 0.0, math.sin(az)]
            base = [x + out[0] * 0.22 * s, trunk_len * height_frac,
                    z + out[2] * 0.22 * s]
            r = 0.16 * s
            arm = f"{name} arm {index + 1}"
            tip = limb(f"{arm} a", base, [out[0], 0.55, out[2]], 0.55 * s, r, material)
            tip = limb(f"{arm} b", tip, [out[0] * 0.45, 1.0, out[2] * 0.45], 0.45 * s, r, material)
            tip = limb(f"{arm} c", tip, [0.0, 1.0, 0.0], 0.65 * s, r, material)
            c.shape("uv_sphere", f"{arm} bloom", tip, radius=0.05,
                    scale=s, motion_mode="none", material_name=bloom,
                    parent_node_id=root)

    def barrel(name, x, z, s, material):
        # Squat and slightly sunk or it reads as a green ball.
        c.shape("uv_sphere", name, [x, 0.28 * s, z], radius=1.0,
                slice_count=32, stack_count=16, scale=[0.44 * s, 0.34 * s, 0.44 * s],
                motion_mode="none", material_name=material, parent_node_id=root)
        c.shape("uv_sphere", f"{name} bloom", [x, 0.60 * s, z], radius=0.06,
                scale=s, motion_mode="none", material_name=bloom,
                parent_node_id=root)

    def prickly_pear(name, x, z, s, pad_count, material):
        """Pad FAN, not a totem: pads must spread sideways with real
        outward LEAN (tilt of the pad plane) or the stacked thin spheres
        read as a ball column (first iteration). Each pad leans further
        out along its own yaw; fruits dot some pad top edges."""
        def pad_quat(yaw, tilt):
            qy = [0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)]
            qx = [math.sin(tilt / 2), 0.0, 0.0, math.cos(tilt / 2)]
            return quat_mul(qy, qx)

        pads = [([x, 0.30 * s, z], rng.uniform(0.0, math.pi), 0.0)]
        c.shape("uv_sphere", f"{name} pad 1", pads[0][0], radius=1.0,
                slice_count=24, stack_count=12,
                scale=[0.38 * s, 0.44 * s, 0.07 * s],
                rotation_xyzw=pad_quat(pads[0][1], 0.0),
                motion_mode="none", material_name=material, parent_node_id=root)
        for i in range(1, pad_count):
            parent_pos, parent_yaw, parent_tilt = pads[rng.randrange(len(pads))]
            yaw = parent_yaw + rng.uniform(-1.1, 1.1)
            lean = rng.uniform(0.35, 0.65)
            tilt = min(parent_tilt + rng.uniform(0.12, 0.35), 0.75)
            out = v_norm([math.sin(yaw) * lean, 1.0 - 0.4 * lean, math.cos(yaw) * lean])
            pos = v_add(parent_pos, v_scale(out, 0.56 * s))
            # Pad plane leans outward with the growth direction (tilt
            # about the pad's local X after its yaw).
            c.shape("uv_sphere", f"{name} pad {i + 1}", pos, radius=1.0,
                    slice_count=24, stack_count=12,
                    scale=[0.34 * s, 0.40 * s, 0.065 * s],
                    rotation_xyzw=pad_quat(yaw, math.copysign(tilt, math.sin(yaw - parent_yaw) or 1.0)),
                    motion_mode="none", material_name=material, parent_node_id=root)
            pads.append((pos, yaw, tilt))
            if rng.random() < 0.4:
                c.shape("uv_sphere", f"{name} fruit {i}",
                        v_add(pos, [math.sin(yaw) * 0.1 * s, 0.42 * s, math.cos(yaw) * 0.1 * s]),
                        radius=0.045, scale=s, motion_mode="none",
                        material_name=fruit, parent_node_id=root)

    saguaro("Saguaro grande", -10.0, -1.0, 1.7,
            [(0.42, 20.0), (0.55, 150.0), (0.62, 265.0)], greens[0])
    saguaro("Saguaro vieja", 12.0, -8.0, 1.4,
            [(0.48, 70.0), (0.58, 220.0)], greens[1])
    saguaro("Saguaro joven", 5.5, 8.0, 0.9, [(0.55, 300.0)], greens[0])
    barrel("Barrel cactus A", 2.2, 4.6, 1.0, greens[0])
    barrel("Barrel cactus B", 8.8, -1.4, 0.8, greens[1])
    barrel("Barrel cactus C", -9.8, -8.6, 1.2, greens[0])
    prickly_pear("Prickly pear W", -5.2, 1.8, 1.0, 6, greens[1])
    prickly_pear("Prickly pear E", 10.5, 3.5, 1.1, 7, greens[0])
    prickly_pear("Prickly pear N", 0.5, -6.0, 0.9, 5, greens[1])


def build_agaves(c, yard, rng):
    """Agave tequilana rosettes from the sweep shape (2026-08-09): each
    leaf is ONE swept blade - a closed CCW crescent profile (channeled
    upper face, convex belly, sharp margins) swept along a 4-point bezier
    spine curving outward, tapered [[0, 0.85], [0.25, 1], [0.7, 0.6],
    [1, 0]] so the tip collapses into the terminal spine point. All
    leaves of a plant share ONE pooled blade brush (identical profile /
    spine / taper; pitch, yaw and bake scale are per-instance), so a
    26-leaf rosette costs one geometry."""
    agave_green = c.ensure_material(
        "agave blue", base_color=[0.35, 0.46, 0.44], roughness=0.7, metallic=0.0)
    agave_pale = c.ensure_material(
        "agave pale", base_color=[0.44, 0.54, 0.50], roughness=0.7, metallic=0.0)
    root = c.group("Agaves", [0.0, 0.0, 0.0])

    # Unit leaf, +Y up, curving outward along +X; length ~1.
    blade_profile = [
        [0.080, 0.0], [0.040, -0.008], [0.0, -0.012], [-0.040, -0.008],
        [-0.080, 0.0], [-0.050, -0.032], [0.0, -0.045], [0.050, -0.032],
    ]
    blade_spine = [[0.0, 0.0, 0.0], [0.02, 0.35, 0.0], [0.10, 0.68, 0.0], [0.30, 0.95, 0.0]]
    blade_taper = [[0.0, 0.85], [0.25, 1.0], [0.7, 0.6], [1.0, 0.0]]

    def leaf(name, position, yaw, pitch, scale, material, parent):
        qy = [0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)]
        qz = [0.0, 0.0, math.sin(-pitch / 2), math.cos(-pitch / 2)]
        c.shape("sweep", name, position,
                profile=blade_profile, spine=blade_spine, taper=blade_taper,
                spine_steps=10, scale=scale,
                rotation_xyzw=quat_mul(qy, qz), motion_mode="none",
                material_name=material, parent_node_id=parent)

    def agave(name, x, z, s, material):
        plant = c.group(name, [x, 0.0, z], parent_node_id=root)
        c.shape("uv_sphere", f"{name} core", [x, 0.10 * s, z], radius=1.0,
                slice_count=16, stack_count=8, scale=[0.18 * s, 0.14 * s, 0.18 * s],
                motion_mode="none", material_name=material, parent_node_id=plant)
        # Rosette rings: outer leaves long and near-horizontal, inner
        # short and steep; per-ring yaw offset staggers the spiral.
        rings = [
            (9, math.radians(68.0), 1.00),
            (7, math.radians(50.0), 0.90),
            (5, math.radians(32.0), 0.75),
            (3, math.radians(14.0), 0.60),
        ]
        index = 0
        for ring_index, (count, pitch, length) in enumerate(rings):
            yaw0 = rng.uniform(0.0, 2.0 * math.pi)
            for k in range(count):
                yaw = yaw0 + (k + 0.5 * ring_index) * 2.0 * math.pi / count
                index += 1
                out = [math.sin(yaw), 0.0, math.cos(yaw)]
                base = [x + out[0] * 0.06 * s, 0.05 * s, z + out[2] * 0.06 * s]
                jitter = rng.uniform(-0.06, 0.06)
                # Leaf local +X (curve direction) lands on `out`: a yaw
                # rotation about Y maps +X to (cos, 0, -sin), so the leaf
                # yaw is the ring angle minus 90 degrees.
                leaf(f"{name} leaf {index}", base, yaw - math.pi / 2.0,
                     pitch + jitter, round(s * length, 2), material, plant)

    agave("Agave grande", -8.5, 3.0, 1.3, agave_green)
    agave("Agave mediana", 3.2, -8.8, 1.0, agave_pale)
    agave("Agave joven", 7.5, 6.2, 0.7, agave_green)


def build_pebbles(c, yard, rng):
    """Static pebble drifts around the pile skirts: analytic placement
    (partially sunk, random pose), motion_mode none - no body cost."""
    root = c.group("Pebbles", [0.0, 0.0, 0.0])
    drifts = [([0.0, 0.0, 0.0], 4.6, 60), ([6.5, 0.0, -3.2], 2.4, 26),
              ([-5.2, 0.0, -3.6], 3.4, 34), ([1.5, 0.0, -7.5], 3.0, 22)]
    for center, radius, count in drifts:
        for _ in range(count):
            ang = rng.uniform(0.0, 2.0 * math.pi)
            rad = radius * (0.55 + 0.45 * math.sqrt(rng.random()))
            s = power_law_size(rng, 0.08, 0.22)
            pos = [center[0] + rad * math.cos(ang), 0.3 * s,
                   center[2] + rad * math.sin(ang)]
            yard.rock("Pebble", pos, s, root, dynamic=False)


# ------------------------------------------------------------------ settling

def settle_rocks(c, cairn_stage=None, pre_s=7.0, post_s=3.0, wake_scope=True):
    """Deterministic physics settle under the MANUAL simulation clock
    (advance_time MCP tool): simulation time is frozen except explicit
    ticks, so mid-settle construction (the staged cairn) places bodies
    into a world that is standing still - regardless of wall clock,
    frame rate or window visibility. Freezes the aftermath.

    wake_scope: True wakes the whole scene (full build); a node name wakes
    only that subtree (--only re-settles must not topple the piles that are
    already built); None skips the wake (staged construction wakes each
    body as it is laid)."""
    c.settle()
    c.advance_time(mode="manual", max_step_ms=500.0)
    c.set_physics(True)
    if wake_scope is True:
        c.wake_physics()
    elif wake_scope:
        c.wake_physics(node_name=wake_scope)
    sim(c, pre_s)
    if cairn_stage is not None:
        cairn_stage()
    sim(c, post_s)
    status = c.advance_time()
    print(f"settled: simulation_time_s={status.get('simulation_time_s', 0):.2f} "
          f"frame={status.get('frame_number')}")
    c.set_physics(False)
    c.advance_time(mode="wall_clock")


# ---------------------------------------------------------------------- main

def grad(stops, interpolation=1):
    return {"interpolation": interpolation,
            "stops": [{"pos": p, "color": list(c)} for p, c in stops]}


SAND_GRADIENT = [
    (0.00, [0.34, 0.27, 0.19, 1.0]),
    (0.40, [0.52, 0.43, 0.30, 1.0]),
    (0.72, [0.64, 0.55, 0.41, 1.0]),
    (1.00, [0.74, 0.67, 0.53, 1.0])]


def make_dune_texture(c):
    """Same gradient as the ground so dune and ground tones match, but a
    LOW fbm scale: a mound's uv_sphere UV spans the whole sphere, so the
    ground texture's scale-90 fbm renders as microscopic bright grain
    that visually detaches the mound from the ground (iteration 6)."""
    g = c.texture_graph("Dune Sand")
    base = g.add("fbm", {"noise": 1, "scale_x": 5.0, "scale_y": 3.0, "iterations": 5.0})
    color = g.add("colorize", {"gradient": grad(SAND_GRADIENT)})
    out = g.add("output", {"name": "Dune Sand", "size": 512})
    g.link(base, color)
    g.link(color, out, dst_slot=2)
    return "Dune Sand"


def make_ground_texture(c):
    # The box face UV spans the WHOLE ground plane - noise scale must be
    # high or the pattern stretches to featureless tan (iteration 1).
    g = c.texture_graph("Dry Earth")
    base = g.add("fbm", {"noise": 1, "scale_x": 90.0, "scale_y": 90.0, "iterations": 6.0})
    # size 9 = 12 m value-noise cells over the plane: reads as a regular
    # checker from the overview camera. Keep the speck grain under ~2.5 m.
    speck = g.add("noise", {"size": 48, "density": 0.4})
    mix = g.add("math", {"op": 0, "clamp": True})
    color = g.add("colorize", {"gradient": grad(SAND_GRADIENT)})
    out = g.add("output", {"name": "Dry Earth", "size": 1024})
    g.link(base, mix)
    g.link(speck, mix, dst_slot=1)
    g.link(mix, color)
    g.link(color, out, dst_slot=2)
    return "Dry Earth"


def build(c, args, only):
    if only:
        scene = c.attach_scene()
        print(f"attached scene: {scene} (rebuilding only '{only}')")
    else:
        scene = c.new_scene()
        print(f"scene: {scene}")

    if not only:
        c.ambience(ambient=[0.15, 0.15, 0.16],
                   clear_color=[0.66, 0.58, 0.47, 1.0], grid=False,
                   sky={"_version": 3, "enabled": True, "mode": 1})

    rock_materials = [
        c.ensure_material("granite gray",  base_color=[0.44, 0.43, 0.41], roughness=0.92, metallic=0.0),
        c.ensure_material("granite warm",  base_color=[0.50, 0.44, 0.37], roughness=0.9,  metallic=0.0),
        c.ensure_material("granite dark",  base_color=[0.36, 0.35, 0.33], roughness=0.95, metallic=0.0),
        c.ensure_material("lichen stone",  base_color=[0.42, 0.44, 0.34], roughness=0.95, metallic=0.0),
        c.ensure_material("sandstone",     base_color=[0.58, 0.48, 0.36], roughness=0.88, metallic=0.0),
    ]
    ground_material = c.ensure_material(
        "dry earth", base_color=[0.60, 0.52, 0.40], roughness=1.0, metallic=0.0)

    if not only:
        # Lights + shadow range FIRST (skill rule): warm afternoon sun.
        c.light("directional", "Afternoon Sun", [0.0, 30.0, 0.0],
                [1.0, 0.87, 0.66], 2.9)
        pitch = math.radians(-140.0)
        yaw = math.radians(35.0)
        qy = [0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)]
        qx = [math.sin(pitch / 2), 0.0, 0.0, math.cos(pitch / 2)]
        x1, y1, z1, w1 = qy
        x2, y2, z2, w2 = qx
        c.set_node_transform("Afternoon Sun", rotation_xyzw=[
            w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
            w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2])
        c.light("point", "Sky Fill", [-14.0, 18.0, 16.0], [0.62, 0.68, 0.82],
                180.0, range=90.0, cast_shadow=False)
        # The WHOLE ground box - including its diagonal corners - must stay
        # inside the shadow range: ground beyond the shadow fit renders as a
        # hard dark band (iteration 2's 500 m ground with a 40 m range drew
        # a giant dark trapezoid; iteration 3's 160 m ground left a corner
        # wedge past its 90 m range). 110 m ground diag = 78 m + camera
        # offset stays under the 110 m range. The ground is a THIN slab so
        # its unlit side face at the far edge reads as a hairline.
        c.shadow_range(110.0, z_far=600.0)

        make_ground_texture(c)
        make_dune_texture(c)
        c.shape("box", "Ground", [0.0, -0.04, 0.0], size=[110.0, 0.08, 110.0],
                material_name=ground_material)
        c.bind_material_texture("dry earth", "Dry Earth")
        c.ensure_material("dune sand", base_color=[0.60, 0.52, 0.40],
                          roughness=1.0, metallic=0.0)
        c.bind_material_texture("dune sand", "Dune Sand")
        details = c.call("get_material_details", {
            "scene_name": c.scene, "material_name": "dry earth"})
        print(f"dry earth material: {details}")

    # Physics stays DISABLED through the whole build (skill rule: joints /
    # drops capture fallen poses otherwise); settle_rocks() flips it on for
    # exactly the simulated settle window and freezes the aftermath.
    c.set_physics(False)

    rng = random.Random(1717)
    yard = RockYard(c, rng, rock_materials)

    # The cairn has no build-phase entry: it is STACKED course by course
    # inside the settle window (see stack_cairn).
    piles = {
        "Talus":   lambda: build_talus(c, yard, rng),
        "Cairn":   None,
        "Outcrop": lambda: build_outcrop(c, yard, rng),
        "Pebbles": lambda: build_pebbles(c, yard, rng),
        "Dunes":   lambda: build_dunes(c, yard, rng),
        "Cacti":   lambda: build_cacti(c, yard, rng),
        "Agaves":  lambda: build_agaves(c, yard, rng),
    }

    if only:
        if only not in piles:
            raise SystemExit(f"--only '{only}' unknown; objects: {list(piles)}")
        c.delete_nodes(names=[only])
        if only == "Cairn":
            # wake_scope None: the staged cairn wakes each stone as it is
            # laid; nothing else may wake or the settled piles could topple.
            settle_rocks(c, cairn_stage=lambda: stack_cairn(c, yard, rng),
                         pre_s=0.5, post_s=2.0, wake_scope=None)
        else:
            piles[only]()
            yard.apply_rock_friction()
            if only not in ("Pebbles", "Dunes", "Cacti", "Agaves"):
                settle_rocks(c, pre_s=7.0, post_s=1.0, wake_scope=only)
        yard.apply_chamfer()
        eye, target = c.shot_relative(only, [5.0, 2.6, 5.5], [0.0, 0.8, 0.0])
        c.screenshot_views("logs/creations/rockfall", [("only", eye, target)])
    else:
        for make in piles.values():
            if make is not None:
                make()
        yard.apply_rock_friction()
        print(f"nodes before settle: {len(c.nodes())}")
        settle_rocks(c, cairn_stage=lambda: stack_cairn(c, yard, rng))
        yard.apply_chamfer()
        c.screenshot_views("logs/creations/rockfall", SHOTS)

    if not args.no_save:
        c.save("res/editor/scenes/creations/rockfall.glb")
    print("Rockfall complete.")


def main():
    args = standard_args("Rockfall")
    if reframe(args, "Rockfall", "logs/creations/rockfall", SHOTS):
        return
    only = args.only
    c = Creation("Rockfall", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe,
                 reuse=args.reuse or bool(only), keep_scenes=bool(only))
    # Fail-soft finalize (common.fail_soft, 2026-08-09): a crash after the
    # expensive settle still screenshots the aftermath (rockfall_failed.png)
    # before the process exits, instead of losing the whole run's evidence.
    with fail_soft(c, "logs/creations/rockfall"):
        build(c, args, only)


if __name__ == "__main__":
    main()
