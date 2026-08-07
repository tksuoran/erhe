"""Shared helpers for the MCP "creations" showcase scripts.

Each creation script drives the in-editor MCP server (headless Vulkan build
preferred, so capture_screenshot works) to build one self-contained scene:
procedural texture graphs, geometry node graphs, brush placement and direct
geometry operations. Run with:

    py -3 scripts/creations/<script>.py [--port 8080]

The editor must already be running (see AGENTS.md "In-editor MCP server").
"""

import argparse
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from erhe_mcp import McpClient, wait_for_server  # noqa: E402


class Creation:
    """Wraps an McpClient with busy-retry, scene bootstrap and camera/screenshot
    helpers used by every creation script."""

    def __init__(self, title, port=8080, wait_s=120.0, pause_s=10.0):
        self.title = title
        self.client = McpClient(port)
        wait_for_server(self.client, wait_s)
        self.scene = None
        # One-time pause after the first visible mesh appears, so a screen
        # video recording can be started before the scene builds up.
        self.pause_s = pause_s
        self._record_pause_done = False

    def _record_pause(self):
        if self._record_pause_done or self.pause_s <= 0:
            return
        self._record_pause_done = True
        print(f"First visible mesh placed - pausing {self.pause_s:.0f} s "
              "(start screen recording now)...", flush=True)
        time.sleep(self.pause_s)

    # ------------------------------------------------------------ transport

    def call(self, tool, args=None, deadline_s=600.0):
        """Query-style call: retry freely while the server is busy."""
        deadline = time.time() + deadline_s
        while True:
            try:
                return self.client.call(tool, args)
            except RuntimeError as error:
                if not self._busy(error) or time.time() > deadline:
                    raise
                time.sleep(2.0)

    def mutate(self, tool, args=None, deadline_s=600.0):
        """Mutation: issue ONCE; on a server-side timeout poll until the server
        drains, then treat the mutation as applied (never re-issue)."""
        try:
            return self.client.call(tool, args)
        except RuntimeError as error:
            if not self._busy(error):
                raise
            deadline = time.time() + deadline_s
            while True:
                try:
                    self.client.call("get_undo_redo_stack")
                    return None
                except RuntimeError as poll_error:
                    if not self._busy(poll_error) or time.time() > deadline:
                        raise
                    time.sleep(2.0)

    @staticmethod
    def _busy(error):
        text = str(error)
        return ("Request timed out" in text) or ("Server busy" in text)

    # ---------------------------------------------------------------- scene

    def new_scene(self):
        """Create a fresh scene (own camera/viewport/library) and activate it."""
        before = {s["name"] for s in self.call("list_scenes")["scenes"]}
        self.mutate("create_scene")
        deadline = time.time() + 30.0
        while time.time() < deadline:
            names = [s["name"] for s in self.call("list_scenes")["scenes"]]
            fresh = [n for n in names if n not in before]
            if fresh:
                self.scene = fresh[0]
                self.mutate("set_active_scene", {"scene_name": self.scene})
                return self.scene
            time.sleep(0.25)
        raise RuntimeError("create_scene did not surface a new scene")

    def settle(self, deadline_s=600.0, extra_sleep=0.0):
        """Wait until pending + running + queued_operations == 0."""
        deadline = time.time() + deadline_s
        while time.time() < deadline:
            status = self.call("get_async_status")
            total = (status.get("pending", 0) + status.get("running", 0)
                     + status.get("queued_operations", 0))
            if total == 0:
                if extra_sleep > 0.0:
                    time.sleep(extra_sleep)
                return
            time.sleep(0.5)
        raise RuntimeError("scene did not settle in time")

    def nodes(self):
        return self.call("get_scene_nodes", {"scene_name": self.scene}).get("nodes", [])

    def node_by_name(self, name):
        for node in self.nodes():
            if node.get("name") == name:
                return node
        return None

    def select(self, ids):
        self.mutate("select_items", {
            "scene_name": self.scene,
            "ids": ids if isinstance(ids, list) else [ids],
        })

    def clear_selection(self):
        self.select([])

    # --------------------------------------------------------------- camera

    def place_camera(self, eye, target, up=(0.0, 1.0, 0.0)):
        """Move the scene's camera node to eye, looking at target."""
        cameras = self.call("get_scene_cameras", {"scene_name": self.scene}).get("cameras", [])
        if not cameras:
            raise RuntimeError("scene has no camera")
        camera_name = cameras[0].get("node") or cameras[0].get("name")
        node = self.node_by_name(camera_name)
        if node is None:
            # fall back: any node whose name matches the camera name loosely
            for candidate in self.nodes():
                if camera_name and camera_name in candidate.get("name", ""):
                    node = candidate
                    break
        if node is None:
            raise RuntimeError(f"camera node '{camera_name}' not found")
        self.select(node["id"])
        rotation = look_at_quaternion(eye, target, up)
        # transform_selection applies one component per call.
        self.mutate("transform_selection", {
            "space": "global", "translation": [float(v) for v in eye],
        })
        self.mutate("transform_selection", {
            "space": "global", "rotation_xyzw": rotation,
        })
        self.clear_selection()

    CLUTTER_WINDOWS = [
        "Animation", "Asset Browser", "Clipboard", "Commands", "Composer",
        "Create", "Depth Visualization", "Fly Camera", "Frame Log",
        "Frame Pacing", "Geometry Graph", "Geometry Graph Palette",
        "Gradient Editor", "Graph", "Graph Style", "Grid", "Headset",
        "Hotbar", "Hover Tool", "Icon Browser", "Inventory", "Layers",
        "Lightmap", "Lightmap Texture", "Log Settings", "Network",
        "Node Properties", "Operation Stack", "Operations", "Paint Tool",
        "Performance", "Physics", "Pipelines", "Post Processing",
        "Properties", "Ray Trace", "Render Graph", "Scene Hierarchy [1]",
        "Scene Hierarchy [2]", "Selection", "Settings", "Sheet", "Tail Log",
        "Texture Graph", "Texture Graph Palette", "Tool Properties",
        "Tools Library", "Transform",
    ]

    def _hide_window(self, title):
        try:
            self.mutate("set_window_visibility", {"title": title, "visible": False})
        except RuntimeError:
            pass

    def presentation(self):
        """Hide every non-viewport window and other scenes' viewports, then
        focus this creation's viewport so the capture shows only it."""
        mine = None
        for viewport in self.call("get_viewports").get("viewports", []):
            if viewport.get("scene") == self.scene:
                mine = viewport["title"]
            else:
                self._hide_window(viewport["title"])
        for title in self.CLUTTER_WINDOWS:
            self._hide_window(title)
        if mine:
            self.mutate("set_window_visibility", {"title": mine, "visible": True, "focus": True})

    def screenshot(self, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.settle()
        self.presentation()
        time.sleep(1.0)  # let a few frames render with final state
        try:
            result = self.call("capture_screenshot", {"path": path})
        except RuntimeError as error:
            # The windowed build has no frame capture; a recording session
            # should still reach save_scene.
            print(f"screenshot skipped: {error}")
            return None
        print(f"screenshot: {json.dumps(result)}")
        return result

    def save(self, path):
        parent = os.path.dirname(path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        self.mutate("save_scene", {"scene_name": self.scene, "path": path})
        print(f"saved scene to {path}")

    # ------------------------------------------------------------ materials

    def materials(self):
        return self.call("get_scene_materials", {"scene_name": self.scene}).get("materials", [])

    def make_material(self, base_name=None, **edits):
        """Copy a material from another scene's library into this scene and
        edit it (copy_library_item refuses same-scene copies). Returns the new
        material's name."""
        source_scene = None
        source = base_name
        for scene in self.call("list_scenes")["scenes"]:
            if scene["name"] == self.scene:
                continue
            mats = self.call("get_scene_materials", {"scene_name": scene["name"]}).get("materials", [])
            if not mats:
                continue
            if source is None:
                source_scene, source = scene["name"], mats[0]["name"]
                break
            if any(m["name"] == source for m in mats):
                source_scene = scene["name"]
                break
        if source_scene is None:
            raise RuntimeError("no other scene has a material to copy")
        before = {m["name"] for m in self.materials()}
        result = self.mutate("copy_library_item", {
            "item_name": source, "item_type": "material",
            "source_scene": source_scene, "target_scene": self.scene,
        })
        if isinstance(result, dict) and "name" in result:
            name = result["name"]
        else:
            after = self.materials()
            fresh = [m["name"] for m in after if m["name"] not in before]
            if not fresh:
                raise RuntimeError(f"copy_library_item produced no new material from '{source}'")
            name = fresh[0]
        if edits:
            args = {"scene_name": self.scene, "material_name": name}
            args.update(edits)
            self.mutate("edit_material", args)
        return name

    # -------------------------------------------------------- texture graph

    def texture_graph(self, name):
        self.mutate("create_graph_texture", {"name": name, "scene_name": self.scene})
        return GraphBuilder(self, "texture_graph")

    def bind_material_texture(self, material_name, graph_texture, slot="base_color"):
        self.mutate("set_material_texture_source", {
            "scene_name": self.scene, "material_name": material_name,
            "slot": slot, "graph_texture": graph_texture,
        })

    # ------------------------------------------------------- geometry graph

    def geometry_graph(self, name):
        self.mutate("create_graph_mesh", {"name": name, "scene_name": self.scene})
        return GraphBuilder(self, "geometry_graph")

    def bind_node_mesh(self, node_name, graph_mesh):
        """Create a scene node bound to a Graph Mesh asset; returns node name."""
        self.mutate("create_node", {"name": node_name, "scene_name": self.scene})
        deadline = time.time() + 20.0
        while self.node_by_name(node_name) is None:
            if time.time() > deadline:
                raise RuntimeError(f"create_node '{node_name}' did not appear")
            time.sleep(0.25)
        self.mutate("set_node_graph_mesh", {
            "node_name": node_name, "graph_mesh": graph_mesh, "scene_name": self.scene,
        })
        self.call("get_geometry_graph")  # evaluation barrier
        self._record_pause()
        return node_name

    def move_node_id(self, node_id, translation=None, rotation_xyzw=None, scale=None):
        self.select(node_id)
        if translation is not None:
            self.mutate("transform_selection", {
                "space": "global", "translation": [float(v) for v in translation]})
        if rotation_xyzw is not None:
            self.mutate("transform_selection", {
                "space": "global", "rotation_xyzw": [float(v) for v in rotation_xyzw]})
        if scale is not None:
            self.mutate("transform_selection", {
                "space": "global", "scale": [float(v) for v in scale]})
        self.clear_selection()

    def move_node(self, node_name, translation=None, rotation_xyzw=None, scale=None):
        node = self.node_by_name(node_name)
        if node is None:
            raise RuntimeError(f"node '{node_name}' not found")
        self.select(node["id"])
        # transform_selection applies one component per call.
        if translation is not None:
            self.mutate("transform_selection", {
                "space": "global", "translation": [float(v) for v in translation]})
        if rotation_xyzw is not None:
            self.mutate("transform_selection", {
                "space": "global", "rotation_xyzw": [float(v) for v in rotation_xyzw]})
        if scale is not None:
            self.mutate("transform_selection", {
                "space": "global", "scale": [float(v) for v in scale]})
        self.clear_selection()

    # ----------------------------------------------------------- primitives

    def shape(self, shape, name, position, **kwargs):
        """create_shape wrapper; returns the tool's result payload.
        Box quirk: Create_box applies mat4_swap_xy to the generated geometry,
        so the effective world extents are (size[1], size[0], size[2]); swap
        here so callers can pass intuitive [x, y, z] extents."""
        if shape == "box" and "size" in kwargs and kwargs["size"] is not None:
            sx, sy, sz = kwargs["size"]
            kwargs["size"] = [sy, sx, sz]
        args = {
            "scene_name": self.scene, "shape": shape, "name": name,
            "position": [float(v) for v in position],
            "motion_mode": kwargs.pop("motion_mode", "static"),
        }
        args.update({k: v for k, v in kwargs.items() if v is not None})
        result = self.mutate("create_shape", args)
        if kwargs.get("instance", True) is not False:
            self._record_pause()
        return result

    def light(self, light_type, name, position, color, intensity, **kwargs):
        args = {
            "scene_name": self.scene, "type": light_type, "name": name,
            "position": [float(v) for v in position],
            "color": [float(v) for v in color], "intensity": float(intensity),
        }
        args.update(kwargs)
        return self.mutate("create_light", args)

    def brushes(self):
        return self.call("get_scene_brushes", {"scene_name": self.scene}).get("brushes", [])

    def brush_id(self, name_fragment):
        for brush in self.brushes():
            if name_fragment.lower() in brush.get("name", "").lower():
                return brush["id"]
        return None

    def place(self, brush_id, position, material_name=None, scale=1.0, motion_mode="static"):
        args = {
            "scene_name": self.scene, "brush_id": brush_id,
            "position": [float(v) for v in position],
            "scale": float(scale), "motion_mode": motion_mode,
        }
        if material_name:
            args["material_name"] = material_name
        result = self.mutate("place_brush", args)
        self._record_pause()
        return result

    def ambience(self, ambient=None, clear_color=None, grid=None):
        args = {"scene_name": self.scene}
        if ambient is not None:
            args["ambient_light"] = [float(v) for v in ambient]
        settings = {}
        if clear_color is not None:
            settings["clear_color"] = [float(v) for v in clear_color]
        if grid is not None:
            settings["grid"] = bool(grid)
        if settings:
            args["settings"] = settings
        self.mutate("set_scene_settings", args)

    def exposure(self, value):
        cameras = self.call("get_scene_cameras", {"scene_name": self.scene}).get("cameras", [])
        if cameras:
            self.mutate("edit_camera", {
                "scene_name": self.scene, "camera_id": cameras[0]["id"],
                "exposure": float(value)})


class GraphBuilder:
    """Thin wrapper over the texture_graph_* / geometry_graph_* tool families.
    The tools target the most recently created asset."""

    def __init__(self, creation, prefix):
        self.c = creation
        self.p = prefix

    def add(self, type_name, params=None):
        result = self.c.call(f"{self.p}_add_node", {"type": type_name})
        node_id = result.get("id") if isinstance(result, dict) else None
        if node_id is None:
            raise RuntimeError(f"{self.p}_add_node({type_name}) returned {result}")
        if params:
            self.set(node_id, params)
        return node_id

    def set(self, node_id, params):
        self.c.mutate(f"{self.p}_set_parameter", {"node_id": node_id, "parameters": params})

    def link(self, src, dst, src_slot=0, dst_slot=0):
        self.c.mutate(f"{self.p}_connect", {
            "source_node_id": src, "source_slot": src_slot,
            "sink_node_id": dst, "sink_slot": dst_slot,
        })

    def chain(self, node_ids):
        for a, b in zip(node_ids, node_ids[1:]):
            self.link(a, b)


def look_at_quaternion(eye, target, up=(0.0, 1.0, 0.0)):
    """Rotation quaternion [x,y,z,w] for a camera at eye looking at target
    (camera forward is -Z, erhe/glTF convention)."""
    fx, fy, fz = (target[0] - eye[0], target[1] - eye[1], target[2] - eye[2])
    fl = math.sqrt(fx * fx + fy * fy + fz * fz) or 1.0
    fx, fy, fz = fx / fl, fy / fl, fz / fl
    zx, zy, zz = -fx, -fy, -fz                       # camera +Z is backward
    ux, uy, uz = up
    xx = uy * zz - uz * zy
    xy = uz * zx - ux * zz
    xz = ux * zy - uy * zx
    xl = math.sqrt(xx * xx + xy * xy + xz * xz) or 1.0
    xx, xy, xz = xx / xl, xy / xl, xz / xl
    yx = zy * xz - zz * xy
    yy = zz * xx - zx * xz
    yz = zx * xy - zy * xx
    # column-major basis -> quaternion
    trace = xx + yy + zz
    if trace > 0.0:
        s = math.sqrt(trace + 1.0) * 2.0
        return [(yz - zy) / s, (zx - xz) / s, (xy - yx) / s, 0.25 * s]
    if xx > yy and xx > zz:
        s = math.sqrt(1.0 + xx - yy - zz) * 2.0
        return [0.25 * s, (yx + xy) / s, (zx + xz) / s, (yz - zy) / s]
    if yy > zz:
        s = math.sqrt(1.0 + yy - xx - zz) * 2.0
        return [(yx + xy) / s, 0.25 * s, (zy + yz) / s, (zx - xz) / s]
    s = math.sqrt(1.0 + zz - xx - yy) * 2.0
    return [(zx + xz) / s, (zy + yz) / s, 0.25 * s, (xy - yx) / s]


def hsv_to_rgb(h, s, v):
    """h in [0,1). Returns linear-ish RGB triple."""
    i = int(h * 6.0) % 6
    f = h * 6.0 - int(h * 6.0)
    p = v * (1.0 - s)
    q = v * (1.0 - f * s)
    t = v * (1.0 - (1.0 - f) * s)
    return [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i]


def standard_args(description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--no-save", action="store_true", help="skip save_scene")
    parser.add_argument("--pause", type=float, default=10.0,
                        help="seconds to pause after the first visible mesh, "
                             "for starting a screen recording (0 disables)")
    return parser.parse_args()
