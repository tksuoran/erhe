"""Automatic level of detail selection dolly check
(doc/plans/lod_gpu_selection.md steps 2a and 2b, doc/editor/lod.md
"Automatic selection").

Launches the headless editor (doc/agents/editor_runs.md), creates a torus
with a 6-level LOD chain in the default scene, turns the automatic selection
on, and per culling classification (--classification: cpu, gpu or both, in
that order in one session) dollies the viewport camera away from the torus
along +Z, looking at it. At every dolly step it reads the
level the CPU classification selected for the torus's color entry in the
viewport's LOD column (get_lod_selection) and the distance from the column's
selection origin to the entry's box, and checks it against the selection
rule of doc/plans/lod_gpu_selection.md section 2: level L is selected from
d_L = error_L * k / threshold on, k = max_axis_scale * projection_scale.
Every step whose distance is more than one dolly step away from every
switch distance must select exactly the predicted level; the switch itself
must happen within one dolly step of d_L, for every level of the chain.

With --hysteresis BAND (doc/plans/lod_gpu_selection.md step 4; default 0,
no hysteresis: the preset's lod_hysteresis_band is set to BAND either way)
the outward dolly switches to a coarser level L at d_L / (1 - BAND), and a
second, inward dolly (from the far end back to the start) switches to the
finer level as soon as the distance drops below d_L. Then, per level L, the
camera stops inside the band (between d_L and d_L / (1 - BAND)), once
arriving from a pose of level L - 1 (it keeps L - 1) and once from a pose of
level L (it keeps L), and the level must stay unchanged for --hold-frames
classified frames (default 300). Under gpu the classification check runs
at every hold pose and every tenth dolly step (0 mismatches across a moving
camera).

Then, per level L, renders render_scene_image at a pose inside L's distance
range (for the image's own projection scale) with the automatic selection,
and again with Lod.mode = forced, Lod.forced_level = L, and requires the two
PNGs to be byte-identical. Shadows are on (the default scene's
directional lights; --no-shadows turns them off for the image pairs): the
shadow passes draw the torus at the level the image's view selects for it,
since it is inside that view's frustum (doc/plans/lod_gpu_selection.md
step 5), so the torus's shadow on itself and on the floor is part of the
comparison.
Under gpu the levels read are still the CPU classification's (the GPU's
are compared by get_cull_classification_check, which the gpu pass runs at
every image pose), and the images come from the GPU-generated commands;
with both, the automatic image of each level must also be byte-identical
between cpu and gpu.

With --outline (doc/plans/lod_gpu_selection.md step 3; threshold 16 pixels,
so the coarsest level's pose stays near), per level L the
viewport camera is placed inside L's distance range for the viewport's own
projection scale with the torus selected, and the viewport (cropped from
capture_screenshot to the viewport window's content region, so other
windows' text does not count) is captured with the automatic selection and again with Lod forced to
L; the two crops must be identical, with the selection fill on and off (the
selected style's polygon_fill), in every classification run: the selection
outline traces the level the fill draws. A crop forced to the next level
must differ (negative control). Needs Pillow.

Usage: python3 scripts/lod_dolly_verify.py [--editor PATH] [--threshold PIXELS]
                                           [--classification cpu|gpu|both]
                                           [--outline] [--hysteresis BAND]
                                           [--hold-frames N] [--no-shadows]
Exit code 0 = pass.
"""

import argparse
import hashlib
import math
import os
import re
import signal
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from erhe_mcp import McpClient, check_true, report  # noqa: E402

REPO_ROOT      = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_PATH       = os.path.join(REPO_ROOT, "logs", "log.txt")
DEFAULT_EDITOR = os.path.join("build_xcode_metal_headless", "bin", "Debug", "editor")
TORUS_POSITION = [0.0, 0.8, -4.0]
LEVEL_COUNT    = 6
DOLLY_FACTOR   = 1.02  # each dolly step moves the camera 2 % farther
# The outline check's lod_threshold_pixels: large, so the coarsest level's
# pose is near enough for the torus to cover the pixels the levels differ in.
OUTLINE_THRESHOLD = 16.0


