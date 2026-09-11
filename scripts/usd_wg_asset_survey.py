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
says which rows a human then corrected by eye. Those by-eye verdicts live in
doc/usd-wg-assets-eye.json, which every run and every --from-summary reads and
only --eye-note writes:

    py -3 scripts/usd_wg_asset_survey.py --eye-note <entry path> "<what the
        capture shows>" [--eye-gap "<the cause it names>"]

A run restricted with --only, --exclude or --limit surveys those entries and
keeps every other entry's record, so summary.json and the document always
state the whole survey; each record carries the date it was surveyed on.
--only keeps entries whose repo-relative path contains any given substring,
--exclude then drops entries whose path contains any given substring; both
are repeatable, so `--only test_assets --exclude full_assets` or
`--exclude intent-vfx` select by group folder:

    py -3 scripts/usd_wg_asset_survey.py --only test_assets --exclude McUsd

Expected results (--expected)
-----------------------------
doc/usd-wg-assets-expected.json is hand-edited and committed. Each item
names an entry path, the `diagnostics` it reports by design (regular
expressions matched against the normalized message and the example line),
the `gaps` the counts and the frame raise by design (regular expressions
matched against the verdict's gap text, e.g. an empty mesh's "no mesh
loaded"), optionally `appearance` (a reason: the entry's by-eye gap is
expected too) and a `reason`. A matched diagnostic keeps its observed level in
`level_observed` and no longer counts against the verdict or the Gaps
section, so an entry whose only issues are expected is `works`. Every run,
--from-summary and --eye-note re-apply the file to the whole summary, so an
added or removed expectation takes effect without a re-run:

    [
     {
      "path": "test_assets/.../usduvtexture_color_test.usda",
      "diagnostics": ["`inputs:sourceColorSpace`: `lin_ap1_scene` is not an allowed token"],
      "reason": "column 5 authors that illegal token on purpose (asset README)"
     }
    ]

Test database (--test-db, --record-test-db, --failing-first, --clear-test-db)
-----------------------------------------------------------------------------
logs/usd_wg_survey/test_database.json (or --test-db) records, per entry
path, the status of its last recorded run - `pass` (works, no gap), `gap`
(works with a gap) or `fail` (fails or crash) - together with the verdict,
the run time in seconds and the date. --record-test-db updates it after
every entry (so an interrupted run keeps what it did), --clear-test-db
empties it (and exits when no --root is given), and --failing-first orders
the run by it: `fail` entries first, shortest run first, then `gap` entries
the same way, then entries the database has not seen in survey order, and
`pass` entries last. --stop-on-gap ends the run after the first entry
whose verdict is not `works`, after recording it, so one gap at a time can
be fixed and the run resumed. --unrecorded-first then moves the entries the database
has no result for to the front, keeping their order (with --failing-first
that puts them ahead of the recorded failures), so a new or cleared
database fills in before known results are re-run. --last <substring>
(repeatable) then moves the entries whose path contains it to the very
end, keeping their order, so a slow or stuck entry stops holding up the
coverage the rest of the run gives. --limit applies after all of these, so
this runs the five quickest known failures with the MaterialX color-space
entries deferred:

    py -3 scripts/usd_wg_asset_survey.py --root <usd-wg-assets>         --record-test-db --failing-first --last ColorSpaceTests/MaterialX --limit 5

OpenUSD reference (--usd-root / ERHE_USD_ROOT)
----------------------------------------------
With a prebuilt OpenUSD at `<usd_root>` (its `scripts/set_usd_env.bat` and
`scripts/usdrecord.bat`), every entry also gets:

* the composed stage's own counts (scripts/usd_wg_pxr_stage.py under the
  bundled Python: prims, UsdGeomMesh, materials, lights, cameras) - what the
  editor should have reached, read by OpenUSD itself, in place of the
  authored-prim-type heuristic;
* a Storm render through the very camera the editor's capture looks through
  (`usdrecord`; the editor's computed camera is authored into a session layer
  in stage space, an authored camera is named by path), at the capture's
  aspect - a reference that exists for every entry and shows the current
  revision of the asset, where the repository's own renders are missing for
  most entries and predate some;
* the normalized cross-correlation of the two grey images (`storm_match`,
  1.0 = identical), which ranks the entries for a by-eye read and, under
  --storm-threshold, names a gap the log does not.
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
# OpenUSD tools (optional): composed-stage facts and Storm reference renders
# --------------------------------------------------------------------------

DEFAULT_USD_ROOT = os.environ.get("ERHE_USD_ROOT", "")
PXR_STAGE_SCRIPT = pathlib.Path(__file__).resolve().parent / "usd_wg_pxr_stage.py"


class Usd_tools:
    """The prebuilt OpenUSD at `<usd_root>`: its Python (`pxr`) for stage
    facts and its `usdrecord` for Storm renders. Both run through the
    wrapper scripts the distribution ships, which set up its environment."""

    def __init__(self, usd_root: pathlib.Path) -> None:
        self.root = usd_root
        self.env_script = usd_root / "scripts" / "set_usd_env.bat"
        self.usdrecord = usd_root / "scripts" / "usdrecord.bat"
        if not self.env_script.is_file():
            raise FileNotFoundError(f"{self.env_script} not found: --usd-root must name a prebuilt OpenUSD")
        if not self.usdrecord.is_file():
            raise FileNotFoundError(f"{self.usdrecord} not found: --usd-root must name a prebuilt OpenUSD")

    def _run(self, command: str, timeout: float) -> subprocess.CompletedProcess:
        # cmd.exe runs the env batch and the tool in one shell; the batch
        # echoes its own comment lines, which the callers skip. The command
        # line is passed as one string: cmd /c strips the outermost pair of
        # quotes and keeps every quoted path inside intact.
        return subprocess.run(f'cmd /c ""{self.env_script}" && {command}"',
                              capture_output=True, text=True, timeout=timeout, errors="replace")

    def stage_stats(self, path: pathlib.Path, timeout: float = 300.0) -> dict:
        try:
            done = self._run(f'python "{PXR_STAGE_SCRIPT}" "{path}"', timeout)
        except subprocess.TimeoutExpired:
            return {"error": f"pxr stage read timed out after {timeout:.0f} s"}
        for line in done.stdout.splitlines():
            if line.startswith("{"):
                try:
                    return json.loads(line)
                except json.JSONDecodeError as error:
                    return {"error": f"pxr stage read: unreadable output ({error})"}
        tail = (done.stderr or done.stdout).strip().splitlines()[-3:]
        return {"error": "pxr stage read produced no result: " + " | ".join(tail)[:300]}

    def record(self, path: pathlib.Path, out_png: pathlib.Path, width: int,
               camera: str = "", session_layer: pathlib.Path = None, timeout: float = 600.0) -> str:
        """Render `path` with Storm into `out_png`; "" on success, else the reason."""
        parts = [f'"{self.usdrecord}"', "--imageWidth", str(max(int(width), 16)), "--complexity", "medium"]
        if session_layer is not None:
            parts += ["--sessionLayer", f'"{session_layer}"']
        if camera:
            parts += ["--camera", f'"{camera}"']
        parts += [f'"{path}"', f'"{out_png}"']
        try:
            done = self._run(" ".join(parts), timeout)
        except subprocess.TimeoutExpired:
            return f"usdrecord timed out after {timeout:.0f} s"
        if out_png.is_file() and (out_png.stat().st_size > 0):
            return ""
        # The env batch echoes its own comment lines (`<cwd>>REM ...`) and
        # blank prompts; the tool's own error lines are what is kept.
        lines = [l.strip() for l in (done.stderr + done.stdout).splitlines()
                 if l.strip() and (">REM" not in l) and (not l.strip().endswith(">")) and ("Warning" not in l)]
        # pxr's own error lines say what failed; the exception trailer does not.
        errors = [l for l in lines if ("Error" in l) or ("error" in l) or ("invalid" in l)]
        return "usdrecord produced no image: " + " | ".join((errors or lines)[-2:])[:300]


def composed_facts(stats: dict) -> dict:
    """The per-entry fields kept from a pxr stage read."""
    if not stats or stats.get("error"):
        return {"composed_error": (stats or {}).get("error", "no pxr read")}
    return {
        "composed_prims": stats.get("prims"),
        "composed_meshes": stats.get("meshes"),
        "composed_gprims": stats.get("gprims"),
        "composed_point_instancers": stats.get("point_instancers"),
        "composed_materials": stats.get("materials"),
        "composed_lights": stats.get("lights"),
        "composed_cameras": stats.get("cameras", []),
        "composed_up_axis": stats.get("up_axis", ""),
        "composed_meters_per_unit": stats.get("meters_per_unit"),
        "composed_bounds_min": stats.get("bounds_min"),
        "composed_bounds_max": stats.get("bounds_max"),
        "composed_skinned": bool(stats.get("skinned")),
        "composed_errors": stats.get("errors", [])[:5],
    }


