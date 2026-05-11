# OpenXR multiview rendering on Quest 3 (Vulkan)

This document describes how erhe drives single-pass stereo rendering
through Vulkan multiview when running OpenXR on Quest 3, what is
already wired up in the codebase, and what work is still required to
exercise the path end-to-end.

## Scope

- Vulkan backend only. OpenGL `GL_OVR_multiview2` is intentionally not
  supported; the OpenGL path keeps the per-eye fallback.
- Stereo only (2 views). The infrastructure scales to higher view
  counts but no view configuration beyond stereo is exercised.
- Per-eye fallback path stays in place for: non-Vulkan backends,
  Vulkan devices without `multiview`, OpenXR runtimes that report
  mismatched per-eye extents, or any combination that fails the
  capability check.

## Capability gate

`Xr_session::create_swapchains()` enables multiview when **all** of:

- The graphics device reports `multiview = true` (queried at Vulkan
  device init from `VkPhysicalDeviceMultiviewFeatures`, see commit
  d3eeb4cb and
  `src/erhe/graphics/erhe_graphics/vulkan/vulkan_device_init.cpp`).
- The OpenXR view configuration has exactly two views.
- All views report identical recommended width, height, and sample
  count.

If any of these is false, the per-eye-swapchain code path runs and
`Xr_session::is_multiview_enabled()` returns false. The editor reads
this once at startup and propagates `max_view_count` (1 or 2) into
`Program_interface_config`, which determines whether multiview shader
variants are compiled.

## Frame data flow (multiview path)

```
Xr_session::render_frame_multiview(setup_cb, callback)
  |
  +-- xrLocateViews -> m_xr_views[0..1]
  +-- shared_color_swapchain.acquire() / wait()
  +-- shared_depth_stencil_swapchain.acquire() / wait()
  +-- allocate views_cb; setup_cb.signal_gpu(views_cb); submit setup_cb
  +-- views_cb.begin(); views_cb.wait_for_gpu(views_cb)
  +-- callback(Render_views_frame { views,
  |                                  shared_color_texture,
  |                                  shared_depth_stencil_texture,
  |                                  view_mask,
  |                                  width, height },
  |            views_cb)
  |     |
  |     +-- light_buffer.update(...)         (cyclopean fit)
  |     +-- shadow render pass               (single-view, layered shadow tex)
  |     +-- camera_buffer.update_views(views) (writes cameras[0..1])
  |     +-- Scoped_render_pass {
  |           Render_pass_descriptor {
  |             color_attachments[0].texture = shared_color_texture,
  |             depth_attachment.texture     = shared_depth_stencil_texture,
  |             view_mask                    = (1u << 0) | (1u << 1)
  |           }
  |         }
  |       |
  |       +-- forward_renderer.render_views(views, ...)
  |       |     [single draw -> two layers via VK_KHR_multiview]
  |       +-- content_wide_line_renderer.compute_and_render_views(views, ...)
  |       |     [one compute dispatch produces both views' triangles;
  |       |      one draw renders to both layers]
  |       +-- debug_renderer.compute_and_render_views(views, ...)
  |             [same shape as above]
  |
  +-- views_cb.end(); submit views_cb
  +-- build XrCompositionLayerProjectionView[i] with subImage.swapchain
      = shared_color_swapchain, .imageArrayIndex = i
```

The per-eye fallback path follows the same shape but with
`render_frame()` (callback per view), per-eye swapchains, two render
passes, and per-view UBO updates.

## Shader convention

To keep one source per shader instead of duplicating files, all
shaders that read camera data use a small helper:

```glsl
#include "erhe_camera_view.glsl"
...
mat4 clip_from_world = camera.cameras[c_view_index].clip_from_world;
```

`erhe_camera_view.glsl` defines `c_view_index` based on the
`ERHE_MULTIVIEW` define injected by the build pipeline:

- non-multiview compile: `#define c_view_index 0u`
- multiview compile (vertex / fragment under Vulkan): `#define
  c_view_index gl_ViewIndex` (after enabling `GL_EXT_multiview`).

A shader stage opts into multiview by calling
`Shader_stages_create_info::enable_multiview(view_count)`, which:

- adds the define `ERHE_MULTIVIEW = 1` and `ERHE_VIEW_COUNT = N`,
- emits `#extension GL_EXT_multiview : require` in the prelude.

