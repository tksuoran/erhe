#!/usr/bin/env python3
"""Procedurally build a ragdoll spider in the erhe editor over MCP.

Anatomy (all from tapered capsules via the create_shape tool):
  - 2 body parts: cephalothorax (front) + abdomen (rear), lying along Z.
  - 8 legs (4 per side), each made of 6 capsule segments with tapering radii.

Articulation: every part pair is connected with a physics joint
(create_physics_joint). Joint frames are captured from node world
transforms, so each joint gets two coincident empty anchor nodes
(create_node) at the anatomical pivot - one parented to each connected
segment - and the joint is created anchor-to-anchor. Joint settings lock the
linear axes (ball joint) and limit the angular axes.

Dynamic physics is toggled off while building so parts stay posed; pass
--simulate to enable it at the end and watch the spider collapse.

Usage:
  py -3 scripts/mcp_ragdoll_spider.py [--port 8080] [--scene NAME] [--wait 30]
      [--name spider] [--position X Y Z] [--height 0.85] [--scale 1.0]
      [--material NAME] [--simulate] [--settle 3.0]

The editor must already be running. Exit code 0 = spider built (and all
sampled joints report a created constraint).
"""

import argparse
import math
import sys
import time

from erhe_mcp import McpClient, check_close, check_true, pick_scene, report, wait_for_server


def v_add(a, b):       return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
def v_scale(a, s):     return [a[0] * s, a[1] * s, a[2] * s]
def v_length(a):       return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
def v_normalize(a):    return v_scale(a, 1.0 / v_length(a))
def v_distance(a, b):  return v_length([b[0] - a[0], b[1] - a[1], b[2] - a[2]])


def quat_align_y(direction):
    """Quaternion [x, y, z, w] rotating the +Y axis onto the given unit direction."""
    c = direction[1]  # dot((0,1,0), direction)
    if c > 1.0 - 1.0e-6:
        return [0.0, 0.0, 0.0, 1.0]
    if c < -1.0 + 1.0e-6:
        return [1.0, 0.0, 0.0, 0.0]  # 180 degrees about X
    axis = v_normalize([direction[2], 0.0, -direction[0]])  # cross((0,1,0), direction)
    angle = math.acos(c)
    s = math.sin(0.5 * angle)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(0.5 * angle)]


