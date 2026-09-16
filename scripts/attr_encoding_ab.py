"""A/B image set for the optimized variant's compact attribute encodings.

    python scripts/attr_encoding_ab.py

Produces, in logs/attr_encoding_ab/, the images the user looks at to accept or
reject doc/meshoptimizer-attribute-encodings-plan.md. Rendering identity is NOT
the criterion here: every encoding in that plan is lossy by design, so the
expected result is a small non-zero difference. The acceptance test is visual.

ABeautifulGame only - Bistro is out of scope for this work.

What it captures (each one a full editor launch; ~1 minute each):

    off_a     optimize_meshes = false                      control
    off_b     optimize_meshes = false                      control
    on_qoff   optimize_meshes = true,  quantize = false    attribute encodings only
    on_qon    optimize_meshes = true,  quantize = true     attributes + positions

quantize_vertex_positions only ever affects the OPTIMIZED variant (requirement 8
of doc/meshoptimizer-integration.md: the base variant is always float3), which
is what makes the on_qoff / on_qon split meaningful and why there is only one
`off` side. The comparisons written out are:

    control       off_a  vs off_b    the noise floor - read every other number
                                     relative to THIS, not to zero
    attributes    off_a  vs on_qoff  what the TBN quaternion, the unorm16 UVs,
                                     the unorm8 colors and the implicit-sum
                                     weights cost, with positions float on both
                                     sides
    all           off_a  vs on_qon   the whole optimized variant
    positions     on_qoff vs on_qon  the position quantization epsilon alone,
                                     already accepted in earlier work

For each comparison: a side-by-side PNG, an amplified difference PNG (absolute
difference times 8, so a 1-LSB difference is actually visible), and the
differing-pixel percentage over the cropped viewport.

It also records what the GPU buffers actually contain, because a passing control
pair is necessary and NOT sufficient - the code path under test may not be
reached at all. If the optimized stride in summary.txt is not smaller than the
base one, the images below are comparing two float builds and mean nothing.

Restores config/editor/*.json afterwards.
"""
import os
import shutil
import subprocess
import sys
import time

REPO = r"D:\erhe"
OUT_DIR_REL = os.path.join("logs", "attr_encoding_ab")
OUT_DIR = os.path.join(REPO, OUT_DIR_REL)

# The tree the encoding work was built in. Both sides of every comparison must
# use the SAME executable, so this is set once, here.
EXE = os.environ.get("ERHE_SHOT_EXE") or os.path.join(REPO, r"build_vs2026_vulkan\bin\Debug\editor.exe")

os.environ["ERHE_SHOT_EXE"] = EXE
os.environ.setdefault("ERHE_SHOT_SCENE", "res/editor/assets/ABeautifulGame.glb")

sys.path.insert(0, os.path.join(REPO, "scripts"))
import mesh_ab_capture as cap  # noqa: E402  (env above has to be set first)

CFG_MESH_MEMORY = os.path.join(REPO, "config", "editor", "mesh_memory.json")

# The ImGui panels surround the viewport and do not participate in the
# comparison; they are pixel-identical in every run, so including them only
# dilutes the percentages. At 2304x1200 with the pinned layout the 3D viewport
# starts near x=700 (there are TWO panel columns on the left, not one).
CROP_LEFT = 700
CROP_TOP = 75

CAPTURES = [
    # name,      optimize_meshes, quantize_vertex_positions
    ("off_a",    False, False),
    ("off_b",    False, False),
    ("on_qoff",  True,  False),
    ("on_qon",   True,  True),
]

COMPARISONS = [
    ("control",   "off_a",   "off_b",  "noise floor - read everything else against this"),
    ("attributes", "off_a",  "on_qoff", "TBN quaternion + unorm16 UVs + unorm8 color + implicit-sum weights"),
    ("all",       "off_a",   "on_qon",  "the whole optimized variant"),
    ("positions", "on_qoff", "on_qon",  "position quantization alone (already accepted)"),
]


