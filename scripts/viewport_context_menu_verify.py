#!/usr/bin/env python3
"""Viewport context menu: a right click in a viewport opens the Scene Hierarchy window's context menu.

See doc/editor/windows.md "Viewport context menu". Checks, each driven as a
user does through the MCP input gestures (a right click at a point of the
viewport, then a click on a menu entry):

1. No selection, the pointer on a box: the menu offers Cut, Copy and Create,
   no Rename, and Paste disabled while the clipboard is empty. Cut, Copy,
   Duplicate and Delete name the box. Copy fills
   the clipboard; the next menu offers Paste enabled. Cut removes the hovered
   box alone, and undo brings it back.
2. A selected box, the pointer on the other box: Cut removes the selected
   box, not the hovered one.
3. No selection, the pointer on nothing: the menu offers Create and Paste
   and no Cut. Paste inserts the clipboard contents under the scene root.
4. Create > Xform with the pointer on a box: the new node is a child of the
   box and its world position is the hovered point. One undo removes it.
5. Paste with the pointer on a box: the entry names the clipboard's item,
   and the pasted box is a child of the hovered box at the hovered point.
6. A right drag turns the camera and opens no menu.

    python3 scripts/viewport_context_menu_verify.py [--editor <path>]

Launches the headless editor (prepared_asset_verify.Editor: deterministic
clock, default layout) and stops it at the end. One PASS/FAIL line per check,
exit code 1 when any check fails.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import prepared_asset_verify as pav  # noqa: E402
from prepared_asset_verify import Editor, check  # noqa: E402

DEFAULT_EDITOR = "build_ninja_linux_vulkan_headless/bin/editor"
BOX_A = "Menu Box A"
BOX_B = "Menu Box B"
MENU_LABELS = ("Create", "Cut", "Copy", "Paste", "Duplicate", "Delete", "Rename")
WAIT_FRAMES = 120


class Session:
    def __init__(self, editor: Editor) -> None:
        self.e = editor
        self.scene = None
        self.box_a = None
        self.box_b = None

    # Scene reads

    def nodes(self):
        return self.e.call("get_scene_nodes", {"scene_name": self.scene})["nodes"]

    def node_ids(self):
        # The editor's own nodes (the hotbar's rendertarget) come and go; the checks follow the scene's content.
        return {n["id"] for n in self.nodes() if n["name"].startswith(("Menu Box", "new "))}

    def node_by_name(self, name):
        return [n for n in self.nodes() if n["name"] == name]

    def details(self, node_id):
        return self.e.call("get_node_details", {"scene_name": self.scene, "node_id": node_id})

    def viewport(self):
        viewports = [v for v in self.e.call("get_viewports")["viewports"] if v.get("scene") == self.scene]
        if not viewports:
            raise RuntimeError(f"no viewport shows scene {self.scene}")
        return viewports[0]

    def viewport_point(self, fx, fy):
        v = self.viewport()
        return [round(v["x"] + (v["width"] * fx)), round(v["y"] + (v["height"] * fy))]

    def settle(self):
        self.e.wait_idle()

    def select(self, ids):
        self.e.call("select_items", {"scene_name": self.scene, "ids": ids})
        self.e.wait_frames(4)

    def undo_depth(self):
        return len(self.e.call("get_undo_redo_stack")["undo"])

    # The menu

    def menu_items(self):
        """The entries of the open context menu by label; empty when no menu is open."""
        items = self.e.call("get_imgui_items", {"visible_only": True, "limit": 2000})["items"]
        windows = {}
        for item in items:
            # A clipboard entry names its target: 'Cut "name"###Cut'.
            label = item.get("label", "").split("###")[-1]
            if label in MENU_LABELS:
                windows.setdefault(item.get("window"), {})[label] = item
        # The menu is the popup that holds Create: the Hierarchy and Edit menus are not open.
        for window, entries in windows.items():
            if ("Create" in entries) and str(window).startswith("##Popup"):
                return entries
        return {}

    def wait_menu(self, shown=True):
        for _ in range(WAIT_FRAMES):
            entries = self.menu_items()
            if bool(entries) == shown:
                return entries
            self.e.wait_frames(1)
        return self.menu_items()

    def right_click(self, point):
        self.e.call("mouse_click", {"x": point[0], "y": point[1], "button": "right"})
        return self.wait_menu(True)

    def click_entry(self, entries, label):
        self.e.call("imgui_click", {"id": entries[label]["id"]})
        self.wait_menu(False)
        self.settle()

    def close_menu(self):
        self.e.call("key_press", {"key": "escape"})
        return self.wait_menu(False)


def shown(item):
    """The entry's text as the menu shows it."""
    return item.get("label", "").split("###")[0]


def is_disabled(item):
    return bool(item.get("status", {}).get("disabled", False))


