# OpenXR multiview rendering on Quest 3 (Vulkan)

How erhe drives single-pass stereo rendering through Vulkan multiview
when running OpenXR on Quest 3.

## Scope

- Vulkan backend only. The OpenGL backend does NOT support OpenXR
  multiview; the OpenGL path covers the desktop-mirror window only.
- Stereo only (2 views) in practice. The infrastructure scales to
  higher view counts but no view configuration beyond stereo is
  exercised.
- Per-eye fallback path stays in place for: non-Vulkan backends,
  Vulkan devices without `multiview`, OpenXR runtimes that report
  mismatched per-eye extents, or any combination that fails the
  capability check.

## Capability gate

`Xr_session::create_swapchains()` enables multiview when **all** of:

- The graphics device reports `multiview = true` (queried at Vulkan
  device init from `VkPhysicalDeviceMultiviewFeatures` in
  `src/erhe/graphics/erhe_graphics/vulkan/vulkan_device_init.cpp`).
- The OpenXR view configuration has exactly two views.
- All views report identical recommended width, height, and sample
  count.

If any of these is false, the per-eye-swapchain code path runs and
`Xr_session::is_multiview_enabled()` returns false. The editor reads
this once at startup and propagates `view_count` (1 or 2) into
`Program_interface_config`, which determines the size of the
camera UBO and which view count is keyed into shader variants.

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
shaders that read camera data include a small helper:

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

A shader stage opts into multiview by setting
`Shader_stages_create_info::view_count > 1`. `final_source()`:

- always emits `#define ERHE_VIEW_COUNT N`,
- when `view_count > 1`, additionally emits `#extension
  GL_EXT_multiview : require` and `#define ERHE_MULTIVIEW 1`.

`GL_EXT_multiview` is the Vulkan / SPIR-V flavour of multiview --
the view count comes from the render pass `viewMask`, not from a
shader layout qualifier. There is intentionally no
`layout(num_views = N) in;` in the prelude; that is the OpenGL
`GL_OVR_multiview2` syntax and is out of scope for this engine.

## Variant cache integration

The `Shader_key` for variant lookups carries
`Shader_int::SHADER_MULTIVIEW_COUNT`. `Shader_variant_cache::get(key,
vertex_format)` reads that count off the key and passes it through as
`Shader_stages_create_info::view_count`, so single-view and multiview
versions of the same shader live as two cache entries keyed by the
same set of bool / scene axes plus differing `SHADER_MULTIVIEW_COUNT`.
There is no parallel multiview shader map; the cache handles both.

## Camera UBO layout

`Program_interface_config::view_count` controls the array size of
the camera UBO block:

```glsl
layout(std140, binding = 4) uniform camera {
    Camera cameras[/*view_count*/];
} camera;
```

`view_count` is set by the editor at startup based on
`Xr_session::is_multiview_enabled()` (1 for non-multiview, 2 for
stereo). `Camera_buffer::update_views(span)` writes one entry per
view in lock-step with `gl_ViewIndex`.

## Wide-line / debug-line

When multiview is active, the wide-line and debug-line compute step
runs as a single dispatch that emits per-view-strided triangle output:

- View UBO is registered as `view[view_count]` instead of a single
  view struct (in `Content_wide_line_renderer` and `Debug_renderer`).
- Triangle output SSBO size is `view_count * line_count * 6`
  vertices, laid out as `[view][line][vertex]`.
- `compute_before_line.comp` and `compute_before_content_line.comp`
  loop over `view[]` (or unroll for stereo) and write each view's
  output to the corresponding stride. The math for each view is
  unchanged: each view goes through the existing `clip_from_world`
  transform, z-clipping, and screen-space widening using its own
  per-eye projection -- output is bit-identical to the per-view path.
- Line draw vertex shaders compile a multiview variant that indexes
  the triangle SSBO by `gl_VertexID + gl_ViewIndex * stride_per_view`.
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

- position = midpoint of left / right eye world-space positions.
- orientation = slerp(left, right, 0.5).
- field of view = union of the left / right eye FOV bounds.