class Spider_builder:
    # Leg profile: 6 segments. Pitch is the angle of each segment relative to
    # the horizontal (positive = up); lengths are cylinder section lengths
    # (= distance between consecutive joint pivots, the capsule cap sphere
    # centers); radii[k] / radii[k + 1] are segment k's bottom / top radius,
    # so the taper is continuous along the leg.
    LEG_PITCH_DEG = [30.0, 8.0, -20.0, -45.0, -65.0, -80.0]
    LEG_LENGTHS   = [0.34, 0.30, 0.28, 0.26, 0.24, 0.22]
    LEG_RADII     = [0.075, 0.060, 0.048, 0.038, 0.030, 0.024, 0.018]
    # Azimuth of each right-side leg in the horizontal plane: 0 = straight out
    # (+X), positive = toward the front (-Z). Left side mirrors X.
    LEG_AZIMUTH_DEG = [42.0, 16.0, -12.0, -38.0]

    def __init__(self, client, scene_name, prefix, material, base, height, scale):
        self.client     = client
        self.scene_name = scene_name
        self.prefix     = prefix
        self.material   = material
        self.scale      = scale
        self.center     = [base[0], base[1] + height * scale, base[2]]
        self.part_count  = 0
        self.joint_count = 0

    def call(self, tool, arguments):
        arguments = dict(arguments)
        arguments["scene_name"] = self.scene_name
        return self.client.call(tool, arguments)

    def local(self, offset):
        """Spider-local offset (already scaled) to world position."""
        return v_add(self.center, v_scale(offset, self.scale))

    def create_capsule_part(self, name, p0, p1, r0, r1):
        """Create one capsule segment whose cap sphere centers sit at world
        points p0 / p1 with radii r0 / r1, rotated from +Y onto p0 -> p1."""
        direction = v_normalize([p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]])
        length    = v_distance(p0, p1)
        node = self.call("create_shape", {
            "shape":         "capsule",
            "name":          name,
            "position":      v_add(p0, v_scale(direction, 0.5 * length)),
            "motion_mode":   "dynamic",
            "length":        length,
            "bottom_radius": r0 * self.scale,
            "top_radius":    r1 * self.scale,
            "slice_count":   16,
            "stack_count":   4,
            **({"material_name": self.material} if self.material else {}),
        })
        self.call("select_items", {"ids": [node["node_id"]]})
        self.call("transform_selection", {"space": "global", "rotation_xyzw": quat_align_y(direction)})
        self.part_count += 1
        return node

    def create_ball_joint(self, name, part_a, part_b, pivot, settings_name):
        """Joint between two parts: two coincident anchor nodes at the world
        pivot (one child per part), joined anchor-to-anchor."""
        anchor_a = self.call("create_node", {"name": f"{name}_a", "parent_node_id": part_a["node_id"], "position": pivot})
        anchor_b = self.call("create_node", {"name": f"{name}_b", "parent_node_id": part_b["node_id"], "position": pivot})
        self.call("create_physics_joint", {
            "node_id":           anchor_b["node_id"],
            "connected_node_id": anchor_a["node_id"],
            "settings_name":     settings_name,
            "enable_collision":  False,
        })
        self.joint_count += 1
        return anchor_b

    def create_joint_settings(self):
        for name, angular_limit in ((f"{self.prefix}_leg_joint", 0.7), (f"{self.prefix}_body_joint", 0.35)):
            self.call("create_physics_joint_settings", {
                "name": name,
                "limits": [
                    {"linear_axes":  [True, True, True], "min": 0.0, "max": 0.0},
                    {"angular_axes": [True, True, True], "min": -angular_limit, "max": angular_limit},
                ],
            })

    def build_body(self):
        body_pivot = self.local([0.0, 0.0, 0.05])
        front = self.create_capsule_part(f"{self.prefix}_body_front", self.local([0.0, 0.0, -0.40]), body_pivot, 0.22, 0.30)
        rear  = self.create_capsule_part(f"{self.prefix}_body_rear",  body_pivot, self.local([0.0, 0.0, 0.60]),  0.34, 0.20)
        self.create_ball_joint(f"{self.prefix}_joint_body", front, rear, body_pivot, f"{self.prefix}_body_joint")
        return front

    def build_leg(self, body_part, side, leg_index):
        """side: +1 = right (+X), -1 = left (-X)."""
        azimuth = math.radians(self.LEG_AZIMUTH_DEG[leg_index])
        out     = [side * math.cos(azimuth), 0.0, -math.sin(azimuth)]  # horizontal leg direction

        front_center = self.local([0.0, 0.0, -0.175])  # cephalothorax center
        hip = v_add(v_add(front_center, v_scale(out, 0.24 * self.scale)), [0.0, 0.06 * self.scale, 0.0])

        side_tag = "r" if side > 0 else "l"
        leg_name = f"{self.prefix}_leg_{side_tag}{leg_index + 1}"

        previous_part  = body_part
        previous_pivot = hip
        settings       = f"{self.prefix}_leg_joint"
        for k in range(6):
            pitch = math.radians(self.LEG_PITCH_DEG[k])
            direction = v_normalize([
                out[0] * math.cos(pitch),
                math.sin(pitch),
                out[2] * math.cos(pitch),
            ])
            next_pivot = v_add(previous_pivot, v_scale(direction, self.LEG_LENGTHS[k] * self.scale))
            part = self.create_capsule_part(
                f"{leg_name}_seg{k + 1}", previous_pivot, next_pivot, self.LEG_RADII[k], self.LEG_RADII[k + 1]
            )
            joint_kind = "hip" if k == 0 else f"knee{k}"
            self.create_ball_joint(f"{leg_name}_{joint_kind}", previous_part, part, previous_pivot, settings)
            previous_part  = part
            previous_pivot = next_pivot
        return previous_part  # foot segment


