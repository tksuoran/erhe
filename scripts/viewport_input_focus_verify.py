#!/usr/bin/env python3
"""Viewport input focus: an ImGui interaction that started outside a viewport keeps the input until it ends.

The desktop ImGui host hands mouse and keyboard events to erhe::commands (the
viewport tools) only while a viewport window requests input. A viewport
requests input while the pointer is over it, unless Dear ImGui is in the
middle of an interaction owned by another window (an active item such as a
drag field or a text field, or a mouse button pressed elsewhere). See
doc/erhe/imgui.md "Input routing" and doc/editor/windows.md.

Checks, each driven as a user does through the MCP input gestures:

1. Idle: the mouse wheel over the viewport zooms the camera.
2. A Properties drag field held and dragged over the viewport: the wheel
   there does not reach the camera. After the release, it does again.
3. The Properties Name text field active, the pointer over the viewport:
   typed w/a/s/d edit the name and W held does not move the camera. After
   Enter commits the name, W held over the viewport moves the camera.
4. The Hierarchy filter text field active, the pointer over the viewport:
   Delete edits the text, not the scene (the selected box survives). After
   Escape ends the text edit, Delete over the viewport deletes the box.
5. A right-button camera turn started in the viewport keeps turning the
   camera while the held pointer moves over the Properties window (pointer
   capture).
6. A brush dragged from the Hierarchy window and dropped on the box: the
   drop preview follows the hover (the brush lands on the hovered face, one
   instance) while the drag owns the input (the camera does not move).

    py -3 scripts/viewport_input_focus_verify.py [--port N] [--launch]

--launch starts build_vs2026_vulkan_headless/bin/Debug/editor.exe (or
--editor), reads its MCP port from logs/log.txt and asks it to exit at the
end. One PASS/FAIL line per check, exit code 1 when any check fails.
"""

import argparse
import sys

from erhe_mcp import DEFAULT_PORT, McpClient, check_true, report, wait_for_server
from ik_interactive_pass_verify import DEFAULT_EDITOR, Editor, ensure_group_open, ensure_row_visible, launch_editor

BOX = "Focus Box"
DROP_BRUSH = "Drop Brush"


def matrix_delta(a, b):
    return max(abs(x - y) for x, y in zip(a, b))


class Session:
    def __init__(self, editor: Editor, scene: str, box_id: int) -> None:
        self.e = editor
        self.scene = scene
        self.box_id = box_id

    def viewport(self):
        viewports = [v for v in self.e.call("get_viewports")["viewports"] if v.get("scene") == self.scene]
        if not viewports:
            raise RuntimeError(f"no viewport shows scene {self.scene}")
        return viewports[0]

    def viewport_center(self):
        v = self.viewport()
        return [v["x"] + (v["width"] * 0.5), v["y"] + (v["height"] * 0.5)]

    def camera(self):
        return self.viewport()["camera_world_from_camera"]

    def box_exists(self):
        nodes = self.e.call("get_scene_nodes", {"scene_name": self.scene})["nodes"]
        return any(n["id"] == self.box_id for n in nodes)

    def box_name(self):
        return self.e.call("get_node_details", {"scene_name": self.scene, "node_id": self.box_id})["name"]

    def select_box(self):
        self.e.call("select_items", {"scene_name": self.scene, "ids": [self.box_id]})
        self.e.advance(4)

    def wheel(self, point):
        self.e.call("mouse_wheel", {"x": point[0], "y": point[1], "dy": 2.0})
        self.e.advance(4)

    def hierarchy_window(self):
        names = [w["name"] for w in self.e.call("get_imgui_windows")["windows"] if w["name"].startswith("Scene Hierarchy")]
        if not names:
            raise RuntimeError("no Scene Hierarchy window")
        return names[-1]


def setup(e: Editor) -> Session:
    print("\n== Setup ==")
    e.call("reset_editor_state")
    before = {s["name"] for s in e.call("list_scenes")["scenes"]}
    scene = e.create_scene()
    check_true("setup: a new scene", scene not in before, scene)
    # No rigid body: a right press on a dynamic body would start the physics drag, not the camera turn.
    created = e.call("create_shape", {"scene_name": scene, "shape": "box", "size": [1.0, 1.0, 1.0], "name": BOX,
                                      "position": [0.0, 0.5, 0.0], "motion_mode": "none"})
    e.advance(6)
    s = Session(e, scene, created["node_id"])
    check_true("setup: box created", s.box_exists())
    return s


