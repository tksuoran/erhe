#!/usr/bin/env python3
"""Measure an indirect diffuse producer on the GI test stations.

doc/plans/radiance_cascades.md section 10: builds each station of
scripts/creations/creation_24_gi_test_rooms.py in the headless editor, selects
the indirect diffuse source, waits until the field has converged and measures
the section 10 metrics with the MCP tool `sample_indirect_diffuse` (linear
float irradiance at the station's world sample points, doc/editor/ddgi.md
"Irradiance queries") and the cost from `get_indirect_diffuse_stats`.
The source is selected with the MCP tool `set_indirect_diffuse`. A source
that produces no field yet (radiance cascades before the reduce pass,
doc/plans/radiance_cascades.md phase 4) builds its layout - printed per
station and stored under "radiance_cascades" in the JSON record - and is
measured as the flat ambient term it leaves in place ("sampled": "ambient").

Usage:
    py -3 scripts/gi_verify.py [--station NAME|all] [--source ambient|ddgi|radiance_cascades]
                               [--compare A,B] [--runs N] [--enforce]
                               [--screenshot-reference DIR | --screenshot-compare DIR]
                               [--screenshot-source render|window]
                               [--reuse] [--port N] [--editor PATH]

Every metric is the WORST value over --runs full runs. Output: a table on
stdout and logs/gi_verify/<source>_<timestamp>.json (<a>_vs_<b> with
--compare); station view screenshots go to logs/gi_verify/<same name>/.

Gates are section 10's, written for radiance cascades; for every source the
table reports PASS / FAIL against them, and the exit code is non-zero on a
FAIL only with --enforce. --compare runs both sources per station back to
back in one editor session and prints both columns plus b / a ratios (the
relative gates and the section 8 cost budget).

Screenshots: --screenshot-reference DIR captures every station view into
DIR/<source>/; --screenshot-compare DIR captures again and compares the
image against the reference pixel by pixel, reporting the number of
differing pixels, the largest channel difference and the pixels above
--screenshot-tolerance per view (default 0 for ambient; 1 for a traced
field, whose per-update random rays measured as at most 1 level between two
runs of one build). All capture goes through capture_view(). The default
--screenshot-source render uses the MCP tool render_scene_image: an
offscreen render of the station camera (eye, target, fov) at
SHOT_WIDTH x SHOT_HEIGHT that no ImGui window or viewport size can affect
(doc/editor/rendergraph.md "Scene image capture"). --screenshot-source
window captures the whole editor window with capture_screenshot and
compares the viewport region only; that image depends on the editor's ImGui
layout, so reference and compare runs need the same layout.

Reference: after each station is built, the MCP tool
`reference_indirect_diffuse` estimates the ground-truth irradiance at every
sample point once (REFERENCE_RAYS cosine-distributed rays per point, the
exact light transport of a DDGI probe ray, doc/editor/ddgi.md "Reference
irradiance"; it does not depend on the source). Per sample group the
table then reports `ref.<group>.ref_mean` (reference group mean luminance),
`ref.<group>.mean_rel_err` (|measured group mean - reference group mean| /
max(reference group mean, floor)) and `ref.<group>.worst_point_rel_err`
(the largest per-point |measured - reference| / max(reference, floor)), and
per station `ref.worst_group_mean_rel_err`. The floor is
REFERENCE_FLOOR_FRACTION of the station's brightest reference group mean (the
lit floor or wall): near-black points are judged by their absolute error
against that level instead of blowing up the relative error.
`ref.<group>.ref_point_rel_se` is the largest per-point reference standard
error on the same scale - the noise level below which an error means
nothing.

Convergence: a check averages CHECK_SAMPLES samples; the field has converged
when every group mean changed by < 0.5 % (or, where per-update noise is
larger, by < 3 standard errors) between two checks one relaxation time
(max(full refresh, 1 / (1 - hysteresis)) updates) apart. Quality metrics
average MEASURE_SAMPLES samples per point.

Without --reuse the script launches its own headless editor
(ERHE_AI_DRIVER=1, empty startup commands), backs up the editor config files
it could touch and restores them byte-exactly afterwards, and stops only the
editor process it launched.
"""

import argparse
import datetime
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPTS_DIR)
sys.path.insert(0, SCRIPTS_DIR)
sys.path.insert(0, os.path.join(SCRIPTS_DIR, "creations"))

import creation_24_gi_test_rooms as rooms  # noqa: E402
from common import Creation  # noqa: E402

DEFAULT_EDITOR = os.path.join("build_vs2026_vulkan_headless", "bin", "Debug", "editor.exe")
LOG_PATH = os.path.join(REPO_ROOT, "logs", "log.txt")
OUT_DIR = os.path.join(REPO_ROOT, "logs", "gi_verify")
SOURCES = ["ambient", "ddgi", "radiance_cascades"]
CONFIG_FILES = [
    "config/editor/desktop_window_imgui_host_imgui.ini",
    "config/editor/desktop_windows.json",
    "config/editor/editor_settings.json",
    "config/editor/erhe_graphics.json",
    "config/editor/default_viewport_config.json",
]

