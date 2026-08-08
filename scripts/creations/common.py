"""Shared helpers for the MCP "creations" showcase scripts.

Each creation script drives the in-editor MCP server (headless Vulkan build
preferred, so capture_screenshot works) to build one self-contained scene:
procedural texture graphs, geometry node graphs, brush placement and direct
geometry operations. Run with:

    py -3 scripts/creations/<script>.py [--port 8080]

The editor must already be running (see AGENTS.md "In-editor MCP server").
"""

import argparse
import atexit
import json
import math
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from erhe_mcp import McpClient, wait_for_server  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_EDITOR_EXE = os.path.join(REPO_ROOT, "build_vs2026_vulkan", "src", "editor", "Release", "editor.exe")
EMPTY_COMMANDS = os.path.join("config", "editor", "commands_empty.json")


def launch_editor(editor_exe):
    """Kill any running editor and launch a fresh one WITHOUT a default scene
    (--commands config/editor/commands_empty.json), so the creation's own
    scene is the only scene (one set of viewport/shadow resources)."""
    if not os.path.isabs(editor_exe):
        editor_exe = os.path.join(REPO_ROOT, editor_exe)
    subprocess.run(["taskkill", "/IM", "editor.exe", "/F"],
                   capture_output=True, check=False)
    time.sleep(2.0)
    env = dict(os.environ)
    env["ERHE_AI_DRIVER"] = "1"
    flags = 0
    if os.name == "nt":
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NO_WINDOW
    subprocess.Popen(
        [editor_exe, "--commands", EMPTY_COMMANDS],
        cwd=REPO_ROOT, env=env, creationflags=flags,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"launched {editor_exe} (no default scene)")


