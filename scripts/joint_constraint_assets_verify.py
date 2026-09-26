#!/usr/bin/env python3
"""Checks the joint constraint test asset against a running editor, over MCP.

The asset (res/editor/assets/joint_constraints/joint_constraints.glb) is
built by scripts/creations/creation_23_joint_constraint_test.py, whose
STATIONS / IK_CASES tables say what every station is; this script imports
them, so the two cannot drift. It loads the asset with load_scene and runs:

  1. Reload fidelity + static state, simulation paused: every physics joint
     is reported, live except the inactive one (the ERHE_physics_joint
     payload keeps it inactive), with the backend-level exactness and the
     Box3D compatibility expected of it, and every coordinate in range at
     rest at both levels (P11 breaks its contract: both backends hold its
     fixed axis at zero, not at the authored 0.1); every IK bone reports its limits and the
     saved pose is within them except the case posed outside.
  2. The Joint Constraints filter: `all` draws every joint and every
     limited bone; `hovered_mesh` / `hovered_bone` with nothing hovered draw
     nothing.
  3. IK drags: the middle bone's IK Lock makes it the root of a drag of the
     chain's tip node, and dragging the tip in two directions leaves that
     limited bone within its limits (the fully locked one does not
     turn at all); every drag is undone.
  4. Physics stress, simulation running on the manual clock: an --impulse on
     each stress station's box must not push its joint past its limits by
     more than --angular-tolerance / --linear-tolerance at any sampled step
     (50 ms samples over 1.5 s).

The physics backend is detected from the hinge's enforced shape (Jolt twists
about X, Box3D's revolute joint about the hinge axis Z).

    py -3 scripts/joint_constraint_assets_verify.py [--port N]

Launch the editor first (repo root as working directory, ERHE_AI_DRIVER=1),
e.g. build_vs2026_vulkan_headless/bin/Debug/editor.exe --commands
config/editor/commands_empty.json, and wait for "completed frame 12" in
logs/log.txt. Exit code 1 when any check fails.
"""

import argparse
import math
import os
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts", "creations"))

from erhe_mcp import DEFAULT_PORT, McpClient, check_true, report, wait_for_server  # noqa: E402
import creation_23_joint_constraint_test as asset  # noqa: E402

ASSET_PATH = asset.SAVE_PATH


def advance(client, frames=3):
    for _ in range(frames):
        client.call("advance_time", {"seconds": 0.016})


def load_asset(client):
    # Scenes are addressed by name, so an already open copy of the asset is
    # closed first; the fresh one is then the only scene of that name.
    stem = os.path.splitext(os.path.basename(ASSET_PATH))[0]
    for scene in client.call("list_scenes")["scenes"]:
        if scene["name"] == stem:
            client.call("close_scene", {"scene_name": scene["name"]})
    deadline = time.monotonic() + 30.0
    while any(scene["name"] == stem for scene in client.call("list_scenes")["scenes"]) and (time.monotonic() < deadline):
        advance(client, 2)
    # By id: the asset may already be open, and the new copy has its name.
    before = {scene["id"] for scene in client.call("list_scenes")["scenes"]}
    client.call("load_scene", {"path": ASSET_PATH})
    deadline = time.monotonic() + 180.0
    while time.monotonic() < deadline:
        advance(client, 2)
        fresh = [scene["name"] for scene in client.call("list_scenes")["scenes"] if scene["id"] not in before]
        if fresh:
            client.call("set_active_scene", {"scene_name": fresh[0]})
            advance(client, 6)
            return fresh[0]
    raise RuntimeError(f"load_scene produced no scene for {ASSET_PATH}")


def state(client, scene):
    advance(client)
    return client.call("get_joint_constraint_state", {"scene_name": scene})


def by_name(entries):
    return {entry["name"]: entry for entry in entries}


def limit_excess(limit, value):
    if not limit.get("limited", False):
        return 0.0
    return max(0.0, limit["min"] - value, value - limit["max"])


def joint_excess(view):
    """(angular, linear) amounts the coordinates of one level of a joint
    ("contract" or "backend") lie outside its limits, radians and meters."""
    limits      = view["limits"]
    coordinates = view["coordinates"]
    linear = max(limit_excess(limits["translation"][axis], coordinates["translation"][axis]) for axis in range(3))
    angular = limit_excess(limits["twist"], coordinates["twist"])
    if limits["swing_model"] == "pyramid":
        for k in range(2):
            angular = max(angular, limit_excess(limits["swing"][k], coordinates["swing"][k]))
    elif limits["cone"].get("limited", False):
        angular = max(angular, coordinates["cone"] - limits["cone"]["max"])
    return angular, linear