# Convergence: the group means change by less than this between two checks
# spaced a full relaxation apart (see wait_converged).
CONVERGED_RELATIVE_CHANGE = 0.005
CONVERGE_DEADLINE_S = 180.0
# Samples averaged per convergence check.
CHECK_SAMPLES = 4
# Samples averaged per point for the quality metrics.
MEASURE_SAMPLES = 8
# Groups dimmer than this fraction of the brightest group are compared with
# the absolute tolerance this level gives (see wait_converged).
DIM_GROUP_FRACTION = 0.01
# Dynamic events: settled when the group mean stays within this of its
# final value.
SETTLED_RELATIVE = 0.05
SETTLE_WINDOW_RELAXATIONS = 10
# Screenshot compare: largest per-channel 8-bit difference that still
# matches. ambient is deterministic (0); a traced field re-traces random rays
# every update, which measured as at most 1 level between two runs of the
# same build (DDGI, cornell).
SCREENSHOT_TOLERANCE = {"ambient": 0, "ddgi": 1, "radiance_cascades": 1}
# render_scene_image size and the station camera's clip / shadow range
# (creation_24_gi_test_rooms: build_station sets shadow range 30 and far 60,
# place_view sets near 0.02).
SHOT_WIDTH = 1280
SHOT_HEIGHT = 720
STATION_CAMERA = {"near": 0.02, "far": 60.0, "shadow_range": 30.0}
# Noise: samples at this many distinct field updates.
NOISE_SAMPLES = 30
# Reference irradiance (reference_indirect_diffuse): rays per sample point,
# fixed seed (the same station gives the same reference in every run), and
# the error floor as a fraction of the station's brightest reference group.
REFERENCE_RAYS = 16384
REFERENCE_SEED = 1
REFERENCE_FLOOR_FRACTION = 0.01

# Section 10 gates. "min" gates pass when value >= limit, "max" when <= limit.
GATES = {
    "leak_pair.leak_ratio":            ("max", 0.01),
    "probe_offset_sweep.worst_leak":   ("max", 0.02),
    "cornell.red_strip_r_over_g":      ("min", 1.2),
    "cornell.green_strip_g_over_r":    ("min", 1.2),
    "emissive_only.panel_1.0_vs_median":  ("min", 1.0),
    "emissive_only.panel_0.25_vs_median": ("min", 1.0),
    "corridor.monotonic_violations":   ("max", 0),
}
# The relative section 10 gates (accuracy against the reference, corridor
# max_log_second_difference, convergence, noise, cost: RC against DDGI) are
# read from the --compare table.

# Worst direction per metric kind: metrics whose name matches a key take
# min over runs, all others max.
LOWER_IS_WORSE = ("placement", "min_over_median", "_over_", "_vs_median", "shadowed", "sunlit")


def luminance(rgb):
    return (0.2126 * rgb[0]) + (0.7152 * rgb[1]) + (0.0722 * rgb[2])


def ratio(a, b):
    if b <= 0.0:
        return None if a <= 0.0 else math.inf
    return a / b


# --- editor process + config hygiene -------------------------------------------

class Config_backup:
    """Byte-exact copies of the editor config files an editor run could write."""

    def __init__(self):
        self.directory = tempfile.mkdtemp(prefix="gi_verify_config_")
        self.saved = []
        for relative in CONFIG_FILES:
            source = os.path.join(REPO_ROOT, relative)
            if os.path.isfile(source):
                target = os.path.join(self.directory, relative.replace("/", "__"))
                shutil.copyfile(source, target)
                self.saved.append((relative, target))

    def restore(self):
        for relative, target in self.saved:
            destination = os.path.join(REPO_ROOT, relative)
            with open(target, "rb") as handle:
                data = handle.read()
            current = None
            if os.path.isfile(destination):
                with open(destination, "rb") as handle:
                    current = handle.read()
            if current != data:
                with open(destination, "wb") as handle:
                    handle.write(data)
                print(f"restored {relative}")
        shutil.rmtree(self.directory, ignore_errors=True)


def launch_editor(editor_exe):
    """Launch a headless editor without a default scene; return (process, port)."""
    exe = editor_exe if os.path.isabs(editor_exe) else os.path.join(REPO_ROOT, editor_exe)
    if not os.path.isfile(exe):
        raise RuntimeError(f"no editor at {exe}")
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    open(LOG_PATH, "w").close()
    env = dict(os.environ, ERHE_AI_DRIVER="1")
    flags = 0
    if os.name == "nt":
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NO_WINDOW
    process = subprocess.Popen([exe, "--commands", rooms_empty_commands()], cwd=REPO_ROOT, env=env,
                               creationflags=flags, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+) \(pid (\d+)")
    deadline = time.monotonic() + 240.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"editor exited with {process.returncode} during startup; see logs/log.txt")
        try:
            with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
        except OSError:
            text = ""
        for match in pattern.finditer(text):
            if (int(match.group(2)) == process.pid) and ("Main loop: completed frame 12" in text):
                return process, int(match.group(1))
        time.sleep(1.0)
    process.kill()
    raise RuntimeError("editor did not become ready")


def rooms_empty_commands():
    return os.path.join("config", "editor", "commands_empty.json")


def stop_editor(c, process):
    try:
        c.client.call("request_exit")
    except Exception:  # noqa: BLE001 - the editor may already be gone
        pass
    try:
        process.wait(timeout=30.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10.0)


# --- field access ------------------------------------------------------------------