def set_dynamic_physics(client, enabled):
    """toggle_physics is toggle-only; flip until the reported state matches.
    Returns the state before the first toggle."""
    state = client.call("toggle_physics")["dynamic_physics_enabled"]
    original = not state
    if state != enabled:
        client.call("toggle_physics")
    return original


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port",     type=int,   default=8080)
    parser.add_argument("--scene",    type=str,   default="")
    parser.add_argument("--wait",     type=float, default=30.0)
    parser.add_argument("--name",     type=str,   default="spider", help="Node name prefix")
    parser.add_argument("--position", type=float, nargs=3, default=[0.0, 0.0, 0.0], metavar=("X", "Y", "Z"), help="Base position; the body floats --height above it")
    parser.add_argument("--height",   type=float, default=0.85, help="Body height above the base position")
    parser.add_argument("--scale",    type=float, default=1.0,  help="Uniform size multiplier")
    parser.add_argument("--material", type=str,   default="",   help="Material name (default: first available)")
    parser.add_argument("--simulate", action="store_true",      help="Enable dynamic physics when done (ragdoll drop)")
    parser.add_argument("--settle",   type=float, default=3.0,  help="Seconds to wait after enabling physics before sampling")
    args = parser.parse_args()

    client = McpClient(args.port)
    print(f"Waiting for MCP server on port {args.port} ...")
    wait_for_server(client, args.wait)

    required_tools = {"create_shape", "create_node", "create_physics_joint", "create_physics_joint_settings", "transform_selection"}
    missing = required_tools - client.tool_names()
    if missing:
        print(f"FAIL: missing MCP tools: {sorted(missing)}")
        return 1

    scene_name = pick_scene(client, args.scene)
    print(f"Using scene: {scene_name}")

    # Unique prefix so repeated runs do not collide on names.
    existing = {n["name"] for n in client.call("get_scene_nodes", {"scene_name": scene_name}).get("nodes", [])}
    prefix = args.name
    suffix = 2
    while f"{prefix}_body_front" in existing:
        prefix = f"{args.name}{suffix}"
        suffix += 1
    print(f"Node prefix: {prefix}")

    original_physics = set_dynamic_physics(client, False)
    print(f"Dynamic physics disabled for construction (was {'on' if original_physics else 'off'})")

    builder = Spider_builder(client, scene_name, prefix, args.material, args.position, args.height, args.scale)
    builder.create_joint_settings()

    print("Building body (2 parts) ...")
    body_front = builder.build_body()

    feet = []
    for side, side_tag in ((1, "right"), (-1, "left")):
        for leg_index in range(4):
            print(f"Building {side_tag} leg {leg_index + 1} (6 parts) ...")
            feet.append(builder.build_leg(body_front, side, leg_index))
    client.call("select_items", {"scene_name": scene_name, "ids": []})  # release selection (kinematic hold)

    print(f"Created {builder.part_count} capsule parts and {builder.joint_count} physics joints.")
    check_true("part count",  builder.part_count == 50, f"{builder.part_count}")
    check_true("joint count", builder.joint_count == 49, f"{builder.joint_count}")

    # Sample a few joints: constraint must be created (not pending).
    for sample in (f"{prefix}_joint_body_b", f"{prefix}_leg_r1_hip_b", f"{prefix}_leg_l4_knee5_b"):
        details = client.call("get_node_details", {"scene_name": scene_name, "node_name": sample})
        joints = [a for a in details.get("attachments", []) if a.get("type") == "Node_joint"]
        check_true(f"constraint created on {sample}", bool(joints) and joints[0].get("constraint") == "created",
                   str(joints[0].get("constraint")) if joints else "no joint attachment")

    if args.simulate:
        foot_name = client.call("get_node_details", {"scene_name": scene_name, "node_name": f"{prefix}_leg_r1_seg6"})
        before = foot_name["world_transform"]["translation"]
        set_dynamic_physics(client, True)
        print(f"Dynamic physics enabled; settling {args.settle} s ...")
        time.sleep(args.settle)
        after = client.call("get_node_details", {"scene_name": scene_name, "node_name": f"{prefix}_leg_r1_seg6"})["world_transform"]["translation"]
        moved = v_distance(before, after)
        print(f"  foot {prefix}_leg_r1_seg6 moved {moved:.3f} m (ragdoll active)")
        check_true("ragdoll moved under physics", moved > 0.01, f"moved={moved:.4f}")
    elif original_physics:
        set_dynamic_physics(client, True)  # restore the state we found

    return report()


if __name__ == "__main__":
    sys.exit(main())
