# erhe_renderer

Stability: stable

## Purpose
GPU rendering utilities for debug visualization and text overlay in 3D viewports. Provides a debug line/shape renderer (compute-shader wide-line expansion plus a direct vertex-buffer path for triangles and thin lines), a 2D text renderer for in-viewport labels, a texture fullscreen renderer, and draw indirect buffer management for batched mesh rendering.

## Key Types
- `Debug_renderer` -- Central coordinator for debug line/shape rendering. Manages a stack of views, dispatches compute shaders to expand lines into triangles, and renders the results.
- `Primitive_renderer` -- Immediate-mode draw API obtained from `Debug_renderer::get()`. Provides `add_lines()`, `add_cube()`, `add_sphere()`, `add_cone()`, `add_capsule()`, `add_torus()`, `add_bone()` etc.
- `Debug_renderer_bucket` -- Groups draw calls by pipeline config (primitive type, stencil, visibility). Internally manages GPU ring buffers.
- `Debug_renderer_config` -- Selects primitive type, stencil reference, visible/hidden draw flags, and x-ray mode (hidden pass blends at full strength instead of the dim constant factor) for a bucket.
- `Text_renderer` -- Renders 2D text at 3D positions using a font atlas texture. Uses `erhe::ui::Font` for glyph layout.
- `Texture_renderer` -- Simple fullscreen texture blit.
- `Draw_indirect_buffer` -- Builds GPU draw-indirect command buffers from a span of meshes filtered by `Item_filter`.
- `Jolt_debug_renderer` -- Adapter implementing Jolt's `JPH::DebugRenderer` interface, forwarding draw calls to `Debug_renderer`.
- `View` -- Camera view data (clip_from_world matrix, viewport rect, FOV sides, pixel scale).

## Public API
- `Debug_renderer::get(config)` returns a `Primitive_renderer` for a given config.
- Call `begin_frame()`, draw with `Primitive_renderer`, then `compute()` and `render()`, finally `end_frame()`.
- `Text_renderer::print(position, color, text)` queues text; `render(encoder, viewport)` draws it.
- `Draw_indirect_buffer::update(meshes, mode, filter)` fills indirect draw commands.

## Dependencies
- erhe::graphics (Device, Ring_buffer_client, Shader_stages, Pipeline, Texture)
- erhe::scene (Camera, Transform -- for sphere/cone rendering)
- erhe::primitive (Primitive_mode for draw indirect)
- erhe::ui (Font for text rendering)
- erhe::math (Viewport)
- erhe::dataformat (Vertex_format)
- erhe::verify
- Jolt Physics (optional, for Jolt_debug_renderer behind `JPH_DEBUG_RENDERER`)
- glm, etl

## Notes
- Debug rendering has two paths, selected per bucket from its config (compute shaders are required on every backend, so there is no capability fallback):
  1. **Compute shader** (wide lines): lines stored as SSBO data, expanded to triangles by compute shader, rendered as GL_TRIANGLES.
  2. **Direct** (triangles, thin lines): vertices drawn straight from the vertex buffer with the primitive's own topology.
- `Debug_renderer_config::primitive_type` is `line` (the default) or `triangle`; any other type fails an `ERHE_VERIFY` when its bucket is created. There is no point primitive.
- Buckets use `etl::vector` (fixed capacity) so that element addresses remain stable.
- `Primitive_renderer` is move-only; obtain one per frame per config.

## Line widths
`Primitive_renderer::set_thickness(t)` sets the width of the wide lines that
follow (the compute path; thin lines are one pixel):
- **Negative** `t`: a constant screen-space width of `-t` logical pixels. It is
  multiplied by `View::pixel_scale` (physical pixels per logical pixel of the
  render target: the window display scale for a desktop viewport, 1.0 for a
  headset eye) to get framebuffer pixels, and does not depend on the viewport
  size, the projection or the field of view. `set_thickness(-4)` with pixel
  scale 1.5 draws a line 6 framebuffer pixels wide.
- **Positive** `t`: a distance-scaled width, a size in the world; its pixel
  width grows with the viewport like other projected geometry.

`Debug_renderer::view_from_camera()` takes the pixel scale; the editor passes
the window's `Context_window::get_scale_factor()`. `erhe_renderer_gpu_tests`
(`src/erhe/renderer/test/`) draws lines through `Debug_renderer` into
offscreen targets and checks the exact pixel width across viewport sizes,
fields of view, orthographic projection and pixel scales, and the
anti-aliased edge profiles below.

## Line anti-aliasing

`Debug_renderer::set_anti_aliasing(Anti_aliasing)` selects the edge treatment
of the wide lines (default `on`; the editor exposes it as "Anti-aliased
Lines" in the Debug Visualizations Style section of the Settings window and
applies it at startup and on the edit).

- **On**: analytic coverage. In framebuffer pixels, with `h` the half width
  and `d` the distance of the fragment centre from the segment (round caps),
  the geometric half width is `hg = max(h, 0.5)`, the compute shader builds
  the ribbon `hg + 0.5` wide on each side and at each end (a one-pixel
  fringe), and the fragment shader outputs premultiplied color scaled by
  `coverage = clamp(hg + 0.5 - d, 0, 1) * min(1, 2h)`. The coverage
  integrated across the line equals the width for every width: a line
  thinner than a pixel keeps a one-pixel footprint and fades by alpha
  instead of flickering between pixel centres. Zero-coverage fragments are
  discarded. The hidden pass draws the same coverage at the bucket's dim
  factor (0.1, 1.0 for x-ray) from its own fragment shader variant
  (`ERHE_DEBUG_LINE_HIDDEN`), blending premultiplied like the visible pass.
- **Off**: the ribbon is exactly the line width and its rasterized edge is
  the line edge (binary, smooth only under MSAA); the round caps are cut by
  the distance test.

Inside a bucket the stencil compare is `greater_or_equal`, so the last
fragment wins: a fully covered fragment overwrites a fringe fragment drawn
earlier at a joint or crossing, and a fringe over a fully covered pixel of
the same color leaves it unchanged. Where two fringes coincide they blend
twice: the overlapping round caps at a polyline joint, or a line drawn
twice, show a fringe pixel at 0.75 instead of 0.5 coverage. Translucent
lines of one bucket blend where they overlap (the selection minor lines and
the shadow-fit visualizations use alpha below 1), instead of the earlier
first-fragment rule. Across buckets a higher stencil reference still wins
regardless of draw order. The direct tier (thin one-pixel lines, filled
triangles) is unchanged.

## Future work

- [plans/debug_renderer_anti_aliasing.md](../plans/debug_renderer_anti_aliasing.md) - cost gate of the anti-aliased wide lines, the fringe double-blend at joints, content wide lines.