def launch_editor(editor_exe):
    exe = editor_exe if os.path.isabs(editor_exe) else os.path.join(REPO_ROOT, editor_exe)
    if not os.path.isfile(exe):
        raise RuntimeError(f"no editor at {exe}")
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    open(LOG_PATH, "w").close()
    env = dict(os.environ, ERHE_AI_DRIVER="1")
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    process = subprocess.Popen([exe], cwd=REPO_ROOT, env=env, creationflags=flags,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 300.0
    pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+) \(pid (\d+)")
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"editor exited with {process.returncode} during startup; see logs/log.txt")
        try:
            with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            matches = [m for m in pattern.finditer(text) if int(m.group(2)) == process.pid]
            if matches and ("Main loop: completed frame 12" in text):
                return process, int(matches[0].group(1))
        except OSError:
            pass
        time.sleep(1.0)
    process.kill()
    raise RuntimeError("editor did not become ready")


def box_distance(origin, center, extent):
    """The distance from origin to the closest point of the box (0 inside)."""
    total = 0.0
    for axis in range(3):
        gap = max(abs(center[axis] - origin[axis]) - extent[axis], 0.0)
        total += gap * gap
    return math.sqrt(total)


def predicted_level(distance, switch_distances):
    """The last level of the passing prefix: level L passes from d_L on."""
    level = 0
    for candidate, d in enumerate(switch_distances):
        if (candidate > 0) and (distance >= d) and (level == candidate - 1):
            level = candidate
    return level