def expected_joints():
    """(joint name, station, is_lower) for every joint the asset has."""
    result = []
    for station in asset.STATIONS:
        result.append((asset.joint_name(station), station, False))
        if "lower_limits" in station:
            result.append((asset.joint_name(station, lower=True), station, True))
    return result


def check_static(client, scene):
    snapshot = state(client, scene)
    joints = by_name(snapshot["physics_joints"])
    hinge = joints.get(asset.joint_name(asset.STATIONS[0]))
    if hinge is None:
        raise RuntimeError("the hinge station is missing - is this the joint constraint test asset?")
    backend = "box3d" if hinge["backend"]["limits"]["twist_axis"] == 2 else "jolt"
    print(f"physics backend (detected): {backend}")

    for name, station, lower in expected_joints():
        joint = joints.get(name)
        check_true(f"1 {name}: reported", joint is not None)
        if joint is None:
            continue
        expect_live = station.get("live", True)
        check_true(f"1 {name}: {'live' if expect_live else 'pending (inactive after reload)'}", joint["live"] == expect_live)
        expect_exact = station[f"exact_{backend}"]
        check_true(f"1 {name}: backend exact == {expect_exact} on {backend}", joint["backend"]["exact"] == expect_exact, f"exact {joint['backend']['exact']}")
        check_true(f"1 {name}: box3d_compatible == {station['exact_box3d']}", joint["box3d_compatible"] == station["exact_box3d"], joint["box3d_note"])
        check_true(f"1 {name}: the contract level is exact", joint["contract"]["exact"] is True)
        if expect_live:
            angular, linear = joint_excess(joint["backend"])
            check_true(f"1 {name}: backend level in range at rest", (angular < 1.0e-3) and (linear < 1.0e-3), f"excess {angular:.4f} rad {linear:.4f} m")
            # P11's Y is authored fixed at 0.1 and both backends hold it at
            # 0, so at rest it breaks its contract by 0.1 m.
            angular, linear = joint_excess(joint["contract"])
            if station["key"] == "P11":
                check_true(f"1 {name}: contract level out of range by 0.1 m at rest", abs(linear - 0.1) < 1.0e-3, f"excess {linear:.4f} m")
            else:
                check_true(f"1 {name}: contract level in range at rest", (angular < 1.0e-3) and (linear < 1.0e-3), f"excess {angular:.4f} rad {linear:.4f} m")

    bones = by_name(snapshot["ik_bones"])
    for case in asset.IK_CASES:
        name = asset.ik_bone_name(case, 1)
        bone = bones.get(name)
        check_true(f"1 {name} ({case['label']}): reported", bone is not None)
        if bone is None:
            continue
        check_true(f"1 {name}: limit flags", bone["limit"] == case["limit"], str(bone["limit"]))
        check_true(f"1 {name}: lock flags", bone["lock"] == case.get("lock", [False, False, False]), str(bone["lock"]))
        check_true(f"1 {name}: within limits == {case['within']}", bone["within_limits"] == case["within"])
    return backend


def check_filter(client, scene):
    client.call("set_joint_constraint_visualization", {"filter": "all"})
    snapshot = state(client, scene)
    viewport = next((v for v in snapshot["viewports"]), None)
    check_true("2 a viewport shows the scene", viewport is not None)
    if viewport is None:
        return
    joint_ids = {joint["id"] for joint in snapshot["physics_joints"]}
    bone_ids  = {bone["id"] for bone in snapshot["ik_bones"]}
    check_true("2 all: every joint drawn", set(viewport["drawn_physics_joints"]) == joint_ids,
               f"{len(viewport['drawn_physics_joints'])} of {len(joint_ids)}")
    check_true("2 all: every limited bone drawn", set(viewport["drawn_ik_bones"]) == bone_ids,
               f"{len(viewport['drawn_ik_bones'])} of {len(bone_ids)}")
    for mode in ("hovered_mesh", "hovered_bone"):
        client.call("set_joint_constraint_visualization", {"filter": mode})
        viewport = state(client, scene)["viewports"][0]
        check_true(f"2 {mode}, nothing hovered: nothing drawn",
                   (viewport["drawn_physics_joints"] == []) and (viewport["drawn_ik_bones"] == []))
    client.call("set_joint_constraint_visualization", {"filter": "all"})


def quat_angle(a, b):
    d = abs(sum(x * y for x, y in zip(a, b)))
    return 2.0 * math.acos(min(1.0, d))