`GL_EXT_multiview` is the Vulkan / SPIR-V flavour of multiview --
the view count comes from the render pass `viewMask`, not from a
shader layout qualifier. There is intentionally no
`layout(num_views = N) in;` in the prelude; that is the OpenGL
`GL_OVR_multiview2` syntax and is out of scope for this engine.

## Camera UBO layout

`Program_interface_config::max_view_count` controls the array size of
the camera UBO block:

```glsl
layout(std140, binding = 4) uniform camera {
    Camera cameras[/*max_view_count*/];
} camera;
```

`max_view_count` is set by the editor at startup based on
`Xr_session::is_multiview_enabled()` (1 for non-multiview, 2 for
stereo). `Camera_buffer::update_views(span)` writes one entry per
view in lock-step with `gl_ViewIndex`.

## Wide-line / debug-line: Option D

When multiview is active, the wide-line and debug-line compute step
runs as a single dispatch that emits per-view-strided triangle output:

- View UBO is registered as `view[max_view_count]` instead of a single
  view struct (in `Content_wide_line_renderer` and `Debug_renderer`).
- Triangle output SSBO size is `max_view_count * line_count * 6`
  vertices, laid out as `[view][line][vertex]`.
- `compute_before_line.comp` and `compute_before_content_line.comp`
  loop over `view[]` (or unroll for stereo) and write each view's
  output to the corresponding stride. The math for each view is
  unchanged: each view goes through the existing `clip_from_world`
  transform, z-clipping, and screen-space widening using its own
  per-eye projection -- output is bit-identical to the per-view path.
- Line draw vertex shaders (`line_after_compute.vert`,
  `content_line_after_compute.{vert,frag}`, `edge_lines.vert`,
  `wide_lines.vert`, `debug_line.vert`) compile a multiview variant
  that indexes the triangle SSBO by
  `gl_VertexID + gl_ViewIndex * stride_per_view`.
- CPU dispatch sites stop looping per view and dispatch once per
  primitive group.

The non-multiview path uses `view_count = 1`, in which case the
strided indexing degenerates to the existing single-view layout.

## Shadow / light buffer

Shadow rendering stays a single pass into the existing shadow array
texture; only the **frustum fit** changes when multiview is active.
`Light_projections` (in
`src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.cpp`) fits
shadow frustums to a "cyclopean" virtual camera computed from the two
eye cameras:

- position = midpoint of left/right eye world-space positions.
- orientation = slerp(left, right, 0.5).
- field of view = union of the left/right eye FOV bounds.

Both eyes then sample the same shadow map. With ~6 cm IPD this
introduces a sub-pixel error compared to per-eye shadow fits and is
visually indistinguishable.

## Capability flag, end to end

| Where | Behaviour when multiview is on |
|---|---|
| `Xr_session::create_swapchains` | Creates one shared color + one shared depth `XrSwapchain` with `arraySize = 2`, wraps as `texture_2d_array` |
| `Xr_session::is_multiview_enabled` | Returns true |
| `editor.cpp` Program_interface_config | `max_view_count = 2` |
| `Camera_interface` | UBO declares `cameras[2]` |
| Material / line shaders | Compiled with `ERHE_MULTIVIEW` -> `c_view_index = gl_ViewIndex` |
| `Headset_view::render_headset` | Calls `Xr_session::render_frame_multiview(...)` |
| Render pass for the headset | `view_mask = 0b11`, attachments are layered, framebuffer `layers = 1` (per Vulkan multiview spec) |
| Forward renderer | `render_views(...)` updates `cameras[]` once and dispatches one pass |
| Wide-line / debug-line | One compute dispatch with per-view-strided output, one draw call per primitive group |
| Shadow fit | Cyclopean view |

When multiview is off, every column above falls back to its current
single-view behaviour (per-eye swapchains, per-eye UBO writes, per-eye
renderer dispatches, single-view shaders).

## Already in place

The foundation has already landed in the working tree:

- Vulkan multiview device feature query/enable (commit d3eeb4cb).
- `Render_pass_descriptor::view_mask` plumbed into
  `VkSubpassDescription2.viewMask`; swapchain render passes assert
  `view_mask == 0`.
