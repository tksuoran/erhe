# Debug renderer: analytic anti-aliasing for wide lines

Status: in progress

Extends [erhe/renderer.md](../erhe/renderer.md) "Line anti-aliasing". Item 3
of [reference/aimd_comparison.md](../reference/aimd_comparison.md) section
5: the analytic coverage of AIMD's `AIMD.hlsl`
(`saturate(halfWidth + 0.5 - distance)` over a ribbon extended by a
one-pixel fringe) on erhe's wide lines, keeping the stencil layering, the
visible + hidden passes, x-ray, multiview and per-endpoint width and color.
The coverage model, the hidden-pass variant, the `Anti_aliasing` setting and
the GPU tests are built and described in `renderer.md`; this plan holds what
is left.

## 1. Cost gate

Measured by `erhe_renderer_gpu_tests --gtest_filter=*aa_cost*`
(`aa_cost_benchmark`: 2000 random wide lines of 1, 2 and 4 pixels at
1920 x 1080, visible + hidden pass, render-pass `Gpu_timer`, median of 20
frames, logged to `logs/log.txt`). Acceptance: on is at most 1.2x off (the
ribbon is one pixel wider, one draw per pass); off is the previous binary
path bit-exact (`aa_off_is_binary`). Passed on the first machine measured
(Vulkan, integrated GPU); the numbers live in `memory-bank/local/` per
machine. Re-run it after any change to the wide-line shaders.

## 2. Known trade: fringe double-blend at joints

Inside a bucket the last fragment wins (`greater_or_equal`), so a fringe
drawn over a fringe of the same line blends twice: the overlapping round caps
of a polyline joint show their edge pixels at 0.75 instead of 0.5 coverage
over a stretch of about the line width, and a line drawn twice has brighter
edges (`aa_opaque_line_twice_brightens_fringe_only` in the GPU tests). AIMD
has the same property. The exact alternative is two draws per pass over the
same triangle range (a core draw keeping fragments with coverage 1 under
`greater_or_equal`, then a fringe draw keeping partial fragments under
`greater`, so the first fringe wins and never draws over a core), at about
twice the fragment work of the debug line pass. Decide from the cost gate
numbers and the visual result on thin polylines (sphere and circle
outlines).

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
