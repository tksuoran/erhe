"""Shared L-system tree generators for the creation scripts.

Extracted from creation 13 (windswept glade) so creations can reuse one
implementation: expand_lsystem + grow_tree are the glade's stochastic
bracket L-system, moved verbatim (same rng draw order). The species-level
generators below (grow_conifer / grow_columnar / grow_shrub +
broadleaf_species_params) target real-height trees for garden-style
scenes; everything is built from unit-geometry PartBatch instances
(motion_mode "none"), one place_brush_instances flush per tree.

Realism notes (v2, after the 2026-08-09 research pass - sources:
Prusinkiewicz & Lindenmayer "The Algorithmic Beauty of Plants" ch. 2,
Weber & Penn "Creation and Rendering of Realistic Trees" (SIGGRAPH 95)):
- TROPISM: branch headings blend a constant world-space vector each
  segment (ABOP eq. for bending toward/away from gravity). Positive
  tropism_y = apical rise (most crowns), negative = pendulous droop
  (Betula pendula tips, Picea branchlets).
- PIPE MODEL (Leonardo / Murray): child branch radius follows
  r_child = r_parent * ratio^(1/2.49) so cross-section area is roughly
  conserved across splits - thin twigs emerge naturally.
- PHYLLOTAXIS: successive branches rotate ~137.5 deg (golden angle)
  around the parent instead of uniform thirds - kills the visible
  3-branch symmetry of the glade trees.
- CROWN SHAPE (Weber-Penn): branch length is a function of relative
  position in the crown (long low branches, short top) - the conifer
  whorl generator uses (1 - t)^shape directly.
"""

import math

from common import (
    BatchHandle, align_y_quaternion, v_add, v_cross, v_norm, v_rotate, v_scale,
)

GOLDEN_ANGLE = math.radians(137.5)


def distal_f_counts(s):
    """Pipe-model support: for each 'F' in an expanded L-system string, the
    number of 'F' segments distal to it (itself + everything it feeds,
    including nested brackets). Right-to-left scan with a scope stack."""
    counts = [0] * len(s)
    running = [0]
    for i in range(len(s) - 1, -1, -1):
        ch = s[i]
        if ch == "]":
            running.append(0)
        elif ch == "[":
            child = running.pop()
            running[-1] += child
        elif ch == "F":
            running[-1] += 1
            counts[i] = running[-1]
    return counts


def grow_roots(batch, tag, base, trunk_r, bark, rng, root_count=5,
               flare=1.6, reach=3.6, parent=None):
    """Root system at a trunk base: a flare cone widening into the ground
    plus root_count surface roots radiating outward - thin cones that start
    just above grade and sink gently below it, so they read as roots
    running partly on the surface."""
    x, y, z = base
    flare_h = max(0.3, trunk_r * 1.3)
    batch.part("cone", f"{tag} Root Flare", [x, y - flare_h * 0.35, z],
               height=flare_h, bottom_radius=trunk_r * flare,
               top_radius=trunk_r * 1.05, slice_count=10,
               material_name=bark, parent_node_id=parent)
    for i in range(root_count):
        a = 2.0 * math.pi * i / root_count + rng.uniform(-0.4, 0.4)
        d = v_norm([math.cos(a), rng.uniform(-0.22, -0.10), math.sin(a)])
        length = trunk_r * flare * rng.uniform(1.6, 2.6) + trunk_r * reach * rng.uniform(0.6, 1.0)
        start = [x + math.cos(a) * trunk_r * 0.7,
                 y + trunk_r * rng.uniform(0.25, 0.45),
                 z + math.sin(a) * trunk_r * 0.7]
        batch.part("cone", f"{tag} Root {i}", start,
                   rotation_xyzw=align_y_quaternion(d),
                   height=length, bottom_radius=trunk_r * rng.uniform(0.38, 0.52),
                   top_radius=trunk_r * 0.08, slice_count=6,
                   material_name=bark, parent_node_id=parent)