class Creation:
    """Wraps an McpClient with busy-retry, scene bootstrap and camera/screenshot
    helpers used by every creation script."""

    def __init__(self, title, port=8080, wait_s=120.0, pause_s=10.0,
                 editor_exe=None, reuse=False):
        self.title = title
        if not reuse:
            launch_editor(editor_exe or DEFAULT_EDITOR_EXE)
        self.client = McpClient(port)
        wait_for_server(self.client, wait_s)
        self.scene = None
        self._material_pool = None
        # Accumulated per-scene setting overrides: set_scene_settings REPLACES
        # the whole Scene_settings object, so every send must carry the union
        # of everything set so far (ambience + wind + ...).
        self._scene_settings = {}
        # One-time pause after the first visible mesh appears, so a screen
        # video recording can be started before the scene builds up.
        self.pause_s = pause_s
        self._record_pause_done = False
        # Per-tool call telemetry: tool -> [count, total_ms]; a summary is
        # printed at exit so the skill's runtime-budget numbers stay measured
        # instead of folklore.
        self._call_stats = {}
        atexit.register(self._print_call_stats)

    def _record_pause(self):
        if self._record_pause_done or self.pause_s <= 0:
            return
        self._record_pause_done = True
        print(f"First visible mesh placed - pausing {self.pause_s:.0f} s "
              "(start screen recording now)...", flush=True)
        time.sleep(self.pause_s)

    # ------------------------------------------------------------ transport

    def _timed(self, tool, args):
        started = time.perf_counter()
        try:
            return self.client.call(tool, args)
        finally:
            stat = self._call_stats.setdefault(tool, [0, 0.0])
            stat[0] += 1
            stat[1] += (time.perf_counter() - started) * 1000.0

    def _print_call_stats(self):
        if not self._call_stats:
            return
        total_calls = sum(s[0] for s in self._call_stats.values())
        total_ms = sum(s[1] for s in self._call_stats.values())
        print(f"MCP telemetry: {total_calls} calls, {total_ms / 1000.0:.1f} s total")
        by_cost = sorted(self._call_stats.items(), key=lambda kv: -kv[1][1])
        for tool, (count, ms) in by_cost[:12]:
            print(f"  {tool:32s} {count:5d} calls {ms / 1000.0:8.1f} s")

    def call(self, tool, args=None, deadline_s=600.0):
        """Query-style call: retry freely while the server is busy."""
        deadline = time.time() + deadline_s
        pause = 0.25
        while True:
            try:
                return self._timed(tool, args)
            except RuntimeError as error:
                if not self._busy(error) or time.time() > deadline:
                    raise
                time.sleep(pause)
                pause = min(pause * 1.6, 2.0)

    def mutate(self, tool, args=None, deadline_s=600.0):
        """Mutation: issue ONCE; on a server-side timeout poll until the server
        drains, then treat the mutation as applied (never re-issue)."""
        try:
            return self._timed(tool, args)
        except RuntimeError as error:
            if not self._busy(error):
                raise
            deadline = time.time() + deadline_s
            pause = 0.25
            while True:
                try:
                    self._timed("get_undo_redo_stack", None)
                    return None
                except RuntimeError as poll_error:
                    if not self._busy(poll_error) or time.time() > deadline:
                        raise
                    time.sleep(pause)
                    pause = min(pause * 1.6, 2.0)

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
            time.sleep(0.1)
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
            time.sleep(0.2)
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
        rotation = look_at_quaternion(eye, target, up)
        self.set_node_transform(node["id"], translation=eye, rotation_xyzw=rotation)

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

    def screenshot_views(self, base_path, views):
        """Screenshot the scene from several angles in one run: views is a
        list of (suffix, eye, target). Composition problems (occlusion, a
        buried face, a floating prop) get caught in ONE iteration instead of
        surfacing one per rerun."""
        root, ext = os.path.splitext(base_path)
        for suffix, eye, target in views:
            self.place_camera(eye, target)
            self.screenshot(f"{root}_{suffix}{ext or '.png'}")

    def save(self, path):
        parent = os.path.dirname(path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        self.mutate("save_scene", {"scene_name": self.scene, "path": path})
        print(f"saved scene to {path}")

    # ------------------------------------------------------------ materials

    def materials(self):
        return self.call("get_scene_materials", {"scene_name": self.scene}).get("materials", [])

    MATERIAL_POOL_EXCLUDE = {"Rendertarget Node"}

    TEXTURE_SLOTS = ("base_color", "metallic_roughness", "normal", "occlusion", "emissive")

    def make_material(self, base_name=None, clear_textures=False, **edits):
        """Claim an unused material from this scene's own stock library
        (every new scene ships with the standard metals) and edit it in
        place. Falls back to copying from another open scene when the pool
        runs dry. Returns the material's name. clear_textures=True makes the
        material plain: drops every texture assignment and resets the BxDF
        to isotropic (stock metals default to circular-brushed anisotropic,
        which draws an X/ring pattern per UV tile on faceted shapes)."""
        if self._material_pool is None:
            self._material_pool = [
                m["name"] for m in self.materials()
                if m["name"] not in self.MATERIAL_POOL_EXCLUDE]
        name = None
        if base_name is not None and base_name in self._material_pool:
            self._material_pool.remove(base_name)
            name = base_name
        elif self._material_pool:
            name = self._material_pool.pop(0)
        if name is None:
            # Pool exhausted: copy a material in from another open scene.
            for scene in self.call("list_scenes")["scenes"]:
                if scene["name"] == self.scene:
                    continue
                mats = self.call("get_scene_materials", {"scene_name": scene["name"]}).get("materials", [])
                if not mats:
                    continue
                result = self.mutate("copy_library_item", {
                    "item_name": mats[0]["name"], "item_type": "material",
                    "source_scene": scene["name"], "target_scene": self.scene,
                })
                if isinstance(result, dict) and "name" in result:
                    name = result["name"]
                    break
        if name is None:
            raise RuntimeError(
                "material pool exhausted and no other scene to copy from - "
                "reuse materials (fewer make_material calls)")
        if clear_textures:
            edits["texture_samplers"] = {slot: {"texture": None} for slot in self.TEXTURE_SLOTS}
            edits.setdefault("bxdf_model", "isotropic_brdf")
            edits.setdefault("use_circular_brushed_metal", False)
            edits.setdefault("use_aniso_control", False)
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
            time.sleep(0.1)
        self.mutate("set_node_graph_mesh", {
            "node_name": node_name, "graph_mesh": graph_mesh, "scene_name": self.scene,
        })
        self.call("get_geometry_graph")  # evaluation barrier
        self._record_pause()
        return node_name

    def set_node_transform(self, node, translation=None, rotation_xyzw=None,
                           scale=None, space="world", deadline_s=5.0):
        """Selection-free ABSOLUTE transform set: all provided components in
        ONE call, by node id (int) or name (str). A node created in the same
        editor frame attaches on the next frame, so 'Node not found' is
        retried briefly."""
        args = {"scene_name": self.scene, "space": space}
        if isinstance(node, int):
            args["node_id"] = node
        else:
            args["node_name"] = str(node)
        if translation is not None:
            args["translation"] = [float(v) for v in translation]
        if rotation_xyzw is not None:
            args["rotation_xyzw"] = [float(v) for v in rotation_xyzw]
        if scale is not None:
            args["scale"] = [float(v) for v in scale]
        deadline = time.time() + deadline_s
        while True:
            try:
                return self.mutate("set_node_transform", args)
            except RuntimeError as error:
                if ("Node not found" not in str(error)) or (time.time() > deadline):
                    raise
                time.sleep(0.05)

    def move_node_id(self, node_id, translation=None, rotation_xyzw=None, scale=None):
        self.set_node_transform(int(node_id), translation=translation,
                                rotation_xyzw=rotation_xyzw, scale=scale)

    def move_node(self, node_name, translation=None, rotation_xyzw=None, scale=None):
        self.set_node_transform(str(node_name), translation=translation,
                                rotation_xyzw=rotation_xyzw, scale=scale)

    # ----------------------------------------------------------- primitives

    def shape(self, shape, name, position, **kwargs):
        """create_shape wrapper; returns the tool's result payload.
        Pose in the SAME call: rotation_xyzw=[x,y,z,w] (world), scale=number
        (uniform brush bake - collision follows) or scale=[x,y,z] (node TRS -
        visual anisotropy, pair with motion_mode 'none'), mass=<kg> for
        dynamic parts (inertia rescales to match). One call replaces the old
        create + select + transform + deselect sequence.
        Box quirk: Create_box applies mat4_swap_xy to the generated geometry,
        so the effective world extents are (size[1], size[0], size[2]); swap
        here so callers can pass intuitive [x, y, z] extents."""
        if kwargs.get("rotation_xyzw") is None:
            kwargs.pop("rotation_xyzw", None)
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

    def _send_scene_settings(self, new_entries, ambient=None):
        """Merge new_entries into the accumulated per-scene settings and send
        the union (set_scene_settings replaces the whole object)."""
        self._scene_settings.update(new_entries)
        args = {"scene_name": self.scene}
        if ambient is not None:
            args["ambient_light"] = [float(v) for v in ambient]
        if self._scene_settings:
            args["settings"] = self._scene_settings
        self.mutate("set_scene_settings", args)

    def ambience(self, ambient=None, clear_color=None, grid=None, sky=None):
        """Scene mood knobs. grid / sky accept a bool (visibility / enable)
        or a full Grid_config / Sky_config dict override."""
        settings = {}
        if clear_color is not None:
            settings["clear_color"] = [float(v) for v in clear_color]
        if grid is not None:
            settings["grid"] = grid if isinstance(grid, dict) else {"_version": 3, "visible": bool(grid)}
        if sky is not None:
            # "_version" matters: sky "enabled" is an added_in=2 field, and a
            # versionless JSON object parses as version 1 (the field is dropped).
            settings["sky"] = sky if isinstance(sky, dict) else {"_version": 3, "enabled": bool(sky)}
        self._send_scene_settings(settings, ambient=ambient)

    def exposure(self, value):
        cameras = self.call("get_scene_cameras", {"scene_name": self.scene}).get("cameras", [])
        if cameras:
            self.mutate("edit_camera", {
                "scene_name": self.scene, "camera_id": cameras[0]["id"],
                "exposure": float(value)})

    def shadow_range(self, value):
        """Camera shadow range / far distance: raise it early when a scene
        is larger than the default range, so distant objects are shadowed
        (and stay shadowed while a windowed build is being watched)."""
        cameras = self.call("get_scene_cameras", {"scene_name": self.scene}).get("cameras", [])
        if cameras:
            self.mutate("edit_camera", {
                "scene_name": self.scene, "camera_id": cameras[0]["id"],
                "shadow_range": float(value)})

    # -------------------------------------------------------------- physics

    def set_physics(self, enabled):
        """Set the dynamic physics simulation on/off deterministically.
        toggle_physics only flips, so toggle once and flip back if the
        reported state is not the wanted one."""
        result = self.mutate("toggle_physics")
        state = result.get("dynamic_physics_enabled") if isinstance(result, dict) else None
        if state == bool(enabled):
            return
        self.mutate("toggle_physics")

    def wake_physics(self):
        """Wake all dynamic bodies (they enter the world deactivated)."""
        self.mutate("wake_physics_bodies", {"scene_name": self.scene})

    def wind(self, enabled=True, direction=(1.0, 0.0, 0.3), speed=3.5,
             gust_amplitude=2.5, gust_frequency=0.35, turbulence=0.4,
             wavelength=10.0):
        """Per-scene wind (Physics_config v2 override). Wind applies
        force = wind_receptivity * (wind_velocity - body_velocity) to every
        dynamic body with wind_receptivity > 0, each fixed step. The
        "_version": 2 is REQUIRED - a versionless physics object parses as
        v1 and the wind fields are silently dropped."""
        self._send_scene_settings({"physics": {
            "_version": 2,
            "static_enable": True,
            "dynamic_enable": True,
            "debug_draw": False,
            "wind_enable": bool(enabled),
            "wind_direction": [float(v) for v in direction],
            "wind_speed": float(speed),
            "wind_gust_amplitude": float(gust_amplitude),
            "wind_gust_frequency": float(gust_frequency),
            "wind_turbulence": float(turbulence),
            "wind_wavelength": float(wavelength),
        }})

    def body(self, node_id, **kwargs):
        """create_physics_body on a node (typically one created with
        motion_mode="none"): shape="auto" hulls the node's own mesh. Pass
        wind_receptivity / gravity_factor / mass / damping etc. through."""
        args = {"scene_name": self.scene, "node_id": int(node_id)}
        args.update(kwargs)
        return self.mutate("create_physics_body", args)

    def joint_settings(self, name, limits, drives=None):
        """Create shared Physics_joint_settings in this scene's library."""
        args = {"scene_name": self.scene, "name": name, "limits": limits}
        if drives:
            args["drives"] = drives
        self.mutate("create_physics_joint_settings", args)
        return name

    def group(self, name, position, parent_node_id=None):
        """Empty node used as the root of one logical object (a plant, a
        prop): children are created with parent_node_id=<returned id> and
        world positions (create_shape/create_node treat position as world
        and parent with world preservation). Returns the node id."""
        args = {
            "scene_name": self.scene, "name": name,
            "position": [float(v) for v in position],
        }
        if parent_node_id is not None:
            args["parent_node_id"] = int(parent_node_id)
        result = self.mutate("create_node", args)
        if isinstance(result, dict) and "node_id" in result:
            return result["node_id"]
        node = self.node_by_name(name)
        if node is None:
            raise RuntimeError(f"group '{name}' did not appear")
        return node["id"]

    def anchor(self, name, parent_node_id, position):
        """Empty child node at a world position, used as a joint pivot."""
        result = self.mutate("create_node", {
            "scene_name": self.scene, "name": name,
            "parent_node_id": int(parent_node_id),
            "position": [float(v) for v in position],
        })
        if isinstance(result, dict) and "node_id" in result:
            return result["node_id"]
        node = self.node_by_name(name)
        if node is None:
            raise RuntimeError(f"anchor '{name}' did not appear")
        return node["id"]

    def joint(self, node_id, connected_node_id=None, settings_name=None, enable_collision=False):
        """create_physics_joint: joins the nearest self-or-ancestor rigid
        body of node_id to that of connected_node_id (or the world)."""
        args = {
            "scene_name": self.scene, "node_id": int(node_id),
            "enable_collision": bool(enable_collision),
        }
        if connected_node_id is not None:
            args["connected_node_id"] = int(connected_node_id)
        if settings_name:
            args["settings_name"] = settings_name
        return self.mutate("create_physics_joint", args)

    def strip_physics(self, node_id):
        """Remove the rigid body from a node (pure visual detail parts)."""
        self.mutate("remove_node_attachment", {
            "scene_name": self.scene, "node_id": int(node_id),
            "type": "Node_physics",
        })


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


# -------------------------------------------------------------- vector math
# Shared by every creation script - import these, never re-derive them
# (the mirrored align-axis sign alone has cost hours of debugging).

def v_add(a, b):       return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
def v_sub(a, b):       return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def v_scale(a, s):     return [a[0] * s, a[1] * s, a[2] * s]
def v_dot(a, b):       return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def v_length(a):       return math.sqrt(v_dot(a, a))
def v_norm(a):         return v_scale(a, 1.0 / (v_length(a) or 1.0))
def v_distance(a, b):  return v_length(v_sub(b, a))


def v_cross(a, b):
    return [a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]]