def setup(e: Editor) -> Session:
    print("\n== Setup ==")
    s = Session(e)
    e.call("reset_editor_state")
    e.wait_idle()
    before = set(e.scene_names())
    e.call("create_scene")
    for _ in range(WAIT_FRAMES):
        new = [name for name in e.scene_names() if name not in before]
        if new:
            s.scene = new[0]
            break
        e.wait_frames(1)
    check("setup: a new scene", s.scene is not None, str(s.scene))
    # No rigid bodies: a right press on a dynamic body would start the physics drag.
    a = e.call("create_shape", {"scene_name": s.scene, "shape": "box", "size": [1.0, 1.0, 1.0], "name": BOX_A,
                                "position": [0.0, 0.5, 0.0], "motion_mode": "none"})
    b = e.call("create_shape", {"scene_name": s.scene, "shape": "box", "size": [1.0, 1.0, 1.0], "name": BOX_B,
                                "position": [-3.0, 0.5, 0.0], "motion_mode": "none"})
    e.wait_idle()
    s.box_a = a["node_id"]
    s.box_b = b["node_id"]
    # The camera looks down -z at box A: the viewport center is the middle of its +z face.
    camera = s.viewport()["camera"]
    e.call("set_node_transform", {"scene_name": s.scene, "node_name": camera, "translation": [0.0, 0.5, 8.0],
                                  "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]})
    e.call("select_items", {"scene_name": s.scene, "ids": []})
    e.wait_idle()
    check("setup: boxes created", {s.box_a, s.box_b} <= s.node_ids())
    return s


def check_hovered_item(s: Session, on_a):
    print("\n== 1. No selection, the pointer on a box ==")
    e = s.e
    entries = s.right_click(on_a)
    check("1.1 a right click on a box opens the menu", bool(entries), ", ".join(sorted(entries)))
    if not entries:
        return
    check("1.2 the menu offers Create, Cut, Copy, Paste", all(label in entries for label in ("Create", "Cut", "Copy", "Paste")))
    check("1.3 the menu offers no Rename", "Rename" not in entries)
    check("1.4 Cut and Copy are enabled", (not is_disabled(entries["Cut"])) and (not is_disabled(entries["Copy"])))
    check("1.5 Paste is disabled while the clipboard is empty", is_disabled(entries["Paste"]) and (shown(entries["Paste"]) == "Paste"),
          shown(entries["Paste"]))
    names = [shown(entries[label]) for label in ("Cut", "Copy", "Duplicate", "Delete")]
    check("1.5b Cut, Copy, Duplicate and Delete name the hovered box",
          names == [f'{verb} "{BOX_A}"' for verb in ("Cut", "Copy", "Duplicate", "Delete")], str(names))
    s.click_entry(entries, "Copy")
    check("1.6 Copy leaves the scene as it was", {s.box_a, s.box_b} <= s.node_ids())

    entries = s.right_click(on_a)
    check("1.7 with a copied item the menu offers Paste enabled", ("Paste" in entries) and (not is_disabled(entries["Paste"])))
    check("1.7b Paste names the copied box", ("Paste" in entries) and (shown(entries["Paste"]) == f'Paste "{BOX_A}"'),
          shown(entries.get("Paste", {})))
    if "Cut" not in entries:
        return
    depth = s.undo_depth()
    s.click_entry(entries, "Cut")
    ids = s.node_ids()
    check("1.8 Cut removes the hovered box alone", (s.box_a not in ids) and (s.box_b in ids))
    check("1.9 Cut is one undo entry", s.undo_depth() == depth + 1, f"{depth} -> {s.undo_depth()}")
    e.call("undo")
    s.settle()
    check("1.10 undo brings the box back", s.box_a in s.node_ids())


def check_selection(s: Session, on_a):
    print("\n== 2. A selected box, the pointer on the other box ==")
    e = s.e
    s.select([s.box_b])
    entries = s.right_click(on_a)
    check("2.1 the menu opens with a selection", "Cut" in entries)
    if "Cut" not in entries:
        return
    check("2.1b Cut names the selected box", shown(entries["Cut"]) == f'Cut "{BOX_B}"', shown(entries["Cut"]))
    s.click_entry(entries, "Cut")
    ids = s.node_ids()
    check("2.2 Cut removes the selected box, not the hovered one", (s.box_b not in ids) and (s.box_a in ids))
    e.call("undo")
    s.settle()
    check("2.3 undo brings the selected box back", s.box_b in s.node_ids())
    s.select([])


def check_empty_area(s: Session, on_nothing):
    print("\n== 3. No selection, the pointer on nothing ==")
    entries = s.right_click(on_nothing)
    check("3.1 the menu opens over the empty area", "Create" in entries, ", ".join(sorted(entries)))
    check("3.2 it offers Paste and no Cut", ("Paste" in entries) and ("Cut" not in entries))
    if "Paste" not in entries:
        return
    before = s.node_ids()
    s.click_entry(entries, "Paste")
    added = s.node_ids() - before
    check("3.3 Paste inserts the clipboard's box", len(added) == 1, f"{len(added)} new node(s)")
    parents = [s.details(node_id).get("parent") for node_id in added]
    check("3.4 the pasted box is under the scene root", parents == ["root"], str(parents))
    s.e.call("undo")
    s.settle()
    check("3.5 undo removes the pasted box", s.node_ids() == before)


def check_create(s: Session, on_a):
    print("\n== 4. Create > Xform with the pointer on a box ==")
    e = s.e
    entries = s.right_click(on_a)
    check("4.1 the menu opens", "Create" in entries)
    if "Create" not in entries:
        return
    before = s.node_ids()
    depth = s.undo_depth()
    e.call("imgui_click", {"id": entries["Create"]["id"]})
    xform = None
    for _ in range(WAIT_FRAMES):
        found = [i for i in e.call("get_imgui_items", {"label_contains": "Xform", "visible_only": True})["items"]
                 if i.get("display_label", i.get("label")) == "Xform"]
        if found:
            xform = found[0]
            break
        e.wait_frames(1)
    check("4.2 Create lists Xform", xform is not None)
    if xform is None:
        s.close_menu()
        return
    e.call("imgui_click", {"id": xform["id"]})
    s.wait_menu(False)
    s.settle()
    added = sorted(s.node_ids() - before)
    check("4.3 one node was created", len(added) == 1, str(added))
    if len(added) != 1:
        return
    details = s.details(added[0])
    position = details.get("world_position")
    parent = details.get("parent")
    # The camera looks down -z through the center of box A's +z face at (0, 0.5, 0.5).
    expected = [0.0, 0.5, 0.5]
    close = (position is not None) and all(abs(p - x) < 0.05 for p, x in zip(position, expected))
    check("4.4 the new node is at the hovered point", close, f"position {position}, expected about {expected}")
    check("4.5 the new node is a child of the hovered box", parent == BOX_A, f"parent {parent}")
    check("4.6 the creation is one undo entry", s.undo_depth() == depth + 1, f"{depth} -> {s.undo_depth()}")
    e.call("undo")
    s.settle()
    check("4.7 undo removes the new node", s.node_ids() == before)


def check_paste_at_point(s: Session, on_a):
    print("\n== 5. Paste with the pointer on a box ==")
    e = s.e
    e.call("clipboard_copy_nodes", {"scene_name": s.scene, "node_ids": [s.box_b]})
    entries = s.right_click(on_a)
    check("5.1 Paste names the clipboard's box", ("Paste" in entries) and (shown(entries["Paste"]) == f'Paste "{BOX_B}"'),
          shown(entries.get("Paste", {})))
    if "Paste" not in entries:
        return
    before = s.node_ids()
    depth = s.undo_depth()
    s.click_entry(entries, "Paste")
    added = sorted(s.node_ids() - before)
    check("5.2 one box was pasted", len(added) == 1, str(added))
    if len(added) != 1:
        return
    details = s.details(added[0])
    position = details.get("world_position")
    expected = [0.0, 0.5, 0.5]
    close = (position is not None) and all(abs(p - x) < 0.05 for p, x in zip(position, expected))
    check("5.3 the pasted box is at the hovered point", close, f"position {position}, expected about {expected}")
    check("5.4 the pasted box is a child of the hovered box", details.get("parent") == BOX_A, f"parent {details.get('parent')}")
    check("5.5 the paste is one undo entry", s.undo_depth() == depth + 1, f"{depth} -> {s.undo_depth()}")
    e.call("undo")
    s.settle()
    check("5.6 undo removes the pasted box", s.node_ids() == before)


def check_right_drag(s: Session, on_a):
    print("\n== 6. A right drag turns the camera ==")
    e = s.e
    before = s.viewport()["camera_world_from_camera"]
    e.call("mouse_drag", {"from": on_a, "to": [on_a[0] + 60, on_a[1] + 10], "button": "right", "frames": 8})
    e.wait_frames(pav.NEGATIVE_WINDOW_FRAMES)
    after = s.viewport()["camera_world_from_camera"]
    delta = max(abs(a - b) for a, b in zip(before, after))
    check("6.1 the right drag turns the camera", delta > 1.0e-4, f"camera delta {delta:.6f}")
    check("6.2 the right drag opens no menu", not s.menu_items())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=DEFAULT_EDITOR, help=f"editor executable (default {DEFAULT_EDITOR})")
    args = parser.parse_args()

    editor = Editor(args.editor)
    try:
        s = setup(editor)
        on_a = s.viewport_point(0.5, 0.5)
        on_nothing = s.viewport_point(0.5, 0.04)
        check_hovered_item(s, on_a)
        check_selection(s, on_a)
        check_empty_area(s, on_nothing)
        check_create(s, on_a)
        check_paste_at_point(s, on_a)
        check_right_drag(s, on_a)
        editor.call("close_scene", {"scene_name": s.scene})
        editor.wait_idle()
    finally:
        editor.stop()
    failed = [label for label, ok in pav.RESULTS if not ok]
    print(f"\n{len(pav.RESULTS) - len(failed)}/{len(pav.RESULTS)} checks passed")
    for label in failed:
        print(f"  FAILED: {label}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
