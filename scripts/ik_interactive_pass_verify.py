#!/usr/bin/env python3
"""The rigging interactive test pass (doc/plans/rigging/interactive_test_pass.md), automated.

The gestures a check is about are the user's own, injected over the editor's
MCP server (doc/agents/mcp_ui_driving.md): clicks on Properties / Transform
window rows, Ctrl+Z / Ctrl+Y key chords, the Hierarchy context menu, and
mouse drags on the gizmo handles where the input path is what is checked.
Every other drag runs the Transform tool's own drag over MCP
(drag_selection, held and retargeted per step) - the same drag a handle
press starts, with exact world deltas. Either way the rig is read after
every step of a drag, so "smooth, no snap, no oscillation" is a measured
property of the samples. Explicit-argument tools otherwise only set up
state and read results back. The pass document's "Automated run" section
states the measures.

    py -3 scripts/ik_interactive_pass_verify.py [--port N] [--launch] [--section 3 --section 4 ...]

--launch starts build_vs2026_vulkan_headless/bin/Debug/editor.exe (or
--editor), reads its MCP port from logs/log.txt and asks it to exit at the
end. Without it the script drives an already running editor. One PASS/FAIL
line per check, exit code 1 when any check fails. Behaviour the checks
measure but a person chooses is printed as DECISION lines; a check that
cannot run (no Pillow for the screenshot analysis) becomes a MANUAL line.
"""

import argparse
import math
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time

from erhe_mcp import DEFAULT_PORT, McpClient, check_true, report, wait_for_server

REPO_ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSET_PATH = "res/editor/assets/skin_test/skin_test_3_boxes.glb"
USD_PATH   = "src/erhe/usd/test/data/cube.usda"
LOG_PATH   = os.path.join(REPO_ROOT, "logs", "log.txt")
DEFAULT_EDITOR = os.path.join("build_vs2026_vulkan_headless", "bin", "Debug", "editor.exe")
SHOT_DIR = os.path.join(REPO_ROOT, "logs", "ik_interactive_pass")  # screenshots the checks analyze

BONES = ["bone_0", "bone_1", "bone_2"]
TIP   = "bone_2 tip"

# Tolerances.
LOCKED_DEG        = 0.5    # a locked axis moves at most this much ("a fraction of a degree")
MOVED_DEG         = 2.0    # an unlocked axis that should move moves at least this much
LIMIT_TOL_DEG     = 0.5    # a limit holds to within this
POSITION_TOL      = 2.0e-3 # world units
# X-ray legibility: RGB distance (0..441) between the drawn line and the mesh
# pixel behind it. 441 is black vs white; a cyan line over a mid-grey surface
# is about 150.
XRAY_MEDIAN_DISTANCE = 100.0
XRAY_LEAST_DISTANCE  = 60.0

MANUAL: list[str] = []
# Behaviour choices the checks measure but cannot decide: printed with the facts.
DECISIONS: list[str] = []


# --- vector / quaternion helpers (no numpy dependency) --------------------

def sub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def add(a, b):
    return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]


def scale(a, s):
    return [a[0] * s, a[1] * s, a[2] * s]


def dot(a, b):
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2])


def cross(a, b):
    return [(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])]


def length(a):
    return math.sqrt(dot(a, a))


def normalize(a):
    n = length(a)
    return scale(a, 1.0 / n) if n > 1.0e-12 else [0.0, 0.0, 0.0]


def rotate_about(v, axis, angle_rad):
    c = math.cos(angle_rad)
    s = math.sin(angle_rad)
    return add(add(scale(v, c), scale(cross(axis, v), s)), scale(axis, dot(axis, v) * (1.0 - c)))


def q_axis_angle(axis, deg):
    axis = normalize(axis)
    h = math.radians(deg) / 2.0
    s = math.sin(h)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h)]


def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [
        (aw * bx) + (ax * bw) + (ay * bz) - (az * by),
        (aw * by) - (ax * bz) + (ay * bw) + (az * bx),
        (aw * bz) + (ax * by) - (ay * bx) + (az * bw),
        (aw * bw) - (ax * bx) - (ay * by) - (az * bz),
    ]


def q_conj(q):
    return [-q[0], -q[1], -q[2], q[3]]


def q_angle_deg(a, b):
    """Angle of the rotation taking a to b."""
    # atan2 form: acos of a dot product near 1 loses the small angles to rounding.
    r = q_mul(q_conj(list(a)), list(b))
    return math.degrees(2.0 * math.atan2(length(r[:3]), abs(r[3])))


def q_rotate(q, v):
    p = q_mul(q_mul(q, [v[0], v[1], v[2], 0.0]), q_conj(q))
    return p[:3]


def joint_angles(q, rest=(0.0, 0.0, 0.0, 1.0)):
    """Swing X, twist Y, swing Z of a bone rotation relative to its rest, in degrees.

    The solver's own measure (src/editor/transform/ik_solver.cpp): the rest-
    relative rotation is split into swing * twist about the bone's twist axis
    (local Y for these bones) and a swing axis angle is 2 asin of its swing
    quaternion component, so a limit of 45 degrees reads exactly 45 here.
    """
    rel = q_mul(q_conj(list(rest)), list(q))
    x, y, z, w = rel
    n = math.sqrt((w * w) + (y * y))
    twist = [0.0, 0.0, 0.0, 1.0] if n < 1.0e-9 else [0.0, y / n, 0.0, w / n]
    swing = q_mul(rel, q_conj(twist))
    if swing[3] < 0.0:
        swing = [-c for c in swing]
    twist_deg = math.degrees(2.0 * math.atan2(twist[1], twist[3]))
    if twist_deg > 180.0:
        twist_deg -= 360.0
    if twist_deg < -180.0:
        twist_deg += 360.0
    clamp = lambda v: max(-1.0, min(1.0, v))
    return {
        "x": math.degrees(2.0 * math.asin(clamp(swing[0]))),
        "y": twist_deg,
        "z": math.degrees(2.0 * math.asin(clamp(swing[2]))),
    }


def fmt3(v):
    return "(" + ", ".join(f"{c:+.3f}" for c in v) + ")"


# --- editor driving -------------------------------------------------------

class Editor:
    def __init__(self, client: McpClient) -> None:
        self.c = client

    def call(self, tool, args=None):
        return self.c.call(tool, args or {})

    def advance(self, frames=4):
        for _ in range(frames):
            self.call("advance_time", {"seconds": 0.016})

    def wait_idle(self, timeout_s=180.0):
        deadline = time.monotonic() + timeout_s
        idle_reads = 0
        while time.monotonic() < deadline:
            self.advance(2)
            s = self.call("get_async_status")
            idle = all(s.get(k, 1) == 0 for k in ("pending", "running", "queued_operations", "pending_scene_commits", "asset_loads"))
            idle_reads = idle_reads + 1 if idle else 0
            if idle_reads >= 2:
                return
        raise RuntimeError("editor did not go idle")

    def undo_depth(self):
        return len(self.call("get_undo_redo_stack")["undo"])

    def scene_names(self):
        return [s["name"] for s in self.call("list_scenes")["scenes"]]

    def create_scene(self):
        before = set(self.scene_names())
        self.call("create_scene")
        for _ in range(20):
            self.advance(3)
            new = [n for n in self.scene_names() if n not in before]
            if new:
                return new[0]
        raise RuntimeError("create_scene produced no scene")

    def close_scene(self, scene):
        try:
            self.call("close_scene", {"scene_name": scene})
            self.advance(6)
        except RuntimeError as error:
            print(f"  (close_scene {scene} failed: {error})")

    # UI gestures -----------------------------------------------------------

    def items(self, window=None, label_contains=None, visible_only=True, limit=400):
        args = {"visible_only": visible_only, "limit": limit}
        if window is not None:
            args["window"] = window
        if label_contains is not None:
            args["label_contains"] = label_contains
        return self.call("get_imgui_items", args)["items"]

    @staticmethod
    def label_matches(item, label):
        # A property row holding a local value shows its name as "* <name>".
        for key in ("display_label", "label"):
            text = item.get(key)
            if (text == label) or (text == "* " + label):
                return True
        return False

    def find_item(self, window, label):
        for item in self.items(window=window):
            if self.label_matches(item, label):
                return item
        return None

    def find_item_any(self, window, label):
        """Like find_item, clipped items included (their status says whether they are visible)."""
        for item in self.items(window=window, visible_only=False):
            if self.label_matches(item, label):
                return item
        return None

    def click(self, window, label, **kwargs):
        args = {"label": label}
        if window is not None:
            args["window"] = window
        args.update(kwargs)
        result = self.call("imgui_click", args)
        self.advance(2)
        return result

    def key(self, key, modifiers=None):
        self.call("key_press", {"key": key, "modifiers": modifiers or []})
        self.advance(3)

    def press(self, x, y):
        self.call("inject_input_events", {"events": [
            {"type": "mouse_move", "x": x, "y": y, "frame": 0},
            {"type": "mouse_button", "button": "left", "pressed": True, "frame": 3},
        ]})

    def move(self, x, y):
        self.call("inject_input_events", {"events": [{"type": "mouse_move", "x": x, "y": y, "frame": 0}]})

    def release(self):
        self.call("inject_input_events", {"events": [{"type": "mouse_button", "button": "left", "pressed": False, "frame": 0}]})
        self.advance(3)


