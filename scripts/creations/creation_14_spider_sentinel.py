#!/usr/bin/env python3
"""Creation 14: Spider Sentinel.

The ragdoll spider (scripts/mcp_ragdoll_spider.py) reborn as a creation -
and this time it STANDS. A 50-part bronze spider (2 body capsules + 8 legs
of 6 tapered capsule segments each) is laced together with 49 six-dof
joints whose angular drives act as muscles: position_target 0 captures the
authored standing pose as the rest pose, and graduated stiffness (strong
hips, soft toes) holds the body off the ground under full gravity - feet
planted on the floor, no gravity_factor tricks. A shove with
apply_physics_force makes it stagger; the motors pull it back upright.
Showcases: rest-pose motor joints as load-bearing muscles (not just sway
springs), graduated per-joint motor tuning along a limb chain,
create_physics_body with explicit masses on motion_mode="none" shapes,
apply_physics_force staggering, analytic capsule-capsule clearance check.

Physics rig pattern:
- every part is created motion_mode="none" (no body), rotated into pose,
  then given a body via create_physics_body shape="auto" with an explicit
  mass (create_shape cannot set mass, and edit_physics_body's mass edit
  does not rescale inertia),
- per joint: two coincident anchor child nodes at the anatomical pivot
  (one per part), joined with shared motor settings - linear locked,
  angular limited, drives on all three angular axes,
- everything is rigged with the simulation DISABLED so the drives capture
  the standing pose, then physics is enabled and the bodies woken.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import (  # noqa: E402
    Creation, standard_args, align_y_quaternion, body_axis_elevation,
    hierarchy_report, probe_pose, rest_rotation,
    v_add, v_sub, v_scale, v_dot, v_distance, v_norm,
)


def segment_closest_parameters(p1, q1, p2, q2):
    """Closest points between segments p1-q1 and p2-q2 (Ericson, RTCD 5.1.9).
    Returns (s, t, distance) with s, t in [0, 1]."""
    d1 = v_sub(q1, p1)
    d2 = v_sub(q2, p2)
    r  = v_sub(p1, p2)
    a  = v_dot(d1, d1)
    e  = v_dot(d2, d2)
    f  = v_dot(d2, r)
    epsilon = 1.0e-12
    if (a <= epsilon) and (e <= epsilon):
        s, t = 0.0, 0.0
    elif a <= epsilon:
        s = 0.0
        t = min(max(f / e, 0.0), 1.0)
    else:
        c = v_dot(d1, r)
        if e <= epsilon:
            t = 0.0
            s = min(max(-c / a, 0.0), 1.0)
        else:
            b = v_dot(d1, d2)
            denominator = a * e - b * b
            s = min(max((b * f - c * e) / denominator, 0.0), 1.0) if denominator > epsilon else 0.0
            t = (b * s + f) / e
            if t < 0.0:
                t = 0.0
                s = min(max(-c / a, 0.0), 1.0)
            elif t > 1.0:
                t = 1.0
                s = min(max((b - c) / a, 0.0), 1.0)
    point_1 = v_add(p1, v_scale(d1, s))
    point_2 = v_add(p2, v_scale(d2, t))
    return s, t, v_distance(point_1, point_2)


def capsule_gap(capsule_a, capsule_b):
    """Surface gap between two tapered capsules (name, cap0, cap1, r0, r1);
    negative = intersection. Radius at the closest axis point is lerped."""
    _, a0, a1, ar0, ar1 = capsule_a
    _, b0, b1, br0, br1 = capsule_b
    s, t, distance = segment_closest_parameters(a0, a1, b0, b1)
    return distance - (ar0 + (ar1 - ar0) * s) - (br0 + (br1 - br0) * t)


# ------------------------------------------------------------------ anatomy

# Leg profile: 6 segments. Pitch relative to horizontal (positive = up);
# lengths are pivot-to-pivot; radii[k] / radii[k+1] are segment k's
# bottom / top cap radius (continuous taper along the leg).
LEG_PITCH_DEG   = [30.0, 8.0, -20.0, -45.0, -65.0, -80.0]
LEG_LENGTHS     = [0.34, 0.30, 0.28, 0.26, 0.24, 0.22]
LEG_RADII       = [0.075, 0.060, 0.048, 0.038, 0.030, 0.024, 0.018]
LEG_AZIMUTH_DEG = [42.0, 16.0, -12.0, -38.0]  # right side; left mirrors X
HIP_RADIAL      = 0.32   # hip pivot distance out from the cephalothorax axis
HIP_LIFT        = 0.06
CLEARANCE       = 0.025  # surface gap between connected parts at each joint

BODY_MASS  = 4.0                               # per body capsule
LEG_MASSES = [0.9, 0.7, 0.5, 0.4, 0.3, 0.2]    # per segment, hip -> toe

# Muscle motors, graduated hip -> toe: (stiffness Nm/rad, damping, max_force).
# Sized from the static hold torque of each joint (~39 N of body weight per
# foot times the horizontal lever from that joint to the foot: 46 / 34 / 23 /
# 12.5 / 5.5 / 1.6 Nm) for ~0.02 rad of sag, with max_force ~5x the hold
# torque so a real shove makes the motors yield before anything explodes.
LEG_MOTORS = [
    (2400.0, 120.0, 240.0),
    (1800.0,  90.0, 180.0),
    (1200.0,  60.0, 120.0),
    ( 650.0,  32.0,  65.0),
    ( 300.0,  15.0,  30.0),
    ( 100.0,   5.0,  12.0),
]
LEG_MOTOR_RANGE  = 0.45
BODY_MOTOR       = (2000.0, 100.0, 120.0)  # cephalothorax<->abdomen waist
BODY_MOTOR_RANGE = 0.20


def stand_height():
    """Body center height that puts the foot tips on the floor: the legs
    drop sum(L*sin(pitch)) below the hip, the hip sits HIP_LIFT above the
    body axis, and the toe cap has radius LEG_RADII[-1]."""
    drop = -sum(l * math.sin(math.radians(p)) for l, p in zip(LEG_LENGTHS, LEG_PITCH_DEG))
    return drop - HIP_LIFT + LEG_RADII[-1] + 0.004


class Spider:
    """Builds the 50 capsule parts (motion_mode="none", posed), collecting
    body jobs and joint jobs to rig after settle()."""

    def __init__(self, c, materials, center):
        self.c         = c
        self.m         = materials
        self.center    = center
        self.root      = c.group("Spider", [center[0], 0.0, center[2]])
        self.capsules  = []   # (name, cap0, cap1, r0, r1) for the clearance check
        self.body_jobs = []   # (node_id, mass)
        self.joint_jobs = []  # (name, part_a_id, part_b_id, pivot, settings)
        self.parts     = {}   # name -> node_id

    def local(self, offset):
        return v_add(self.center, offset)

    def capsule_part(self, name, p0, p1, r0, r1, mass, material,
                     joint_at_p0=True, joint_at_p1=True):
        """One capsule between pivots p0/p1; at a jointed end the cap sphere
        is inset by cap radius + half clearance so parts meeting at a pivot
        are separated by the full clearance."""
        direction = v_norm(v_sub(p1, p0))
        span      = v_distance(p0, p1)
        inset0    = (0.5 * CLEARANCE + r0) if joint_at_p0 else 0.0
        inset1    = (0.5 * CLEARANCE + r1) if joint_at_p1 else 0.0
        length    = span - inset0 - inset1
        if length <= abs(r0 - r1):
            raise RuntimeError(f"{name}: section {length:.3f} too short for taper")
        cap0   = v_add(p0, v_scale(direction, inset0))
        center = v_add(cap0, v_scale(direction, 0.5 * length))
        result = self.c.shape("capsule", name, center, motion_mode="none",
                              length=length, bottom_radius=r0, top_radius=r1,
                              slice_count=16, stack_count=4,
                              material_name=material, parent_node_id=self.root)
        node_id = result.get("node_id") if isinstance(result, dict) else None
        if node_id is None:
            raise RuntimeError(f"create_shape '{name}' returned no node_id")
        rotation = align_y_quaternion(direction)
        if rotation is not None:
            self.c.move_node_id(node_id, rotation_xyzw=rotation)
        self.parts[name] = node_id
        self.body_jobs.append((node_id, mass))
        self.capsules.append((name, cap0, v_add(cap0, v_scale(direction, length)), r0, r1))
        return node_id

    def build_body(self):
        pivot = self.local([0.0, 0.0, 0.05])
        front = self.capsule_part("Spider Body Front", self.local([0.0, 0.0, -0.40]),
                                  pivot, 0.22, 0.30, BODY_MASS, self.m["bronze"],
                                  joint_at_p0=False)
        rear = self.capsule_part("Spider Abdomen", pivot, self.local([0.0, 0.0, 0.60]),
                                 0.34, 0.20, BODY_MASS, self.m["shell"],
                                 joint_at_p1=False)
        self.joint_jobs.append(("Spider Waist", front, rear, pivot, "spider_body_motor"))
        return front

    def build_leg(self, body_id, side, leg_index):
        """side: +1 = right (+X), -1 = left (-X)."""
        azimuth = math.radians(LEG_AZIMUTH_DEG[leg_index])
        out = [side * math.cos(azimuth), 0.0, -math.sin(azimuth)]
        front_center = self.local([0.0, 0.0, -0.175])
        hip = v_add(v_add(front_center, v_scale(out, HIP_RADIAL)),
                    [0.0, HIP_LIFT, 0.0])
        tag = f"Spider Leg {'R' if side > 0 else 'L'}{leg_index + 1}"
        previous_id    = body_id
        previous_pivot = hip
        for k in range(6):
            pitch = math.radians(LEG_PITCH_DEG[k])
            direction = v_norm([out[0] * math.cos(pitch), math.sin(pitch),
                                out[2] * math.cos(pitch)])
            next_pivot = v_add(previous_pivot, v_scale(direction, LEG_LENGTHS[k]))
            part_id = self.capsule_part(
                f"{tag} Seg {k + 1}", previous_pivot, next_pivot,
                LEG_RADII[k], LEG_RADII[k + 1], LEG_MASSES[k],
                self.m["bronze" if k % 2 else "steel"],
                joint_at_p1=(k < 5))  # the toe tip is a free end
            joint = "Hip" if k == 0 else f"Knee {k}"
            self.joint_jobs.append((f"{tag} {joint}", previous_id, part_id,
                                    previous_pivot, f"spider_leg_motor_{k}"))
            previous_id    = part_id
            previous_pivot = next_pivot

    def build_face(self):
        """Glow eyes + fangs riding the front body node as pure visuals.
        The front cap sphere (radius 0.22) is centered at local z -0.40, so
        the face sits ON that sphere's forward surface, not inside it."""
        front_id = self.parts["Spider Body Front"]
        for side in (-1.0, 1.0):
            for i, (dx, dy, dz, r) in enumerate([(0.075, 0.10, -0.585, 0.040),
                                                 (0.150, 0.05, -0.545, 0.025)]):
                self.c.shape("uv_sphere", f"Spider Eye {side:+.0f}.{i}",
                             self.local([side * dx, dy, dz]), radius=r,
                             slice_count=10, stack_count=8,
                             material_name=self.m["glow"],
                             parent_node_id=front_id, motion_mode="none")
            result = self.c.shape("cone", f"Spider Fang {side:+.0f}",
                                  self.local([side * 0.085, -0.115, -0.545]),
                                  height=0.17, bottom_radius=0.038, top_radius=0.006,
                                  slice_count=8, material_name=self.m["steel"],
                                  parent_node_id=front_id, motion_mode="none")
            node_id = result.get("node_id") if isinstance(result, dict) else None
            rotation = align_y_quaternion([0.0, -0.75, -0.66])
            if node_id is not None and rotation is not None:
                self.c.move_node_id(node_id, rotation_xyzw=rotation)

    def worst_clearance(self):
        worst_gap, worst_pair = math.inf, ""
        for i in range(len(self.capsules)):
            for j in range(i + 1, len(self.capsules)):
                gap = capsule_gap(self.capsules[i], self.capsules[j])
                if gap < worst_gap:
                    worst_gap  = gap
                    worst_pair = f"{self.capsules[i][0]} vs {self.capsules[j][0]}"
        return worst_gap, worst_pair