Both eyes then sample the same shadow map. With ~6 cm IPD this
introduces a sub-pixel error compared to per-eye shadow fits and is
visually indistinguishable.

## Capability flag, end to end

| Where | Behaviour when multiview is on |
|---|---|
| `Xr_session::create_swapchains` | Creates one shared color + one shared depth `XrSwapchain` with `arraySize = 2`, wraps as `texture_2d_array` |
| `Xr_session::is_multiview_enabled` | Returns true |
| `editor.cpp` Program_interface_config | `view_count = 2` |
| `Camera_interface` | UBO declares `cameras[2]` |
| `Shader_key` | `SHADER_MULTIVIEW_COUNT = 2`; cache compiles a multiview variant |
| Material / line shaders | Compiled with `ERHE_MULTIVIEW` -> `c_view_index = gl_ViewIndex` |
| `Headset_view::render_headset` | Calls `Xr_session::render_frame_multiview(...)` |
| Render pass for the headset | `view_mask = 0b11`, attachments are layered, framebuffer `layers = 1` (per Vulkan multiview spec) |
| Forward renderer | `render_views(...)` updates `cameras[]` once and dispatches one pass |
| Wide-line / debug-line | One compute dispatch with per-view-strided output, one draw call per primitive group |
| Shadow fit | Cyclopean view |

When multiview is off, every column above falls back to its
single-view behaviour (per-eye swapchains, per-eye UBO writes,
per-eye renderer dispatches, single-view shaders).

## File reference

| Path | Role |
|---|---|
| `src/erhe/graphics/erhe_graphics/device.hpp` | Vulkan multiview cap |
| `src/erhe/graphics/erhe_graphics/render_pass.hpp` | `view_mask` field on `Render_pass_descriptor` |
| `src/erhe/graphics/erhe_graphics/vulkan/vulkan_render_pass.cpp` | Threads `view_mask` into `VkSubpassDescription2`; layered image views |
| `src/erhe/graphics/erhe_graphics/shader_stages.{hpp,cpp}` | `Shader_stages_create_info::view_count`; emits `GL_EXT_multiview` / `ERHE_MULTIVIEW` / `ERHE_VIEW_COUNT` when `view_count > 1` |
| `src/erhe/scene_renderer/erhe_scene_renderer/camera_buffer.{hpp,cpp}` | `cameras[view_count]`; `update_views(span)` |
| `src/erhe/scene_renderer/erhe_scene_renderer/forward_renderer.{hpp,cpp}` | `render_views(...)` entry |
| `src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.{hpp,cpp}` | Cyclopean shadow fit |
| `src/erhe/scene_renderer/erhe_scene_renderer/shader_key.hpp` | `Shader_int::SHADER_MULTIVIEW_COUNT` axis |
| `src/erhe/scene_renderer/erhe_scene_renderer/shader_variant_cache.{hpp,cpp}` | Resolves the multiview variant via the same cache |
| `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.{hpp,cpp}` | `view[N]`, single dispatch, strided output |
| `src/erhe/renderer/erhe_renderer/debug_renderer.{hpp,cpp}` | Same |
| `src/erhe/renderer/erhe_renderer/debug_renderer_bucket.{hpp,cpp}` | Single dispatch |
| `res/shaders/erhe_camera_view.glsl` | `c_view_index` helper |
| `res/shaders/compute_before_line.comp` | Per-view loop |
| `res/shaders/compute_before_content_line.comp` | Per-view loop |
| `res/shaders/*line*.vert` | Multiview SSBO indexing |
| `res/shaders/standard.{vert,frag}`, `points.vert`, `id.vert`, `textured.vert`, `error.vert`, `wide_lines.{vert,geom}`, `edge_lines.{vert,frag}` | Use `c_view_index` |
| `src/editor/editor.cpp` | Set `view_count` from XR cap |
| `src/editor/xr/headset_view.{hpp,cpp}` | Multiview render path |
| `src/erhe/xr/erhe_xr/xr_session.{hpp,cpp}` | `render_frame_multiview()`; shared layered swapchain |
| `src/erhe/xr/erhe_xr/xr_swapchain_image.{hpp,cpp}` | `array_layer_count` parameter |
