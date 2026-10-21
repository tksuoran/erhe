#!/usr/bin/env python3
"""Steady-state frame benchmark of the windowed editor on the Bistro scene.

Reproduces a user session: the windowed editor is launched from the repo root
with --no-scene and ERHE_AI_DRIVER=1 (the working copy's config/ applies, no
config file is written), bistro_lights_c.gltf is opened over MCP, the scene's
Camera and import root are set to the given transforms, the deferred loading
tasks settle (get_async_status idle, then --settle-s seconds with no MCP
traffic), and a quiet measurement window of --measure-s seconds follows during
which the script makes no MCP call at all. The editor is then asked to exit.

The run from the scene open on is recorded with tracy-capture (the editor
must be built with ERHE_PROFILE_LIBRARY=tracy; the capture tool connects once
the editor is ready, so with ERHE_TRACY_ON_DEMAND=ON the startup is not
recorded). The capture is exported with
tracy-csvexport and the frames of the quiet window are analysed per frame:
the main-thread phases (M1, M2, M3, kick, join), the render thread's R, the
device waits inside them, and the GPU zones, so an alternating slow / fast
frame pattern shows up as such and the wait that carries it is named.

The quiet window is found in the capture itself: every MCP request runs in
the "MCP server" zone, so the frames between the last settle poll and the
request_exit call are the ones with no MCP traffic.

The analysis step can be re-run on a saved capture with --analyze-only.

Examples:
  set ERHE_TRACY_DIR=<tracy tools directory with tracy-capture.exe and tracy-csvexport.exe>
  py -3 scripts/bistro_frame_bench.py --editor build_vs2026_vulkan/bin/Release/editor.exe
  py -3 scripts/bistro_frame_bench.py --analyze-only --out logs/bistro_frames.tracy --frames 6
"""

import argparse
import base64
import http.cookiejar
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import zipfile
from collections import defaultdict

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_PATH = os.path.join(REPO_ROOT, "logs", "log.txt")
READY_LINE = "Main loop: completed frame 12"
DEFAULT_SCENE = "res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf"
# The default scene is not in the repository (res/editor/assets/ is
# ignored): a zip of the bistro_lights_compressed folder (268 MB; the
# folder's parent is the extraction root) is downloaded from Google Drive
# when the scene file is missing.
DEFAULT_SCENE_ARCHIVE_DRIVE_ID = "1GlmOPAURKOgwAshlP8hhiUwg-US-l9JI"
DEFAULT_SCENE_ARCHIVE_ROOT = "res/editor/assets"

# The user's session on 2026-10-18 (queried over MCP with get_node_details):
# the viewport camera and the glTF import root (Y-up conversion).
DEFAULT_CAMERA_TRANSLATION = [-29.2810115814209, 9.677236557006836, -16.337175369262695]
DEFAULT_CAMERA_ROTATION = [-0.03749912232160568, -0.9181863069534302, -0.08965181559324265, 0.3840445876121521]
DEFAULT_ROOT_TRANSLATION = [0.0, 0.0, 0.0]
DEFAULT_ROOT_ROTATION = [-0.7071068286895752, 0.0, 0.0, 0.7071068286895752]

SEP = "|"
MAIN_PHASES = ["frame_overlap_phase", "frame_prepare_phase", "frame_imgui_phase", "kick", "join"]
RENDER_PHASE = "frame_render_phase"
MCP_ZONE = "MCP server"
MCP_REQUEST_THRESHOLD_NS = 60_000


def download_google_drive_file(file_id, target_path):
    """Downloads a shared Google Drive file to target_path. A large file
    first answers with the virus-scan warning page, whose form carries the
    confirm token and a uuid; its action URL with those fields is the
    download. Returns the file name from the Content-Disposition header."""
    cookies = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cookies))
    url = f"https://drive.google.com/uc?export=download&id={file_id}"
    response = opener.open(url, timeout=120)
    content_type = response.headers.get("Content-Type", "")
    if content_type.startswith("text/html"):
        page = response.read().decode("utf-8", errors="replace")
        response.close()
        action = re.search(r'action="([^"]+)"', page)
        fields = dict(re.findall(r'name="([a-z]+)" value="([^"]*)"', page))
        if (action is None) or ("confirm" not in fields):
            sys.exit(f"Google Drive did not offer a download for {file_id}: {page[:200]}")
        url = action.group(1) + "?" + urllib.parse.urlencode(fields)
        response = opener.open(url, timeout=120)
    name = re.search(r'filename="([^"]+)"', response.headers.get("Content-Disposition", ""))
    total = int(response.headers.get("Content-Length", "0") or 0)
    done = 0
    next_report = 0.1
    start = time.time()
    with open(target_path, "wb") as f:
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            if total and (done >= next_report * total):
                print(f"  {100.0 * done / total:3.0f}% of {total / 1e6:.0f} MB ({time.time() - start:.0f} s)")
                next_report += 0.1
    response.close()
    return name.group(1) if name else os.path.basename(target_path)


