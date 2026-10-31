#!/usr/bin/env python3
"""Measure the standard shader's fp16 paths against the fp32 chain on the fp16 test stations.

doc/plans/shader_fp16.md (sections 2 - 4; commands in doc/testing.md "Shader
fp16 verification"): builds each station of
scripts/creations/creation_26_fp16_test_rooms.py (imported: every material,
mesh, light and camera comes from that module) in a headless editor, sets the
low-end graphics configuration and, per case, renders the fp32 chain
(`set_graphics_preset` shader_fp16_paths 0) once and every mask of the matrix
with `render_scene_image` (linear, rgba32f, msaa 0), applies the gates,
prints a PASS / FAIL table, writes logs/shader_fp16/<timestamp>/summary.json
and exits non-zero on a FAIL with --enforce.

Usage:
    py -3 scripts/shader_fp16_verify.py [--matrix alone|chains|full] [--mask N[,N...]]
                                        [--station NAME[,NAME]] [--case NAME[,NAME]]
                                        [--save-images none|failing|all] [--enforce]
                                        [--reuse] [--port N] [--editor PATH]
                                        [--shader-variant-browser DIR] [--allow-no-fp16]

Masks (erhe::scene_renderer::Fp16_path): 1 material (P1), 2 BRDF (P2),
4 accumulate (P3), 8 attenuation (P4), 16 visibility (P5). The matrix is:
  alone:  each path alone (1, 2, 4, 8, 16);
  chains: alone + every chain prefix in build order (3, 7, 15, 31 = the
          low-end set) + the named feeding pairs (6 = P2+P3, 12 = P3+P4,
          20 = P3+P5) (the default);
  full:   every mask 1 .. 31.
--mask replaces the matrix with the given masks. Mask 0 is always rendered
twice: once as the reference and once as the row "self", which must report
zero error (the render is deterministic).

Gates per case (plan section 2), against the fp32 render of the same
camera:
  G-display: the linear image (exposure applied) clamped to [0, 1] and
             sRGB-encoded to 8 bits, as a display shows it: for 99.9% of
             pixels every channel is within 4/255 of the reference, and
             no channel is off by more than 16/255.
  G-finite:  no NaN or infinity.
  G-dark:    no pixel the reference lights above 2^-10 goes to zero.
The fp16 ulp statistics (relative error where the reference is at least
2^-4, absolute error below) are reported per row for the analysis but are
not gated: a chain of fp16 operations rounds by up to 2^-12 each, so an
ulp-scale gate rejects any fp16 computation of more than a few operations.
A case the station marks ungated (roughness below the fp16 floor) is
reported with its numbers but has no verdict.

The run sets the low-end configuration through set_graphics_preset (hard
filter, depth technique, one shadow-mapped light per type, loops shading,
specular anti-aliasing off) before the first render; set_graphics_preset is
session-only, so the stored preset is untouched. The device must report
shaderFloat16 (the reply's shader_float16) or every mask renders the fp32
chain and the gates say nothing; --allow-no-fp16 runs anyway.

Without --reuse the script launches its own headless editor
(ERHE_AI_DRIVER=1, no default scene; --shader-variant-browser DIR records the
compiled variants for scripts/shader_analytics.py, the G-cost gate) and
stops it at the end. With --reuse it drives the editor already listening on
--port. The headless editor needs the usual environment (on Linux with
several ICDs, VK_DRIVER_FILES).
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

import numpy as np

SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT   = os.path.dirname(SCRIPTS_DIR)
sys.path.insert(0, SCRIPTS_DIR)
sys.path.insert(0, os.path.join(SCRIPTS_DIR, "creations"))

from common import Creation  # noqa: E402
import creation_26_fp16_test_rooms as rooms  # noqa: E402

DEFAULT_EDITOR = os.path.join("build_vs2026_vulkan_headless", "bin", "Debug", "editor.exe")

PATH_NAMES = {1: "material", 2: "brdf", 4: "accumulate", 8: "attenuation", 16: "visibility"}
LOW_END_SET = 31
MATRICES = {
    "alone":  [1, 2, 4, 8, 16],
    "chains": [1, 2, 4, 8, 16, 3, 7, 15, 31, 6, 12, 20],
    "full":   list(range(1, 32)),
}

# The low-end configuration of the plan: one shadow-mapped directional
# light, hard filter, depth technique, loops shading.
LOW_END_PRESET = {
    "shadow_enable": True,
    "shadow_filter": "hard",
    "shadow_technique": "depth",
    "shadow_bias": "receiver_plane",
    "shadow_depth_bits": 16,
    "directional_shadow_light_count": 1,
    "directional_unshadowed_light_count": 0,
    "spot_shadow_light_count": 1,
    "spot_unshadowed_light_count": 0,
    "point_shadow_light_count": 1,
    "point_unshadowed_light_count": 0,
    "light_shading": "loops",
    "specular_anti_aliasing": "off",
}

# Gate constants (plan section 2).
DISPLAY_LIMIT    = 4            # 8-bit sRGB steps, 99.9% of pixels
DISPLAY_MAX      = 16           # 8-bit sRGB steps, every pixel
FRACTION         = 0.999
DARK_THRESHOLD   = 2.0 ** -10
# Reported fp16 ulp statistics (not gated).
BRIGHT_THRESHOLD = 2.0 ** -4
REL_LIMIT        = 2.0 ** -10
ABS_LIMIT        = 2.0 ** -14

LUMINANCE = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)


def mask_name(mask):
    if mask == 0:
        return "fp32"
    return "+".join(PATH_NAMES[bit] for bit in sorted(PATH_NAMES) if mask & bit) + f" ({mask})"


def launch_editor(editor_exe, port, extra_args):
    """Launch a headless editor without a default scene; return (process,
    port). The editor is identified by its pid in its 'MCP server:
    listening' log line."""
    exe = editor_exe if os.path.isabs(editor_exe) else os.path.join(REPO_ROOT, editor_exe)
    if not os.path.isfile(exe):
        raise RuntimeError(f"no editor at {exe}")
    log_path = os.path.join(REPO_ROOT, "logs", "log.txt")
    env = dict(os.environ, ERHE_AI_DRIVER="1", ERHE_MCP_PORT=str(port))
    flags = 0
    if os.name == "nt":
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NO_WINDOW
    command = [exe, "--commands", os.path.join("config", "editor", "commands_empty.json")] + list(extra_args)
    process = subprocess.Popen(command, cwd=REPO_ROOT, env=env, creationflags=flags,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+) \(pid (\d+)")
    deadline = time.monotonic() + 300.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"editor exited with {process.returncode} during startup; see logs/log.txt")
        try:
            with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
        except OSError:
            text = ""
        for match in pattern.finditer(text):
            if int(match.group(2)) == process.pid:
                if "Main loop: completed frame 12" in text[match.end():]:
                    return process, int(match.group(1))
        time.sleep(1.0)
    process.kill()
    raise RuntimeError("editor did not become ready")