class Field:
    """Source selection, sampling and stats for one source."""

    def __init__(self, c, source, tool_names):
        self.c = c
        self.source = source
        self.tool_names = tool_names
        # group -> per-point luminance of the station's quality measurement
        # (measure_sample); cleared per station by run_station.
        self.measured = {}
        # The field sample_indirect_diffuse answers from (set by select()):
        # the source itself, or "ambient" while the source produces no field
        # yet (radiance cascades before the reduce pass,
        # doc/plans/radiance_cascades.md phase 4) - its metrics are then the
        # flat ambient term's.
        self.sampled = source

    def available(self):
        return "set_indirect_diffuse" in self.tool_names

    def select(self):
        """Select this source (set_indirect_diffuse; DDGI with the pinned
        DDGI_SETTINGS) and, for radiance cascades, wait for the fitted
        layout. Returns the radiance cascades stats, or None."""
        if self.source == "ddgi":
            rooms.set_ddgi(self.c, True)
        else:
            rooms.set_indirect_diffuse(self.c, self.source)
        self.sampled = self.source
        if self.source != "radiance_cascades":
            return None
        deadline = time.time() + 30.0
        while True:
            rc = self.c.call("get_indirect_diffuse_stats").get("radiance_cascades", {})
            if rc.get("active") and rc.get("cascade_count", 0) > 0:
                break
            if time.time() > deadline:
                raise RuntimeError(f"radiance_cascades did not become active: {rc}")
            time.sleep(0.1)
        if not rc.get("has_field"):
            self.sampled = "ambient"
        return rc

    def stats(self):
        stats = self.c.call("get_indirect_diffuse_stats")
        return stats.get(self.source, {}) if self.source != "ambient" else {}

    def update_count(self):
        return int(self.stats().get("update_count", 0))

    def reference(self, groups):
        """Ground truth per group: {group: {"luminance": [...], "standard_error": [...],
        "irradiance": [[r, g, b], ...]}} (reference_indirect_diffuse)."""
        flat = []
        index = []
        for name, points in groups.items():
            index.append((name, len(flat), len(points)))
            flat += points
        result = self.c.call("reference_indirect_diffuse",
                             {"samples": flat, "rays_per_point": REFERENCE_RAYS, "seed": REFERENCE_SEED})
        samples = result["samples"]
        out = {}
        for name, start, count in index:
            part = samples[start:start + count]
            out[name] = {"irradiance": [s["irradiance"] for s in part],
                         "luminance": [luminance(s["irradiance"]) for s in part],
                         "standard_error": [s["standard_error"] for s in part]}
        return out

    def sample(self, groups):
        """-> (update_count, {group: [luminance...]}, {group: [rgb...]})."""
        flat = []
        index = []
        for name, points in groups.items():
            index.append((name, len(flat), len(points)))
            flat += points
        result = self.c.call("sample_indirect_diffuse", {"samples": flat})
        if result.get("source") != self.sampled:
            raise RuntimeError(f"sample_indirect_diffuse answered from {result.get('source')!r}, expected {self.sampled!r}")
        values = [s["irradiance"] for s in result["samples"]]
        rgb = {name: values[start:start + count] for name, start, count in index}
        lum = {name: [luminance(v) for v in rgb[name]] for name in rgb}
        return int(result.get("update_count", 0)), lum, rgb

    def relax_updates(self):
        """Updates between convergence checks: one full refresh, and at
        least the hysteresis time constant 1 / (1 - h)."""
        if self.sampled == "ambient":
            return 0
        per_refresh = int(self.stats().get("updates_per_full_refresh", 1) or 1)
        hysteresis = rooms.DDGI_SETTINGS["hysteresis"]
        return max(per_refresh, int(math.ceil(1.0 / (1.0 - hysteresis))))

    def wait_updates(self, target, deadline_s=60.0):
        deadline = time.time() + deadline_s
        while self.update_count() < target:
            if time.time() > deadline:
                raise RuntimeError(f"{self.source}: update_count did not reach {target}")
            time.sleep(0.02)


def group_means(lum):
    return {name: statistics.fmean(values) for name, values in lum.items()}


def sample_averaged(field, groups, count):
    """Sample `count` times at distinct field updates and average per point:
    -> (last update_count, {group: [luminance]}, {group: [rgb]},
    {group: standard error of the group mean}). DDGI traces fresh random
    rays every update, so a single sample carries per-update noise (percent
    level on small-source groups); the standard error lets the convergence
    test tell that noise from drift."""
    last = None
    lum_sum = None
    rgb_sum = None
    group_series = {}
    taken = 0
    while taken < count:
        update, lum, rgb = field.sample(groups)
        if (update == last) and (field.sampled != "ambient"):
            field.wait_updates(update + 1)
            continue
        if lum_sum is None:
            lum_sum = {k: list(v) for k, v in lum.items()}
            rgb_sum = {k: [list(c) for c in v] for k, v in rgb.items()}
        else:
            for k in lum:
                lum_sum[k] = [a + b for a, b in zip(lum_sum[k], lum[k])]
                rgb_sum[k] = [[a + b for a, b in zip(ca, cb)] for ca, cb in zip(rgb_sum[k], rgb[k])]
        for k, v in group_means(lum).items():
            group_series.setdefault(k, []).append(v)
        last = update
        taken += 1
    lum_avg = {k: [v / count for v in values] for k, values in lum_sum.items()}
    rgb_avg = {k: [[c / count for c in rgb] for rgb in values] for k, values in rgb_sum.items()}
    sem = {k: ((statistics.stdev(v) / math.sqrt(len(v))) if len(v) > 1 else 0.0) for k, v in group_series.items()}
    return last, lum_avg, rgb_avg, sem