- Camera UBO supports `cameras[max_view_count]`;
  `Camera_buffer::update_views(span)` overload added.
- `Program_interface_config::max_view_count` config knob
  (default 1). Editor sets it from
  `Xr_session::is_multiview_enabled()` at startup.
- `Xr_session::create_swapchains()` creates a shared layered
  `XrSwapchain` (color + depth, `arraySize = 2`) when capability
  allows.
- `erhe::xr::Swapchain` `array_layer_count` parameter; wraps as
  `Texture_type::texture_2d_array` when > 1.
- `Xr_session::render_frame_multiview()` callback API plus
  composition-layer projection views with
  `imageArrayIndex = view_index`.
- Vulkan render-pass attachment image views span all multiview
  layers when `view_mask != 0`.
- `Shader_stages_create_info::enable_multiview(view_count)` helper
  emits `GL_EXT_multiview`, `layout(num_views = N) in;` for vertex
  stages, and `ERHE_MULTIVIEW` / `ERHE_VIEW_COUNT` defines.
- `res/shaders/erhe_camera_view.glsl` defines `c_view_index` as
  `gl_ViewIndex` under multiview, `0u` otherwise.
- All material/line shaders (`standard.{vert,frag}`,
  `standard_debug.{vert,frag}`, `textured.vert`, `points.vert`,
  `id.vert`, `error.vert`, `wide_lines.{vert,geom}`,
  `edge_lines.{vert,frag}`, `anisotropic_*.{vert,frag}`,
  `circular_brushed_metal.{vert,frag}`,
  `content_line_after_compute.frag`, `erhe_light.glsl`) read
  `camera.cameras[c_view_index]`.
- `Forward_renderer::Render_parameters::multiview_views` span:
  when set, `render()` and `draw_primitives()` call
  `Camera_buffer::update_views(...)` instead of the single-camera
  path.
- `Programs::get_multiview(name)` accessor with a parallel map of
  multiview-compiled `Reloadable_shader_stages`. Populated eagerly
  during `Programs::load_programs()` for every shader registered
  there when `program_interface.config.max_view_count >= 2`.
  Single-view fields and existing call sites remain untouched. The
  map key is `debug_label` (when set) else `.name`, since several
  shaders share the same `.name` (e.g. `"standard_debug"`) but
  differ via `debug_label`; keying by `.name` would have replaced
  earlier map entries and left the corresponding
  `Shader_stages_builder` references dangling.
- `erhe::xr::Headset::render_multiview()` forwards to
  `Xr_session::render_frame_multiview()`.
- `Headset_view::m_use_multiview` flag latched at construction
  from `Xr_session::is_multiview_enabled()`. When true,
  `render_headset()` drives `Xr_session::render_frame_multiview()`:
  acquires the shared layered swapchain, builds a
  `std::vector<Camera_view_input>` from the per-eye
  `Headset_view_resources::Camera`s, opens one `Scoped_render_pass`
  with `view_mask = (1 << view_count) - 1`, and calls
  `App_rendering::render_composer()` with the per-eye
  `multiview_views` span on the `Render_context`.
- `Headset_view::m_multiview_view_resources` indexes
  `Headset_view_resources` by `Render_view::slot` on the multiview
  path. The per-eye fallback's color-texture-keyed
  `m_view_resources` lookup is wrong here because
  `Xr_session::render_frame_multiview()` exposes the layered
  textures on `Render_views_frame::shared_*` and leaves every
  per-view `color_texture = nullptr`; matching by null collapses
  both eyes onto one resources object and one shared `Camera/Node`,
  producing cyclopean stereo. Per-slot lookup keeps each eye on its
  own `Camera/Node`.
- `Headset_view_resources` tolerates a null `color_texture` /
  `depth_stencil_texture` on the multiview path (skips per-view
  `Render_pass` construction; the headset multiview callback builds
  the layered render pass over `frame.shared_*_texture`).
- `Render_pipeline_create_info::multiview_shader_stages`: optional
  sibling-shader pointer on each pipeline. Populated by
  `Pipeline_renderpasses` from `Programs::get_multiview(name)` for
  every editor pipeline. `App_rendering`'s init task succeeds
  `programs_load_task` so the multiview map is fully populated
  before the constructor runs (without that edge, parallel init
  races leave every `multiview_shader_stages` field null and the
  headset path silently falls back to single-view shaders).
