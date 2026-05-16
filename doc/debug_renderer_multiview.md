# Debug_renderer multiview port

> **Status (2026-05-19): in flight after the pipeline-create-info split.**
> The data flow described below is still correct, but the pipeline plumbing
> changed:
>
> - `Render_pipeline_create_info` was split into
>   `Base_render_pipeline_create_info` (format-independent state) and
>   `Render_pipeline_create_info` (composes the base plus shader_stages,
>   vertex_input, vertex_format). `Lazy_render_pipeline` now takes the base
>   and resolves shader_stages / vertex_input at the call site via
>   `get_pipeline_for(render_pass_desc, shader_stages, vertex_input,
>   vertex_format)`.
> - `Shader_stages_create_info::enable_multiview(N)` is gone; set
>   `view_count` directly. With this, the multiview graphics pipeline no
>   longer needs a separate `Lazy_render_pipeline` bypass -- the
>   per-call `get_pipeline_for(...)` resolves either the single-view or the
>   multiview `Shader_stages*` per draw.
> - The "Compute / graphics pipelines" section below still references the
>   `vertex_input = nullptr` bypass via `Render_pipeline_state`; the same
>   shape is now spelled by passing `vertex_input = nullptr` into
>   `Lazy_render_pipeline::get_pipeline_for(...)`.
>
> The view UBO layout, compute / graphics shader pairing, and bucket
> internals described below are unchanged.

## Status

Implemented. `Debug_renderer` participates in
`Headset_view::multiview_callback` so debug lines (tool gizmos,
manipulators, debug visualisations) render in both eyes on Quest in
a single multiview render pass. Single-view callers keep working
unchanged.

The port mirrors `erhe::scene_renderer::Content_wide_line_renderer`
(read that side-by-side when extending the multiview path):

- `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.{hpp,cpp}`
- `res/shaders/compute_before_content_line.comp`
- `res/shaders/content_line_after_compute.{vert,frag}`

## View UBO layout

Defined in `Debug_renderer_program_interface` and consumed by every
debug-line shader through the `view` block at binding 3:

```glsl
struct DebugViewCamera {
    mat4 clip_from_world;
    vec4 viewport;          // (x, y, w, h) -- shared across eyes on
                            // the multiview swapchain
    vec4 fov;               // (left, right, up, down) tan-half-angles
    vec4 view_position_in_world;
};

layout(std140, binding = 3) uniform view_block {
    DebugViewCamera cameras[max_view_count];
    uint  view_count;       // 1 = single-view, N = multiview
    uint  stride_per_view;  // triangle SSBO vertices per eye (multiview only)
    float vp_y_sign;
    float _padding0;
} view;
```

Per-coordinate-convention scalars (`vp_y_sign`,
`clip_depth_direction`) stay top-level -- they are not per-view.
`view.stride_per_view` is unused on the single-view path; the bucket
writes `0` there.

`max_view_count` is a constructor parameter on
`Debug_renderer` / `Debug_renderer_program_interface` and threads
through from `editor.cpp` as `xr_max_view_count` (taken from the
OpenXR session). Default `1` keeps non-XR callers single-view.

The `c_view_index` macro in `res/shaders/erhe_camera_view.glsl`
resolves to `gl_ViewIndex` under `ERHE_MULTIVIEW` and to `0u`
otherwise, so the same shader source can read
`view.cameras[c_view_index].clip_from_world` on either path.

## Public C++ API

```cpp
class Debug_renderer {
public:
    explicit Debug_renderer(
        erhe::graphics::Device& graphics_device,
        int                     max_view_count = 1
    );

    // Single-view begin_frame: cameras[0] is populated, view_count = 1.
    void begin_frame(
        erhe::math::Viewport                      viewport,
        const erhe::scene::Camera&                camera,
        const erhe::math::Coordinate_conventions& conventions = {}
    );

    // Multiview begin_frame: cameras[0..N-1] populated, view_count = N.
    // views.size() must equal max_view_count from construction.
    void begin_frame(
        erhe::math::Viewport                      viewport,
        std::span<const View>                     views,
        const erhe::math::Coordinate_conventions& conventions = {}
    );

    void compute(erhe::graphics::Compute_command_encoder& encoder);
    void render (
        erhe::graphics::Render_command_encoder& encoder,
        const erhe::graphics::Render_pass&      render_pass,
        erhe::math::Viewport                    viewport,
        bool                                    multiview = false
    );
    void end_frame();
};
```