def wait_converged(field, groups):
    """Converged when, between two checks at least relax_updates() field
    updates apart (so the update_count also advanced by at least one full
    refresh), every group mean - averaged over CHECK_SAMPLES samples -
    changed by less than 0.5 %, or by less than 3 standard errors of the
    difference where the per-update noise exceeds 0.5 % (a noise-limited
    group has converged once its change is indistinguishable from its
    noise). Groups dimmer than DIM_GROUP_FRACTION of the brightest group
    use that level for the 0.5 %. Returns the updates it took."""
    if field.sampled == "ambient":
        return 0
    relax = field.relax_updates()
    start = field.update_count()
    deadline = time.time() + CONVERGE_DEADLINE_S
    previous_count, lum, _, previous_sem = sample_averaged(field, groups, CHECK_SAMPLES)
    previous = group_means(lum)
    while True:
        field.wait_updates(previous_count + relax)
        count, lum, _, sem = sample_averaged(field, groups, CHECK_SAMPLES)
        means = group_means(lum)
        scale = max([abs(v) for v in means.values()] + [1e-12])
        floor = DIM_GROUP_FRACTION * scale
        if all(abs(means[k] - previous[k]) <= max(CONVERGED_RELATIVE_CHANGE * max(abs(previous[k]), floor),
                                                   3.0 * math.hypot(sem[k], previous_sem[k]))
               for k in means):
            return count - start
        if time.time() > deadline:
            raise RuntimeError(f"{field.source}: field did not converge in {CONVERGE_DEADLINE_S} s")
        previous_count, previous, previous_sem = count, means, sem


def measure_sample(field, groups):
    """The metric input: per-point values averaged over MEASURE_SAMPLES
    field updates. -> (lum, rgb). The luminance is also kept in
    field.measured (group -> per-point luminance, merged over the station's
    measure_sample calls), which reference_metrics() compares with the
    reference."""
    _, lum, rgb, _ = sample_averaged(field, groups, MEASURE_SAMPLES)
    field.measured.update(lum)
    return lum, rgb


# --- metrics ---------------------------------------------------------------------------

def mean_of(lum, *names):
    values = []
    for name in names:
        values += lum[name]
    return statistics.fmean(values)


def leak_metrics(lum, prefix=""):
    floor = ratio(mean_of(lum, f"{prefix}b_floor"), mean_of(lum, f"{prefix}a_floor"))
    wall = ratio(mean_of(lum, f"{prefix}b_wall"), mean_of(lum, f"{prefix}a_wall"))
    worst = max(v for v in (floor, wall) if v is not None) if (floor is not None or wall is not None) else None
    return floor, wall, worst


def min_over_median(values):
    median = statistics.median(values)
    if median <= 0.0:
        return None  # a black group has no splotch contrast to measure
    return min(values) / median


def measure_leak_pair(field, info, metrics):
    lum, _ = measure_sample(field, info["samples"])
    floor, wall, worst = leak_metrics(lum)
    metrics["leak_ratio_floor"] = floor
    metrics["leak_ratio_wall"] = wall
    metrics["leak_ratio"] = worst
    metrics["room_a_floor_mean"] = mean_of(lum, "a_floor")


def measure_probe_offset_sweep(field, info, metrics):
    lum, _ = measure_sample(field, info["samples"])
    worst = None
    for index, wall in enumerate(info["layout"]["walls"]):
        _, _, leak = leak_metrics(lum, prefix=f"pair{index}_")
        metrics[f"leak_offset_{wall['requested_offset']}"] = leak
        if leak is not None:
            worst = leak if worst is None else max(worst, leak)
    metrics["worst_leak"] = worst
    placement = []
    for group in ("pillar_faces", "pillar_base", "crawl_floor"):
        value = min_over_median(lum[group])
        metrics[f"{group}_min_over_median"] = value
        metrics[f"{group}_median"] = statistics.median(lum[group])
        placement.append(value)
    known = [v for v in placement if v is not None]
    metrics["placement"] = min(known) if known else None


def measure_cornell(field, info, metrics, noise=True):
    _, rgb = measure_sample(field, info["samples"])

    def channel_mean(group, channel):
        return statistics.fmean(v[channel] for v in rgb[group])

    metrics["red_strip_r_over_g"] = ratio(channel_mean("red_strip", 0), channel_mean("red_strip", 1))
    metrics["green_strip_g_over_r"] = ratio(channel_mean("green_strip", 1), channel_mean("green_strip", 0))
    if noise:
        metrics.update(measure_noise(field, {"back_wall": info["samples"]["back_wall"]}))


