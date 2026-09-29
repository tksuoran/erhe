# Debug renderer: analytic anti-aliasing for wide lines

Status: proposed

Extends [erhe/renderer.md](../erhe/renderer.md) "Line widths". Item 3 of
[reference/aimd_comparison.md](../reference/aimd_comparison.md) section 5:
replace the binary edge of `Debug_renderer` wide lines (discard below 50 %
coverage, smoothness only from MSAA) with analytic coverage the way AIMD's
`AIMD.hlsl` does it (`saturate(halfWidth + 0.5 - distance)` over a ribbon
extended by a one-pixel fringe), while keeping erhe's stencil layering,
visible + hidden passes, x-ray, multiview and per-endpoint width and color
intact.

## 1. Scope

In scope: the compute tier of `Debug_renderer` (wide lines: every
`Primitive_renderer::add_line*`, `add_surface_lines` and the shape helpers
built on them, and through them `Jolt_debug_renderer`). Files:
`res/shaders/compute_before_line.comp`, `res/shaders/line_after_compute.frag`,
`src/erhe/renderer/erhe_renderer/debug_renderer.{hpp,cpp}`,
`debug_renderer_bucket.{hpp,cpp}`, `src/erhe/renderer/test/`,
`doc/erhe/renderer.md`.

Out of scope, each a possible follow-up with its own plan line:

- The direct tier (`line_simple.{vert,frag}`): thin lines are one-pixel
  hardware line primitives and filled triangles share edges with neighbours;
  neither has a ribbon to put a fringe on. A caller that wants smooth thin
  lines uses `set_thickness(-1)` (a one-pixel wide line on the compute tier).
- `erhe::scene_renderer` content wide lines
  (`compute_before_content_line.comp`, `content_line_after_compute.frag`):
  the same distance test copied; apply the same change in a separate commit
  after this plan lands.
- `Text_renderer` labels.

## 2. Coverage model

Definitions, all in framebuffer pixels: `w` is the full line width the
compute shader already derives per endpoint (`get_line_width`, doubled into
`.w`), `h = 0.5 * w` the half width, `d` the distance from the fragment
centre to the clipped segment (round caps: distance to the clamped
projection, as today).

- Geometric half width `hg = max(h, 0.5)`. The ribbon is built with
  `hg + 0.5` on each side and at each end (the fringe), so a fragment at
  `d = hg + 0.5` has coverage 0 and the ribbon contains every fragment with
  non-zero coverage.
- Coverage `c = clamp(hg + 0.5 - d, 0, 1) * min(1, 2 * h)`. For `w >= 1`
  the profile is the trapezoid of a box filter of width 1 over a bar of
  width `w`; its integral across the line is exactly `w`. For `w < 1` the
  geometry stays one pixel wide (`hg = 0.5`, integral 1) and the alpha scale
  `2 * h = w` keeps the integral at `w`: a quarter-pixel line is a
  one-pixel-wide line at 25 % intensity rather than a line that flickers in
  and out of pixel centres. This is the "fade lines thinner than a pixel"
  behaviour of AIMD made energy-conserving.
- Output `out_color = vec4(rgb * a * c, a * c)` (premultiplied, matching
  `Color_blend_state::color_blend_premultiplied` already used by the visible
  pass). The `end_weight` term and the `s - d2` clamp of the current shader
  go away; the distance computation stays.

Per-endpoint width is interpolated across the ribbon as today
(`v_line_width`); the compute shader offsets corners `a`, `d`, `e0` by
`hg0 + 0.5` and `b`, `c`, `e1` by `hg1 + 0.5`. The surface-line tent is
unaffected: corner depths are evaluated from the face planes at whatever NDC
the corner lands on, so a wider ribbon only moves the corner along the same
plane.

## 3. Stencil: core and fringe draws

