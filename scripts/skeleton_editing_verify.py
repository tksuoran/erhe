#!/usr/bin/env python3
"""Skeleton editing slices A and B (doc/plans/rigging/skeleton_editing.md R10-R15), driven as a user does.

On res/editor/assets/RiggedFigure/RiggedFigure.glb (its bones carry the side
before a trailing index: arm_joint_L_1 .. arm_joint_R_3, leg_joint_L_1 ..
leg_joint_R_5), every verb runs from the Hierarchy context menu of a bone: the
row is revealed by typing the bone's name into the Hierarchy filter, then
right-clicked, then the menu entry is clicked. Explicit-argument MCP tools
only set up the selection and the pose, and read the results back. Flip
Names, Clear Rotation / Location and Paste Pose Flipped are each checked for
exactly one undo step, and Ctrl+Z over the viewport restoring what they
changed.

    py -3 scripts/skeleton_editing_verify.py [--port N] [--launch]

--launch starts build_vs2026_vulkan_headless/bin/Debug/editor.exe (or
--editor), reads its MCP port from logs/log.txt and asks it to exit at the
end. The scenes the script opens are closed before that, so the editor's
window-visibility file (config/editor/desktop_windows.json, rewritten at exit)
is left as the script found it. One PASS/FAIL line per check, exit code 1 when
any check fails.
"""

import argparse
import math
import os
import sys
import tempfile

from erhe_mcp import DEFAULT_PORT, McpClient, check_true, report, wait_for_server
from ik_interactive_pass_verify import DEFAULT_EDITOR, USD_PATH, Editor, launch_editor, load_scene_file, log_since, log_size

ASSET_PATH = "res/editor/assets/RiggedFigure/RiggedFigure.glb"
BONE_ENTRIES = ["Select Parent", "Select Children", "Select Children (All)", "Select Chain", "Select Mirror", "Flip Names"]
POSE_ENTRIES = ["Clear Location", "Clear Rotation", "Clear Scale", "Clear All", "Copy Pose", "Paste Pose", "Paste Pose Flipped"]


class Figure:
    """RiggedFigure imported into its own scene, driven through the Hierarchy window."""

    def __init__(self, editor: Editor, scene: str) -> None:
        self.e = editor
        self.scene = scene
        windows = [w["name"] for w in editor.call("get_imgui_windows")["windows"]]
        hierarchy = [name for name in windows if name.startswith("Scene Hierarchy")]
        if not hierarchy:
            raise RuntimeError("no Scene Hierarchy window")
        self.hierarchy = hierarchy[-1]
        self.viewport = [v for v in editor.call("get_viewports")["viewports"] if v["scene"] == scene][0]
        nodes = editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]
        self.ids = {n["name"]: n["id"] for n in nodes if n.get("content", False)}

    def set_filter(self, text):
        """Type into the Hierarchy filter: the rows on the way to every match open."""
        self.e.click(self.hierarchy, "##Filter")
        self.e.key("a", ["ctrl"])
        self.e.key("backspace")
        if text:
            self.e.call("type_text", {"text": text})
        self.e.advance(4)

    def open_menu(self, bone):
        """Reveal the bone's row and right-click it."""
        self.set_filter(bone)
        self.e.click(self.hierarchy, bone, button="right")
        self.e.advance(2)

    def menu_labels(self):
        items = []
        for text in ("Select", "Flip", "Clear", "Pose"):
            items += self.e.items(label_contains=text)
        return {item.get("label") for item in items}

    def run_entry(self, bone, entry):
        self.open_menu(bone)
        self.e.click(None, entry)
        self.e.advance(4)

    def select(self, names, active=None):
        args = {"scene_name": self.scene, "paths": names}
        if active is not None:
            args["active"] = active
        self.e.call("select_items", args)
        self.e.advance(2)

    def clear_selection(self):
        self.e.call("select_items", {"scene_name": self.scene, "ids": []})
        self.e.advance(2)

    def selection(self):
        state = self.e.call("get_selection")
        names = sorted(item["name"] for item in state["items"] if item.get("scene_name", self.scene) == self.scene)
        active = state.get("active_item", {}).get("name") if state.get("active_item") else None
        return names, active

    def name_of(self, node_id):
        return self.e.call("get_node_details", {"scene_name": self.scene, "node_id": node_id})["name"]

    def node(self, name):
        return self.e.call("get_node_details", {"scene_name": self.scene, "node_id": self.ids[name]})

    def local(self, name):
        return self.node(name)["local_transform"]

    def world_position(self, name):
        return self.node(name)["world_transform"]["translation"]

    def set_local(self, name, **components):
        args = {"scene_name": self.scene, "node_id": self.ids[name], "space": "local"}
        args.update(components)
        self.e.call("set_node_transform", args)
        self.e.advance(2)

    def set_flags(self, name, flags, enabled=True):
        self.e.call("set_item_flags", {"scene_name": self.scene, "ids": [self.ids[name]], "flags": flags, "enabled": enabled})
        self.e.advance(2)

    def mirror_in_root(self, point):
        """The point mirrored across the X = 0 plane of the skeleton root torso_joint_1."""
        root = self.node("torso_joint_1")["world_transform"]
        t = root["translation"]
        q = root["rotation_xyzw"]
        local = quat_rotate([-q[0], -q[1], -q[2], q[3]], [point[i] - t[i] for i in range(3)])
        local[0] = -local[0]
        back = quat_rotate(q, local)
        return [back[i] + t[i] for i in range(3)]

    def undo_over_viewport(self):
        v = self.viewport
        self.e.move(v["x"] + (v["width"] / 2.0), v["y"] + (v["height"] / 2.0))
        self.e.key("z", ["ctrl"])
        self.e.advance(4)


