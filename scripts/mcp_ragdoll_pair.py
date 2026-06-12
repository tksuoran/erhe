#!/usr/bin/env python3
"""Build a minimal two-part ragdoll over MCP: just the spider's two body
capsules (cephalothorax + abdomen) connected by one ball joint.

This is a reduced repro model for isolating physics joint issues without the
spider's 48 leg parts. Construction is identical to mcp_ragdoll_spider.py
(same Spider_builder): tapered capsules with clearance insets at the shared
pivot, two coincident empty anchor nodes, one six-dof joint with linear axes
locked and angular axes limited by --angular-limit.

With --simulate the script also reports a stretch diagnostic: the distance
between the two part centers at build time vs after settling. The joint
locks the pivot, so any growth here is constraint stretch.

Usage:
  py -3 scripts/mcp_ragdoll_pair.py [--port 8080] [--scene NAME] [--wait 30]
      [--name pair] [--position X Y Z] [--height 0.85] [--scale 1.0]
      [--clearance 0.025] [--angular-limit 0.35] [--material NAME]
      [--simulate] [--settle 3.0]

The editor must already be running. Exit code 0 = built and verified.
"""

import argparse
import sys
import time

from erhe_mcp import McpClient, check_true, pick_scene, report, wait_for_server
from mcp_ragdoll_spider import Spider_builder, set_dynamic_physics, v_distance


def part_centers(client, scene_name, names):
    centers = {}
    for name in names:
        details = client.call("get_node_details", {"scene_name": scene_name, "node_name": name})
        centers[name] = details["world_transform"]["translation"]
    return centers


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port",          type=int,   default=8080)
    parser.add_argument("--scene",         type=str,   default="")
    parser.add_argument("--wait",          type=float, default=30.0)
    parser.add_argument("--name",          type=str,   default="pair", help="Node name prefix")
    parser.add_argument("--position",      type=float, nargs=3, default=[0.0, 0.0, 0.0], metavar=("X", "Y", "Z"), help="Base position; the body floats --height above it")
    parser.add_argument("--height",        type=float, default=0.85,  help="Body height above the base position")
    parser.add_argument("--scale",         type=float, default=1.0,   help="Uniform size multiplier")
    parser.add_argument("--clearance",     type=float, default=0.025, help="Surface gap between the parts at the joint")
    parser.add_argument("--angular-limit", type=float, default=0.35,  help="Joint angular limit in radians (all axes)")
    parser.add_argument("--material",      type=str,   default="",    help="Material name (default: first available)")
    parser.add_argument("--simulate",      action="store_true",       help="Enable dynamic physics when done (ragdoll drop)")
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
    while f"{prefix}_body_front" in existing:
        prefix = f"{args.name}{suffix}"
        suffix += 1
    print(f"Node prefix: {prefix}")

    original_physics = set_dynamic_physics(client, False)
    print(f"Dynamic physics disabled for construction (was {'on' if original_physics else 'off'})")

    builder = Spider_builder(client, scene_name, prefix, args.material, args.position, args.height, args.scale, args.clearance)
    builder.call("create_physics_joint_settings", {
        "name": f"{prefix}_body_joint",
        "limits": [
            {"linear_axes":  [True, True, True], "min": 0.0, "max": 0.0},
            {"angular_axes": [True, True, True], "min": -args.angular_limit, "max": args.angular_limit},
        ],
    })

    print("Building body (2 parts, 1 joint) ...")
    builder.build_body()
    client.call("select_items", {"scene_name": scene_name, "ids": []})  # release selection (kinematic hold)

    part_names = [f"{prefix}_body_front", f"{prefix}_body_rear"]
    print(f"Created {builder.part_count} capsule parts and {builder.joint_count} physics joint(s).")
    check_true("part count",  builder.part_count == 2, f"{builder.part_count}")
    check_true("joint count", builder.joint_count == 1, f"{builder.joint_count}")

    worst_gap, worst_pair = builder.check_clearances()
    check_true("no part intersection", worst_gap > 0.0, f"surface gap {worst_gap * 1000.0:.1f} mm ({worst_pair})")

    details = client.call("get_node_details", {"scene_name": scene_name, "node_name": f"{prefix}_joint_body_b"})
    joints = [a for a in details.get("attachments", []) if a.get("type") == "Node_joint"]
    check_true("constraint created", bool(joints) and joints[0].get("constraint") == "created",
               str(joints[0].get("constraint")) if joints else "no joint attachment")

    built = part_centers(client, scene_name, part_names)
    built_distance = v_distance(built[part_names[0]], built[part_names[1]])
    print(f"Part center distance at build time: {built_distance:.3f}")

    if args.simulate:
        set_dynamic_physics(client, True)
        print(f"Dynamic physics enabled; settling {args.settle} s ...")
        time.sleep(args.settle)
        settled = part_centers(client, scene_name, part_names)
        for name in part_names:
            t = settled[name]
            print(f"  {name}: world = [{t[0]:.3f}, {t[1]:.3f}, {t[2]:.3f}] (moved {v_distance(built[name], settled[name]):.3f})")
        settled_distance = v_distance(settled[part_names[0]], settled[part_names[1]])
        stretch = settled_distance - built_distance
        print(f"Part center distance after settling: {settled_distance:.3f} (stretch {stretch * 1000.0:+.1f} mm)")
        check_true("ragdoll moved under physics", v_distance(built[part_names[0]], settled[part_names[0]]) > 0.01)
    elif original_physics:
        set_dynamic_physics(client, True)  # restore the state we found

    return report()


if __name__ == "__main__":
    sys.exit(main())
