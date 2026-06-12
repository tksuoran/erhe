#!/usr/bin/env python3
"""Build a single ragdoll spider leg over MCP: 6 tapered capsule segments
connected by ball joints, using the same Spider_builder construction as
mcp_ragdoll_spider.py (clearance insets, paired anchor nodes, six-dof joints
with linear axes locked).

This is a reduced repro model for isolating physics joint chain issues. By
default the hip end is anchored to the world (a joint with no connected node
pins it to its initial world frame), so the leg dangles and swings from a
fixed pivot; pass --free to skip the hip joint and drop the chain instead
(6 parts, 5 joints).

With --simulate the script reports joint integrity diagnostics after
settling:
  - per-joint pivot separation: each joint's two anchor nodes (children of
    the two connected segments) start coincident at the pivot, so their
    world distance is the constraint's linear violation, measured directly;
  - per-link center distances (note: these shrink when a joint bends, so
    only growth beyond the built value indicates stretch).

Usage:
  py -3 scripts/mcp_ragdoll_leg.py [--port 8080] [--scene NAME] [--wait 30]
      [--name leg] [--position X Y Z] [--height 1.2] [--scale 1.0]
      [--clearance 0.025] [--angular-limit 0.7] [--free] [--material NAME]
      [--simulate] [--settle 3.0]

The editor must already be running. Exit code 0 = built and verified.
"""

import argparse
import math
import sys
import time

from erhe_mcp import McpClient, check_true, pick_scene, report, wait_for_server
from mcp_ragdoll_spider import Spider_builder, set_dynamic_physics, v_add, v_distance, v_normalize, v_scale


class Leg_builder(Spider_builder):
    def create_world_joint(self, name, part, pivot, settings_name):
        """Joint pinning a part to the world: one anchor node at the pivot,
        no connected node - the constraint anchors to the world at the
        anchor's initial world frame."""
        anchor = self.call("create_node", {"name": f"{name}_b", "parent_node_id": part["node_id"], "position": pivot})
        self.call("create_physics_joint", {
            "node_id":          anchor["node_id"],
            "settings_name":    settings_name,
            "enable_collision": False,
        })
        self.joint_count += 1
        return anchor

    def build_single_leg(self, anchored):
        """One leg extending along +X from the hip at the spider center.
        Returns the list of created segment nodes, hip to foot."""
        out      = [1.0, 0.0, 0.0]
        hip      = self.center
        settings = f"{self.prefix}_leg_joint"

        parts          = []
        previous_part  = None
        previous_pivot = hip
        for k in range(6):
            pitch = math.radians(self.LEG_PITCH_DEG[k])
            direction = v_normalize([
                out[0] * math.cos(pitch),
                math.sin(pitch),
                out[2] * math.cos(pitch),
            ])
            next_pivot = v_add(previous_pivot, v_scale(direction, self.LEG_LENGTHS[k] * self.scale))
            part = self.create_capsule_part(
                f"{self.prefix}_seg{k + 1}", previous_pivot, next_pivot,
                self.LEG_RADII[k], self.LEG_RADII[k + 1],
                joint_at_p0=(k > 0) or anchored,  # free hip end gets no inset
                joint_at_p1=(k < 5)               # the foot tip is a free end
            )
            if k == 0:
                if anchored:
                    self.create_world_joint(f"{self.prefix}_hip", part, hip, settings)
            else:
                self.create_ball_joint(f"{self.prefix}_knee{k}", previous_part, part, previous_pivot, settings)
            parts.append(part)
            previous_part  = part
            previous_pivot = next_pivot
        return parts


def node_world_position(client, scene_name, name):
    details = client.call("get_node_details", {"scene_name": scene_name, "node_name": name})
    return details["world_transform"]["translation"]


def segment_centers(client, scene_name, prefix):
    return [node_world_position(client, scene_name, f"{prefix}_seg{k + 1}") for k in range(6)]


def joint_separations(client, scene_name, prefix, anchored, hip_pivot):
    """World distance between each joint's paired anchor nodes (0 at build
    time) - the constraint's linear violation. The world-anchored hip is
    compared against the fixed hip pivot instead."""
    separations = []
    if anchored:
        anchor = node_world_position(client, scene_name, f"{prefix}_hip_b")
        separations.append(("hip", v_distance(anchor, hip_pivot)))
    for k in range(1, 6):
        a = node_world_position(client, scene_name, f"{prefix}_knee{k}_a")
        b = node_world_position(client, scene_name, f"{prefix}_knee{k}_b")
        separations.append((f"knee{k}", v_distance(a, b)))
    return separations


