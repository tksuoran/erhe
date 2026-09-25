#!/usr/bin/env python3
"""Skeleton editing slices A, B, the foundations, C and D (doc/plans/rigging/skeleton_editing.md R1-R18), driven as a user does.

On res/editor/assets/RiggedFigure/RiggedFigure.glb (its bones carry the side
before a trailing index: arm_joint_L_1 .. arm_joint_R_3, leg_joint_L_1 ..
leg_joint_R_5), every verb runs from the Hierarchy context menu of a bone: the
row is revealed by typing the bone's name into the Hierarchy filter, then
right-clicked, then the menu entry is clicked. Explicit-argument MCP tools
only set up the selection and the pose, and read the results back. Flip
Names, Clear Rotation / Location and Paste Pose Flipped are each checked for
exactly one undo step, and Ctrl+Z over the viewport restoring what they
changed.

Section D builds a skeleton no skin lists in a scene of its own: nodes made
bones with the Properties window's Rig > Bone checkbox, a child connected
with Rig > Connected, the parent's Rig > Tail typed into its row (the
connected child follows, one undo step, Ctrl+Z restores), the bone proxies
redrawn from the new tail (bone selection mode, screenshots compared along
the old and the new tail) and a glTF save + reopen that keeps the bones, the
tail and the connection.

Section E authors a skeleton in another scene through the Hierarchy menu:
Create > Bone on a plain node, Extrude twice, Subdivide > 2 Bones, Delete
Bone and Dissolve Bone, each checked for its result, exactly one undo step
and Ctrl+Z restoring it; on RiggedFigure's skin joints the structure entries
are offered disabled and extrude_bones is refused with the skin named in the
log; a glTF save + reopen keeps the authored bones, tails, connected flags
and rest values.

Section F mirrors a one-sided arm authored off the X = 0 plane: Symmetrize
from the menu of a selected bone (the mirrored bones' world heads and tails
checked), Recalculate Roll > X to World Z (the X axis aimed, the bone axis and
the child kept) and Align to Active, each one undo step Ctrl+Z reverts; on
RiggedFigure's skin joints the three entries are offered disabled.

Section G runs first, before any other scene's viewport window can float over
its own, which its screenshots read. It sets a bone's display from its Properties rows in bone selection
mode: Rig > Display Color Mode 'custom' (the bone draws in the default orange),
Display Color.x typed to 0 (green) and Display Shape 'box' (the prism, wide
beside the tail half where the octahedron is narrow), each checked in a
screenshot and one undo step, Ctrl+Z restoring the shape. Then it grows the
bone into a three-bone chain inside a box mesh, binds the mesh with the
Hierarchy menu entry 'Bind to Selected Bones (Rigid)' of the mesh row (the
bones selected; one undo step; the entry disabled once the mesh is skinned),
checks that an IK drag of the top bone deforms the mesh (skinned bounds and a
screenshot diff) and that Ctrl+Z undoes the drag and then the bind.

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
from ik_interactive_pass_verify import (DEFAULT_EDITOR, ROW_PROPERTIES, USD_PATH, Editor, Rig, ensure_group_open,
                                       ensure_row_visible, have_pil, launch_editor, load_rgb, load_scene_file, log_since,
                                       log_size, select_for_properties, ui_checkbox, ui_type_field, undo_viewport)

ASSET_PATH = "res/editor/assets/RiggedFigure/RiggedFigure.glb"
BONE_ENTRIES = ["Select Parent", "Select Children", "Select Children (All)", "Select Chain", "Select Mirror", "Flip Names"]
POSE_ENTRIES = ["Clear Location", "Clear Rotation", "Clear Scale", "Clear All", "Copy Pose", "Paste Pose", "Paste Pose Flipped"]


def hierarchy_window(editor: Editor, scene: str) -> str:
    """The Scene Hierarchy window listing `scene` (a closed scene's window can linger)."""
    windows = [w["name"] for w in editor.call("get_imgui_windows")["windows"]]
    hierarchy = [name for name in windows if name.startswith("Scene Hierarchy")]
    if not hierarchy:
        raise RuntimeError("no Scene Hierarchy window")
    for name in reversed(hierarchy):
        if any(Editor.label_matches(item, scene) for item in editor.items(window=name)):
            return name
    return hierarchy[-1]


class Figure:
    """RiggedFigure imported into its own scene, driven through the Hierarchy window."""

    def __init__(self, editor: Editor, scene: str) -> None:
        self.e = editor
        self.scene = scene
        self.hierarchy = hierarchy_window(editor, scene)
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
    """Local Rig.* values are counted by the USD save's 'not written' warning, as Ik.* values are."""
    usd_scene = load_scene_file(e, USD_PATH)
    opened.append(usd_scene)
    formats = {sc["name"]: sc.get("source_format") for sc in e.call("list_scenes")["scenes"]}
    if formats.get(usd_scene) != "usd":
        print(f"  [SKIP] C.10 USD save warning ({USD_PATH} did not open as a USD scene)")
        return
    e.call("import_gltf", {"scene_name": usd_scene, "path": ASSET_PATH})
    e.wait_idle()
    # A bound bone refuses a Rig.* write (R9): the local value goes on a bone
    # created in the scene, whose creation records its Rig.tail and rest.
    e.call("create_bone", {"scene_name": usd_scene, "name": "usd_rest_bone"})
    e.advance(4)
    # Create Bone selects the new bone; later sections select in their own scenes.
    e.call("select_items", {"scene_name": usd_scene, "ids": []})
    e.advance(2)
    usd_path = os.path.join(tempfile.mkdtemp(prefix="erhe_skeleton_editing_"), "rest.usda")
    offset = log_size()
    e.call("save_scene", {"scene_name": usd_scene, "path": usd_path})
    e.wait_idle()
    text = log_since(offset)
    warned = ("carry IK settings or rest transforms" in text) and ("1 node(s)" in text)
    check_true("C.10 saving as USD warns that the one created bone's Rig.* values (its rest transform) are not written",
               warned and os.path.isfile(usd_path), f"warned={warned} file={os.path.isfile(usd_path)}")


# --- section D: foundations (R1, R3, R4) -----------------------------------

ROW_PROPERTIES[("Rig", "Bone")] = "bone"
ROW_PROPERTIES[("Rig", "Connected")] = "Rig.connected"


def parse_vec(text):
    return [float(v) for v in text.replace(",", " ").split()]


def changed_fraction(path_a, path_b, rig, head, tail, begin=0.55, end=0.95, samples=12, radius=2, threshold=40):
    """Fraction of points along the projected head-tail segment (between `begin` and `end` of
    its length) where the two screenshots differ by more than `threshold` somewhere within `radius`."""
    image_a, pixels_a = load_rgb(path_a)
    _, pixels_b = load_rgb(path_b)
    size = image_a.size
    hits = 0
    for k in range(samples):
        f = begin + ((end - begin) * ((k + 0.5) / samples))
        x, y = rig.project([head[i] + ((tail[i] - head[i]) * f) for i in range(3)])
        found = False
        for py in range(int(y) - radius, int(y) + radius + 1):
            for px in range(int(x) - radius, int(x) + radius + 1):
                if (0 <= px < size[0]) and (0 <= py < size[1]):
                    a = pixels_a[px, py]
                    b = pixels_b[px, py]
                    if sum(abs(a[i] - b[i]) for i in range(3)) > threshold:
                        found = True
        if found:
            hits += 1
    return hits / samples


def foundations_checks(e: Editor, opened):
    print("\n== Foundations: authored bones, Rig.tail, Rig.connected (R1, R3, R4) ==")
    scene = e.create_scene()
    opened.append(scene)
    rig = Rig(e, scene)

    def create(name, parent, position):
        args = {"scene_name": scene, "name": name, "position": position}
        if parent is not None:
            args["parent_node_id"] = rig.ids[parent]
        rig.ids[name] = e.call("create_node", args)["node_id"]
        e.advance(3)

    create("rig_root", None, [0.0, 0.0, 0.0])
    create("rig_upper", "rig_root", [0.0, 1.0, 0.0])
    create("rig_side", "rig_root", [0.6, 0.0, 0.0])
    names = ("rig_root", "rig_upper", "rig_side")

    made = all(ui_checkbox(rig, name, "Rig", "Bone", True) for name in names)
    check_true("D.1 the Properties 'Rig > Bone' checkbox makes the three new nodes bones (no skin lists them)",
               made and all(rig.prop(name, "bone")["value"] == "true" for name in names),
               f"bone={[rig.prop(name, 'bone')['value'] for name in names]}")
    default_tail = rig.prop("rig_root", "Rig.tail")
    check_true("D.2 rig_root's Rig.tail defaults to its first bone child's head (0, 1, 0)",
               (default_tail["source"] == "default") and (distance(parse_vec(default_tail["value"]), [0.0, 1.0, 0.0]) < 1.0e-4),
               f"Rig.tail={default_tail['value']} ({default_tail['source']})")

    rig.viewport = [v for v in e.call("get_viewports")["viewports"] if v["scene"] == scene][0]
    depth_before = e.undo_depth()
    connected = ui_checkbox(rig, "rig_side", "Rig", "Connected", True)
    side = rig.node("rig_side")["local_transform"]["translation"]
    check_true("D.3 'Rig > Connected' on rig_side snaps its head onto rig_root's tail, one undo step",
               connected and (distance(side, [0.0, 1.0, 0.0]) < 1.0e-4) and (e.undo_depth() == depth_before + 1),
               f"rig_side at {[round(v, 4) for v in side]}, undo depth {depth_before} -> {e.undo_depth()}")
    undo_viewport(rig)
    e.advance(4)
    side = rig.node("rig_side")["local_transform"]["translation"]
    check_true("D.4 Ctrl+Z puts rig_side back at (0.6, 0, 0), not connected",
               (distance(side, [0.6, 0.0, 0.0]) < 1.0e-4) and (rig.prop("rig_side", "Rig.connected")["value"] == "false"),
               f"rig_side at {[round(v, 4) for v in side]}")
    ui_checkbox(rig, "rig_upper", "Rig", "Connected", True)

    # Bone selection mode shows every bone proxy; a frontal camera on the skeleton.
    e.call("set_mesh_component_mode", {"mode": "bone"})
    e.advance(3)
    cameras = [n for n in e.call("get_scene_nodes", {"scene_name": scene})["nodes"] if (n["type"] == "Camera") and (n["parent"] == "root")]
    if cameras:
        rig.place_camera([0.0, 1.0, 4.0], [0.0, 1.0, 0.0])
    rig.viewport = [v for v in e.call("get_viewports")["viewports"] if v["scene"] == scene][0]
    shots = tempfile.mkdtemp(prefix="erhe_skeleton_editing_")
    before_png = os.path.join(shots, "tail_before.png")
    after_png = os.path.join(shots, "tail_after.png")
    select_for_properties(rig, "rig_root")
    e.advance(4)
    e.call("capture_screenshot", {"path": before_png})

    depth_before = e.undo_depth()
    typed = ui_type_field(rig, "rig_root", "Rig", "Tail.x", "0.8")
    e.advance(4)
    tail = rig.prop("rig_root", "Rig.tail")
    upper = rig.node("rig_upper")["local_transform"]["translation"]
    side = rig.node("rig_side")["local_transform"]["translation"]
    check_true("D.5 typing 0.8 into rig_root's 'Rig > Tail.x' row moves the connected rig_upper to the new tail (0.8, 1, 0), "
               "rig_side (not connected) stays, one undo step",
               typed and (tail["source"] == "local") and (distance(upper, [0.8, 1.0, 0.0]) < 1.0e-4)
               and (distance(side, [0.6, 0.0, 0.0]) < 1.0e-4) and (e.undo_depth() == depth_before + 1),
               f"Rig.tail={tail['value']} ({tail['source']}) rig_upper at {[round(v, 4) for v in upper]} "
               f"rig_side at {[round(v, 4) for v in side]} undo depth {depth_before} -> {e.undo_depth()}")

    e.call("capture_screenshot", {"path": after_png})
    if have_pil() and cameras:
        head = rig.node("rig_root")["world_transform"]["translation"]
        new_part = changed_fraction(before_png, after_png, rig, head, [0.8, 1.0, 0.0])
        old_part = changed_fraction(before_png, after_png, rig, head, [0.0, 1.0, 0.0])
        check_true("D.6 the bone proxies redraw from the new tail: the screenshots differ along the far part of both the "
                   "new and the old rig_root head-tail segment", (new_part >= 0.6) and (old_part >= 0.6),
                   f"changed along the new tail {new_part:.2f}, along the old tail {old_part:.2f} ({shots})")
    else:
        print(f"  [SKIP] D.6 proxy redraw (PIL {'present' if have_pil() else 'missing'}, scene camera {'found' if cameras else 'missing'})")

    undo_viewport(rig)
    e.advance(4)
    upper = rig.node("rig_upper")["local_transform"]["translation"]
    check_true("D.7 Ctrl+Z restores rig_root's default tail and rig_upper's head in one step",
               (rig.prop("rig_root", "Rig.tail")["source"] == "default") and (distance(upper, [0.0, 1.0, 0.0]) < 1.0e-4)
               and (e.undo_depth() == depth_before),
               f"rig_upper at {[round(v, 4) for v in upper]} undo depth {e.undo_depth()}")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.advance(2)

    # Save + reopen: the authored bones, the tail and the connection persist.
    e.call("set_item_property", {"item_id": rig.ids["rig_root"], "property": "Rig.tail", "value": "0.8 1 0"})
    e.advance(4)
    path = os.path.join(shots, "unskinned_skeleton.glb")
    e.call("save_scene", {"scene_name": scene, "path": path})
    e.wait_idle()
    reopened = load_scene_file(e, path)
    opened.append(reopened)
    nodes = {n["name"]: n["id"] for n in e.call("get_scene_nodes", {"scene_name": reopened})["nodes"] if n.get("content", False)}
    reloaded = Rig(e, reopened)
    reloaded.ids = {name: nodes.get(name) for name in names}
    missing = [name for name in names if reloaded.ids[name] is None]
    bones = [] if missing else [reloaded.prop(name, "bone")["value"] for name in names]
    tail = None if missing else reloaded.prop("rig_root", "Rig.tail")
    check_true("D.8 a glTF save + reopen keeps the three bones (no skin), rig_root's tail and rig_upper's connection",
               (not missing) and (bones == ["true"] * 3) and (tail["source"] == "local")
               and (distance(parse_vec(tail["value"]), [0.8, 1.0, 0.0]) < 1.0e-4)
               and (reloaded.prop("rig_upper", "Rig.connected")["value"] == "true"),
               f"missing={missing} bone={bones} Rig.tail={tail['value'] if tail else None}")


# --- section E: bone creation and structure (R5-R9) -------------------------

STRUCTURE_ENTRIES = ["Extrude", "Subdivide", "Delete Bone", "Dissolve Bone"]


class Skeleton:
    """A scene of its own for the authored skeleton, driven through its Hierarchy window like Figure."""

    def __init__(self, editor: Editor, scene: str) -> None:
        self.e = editor
        self.scene = scene
        self.hierarchy = hierarchy_window(editor, scene)
        self.viewport = [v for v in editor.call("get_viewports")["viewports"] if v["scene"] == scene][0]

    def set_filter(self, text):
        self.e.click(self.hierarchy, "##Filter")
        self.e.key("a", ["ctrl"])
        self.e.key("backspace")
        if text:
            self.e.call("type_text", {"text": text})
        self.e.advance(4)

    def run(self, node, *entries):
        """Right-click the node's row, then click the menu entries in turn (a submenu, then its item)."""
        self.set_filter(node)
        self.e.click(self.hierarchy, node, button="right")
        self.e.advance(2)
        for entry in entries:
            self.e.click(None, entry)
            self.e.advance(2)
        self.e.advance(4)
        self.set_filter("")

    def details(self, name):
        try:
            return self.e.call("get_node_details", {"scene_name": self.scene, "node_name": name})
        except RuntimeError:
            return None

    def exists(self, name):
        return self.details(name) is not None

    def parent(self, name):
        d = self.details(name)
        return d["parent"] if d else None

    def world(self, name):
        return self.details(name)["world_transform"]["translation"]

    def local(self, name):
        return self.details(name)["local_transform"]["translation"]

    def prop(self, name, prop):
        d = self.details(name)
        if d is None:
            return None
        for entry in self.e.call("get_item_properties", {"item_id": d["id"]})["properties"]:
            if entry.get("name") == prop:
                return entry
        return None

    def vec(self, name, prop):
        return parse_vec(self.prop(name, prop)["value"])

    def undo(self):
        v = self.viewport
        self.e.move(v["x"] + (v["width"] / 2.0), v["y"] + (v["height"] / 2.0))
        self.e.key("z", ["ctrl"])
        self.e.advance(4)


def vec_close(a, b, eps=1.0e-4):
    return (len(a) == len(b)) and all(abs(a[i] - b[i]) < eps for i in range(len(a)))


def near(a, b, eps=1.0e-4):
    return (a is not None) and (b is not None) and (distance(a, b) < eps)


def structure_refusal_checks(figure: Figure):
    """R9 on RiggedFigure, while its Hierarchy window is the visible one."""
    print("\n== Bone structure refused on a bound skeleton (R9) ==")
    e = figure.e
    figure.set_filter("arm_joint_L_2")
    e.click(figure.hierarchy, "arm_joint_L_2", button="right")
    e.advance(2)
    items = {item.get("label"): item for item in e.items(visible_only=True) if item.get("label") in STRUCTURE_ENTRIES}
    e.key("escape")
    figure.set_filter("")
    disabled = {label: items[label].get("status", {}).get("disabled", False) for label in items}
    check_true("E.1 on RiggedFigure's 'arm_joint_L_2' (a skin joint) the structure entries are offered disabled",
               (set(disabled) == set(STRUCTURE_ENTRIES)) and all(disabled.values()), f"disabled={disabled}")
    offset = log_size()
    depth = e.undo_depth()
    try:
        e.call("extrude_bones", {"scene_name": figure.scene, "bones": ["arm_joint_L_2"]})
        refused_call = False
    except RuntimeError:
        refused_call = True
    e.advance(2)
    text = log_since(offset)
    check_true("E.2 extrude_bones on it is refused and the log names the skin; nothing is queued",
               refused_call and ("is a joint of skin '" in text) and (e.undo_depth() == depth),
               f"refused={refused_call} logged={'is a joint of skin' in text}")


def structure_checks(e: Editor, opened):
    print("\n== Bone creation and structure (R5-R9) ==")
    scene = e.create_scene()
    opened.append(scene)
    e.call("create_node", {"scene_name": scene, "name": "skeleton_base", "position": [0.0, 0.0, 0.0]})
    e.advance(3)
    s = Skeleton(e, scene)

    # E.3 / E.4: Create > Bone on a non-bone node.
    depth = e.undo_depth()
    s.run("skeleton_base", "Create", "Bone")
    created = s.exists("Bone") and (s.parent("Bone") == "skeleton_base")
    tail = s.prop("Bone", "Rig.tail") if created else None
    selected = created and s.details("Bone")["selected"]
    check_true("E.3 Create > Bone on 'skeleton_base' adds bone 'Bone' at the node's origin, tail +Y (local value), "
               "selected, one undo step",
               created and near(s.world("Bone"), [0.0, 0.0, 0.0]) and (tail["source"] == "local")
               and near(parse_vec(tail["value"]), [0.0, 1.0, 0.0]) and selected
               and (s.prop("Bone", "bone")["value"] == "true") and (e.undo_depth() == depth + 1),
               f"created={created} tail={tail['value'] if tail else None} selected={selected} undo depth {depth} -> {e.undo_depth()}")
    s.undo()
    check_true("E.4 Ctrl+Z removes the created bone", (not s.exists("Bone")) and (e.undo_depth() == depth),
               f"exists={s.exists('Bone')} undo depth {e.undo_depth()}")
    s.run("skeleton_base", "Create", "Bone")

    # E.5 - E.7: Extrude twice grows a connected chain.
    depth = e.undo_depth()
    s.run("Bone", "Extrude")
    ok = s.exists("Bone.001") and (s.parent("Bone.001") == "Bone")
    check_true("E.5 Extrude on 'Bone' adds the connected child 'Bone.001' on its tail (0, 1, 0), same tail, selected, one undo step",
               ok and near(s.world("Bone.001"), [0.0, 1.0, 0.0]) and near(s.vec("Bone.001", "Rig.tail"), [0.0, 1.0, 0.0])
               and (s.prop("Bone.001", "Rig.connected")["value"] == "true") and s.details("Bone.001")["selected"]
               and (not s.details("Bone")["selected"]) and (e.undo_depth() == depth + 1),
               f"exists={ok} world={s.world('Bone.001') if ok else None} undo depth {depth} -> {e.undo_depth()}")
    s.undo()
    check_true("E.6 Ctrl+Z removes the extruded bone", not s.exists("Bone.001"), f"exists={s.exists('Bone.001')}")
    s.run("Bone", "Extrude")
    depth = e.undo_depth()
    s.run("Bone.001", "Extrude")
    ok = s.exists("Bone.002") and (s.parent("Bone.002") == "Bone.001")
    check_true("E.7 a second Extrude on 'Bone.001' adds 'Bone.002' at (0, 2, 0), one undo step",
               ok and near(s.world("Bone.002"), [0.0, 2.0, 0.0]) and (e.undo_depth() == depth + 1),
               f"exists={ok} world={s.world('Bone.002') if ok else None} undo depth {depth} -> {e.undo_depth()}")

    # E.8 / E.9: Subdivide > 2 Bones.
    depth = e.undo_depth()
    s.run("Bone.001", "Subdivide", "2 Bones")
    ok = s.exists("Bone.003") and (s.parent("Bone.003") == "Bone.001") and (s.parent("Bone.002") == "Bone.003")
    check_true("E.8 Subdivide > 2 Bones on 'Bone.001' halves its tail, adds 'Bone.003' at (0, 1.5, 0) and moves 'Bone.002' "
               "under it keeping its world position, one undo step",
               ok and near(s.vec("Bone.001", "Rig.tail"), [0.0, 0.5, 0.0]) and near(s.world("Bone.003"), [0.0, 1.5, 0.0])
               and near(s.world("Bone.002"), [0.0, 2.0, 0.0]) and near(s.local("Bone.002"), [0.0, 0.5, 0.0])
               and (e.undo_depth() == depth + 1),
               f"structure={ok} Bone.002 parent={s.parent('Bone.002')} undo depth {depth} -> {e.undo_depth()}")
    s.undo()
    check_true("E.9 Ctrl+Z restores 'Bone.001' whole: its tail, 'Bone.002' back under it at local (0, 1, 0)",
               (not s.exists("Bone.003")) and (s.parent("Bone.002") == "Bone.001") and near(s.vec("Bone.001", "Rig.tail"), [0.0, 1.0, 0.0])
               and near(s.local("Bone.002"), [0.0, 1.0, 0.0]) and (e.undo_depth() == depth),
               f"Bone.003 exists={s.exists('Bone.003')} Bone.002 parent={s.parent('Bone.002')}")

    # E.10 / E.11: Delete Bone.
    depth = e.undo_depth()
    s.run("Bone.001", "Delete Bone")
    ok = (not s.exists("Bone.001")) and (s.parent("Bone.002") == "Bone")
    check_true("E.10 Delete Bone on 'Bone.001' moves 'Bone.002' to 'Bone' keeping (0, 2, 0), disconnected, 'Bone' tail kept, one undo step",
               ok and near(s.world("Bone.002"), [0.0, 2.0, 0.0]) and (s.prop("Bone.002", "Rig.connected")["value"] == "false")
               and near(s.vec("Bone", "Rig.tail"), [0.0, 1.0, 0.0]) and (e.undo_depth() == depth + 1),
               f"removed={ok} undo depth {depth} -> {e.undo_depth()}")
    s.undo()
    check_true("E.11 Ctrl+Z restores 'Bone.001' with 'Bone.002' connected under it",
               s.exists("Bone.001") and (s.parent("Bone.002") == "Bone.001") and (s.prop("Bone.002", "Rig.connected")["value"] == "true")
               and near(s.local("Bone.002"), [0.0, 1.0, 0.0]) and (e.undo_depth() == depth),
               f"Bone.002 parent={s.parent('Bone.002')}")

    # E.12 / E.13: Dissolve Bone.
    depth = e.undo_depth()
    s.run("Bone.001", "Dissolve Bone")
    ok = (not s.exists("Bone.001")) and (s.parent("Bone.002") == "Bone")
    check_true("E.12 Dissolve Bone on 'Bone.001' extends 'Bone''s tail to (0, 2, 0); 'Bone.002' stays connected on it, one undo step",
               ok and near(s.vec("Bone", "Rig.tail"), [0.0, 2.0, 0.0]) and near(s.local("Bone.002"), [0.0, 2.0, 0.0])
               and (s.prop("Bone.002", "Rig.connected")["value"] == "true") and (e.undo_depth() == depth + 1),
               f"removed={ok} Bone tail={s.prop('Bone', 'Rig.tail')['value']} undo depth {depth} -> {e.undo_depth()}")
    s.undo()
    check_true("E.13 Ctrl+Z restores 'Bone.001' and 'Bone''s tail",
               s.exists("Bone.001") and (s.parent("Bone.002") == "Bone.001") and near(s.vec("Bone", "Rig.tail"), [0.0, 1.0, 0.0])
               and (e.undo_depth() == depth),
               f"Bone tail={s.prop('Bone', 'Rig.tail')['value']}")

    # E.14: glTF save + reopen keeps the authored skeleton.
    path = os.path.join(tempfile.mkdtemp(prefix="erhe_skeleton_editing_"), "authored_skeleton.glb")
    names = ("Bone", "Bone.001", "Bone.002")
    before = {name: {prop: s.prop(name, prop)["value"] for prop in ("bone", "Rig.tail", "Rig.connected", "Rig.rest_translation", "Rig.rest_rotation", "Rig.rest_scale")}
              for name in names}
    e.call("save_scene", {"scene_name": scene, "path": path})
    e.wait_idle()
    reopened = load_scene_file(e, path)
    opened.append(reopened)
    r = Skeleton(e, reopened)
    after = {}
    for name in names:
        if not r.exists(name):
            after[name] = None
            continue
        after[name] = {prop: r.prop(name, prop)["value"] for prop in before[name]}
    same = all((after[name] is not None) and all(
        (after[name][prop] == before[name][prop]) if prop in ("bone", "Rig.connected")
        else vec_close(parse_vec(after[name][prop]), parse_vec(before[name][prop]))
        for prop in before[name]) for name in names)
    parents = [r.parent(name) for name in names]
    check_true("E.14 a glTF save + reopen keeps the authored bones, tails, connected flags and rest values",
               same and (parents == ["skeleton_base", "Bone", "Bone.001"]),
               f"parents={parents} after={after}")


# --- section F: Symmetrize (R13) and bone roll (R16) -------------------------

SYMMETRY_ENTRIES = ["Symmetrize", "Recalculate Roll", "Align to Active"]


def symmetry_refusal_checks(figure: Figure):
    """R9 for the slice C symmetry and roll entries on RiggedFigure, while its Hierarchy window is the visible one."""
    print("\n== Symmetrize and roll refused on a bound skeleton (R9) ==")
    e = figure.e
    figure.clear_selection()
    figure.set_filter("arm_joint_L_2")
    e.click(figure.hierarchy, "arm_joint_L_2", button="right")
    e.advance(2)
    items = {item.get("label"): item for item in e.items(visible_only=True) if item.get("label") in SYMMETRY_ENTRIES}
    e.key("escape")
    figure.set_filter("")
    disabled = {label: items[label].get("status", {}).get("disabled", False) for label in items}
    check_true("F.1 on RiggedFigure's 'arm_joint_L_2' (a skin joint) Symmetrize, Recalculate Roll and Align to Active are offered disabled",
               (set(disabled) == set(SYMMETRY_ENTRIES)) and all(disabled.values()), f"disabled={disabled}")


def normalized(q):
    length = math.sqrt(sum(c * c for c in q))
    return [c / length for c in q]


def world_rotation(s, name):
    return s.details(name)["world_transform"]["rotation_xyzw"]


def world_tail(s, name):
    head = s.world(name)
    tail = quat_rotate(world_rotation(s, name), s.vec(name, "Rig.tail"))
    return [head[i] + tail[i] for i in range(3)]


def mirror_x(p):
    return [-p[0], p[1], p[2]]


def vec_angle_deg(a, b):
    cross = [(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])]
    return math.degrees(math.atan2(math.sqrt(sum(c * c for c in cross)), sum(a[i] * b[i] for i in range(3))))