def rig_tree_sway(c, sway_jobs):
    """Glade-pattern rest-pose motor rig with HIERARCHY support: each spine
    node becomes a wind-receptive dynamic body (shape="auto" hull of its
    own mesh - spines must be REAL geometry, not unit-part instances)
    jointed at its base to its carrier. A carrier that is itself a spine
    (a limb jointed to its trunk) already has a dynamic body, so only
    non-spine carriers get the tiny static sensor body. Jobs must list a
    tree's trunk before its limbs so the trunk body exists when the limb
    joints look it up. Run AFTER settle() with the simulation DISABLED so
    the joints capture the authored rest pose.

    Joints use the ragdoll pattern (creations 7/14): TWO coincident anchor
    children at the pivot, one on each body, jointed together. Connecting
    the spine anchor straight to the carrier NODE put the carrier-side
    constraint frame at the carrier body's origin - for a limb jointed to
    a trunk that is the trunk BASE, meters from the pivot, and the motors
    drove the limb around the wrong point (crowns slumped to half
    height)."""
    spine_ids = {job[1] for job in sway_jobs}
    carriers_with_body = set()
    for tag, node_id, base_pos, settings, mass, receptivity, ang_damp, carrier_id in sway_jobs:
        if carrier_id not in carriers_with_body and carrier_id not in spine_ids:
            c.body(carrier_id, shape="sphere", radius=0.05,
                   motion_mode="static", is_trigger=True)
            carriers_with_body.add(carrier_id)
        c.body(node_id, shape="auto", motion_mode="dynamic", mass=mass,
               gravity_factor=0.0, angular_damping=ang_damp,
               linear_damping=0.05, wind_receptivity=receptivity)
        anchor_spine = c.anchor(f"{tag} Sway Anchor", node_id, base_pos)
        anchor_carrier = c.anchor(f"{tag} Sway Pivot", carrier_id, base_pos)
        c.joint(anchor_spine, connected_node_id=anchor_carrier, settings_name=settings)
    print(f"rigged {len(sway_jobs)} sway spines")


def make_rest_pose_settings(c, name, range_rad, stiffness, damping, max_force):
    """Rest-pose motor joint settings (glade recipe): linear XYZ + angular Y
    locked, angular X/Z limited, drives spring back to the authored pose."""
    c.joint_settings(
        name,
        limits=[
            {"linear_axes": [True, True, True], "angular_axes": [False, False, False], "min": 0.0, "max": 0.0},
            {"linear_axes": [False, False, False], "angular_axes": [False, True, False], "min": 0.0, "max": 0.0},
            {"linear_axes": [False, False, False], "angular_axes": [True, False, False], "min": -range_rad, "max": range_rad},
            {"linear_axes": [False, False, False], "angular_axes": [False, False, True], "min": -range_rad, "max": range_rad},
        ],
        drives=[
            {"type": "angular", "axis": 0, "stiffness": stiffness, "damping": damping, "max_force": max_force, "position_target": 0.0},
            {"type": "angular", "axis": 2, "stiffness": stiffness, "damping": damping, "max_force": max_force, "position_target": 0.0},
        ],
    )


_sway_setting_cache = set()


def sway_setting_for_height(c, height):
    """BEAM-SCALED joint settings, one per 4 m height bucket (joint settings
    are shared library items, so they quantize). Bending stiffness of a
    tapered beam goes ~ r^4 / L and trunk radius grows with height, so
    stiffness ~ h^2 while rotational inertia grows ~ h^3: tall thick trunks
    get SMALL, LOW-FREQUENCY sway (omega ~ 1/sqrt(h)) and thin trunks sway
    MORE and faster. The angular range widens for thin members the same
    way. Limbs reuse this rule via their EQUIVALENT height (radius / 0.014,
    the trunk radius-per-height factor), so stiffness keeps falling
    steeply toward thinner and thinner branches."""
    bucket = max(4, int(round(height / 4.0)) * 4)
    name = f"tree_sway_h{bucket}"
    if name not in _sway_setting_cache:
        stiffness = 2.2 * bucket * bucket
        make_rest_pose_settings(
            c, name,
            range_rad=0.06 + 1.6 / bucket,
            stiffness=stiffness,
            damping=stiffness * 0.10,
            max_force=stiffness * 2.2,
        )
        _sway_setting_cache.add(name)
    return name


def add_trunk_collider(c, tag, parent_node_id, base, height, bottom_radius, top_radius):
    """Static tapered-cylinder rigid body along the trunk axis. Collision
    shapes have no offset and unit-part trunks cannot use shape=\"auto\"
    (hulls ignore node scale), so an anchor child at mid-height carries an
    explicitly sized shape instead."""
    anchor_id = c.anchor(f"{tag} Trunk Collider", parent_node_id,
                         [base[0], base[1] + height * 0.5, base[2]])
    c.body(anchor_id, shape="tapered_cylinder", bottom_radius=float(bottom_radius),
           top_radius=float(top_radius), length=float(height),
           motion_mode="static")
    return anchor_id


# ----------------------------------------------------- glade L-system (moved)

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