def measure_noise(field, groups, prefix="noise"):
    """Per-point std / mean of the luminance over NOISE_SAMPLES distinct field
    updates with the scene static."""
    if field.sampled == "ambient":
        return {f"{prefix}_mean_rel_std": 0.0, f"{prefix}_max_rel_std": 0.0, f"{prefix}_update_span": 0}
    series = []
    counts = []
    name = next(iter(groups))
    while len(series) < NOISE_SAMPLES:
        count, lum, _ = field.sample(groups)
        if counts and (count == counts[-1]):
            field.wait_updates(count + 1)
            continue
        counts.append(count)
        series.append(lum[name])
    relative = []
    for point in range(len(series[0])):
        values = [s[point] for s in series]
        mean = statistics.fmean(values)
        relative.append((statistics.pstdev(values) / mean) if mean > 0.0 else 0.0)
    return {f"{prefix}_mean_rel_std": statistics.fmean(relative), f"{prefix}_max_rel_std": max(relative),
            f"{prefix}_update_span": counts[-1] - counts[0]}


def measure_emissive_only(field, info, metrics):
    lum, _ = measure_sample(field, info["samples"])
    median = statistics.median(lum["room_floor"])
    metrics["room_floor_median"] = median
    for tag in ("1.0", "0.25", "0.05"):
        metrics[f"panel_{tag}_vs_median"] = ratio(mean_of(lum, f"floor_{tag}"), median)
    metrics.update(measure_noise(field, {"floor_1.0": info["samples"]["floor_1.0"]}, prefix="panel_1.0_noise"))


def measure_corridor(field, info, metrics):
    lum, _ = measure_sample(field, info["samples"])
    profile = lum["profile"]
    peak = max(profile) if profile else 0.0
    tolerance = 1e-3 * peak
    violations = sum(1 for i in range(len(profile) - 1) if profile[i + 1] > (profile[i] + tolerance))
    second = [abs(profile[i - 1] - (2.0 * profile[i]) + profile[i + 1]) for i in range(1, len(profile) - 1)]
    metrics["monotonic_violations"] = violations
    metrics["max_second_difference"] = (max(second) / peak) if peak > 0.0 else None
    # Informational, scale free: the same on ln(profile), where a smooth
    # exponential-like falloff is nearly linear and a step still stands out.
    metrics["max_log_second_difference"] = None
    if min(profile) > 0.0:
        logs = [math.log(v) for v in profile]
        metrics["max_log_second_difference"] = max(
            abs(logs[i - 1] - (2.0 * logs[i]) + logs[i + 1]) for i in range(1, len(logs) - 1))
    metrics["profile_max"] = peak
    metrics["profile"] = profile


def measure_courtyard(field, info, metrics):
    lum, _ = measure_sample(field, info["samples"])
    for group in ("shadowed_wall", "shadowed_floor", "sunlit_floor"):
        metrics[f"{group}_mean"] = mean_of(lum, group)


def measure_dynamic(c, field, info, metrics):
    measure_cornell(field, info, metrics, noise=False)
    for event_name, event in rooms.STATIONS["dynamic"]["events"].items():
        names = rooms.STATIONS["dynamic"]["event_groups"][event_name]
        groups = {n: info["samples"][n] for n in names}
        before = field.update_count() if field.sampled != "ambient" else 0
        event(c, info)
        if field.sampled == "ambient":
            c.settle()
            metrics[f"{event_name}_updates_to_settle"] = 0
            continue
        # Record single samples for SETTLE_WINDOW_RELAXATIONS relaxation
        # times; the settled value is the mean over the last quarter of the
        # window, the band around it 5 % or 3 standard deviations of that
        # tail, whichever is wider (per-update ray noise on dim groups
        # exceeds 5 %).
        window = SETTLE_WINDOW_RELAXATIONS * field.relax_updates()
        series = []
        deadline = time.time() + CONVERGE_DEADLINE_S
        while (not series) or ((series[-1][0] - before) < window):
            count, lum, _ = field.sample(groups)
            series.append((count, group_means(lum)))
            if time.time() > deadline:
                raise RuntimeError(f"dynamic/{event_name}: window of {window} updates not reached")
        tail = [m for count, m in series if (count - before) >= (0.75 * window)]
        settled = {k: statistics.fmean(m[k] for m in tail) for k in names}
        band = {k: max(SETTLED_RELATIVE * abs(settled[k]),
                       3.0 * (statistics.pstdev([m[k] for m in tail]) if len(tail) > 1 else 0.0)) for k in names}
        settle_count = series[-1][0]
        for i in range(len(series)):
            if all(all(abs(m[k] - settled[k]) <= band[k] for k in names) for _, m in series[i:]):
                settle_count = series[i][0]
                break
        metrics[f"{event_name}_updates_to_settle"] = settle_count - before
        metrics[f"{event_name}_sample_spacing_updates"] = (
            (series[-1][0] - series[0][0]) / max(1, len(series) - 1))
        metrics[f"{event_name}_settled_mean"] = statistics.fmean(settled.values())
        metrics[f"{event_name}_band_relative"] = max(band[k] / abs(settled[k]) for k in names if settled[k] != 0.0)             if any(settled[k] != 0.0 for k in names) else None