def check_idle_wheel(s: Session):
    print("\n== 1. Idle wheel over the viewport ==")
    before = s.camera()
    s.wheel(s.viewport_center())
    delta = matrix_delta(before, s.camera())
    check_true("1.1 the wheel over an idle viewport zooms the camera", delta > 1.0e-4, f"camera delta {delta:.6f}")


def check_drag_field(s: Session):
    print("\n== 2. Properties drag field dragged over the viewport ==")
    e = s.e
    s.select_box()
    ensure_group_open(e, "Local Transform")
    item = ensure_row_visible(e, "Translation.x")
    if item is None:
        check_true("2.0 Properties shows Translation.x for the selected box", False)
        return
    start = [item["center_x"], item["center_y"]]
    center = s.viewport_center()
    e.call("mouse_drag", {"from": start, "to": center, "frames": 12, "hold": True})
    e.advance(4)
    active = e.find_item_any("Properties", "Translation.x")
    check_true("2.1 the drag field stays active over the viewport",
               (active is not None) and active["status"].get("active", False))
    before = s.camera()
    s.wheel(center)
    delta = matrix_delta(before, s.camera())
    check_true("2.2 the wheel over the viewport during the drag does not reach the camera", delta <= 1.0e-6,
               f"camera delta {delta:.6f}")
    e.call("mouse_release", {"button": "left"})
    e.advance(4)
    before = s.camera()
    s.wheel(center)
    delta = matrix_delta(before, s.camera())
    check_true("2.3 after the release the wheel over the viewport zooms the camera", delta > 1.0e-4,
               f"camera delta {delta:.6f}")
    e.call("undo")
    e.advance(4)


def check_name_field(s: Session):
    print("\n== 3. Properties Name text field active, typing over the viewport ==")
    e = s.e
    s.select_box()
    name_row = ensure_row_visible(e, "Name")
    if name_row is None:
        check_true("3.0 Properties shows the Name row of the selected box", False)
        return
    old_name = s.box_name()
    e.call("imgui_click", {"window": "Properties", "id": name_row["id"]})
    e.advance(3)
    e.key("a", ["ctrl"])  # still over the field: select the whole name
    center = s.viewport_center()
    e.move(center[0], center[1])
    e.advance(4)
    active = e.find_item_any("Properties", "Name")
    check_true("3.1 the Name field is active with the pointer over the viewport",
               (active is not None) and active["status"].get("active", False))
    before = s.camera()
    for key in ("w", "a", "s", "d"):
        e.call("key_press", {"key": key, "hold_frames": 8})
        e.advance(2)
    e.call("type_text", {"text": "wasd"})
    e.advance(4)
    delta = matrix_delta(before, s.camera())
    check_true("3.2 W/A/S/D typed into the Name field do not move the camera", delta <= 1.0e-6,
               f"camera delta {delta:.6f}")
    e.key("enter")
    e.advance(4)
    new_name = s.box_name()
    check_true("3.3 Enter commits the typed name", new_name == "wasd", f"'{old_name}' -> '{new_name}'")
    e.move(center[0], center[1])
    e.advance(4)
    before = s.camera()
    e.call("key_press", {"key": "w", "hold_frames": 8})
    e.advance(4)
    delta = matrix_delta(before, s.camera())
    check_true("3.4 after the commit W held over the viewport moves the camera", delta > 1.0e-4,
               f"camera delta {delta:.6f}")