def check_ik_drags(client, scene):
    for index, case in enumerate(asset.IK_CASES):
        if not case["within"]:
            continue  # a drag starting outside the limits may stay outside (no-teleport rule)
        middle = asset.ik_bone_name(case, 1)
        last   = asset.ik_bone_name(case, 2)
        tip    = asset.ik_tip_name(case)
        x      = asset.chain_x(index)
        rest   = client.call("get_node_details", {"scene_name": scene, "node_name": middle})["local_transform"]["rotation_xyzw"]
        worst_turn = 0.0
        # Dragging the tip node: inside the 0.8 m reach from the middle bone,
        # so the solve has to bend the chain instead of laying it straight.
        for target in ([x + 0.35, 0.5, asset.CHAIN_Z + 0.15], [x - 0.25, 0.55, asset.CHAIN_Z - 0.3]):
            result = client.call("ik_drag", {"scene_name": scene, "node_name": tip, "target": target})
            advance(client, 4)
            chain = [joint["name"] for joint in result.get("joints", [])]
            check_true(f"3 {middle}: IK Lock makes it the chain root, the tip node the end", chain == [middle, last, tip], str(chain))
            bone = by_name(state(client, scene)["ik_bones"]).get(middle, {})
            turned = client.call("get_node_details", {"scene_name": scene, "node_name": middle})["local_transform"]["rotation_xyzw"]
            worst_turn = max(worst_turn, quat_angle(rest, turned))
            check_true(f"3 {middle} ({case['label']}): within limits after a drag to {target}",
                       bone.get("within_limits") is True, str(bone.get("swing")))
            if result.get("recorded"):
                client.call("undo")
                advance(client, 4)
        if case.get("lock") == [True, True, True]:
            check_true(f"3 {middle}: fully locked bone does not turn", worst_turn < 1.0e-3, f"{math.degrees(worst_turn):.3f} deg")
        else:
            check_true(f"3 {middle}: the drags turned the bone", worst_turn > 1.0e-2, f"{math.degrees(worst_turn):.2f} deg")


def check_stress(client, scene, impulse, angular_tolerance, linear_tolerance):
    client.call("advance_time", {"mode": "manual"})
    client.call("toggle_physics", {"enabled": True})
    names = [(name, station, lower) for name, station, lower in expected_joints() if station["stress"]]
    for name, station, lower in names:
        bob = asset.bob_name(station, lower)
        client.call("apply_physics_force", {"scene_name": scene, "node_name": bob, "impulse": impulse})
        worst_angular = 0.0
        worst_linear  = 0.0
        for _ in range(30):
            client.call("advance_time", {"seconds": 0.05})
            deadline = time.monotonic() + 10.0
            while (client.call("advance_time", {}).get("pending_seconds", 0.0) > 0.0) and (time.monotonic() < deadline):
                time.sleep(0.02)
            joint = by_name(client.call("get_joint_constraint_state", {"scene_name": scene})["physics_joints"]).get(name)
            if joint is None:
                break
            angular, linear = joint_excess(joint["backend"])
            worst_angular = max(worst_angular, angular)
            worst_linear  = max(worst_linear, linear)
        check_true(
            f"4 {name}: holds under an impulse",
            (worst_angular <= angular_tolerance) and (worst_linear <= linear_tolerance),
            f"worst excess {math.degrees(worst_angular):.2f} deg, {worst_linear * 1000.0:.1f} mm"
        )
    client.call("toggle_physics", {"enabled": False})
    client.call("advance_time", {"mode": "wall_clock"})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    # About 1.3 m/s at a 1 kg box. Harder hits push Jolt's default solver
    # (10 velocity / 2 position steps) past combined swing + twist limits:
    # measured 2.6 m/s -> ~15 mm, 3.7 m/s -> ~28 mm pivot separation on the
    # ball joints; the hinges hold.
    parser.add_argument("--impulse", type=float, nargs=3, default=[1.0, 0.3, 0.7], help="impulse on each stressed box, N s (default 1.0 0.3 0.7)")
    parser.add_argument("--angular-tolerance", type=float, default=math.radians(1.0), help="radians a stressed joint may exceed its limits by (default 1 degree)")
    parser.add_argument("--linear-tolerance", type=float, default=0.003, help="meters a stressed joint may exceed its limits by (default 0.003)")
    parser.add_argument("--skip-stress", action="store_true", help="skip the physics stress section")
    args = parser.parse_args()

    client = McpClient(args.port)
    wait_for_server(client, 60.0)
    client.call("toggle_physics", {"enabled": False})
    scene = load_asset(client)
    print(f"loaded {ASSET_PATH} as '{scene}'")

    check_static(client, scene)
    check_filter(client, scene)
    check_ik_drags(client, scene)
    if not args.skip_stress:
        check_stress(client, scene, args.impulse, args.angular_tolerance, args.linear_tolerance)
    client.call("close_scene", {"scene_name": scene})
    return report()


if __name__ == "__main__":
    sys.exit(main())