def reference_metrics(reference, measured):
    """Errors of the measured field against the reference, per group (see
    the module docstring for the rule). -> {metric: value}."""
    level = max([statistics.fmean(r["luminance"]) for r in reference.values()] + [0.0])
    floor = max(REFERENCE_FLOOR_FRACTION * level, 1e-12)
    metrics = {}
    worst_group = None
    for group, ref in reference.items():
        if group not in measured:
            continue
        values = measured[group]
        ref_mean = statistics.fmean(ref["luminance"])
        mean = statistics.fmean(values)
        mean_err = abs(mean - ref_mean) / max(ref_mean, floor)
        point_err = max(abs(v - r) / max(r, floor) for v, r in zip(values, ref["luminance"]))
        point_se = max(se / max(r, floor) for se, r in zip(ref["standard_error"], ref["luminance"]))
        metrics[f"ref.{group}.ref_mean"] = ref_mean
        metrics[f"ref.{group}.mean_rel_err"] = mean_err
        metrics[f"ref.{group}.worst_point_rel_err"] = point_err
        metrics[f"ref.{group}.ref_point_rel_se"] = point_se
        worst_group = mean_err if worst_group is None else max(worst_group, mean_err)
    metrics["ref.worst_group_mean_rel_err"] = worst_group
    metrics["ref.floor"] = floor
    return metrics


def cost_metrics(field):
    if field.source == "ambient":
        return {}
    stats = field.stats()
    return {
        "gpu_ms_per_update_avg": stats.get("gpu_ms_total", {}).get("average_ms"),
        "ms_per_million_rays": stats.get("ms_per_million_rays"),
        "full_refresh_ms": stats.get("full_refresh_ms"),
        "texture_bytes": stats.get("texture_bytes"),
        "probe_count": stats.get("probe_count"),
        "rays_per_update": stats.get("rays_per_update"),
        "updates_per_full_refresh": stats.get("updates_per_full_refresh"),
    }


COST_KEYS = ("gpu_ms_per_update_avg", "ms_per_million_rays", "full_refresh_ms", "texture_bytes",
             "probe_count", "rays_per_update", "updates_per_full_refresh")


# --- screenshots ---------------------------------------------------------------------------

def compare_images(reference_path, current_path, viewport, tolerance):
    """viewport: [x, y, w, h] region to compare, or None for the whole image."""
    from PIL import Image, ImageChops
    a = Image.open(reference_path).convert("RGB")
    b = Image.open(current_path).convert("RGB")
    if a.size != b.size:
        return {"error": f"size {a.size} != {b.size}"}
    x, y, w, h = viewport if viewport is not None else (0, 0, a.size[0], a.size[1])
    box = (x, y, x + w, y + h)
    diff = ImageChops.difference(a.crop(box), b.crop(box))
    r, g, bl = diff.split()
    channel_max = ImageChops.lighter(ImageChops.lighter(r, g), bl)
    histogram = channel_max.histogram()
    differing = sum(histogram[1:])
    largest = max((i for i, n in enumerate(histogram) if n > 0), default=0)
    over = sum(histogram[tolerance + 1:])
    return {"differing_pixels": differing, "max_channel_difference": largest, "pixels": w * h,
            "tolerance": tolerance, "pixels_over_tolerance": over, "match": over == 0}