def extract_scene_archive(archive_path, extraction_root, scene_relative_path):
    """Extracts the archive into extraction_root; the archive must hold
    scene_relative_path (relative to that root) and no entry may escape
    it. Returns the number of entries extracted."""
    root = os.path.abspath(extraction_root)
    with zipfile.ZipFile(archive_path) as archive:
        names = archive.namelist()
        if scene_relative_path.replace(os.sep, "/") not in names:
            sys.exit(f"{archive_path} does not contain {scene_relative_path}; entries: {names[:5]}")
        for entry in names:
            if not os.path.abspath(os.path.join(root, entry)).startswith(root + os.sep):
                sys.exit(f"{archive_path}: entry {entry} escapes {root}")
        os.makedirs(root, exist_ok=True)
        archive.extractall(root)
    return len(names)


def ensure_scene(scene_path, drive_id, archive_root, test_download):
    """The scene file is present, or is fetched: the Google Drive archive
    is downloaded and extracted under archive_root (repo-root relative).
    With test_download the download and the archive check run even when
    the scene is present, into a temporary directory that is removed
    again, so the download path can be verified without touching the
    local assets."""
    scene_absolute = os.path.join(REPO_ROOT, scene_path)
    present = os.path.isfile(scene_absolute)
    print(f"scene {scene_path}: {'present' if present else 'missing'}")
    if present and not test_download:
        return
    if drive_id is None:
        sys.exit(f"scene not found: {scene_absolute} (no download source for this scene)")
    scene_in_archive = os.path.relpath(scene_absolute, os.path.join(REPO_ROOT, archive_root)).replace(os.sep, "/")
    temp_dir = tempfile.mkdtemp(prefix="bistro_scene_")
    try:
        archive_path = os.path.join(temp_dir, "scene.zip")
        print(f"downloading Google Drive file {drive_id}")
        name = download_google_drive_file(drive_id, archive_path)
        print(f"downloaded {name}: {os.path.getsize(archive_path) / 1e6:.1f} MB")
        extraction_root = os.path.join(temp_dir, "extracted") if present else os.path.join(REPO_ROOT, archive_root)
        count = extract_scene_archive(archive_path, extraction_root, scene_in_archive)
        extracted_scene = os.path.join(extraction_root, scene_in_archive)
        print(f"extracted {count} entries to {extraction_root}; scene file {os.path.getsize(extracted_scene) / 1e6:.1f} MB")
        if present:
            print("download test passed (the local scene was left untouched)")
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