def v_rotate(v, axis, angle):
    """Rodrigues rotation of v about a unit axis (the L-system turtle's
    workhorse)."""
    c, s = math.cos(angle), math.sin(angle)
    kv = v_cross(axis, v)
    kkv = v_cross(axis, kv)
    return [v[i] + s * kv[i] + (1.0 - c) * kkv[i] for i in range(3)]


def align_y_quaternion(direction):
    """Quaternion [x,y,z,w] rotating +Y onto direction; None when direction
    is already +Y (no rotation needed). The axis MUST be cross(+Y, d) =
    (d.z, 0, -d.x) - the mirrored sign tilts every chained segment opposite
    its chain step (gapped 'dashed' trunks)."""
    d = v_norm(direction)
    c = max(-1.0, min(1.0, d[1]))
    if c > 0.99999:
        return None
    if c < -0.99999:
        return [1.0, 0.0, 0.0, 0.0]
    axis = v_norm([d[2], 0.0, -d[0]])
    half = math.acos(c) / 2.0
    s = math.sin(half)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half)]


# ------------------------------------------------------------ pose probing

def rest_rotation(creation, node_name):
    details = creation.call("get_node_details", {"scene_name": creation.scene, "node_name": node_name})
    return details["world_transform"]["rotation_xyzw"]


