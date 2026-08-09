#!/usr/bin/env python3
"""Creation 16: Sail Ships.

A three-ship squadron running before a golden-hour wind on open water -
and the first creation whose modeling is carried by geometry OPERATIONS
instead of shape assembly. Every hull is a convex_hull over authored
station points (keel sweep, bow stem, transom corners), then CSG-carved:
difference a deck box out of the hull to raise real bulwarks, difference
a row of gunport pockets through each side, difference arched windows
into the flagship's transom. Fittings continue the theme: the crow's
nest is a cone minus its inner cone (an open cup), the flagship's bow
cannons get bored muzzles, and the ship's wheel rim + spokes are UNIONED
into one solid. Sails are subdivided thin boxes billowed with
lattice_deform (bezier FFD - the belly pins at the corners and bulges
mid-panel), and the masthead pennants ripple with an S-curve lattice.

Showcases (all new MCP tools, 2026-08-09): create_shape convex_hull /
regular_polyhedron / disc, csg union / intersection / difference
(in-place target replace + tool removal, one undo entry each), and
lattice_deform (auto-fit cage FFD).

Fleet: a 30 m three-masted galleon (flagship, full detail), an 18 m
two-masted brig, and a 12 m single-masted sloop, each heading offset a
few degrees so the formation reads as sailing, not parked.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import (  # noqa: E402
    Creation, standard_args, reframe, align_y_quaternion, quat_mul,
    quat_rotate, v_add,
)

SHOTS = [
    ("",         [34.0, 8.5, 40.0], [-10.0, 5.0, -14.0]),
    ("_bow",     [-16.0, 3.5, -36.0], [4.0, 4.5, 6.0]),
    ("_stern",   [26.0, 6.5, 22.0], [-6.0, 3.5, -6.0]),
    ("_vespucci", [-88.0, 14.0, -18.0], [-14.0, 8.0, -62.0]),
    # Vespucci clipper-bow close-up (bow ~ [-44.7, 3.5, -73.1])
    ("_vespucci_bow", [-68.5, 6.0, -71.0], [-43.0, 4.5, -73.5]),
]


def bezier2(p0, p1, p2, t):
    """Quadratic bezier point (p1 is the control point)."""
    u = 1.0 - t
    return [u * u * p0[k] + 2.0 * u * t * p1[k] + t * t * p2[k]
            for k in range(3)]


def yaw_quat(degrees):
    a = math.radians(degrees) * 0.5
    return [0.0, math.sin(a), 0.0, math.cos(a)]


def pitch_quat(degrees):
    a = math.radians(degrees) * 0.5
    return [math.sin(a), 0.0, 0.0, math.cos(a)]


def roll_quat(degrees):
    a = math.radians(degrees) * 0.5
    return [0.0, 0.0, math.sin(a), math.cos(a)]


# --------------------------------------------------------------------- hull

def hull_station_points(length, beam, depth, freeboard, clipper=False):
    """Authored station points for a convex ship hull, local space: +Z bow,
    +Y up, waterline at y=0. Keel at -depth, rail at +freeboard. Convexity
    is fine for a beamy age-of-sail hull: the faceted envelope reads as
    planking, and the deck well is carved back in with CSG.

    clipper=True (the Amerigo Vespucci) swaps the blunt raised prow for a
    clipper bow: denser fore stations with rising sheer and narrowing beam
    round the entry, and the stem overhangs the waterline by ~6.5% of the
    length (waterline entry at 0.50 L, stem head at 0.565 L). The straight
    hull edge between those two is scooped concave with a CSG cylinder in
    build_hull - a convex hull cannot hollow itself."""
    half = length * 0.5
    b = beam * 0.5
    points = []
    # (z_frac, rail_half_beam_frac, keel_y_frac, sheer_frac): station spine
    if clipper:
        stations = [
            (-0.50, 0.62, -0.30, 1.00),   # transom
            (-0.34, 0.88, -0.80, 1.00),
            (-0.12, 1.00, -1.00, 1.00),   # midship, deepest keel
            ( 0.12, 0.97, -1.00, 1.00),
            ( 0.30, 0.82, -0.78, 1.00),
            ( 0.40, 0.60, -0.52, 1.06),   # sheer starts rising
            ( 0.46, 0.36, -0.24, 1.14),   # fine entry
        ]
    else:
        stations = [
            (-0.50, 0.62, -0.30, 1.00),   # transom
            (-0.34, 0.88, -0.80, 1.00),
            (-0.12, 1.00, -1.00, 1.00),   # midship, deepest keel
            ( 0.12, 0.97, -1.00, 1.00),
            ( 0.32, 0.80, -0.72, 1.00),
            ( 0.46, 0.52, -0.35, 1.00),   # fore shoulder
        ]
    for z_frac, beam_frac, keel_frac, sheer_frac in stations:
        z = z_frac * length
        bw = b * beam_frac
        keel_y = depth * keel_frac
        rail_y = freeboard * sheer_frac
        points.append([+bw, rail_y, z])
        points.append([-bw, rail_y, z])
        points.append([+bw * 0.92, 0.4 * freeboard - 0.35 * depth, z])  # bilge turn
        points.append([-bw * 0.92, 0.4 * freeboard - 0.35 * depth, z])
        points.append([0.0, keel_y, z])
    if clipper:
        # Clipper stem: waterline entry + overhanging stem head; the
        # concave hollow between them is carved in build_hull.
        points.append([0.0, freeboard * 1.30, half * 1.13])  # stem head
        points.append([0.0, 0.0,              half * 1.00])  # waterline entry
        points.append([0.0, -depth * 0.10,    half * 0.955])  # forefoot round
    else:
        # Bow: raised stem + forefoot
        points.append([0.0, freeboard * 1.45, half * 1.04])   # stem head (raised prow)
        points.append([0.0, freeboard * 0.3,  half * 1.10])   # cutwater
        points.append([0.0, -depth * 0.45,    half * 0.92])   # forefoot
    # Stern: raised transom corners (poop sheer)
    points.append([+b * 0.55, freeboard * 1.30, -half * 1.02])
    points.append([-b * 0.55, freeboard * 1.30, -half * 1.02])
    points.append([0.0, -depth * 0.20, -half * 1.06])     # sternpost heel
    return points


def billow_sail(c, node, belly, direction=1.0):
    """Bezier-FFD belly on a sail box (local +Z = ship forward). Corners
    stay pinned (only interior control points move), the middle bulges
    toward the bow, the foot bulges slightly less than the center."""
    dz = belly * direction
    c.lattice_deform(node, wait=False, offsets=[
        [1, 0, 0, 0.0, 0.0, 0.70 * dz], [1, 0, 1, 0.0, 0.0, 0.70 * dz], [1, 0, 2, 0.0, 0.0, 0.70 * dz],
        [1, 1, 0, 0.0, 0.0, 1.00 * dz], [1, 1, 1, 0.0, 0.0, 1.00 * dz], [1, 1, 2, 0.0, 0.0, 1.00 * dz],
        [1, 2, 0, 0.0, 0.0, 0.55 * dz], [1, 2, 1, 0.0, 0.0, 0.55 * dz], [1, 2, 2, 0.0, 0.0, 0.55 * dz],
    ])


def ripple_pennant(c, node, amp):
    """S-curve ripple along a pennant's length (local X): divisions [3,1,1],
    alternating lateral offsets."""
    c.lattice_deform(node, wait=False, divisions=[3, 1, 1], offsets=[
        [1, 0, 0, 0.0, 0.0, +amp], [1, 1, 0, 0.0, 0.0, +amp],
        [1, 0, 1, 0.0, 0.0, +amp], [1, 1, 1, 0.0, 0.0, +amp],
        [2, 0, 0, 0.0, 0.0, -amp], [2, 1, 0, 0.0, 0.0, -amp],
        [2, 0, 1, 0.0, 0.0, -amp], [2, 1, 1, 0.0, 0.0, -amp],
    ])


class ShipYard:
    """Builds one ship as a single subtree. All positions are computed in
    ship-local space (+Z bow, y=0 waterline) and converted to world by
    to_world(); the root group carries only the ship's position, and the
    heading is applied to the root at the end (the subtree rides along)."""

    def __init__(self, c, m, name, position, heading_deg, length, beam,
                 depth, freeboard, masts, gunports=0, flagship=False,
                 heel_deg=0.0, hull_material=None, stripes=False,
                 deckhouses=False, sails_per_mast=2, clipper=False):
        self.c = c
        self.m = m
        self.name = name
        self.position = list(position)
        self.heading = heading_deg
        self.length = length
        self.beam = beam
        self.depth = depth
        self.freeboard = freeboard
        self.mast_fracs = masts      # [(z_frac, height, rake_deg), ...]
        self.gunports = gunports
        self.flagship = flagship
        self.heel = heel_deg
        # Style knobs (the Amerigo Vespucci look): hull material override,
        # white gun-deck stripes with dark ports instead of wales, white
        # deckhouses, three-sail stacks per mast.
        self.hull_material = hull_material
        self.stripes = stripes
        self.deckhouses = deckhouses
        self.sails_per_mast = sails_per_mast
        self.clipper = clipper
        if clipper:
            self.BEAM_PROFILE = [(-0.50, 0.62), (-0.34, 0.88), (-0.12, 1.00),
                                 (0.12, 0.97), (0.30, 0.82), (0.40, 0.60),
                                 (0.46, 0.36)]
        self.root = None
        # Sails/pennants collect here; their shadows on the water render as
        # harsh aliased spikes, so shadow_cast is disabled on them after the
        # build (hull + mast shadows stay for grounding).
        self.no_shadow_ids = []

    def to_world(self, local):
        return v_add(self.position, local)

    BEAM_PROFILE = [(-0.50, 0.62), (-0.34, 0.88), (-0.12, 1.00),
                    (0.12, 0.97), (0.32, 0.80), (0.46, 0.52)]

    def half_beam_at(self, z_frac):
        """Rail half-breadth at a length fraction (linear interpolation over
        the hull station table) - keeps rigging anchors ON the tapering
        hull instead of floating beside it."""
        profile = self.BEAM_PROFILE
        z_frac = max(profile[0][0], min(profile[-1][0], z_frac))
        for (z0, b0), (z1, b1) in zip(profile, profile[1:]):
            if z_frac <= z1:
                t = (z_frac - z0) / (z1 - z0)
                return 0.5 * self.beam * (b0 + t * (b1 - b0))
        return 0.5 * self.beam * profile[-1][1]

    def n(self, label):
        return f"{self.name} {label}"

    # ---------------------------------------------------------------- parts

    def build_hull(self):
        c, m = self.c, self.m
        self.root = c.group(self.name, self.position)
        points = hull_station_points(self.length, self.beam, self.depth,
                                     self.freeboard, clipper=self.clipper)
        hull_mat = self.hull_material or m["hull"]
        c.shape("convex_hull", self.n("Hull"), self.to_world([0, 0, 0]),
                points=points, motion_mode="none", reuse=False,
                material_name=hull_mat, parent_node_id=self.root)

        # All hull carves in ONE multi-tool difference: each CSG pass
        # re-triangulates the whole hull, so stacking a pass per pocket
        # leaves sliver-triangle shading artifacts (iteration 1 finding).
        carves = []

        # Deck well: carve the interior open so real bulwarks remain.
        # Well floor sits at deck level; walls ~7% beam thick.
        deck_y = self.freeboard * 0.45
        well_w = self.beam * 0.72
        well_l = self.length * 0.70
        well_h = self.freeboard * 2.5
        r = c.shape("box", self.n("deck carve"),
                    self.to_world([0.0, deck_y + well_h * 0.5, -self.length * 0.02]),
                    size=[well_w, well_h, well_l], motion_mode="none",
                    reuse=False, material_name=m["hull"])
        carves.append(r.get("node_id"))

        # Gunport pockets through each side: a row of boxes biting through
        # the planking between the wales.
        if self.gunports > 0:
            port_y = 0.45
            for side in (+1, -1):
                for i in range(self.gunports):
                    z = (i - (self.gunports - 1) * 0.5) * (self.length * 0.55 / self.gunports)
                    x = side * self.beam * 0.44
                    r = c.shape("box", self.n(f"port carve {side} {i}"),
                                self.to_world([x, port_y, z]),
                                size=[self.beam * 0.30, 0.52, 0.62],
                                motion_mode="none", reuse=False,
                                material_name=m["hull"])
                    carves.append(r.get("node_id"))

        # Clipper bow: the hull edge from waterline entry to stem head is a
        # straight convex-hull chord - scoop it concave with a beam-spanning
        # cylinder (capped equal-radius cone laid along X) whose surface
        # passes through both chord endpoints with the wanted sagitta. The
        # cut face IS the clipper stem profile; the circle parameters are
        # kept for the gold stem trim to trace (build_clipper_bow).
        if self.clipper:
            L, fb = self.length, self.freeboard
            E = [0.0, 0.0, L * 0.500]          # waterline entry
            H = [0.0, fb * 1.30, L * 0.565]    # stem head
            t0, t1 = 0.10, 0.90                # hollow spans this stem range
            P0 = [E[k] + (H[k] - E[k]) * t0 for k in range(3)]
            P1 = [E[k] + (H[k] - E[k]) * t1 for k in range(3)]
            dy, dz = P1[1] - P0[1], P1[2] - P0[2]
            c_len = math.hypot(dy, dz)
            ny, nz = -dz / c_len, dy / c_len   # forward-down chord normal
            sag = fb * 0.18
            radius = c_len * c_len / (8.0 * sag) + 0.5 * sag
            cy = (P0[1] + P1[1]) * 0.5 + ny * (radius - sag)
            cz = (P0[2] + P1[2]) * 0.5 + nz * (radius - sag)
            self.scoop = (cy, cz, radius, P0, P1)
            span = self.beam * 1.6
            r = c.shape("cone", self.n("bow scoop carve"),
                        self.to_world([span * 0.5, cy, cz]),
                        height=span, bottom_radius=radius, top_radius=radius,
                        slice_count=28, rotation_xyzw=roll_quat(90.0),
                        motion_mode="none", reuse=False,
                        material_name=m["hull"])
            carves.append(r.get("node_id"))

        # Flagship: three windows carved into the transom.
        if self.flagship:
            win_y = self.freeboard * 0.95
            for i, dx in enumerate((-0.9, 0.0, 0.9)):
                r = c.shape("box", self.n(f"window carve {i}"),
                            self.to_world([dx, win_y, -self.length * 0.515]),
                            size=[0.55, 0.7, 0.6], motion_mode="none",
                            reuse=False, material_name=m["hull"])
                carves.append(r.get("node_id"))

        # All csg/lattice ops in this builder target INDEPENDENT meshes, so
        # they go out with wait=False and build() settles ONCE at the end
        # (each wrapper settle sleeps at least a poll cycle).
        c.csg(self.n("Hull"), carves, "difference", wait=False)

        if self.flagship:
            # Warm cabin glow just behind the window openings
            c.shape("box", self.n("cabin glow"),
                    self.to_world([0.0, self.freeboard * 0.95, -self.length * 0.506]),
                    size=[2.7, 0.8, 0.06], motion_mode="none",
                    material_name=m["lantern"], parent_node_id=self.root)

        # Keel, rudder, wales (rubbing strakes) - attached parts, shared
        # brushes across the fleet where sizes repeat.
        c.shape("box", self.n("Keel"),
                self.to_world([0.0, -self.depth * 1.02, -self.length * 0.02]),
                size=[0.22, 0.3, self.length * 0.9], motion_mode="none",
                material_name=m["hull_dark"], parent_node_id=self.root)
        c.shape("box", self.n("Rudder"),
                self.to_world([0.0, -self.depth * 0.45, -self.length * 0.54]),
                size=[0.12, self.depth * 0.9, 0.8], motion_mode="none",
                material_name=m["hull_dark"], parent_node_id=self.root)
        # Wales only span the parallel midbody - straight boxes poke out of
        # the tapering ends otherwise (iteration 1 finding). Striped hulls
        # (Vespucci) get white gun-deck bands instead - see build_style().
        if not self.stripes:
            for wale_y in (0.12, self.freeboard * 0.62):
                c.shape("box", self.n(f"Wale {wale_y:.2f}"),
                        self.to_world([0.0, wale_y, -self.length * 0.02]),
                        size=[self.beam * 1.01, 0.12, self.length * 0.48],
                        motion_mode="none", material_name=m["trim"],
                        parent_node_id=self.root)

        # Bowsprit - rooted inside the forecastle block so it reads attached.
        # self.s scales fixed-size details with the ship (the sloop is 0.6,
        # the Vespucci ~1.9 - unscaled 0.14 m spars vanish on a 58 m hull).
        # Clipper bow (references: the real Vespucci): the bowsprit is much
        # SHALLOWER (~19 deg above horizontal, continuing the sheer line)
        # and roots in the raised forecastle just aft of the stem head.
        self.s = max(0.6, self.length / 30.0)
        L = self.length
        if self.clipper:
            self.sprit_base = [0.0, self.freeboard * 1.06, L * 0.42]
            self.sprit_pitch = 71.0
            self.sprit_len = L * 0.29
        else:
            self.sprit_base = [0.0, self.freeboard * 0.80, L * 0.38]
            self.sprit_pitch = 62.0
            self.sprit_len = L * 0.30
        c.shape("cone", self.n("Bowsprit"),
                self.to_world(self.sprit_base),
                height=self.sprit_len, bottom_radius=0.14 * self.s,
                top_radius=0.05 * self.s,
                rotation_xyzw=pitch_quat(self.sprit_pitch), motion_mode="none",
                material_name=m["mast"], parent_node_id=self.root)

    def bent_strip(self, label, curve_fn, size_x, size_z, material,
                   steps=12):
        """One lattice-bent box strip whose centerline follows curve_fn(t),
        t in [0,1], ship-local points. The box is created straight between
        curve_fn(0) and curve_fn(1) (local +Y along the chord), then a
        [1,3,1] bezier FFD moves the two interior control planes so the
        strip passes through the curve at t=1/3 and 2/3 (cubic bezier fit;
        the ends stay pinned). Smooth curved trim from ONE node - no
        faceted cone chains."""
        c = self.c
        p0 = curve_fn(0.0)
        p3 = curve_fn(1.0)
        d = [p3[k] - p0[k] for k in range(3)]
        ln = math.sqrt(sum(v * v for v in d))
        q = align_y_quaternion([v / ln for v in d])
        mid = [(p0[k] + p3[k]) * 0.5 for k in range(3)]
        r = c.shape("box", self.n(label), self.to_world(mid),
                    size=[size_x, ln * 1.01, size_z], steps=[2, steps, 2],
                    rotation_xyzw=q, motion_mode="none", reuse=False,
                    material_name=material, parent_node_id=self.root)
        node = r.get("node_id")

        def chord_dev(t):
            b = curve_fn(t)
            return [b[k] - (p0[k] + d[k] * t) for k in range(3)]

        d13, d23 = chord_dev(1.0 / 3.0), chord_dev(2.0 / 3.0)
        # Cubic bezier through the chord deviations: B(1/3) and B(2/3) with
        # pinned ends give the control-plane offsets C1, C2.
        c1 = [3.0 * d13[k] - 1.5 * d23[k] for k in range(3)]
        c2 = [3.0 * d23[k] - 1.5 * d13[k] for k in range(3)]
        qc = [-q[0], -q[1], -q[2], q[3]]     # world -> node local
        c1l, c2l = quat_rotate(qc, c1), quat_rotate(qc, c2)
        offsets = [[i, j, k] + list(cl)
                   for j, cl in ((1, c1l), (2, c2l))
                   for i in (0, 1) for k in (0, 1)]
        c.lattice_deform(node, divisions=[1, 3, 1], offsets=offsets,
                         wait=False)
        return node

    def bobstay(self, start):
        """Rope from a stem point to the bowsprit tip."""
        c, m = self.c, self.m
        pr = math.radians(self.sprit_pitch)
        sprit_dir = [0.0, math.cos(pr), math.sin(pr)]
        tip = [self.sprit_base[k] + sprit_dir[k] * self.sprit_len
               for k in range(3)]
        d = [tip[k] - start[k] for k in range(3)]
        length = math.sqrt(sum(v * v for v in d))
        q = align_y_quaternion([v / length for v in d])
        c.shape("cone", self.n("Bobstay"), self.to_world(start),
                rotation_xyzw=q, height=length, bottom_radius=0.025 * self.s,
                top_radius=0.025 * self.s, motion_mode="none",
                material_name=m["rope"], parent_node_id=self.root)

    def build_clipper_bow(self):
        """The Amerigo Vespucci bow (references: blueprint + side profiles):
        gold trim chained along the CONCAVE stem cut, a gilded statue
        figurehead standing on the stem head under the shallow bowsprit,
        hawse discs low on the flare, and a bobstay to the bowsprit tip.
        No beakhead rails - her bow is solid flared plating; the sheer
        scrollwork arc lives in build_style with the livery."""
        c, m, s = self.c, self.m, self.s
        L, fb = self.length, self.freeboard

        # Stem trim: a gold blade lattice-bent along the scoop circle so it
        # lies ON the concave cut face, standing slightly proud of it.
        cy, cz, radius, P0, P1 = self.scoop
        a0 = math.atan2(P0[1] - cy, P0[2] - cz)
        a1 = math.atan2(P1[1] - cy, P1[2] - cz)
        rr = radius - 0.03 * s

        def stem_curve(t):
            a = a0 + (a1 - a0) * t
            return [0.0, cy + rr * math.sin(a), cz + rr * math.cos(a)]

        self.bent_strip("Stem Trim", stem_curve, 0.09 * s, 0.18 * s,
                        m["gold"])

        # Figurehead: gilded standing figure leaning out over the stem -
        # robe cone, torso cone, head sphere, two swept-back arm blades.
        # Sized MONUMENTAL (the real figure is ~4 m gilded bronze): a
        # human-proportioned one read as a flagpole knob at fleet
        # distance. The robe base sits ON the scoop-circle stem surface
        # and its lean FOLLOWS the stem tangent there (minus a few
        # degrees upright) so the figure flows out of the raked stem -
        # both a hardcoded z and a fixed lean read detached.
        base_y = fb * 1.02
        z_surf = cz - math.sqrt(max(0.0, radius * radius
                                    - (base_y - cy) ** 2))
        base = [0.0, base_y, z_surf + 0.02 * s]
        dy, dz = base_y - cy, z_surf - cz
        lean = math.degrees(math.atan2(dy, -dz)) - 8.0
        lr = math.radians(lean)
        up = [0.0, math.cos(lr), math.sin(lr)]
        robe_h = 1.00 * s
        torso_h = 0.52 * s
        c.shape("cone", self.n("Figurehead Robe"), self.to_world(base),
                height=robe_h, bottom_radius=0.16 * s, top_radius=0.09 * s,
                rotation_xyzw=pitch_quat(lean), motion_mode="none",
                material_name=m["gold"], parent_node_id=self.root)
        shoulder = [base[k] + up[k] * robe_h for k in range(3)]
        c.shape("cone", self.n("Figurehead Torso"), self.to_world(shoulder),
                height=torso_h, bottom_radius=0.10 * s, top_radius=0.055 * s,
                rotation_xyzw=pitch_quat(lean), motion_mode="none",
                material_name=m["gold"], parent_node_id=self.root)
        head = [shoulder[k] + up[k] * (torso_h + 0.08 * s) for k in range(3)]
        c.shape("uv_sphere", self.n("Figurehead Head"), self.to_world(head),
                radius=0.10 * s, slice_count=10, stack_count=8,
                motion_mode="none", material_name=m["gold"],
                parent_node_id=self.root)
        # Arm blades sweep back-down along the sheer (elongated spheres).
        for side in (+1, -1):
            arm_dir = [side * 0.35, -0.45, -0.82]
            ln = math.sqrt(sum(v * v for v in arm_dir))
            q = align_y_quaternion([v / ln for v in arm_dir])
            c.shape("uv_sphere", self.n(f"Figurehead Arm {side}"),
                    self.to_world([shoulder[0] + side * 0.08 * s,
                                   shoulder[1], shoulder[2]]),
                    radius=1.0, scale=[0.045 * s, 0.22 * s, 0.045 * s],
                    slice_count=8, stack_count=6, rotation_xyzw=q,
                    motion_mode="none", material_name=m["gold"],
                    parent_node_id=self.root)

        # Hawse discs on the bow flare (proud of the bulged surface), with
        # an anchor nested below and a chain rod between them - the
        # generic midship anchor floats beside the narrowed clipper bow.
        for side in (+1, -1):
            hawse = [side * self.half_beam_at(0.43) * 1.00, fb * 0.45, L * 0.43]
            c.shape("disc", self.n(f"Hawse {side}"),
                    self.to_world(hawse),
                    outer_radius=0.10 * s, slice_count=12,
                    rotation_xyzw=yaw_quat(side * 104.0), motion_mode="none",
                    material_name=m["hull_dark"], parent_node_id=self.root)
        anchor = [self.half_beam_at(0.415) * 1.02, fb * 0.38, L * 0.415]
        c.shape("torus", self.n("Bow Anchor"), self.to_world(anchor),
                major_radius=0.16 * s, minor_radius=0.035 * s, major_steps=14,
                minor_steps=8, motion_mode="none", reuse=False,
                material_name=m["iron"], parent_node_id=self.root)
        c.shape("capsule", self.n("bow anchor shank"),
                self.to_world([anchor[0], anchor[1] - 0.30 * s, anchor[2]]),
                length=0.45 * s, bottom_radius=0.035 * s, top_radius=0.035 * s,
                motion_mode="none", reuse=False, material_name=m["iron"])
        c.csg(self.n("Bow Anchor"), self.n("bow anchor shank"), "union",
              wait=False)
        hawse_r = [self.half_beam_at(0.43) * 1.00, fb * 0.45, L * 0.43]
        d = [anchor[k] - hawse_r[k] for k in range(3)]
        ln = math.sqrt(sum(v * v for v in d))
        c.shape("cone", self.n("Anchor Chain"), self.to_world(hawse_r),
                rotation_xyzw=align_y_quaternion([v / ln for v in d]),
                height=ln, bottom_radius=0.03 * s, top_radius=0.03 * s,
                motion_mode="none", material_name=m["iron"],
                parent_node_id=self.root)

        self.bobstay([0.0, 0.25, L * 0.492])

    def build_bow_detail(self):
        """Bow furniture, scaled by self.s: cutwater blade along the stem,
        gilded figurehead under the bowsprit, beakhead rails from the
        forecastle to the stem head, hawse discs, and a bobstay rope from
        the cutwater to the bowsprit tip."""
        if self.clipper:
            self.build_clipper_bow()
            return
        c, m, s = self.c, self.m, self.s
        L, fb, dep = self.length, self.freeboard, self.depth

        # Cutwater: thin blade proud of the stem, leaning with the stem
        # line (forefoot -> stem head).
        # The hull surface bulges past the authored stem points, so all bow
        # furniture sits FORWARD of the stem line or it ends up buried
        # inside the prow block (iteration finding).
        stem_head = [0.0, fb * 1.45, L * 0.52]
        forefoot = [0.0, -dep * 0.45, L * 0.46]
        dy = stem_head[1] - forefoot[1]
        dz = stem_head[2] - forefoot[2]
        stem_pitch = math.degrees(math.atan2(dz, dy))
        mid = [0.0, (stem_head[1] + forefoot[1]) * 0.5,
               (stem_head[2] + forefoot[2]) * 0.5 + 0.55 * s]
        c.shape("box", self.n("Cutwater"), self.to_world(mid),
                size=[0.12 * s, dy * 1.02, 0.7 * s],
                rotation_xyzw=pitch_quat(stem_pitch), motion_mode="none",
                material_name=m["trim"], parent_node_id=self.root)

        # Figurehead: gilded bust on the stem head - sphere head over a
        # forward-leaning cone torso.
        c.shape("cone", self.n("Figurehead Torso"),
                self.to_world([0.0, fb * 1.05, L * 0.535]),
                height=0.62 * s, bottom_radius=0.11 * s,
                top_radius=0.05 * s, rotation_xyzw=pitch_quat(35.0),
                motion_mode="none", material_name=m["gold"],
                parent_node_id=self.root)
        c.shape("uv_sphere", self.n("Figurehead"),
                self.to_world([0.0, fb * 1.05 + 0.52 * s, L * 0.535 + 0.40 * s]),
                radius=0.13 * s, slice_count=10, stack_count=8,
                motion_mode="none", material_name=m["gold"],
                parent_node_id=self.root)

        # Beakhead rails: two rods per side from the forecastle rail to the
        # stem head (base-origin cones - base AT the start point).
        for side in (+1, -1):
            for k, (y0, y1) in enumerate(((fb * 1.02, fb * 1.38),
                                          (fb * 0.78, fb * 1.20))):
                start = [side * self.half_beam_at(0.38) * 0.85, y0, L * 0.38]
                end = [side * 0.04 * s, y1, L * 0.535]
                d = [end[0] - start[0], end[1] - start[1], end[2] - start[2]]
                length = math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2)
                q = align_y_quaternion([v / length for v in d])
                c.shape("cone", self.n(f"Beak Rail {side}.{k}"),
                        self.to_world(start), rotation_xyzw=q,
                        height=length, bottom_radius=0.035 * s,
                        top_radius=0.035 * s, motion_mode="none",
                        material_name=m["trim"], parent_node_id=self.root)

        # Hawse holes: dark discs on the bow flare (disc faces +/-Z; yaw
        # +/-100 deg turns it outward with a touch of flare).
        for side in (+1, -1):
            c.shape("disc", self.n(f"Hawse {side}"),
                    self.to_world([side * self.half_beam_at(0.40) * 0.90,
                                   fb * 0.52, L * 0.40]),
                    outer_radius=0.10 * s, slice_count=12,
                    rotation_xyzw=yaw_quat(side * 100.0), motion_mode="none",
                    material_name=m["hull_dark"], parent_node_id=self.root)

        # Bobstay: rope from the cutwater waterline to the bowsprit tip.
        self.bobstay([0.0, 0.15, L * 0.53])

    def build_style(self):
        """Hull livery beyond the default wales: the Amerigo Vespucci look -
        two white gun-deck stripes with a row of dark ports, white
        deckhouses on the main deck, and a gilded bow scroll."""
        c, m, s = self.c, self.m, self.s
        L, fb = self.length, self.freeboard
        deck_y = fb * 0.45

        if self.stripes:
            # The white stripes run the FULL hull into the stem on the real
            # ship. Boxes only work on the parallel midbody; in the bow
            # taper a rectangular box either shelves out or buries, so the
            # fore run is a chained TUBE per side that follows the beam
            # profile, rises with the sheer, and converges onto the concave
            # stem cut (its end point is computed from the scoop circle).
            def sheer_rise(zf):
                if zf <= 0.30:
                    return 0.0
                if zf <= 0.40:
                    return 0.06 * (zf - 0.30) / 0.10
                return 0.06 + 0.08 * min(1.0, (zf - 0.40) / 0.06)

            def polyline_curve(pts):
                def fn(t, pts=pts):
                    seg = t * (len(pts) - 1)
                    i = min(int(seg), len(pts) - 2)
                    f = seg - i
                    return [pts[i][k] + (pts[i + 1][k] - pts[i][k]) * f
                            for k in range(3)]
                return fn

            cy, cz, radius, _, _ = self.scoop
            for band, stripe_y in enumerate((fb * 0.28, fb * 0.68)):
                # Whole stripe = three lattice-bent strips per side (aft
                # taper, midbody, bow run) with ONE cross-section - a
                # box/strip mix showed as steps at every joint. Hug factor
                # per band: the hull FLARES, so at the lower stripe's
                # height the surface sits inside the rail half-beam
                # (iteration 2: the lower strip stood off the bow like a
                # fin). The bow strip ends just AFT of the stem edge
                # instead of wrapping past it.
                hug = 1.04 if band == 0 else 1.05
                y_end = stripe_y + fb * 0.14
                z_end = cz - math.sqrt(max(0.0, radius * radius
                                           - (y_end - cy) ** 2)) - 0.06 * s
                for side in (+1, -1):
                    def hull_pt(zf):
                        return [side * self.half_beam_at(zf) * hug,
                                stripe_y + fb * sheer_rise(zf), zf * L]

                    runs = {
                        "aft": [hull_pt(zf) for zf in
                                (-0.475, -0.38, -0.27, -0.12)],
                        "mid": [hull_pt(zf) for zf in
                                (-0.12, 0.0, 0.12, 0.235)],
                        "bow": [hull_pt(zf) for zf in
                                (0.235, 0.30, 0.36, 0.41, 0.45)]
                               + [[side * 0.05 * s, y_end, z_end]],
                    }
                    for run, pts in runs.items():
                        self.bent_strip(f"Stripe {band} {run} {side}",
                                        polyline_curve(pts), 0.12 * s,
                                        0.16 * s, m["white"])
                # Dark ports: thin boxes spanning the beam, poking through
                # the stripe on both sides; two extra chase the bow taper.
                # Dark ports: thin plates just proud of each strip FACE.
                # Beam-spanning boxes poked out of the tapering hull as
                # brown tabs riding on the stripes (iteration 5).
                batch = c.part_batch()
                port_zfs = ([-0.02 + (i - 4) * (0.52 / 9) for i in range(9)]
                            + [0.31, 0.40])
                for side in (+1, -1):
                    for k, zf in enumerate(port_zfs):
                        x = self.half_beam_at(zf) * hug + 0.065 * s
                        batch.part("box", self.n(f"Port {band}.{side}.{k}"),
                                   self.to_world([side * x,
                                                  stripe_y + fb * sheer_rise(zf),
                                                  zf * L]),
                                   parent_node_id=self.root,
                                   material_name=m["hull_dark"],
                                   size=[0.08 * s, 0.09 * s, 0.30 * s])
                batch.flush()
            # Gold sheer line: the references run a continuous gilded band
            # at rail level the FULL hull length; the bow scroll below
            # continues it forward, so the boxes stop where the scroll
            # starts.
            for zc, zl in ((-0.3775, 0.195), (-0.03, 0.50)):
                c.shape("box", self.n(f"Sheer Line @{zc}"),
                        self.to_world([0.0, fb * 0.98, zc * L]),
                        size=[2.0 * self.half_beam_at(zc) * 1.05,
                              0.10 * s, zl * L],
                        motion_mode="none", material_name=m["gold"],
                        parent_node_id=self.root)
            # Gilded bow scroll: a gold ARC sweeping from the sheer up and
            # forward into the figurehead (references show a curved band,
            # not a straight one) - quadratic bezier, one lattice-bent
            # strip per side.
            for side in (+1, -1):
                p0 = [side * self.half_beam_at(0.26) * 1.00, fb * 0.98, L * 0.26]
                p1 = [side * self.half_beam_at(0.40) * 1.06, fb * 1.20, L * 0.42]
                p2 = [side * 0.06 * s, fb * 1.26, L * 0.55]
                self.bent_strip(
                    f"Bow Scroll {side}",
                    lambda t, p0=p0, p1=p1, p2=p2: bezier2(p0, p1, p2, t),
                    0.08 * s, 0.11 * s, m["gold"])

        if self.deckhouses:
            # Tall enough to peek over the bulwark (deck sits at fb*0.45,
            # the rail at fb - a house shorter than ~0.55*fb/s vanishes).
            houses = [
                (0.20, 0.42, 1.30, 0.13),   # (z_frac, width frac, height, length frac)
                (-0.06, 0.50, 1.60, 0.17),
                (-0.34, 0.40, 1.20, 0.11),
            ]
            for k, (z_frac, w_frac, h, l_frac) in enumerate(houses):
                c.shape("box", self.n(f"Deckhouse {k}"),
                        self.to_world([0.0, deck_y + h * 0.5 * s, z_frac * L]),
                        size=[self.beam * w_frac, h * s, L * l_frac],
                        motion_mode="none", material_name=m["white"],
                        parent_node_id=self.root)
            # Gold name band across the transom.
            c.shape("box", self.n("Stern Band"),
                    self.to_world([0.0, fb * 1.05, -L * 0.508]),
                    size=[self.beam * 0.55, 0.14 * s, 0.10],
                    motion_mode="none", material_name=m["gold"],
                    parent_node_id=self.root)

    def build_mast(self, index, z_frac, height, rake_deg):
        """Mast + yards + square sails + pennant. Returns nothing; children
        parent to the root group with world positions."""
        c, m = self.c, self.m
        base_z = z_frac * self.length
        deck_y = self.freeboard * 0.45
        rake = pitch_quat(-rake_deg)  # lean aft slightly
        mast = c.shape("cone", self.n(f"Mast {index}"),
                       self.to_world([0.0, deck_y, base_z]),
                       height=height, bottom_radius=0.16 * self.s,
                       top_radius=0.07 * self.s,
                       slice_count=10, rotation_xyzw=rake,
                       motion_mode="none", material_name=m["mast"],
                       parent_node_id=self.root)
        mast_id = mast.get("node_id")

        # Square sail stacks: course (big, low) upward to smaller sails.
        sail_stacks = {
            2: [(0.32, 0.95, 0.42), (0.68, 0.62, 0.26)],
            3: [(0.26, 0.95, 0.34), (0.55, 0.76, 0.25), (0.79, 0.56, 0.17)],
        }
        sails = sail_stacks[self.sails_per_mast]
        # Full-rigged stacks carry proportionally wider yards, or the tall
        # masts read as ribbons against a long hull.
        yard_span = height * (0.72 if self.sails_per_mast >= 3 else 0.62)
        for s, (h_frac, w_frac, sh_frac) in enumerate(sails):
            y = deck_y + height * h_frac
            span = yard_span * w_frac
            sail_h = height * sh_frac
            yard_y = y + sail_h * 0.52
            if s == 0:
                # Course foot raised half way up (yard stays): the
                # full-depth course hung down into the bulwarks and
                # deckhouses.
                y += sail_h * 0.25
                sail_h *= 0.5
            # Yard (horizontal spar)
            c.shape("capsule", self.n(f"Yard {index}.{s}"),
                    self.to_world([0.0, yard_y, base_z]),
                    length=span, bottom_radius=0.06 * self.s,
                    top_radius=0.06 * self.s,
                    rotation_xyzw=roll_quat(90.0), motion_mode="none",
                    material_name=m["mast"], parent_node_id=mast_id)
            # Sail: subdivided thin box, belly billowed forward with FFD.
            sail = c.shape("box", self.n(f"Sail {index}.{s}"),
                           self.to_world([0.0, y, base_z + 0.18]),
                           size=[span * 0.96, sail_h, 0.05],
                           steps=[8, 8, 1], motion_mode="none", reuse=False,
                           material_name=m["canvas"], parent_node_id=mast_id)
            billow_sail(c, sail.get("node_id"), belly=sail_h * 0.62)
            self.no_shadow_ids.append(sail.get("node_id"))

        # Crow's nest on the tallest mast: cone minus inner cone = open cup.
        if index == self.tallest_mast_index():
            ns = self.s
            nest_y = deck_y + height * 0.80
            c.shape("cone", self.n("Crows Nest"),
                    self.to_world([0.0, nest_y, base_z]),
                    height=0.55 * ns, bottom_radius=0.34 * ns,
                    top_radius=0.46 * ns,
                    slice_count=14, motion_mode="none", reuse=False,
                    material_name=m["trim"], parent_node_id=mast_id)
            c.shape("cone", self.n("nest carve"),
                    self.to_world([0.0, nest_y + 0.12 * ns, base_z]),
                    height=0.6 * ns, bottom_radius=0.26 * ns,
                    top_radius=0.40 * ns,
                    slice_count=14, motion_mode="none", reuse=False,
                    material_name=m["trim"])
            c.csg(self.n("Crows Nest"), self.n("nest carve"), "difference",
                  wait=False)

        # Masthead pennant: thin box rippled with an S-curve FFD.
        pen = c.shape("box", self.n(f"Pennant {index}"),
                      self.to_world([1.1 * self.s, deck_y + height + 0.25, base_z]),
                      size=[2.2 * self.s, 0.28 * self.s, 0.02], steps=[9, 2, 1],
                      motion_mode="none", reuse=False,
                      material_name=m["flag"], parent_node_id=mast_id)
        ripple_pennant(c, pen.get("node_id"), amp=0.16 * self.s)
        self.no_shadow_ids.append(pen.get("node_id"))

        # Shrouds: stays from masthead to the rails (thin capsules).
        top = [0.0, deck_y + height * 0.92, base_z]
        rail_y = self.freeboard * 0.95
        batch = c.part_batch()
        for side in (+1, -1):
            for k, dz in enumerate((-0.08, -0.02, 0.04)):
                z_frac = base_z / self.length + dz
                anchor = [side * self.half_beam_at(z_frac) * 0.94, rail_y,
                          (z_frac) * self.length]
                d = [anchor[0] - top[0], anchor[1] - top[1], anchor[2] - top[2]]
                length = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
                q = align_y_quaternion([v / length for v in d])
                # Cones are BASE-origin: place the base at the masthead and
                # aim at the anchor, or the rod overshoots past the rail.
                batch.part("cone", self.n(f"Shroud {index}.{side}.{k}"),
                           self.to_world(top), rotation_xyzw=q,
                           parent_node_id=mast_id,
                           material_name=m["rope"],
                           height=length, bottom_radius=0.02 * self.s,
                           top_radius=0.02 * self.s)
        batch.flush()

    def tallest_mast_index(self):
        return max(range(len(self.mast_fracs)),
                   key=lambda i: self.mast_fracs[i][1])

    def build_fittings(self):
        c, m = self.c, self.m
        deck_y = self.freeboard * 0.45
        L = self.length

        if self.flagship:
            # Ship's wheel: torus rim UNIONED with two crossing spoke
            # capsules, standing on the quarterdeck.
            wheel_z = -L * 0.30
            wheel_pos = [0.0, deck_y + 1.05, wheel_z]
            c.shape("torus", self.n("Wheel"), self.to_world(wheel_pos),
                    major_radius=0.45, minor_radius=0.05, major_steps=18,
                    minor_steps=8, motion_mode="none", reuse=False,
                    material_name=m["trim"], parent_node_id=self.root)
            spokes = []
            for k, ang in enumerate((0.0, 60.0, 120.0)):
                r = c.shape("capsule", self.n(f"wheel spoke {k}"),
                            self.to_world(wheel_pos),
                            length=1.05, bottom_radius=0.035, top_radius=0.035,
                            rotation_xyzw=roll_quat(ang), motion_mode="none",
                            reuse=False, material_name=m["trim"])
                spokes.append(r.get("node_id"))
            c.csg(self.n("Wheel"), spokes, "union", wait=False)
            c.shape("capsule", self.n("Wheel Post"),
                    self.to_world([0.0, deck_y + 0.5, wheel_z - 0.15]),
                    length=0.9, bottom_radius=0.07, top_radius=0.07,
                    motion_mode="none", material_name=m["mast"],
                    parent_node_id=self.root)

            # Two bow chasers with CSG-bored muzzles.
            for side in (+1, -1):
                gz = L * 0.30
                gun_pos = [side * self.beam * 0.22, deck_y + 0.35, gz]
                c.shape("cone", self.n(f"Cannon {side}"),
                        self.to_world(gun_pos),
                        height=1.5, bottom_radius=0.16, top_radius=0.10,
                        slice_count=12, rotation_xyzw=pitch_quat(88.0),
                        motion_mode="none", reuse=False,
                        material_name=m["iron"], parent_node_id=self.root)
                c.shape("cone", self.n(f"bore {side}"),
                        self.to_world([gun_pos[0], gun_pos[1], gz + 1.1]),
                        height=0.6, bottom_radius=0.06, top_radius=0.06,
                        slice_count=10, rotation_xyzw=pitch_quat(88.0),
                        motion_mode="none", reuse=False,
                        material_name=m["iron"])
                c.csg(self.n(f"Cannon {side}"), self.n(f"bore {side}"),
                      "difference", wait=False)
                c.shape("box", self.n(f"Carriage {side}"),
                        self.to_world([gun_pos[0], deck_y + 0.14, gz - 0.25]),
                        size=[0.4, 0.28, 0.8], motion_mode="none",
                        material_name=m["hull_dark"], parent_node_id=self.root)

            # Cannonball pyramid (regular_polyhedron would lie - balls are
            # balls; the icosahedron shows up as the binnacle base instead).
            batch = c.part_batch()
            base = [self.beam * 0.18, deck_y + 0.08, -L * 0.10]
            k = 0
            for layer, count in ((0.0, 3), (0.14, 1)):
                for i in range(count):
                    batch.part("uv_sphere", self.n(f"Ball {k}"),
                               self.to_world([base[0] + (i - count * 0.5 + 0.5) * 0.17,
                                              base[1] + layer, base[2]]),
                               parent_node_id=self.root,
                               material_name=m["iron"], radius=0.08)
                    k += 1
            batch.flush()

            # Binnacle: icosahedron compass housing on a post.
            c.shape("regular_polyhedron", self.n("Binnacle"),
                    self.to_world([0.0, deck_y + 0.95, -L * 0.26]),
                    kind="icosahedron", radius=0.16, motion_mode="none",
                    material_name=m["gold"], parent_node_id=self.root)

        # Anchor at the bow (non-clipper ships - the clipper bow carries
        # its own hawse-nested anchor in build_clipper_bow; the generic one
        # floats beside the narrowed clipper entry).
        s = self.s
        if self.clipper:
            self.build_stern_lantern()
            return
        anchor_pos = [self.beam * 0.52, self.freeboard * 0.55, L * 0.34]
        c.shape("torus", self.n("Anchor"), self.to_world(anchor_pos),
                major_radius=0.16 * s, minor_radius=0.035 * s, major_steps=14,
                minor_steps=8, motion_mode="none", reuse=False,
                material_name=m["iron"], parent_node_id=self.root)
        c.shape("capsule", self.n("anchor shank"),
                self.to_world([anchor_pos[0], anchor_pos[1] - 0.42 * s, anchor_pos[2]]),
                length=0.7 * s, bottom_radius=0.035 * s, top_radius=0.035 * s,
                motion_mode="none", reuse=False, material_name=m["iron"])
        c.csg(self.n("Anchor"), self.n("anchor shank"), "union", wait=False)
        c.shape("capsule", self.n("Anchor Stock"),
                self.to_world([anchor_pos[0], anchor_pos[1] - 0.68 * s, anchor_pos[2]]),
                length=0.55 * s, bottom_radius=0.03 * s, top_radius=0.03 * s,
                rotation_xyzw=pitch_quat(90.0), motion_mode="none",
                material_name=m["mast"], parent_node_id=self.root)

        self.build_stern_lantern()

    def build_stern_lantern(self):
        # Stern lantern on a short post at the taffrail (all ships)
        c, m, s, L = self.c, self.m, self.s, self.length
        c.shape("capsule", self.n("Lantern Post"),
                self.to_world([0.0, self.freeboard * 1.28, -L * 0.495]),
                length=0.5 * s, bottom_radius=0.03 * s, top_radius=0.03 * s,
                motion_mode="none", material_name=m["mast"],
                parent_node_id=self.root)
        c.shape("uv_sphere", self.n("Stern Lantern"),
                self.to_world([0.0, self.freeboard * 1.28 + 0.36 * s, -L * 0.495]),
                radius=0.14 * s, slice_count=10, stack_count=8,
                motion_mode="none", material_name=m["lantern"],
                parent_node_id=self.root)

    def build(self):
        self.build_hull()
        self.build_bow_detail()
        self.build_style()
        for i, (z_frac, height, rake) in enumerate(self.mast_fracs):
            self.build_mast(i, z_frac, height, rake)
        self.build_fittings()
        # Heading + a touch of heel, applied to the root so the whole
        # subtree turns as one object.
        q = quat_mul(yaw_quat(self.heading), roll_quat(self.heel))
        self.c.set_node_transform(self.root, rotation_xyzw=q)
        self.c.mutate("set_item_flags", {
            "scene_name": self.c.scene,
            "ids": [i for i in self.no_shadow_ids if i],
            "flags": ["shadow_cast"], "enabled": False,
        })
        # One settle for every wait=False csg/lattice op issued above.
        self.c.settle()


# --------------------------------------------------------------------- main

def main():
    args = standard_args("Sail Ships")
    if reframe(args, "Sail Ships", "logs/creations/sail_ships", SHOTS):
        return
    c = Creation("Sail Ships", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    c.ambience(ambient=[0.16, 0.17, 0.20],
               clear_color=[0.72, 0.60, 0.48, 1.0], grid=False,
               sky={"_version": 3, "enabled": True, "mode": 1})

    m = {
        "hull":      c.make_material("oak planking", base_color=[0.30, 0.19, 0.10], roughness=0.85, metallic=0.0),
        "hull_dark": c.make_material("tarred oak",   base_color=[0.13, 0.09, 0.06], roughness=0.9,  metallic=0.0),
        "trim":      c.make_material("teak trim",    base_color=[0.45, 0.29, 0.14], roughness=0.7,  metallic=0.0),
        "mast":      c.make_material("pine spar",    base_color=[0.52, 0.40, 0.24], roughness=0.8,  metallic=0.0),
        "canvas":    c.make_material("sailcloth",    base_color=[0.87, 0.82, 0.70], roughness=0.95, metallic=0.0),
        "rope":      c.make_material("hemp rope",    base_color=[0.42, 0.35, 0.22], roughness=1.0,  metallic=0.0),
        "iron":      c.make_material("cast iron",    base_color=[0.10, 0.10, 0.11], roughness=0.45, metallic=0.9),
        "gold":      c.make_material("brass",        base_color=[0.85, 0.65, 0.25], roughness=0.35, metallic=1.0),
        "flag":      c.make_material("pennant red",  base_color=[0.70, 0.08, 0.06], roughness=0.8,  metallic=0.0),
        "lantern":   c.make_material("lantern glow", base_color=[1.0, 0.75, 0.35],  roughness=0.6,  metallic=0.0,
                                     emissive=[2.6, 1.7, 0.7]),
        "hull_black": c.make_material("black steel", base_color=[0.035, 0.035, 0.045], roughness=0.5, metallic=0.2),
        "white":     c.make_material("ship white",   base_color=[0.90, 0.90, 0.87],  roughness=0.6,  metallic=0.0),
        "sea":       c.make_material("sea water",    base_color=[0.02, 0.15, 0.28], roughness=0.10, metallic=0.0,
                                     blending_mode="alpha_blend", opacity=0.25),
        "seabed":    c.make_material("seabed",       base_color=[0.10, 0.17, 0.20], roughness=1.0,  metallic=0.0),
    }

    # Lights first (SKILL rule): golden-hour sun low off the port bows.
    c.light("directional", "Evening Sun", [0.0, 40.0, 0.0],
            [1.0, 0.78, 0.52], 2.6)
    pitch = math.radians(-150.0)
    yaw = math.radians(55.0)
    qy = [0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)]
    qx = [math.sin(pitch / 2), 0.0, 0.0, math.cos(pitch / 2)]
    c.set_node_transform("Evening Sun", rotation_xyzw=quat_mul(qy, qx))
    c.light("point", "Sky Fill", [-20.0, 30.0, 30.0], [0.55, 0.62, 0.80],
            260.0, range=140.0, cast_shadow=False)
    # z_far decoupled: the 600 m sea plane far-clips at the default
    # shadow-coupled 140/160 m and the horizon shows as a hard band.
    c.shadow_range(140.0, z_far=900.0)

    # Sea: OPEN-OCEAN recipe - deep dark bottom under near-opaque water, so
    # the surface reads blue everywhere instead of showing seabed shadows
    # (the SKILL's bright-seabed recipe is for shallow lagoons); 600 m
    # across so the box edge stays past the horizon line.
    c.shape("box", "Seabed", [0.0, -6.0, 0.0], size=[600.0, 1.0, 600.0],
            material_name=m["seabed"])
    c.shape("box", "Sea", [0.0, -2.75, 0.0], size=[600.0, 5.5, 600.0],
            motion_mode="none", material_name=m["sea"])

    # ------------------------------------------------------------- the fleet
    # Headings face the evening sun: the sun light travels toward
    # (+0.71, -0.50, +0.50), so the sun sits toward (-0.71, -0.50) on the
    # horizon = heading ~ -125 deg; ships keep a few degrees of spread.
    flagship = ShipYard(
        c, m, "Galleon Aurora", [0.0, 0.0, 0.0], heading_deg=-122.0,
        length=30.0, beam=7.6, depth=2.6, freeboard=2.6,
        masts=[(0.28, 13.0, 4.0), (-0.02, 16.0, 6.0), (-0.30, 11.0, 8.0)],
        gunports=5, flagship=True, heel_deg=3.0)
    flagship.build()

    brig = ShipYard(
        c, m, "Brig Meri", [-30.0, 0.0, -2.0], heading_deg=-116.0,
        length=18.0, beam=5.0, depth=1.8, freeboard=1.9,
        masts=[(0.20, 10.0, 4.0), (-0.22, 11.5, 6.0)],
        gunports=3, heel_deg=4.0)
    brig.build()

    sloop = ShipYard(
        c, m, "Sloop Tuuli", [15.0, 0.0, -17.0], heading_deg=-130.0,
        length=12.0, beam=3.4, depth=1.3, freeboard=1.3,
        masts=[(-0.05, 9.0, 5.0)],
        gunports=0, heel_deg=5.0)
    sloop.build()

    # The nave scuola herself: black steel hull, two white gun-deck
    # stripes with a row of dark ports, white deckhouses, gilded bow
    # scroll and figurehead, full-rigged three-sail stacks. Placed astern
    # of the squadron so her size reads against the galleon.
    vespucci = ShipYard(
        c, m, "Amerigo Vespucci", [-20.0, 0.0, -60.0], heading_deg=-118.0,
        length=58.0, beam=10.0, depth=3.4, freeboard=4.4,
        masts=[(0.30, 28.0, 2.0), (0.0, 30.0, 3.0), (-0.30, 24.0, 4.0)],
        gunports=0, heel_deg=2.0, hull_material=m["hull_black"],
        stripes=True, deckhouses=True, sails_per_mast=3, clipper=True)
    vespucci.build()

    # ------------------------------------------------------------- verify
    print(f"nodes: {len(c.nodes())}")

    # ------------------------------------------------------------- cameras
    c.shadow_range(160.0, z_far=900.0)
    c.screenshot_views("logs/creations/sail_ships", SHOTS)

    if not args.no_save:
        c.save("res/editor/scenes/creations/sail_ships.glb")
    print("Sail Ships complete.")


if __name__ == "__main__":
    main()