def rotation_angle_deg(a, b):
    """The angle of conj(a) * b, both [x, y, z, w], well conditioned near zero."""
    va, vb = a[:3], b[:3]
    cross = [(va[1] * vb[2]) - (va[2] * vb[1]), (va[2] * vb[0]) - (va[0] * vb[2]), (va[0] * vb[1]) - (va[1] * vb[0])]
    w = (a[3] * b[3]) + sum(va[i] * vb[i] for i in range(3))
    v = [(a[3] * vb[i]) - (b[3] * va[i]) - cross[i] for i in range(3)]
    return math.degrees(2.0 * math.atan2(math.sqrt(sum(c * c for c in v)), abs(w)))


def symmetry_checks(e: Editor, opened):
    print("\n== Symmetrize and bone roll (R13, R16) ==")
    scene = e.create_scene()
    opened.append(scene)
    e.call("create_node", {"scene_name": scene, "name": "rig_base", "position": [0.0, 0.0, 0.0]})
    e.advance(3)
    s = Skeleton(e, scene)
    # Setup through explicit-argument tools: a spine and a one-sided two-bone
    # arm off the X = 0 plane of the skeleton frame (rig_base's frame).
    e.call("create_bone", {"scene_name": scene, "parent": "rig_base", "name": "spine"})
    e.advance(3)
    e.call("create_bone", {"scene_name": scene, "parent": "spine", "name": "arm_L"})
    e.advance(3)
    e.call("set_node_transform", {"scene_name": scene, "node_name": "arm_L", "space": "local",
                                  "translation": [0.3, 1.1, 0.2], "rotation_xyzw": normalized([0.1, 0.2, -0.55, 0.67])})
    e.advance(3)
    e.call("extrude_bones", {"scene_name": scene, "bones": ["arm_L"]})
    e.advance(3)
    e.call("select_items", {"scene_name": scene, "paths": ["arm_L", "arm_L.001"], "active": "arm_L"})
    e.advance(2)
    heads = {name: s.world(name) for name in ("arm_L", "arm_L.001")}
    tails = {name: world_tail(s, name) for name in ("arm_L", "arm_L.001")}

    # F.2 / F.3: Symmetrize on the selected side, from the menu of a selected bone.
    depth = e.undo_depth()
    s.run("arm_L", "Symmetrize")
    ok = s.exists("arm_R") and s.exists("arm_R.001") and (s.parent("arm_R") == "spine") and (s.parent("arm_R.001") == "arm_R")
    mirrored = ok and all(near(s.world(flip), mirror_x(heads[name]), 1.0e-5) and near(world_tail(s, flip), mirror_x(tails[name]), 1.0e-5)
                          for name, flip in (("arm_L", "arm_R"), ("arm_L.001", "arm_R.001")))
    check_true("F.2 Symmetrize on the selected 'arm_L' + 'arm_L.001' creates 'arm_R' under 'spine' and 'arm_R.001' under it, "
               "world heads and tails mirrored across X = 0 (1e-5), connected kept, one undo step",
               ok and mirrored and (s.prop("arm_R.001", "Rig.connected")["value"] == "true") and (e.undo_depth() == depth + 1),
               f"structure={ok} mirrored={mirrored} undo depth {depth} -> {e.undo_depth()}")
    s.undo()
    check_true("F.3 Ctrl+Z removes the mirrored side", (not s.exists("arm_R")) and (not s.exists("arm_R.001")) and (e.undo_depth() == depth),
               f"arm_R exists={s.exists('arm_R')} undo depth {e.undo_depth()}")

    # F.4 / F.5: Recalculate Roll > X to World Z on 'arm_L' (the clicked bone alone).
    e.call("select_items", {"scene_name": scene, "ids": []})
    e.advance(2)
    local_before = s.details("arm_L")["local_transform"]["rotation_xyzw"]
    axis_before = quat_rotate(world_rotation(s, "arm_L"), s.vec("arm_L", "Rig.tail"))
    child_before = s.details("arm_L.001")["world_transform"]
    depth = e.undo_depth()
    s.run("arm_L", "Recalculate Roll", "X to World Z")
    q = world_rotation(s, "arm_L")
    axis = quat_rotate(q, s.vec("arm_L", "Rig.tail"))
    length = math.sqrt(sum(a * a for a in axis))
    unit = [a / length for a in axis]
    z_in_plane = [0.0 - (unit[2] * unit[0]), 0.0 - (unit[2] * unit[1]), 1.0 - (unit[2] * unit[2])]
    aim = vec_angle_deg(quat_rotate(q, [1.0, 0.0, 0.0]), z_in_plane)
    turned = vec_angle_deg(axis, axis_before)
    child_after = s.details("arm_L.001")["world_transform"]
    child_kept = near(child_after["translation"], child_before["translation"], 1.0e-5) and \
        (rotation_angle_deg(child_after["rotation_xyzw"], child_before["rotation_xyzw"]) < 1.0e-3)
    check_true("F.4 Recalculate Roll > X to World Z on 'arm_L': its X axis points to +Z in the plane across the bone (1e-4 deg), "
               "the head-to-tail axis and the child 'arm_L.001' stay, one undo step",
               (aim < 1.0e-4) and (turned < 1.0e-4) and child_kept and (e.undo_depth() == depth + 1),
               f"aim={aim:.2e} deg axis turn={turned:.2e} deg child kept={child_kept} undo depth {depth} -> {e.undo_depth()}")
    s.undo()
    restored = rotation_angle_deg(s.details("arm_L")["local_transform"]["rotation_xyzw"], local_before)
    check_true("F.5 Ctrl+Z restores 'arm_L''s local rotation", (restored < 1.0e-4) and (e.undo_depth() == depth),
               f"restored within {restored:.2e} deg")

    # F.6 / F.7: Align to Active: 'arm_L.001' onto the active 'spine'.
    e.call("select_items", {"scene_name": scene, "paths": ["arm_L.001", "spine"], "active": "spine"})
    e.advance(2)
    head_before = s.world("arm_L.001")
    rotation_before = world_rotation(s, "arm_L.001")
    depth = e.undo_depth()
    s.run("arm_L.001", "Align to Active")
    same = rotation_angle_deg(world_rotation(s, "arm_L.001"), world_rotation(s, "spine"))
    check_true("F.6 Align to Active on the selected 'arm_L.001' with 'spine' active gives it the spine's world frame, head kept, one undo step",
               (same < 1.0e-3) and near(s.world("arm_L.001"), head_before, 1.0e-5) and (e.undo_depth() == depth + 1),
               f"frame differs by {same:.2e} deg undo depth {depth} -> {e.undo_depth()}")
    s.undo()
    back = rotation_angle_deg(world_rotation(s, "arm_L.001"), rotation_before)
    check_true("F.7 Ctrl+Z restores 'arm_L.001''s rotation", (back < 1.0e-3) and (e.undo_depth() == depth),
               f"restored within {back:.2e} deg")


