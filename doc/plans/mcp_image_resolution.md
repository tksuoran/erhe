# MCP image tools: requested resolution

Status: proposed

This plan brings the editor's image-producing MCP tools to the rule in
`doc/agents/mcp_api_guidelines.md` "Image tools: rendering requests state
the resolution, captures do not": a tool that renders an image requires
`width` and `height` and renders at exactly that size into resources it
owns for the request; a tool that captures an existing image writes it at
its own size and reports that size. No tool scales an image.

## Tools and their changes

| Tool | Kind | Now | Change |
|---|---|---|---|
| `render_scene_image` | render | `width` / `height` optional (1280 x 720), request-owned target | `width` / `height` required |
| `texture_graph_export_png`, `texture_graph_export_material` | render | `size` optional (256), square | `width` / `height` required, replacing `size` |
| `render_scene_image` `output: "overdraw"` | render | - | new, `doc/plans/overdraw_visualization.md` O6 |
| lightmap debug PNGs (`lightmap_bake_gbuffer` `debug_png_base`, `lightmap_bake_direct` and `lightmap_set_baking` `debug_png`) | capture | the atlas at its size | the reply reports `width` / `height` |

## R1. Request-owned render targets, reused by size

A rendering tool allocates its color (and depth) target for the request,
sized to `width` x `height`, as `render_scene_image` does now
(`doc/editor/rendergraph.md` "Scene image capture"). Repeated requests at
one size reuse a small cache of targets keyed by size and format (one entry
per size seen, released after it goes unused for a number of frames), so a
script that renders a hundred images at one size allocates once and no
request resizes anything the editor shows.

## Build order

1. The rule in `doc/agents/mcp_api_guidelines.md` (done with this plan).
2. `render_scene_image` and the texture graph exports: required arguments,
   schema and descriptions, and every caller (the scripts under `scripts/`
   and `scripts/creations/` that call them, `mcp_server_tests.cpp`).
3. The render target cache (R1).
4. The lightmap debug PNG tools report the size they wrote
   (`capture_screenshot` and `set_ray_trace` `save_path` already do).

## Verification

- Each rendering tool refuses a request without `width` / `height`, and
  one outside its range, naming the argument; the written image has the
  requested size, also after the editor window is resized between two
  requests.
- No viewport render target, post-processing pyramid or swapchain is
  recreated by 100 consecutive `render_scene_image` requests (their
  serials stay unchanged).
- The `render_scene_image` gates (`memory-bank/local/render_thread_gates*`,
  `gi_verify.py`, `shadow_verify.py`) give identical images with the sizes
  they used as defaults now passed explicitly.
- Each capture tool's reported size equals the PNG's size.
