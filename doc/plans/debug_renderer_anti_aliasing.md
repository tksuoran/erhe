# Debug renderer: analytic anti-aliasing for wide lines

Status: in progress

Extends [erhe/renderer.md](../erhe/renderer.md) "Line anti-aliasing". Item 3
of [reference/aimd_comparison.md](../reference/aimd_comparison.md) section
5: the analytic coverage of AIMD's `AIMD.hlsl`
(`saturate(halfWidth + 0.5 - distance)` over a ribbon extended by a
one-pixel fringe) on erhe's wide lines, keeping the stencil layering, the
visible + hidden passes, x-ray, multiview and per-endpoint width and color.
The coverage model, the core and fringe draws, the hidden-pass variant, the
`Anti_aliasing` setting and the GPU tests are built and described in
`renderer.md`; this plan holds what is left.

## 1. Cost

Measured by `erhe_renderer_gpu_tests --gtest_filter=*aa_cost*`
(`aa_cost_benchmark`: 2000 random wide lines of 1, 2 and 4 pixels at
1920 x 1080, visible + hidden pass, render-pass `Gpu_timer`, median of 20
frames, logged to `logs/log.txt`). Off is the previous binary path bit-exact
(`aa_off_is_binary`); on costs about 1.7x off on the first machine measured
(Vulkan and OpenGL, integrated GPU), accepted for the visual result: the
one-draw variant (1.17x) blended overlapping fringes twice and showed it on
every polyline joint. Numbers live in `memory-bank/local/` per machine.
Re-run it after any change to the wide-line shaders.

## 2. Possible refinement: joined polylines

A polyline primitive whose segments meet at the angle bisector instead of
overlapping round caps (neighbour points in the compute shader, miter
clamped to a bevel at sharp angles) would let the shape helpers (sphere,
circle, cone, capsule, torus) draw their outlines without overlap, which
improves joint shape and lets those outlines skip the fringe draw's overlap
handling. Independent segments meeting at a point (box corners, Jolt, any
`add_lines` caller) still rely on the two draws.

## 3. Follow-ups

- `erhe::scene_renderer` content wide lines
  (`compute_before_content_line.comp`, `content_line_after_compute.frag`):
  the same distance test copied; apply the same coverage in a separate
  commit.
- The direct tier (`line_simple.{vert,frag}`): thin lines are one-pixel
  hardware line primitives and filled triangles share edges with neighbours;
  neither has a ribbon to put a fringe on. A caller that wants smooth thin
  lines uses `set_thickness(-1)` (a one-pixel wide line on the compute tier).
- Metal: run `erhe_renderer_gpu_tests` on a macOS session.