def make_motor_settings(c):
    """Six-dof settings: linear locked, angular limited, position-target-0
    drives on all three angular axes (the rest pose is captured at joint
    creation = the authored standing pose)."""
    def motor(name, angular_range, stiffness, damping, max_force):
        c.joint_settings(
            name,
            limits=[
                {"linear_axes": [True, True, True], "angular_axes": [False, False, False], "min": 0.0, "max": 0.0},
                {"linear_axes": [False, False, False], "angular_axes": [True, True, True], "min": -angular_range, "max": angular_range},
            ],
            drives=[
                {"type": "angular", "axis": axis, "stiffness": stiffness,
                 "damping": damping, "max_force": max_force, "position_target": 0.0}
                for axis in (0, 1, 2)
            ],
        )
    motor("spider_body_motor", BODY_MOTOR_RANGE, *BODY_MOTOR)
    for k, (stiffness, damping, max_force) in enumerate(LEG_MOTORS):
        motor(f"spider_leg_motor_{k}", LEG_MOTOR_RANGE, stiffness, damping, max_force)


def rig_spider(c, spider):
    """Attach bodies (explicit masses) and motor joints. Run AFTER settle()
    with the simulation disabled so the drives capture the standing pose."""
    for node_id, mass in spider.body_jobs:
        c.body(node_id, shape="auto", motion_mode="dynamic", mass=mass,
               angular_damping=0.3, linear_damping=0.1)
    for name, part_a, part_b, pivot, settings in spider.joint_jobs:
        anchor_a = c.anchor(f"{name} A", part_a, pivot)
        anchor_b = c.anchor(f"{name} B", part_b, pivot)
        c.joint(anchor_b, connected_node_id=anchor_a, settings_name=settings)
    print(f"rigged {len(spider.body_jobs)} bodies, {len(spider.joint_jobs)} motor joints")


