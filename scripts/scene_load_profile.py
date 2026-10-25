#!/usr/bin/env python3
"""Time a scene open in a running editor, optionally under a Tracy capture.

Launches the editor with --no-scene and ERHE_AI_DRIVER=1 from the repo root,
waits for the main loop, then for each requested open: calls open_scene over
MCP, polls get_async_status until the editor reports idle and the scene is
listed, records the wall time, and closes the scene again. The second open in
the same process shows what the caches (BVH, mesh optimizer) save. At the end
the editor is asked to exit and the per-phase log lines are summarized.

With --tracy-dir the whole run is captured with tracy-capture into --out; an
editor built with ERHE_TRACY_ON_DEMAND=ON works because the capture tool is
connected before the open starts. Summarize the capture afterwards with
scripts/tracy_zone_summary.py or the tracy MCP.

Examples:
  py -3 scripts/scene_load_profile.py --editor build_vs2026_vulkan_headless/bin/Debug/editor.exe
  py -3 scripts/scene_load_profile.py --editor build_vs2026_vulkan_headless/bin/Release/editor.exe --opens 2 --tracy-dir D:/tracy --out logs/bistro_release.tracy
"""

import argparse
import base64
import json
import os
import re
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_PATH = os.path.join(REPO_ROOT, "logs", "log.txt")
READY_LINE = "Main loop: completed frame 12"
DEFAULT_SCENE = "res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf"


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


def scene_stem(scene_path):
    """The listed name of a scene opened from scene_path: load_scene names the
    scene after the file stem (no extension). The --no-scene startup scene
    ("Scene 1") never matches."""
    return os.path.splitext(os.path.basename(scene_path))[0]


def scene_names(port):
    """Names of the listed scenes, or None when list_scenes did not answer
    (the request expires while the main thread is stalled, e.g. inside a
    long scene close; an expired reply must not read as "no scenes")."""
    reply = mcp(port, "list_scenes")
    if isinstance(reply, dict) and ("scenes" in reply):
        return [scene.get("name", "") for scene in reply.get("scenes", [])]
    return None


def wait_until_settled(port, scene_path, timeout_s):
    """Returns (seconds until the scene is listed, seconds until idle)."""
    start = time.time()
    listed_s = None
    scene_file = scene_stem(scene_path)
    while (time.time() - start) < timeout_s:
        status = mcp(port, "get_async_status")
        if (listed_s is None) and any(scene_file in name for name in (scene_names(port) or [])):
            listed_s = time.time() - start
        if isinstance(status, dict) and status.get("idle") and (listed_s is not None):
            return listed_s, time.time() - start
        time.sleep(0.25)
    return listed_s, None


