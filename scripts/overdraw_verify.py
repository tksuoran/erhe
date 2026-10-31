"""Numeric verification of the viewport overdraw counting
(doc/plans/overdraw_visualization.md "Verification").

Drives a running editor over MCP (doc/agents/editor_runs.md): creates a new
scene, points its camera down -Z and stacks N full-screen rectangles facing
it, then reads get_overdraw_stats with the overdraw visualization in both
counting modes, for each material blending mode and on the bucket path and
the draw-list path (culling classification cpu and gpu). Every pixel of the
viewport is covered by every rectangle, so the histogram has one bin. Mode
shaded:

- alpha_test: N per pixel in any draw order (a discarding variant keeps
  the late depth test and counts every rasterized fragment);
- opaque at N distinct depths: 1 per pixel created near first, N created
  far first (early depth test; the passes draw in creation order);
- alpha_blend at N distinct depths: between 1 and N per pixel, the same
  count everywhere (the translucent passes depth-test and write depth like
  the opaque ones).

Mode rasterized: N per pixel in every case (every rasterized fragment
counts, whatever the depth test does).

These run with occlusion culling off. With it on (draw lists, gpu
classification), the rectangles behind the nearest one are culled, so the
distinct-depth opaque stacks count 1 per pixel in both modes, the rasterized
count included: what occlusion culling removes.
- opaque coplanar: 1 per pixel (only the first draw passes the depth test).

Then turns the mode off and checks the statistics are released.

Usage: python3 scripts/overdraw_verify.py [--count N]
Exit code 0 = pass.
"""

import argparse
import sys
import time

from erhe_mcp import McpClient, check_true, report
from mcp_viewport_isolation import isolate_viewport


def wait_for_stats(client, viewport, min_frame_index, timeout_s=20.0):
    deadline = time.time() + timeout_s
    stats = {}
    while time.time() < deadline:
        stats = client.call("get_overdraw_stats", {"viewport": viewport})
        if stats.get("valid") and (stats.get("frame_index", 0) > min_frame_index) and (stats.get("counted_mode") == stats.get("mode")):
            return stats
        time.sleep(0.2)
    raise RuntimeError(f"no overdraw statistics newer than frame {min_frame_index}: {stats}")


def settled_stats(client, viewport):
    """Statistics of a frame that counted the current scene state: waits for
    two retired frames after the call (edits reach the frame prepared after
    them)."""
    first = wait_for_stats(client, viewport, 0)
    second = wait_for_stats(client, viewport, first["frame_index"] + 4)
    return wait_for_stats(client, viewport, second["frame_index"])


