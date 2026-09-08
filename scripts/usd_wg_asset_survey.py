#!/usr/bin/env python3
"""USD Assets Working Group survey (doc/usd-compatibility-plan.md step S1).

Opens every entry asset of a local clone of github.com/usd-wg/assets in a
headless erhe editor, records what the editor made of it, and writes
doc/usd-wg-assets.md plus logs/usd_wg_survey/summary.json.

Run it from the repo root, with the clone's root passed as --root or in the
ERHE_USD_WG_ASSETS environment variable:

    py -3 scripts/usd_wg_asset_survey.py --root <usd-wg-assets>
    py -3 scripts/usd_wg_asset_survey.py --from-summary      (doc only)

It needs the headless Vulkan build
(build_vs2026_vulkan_headless/src/editor/Debug/editor.exe, see AGENTS.md
"In-editor MCP server"); it launches and, after a crash or a load that does
not answer in time, relaunches the editor itself.

Entry-asset rule
----------------
The repository holds `full_assets/<name>/`, `test_assets/<name>/` and
`intent-vfx/scenes/`, and most of its 280-odd USD files are sub-layers of
one entry file. A folder's ENTRIES are:

1. the top-level `.usd` / `.usda` / `.usdc` / `.usdz` files the folder's
   README.md names, when it names any (Teapot's README names `Teapot.usd`
   and `DrawModes.usd`, so `Teapot_Geometry.usd`, `Teapot_Materials.usd`
   and `Teapot_Payload.usd` are sub-layers, not entries);
2. otherwise every top-level `.usd*` file whose name does not mark it as a
   sub-layer: a leading `_` (`_scene.usda`, `_parent_stage.usda`) or a stem
   ending in `_thumbnail`, `_Geometry`, `_Materials`, `_Payload` or `Geo`.

A folder that yields no entry (its top level holds no `.usd*` file at all)
is recursed into, skipping subfolders that only carry an entry's parts:
names starting with `_`, a name equal to the stem of a top-level file
(`ExternalReferenceBadTargetTest/` beside `ExternalReferenceBadTargetTest.usda`),
and the auxiliary folder names in AUX_DIRS (cards, thumbnails, screenshots,
textures, maps, media, geo, materials, ...). The index folders in
INDEX_DIRS - the ones whose top level holds an entry AND whose subfolders
hold entries of their own - are recursed into as well.

Variation families (a folder holding one file per value of one stage
knob, `framesPerSecond_24.usda` ... `framesPerSecond_128.usda`) are capped
at --max-per-folder entries; the doc records how many a folder had.

Verdicts
--------
`works`, `works, gap: <name>`, `fails: <cause>`, `crash`. The script assigns
a provisional verdict from the counts, the log and the screenshot; the doc
says which rows a human then corrected by eye.
"""

import argparse
import collections
import datetime
import json
import os
import pathlib
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

# --------------------------------------------------------------------------
# Entry enumeration
# --------------------------------------------------------------------------

USD_EXTENSIONS = {".usd", ".usda", ".usdc", ".usdz"}

# Folders that never hold an entry asset: documentation, imagery, and the
# resource folders an entry's layers point at.
AUX_DIRS = {
    "cards", "thumbnail", "thumbnails", "screenshot", "screenshots",
    "texture", "textures", "maps", "media", "src", "docs", "documentation",
    "labels", "utils", "geo", "layers", "example_materials",
    "material_surface_geo", "materials", "material", "looks",
    "global-colors", "global-textures",
}

# Folders whose own top level holds entries AND whose subfolders hold more.
INDEX_DIRS = {
    "vehicles", "usdz", "schematests", "foundation", "colorspacetests",
    "texturefileformattests", "relationshipencapsulationtests",
    "stage_composition", "stage_configuration", "wheels",
}

SUBLAYER_STEM = re.compile(r"(_thumbnail|_Geometry|_Materials|_Payload|Geo)$", re.IGNORECASE)

USD_NAME_IN_TEXT = re.compile(r"[A-Za-z0-9_.\-]+\.usd[acz]?")


def is_entry_file(path: pathlib.Path) -> bool:
    if path.name.startswith("_"):
        return False
    return SUBLAYER_STEM.search(path.stem) is None


def readme_named_files(folder: pathlib.Path) -> set:
    names = set()
    for child in folder.iterdir():
        if child.is_file() and child.name.lower() in ("readme.md", "readme.txt"):
            text = child.read_text(encoding="utf-8", errors="ignore")
            names.update(USD_NAME_IN_TEXT.findall(text))
    return names


def collect_entries(root: pathlib.Path, max_per_folder: int) -> list:
    """Return [{folder, path, folder_entry_count}] for every entry asset."""
    entries = []

    def scan(folder: pathlib.Path) -> None:
        top_level = [p for p in sorted(folder.iterdir())
                     if p.is_file() and (p.suffix.lower() in USD_EXTENSIONS)]
        candidates = [p for p in top_level if is_entry_file(p)]
        named = readme_named_files(folder)
        picked = [p for p in candidates if p.name in named] or candidates
        total_here = len(picked)
        if (max_per_folder > 0) and (total_here > max_per_folder):
            picked = picked[:max_per_folder]
        for path in picked:
            entries.append({
                "folder": folder.relative_to(root).as_posix(),
                "path": path.relative_to(root).as_posix(),
                "folder_entry_count": total_here,
            })
        recurse = (not top_level) or (folder.name.lower() in INDEX_DIRS)
        if not recurse:
            return
        part_folders = {p.stem.lower() for p in top_level}
        for child in sorted(folder.iterdir()):
            if not child.is_dir():
                continue
            name = child.name.lower()
            if name.startswith("_") or (name in AUX_DIRS) or (name in part_folders):
                continue
            scan(child)

    for group in ("full_assets", "test_assets"):
        group_dir = root / group
        if not group_dir.is_dir():
            continue
        for child in sorted(group_dir.iterdir()):
            if child.is_dir() and (child.name.lower() not in AUX_DIRS):
                scan(child)

    scenes_dir = root / "intent-vfx" / "scenes"
    if scenes_dir.is_dir():
        for path in sorted(scenes_dir.iterdir()):
            if path.is_file() and (path.suffix.lower() in USD_EXTENSIONS) and is_entry_file(path):
                entries.append({
                    "folder": scenes_dir.relative_to(root).as_posix(),
                    "path": path.relative_to(root).as_posix(),
                    "folder_entry_count": 0,
                })
    return entries