class Dolly:
    def __init__(self, client, scene, threshold, band=0.0, hold_frames=300, shadows=True):
        self.c              = client
        self.shadows        = shadows
        self.scene          = scene
        self.threshold      = threshold
        self.band           = band
        self.hold_frames    = hold_frames
        self.classification = "cpu"

    def call(self, tool, args=None):
        return self.c.call(tool, args or {})

    def set_classification(self, classification):
        self.classification = classification
        reply = self.call("set_culling_classification", {"mode": classification})
        print(f"culling classification {classification}: {reply}")

    def setup(self):
        self.call("set_graphics_preset", {"lod_enabled": True, "lod_threshold_pixels": self.threshold, "lod_bias": 0, "lod_hysteresis_band": self.band})
        reply = self.call("create_shape", {
            "scene_name": self.scene, "shape": "torus", "name": "lod_dolly_torus",
            "position": TORUS_POSITION, "scale": 2.0, "motion_mode": "none"
        })
        self.torus = reply["node_id"]
        self.call("generate_lods", {"scene_name": self.scene, "node_id": self.torus, "settings": {"level_count": LEVEL_COUNT}})
        deadline = time.monotonic() + 120.0
        while time.monotonic() < deadline:
            status = self.call("get_async_status")
            if status.get("idle", False):
                break
            time.sleep(0.5)
        info = self.call("get_lod_info", {"scene_name": self.scene, "node_id": self.torus})
        levels = info["primitives"][0]["levels"]
        self.errors = [level["error"] for level in levels]
        check_true("torus has a 6-level chain", len(self.errors) == LEVEL_COUNT, f"errors {self.errors}")
        cameras = self.call("get_scene_cameras", {"scene_name": self.scene})
        camera_list = cameras.get("cameras", cameras if isinstance(cameras, list) else [])
        self.camera_node = camera_list[0]["node_id"] if "node_id" in camera_list[0] else camera_list[0]["id"]

    def selection(self, min_frame):
        """The capture of a frame classified after min_frame."""
        for _ in range(20):
            reply = self.call("get_lod_selection", {"scene_name": self.scene, "node_id": self.torus})
            if reply["frame_index"] > min_frame:
                return reply
        raise RuntimeError("get_lod_selection returned no newer frame")

    def place_camera(self, z):
        # The camera looks along -Z (identity rotation) at the torus.
        self.call("set_node_transform", {
            "scene_name": self.scene, "node_id": self.camera_node,
            "translation": [TORUS_POSITION[0], TORUS_POSITION[1], z],
            "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
        })

    def sample(self, z, last_frame):
        self.place_camera(z)
        # Two captures: the second classified a frame prepared after the move.
        first = self.selection(last_frame)
        reply = self.selection(first["frame_index"] + 1)
        origin = [TORUS_POSITION[0], TORUS_POSITION[1], z]
        columns = [c for c in reply["columns"] if all(abs(c["origin"][a] - origin[a]) < 1.0e-4 for a in range(3))]
        if len(columns) != 1:
            raise RuntimeError(f"no single LOD column at the camera position {origin}: {reply['columns']}")
        column = columns[0]
        slot   = reply["columns"].index(column)
        entries = [e for e in reply["entries"] if e["purpose"] == "color"]
        if len(entries) != 1:
            raise RuntimeError(f"expected one color entry: {reply['entries']}")
        entry = entries[0]
        distance = box_distance(column["origin"], entry["center"], entry["extent"])
        k = entry["max_axis_scale"] * column["projection_scale"]
        switch_distances = [(error * k) / self.threshold for error in self.errors]
        return reply["frame_index"], distance, entry["levels"][slot], switch_distances

    def dolly_pass(self, distances, switch_of, label):
        """Samples the level at each distance; switch_of(switch) gives the
        distances the level changes at in this direction."""
        observed = {}
        frame    = 0
        previous = None
        for step, distance_target in enumerate(distances):
            # The box's nearest face on +Z: the camera z that puts it at distance_target.
            z = TORUS_POSITION[2] + self.box_half_z + distance_target
            frame, distance, level, switch = self.sample(z, frame)
            switch = switch_of(switch)
            observed.setdefault(level, distance)
            expected = predicted_level(distance, switch)
            near_switch = any(abs(distance - d) <= (distance * (DOLLY_FACTOR - 1.0)) for d in switch[1:])
            if near_switch:
                check_true(f"{label} d={distance:.4f} level {level} within one step of a switch", level in (expected, expected - 1, expected + 1))
            elif level != expected:
                check_true(f"{label} d={distance:.4f} level {level} == predicted {expected}", False)
            if (previous is not None) and (level != previous[1]):
                # The switch to level happens between the previous step and this one.
                d_switch = switch[level] if level > previous[1] else switch[previous[1]]
                lo = min(previous[0], distance)
                hi = max(previous[0], distance)
                within = (lo - (lo * (DOLLY_FACTOR - 1.0))) <= d_switch <= (hi + (hi * (DOLLY_FACTOR - 1.0)))
                check_true(f"{label} switch {previous[1]} -> {level} between d={previous[0]:.4f} and d={distance:.4f} (predicted {d_switch:.4f})", within)
            previous = (distance, level)
            if (self.classification == "gpu") and ((step % 10) == 0):
                self.classification_check(f"{label} step {step}")
        check_true(f"{label}: every level of the chain was selected", sorted(observed.keys()) == list(range(LEVEL_COUNT)), f"first distance per level {observed}")

    def dolly(self):
        # Start just outside the torus box, end past the last switch distance.
        _, _, _, switch = self.sample(TORUS_POSITION[2] + 6.0, 0)
        start = max(0.5 * switch[1], 0.1)
        end   = 1.3 * switch[-1] / (1.0 - self.band)
        print(f"switch distances {[round(d, 4) for d in switch]}; band {self.band}; dolly {start:.3f} .. {end:.3f} m")
        distances = []
        distance_target = start
        while distance_target <= end:
            distances.append(distance_target)
            distance_target *= DOLLY_FACTOR
        # Outward: a coarser level once its error is within the coarsening
        # threshold, threshold * (1 - band): at d_L / (1 - band).
        self.dolly_pass(distances, lambda sw: [d / (1.0 - self.band) for d in sw], f"{self.classification} outward")
        if self.band > 0.0:
            # Inward: a finer level as soon as the level fails the threshold:
            # below d_L.
            self.dolly_pass(list(reversed(distances)), lambda sw: sw, f"{self.classification} inward")
        self.switch = switch

    def hold(self):
        """Stops inside each level's band, from either side; the level must
        stay unchanged for hold_frames classified frames."""
        _, _, _, switch = self.sample(TORUS_POSITION[2] + 6.0, 0)
        frame = 0
        for level in range(1, LEVEL_COUNT):
            d_switch = switch[level]
            d_coarse = d_switch / (1.0 - self.band)
            d_band   = 0.5 * (d_switch + d_coarse)
            # Just below the switch (level - 1 by the threshold) and just
            # past the coarsening edge (level by both thresholds).
            finer_pose  = 0.97 * d_switch
            coarse_pose = 1.03 * d_coarse
            for label, approach, kept in (("outward", finer_pose, level - 1), ("inward", coarse_pose, level)):
                frame, _, approach_level, _ = self.sample(TORUS_POSITION[2] + self.box_half_z + approach, frame)
                check_true(f"{self.classification} hold level {level} {label}: approach pose selects {kept}", approach_level == kept, f"selected {approach_level}")
                frame, distance, band_level, _ = self.sample(TORUS_POSITION[2] + self.box_half_z + d_band, frame)
                check_true(f"{self.classification} hold level {level} {label}: band pose d={distance:.4f} keeps {kept}", band_level == kept, f"selected {band_level}")
                if self.classification == "gpu":
                    self.classification_check(f"gpu hold level {level} {label}")
                first   = frame
                changed = []
                while frame < (first + self.hold_frames):
                    reply = self.selection(frame)
                    frame = reply["frame_index"]
                    # The viewport's color column (the shadow passes have
                    # columns of their own; the torus's shadow entry holds
                    # the color entry's level in the color column).
                    color_columns = [i for i, column in enumerate(reply["columns"]) if column.get("purpose", "color") == "color"]
                    entry  = [e for e in reply["entries"] if e["purpose"] == "color"][0]
                    shadow = [e for e in reply["entries"] if e["purpose"] == "shadow"]
                    levels = [entry["levels"][i] for i in color_columns]
                    shadow_levels = [shadow[0]["levels"][i] for i in color_columns] if shadow else levels
                    if (len(levels) != 1) or (levels[0] != kept) or (shadow_levels != levels):
                        changed.append((frame, levels))
                check_true(f"{self.classification} hold level {level} {label}: level {kept} for {frame - first} frames", not changed, f"changed {changed[:5]}")
                if self.classification == "gpu":
                    self.classification_check(f"gpu hold level {level} {label} after {frame - first} frames")

    def classification_check(self, label):
        reply = self.call("get_cull_classification_check", {"scene_name": self.scene})
        check_true(
            f"{label}: classification check 0 mismatches (masks, levels, commands)",
            (reply["mismatch_count"] == 0) and (reply["lod_mismatch_count"] == 0) and (reply["command_mismatch_count"] == 0) and (reply["lod_level_count"] > 0),
            f"mismatch {reply['mismatch_count']} lod {reply['lod_mismatch_count']}/{reply['lod_level_count']} command {reply['command_mismatch_count']} color jobs {reply['command_color_job_count']}"
        )

    def images(self, out_dir):
        """The automatic image hash per level (image poses)."""
        automatic = {}
        width, height, fov_y_degrees = 640, 400, 50.0
        image_projection_scale = height / (2.0 * math.tan(math.radians(fov_y_degrees) * 0.5))
        self.call("set_graphics_preset", {"shadow_enable": self.shadows})
        mas = self.max_axis_scale
        switch = [(error * mas * image_projection_scale) / self.threshold for error in self.errors]
        for level in range(LEVEL_COUNT):
            # Past the coarsening edge of L (any history selects L) and
            # before the switch to L + 1.
            low  = (switch[level] / (1.0 - self.band)) if level > 0 else 0.5 * switch[1]
            high = switch[level + 1] if (level + 1) < LEVEL_COUNT else 1.5 * switch[-1] / (1.0 - self.band)
            distance = math.sqrt(low * high) if level > 0 else 0.5 * (low + high)
            eye = [TORUS_POSITION[0], TORUS_POSITION[1], TORUS_POSITION[2] + self.box_half_z + distance]
            camera = {"eye": eye, "target": TORUS_POSITION, "fov_y_degrees": fov_y_degrees, "near": 0.03, "far": 2000.0}
            hashes = []
            # automatic, forced at the level, and (negative control: the
            # comparison sees a level change at this pose) forced at the
            # next level.
            other = (level + 1) % LEVEL_COUNT
            for mode, forced_level in (("automatic", None), ("forced", level), ("other", other)):
                if forced_level is not None:
                    for prop, value in (("Lod.apply_lod", "true"), ("Lod.mode", "Forced"), ("Lod.forced_level", str(forced_level))):
                        self.call("set_item_property", {"item_id": self.torus, "property": prop, "value": value})
                path = os.path.join(out_dir, f"lod_dolly_{self.classification}_level{level}_{mode}.png")
                self.call("render_scene_image", {"scene": self.scene, "camera": camera, "width": width, "height": height, "output": "png", "path": path})
                with open(path, "rb") as handle:
                    hashes.append(hashlib.md5(handle.read()).hexdigest())
                if (mode == "automatic") and (self.classification == "gpu"):
                    self.classification_check(f"gpu level {level} pose")
            self.call("set_item_property", {"item_id": self.torus, "property": "Lod.apply_lod", "value": "false"})
            check_true(f"image at automatic level {level} (d={distance:.3f}) equals forced level {level}", hashes[0] == hashes[1], f"{hashes[:2]}")
            check_true(f"image at forced level {other} differs from level {level} at that pose", hashes[2] != hashes[1])
            automatic[level] = hashes[0]
        self.call("set_graphics_preset", {"shadow_enable": True})
        return automatic

    def viewport_crop(self, path):
        """capture_screenshot cropped to the inside of the viewport window; the crop's md5."""
        from PIL import Image
        self.call("capture_screenshot", {"path": path})
        windows = self.call("get_imgui_windows").get("windows", [])
        # The viewport window's content region (its image).
        viewports = [w for w in windows if ("Viewport_window::imgui()" in str(w.get("name", ""))) and not w.get("hidden")]
        if not viewports:
            raise RuntimeError(f"no viewport window: {[w.get('name') for w in windows]}")
        rect  = max(viewports, key=lambda w: w["width"] * w["height"])
        hosts = self.call("get_imgui_hosts").get("hosts", [])
        host  = next((h for h in hosts if h.get("default")), None)
        image = Image.open(path).convert("RGB")
        sx = image.width  / float(host["width"])  if host else 1.0
        sy = image.height / float(host["height"]) if host else 1.0
        box  = (int(rect["x"] * sx), int(rect["y"] * sy), int((rect["x"] + rect["width"]) * sx), int((rect["y"] + rect["height"]) * sy))
        crop = image.crop(box)
        crop.save(path.replace(".png", "_crop.png"))
        return hashlib.md5(crop.tobytes()).hexdigest()

    def outline(self, out_dir):
        """The selected torus's viewport crop at automatic level L equals the crop with Lod forced to L."""
        self.call("select_items", {"scene_name": self.scene, "ids": [self.torus]})
        saved_threshold = self.threshold
        self.threshold  = OUTLINE_THRESHOLD
        self.call("set_graphics_preset", {"shadow_enable": self.shadows, "lod_threshold_pixels": self.threshold})
        # The viewport column's switch distances (sample() reads the
        # column at the camera position).
        _, _, _, switch = self.sample(TORUS_POSITION[2] + 6.0, 0)
        frame = 0
        for level in range(LEVEL_COUNT):
            low      = (switch[level] / (1.0 - self.band)) if level > 0 else 0.5 * switch[1]
            high     = switch[level + 1] if (level + 1) < LEVEL_COUNT else 1.5 * switch[-1] / (1.0 - self.band)
            distance = math.sqrt(low * high) if level > 0 else 0.5 * (low + high)
            z = TORUS_POSITION[2] + self.box_half_z + distance
            frame, measured, selected, _ = self.sample(z, frame)
            check_true(f"outline pose d={measured:.3f} selects level {level}", selected == level, f"selected {selected}")
            for fill in (True, False):
                self.call("set_viewport_style", {"style": "selected", "polygon_fill": fill})
                hashes = []
                other = (level + 1) % LEVEL_COUNT
                for mode, forced_level in (("automatic", None), ("forced", level), ("other", other)):
                    if forced_level is not None:
                        for prop, value in (("Lod.apply_lod", "true"), ("Lod.mode", "Forced"), ("Lod.forced_level", str(forced_level))):
                            self.call("set_item_property", {"item_id": self.torus, "property": prop, "value": value})
                    # Frames rendered after the edit (the selection reads
                    # the frame classified after the move).
                    frame = self.selection(frame)["frame_index"]
                    frame = self.selection(frame + 1)["frame_index"]
                    path = os.path.join(out_dir, f"lod_outline_{self.classification}_level{level}_fill{int(fill)}_{mode}.png")
                    hashes.append(self.viewport_crop(path))
                self.call("set_item_property", {"item_id": self.torus, "property": "Lod.apply_lod", "value": "false"})
                check_true(f"outline at automatic level {level} (fill {fill}) equals forced level {level}", hashes[0] == hashes[1], f"{hashes[:2]}")
                check_true(f"outline at forced level {other} (fill {fill}) differs from level {level}", hashes[2] != hashes[1])
            reply = self.call("get_lod_info", {"scene_name": self.scene, "node_id": self.torus})
            drawn = [d for d in reply.get("drawn", []) if d["source"] == "selected"]
            check_true(f"get_lod_info reports the viewport's drawn level {level}", any(d["level"] == level for d in drawn), f"drawn {reply.get('drawn')} frame {reply.get('drawn_frame_index')}")
        self.call("set_viewport_style", {"style": "selected", "polygon_fill": True})
        self.call("select_items", {"scene_name": self.scene, "ids": []})
        self.threshold = saved_threshold
        self.call("set_graphics_preset", {"shadow_enable": True, "lod_threshold_pixels": self.threshold})

    def measure_box(self):
        reply = self.selection(0)
        entry = [e for e in reply["entries"] if e["purpose"] == "color"][0]
        self.box_half_z     = entry["extent"][2]
        self.max_axis_scale = entry["max_axis_scale"]
        check_true("the selection is active", reply["active"])
        check_true("no view past the column limits", reply["column_overflow_count"] == 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=DEFAULT_EDITOR)
    parser.add_argument("--threshold", type=float, default=4.0, help="lod_threshold_pixels")
    parser.add_argument("--out", default=os.path.join(REPO_ROOT, "logs", "lod_dolly"))
    parser.add_argument("--classification", choices=("cpu", "gpu", "both"), default="both",
                        help="culling_classification setting(s) to run (both: cpu, then gpu, and the images compared)")
    parser.add_argument("--outline", action="store_true",
                        help="also compare the selected torus's viewport at automatic and forced levels (selection outline)")
    parser.add_argument("--hysteresis", type=float, default=0.0,
                        help="lod_hysteresis_band (0: none); > 0 adds the inward dolly and the band holds")
    parser.add_argument("--hold-frames", type=int, default=300, help="classified frames each band hold lasts")
    parser.add_argument("--no-shadows", action="store_true", help="shadows off for the image and outline comparisons")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    process, port = launch_editor(args.editor)
    try:
        client = McpClient(port)
        scene  = client.call("list_scenes")["scenes"][0]["name"]
        dolly  = Dolly(client, scene, args.threshold, args.hysteresis, args.hold_frames, shadows=not args.no_shadows)
        classifications = ["cpu", "gpu"] if (args.classification == "both") else [args.classification]
        dolly.set_classification(classifications[0])
        dolly.setup()
        dolly.measure_box()
        images = {}
        for classification in classifications:
            dolly.set_classification(classification)
            print(f"--- culling classification {classification}")
            dolly.dolly()
            if args.hysteresis > 0.0:
                dolly.hold()
            images[classification] = dolly.images(args.out)
            if args.outline:
                dolly.outline(args.out)
        if len(classifications) == 2:
            for level, cpu_hash in images["cpu"].items():
                gpu_hash = images["gpu"][level]
                check_true(f"automatic image at level {level}: cpu == gpu", cpu_hash == gpu_hash, f"cpu {cpu_hash} gpu {gpu_hash}")
    finally:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(60)
        except subprocess.TimeoutExpired:
            process.kill()
    return report()


if __name__ == "__main__":
    sys.exit(main())