def read_log():
    try:
        with open(LOG_PATH, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def mcp(port, tool, arguments=None):
    encoded = "b64:" + base64.b64encode(json.dumps(arguments or {}).encode("utf-8")).decode("ascii")
    result = subprocess.run(
        [sys.executable, os.path.join(REPO_ROOT, "scripts", "mcp_call.py"), tool, encoded, "--port", str(port)],
        cwd=REPO_ROOT, capture_output=True, text=True, errors="replace"
    )
    text = result.stdout.strip()
    try:
        reply = json.loads(text)
    except ValueError:
        return text
    content = reply.get("result", reply).get("content", []) if isinstance(reply, dict) else []
    for item in content:
        if item.get("type") == "text":
            try:
                return json.loads(item["text"])
            except ValueError:
                return item["text"]
    return reply


def wait_for_ready(editor_process, timeout_s):
    start = time.time()
    while ((time.time() - start) < timeout_s) and (editor_process.poll() is None):
        text = read_log()
        if READY_LINE in text:
            port = re.search(r"MCP server: listening on 127\.0\.0\.1:(\d+)", text)
            if port:
                return int(port.group(1)), time.time() - start
        time.sleep(0.1)
    return None, None


def scene_names(port):
    reply = mcp(port, "list_scenes")
    if isinstance(reply, dict) and ("scenes" in reply):
        return [scene.get("name", "") for scene in reply.get("scenes", [])]
    return None


def wait_until_settled(port, scene_file, timeout_s):
    start = time.time()
    listed = False
    while (time.time() - start) < timeout_s:
        status = mcp(port, "get_async_status")
        if not listed and any(scene_file in name for name in (scene_names(port) or [])):
            listed = True
        if listed and isinstance(status, dict) and status.get("idle"):
            return time.time() - start
        time.sleep(0.5)
    return None


def find_tool(tracy_dir, name):
    for candidate in (os.path.join(tracy_dir, name + ".exe"), os.path.join(tracy_dir, name)):
        if os.path.isfile(candidate):
            return candidate
    return None


def phase_lines(text):
    rows = []
    pair_re = re.compile(r"([A-Za-z_][A-Za-z0-9_ ]*?) (-?\d+\.\d+)")
    for line in text.splitlines():
        m = re.search(r"Phase timing \(us/frame, avg of (\d+) frames\): (.*)$", line)
        if m:
            row = {}
            for name, value in pair_re.findall(m.group(2)):
                row[name.strip()] = float(value)
            rows.append(row)
    return rows


# ---------------------------------------------------------------------------
# Capture analysis


def export(csvexport, capture, extra):
    """tracy-csvexport rows of the capture; the export is cached next to the
    capture (<capture>.<flags>.csv) because exporting a large capture takes
    minutes and the analysis is re-run with different frame counts."""
    cache = capture + "." + "".join(flag.strip("-") for flag in extra) + ".csv"
    if os.path.isfile(cache) and (os.path.getmtime(cache) >= os.path.getmtime(capture)):
        with open(cache, encoding="utf-8", errors="replace") as f:
            text = f.read()
    else:
        out = subprocess.run([csvexport, "-s", SEP, *extra, capture], capture_output=True, text=True, check=True)
        text = out.stdout
        with open(cache, "w", encoding="utf-8", newline="") as f:
            f.write(text)
    lines = text.splitlines()
    if not lines:
        return []
    header = lines[0].split(SEP)
    rows = []
    for line in lines[1:]:
        parts = line.split(SEP)
        if len(parts) != len(header):
            continue
        rows.append(dict(zip(header, parts)))
    return rows


def zone_intervals(rows):
    """{name: [(start, end, thread)]} sorted by start, for every CPU zone.

    ERHE_PROFILE_FUNCTION names a zone after the function: a bare name with
    GCC and Clang (`frame_prepare_phase`), the qualified name with MSVC
    (`editor::Editor::frame_prepare_phase`). The phase zones are looked up
    under the bare name, so the qualified names are stored under both.
    """
    result = defaultdict(list)
    for row in rows:
        start = int(row["ns_since_start"])
        name = row["name"]
        entry = (start, start + int(row["exec_time_ns"]), row["thread"])
        result[name].append(entry)
        if "::" in name:
            bare = name.rsplit("::", 1)[1]
            if bare in ("frame", *MAIN_PHASES, RENDER_PHASE):
                result[bare].append(entry)
    for intervals in result.values():
        intervals.sort()
    return result


def overlap_ns(a_start, a_end, intervals):
    total = 0
    for start, end, _thread in intervals:
        if end <= a_start:
            continue
        if start >= a_end:
            break
        total += min(end, a_end) - max(start, a_start)
    return total


def inside(window, intervals):
    return [iv for iv in intervals if (iv[0] >= window[0]) and (iv[0] < window[1])]


def us(ns):
    return ns / 1000.0


def lag1_autocorrelation(values):
    if len(values) < 3:
        return 0.0
    mean = statistics.fmean(values)
    num = sum((values[i] - mean) * (values[i + 1] - mean) for i in range(len(values) - 1))
    den = sum((v - mean) ** 2 for v in values)
    return (num / den) if den > 0 else 0.0


def analyze(capture, csvexport, frame_count, settle_window_s, gpu_rows_wanted=True):
    rows = export(csvexport, capture, ["-u"])
    if not rows:
        print("no zones exported")
        return 1
    zones = zone_intervals(rows)
    frames = zones.get("frame", [])
    if not frames:
        print("no `frame` zones in the capture")
        return 1

    # The quiet window: between the last MCP request before the exit request
    # and the exit request itself (the last MCP zone of the capture). Without
    # MCP zones the final --measure-s seconds of frames are used instead.
    # The MCP server zone runs every frame (it polls the request queue); a
    # frame that served a request spends far longer in it (JSON reply
    # building), so the request frames are the zones above the threshold.
    mcp_zones = [z for z in zones.get(MCP_ZONE, []) if (z[1] - z[0]) >= MCP_REQUEST_THRESHOLD_NS]
    if len(mcp_zones) >= 2:
        # The quiet window is the longest gap between consecutive requests
        # among the last few (the measurement window; the reads of the
        # records and the exit request follow it closely).
        tail = mcp_zones[-6:]
        window = max(((a[1], b[0]) for a, b in zip(tail, tail[1:])), key=lambda w: w[1] - w[0])
    else:
        window = (frames[-1][1] - int(settle_window_s * 1e9), frames[-1][1])
    # A tick is one `frame` zone start to the next: the frame start (the
    # pacing wait) and the input poll lie between the zones, in run().
    window_frames = [(a[0], b[0], a[2]) for a, b in zip(frames, frames[1:]) if (a[0] >= window[0]) and (b[0] <= window[1])]
    print(f"capture: {capture}")
    print(f"frames in capture: {len(frames)}; quiet window {us(window[1] - window[0]) / 1000:.1f} ms with {len(window_frames)} complete frames (no MCP traffic)")
    if len(window_frames) < frame_count:
        print("quiet window shorter than the requested frame count; analysing what there is")

    # Thread names: the render phase thread versus the frame (main) thread.
    main_thread = frames[0][2]
    render_zones = zones.get(RENDER_PHASE, [])
    render_thread = render_zones[0][2] if render_zones else "?"

    # Whole-window statistics of the tick and the phases.
    ticks = [f[1] - f[0] for f in window_frames]
    print()
    print(f"tick over the quiet window: n={len(ticks)} mean {us(statistics.fmean(ticks)):.0f} us  median {us(statistics.median(ticks)):.0f}  "
          f"min {us(min(ticks)):.0f}  max {us(max(ticks)):.0f}  stdev {us(statistics.pstdev(ticks)):.0f}  lag-1 autocorrelation {lag1_autocorrelation(ticks):+.2f}")
    odd = ticks[0::2]
    even = ticks[1::2]
    if odd and even:
        print(f"  alternate frames: mean {us(statistics.fmean(odd)):.0f} us vs {us(statistics.fmean(even)):.0f} us "
              f"(a negative lag-1 autocorrelation with these apart = slow/fast alternation)")
    runs = []
    if len(ticks) >= 2:
        above = [t > statistics.median(ticks) for t in ticks]
        flips = sum(1 for i in range(1, len(above)) if above[i] != above[i - 1])
        print(f"  median crossings: {flips} of {len(ticks) - 1} transitions ({100.0 * flips / (len(ticks) - 1):.0f}%; ~100% = strict alternation, ~50% = random)")

    # Per-frame picture of the last `frame_count` frames of the window.
    picked = window_frames[-frame_count:]
    print()
    print(f"per-frame picture of the last {len(picked)} quiet frames (us; tick = frame start to next frame start):")
    header = f"{'#':>3} {'start_ms':>10} {'tick':>8} {'M1':>8} {'M2':>8} {'M3':>8} {'kick':>8} {'join':>8} {'R(start)':>10} {'R':>8} {'R_gap':>8}"
    print(header)
    render_sorted = sorted(render_zones)
    previous_render_end = None
    for index, frame in enumerate(picked):
        start, end, _thread = frame
        values = {}
        for name in MAIN_PHASES:
            values[name] = overlap_ns(start, end, zones.get(name, []))
        # The R that this tick kicked: the first R starting after the kick
        # begins (with pipelining the kick joins the previous R first).
        kicks = inside((start, end), zones.get("kick", []))
        kick_start = kicks[0][0] if kicks else start
        renders = [r for r in render_sorted if r[0] >= kick_start]
        render = renders[0] if renders else None
        r_len = (render[1] - render[0]) if render else 0
        r_gap = (render[0] - previous_render_end) if (render and previous_render_end is not None) else 0
        if render:
            previous_render_end = render[1]
        print(f"{index:3d} {us(start) / 1000:10.3f} {us(end - start):8.0f} {us(values['frame_overlap_phase']):8.0f} {us(values['frame_prepare_phase']):8.0f} "
              f"{us(values['frame_imgui_phase']):8.0f} {us(values['kick']):8.0f} {us(values['join']):8.0f} "
              f"{(us(render[0]) / 1000) if render else 0:10.3f} {us(r_len):8.0f} {us(r_gap):8.0f}")

    # Thread overlap over the quiet window: how much of each R the main
    # thread was busy (and of that, in M2), how much it spent blocked (the
    # kick's join of the previous R with pipelining, join without it), and
    # how much of each tick the render thread was idle; plus the main
    # thread's waits per tick (pacing sleep, slot fence wait).
    main_work = sorted(sum((zones.get(name, []) for name in ("frame_overlap_phase", "frame_prepare_phase", "frame_imgui_phase")), []))
    prepares = zones.get("frame_prepare_phase", [])
    blocks = sorted(zones.get("kick", []) + zones.get("join", []))
    pacing = zones.get("frame pacing", [])
    fence = zones.get("acquire_frame_slot: vkWaitSemaphores", [])
    renders_in_window = [r for r in render_sorted if (r[0] >= window[0]) and (r[1] <= window[1])]
    if renders_in_window and window_frames:
        r_count = len(renders_in_window)
        r_total = sum(r[1] - r[0] for r in renders_in_window)
        r_in_phases = sum(overlap_ns(r[0], r[1], main_work) for r in renders_in_window)
        r_in_sleep = sum(overlap_ns(r[0], r[1], pacing) for r in renders_in_window)
        r_in_fence = sum(overlap_ns(r[0], r[1], fence) for r in renders_in_window)
        r_in_m2 = sum(overlap_ns(r[0], r[1], prepares) for r in renders_in_window) - r_in_fence
        r_blocked = sum(overlap_ns(r[0], r[1], blocks) for r in renders_in_window)
        r_working = r_in_phases - r_in_fence # the pacing sleep is outside the phases
        tick_total = sum(f[1] - f[0] for f in window_frames)
        r_idle = sum((f[1] - f[0]) - overlap_ns(f[0], f[1], render_sorted) for f in window_frames)
        pacing_total = sum(overlap_ns(f[0], f[1], pacing) for f in window_frames)
        fence_total = sum(overlap_ns(f[0], f[1], fence) for f in window_frames)
        blocked_total = sum(overlap_ns(f[0], f[1], blocks) for f in window_frames)
        # The pacing sleep lies outside the phases (run()'s frame start);
        # the slot fence wait is inside M2.
        main_busy_total = sum(overlap_ns(f[0], f[1], main_work) for f in window_frames) - fence_total
        n = len(window_frames)
        print()
        print(f"thread overlap over the quiet window ({n} ticks, {r_count} render phases; render thread {'dedicated' if render_thread != main_thread else 'inline'}):")
        print(f"  R per tick                 {us(r_total / r_count):8.0f} us")
        print(f"  R while main works         {us(r_working / r_count):8.0f} us ({100.0 * r_working / r_total:5.1f}% of R; in M2 {us(r_in_m2 / r_count):.0f} us)")
        print(f"  R while main sleeps (pacer){us(r_in_sleep / r_count):8.0f} us ({100.0 * r_in_sleep / r_total:5.1f}% of R)")
        print(f"  R while main in fence wait {us(r_in_fence / r_count):8.0f} us ({100.0 * r_in_fence / r_total:5.1f}% of R)")
        print(f"  R while main blocked on R  {us(r_blocked / r_count):8.0f} us ({100.0 * r_blocked / r_total:5.1f}% of R; kick/join)")
        print(f"  R idle per tick            {us(r_idle / n):8.0f} us ({100.0 * r_idle / tick_total:5.1f}% of tick)")
        print(f"  main per tick: work {us(main_busy_total / n):.0f} us, pacing sleep {us(pacing_total / n):.0f} us, slot fence wait {us(fence_total / n):.0f} us, blocked on R {us(blocked_total / n):.0f} us")

    # Which zones fill the main thread and the render thread in those frames,
    # per frame: every zone whose start lies in the frame (main thread) or in
    # the R that the frame kicked (render thread), above a threshold, grouped
    # by name so the wait that carries the alternation shows up.
    threshold_ns = 200_000
    skip = {"frame", RENDER_PHASE, *MAIN_PHASES}
    skip.update(name for name in zones if ("::" in name) and (name.rsplit("::", 1)[1] in skip))
    print()
    print(f"zones >= {threshold_ns / 1000:.0f} us per frame (main thread {main_thread}, render thread {render_thread}); sums per name:")
    previous_render_end = None
    for index, frame in enumerate(picked):
        start, end, _thread = frame
        kicks = inside((start, end), zones.get("kick", []))
        kick_start = kicks[0][0] if kicks else start
        renders = [r for r in render_sorted if r[0] >= kick_start]
        render = renders[0] if renders else None
        main_sums = defaultdict(int)
        render_sums = defaultdict(int)
        for name, intervals in zones.items():
            if name in skip:
                continue
            for z_start, z_end, thread in intervals:
                if z_end <= start:
                    continue
                if (z_start >= end) and ((render is None) or (z_start >= render[1])):
                    break
                if (thread == main_thread) and (z_start >= start) and (z_start < end):
                    main_sums[name] += z_end - z_start
                elif render and (thread == render_thread) and (z_start >= render[0]) and (z_start < render[1]):
                    render_sums[name] += z_end - z_start
        main_text = ", ".join(f"{name} {us(v):.0f}" for name, v in sorted(main_sums.items(), key=lambda item: -item[1]) if v >= threshold_ns)
        render_text = ", ".join(f"{name} {us(v):.0f}" for name, v in sorted(render_sums.items(), key=lambda item: -item[1]) if v >= threshold_ns)
        print(f"  frame {index} tick {us(end - start):.0f}:")
        print(f"    main:   {main_text or '-'}")
        print(f"    render: {render_text or '-'}")

    # GPU zones per frame: total GPU time of the zones that start inside each
    # frame's R (by GPU timestamp, which Tracy aligns to the CPU clock).
    if gpu_rows_wanted:
        gpu_rows = export(csvexport, capture, ["-g"])
        if gpu_rows:
            gpu = []
            for row in gpu_rows:
                g_start = int(row["Time from start of program"])
                gpu.append((g_start, g_start + int(row["GPU execution time"]), row["name"]))
            gpu.sort()
            print()
            print("GPU zones per frame (zone starts inside the frame's tick, by Tracy's GPU-to-CPU alignment; us):")
            for index, frame in enumerate(picked):
                start, end, _thread = frame
                sums = defaultdict(int)
                for g_start, g_end, name in gpu:
                    if (g_start >= start) and (g_start < end):
                        sums[name] += g_end - g_start
                total = sum(sums.values())
                top = ", ".join(f"{name} {us(v):.0f}" for name, v in sorted(sums.items(), key=lambda item: -item[1])[:6])
                print(f"  frame {index}: total {us(total):.0f}: {top or '-'}")
    return 0


def analyze_pacing_records(pacing_path, frame_count):
    """Per-frame picture from the editor's own Frame_time_records (all times
    in the recorder's clock, seconds): the GPU bracket (first render pass
    begin to last render pass end, calibrated), the slot fence wait, the
    submit and the present call, so GPU busy time, the GPU gap between
    frames and the present interval come from the device, not from Tracy's
    GPU clock alignment."""
    if not os.path.isfile(pacing_path):
        return
    with open(pacing_path, encoding="utf-8") as f:
        pacing = json.load(f)
    frames = [fr for fr in pacing.get("frames", []) if fr.get("gpu_end", 0.0) > 0.0 and fr.get("gpu_begin", 0.0) > 0.0]
    if len(frames) < 3:
        print(f"\nframe pacing records: {len(frames)} frames with a GPU bracket; nothing to analyse")
        return
    frames.sort(key=lambda fr: fr["frame_id"])
    # Steady-state subset: drop the first quarter as warm-up of the window.
    used = frames[len(frames) // 4:]
    gpu_spans = [(fr["gpu_end"] - fr["gpu_begin"]) * 1e3 for fr in used]
    gpu_gaps = [(b["gpu_begin"] - a["gpu_end"]) * 1e3 for a, b in zip(used, used[1:])]
    present_intervals = [(b["present_request_time"] - a["present_request_time"]) * 1e3 for a, b in zip(used, used[1:]) if a.get("present_request_time", 0.0) > 0.0 and b.get("present_request_time", 0.0) > 0.0]
    gpu_end_intervals = [(b["gpu_end"] - a["gpu_end"]) * 1e3 for a, b in zip(used, used[1:])]
    print()
    print(f"frame pacing records over {len(used)} frames (refresh period {pacing.get('refresh_period', 0.0) * 1e3:.3f} ms):")
    print(f"  GPU span      mean {statistics.fmean(gpu_spans):7.2f} ms  min {min(gpu_spans):7.2f}  max {max(gpu_spans):7.2f}  (first render pass begin to last render pass end)")
    print(f"  GPU gap       mean {statistics.fmean(gpu_gaps):7.2f} ms  min {min(gpu_gaps):7.2f}  max {max(gpu_gaps):7.2f}  (previous frame's GPU end to this frame's GPU begin)")
    print(f"  GPU end to end mean {statistics.fmean(gpu_end_intervals):6.2f} ms  stdev {statistics.pstdev(gpu_end_intervals):5.2f}  lag-1 autocorrelation {lag1_autocorrelation(gpu_end_intervals):+.2f}")
    if present_intervals:
        print(f"  present call interval mean {statistics.fmean(present_intervals):6.2f} ms  stdev {statistics.pstdev(present_intervals):5.2f}  lag-1 autocorrelation {lag1_autocorrelation(present_intervals):+.2f}")
    picked = used[-frame_count:]
    t0 = picked[0]["cpu_slot_begin"]
    print(f"  last {len(picked)} frames (ms relative to the first one's cpu_slot_begin):")
    print(f"  {'frame':>7} {'slot_begin':>10} {'fence_ms':>8} {'submit':>8} {'gpu_begin':>9} {'gpu_end':>8} {'gpu_ms':>7} {'gap_ms':>7} {'acq_ms':>7} {'present':>8} {'pres_blk':>8} {'pacer_ms':>8}")
    previous_end = None
    for fr in picked:
        rel = lambda key: (fr.get(key, 0.0) - t0) * 1e3 if fr.get(key, 0.0) > 0.0 else float("nan")
        gap = ((fr["gpu_begin"] - previous_end) * 1e3) if previous_end is not None else float("nan")
        previous_end = fr["gpu_end"]
        acquire = (fr.get("acquire_end", 0.0) - fr.get("acquire_begin", 0.0)) * 1e3
        present_block = (fr.get("present_return_time", 0.0) - fr.get("present_request_time", 0.0)) * 1e3
        pacer = (fr.get("pacer_wait_end", 0.0) - fr.get("pacer_wait_begin", 0.0)) * 1e3
        print(f"  {fr['frame_id']:7d} {rel('cpu_slot_begin'):10.2f} {fr.get('fence_wait_ms', 0.0):8.2f} {rel('submit_time'):8.2f} {rel('gpu_begin'):9.2f} {rel('gpu_end'):8.2f} "
              f"{(fr['gpu_end'] - fr['gpu_begin']) * 1e3:7.2f} {gap:7.2f} {acquire:7.2f} {rel('present_request_time'):8.2f} {present_block:8.2f} {pacer:8.2f}")


# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default="build_vs2026_vulkan/bin/Release/editor.exe", help="editor executable (relative to the repo root or absolute)")
    parser.add_argument("--scene", default=DEFAULT_SCENE, help="scene file to open (repo-root relative)")
    parser.add_argument("--camera-translation", default=json.dumps(DEFAULT_CAMERA_TRANSLATION), help="JSON [x,y,z] world translation for the scene's Camera node")
    parser.add_argument("--camera-rotation", default=json.dumps(DEFAULT_CAMERA_ROTATION), help="JSON [x,y,z,w] world rotation for the scene's Camera node")
    parser.add_argument("--root-translation", default=json.dumps(DEFAULT_ROOT_TRANSLATION), help="JSON [x,y,z] world translation for the import root node")
    parser.add_argument("--root-rotation", default=json.dumps(DEFAULT_ROOT_ROTATION), help="JSON [x,y,z,w] world rotation for the import root node")
    parser.add_argument("--settle-s", type=float, default=10.0, help="seconds of no MCP traffic after the editor reports idle, before the measurement window")
    parser.add_argument("--measure-s", type=float, default=3.0, help="seconds of the quiet measurement window (no MCP traffic)")
    parser.add_argument("--frames", type=int, default=6, help="consecutive frames shown in the per-frame picture (the last ones of the quiet window)")
    parser.add_argument("--timeout", type=float, default=900.0, help="seconds to wait for the scene open to settle")
    parser.add_argument("--tracy-dir", default=os.environ.get("ERHE_TRACY_DIR", ""), help="directory with tracy-capture and tracy-csvexport (default ERHE_TRACY_DIR)")
    parser.add_argument("--out", default=os.path.join(REPO_ROOT, "logs", "bistro_frames.tracy"), help="capture file")
    parser.add_argument("--port", type=int, default=None, help="preferred MCP port (ERHE_MCP_PORT)")
    parser.add_argument("--graphics-override", action="append", default=[], metavar="KEY=JSON", help="top-level erhe_graphics.json value for this run, e.g. force_disable_vsync=true (the file is restored at exit)")
    parser.add_argument("--analyze-only", action="store_true", help="skip the editor run; analyse --out")
    parser.add_argument("--keep-running", action="store_true", help="leave the editor running after the measurement (the capture is then analysed only after the editor exits by itself)")
    parser.add_argument("--test-download", action="store_true", help="download and verify the default scene archive even when the scene is present (into a temporary directory), then exit")
    args = parser.parse_args()

    drive_id = DEFAULT_SCENE_ARCHIVE_DRIVE_ID if (args.scene == DEFAULT_SCENE) else None
    if args.test_download:
        ensure_scene(args.scene, drive_id, DEFAULT_SCENE_ARCHIVE_ROOT, test_download=True)
        return 0

    if not args.tracy_dir:
        sys.exit("set ERHE_TRACY_DIR or pass --tracy-dir (directory with tracy-capture and tracy-csvexport)")
    capture_exe = find_tool(args.tracy_dir, "tracy-capture")
    csvexport_exe = find_tool(args.tracy_dir, "tracy-csvexport")
    if (capture_exe is None) or (csvexport_exe is None):
        sys.exit(f"tracy-capture / tracy-csvexport not found in {args.tracy_dir}")

    if args.analyze_only:
        result = analyze(args.out, csvexport_exe, args.frames, args.measure_s)
        analyze_pacing_records(args.out + ".pacing.json", args.frames)
        return result

    editor = args.editor if os.path.isabs(args.editor) else os.path.join(REPO_ROOT, args.editor)
    if not os.path.isfile(editor):
        sys.exit(f"editor not found: {editor}")
    ensure_scene(args.scene, drive_id, DEFAULT_SCENE_ARCHIVE_ROOT, test_download=False)
    if os.path.exists(LOG_PATH):
        os.remove(LOG_PATH)
    if os.path.exists(args.out):
        os.remove(args.out)

    graphics_json = os.path.join(REPO_ROOT, "config", "editor", "erhe_graphics.json")
    graphics_backup = None
    if args.graphics_override:
        with open(graphics_json, encoding="utf-8", newline="") as f:
            graphics_backup = f.read()
        values = json.loads(graphics_backup)
        for spec in args.graphics_override:
            key, _sep, value = spec.partition("=")
            values[key] = json.loads(value)
        with open(graphics_json, "w", encoding="utf-8", newline="") as f:
            f.write(json.dumps(values, indent=4) + "\n")
    try:
        return run_and_analyze(args, capture_exe, csvexport_exe, editor)
    finally:
        if graphics_backup is not None:
            with open(graphics_json, "w", encoding="utf-8", newline="") as f:
                f.write(graphics_backup)


def run_and_analyze(args, capture_exe, csvexport_exe, editor):
    env = dict(os.environ, ERHE_AI_DRIVER="1")
    if args.port is not None:
        env["ERHE_MCP_PORT"] = str(args.port)
    creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    editor_process = subprocess.Popen(
        [editor, "--no-scene"], cwd=REPO_ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        creationflags=creation_flags
    )
    port, ready_s = wait_for_ready(editor_process, 600.0)
    if port is None:
        sys.exit(f"editor did not reach frame 12 (exit code {editor_process.poll()}); see logs/log.txt")
    print(f"editor ready in {ready_s:.1f} s, MCP port {port}")
    # The capture connects once the editor runs: connected before the
    # editor's Tracy listener was up, it was dropped in the handshake
    # (measured on macOS, "client has disconnected during the initial
    # connection handshake") and no capture was written. With
    # ERHE_TRACY_ON_DEMAND the editor records from the connection on, which
    # covers the scene open and the quiet window.
    capture_process = subprocess.Popen([capture_exe, "-o", args.out, "-f"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)

    scene_file = os.path.basename(args.scene)
    start = time.time()
    reply = mcp(port, "open_scene", {"path": args.scene})
    idle_s = wait_until_settled(port, scene_file, args.timeout)
    if idle_s is None:
        print(f"open_scene did not settle in {args.timeout:.0f} s: {str(reply)[:200]}")
    else:
        print(f"open: idle after {idle_s:.1f} s")
    scene_name = next((name for name in (scene_names(port) or []) if scene_file in name), None)
    if scene_name is None:
        sys.exit("scene not listed")
    # The editor's own session state: the scene's camera and the glTF import
    # root (set_node_transform is absolute, world space).
    for node_name, translation, rotation in (
        ("Camera", json.loads(args.camera_translation), json.loads(args.camera_rotation)),
        (scene_name, json.loads(args.root_translation), json.loads(args.root_rotation)),
    ):
        reply = mcp(port, "set_node_transform", {
            "scene_name": scene_name, "node_name": node_name, "space": "world",
            "translation": translation, "rotation_xyzw": rotation,
        })
        if not (isinstance(reply, dict) and not reply.get("isError")):
            print(f"set_node_transform {node_name}: {str(reply)[:300]}")
    viewports = mcp(port, "get_viewports")
    if isinstance(viewports, dict):
        for viewport in viewports.get("viewports", []):
            print(f"viewport '{viewport.get('title')}': scene {viewport.get('scene')!r} camera {viewport.get('camera')!r} "
                  f"{viewport.get('width')}x{viewport.get('height')}")
    else:
        print(f"get_viewports: {str(viewports)[:300]}")
    # Any deferred work the transform change queued (lightmap / BVH / draw
    # list rebuilds) settles before the silence.
    wait_until_settled(port, scene_file, args.timeout)
    print(f"settling {args.settle_s:.0f} s without MCP traffic, then measuring {args.measure_s:.0f} s")
    log_offset = len(read_log())
    time.sleep(args.settle_s)
    # One last MCP call marks the start of the quiet window in the capture;
    # it also restarts the frame pacing capture (the Frame_time_record of
    # every frame: fence wait, GPU bracket, submit, present), read back after
    # the window.
    mcp(port, "set_frame_pacing_capture", {"clear": True, "max_frames": 0})
    time.sleep(args.measure_s)
    status = mcp(port, "get_frame_pacing_status")
    if isinstance(status, dict):
        print(f"frame pacing: tier {status.get('tier')}, refresh {status.get('display_refresh_period_ms')} ms, enforce {status.get('frame_pacing_enforce')}")
    pacing = mcp(port, "get_frame_pacing_frames", {"count": 2000})
    pacing_path = args.out + ".pacing.json"
    if isinstance(pacing, dict) and ("frames" in pacing):
        with open(pacing_path, "w", encoding="utf-8", newline="") as f:
            f.write(json.dumps(pacing, indent=1) + "\n")
        print(f"frame pacing records: {len(pacing['frames'])} frames -> {pacing_path}")
    else:
        print(f"frame pacing records unavailable: {str(pacing)[:200]}")
    rows = phase_lines(read_log()[log_offset:])
    if rows:
        print(f"phase log lines during settle + measure ({len(rows)}):")
        for row in rows:
            print("  " + " ".join(f"{k} {v:.0f}" for k, v in row.items() if k in ("full tick", "M1", "frame_start_wait", "M2", "slot_acquire", "scene", "M3", "R", "swapchain", "join_wait")))

    if args.keep_running:
        print(f"editor left running on port {port}; analyse later with --analyze-only once it has exited")
        capture_process.wait()
        return analyze(args.out, csvexport_exe, args.frames, args.measure_s)

    mcp(port, "request_exit")
    try:
        editor_process.wait(120)
    except subprocess.TimeoutExpired:
        editor_process.kill()
    try:
        capture_process.wait(300)
    except subprocess.TimeoutExpired:
        capture_process.kill()
    print()
    result = analyze(args.out, csvexport_exe, args.frames, args.measure_s)
    analyze_pacing_records(args.out + ".pacing.json", args.frames)
    return result


if __name__ == "__main__":
    sys.exit(main())
