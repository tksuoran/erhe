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

Measure the wide-line pass GPU time with `erhe::graphics::Gpu_timer`, with
anti-aliasing on and off, on a line-heavy frame. Acceptance: on is at most
1.2x off (the ribbon is one pixel wider, one draw per pass); off equals the
previous binary path within noise. Record the numbers in
`memory-bank/local/` (per machine), not in this document.

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
