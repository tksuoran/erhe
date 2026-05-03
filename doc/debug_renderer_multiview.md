# Debug_renderer multiview port (deferred)

## Status

Deferred. As of the editor's current Quest bring-up, `Debug_renderer`
(`src/erhe/renderer/erhe_renderer/debug_renderer.{hpp,cpp}` plus
`debug_renderer_bucket.{hpp,cpp}`) only renders correctly into a
single-view render pass. On Quest the headset path uses a multiview
render pass (`view_mask != 0`), so dropping `Debug_renderer::render()`
into `Headset_view::multiview_callback` would trip
`VUID-vkCmdDraw-renderPass-02684` (pipeline / render-pass viewMask
incompatibility). The only place `Debug_renderer` runs today on the
headset is the per-eye fallback at `headset_view.cpp:746-795`, which
Quest never reaches because `Xr_session::is_multiview_enabled()`
returns true on Vulkan.

This document captures the shape of the work to make Debug_renderer
multiview-aware so debug lines appear in both eyes on Quest.

## Goal

`Debug_renderer` participates in `Headset_view::multiview_callback`
(`src/editor/xr/headset_view.cpp:485-655`) the same way
`Content_wide_line_renderer` already does: one compute dispatch writes
per-view triangle slabs into a shared SSBO, one draw inside the
multiview render pass produces correct stereo output by indexing the
slab via `gl_ViewIndex`. Single-view callers
(`viewport_scene_view.cpp:156`, `headset_view.cpp:746` per-eye path)
keep working unchanged.

## Reference

Mirror `erhe::scene_renderer::Content_wide_line_renderer` step-by-step
-- it has already walked this path. Key files to study before starting:

- `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.hpp:54-82`
  -- the public surface (`max_view_count` ctor arg, single-view vs
  multiview `compute()` overloads, `bool multiview` flag on `render()`,
  optional `multiview_graphics_shader_stages` slot).
- `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.cpp:111-209`
  -- the View UBO layout (`ViewCamera cameras[max_view_count]` plus
  `view_count`, `stride_per_view`, `vp_y_sign`, ...) and the two
  bind-group layouts (compute-time `m_bind_group_layout`; multiview
  graphics-time `m_multiview_graphics_bind_group_layout` which
  intentionally omits the line input SSBO and the program-interface
  camera UBO -- see the comment at lines 184-195 for *why*).
- `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.cpp:323-528`
  -- `compute()` body: how cameras[] gets padded to `max_view_count`
  even on single-view, how `view_count` is set to 1 vs N at runtime,
  how the triangle SSBO is sized to `view_count_runtime` slabs.
- `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.cpp:549-603`
  -- `render(bool multiview, ...)`: how the multiview branch bypasses
  `Lazy_render_pipeline` and constructs a temp pipeline with
  `vertex_input = nullptr` plus `multiview_graphics_shader_stages`.
- `res/shaders/compute_before_content_line.comp` -- multiview-aware
  compute that loops over `view.view_count` and indexes
  `view.cameras[view_index]`.
- `res/shaders/content_line_after_compute.{vert,frag}` -- multiview
  vertex shader that reads the triangle SSBO via
  `gl_VertexID + gl_ViewIndex * view.stride_per_view`, and fragment
  shader that subtracts `view.cameras[c_view_index].viewport.xy` from
  `gl_FragCoord` for per-eye line-distance math.
- `res/shaders/erhe_camera_view.glsl` -- the `c_view_index` macro
  (`gl_ViewIndex` under `ERHE_MULTIVIEW`, else `0u`). Lets a single
  shader source serve both pipelines.

## Existing scaffolding

`res/shaders/line_after_compute.vert` already has an
`#ifdef ERHE_MULTIVIEW` branch (lines 5-30) authored against the
target UBO shape: it indexes
`triangle_vertex_buffer.vertices[gl_VertexID + gl_ViewIndex * view.stride_per_view]`
and reads `position_0 / color_0 / custom_0`. The non-multiview branch
(lines 32-38) consumes input-assembler attributes the way the current
single-view pipeline does. That branch is dormant -- the C++ side
neither defines `ERHE_MULTIVIEW`, declares `view.stride_per_view`,
binds the triangle SSBO read-only into the vertex stage, nor builds a
multiview pipeline -- but the fact that the GLSL is already there
trims part of Phase 2's shader work.

