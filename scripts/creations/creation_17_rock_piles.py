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
import time

sys.path.insert(0, os.path.dirname(__file__))
from common import (  # noqa: E402
    Creation, standard_args, reframe,
)

import random  # noqa: E402

SHOTS = [
    ("",        [14.0, 5.2, 10.0],  [-0.5, 1.0, -3.0]),
    ("_talus",  [5.8, 2.4, 6.8],    [0.0, 1.0, 0.0]),
    ("_cairn",  [9.4, 1.6, -0.6],   [6.5, 1.1, -3.4]),
    ("_low",    [-6.5, 0.9, 8.5],   [-2.0, 1.2, -4.0]),
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


def power_law_size(rng, s_min, s_max, alpha=2.2):
    """Inverse-CDF sample of pdf ~ s^-alpha on [s_min, s_max]: many small
    rocks, few boulders - the real talus size distribution."""
    a = 1.0 - alpha
    u = rng.random()
    return (s_min ** a + u * (s_max ** a - s_min ** a)) ** (1.0 / a)


def quantize_scale(s, ratio=1.16, base=0.18):
    """Multiplicative scale ladder: a number bake scale builds one extra
    primitive per DISTINCT value per brush, so sizes snap to base*ratio^n."""
    n = round(math.log(s / base) / math.log(ratio))
    return round(base * (ratio ** n), 4)


def rand_quat(rng):
    """Uniform random rotation (Shoemake)."""
    u1, u2, u3 = rng.random(), rng.random(), rng.random()
    a, b = math.sqrt(1.0 - u1), math.sqrt(u1)
    return [a * math.sin(2 * math.pi * u2), a * math.cos(2 * math.pi * u2),
            b * math.sin(2 * math.pi * u3), b * math.cos(2 * math.pi * u3)]


def tilt_yaw_quat(rng, max_tilt_deg):
    """Near-upright orientation: random yaw + a small random tilt (cairn
    slabs stay flattish instead of landing on edge)."""
    yaw = rng.uniform(0.0, 2.0 * math.pi)
    tilt = math.radians(rng.uniform(-max_tilt_deg, max_tilt_deg))
    qy = [0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)]
    qx = [math.sin(tilt / 2), 0.0, 0.0, math.cos(tilt / 2)]
    x1, y1, z1, w1 = qy
    x2, y2, z2, w2 = qx
    return [w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
            w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2]


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
            yard.rock(f"Cairn c{i}", pos, s, root, archetype="cairn_flat",
                      rotation=tilt_yaw_quat(rng, 2.0), friction=1.5)
            yard.apply_rock_friction()
            name = f"Cairn c{i} rock {yard.counter}"
            c.wake_physics()
            sim(c, 1.2)
            position, _rotation = c.node_world_pose(name)
            if position[1] > top_y - half:
                break
            c.delete_nodes(names=[name])
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

def sim(c, seconds):
    """Advance the (manual-mode) simulation by exactly `seconds` and block
    until the queue drains."""
    c.advance_time(seconds=seconds, max_step_ms=500.0)
    while c.advance_time().get("pending_seconds", 0.0) > 0.0:
        time.sleep(0.05)


def settle_rocks(c, cairn_stage=None, pre_s=7.0, post_s=3.0):
    """Deterministic physics settle under the MANUAL simulation clock
    (advance_time MCP tool): simulation time is frozen except explicit
    ticks, so mid-settle construction (the staged cairn) places bodies
    into a world that is standing still - regardless of wall clock,
    frame rate or window visibility. Freezes the aftermath."""
    c.settle()
    c.advance_time(mode="manual", max_step_ms=500.0)
    c.set_physics(True)
    c.wake_physics()
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


def make_ground_texture(c):
    # The box face UV spans the WHOLE ground plane - noise scale must be
    # high or the pattern stretches to featureless tan (iteration 1).
    g = c.texture_graph("Dry Earth")
    base = g.add("fbm", {"noise": 1, "scale_x": 90.0, "scale_y": 90.0, "iterations": 6.0})
    # size 9 = 12 m value-noise cells over the plane: reads as a regular
    # checker from the overview camera. Keep the speck grain under ~2.5 m.
    speck = g.add("noise", {"size": 48, "density": 0.4})
    mix = g.add("math", {"op": 0, "clamp": True})
    color = g.add("colorize", {"gradient": grad([
        (0.00, [0.34, 0.27, 0.19, 1.0]),
        (0.40, [0.52, 0.43, 0.30, 1.0]),
        (0.72, [0.64, 0.55, 0.41, 1.0]),
        (1.00, [0.74, 0.67, 0.53, 1.0])])})
    out = g.add("output", {"name": "Dry Earth", "size": 1024})
    g.link(base, mix)
    g.link(speck, mix, dst_slot=1)
    g.link(mix, color)
    g.link(color, out, dst_slot=2)
    return "Dry Earth"


def main():
    args = standard_args("Rockfall")
    if reframe(args, "Rockfall", "logs/creations/rockfall", SHOTS):
        return
    only = args.only
    c = Creation("Rockfall", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe,
                 reuse=args.reuse or bool(only), keep_scenes=bool(only))
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
        c.shape("box", "Ground", [0.0, -0.04, 0.0], size=[110.0, 0.08, 110.0],
                material_name=ground_material)
        c.bind_material_texture("dry earth", "Dry Earth")
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
    }

    if only:
        if only not in piles:
            raise SystemExit(f"--only '{only}' unknown; objects: {list(piles)}")
        c.delete_nodes(names=[only])
        if only == "Cairn":
            settle_rocks(c, cairn_stage=lambda: stack_cairn(c, yard, rng),
                         pre_s=0.5, post_s=2.0)
        else:
            piles[only]()
            yard.apply_rock_friction()
            if only != "Pebbles":
                settle_rocks(c, pre_s=7.0, post_s=1.0)
        eye, target = c.shot_relative(only, [5.0, 2.6, 5.5], [0.0, 0.8, 0.0])
        c.screenshot_views("logs/creations/rockfall", [("only", eye, target)])
    else:
        for make in piles.values():
            if make is not None:
                make()
        yard.apply_rock_friction()
        print(f"nodes before settle: {len(c.nodes())}")
        settle_rocks(c, cairn_stage=lambda: stack_cairn(c, yard, rng))
        c.screenshot_views("logs/creations/rockfall", SHOTS)

    if not args.no_save:
        c.save("res/editor/scenes/creations/rockfall.glb")
    print("Rockfall complete.")


if __name__ == "__main__":
    main()