def stop_editor(c, process):
    try:
        if c is not None:
            c.client.call("request_exit")
    except Exception:  # noqa: BLE001 - the editor may already be gone
        pass
    try:
        process.wait(timeout=60.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10.0)


def read_pfm(path):
    with open(path, "rb") as handle:
        kind = handle.readline().strip()
        width, height = (int(v) for v in handle.readline().split())
        scale = float(handle.readline().strip())
        data = np.fromfile(handle, dtype="<f4" if scale < 0 else ">f4")
    channels = 3 if kind == b"PF" else 1
    image = data.reshape(height, width, channels)
    return np.flipud(image).copy()


def write_ppm(path, rgb):
    """8-bit PPM of an [0, 1] float image (no image library needed)."""
    data = np.clip(rgb * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    with open(path, "wb") as handle:
        handle.write(f"P6\n{data.shape[1]} {data.shape[0]}\n255\n".encode("ascii"))
        handle.write(data.tobytes())


def display8(linear):
    """The 8-bit sRGB display value of a linear image clamped to [0, 1]."""
    v = np.clip(linear, 0.0, 1.0)
    srgb = np.where(v <= 0.0031308, 12.92 * v, 1.055 * np.power(v, 1.0 / 2.4) - 0.055)
    return np.floor(srgb * 255.0 + 0.5)


def compare(reference, test):
    """The gate statistics of `test` against `reference` (H x W x 3 fp32)."""
    ref = reference.astype(np.float64)
    tst = test.astype(np.float64)
    finite = bool(np.isfinite(tst).all())
    tst_safe = np.where(np.isfinite(tst), tst, 0.0)
    display_delta = np.abs(display8(tst_safe) - display8(ref)).max(axis=2)
    display_fraction = float((display_delta <= DISPLAY_LIMIT).mean())
    display_max = int(display_delta.max())
    display_where = None
    if display_max > 0:
        y, x = divmod(int(np.argmax(display_delta)), ref.shape[1])
        display_where = {"x": x, "y": y, "reference": [float(v) for v in ref[y, x]], "test": [float(v) for v in tst_safe[y, x]]}
    ref_values = np.concatenate([ref, (ref @ LUMINANCE)[..., None]], axis=2)
    tst_values = np.concatenate([tst_safe, (tst_safe @ LUMINANCE)[..., None]], axis=2)
    error = np.abs(tst_values - ref_values)
    bright = ref_values >= BRIGHT_THRESHOLD
    relative = np.where(bright, error / np.maximum(ref_values, 1e-30), 0.0)
    ok = np.where(bright, relative <= REL_LIMIT, error <= ABS_LIMIT)
    pixel_ok = ok.all(axis=2)
    fraction = float(pixel_ok.mean())
    rel_max = float(relative[bright].max()) if bright.any() else 0.0
    abs_max = float(error[~bright].max()) if (~bright).any() else 0.0
    ref_lum = ref_values[..., 3]
    tst_lum = tst_values[..., 3]
    dark_lost = int(((ref_lum > DARK_THRESHOLD) & (tst_lum == 0.0)).sum())
    where = None
    if bright.any():
        flat = np.argmax(np.where(bright, relative, -1.0).max(axis=2))
        y, x = divmod(int(flat), ref.shape[1])
        where = {"x": x, "y": y, "reference": [float(v) for v in ref[y, x]], "test": [float(v) for v in tst_safe[y, x]]}
    where_dark = None
    if (~bright).any():
        flat = np.argmax(np.where(~bright, error, -1.0).max(axis=2))
        y, x = divmod(int(flat), ref.shape[1])
        where_dark = {"x": x, "y": y, "reference": [float(v) for v in ref[y, x]], "test": [float(v) for v in tst_safe[y, x]]}
    changed = float((error.max(axis=2) > 0.0).mean())
    # Relative error percentile over the pixels where the relative measure
    # applies (informative, the fraction above is the gate).
    rel_p999 = float(np.quantile(relative.max(axis=2)[bright.any(axis=2)], 0.999)) if bright.any() else 0.0
    return {
        "finite": finite,
        "display_fraction_within": display_fraction,
        "display_delta_max": display_max,
        "display_worst": display_where,
        "g_display": (display_fraction >= FRACTION) and (display_max <= DISPLAY_MAX),
        "fraction_within_limits": fraction,
        "relative_error_max": rel_max,
        "relative_error_p999": rel_p999,
        "absolute_error_max_dark": abs_max,
        "dark_pixels_lost": dark_lost,
        "changed_fraction": changed,
        "worst": where,
        "worst_dark": where_dark,
        "g_finite": finite,
        "g_dark": dark_lost == 0,
    }


class Runner:
    def __init__(self, c, out_dir, save_images):
        self.c = c
        self.out_dir = out_dir
        self.save_images = save_images
        self.tmp_dir = os.path.join(out_dir, "tmp")
        os.makedirs(self.tmp_dir, exist_ok=True)
        self.render_seconds = []

    def set_mask(self, mask):
        reply = self.c.call("set_graphics_preset", dict(LOW_END_PRESET, shader_fp16_paths=int(mask)))
        if int(reply.get("shader_fp16_paths", -1)) != int(mask):
            raise RuntimeError(f"set_graphics_preset: shader_fp16_paths = {reply.get('shader_fp16_paths')}, requested {mask}")
        return reply

    def render(self, case_, tag):
        path = os.path.join(self.tmp_dir, f"{tag}.pfm")
        args = {"scene": self.c.scene, "camera": case_["camera"], "width": rooms.IMAGE_W, "height": rooms.IMAGE_H,
                "output": "linear", "color_format": "rgba32f", "msaa_samples": 0,
                "path": os.path.relpath(path, REPO_ROOT).replace(os.sep, "/")}
        started = time.time()
        self.c.call("render_scene_image", args)
        image = read_pfm(path)
        self.render_seconds.append(time.time() - started)
        return image

    def save(self, station, case_, mask, reference, test, result):
        if self.save_images == "none":
            return
        gated = case_["gated"]
        failing = gated and not (result["g_display"] and result["g_finite"] and result["g_dark"])
        if (self.save_images == "failing") and not failing:
            return
        base = os.path.join(self.out_dir, f"{station}_{case_['name']}_{mask}")
        error = np.abs(test.astype(np.float64) - reference.astype(np.float64))
        scale = max(float(error.max()), 1e-30)
        write_ppm(base + "_reference.ppm", reference / max(float(reference.max()), 1e-30))
        write_ppm(base + "_error.ppm", error / scale)
        result["images"] = {"reference": base + "_reference.ppm", "error": base + "_error.ppm", "error_scale": scale}


def run(args):
    stamp = time.strftime("%Y%m%d_%H%M%S")
    out_dir = os.path.join(REPO_ROOT, "logs", "shader_fp16", stamp)
    os.makedirs(out_dir, exist_ok=True)
    masks = [int(m) for m in args.mask.split(",")] if args.mask else MATRICES[args.matrix]
    station_names = list(rooms.STATIONS) if args.station == "all" else args.station.split(",")
    for name in station_names:
        if name not in rooms.STATIONS:
            raise SystemExit(f"unknown station {name!r}; one of {', '.join(rooms.STATIONS)}")
    case_filter = set(args.case.split(",")) if args.case else None

    process = None
    c = None
    rows = []
    summary = {"timestamp": stamp, "masks": masks, "stations": station_names, "rows": rows}
    try:
        port = args.port
        if not args.reuse:
            extra = ["--shader-variant-browser", args.shader_variant_browser] if args.shader_variant_browser else []
            process, port = launch_editor(args.editor, args.port, extra)
        c = Creation("shader_fp16_verify", port=port, pause_s=0.0, reuse=True, manage_windows=False)
        runner = Runner(c, out_dir, args.save_images)
        headlight = c.call("set_graphics_settings", {}).get("headlight_when_unlit", True)
        c.mutate("set_graphics_settings", {"headlight_when_unlit": False})
        try:
            reply = runner.set_mask(0)
            summary["shader_float16"] = bool(reply.get("shader_float16", False))
            if not summary["shader_float16"]:
                message = "the device reports no shaderFloat16: every mask renders the fp32 chain"
                if not args.allow_no_fp16:
                    raise SystemExit(message + " (--allow-no-fp16 runs anyway)")
                print("WARNING: " + message)
            for name in station_names:
                station = rooms.STATIONS[name]
                rooms.build_station(c, name)
                for case_ in station["cases"]:
                    if case_filter and (case_["name"] not in case_filter):
                        continue
                    rooms.apply_light(c, case_["light"])
                    c.settle()
                    runner.set_mask(0)
                    reference = runner.render(case_, "reference")
                    self_image = runner.render(case_, "self")
                    for mask, image in [(0, self_image)] + [(m, None) for m in masks]:
                        if image is None:
                            runner.set_mask(mask)
                            image = runner.render(case_, f"mask_{mask}")
                        result = compare(reference, image)
                        runner.save(name, case_, mask, reference, image, result)
                        verdict = None
                        if case_["gated"]:
                            verdict = "PASS" if (result["g_display"] and result["g_finite"] and result["g_dark"]) else "FAIL"
                        row = {"station": name, "case": case_["name"], "mask": mask, "mask_name": mask_name(mask) if mask else "self",
                               "gated": case_["gated"], "note": case_["note"], "verdict": verdict}
                        row.update(result)
                        rows.append(row)
                        print_row(row)
            c.close_all_scenes()
        finally:
            c.mutate("set_graphics_settings", {"headlight_when_unlit": bool(headlight)})
    finally:
        if process is not None:
            stop_editor(c, process)
    summary["render_seconds_mean"] = float(np.mean(runner.render_seconds)) if rows else 0.0
    with open(os.path.join(out_dir, "summary.json"), "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
    print_table(rows)
    failures = sum(1 for row in rows if row["verdict"] == "FAIL")
    print(f"\n{len(rows)} row(s), {failures} FAIL; results in {os.path.relpath(out_dir, REPO_ROOT)}/summary.json")
    return failures


def print_row(row):
    verdict = row["verdict"] or "info"
    print(f"{verdict:4} {row['station']:19} {row['case']:34} {row['mask_name']:44} "
          f"display within {100.0 * row['display_fraction_within']:7.3f}% max {row['display_delta_max']:3}/255  "
          f"ulp: rel max {row['relative_error_max']:.3e} abs max {row['absolute_error_max_dark']:.3e}  "
          f"changed {100.0 * row['changed_fraction']:6.2f}%  dark lost {row['dark_pixels_lost']}"
          + ("" if row["finite"] else "  NON-FINITE"))


def print_table(rows):
    """Per mask: how many gated cases pass, and the worst relative error."""
    print("\nPer mask (gated cases):")
    masks = sorted({row["mask"] for row in rows})
    for mask in masks:
        gated = [row for row in rows if (row["mask"] == mask) and row["gated"]]
        passed = sum(1 for row in gated if row["verdict"] == "PASS")
        worst_display = max((row["display_delta_max"] for row in gated), default=0)
        worst_within = min((row["display_fraction_within"] for row in gated), default=1.0)
        worst = max((row["relative_error_max"] for row in gated), default=0.0)
        name = mask_name(mask) if mask else "self"
        print(f"  {name:44} {passed:3}/{len(gated):<3} PASS  display max {worst_display:3}/255 within {100.0 * worst_within:7.3f}%  ulp rel max {worst:.3e}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--matrix", choices=sorted(MATRICES), default="chains")
    parser.add_argument("--mask", default=None, help="comma separated masks replacing the matrix")
    parser.add_argument("--station", default="all", help="station name(s), comma separated, or all")
    parser.add_argument("--case", default=None, help="case name(s), comma separated")
    parser.add_argument("--save-images", choices=["none", "failing", "all"], default="failing")
    parser.add_argument("--enforce", action="store_true", help="exit non-zero when a gate FAILs")
    parser.add_argument("--reuse", action="store_true", help="drive the editor already running on --port")
    parser.add_argument("--port", type=int, default=3743)
    parser.add_argument("--editor", default=DEFAULT_EDITOR)
    parser.add_argument("--shader-variant-browser", default=None, metavar="DIR",
                        help="record the compiled variants into DIR (launched editor only)")
    parser.add_argument("--allow-no-fp16", action="store_true")
    args = parser.parse_args()
    failures = run(args)
    if args.enforce and failures:
        sys.exit(1)


if __name__ == "__main__":
    main()