Nothing analogous exists yet for `compute_before_line.comp` or
`line_after_compute.frag` -- they need their multiview branches added.

## Phased plan

### Phase 1 -- migrate the View UBO to a `cameras[]` layout

Goal: refactor `Debug_renderer_program_interface::view_block` from the
flat `(clip_from_world, viewport, fov, vp_y_sign)` into the multiview
shape, and migrate the existing single-view shaders to read
`view.cameras[c_view_index]`. Single-view = `view_count = 1`,
`cameras[0]` populated, `c_view_index = 0u` via the macro. **No new
public API and no new pipeline yet** -- the only externally observable
change is that the desktop OpenGL viewport debug lines should look
identical after Phase 1.

Files:

- `src/erhe/renderer/erhe_renderer/view.hpp`
  -- extend `View` with `glm::vec4 view_position_in_world` (xyz used,
  w padding). Default-init to 0; single-view callers ignore it,
  multiview callers populate it from `Render_view::view_pose.position`.
- `src/erhe/renderer/erhe_renderer/debug_renderer.{hpp,cpp}`
  -- add `int max_view_count` ctor parameter (default 1, mirror
  `Content_wide_line_renderer`'s constructor). Replace the four
  top-level `view_block->add_*` calls with `add_struct("cameras",
  view_camera_struct.get(), max_view_count)` plus per-dispatch fields
  (`view_count`, `stride_per_view`, `vp_y_sign`, padding). Add a new
  `view_camera_struct` Shader_resource (clip_from_world, viewport,
  fov, view_position_in_world). Record per-camera and per-block byte
  offsets so the bucket can write them.
- `src/erhe/renderer/erhe_renderer/debug_renderer_bucket.cpp:161-191`
  -- `update_view_buffer()` writes one `cameras[0]` entry from the
  passed `View`, sets `view_count = 1`, leaves the rest of cameras[]
  zero (must still cover the full byte range so MoltenVK descriptor
  validation holds). The multi-eye case is added in Phase 2.
- `res/shaders/compute_before_line.comp:6-14, 116, 154`
  -- read `view.cameras[c_view_index].clip_from_world` instead of
  `view.clip_from_world`; same for `viewport` (line 154, used as
  `vp_size`) and `fov` (lines 6-9, `get_line_width()`). Add
  `#include "erhe_camera_view.glsl"` at the top. `vp_y_sign` stays
  top-level on the UBO (it's a coordinate-convention scalar, not
  per-view).
- `res/shaders/line_after_compute.frag` and `line_simple.frag`
  -- single-view branch is unchanged; they don't currently read view
  data. (In Phase 2 the frag shader gains a multiview branch reading
  `view.cameras[c_view_index].viewport.xy`.)
- `res/shaders/debug_line.vert`, `debug_line.geom`, `line_simple.vert`
  -- audit any `view.clip_from_world` reads and migrate to
  `view.cameras[c_view_index].clip_from_world`. (Quick `grep -n view\\.`
  across the four files will surface them.)

Verification: `scripts/configure_vs2026_opengl.bat` build, run desktop
editor, open a scene with debug-line decorations enabled, confirm
viewport debug lines render identically to before (and that
`erhe.graphics.glsl`/`erhe.graphics.shader_monitor` log no errors).

Commit subject suggestion:
`renderer: Debug_renderer view UBO -> cameras[] layout (single-view)`.

### Phase 2 -- add the multiview pipeline variant

Goal: the multiview API exists and is invokable, but no caller uses it
yet. Single-view path keeps working.

Files:

- `Debug_renderer_program_interface`: add
  `multiview_compute_shader_stages`, `multiview_graphics_shader_stages`,
  `multiview_bind_group_layout`. The multiview graphics layout binds
  triangle SSBO (read-only at binding 1) + view UBO (binding 3); the
  compute layout stays as today. Both shader-stage slots are loaded
  with `ERHE_MULTIVIEW` injected as a define so `c_view_index =
  gl_ViewIndex` and the dormant branch in
  `line_after_compute.vert:5-30` activates. (Use the same
  `Shader_stages_create_info::defines` mechanism content_wide_line
  uses.) Also reuse the existing read-only-triangle-SSBO pattern from
  `content_wide_line_renderer.cpp:111-127` -- a second
  `Shader_resource` block at the same binding point but `readonly =
  true` so the vertex shader can declare it without colliding with the
  compute output declaration.
- `Debug_renderer`: add
  `std::optional<Compute_pipeline> m_lines_to_triangles_multiview_compute_pipeline`
  (probably equal to the single-view one in practice -- the compute
  shader source becomes view-count-aware, and `view.view_count` at
  runtime selects the loop trip count -- so this slot may not be
  needed, depending on whether the compute shader is recompiled with
  `ERHE_MULTIVIEW` or stays one program). Add multiview overload of
  `begin_frame()`:

  ```cpp
  void begin_frame(
      erhe::math::Viewport                       viewport,
      std::span<const View>                      views,
      const erhe::math::Coordinate_conventions&  conventions = {}
  );
  ```

  This pushes a multi-camera View descriptor onto an internal stack
  parallel to the existing `m_view_stack`. Two ways to model it: (a)
  promote `View` to hold `std::array<Camera_view, max_view_count>`
  plus `view_count`, or (b) keep `View` single-camera and add a sibling
  `Multiview_view` type. (a) is closer to what the UBO actually holds;
  (b) is less churn for existing callers. Pick whichever causes fewer
  changes in `viewport_scene_view.cpp` / `headset_view.cpp` per-eye.
- `Debug_renderer::render()` gains a `bool multiview` parameter (or a
  parallel `render_multiview()` method). Multiview branch picks
  `multiview_graphics_shader_stages` and the multiview bind group
  layout, and -- per
  `content_wide_line_renderer.cpp:562-577` -- bypasses
  `Lazy_render_pipeline`, building a per-call `Render_pipeline_state`
  with `vertex_input = nullptr`. Buckets need an analogous "render
  multiview" code path that issues one `draw_primitives()` covering
  `6 * primitive_count` vertices into the triangle SSBO, indexed by
  `gl_VertexID + gl_ViewIndex * view.stride_per_view`.
- `Debug_renderer_bucket`: needs to know which UBO layout to write.
  Either thread `bool multiview` through `start_view` / `dispatch_compute`
  / `render`, or add a `Debug_renderer_multiview` flag on the bucket
  set at the same time as `start_view`. The triangle SSBO acquisition
  in `dispatch_compute()` (`debug_renderer_bucket.cpp:283-327`) sizes
  to `6 * primitive_count * triangle_vertex_stride` -- multiview must
  multiply by `view_count` to leave room for all per-eye slabs.
- Shader migrations (still no caller switched yet):
  - `compute_before_line.comp` -- mirror
    `compute_before_content_line.comp:71-245`. Wrap `do_wide_line`
    body in a `for (uint v = 0u; v < view.view_count; ++v)` over
    `out_triangle_index = v * (view.stride_per_view / 3u) + 2u * in_line_index`,
    indexing `view.cameras[v]`. Single-view trip count = 1.
  - `line_after_compute.frag` -- add `#ifdef ERHE_MULTIVIEW` branch
    reading `view.cameras[c_view_index].viewport.xy`, subtracting
    from `gl_FragCoord.xy` before line-distance math (mirror
    `content_line_after_compute.frag:1-45`).
  - `line_after_compute.vert` already has the multiview branch -- just
    confirm its `view.stride_per_view` reference resolves once the UBO
    has that field.

Verification: build OpenGL Debug + Vulkan Debug. Multiview API exists
but is unreachable in production -- single-view debug lines still work
as before.

Commit subject suggestion:
`renderer: add Debug_renderer multiview pipeline variant`.

### Phase 3 -- wire into Headset_view multiview path

Goal: debug lines appear in both eyes on Quest.

Files:

- `src/editor/xr/headset_view.cpp:485-655`
  -- inside `multiview_callback`, before opening the multiview render
  pass, build a `std::vector<View>` (or whatever type Phase 2 settled
  on) populated from `frame.views`: each entry's `clip_from_world`
  comes from the per-view `Camera_view_input` like content_wide_line
  already constructs at lines 519-523, viewport is the shared
  `viewport_xy` (since multiview shares an attachment), fov from
  `render_view.fov_*`, view_position_in_world from
  `render_view.view_pose.position`. Then call:

  ```cpp
  m_context.debug_renderer->begin_frame(viewport_xy, views, conventions);
  // (existing tools / app_rendering calls below feed it)
  if (m_context.debug_renderer->use_compute()) {
      // emit compute encoder + barrier the same way the per-eye path
      // does at headset_view.cpp:753-763
  }
  // inside the multiview render pass:
  m_context.debug_renderer->render(encoder, multiview_render_pass,
                                   viewport_xy, /*multiview=*/true);
  m_context.debug_renderer->end_frame();
  ```

- Audit `tools->render_viewport_tools(...)` and
  `app_rendering->render_viewport_renderables(...)` -- those are the
  call sites that submit debug-line draws via Debug_renderer. They
  expect a single-camera Render_context. They probably need either a
  multi-view variant or to be invoked once per eye against the
  multiview Debug_renderer state. The simpler tactic is to keep the
  per-eye-of-multiview submission of Tool / Renderable lines (the lines
  themselves are world-space and the same across eyes), and only let
  Debug_renderer's compute/render know about multiview when actually
  emitting GPU commands.

Verification:

1. Desktop OpenGL viewport: debug lines unchanged (regression check).
2. Desktop Vulkan + Meta Link: debug lines appear in headset.
3. Quest standalone via `scripts\install_android.bat quest run`: debug
   lines appear in both eyes (uninstall first to wipe SPIR-V cache --
   see `erhe-quest-launch` skill).

Commit subject suggestion:
`editor: drive Debug_renderer through Headset_view multiview path`.

## Caller inventory

`Debug_renderer::begin_frame` is called from these single-camera sites
today; all should keep working unchanged after Phase 1:

- `src/editor/scene/viewport_scene_view.cpp:156` -- desktop viewport.
- `src/editor/xr/headset_view.cpp:746` -- per-eye fallback (loops over
  `Xr_session::render_frame`, called once per eye on
  non-multiview-capable graphics APIs).

Phase 3 adds one new caller at `headset_view.cpp:485` (the multiview
callback), using the new `std::span<const View>` overload.

## Gotchas

- **Triangle SSBO sizing**: in
  `debug_renderer_bucket.cpp:306-315`, the multi-view path must
  allocate `view_count * 6 * primitive_count * triangle_vertex_stride`
  bytes (one slab per eye). Single-view stays
  `6 * primitive_count * triangle_vertex_stride`. Use the runtime
  `view_count` field, not `max_view_count`, so single-view doesn't
  over-allocate.
- **MoltenVK SSBO size clamp**: `debug_renderer_bucket.cpp:206-211`
  notes that the buffer-client acquire must clamp to the block's
  reported byte size. The same clamp applies in the multi-view path
  -- the triangle block's reported size already covers the maximum
  slab layout; just confirm.
- **vp_y_sign and clip_depth_direction stay top-level**: they're
  per-coordinate-convention scalars, not per-view. Don't push them
  inside the cameras[] struct.
- **Multiview pipeline view_input is null**: the multiview vertex
  shader reads pre-transformed triangle vertices from the SSBO, not
  through the input assembler. The pipeline must be created with
  `vertex_input = nullptr` -- see
  `content_wide_line_renderer.cpp:562-577`. Setting a non-null vertex
  input here is the easy way to trip
  VUID-VkGraphicsPipelineCreateInfo input-assembler-format mismatches.
- **Per-eye fragment shader viewport**: when extending
  `line_after_compute.frag` with the multiview branch, remember that
  on Vulkan multiview each layer's `gl_FragCoord` is local to the
  attachment region for that view -- but the Quest swapchain shares
  one attachment across views, so the xy offset is constant 0 per eye
  in practice. `Content_wide_line_renderer`'s frag still reads
  `view.cameras[c_view_index].viewport.xy` for symmetry; doing the
  same here keeps the math identical between single-view and
  multiview.
- **No band-aid fixes**: under no circumstances ship a Phase 1 that
  silently no-ops the multiview path -- the goal is to make
  Debug_renderer multiview-aware, not to make the symptom go away. If
  Phase 2 hits a wall, document the wall in this file and back out
  cleanly to the Phase 1 single-view-with-cameras[] state.

## Out of scope

- `Text_renderer` multiview. Same shape of work, separately tracked
  via `kAllowMultiview` in `init_status_display.cpp`. Could be ported
  immediately after Debug_renderer using the same scaffolding (the
  `cameras[c_view_index]` macro + multiview pipeline variant pattern).
- Geometry-shader fallback path (`debug_line.geom`). Geometry-shader
  builds (`!use_compute`) target OpenGL 4.1 / macOS where multiview
  isn't relevant -- the geometry-shader path can stay single-view
  forever. Only the compute path needs multiview.