class Rig:
    """The skin_test_3_boxes rig in its own scene, seen by the scene's viewport camera."""

    def __init__(self, editor: Editor, scene: str) -> None:
        self.e = editor
        self.scene = scene
        self.viewport = None
        self.ids = {}
        self.held = None
        self.gizmo_radius = 0.0
        self.probes = []

    # Scene access ----------------------------------------------------------

    def node(self, name):
        return self.e.call("get_node_details", {"scene_name": self.scene, "node_id": self.ids[name]} if name in self.ids
                           else {"scene_name": self.scene, "node_name": name})

    def local_rotation(self, name):
        return self.node(name)["local_transform"]["rotation_xyzw"]

    def world_position(self, name):
        return self.node(name)["world_transform"]["translation"]

    def world_rotation(self, name):
        return self.node(name)["world_transform"]["rotation_xyzw"]

    def snapshot(self):
        """World positions of the chain joints + tip, local rotations of the bones and the tip."""
        state = {"positions": [], "rotations": {}, "tip_world_rotation": None}
        for name in BONES + [TIP]:
            details = self.node(name)
            state["positions"].append(details["world_transform"]["translation"])
            state["rotations"][name] = details["local_transform"]["rotation_xyzw"]
            if name == TIP:
                state["tip_world_rotation"] = details["world_transform"]["rotation_xyzw"]
        return state

    def angles(self, state, bone, rest=(0.0, 0.0, 0.0, 1.0)):
        return joint_angles(state["rotations"][bone], rest)

    def prop(self, name, prop):
        for entry in self.e.call("get_item_properties", {"item_id": self.ids[name]})["properties"]:
            if entry.get("name") == prop:
                return entry
        return None

    def set_prop(self, name, prop, value=None, reference_id=None):
        args = {"item_id": self.ids[name], "property": prop}
        if reference_id is not None:
            args["reference_id"] = reference_id
        else:
            args["value"] = value
        self.e.call("set_item_property", args)
        self.e.advance(3)

    def set_local_rotation(self, name, q):
        self.e.call("set_node_transform", {"scene_name": self.scene, "node_id": self.ids[name], "space": "local", "rotation_xyzw": q})
        self.e.advance(2)

    def select(self, name):
        self.e.call("select_items", {"scene_name": self.scene, "ids": [self.ids[name]]})
        self.e.advance(4)

    # Poses -----------------------------------------------------------------

    IK_PROPS = [
        "ik_lock", "Ik.lock_x", "Ik.lock_y", "Ik.lock_z", "Ik.limit_x", "Ik.limit_y", "Ik.limit_z",
        "Ik.limit_min", "Ik.limit_max", "Ik.rest_rotation", "Ik.pole_target", "Ik.pole_angle",
        "lock_rotation_x", "lock_rotation_y", "lock_rotation_z",
        "lock_translation_x", "lock_translation_y", "lock_translation_z",
    ]

    def clear_settings(self):
        """Every IK / channel-lock value of the bones back to its default (only local values are cleared)."""
        for bone in BONES:
            local = {
                entry["name"] for entry in self.e.call("get_item_properties", {"item_id": self.ids[bone]})["properties"]
                if entry.get("source") == "local"
            }
            for prop in self.IK_PROPS:
                if prop in local:
                    self.set_prop(bone, prop, None)

    def pose(self, bends_deg=(0.0, 0.0, 0.0)):
        """Bones rotated about their local X by the given angles (0 = straight rest pose), tip at identity."""
        for bone, deg in zip(BONES, bends_deg):
            self.set_local_rotation(bone, q_axis_angle([1.0, 0.0, 0.0], deg))
        # The tip back at bone_2's end too (a drag without IK moves it off).
        self.e.call("set_node_transform", {"scene_name": self.scene, "node_id": self.ids[TIP], "space": "local",
                                           "translation": [0.0, 1.0, 0.0], "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]})
        self.e.advance(3)

    # Camera / projection ---------------------------------------------------

    def place_camera(self, eye, target):
        forward = normalize(sub(target, eye))
        z = scale(forward, -1.0)
        x = normalize(cross([0.0, 1.0, 0.0], z))
        y = cross(z, x)
        m00, m01, m02 = x[0], y[0], z[0]
        m10, m11, m12 = x[1], y[1], z[1]
        m20, m21, m22 = x[2], y[2], z[2]
        s = math.sqrt(1.0 + m00 + m11 + m22) * 2.0
        q = [(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s]
        camera = [n for n in self.e.call("get_scene_nodes", {"scene_name": self.scene})["nodes"]
                  if (n["type"] == "Camera") and (n["parent"] == "root")][0]
        self.e.call("set_node_transform", {"scene_name": self.scene, "node_id": camera["id"], "translation": eye, "rotation_xyzw": q})
        self.e.advance(4)
        self.viewport = [v for v in self.e.call("get_viewports")["viewports"] if v["scene"] == self.scene][0]

    def project(self, p):
        v = self.viewport
        m = v["camera_world_from_camera"]
        eye = [m[12], m[13], m[14]]
        d = sub(p, eye)
        cx = dot(d, [m[0], m[1], m[2]])
        cy = dot(d, [m[4], m[5], m[6]])
        cz = dot(d, [m[8], m[9], m[10]])
        ty = math.tan(v["camera_fov_y"] / 2.0)
        tx = ty * v["width"] / v["height"]
        return [
            v["x"] + ((((cx / -cz) / tx) + 1.0) / 2.0 * v["width"]),
            v["y"] + ((1.0 - ((cy / -cz) / ty)) / 2.0 * v["height"]),
        ]

    def pixel_size(self, p):
        """World size of one window pixel at `p`: the precision a pointer-driven target has there."""
        v = self.viewport
        m = v["camera_world_from_camera"]
        cz = dot(sub(p, [m[12], m[13], m[14]]), [m[8], m[9], m[10]])
        return (2.0 * abs(cz) * math.tan(v["camera_fov_y"] / 2.0)) / v["height"]

    def viewport_center(self):
        v = self.viewport
        return [v["x"] + (v["width"] / 2.0), v["y"] + (v["height"] / 2.0)]

    # Drags -----------------------------------------------------------------
    #
    # Two ways in. The default drives the Transform tool's drag directly over
    # MCP (drag_selection): the drag starts like a press on a gizmo handle of
    # that kind - the Move / Rotate / Scale tool active, so a bone solves IK -
    # and every step is retargeted with action 'move' while held, so the rig is
    # read between steps with exact world deltas. The *_pointer variants inject
    # real mouse events on the gizmo handles instead; they cover the input path
    # itself (hover, pick, press, ray-plane drag) and are used where that path
    # is what a check is about.

    def translate_drag(self, node, deltas, sample=None, hold=False):
        """Translate `node` through world `deltas` (each relative to the drag start).

        `sample()` runs after every step while the drag is held; the list of
        its results is returned. The drag is released after the last step
        unless `hold` (end it with release_drag()).
        """
        self.select(node)
        samples = []
        for i, delta in enumerate(deltas):
            if i == 0:
                self.e.call("drag_selection", {"translation": delta, "frames": 1, "release": False})
            else:
                self.e.call("drag_selection", {"action": "move", "translation": delta})
            self.e.advance(1)
            if sample is not None:
                samples.append(sample())
        self.held = "api"
        if not hold:
            self.release_drag()
        return samples

    def rotate_drag(self, node, axis, angles_deg, sample=None):
        """Rotate `node` about world `axis` through the gizmo anchor by each of `angles_deg` (from the drag start)."""
        self.select(node)
        samples = []
        for i, deg in enumerate(angles_deg):
            if i == 0:
                self.e.call("drag_selection", {"rotation_axis": axis, "rotation_angle_deg": deg, "frames": 1, "release": False})
            else:
                self.e.call("drag_selection", {"action": "move", "rotation_axis": axis, "rotation_angle_deg": deg})
            self.e.advance(1)
            if sample is not None:
                samples.append(sample())
        self.held = "api"
        self.release_drag()
        return samples

    def release_drag(self):
        if self.held == "api":
            self.e.call("drag_selection", {"action": "release"})
            self.e.advance(3)
        elif self.held == "pointer":
            self.e.release()
        self.held = None

    def handle(self, handle_name, probe_points=None):
        args = {} if probe_points is None else {"probe_points": probe_points}
        handles = self.e.call("get_transform_handles", args)
        self.gizmo_radius = handles["gizmo_radius"]
        self.probes = handles.get("probes", [])
        for entry in handles["handles"]:
            if entry["handle"] == handle_name:
                return entry, handles["anchor"]
        raise RuntimeError(f"gizmo handle '{handle_name}' not on screen: {[h['handle'] for h in handles['handles']]}")

    def translate_drag_pointer(self, node, deltas, handle_name, sample=None, hold=False):
        """translate_drag() with real mouse events on the translate handle `handle_name`.

        The pointer goes to the projection of `grab point + delta`, which a
        translate axis / plane handle resolves back to that delta (to within a
        pixel).
        """
        self.select(node)
        entry, _ = self.handle(handle_name)
        grab = entry["world"]
        self.e.press(entry["x"], entry["y"])
        samples = []
        for delta in deltas:
            x, y = self.project(add(grab, delta))
            self.e.move(x, y)
            self.e.advance(1)  # the move takes effect in the next frame's update
            if sample is not None:
                samples.append(sample())
        self.held = "pointer"
        if not hold:
            self.release_drag()
        return samples

    def ray(self, x, y):
        """World ray (origin, direction) through window pixel (x, y)."""
        v = self.viewport
        m = v["camera_world_from_camera"]
        ty = math.tan(v["camera_fov_y"] / 2.0)
        tx = ty * v["width"] / v["height"]
        nx = (((x - v["x"]) / v["width"]) * 2.0) - 1.0
        ny = 1.0 - (((y - v["y"]) / v["height"]) * 2.0)
        d = add(add(scale([m[0], m[1], m[2]], nx * tx), scale([m[4], m[5], m[6]], ny * ty)), scale([m[8], m[9], m[10]], -1.0))
        return [m[12], m[13], m[14]], normalize(d)

    def ring_handle(self, axis):
        """The rotate handle whose ring turns about world `axis` (get_transform_handles ring_axis).

        Chosen by axis, not by name: in Euler gimbal mode handle 'Rotate X' is
        the Euler order's first ring, which for ZYX turns about Z.
        """
        handles = self.e.call("get_transform_handles")
        self.gizmo_radius = handles["gizmo_radius"]
        for entry in handles["handles"]:
            ring_axis = entry.get("ring_axis")
            if (ring_axis is not None) and (abs(dot(normalize(ring_axis), axis)) > 0.999):
                return entry["handle"], handles["anchor"]
        raise RuntimeError(f"no rotate ring about {axis} on screen")

    def rotate_drag_pointer(self, node, axis, angles_deg, sample=None):
        """rotate_drag() with real mouse events on the rotate ring about world `axis`.

        A ring is a few pixels wide and crosses the others, so the grab point
        is chosen with get_transform_handles probe_points: a pixel that picks
        this ring with its 2-pixel neighbourhood picking it too. The ring
        resolves the pointer by intersecting its ray with the ring plane, so
        the moves go to the projection of the grab point's plane hit turned
        about the anchor.
        """
        self.select(node)
        axis = normalize(axis)
        handle_name, anchor = self.ring_handle(axis)
        center = anchor["world"]
        world_axes = ([1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0])
        u = [a for a in world_axes if abs(dot(a, axis)) < 0.5]
        candidates = []
        for f in (0.7, 0.8, 0.9, 1.0, 1.1, 1.2):
            for d in range(0, 360, 10):
                t = math.radians(d)
                p = add(center, scale(add(scale(u[0], math.cos(t)), scale(u[1], math.sin(t))), self.gizmo_radius * f))
                candidates.append(self.project(p))
        points = []
        for (x, y) in candidates:
            points += [[x, y], [x - 2.0, y], [x + 2.0, y], [x, y - 2.0], [x, y + 2.0]]
        self.handle(handle_name, probe_points=points)
        grab = None
        for i in range(len(candidates)):
            if all(self.probes[(5 * i) + k]["handle"] == handle_name for k in range(5)):
                grab = candidates[i]
                break
        if grab is None:
            raise RuntimeError(f"no grab point found on ring '{handle_name}'")
        origin, direction = self.ray(grab[0], grab[1])
        t = dot(sub(center, origin), axis) / dot(direction, axis)
        arm = sub(add(origin, scale(direction, t)), center)
        self.e.press(grab[0], grab[1])
        samples = []
        for deg in angles_deg:
            x, y = self.project(add(center, rotate_about(arm, axis, math.radians(deg))))
            self.e.move(x, y)
            self.e.advance(1)  # the move takes effect in the next frame's update
            if sample is not None:
                samples.append(sample())
        self.held = "pointer"
        self.release_drag()
        return samples


def ramp(target, steps):
    """`steps` deltas evenly from the drag start to `target`."""
    return [scale(target, (i + 1) / steps) for i in range(steps)]


def segment_lengths(positions):
    return [length(sub(positions[i + 1], positions[i])) for i in range(len(positions) - 1)]


# --- launching ------------------------------------------------------------

def launch_editor(editor_exe):
    exe = editor_exe if os.path.isabs(editor_exe) else os.path.join(REPO_ROOT, editor_exe)
    if not os.path.isfile(exe):
        raise RuntimeError(f"no editor at {exe}")
    try:
        os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
        open(LOG_PATH, "w").close()
    except OSError:
        pass
    env = dict(os.environ, ERHE_AI_DRIVER="1")
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    process = subprocess.Popen([exe], cwd=REPO_ROOT, env=env, creationflags=flags,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 180.0
    pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+)")
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"editor exited with {process.returncode} during startup; see logs/log.txt")
        try:
            with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            match = pattern.search(text)
            if match and ("Main loop: completed frame 12" in text):
                return process, int(match.group(1))
        except OSError:
            pass
        time.sleep(1.0)
    process.kill()
    raise RuntimeError("editor did not become ready")


def log_size():
    try:
        return os.path.getsize(LOG_PATH)
    except OSError:
        return 0


def log_since(offset):
    try:
        with open(LOG_PATH, "rb") as handle:
            handle.seek(offset)
            return handle.read().decode("utf-8", errors="replace")
    except OSError:
        return ""


# --- section 0: setup -----------------------------------------------------

def setup(e: Editor) -> Rig:
    print("\n== 0. Setup ==")
    e.call("reset_editor_state")
    scene = e.create_scene()
    e.call("import_gltf", {"scene_name": scene, "path": ASSET_PATH})
    e.wait_idle()
    rig = Rig(e, scene)

    # Add Bone Tip Nodes, from the Hierarchy context menu of the imported root.
    root_label = os.path.basename(ASSET_PATH)
    hierarchy = [w["name"] for w in e.call("get_imgui_windows")["windows"] if w["name"].startswith("Scene Hierarchy")]
    e.click(hierarchy[0] if hierarchy else None, root_label, button="right")
    e.click(None, "Add Bone Tip Nodes")
    e.advance(4)
    # Content nodes only: each bone's visualization proxy mesh carries the bone's name too.
    nodes = {n["name"]: n for n in e.call("get_scene_nodes", {"scene_name": scene})["nodes"] if n.get("content", False)}
    for name in BONES + [TIP, "skin_test_3_boxes"]:
        if name not in nodes:
            raise RuntimeError(f"setup: node '{name}' missing after import + Add Bone Tip Nodes")
        rig.ids[name] = nodes[name]["id"]
    check_true("0.1 import + Add Bone Tip Nodes (Hierarchy context menu) adds 'bone_2 tip' under bone_2",
               nodes[TIP]["parent"] == "bone_2", f"tip parent={nodes[TIP]['parent']}")

    # High enough that every translate plane handle faces the camera.
    rig.place_camera([4.5, 5.5, 7.0], [0.0, 1.5, 0.0])

    # Move tool parameters are session state: a reused editor keeps what an
    # earlier run left, so set them rather than assume the startup values.
    bone_ik = e.find_item("Transform", "Bone IK")
    was_on = (bone_ik is not None) and bone_ik["status"].get("checked", False)
    ok = set_bone_ik(rig, True) and set_effector_orientation(rig, "Keep World")
    check_true("0.2 Move tool group in the Transform window: Bone IK on, Effector Orientation 'Keep World'", ok,
               f"Bone IK was {'on' if was_on else 'off'} before")
    return rig


def set_effector_orientation(rig: Rig, label: str):
    """Pick Move tool > Effector Orientation in the Transform window, as a user does."""
    e = rig.e
    if e.find_item("Transform", "Effector Orientation") is None:
        return False
    e.click("Transform", "Effector Orientation")
    e.click(None, label)
    return True


def set_bone_ik(rig: Rig, on: bool):
    """Tick / untick Move tool > Bone IK in the Transform window, as a user does."""
    e = rig.e
    item = e.find_item("Transform", "Bone IK")
    if item is None:
        raise RuntimeError("Transform window has no 'Bone IK' row")
    if item["status"].get("checked", False) != on:
        e.click("Transform", "Bone IK")
    item = e.find_item("Transform", "Bone IK")
    return (item is not None) and (item["status"].get("checked", False) == on)


# --- section 1: plain IK drag ---------------------------------------------

def section_1(rig: Rig):
    print("\n== 1. Plain IK drag ==")
    e = rig.e
    rig.clear_settings()
    rig.pose((0.0, 30.0, 30.0))
    start = rig.snapshot()
    rest_lengths = segment_lengths(start["positions"])
    tip0 = start["positions"][-1]

    # 1.1 a reachable target in the XY plane.
    delta = [0.5, -0.3, 0.0]
    depth = e.undo_depth()
    samples = rig.translate_drag_pointer(TIP, ramp(delta, 12), "Translate XY", sample=rig.snapshot)
    end = rig.snapshot()
    reach_error = length(sub(end["positions"][-1], add(tip0, delta)))
    worst_length = max(
        max(abs(a - b) for a, b in zip(segment_lengths(s["positions"]), rest_lengths)) for s in samples
    )
    orient_change = q_angle_deg(end["tip_world_rotation"], start["tip_world_rotation"])
    pixel = rig.pixel_size(tip0)
    check_true("1.1 the chain follows: tip on target, bone lengths fixed in every step, tip keeps world orientation",
               (reach_error < 1.5 * pixel) and (worst_length < POSITION_TOL) and (orient_change < 0.01),
               f"tip error={reach_error:.2e} (1.5 px={1.5 * pixel:.2e}) worst length error={worst_length:.2e} "
               f"tip orientation change={orient_change:.4f} deg")
    check_true("1.3 the drag is one undo step", e.undo_depth() - depth == 1, f"undo delta={e.undo_depth() - depth}")

    # 1.3 Ctrl+Z restores the whole chain, Ctrl+Y re-applies it.
    e.move(*rig.viewport_center())
    e.key("z", ["ctrl"])
    undone = rig.snapshot()
    undo_error = max(length(sub(a, b)) for a, b in zip(undone["positions"], start["positions"]))
    e.key("y", ["ctrl"])
    redone = rig.snapshot()
    redo_error = max(length(sub(a, b)) for a, b in zip(redone["positions"], end["positions"]))
    check_true("1.3 Ctrl+Z returns the whole chain in one step, Ctrl+Y restores it",
               (undo_error < 1.0e-4) and (redo_error < 1.0e-4), f"undo error={undo_error:.2e} redo error={redo_error:.2e}")

    # 1.2 far out of reach: the chain straightens toward the target without jitter.
    rig.pose((0.0, 30.0, 30.0))
    far = [4.0, -1.0, 0.0]
    samples = rig.translate_drag(TIP, ramp(far, 16), sample=rig.snapshot)
    end = samples[-1]
    root = end["positions"][0]
    aim = normalize(sub(add(tip0, far), root))
    worst_off_line = max(length(sub(sub(p, root), scale(aim, dot(sub(p, root), aim)))) for p in end["positions"])
    # Jitter: once out of reach, the tip must keep moving monotonically toward the target direction.
    tip_along = [dot(sub(s["positions"][-1], root), aim) for s in samples]
    check_true("1.2 dragged far out of reach the chain lies straight on the root-target line",
               worst_off_line < POSITION_TOL, f"worst joint distance off the line={worst_off_line:.2e}")
    back_steps = sum(1 for i in range(len(tip_along) - 1) if tip_along[i + 1] < tip_along[i] - 1.0e-4)
    check_true("1.2 no jitter: the tip never steps back while the target recedes", back_steps == 0,
               f"backward steps={back_steps} of {len(tip_along) - 1}")
    e.move(*rig.viewport_center())
    e.key("z", ["ctrl"])

    # 1.4 Bone IK off: only the dragged node moves.
    ok = set_bone_ik(rig, False)
    rig.pose((0.0, 30.0, 30.0))
    before = rig.snapshot()
    rig.translate_drag(TIP, ramp([0.4, 0.0, 0.0], 6))
    after = rig.snapshot()
    bones_moved = max(q_angle_deg(before["rotations"][b], after["rotations"][b]) for b in BONES)
    tip_moved = length(sub(after["positions"][-1], before["positions"][-1]))
    check_true("1.4 Bone IK off (Transform window checkbox): only the dragged node moves",
               ok and (bones_moved < 1.0e-3) and (tip_moved > 0.3), f"checkbox ok={ok} bones turned={bones_moved:.4f} deg tip moved={tip_moved:.3f}")
    undo_viewport(rig)
    ok = set_bone_ik(rig, True)
    check_true("1.4 Bone IK back on", ok)

    # 1.5 IK Lock on bone_1 ends the chain there: bone_0 stays.
    rig.pose((0.0, 30.0, 30.0))
    rig.set_prop("bone_1", "ik_lock", True)
    before = rig.snapshot()
    rig.translate_drag(TIP, ramp([0.3, -0.2, 0.0], 8))
    after = rig.snapshot()
    bone0 = q_angle_deg(before["rotations"]["bone_0"], after["rotations"]["bone_0"])
    bone1 = q_angle_deg(before["rotations"]["bone_1"], after["rotations"]["bone_1"])
    check_true("1.5 IK Lock on bone_1: bone_0 does not move, bone_1 does",
               (bone0 < 1.0e-3) and (bone1 > MOVED_DEG), f"bone_0 turned={bone0:.4f} deg bone_1 turned={bone1:.2f} deg")
    rig.set_prop("bone_1", "ik_lock", None)


# --- Properties window rows ------------------------------------------------

def ensure_row_visible(e: Editor, label):
    """Scroll the Properties window until the row labelled `label` is not clipped."""
    for direction in (-3.0, 3.0):
        for _ in range(12):
            item = e.find_item_any("Properties", label)
            if item is None:
                return None
            if item["status"].get("visible", False):
                return item
            e.call("imgui_scroll", {"window": "Properties", "dy": direction})
            e.advance(2)
    return None


def ensure_group_open(e: Editor, group):
    for _ in range(3):
        item = ensure_row_visible(e, group)
        if item is None:
            return False
        if item["status"].get("opened", False):
            return True
        e.call("imgui_click", {"window": "Properties", "id": item["id"]})
        e.advance(3)
    return False


# Properties rows the checks click, and the property each one writes.
ROW_PROPERTIES = {
    ("IK", "IK Lock"): "ik_lock",
    ("IK", "Lock X"): "Ik.lock_x", ("IK", "Lock Y"): "Ik.lock_y", ("IK", "Lock Z"): "Ik.lock_z",
    ("IK", "Limit X"): "Ik.limit_x", ("IK", "Limit Y"): "Ik.limit_y", ("IK", "Limit Z"): "Ik.limit_z",
}
for _axis in "XYZ":
    for _kind in ("Translation", "Rotation", "Scale"):
        ROW_PROPERTIES[("Channel Locks", f"{_kind} {_axis}")] = f"lock_{_kind.lower()}_{_axis.lower()}"

UI_RETRIES: list[str] = []


def select_for_properties(rig: Rig, node):
    """Select `node` and wait until the Properties window shows it (its header row names it)."""
    rig.select(node)
    for _ in range(10):
        labels = [i.get("display_label") or "" for i in rig.e.items(window="Properties", visible_only=False)]
        if any(label.endswith(" " + node) for label in labels):
            return True
        rig.e.advance(2)
    return False


def ui_checkbox(rig: Rig, node, group, label, want):
    """Select `node` and set the checkbox row `label` of Properties group `group` by clicking it.

    Success is read back from the property the row writes, not from the
    widget. A click that did not take is retried once and reported in the
    summary (UI_RETRIES), so a flaky widget path stays visible.
    """
    e = rig.e
    prop = ROW_PROPERTIES[(group, label)]
    wanted = "true" if want else "false"
    for attempt in range(2):
        if not select_for_properties(rig, node) or not ensure_group_open(e, group):
            return False
        if rig.prop(node, prop)["value"] == wanted:
            return True
        item = ensure_row_visible(e, label)
        if item is None:
            return False
        e.call("imgui_click", {"window": "Properties", "id": item["id"]})
        e.advance(3)
        if rig.prop(node, prop)["value"] == wanted:
            if attempt > 0:
                UI_RETRIES.append(f"{node}: '{group} > {label}' click needed a retry")
            return True
    return False


def ui_button(rig: Rig, node, group, label):
    """Click the button row `label`; success is one new undo entry (retried once, reported like ui_checkbox)."""
    e = rig.e
    for attempt in range(2):
        if not select_for_properties(rig, node) or not ensure_group_open(e, group):
            return False
        item = ensure_row_visible(e, label)
        if (item is None) or item["status"].get("disabled", False):
            return False
        depth = e.undo_depth()
        e.call("imgui_click", {"window": "Properties", "id": item["id"]})
        e.advance(3)
        if e.undo_depth() == depth + 1:
            if attempt > 0:
                UI_RETRIES.append(f"{node}: '{group} > {label}' click needed a retry")
            return True
    return False


def ui_click_row(rig: Rig, node, group, label):
    """Click the Properties item `label` (a row, or a named part of one such as 'Pole Target.clear')."""
    e = rig.e
    if not select_for_properties(rig, node) or not ensure_group_open(e, group):
        return False
    item = ensure_row_visible(e, label)
    if (item is None) or item["status"].get("disabled", False):
        return False
    e.call("imgui_click", {"window": "Properties", "id": item["id"]})
    e.advance(3)
    return True


def ui_pick_reference(rig: Rig, node, group, row, choice):
    """Open the picker of reference row `row` (its '<row>.pick' arrow) and click `choice` in the list.

    Returns the names the list offered, or None when the picker did not open
    or did not offer `choice` exactly once.
    """
    e = rig.e
    if not ui_click_row(rig, node, group, row + ".pick"):
        return None
    listed = [i for i in e.items() if (i.get("window") or "").startswith("##Popup")]
    offered = [i.get("display_label") or i.get("label") for i in listed]
    target = [i for i in listed if Editor.label_matches(i, choice)]
    if len(target) != 1:
        e.key("escape")
        return None
    e.call("imgui_click", {"window": target[0]["window"], "id": target[0]["id"]})
    e.advance(3)
    return offered


def ui_drag_field(rig: Rig, node, group, label, pixels):
    """Drag the numeric field `label` (for example 'Limit Min.x') horizontally by `pixels`."""
    e = rig.e
    if not select_for_properties(rig, node) or not ensure_group_open(e, group):
        return False
    item = ensure_row_visible(e, label)
    if item is None:
        return False
    x = item["center_x"]
    y = item["center_y"]
    e.call("mouse_drag", {"from": [x, y], "to": [x + pixels, y], "frames": 10})
    e.advance(3)
    return True


def float_list_prop(rig: Rig, node, prop):
    return [float(v) for v in rig.prop(node, prop)["value"].replace(",", " ").split()]


def limit_value(min_deg, max_deg, axis=0):
    """Ik.limit_min / Ik.limit_max values (radians, per axis) with one axis set."""
    lo = [-math.pi, -math.pi, -math.pi]
    hi = [math.pi, math.pi, math.pi]
    lo[axis] = math.radians(min_deg)
    hi[axis] = math.radians(max_deg)
    return " ".join(f"{v:.7f}" for v in lo), " ".join(f"{v:.7f}" for v in hi)


def check_limit_samples(label, xs, bound, lo, hi):
    """A limited angle over the steps of one drag: reaches `bound`, never leaves [lo, hi],
    no snap (no step far larger than the drag's typical step) and no oscillation at the limit."""
    engaged = [i for i, x in enumerate(xs) if abs(x - bound) <= 1.0]
    after = xs[engaged[0]:] if engaged else []
    at_limit = [x for x in after if abs(x - bound) <= 1.0]
    outside = max(max(xs) - hi, lo - min(xs), 0.0)
    steps = sorted(abs(xs[i + 1] - xs[i]) for i in range(len(xs) - 1))
    typical = steps[len(steps) // 2] if steps else 0.0
    largest = steps[-1] if steps else 0.0
    # The snap the pass asks about is the step onto the limit.
    into = abs(xs[engaged[0]] - xs[engaged[0] - 1]) if (engaged and (engaged[0] > 0)) else 0.0
    check_true(label + ", smoothly, steady while pulled past",
               bool(engaged) and (outside <= LIMIT_TOL_DEG) and (into <= max(3.0 * typical, 2.0))
               and ((max(at_limit) - min(at_limit)) <= 1.0 if at_limit else False),
               f"X range {min(xs):.2f} .. {max(xs):.2f}, outside the limits by {outside:.3f}, steps at the limit={len(at_limit)}, "
               f"step onto the limit={into:.2f} deg (median step {typical:.2f}, largest {largest:.2f})")


def sign_flips(values, eps=1.0e-3):
    """How often the per-step change of `values` reverses direction (oscillation measure)."""
    steps = [values[i + 1] - values[i] for i in range(len(values) - 1)]
    steps = [s for s in steps if abs(s) > eps]
    return sum(1 for i in range(len(steps) - 1) if (steps[i] > 0.0) != (steps[i + 1] > 0.0))


def undo_viewport(rig: Rig, count=1):
    """Ctrl+Z with the pointer over the viewport (keyboard commands need it there)."""
    rig.e.move(*rig.viewport_center())
    for _ in range(count):
        rig.e.key("z", ["ctrl"])


def circle(radius, steps):
    """A closed circle in the world XZ plane through the drag start."""
    return [[radius * (math.cos(t) - 1.0), 0.0, radius * math.sin(t)]
            for t in [(2.0 * math.pi * (i + 1)) / steps for i in range(steps)]]


# --- section 2: chain visualization -----------------------------------------

def load_rgb(path):
    from PIL import Image  # optional dependency; only the screenshot checks need it
    image = Image.open(path).convert("RGB")
    return image, image.load()


def color_near(pixels, size, x, y, radius, predicate):
    """True when a pixel within `radius` of window pixel (x, y) satisfies `predicate`."""
    for py in range(int(y) - radius, int(y) + radius + 1):
        for px in range(int(x) - radius, int(x) + radius + 1):
            if (0 <= px < size[0]) and (0 <= py < size[1]) and predicate(pixels[px, py]):
                return True
    return False


def line_coverage(path, rig, world_points, predicate, samples_per_segment=12, radius=3):
    """Fraction of points sampled along the projected polyline `world_points` with `predicate` colour next to them."""
    image, pixels = load_rgb(path)
    hits = 0
    total = 0
    for i in range(len(world_points) - 1):
        for k in range(samples_per_segment):
            f = (k + 0.5) / samples_per_segment
            x, y = rig.project(add(world_points[i], scale(sub(world_points[i + 1], world_points[i]), f)))
            total += 1
            if color_near(pixels, image.size, x, y, radius, predicate):
                hits += 1
    return hits / max(total, 1)


def marker_present(path, rig, world_point, predicate, radius=8):
    image, pixels = load_rgb(path)
    x, y = rig.project(world_point)
    return color_near(pixels, image.size, x, y, radius, predicate)


def is_chain_cyan(p):
    r, g, b = p
    return (g > 140) and (b > 150) and ((b - r) > 60) and ((g - r) > 40)


def is_root_orange(p):
    r, g, b = p
    return (r > 170) and (70 < g < 170) and (b < 90) and ((r - g) > 60)


def is_pole_magenta(p):
    r, g, b = p
    return (r > 170) and (g < 120) and (b > 130)


def have_pil():
    try:
        import PIL  # noqa: F401
        return True
    except ImportError:
        return False


def section_2(rig: Rig):
    print("\n== 2. Chain visualization ==")
    if not have_pil():
        MANUAL.append("2: chain visualization (PIL not installed, screenshots not analyzed)")
        return
    e = rig.e
    rig.clear_settings()
    rig.pose(BENT)
    shots = SHOT_DIR
    os.makedirs(shots, exist_ok=True)
    held = os.path.join(shots, "2_held.png")
    released = os.path.join(shots, "2_released.png")
    rig.translate_drag(TIP, ramp([0.3, -0.2, 0.0], 6), hold=True)
    chain = rig.snapshot()["positions"]
    e.call("capture_screenshot", {"path": held})
    rig.release_drag()
    e.call("capture_screenshot", {"path": released})
    cyan_held = line_coverage(held, rig, chain, is_chain_cyan)
    cyan_released = line_coverage(released, rig, chain, is_chain_cyan)
    root_held = marker_present(held, rig, chain[0], is_root_orange)
    root_released = marker_present(released, rig, chain[0], is_root_orange)
    check_true("2 during the drag the cyan chain polyline runs along the chain and the orange cross marks the root",
               (cyan_held >= 0.6) and root_held, f"polyline coverage={cyan_held:.2f} root cross={root_held}")
    check_true("2 after release both are gone", (cyan_released <= 0.2) and not root_released,
               f"polyline coverage={cyan_released:.2f} root cross={root_released}")
    check_xray(rig, held, released, chain)
    undo_viewport(rig)


def check_xray(rig: Rig, held, released, chain):
    """The chain lines read through the skinned mesh: where the mesh covers the
    chain, the cyan line is still drawn, in a colour far from the mesh behind it.

    Which chain pixels the mesh covers is measured, not assumed: the released
    frame (mesh, no lines) is compared with the same frame with the mesh
    hidden. The released pose is the held pose (a drag commits where it ends).
    """
    e = rig.e
    no_mesh = os.path.join(SHOT_DIR, "2_released_no_mesh.png")
    mesh_id = node_id(e, rig.scene, "skin_test_3_boxes_mesh")
    depth = e.undo_depth()
    e.call("set_item_property", {"item_id": mesh_id, "property": "visible", "value": False})
    e.advance(3)
    e.call("capture_screenshot", {"path": no_mesh})
    for _ in range(e.undo_depth() - depth):
        e.call("undo")
    e.advance(3)
    image, held_px = load_rgb(held)
    _, released_px = load_rgb(released)
    _, bare_px = load_rgb(no_mesh)
    size = image.size
    occluded = 0
    shown = 0
    distances = []
    for i in range(len(chain) - 1):
        for k in range(12):
            f = (k + 0.5) / 12
            x, y = rig.project(add(chain[i], scale(sub(chain[i + 1], chain[i]), f)))
            xi, yi = int(x), int(y)
            if not ((0 <= xi < size[0]) and (0 <= yi < size[1])):
                continue
            if sum(abs(a - b) for a, b in zip(released_px[xi, yi], bare_px[xi, yi])) < 40:
                continue  # the mesh does not cover this chain point
            occluded += 1
            line_pixel = nearest_matching(held_px, size, x, y, 3, is_chain_cyan)
            if line_pixel is None:
                continue
            shown += 1
            behind = released_px[line_pixel[0], line_pixel[1]]
            drawn = held_px[line_pixel[0], line_pixel[1]]
            distances.append(math.sqrt(sum((a - b) ** 2 for a, b in zip(drawn, behind))))
    distances.sort()
    fraction = shown / max(occluded, 1)
    median = distances[len(distances) // 2] if distances else 0.0
    least = distances[0] if distances else 0.0
    check_true("2 x-ray: where the mesh covers the chain the cyan line still shows, well apart from the mesh colour behind it",
               (occluded >= 12) and (fraction >= 0.9) and (median >= XRAY_MEDIAN_DISTANCE) and (least >= XRAY_LEAST_DISTANCE),
               f"covered samples={occluded} line shown on {fraction:.2f} of them, RGB distance line vs mesh behind: "
               f"median {median:.0f} least {least:.0f} (need {XRAY_MEDIAN_DISTANCE:.0f} / {XRAY_LEAST_DISTANCE:.0f})")


def nearest_matching(pixels, size, x, y, radius, predicate):
    """The pixel within `radius` of (x, y) nearest to it that satisfies `predicate`, or None."""
    best = None
    best_d2 = None
    for py in range(int(y) - radius, int(y) + radius + 1):
        for px in range(int(x) - radius, int(x) + radius + 1):
            if (0 <= px < size[0]) and (0 <= py < size[1]) and predicate(pixels[px, py]):
                d2 = ((px - x) ** 2) + ((py - y) ** 2)
                if (best_d2 is None) or (d2 < best_d2):
                    best = (px, py)
                    best_d2 = d2
    return best


# --- section 3: per-bone IK settings ----------------------------------------

BENT = (0.0, 30.0, 30.0)


def section_3(rig: Rig):
    print("\n== 3. Per-bone IK settings ==")
    e = rig.e
    rig.clear_settings()

    # 3.1 the IK group on bones only.
    rig.select("bone_1")
    on_bone = ensure_group_open(e, "IK")
    rows = {(i.get("display_label") or "").removeprefix("* ") for i in e.items(window="Properties", visible_only=False)}
    wanted = {"IK Lock", "Lock X", "Lock Y", "Lock Z", "Limit X", "Limit Y", "Limit Z", "Limit Min.x", "Limit Max.x",
              "Stiffness.x", "Rest Rotation.x", "Set Rest", "Pole Target", "Pole Target.pick", "Pole Angle"}
    missing = sorted(wanted - rows)
    rig.select("skin_test_3_boxes")
    e.advance(2)
    non_bone_rows = {(i.get("display_label") or "").removeprefix("* ") for i in e.items(window="Properties", visible_only=False)}
    check_true("3.1 a bone shows the IK group with every row, a non-bone node has none",
               on_bone and (not missing) and ("IK" not in non_bone_rows) and ("Lock X" not in non_bone_rows),
               f"missing on bone_1={missing} non-bone has IK group={'IK' in non_bone_rows}")

    # 3.2 one swing lock (checkbox clicked in Properties).
    rig.pose(BENT)
    ok = ui_checkbox(rig, "bone_1", "IK", "Lock Z", True)
    samples = rig.translate_drag(TIP, ramp([0.6, 0.0, 0.0], 12), sample=rig.snapshot)
    worst_z1 = max(abs(rig.angles(s, "bone_1")["z"]) for s in samples)
    other_z = max(abs(rig.angles(samples[-1], "bone_0")["z"]), abs(rig.angles(samples[-1], "bone_2")["z"]))
    check_true("3.2 Lock Z on bone_1: dragging along world X keeps bone_1's Z at 0 while bone_0 / bone_2 bend about Z",
               ok and (worst_z1 <= LOCKED_DEG) and (other_z >= MOVED_DEG),
               f"checkbox ok={ok} bone_1 worst |Z|={worst_z1:.3f} deg, bone_0/bone_2 |Z|={other_z:.2f} deg")
    undo_viewport(rig)
    start = rig.snapshot()
    samples = rig.translate_drag(TIP, ramp([0.0, -0.2, -0.4], 10), sample=rig.snapshot)
    x_change = abs(rig.angles(samples[-1], "bone_1")["x"] - rig.angles(start, "bone_1")["x"])
    check_true("3.2 ... and dragging along world Z still bends bone_1 about X", x_change >= MOVED_DEG,
               f"bone_1 X changed {x_change:.2f} deg")
    undo_viewport(rig)
    ok = ui_checkbox(rig, "bone_1", "IK", "Lock Z", False)
    check_true("3.2 Lock Z unticked", ok)

    # 3.3 twist lock, against the same gesture without it.
    rig.pose(BENT)
    samples = rig.translate_drag(TIP, circle(0.6, 24), sample=rig.snapshot)
    control = max(abs(rig.angles(s, "bone_1")["y"]) for s in samples)
    undo_viewport(rig)
    ok = ui_checkbox(rig, "bone_1", "IK", "Lock Y", True)
    samples = rig.translate_drag(TIP, circle(0.6, 24), sample=rig.snapshot)
    locked = max(abs(rig.angles(s, "bone_1")["y"]) for s in samples)
    check_true("3.3 the circle gesture twists bone_1 without a lock (control)", control > 1.0,
               f"bone_1 worst |twist|={control:.2f} deg")
    check_true("3.3 Lock Y on bone_1: the same circle leaves its twist at 0", ok and (locked <= LOCKED_DEG),
               f"checkbox ok={ok} bone_1 worst |twist|={locked:.3f} deg")
    undo_viewport(rig)
    ui_checkbox(rig, "bone_1", "IK", "Lock Y", False)

    # 3.4 hinge chain.
    rig.pose(BENT)
    for bone in BONES:
        rig.set_prop(bone, "Ik.lock_y", True)
        rig.set_prop(bone, "Ik.lock_z", True)
    start = rig.snapshot()
    samples = rig.translate_drag(TIP, ramp([0.0, 0.0, -0.6], 12), sample=rig.snapshot)
    worst_yz = max(max(abs(rig.angles(s, b)["y"]), abs(rig.angles(s, b)["z"])) for s in samples for b in BONES)
    tip_z_moved = abs(samples[-1]["positions"][-1][2] - start["positions"][-1][2])
    check_true("3.4 hinge (Lock Y + Z on all bones): dragging along world Z curls the chain in the YZ plane",
               (worst_yz <= LOCKED_DEG) and (tip_z_moved > 0.2),
               f"worst |Y|,|Z| over bones and steps={worst_yz:.3f} deg, tip moved {tip_z_moved:.3f} along Z")
    undo_viewport(rig)
    samples = rig.translate_drag(TIP, ramp([0.6, 0.0, 0.0], 12), sample=rig.snapshot)
    worst_x = max(abs(s["positions"][-1][0]) for s in samples)
    worst_yz = max(max(abs(rig.angles(s, b)["y"]), abs(rig.angles(s, b)["z"])) for s in samples for b in BONES)
    flips = max(sign_flips([s["positions"][-1][1] for s in samples]), sign_flips([s["positions"][-1][2] for s in samples]))
    check_true("3.4 ... dragged along world X (unreachable) the tip stays in the YZ plane, no shaking",
               (worst_x < POSITION_TOL) and (worst_yz <= LOCKED_DEG) and (flips <= 1),
               f"worst |tip x|={worst_x:.2e} worst |Y|,|Z|={worst_yz:.3f} deg direction reversals={flips}")
    undo_viewport(rig)

    # 3.5 all three axes of bone_1 locked: it moves as a rigid link.
    rig.set_prop("bone_1", "Ik.lock_x", True)
    before = rig.snapshot()
    samples = rig.translate_drag(TIP, ramp([0.0, -0.3, -0.3], 10), sample=rig.snapshot)
    bone1 = max(q_angle_deg(before["rotations"]["bone_1"], s["rotations"]["bone_1"]) for s in samples)
    others = min(q_angle_deg(before["rotations"][b], samples[-1]["rotations"][b]) for b in ("bone_0", "bone_2"))
    check_true("3.5 bone_1 fully locked stays rigid, bone_0 and bone_2 still solve",
               (bone1 < 0.05) and (others > MOVED_DEG), f"bone_1 turned {bone1:.4f} deg, least of bone_0/bone_2 {others:.2f} deg")
    undo_viewport(rig)
    rig.clear_settings()

    # 3.6 limit X of bone_1: -10 .. 45 degrees.
    lo, hi = limit_value(-10.0, 45.0)
    rig.pose((0.0, 20.0, 20.0))
    rig.set_prop("bone_1", "Ik.limit_x", True)
    rig.set_prop("bone_1", "Ik.limit_min", lo)
    rig.set_prop("bone_1", "Ik.limit_max", hi)
    # Targets that, without the limit, turn bone_1 well past each bound.
    for label, target, bound in (("+45 one way", [0.0, -1.8, 1.0], 45.0), ("-10 the other way", [0.0, -0.5, -2.5], -10.0)):
        samples = rig.translate_drag(TIP, ramp(target, 24), sample=rig.snapshot)
        xs = [rig.angles(s, "bone_1")["x"] for s in samples]
        check_limit_samples(f"3.6 limit -10..45: pulled, bone_1's X stops at {label}", xs, bound, -10.0, 45.0)
        undo_viewport(rig)

    # 3.7 lock wins over limit.
    rig.set_prop("bone_1", "Ik.lock_x", True)
    before = rig.snapshot()
    samples = rig.translate_drag(TIP, ramp([0.0, -1.0, 0.4], 10), sample=rig.snapshot)
    change = max(abs(rig.angles(s, "bone_1")["x"] - rig.angles(before, "bone_1")["x"]) for s in samples)
    check_true("3.7 Lock X with the limit on: bone_1's X does not change", change <= LOCKED_DEG, f"X changed {change:.3f} deg")
    undo_viewport(rig)
    rig.clear_settings()

    # 3.8 undo granularity of UI edits: checkbox tick, slider drag, IK drag.
    rig.pose(BENT)
    depth0 = e.undo_depth()
    ok_box = ui_checkbox(rig, "bone_1", "IK", "Limit X", True)
    depth1 = e.undo_depth()
    min_before = float_list_prop(rig, "bone_1", "Ik.limit_min")
    ok_drag = ui_drag_field(rig, "bone_1", "IK", "Limit Min.x", 60)
    depth2 = e.undo_depth()
    min_after = float_list_prop(rig, "bone_1", "Ik.limit_min")
    pose_before = rig.snapshot()
    rig.translate_drag(TIP, ramp([0.3, -0.2, 0.0], 8))
    depth3 = e.undo_depth()
    check_true("3.8 a checkbox tick, a whole Limit Min slider drag and an IK drag are one undo step each",
               ok_box and ok_drag and (depth1 - depth0 == 1) and (depth2 - depth1 == 1) and (depth3 - depth2 == 1)
               and (abs(min_after[0] - min_before[0]) > 1.0e-4),
               f"deltas: tick={depth1 - depth0} slider={depth2 - depth1} drag={depth3 - depth2}; "
               f"Limit Min.x {math.degrees(min_before[0]):.2f} -> {math.degrees(min_after[0]):.2f} deg")
    undo_viewport(rig)
    pose_undone = max(q_angle_deg(pose_before["rotations"][b], rig.snapshot()["rotations"][b]) for b in BONES)
    undo_viewport(rig)
    min_undone = float_list_prop(rig, "bone_1", "Ik.limit_min")
    undo_viewport(rig)
    limit_undone = rig.prop("bone_1", "Ik.limit_x")["value"]
    check_true("3.8 Ctrl+Z steps back through them one at a time",
               (pose_undone < 1.0e-3) and (abs(min_undone[0] - min_before[0]) < 1.0e-5) and (limit_undone == "false"),
               f"pose error={pose_undone:.4f} deg Limit Min.x restored={abs(min_undone[0] - min_before[0]) < 1.0e-5} "
               f"Limit X={limit_undone}")
    rig.clear_settings()

    # 3.9 rest pose: rotate bone_1 by 30 degrees with the Rotate X ring, Set Rest, limits measured from it.
    rig.pose((0.0, 0.0, 0.0))
    rig.rotate_drag("bone_1", [1.0, 0.0, 0.0], [5.0 * (i + 1) for i in range(6)])
    rotated = rig.local_rotation("bone_1")
    rotated_x = joint_angles(rotated)["x"]
    ok = ui_button(rig, "bone_1", "IK", "Set Rest")
    rest = float_list_prop(rig, "bone_1", "Ik.rest_rotation")
    check_true("3.9 Set Rest (button in the IK group) stores the current rotation as Rest Rotation, one undo step",
               ok and (q_angle_deg(rest, rotated) < 0.01),
               f"button ok (one undo entry)={ok} rotation X={rotated_x:.2f} deg rest vs rotation={q_angle_deg(rest, rotated):.4f} deg")
    undo_viewport(rig)
    rest_undone = float_list_prop(rig, "bone_1", "Ik.rest_rotation")
    check_true("3.9 one Ctrl+Z undoes the Set Rest press", q_angle_deg(rest_undone, rotated) > 1.0,
               f"rest after undo={rest_undone}")
    e.move(*rig.viewport_center())
    e.key("y", ["ctrl"])
    rest_redone = float_list_prop(rig, "bone_1", "Ik.rest_rotation")
    check_true("3.9 Ctrl+Y re-applies it", q_angle_deg(rest_redone, rotated) < 0.01, f"rest after redo={rest_redone}")
    rig.set_prop("bone_1", "Ik.limit_x", True)
    rig.set_prop("bone_1", "Ik.limit_min", lo)
    rig.set_prop("bone_1", "Ik.limit_max", hi)
    rig.set_local_rotation("bone_2", q_axis_angle([1.0, 0.0, 0.0], 30.0))
    samples = rig.translate_drag(TIP, ramp([0.0, -1.0, -2.2], 24), sample=rig.snapshot)
    high = max(joint_angles(s["rotations"]["bone_1"])["x"] for s in samples)
    undo_viewport(rig)
    samples = rig.translate_drag(TIP, ramp([0.0, -0.5, -2.5], 24), sample=rig.snapshot)
    low = min(joint_angles(s["rotations"]["bone_1"])["x"] for s in samples)
    undo_viewport(rig)
    check_true("3.9 limits are measured from the rest pose: bone_1 stops at rest + 45 and rest - 10",
               (abs(high - (rotated_x + 45.0)) <= 1.0) and (abs(low - (rotated_x - 10.0)) <= 1.0),
               f"reached X {low:.2f} .. {high:.2f} deg, rest {rotated_x:.2f} deg")
    rig.clear_settings()


# --- section 4: channel locks -----------------------------------------------

def section_4(rig: Rig):
    print("\n== 4. Channel locks ==")
    e = rig.e
    rig.clear_settings()
    box = "channel_lock_box"
    if box not in rig.ids:
        e.call("create_shape", {"scene_name": rig.scene, "shape": "box", "name": box, "motion_mode": "none",
                                "size": [0.4, 0.4, 0.4], "position": [-1.5, 0.6, 1.5]})
        e.wait_idle()
        rig.ids[box] = node_id(e, rig.scene, box)

    # 4.1 tool masking on a plain node.
    ok = ui_checkbox(rig, box, "Channel Locks", "Translation X", True) and \
        ui_checkbox(rig, box, "Channel Locks", "Rotation Y", True)
    check_true("4.1 Translation X and Rotation Y ticked in Channel Locks", ok)
    p0 = rig.world_position(box)
    rig.translate_drag_pointer(box, ramp([0.5, 0.0, 0.0], 6), "Translate X")
    p1 = rig.world_position(box)
    check_true("4.1 Move tool, X arrow: the node does not move", length(sub(p1, p0)) < 1.0e-4, f"moved {fmt3(sub(p1, p0))}")
    rig.translate_drag_pointer(box, ramp([0.4, 0.3, 0.0], 6), "Translate XY")
    p2 = rig.world_position(box)
    d = sub(p2, p1)
    check_true("4.1 Move tool, XY plane: it moves in Y only", (abs(d[0]) < 1.0e-4) and (abs(d[1] - 0.3) < 0.02),
               f"moved {fmt3(d)}")
    q0 = rig.local_rotation(box)
    rig.rotate_drag_pointer(box, [0.0, 1.0, 0.0], [8.0 * (i + 1) for i in range(5)])
    q1 = rig.local_rotation(box)
    check_true("4.1 Rotate tool, Y ring: nothing", q_angle_deg(q0, q1) < 0.01, f"turned {q_angle_deg(q0, q1):.4f} deg")
    rig.rotate_drag_pointer(box, [1.0, 0.0, 0.0], [8.0 * (i + 1) for i in range(5)])
    q2 = rig.local_rotation(box)
    check_true("4.1 Rotate tool, X ring: it turns about X only",
               (q_angle_deg(q1, q2) > 20.0) and (abs(q2[1]) < 1.0e-3) and (abs(q2[2]) < 1.0e-3),
               f"turned {q_angle_deg(q1, q2):.2f} deg, rotation {q2}")
    # The Transform window greys locked translation / scale components out in
    # Local mode (it edits the node's own local transform there). Rotation
    # fields stay editable - the Euler / quaternion / axis-angle views mix
    # axes - and the commit path drops the locked component instead.
    rig.select(box)
    e.click("Transform", "Coordinate System.y")
    e.advance(2)
    fields = {i.get("display_label"): i["status"] for i in e.items(window="Transform", visible_only=False)}
    translation = [fields.get(f"Translation.{c}", {}).get("disabled") for c in "xyz"]
    check_true("4.1 Transform window (Local mode): Translation X greyed out, Y and Z editable",
               translation == [True, False, False], f"translation disabled={translation}")
    q_before = rig.local_rotation(box)
    e.call("transform_selection", {"space": "local", "rotation_xyzw": q_mul(q_axis_angle([0.0, 1.0, 0.0], 30.0), q_before)})
    e.advance(3)
    q_after = rig.local_rotation(box)
    check_true("4.1 a Transform window rotation edit about Y is dropped by the Rotation Y lock",
               q_angle_deg(q_before, q_after) < 0.01, f"turned {q_angle_deg(q_before, q_after):.4f} deg")
    e.click("Transform", "Coordinate System.x")
    s0 = rig.node(box)["local_transform"]["scale"]
    rig.translate_drag_pointer(box, ramp([0.3, 0.0, 0.0], 6), "Scale X")
    s1 = rig.node(box)["local_transform"]["scale"]
    check_true("4.1 scale is untouched by either lock", abs(s1[0] - s0[0]) > 0.02, f"scale {s0} -> {s1}")

    # 4.4 undo of lock toggles.
    depth = e.undo_depth()
    ok = ui_checkbox(rig, box, "Channel Locks", "Scale Z", True)
    toggled = e.undo_depth() - depth
    undo_viewport(rig)
    back = rig.prop(box, "lock_scale_z")["value"]
    check_true("4.4 a lock toggle is one undo step and Ctrl+Z reverts it", ok and (toggled == 1) and (back == "false"),
               f"undo delta={toggled} lock_scale_z after Ctrl+Z={back}")

    # 4.3 no flip past 90 degrees.
    ui_checkbox(rig, box, "Channel Locks", "Translation X", False)
    ui_checkbox(rig, box, "Channel Locks", "Rotation Y", False)
    for axis, ring, index in (([0.0, 1.0, 0.0], "Rotate Y", 1), ([0.0, 0.0, 1.0], "Rotate Z", 2)):
        # Reset before locking: set_node_transform honours channel locks too.
        ui_checkbox(rig, box, "Channel Locks", "Rotation X", False)
        rig.set_local_rotation(box, [0.0, 0.0, 0.0, 1.0])
        ui_checkbox(rig, box, "Channel Locks", "Rotation X", True)
        depth = e.undo_depth()
        # The Y ring by mouse (the input path), the Z ring over drag_selection.
        angles = [5.0 * (i + 1) for i in range(34)]
        if ring == "Rotate Y":
            samples = rig.rotate_drag_pointer(box, axis, angles, sample=lambda: rig.local_rotation(box))
        else:
            samples = rig.rotate_drag(box, axis, angles, sample=lambda: rig.local_rotation(box))
        steps = [q_angle_deg(samples[i], samples[i + 1]) for i in range(len(samples) - 1)]
        off_axis = max(max(abs(q[j]) for j in range(3) if j != index) for q in samples)
        total = q_angle_deg([0.0, 0.0, 0.0, 1.0], rig.local_rotation(box))
        check_true(f"4.3 Rotation X locked, one long {ring} drag to 170 degrees turns continuously (no flip near 90)",
                   (max(steps) <= 7.0) and (off_axis < 1.0e-3) and (abs(total - 170.0) < 2.0),
                   f"largest step={max(steps):.2f} deg, off-axis quaternion part={off_axis:.2e}, total={total:.2f} deg")
        check_true(f"4.4 the locked {ring} drag is one undo step", e.undo_depth() - depth == 1, f"undo delta={e.undo_depth() - depth}")
    ui_checkbox(rig, box, "Channel Locks", "Rotation X", False)

    # 4.2 rotation channel locks act as IK axis locks.
    rig.pose(BENT)
    ok = ui_checkbox(rig, "bone_1", "Channel Locks", "Rotation Z", True)
    start = rig.snapshot()
    samples = rig.translate_drag(TIP, ramp([0.6, 0.0, 0.0], 12), sample=rig.snapshot)
    worst = max(abs(rig.angles(s, "bone_1")["z"] - rig.angles(start, "bone_1")["z"]) for s in samples)
    check_true("4.2 Channel Locks > Rotation Z on bone_1 holds its Z during an IK drag along X",
               ok and (worst <= LOCKED_DEG), f"checkbox ok={ok} worst Z change={worst:.3f} deg")
    undo_viewport(rig)
    ok = ui_checkbox(rig, "bone_1", "Channel Locks", "Rotation Y", True)
    samples = rig.translate_drag(TIP, circle(0.6, 24), sample=rig.snapshot)
    worst = max(max(abs(rig.angles(s, "bone_1")[a] - rig.angles(start, "bone_1")[a]) for a in "yz") for s in samples)
    check_true("4.2 ... with Rotation Y too, an out-of-plane circle leaves bone_1's Y and Z put",
               ok and (worst <= LOCKED_DEG), f"checkbox ok={ok} worst Y/Z change={worst:.3f} deg")
    undo_viewport(rig)
    ui_checkbox(rig, "bone_1", "Channel Locks", "Rotation Y", False)
    ui_checkbox(rig, "bone_1", "Channel Locks", "Rotation Z", False)
    rig.clear_settings()


# --- section 5: pole target -------------------------------------------------

def bend_direction(positions):
    root = positions[0]
    axis = normalize(sub(positions[-1], root))
    total = [0.0, 0.0, 0.0]
    for p in positions[1:-1]:
        d = sub(p, root)
        total = add(total, sub(d, scale(axis, dot(d, axis))))
    return normalize(total), axis


def swivel_to(positions, point):
    """Signed angle about the root-to-tip axis from `point`'s direction to the chain's bend."""
    bend, axis = bend_direction(positions)
    d = sub(point, positions[0])
    to_point = normalize(sub(d, scale(axis, dot(d, axis))))
    return math.degrees(math.atan2(dot(cross(to_point, bend), axis), dot(to_point, bend)))


def node_id(e: Editor, scene, name):
    found = [n for n in e.call("get_scene_nodes", {"scene_name": scene})["nodes"]
             if (n["name"] == name) and n.get("content", False)]
    return found[0]["id"] if found else None


def ensure_pole(rig: Rig, position):
    e = rig.e
    pole = "ik_pole"
    if pole not in rig.ids:
        e.call("create_node", {"scene_name": rig.scene, "name": pole, "position": position})
        for _ in range(10):
            e.advance(2)
            found = node_id(e, rig.scene, pole)
            if found is not None:
                rig.ids[pole] = found
                break
    else:
        e.call("set_node_transform", {"scene_name": rig.scene, "node_id": rig.ids[pole], "translation": position})
        e.advance(2)
    return pole


def section_5(rig: Rig):
    print("\n== 5. Pole target ==")
    e = rig.e
    rig.clear_settings()
    rig.pose(BENT)
    pole_position = [1.5, 1.2, 0.5]
    pole = ensure_pole(rig, pole_position)

    # 5.2 through the widget: the picker arrow of the Pole Target row, then
    # the node in the list it opens.
    depth = e.undo_depth()
    offered = ui_pick_reference(rig, "bone_1", "IK", "Pole Target", pole)
    entry = rig.prop("bone_1", "Ik.pole_target")
    wanted = set(BONES + [TIP, pole])
    missing = sorted(wanted - set(offered or []))
    check_true("5.2 the Pole Target picker (arrow) lists the scene's nodes, picking one names it, one undo step",
               (offered is not None) and not missing and (entry.get("reference_id") == rig.ids[pole])
               and (e.undo_depth() - depth == 1),
               f"offered {len(offered or [])} missing={missing} row value={entry.get('value')!r} undo delta={e.undo_depth() - depth}")
    shows = select_for_properties(rig, "bone_1") and (ensure_row_visible(e, "Pole Target.clear") is not None)
    check_true("5.2 the row then shows the reference (its clear button is drawn only while it holds one)", shows)

    shots = SHOT_DIR
    os.makedirs(shots, exist_ok=True)
    before = rig.snapshot()
    samples = rig.translate_drag(TIP, ramp([0.2, -0.2, 0.0], 8), sample=rig.snapshot, hold=True)
    held = os.path.join(shots, "5_pole_held.png")
    e.call("capture_screenshot", {"path": held})
    rig.release_drag()
    final = rig.snapshot()
    swivel = swivel_to(final["positions"], pole_position)
    first_jump = q_angle_deg(before["rotations"]["bone_1"], samples[0]["rotations"]["bone_1"])
    check_true("5.3 dragging the hand swings the elbow toward the pole", abs(swivel) <= 2.0, f"swivel={swivel:+.2f} deg")
    # The first step turns the bend plane onto the pole at once; after it the
    # bend stays on the pole and the steps are the drag's own.
    start_off = swivel_to(before["positions"], pole_position)
    swivels = [swivel_to(sample["positions"], pole_position) for sample in samples]
    later = [max(q_angle_deg(samples[i]["rotations"][b], samples[i + 1]["rotations"][b]) for b in BONES)
             for i in range(len(samples) - 1)]
    check_true("5.3 the first step is the pole alignment alone: the bend is on the pole from step 1 on, later steps small",
               (max(abs(v) for v in swivels) <= 2.0) and (max(later) <= 5.0),
               f"start pose {start_off:+.1f} deg off the pole, bend off the pole over the steps <= {max(abs(v) for v in swivels):.2f} deg, "
               f"largest later step {max(later):.2f} deg")
    if have_pil():
        coverage = line_coverage(held, rig, [pole_position, samples[-1]["positions"][0]], is_pole_magenta)
        cross = marker_present(held, rig, pole_position, is_pole_magenta)
        check_true("5.3 the visualization adds the magenta line from the pole to the root and a cross at the pole",
                   (coverage >= 0.8) and cross, f"line coverage={coverage:.2f} pole cross={cross}")
    else:
        MANUAL.append("5.3: magenta pole line / cross (PIL not installed)")
    DECISIONS.append(f"5.3: a drag whose start pose is off the pole plane snaps onto it in the first step "
                     f"(here the bend plane turns {abs(start_off):.1f} deg, the elbow {first_jump:.1f} deg); "
                     f"the alternative is easing the swivel in over the first part of the drag")
    undo_viewport(rig)

    rig.set_prop("bone_1", "Ik.pole_angle", math.pi / 2.0)
    rig.translate_drag(TIP, ramp([0.2, -0.2, 0.0], 8))
    swivel = swivel_to(rig.snapshot()["positions"], pole_position)
    check_true("5.4 Pole Angle 90: the bend turns a quarter turn about the root-to-hand line", abs(swivel - 90.0) <= 2.0,
               f"swivel={swivel:+.2f} deg")
    undo_viewport(rig)
    rig.set_prop("bone_1", "Ik.pole_angle", None)

    moved_pole = [-1.5, 1.2, 0.5]
    before = rig.snapshot()
    ensure_pole(rig, moved_pole)
    untouched = max(q_angle_deg(before["rotations"][b], rig.snapshot()["rotations"][b]) for b in BONES)
    rig.translate_drag(TIP, ramp([0.2, -0.2, 0.0], 8))
    swivel = swivel_to(rig.snapshot()["positions"], moved_pole)
    check_true("5.5 moving the pole alone does not re-pose the arm; the next drag follows the new side",
               (untouched < 1.0e-3) and (abs(swivel) <= 2.0), f"pose change on pole move={untouched:.4f} deg swivel={swivel:+.2f} deg")
    undo_viewport(rig)

    # 5.7 a limit on the elbow wins over the pole, without shaking. The pole
    # sits to the side (-X), so aiming the elbow at it takes a swing about the
    # elbow's Z; a Z limit of +-10 degrees stops that swing short of the pole.
    # The pose starts inside the limit: a limit is widened to contain the
    # drag-start state (no teleport, ik_settings.md).
    rig.pose(BENT)
    lo, hi = limit_value(-10.0, 10.0, axis=2)
    rig.set_prop("bone_1", "Ik.limit_z", True)
    rig.set_prop("bone_1", "Ik.limit_min", lo)
    rig.set_prop("bone_1", "Ik.limit_max", hi)
    samples = rig.translate_drag(TIP, ramp([0.2, -0.6, 0.0], 16), sample=rig.snapshot)
    zs = [rig.angles(s, "bone_1")["z"] for s in samples]
    flips = sign_flips(zs, 0.05)
    swivel = swivel_to(samples[-1]["positions"], moved_pole)
    check_true("5.7 a Z limit on the elbow wins over the pole (reached, held) and the arm does not shake",
               (max(abs(z) for z in zs) <= 10.0 + LIMIT_TOL_DEG) and (max(abs(z) for z in zs) >= 9.0) and (flips <= 1),
               f"elbow Z range {min(zs):.2f} .. {max(zs):.2f} deg, direction reversals={flips}, "
               f"bend {swivel:+.1f} deg short of the pole")
    undo_viewport(rig)

    depth = e.undo_depth()
    clicked = ui_click_row(rig, "bone_1", "IK", "Pole Target.clear")
    cleared = rig.prop("bone_1", "Ik.pole_target").get("reference_id")
    check_true("5.6 clearing the pole (the row's clear button) is one undo step",
               clicked and (e.undo_depth() - depth == 1) and not cleared,
               f"clicked={clicked} undo delta={e.undo_depth() - depth} reference after clear={cleared}")
    rig.clear_settings()


# --- section 6: effector orientation ----------------------------------------

def section_6(rig: Rig):
    print("\n== 6. Effector orientation ==")
    rig.clear_settings()
    rig.pose(BENT)
    ok = set_effector_orientation(rig, "Follow Last Segment")
    before = rig.snapshot()
    rig.translate_drag(TIP, ramp([0.8, -0.6, 0.0], 12))
    after = rig.snapshot()
    local = q_angle_deg(before["rotations"][TIP], after["rotations"][TIP])
    world = q_angle_deg(before["tip_world_rotation"], after["tip_world_rotation"])
    check_true("6.1 'Follow Last Segment': the tip turns with bone_2 (local rotation kept)",
               ok and (local < 0.01) and (world > MOVED_DEG),
               f"combo ok={ok} tip local change={local:.4f} deg world change={world:.2f} deg")
    undo_viewport(rig)
    ok = set_effector_orientation(rig, "Keep World")
    rig.translate_drag(TIP, ramp([0.8, -0.6, 0.0], 12))
    after = rig.snapshot()
    world = q_angle_deg(before["tip_world_rotation"], after["tip_world_rotation"])
    check_true("6.2 'Keep World': the tip holds its world orientation", ok and (world < 0.01), f"tip world change={world:.4f} deg")
    undo_viewport(rig)


# --- section 7: persistence -------------------------------------------------

def load_scene_file(e: Editor, path):
    before = set(e.scene_names())
    e.call("load_scene", {"path": path})
    for _ in range(200):
        e.advance(4)
        new = [n for n in e.scene_names() if n not in before]
        if new:
            e.wait_idle()
            return new[0]
    raise RuntimeError(f"load_scene produced no scene for {path}")


def persisted_state(e: Editor, scene):
    bone_1 = {p["name"]: p for p in e.call("get_item_properties", {"item_id": node_id(e, scene, "bone_1")})["properties"]}
    bone_0 = {p["name"]: p for p in e.call("get_item_properties", {"item_id": node_id(e, scene, "bone_0")})["properties"]}
    return {
        "pole_reference":  bone_1["Ik.pole_target"].get("reference_id"),
        "pole_node":       node_id(e, scene, "ik_pole"),
        "limit_x":         bone_1["Ik.limit_x"]["value"],
        "limit_max_x_deg": round(math.degrees(float(bone_1["Ik.limit_max"]["value"].split()[0])), 3),
        "lock_rotation_z": bone_0["lock_rotation_z"]["value"],
    }


def section_7(rig: Rig):
    print("\n== 7. Persistence ==")
    e = rig.e
    rig.clear_settings()
    pole = ensure_pole(rig, [1.5, 1.2, 0.5])
    lo, hi = limit_value(-10.0, 45.0)
    rig.set_prop("bone_1", "Ik.pole_target", reference_id=rig.ids[pole])
    rig.set_prop("bone_1", "Ik.limit_x", True)
    rig.set_prop("bone_1", "Ik.limit_min", lo)
    rig.set_prop("bone_1", "Ik.limit_max", hi)
    rig.set_prop("bone_0", "lock_rotation_z", True)
    scratch = tempfile.mkdtemp(prefix="erhe_ik_persist_")
    opened = []
    try:
        path = os.path.join(scratch, "ik_persistence.glb")
        e.call("save_scene", {"scene_name": rig.scene, "path": path})
        e.wait_idle()

        reopened = load_scene_file(e, path)
        opened.append(reopened)
        s = persisted_state(e, reopened)
        check_true("7.1 save + re-open keeps the pole (naming the re-opened pole node), the limit and the channel lock",
                   (s["pole_node"] is not None) and (s["pole_reference"] == s["pole_node"]) and (s["limit_x"] == "true")
                   and (abs(s["limit_max_x_deg"] - 45.0) < 0.01) and (s["lock_rotation_z"] == "true"), f"{s}")

        importing = e.create_scene()
        opened.append(importing)
        e.call("import_gltf", {"scene_name": importing, "path": path})
        e.wait_idle()
        s = persisted_state(e, importing)
        check_true("7.2 importing the saved .glb into another scene binds the pole to the imported copy",
                   (s["pole_node"] is not None) and (s["pole_reference"] == s["pole_node"]), f"{s}")

        usd_scene = load_scene_file(e, USD_PATH)
        opened.append(usd_scene)
        formats = {sc["name"]: sc.get("source_format") for sc in e.call("list_scenes")["scenes"]}
        if formats.get(usd_scene) != "usd":
            print(f"  [SKIP] 7.3 USD save warning ({USD_PATH} did not open as a USD scene)")
        else:
            e.call("import_gltf", {"scene_name": usd_scene, "path": ASSET_PATH})
            e.wait_idle()
            e.call("set_item_property", {"item_id": node_id(e, usd_scene, "bone_1"), "property": "Ik.limit_x", "value": True})
            e.advance(4)
            usd_path = os.path.join(scratch, "ik_persistence.usda")
            offset = log_size()
            e.call("save_scene", {"scene_name": usd_scene, "path": usd_path})
            e.wait_idle()
            warned = "carry IK settings" in log_since(offset)
            check_true("7.3 saving as USD logs that IK settings are not written", warned and os.path.isfile(usd_path),
                       f"warned={warned} file={os.path.isfile(usd_path)}")

        offset = log_size()
        for scene in reversed(opened):
            e.close_scene(scene)
        opened.clear()
        deadline = time.monotonic() + 4.0
        while time.monotonic() < deadline:
            e.advance(4)
        leaked = "scene-close leak" in log_since(offset)
        check_true("7.4 closing the scenes logs no 'scene-close leak'", not leaked)
    finally:
        for scene in reversed(opened):
            e.close_scene(scene)
        shutil.rmtree(scratch, ignore_errors=True)
    rig.clear_settings()


def section_8(rig: Rig):
    print("\n== 8. Behaviour the verdicts rest on ==")
    rig.clear_settings()

    # 8.1 mid-chain drag: the dragged bone is the effector, bone_0 alone aims
    # at the target (one segment: the bone lands on the root-target line) and
    # everything below the dragged bone follows rigidly.
    rig.pose(BENT)
    before = rig.snapshot()
    world_before = rig.world_rotation("bone_1")
    delta = [0.3, -0.2, 0.2]
    rig.translate_drag("bone_1", ramp(delta, 8))
    after = rig.snapshot()
    root = before["positions"][0]
    target = add(before["positions"][1], delta)
    on_line = add(root, scale(normalize(sub(target, root)), length(sub(before["positions"][1], root))))
    aim_error = length(sub(after["positions"][1], on_line))
    children = max(q_angle_deg(before["rotations"][n], after["rotations"][n]) for n in ("bone_2", TIP))
    world_turn = q_angle_deg(world_before, rig.world_rotation("bone_1"))
    bone0 = q_angle_deg(before["rotations"]["bone_0"], after["rotations"]["bone_0"])
    check_true("8.1 mid-chain drag (bone_1): bone_0 aims at the target, bone_1 keeps its world orientation, "
               "its children follow rigidly",
               (aim_error < POSITION_TOL) and (children < 1.0e-3) and (world_turn < 0.01) and (bone0 > MOVED_DEG),
               f"bone_1 off the root-target line by {aim_error:.2e}, children turned {children:.4f} deg, "
               f"bone_1 world turn {world_turn:.4f} deg, bone_0 turned {bone0:.2f} deg")
    undo_viewport(rig)
    DECISIONS.append("8: a mid-chain drag makes the dragged bone the effector; the bones below it follow rigidly "
                     "(8.1). The alternative is keeping the chain's end in place (a two-target solve)")

    # 8.2 each drag step solves from the drag-start pose: a target visited
    # twice by different paths gives the same pose, back at the start gives
    # the start pose.
    rig.pose(BENT)
    start = rig.snapshot()
    a = [0.4, -0.3, 0.2]
    path = ramp(a, 6) + [add(a, d) for d in ([0.3, 0.0, 0.0], [0.3, 0.3, -0.2], [0.0, 0.3, -0.4], [0.0, 0.0, 0.0])]
    path += [scale(a, 1.0 - ((i + 1) / 6)) for i in range(6)]
    samples = rig.translate_drag(TIP, path, sample=rig.snapshot)
    first_a = samples[5]
    second_a = samples[9]
    revisit = max(q_angle_deg(first_a["rotations"][b], second_a["rotations"][b]) for b in BONES)
    back = max(q_angle_deg(start["rotations"][b], samples[-1]["rotations"][b]) for b in BONES)
    check_true("8.2 path independence: the same target by two paths gives the same pose, back at the start the start pose",
               (revisit < 1.0e-3) and (back < 1.0e-3), f"revisit difference={revisit:.2e} deg back-at-start difference={back:.2e} deg")
    undo_viewport(rig)
    DECISIONS.append("8: each drag step solves from the drag-start pose, so a drag is path independent (8.2); "
                     "an incremental solve would keep bends picked up on the way and drift")

    stability_sweep(rig)


# The random sweep: settings per bone, one of these, drawn per scenario.
SWEEP_KINDS = ["free", "lock_y", "lock_z", "hinge", "limit_x", "limit_z"]
SWEEP_SCENARIOS = 12
SWEEP_STEPS     = 24
SWEEP_STEP      = 0.05  # world units per step of the target's random walk
SWEEP_SEED      = 20260925
SWEEP_REPLAYS   = 3     # scenarios replayed for the determinism check
# A step moving a joint more than this many times the target's step is a jump.
# The unconstrained solve near a straight chain with a pole reaches about 4x.
JUMP_RATIO      = 5.0


def random_unit(rng):
    while True:
        v = [rng.uniform(-1.0, 1.0) for _ in range(3)]
        n = length(v)
        if 0.1 < n <= 1.0:
            return scale(v, 1.0 / n)


def random_walk(rng, steps, step):
    p = [0.0, 0.0, 0.0]
    heading = random_unit(rng)
    out = []
    for _ in range(steps):
        heading = normalize(add(heading, scale(random_unit(rng), 0.6)))
        p = add(p, scale(heading, step))
        out.append(p)
    return out


def make_sweep_scenarios(seed=SWEEP_SEED, count=SWEEP_SCENARIOS):
    """Every scenario of the sweep, drawn up front, so one can be replayed alone (run_sweep_scenario)."""
    rng = random.Random(seed)
    scenarios = []
    for _ in range(count):
        scenario = {
            "bends": [rng.uniform(-40.0, 40.0) for _ in BONES],
            "kinds": [rng.choice(SWEEP_KINDS) for _ in BONES],
            "margins": [(rng.uniform(10.0, 40.0), rng.uniform(10.0, 40.0)) for _ in BONES],
            "pole": add([0.0, 1.5, 0.0], scale(random_unit(rng), 1.5)) if (rng.random() < 0.35) else None,
        }
        scenario["walk"] = random_walk(rng, SWEEP_STEPS, SWEEP_STEP)
        scenarios.append(scenario)
    return scenarios


def apply_sweep_settings(rig: Rig, scenario, start):
    """Set one scenario's per-bone settings; returns {bone: (kind, locked axes, (axis, lo, hi) | None)}.

    A limit spans the rest angle 0 (Limit Min is in [-180, 0], Limit Max in
    [0, 180]) and the bone's drag-start angle, plus the scenario's margins,
    so the start pose is inside it (no widening)."""
    chosen = {}
    for bone, kind, (below, above) in zip(BONES, scenario["kinds"], scenario["margins"]):
        locked = {"lock_y": ["y"], "lock_z": ["z"], "hinge": ["y", "z"]}.get(kind, [])
        for axis in locked:
            rig.set_prop(bone, f"Ik.lock_{axis}", True)
        limit = None
        if kind in ("limit_x", "limit_z"):
            axis = kind[-1]
            now = rig.angles(start, bone)[axis]
            lo = max(min(now, 0.0) - below, -180.0)
            hi = min(max(now, 0.0) + above, 180.0)
            lo_value, hi_value = limit_value(lo, hi, axis={"x": 0, "z": 2}[axis])
            rig.set_prop(bone, f"Ik.limit_{axis}", True)
            rig.set_prop(bone, "Ik.limit_min", lo_value)
            rig.set_prop(bone, "Ik.limit_max", hi_value)
            limit = (axis, lo, hi)
        chosen[bone] = (kind, locked, limit)
    return chosen


def run_sweep_scenario(rig: Rig, scenario):
    """Pose, set up and drag one sweep scenario; the drag is undone, the settings are left set.

    Returns (start snapshot, per-step snapshots, chosen settings)."""
    rig.clear_settings()
    rig.pose(tuple(scenario["bends"]))
    start = rig.snapshot()
    chosen = apply_sweep_settings(rig, scenario, start)
    if scenario["pole"] is not None:
        pole = ensure_pole(rig, scenario["pole"])
        rig.set_prop("bone_1", "Ik.pole_target", reference_id=rig.ids[pole])
    samples = rig.translate_drag(TIP, scenario["walk"], sample=rig.snapshot)
    undo_viewport(rig)
    return start, samples, chosen


def stability_sweep(rig: Rig):
    """8.3 the constrained solver under random settings and random drags: locks
    and limits hold, bone lengths hold, no step makes the chain jump, and the
    same drag replayed gives the same poses.

    A jump is measured where it is seen: how far the intermediate joints move
    in one step, against how far the target moved (SWEEP_STEP). A bone
    turning about its own length moves no joint and is no jump.
    """
    worst_lock = 0.0
    worst_limit = 0.0
    worst_limit_at = ""
    worst_length = 0.0
    jumps = []
    replay_error = 0.0
    largest_ratio = 0.0
    for scenario, parameters in enumerate(make_sweep_scenarios()):
        start, samples, chosen = run_sweep_scenario(rig, parameters)
        rest_lengths = segment_lengths(start["positions"])
        pole_position = parameters["pole"]
        ratios = []
        previous = start
        for step, s in enumerate(samples):
            moved = max(length(sub(a, b)) for a, b in zip(previous["positions"][1:-1], s["positions"][1:-1]))
            ratios.append(moved / SWEEP_STEP)
            previous = s
            worst_length = max(worst_length, max(abs(x - y) for x, y in zip(segment_lengths(s["positions"]), rest_lengths)))
            for bone, (kind, locked, limit) in chosen.items():
                now = rig.angles(s, bone)
                began = rig.angles(start, bone)
                for axis in locked:
                    worst_lock = max(worst_lock, abs(now[axis] - began[axis]))
                if limit is not None:
                    axis, lo, hi = limit
                    excess = max(lo - now[axis], now[axis] - hi, 0.0)
                    if excess > worst_limit:
                        worst_limit = excess
                        worst_limit_at = (f" (scenario {scenario} step {step} {bone} {axis}={now[axis]:.2f} "
                                          f"limit {lo:.2f} .. {hi:.2f}, start {began[axis]:.2f})")
        # With a pole the first step carries the pole alignment (5.3); jumps are judged after it.
        first = 1 if pole_position is not None else 0
        for i in range(first, len(ratios)):
            largest_ratio = max(largest_ratio, ratios[i])
            if ratios[i] > JUMP_RATIO:
                target = add(start["positions"][-1], parameters["walk"][i])
                tip_error = length(sub(samples[i]["positions"][-1], target))
                jumps.append(f"scenario {scenario} step {i}: joints moved {ratios[i]:.1f}x the target step, "
                             f"tip {tip_error:.2f} off the target, settings={[chosen[b][0] for b in BONES]} "
                             f"pole={pole_position is not None}")
        if scenario < SWEEP_REPLAYS:
            again = rig.translate_drag(TIP, parameters["walk"], sample=rig.snapshot)
            undo_viewport(rig)
            for x, y in zip(samples, again):
                replay_error = max(replay_error, max(q_angle_deg(x["rotations"][b], y["rotations"][b]) for b in BONES))
        if pole_position is not None:
            rig.set_prop("bone_1", "Ik.pole_target", None)
    rig.clear_settings()
    check_true(f"8.3 stability sweep ({SWEEP_SCENARIOS} random setting sets x {SWEEP_STEPS}-step random drags, seed {SWEEP_SEED}): "
               "locks hold, limits hold, bone lengths hold",
               (worst_lock <= LOCKED_DEG) and (worst_limit <= LIMIT_TOL_DEG) and (worst_length < POSITION_TOL),
               f"worst lock drift={worst_lock:.3f} deg worst limit excess={worst_limit:.3f} deg"
               f"{worst_limit_at if worst_limit > LIMIT_TOL_DEG else ''} worst length error={worst_length:.2e}")
    check_true(f"8.3 stability sweep: no step moves a joint more than {JUMP_RATIO:.0f}x the target's step",
               not jumps, f"largest {largest_ratio:.1f}x; {len(jumps)} jump(s)" + ("; " + "; ".join(jumps[:4]) if jumps else ""))
    check_true("8.3 stability sweep: the same drag replayed gives the same poses", replay_error < 1.0e-3,
               f"largest replay difference={replay_error:.2e} deg over {SWEEP_REPLAYS} replays")

SECTIONS = {
    1: section_1,
    2: section_2,
    3: section_3,
    4: section_4,
    5: section_5,
    6: section_6,
    7: section_7,
    8: section_8,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=None)
    parser.add_argument("--launch", action="store_true", help="start the headless editor and stop it at the end")
    parser.add_argument("--editor", default=DEFAULT_EDITOR, help=f"editor executable for --launch (default {DEFAULT_EDITOR})")
    parser.add_argument("--section", type=int, action="append", help="run only these sections (repeatable)")
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
        rig = setup(e)
        for number, function in SECTIONS.items():
            if args.section and (number not in args.section):
                continue
            function(rig)
    finally:
        if UI_RETRIES:
            print("\nUI clicks that needed a retry:")
            for line in UI_RETRIES:
                print(f"  [RETRY] {line}")
        if MANUAL:
            print("\nNeeds a human (interactive_test_pass.md):")
            for line in MANUAL:
                print(f"  [MANUAL] {line}")
        if DECISIONS:
            print("\nBehaviour choices for the user (measured above, not pass / fail):")
            for line in DECISIONS:
                print(f"  [DECISION] {line}")
        if process is not None:
            try:
                client.call("request_exit")
                process.wait(timeout=60)
            except Exception:
                process.kill()
    return report()


if __name__ == "__main__":
    sys.exit(main())