def check_text_field(s: Session):
    print("\n== 4. Hierarchy filter text field active, pointer over the viewport ==")
    e = s.e
    s.select_box()
    hierarchy = s.hierarchy_window()
    e.click(hierarchy, "##Filter")
    center = s.viewport_center()
    e.move(center[0], center[1])
    e.advance(4)
    e.key("delete")
    survived = s.box_exists()
    check_true("4.1 Delete while the text field is active does not delete the selected box", survived)
    if not survived:
        e.call("undo")
        e.advance(4)
        s.select_box()
    e.key("escape")
    field = e.find_item_any(hierarchy, "##Filter")
    check_true("4.2 Escape ends the text edit", (field is not None) and not field["status"].get("active", False))
    e.move(center[0], center[1])
    e.advance(4)
    s.select_box()
    e.key("delete")
    deleted = not s.box_exists()
    check_true("4.3 after Escape ends the text edit, Delete over the viewport deletes the box", deleted)
    if deleted:
        e.call("undo")
        e.advance(4)
    check_true("4.4 the box is back after undo", s.box_exists())


def check_pointer_capture(s: Session):
    print("\n== 5. Camera turn started in the viewport, continued over Properties ==")
    e = s.e
    start_point = s.viewport_center()
    properties = [w for w in e.call("get_imgui_windows")["windows"] if w["name"] == "Properties"]
    if not properties:
        check_true("5.0 a Properties window", False)
        return
    p = properties[0]
    over = [p["x"] + (p["width"] * 0.5), p["y"] + (p["height"] * 0.5)]
    start = s.camera()
    e.call("mouse_drag", {"from": start_point, "to": over, "button": "right", "frames": 12, "hold": True})
    e.advance(2)
    before = s.camera()
    drag_delta = matrix_delta(start, before)
    e.move(over[0], over[1] + 40.0)
    e.advance(4)
    delta = matrix_delta(before, s.camera())
    # Release back over the viewport: a right release over Properties would
    # open its context menu.
    e.move(start_point[0], start_point[1])
    e.advance(2)
    e.call("mouse_release", {"button": "right"})
    e.advance(4)
    check_true("5.1 the held turn keeps turning the camera over another window", delta > 1.0e-4,
               f"camera delta {delta:.6f}, during the drag {drag_delta:.6f}")


def check_brush_drop(s: Session):
    print("\n== 6. Brush dragged from the Hierarchy and dropped on the box ==")
    e = s.e
    e.call("create_shape", {"scene_name": s.scene, "shape": "box", "size": [0.25, 0.25, 0.25], "name": DROP_BRUSH,
                            "add_brush": True, "instance": False})
    e.advance(4)
    hierarchy = s.hierarchy_window()
    folder = e.find_item(hierarchy, "Brushes")
    if (folder is not None) and not folder["status"].get("opened", False):
        # The tree arrow at the row's left edge opens the folder.
        e.call("mouse_click", {"x": folder["x"] + 8.0, "y": folder["center_y"]})
        e.advance(4)
    row = e.find_item(hierarchy, DROP_BRUSH)
    if row is None:
        rows = [(i.get("display_label"), i["status"].get("visible"), i["status"].get("opened")) for i in e.items(window=hierarchy, visible_only=False)]
        check_true("6.0 the Hierarchy lists the brush", False, f"{hierarchy}: {rows}")
        return
    nodes_before = {n["id"] for n in e.call("get_scene_nodes", {"scene_name": s.scene})["nodes"]}
    camera_before = s.camera()
    e.call("mouse_drag", {"from": [row["center_x"], row["center_y"]], "to": s.viewport_center(), "frames": 20})
    e.advance(6)
    nodes = e.call("get_scene_nodes", {"scene_name": s.scene})["nodes"]
    added = [n for n in nodes if (n["id"] not in nodes_before) and n["name"].startswith(DROP_BRUSH)]
    check_true("6.1 the drop over the hovered box places exactly one brush instance", len(added) == 1,
               f"added {[n['name'] for n in added]}")
    delta = matrix_delta(camera_before, s.camera())
    check_true("6.2 the drag and drop does not reach the camera", delta <= 1.0e-6, f"camera delta {delta:.6f}")


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
    session = None
    try:
        session = setup(e)
        check_idle_wheel(session)
        check_drag_field(session)
        check_name_field(session)
        check_text_field(session)
        check_pointer_capture(session)
        check_brush_drop(session)
    finally:
        if session is not None:
            e.close_scene(session.scene)
        if process is not None:
            try:
                client.call("request_exit")
                process.wait(timeout=60)
            except Exception:
                process.kill()
    return report()


if __name__ == "__main__":
    sys.exit(main())