def entry_slug(relative_path: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", relative_path)


# --------------------------------------------------------------------------
# Reference images
# --------------------------------------------------------------------------

# The repository ships renders of each asset next to it. `screenshots/` holds
# what the asset is meant to look like (usdview, Omniverse, Blender, ...) and
# `thumbnails/` a smaller render of the same; `cards/` holds the draw-mode
# card textures and `textures/` the asset's own maps, neither of which is a
# reference image.
REFERENCE_DIRS = ("screenshots", "screenshot", "thumbnails", "thumbnail")
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp"}


def reference_images(entry_path: pathlib.Path, root: pathlib.Path, limit: int = 6) -> list:
    """Repo-relative reference renders for one entry, most relevant first:
    the ones named after the entry, then the usdview / usdrecord renders,
    then the rest of the folder's images."""
    folder = entry_path.parent
    stem = entry_path.stem.lower()
    found = []
    for directory_index, directory in enumerate(REFERENCE_DIRS):
        reference_dir = folder / directory
        if not reference_dir.is_dir():
            continue
        for image in sorted(reference_dir.iterdir()):
            if not image.is_file() or (image.suffix.lower() not in IMAGE_SUFFIXES):
                continue
            name = image.stem.lower()
            name_rank = 0 if name.startswith(stem) else (1 if ("usdview" in name) or ("usdrecord" in name) else 2)
            found.append((directory_index, name_rank, image.relative_to(root).as_posix()))
    found.sort()
    return [path for _, _, path in found[:limit]]


# --------------------------------------------------------------------------
# MCP plumbing (same shape as scripts/scene_roundtrip_verify.py)
# --------------------------------------------------------------------------

class EditorDown(Exception):
    """The editor process is gone or stopped answering."""


class Mcp:
    def __init__(self, port: int) -> None:
        self.url = f"http://127.0.0.1:{port}/mcp"
        self.headers = {"Content-Type": "application/json"}
        token_path = pathlib.Path.home() / ".agents" / "erhe_mcp_token"
        if token_path.is_file():
            token = token_path.read_text(encoding="utf-8").strip()
            if token:
                self.headers["Authorization"] = "Bearer " + token

    def rpc(self, method: str, params: dict, timeout: float) -> dict:
        body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode("utf-8")
        request = urllib.request.Request(self.url, data=body, headers=self.headers)
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return json.loads(response.read().decode("utf-8"))
        except (urllib.error.URLError, ConnectionError, OSError) as error:
            raise EditorDown(str(error)) from error

    def call_once(self, tool: str, arguments: dict, timeout: float):
        payload = self.rpc("tools/call", {"name": tool, "arguments": arguments or {}}, timeout)
        if "error" in payload:
            raise RuntimeError(f"{tool}: {payload['error']}")
        result = payload.get("result", {})
        content = result.get("content")
        text = content[0]["text"] if (isinstance(content, list) and content and ("text" in content[0])) else ""
        if result.get("isError", False):
            raise RuntimeError(f"{tool}: {text}")
        try:
            return json.loads(text)
        except (json.JSONDecodeError, TypeError):
            return text

    def call(self, tool: str, arguments: dict = None, timeout: float = 120.0):
        """The server answers "Request timed out" / "Server busy" while the main
        thread is inside a long load; that is not a result, so retry until the
        queue drains (scripts/scene_roundtrip_verify.py takes the same line)."""
        deadline = time.monotonic() + timeout
        while True:
            try:
                return self.call_once(tool, arguments, timeout)
            except RuntimeError as error:
                text = str(error)
                busy = ("Request timed out" in text) or ("Server busy" in text)
                if (not busy) or (time.monotonic() >= deadline):
                    raise
                time.sleep(1.0)


class Editor:
    """The headless editor process, restarted after every crash."""

    def __init__(self, exe: pathlib.Path, port: int, log_path: pathlib.Path) -> None:
        self.exe = exe
        self.port = port
        self.log_path = log_path
        self.process = None
        self.mcp = Mcp(port)
        self.launches = 0
        self.baseline_scenes = set()

    def start(self, ready_timeout: float) -> None:
        self.stop()
        environment = dict(os.environ)
        environment["ERHE_AI_DRIVER"] = "1"
        environment["ERHE_MCP_PORT"] = str(self.port)
        creation_flags = 0
        if os.name == "nt":
            creation_flags = subprocess.CREATE_NO_WINDOW  # hidden console (AGENTS.md)
        self.process = subprocess.Popen(
            [str(self.exe)], cwd=str(pathlib.Path.cwd()), env=environment,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=creation_flags,
        )
        self.launches += 1
        deadline = time.monotonic() + ready_timeout
        while True:
            if self.process.poll() is not None:
                raise RuntimeError(f"editor exited with code {self.process.returncode} before answering MCP")
            try:
                self.mcp.rpc("initialize", {}, timeout=10.0)
                self.baseline_scenes = {s.get("name") for s in
                                        self.mcp.call("list_scenes", {}, timeout=120.0).get("scenes", [])}
                return
            except EditorDown:
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"editor did not answer MCP within {ready_timeout} s")
                time.sleep(1.0)

    def stop(self) -> None:
        if self.process is None:
            return
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=20)
        self.process = None

    def alive(self) -> bool:
        return (self.process is not None) and (self.process.poll() is None)

    def log_size(self) -> int:
        return self.log_path.stat().st_size if self.log_path.is_file() else 0

    def log_since(self, position: int) -> list:
        if not self.log_path.is_file():
            return []
        with self.log_path.open("r", encoding="utf-8", errors="replace") as handle:
            handle.seek(min(position, self.log_path.stat().st_size))
            return handle.read().splitlines()


# --------------------------------------------------------------------------
# Log and screenshot inspection
# --------------------------------------------------------------------------

# One erhe log line: "[hh:mm:ss.mmm] [L] [logger] message". A message that
# carries a multi-line payload (LightUSD's Tydra reports several failures in
# one string) continues on the following lines, which have no prefix.
LOG_LINE = re.compile(r"^\[\d\d:\d\d:\d\d\.\d+\]\s+\[([A-Z])\]\s+\[([^\]]+)\]\s*(.*)$")

# A source location LightUSD prefixes each of its own diagnostics with.
# Everything before the LAST one is context the erhe wrapper and LightUSD's
# outer frames added, so the cause itself starts after it.
SOURCE_LOCATION = re.compile(r"[A-Za-z]:[\\/][^\s]*?\.(?:cc|cpp|hh|hpp|h):(?:[A-Za-z_][A-Za-z0-9_:<> ]*)?\(\):\d+\s+")

# Messages that report how this build is configured, not what the file needs.
BENIGN = [re.compile(r"Threading is disabled for this build", re.IGNORECASE)]

# Volatile parts of a message, so two messages about different prims
# deduplicate into one gap.
NOISE = [
    (re.compile(r"'[^']*'"), "'*'"),
    (re.compile(r'"[^"]*"'), '"*"'),
    (re.compile(r"`[^`]*`"), "`*`"),
    (re.compile(r"[A-Za-z]:[\\/][^\s]*"), "<path>"),
    (re.compile(r"/[A-Za-z0-9_/.]{4,}"), "<path>"),
    (re.compile(r"(?<![A-Za-z0-9_.])-?\d+(\.\d+)?"), "N"),
    (re.compile(r"\bin Prim \S+"), "in Prim <prim>"),
    # One hierarchy-sanity failure per prim is one cause, not one per prim.
    (re.compile(r"^Item \S+ child \S+ parent =="), "Item <item> child <child> parent =="),
]


def log_message(line: str) -> str:
    """The message of one log line, without the timestamp / level / logger."""
    match = LOG_LINE.match(line)
    return (match.group(3) if match else line).strip()


def strip_source_locations(message: str) -> str:
    """Keep only what follows the last source location: the cause itself."""
    last = None
    for match in SOURCE_LOCATION.finditer(message):
        last = match
    return message[last.end():] if last is not None else message


def is_benign(message: str) -> bool:
    return any(pattern.search(message) for pattern in BENIGN)


def normalize_message(message: str) -> str:
    text = strip_source_locations(message)
    for pattern, replacement in NOISE:
        text = pattern.sub(replacement, text)
    return re.sub(r"\s+", " ", text).strip()