# --- section G: bone display (R17) and Bind (rigid) (R18) -------------------

def ui_combo(rig: Rig, node, group, row, choice):
    """Pick `choice` in the Properties combo row `row` of group `group`, as a user does."""
    e = rig.e
    if not select_for_properties(rig, node) or not ensure_group_open(e, group):
        return False
    item = ensure_row_visible(e, row)
    if item is None:
        return False
    # A new scene's viewport window may float over part of the Properties
    # window: click along the combo until its popup lists `choice`.
    for fraction in (0.03, 0.5, 0.97):
        e.call("mouse_click", {"x": item["x"] + (fraction * item["width"]), "y": item["center_y"]})
        e.advance(2)
        popups = [w for w in e.call("get_imgui_windows")["windows"]
                  if w.get("name", "").startswith("##Combo") and not w.get("hidden", True)]
        if popups:
            e.click(None, choice)
            e.advance(3)
            return True
    return False


def is_orange(p):
    return (p[0] > 60) and (p[1] > 0.2 * p[0]) and (p[1] < 0.75 * p[0]) and (p[2] < 0.25 * p[0])


def is_green(p):
    return (p[1] > 40) and (p[0] < 0.3 * p[1]) and (p[2] < 0.3 * p[1])


def pixel_fraction(path, rig, points, predicate, radius=1):
    """Fraction of the projected world `points` with a `predicate` pixel within `radius`."""
    image, pixels = load_rgb(path)
    hits = 0
    for point in points:
        x, y = rig.project(point)
        found = False
        for py in range(int(y) - radius, int(y) + radius + 1):
            for px in range(int(x) - radius, int(x) + radius + 1):
                if (0 <= px < image.size[0]) and (0 <= py < image.size[1]) and predicate(pixels[px, py]):
                    found = True
        hits += 1 if found else 0
    return hits / len(points)