def summarize_log(text, marker_offset):
    lines = text[marker_offset:].splitlines()
    optimize_ms = 0.0
    optimize_count = 0
    replayed = 0
    replayed_ms = 0.0
    bvh_build_ms = 0
    bvh_build_count = 0
    bvh_save_failed = 0
    for line in lines:
        # Per-primitive lines only: the "mesh optimize totals:" summary also
        # says "replayed from cache" and must not count as a replay.
        m = re.search(r"mesh optimize (?!totals:).*, replayed from cache(?: in ([0-9.]+) ms)?$", line)
        if m:
            replayed += 1
            replayed_ms += float(m.group(1) or 0.0)
        else:
            m = re.search(r"mesh optimize (?!totals:).*?, ([0-9.]+) ms$", line)
            if m:
                optimize_ms += float(m.group(1))
                optimize_count += 1
        m = re.search(r"BVH build .* in (\d+) ms", line)
        if m:
            bvh_build_ms += int(m.group(1))
            bvh_build_count += 1
        if "BVH save failed" in line:
            bvh_save_failed += 1
    print(f"  log: mesh optimize {optimize_count} meshes, {optimize_ms:.0f} ms summed; {replayed} replayed from cache, {replayed_ms:.0f} ms summed")
    print(f"  log: BVH build {bvh_build_count} geometries, {bvh_build_ms} ms summed; {bvh_save_failed} save failures")
    for line in lines:
        if re.search(r"(parse_gltf|Gltf|gltf).*\b(ms|s)\b", line) and re.search(r"\d+(\.\d+)? ?(ms|s)\b", line):
            print("  log: " + line.strip()[:200])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True, help="editor executable (relative to the repo root or absolute)")
    parser.add_argument("--scene", default=DEFAULT_SCENE, help="scene file to open (repo-root relative)")
    parser.add_argument("--opens", type=int, default=1, help="how many times to open (and close) the scene")
    parser.add_argument("--timeout", type=float, default=1800.0, help="seconds to wait for one open to settle")
    parser.add_argument("--tracy-dir", default=os.environ.get("ERHE_TRACY_DIR", ""), help="directory of tracy-capture; empty = no capture")
    parser.add_argument("--out", default=os.path.join(REPO_ROOT, "logs", "scene_load.tracy"), help="capture file")
    parser.add_argument("--port", type=int, default=None, help="preferred MCP port (ERHE_MCP_PORT)")
    parser.add_argument("--keep-open", action="store_true", help="do not close the scene after the last open")
    parser.add_argument("--render-image", default=None, metavar="PNG", help="after each open settles, render the scene through a fixed camera to this path (an index suffix is added for the second and later opens); byte-compare it against a baseline")
    parser.add_argument("--tool", default="open_scene", choices=["open_scene", "load_scene"], help="open_scene = Scene_open_operation with the parse on the main thread (asset browser Load); load_scene = File > Load Scene (async loader)")
    args = parser.parse_args()

    editor = args.editor if os.path.isabs(args.editor) else os.path.join(REPO_ROOT, args.editor)
    if not os.path.isfile(editor):
        sys.exit(f"editor not found: {editor}")
    if os.path.exists(LOG_PATH):
        os.remove(LOG_PATH)

    capture_process = None
    if args.tracy_dir:
        capture_exe = next(
            (p for p in (os.path.join(args.tracy_dir, "tracy-capture.exe"), os.path.join(args.tracy_dir, "tracy-capture")) if os.path.isfile(p)),
            None
        )
        if capture_exe is None:
            sys.exit(f"tracy-capture not found in {args.tracy_dir}")
        if os.path.exists(args.out):
            os.remove(args.out)
        capture_process = subprocess.Popen([capture_exe, "-o", args.out, "-f"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.0)

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

    scene_path = args.scene
    for open_index in range(1, args.opens + 1):
        marker_offset = len(read_log())
        start = time.time()
        reply = mcp(port, args.tool, {"path": scene_path})
        listed_s, idle_s = wait_until_settled(port, scene_path, args.timeout)
        total_s = time.time() - start
        listed_text = f"{listed_s:.2f} s" if listed_s is not None else "never"
        idle_text = f"{idle_s:.2f} s" if idle_s is not None else f"not idle after {total_s:.0f} s"
        print(f"open {open_index}: scene listed after {listed_text}, idle after {idle_text}")
        if not isinstance(reply, dict):
            print(f"  open_scene replied: {str(reply)[:200]}")
        summarize_log(read_log(), marker_offset)
        if args.render_image:
            image_path = args.render_image if open_index == 1 else re.sub(r"(\.[^.]+)$", lambda m: "_" + str(open_index) + m.group(1), args.render_image)
            image_path = os.path.abspath(image_path)
            # Fixed camera over the Bistro content fit (center 24.4 -4.2 18.0, radius 129); the same
            # view for every build so the images byte-compare.
            render = mcp(port, "render_scene_image", {
                "camera": {"eye": [114.4, 45.8, 108.0], "target": [24.4, -4.2, 18.0], "up": [0.0, 1.0, 0.0],
                           "fov_y_degrees": 45.0, "near": 0.1, "far": 1000.0},
                "width": 1024, "height": 768, "output": "png", "path": image_path
            })
            print(f"  render: {image_path} {'ok' if os.path.isfile(image_path) else 'FAILED: ' + str(render)[:200]}")
        if (open_index < args.opens) or not args.keep_open:
            close_log_offset = len(read_log())
            for name in (scene_names(port) or []):
                if scene_stem(scene_path) in name:
                    mcp(port, "close_scene", {"scene_name": name})
            close_start = time.time()
            gone_s = None
            while (time.time() - close_start) < 120.0:
                names = scene_names(port)
                if (names is not None) and not any(scene_stem(scene_path) in name for name in names):
                    gone_s = time.time() - close_start
                    break
                time.sleep(0.25)
            # The close runs on the main thread; a stalled main loop delays the
            # list_scenes reply, so this is the user-visible close time.
            print(f"close {open_index}: scene gone after {gone_s:.2f} s" if gone_s is not None else f"close {open_index}: scene still listed after 120 s")
            # The closed scene's mesh payload is freed by a worker task
            # (Scene_payload_release) that runs for seconds after the close and
            # contends with a reopen on the process heap. Wait for its log line
            # so the next open measures the caches, not that contention.
            freed_s = None
            while (time.time() - close_start) < 300.0:
                m = re.search(r"scene payload release: .* released in (\d+) ms", read_log()[close_log_offset:])
                if m:
                    freed_s = time.time() - close_start
                    break
                time.sleep(0.25)
            print(f"release {open_index}: payload freed after {freed_s:.2f} s" if freed_s is not None else f"release {open_index}: no payload release line after 300 s")
            time.sleep(1.0)

    mcp(port, "request_exit")
    try:
        editor_process.wait(120)
    except subprocess.TimeoutExpired:
        editor_process.kill()
    if capture_process is not None:
        try:
            capture_process.wait(300)
        except subprocess.TimeoutExpired:
            capture_process.kill()
        print(f"capture: {args.out}")


if __name__ == "__main__":
    main()