def grow_tree(c, tag, base, species, bark, leaf_materials, rng, sway_jobs, age=1.0):
    """Stochastic L-system tree, graph mirroring the bracket structure.
    Every part is motion_mode="none"; the trunk is the sway spine (the
    whole branch/canopy subtree rides its node), collected in sway_jobs.

    age scales the whole tree: an old tree (age > ~1.3) is taller and
    thicker, branches one L-system iteration DEEPER, branches more
    densely, is more gnarled (wider wobble) and carries larger canopy
    clumps; a sapling (age < ~0.7) is the reverse. The sway body's mass
    grows with age^2 and its wind receptivity with age, so old trees
    lumber and saplings whip.

    Species dicts may carry optional realism knobs (v2):
    - "tropism": [x, y, z] world vector blended into the heading each
      segment (e.g. [0, 0.06, 0] apical rise, [0, -0.2, 0] pendulous).
    - "tip_tropism": like tropism but scaled by depth/iterations, so only
      the fine outer growth bends (pendulous birch tips).
    - "phyllotaxis": True = golden-angle (137.5 deg) rotation between
      sibling branches instead of the uniform 95-145 deg draw.
    - "pipe_exponent": pipe-model radii (Leonardo / Murray, ~2.49):
      radius = seg_r * (distal segment count / total)^(1/exp) - thick
      boughs and fine twigs replace the legacy per-depth r_falloff.
    - "curve_res": draw each segment as this many sub-cones with the
      wobble/tropism re-applied per sub-segment - curved branches
      instead of straight sticks (default 1 = legacy).
    """
    p = species
    iterations = p["iterations"]
    if age >= 1.3:
        iterations += 1
    elif age <= 0.7:
        iterations -= 1
    weights = (min(0.95, p["weights"][0] * min(age, 1.15)), p["weights"][1])
    trunk_h = p["trunk_h"] * age
    trunk_r = (p["trunk_r"][0] * (age ** 0.8), p["trunk_r"][1] * (age ** 0.8))
    seg_len = p["seg_len"] * (age ** 0.7)
    wobble = p["wobble"] * (1.0 + 0.6 * max(0.0, age - 1.0))
    pitch_lo, pitch_hi = p["pitch"]
    pitch = (pitch_lo, pitch_hi + 8.0 * max(0.0, age - 1.0))
    tropism = p.get("tropism")
    tip_tropism = p.get("tip_tropism")
    phyllotaxis = p.get("phyllotaxis", False)
    pipe_exponent = p.get("pipe_exponent")
    curve_res = max(1, int(p.get("curve_res", 1)))

    root = c.group(tag, base)
    result = c.shape("cone", f"{tag} Trunk", base, height=trunk_h,
                     bottom_radius=trunk_r[0], top_radius=trunk_r[1],
                     slice_count=14, material_name=bark, parent_node_id=root,
                     motion_mode="none")
    trunk_id = result.get("node_id") if isinstance(result, dict) else None
    parent = trunk_id if trunk_id is not None else root
    # Optional "sway" dict tunes the rig (absent = glade defaults):
    # trunk_settings/mass/receptivity/damping, and branch_sway=True turns
    # major limbs (pipe share > branch_share_min) into second-level spines
    # jointed to the trunk - hierarchical wind.
    sway = p.get("sway") or {}
    if trunk_id is not None and sway_jobs is not None:
        sway_jobs.append((tag, trunk_id, list(base),
                          sway.get("trunk_settings", "tree_sway"),
                          sway.get("trunk_mass", 25.0 * age * age),
                          sway.get("trunk_receptivity", 6.0 * age),
                          sway.get("trunk_damping", 0.8), root))
    # Whole branch + canopy structure goes out as ONE place_brush_instances
    # call: chained segments parent via in-batch handles.
    batch = c.part_batch()

    # Optional root system (opt-in "root_count"): flare + surface roots at
    # the trunk base, parented to the tree root group.
    if p.get("root_count"):
        grow_roots(batch, tag, base, trunk_r[0], bark, rng,
                   root_count=p["root_count"], parent=root)
    # Optional lower trunk branches (opt-in "trunk_branches"): an EVEN
    # golden-angle ladder from mid-trunk THROUGH the first crown spine
    # segments (the L-system only brackets at segment tops, so the zone
    # covered by Branch 0-1 - a full seg_len above the trunk - was bare;
    # the ladder now spans one continuous branch distribution up to the
    # first bracket region, Weber-Penn style). Length and foliage grow
    # toward the crown to meet the crown branches; thickness comes from
    # the trunk they leave. "branch_stub_chance" turns some into short
    # blunt stubs - broken branches on an old tree.
    n_lower = p.get("trunk_branches", 0)
    stub_chance = p.get("branch_stub_chance", 0.0)
    ladder_top = trunk_h + seg_len * 1.15
    for i in range(n_lower):
        u = min(1.0, (i + rng.uniform(0.2, 0.8)) / n_lower)
        band_y = trunk_h * 0.45 + (ladder_top - trunk_h * 0.45) * u
        frac = band_y / trunk_h  # may exceed 1.0: rungs along Branch 0-1
        a = i * GOLDEN_ANGLE + rng.uniform(-0.5, 0.5)
        bpos = v_add(base, [0.0, band_y, 0.0])
        branch_r = max(0.04, trunk_r[1] * max(0.25, 0.62 - 0.22 * frac))
        if rng.random() < stub_chance:
            # Broken stub: short, blunt, near-horizontal, no foliage.
            d = v_norm([math.cos(a), rng.uniform(-0.10, 0.15), math.sin(a)])
            batch.part("cone", f"{tag} Stub {i}", bpos,
                       rotation_xyzw=align_y_quaternion(d),
                       height=seg_len * rng.uniform(0.10, 0.22),
                       bottom_radius=branch_r * 1.15,
                       top_radius=branch_r * 0.7, slice_count=6,
                       material_name=bark, parent_node_id=parent)
            continue
        d = v_norm([math.cos(a), rng.uniform(0.20, 0.45), math.sin(a)])
        blen = seg_len * (0.55 + 0.6 * min(1.05, frac)) * rng.uniform(0.85, 1.1)
        handle = batch.part("cone", f"{tag} Low Branch {i}", bpos,
                            rotation_xyzw=align_y_quaternion(d),
                            height=blen, bottom_radius=branch_r,
                            top_radius=max(0.025, branch_r * 0.45),
                            slice_count=6, material_name=bark,
                            parent_node_id=parent, as_parent=True)
        tip = v_add(bpos, v_scale(d, blen))
        batch.part("uv_sphere", f"{tag} Low Canopy {i}", tip,
                   radius=p["leaf_r"][0] * (0.55 + 0.5 * min(1.05, frac)) * rng.uniform(0.85, 1.1),
                   slice_count=10, stack_count=8,
                   material_name=leaf_materials[i % len(leaf_materials)],
                   parent_node_id=handle)

    leaves = []                         # (tip position, carrying branch id)
    seg_count = 0
    pos = v_add(base, [0.0, trunk_h, 0.0])
    heading = v_norm(p["tilt"])
    left = v_norm(v_cross([0.0, 1.0, 0.0], heading)) if abs(heading[1]) < 0.999 else [1.0, 0.0, 0.0]
    left = v_norm(v_cross(heading, v_cross(left, heading)))
    depth = 0
    stack = []

    expanded = expand_lsystem(rng, iterations, weights)
    pipe_counts = distal_f_counts(expanded) if pipe_exponent else None
    pipe_total = max(pipe_counts) if pipe_counts else 1

    # Hierarchical wind: the first segment of a depth-1 branch carrying a
    # large enough share of the tree (a MAJOR limb) becomes a second-level
    # sway spine - REAL geometry (shape="auto" hulls cannot see unit-part
    # node scale), jointed to the trunk by rig_tree_sway.
    branch_sway = bool(sway.get("branch_sway")) and (sway_jobs is not None) and (pipe_counts is not None)
    branch_share_min = sway.get("branch_share_min", 0.12)
    branch_head = False

    for char_index, ch in enumerate(expanded):
        if ch == "F":
            bend = tropism
            if tip_tropism is not None and iterations > 0:
                t = min(1.0, depth / iterations)
                scaled = v_scale(tip_tropism, t)
                bend = v_add(bend, scaled) if bend is not None else scaled
            # rng draw order (wobble, then length) matches the legacy glade
            # implementation so curve_res=1 species are byte-identical.
            wob = math.radians(rng.uniform(-wobble, wobble)) / curve_res
            heading = v_norm(v_rotate(heading, left, wob))
            if bend is not None:
                heading = v_norm(v_add(heading, bend))
            length = seg_len * (p["len_falloff"] ** depth) * rng.uniform(0.85, 1.15)
            if pipe_counts is not None:
                share = pipe_counts[char_index] / pipe_total
                radius = max(0.03, p["seg_r"] * (age ** 0.8) * (share ** (1.0 / pipe_exponent)))
            else:
                radius = max(0.03, p["seg_r"] * (age ** 0.8) * (p["r_falloff"] ** depth))
            if (branch_sway and branch_head and (depth == 1)
                    and (pipe_counts[char_index] / pipe_total) >= branch_share_min):
                # Major limb spine: a REAL cone the rig can hull. Parented to
                # the tree ROOT GROUP, not its visual parent chain - a
                # dynamic body nested under another dynamic body (the trunk)
                # double-drives the node transform and the crown collapses.
                # The joint to the trunk body is the physical attachment.
                result = c.shape("cone", f"{tag} Limb {seg_count}", list(pos),
                                 rotation_xyzw=align_y_quaternion(heading),
                                 height=length, bottom_radius=radius,
                                 top_radius=max(0.025, radius * 0.7),
                                 slice_count=8, material_name=bark,
                                 parent_node_id=root, motion_mode="none")
                limb_id = result.get("node_id") if isinstance(result, dict) else None
                if limb_id is not None:
                    # Beam-scaled limb stiffness: reuse the height-bucket rule
                    # via the limb's EQUIVALENT height (radius / trunk
                    # radius-per-height) - much softer than the trunk, and
                    # softer still the thinner the limb.
                    settings_name = sway.get("branch_settings")
                    if settings_name is None:
                        settings_name = sway_setting_for_height(c, radius / 0.014)
                    sway_jobs.append((f"{tag} Limb {seg_count}", limb_id, list(pos),
                                      settings_name,
                                      sway.get("branch_mass", max(0.4, radius * length * 30.0)),
                                      sway.get("branch_receptivity", 2.0),
                                      0.8, trunk_id if trunk_id is not None else root))
                    parent = limb_id
                seg_count += 1
                pos = v_add(pos, v_scale(heading, length))
                branch_head = False
                continue
            branch_head = False
            # Curved branches: curve_res sub-cones, wobble + tropism
            # re-applied per sub-segment (Weber-Penn curve resolution).
            for sub in range(curve_res):
                if sub > 0:
                    wob = math.radians(rng.uniform(-wobble, wobble)) / curve_res
                    heading = v_norm(v_rotate(heading, left, wob))
                    if bend is not None:
                        heading = v_norm(v_add(heading, bend))
                sub_len = length / curve_res
                # Unit-cone instance; as_parent adds a rigid pose node so the
                # child branches/canopies do not inherit this segment's scale.
                parent = batch.part("cone", f"{tag} Branch {seg_count}", list(pos),
                                    rotation_xyzw=align_y_quaternion(heading),
                                    height=sub_len, bottom_radius=radius,
                                    top_radius=max(0.025, radius * (0.7 ** (1.0 / curve_res))),
                                    slice_count=8, material_name=bark,
                                    parent_node_id=parent, as_parent=True)
                seg_count += 1
                pos = v_add(pos, v_scale(heading, sub_len))
        elif ch == "&":
            angle = math.radians(rng.uniform(*pitch))
            heading = v_norm(v_rotate(heading, left, angle))
        elif ch == "/":
            if phyllotaxis:
                angle = GOLDEN_ANGLE + math.radians(rng.uniform(-8.0, 8.0))
            else:
                angle = math.radians(rng.uniform(95.0, 145.0))
            left = v_norm(v_rotate(left, heading, angle))
        elif ch == "[":
            stack.append((list(pos), list(heading), list(left), depth, parent))
            depth += 1
            branch_head = True
        elif ch == "]":
            pos, heading, left, depth, parent = stack.pop()
            branch_head = False
        elif ch == "L":
            leaves.append((v_add(pos, v_scale(heading, 0.12)), parent))

    clusters = []                       # [centroid, count, contributor counts]
    cluster_r = p["cluster_r"] * (age ** 0.5)
    merge_sq = cluster_r * cluster_r
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
    age_leaf = age ** 0.6
    for i, (center, count, carriers) in enumerate(clusters):
        radius = min(base_r + per * math.sqrt(count), cap) * age_leaf
        jitter = [rng.uniform(-0.06, 0.06) for _ in range(3)]
        carrier = max(carriers.items(), key=lambda kv: kv[1])[0]
        batch.part("uv_sphere", f"{tag} Canopy {i}", v_add(center, jitter),
                   radius=radius, slice_count=12, stack_count=9,
                   material_name=leaf_materials[i % len(leaf_materials)],
                   parent_node_id=carrier)
    batch.flush()
    # Opt-in static trunk collider (physics presence without a sway rig).
    if p.get("trunk_collider"):
        add_trunk_collider(c, tag, root, base, trunk_h + seg_len,
                           trunk_r[0], trunk_r[1])
    print(f"{tag}: {seg_count} segments, {len(clusters)} canopy clusters")
    return root


