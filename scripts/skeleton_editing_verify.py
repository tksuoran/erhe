#!/usr/bin/env python3
"""Skeleton editing slice A (doc/plans/rigging/skeleton_editing.md R10-R13), driven as a user does.

On res/editor/assets/RiggedFigure/RiggedFigure.glb (its bones carry the side
before a trailing index: arm_joint_L_1 .. arm_joint_R_3, leg_joint_L_1 ..
leg_joint_R_5), every verb runs from the Hierarchy context menu of a bone: the
row is revealed by typing the bone's name into the Hierarchy filter, then
right-clicked, then the menu entry is clicked. Explicit-argument MCP tools
only set up the selection and read the results back. Flip Names is checked
for exactly one undo step, and Ctrl+Z over the viewport restoring the names.

    py -3 scripts/skeleton_editing_verify.py [--port N] [--launch]

--launch starts build_vs2026_vulkan_headless/bin/Debug/editor.exe (or
--editor), reads its MCP port from logs/log.txt and asks it to exit at the
end. One PASS/FAIL line per check, exit code 1 when any check fails.
"""

import argparse
import sys

from erhe_mcp import DEFAULT_PORT, McpClient, check_true, report, wait_for_server
from ik_interactive_pass_verify import DEFAULT_EDITOR, Editor, launch_editor

ASSET_PATH = "res/editor/assets/RiggedFigure/RiggedFigure.glb"
BONE_ENTRIES = ["Select Parent", "Select Children", "Select Children (All)", "Select Chain", "Select Mirror", "Flip Names"]


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
        return {item.get("label") for item in self.e.items(label_contains="Select") + self.e.items(label_contains="Flip")}

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

    def undo_over_viewport(self):
        v = self.viewport
        self.e.move(v["x"] + (v["width"] / 2.0), v["y"] + (v["height"] / 2.0))
        self.e.key("z", ["ctrl"])
        self.e.advance(4)


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
    try:
        figure = setup(e)
        selection_checks(figure)
        flip_names_checks(figure)
    finally:
        if process is not None:
            try:
                client.call("request_exit")
                process.wait(timeout=60)
            except Exception:
                process.kill()
    return report()


if __name__ == "__main__":
    sys.exit(main())