def capture_view(c, info, view, path, screenshot_source):
    """The ONE screenshot path: capture the station view `view` to `path` and
    return the region the comparison looks at ([x, y, w, h], or None for the
    whole image).

    render: render_scene_image renders the scene offscreen through the view's
    camera (eye, target, fov) at SHOT_WIDTH x SHOT_HEIGHT; the image does not
    depend on any ImGui window or viewport size, and the whole image is
    compared.
    window: place the station camera, capture_screenshot the whole editor
    window, and compare only the viewport region; the image depends on the
    editor's ImGui layout (docked / floating windows around or over the
    viewport)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if screenshot_source == "render":
        camera = dict(STATION_CAMERA, eye=view["eye"], target=view["target"], fov_y_degrees=view["fov_y_deg"])
        c.call("render_scene_image", {"scene": c.scene, "camera": camera, "width": SHOT_WIDTH,
                                      "height": SHOT_HEIGHT, "output": "png", "path": path})
        return None
    rooms.place_view(c, view)
    c.settle()
    time.sleep(0.3)
    c.call("capture_screenshot", {"path": path})
    return rooms.viewport_rect(c)


def capture_views(c, info, source, shot_dir, args, results):
    for v in info["views"]:
        name = f"{info['station']}_{v['name']}.png"
        path = os.path.join(shot_dir, name)
        viewport = capture_view(c, info, v, path, args.screenshot_source)
        entry = {"view": v["name"], "path": os.path.relpath(path, REPO_ROOT), "viewport": viewport}
        if args.screenshot_reference:
            target_dir = os.path.join(args.screenshot_reference, source)
            os.makedirs(target_dir, exist_ok=True)
            shutil.copyfile(path, os.path.join(target_dir, name))
        if args.screenshot_compare:
            reference = os.path.join(args.screenshot_compare, source, name)
            if os.path.isfile(reference):
                tolerance = args.screenshot_tolerance
                if tolerance is None:
                    tolerance = SCREENSHOT_TOLERANCE.get(source, 0)
                entry["compare"] = compare_images(reference, path, viewport, tolerance)
            else:
                entry["compare"] = {"error": "no reference"}
        results.append(entry)


# --- one station, one source ------------------------------------------------------------

def print_rc_layout(station, rc):
    """The fitted radiance cascades layout, one line per cascade."""
    print(f"{station}: radiance cascades, {rc['cascade_count']} cascades, r0 {rc['r0']:.3f} m, "
          f"{rc['probe_count']} probes, {rc['texels']} texels, {rc['texture_bytes'] / (1024 * 1024):.2f} MB")
    for cascade in rc["cascades"]:
        counts = "x".join(str(n) for n in cascade["grid_counts"])
        spacing = " ".join(f"{v:.3f}" for v in cascade["grid_spacing"])
        start, end = cascade["interval"]
        print(f"  cascade {cascade['index']}: {counts} = {cascade['probe_count']} probes, spacing {spacing} m, "
              f"q {cascade['tile_texels']}, interval [{start:.3f}, {end:.3f}] m, {cascade['texels']} texels, "
              f"{cascade['texture_bytes'] / (1024 * 1024):.2f} MB")


def run_station(c, field, name, shot_dir, args):
    started = time.time()
    info = rooms.build_station(c, name, ddgi=(field.source == "ddgi"))
    rc_layout = field.select()
    if rc_layout is not None:
        print_rc_layout(name, rc_layout)
    c.settle()
    # Ground truth for this build; independent of the source and its state.
    reference = field.reference(info["samples"])
    field.measured = {}
    record = {"station": name, "source": field.source, "sampled": field.sampled, "grid": info["grid"], "metrics": {}}
    if rc_layout is not None:
        record["radiance_cascades"] = {k: rc_layout.get(k) for k in
                                       ("cascade_count", "r0", "probe_count", "texels", "texture_bytes", "cascades")}
    if name == "probe_offset_sweep":
        record["walls"] = info["layout"]["walls"]
        record["pillar"] = info["layout"]["pillar"]
    record["converge_updates"] = wait_converged(field, info["samples"])
    metrics = record["metrics"]
    if name == "leak_pair":
        measure_leak_pair(field, info, metrics)
    elif name == "probe_offset_sweep":
        measure_probe_offset_sweep(field, info, metrics)
    elif name == "cornell":
        measure_cornell(field, info, metrics)
    elif name == "emissive_only":
        measure_emissive_only(field, info, metrics)
    elif name == "corridor":
        measure_corridor(field, info, metrics)
    elif name == "courtyard":
        measure_courtyard(field, info, metrics)
    elif name == "dynamic":
        measure_dynamic(c, field, info, metrics)
    metrics.update(reference_metrics(reference, field.measured))
    record["reference"] = {g: {"luminance": r["luminance"], "standard_error": r["standard_error"]}
                           for g, r in reference.items()}
    record["measured_luminance"] = dict(field.measured)
    record["cost"] = cost_metrics(field)
    # dynamic: the views show the state after the events.
    screenshots = []
    capture_views(c, info, field.source, shot_dir, args, screenshots)
    record["screenshots"] = screenshots
    record["seconds"] = round(time.time() - started, 1)
    rooms.set_headlight(c, info["headlight_before"])
    return record


# --- aggregation + report -------------------------------------------------------------------

def lower_is_worse(metric):
    if metric.startswith("ref."):
        return False  # reference errors: larger is worse (group names may contain the keys below)
    return any(key in metric for key in LOWER_IS_WORSE)


def worst_of(values, metric):
    values = [v for v in values if isinstance(v, (int, float))]
    if not values:
        return None
    return min(values) if lower_is_worse(metric) else max(values)


def aggregate(records):
    """records: list over runs of {station: record} -> {station: {metric: worst}}."""
    out = {}
    for station in records[0]:
        metrics = {}
        keys = [k for k, v in records[0][station]["metrics"].items() if not isinstance(v, list)]
        for key in keys:
            metrics[key] = worst_of([run[station]["metrics"].get(key) for run in records], key)
        for key in COST_KEYS:
            values = [run[station]["cost"].get(key) for run in records]
            if any(v is not None for v in values):
                metrics[f"cost.{key}"] = worst_of(values, key)
        metrics["converge_updates"] = worst_of([run[station]["converge_updates"] for run in records], "converge")
        out[station] = metrics
    return out


def gate_verdict(station, metric, value):
    gate = GATES.get(f"{station}.{metric}")
    if gate is None or value is None:
        return ""
    kind, limit = gate
    ok = (value >= limit) if kind == "min" else (value <= limit)
    sign = ">=" if kind == "min" else "<="
    return f"{'PASS' if ok else 'FAIL'} ({sign} {limit})"


def fmt(value):
    if value is None:
        return "n/a"
    if isinstance(value, float):
        if math.isinf(value):
            return "inf"
        return f"{value:.4g}"
    return str(value)


def print_table(sources, worst):
    header = f"{'station.metric':52s}" + "".join(f"{s:>16s}" for s in sources)
    if len(sources) == 2:
        header += f"{'b/a':>10s}"
    header += "  gate (" + sources[-1] + ")"
    print(header)
    print("-" * len(header))
    failures = 0
    stations = list(worst[sources[0]].keys())
    for station in stations:
        keys = []
        for source in sources:
            keys += [k for k in worst[source][station] if k not in keys]
        for key in keys:
            values = [worst[s][station].get(key) for s in sources]
            line = f"{station + '.' + key:52s}" + "".join(f"{fmt(v):>16s}" for v in values)
            if len(sources) == 2:
                a, b = values
                r = ratio(b, a) if isinstance(a, (int, float)) and isinstance(b, (int, float)) else None
                line += f"{fmt(r):>10s}"
            verdict = gate_verdict(station, key, values[-1])
            if verdict.startswith("FAIL"):
                failures += 1
            line += "  " + verdict
            print(line)
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--station", default="all", help="station name or 'all' (" + ", ".join(rooms.STATIONS) + ")")
    parser.add_argument("--source", default="ddgi", choices=SOURCES)
    parser.add_argument("--compare", default=None, metavar="A,B", help="two sources measured back to back per station")
    parser.add_argument("--runs", type=int, default=3, help="full runs; each metric is the worst over the runs")
    parser.add_argument("--enforce", action="store_true", help="exit non-zero when a gate FAILs")
    parser.add_argument("--screenshot-reference", default=None, metavar="DIR")
    parser.add_argument("--screenshot-compare", default=None, metavar="DIR")
    parser.add_argument("--screenshot-source", default="render", choices=["render", "window"],
                        help="render: render_scene_image offscreen (default); "
                             "window: capture_screenshot of the editor window, viewport region")
    parser.add_argument("--screenshot-tolerance", type=int, default=None, metavar="LEVELS",
                        help="largest per-channel 8-bit difference a compared pixel may have "
                             "(default: 0 for ambient, 1 for a traced field)")
    parser.add_argument("--reuse", action="store_true", help="drive the editor already running on --port")
    parser.add_argument("--port", type=int, default=3743)
    parser.add_argument("--editor", default=DEFAULT_EDITOR)
    args = parser.parse_args()

    stations = list(rooms.STATIONS) if args.station == "all" else [args.station]
    for name in stations:
        if name not in rooms.STATIONS:
            raise SystemExit(f"unknown station {name!r}; one of {', '.join(rooms.STATIONS)}")
    sources = args.compare.split(",") if args.compare else [args.source]
    for source in sources:
        if source not in SOURCES:
            raise SystemExit(f"unknown source {source!r}; one of {', '.join(SOURCES)}")
    if args.compare and len(sources) != 2:
        raise SystemExit("--compare takes exactly two sources, e.g. --compare ddgi,radiance_cascades")

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    label = "_vs_".join(sources)
    shot_root = os.path.join(OUT_DIR, f"{label}_{stamp}")

    backup = None
    process = None
    port = args.port
    if not args.reuse:
        backup = Config_backup()
        process, port = launch_editor(args.editor)
        print(f"launched editor pid {process.pid} on port {port}")
    c = None
    try:
        c = Creation("gi_verify", port=port, pause_s=0.0, reuse=True, manage_windows=False)
        if process is not None:
            pid = c.call("get_server_info").get("pid")
            if pid != process.pid:
                raise RuntimeError(f"port {port} is served by pid {pid}, not the launched editor {process.pid}")
        tool_names = c.client.tool_names()
        fields = {s: Field(c, s, tool_names) for s in sources}
        for source, field in fields.items():
            if not field.available():
                print(f"source {source}: not available yet (no set_indirect_diffuse MCP tool)")
                return 2
        headlight = c.call("set_graphics_settings", {}).get("headlight_when_unlit", True)
        runs = {s: [] for s in sources}
        try:
            for run in range(args.runs):
                per_source = {s: {} for s in sources}
                for station in stations:
                    for source in sources:
                        shot_dir = os.path.join(shot_root, source, f"run{run}")
                        print(f"run {run + 1}/{args.runs} {station} [{source}]", flush=True)
                        record = run_station(c, fields[source], station, shot_dir, args)
                        per_source[source][station] = record
                        print(f"  {record['seconds']} s, converged after {record['converge_updates']} updates; "
                              + ", ".join(f"{k}={fmt(v)}" for k, v in record["metrics"].items()
                                          if not isinstance(v, list)), flush=True)
                        for shot in record["screenshots"]:
                            if "compare" in shot:
                                print(f"  screenshot {shot['view']}: {shot['compare']}")
                for source in sources:
                    runs[source].append(per_source[source])
            c.close_all_scenes()
        finally:
            rooms.set_headlight(c, headlight)
        worst = {s: aggregate(runs[s]) for s in sources}
        print()
        failures = print_table(sources, worst)
        os.makedirs(OUT_DIR, exist_ok=True)
        json_path = os.path.join(OUT_DIR, f"{label}_{stamp}.json")
        with open(json_path, "w", encoding="utf-8") as handle:
            json.dump({"sources": sources, "stations": stations, "runs": args.runs,
                       "worst": worst, "per_run": runs, "gates": GATES,
                       "server": c.call("get_server_info")}, handle, indent=1)
        print(f"\nwrote {os.path.relpath(json_path, REPO_ROOT)}; screenshots under {os.path.relpath(shot_root, REPO_ROOT)}")
        if failures:
            print(f"{failures} gate(s) FAIL" + ("" if args.enforce else " (reported only; --enforce fails the exit code)"))
        return 1 if (failures and args.enforce) else 0
    finally:
        if process is not None:
            stop_editor(c, process) if c is not None else process.kill()
        if backup is not None:
            backup.restore()


if __name__ == "__main__":
    sys.exit(main())