# ------------------------------------------------- species-level generators

def broadleaf_species_params(height, spread=1.0, gnarl=1.0, canopy=1.0,
                             trunk_frac=0.22, tropism=None, tip_tropism=None,
                             phyllotaxis=True, curve_res=2, root_count=5,
                             trunk_branches=None, trunk_collider=False, tilt=None,
                             stubs=0.12, sway=None):
    """Derive a grow_tree species dict from a REAL target height (m).
    spread widens branching (weights + pitch), gnarl adds wobble, canopy
    scales foliage clump size, tip_tropism droops the fine outer growth
    (pendulous species). The main spine is trunk plus roughly
    iterations+2 segments with len_falloff, so seg_len is solved from the
    remaining height (crown wobble eats ~10%). Realism defaults (2026-08-09
    research pass): pipe-model radii, golden-angle phyllotaxis, apical-rise
    tropism, 2 sub-cones per segment."""
    iterations = 5 if height > 30.0 else 4 if height > 14.0 else 3
    trunk_h = height * trunk_frac
    if trunk_branches is None:
        # Even ladder density scales with the bare trunk length.
        trunk_branches = min(8, max(3, int(trunk_h * 0.7)))
    falloff = 0.76
    spine_depth = iterations + 2
    geometric_sum = (1.0 - falloff ** spine_depth) / (1.0 - falloff)
    seg_len = (height * 0.92 - trunk_h) / geometric_sum
    return {
        "iterations": iterations,
        "weights": (min(0.9, 0.42 * spread), 0.9),
        "trunk_h": trunk_h,
        "trunk_r": (height * 0.014, height * 0.009),
        "seg_len": seg_len, "len_falloff": falloff,
        "seg_r": height * 0.011, "r_falloff": 0.62,
        "pitch": (18.0 * spread, 36.0 * spread), "wobble": 7.0 * gnarl,
        "tilt": tilt if tilt is not None else [0.04, 0.995, 0.02],
        "cluster_r": height * 0.045 * canopy,
        "leaf_r": (height * 0.042 * canopy, height * 0.012 * canopy, height * 0.11 * canopy),
        "tropism": tropism if tropism is not None else [0.0, 0.05, 0.0],
        "tip_tropism": tip_tropism,
        "phyllotaxis": phyllotaxis,
        "pipe_exponent": 2.49,
        "curve_res": curve_res,
        "root_count": root_count,
        "trunk_branches": trunk_branches,
        "trunk_collider": trunk_collider,
        "branch_stub_chance": stubs,
        "sway": sway,
    }