def capture(name, optimize, quantize, buffer_report):
    """One editor launch. Mirrors mesh_ab_capture.__main__, plus a buffer dump."""
    png_rel = os.path.join(OUT_DIR_REL, name + ".png").replace("\\", "/")
    target = os.path.join(REPO, png_rel)
    if os.path.exists(target):
        os.remove(target)

    cap.set_flag("optimize_meshes", optimize)
    cap.set_flag("mesh_optimize_cache", False)
    cap.set_flag("quantize_vertex_positions", quantize)

    # The editor rewrites its window layout on exit and a different layout moves
    # the viewport - a ~96% difference that is not a rendering difference. Pin it.
    live = os.path.join(REPO, "config", "editor", "desktop_windows.json")
    layout = os.path.join(REPO, "cache", "desktop_windows.pinned.json")
    if os.path.exists(layout):
        shutil.copyfile(layout, live)
    elif os.path.exists(live):
        os.makedirs(os.path.dirname(layout), exist_ok=True)
        shutil.copyfile(live, layout)

    # A leftover editor keeps port 3743; the new one then fails to bind and every
    # RPC silently drives the OLD process with the OLD config.
    try:
        import urllib.request
        with urllib.request.urlopen(cap.BASE + "/health", timeout=2):
            raise SystemExit("an editor is already serving %s - kill it first" % cap.BASE)
    except SystemExit:
        raise
    except Exception:
        pass

    print("=== capture %s (optimize=%s quantize=%s)" % (name, optimize, quantize))
    proc = subprocess.Popen([EXE, "--scene", os.environ["ERHE_SHOT_SCENE"]], cwd=REPO)
    try:
        if not cap.wait_health(time.time() + 150):
            raise SystemExit("MCP never came up")
        time.sleep(30)
        act = cap.rpc("get_active_scene", {})
        scene = act.get("active_scene") or "ABeautifulGame.glb"
        cap.set_exposure(scene)
        cap.frame_scene(scene)
        cap.rpc("set_ddgi", {"enabled": False})
        cap.rpc("advance_time", {"mode": "paused"})
        time.sleep(6)
        print("capture:", cap.rpc("capture_screenshot", {"path": png_rel}))
        buffer_report.append((name, describe_buffers(scene)))
    finally:
        proc.kill()
        proc.wait()
    return target


def describe_buffers(scene):
    """What the GPU vertex buffers actually hold, per variant.

    A passing control pair is necessary and not sufficient: it cannot tell you
    the optimized variant was built and selected at all. This can. Reports the
    per-stream strides and attribute formats of the first few nodes that have a
    buffer mesh at all.
    """
    lines = []
    nodes = cap.rpc("get_scene_nodes", {"scene_name": scene})
    names = nodes.get("nodes", nodes) if isinstance(nodes, dict) else nodes
    names = [n if isinstance(n, str) else n.get("name") for n in (names or [])]
    reported = 0
    for node_name in [n for n in names if n]:
        if reported >= 3:
            break
        rows = []
        for variant in ("original", "optimized"):
            info = cap.rpc("get_mesh_buffer_info", {
                "scene_name": scene, "node_name": node_name, "variant": variant
            })
            if not isinstance(info, dict) or "vertex_streams" not in info:
                rows.append("  %-26s %-10s %s" % (node_name, variant, str(info)[:120]))
                continue
            rows.append("  %-26s %-10s position_encoding %s"
                        % (node_name, variant, info.get("position_encoding")))
            for stream in info["vertex_streams"]:
                formats = ",".join(
                    "%s%s=%s" % (a.get("usage"), a.get("usage_index"), a.get("format"))
                    for a in stream.get("attributes", [])
                )
                rows.append("  %-26s %-10s stream %d stride %-3s %s"
                            % (node_name, variant, stream.get("stream", -1), stream.get("stride"), formats))
        # Only nodes that actually resolved to a mesh are worth printing.
        if any("stride" in row for row in rows):
            lines += rows
            reported += 1
    if not lines:
        lines.append("  (no node resolved to a buffer mesh - check get_mesh_buffer_info by hand)")
    return lines


def load_cropped(path):
    from PIL import Image
    import numpy as np
    image = Image.open(path).convert("RGB")
    image = image.crop((CROP_LEFT, CROP_TOP, image.width, image.height))
    return image, np.asarray(image).astype("int16")