def single_bin(stats):
    """The count every pixel has, or None when the counts differ. With MSAA
    the pixels on the shared diagonal of a rectangle's two triangles run
    the shader once per triangle, so up to one row's worth of pixels (the
    diagonal crosses each row once) may count more."""
    histogram = stats["histogram"]
    total     = stats["width"] * stats["height"]
    count     = max(range(len(histogram)), key=lambda c: histogram[c])
    if (total - histogram[count]) > stats["height"]:
        return None
    return count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=4, help="rectangles per stack")
    args = parser.parse_args()
    n = args.count

    client = McpClient()
    before = {s["name"] for s in client.call("list_scenes")["scenes"]}
    client.call("create_scene")
    scene = None
    deadline = time.time() + 10.0
    while (scene is None) and (time.time() < deadline):
        time.sleep(0.2)
        added = [s["name"] for s in client.call("list_scenes")["scenes"] if s["name"] not in before]
        scene = added[0] if added else None
    check_true("scene created", scene is not None)
    if scene is None:
        return report()

    target = isolate_viewport(client.call, scene_name=scene)
    viewport = target["title"]

    # The camera at z = 10 looking down -Z (identity rotation).
    client.call("set_node_transform", {"scene_name": scene, "node_name": "Camera", "translation": [0.0, 0.0, 10.0], "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]})

    materials = {
        "opaque":      {"blending_mode": "opaque"},
        "alpha_test":  {"blending_mode": "alpha_test", "alpha_cutoff": 0.5, "opacity": 1.0},
        "alpha_blend": {"blending_mode": "alpha_blend", "opacity": 0.5},
    }
    for name, fields in materials.items():
        client.call("create_material", {"scene_name": scene, "name": f"overdraw_{name}", "base_color": [0.5, 0.5, 0.5], "bxdf_model": "unlit", **fields})

    def make_stack(material, depths):
        names = []
        for i, z in enumerate(depths):
            node_name = f"overdraw_{material}_{i}"
            client.call("create_shape", {
                "scene_name":    scene,
                "shape":         "rectangle",
                "name":          node_name,
                "front_face":    True,
                "back_face":     False,
                "material_name": f"overdraw_{material}",
                "motion_mode":   "none",
                "position":      [0.0, 0.0, z],
                "scale":         [200.0, 200.0, 1.0]
            })
            names.append(node_name)
        return names

    def remove(names):
        client.call("delete_nodes", {"scene_name": scene, "names": names})

    configurations = [
        ("bucket",          {"draw_lists": False, "classification": "cpu", "occlusion": False}),
        ("draw lists cpu",  {"draw_lists": True,  "classification": "cpu", "occlusion": False}),
        ("draw lists gpu",  {"draw_lists": True,  "classification": "gpu", "occlusion": False}),
    ]
    occlusion_configuration = ("occlusion", {"draw_lists": True, "classification": "gpu", "occlusion": True})

    distinct = [-float(i) for i in range(n)]
    far_first = list(reversed(distinct))
    coplanar = [0.0] * n
    any_order = lambda c: 1 <= c <= n
    exactly_n = lambda c: c == n
    exactly_1 = lambda c: c == 1
    # (name, material, depths, {mode: (accept, expected)}, occlusion culled)
    cases = [
        ("alpha_test distinct",  "alpha_test",  distinct,  {"shaded": (exactly_n, f"== {n}"),      "rasterized": (exactly_n, f"== {n}")}, False),
        ("alpha_blend distinct", "alpha_blend", distinct,  {"shaded": (any_order, f"in 1..{n}"),   "rasterized": (exactly_n, f"== {n}")}, False),
        ("opaque near first",    "opaque",      distinct,  {"shaded": (exactly_1, "== 1"),         "rasterized": (exactly_n, f"== {n}")}, True),
        ("opaque far first",     "opaque",      far_first, {"shaded": (exactly_n, f"== {n}"),      "rasterized": (exactly_n, f"== {n}")}, True),
        ("opaque coplanar",      "opaque",      coplanar,  {"shaded": (exactly_1, "== 1"),         "rasterized": (exactly_n, f"== {n}")}, False),
    ]
    for case_name, material, depths, expectations, occlusion_culled in cases:
        names = make_stack(material, depths)
        for mode, (accept, expected) in expectations.items():
            result = client.call("set_overdraw_visualization", {"viewport": viewport, "mode": mode})
            check_true(f"set_overdraw_visualization {mode}", result.get("mode") == mode, str(result))
            runs = [(configuration_name, configuration, accept, expected) for configuration_name, configuration in configurations]
            if occlusion_culled:
                runs.append((occlusion_configuration[0], occlusion_configuration[1], exactly_1, "== 1"))
            for configuration_name, configuration, accept, expected in runs:
                client.call("set_draw_lists_enabled", {"enabled": configuration["draw_lists"]})
                client.call("set_culling_classification", {"mode": configuration["classification"]})
                client.call("set_occlusion_culling", {"enabled": configuration["occlusion"]})
                stats = settled_stats(client, viewport)
                count = single_bin(stats)
                check_true(
                    f"{case_name} / {mode} / {configuration_name}: every pixel counts {expected}",
                    (count is not None) and accept(count),
                    f"count {count}, max {stats['max_count']}, covered {stats['covered_pixels']} of {stats['width']}x{stats['height']}, "
                    f"histogram head {stats['histogram'][:n + 2]}"
                )
                print(f"{case_name:22} {mode:10} {configuration_name:16} count {count} invocations {stats['invocations_total']}")
        remove(names)

    client.call("set_overdraw_visualization", {"viewport": viewport, "mode": "off"})
    time.sleep(1.0)
    stats = client.call("get_overdraw_stats", {"viewport": viewport})
    check_true("mode off releases the statistics", (stats.get("mode") == "off") and (stats.get("valid") is False), str(stats))

    client.call("set_draw_lists_enabled", {"enabled": True})
    return report()


if __name__ == "__main__":
    sys.exit(main())