def grow_conifer(c, tag, base, height, bark, leaf, rng,
                 crown_frac=0.85, droop=-0.25, whorl_branches=5,
                 whorl_step_frac=0.05, branch_len_frac=0.16, shape=1.0,
                 trunk_r_frac=0.013, tip_rise=0.0, root_count=0,
                 sparse_lower=0, trunk_collider=False, sway_jobs=None,
                 sway_settings="tree_sway", sway_mass=25.0, sway_receptivity=6.0):
    """Excurrent (single-leader) conifer: straight trunk cone + whorls of
    branches whose length follows the Weber-Penn crown shape
    (1 - t)^shape from crown base (t=0) to tip (t=1). Each branch is one
    cone with one elongated foliage sphere along it; the leader gets a
    foliage spike. droop < 0 sweeps branches down (spruce), > 0 up
    (young pine tops). Everything batches into one call."""
    x, y, z = base
    root = c.group(tag, base)
    batch = c.part_batch()
    trunk_r = height * trunk_r_frac
    if sway_jobs is not None:
        # Sway spine trunk: REAL geometry so the rig's shape="auto" body
        # hulls it; the whole whorl structure rides its node.
        result = c.shape("cone", f"{tag} Trunk", base, height=height * 0.97,
                         bottom_radius=trunk_r, top_radius=max(0.02, trunk_r * 0.08),
                         slice_count=10, material_name=bark,
                         parent_node_id=root, motion_mode="none")
        trunk = result.get("node_id") if isinstance(result, dict) else root
        sway_jobs.append((tag, trunk, list(base), sway_settings,
                          sway_mass, sway_receptivity, 0.8, root))
    else:
        trunk = batch.part("cone", f"{tag} Trunk", base, height=height * 0.97,
                           bottom_radius=trunk_r, top_radius=max(0.02, trunk_r * 0.08),
                           slice_count=10, material_name=bark,
                           parent_node_id=root, as_parent=True)
    if root_count:
        grow_roots(batch, tag, base, trunk_r, bark, rng,
                   root_count=root_count, parent=root)
    crown_base = height * (1.0 - crown_frac)
    # Sparse lower boughs below the crown (old pines keep a few).
    for i in range(sparse_lower):
        hfrac = rng.uniform(0.22, max(0.30, (crown_base / height) * 0.95))
        a = rng.uniform(0.0, 2.0 * math.pi)
        d = v_norm([math.cos(a), rng.uniform(-0.15, 0.10), math.sin(a)])
        blen = height * branch_len_frac * rng.uniform(0.45, 0.75)
        bpos = [x, y + height * hfrac, z]
        handle = batch.part("cone", f"{tag} Low {i}", bpos,
                            rotation_xyzw=align_y_quaternion(d),
                            height=blen, bottom_radius=trunk_r * 0.22,
                            top_radius=trunk_r * 0.08, slice_count=5,
                            material_name=bark, parent_node_id=trunk,
                            as_parent=True)
        fol_center = v_add(bpos, v_scale(d, blen * 0.7))
        batch.part("uv_sphere", f"{tag} Low {i} Foliage", fol_center,
                   rotation_xyzw=align_y_quaternion(d),
                   radii=[blen * 0.24, blen * 0.45, blen * 0.24],
                   slice_count=8, stack_count=6, material_name=leaf,
                   parent_node_id=handle)
    step = max(0.8, height * whorl_step_frac)
    hgt = crown_base
    whorl = 0
    phase = rng.uniform(0.0, 2.0 * math.pi)
    while hgt < height * 0.94:
        t = (hgt - crown_base) / max(0.001, height * 0.94 - crown_base)
        blen = height * branch_len_frac * ((1.0 - t) ** shape) + height * 0.03
        count = whorl_branches
        phase += GOLDEN_ANGLE  # whorls spiral by the golden angle
        for i in range(count):
            a = phase + 2.0 * math.pi * i / count + rng.uniform(-0.25, 0.25)
            d = v_norm([math.cos(a), droop + rng.uniform(-0.06, 0.06), math.sin(a)])
            bpos = [x, y + hgt, z]
            branch_r = max(0.015, trunk_r * 0.28 * (1.0 - t) + 0.01)
            if tip_rise > 0.0:
                # Down-swept bough whose outer part rises (spruce habit):
                # inner cone along d, outer cone bent up, foliage on the
                # outer segment.
                inner_len = blen * 0.6
                handle = batch.part("cone", f"{tag} W{whorl}.{i}", bpos,
                                    rotation_xyzw=align_y_quaternion(d),
                                    height=inner_len, bottom_radius=branch_r,
                                    top_radius=max(0.01, branch_r * 0.6),
                                    slice_count=5, material_name=bark,
                                    parent_node_id=trunk, as_parent=True)
                d2 = v_norm(v_add(d, [0.0, tip_rise, 0.0]))
                mid = v_add(bpos, v_scale(d, inner_len))
                outer_len = blen * 0.5
                outer = batch.part("cone", f"{tag} W{whorl}.{i} Tip", mid,
                                   rotation_xyzw=align_y_quaternion(d2),
                                   height=outer_len, bottom_radius=max(0.01, branch_r * 0.6),
                                   top_radius=max(0.008, branch_r * 0.25),
                                   slice_count=5, material_name=bark,
                                   parent_node_id=handle, as_parent=True)
                fol_center = v_add(mid, v_scale(d2, outer_len * 0.55))
                batch.part("uv_sphere", f"{tag} W{whorl}.{i} Foliage", fol_center,
                           rotation_xyzw=align_y_quaternion(d2),
                           radii=[blen * 0.26, blen * 0.52, blen * 0.26],
                           slice_count=8, stack_count=6, material_name=leaf,
                           parent_node_id=outer)
            else:
                handle = batch.part("cone", f"{tag} W{whorl}.{i}", bpos,
                                    rotation_xyzw=align_y_quaternion(d),
                                    height=blen, bottom_radius=branch_r,
                                    top_radius=max(0.01, branch_r * 0.4),
                                    slice_count=5, material_name=bark,
                                    parent_node_id=trunk, as_parent=True)
                fol_center = v_add(bpos, v_scale(d, blen * 0.62))
                batch.part("uv_sphere", f"{tag} W{whorl}.{i} Foliage", fol_center,
                           rotation_xyzw=align_y_quaternion(d),
                           radii=[blen * 0.24, blen * 0.52, blen * 0.24],
                           slice_count=8, stack_count=6, material_name=leaf,
                           parent_node_id=handle)
        hgt += step
        whorl += 1
    # Leader spike: narrow foliage cone capping the tip.
    spike_h = max(1.0, height * 0.10)
    batch.part("cone", f"{tag} Spike", [x, y + height * 0.90, z],
               height=spike_h, bottom_radius=spike_h * 0.28, top_radius=0.02,
               slice_count=8, material_name=leaf, parent_node_id=trunk)
    batch.flush()
    if trunk_collider:
        add_trunk_collider(c, tag, root, base, height * 0.97,
                           trunk_r, max(0.02, trunk_r * 0.08))
    print(f"{tag}: {whorl} whorls")
    return root