def changed_points(path_a, path_b, rig, points, radius=2, threshold=40):
    """Fraction of the projected world `points` where the two screenshots differ within `radius`."""
    image_a, pixels_a = load_rgb(path_a)
    _, pixels_b = load_rgb(path_b)
    hits = 0
    for point in points:
        x, y = rig.project(point)
        found = False
        for py in range(int(y) - radius, int(y) + radius + 1):
            for px in range(int(x) - radius, int(x) + radius + 1):
                if (0 <= px < image_a.size[0]) and (0 <= py < image_a.size[1]):
                    a = pixels_a[px, py]
                    b = pixels_b[px, py]
                    if sum(abs(a[i] - b[i]) for i in range(3)) > threshold:
                        found = True
        hits += 1 if found else 0
    return hits / len(points)


def display_and_bind_checks(e: Editor, opened):
    print("\n== Bone display and Bind (rigid) (R17, R18) ==")
    scene = e.create_scene()
    opened.append(scene)
    s = Skeleton(e, scene)
    rig = Rig(e, scene)
    shots = tempfile.mkdtemp(prefix="erhe_skeleton_display_")

    # Setup through explicit-argument tools: a bone 'arm' from the origin up +Y, one unit.
    e.call("create_bone", {"scene_name": scene, "name": "arm"})
    e.advance(3)
    rig.ids["arm"] = s.details("arm")["id"]
    e.call("select_items", {"scene_name": scene, "ids": []})
    e.advance(2)
    e.call("set_mesh_component_mode", {"mode": "bone"})
    e.advance(3)
    rig.place_camera([0.0, 0.5, 2.5], [0.0, 0.5, 0.0])
    axis = [[0.0, 0.15 + (0.7 * (k / 11.0)), 0.0] for k in range(12)]
    # Beside the tail half, where only the box is wide: the octahedron's
    # half-width there is at most 0.033 of the bone length, the box's 0.1.
    beside = [[side * 0.07, y, 0.0] for side in (-1.0, 1.0) for y in (0.7, 0.75, 0.8, 0.85, 0.9)]
    have_shots = have_pil()

    def shot(name):
        path = os.path.join(shots, name)
        e.call("select_items", {"scene_name": scene, "ids": []})
        e.advance(4)
        e.call("capture_screenshot", {"path": path})
        return path

    # G.1 / G.2: Display Color Mode 'custom' from its Properties combo: the solid bone turns orange (the default color).
    before = shot("style.png") if have_shots else None
    depth = e.undo_depth()
    picked = ui_combo(rig, "arm", "Rig", "Display Color Mode", "custom")
    mode = rig.prop("arm", "Rig.display_color_mode")["value"]
    custom = shot("custom.png") if have_shots else None
    if have_shots:
        grey_orange = pixel_fraction(before, rig, axis, is_orange)
        custom_orange = pixel_fraction(custom, rig, axis, is_orange)
        check_true("G.1 'Rig > Display Color Mode' set to 'custom' in the Properties combo: the solid bone draws in the default "
                   "display color (orange along its axis, not before), one undo step",
                   picked and (mode == "custom") and (grey_orange < 0.2) and (custom_orange >= 0.8) and (e.undo_depth() == depth + 1),
                   f"mode={mode} orange before {grey_orange:.2f} after {custom_orange:.2f} undo depth {depth} -> {e.undo_depth()} ({shots})")
    else:
        print("  [SKIP] G.1 display color screenshot (PIL missing)")

    # G.2: typing 0 into 'Display Color.x' makes it green (0, 0.45, 0).
    depth = e.undo_depth()
    typed = ui_type_field(rig, "arm", "Rig", "Display Color.x", "0")
    e.advance(4)
    color = parse_vec(rig.prop("arm", "Rig.display_color")["value"])
    green = shot("green.png") if have_shots else None
    green_fraction = pixel_fraction(green, rig, axis, is_green) if have_shots else 1.0
    check_true("G.2 typing 0 into 'Rig > Display Color.x' makes the display color (0, 0.45, 0): the bone draws green, one undo step",
               typed and (distance(color, [0.0, 0.45, 0.0]) < 1.0e-4) and (green_fraction >= 0.8) and (e.undo_depth() == depth + 1),
               f"color={color} green along the axis {green_fraction:.2f} undo depth {depth} -> {e.undo_depth()}")

    # G.3 / G.4: Display Shape 'box' from its combo: the bone fills out beside its tail half; Ctrl+Z restores the octahedron.
    depth = e.undo_depth()
    picked = ui_combo(rig, "arm", "Rig", "Display Shape", "box")
    shape = rig.prop("arm", "Rig.display_shape")["value"]
    box = shot("box.png") if have_shots else None
    if have_shots:
        octahedral_beside = pixel_fraction(green, rig, beside, is_green)
        box_beside = pixel_fraction(box, rig, beside, is_green)
        check_true("G.3 'Rig > Display Shape' set to 'box' in the Properties combo: the bone is drawn as the prism "
                   "(green beside its tail half, where the octahedron is not), one undo step",
                   picked and (shape == "box") and (octahedral_beside <= 0.2) and (box_beside >= 0.8) and (e.undo_depth() == depth + 1),
                   f"shape={shape} beside the tail: octahedral {octahedral_beside:.2f} box {box_beside:.2f} undo depth {depth} -> {e.undo_depth()}")
    else:
        print("  [SKIP] G.3 display shape screenshot (PIL missing)")
    s.undo()
    undone = shot("undone.png") if have_shots else None
    undone_beside = pixel_fraction(undone, rig, beside, is_green) if have_shots else 0.0
    check_true("G.4 Ctrl+Z puts the octahedron back", (rig.prop("arm", "Rig.display_shape")["value"] == "octahedral") and (undone_beside <= 0.2),
               f"shape={rig.prop('arm', 'Rig.display_shape')['value']} green beside the tail {undone_beside:.2f}")
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.advance(3)

    # Setup: extend 'arm' into a three-bone chain along +Y and a box mesh around it.
    e.call("extrude_bones", {"scene_name": scene, "bones": ["arm"]})
    e.advance(3)
    e.call("extrude_bones", {"scene_name": scene, "bones": ["arm.001"]})
    e.advance(3)
    e.call("create_shape", {"scene_name": scene, "shape": "box", "name": "body", "size": [0.3, 3.0, 0.3], "steps": [1, 6, 1],
                            "position": [0.0, 1.5, 0.0], "motion_mode": "none"})
    e.advance(4)
    e.call("select_items", {"scene_name": scene, "paths": ["arm", "arm.001", "arm.002"], "active": "arm"})
    e.advance(2)
    rig.place_camera([0.0, 1.5, 6.0], [0.0, 1.5, 0.0])

    # G.5 / G.6: 'Bind to Selected Bones (Rigid)' from the Hierarchy menu of the mesh.
    depth = e.undo_depth()
    s.run("body", "Bind to Selected Bones (Rigid)")
    body = s.details("body")
    mesh = body["mesh"] if (body is not None) and ("mesh" in body) else {}
    joints = [j.get("node_name") for j in mesh.get("joints", [])]
    check_true("G.5 'Bind to Selected Bones (Rigid)' on the 'body' row with the three bones selected skins 'body' to them "
               "('body skin', joints in order), one undo step",
               mesh.get("skinned", False) and (mesh.get("skin_name") == "body skin") and (joints == ["arm", "arm.001", "arm.002"])
               and (e.undo_depth() == depth + 1),
               f"skinned={mesh.get('skinned')} skin={mesh.get('skin_name')} joints={joints} undo depth {depth} -> {e.undo_depth()}")
    s.set_filter("body")
    e.click(s.hierarchy, "body", button="right")
    e.advance(2)
    entry = [item for item in e.items(visible_only=True) if item.get("label") == "Bind to Selected Bones (Rigid)"]
    e.key("escape")
    s.set_filter("")
    check_true("G.6 on the now skinned 'body' the entry is offered disabled",
               bool(entry) and entry[0].get("status", {}).get("disabled", False), f"entry={[i.get('status') for i in entry]}")

    # G.7: an IK drag of the top bone deforms the mesh (skinned bounds and screenshot).
    top = [[0.0, 2.2 + (0.7 * (k / 7.0)), 0.0] for k in range(8)]
    e.call("select_items", {"scene_name": scene, "ids": []})
    e.advance(2)
    rest_png = shot("bound_rest.png") if have_shots else None
    bounds_before = s.details("body")["mesh"]["world_aabb"]
    e.call("ik_drag", {"scene_name": scene, "node_name": "arm.002", "target": [0.9, 1.6, 0.0]})
    e.advance(4)
    bounds_after = s.details("body")["mesh"]["world_aabb"]
    dragged_png = shot("bound_dragged.png") if have_shots else None
    moved = changed_points(rest_png, dragged_png, rig, top) if have_shots else 1.0
    check_true("G.7 an IK drag of 'arm.002' toward (0.9, 1.6, 0) deforms the bound mesh: its skinned bounds grow toward +X "
               "and the screenshot changes along the top bone's part",
               (bounds_after["max"][0] > bounds_before["max"][0] + 0.3) and (moved >= 0.6),
               f"max x {bounds_before['max'][0]:.3f} -> {bounds_after['max'][0]:.3f} changed along the top {moved:.2f}")

    # G.8: Ctrl+Z twice: the drag, then the bind.
    s.undo()
    s.undo()
    body = s.details("body")
    mesh = body["mesh"] if (body is not None) and ("mesh" in body) else {}
    check_true("G.8 Ctrl+Z undoes the drag, a second Ctrl+Z the bind: 'body' is unskinned again",
               (body is not None) and (not mesh.get("skinned", True)) and (e.undo_depth() == depth),
               f"skinned={mesh.get('skinned')} undo depth {e.undo_depth()} (before the bind {depth})")


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
        display_and_bind_checks(e, opened)
        opened.clear()  # setup() resets the editor, which closes that scene
        figure = setup(e)
        opened.append(figure.scene)
        selection_checks(figure)
        flip_names_checks(figure)
        posing_checks(figure)
        structure_refusal_checks(figure)
        symmetry_refusal_checks(figure)
        usd_warning_check(e, opened)
        foundations_checks(e, opened)
        structure_checks(e, opened)
        symmetry_checks(e, opened)
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