`render(..., multiview = true)` requires the surrounding render pass
to be a multiview pass (`view_mask != 0`) and the current frame to
have been opened with the multi-camera `begin_frame` overload.

## Compute / graphics pipelines

Compute (`compute_before_line.comp`) is a single program for both
paths. Its `main()` always loops `for (uint v = 0; v <
view.view_count; ++v)`, calling `do_wide_line(v, in_line_index,
out_triangle_index)` and indexing `view.cameras[v]`. With
`view_count = 1` the loop runs once; with `view_count = N` it
writes one triangle slab per eye into a single SSBO at
`out_triangle_index = v * (view.stride_per_view / 3u) + 2u *
in_line_index`.

Graphics has two variants:

- `graphics_shader_stages` (single-view): vertex stage reads the
  triangle SSBO via the input assembler, fragment stage uses
  `gl_FragCoord` directly.
- `multiview_graphics_shader_stages` (built only when
  `max_view_count >= 2`): same `line_after_compute.{vert,frag}`
  source recompiled with `enable_multiview(N)`. The vertex stage's
  `#ifdef ERHE_MULTIVIEW` branch reads the SSBO at `gl_VertexID +
  gl_ViewIndex * view.stride_per_view`; the fragment stage's
  multiview branch subtracts `view.cameras[c_view_index].viewport.xy`
  from `gl_FragCoord` for per-eye line-distance math.

Bind group layouts:

- `bind_group_layout` -- compute + single-view graphics. Binds the
  line-input SSBO (binding 0), triangle-output SSBO writeonly
  (binding 1), and the view UBO (binding 3).
- `multiview_graphics_bind_group_layout` -- multiview graphics only.
  Binds the triangle SSBO read-only (binding 1) and the view UBO
  (binding 3); intentionally omits the line-input SSBO at binding
  0 because the multiview vertex shader reads pre-transformed
  triangles directly from the SSBO and never touches the original
  line vertices.

`triangle_vertex_buffer_read_block` is the read-only sibling
declaration of the writeonly compute output, mapped to the same
descriptor binding (1). Different GLSL block names (auto-suffixed
`_block`) avoid duplicate-symbol errors but the descriptor binding
is shared so a single buffer bind serves both declarations.

The multiview graphics path bypasses `Lazy_render_pipeline` and
constructs a per-call `Render_pipeline_state` with `vertex_input =
nullptr` and the multiview shader stages. This is the same pattern
`Forward_renderer` and `Content_wide_line_renderer` use for their
multiview overrides.

## Bucket internals

`Debug_draw_view_span::views` holds either 1 or N `View` entries.
`Debug_renderer_bucket::start_view` has overloads for `const View&`
(single-view) and `std::span<const View>` (multiview).

`update_view_buffer(views, primitive_count_for_stride)` writes
`cameras[0..views.size()-1]`, sets `view_count = views.size()`,
and writes `stride_per_view = 6 * primitive_count_for_stride` when
`views.size() >= 2`. The whole UBO range is zero-filled first so
`cameras[k > views.size()]` and any padding read as zero -- MoltenVK
descriptor validation requires the bound range to cover the full
block size, so leaving tail bytes uninitialised would expose
ring-buffer leftovers from a prior frame.

`dispatch_compute` writes a fresh view UBO per draw on the
multiview path (each draw has its own
`stride_per_view = 6 * draw.primitive_count` tied to its primitive
count) and stores the range on `Debug_draw_entry::view_buffer_range`
so the render encoder can re-bind it without re-uploading. The
single-view path keeps the per-view-span batching it had before
because all draws inside a span share the same UBO contents.