def grow_columnar(c, tag, base, height, bark, leaf, rng, width_frac=0.16,
                  lobes=5, root_count=0, trunk_collider=False, sway_jobs=None,
                  sway_settings="tree_sway", sway_mass=8.0, sway_receptivity=4.0):
    """Columnar evergreen (juniper): short trunk + a stack of squashed
    foliage spheres narrowing toward the tip."""
    x, y, z = base
    root = c.group(tag, base)
    batch = c.part_batch()
    if sway_jobs is not None:
        result = c.shape("cone", f"{tag} Trunk", base, height=height * 0.35,
                         bottom_radius=height * 0.02, top_radius=height * 0.012,
                         slice_count=8, material_name=bark,
                         parent_node_id=root, motion_mode="none")
        trunk = result.get("node_id") if isinstance(result, dict) else root
        sway_jobs.append((tag, trunk, list(base), sway_settings,
                          sway_mass, sway_receptivity, 0.8, root))
    else:
        trunk = batch.part("cone", f"{tag} Trunk", base, height=height * 0.35,
                           bottom_radius=height * 0.02, top_radius=height * 0.012,
                           slice_count=8, material_name=bark,
                           parent_node_id=root, as_parent=True)
    if root_count:
        grow_roots(batch, tag, base, height * 0.02, bark, rng,
                   root_count=root_count, parent=root)
    for i in range(lobes):
        t = i / max(1, lobes - 1)
        cy = height * (0.14 + 0.80 * t)
        r = height * width_frac * (1.0 - 0.55 * t)
        jitter = [rng.uniform(-0.08, 0.08) * r for _ in range(2)]
        batch.part("uv_sphere", f"{tag} Lobe {i}",
                   [x + jitter[0], y + cy, z + jitter[1]],
                   radii=[r, height * 0.16, r],
                   slice_count=10, stack_count=8, material_name=leaf,
                   parent_node_id=trunk)
    batch.flush()
    if trunk_collider:
        add_trunk_collider(c, tag, root, base, height * 0.9,
                           height * 0.05, height * 0.02)
    return root