def body_axis_elevation(q):
    """Elevation (deg) of a node's +Y axis above the horizontal. Yaw-
    insensitive on purpose: a shoved creature may legitimately re-plant
    facing a new heading, and that is still a recovery."""
    qx, qy, qz, qw = q
    axis_y = 1.0 - 2.0 * (qx * qx + qz * qz)  # y component of the rotated +Y axis
    return math.degrees(math.asin(max(-1.0, min(1.0, axis_y))))


def probe_tilt(creation, node_names, seconds=6.0, interval=0.25):
    """Sample world tilt (deg from upright) of the named nodes and print a
    small table - numeric proof that wind/physics moves them. Roughness
    (mean |second difference|) exposes high-frequency ringing the range
    alone hides: smooth sway ~ a fraction of a degree, jitter >> 1. A
    monotonic ramp that parks at the angular limit means receptivity is
    too high for the drive stiffness."""
    steps = max(1, int(seconds / interval))
    series = {name: [] for name in node_names}
    for _ in range(steps):
        time.sleep(interval)
        for name in node_names:
            details = creation.call("get_node_details", {"scene_name": creation.scene, "node_name": name})
            qx, qy, qz, qw = details["world_transform"]["rotation_xyzw"]
            y_up = 1.0 - 2.0 * (qx * qx + qz * qz)
            series[name].append(math.degrees(math.acos(max(-1.0, min(1.0, y_up)))))
    for name, tilts in series.items():
        lo, hi = min(tilts), max(tilts)
        roughness = 0.0
        if len(tilts) > 2:
            roughness = sum(abs(tilts[i + 1] - 2.0 * tilts[i] + tilts[i - 1])
                            for i in range(1, len(tilts) - 1)) / (len(tilts) - 2)
        print(f"sway {name}: {' '.join(f'{t:5.1f}' for t in tilts)}  "
              f"(range {hi - lo:.1f} deg, roughness {roughness:.2f})")
    return series