def link_distances(centers):
    return [v_distance(centers[k], centers[k + 1]) for k in range(len(centers) - 1)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port",          type=int,   default=8080)
    parser.add_argument("--scene",         type=str,   default="")
    parser.add_argument("--wait",          type=float, default=30.0)
    parser.add_argument("--name",          type=str,   default="leg", help="Node name prefix")
    parser.add_argument("--position",      type=float, nargs=3, default=[0.0, 0.0, 0.0], metavar=("X", "Y", "Z"), help="Base position; the hip sits --height above it")
    parser.add_argument("--height",        type=float, default=1.2,   help="Hip height above the base position")
    parser.add_argument("--scale",         type=float, default=1.0,   help="Uniform size multiplier")
    parser.add_argument("--clearance",     type=float, default=0.025, help="Surface gap between connected parts at each joint")
    parser.add_argument("--angular-limit", type=float, default=0.7,   help="Joint angular limit in radians (all axes)")
    parser.add_argument("--free",          action="store_true",       help="No hip joint: drop the chain instead of dangling it")
    parser.add_argument("--material",      type=str,   default="",    help="Material name (default: first available)")
    parser.add_argument("--simulate",      action="store_true",       help="Enable dynamic physics when done")
    parser.add_argument("--settle",        type=float, default=3.0,   help="Seconds to wait after enabling physics before sampling")
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
    while f"{prefix}_seg1" in existing:
        prefix = f"{args.name}{suffix}"
        suffix += 1
    print(f"Node prefix: {prefix}")

    original_physics = set_dynamic_physics(client, False)
    print(f"Dynamic physics disabled for construction (was {'on' if original_physics else 'off'})")

    anchored = not args.free
    builder = Leg_builder(client, scene_name, prefix, args.material, args.position, args.height, args.scale, args.clearance)
    builder.call("create_physics_joint_settings", {
        "name": f"{prefix}_leg_joint",
        "limits": [
            {"linear_axes":  [True, True, True], "min": 0.0, "max": 0.0},
            {"angular_axes": [True, True, True], "min": -args.angular_limit, "max": args.angular_limit},
        ],
    })

    print(f"Building leg (6 parts, hip {'anchored to world' if anchored else 'free'}) ...")
    builder.build_single_leg(anchored)
    client.call("select_items", {"scene_name": scene_name, "ids": []})  # release selection (kinematic hold)

    expected_joints = 6 if anchored else 5
    print(f"Created {builder.part_count} capsule parts and {builder.joint_count} physics joint(s).")
    check_true("part count",  builder.part_count == 6, f"{builder.part_count}")
    check_true("joint count", builder.joint_count == expected_joints, f"{builder.joint_count}")

    worst_gap, worst_pair = builder.check_clearances()
    check_true("no part intersections", worst_gap > 0.0, f"smallest surface gap {worst_gap * 1000.0:.1f} mm ({worst_pair})")

    samples = [f"{prefix}_knee1_b", f"{prefix}_knee5_b"] + ([f"{prefix}_hip_b"] if anchored else [])
    for sample in samples:
        details = client.call("get_node_details", {"scene_name": scene_name, "node_name": sample})
        joints = [a for a in details.get("attachments", []) if a.get("type") == "Node_joint"]
        check_true(f"constraint created on {sample}", bool(joints) and joints[0].get("constraint") == "created",
                   str(joints[0].get("constraint")) if joints else "no joint attachment")

    built_centers = segment_centers(client, scene_name, prefix)
    built_links   = link_distances(built_centers)
    print("Link distances at build time: " + ", ".join(f"{d:.3f}" for d in built_links))

    if args.simulate:
        set_dynamic_physics(client, True)
        print(f"Dynamic physics enabled; settling {args.settle} s ...")
        time.sleep(args.settle)
        settled_centers = segment_centers(client, scene_name, prefix)
        for k, t in enumerate(settled_centers):
            print(f"  {prefix}_seg{k + 1}: world = [{t[0]:.3f}, {t[1]:.3f}, {t[2]:.3f}] (moved {v_distance(built_centers[k], settled_centers[k]):.3f})")
        settled_links = link_distances(settled_centers)
        deltas = [(s - b) * 1000.0 for b, s in zip(built_links, settled_links)]
        print("Link distances after settling:  " + ", ".join(f"{d:.3f}" for d in settled_links))
        print("Link delta (mm, negative = joint bend, positive = stretch): " + ", ".join(f"{d:+.1f}" for d in deltas))
        separations = joint_separations(client, scene_name, prefix, anchored, builder.center)
        print("Joint pivot separation (mm):    " + ", ".join(f"{label} {distance * 1000.0:.1f}" for label, distance in separations))
        worst_label, worst_separation = max(separations, key=lambda entry: entry[1])
        print(f"Worst joint violation: {worst_label} at {worst_separation * 1000.0:.1f} mm")
        check_true("chain moved under physics", v_distance(built_centers[5], settled_centers[5]) > 0.01)
    elif original_physics:
        set_dynamic_physics(client, True)  # restore the state we found

    return report()


if __name__ == "__main__":
    sys.exit(main())