def diagnostics(lines: list) -> list:
    """[{level, message, count, example}] for the warning and error lines given.

    Each line of a multi-line message counts on its own: LightUSD packs
    several distinct failures ("Failed to load texture image", "Failed to
    read property") into one warning, and each is a different gap.
    """
    found = collections.OrderedDict()
    level = None
    for line in lines:
        match = LOG_LINE.match(line)
        if match is not None:
            marker = match.group(1)
            if marker in ("W",):
                level = "warning"
            elif marker in ("E", "C"):
                level = "error"
            else:
                level = None
                continue
            body = match.group(3)
        else:
            if level is None:
                continue
            body = line
        text = body.strip()
        if not text:
            continue
        entry_level = "note" if is_benign(text) else level
        key = (entry_level, normalize_message(text))
        if not key[1]:
            continue
        if key in found:
            found[key]["count"] += 1
        else:
            found[key] = {"level": entry_level, "message": key[1], "count": 1,
                          "example": strip_source_locations(text).strip()[:300]}
    return list(found.values())


# The fraction of the captured frame that is inside the 3D viewport, clear of
# the docked ImGui windows around it. The whole frame is never flat - the
# editor's own UI fills the left half - so "renders nothing" is decided on
# this region alone.
VIEWPORT_REGION = (0.52, 0.10, 0.98, 0.95)


def screenshot_stats(path: pathlib.Path) -> dict:
    try:
        from PIL import Image, ImageStat
    except ImportError:
        return {"available": False}
    if not path.is_file():
        return {"available": False}
    try:
        with Image.open(path) as image:
            gray = image.convert("L")
            width, height = gray.size
            left, top, right, bottom = VIEWPORT_REGION
            viewport = gray.crop((int(left * width), int(top * height),
                                  int(right * width), int(bottom * height)))
            frame_stat = ImageStat.Stat(gray)
            viewport_stat = ImageStat.Stat(viewport)
            viewport_extrema = viewport.getextrema()
    except Exception as error:  # a truncated or unreadable PNG is data too
        return {"available": False, "error": str(error)}
    return {
        "available": True,
        "frame_stddev": round(frame_stat.stddev[0], 3),
        "viewport_stddev": round(viewport_stat.stddev[0], 3),
        "viewport_min": viewport_extrema[0],
        "viewport_max": viewport_extrema[1],
        "flat": (viewport_extrema[0] == viewport_extrema[1]),
    }


# --------------------------------------------------------------------------
# One entry
# --------------------------------------------------------------------------

def scene_names(editor: Editor) -> set:
    scenes = editor.mcp.call("list_scenes", {}, timeout=60.0)
    return {s.get("name") for s in scenes.get("scenes", [])}