def bounds_deviation(record: dict):
    """How far the editor's world AABB of the scene's meshes is from the
    composed stage's, as a fraction of the composed diagonal: the editor's
    bounds (Y-up metres) go back to stage space through the inverse of the
    importer's stage transform first. None when either side is missing."""
    framing = record.get("framing") or {}
    erhe_min = framing.get("bounds_min")
    erhe_max = framing.get("bounds_max")
    stage_min = record.get("composed_bounds_min")
    stage_max = record.get("composed_bounds_max")
    if not (erhe_min and erhe_max and stage_min and stage_max) or (framing.get("meshes", 0) == 0):
        return None
    if record.get("composed_skinned"):
        # pxr's bounds cache reports the rest pose of a skinned mesh, which
        # the editor's skinned bounds are not: no comparison to make.
        return None
    scale = float(record.get("composed_meters_per_unit") or 1.0) or 1.0
    z_up = (record.get("composed_up_axis") or "Y") == "Z"
    corners = []
    for x in (erhe_min[0], erhe_max[0]):
        for y in (erhe_min[1], erhe_max[1]):
            for z in (erhe_min[2], erhe_max[2]):
                # Inverse of R_x(-90 deg): erhe (x, y, z) -> stage (x, -z, y).
                # New names: rebinding the loop variables here would corrupt
                # the outer loops' values for the corners that follow.
                stage_x, stage_y, stage_z = (x, -z, y) if z_up else (x, y, z)
                corners.append((stage_x / scale, stage_y / scale, stage_z / scale))
    back_min = [min(c[i] for c in corners) for i in range(3)]
    back_max = [max(c[i] for c in corners) for i in range(3)]
    diagonal = sum((stage_max[i] - stage_min[i]) ** 2 for i in range(3)) ** 0.5
    if diagonal <= 1.0e-9:
        return None
    deviation = max(
        sum((back_min[i] - stage_min[i]) ** 2 for i in range(3)) ** 0.5,
        sum((back_max[i] - stage_max[i]) ** 2 for i in range(3)) ** 0.5,
    )
    return round(deviation / diagonal, 4)


def stage_camera_session_layer(view: dict, up_axis: str, meters_per_unit: float,
                               out_path: pathlib.Path) -> None:
    """Author the editor's viewport camera as a Camera prim in a session layer,
    in the stage's own space: the importer applies R_x(-90 deg) for a Z-up
    stage and scales by metersPerUnit (usd_import.cpp make_stage_transform),
    so the camera goes back through the inverse before it is written."""
    import numpy

    world_from_camera = numpy.array(view["camera_world_from_camera"], dtype=float).reshape(4, 4).T  # column-major in
    stage_from_world = numpy.identity(4)
    if up_axis == "Z":
        # inverse of glm::rotate(-pi/2, X): rotate +pi/2 about X
        stage_from_world[1, 1] = 0.0
        stage_from_world[1, 2] = -1.0
        stage_from_world[2, 1] = 1.0
        stage_from_world[2, 2] = 0.0
    scale = float(meters_per_unit or 1.0)
    if scale <= 0.0:
        scale = 1.0
    camera = stage_from_world @ world_from_camera
    camera[:3, 3] /= scale
    for column in range(3):   # the erhe camera carries no scale; keep the basis unit
        length = numpy.linalg.norm(camera[:3, column])
        if length > 0.0:
            camera[:3, column] /= length
    fov_y = float(view.get("camera_fov_y") or 0.7853981634)
    aspect = float(view["width"]) / float(max(view["height"], 1))
    vertical_aperture = 24.0
    horizontal_aperture = vertical_aperture * aspect
    focal_length = (0.5 * vertical_aperture) / max(numpy.tan(0.5 * fov_y), 1.0e-6)
    z_near = float(view.get("camera_z_near") or 0.01) / scale
    z_far = float(view.get("camera_z_far") or 1000.0) / scale
    rows = []
    for row in range(4):   # usda writes the matrix as rows of the row-vector convention: our columns
        rows.append("(" + ", ".join(f"{camera[component, row]:.9g}" for component in range(4)) + ")")
    out_path.write_text(
        "#usda 1.0\n"
        "def Camera \"ErheSurveyCamera\" {\n"
        f"    float focalLength = {focal_length:.9g}\n"
        f"    float horizontalAperture = {horizontal_aperture:.9g}\n"
        f"    float verticalAperture = {vertical_aperture:.9g}\n"
        f"    float2 clippingRange = ({z_near:.9g}, {z_far:.9g})\n"
        f"    matrix4d xformOp:transform = ( {', '.join(rows)} )\n"
        "    uniform token[] xformOpOrder = [\"xformOp:transform\"]\n"
        "}\n",
        encoding="utf-8")


def capture_view_crop(record: dict):
    """The editor's capture cropped to its 3D viewport (the rect get_viewports
    reported, else the fixed region), as an RGB PIL image, or None."""
    from PIL import Image
    capture_path = pathlib.Path(record.get("screenshot", ""))
    if not capture_path.is_file():
        return None
    with Image.open(capture_path) as capture_image:
        capture = capture_image.convert("RGB")
    width, height = capture.size
    view = record.get("view") or {}
    if view.get("width", 0) > 0 and view.get("height", 0) > 0:
        box = (int(view["x"]), int(view["y"]), int(view["x"] + view["width"]), int(view["y"] + view["height"]))
        box = (max(box[0], 0), max(box[1], 0), min(box[2], width), min(box[3], height))
        if (box[2] > box[0]) and (box[3] > box[1]):
            return capture.crop(box)
    left, top, right, bottom = VIEWPORT_REGION
    return capture.crop((int(left * width), int(top * height), int(right * width), int(bottom * height)))


def match_against_storm(record: dict) -> dict:
    """Normalized cross-correlation between the capture's viewport and the
    Storm render, both grey, at the render's size. Storm leaves the
    background transparent; it is composited over the capture's own
    background colour (the median of the crop's border) so that only the
    content decides the score. `object_match` scores the pixels Storm
    covered, `match` the whole frame (a missing object lowers it)."""
    try:
        import numpy
        from PIL import Image
    except ImportError:
        return {"error": "PIL / numpy missing under py -3"}
    storm_path = pathlib.Path(record.get("storm_render", ""))
    capture = capture_view_crop(record)
    if (capture is None) or (not storm_path.is_file()):
        return {"error": "no capture or no Storm render"}
    with Image.open(storm_path) as storm_image:
        storm = storm_image.convert("RGBA")
    # Same vertical field of view on both sides (perspective_vertical): the
    # wider image loses its margins so the two frames cover the same view.
    def crop_to_aspect(image, aspect):
        w, h = image.size
        if (w / float(h)) > (aspect + 1.0e-3):
            new_w = int(round(h * aspect))
            left = (w - new_w) // 2
            return image.crop((left, 0, left + new_w, h))
        if (w / float(h)) < (aspect - 1.0e-3):
            new_h = int(round(w / aspect))
            top = (h - new_h) // 2
            return image.crop((0, top, w, top + new_h))
        return image
    horizontal = (record.get("view") or {}).get("camera_projection_type", "") == "perspective_horizontal"
    aspect = min(capture.width / float(capture.height), storm.width / float(storm.height))
    if horizontal:
        aspect = max(capture.width / float(capture.height), storm.width / float(storm.height))
    capture = crop_to_aspect(capture, aspect)
    storm = crop_to_aspect(storm, aspect)
    size = (min(storm.width, 512), max(int(round(min(storm.width, 512) / aspect)), 1))
    capture = numpy.asarray(capture.resize(size, Image.LANCZOS), dtype=float)
    storm = numpy.asarray(storm.resize(size, Image.LANCZOS), dtype=float)
    border = numpy.concatenate([capture[0], capture[-1], capture[:, 0], capture[:, -1]])
    background = numpy.median(border, axis=0)
    alpha = storm[:, :, 3:4] / 255.0
    storm_rgb = (storm[:, :, :3] * alpha) + (background * (1.0 - alpha))
    weights = numpy.array([0.299, 0.587, 0.114])
    a = capture @ weights
    b = storm_rgb @ weights

    def ncc(x, y):
        if x.size < 4:
            return None
        x = x - x.mean()
        y = y - y.mean()
        denominator = numpy.sqrt((x * x).sum() * (y * y).sum())
        if denominator <= 1.0e-9:
            return 1.0 if (numpy.abs(x).max() < 1.0e-9 and numpy.abs(y).max() < 1.0e-9) else 0.0
        return float((x * y).sum() / denominator)

    covered = alpha[:, :, 0] > 0.5
    coverage = float(covered.mean())
    # The silhouettes, independent of lighting and colour: where the capture
    # departs from its background against where Storm drew anything.
    drawn = numpy.abs(capture - background).max(axis=2) > 12.0
    union = float((drawn | covered).sum())
    silhouette = (float((drawn & covered).sum()) / union) if union > 0.0 else 1.0
    result = {
        "match": ncc(a, b),
        "object_match": ncc(a[covered], b[covered]) if covered.any() else None,
        "silhouette": round(silhouette, 4),
        "coverage": round(coverage, 4),
        "size": list(size),
    }
    for key in ("match", "object_match"):
        if result[key] is not None:
            result[key] = round(result[key], 4)
    return result


