"""Verification of the content draws switch (set_content_draws,
src/editor/renderers/content_draws.hpp).

Drives a running editor over MCP (doc/agents/editor_runs.md): creates a new
scene, points its camera down -Z and stacks three full-screen rectangles
facing it, one opaque (farthest), one alpha_test and one alpha_blend, then
reads the viewport's rasterized overdraw count (set_overdraw_visualization,
get_overdraw_stats), which counts every rasterized fragment whatever the
depth test does. Content draws all: 3 per pixel. Content draws opaque_only:
1 per pixel (the alpha-tested and translucent passes draw nothing). Checked
on the bucket path and the draw-list path (culling classification cpu and
gpu), occlusion culling off.

Usage: python3 scripts/content_draws_verify.py
Exit code 0 = pass.
"""

import sys
import time

from erhe_mcp import McpClient, check_true, report
from mcp_viewport_isolation import isolate_viewport
from overdraw_verify import settled_stats, single_bin


def main() -> int:
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

    client.call("set_node_transform", {"scene_name": scene, "node_name": "Camera", "translation": [0.0, 0.0, 10.0], "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]})

    # Farthest first: opaque at z = -2, alpha_test at -1, alpha_blend at 0.
    layers = [
        ("opaque",      {"blending_mode": "opaque"},                                      -2.0),
        ("alpha_test",  {"blending_mode": "alpha_test", "alpha_cutoff": 0.5, "opacity": 1.0}, -1.0),
        ("alpha_blend", {"blending_mode": "alpha_blend", "opacity": 0.5},                   0.0),
    ]
    for name, fields, z in layers:
        client.call("create_material", {"scene_name": scene, "name": f"content_draws_{name}", "base_color": [0.5, 0.5, 0.5], "bxdf_model": "unlit", **fields})
        client.call("create_shape", {
            "scene_name":    scene,
            "shape":         "rectangle",
            "name":          f"content_draws_{name}",
            "front_face":    True,
            "back_face":     False,
            "material_name": f"content_draws_{name}",
            "motion_mode":   "none",
            "position":      [0.0, 0.0, z],
            "scale":         [200.0, 200.0, 1.0]
        })

    result = client.call("set_overdraw_visualization", {"viewport": viewport, "mode": "rasterized"})
    check_true("set_overdraw_visualization rasterized", result.get("mode") == "rasterized", str(result))

    configurations = [
        ("bucket",         {"draw_lists": False, "classification": "cpu"}),
        ("draw lists cpu", {"draw_lists": True,  "classification": "cpu"}),
        ("draw lists gpu", {"draw_lists": True,  "classification": "gpu"}),
    ]
    client.call("set_occlusion_culling", {"enabled": False})
    for mode, expected in (("all", 3), ("opaque_only", 1), ("all", 3)):
        reply = client.call("set_content_draws", {"mode": mode})
        check_true(f"set_content_draws {mode}", reply.get("mode") == mode, str(reply))
        for configuration_name, configuration in configurations:
            client.call("set_draw_lists_enabled", {"enabled": configuration["draw_lists"]})
            client.call("set_culling_classification", {"mode": configuration["classification"]})
            stats = settled_stats(client, viewport)
            count = single_bin(stats)
            check_true(
                f"content draws {mode} / {configuration_name}: every pixel counts {expected}",
                count == expected,
                f"count {count}, histogram head {stats['histogram'][:5]}"
            )
            print(f"{mode:12} {configuration_name:16} count {count} invocations {stats['invocations_total']}")

    check_true("set_content_draws without mode reads the setting", client.call("set_content_draws", {}).get("mode") == "all")
    client.call("set_overdraw_visualization", {"viewport": viewport, "mode": "off"})
    client.call("set_draw_lists_enabled", {"enabled": True})
    return report()


if __name__ == "__main__":
    sys.exit(main())