- `Forward_renderer::render` and `draw_primitives` pick the
  pipeline's `multiview_shader_stages` sibling when
  `parameters.multiview_views` is non-empty. The selection routes
  through `set_render_pipeline_state(temp_state, override)` so the
  Vulkan render-encoder cache compiles a separate `VkPipeline`
  keyed on the multiview shader modules + the multiview render
  pass; the `Lazy_render_pipeline` cache (which baked the
  single-view shader_stages) is intentionally bypassed for these
  draws.
- `Composition_pass::render` forwards `Render_context::multiview_views`
  to `Render_parameters::multiview_views` on both the mesh
  `render(...)` and fullscreen `draw_primitives(...)` paths.

### Validated on Quest 3 (Vulkan), 2026-05-02

End-to-end multiview forward rendering runs on device with correct
per-eye stereo parallax. The full chain is exercised:

- Vulkan multiview device feature detected at startup
  (`multiview = true`, `maxMultiviewViewCount = 6`).
- Single shared color + depth `XrSwapchain` created at
  `1680 x 1760, 2 views, sampleCount 1` (log line "OpenXR
  multiview swapchain created").
- `Headset_view::m_use_multiview` latches true (log line
  "Headset_view: multiview render path enabled").
- Layered render pass with `view_mask = 0b11` is accepted by
  `vkCreateRenderPass2`; framebuffer image views are 2D_ARRAY
  spanning both layers.
- Composition layer projection views with
  `imageArrayIndex = view_index` route layer 0 to the left eye
  and layer 1 to the right eye on the OpenXR runtime.
- Per-eye material draws use the multiview-compiled
  `circular_brushed_metal` shader (verified via a temporary
  per-eye `gl_ViewIndex` tint producing red-left / blue-right);
  scene geometry shows IPD parallax between eyes (verified
  visually in headset).

## Remaining work

### A. Wide-line + debug-line per-view-strided compute (Option D)

The remaining gap is the line / debug-line render path. Today the
headset multiview callback drives `App_rendering::render_composer()`
only, which covers material draws but skips
`Content_wide_line_renderer`, `Debug_renderer`, and the tool
overlay. Wiring those into the multiview path requires:

- View UBO in `Content_wide_line_renderer` and `Debug_renderer`
  becomes `view[max_view_count]`.
- Triangle SSBO sized for `max_view_count * line_count * 6`
  vertices, laid out `[view][line][vertex]`.
- `compute_before_line.comp` and `compute_before_content_line.comp`
  loop over `view[]` (or unroll for stereo) and write each view's
  output to its stride.
- Line draw vertex shaders (`line_after_compute.vert`,
  `content_line_after_compute.{vert,frag}`, `edge_lines.vert`,
  `wide_lines.vert`, `debug_line.vert`) compile a multiview variant
  that indexes the triangle SSBO by `gl_VertexID + gl_ViewIndex *
  stride_per_view`. The pipeline-pair plumbing
  (`Render_pipeline_create_info::multiview_shader_stages`) is
  already in place and ready to receive these.
- CPU dispatch sites stop looping per view and dispatch once per
  primitive group.
- `Headset_view::render_headset()` multiview callback adds the
  line / debug-line dispatches (and tools overlay) inside the
  layered render pass.

Output is bit-identical to the current per-view path at
`view_count = 1`.

### B. Cyclopean shadow fit on the headset multiview path

`Light_projections` already supports a cyclopean fit (see
"Shadow / light buffer" above), but the headset multiview
callback does not currently compute a cyclopean Camera and pass
it through. Today the shadow fit reuses whatever the per-eye
fallback path set up. Compute the midpoint-position +
slerp-orientation + union-FOV virtual camera in
`render_headset()` and feed it into the shadow render node before
opening the multiview render pass.

## Risks / open items

- **Pipeline cache key.** Resolved by the
  `multiview_shader_stages` sibling-pointer approach: the
  multiview path goes through
  `set_render_pipeline_state(temp_state, override_shader_stages)`,
  which lets the Vulkan render-encoder cache key on the multiview
  shader modules + the multiview render pass independently of the
  `Lazy_render_pipeline` cache (which is keyed on the single-view
  shader_stages and is intentionally bypassed for multiview
  draws).
- **MSAA + multiview.** The depth/color resolve attachments now
  span `multiview_layer_count` layers (already plumbed). Validate
  on device before relying on it; Adreno 740 supports the
  combination but layered resolves are not on the standard path.
- **Shader prelude ordering.** `layout(num_views = N) in;` must
  appear in the vertex shader after `#version` and after the
  multiview extension declaration. The build helper enforces this.
- **Stencil aspect.** The editor's depth attachment carries a
  stencil aspect on some configurations; confirm it survives the
  layered image-view path on Quest 3 (Adreno 740) before merging.

## Verification

1. **Non-multiview regression** on each backend (OpenGL Windows /
   Vulkan Windows / Metal macOS): editor opens, meshes render,
   content wide lines and debug lines pixel-match baseline.
   Confirms shader and buffer layout changes are no-ops at
   `view_count = 1`.
2. **Quest 3 + Vulkan**: launch in VR, confirm log line "OpenXR
   multiview swapchain created: WxH, 2 views, ..." appears and
   `Xr_session::is_multiview_enabled()` returns true.
3. **Stereo correctness on device**: each eye sees the correct
   image with IPD parallax; no flickering, no swapped eyes.
4. **Wide / debug line per-eye correctness**: spawn known debug
   lines (axes, frustum); compare each eye against the per-eye
   baseline render. Widths and positions must match.
5. **Performance**: capture a Perfetto trace via the
   `meta-vr:hz-perfetto-debug` skill; compare against the
   2026-05-01 baseline in
   `doc/2026-05-01-quest3-gpu-profiling.md`. Expect:
   - command-buffer count per frame: 2 -> 1 (setup + views).
   - duplicated CPU draw-call submission: gone.
   - GPU vertex/setup work per primitive: roughly halved (one
     pass over both layers instead of two passes).
6. **Capability fallback**: force the Vulkan multiview cap to
   false; confirm Headset_view takes the per-view path with no
   shader recompile errors and no rendering regressions.

## File reference

| Path | Role |
|---|---|
| `src/erhe/graphics/erhe_graphics/device.hpp` | Vulkan multiview cap |
| `src/erhe/graphics/erhe_graphics/render_pass.hpp` | `view_mask` field on `Render_pass_descriptor` |
| `src/erhe/graphics/erhe_graphics/vulkan/vulkan_render_pass.cpp` | Threads `view_mask` into `VkSubpassDescription2`; layered image views |
| `src/erhe/graphics/erhe_graphics/shader_stages.{hpp,cpp}` | `enable_multiview(N)` helper; vertex prelude `layout(num_views=N) in;` |
| `src/erhe/scene_renderer/erhe_scene_renderer/camera_buffer.{hpp,cpp}` | `cameras[max_view_count]`; `update_views(span)` |
| `src/erhe/scene_renderer/erhe_scene_renderer/forward_renderer.{hpp,cpp}` | `render_views(...)` entry |
| `src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.{hpp,cpp}` | Cyclopean shadow fit |
| `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.{hpp,cpp}` | `view[N]`, single dispatch, strided output |
| `src/erhe/renderer/erhe_renderer/debug_renderer.{hpp,cpp}` | Same |
| `src/erhe/renderer/erhe_renderer/debug_renderer_bucket.{hpp,cpp}` | Single dispatch |
| `res/shaders/erhe_camera_view.glsl` (new) | `c_view_index` helper |
| `res/shaders/compute_before_line.comp` | Per-view loop |
| `res/shaders/compute_before_content_line.comp` | Per-view loop |
| `res/shaders/*line*.vert` | Multiview SSBO indexing |
| `res/shaders/standard*.vert/frag`, `points.vert`, `id.vert`, `textured.vert`, `error.vert`, `anisotropic_*.vert/frag`, `circular_brushed_metal.*` | Use `c_view_index` |
| `src/editor/editor.cpp` | Set `max_view_count` from XR cap |
| `src/editor/xr/headset_view.{hpp,cpp}` | Multiview render path |
| `src/erhe/xr/erhe_xr/xr_session.{hpp,cpp}` | `render_frame_multiview()` (already added), shared layered swapchain (already added) |
| `src/erhe/xr/erhe_xr/xr_swapchain_image.{hpp,cpp}` | `array_layer_count` parameter (already added) |