def grow_shrub(c, tag, base, height, bark, leaf, rng, stems=4, spread=0.35,
               root_count=0, trunk_collider=False, sway_jobs=None,
               sway_settings="branch_sway", sway_mass=1.5, sway_receptivity=1.5):
    """Multi-stem shrub / small bushy tree: stems lean outward from the
    base, each carrying an elongated canopy blob at its tip. With
    root_count the stems rise from a shared root mound - branching that
    reads as starting below ground."""
    x, y, z = base
    root = c.group(tag, base)
    batch = c.part_batch()
    if root_count:
        grow_roots(batch, tag, base, height * 0.03, bark, rng,
                   root_count=root_count, flare=2.4, parent=root)
    for i in range(stems):
        a = 2.0 * math.pi * i / stems + rng.uniform(-0.5, 0.5)
        lean = spread * rng.uniform(0.6, 1.3)
        d = v_norm([math.cos(a) * lean, 1.0, math.sin(a) * lean])
        stem_h = height * rng.uniform(0.55, 0.75)
        if sway_jobs is not None:
            # Each stem is a light sway spine jointed to the shrub root.
            result = c.shape("cone", f"{tag} Stem {i}", base,
                             rotation_xyzw=align_y_quaternion(d),
                             height=stem_h, bottom_radius=height * 0.018,
                             top_radius=height * 0.008, slice_count=6,
                             material_name=bark, parent_node_id=root,
                             motion_mode="none")
            handle = result.get("node_id") if isinstance(result, dict) else root
            sway_jobs.append((f"{tag} Stem {i}", handle, list(base), sway_settings,
                              sway_mass, sway_receptivity, 0.6, root))
        else:
            handle = batch.part("cone", f"{tag} Stem {i}", base,
                                rotation_xyzw=align_y_quaternion(d),
                                height=stem_h, bottom_radius=height * 0.018,
                                top_radius=height * 0.008, slice_count=6,
                                material_name=bark, parent_node_id=root,
                                as_parent=True)
        tip = v_add(base, v_scale(d, stem_h))
        r = height * rng.uniform(0.20, 0.28)
        batch.part("uv_sphere", f"{tag} Crown {i}",
                   [tip[0], tip[1] + r * 0.4, tip[2]],
                   radii=[r, r * 0.85, r],
                   slice_count=10, stack_count=8, material_name=leaf,
                   parent_node_id=handle)
    batch.flush()
    if trunk_collider:
        add_trunk_collider(c, tag, root, base, height * 0.55,
                           height * 0.05, height * 0.03)
    return root