def storm_reference(record: dict, absolute: pathlib.Path, usd_tools: Usd_tools, shots_dir: pathlib.Path,
                    timeout: float) -> None:
    """The Storm render of the entry through the editor's own view, and the
    match score; fills the storm_* fields of the record."""
    record["storm_render"] = ""
    record["storm_error"] = ""
    record["storm_match"] = None
    record["storm_object_match"] = None
    record["storm_silhouette"] = None
    record["storm_coverage"] = None
    view = record.get("view") or {}
    if not view.get("camera_world_from_camera"):
        record["storm_error"] = "no viewport camera reported"
        return
    storm_dir = shots_dir / "storm"
    storm_dir.mkdir(parents=True, exist_ok=True)
    out_png = storm_dir / (record["slug"] + ".png")
    if out_png.is_file():
        out_png.unlink()
    width = min(int(view.get("width") or 960), 1024)
    camera = ""
    session_layer = None
    if (record.get("framing") or {}).get("camera_source") == "authored":
        # The camera the editor looks through is the file's first camera in
        # traversal order; pxr's list is in the same order.
        cameras = record.get("composed_cameras") or []
        camera = cameras[0] if cameras else ("/" + str(view.get("camera_path", "")).lstrip("/"))
    else:
        session_layer = storm_dir / (record["slug"] + ".camera.usda")
        stage_camera_session_layer(
            view,
            record.get("composed_up_axis") or record.get("up_axis") or "Y",
            record.get("composed_meters_per_unit") or record.get("meters_per_unit") or 1.0,
            session_layer)
        camera = "/ErheSurveyCamera"
    error = usd_tools.record(absolute, out_png, width, camera=camera, session_layer=session_layer, timeout=timeout)
    if error:
        record["storm_error"] = error
        return
    record["storm_render"] = out_png.as_posix()
    scored = match_against_storm(record)
    if scored.get("error"):
        record["storm_error"] = scored["error"]
        return
    record["storm_match"] = scored["match"]
    record["storm_object_match"] = scored["object_match"]
    record["storm_silhouette"] = scored["silhouette"]
    record["storm_coverage"] = scored["coverage"]


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


def is_unanswered(error: Exception) -> bool:
    """True when the editor is up but its main thread is inside a load that
    outlasted the wait: the MCP server answers "Request timed out" / "Server
    busy" from its own queue. The entry cannot be surveyed, and the run goes
    on."""
    text = str(error)
    return ("Request timed out" in text) or ("Server busy" in text)


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
                # Captures are compared against the renders each asset ships,
                # which hold neither of these. Session-only: the editor's
                # stored settings are left alone.
                self.mcp.call("set_graphics_settings",
                              {"sky_enabled": False, "grid_visible": False}, timeout=30.0)
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
SOURCE_LOCATION = re.compile(r"[A-Za-z]:[\\/][^\s]*?\.(?:cc|cpp|hh|hpp|h|inc):(?:[A-Za-z_][A-Za-z0-9_:<> ]*)?\(\):\d+\s+")

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
# the docked ImGui windows around it, of the view-axis widget in the
# viewport's top-right corner and of the tool buttons down its right edge.
# The whole frame is never flat - the editor's own UI fills the left half and
# the axis widget is always drawn - so "renders nothing" is decided on this
# region alone.
VIEWPORT_REGION = (0.52, 0.20, 0.92, 0.95)


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

def scene_names(editor: Editor, timeout: float = 120.0) -> set:
    scenes = editor.mcp.call("list_scenes", {}, timeout=timeout)
    return {s.get("name") for s in scenes.get("scenes", [])}