# --------------------------------------------------------------------- main

def main():
    args = standard_args("Spider Sentinel")
    c = Creation("Spider Sentinel", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    # Rig with the simulation frozen so the motors capture the standing pose.
    c.set_physics(False)

    c.ambience(ambient=[0.10, 0.10, 0.13],
               clear_color=[0.30, 0.34, 0.45, 1.0], grid=False,
               sky={"_version": 3, "enabled": True, "mode": 1})

    def mat(**edits):
        return c.make_material(clear_textures=True, **edits)

    m = {
        "bronze": mat(base_color=[0.62, 0.40, 0.18], roughness=0.38, metallic=1.0),
        "steel":  mat(base_color=[0.16, 0.16, 0.19], roughness=0.45, metallic=0.9),
        "shell":  mat(base_color=[0.45, 0.26, 0.12], roughness=0.55, metallic=0.8),
        "glow":   mat(base_color=[0.9, 0.25, 0.1], roughness=0.4, metallic=0.0,
                      emissive=[4.5, 0.6, 0.15]),
        "floor":  mat(base_color=[0.32, 0.30, 0.27], roughness=0.95, metallic=0.0),
        "rock":   mat(base_color=[0.40, 0.40, 0.42], roughness=0.9, metallic=0.0),
    }

    # Lights first, so a windowed viewing is lit from the first shape onward.
    c.light("directional", "Dusk Sun", [0.0, 10.0, 0.0], [1.0, 0.82, 0.60], 2.4)
    c.settle()  # light nodes insert on the next frame; lookup needs it live
    sun = c.node_by_name("Dusk Sun")
    if sun is not None:
        pitch, yaw = math.radians(-150.0), math.radians(40.0)
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
    c.light("point", "Cool Fill", [-4.5, 3.0, 3.5], [0.4, 0.5, 0.85], 130.0,
            range=16.0, cast_shadow=False)
    c.light("point", "Warm Rim", [3.0, 2.2, -4.5], [1.0, 0.6, 0.3], 110.0,
            range=14.0, cast_shadow=False)
    c.shadow_range(40.0)

    c.shape("box", "Ground", [0.0, -0.25, 0.0], size=[60.0, 0.5, 60.0],
            material_name=m["floor"])
    for i, (x, z, r) in enumerate([(-4.8, -3.6, 0.55), (6.5, 5.0, 0.5),
                                   (-5.5, 6.5, 0.45)]):
        result = c.shape("uv_sphere", f"Boulder {i}", [x, r * 0.35, z], radius=r,
                         slice_count=14, stack_count=10, material_name=m["rock"])
        node_id = result.get("node_id") if isinstance(result, dict) else None
        if node_id is not None:
            c.move_node_id(node_id, scale=[1.15, 0.55, 1.0])

    # ------------------------------------------------------------- spider
    center = [0.0, stand_height(), 0.0]
    print(f"stand height: {center[1]:.3f}")
    spider = Spider(c, m, center)
    print("Building body (2 parts) ...")
    body_front = spider.build_body()
    for side, side_tag in ((1, "right"), (-1, "left")):
        for leg_index in range(4):
            print(f"Building {side_tag} leg {leg_index + 1} (6 parts) ...")
            spider.build_leg(body_front, side, leg_index)
    spider.build_face()

    worst_gap, worst_pair = spider.worst_clearance()
    print(f"part count {len(spider.body_jobs)}, joint count {len(spider.joint_jobs)}, "
          f"smallest surface gap {worst_gap * 1000.0:.1f} mm ({worst_pair})")
    if worst_gap <= 0.0:
        print("FAIL: parts intersect as placed")

    c.settle()
    make_motor_settings(c)
    rig_spider(c, spider)
    c.settle()
    hierarchy_report(c)

    # ------------------------------------------------- stand, shove, recover
    rest_elev = body_axis_elevation(rest_rotation(c, "Spider Body Front"))
    c.set_physics(True)
    c.wake_physics()
    print("Physics enabled - the motors hold the stance...")
    heights, drifts, _ = probe_pose(c, "Spider Body Front", rest_elev, "stand", seconds=6.0)
    stand_ok = min(heights) > 0.7 * center[1] and drifts[-1] < 10.0
    print(f"{'PASS' if stand_ok else 'FAIL'}: standing "
          f"(min height {min(heights):.3f} vs rest {center[1]:.3f}, "
          f"final lean {drifts[-1]:.1f} deg)")

    # Front three-quarter view: the spider faces -Z (eyes + fangs).
    c.place_camera([2.9, 1.5, -3.3], [0.0, 0.5, 0.3])
    c.screenshot("logs/creations/spider_sentinel_standing.png")

    print("Shoving the spider...")
    c.mutate("apply_physics_force", {
        "scene_name": c.scene, "node_id": body_front,
        "impulse": [110.0, 25.0, 70.0],
        "point": v_add(center, [0.0, 0.25, -0.175]),
    })
    heights, drifts, position = probe_pose(c, "Spider Body Front", rest_elev, "recover",
                                           seconds=8.0, interval=0.25)
    recover_ok = heights[-1] > 0.7 * center[1] and drifts[-1] < 12.0
    print(f"{'PASS' if recover_ok else 'FAIL'}: recovered "
          f"(final height {heights[-1]:.3f} vs rest {center[1]:.3f}, "
          f"final lean {drifts[-1]:.1f} deg)")
    # The stagger slides the spider a meter or two; re-frame on where it
    # actually ended up.
    c.place_camera([position[0] + 2.9, 1.5, position[2] - 3.3],
                   [position[0], 0.5, position[2] + 0.3])
    c.screenshot("logs/creations/spider_sentinel_recovered.png")

    if not args.no_save:
        c.save("res/editor/scenes/creations/spider_sentinel.glb")
    print("Spider Sentinel complete.")


if __name__ == "__main__":
    main()