def probe_pose(creation, node_name, rest_elevation, label, seconds=6.0, interval=0.5):
    """Sample a body node's height and its pitch/roll drift from the rest
    pose (see body_axis_elevation). 'Tilt from world up' is useless for
    capsules rotated off +Y - it reads 90 deg forever. Returns (heights,
    leans, last world position) - the position matters because a shove can
    slide a creature 1-2 m and the aftermath camera must re-frame on it."""
    steps = max(1, int(seconds / interval))
    heights, leans = [], []
    position = None
    for _ in range(steps):
        time.sleep(interval)
        details = creation.call("get_node_details", {"scene_name": creation.scene, "node_name": node_name})
        position = details["world_transform"]["translation"]
        heights.append(position[1])
        elevation = body_axis_elevation(details["world_transform"]["rotation_xyzw"])
        leans.append(abs(elevation - rest_elevation))
    print(f"{label} height: {' '.join(f'{h:5.3f}' for h in heights)}")
    print(f"{label} lean:   {' '.join(f'{t:5.1f}' for t in leans)}")
    return heights, leans, position


def hierarchy_report(creation, label="hierarchy"):
    """Print root count + depth histogram (the skill's mandatory hierarchy
    verification). Roots should be ~one per logical object + camera/floor/
    lights. Returns (root_count, depth_histogram)."""
    nodes = creation.nodes()
    by_id = {n["id"]: n for n in nodes if "id" in n}

    def depth_of(node):
        depth = 0
        seen = set()
        while True:
            parent = node.get("parent_id")
            if parent is None or parent not in by_id or parent in seen:
                return depth
            seen.add(parent)
            node = by_id[parent]
            depth += 1

    histogram = {}
    roots = 0
    for node in nodes:
        depth = depth_of(node)
        histogram[depth] = histogram.get(depth, 0) + 1
        if depth == 0:
            roots += 1
    shape = " ".join(f"d{d}:{histogram[d]}" for d in sorted(histogram))
    print(f"{label}: {len(nodes)} nodes, {roots} roots, {shape}")
    return roots, histogram


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


def axis_angle_quaternion(axis, angle_rad):
    """Quaternion [x,y,z,w] rotating angle_rad around a (unit) axis."""
    ax, ay, az = axis
    length = math.sqrt(ax * ax + ay * ay + az * az) or 1.0
    s = math.sin(angle_rad / 2.0)
    return [ax / length * s, ay / length * s, az / length * s, math.cos(angle_rad / 2.0)]


def quat_mul(a, b):
    """Hamilton product a*b for [x,y,z,w] quaternions (applies b, then a)."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ]


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
    parser.add_argument("--editor-exe", default=DEFAULT_EDITOR_EXE,
                        help="editor executable to launch (default: windowed "
                             "Vulkan Release; pass the headless build for "
                             "screenshot runs)")
    parser.add_argument("--reuse", action="store_true",
                        help="use the already-running editor instead of "
                             "launching a fresh one without a default scene")
    return parser.parse_args()