The buckets write their `stencil_reference` with compare `greater`
(`debug_renderer_bucket.cpp` `make_pipeline`), which gives "first fragment
wins" inside a bucket and "higher bucket wins" across buckets. With analytic
coverage every non-discarded fragment still claims the stencil, so a fringe
fragment at 10 % coverage drawn first would block a later fully covered
fragment of the crossing or joining line and leave a dark notch. One draw per
pass cannot fix this because the claim is made by the fixed-function stencil
stage.

Each pass (visible, hidden) therefore issues two draws of the same triangle
SSBO range with the same pipeline state and two fragment shader variants:

1. **Core**: keeps fragments with `c >= 1`, discards the rest. Drawn first,
   so fully covered pixels claim the stencil before any partial pixel can.
2. **Fringe**: keeps fragments with `0 < c < 1`, discards the rest (a
   zero-coverage fragment in the ribbon's cap corners must not claim a
   pixel). Same stencil state as the core draw (compare `greater`, op
   `replace`): a fringe fragment never draws over a pixel a core fragment of
   this or a higher bucket claimed, and where two fringes coincide (round
   caps of a polyline joint, a line drawn twice) the first wins with the
   identical value instead of blending twice into a brighter edge. Where two
   fringes of different lines cross, the pixel shows the first line's
   coverage rather than the union; this is the same first-fragment rule the
   bucket already applies to overlapping translucent lines.

The two variants are one source file, `line_after_compute.frag`, with the
define `ERHE_DEBUG_LINE_FRINGE` (0 or 1) passed through
`Shader_stages_create_info::defines`, next to the existing `ERHE_MULTIVIEW`
variant. Draw order per bucket: hidden core, hidden fringe, visible core,
visible fringe. The core draw is the existing draw with `discard` at `c < 1`
instead of `alpha < 0.5`; the fringe draw is the new one.

A per-bucket draw therefore costs two fragment-shader passes over the ribbon
instead of one. The fringe draw is skipped when anti-aliasing is off (section
5), which restores today's cost with the coverage test at `c >= 0.5`.

## 4. Hidden pass

The hidden pass blends with a constant factor (`color_blend_hidden`, constant
0.1, fragment alpha ignored), which cannot scale by coverage. Change it to the
premultiplied blend of the visible pass and move the dim factor into the
shader: the view UBO gains `float hidden_dim` (0.1 for a normal bucket, 1.0
for an `xray` bucket, written where the bucket fills its view block), and a
second define `ERHE_DEBUG_LINE_HIDDEN` (0 or 1) selects whether the fragment
shader multiplies its premultiplied output by `view.hidden_dim`. The
`(visible || xray)` blend-state selection in `render()` goes away for the
compute tier; `color_blend_hidden` remains for the direct tier, which is not
changed by this plan.

Fragment shader variants: {core, fringe} x {visible, hidden} = 4 per vertex
variant (single view and multiview), built in one loop over the two defines
in `Debug_renderer_program_interface` and registered with the shader monitor
each, so hot reload keeps working.

## 5. Setting

`Debug_renderer::set_anti_aliasing(Anti_aliasing)` with
`enum class Anti_aliasing { off, on }`, default `on`. Off restores the current
behaviour: no fringe extension in the compute shader (`view.fringe`, 0.0 or
0.5, read where the corners are offset), the core discard threshold at 0.5
(`view.core_coverage_threshold`, 0.5 or 1.0), no fringe draw. The editor
reads it from `editor_settings.json` next to `line_bias_margin` under the
same object and applies it at the single change site (the settings apply
code that calls `set_line_bias_margin`; the Graphics settings UI toggles it
there too). Both values live in the view UBO so the shaders need no further
variants.

## 6. MSAA

With a multisampled target the rasterizer's per-sample coverage and the
analytic coverage multiply; edges become slightly softer than either alone,
which is the accepted result (AIMD behaves the same). `gl_FragCoord` is the
pixel centre under MSAA, so the distance test is unchanged.

## 7. Tests

Extend `src/erhe/renderer/test/test_debug_line_width_gpu.cpp` (fixture
`Debug_line_width_gpu_test`, label `gpu`). The existing width tests keep
passing unchanged: the test line sits on a pixel boundary at NDC x = 0, so a
4-pixel line covers exactly four pixels at coverage 1 and its fringe pixels
have coverage 0. New tests, all reading the middle row:

1. **Half-pixel offset.** Shift the line by half a pixel (world offset
   `0.5 * pixel_width_in_world`); expect the profile
   `[~128, 255, 255, 255, ~128]` within 2 per channel for `set_thickness(-4)`.
2. **Energy conservation.** For widths 0.25, 0.5, 1, 1.5, 2, 4, 4.5 and
   offsets 0 and 0.5 pixel, the row sum of `pixel / 255` equals the width
   within 2 % plus the 8-bit rounding of the non-zero pixels.
3. **Sub-pixel width.** `set_thickness(-0.25)` lights exactly one or two
   pixels with peak intensity `255 * 0.25` within 2.
4. **Idempotence.** The same line added twice produces the same image as
   once, pixel-exact (fringe first-fragment rule, section 3).
5. **Crossing.** Two perpendicular 4-pixel lines: every pixel inside either
   core is 255 (the core draw is never blocked by a fringe).
6. **Hidden pass.** With the depth attachment cleared to the near value the
   line is behind everything and only the hidden pass draws; the profile
   equals the visible profile scaled by 0.1 within 2, and with `xray` it
   equals the visible profile.
7. **Anti-aliasing off.** With `set_anti_aliasing(Anti_aliasing::off)` the
   half-pixel-offset 4-pixel line lights exactly four pixels at 255 and no
   pixel between 1 and 254 exists on the row (binary edge, today's
   behaviour).

Run on Vulkan and OpenGL (`ctest -L gpu -R renderer` in the test tree named
in [testing.md](../testing.md)); Metal when a macOS session is available.

## 8. Cost gate

Measure the wide-line render pass GPU time with `erhe::graphics::Gpu_timer`
around `Debug_renderer::render` in the headless editor on
`creation_21` (physics rig with Jolt debug lines) and with the Mesh
Component Selection tool showing all edges of a subdivided sphere, before and
after, anti-aliasing on. Acceptance: the pass time with anti-aliasing on is
at most 2.0x the before value (two fragment passes over the same ribbons,
each discarding about half of its fragments early in the shader); with
anti-aliasing off it equals the before value within noise. Record the
numbers in `memory-bank/local/` (per machine), not in this document.

## 9. Work order

Each step builds, runs `erhe_renderer_gpu_tests`, and is one commit.

1. **Tests first.** Add tests 1 to 5 and 7 from section 7 as `DISABLED_`
   (they fail on the binary edge) and the `Anti_aliasing` setter as a no-op
   so the test file compiles; enable each test in the step that makes it
   pass.
2. **Fringe geometry and coverage.** Compute shader: `hg`, fringe extension
   from `view.fringe`; fragment shader: coverage `c`, premultiplied output,
   discard against `view.core_coverage_threshold`. At this step there is one
   draw per pass and joints show the notch of section 3; tests 1, 2, 3 pass
   (single line), test 4 and 5 stay disabled.
3. **Core and fringe draws.** `ERHE_DEBUG_LINE_FRINGE` variants, second draw
   per pass, skipped when anti-aliasing is off. Tests 4, 5 and 7 pass.
4. **Hidden pass.** `hidden_dim` in the view UBO, `ERHE_DEBUG_LINE_HIDDEN`
   variants, premultiplied blend for the compute tier. Test 6.
5. **Setting and docs.** `editor_settings.json` key, settings UI toggle,
   `renderer.md` "Line widths" gains the coverage model and the setting,
   `CHANGELOG.md` line for the `erhe::renderer` API addition, this plan's
   remaining items (section 1 follow-ups) moved to their own lines in the
   doc index.
6. **Cost gate** (section 8), numbers to the local memory bank; if the gate
   fails, the fringe draw is restricted to a narrower ribbon (a second,
   fringe-only triangle set written by the compute shader) before this plan
   is called done.