def compare(name, left_name, right_name, note, summary, results):
    from PIL import Image
    import numpy as np

    left_image, left = load_cropped(os.path.join(OUT_DIR, left_name + ".png"))
    right_image, right = load_cropped(os.path.join(OUT_DIR, right_name + ".png"))
    if left.shape != right.shape:
        summary.append("%-11s SHAPE MISMATCH %s vs %s" % (name, left.shape, right.shape))
        return

    difference = np.abs(left - right)
    differing = np.any(difference > 0, axis=2)
    percent = 100.0 * float(differing.sum()) / float(differing.size)
    channel_difference = difference[difference > 0]
    worst = int(channel_difference.max()) if channel_difference.size else 0
    within_4 = (100.0 * float((channel_difference <= 4).sum()) / float(channel_difference.size)) if channel_difference.size else 100.0

    # Saturation check: a blown-out viewport compares equal to another blown-out
    # viewport, and that meaningless 0 reads exactly like a passing A/B.
    saturated = 100.0 * float(np.all(left >= 254, axis=2).sum()) / float(differing.size)

    side_by_side = Image.new("RGB", (left_image.width * 2, left_image.height))
    side_by_side.paste(left_image, (0, 0))
    side_by_side.paste(right_image, (left_image.width, 0))
    side_by_side.save(os.path.join(OUT_DIR, "cmp_%s_side_by_side.png" % name))

    # x8, so a 1-LSB difference is actually visible to a human.
    Image.fromarray(np.clip(difference * 8, 0, 255).astype("uint8")).save(
        os.path.join(OUT_DIR, "cmp_%s_diff_x8.png" % name)
    )

    results[name] = percent
    summary.append(
        "%-11s %-9s vs %-9s  differing %6.3f%%  worst %3d LSB  <=4 LSB %5.1f%%  saturated %5.1f%%   %s"
        % (name, left_name, right_name, percent, worst, within_4, saturated, note)
    )


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    buffer_report = []
    for name, optimize, quantize in CAPTURES:
        capture(name, optimize, quantize, buffer_report)

    summary = []
    results = {}
    for name, left, right, note in COMPARISONS:
        compare(name, left, right, note, summary, results)

    text = ["Optimized-variant attribute encodings, A/B on ABeautifulGame", ""]
    text.append("executable: %s" % EXE)
    text.append("")
    text += summary
    text.append("")
    # A non-zero control means the two identical runs did not converge - a shot
    # taken before the scene settled, most often - and every other row in the
    # batch is then measuring that too. It happens; just run the script again.
    if results.get("control", 0.0) > 0.0:
        text.append("*** BATCH INVALID: the control pair differs (%.3f%%). Two identical runs" % results["control"])
        text.append("*** must render identically. Re-run before reading anything below.")
    else:
        text.append("Control pair is identical: this batch is valid.")
    text.append("")
    text.append("Read every percentage against the `control` row, not against zero.")
    text.append("The `attributes` and `all` rows are EXPECTED to be non-zero: the")
    text.append("encodings are lossy by design, and a non-zero there is also the")
    text.append("proof the optimized variant is what is being rendered.")
    text.append("")
    text.append("GPU vertex buffers per variant (the optimized stride must be the")
    text.append("smaller one, or the images above compare two float builds):")
    for name, lines in buffer_report:
        text.append("  [%s]" % name)
        text += lines
    body = "\n".join(text)
    open(os.path.join(OUT_DIR, "summary.txt"), "w", encoding="utf-8", newline="\n").write(body + "\n")
    print()
    print(body)
    print()
    print("images in", OUT_DIR)


SESSION_STATE = [
    os.path.join(REPO, "config", "editor", "mesh_memory.json"),
    os.path.join(REPO, "config", "editor", "desktop_windows.json"),
    os.path.join(REPO, "config", "editor", "editor_settings.json"),
]


if __name__ == "__main__":
    # The editor rewrites these on exit and this script edits mesh_memory.json
    # directly. Restore the files as they were WHEN THIS RUN STARTED - not with
    # git checkout, which would also throw away whatever the user already had
    # uncommitted in them.
    saved = {}
    for path in SESSION_STATE:
        if os.path.exists(path):
            saved[path] = open(path, "rb").read()
    try:
        main()
    finally:
        for path, content in saved.items():
            open(path, "wb").write(content)