def wait_for_new_scene(editor: Editor, before: set, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not editor.alive():
            raise EditorDown("editor process exited during open_scene")
        added = scene_names(editor, timeout) - before
        if added:
            return sorted(added)[0]
        time.sleep(0.25)
    return ""


def wait_for_scene_gone(editor: Editor, name: str, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if name not in scene_names(editor, timeout):
            return True
        time.sleep(0.25)
    return False


def close_extra_scenes(editor: Editor, timeout: float) -> None:
    """A failed entry can still have left a scene behind (the load answered
    after the wait gave up); the next entry must start from the baseline. An
    editor that does not answer at all is the caller's business: the entry is
    already recorded, and the run goes on with a fresh editor."""
    try:
        leftovers = sorted(scene_names(editor, timeout) - editor.baseline_scenes)
    except RuntimeError:
        return
    for name in leftovers:
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


# get_async_status counters that must all read 0 before the scene is settled:
# async workers pending / running, operations queued on the operation stack,
# worker-prepared scene changes waiting for the main thread's flush, and asset
# load tasks still in flight.
IDLE_COUNTERS = ("pending", "running", "queued_operations", "pending_scene_commits", "asset_loads")


def wait_until_idle(editor: Editor, timeout: float) -> tuple:
    """Wait until the editor reports nothing in flight, so the capture shows the
    finished asset instead of a half-materialized one. Returns (seconds waited,
    "" or the message describing what was still in flight at the timeout)."""
    started = time.monotonic()
    idle_reads = 0
    counters = {}
    while True:
        status = editor.mcp.call("get_async_status", {}, timeout=timeout)
        counters = {name: int(status.get(name) or 0) for name in IDLE_COUNTERS}
        if all(value == 0 for value in counters.values()):
            # Two idle reads a poll apart: a worker publishing its result on the
            # main thread queues its follow-up work between two reads, so a
            # single idle read can fall into that gap.
            idle_reads += 1
            if idle_reads >= 2:
                return time.monotonic() - started, ""
        else:
            idle_reads = 0
        waited = time.monotonic() - started
        if waited >= timeout:
            in_flight = ", ".join(f"{name} {value}" for name, value in counters.items() if value > 0)
            return waited, f"still in flight after {waited:.0f} s: {in_flight}"
        time.sleep(0.2)


def survey_entry(editor: Editor, root: pathlib.Path, entry: dict, shots_dir: pathlib.Path,
                 load_timeout: float, close_wait: float, settle_frames: int,
                 usd_tools: Usd_tools = None, storm_threshold: float = 0.0) -> dict:
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
        "settle_error": "",
        "settle_seconds": None,
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
        "view": {},
        "storm_threshold": storm_threshold,
        "crash": False,
        "crash_detail": "",
        "log_tail": [],
        "verdict": "",
    })

    position = editor.log_size()

    if usd_tools is not None:
        record.update(composed_facts(usd_tools.stage_stats(absolute)))

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
                # An open_scene answers as soon as the scene exists; its meshes,
                # textures and materials keep arriving on worker threads. Every
                # count below, the framing AABB and the capture need the
                # finished asset, so wait for the editor to report itself idle
                # first.
                record["settle_seconds"], record["settle_error"] = wait_until_idle(editor, load_timeout)
                nodes = editor.mcp.call("get_scene_nodes", {"scene_name": scene}, timeout=load_timeout).get("nodes", [])
                record["prims"] = len(nodes)
                # Active content meshes only: an inactive prim (`active = false`,
                # an undefined `over`) and a prototype held by a `class` prim
                # (no content flag) are prims of erhe's tree but out of render,
                # as USD's default traversal predicate leaves them out of the
                # composed count this is compared against.
                record["meshes"] = sum(
                    1 for n in nodes
                    if (n.get("type") == "Mesh") and n.get("active", True) and n.get("content", True)
                )
                record["materials"] = len(editor.mcp.call("get_scene_materials", {"scene_name": scene}, timeout=load_timeout).get("materials", []))
                record["lights"] = len(editor.mcp.call("get_scene_lights", {"scene_name": scene}, timeout=load_timeout).get("lights", []))
                record["cameras"] = len(editor.mcp.call("get_scene_cameras", {"scene_name": scene}, timeout=load_timeout).get("cameras", []))
                # Opening a scene leaves its viewport bound to nothing when the
                # file authors no camera, so the capture must frame the scene
                # first and then let the viewport render.
                record["framing"] = editor.mcp.call("frame_scene", {"scene_name": scene}, timeout=load_timeout)
                # Framing can itself queue work (the mesh AABBs it reads).
                framing_wait, framing_error = wait_until_idle(editor, load_timeout)
                record["settle_seconds"] = (record["settle_seconds"] or 0.0) + framing_wait
                record["settle_error"] = record["settle_error"] or framing_error
                wait_frames(editor, settle_frames, load_timeout)
                editor.mcp.call("capture_screenshot", {"path": shot.as_posix()}, timeout=load_timeout)
                # The rect the 3D view occupies in the capture and the camera
                # it was rendered through, read after the frames above so the
                # window has its final size.
                # By the title frame_scene named, else by the scene the
                # viewport shows (the first framed window can be retitled
                # between the two calls).
                viewports = editor.mcp.call("get_viewports", {}, timeout=load_timeout).get("viewports", [])
                for viewport in viewports:
                    if viewport.get("title") == record["framing"].get("viewport"):
                        record["view"] = viewport
                        break
                if not record["view"]:
                    for viewport in viewports:
                        if viewport.get("scene") == scene:
                            record["view"] = viewport
                            break
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

    record["bounds_deviation"] = bounds_deviation(record)
    if (usd_tools is not None) and record["loaded"] and (not record["crash"]):
        # After the close: usdrecord holds the GPU for a while, and the editor
        # need not be idle for it.
        storm_reference(record, absolute, usd_tools, shots_dir, load_timeout)

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
    storm_path = pathlib.Path(record.get("storm_render", ""))
    try:
        capture = capture_view_crop(record)
    except Exception:
        capture = None
    if capture is None:
        return ""
    images = [capture]
    # The Storm render of the same view comes first when there is one; the
    # repository's own render (its own view, often an older revision) last.
    if storm_path.is_file():
        try:
            with Image.open(storm_path) as storm_image:
                storm = storm_image.convert("RGBA")
            backdrop = Image.new("RGBA", storm.size, (48, 48, 48, 255))
            images.append(Image.alpha_composite(backdrop, storm).convert("RGB"))
        except Exception:
            pass
    if references and (root / references[0]).is_file():
        try:
            with Image.open(root / references[0]) as reference_image:
                images.append(reference_image.convert("RGB"))
        except Exception:
            pass
    if len(images) < 2:
        return ""
    # Every panel is shown at the last reference's own height, so a file whose
    # authored camera the capture looks through puts the same view on every
    # panel and a difference between them is a real one.
    target_height = min(max(images[-1].height, 320), 900)
    panels = []
    for image in images:
        scale = target_height / float(image.height)
        panels.append(image.resize((max(int(image.width * scale), 1), target_height)))
    gap = 8
    sheet = Image.new("RGB", (sum(p.width for p in panels) + gap * (len(panels) - 1), target_height), (90, 90, 90))
    x = 0
    for panel in panels:
        sheet.paste(panel, (x, 0))
        x += panel.width + gap
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
    """Whether the file holds geometry the editor should have turned into a
    mesh: the composed stage's own Gprim / instancer count when OpenUSD read
    it, else the authored prim types LightUSD reported."""
    if record.get("composed_gprims") is not None:
        return ((record.get("composed_gprims") or 0) > 0) or ((record.get("composed_point_instancers") or 0) > 0)
    return any((t.get("type_name") in GEOMETRY_PRIM_TYPES) and (t.get("count", 0) > 0)
               for t in (record.get("authored_types") or []))


def storm_gap(record: dict) -> str:
    """The gap the Storm comparison names, or "": a match under the run's
    threshold on an entry whose render Storm produced."""
    threshold = float(record.get("storm_threshold") or 0.0)
    match = record.get("storm_match")
    if (threshold <= 0.0) or (match is None):
        return ""
    if match < threshold:
        return f"the frame differs from the Storm render of the same view (match {match:.2f})"
    return ""


def bounds_gap(record: dict) -> str:
    """The scene's extent disagrees with the composed stage's by more than a
    tenth of its diagonal: content placed, scaled or instanced differently
    from what OpenUSD composes (the transform / skinning class of gap)."""
    deviation = record.get("bounds_deviation")
    if (deviation is None) or (deviation <= 0.10):
        return ""
    return f"the scene's bounds are off the composed stage's by {deviation:.0%} of its diagonal"


def missing_mesh_gap(record: dict) -> str:
    """A PointInstancer or an instanced prototype makes the editor's mesh count
    legitimately exceed the composed one, so the counts name a gap only in the
    other direction; a count of zero is the no-mesh gap, named elsewhere."""
    composed = record.get("composed_meshes")
    if (composed is None) or (record.get("meshes", 0) >= composed) or (record.get("meshes", 0) == 0):
        return ""
    return f"{record['meshes']} of the {composed} composed meshes loaded"


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
    if record.get("settle_error"):
        return f"works, gap: the capture was taken before the load settled ({record['settle_error']})"
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
    if missing_mesh_gap(record):
        return f"works, gap: {missing_mesh_gap(record)}"
    if bounds_gap(record):
        return f"works, gap: {bounds_gap(record)}"
    if errors:
        return f"works, gap: {gap_name(errors[0]['message'])}"
    if warnings:
        return f"works, gap: {gap_name(warnings[0]['message'])}"
    if storm_gap(record):
        return f"works, gap: {storm_gap(record)}"
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

    expected_gap_patterns = []

    def add(key: str, level: str, asset: str, example: str) -> None:
        # A cause the entry's expected results name (the `gaps` patterns of
        # doc/usd-wg-assets-expected.json) is by design, not a gap.
        if any(pattern.search(key) for pattern in expected_gap_patterns):
            return
        slot = causes.setdefault(key, {"cause": key, "level": level, "assets": set(), "example": example})
        slot["assets"].add(asset)
        if (level == "error") and (slot["level"] == "warning"):
            slot["level"] = "error"

    for record in records:
        asset = record["path"]
        expected_gap_patterns = [re.compile(text) for text in record.get("expected_gap_patterns", [])]
        if record["crash"]:
            add("editor crash while loading the file", "failure", asset, record.get("crash_detail", ""))
        if (not record["loaded"]) and (not record["crash"]):
            cause = record["load_error"] or record["describe_error"] or "load did not produce a scene"
            add("load failure: " + gap_name(cause), "failure", asset, cause)
        for diagnostic in record["diagnostics"]:
            if diagnostic["level"] == "expected":
                continue
            add(diagnostic["message"], diagnostic["level"], asset, diagnostic.get("example", ""))
        stats = record.get("screenshot_stats") or {}
        # The two conditions the counts and the frame carry that no log line
        # states: geometry that produced no mesh, and meshes that reach the
        # frame but nothing lights them.
        if record["loaded"] and (record["meshes"] == 0) and authors_geometry(record):
            add("no mesh loaded: " + no_mesh_cause(record), "failure", asset, "")
        if record["loaded"] and (record["meshes"] > 0) and stats.get("available") and stats.get("flat"):
            add("renders nothing: the framed viewport is empty although meshes loaded", "failure", asset, "")
        if record["loaded"] and missing_mesh_gap(record):
            add("some composed meshes are not loaded", "failure", asset, missing_mesh_gap(record))
        if record["loaded"] and bounds_gap(record):
            add("the scene's bounds disagree with the composed stage's", "failure", asset, bounds_gap(record))
        if record["loaded"] and storm_gap(record):
            add("the frame differs from the Storm render of the same view", "appearance", asset,
                f"match {record['storm_match']:.2f}: {record.get('storm_render', '')}")
        # A file whose content lives in a subLayer loads as an empty stage and
        # says nothing about it, so the layer list is what names the cause.
        sublayered = any("sub" in str(layer.get("kind", "")).lower()
                         for layer in (record.get("layers") or []))
        if record["loaded"] and sublayered and (record["prims"] == 0):
            add(SUBLAYER_GAP, "failure", asset, "")
        # What the capture, compared against the repository's own reference
        # render, shows the editor getting wrong: a cause no log line states.
        if record.get("eye_gap") and eye_gap_holds(record) and (not record.get("expected_appearance")):
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
# Expected results: issues an entry is known to report by design
# --------------------------------------------------------------------------