The triangle SSBO acquires `view_count * 6 * primitive_count *
triangle_vertex_stride` bytes (one slab per eye), clamped up to
`triangle_vertex_buffer_block.get_size_bytes()` for MoltenVK. Use
the runtime `view_count` (from the current view span), not
`max_view_count`, so single-view callers do not over-allocate.

`Debug_renderer_bucket::render(..., multiview)` issues one
`draw_primitives` per dispatched draw, covering `6 *
draw.primitive_count` vertices, with the triangle SSBO bound
read-only at binding 1 and the per-draw view UBO bound at binding
3.

## Caller inventory

`begin_frame` is called from:

- `src/editor/scene/viewport_scene_view.cpp` -- desktop viewport,
  single-camera overload.
- `src/editor/xr/headset_view.cpp` per-eye fallback -- single-camera
  overload, used on graphics APIs that do not enable multiview.
- `src/editor/xr/headset_view.cpp` multiview_callback -- multi-camera
  overload, used on Quest (Vulkan + `XR_KHR_vulkan_enable2` with
  multiview enabled).

The multiview caller builds one `View` per eye from the existing
`view_inputs` vector by composing
`projection->clip_from_node_transform(...) * node->node_from_world()`,
copying the shared viewport, reading `fov` from
`projection->get_fov_sides`, and taking
`view_position_in_world` from `world_from_node[3]`.

Tool / Renderable submission (`tools->render_viewport_tools`,
`app_rendering->render_viewport_renderables`) runs once per frame
against a single `Render_context` whose camera is the primary eye.
The lines are world-space and identical across eyes;
Debug_renderer's compute stage fans them out per-eye via the
`view.view_count` loop. There is no need for a per-eye submission
pass.

The combined compute encoder block in `multiview_callback` issues
both `Content_wide_line_renderer::compute` and
`Debug_renderer::compute` inside one encoder scope, gated together
so neither runs an empty Compute_command_encoder when the other is
also idle. The single trailing memory barrier covers both with
`vertex_attrib_array_barrier_bit | shader_storage_barrier_bit`.

## Gotchas

- **Triangle SSBO sizing**: multiview needs `view_count * 6 *
  primitive_count * triangle_vertex_stride` bytes. Use the runtime
  `view_count` (from the current `Debug_draw_view_span::views`),
  not `max_view_count`, so single-view callers do not over-allocate.
- **MoltenVK SSBO size clamp**: the buffer-client acquire must
  clamp to the block's reported byte size (see comment in
  `joint_buffer.cpp`). The triangle block's reported size covers
  the maximum slab layout; same clamp applies on the multi-view
  path.
- **vp_y_sign and clip_depth_direction stay top-level**: they are
  per-coordinate-convention scalars, not per-view. Do not push
  them inside the `cameras[]` struct.
- **Multiview pipeline `vertex_input` is null**: the multiview
  vertex shader reads pre-transformed triangles from the SSBO, not
  through the input assembler. The pipeline must be created with
  `vertex_input = nullptr` (see how the bucket builds the
  per-call `Render_pipeline_state`). Setting a non-null
  `vertex_input` here is the easy way to trip
  `VUID-VkGraphicsPipelineCreateInfo` input-assembler-format
  mismatches.
- **Per-eye fragment shader viewport**: each multiview layer's
  `gl_FragCoord` is local to the attachment region for that view,
  but the Quest swapchain shares one attachment across views so
  the per-eye xy offset is constant 0 in practice. The fragment
  shader still reads `view.cameras[c_view_index].viewport.xy` for
  symmetry with `content_line_after_compute.frag`; doing so keeps
  the math identical between single-view and multiview.
- **`struct_types` registration**: every shader create_info that
  references the view UBO must list `view_camera_struct` in
  `struct_types` so the GLSL preamble emits `struct DebugViewCamera
  { ... };` before the block declaration. Forgetting this fails
  shader compilation with "undefined struct" the first time a
  caller exercises that pipeline.

## Out of scope

- **Geometry-shader fallback** (`debug_line.geom`).
  Geometry-shader builds (`!use_compute`) target OpenGL 4.1 /
  macOS where multiview is not relevant; the geometry-shader path
  stays single-view. Only the compute path is multiview-aware.