def wait_for_new_scene(editor: Editor, before: set, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not editor.alive():
            raise EditorDown("editor process exited during open_scene")
        added = scene_names(editor) - before
        if added:
            return sorted(added)[0]
        time.sleep(0.25)
    return ""


def wait_for_scene_gone(editor: Editor, name: str, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if name not in scene_names(editor):
            return True
        time.sleep(0.25)
    return False


def close_extra_scenes(editor: Editor, timeout: float) -> None:
    """A failed entry can still have left a scene behind (the load answered
    after the wait gave up); the next entry must start from the baseline."""
    for name in sorted(scene_names(editor) - editor.baseline_scenes):
        try:
            editor.mcp.call("close_scene", {"scene_name": name}, timeout=timeout)
            wait_for_scene_gone(editor, name, 30.0)
        except RuntimeError:
            return


def wait_frames(editor: Editor, count: int, timeout: float) -> None:
    """Let the editor render `count` frames. Every MCP call is dispatched on
    the main thread once per frame, so a round trip is at least one frame."""
    for _ in range(max(count, 0)):
        editor.mcp.call("list_scenes", {}, timeout=timeout)
        time.sleep(0.1)


def survey_entry(editor: Editor, root: pathlib.Path, entry: dict, shots_dir: pathlib.Path,
                 load_timeout: float, close_wait: float, settle_frames: int) -> dict:
    absolute = (root / entry["path"]).resolve()
    slug = entry_slug(entry["path"])
    shot = shots_dir / (slug + ".png")
    record = dict(entry)
    record.update({
        "slug": slug,
        "screenshot": shot.as_posix(),
        "references": reference_images(absolute, root),
        "authored_prims": None,
        "authored_types": [],
        "layers": [],
        "up_axis": "",
        "default_prim": "",
        "meters_per_unit": None,
        "describe_error": "",
        "loaded": False,
        "load_error": "",
        "scene_name": "",
        "prims": 0,
        "meshes": 0,
        "materials": 0,
        "lights": 0,
        "cameras": 0,
        "diagnostics": [],
        "scene_close": "",
        "screenshot_stats": {},
        "framing": {},
        "crash": False,
        "crash_detail": "",
        "log_tail": [],
        "verdict": "",
    })

    position = editor.log_size()

    try:
        description = editor.mcp.call("describe_usd_file", {"path": str(absolute)}, timeout=load_timeout)
        if isinstance(description, dict):
            record["authored_prims"] = description.get("prim_count")
            record["authored_types"] = description.get("prim_types", [])
            record["layers"] = description.get("layers", [])
            record["up_axis"] = description.get("up_axis", "")
            record["default_prim"] = description.get("default_prim", "")
            record["meters_per_unit"] = description.get("meters_per_unit")
            if description.get("warning"):
                record["describe_error"] = str(description["warning"])[:300]
    except EditorDown as error:
        record["crash"] = True
        record["crash_detail"] = f"describe_usd_file: {error}"
    except RuntimeError as error:
        record["describe_error"] = str(error)[:300]

    if not record["crash"]:
        try:
            before = scene_names(editor)
            editor.mcp.call("open_scene", {"path": str(absolute)}, timeout=load_timeout)
            scene = wait_for_new_scene(editor, before, load_timeout)
            if not scene:
                record["load_error"] = f"no scene appeared within {load_timeout:.0f} s"
            else:
                record["loaded"] = True
                record["scene_name"] = scene
                nodes = editor.mcp.call("get_scene_nodes", {"scene_name": scene}, timeout=load_timeout).get("nodes", [])
                record["prims"] = len(nodes)
                record["meshes"] = sum(1 for n in nodes if n.get("type") == "Mesh")
                record["materials"] = len(editor.mcp.call("get_scene_materials", {"scene_name": scene}, timeout=load_timeout).get("materials", []))
                record["lights"] = len(editor.mcp.call("get_scene_lights", {"scene_name": scene}, timeout=load_timeout).get("lights", []))
                record["cameras"] = len(editor.mcp.call("get_scene_cameras", {"scene_name": scene}, timeout=load_timeout).get("cameras", []))
                # Opening a scene leaves its viewport bound to nothing when the
                # file authors no camera, so the capture must frame the scene
                # first and then let the viewport render.
                record["framing"] = editor.mcp.call("frame_scene", {"scene_name": scene}, timeout=load_timeout)
                wait_frames(editor, settle_frames, load_timeout)
                editor.mcp.call("capture_screenshot", {"path": shot.as_posix()}, timeout=load_timeout)
                record["screenshot_stats"] = screenshot_stats(shot)
        except EditorDown as error:
            record["crash"] = True
            record["crash_detail"] = f"open_scene: {error}"
        except RuntimeError as error:
            record["load_error"] = str(error)[:300]

    if (not record["crash"]) and record["scene_name"]:
        try:
            editor.mcp.call("close_scene", {"scene_name": record["scene_name"]}, timeout=load_timeout)
            wait_for_scene_gone(editor, record["scene_name"], 30.0)
            time.sleep(close_wait)  # the leak watchdog reports 60 frames after the close
        except EditorDown as error:
            record["crash"] = True
            record["crash_detail"] = f"close_scene: {error}"
        except RuntimeError as error:
            record["load_error"] = record["load_error"] or str(error)[:300]

    lines = editor.log_since(position)
    record["diagnostics"] = diagnostics(lines)
    close_lines = [log_message(l) for l in lines if "scene-close " in l]
    if close_lines:
        record["scene_close"] = ("leak" if any("scene-close leak" in l for l in close_lines) else "clean")
    if record["crash"]:
        record["log_tail"] = [l[-300:] for l in lines[-25:]]
    record["verdict"] = provisional_verdict(record)
    return record


def compose_comparison(record: dict, root: pathlib.Path, out_dir: pathlib.Path) -> str:
    """One PNG holding the erhe capture beside the entry's first reference
    render, so the two can be judged side by side. Returns its path, or "".

    The capture is cropped to the viewport region (VIEWPORT_REGION) so the
    editor's docked windows do not take half the composite."""
    try:
        from PIL import Image
    except ImportError:
        return ""
    references = record.get("references") or []
    capture_path = pathlib.Path(record.get("screenshot", ""))
    if (not references) or (not capture_path.is_file()):
        return ""
    reference_path = root / references[0]
    if not reference_path.is_file():
        return ""
    try:
        with Image.open(capture_path) as capture_image:
            capture = capture_image.convert("RGB")
            width, height = capture.size
            left, top, right, bottom = VIEWPORT_REGION
            capture = capture.crop((int(left * width), int(top * height),
                                    int(right * width), int(bottom * height)))
        with Image.open(reference_path) as reference_image:
            reference = reference_image.convert("RGB")
    except Exception:
        return ""
    # Both halves are shown at the reference render's own height, so a file
    # whose authored camera the capture looks through puts the same view on
    # both sides and a difference between them is a real one.
    target_height = min(max(reference.height, 320), 900)
    panels = []
    for image in (capture, reference):
        scale = target_height / float(image.height)
        panels.append(image.resize((max(int(image.width * scale), 1), target_height)))
    gap = 8
    sheet = Image.new("RGB", (panels[0].width + gap + panels[1].width, target_height), (90, 90, 90))
    sheet.paste(panels[0], (0, 0))
    sheet.paste(panels[1], (panels[0].width + gap, 0))
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / (record["slug"] + ".png")
    sheet.save(out_path)
    return out_path.as_posix()


# --------------------------------------------------------------------------
# Verdicts and gaps
# --------------------------------------------------------------------------

def gap_name(message: str) -> str:
    """A few words naming what the editor lacks, from a log message."""
    text = message.strip().rstrip(".")
    text = re.sub(r"\s+", " ", text)
    return text[:80]


SUBLAYER_GAP = (
    "subLayers are not composed: a file whose content lives in a subLayer loads as an empty stage"
)


# The prim types whose presence in a file means the editor should have
# produced a mesh: UsdGeomMesh plus the implicit surfaces and the instancer.
GEOMETRY_PRIM_TYPES = {
    "Mesh", "Cube", "Sphere", "Cone", "Cylinder", "Capsule", "Plane", "PointInstancer",
}

# What swallowed the geometry, most specific first: the first pattern that
# matches one of the entry's log messages names the cause.
NO_MESH_CAUSES = [
    (re.compile(r"TODO: Prim type (\w+)"), "Tydra converts no geometry for the {0} schema"),
    (re.compile(r"PointInstancer .* resolved to no RenderMesh"), "a PointInstancer prototype resolved to no mesh"),
    (re.compile(r"authors composition arcs but carries no transform"), "a reference or payload arc sits on a prim that carries no transform"),
    (re.compile(r"failed to load reference target"), "a reference target failed to load"),
    (re.compile(r"Prefab '[^']*': prim"), "the referenced prim path is not in the target layer"),
    (re.compile(r"Prefab '[^']*' produced no nodes"), "the referenced layer produced no prims"),
    (re.compile(r"variant set .* authors .* opinion"), "the geometry sits behind a variant opinion that is not a material binding"),
]


def authors_geometry(record: dict) -> bool:
    return any((t.get("type_name") in GEOMETRY_PRIM_TYPES) and (t.get("count", 0) > 0)
               for t in (record.get("authored_types") or []))


def no_mesh_cause(record: dict) -> str:
    for pattern, template in NO_MESH_CAUSES:
        for diagnostic in record["diagnostics"]:
            match = pattern.search(diagnostic["message"]) or pattern.search(diagnostic.get("example", ""))
            if match is not None:
                return template.format(*match.groups()) if match.groups() else template
    return "cause not in the log"


def provisional_verdict(record: dict) -> str:
    if record["crash"]:
        return "crash"
    if not record["loaded"]:
        cause = record["load_error"] or record["describe_error"] or "load did not produce a scene"
        return f"fails: {gap_name(cause)}"
    errors = [d for d in record["diagnostics"] if d["level"] == "error"]
    warnings = [d for d in record["diagnostics"] if d["level"] == "warning"]
    stats = record.get("screenshot_stats") or {}
    authored = record.get("authored_prims") or 0
    if (record["prims"] == 0) and (authored > 0):
        return "fails: no prim loaded"
    # A file that authors renderable geometry but produced no mesh is named by
    # the cause that swallowed the geometry, not by what the frame shows.
    if (record["meshes"] == 0) and authors_geometry(record):
        return f"works, gap: no mesh loaded ({no_mesh_cause(record)})"
    # Only now can an empty frame mean the render path: the meshes are there.
    if stats.get("available") and stats.get("flat"):
        return "works, gap: renders nothing"
    if errors:
        return f"works, gap: {gap_name(errors[0]['message'])}"
    if warnings:
        return f"works, gap: {gap_name(warnings[0]['message'])}"
    return "works"


def verdict_class(verdict: str) -> str:
    for prefix in ("works, gap:", "fails:", "crash"):
        if verdict.startswith(prefix):
            return prefix.rstrip(":")
    return "works"


# A cause the recorded facts can refute. A stage that authors metersPerUnit 1
# is already in erhe's unit, so "the scale is not applied" cannot be why its
# content reads small - the framing is.
SCALE_CAUSE = re.compile(r"metersPerUnit|stage scale|scale (is )?not applied", re.IGNORECASE)


def eye_gap_holds(record: dict) -> bool:
    """False when the entry's own data refutes the cause read from the capture."""
    cause = record.get("eye_gap", "")
    if SCALE_CAUSE.search(cause):
        meters_per_unit = record.get("meters_per_unit")
        if (meters_per_unit is not None) and (abs(float(meters_per_unit) - 1.0) < 1.0e-6):
            return False
    return True


def gather_gaps(records: list) -> list:
    """Each distinct cause once, with the number of assets it affects."""
    causes = {}

    def add(key: str, level: str, asset: str, example: str) -> None:
        slot = causes.setdefault(key, {"cause": key, "level": level, "assets": set(), "example": example})
        slot["assets"].add(asset)
        if (level == "error") and (slot["level"] == "warning"):
            slot["level"] = "error"

    for record in records:
        asset = record["path"]
        if record["crash"]:
            add("editor crash while loading the file", "failure", asset, record.get("crash_detail", ""))
        if (not record["loaded"]) and (not record["crash"]):
            cause = record["load_error"] or record["describe_error"] or "load did not produce a scene"
            add("load failure: " + gap_name(cause), "failure", asset, cause)
        for diagnostic in record["diagnostics"]:
            add(diagnostic["message"], diagnostic["level"], asset, diagnostic.get("example", ""))
        stats = record.get("screenshot_stats") or {}
        # The two conditions the counts and the frame carry that no log line
        # states: geometry that produced no mesh, and meshes that reach the
        # frame but nothing lights them.
        if record["loaded"] and (record["meshes"] == 0) and authors_geometry(record):
            add("no mesh loaded: " + no_mesh_cause(record), "failure", asset, "")
        if record["loaded"] and (record["meshes"] > 0) and stats.get("available") and stats.get("flat"):
            add("renders nothing: the framed viewport is empty although meshes loaded", "failure", asset, "")
        # A file whose content lives in a subLayer loads as an empty stage and
        # says nothing about it, so the layer list is what names the cause.
        sublayered = any("sub" in str(layer.get("kind", "")).lower()
                         for layer in (record.get("layers") or []))
        if record["loaded"] and sublayered and (record["prims"] == 0):
            add(SUBLAYER_GAP, "failure", asset, "")
        # What the capture, compared against the repository's own reference
        # render, shows the editor getting wrong: a cause no log line states.
        if record.get("eye_gap") and eye_gap_holds(record):
            add(record["eye_gap"], "appearance", asset, record.get("eye_note", ""))

    gaps = []
    for slot in causes.values():
        gaps.append({
            "cause": slot["cause"],
            "level": slot["level"],
            "assets": sorted(slot["assets"]),
            "count": len(slot["assets"]),
            "example": slot["example"],
        })
    gaps.sort(key=lambda g: (-g["count"], g["cause"]))
    return gaps


# --------------------------------------------------------------------------
# Document
# --------------------------------------------------------------------------

def cell(text: str) -> str:
    text = str(text).replace("|", "\\|").replace("\n", " ")
    return re.sub(r"\s+", " ", text).strip()


def summarize_diagnostics(record: dict) -> str:
    if record["crash"]:
        return "editor crash"
    errors = sum(d["count"] for d in record["diagnostics"] if d["level"] == "error")
    warnings = sum(d["count"] for d in record["diagnostics"] if d["level"] == "warning")
    if (errors == 0) and (warnings == 0):
        return "none"
    named = [d["message"] for d in record["diagnostics"] if d["level"] != "note"]
    first = named[0] if named else ""
    parts = []
    if errors:
        parts.append(f"{errors} error")
    if warnings:
        parts.append(f"{warnings} warning")
    return ", ".join(parts) + (f"; {gap_name(first)[:60]}" if first else "")


# What the editor would have to support to clear each cause. Each line was
# written after reading the code that emits the message (the source is named),
# so it states the missing support, not a instruction to go and look.
REMEDY = [
    (re.compile(r"Attribute .* does not exist in Prim"),
     "nothing: LightUSD's Tydra probes every optional Gprim attribute (extent, doubleSided) "
     "for time samples and reports the ones a prim does not author; the schema fallback is "
     "used and the mesh loads (.cpm_cache lightusd src/tydra/render-data-anim.cc)"),
    (re.compile(r"TODO: Prim type|resolved to no RenderMesh"),
     "build geometry for the UsdGeom schemas Tydra does not convert (Cube, Sphere, Cone, "
     "Cylinder, Capsule, PointInstancer): the prim loads with no mesh "
     "(lightusd src/tydra/scene-access.cc)"),
    (re.compile(r"authors composition arcs but carries no transform"),
     "instantiate a reference or payload arc whose carrier is not an Xformable; "
     "resolve_usd_references drops the arc unless the prim is a Node "
     "(src/editor/parsers/usd.cpp)"),
    (re.compile(r"authors a transform, which a prim of this class does not carry"),
     "map the prim's schema onto an Xformable class, or carry a transform on a Typed prim of "
     "an unsupported schema (src/erhe/usd/erhe_usd/usd_import.cpp; plan section 5)"),
    (re.compile(r"unowned and unregistered"),
     "register a USD-imported material at its creation site so the scene owns it rather than "
     "listing it from the mesh binding (R5.2b, src/editor/scene/scene_root.cpp); the material "
     "still binds and renders"),
    (re.compile(r"sRGB texture is converted to fp"),
     "read 16-bit and 32-bit sRGB images directly instead of Tydra's un-linearized float "
     "conversion, which loses the transfer function"),
    (re.compile(r"Failed to load texture image|image .* not found|Failed to resolve asset path"),
     "resolve a texture asset path against the layer that authored it, including inside a "
     ".usdz package"),
    (re.compile(r"failed to load reference target"),
     "load a reference target that names a missing file, holds no prims, or closes a cycle "
     "(X1, src/editor/parsers/usd.cpp)"),
    (re.compile(r"Prefab '"),
     "instantiate a reference whose target prim path is absent from the target layer "
     "(Prefab_library::get_or_load, X1)"),
    (re.compile(r"variant set .* opinion"),
     "carry variant opinions beyond material bindings; X4 reads bindings only "
     "(src/erhe/usd/notes.md, Variant sets)"),
    (re.compile(r"no UsdPreviewSurface shader|MaterialX|Not a NodeGraph|unshaded material"),
     "convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2)"),
    (re.compile(r"primvar as un-indexed|no authored value"),
     "read an indexed primvar whose indices attribute is declared but carries no value"),
    (re.compile(r"light type has no erhe counterpart"),
     "add the USD light schemas erhe has no counterpart for (the area lights import as point lights)"),
    (re.compile(r"^no mesh loaded"),
     "see the cause named in the parentheses; the file authors geometry that produced no mesh"),
    (re.compile(r"renders nothing"),
     "make framed, loaded meshes reach the frame: the geometry, the camera or the material"),
    (re.compile(r"BVH save failed|dropped expired|Main loop STALLED|breadcrumb|scene-close"),
     "not a USD gap: editor-internal noise this survey happens to capture"),
    (re.compile(r"^Item <item> child <child> parent =="),
     "keep a prim's parent pointer when an instantiated reference re-parents its clones: the hierarchy "
     "sanity check reports a child whose parent is none right after a prefab instantiation"),
    (re.compile(r"has no converted material - it is not placed in the tree"),
     "convert a Material prim Tydra hands over without a converted network (a MaterialX or non-preview "
     "surface), so it can be placed where the file puts it"),
    (re.compile(r"DomeLight texture .* is not sampled"),
     "sample a DomeLight's texture as an environment map; erhe takes only the light's intensity and "
     "colour, so an HDRI-lit stage loses the image"),
    (re.compile(r"could not be decoded"),
     "decode the image formats the assets use that the loader rejects (Radiance .hdr above all)"),
    (re.compile(r"Prefab source file not found"),
     "resolve a reference asset path relative to the layer that authored it before opening it as a "
     "prefab template"),
    (re.compile(r"glTF parse error"),
     "nothing in USD: the prefab library parses a reference target as glTF when it is a USD layer "
     "(Prefab_library::get_or_load, X1)"),
    (re.compile(r"Failed to parse USDA|Failed to parse (Attribute|Prim|`)"),
     "read the USDA constructs LightUSD's parser rejects; the file then loads as an empty stage"),
    (re.compile(r"up axis .* has no erhe counterpart"),
     "carry a Z-up stage's up axis into the scene instead of importing it as Y-up"),
    (re.compile(r"renders flat white when its bound material comes from another layer"),
     "bind a material the file authors in a layer other than the mesh's own; the mesh loads and shades "
     "with the default white material instead of the one the file binds"),
    (re.compile(r"bit, .*bit and CMYK images do not load|images do not load"),
     "decode the image depths and colour models the assets use beyond 8-bit RGB: 16-bit and 32-bit PNG and "
     "CMYK JPEG produce no texture, so their tiles render blank"),
    (re.compile(r"no scene appeared within"),
     "open a stage whose root layer is a MaterialX document reference; the load never produces a scene "
     "and the open never answers"),
    (re.compile(r"no mesh loaded: cause not in the log"),
     "nothing: empty.usda authors a Mesh with no points, so producing no mesh is what the file asks for"),
    (re.compile(r"renders black when its bound material comes from another layer"),
     "bind a material the file authors in a layer other than the mesh's own; the binding survives the "
     "import (the log's 'has no converted material' line names the same prims) but the mesh shades black"),
    (re.compile(r"colour reaches only one row"),
     "sample a UsdUVTexture through every colour-space path the file exercises (raw / sRGB / auto / omit / "
     "lin_ap1_scene); only one row reaches the surface, the rest render white"),
    (re.compile(r"time-sampled translation is not evaluated"),
     "evaluate a time-sampled xformOp at the sample the file's own render uses; the import takes the "
     "default or first sample, so the prim sits elsewhere (plan section 6, animation)"),
    (re.compile(r"translucent material renders opaque"),
     "render a material whose opacity is below one as translucent; the stained-glass cube hides what is "
     "behind it where the reference shows through"),
    (re.compile(r"per-channel output selection is ignored"),
     "honour a UsdUVTexture's outputs:r / :g / :b / :rgb connection: only the row whose swatch is wired "
     "to the output erhe reads samples, and the others render white"),
    (re.compile(r"no material binding renders black"),
     "give a prim with no material binding USD's unauthored UsdPreviewSurface fallback (diffuseColor "
     "0.18 grey) instead of the black erhe shades it with"),
    (re.compile(r"PointInstancer prims are not instanced"),
     "instance a PointInstancer's prototypes at its positions / orientations / scales; Tydra converts "
     "neither the instancer nor its prototypes, so the whole scene loads empty"),
    (re.compile(r"subLayers are not composed"),
     "compose the root layer's subLayers before the prim walk: the stage loads the root layer alone, "
     "so a file that only sublayers its content opens empty and says nothing about it"),
    (re.compile(r"crash", re.I),
     "find and fix the crash before anything else in this list"),
    # The causes the side-by-side comparison against the repository's own
    # reference renders names, each read back to the code that owns it.
    (re.compile(r"DomeLight is not imported"),
     "sample a DomeLight's inputs:texture:file; the dome's constant color reaches the scene as ambient "
     "light, but erhe has no environment map, so a textured sky contributes one flat color"),
    (re.compile(r"normal map's bias and scale are (ignored|still not applied)"),
     "apply a UsdUVTexture's inputs:bias and inputs:scale to the sampled normal (src/erhe/usd/notes.md, "
     "\"Not yet imported\"); without them a 0..1 normal map is never mapped back to -1..1"),
    (re.compile(r"UsdTransform2d is (not applied|applied wrongly)"),
     "read the UsdTransform2d node between a primvar reader and a texture and fold its translate, rotate "
     "and scale into the sampled coordinates (src/erhe/usd/notes.md names it as not imported)"),
    (re.compile(r"texture coordinates are mirrored|st primvar is sampled mirrored"),
     "carry the st primvar's orientation through the import: USD's texture origin is bottom-left, and the "
     "mesh conversion must not mirror it (src/erhe/usd/erhe_usd/usd_import.cpp, the texcoord read)"),
    (re.compile(r"opacity is not blended"),
     "render the material's blended alpha: the importer sets opacity and the blend mode "
     "(usd_import.cpp inputs:opacity / inputs:opacityThreshold) but the surface still draws opaque"),
    (re.compile(r"UsdUVTexture's color does not reach the surface"),
     "sample a UsdUVTexture whose color-space handling the file exercises (sourceColorSpace raw / sRGB / "
     "auto); every swatch renders white, so no texture value reaches the shader"),
    (re.compile(r"texture packed inside a .usdz"),
     "resolve a texture asset path that names a file inside the .usdz package it was authored in"),
    (re.compile(r"roughness (and its texture do not reach|does not reach)"),
     "apply inputs:roughness and its texture to the shaded surface; the bands render with one matte "
     "response, so neither the constant nor the textured roughness reaches the shader"),
]


def remedy(cause: str, level: str = "") -> str:
    if level == "note":
        return "nothing: the line reports how this build is configured"
    for pattern, text in REMEDY:
        if pattern.search(cause):
            return text
    return "diagnose the message and add the support it asks for"


def write_document(path: pathlib.Path, summary: dict) -> None:
    records = summary["entries"]
    gaps = gather_gaps(records)
    counts = collections.Counter(verdict_class(r["verdict"]) for r in records)
    checked = sorted(r["path"] for r in records if r.get("eye_checked"))

    out = []
    out.append("# USD Assets Working Group survey")
    out.append("")
    out.append("Every entry asset of the ASWF USD Assets Working Group repository")
    out.append("(github.com/usd-wg/assets) opened in a headless erhe editor, with what")
    out.append("the editor made of it. This is the checklist of USD support the editor")
    out.append("still lacks, ordered in the Gaps section by how many assets each gap")
    out.append("affects; `doc/usd-compatibility-plan.md` step S1 owns the survey and its")
    out.append("later steps take their next fix from that list.")
    out.append("")
    out.append("Re-run it from the repo root against a local clone of the repository")
    out.append("(`<usd-wg-assets>`), with the headless Vulkan build current:")
    out.append("")
    out.append("```")
    out.append("py -3 scripts/usd_wg_asset_survey.py --root <usd-wg-assets>")
    out.append("```")
    out.append("")
    out.append("The script launches and relaunches the editor itself, writes the raw")
    out.append("per-entry data to `logs/usd_wg_survey/summary.json` and regenerates this")
    out.append("document from it (`--from-summary` regenerates without a run). Its")
    out.append("docstring states which files of a folder count as entry assets.")
    out.append("Screenshot paths are under `logs/`, which is gitignored: the column is a")
    out.append("pointer into the last run's output, not a committed file.")
    out.append("")
    out.append("Each capture is taken through `frame_scene`, which binds the opened scene")
    out.append("into a viewport, gives it a camera when the file authors none and places")
    out.append("that camera on the union world AABB of the scene's meshes: three quarters")
    out.append("round, or straight down the shared axis when every mesh is coplanar, so a")
    out.append("test card is seen the way its own reference image shows it. A scene whose")
    out.append("file authors no light is lit by the editor's own headlight - one white")
    out.append("directional light along the viewport camera's axis, the way usdview lights")
    out.append("a stage that authors none - so the capture shows the geometry. That light")
    out.append("is no scene item, and the `Lights` column is what the file itself authored.")
    out.append("")
    out.append("The `Reference` column names the renders the repository ships beside each")
    out.append("asset (`screenshots/` first, then `thumbnails/`), repo-relative to")
    out.append("`<usd-wg-assets>`. `--compose-comparisons` puts the capture and the first")
    out.append("of those side by side under `logs/usd_wg_survey/compare/`, which is how the")
    out.append("appearance verdicts below were reached.")
    out.append("")
    # What the importer makes of an authored camera, recorded because the
    # survey's own capture camera hides it: --refresh-cameras reopens these
    # entries and reads get_scene_cameras.
    with_camera = [r for r in records if r.get("authored_cameras")]
    if with_camera:
        fovs = [c.get("fov_y") for r in with_camera for c in (r.get("imported_cameras") or [])]
        usable = [f for f in fovs if isinstance(f, (int, float)) and (0.01 < f < 3.13)]
        out.append(f"Authored cameras: {len(with_camera)} entries author a `UsdGeomCamera`; "
                   f"{len(usable)} of {len(fovs)} imported cameras carry a field of view in "
                   "(0.6, 179) degrees, so `convert_cameras` maps `focalLength`, "
                   "`horizontalAperture` and `verticalAperture` onto `fov_y` / `fov_x` as the "
                   "files author them. No gap row: the survey's capture uses its own camera, "
                   "not the authored one.")
        out.append("")
    out.append(f"Run: {summary['run_date']}, {summary['entry_count']} entries, "
               f"{summary['wall_time_s']:.0f} s wall time, {summary['editor_launches']} editor launch(es).")
    out.append(f"Verdicts: {counts['works']} works, {counts['works, gap']} works with a gap, "
               f"{counts['fails']} fails, {counts['crash']} crash.")
    out.append("")
    if checked:
        out.append("## Verdicts checked by eye")
        out.append("")
        out.append("The capture was read for these entries, and it - not the counts and not")
        out.append("the log - settled the verdict. Every other row is what the counts, the")
        out.append("log and the empty-viewport test decided.")
        out.append("")
        out.append("| Entry file | What the capture shows |")
        out.append("| --- | --- |")
        for record in sorted(records, key=lambda r: r["path"]):
            if record.get("eye_checked"):
                out.append("| {} | {} |".format(cell(record["path"]), cell(record.get("eye_note", ""))))
    else:
        out.append("No verdict has been checked by eye yet; every row is what the counts,")
        out.append("the log and the flat-screenshot test decided.")
    out.append("")
    out.append("## Entries")
    out.append("")
    out.append("`authored` is the prim count `describe_usd_file` reports for the file;")
    out.append("`prims` / `meshes` / `materials` / `lights` are what the loaded scene holds.")
    out.append("")
    out.append("| Folder | Entry file | Load | Authored | Prims | Meshes | Mats | Lights | Warnings and errors | Screenshot | Verdict |")
    out.append("| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |")
    for record in sorted(records, key=lambda r: r["path"]):
        load = "crash" if record["crash"] else ("ok" if record["loaded"] else "failed")
        authored = record["authored_prims"]
        out.append("| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |".format(
            cell(record["folder"]),
            cell(pathlib.PurePosixPath(record["path"]).name),
            load,
            "-" if authored is None else authored,
            record["prims"], record["meshes"], record["materials"], record["lights"],
            cell(summarize_diagnostics(record)),
            cell(record["screenshot"]),
            cell(record["verdict"]),
        ))
    out.append("")
    out.append("## Gaps")
    out.append("")
    out.append("Each distinct failure, error or warning once, with the number of entry")
    out.append("assets it affects and what the editor would have to support to clear it.")
    out.append("")
    out.append("| Assets | Kind | Cause | What the editor would have to support |")
    out.append("| ---: | --- | --- | --- |")
    for gap in gaps:
        out.append("| {} | {} | {} | {} |".format(
            gap["count"], gap["level"], cell(gap["cause"]), cell(remedy(gap["cause"], gap["level"]))))
    out.append("")
    path.write_text("\n".join(out) + "\n", encoding="utf-8")


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

DEFAULT_EDITOR = pathlib.Path("build_vs2026_vulkan_headless/src/editor/Debug/editor.exe")
DEFAULT_SHOTS = pathlib.Path("logs/usd_wg_survey")
DEFAULT_DOC = pathlib.Path("doc/usd-wg-assets.md")


def refresh_camera_fields(args, summary: dict) -> int:
    """Fill in, for each entry whose file authors a UsdGeomCamera, what the
    imported Camera reads: its projection type and field of view.

    The survey's own capture camera hides this - `frame_scene` never reads an
    authored camera - so the two facts are gathered on their own: the authored
    count from `describe_usd_file`'s prim types (already recorded), and the
    imported values from `get_scene_cameras` on a reopened scene.
    """
    root = pathlib.Path(args.root).resolve()
    todo = [record for record in summary["entries"]
            if any(t.get("type_name") == "Camera" for t in (record.get("authored_types") or []))]
    if not todo:
        return 0
    editor = Editor(args.editor, args.port, pathlib.Path("logs/log.txt"))
    editor.start(args.ready_timeout)
    refreshed = 0
    try:
        for record in todo:
            record["authored_cameras"] = sum(t.get("count", 0) for t in record["authored_types"]
                                             if t.get("type_name") == "Camera")
            before = scene_names(editor)
            editor.mcp.call("open_scene", {"path": str((root / record["path"]).resolve())}, timeout=args.load_timeout)
            scene = wait_for_new_scene(editor, before, args.load_timeout)
            if not scene:
                continue
            cameras = editor.mcp.call("get_scene_cameras", {"scene_name": scene}, timeout=args.load_timeout).get("cameras", [])
            record["imported_cameras"] = [
                {"name": camera.get("name", ""), "fov_y": camera.get("fov_y")}
                for camera in cameras
            ]
            refreshed += 1
            editor.mcp.call("close_scene", {"scene_name": scene}, timeout=args.load_timeout)
            wait_for_scene_gone(editor, scene, 30.0)
            close_extra_scenes(editor, args.load_timeout)
    finally:
        editor.stop()
    return refreshed


def run_self_test(args) -> int:
    """Prove the capture path before trusting a survey run.

    Opens a scene known to hold one lit, materialled mesh, frames it and
    captures, then checks the viewport region of the PNG: an unbound or
    unframed viewport is one flat color, so a varied crop means the pipeline
    from open_scene through frame_scene to capture_screenshot works.
    """
    source = pathlib.Path(args.self_test_file)
    if not source.is_file():
        print(f"self-test file not found: {source}", file=sys.stderr)
        return 2
    if not args.editor.is_file():
        print(f"editor not found: {args.editor}", file=sys.stderr)
        return 2
    shot = args.shots / "selftest.png"
    editor = Editor(args.editor, args.port, pathlib.Path("logs/log.txt"))
    editor.start(args.ready_timeout)
    try:
        before = scene_names(editor)
        editor.mcp.call("open_scene", {"path": str(source.resolve())}, timeout=args.load_timeout)
        scene = wait_for_new_scene(editor, before, args.load_timeout)
        if not scene:
            print(f"FAIL: {source.name} produced no scene")
            return 1
        framing = editor.mcp.call("frame_scene", {"scene_name": scene}, timeout=args.load_timeout)
        wait_frames(editor, args.settle_frames, args.load_timeout)
        editor.mcp.call("capture_screenshot", {"path": shot.as_posix()}, timeout=args.load_timeout)
        stats = screenshot_stats(shot)
    finally:
        editor.stop()

    print(f"self-test: {source.name} -> scene '{scene}', "
          f"camera '{framing.get('camera')}' (created={framing.get('camera_created')}), "
          f"{framing.get('meshes')} mesh(es), camera_source={framing.get('camera_source')}, "
          f"viewport '{framing.get('viewport')}'")
    print(f"self-test: {shot} viewport stddev={stats.get('viewport_stddev')} "
          f"min={stats.get('viewport_min')} max={stats.get('viewport_max')}")
    # A scene whose file authors a camera is looked through, not framed, so
    # `framed` is false there by design.
    ok = (
        (bool(framing.get("framed")) or (framing.get("camera_source") == "authored")) and
        (framing.get("meshes", 0) > 0) and
        stats.get("available", False) and
        (not stats.get("flat", True)) and
        (float(stats.get("viewport_stddev", 0.0)) >= 5.0)
    )
    print("self-test: PASS - geometry is visible in the viewport" if ok
          else "self-test: FAIL - the viewport shows no framed geometry")
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Survey the USD Assets Working Group repository in a headless erhe editor")
    parser.add_argument("--root", default=os.environ.get("ERHE_USD_WG_ASSETS", ""),
                        help="root of a clone of github.com/usd-wg/assets (default: ERHE_USD_WG_ASSETS)")
    parser.add_argument("--editor", type=pathlib.Path, default=DEFAULT_EDITOR, help="headless editor executable")
    parser.add_argument("--port", type=int, default=int(os.environ.get("ERHE_MCP_PORT", "3743")), help="MCP port")
    parser.add_argument("--doc", type=pathlib.Path, default=DEFAULT_DOC, help="document to write")
    parser.add_argument("--shots", type=pathlib.Path, default=DEFAULT_SHOTS, help="screenshot and summary directory")
    parser.add_argument("--load-timeout", type=float, default=120.0, help="seconds a single load may take")
    parser.add_argument("--ready-timeout", type=float, default=300.0, help="seconds to wait for a launched editor")
    parser.add_argument("--close-wait", type=float, default=7.0, help="seconds to wait for the scene-close leak watchdog")
    parser.add_argument("--settle-frames", type=int, default=6, help="frames to render after framing, before the capture")
    parser.add_argument("--self-test", action="store_true", help="prove the capture on a known-good scene and exit")
    parser.add_argument("--self-test-file", default="src/erhe/usd/test/data/cube.usda", help="scene the self-test opens")
    parser.add_argument("--max-per-folder", type=int, default=4, help="cap on entries taken from one folder (0 = no cap)")
    parser.add_argument("--limit", type=int, default=0, help="survey only the first N entries")
    parser.add_argument("--only", action="append", default=[],
                        help="survey only entries whose repo-relative path contains this substring (repeatable)")
    parser.add_argument("--list-entries", action="store_true", help="print the entry list and exit")
    parser.add_argument("--from-summary", action="store_true", help="regenerate the document from summary.json, no editor")
    parser.add_argument("--refresh-cameras", action="store_true",
                        help="reopen the entries whose file authors a UsdGeomCamera and record what the imported Camera reads; needs --root")
    parser.add_argument("--compose-comparisons", action="store_true",
                        help="write logs/usd_wg_survey/compare/<entry>.png (capture beside the first reference render) and exit; needs --root")
    args = parser.parse_args()

    args.shots.mkdir(parents=True, exist_ok=True)
    summary_path = args.shots / "summary.json"

    if args.refresh_cameras:
        if not args.root:
            print("--refresh-cameras needs --root", file=sys.stderr)
            return 2
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        refreshed = refresh_camera_fields(args, summary)
        summary_path.write_text(json.dumps(summary, indent=1), encoding="utf-8")
        print(f"refreshed the camera fields of {refreshed} entry/entries")
        return 0

    if args.compose_comparisons:
        if not args.root:
            print("--compose-comparisons needs --root", file=sys.stderr)
            return 2
        root = pathlib.Path(args.root).resolve()
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        written = 0
        for record in summary["entries"]:
            if not record.get("references"):
                record["references"] = reference_images(root / record["path"], root)
            path = compose_comparison(record, root, args.shots / "compare")
            if path:
                record["comparison"] = path
                written += 1
        summary_path.write_text(json.dumps(summary, indent=1), encoding="utf-8")
        print(f"wrote {written} comparison image(s) under {args.shots / 'compare'}")
        return 0

    if args.self_test:
        return run_self_test(args)

    if args.from_summary:
        if not summary_path.is_file():
            print(f"no summary at {summary_path}; run the survey first", file=sys.stderr)
            return 2
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        write_document(args.doc, summary)
        print(f"wrote {args.doc} from {summary_path} ({summary['entry_count']} entries)")
        return 0

    if not args.root:
        print("--root (or ERHE_USD_WG_ASSETS) must name a clone of github.com/usd-wg/assets", file=sys.stderr)
        return 2
    root = pathlib.Path(args.root).resolve()
    if not root.is_dir():
        print(f"not a directory: {root}", file=sys.stderr)
        return 2

    entries = collect_entries(root, args.max_per_folder)
    if args.only:
        entries = [e for e in entries if any(fragment in e["path"] for fragment in args.only)]
    if args.limit > 0:
        entries = entries[:args.limit]
    if args.list_entries:
        for entry in entries:
            print(entry["path"])
        print(f"{len(entries)} entries", file=sys.stderr)
        return 0
    if not entries:
        print(f"no entry assets found under {root}", file=sys.stderr)
        return 2

    if not args.editor.is_file():
        print(f"editor not found: {args.editor}", file=sys.stderr)
        return 2

    editor = Editor(args.editor, args.port, pathlib.Path("logs/log.txt"))
    started = time.monotonic()
    records = []
    print(f"{len(entries)} entries; launching {args.editor}")
    editor.start(args.ready_timeout)
    try:
        for index, entry in enumerate(entries, start=1):
            if not editor.alive():
                editor.start(args.ready_timeout)
            print(f"[{index}/{len(entries)}] {entry['path']}", flush=True)
            try:
                record = survey_entry(editor, root, entry, args.shots, args.load_timeout,
                                      args.close_wait, args.settle_frames)
            except (RuntimeError, EditorDown) as error:
                record = dict(entry)
                record.update({
                    "slug": entry_slug(entry["path"]), "screenshot": "", "crash": True,
                    "crash_detail": str(error)[:300], "loaded": False, "prims": 0, "meshes": 0,
                    "materials": 0, "lights": 0, "cameras": 0, "diagnostics": [],
                    "authored_prims": None, "authored_types": [], "verdict": "crash",
                    "log_tail": [], "scene_close": "", "screenshot_stats": {},
                    "load_error": "", "describe_error": "", "scene_name": "",
                })
            records.append(record)
            print(f"      {record['verdict']}", flush=True)
            if (not record["crash"]) and editor.alive():
                try:
                    close_extra_scenes(editor, args.load_timeout)
                except EditorDown:
                    record["crash"] = True
                    record["crash_detail"] = "editor died while closing leftover scenes"
                    record["verdict"] = "crash"
            if record["crash"] or (not editor.alive()):
                print("      editor down; restarting", flush=True)
                editor.stop()
                editor.start(args.ready_timeout)
    finally:
        editor.stop()

    summary = {
        "run_date": datetime.datetime.now().strftime("%Y-%m-%d"),
        "entry_count": len(records),
        "wall_time_s": time.monotonic() - started,
        "editor_launches": editor.launches,
        "max_per_folder": args.max_per_folder,
        "entries": records,
    }
    summary_path.write_text(json.dumps(summary, indent=1), encoding="utf-8")
    write_document(args.doc, summary)

    counts = collections.Counter(verdict_class(r["verdict"]) for r in records)
    print(f"\n{len(records)} entries in {summary['wall_time_s']:.0f} s, {editor.launches} launch(es)")
    print(f"works {counts['works']}, works-gap {counts['works, gap']}, fails {counts['fails']}, crash {counts['crash']}")
    for gap in gather_gaps(records)[:5]:
        print(f"  {gap['count']:3d}  {gap['cause'][:90]}")
    print(f"wrote {args.doc} and {summary_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