def quat_rotate(q, v):
    """v rotated by the unit quaternion q = [x, y, z, w]."""
    x, y, z, w = q
    t = [2.0 * ((y * v[2]) - (z * v[1])), 2.0 * ((z * v[0]) - (x * v[2])), 2.0 * ((x * v[1]) - (y * v[0]))]
    return [v[0] + (w * t[0]) + ((y * t[2]) - (z * t[1])),
            v[1] + (w * t[1]) + ((z * t[0]) - (x * t[2])),
            v[2] + (w * t[2]) + ((x * t[1]) - (y * t[0]))]


def quat_angle_deg(a, b):
    d = min(1.0, abs(sum(a[i] * b[i] for i in range(4))))
    return math.degrees(2.0 * math.acos(d))


def distance(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def setup(e: Editor) -> Figure:
    print("\n== Setup ==")
    e.call("reset_editor_state")
    scene = e.create_scene()
    e.call("import_gltf", {"scene_name": scene, "path": ASSET_PATH})
    e.wait_idle()
    figure = Figure(e, scene)
    for name in ("torso_joint_1", "arm_joint_L_2", "leg_joint_R_5"):
        if name not in figure.ids:
            raise RuntimeError(f"setup: bone '{name}' missing after import")
    return figure


def check_selection(figure: Figure, label, bone, entry, expected, expected_active, keep_selection=False):
    # A right-click on an unselected bone acts on that bone alone; clear the
    # previous check's result so a bone it selected does not widen the targets.
    if not keep_selection:
        figure.clear_selection()
    figure.run_entry(bone, entry)
    names, active = figure.selection()
    check_true(label, (names == sorted(expected)) and (active == expected_active),
               f"selection={names} active={active}")


def selection_checks(figure: Figure):
    print("\n== Selection helpers (R10, R12) ==")
    figure.clear_selection()
    figure.open_menu("Armature")
    offered = figure.menu_labels() & set(BONE_ENTRIES)
    figure.e.key("escape")
    check_true("A.1 the bone entries are offered only on a bone (none on 'Armature')", not offered, f"offered={sorted(offered)}")

    figure.open_menu("arm_joint_L_2")
    offered = figure.menu_labels() & set(BONE_ENTRIES)
    figure.e.key("escape")
    check_true("A.2 a bone's context menu offers Select Parent / Children / Children (All) / Chain / Mirror and Flip Names",
               offered == set(BONE_ENTRIES), f"offered={sorted(offered)}")

    check_selection(figure, "A.3 Select Parent on arm_joint_L_2 selects arm_joint_L_1 (active)",
                    "arm_joint_L_2", "Select Parent", ["arm_joint_L_1"], "arm_joint_L_1")
    check_selection(figure, "A.4 Select Children on torso_joint_3 selects neck_joint_1, arm_joint_L_1, arm_joint_R_1",
                    "torso_joint_3", "Select Children", ["neck_joint_1", "arm_joint_L_1", "arm_joint_R_1"], "neck_joint_1")
    check_selection(figure, "A.5 Select Children (All) on torso_joint_3 selects the neck and both arms",
                    "torso_joint_3", "Select Children (All)",
                    ["neck_joint_1", "neck_joint_2", "arm_joint_L_1", "arm_joint_L_2", "arm_joint_L_3",
                     "arm_joint_R_1", "arm_joint_R_2", "arm_joint_R_3"], "neck_joint_1")
    check_selection(figure, "A.6 Select Chain on arm_joint_R_2 selects arm_joint_R_1 .. arm_joint_R_3 (stops at the branching torso_joint_3)",
                    "arm_joint_R_2", "Select Chain", ["arm_joint_R_1", "arm_joint_R_2", "arm_joint_R_3"], "arm_joint_R_2")
    check_selection(figure, "A.7 Select Chain on torso_joint_2 selects torso_joint_2, torso_joint_3 (branching bones end chains)",
                    "torso_joint_2", "Select Chain", ["torso_joint_2", "torso_joint_3"], "torso_joint_2")
    check_selection(figure, "A.8 Select Mirror on leg_joint_L_3 selects leg_joint_R_3",
                    "leg_joint_L_3", "Select Mirror", ["leg_joint_R_3"], "leg_joint_R_3")

    # A right-click on a selected bone acts on every selected bone.
    figure.select(["leg_joint_R_5", "arm_joint_L_2"], active="arm_joint_L_2")
    check_selection(figure, "A.9 Select Mirror on a selected bone mirrors the whole selection (arm_joint_L_2 + leg_joint_R_5)",
                    "arm_joint_L_2", "Select Mirror", ["arm_joint_R_2", "leg_joint_L_5"], "arm_joint_R_2",
                    keep_selection=True)

    # Nothing to select: the selection stays.
    figure.select(["torso_joint_2"])
    check_selection(figure, "A.10 Select Mirror on torso_joint_2 (no side) leaves the selection unchanged",
                    "torso_joint_2", "Select Mirror", ["torso_joint_2"], "torso_joint_2", keep_selection=True)


def flip_names_checks(figure: Figure):
    print("\n== Flip Names (R13) ==")
    ids = {name: figure.ids[name] for name in ("leg_joint_L_1", "leg_joint_R_1", "arm_joint_L_3", "torso_joint_2")}
    # The two leg roots are siblings under torso_joint_1: they swap names.
    figure.select(["leg_joint_L_1", "leg_joint_R_1", "arm_joint_L_3", "torso_joint_2"], active="leg_joint_L_1")
    depth_before = figure.e.undo_depth()
    figure.run_entry("leg_joint_L_1", "Flip Names")
    after = {name: figure.name_of(node_id) for name, node_id in ids.items()}
    depth_after = figure.e.undo_depth()
    expected = {"leg_joint_L_1": "leg_joint_R_1", "leg_joint_R_1": "leg_joint_L_1",
                "arm_joint_L_3": "arm_joint_R_3", "torso_joint_2": "torso_joint_2"}
    check_true("B.1 Flip Names on the selection swaps the sibling leg roots, flips arm_joint_L_3, leaves torso_joint_2",
               after == expected, f"names by original name={after}")
    check_true("B.2 Flip Names is exactly one undo step", depth_after == depth_before + 1,
               f"undo depth {depth_before} -> {depth_after}")

    figure.undo_over_viewport()
    restored = {name: figure.name_of(node_id) for name, node_id in ids.items()}
    check_true("B.3 Ctrl+Z restores every name in one step",
               (restored == {name: name for name in ids}) and (figure.e.undo_depth() == depth_before),
               f"names={restored} undo depth={figure.e.undo_depth()}")
    figure.set_filter("")


def posing_checks(figure: Figure):
    print("\n== Posing verbs (R14, R15) ==")
    figure.clear_selection()
    figure.open_menu("arm_joint_L_2")
    offered = figure.menu_labels() & set(POSE_ENTRIES)
    figure.e.key("escape")
    check_true("C.1 a bone's context menu offers Clear Location / Rotation / Scale / All, Copy Pose, Paste Pose (Flipped)",
               offered == set(POSE_ENTRIES), f"offered={sorted(offered)}")

    # The imported pose is the bind pose, which Rig.rest_* default to.
    rest = {name: figure.local(name) for name in ("arm_joint_L_1", "arm_joint_L_2", "arm_joint_L_3", "arm_joint_R_1", "arm_joint_R_2", "arm_joint_R_3")}

    # Clear Rotation restores the rest rotation, in one undo step.
    figure.set_local("arm_joint_L_2", rotation_xyzw=[0.2588190, 0.0, 0.0, 0.9659258])
    posed = figure.local("arm_joint_L_2")["rotation_xyzw"]
    depth_before = figure.e.undo_depth()
    figure.run_entry("arm_joint_L_2", "Clear Rotation")
    cleared = figure.local("arm_joint_L_2")
    check_true("C.2 Clear Rotation on a posed arm_joint_L_2 restores its rest rotation and keeps its location",
               (quat_angle_deg(posed, rest["arm_joint_L_2"]["rotation_xyzw"]) > 5.0)
               and (quat_angle_deg(cleared["rotation_xyzw"], rest["arm_joint_L_2"]["rotation_xyzw"]) < 0.1)
               and (distance(cleared["translation"], rest["arm_joint_L_2"]["translation"]) < 1.0e-5),
               f"posed-to-rest={quat_angle_deg(posed, rest['arm_joint_L_2']['rotation_xyzw']):.2f} deg "
               f"cleared-to-rest={quat_angle_deg(cleared['rotation_xyzw'], rest['arm_joint_L_2']['rotation_xyzw']):.3f} deg")
    check_true("C.3 Clear Rotation is exactly one undo step", figure.e.undo_depth() == depth_before + 1,
               f"undo depth {depth_before} -> {figure.e.undo_depth()}")
    figure.undo_over_viewport()
    check_true("C.4 Ctrl+Z restores the posed rotation",
               quat_angle_deg(figure.local("arm_joint_L_2")["rotation_xyzw"], posed) < 0.1,
               f"to-posed={quat_angle_deg(figure.local('arm_joint_L_2')['rotation_xyzw'], posed):.3f} deg")
    figure.set_local("arm_joint_L_2", rotation_xyzw=rest["arm_joint_L_2"]["rotation_xyzw"])

    # A locked channel stays: translation Y of arm_joint_L_3.
    figure.set_local("arm_joint_L_3", translation=[0.05, 0.25, 0.02])
    figure.set_flags("arm_joint_L_3", ["lock_translation_y"])
    depth_before = figure.e.undo_depth()
    figure.run_entry("arm_joint_L_3", "Clear Location")
    t = figure.local("arm_joint_L_3")["translation"]
    r = rest["arm_joint_L_3"]["translation"]
    check_true("C.5 Clear Location keeps the locked translation Y (0.25) and restores X and Z",
               (abs(t[1] - 0.25) < 1.0e-5) and (abs(t[0] - r[0]) < 1.0e-5) and (abs(t[2] - r[2]) < 1.0e-5),
               f"translation={[round(v, 5) for v in t]} rest={[round(v, 5) for v in r]}")
    check_true("C.6 Clear Location is exactly one undo step", figure.e.undo_depth() == depth_before + 1,
               f"undo depth {depth_before} -> {figure.e.undo_depth()}")
    figure.undo_over_viewport()
    figure.set_flags("arm_joint_L_3", ["lock_translation_y"], enabled=False)
    figure.set_local("arm_joint_L_3", translation=r)

    # Copy Pose on the posed left arm, Paste Pose Flipped onto the right arm.
    right_rest_positions = {name: figure.world_position(name) for name in ("arm_joint_R_2", "arm_joint_R_3")}
    rest_errors = {right: distance(right_rest_positions[right], figure.mirror_in_root(figure.world_position(left)))
                   for left, right in (("arm_joint_L_2", "arm_joint_R_2"), ("arm_joint_L_3", "arm_joint_R_3"))}
    figure.set_local("arm_joint_L_1", rotation_xyzw=[0.0, 0.0, 0.3826834, 0.9238795])
    figure.set_local("arm_joint_L_2", rotation_xyzw=[0.2588190, 0.0, 0.0, 0.9659258])
    figure.select(["arm_joint_L_1", "arm_joint_L_2", "arm_joint_L_3"], active="arm_joint_L_1")
    figure.run_entry("arm_joint_L_1", "Copy Pose")
    figure.clear_selection()
    depth_before = figure.e.undo_depth()
    figure.run_entry("arm_joint_R_1", "Paste Pose Flipped")
    errors = {}
    moved = {}
    for left, right in (("arm_joint_L_2", "arm_joint_R_2"), ("arm_joint_L_3", "arm_joint_R_3")):
        position = figure.world_position(right)
        errors[right] = distance(position, figure.mirror_in_root(figure.world_position(left)))
        moved[right] = distance(position, right_rest_positions[right])
    check_true("C.7 Paste Pose Flipped puts the right arm at the mirror of the posed left arm "
               "(to within the rest pose's own asymmetry + 1 mm)",
               all((moved[name] > 0.02) and (errors[name] < rest_errors[name] + 1.0e-3) for name in errors),
               f"moved={ {k: round(v, 4) for k, v in moved.items()} } mirror error={ {k: round(v, 4) for k, v in errors.items()} } "
               f"rest asymmetry={ {k: round(v, 4) for k, v in rest_errors.items()} }")
    check_true("C.8 Paste Pose Flipped is exactly one undo step", figure.e.undo_depth() == depth_before + 1,
               f"undo depth {depth_before} -> {figure.e.undo_depth()}")
    figure.undo_over_viewport()
    restored = max(quat_angle_deg(figure.local(name)["rotation_xyzw"], rest[name]["rotation_xyzw"])
                   for name in ("arm_joint_R_1", "arm_joint_R_2", "arm_joint_R_3"))
    check_true("C.9 Ctrl+Z restores the right arm in one step",
               (restored < 0.1) and (figure.e.undo_depth() == depth_before), f"max angle to rest={restored:.3f} deg")
    figure.set_filter("")


def usd_warning_check(e: Editor, opened):
    """A local Rig.* value is counted by the USD save's 'not written' warning, as Ik.* values are."""
    usd_scene = load_scene_file(e, USD_PATH)
    opened.append(usd_scene)
    formats = {sc["name"]: sc.get("source_format") for sc in e.call("list_scenes")["scenes"]}
    if formats.get(usd_scene) != "usd":
        print(f"  [SKIP] C.10 USD save warning ({USD_PATH} did not open as a USD scene)")
        return
    e.call("import_gltf", {"scene_name": usd_scene, "path": ASSET_PATH})
    e.wait_idle()
    bone = [n["id"] for n in e.call("get_scene_nodes", {"scene_name": usd_scene})["nodes"]
            if (n["name"] == "arm_joint_L_2") and n.get("content", False)][0]
    e.call("set_item_property", {"item_id": bone, "property": "Rig.rest_rotation", "value": "0 0 0.3826834 0.9238795"})
    e.advance(4)
    usd_path = os.path.join(tempfile.mkdtemp(prefix="erhe_skeleton_editing_"), "rest.usda")
    offset = log_size()
    e.call("save_scene", {"scene_name": usd_scene, "path": usd_path})
    e.wait_idle()
    text = log_since(offset)
    warned = ("carry IK settings or rest transforms" in text) and ("1 node(s)" in text)
    check_true("C.10 saving as USD warns that the one node's rest transform (Rig.*) is not written",
               warned and os.path.isfile(usd_path), f"warned={warned} file={os.path.isfile(usd_path)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=None)
    parser.add_argument("--launch", action="store_true", help="start the headless editor and stop it at the end")
    parser.add_argument("--editor", default=DEFAULT_EDITOR, help=f"editor executable for --launch (default {DEFAULT_EDITOR})")
    args = parser.parse_args()

    process = None
    port = args.port if args.port is not None else DEFAULT_PORT
    if args.launch:
        process, port = launch_editor(args.editor)
        print(f"launched {args.editor} (MCP port {port})")
    client = McpClient(port)
    wait_for_server(client, 60.0)
    e = Editor(client)
    opened = []
    try:
        figure = setup(e)
        opened.append(figure.scene)
        selection_checks(figure)
        flip_names_checks(figure)
        posing_checks(figure)
        usd_warning_check(e, opened)
    finally:
        # Closing the scenes closes their viewport windows, which the editor
        # would otherwise record as open in desktop_windows.json at exit.
        for scene in reversed(opened):
            e.close_scene(scene)
        if process is not None:
            try:
                client.call("request_exit")
                process.wait(timeout=60)
            except Exception:
                process.kill()
    return report()


if __name__ == "__main__":
    sys.exit(main())