DEFAULT_EXPECTED = pathlib.Path("doc/usd-wg-assets-expected.json")


def load_expected_results(path: pathlib.Path) -> dict:
    """The committed expected results, keyed by the entry's repo-relative path.

    Each item names the diagnostics (regular expressions matched against the
    normalized message and the example line) the entry reports by design, the
    `gaps` (regular expressions matched against the verdict's gap text and the
    Gaps section's cause text) the counts and the frame raise by design, and
    may declare its by-eye appearance gap expected too. None of those count
    against the verdict, so an entry whose only issues are expected is `works`.
    """
    if not path.is_file():
        return {}
    expected = {}
    for item in json.loads(path.read_text(encoding="utf-8")):
        expected[item["path"]] = {
            "diagnostics": [re.compile(pattern) for pattern in item.get("diagnostics", [])],
            "gaps": [pattern for pattern in item.get("gaps", [])],
            "appearance": item.get("appearance", ""),
            "reason": item.get("reason", ""),
        }
    return expected


def apply_expected_results(record: dict, expected: dict) -> bool:
    """Relabel the record's expected diagnostics and recompute its verdict.

    Idempotent: a diagnostic keeps its observed level in `level_observed`, so
    an expectation removed from the sidecar takes effect on the next apply
    without a re-run. Returns True when the record has any expectation.
    """
    restored = False
    for diagnostic in record.get("diagnostics", []):
        if "level_observed" in diagnostic:
            diagnostic["level"] = diagnostic.pop("level_observed")
            restored = True
    record.pop("expected_appearance", None)
    record.pop("expected_reason", None)
    record.pop("expected_gap", None)
    record.pop("expected_gap_patterns", None)
    item = expected.get(record["path"])
    if item is None:
        record["expected_diagnostics"] = 0
        if restored and (not record.get("crash")) and record.get("loaded"):
            record["verdict"] = provisional_verdict(record)
        return False
    matched = 0
    for diagnostic in record.get("diagnostics", []):
        if diagnostic["level"] not in ("warning", "error"):
            continue
        texts = (diagnostic["message"], diagnostic.get("example", ""))
        if any(pattern.search(text) for pattern in item["diagnostics"] for text in texts):
            diagnostic["level_observed"] = diagnostic["level"]
            diagnostic["level"] = "expected"
            matched += diagnostic["count"]
    record["expected_diagnostics"] = matched
    record["expected_reason"] = item["reason"]
    if item["appearance"]:
        record["expected_appearance"] = item["appearance"]
    gaps = item.get("gaps", [])
    if gaps:
        record["expected_gap_patterns"] = list(gaps)
    if (not record.get("crash")) and record.get("loaded"):
        record["verdict"] = provisional_verdict(record)
        if verdict_class(record["verdict"]) == "works, gap":
            gap_text = record["verdict"][len("works, gap:"):].strip()
            if any(re.search(pattern, gap_text) for pattern in gaps):
                record["expected_gap"] = gap_text
                record["verdict"]      = "works"
    return True


def apply_expected_results_to_summary(summary: dict, expected: dict) -> int:
    return sum(1 for record in summary["entries"] if apply_expected_results(record, expected))


# --------------------------------------------------------------------------
# By-eye verdicts and summary merging
# --------------------------------------------------------------------------

DEFAULT_EYE = pathlib.Path("doc/usd-wg-assets-eye.json")


def load_eye_notes(path: pathlib.Path) -> dict:
    """The committed by-eye verdicts, keyed by the entry's repo-relative path.

    A run rebuilds every other per-entry fact from the editor, so the one fact
    only a human can supply lives beside the document instead of in the run's
    summary.json.
    """
    if not path.is_file():
        return {}
    notes = {}
    for item in json.loads(path.read_text(encoding="utf-8")):
        notes[item["path"]] = {"note": item.get("note", ""), "gap": item.get("gap", "")}
    return notes


def save_eye_notes(path: pathlib.Path, notes: dict) -> None:
    rows = [{"path": key, "note": value["note"], "gap": value["gap"]}
            for key, value in sorted(notes.items())]
    path.write_text(json.dumps(rows, indent=1) + "\n", encoding="utf-8")


def apply_eye_notes(summary: dict, notes: dict) -> int:
    """Put each recorded by-eye verdict on its entry; entries with none carry
    the verdict the counts, the log and the screenshot decided."""
    applied = 0
    for record in summary["entries"]:
        note = notes.get(record["path"])
        if note is None:
            record["eye_checked"] = False
            record["eye_note"] = ""
            record["eye_gap"] = ""
            continue
        record["eye_checked"] = True
        record["eye_note"] = note["note"]
        record["eye_gap"] = note["gap"]
        # The capture outranks the counts and the log: a gap only the eye sees
        # is a gap, even where nothing was logged and the frame is not empty.
        if note["gap"] and (record.get("verdict") == "works") and (not record.get("expected_appearance")):
            record["verdict"] = "works, gap: " + gap_name(note["gap"])
        applied += 1
    return applied


def merge_entries(summary: dict, fresh: list) -> dict:
    """Fold a run's records into the summary: a surveyed entry replaces its
    older record, every other record is kept as it was, and the counters state
    what the merged whole holds."""
    entries = {record["path"]: record for record in summary.get("entries", [])}
    for record in fresh:
        entries[record["path"]] = record
    records = sorted(entries.values(), key=lambda r: r["path"])
    dates = sorted(str(r.get("surveyed_at", ""))[:10] for r in records if r.get("surveyed_at"))
    summary["entries"] = records
    summary["entry_count"] = len(records)
    summary["wall_time_s"] = sum(float(r.get("survey_seconds") or 0.0) for r in records)
    summary["run_date"] = dates[-1] if dates else summary.get("run_date", "")
    return summary


def entries_of_newest_run(records: list) -> int:
    dates = [str(r.get("surveyed_at", ""))[:10] for r in records if r.get("surveyed_at")]
    if not dates:
        return len(records)
    newest = max(dates)
    return sum(1 for date in dates if date == newest)


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
    if (errors == 0) and (warnings == 0) and (not record.get("expected_diagnostics")):
        return "none"
    named = [d["message"] for d in record["diagnostics"] if d["level"] in ("warning", "error")]
    first = named[0] if named else ""
    parts = []
    if errors:
        parts.append(f"{errors} error")
    if warnings:
        parts.append(f"{warnings} warning")
    expected = record.get("expected_diagnostics", 0)
    if expected:
        parts.append(f"{expected} expected")
    return ", ".join(parts) + (f"; {gap_name(first)[:60]}" if first else "")


# What the editor would have to support to clear each cause. Each line was
# written after reading the code that emits the message (the source is named),
# so it states the missing support, not a instruction to go and look.
REMEDY = [
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
    (re.compile(r"Primvar `\*` has no authored value"),
     "a primvar declared with no value at all is skipped; check the reference render ignores it too before calling this a gap"),
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
    (re.compile(r"bounds disagree with the composed stage"),
     "place, scale or instance the content the way OpenUSD composes it: compare the prim's world "
     "transform with pxr's XformCache (scripts/usd_wg_pxr_stage.py reads the composed bounds). Only "
     "the root layer's upAxis / metersPerUnit apply to a stage, so a reference or payload target is "
     "loaded with erhe::usd::Stage_metrics::referenced and contributes no correction of its own"),
    (re.compile(r"some composed meshes are not loaded"),
     "load every UsdGeomMesh the composed stage holds: the missing ones name the prim kind or the "
     "composition arc the importer skips"),
    (re.compile(r"differs from the Storm render of the same view"),
     "read the comparison composite (capture | Storm | reference) and name the appearance difference"),
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
    out.append("A run restricted to some entries (`--only`, `--exclude`, `--limit`) surveys those and")
    out.append("keeps every other entry's record, so the document always states the whole")
    out.append("survey; each record carries the date it was surveyed on. The by-eye")
    out.append("verdicts of the next section come from `doc/usd-wg-assets-eye.json`, which")
    out.append("a run reads and never writes; `--eye-note <entry> \"<what the capture")
    out.append("shows>\" [--eye-gap \"<cause>\"]` is how one is recorded. The expected")
    out.append("results of the section after it come from `doc/usd-wg-assets-expected.json`,")
    out.append("hand-edited: the diagnostics an entry reports by design (and, where stated,")
    out.append("its by-eye appearance gap) do not count against its verdict.")
    out.append("Screenshot paths are under `logs/`, which is gitignored: the column is a")
    out.append("pointer into the last run's output, not a committed file.")
    out.append("")
    out.append("A capture waits for the editor to report itself idle first")
    out.append("(`get_async_status`: pending, running, queued_operations,")
    out.append("pending_scene_commits and asset_loads all 0 over two reads), because")
    out.append("`open_scene` answers as soon as the scene exists while its meshes and")
    out.append("textures keep arriving on worker threads; the counts, the framing and the")
    out.append("image all come from the finished asset. An entry whose load is still in")
    out.append("flight after `--load-timeout` says so as its gap.")
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
    out.append("The editor's own sky background and grid are off for every capture")
    out.append("(`set_graphics_settings {\"sky_enabled\": false, \"grid_visible\": false}` once")
    out.append("per editor launch, a session-only override that leaves the stored settings")
    out.append("untouched), so an image holds only what the file authors and compares")
    out.append("cleanly against the asset's own reference render.")
    out.append("")
    out.append("The `Reference` column names the renders the repository ships beside each")
    out.append("asset (`screenshots/` first, then `thumbnails/`), repo-relative to")
    out.append("`<usd-wg-assets>`. `--compose-comparisons` puts the capture, the Storm render")
    out.append("and the first of those side by side under `logs/usd_wg_survey/compare/`,")
    out.append("which is how the appearance verdicts below were reached.")
    out.append("")
    out.append("With a prebuilt OpenUSD named by `--usd-root` (or `ERHE_USD_ROOT`), each")
    out.append("entry also carries what OpenUSD itself makes of the file: the `Composed`")
    out.append("column is the composed stage's `UsdGeomMesh` count (`scripts/usd_wg_pxr_stage.py`,")
    out.append("instance proxies included, so an instanced prototype counts once per")
    out.append("instance and a `PointInstancer`'s instances not at all), and the `Storm`")
    out.append("column is the normalized cross-correlation between the capture's 3D view and")
    out.append("a `usdrecord` Storm render of the same file through the same camera - the")
    out.append("editor's computed camera authored into a session layer in the stage's own")
    out.append("space, or the file's first camera named by path - at the capture's aspect")
    out.append("(1.00 = identical grey images; lighting differs by design, so a lit, matching")
    out.append("scene scores well below 1). Under `--storm-threshold` a lower score is a gap")
    out.append("of its own; the score is what orders the by-eye reads. The composed stage's")
    out.append("world bounds are compared with the scene's too (`bounds_deviation` in the")
    out.append("summary): a disagreement over a tenth of the diagonal is a gap, the way a")
    out.append("dropped transform, an unapplied skin or a stray prototype shows up.")
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
    newest = entries_of_newest_run(records)
    on_date = "" if (newest == len(records)) else f" ({newest} of them on that date)"
    out.append(f"Run: {summary['run_date']}{on_date}, {summary['entry_count']} entries, "
               f"{summary['wall_time_s']:.0f} s of survey time.")
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
    expected_rows = sorted((r for r in records if r.get("expected_reason")), key=lambda r: r["path"])
    if expected_rows:
        out.append("## Expected results")
        out.append("")
        out.append("These entries report issues by design; the issues named in")
        out.append("`doc/usd-wg-assets-expected.json` are excluded from their verdict and from")
        out.append("the Gaps section. The count is how many logged lines the expectation took.")
        out.append("")
        out.append("| Entry file | Expected | Why |")
        out.append("| --- | --- | --- |")
        for record in expected_rows:
            what = f"{record.get('expected_diagnostics', 0)} diagnostic line(s)"
            if record.get("expected_gap"):
                what += f"; the gap `{record['expected_gap']}`"
            if record.get("expected_appearance"):
                what += "; the by-eye appearance gap"
            out.append("| {} | {} | {} |".format(cell(record["path"]), cell(what), cell(record["expected_reason"])))
        out.append("")
    out.append("## Entries")
    out.append("")
    out.append("`authored` is the prim count `describe_usd_file` reports for the file;")
    out.append("`prims` / `meshes` / `materials` / `lights` are what the loaded scene holds.")
    out.append("")
    out.append("| Folder | Entry file | Load | Authored | Prims | Meshes | Composed | Mats | Lights | Storm | Warnings and errors | Screenshot | Verdict |")
    out.append("| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |")
    for record in sorted(records, key=lambda r: r["path"]):
        load = "crash" if record["crash"] else ("ok" if record["loaded"] else "failed")
        authored = record["authored_prims"]
        composed = record.get("composed_meshes")
        storm = record.get("storm_match")
        out.append("| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |".format(
            cell(record["folder"]),
            cell(pathlib.PurePosixPath(record["path"]).name),
            load,
            "-" if authored is None else authored,
            record["prims"], record["meshes"],
            "-" if composed is None else composed,
            record["materials"], record["lights"],
            "-" if storm is None else f"{storm:.2f}",
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
# --------------------------------------------------------------------------
# Test database: per-entry status and run time of the last recorded run
# --------------------------------------------------------------------------

TEST_DB_STATUSES = ("pass", "gap", "fail")


def test_status(verdict: str) -> str:
    """pass = works without a gap, gap = works with gap(s), fail = fails or crash."""
    kind = verdict_class(verdict)
    if kind == "works":
        return "pass"
    if kind == "works, gap":
        return "gap"
    return "fail"


def load_test_db(path: pathlib.Path) -> dict:
    if not path.is_file():
        return {"entries": {}}
    db = json.loads(path.read_text(encoding="utf-8"))
    db.setdefault("entries", {})
    return db


def save_test_db(path: pathlib.Path, db: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(db, indent=1, sort_keys=True), encoding="utf-8")


def record_test_result(db: dict, record: dict) -> None:
    db["entries"][record["path"]] = {
        "status": test_status(record["verdict"]),
        "verdict": record["verdict"],
        "seconds": round(float(record.get("survey_seconds") or 0.0), 3),
        "recorded_at": record.get("surveyed_at", ""),
    }


def sort_failing_first(entries: list, db: dict) -> list:
    """Recorded failures first, shortest run first; then recorded gaps the same
    way; then entries the database has never seen, in survey order; passing
    entries last, in survey order."""
    rank = {"fail": 0, "gap": 1, None: 2, "pass": 3}
    known = db["entries"]

    def key(item):
        index, entry = item
        recorded = known.get(entry["path"])
        status = recorded["status"] if recorded else None
        if status in ("fail", "gap"):
            return (rank[status], float(recorded.get("seconds") or 0.0), index)
        return (rank.get(status, 2), 0.0, index)

    return [entry for _, entry in sorted(enumerate(entries), key=key)]


def sort_unrecorded_first(entries: list, db: dict) -> list:
    """Entries the database has no result for move to the front, keeping
    their order; the recorded entries follow in the order they had."""
    known = db["entries"]
    unrecorded = [e for e in entries if e["path"] not in known]
    return unrecorded + [e for e in entries if e["path"] in known]


def sort_last(entries: list, fragments: list) -> list:
    """Entries whose path contains any fragment move to the end, in the order
    they had; the rest keep their order."""
    deferred = [e for e in entries if any(fragment in e["path"] for fragment in fragments)]
    return [e for e in entries if e not in deferred] + deferred


DEFAULT_SHOTS = pathlib.Path("logs/usd_wg_survey")
DEFAULT_DOC = pathlib.Path("doc/usd-wg-assets.md")
DEFAULT_TEST_DB = DEFAULT_SHOTS / "test_database.json"


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


def check_bookkeeping() -> bool:
    """Prove the document-level rules that no editor run exercises: a by-eye
    verdict reaches its entry from the sidecar, a restricted run keeps the
    entries it did not survey, and an expected result clears the verdict."""
    ok = True

    notes = {"a/one.usda": {"note": "the cube is blue", "gap": "blue is wrong"}}
    summary = {"entries": [{"path": "a/one.usda", "eye_note": "stale", "verdict": "works"},
                           {"path": "b/two.usda", "verdict": "works"}]}
    applied = apply_eye_notes(summary, notes)
    first, second = summary["entries"]
    if (applied != 1) or (not first["eye_checked"]) or (first["eye_note"] != "the cube is blue") \
            or (first["eye_gap"] != "blue is wrong") or second["eye_checked"] or (second["eye_note"] != "") \
            or (first["verdict"] != "works, gap: blue is wrong") or (second["verdict"] != "works"):
        print("self-test: FAIL - the sidecar's by-eye verdict did not reach its entry")
        ok = False

    gaps = gather_gaps([
        {"path": "a/one.usda", "crash": False, "loaded": True, "prims": 1, "meshes": 1,
         "diagnostics": [], "screenshot_stats": {}, "layers": [], "authored_types": [],
         "eye_gap": "blue is wrong", "eye_note": "the cube is blue"},
    ])
    if not any((g["cause"] == "blue is wrong") and (g["level"] == "appearance") for g in gaps):
        print("self-test: FAIL - a by-eye gap did not reach the gaps table")
        ok = False

    summary = {"entries": [
        {"path": "a/one.usda", "surveyed_at": "2026-09-01T10:00:00", "survey_seconds": 4.0, "verdict": "works"},
        {"path": "b/two.usda", "surveyed_at": "2026-09-01T10:00:10", "survey_seconds": 6.0, "verdict": "works"},
    ]}
    merge_entries(summary, [{"path": "b/two.usda", "surveyed_at": "2026-09-08T09:00:00",
                             "survey_seconds": 10.0, "verdict": "crash"}])
    paths = [record["path"] for record in summary["entries"]]
    if (paths != ["a/one.usda", "b/two.usda"]) or (summary["entry_count"] != 2) \
            or (summary["entries"][1]["verdict"] != "crash") \
            or (summary["entries"][0]["verdict"] != "works") \
            or (abs(summary["wall_time_s"] - 14.0) > 1.0e-6) or (summary["run_date"] != "2026-09-08"):
        print("self-test: FAIL - a restricted run did not merge into the whole survey")
        ok = False
    if entries_of_newest_run(summary["entries"]) != 1:
        print("self-test: FAIL - the mixed-date entry count is wrong")
        ok = False

    # An expected diagnostic and an expected appearance gap leave the verdict
    # at works; removing the expectation restores the observed level.
    expected = {"a/one.usda": {"diagnostics": [re.compile("not an allowed token")],
                               "appearance": "by design", "reason": "the file authors it on purpose"}}
    record = {"path": "a/one.usda", "crash": False, "loaded": True, "prims": 1, "meshes": 1,
              "materials": 1, "lights": 0, "cameras": 0, "authored_prims": 1, "authored_types": [],
              "screenshot_stats": {"available": True, "flat": False}, "layers": [],
              "diagnostics": [{"level": "warning", "count": 5,
                               "message": "Attribute `*`: `*` is not an allowed token. Ignore it",
                               "example": "Attribute `inputs:x`: `y` is not an allowed token. Ignore it"}],
              "verdict": "works, gap: stale"}
    apply_expected_results(record, expected)
    summary = {"entries": [record]}
    apply_eye_notes(summary, notes)
    gaps = gather_gaps(summary["entries"])
    if (record["verdict"] != "works") or (record["expected_diagnostics"] != 5)             or (record["diagnostics"][0]["level"] != "expected") or gaps:
        print("self-test: FAIL - an expected result did not clear the verdict")
        ok = False
    apply_expected_results(record, {})
    if (record["diagnostics"][0]["level"] != "warning") or ("level_observed" in record["diagnostics"][0])             or (record["verdict"] != "works, gap: Attribute `*`: `*` is not an allowed token. Ignore it"):
        print("self-test: FAIL - removing an expectation did not restore the observed level")
        ok = False

    print("self-test: bookkeeping " + ("PASS - sidecar and merge behave" if ok else "FAIL"))
    return ok


def run_self_test(args, usd_tools: Usd_tools = None) -> int:
    """Prove the capture path before trusting a survey run.

    Opens a scene known to hold one lit, materialled mesh, frames it and
    captures, then checks the viewport region of the PNG: an unbound or
    unframed viewport is one flat color, so a varied crop means the pipeline
    from open_scene through frame_scene to capture_screenshot works. The
    document's own bookkeeping - the by-eye sidecar and the subset merge - is
    checked first, without an editor.
    """
    bookkeeping_ok = check_bookkeeping()
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
        wait_until_idle(editor, args.load_timeout)
        framing = editor.mcp.call("frame_scene", {"scene_name": scene}, timeout=args.load_timeout)
        wait_until_idle(editor, args.load_timeout)
        wait_frames(editor, args.settle_frames, args.load_timeout)
        editor.mcp.call("capture_screenshot", {"path": shot.as_posix()}, timeout=args.load_timeout)
        stats = screenshot_stats(shot)
        view = {}
        for viewport in editor.mcp.call("get_viewports", {}, timeout=args.load_timeout).get("viewports", []):
            if viewport.get("title") == framing.get("viewport"):
                view = viewport
    finally:
        editor.stop()

    storm_ok = True
    if usd_tools is not None:
        # The Storm leg on the same file: a render must come back and the two
        # silhouettes must overlap, which proves the camera transfer; the grey
        # match is printed, not asserted - the fixture's unrotated DistantLight
        # lights nothing in Storm, so its cube renders black there.
        record = {"slug": "selftest", "screenshot": shot.as_posix(), "framing": framing, "view": view}
        record.update(composed_facts(usd_tools.stage_stats(source.resolve())))
        storm_reference(record, source.resolve(), usd_tools, args.shots, args.load_timeout)
        print(f"self-test: composed meshes={record.get('composed_meshes')} "
              f"storm={record.get('storm_render') or record.get('storm_error')} "
              f"match={record.get('storm_match')} object_match={record.get('storm_object_match')} "
              f"silhouette={record.get('storm_silhouette')} coverage={record.get('storm_coverage')}")
        storm_ok = (record.get("storm_silhouette") is not None) and (record["storm_silhouette"] >= 0.5)
        print("self-test: PASS - the Storm render shows the same view" if storm_ok
              else "self-test: FAIL - no Storm render, or its silhouette is not where the capture's is")

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
    return 0 if (ok and bookkeeping_ok and storm_ok) else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Survey the USD Assets Working Group repository in a headless erhe editor")
    parser.add_argument("--root", default=os.environ.get("ERHE_USD_WG_ASSETS", ""),
                        help="root of a clone of github.com/usd-wg/assets (default: ERHE_USD_WG_ASSETS)")
    parser.add_argument("--editor", type=pathlib.Path, default=DEFAULT_EDITOR, help="headless editor executable")
    parser.add_argument("--port", type=int, default=int(os.environ.get("ERHE_MCP_PORT", "3743")), help="MCP port")
    parser.add_argument("--doc", type=pathlib.Path, default=DEFAULT_DOC, help="document to write")
    parser.add_argument("--shots", type=pathlib.Path, default=DEFAULT_SHOTS, help="screenshot and summary directory")
    parser.add_argument("--load-timeout", type=float, default=400.0, help="seconds a single load may take")
    parser.add_argument("--ready-timeout", type=float, default=300.0, help="seconds to wait for a launched editor")
    parser.add_argument("--close-wait", type=float, default=7.0, help="seconds to wait for the scene-close leak watchdog")
    parser.add_argument("--settle-frames", type=int, default=6, help="frames to render after framing, before the capture")
    parser.add_argument("--self-test", action="store_true", help="prove the capture on a known-good scene and exit")
    parser.add_argument("--self-test-file", default="src/erhe/usd/test/data/cube.usda", help="scene the self-test opens")
    parser.add_argument("--max-per-folder", type=int, default=4, help="cap on entries taken from one folder (0 = no cap)")
    parser.add_argument("--limit", type=int, default=0, help="survey only the first N entries")
    parser.add_argument("--only", action="append", default=[],
                        help="survey only entries whose repo-relative path contains this substring (repeatable)")
    parser.add_argument("--exclude", action="append", default=[],
                        help="skip entries whose repo-relative path contains this substring (repeatable, applied after --only)")
    parser.add_argument("--unrecorded-first", action="store_true",
                        help="run the entries the test database has no result for before the recorded ones (applied after --failing-first, before --last)")
    parser.add_argument("--last", action="append", default=[],
                        help="run entries whose repo-relative path contains this substring after every other entry (repeatable; applied after --failing-first, before --limit)")
    parser.add_argument("--stop-on-gap", action="store_true",
                        help="stop after the first entry whose verdict is not `works` (the summary, document and test database still record what ran)")
    parser.add_argument("--list-entries", action="store_true", help="print the entry list and exit")
    parser.add_argument("--from-summary", action="store_true", help="regenerate the document from summary.json, no editor")
    parser.add_argument("--eye", type=pathlib.Path, default=DEFAULT_EYE,
                        help="the by-eye verdict sidecar a run reads and --eye-note writes")
    parser.add_argument("--expected", type=pathlib.Path, default=DEFAULT_EXPECTED,
                        help="the expected-results sidecar: per entry, the diagnostics (regular expressions) it reports by design")
    parser.add_argument("--eye-note", nargs=2, metavar=("ENTRY", "NOTE"), default=None,
                        help="record what the capture of ENTRY shows in the sidecar and exit")
    parser.add_argument("--eye-gap", default="",
                        help="with --eye-note: the appearance gap that verdict names (empty = none)")
    parser.add_argument("--refresh-cameras", action="store_true",
                        help="reopen the entries whose file authors a UsdGeomCamera and record what the imported Camera reads; needs --root")
    parser.add_argument("--compose-comparisons", action="store_true",
                        help="write logs/usd_wg_survey/compare/<entry>.png (capture beside the Storm render and the first reference render) and exit; needs --root")
    parser.add_argument("--usd-root", default=DEFAULT_USD_ROOT,
                        help="a prebuilt OpenUSD (its scripts/set_usd_env.bat and usdrecord.bat): adds the composed-stage counts and a Storm render of the same view per entry; default from ERHE_USD_ROOT; empty = off")
    parser.add_argument("--storm-threshold", type=float, default=0.0,
                        help="a Storm match under this value is a gap of its own (0 = record the score only)")
    parser.add_argument("--test-db", type=pathlib.Path, default=DEFAULT_TEST_DB,
                        help="the test database: per entry, pass / gap / fail and the run time of its last recorded run")
    parser.add_argument("--clear-test-db", action="store_true",
                        help="empty the test database first (exits when no survey is requested)")
    parser.add_argument("--record-test-db", action="store_true",
                        help="update the test database with each surveyed entry's status and run time")
    parser.add_argument("--failing-first", action="store_true",
                        help="run the entries the test database records as failing first, shortest run first, then gaps, unrecorded entries, passing entries")
    args = parser.parse_args()

    usd_tools = None
    if args.usd_root:
        try:
            usd_tools = Usd_tools(pathlib.Path(args.usd_root))
        except FileNotFoundError as error:
            print(str(error), file=sys.stderr)
            return 2

    args.shots.mkdir(parents=True, exist_ok=True)
    summary_path = args.shots / "summary.json"

    if args.clear_test_db:
        save_test_db(args.test_db, {"entries": {}})
        print(f"cleared the test database {args.test_db}")
        if not args.root:
            return 0
    test_db = load_test_db(args.test_db)
    expected = load_expected_results(args.expected)
    eye_notes = load_eye_notes(args.eye)

    if args.eye_note:
        entry, note = args.eye_note
        notes = load_eye_notes(args.eye)
        notes[entry] = {"note": note, "gap": args.eye_gap}
        save_eye_notes(args.eye, notes)
        print(f"recorded the by-eye verdict of {entry} in {args.eye} ({len(notes)} entries)")
        if summary_path.is_file():
            summary = json.loads(summary_path.read_text(encoding="utf-8"))
            apply_expected_results_to_summary(summary, expected)
            apply_eye_notes(summary, notes)
            write_document(args.doc, summary)
            print(f"wrote {args.doc} from {summary_path}")
        return 0

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
        return run_self_test(args, usd_tools)

    if args.from_summary:
        if not summary_path.is_file():
            print(f"no summary at {summary_path}; run the survey first", file=sys.stderr)
            return 2
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        expected_count = apply_expected_results_to_summary(summary, expected)
        applied = apply_eye_notes(summary, load_eye_notes(args.eye))
        write_document(args.doc, summary)
        print(f"wrote {args.doc} from {summary_path} ({summary['entry_count']} entries, "
              f"{applied} by-eye verdict(s) from {args.eye}, {expected_count} expected result(s) from {args.expected})")
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
    if args.exclude:
        entries = [e for e in entries if not any(fragment in e["path"] for fragment in args.exclude)]
    if args.failing_first:
        entries = sort_failing_first(entries, test_db)
    if args.unrecorded_first:
        entries = sort_unrecorded_first(entries, test_db)
    if args.last:
        entries = sort_last(entries, args.last)
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
            entry_started = time.monotonic()
            entry_log_position = editor.log_size()
            try:
                record = survey_entry(editor, root, entry, args.shots, args.load_timeout,
                                      args.close_wait, args.settle_frames,
                                      usd_tools, args.storm_threshold)
            except (RuntimeError, EditorDown) as error:
                # An editor that stops answering while a load holds the main
                # thread has not crashed: the entry is recorded as a failure
                # naming the wait, and the editor is replaced below so the
                # next entry starts from a fresh one either way.
                unanswered = is_unanswered(error)
                detail = (
                    f"the editor did not answer within {args.load_timeout:.0f} s while loading"
                    if unanswered else str(error)[:300]
                )
                record = dict(entry)
                record.update({
                    "slug": entry_slug(entry["path"]), "screenshot": "", "crash": not unanswered,
                    "crash_detail": "" if unanswered else detail, "loaded": False,
                    "prims": 0, "meshes": 0,
                    "materials": 0, "lights": 0, "cameras": 0, "diagnostics": [],
                    "authored_prims": None, "authored_types": [],
                    "verdict": (f"fails: {detail}" if unanswered else "crash"),
                    "log_tail": [l[-300:] for l in editor.log_since(entry_log_position)[-25:]],
                    "scene_close": "", "screenshot_stats": {},
                    "load_error": detail if unanswered else "",
                    "describe_error": "", "scene_name": "",
                    "settle_error": "", "settle_seconds": None,
                    "unanswered": unanswered,
                })
            record["surveyed_at"] = datetime.datetime.now().isoformat(timespec="seconds")
            record["survey_seconds"] = time.monotonic() - entry_started
            apply_expected_results(record, expected)
            # The by-eye verdict belongs to the entry's status and to the
            # stop decision, so it is applied here as well as on the summary.
            apply_eye_notes({"entries": [record]}, eye_notes)
            records.append(record)
            print(f"      {record['verdict']}", flush=True)
            if args.record_test_db:
                record_test_result(test_db, record)
                save_test_db(args.test_db, test_db)
            if (not record["crash"]) and editor.alive():
                try:
                    close_extra_scenes(editor, args.load_timeout)
                except EditorDown:
                    record["crash"] = True
                    record["crash_detail"] = "editor died while closing leftover scenes"
                    record["verdict"] = "crash"
            if args.stop_on_gap and (verdict_class(record["verdict"]) != "works"):
                print(f"      stopping at the first gap/failure (--stop-on-gap): {entry['path']}", flush=True)
                break
            if record["crash"] or record.get("unanswered", False) or (not editor.alive()):
                print("      editor down; restarting", flush=True)
                editor.stop()
                editor.start(args.ready_timeout)
    finally:
        editor.stop()

    # A run restricted to some entries keeps every entry it did not survey, so
    # the summary and the document always state the whole survey.
    subset = bool(args.only) or bool(args.exclude) or (args.limit > 0) or (len(records) < len(entries))
    if subset and summary_path.is_file():
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
    else:
        summary = {"entries": []}
    summary["editor_launches"] = editor.launches
    summary["max_per_folder"] = args.max_per_folder
    merge_entries(summary, records)
    apply_expected_results_to_summary(summary, expected)
    for record in summary["entries"]:
        # The by-eye verdicts live in the sidecar; the summary states what the
        # run itself observed.
        for key in ("eye_checked", "eye_note", "eye_gap"):
            record.pop(key, None)
    summary_path.write_text(json.dumps(summary, indent=1), encoding="utf-8")
    apply_eye_notes(summary, load_eye_notes(args.eye))
    write_document(args.doc, summary)
    surveyed_now = len(records)
    records = summary["entries"]

    counts = collections.Counter(verdict_class(r["verdict"]) for r in records)
    print(f"\n{len(records)} entries ({surveyed_now} surveyed now) in "
          f"{time.monotonic() - started:.0f} s, {editor.launches} launch(es)")
    print(f"works {counts['works']}, works-gap {counts['works, gap']}, fails {counts['fails']}, crash {counts['crash']}")
    for gap in gather_gaps(records)[:5]:
        print(f"  {gap['count']:3d}  {gap['cause'][:90]}")
    print(f"wrote {args.doc} and {summary_path}")
    if args.record_test_db:
        db_counts = collections.Counter(e["status"] for e in test_db["entries"].values())
        print(f"test database {args.test_db}: {len(test_db['entries'])} entries "
              f"(pass {db_counts['pass']}, gap {db_counts['gap']}, fail {db_counts['fail']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
