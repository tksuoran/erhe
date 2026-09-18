# Init-time GPU prewarm

Stability: stable

## Why

Quest's launch interstitial gives roughly 30 seconds for the first
`xrEndFrame` call to land. A first frame on a cold install otherwise triggers
a burst of glslang -> SPIR-V -> `vkCreateShaderModule` compiles for every
standard-shader variant the forward path asks for, plus
`vkCreateGraphicsPipelines` calls for each new render-pass / format
combination. Cumulatively that can exceed the OS budget, at which point the
process is `SIGKILL`'d with no abort, no validation message, and no log line
beyond the OS's `Process org.libsdl.app.quest has died` notice.

The prewarm moves that work into the init phase, where the trailing
`m_graphics_device->wait_idle()` at the end of init absorbs any GPU work it
enqueues. The cost shows up at init instead of at first frame.

## Pieces

### Shader-module cache (glslang -> SPIR-V -> `vkCreateShaderModule`)

`Forward_renderer::prewarm_standard_variants(const Prewarm_parameters&)` in
`forward_renderer.{hpp,cpp}`. It walks the same buckets
`Forward_renderer::render` would build for each `(pipeline, mesh_span)` pair
and, for every requested view count, resolves the bucket's `Shader_key`
through `Shader_variant_cache::get(...)`, mirroring the runtime gate that
picks pipelines with a non-null `vertex_format`. Buckets are built for both
mesh variants, so the base and optimized vertex formats - which differ in
position encoding - are warmed side by side.

The `extra_materials` span handles content-library materials whose meshes are
not yet attached; each material is keyed against the fallback `Vertex_format`
(from `Mesh_memory`) with both `mesh_has_skin = false` and `true`, so a later
runtime assignment to a static or skinned mesh hits the cache. When the
fallback format carries no joint attributes, the skinned variant collapses to
the same key and the second `get` is a cache hit.

`Scene_preview::prewarm_variants(...)` in `scene_preview.{hpp,cpp}` drives
`prewarm_standard_variants` against the preview's own scene root and content
library. Single-view only, since preview render targets are 2D offscreen
textures. `Material_preview` and `Brush_preview` inherit it.

### VkPipeline cache (`vkCreateGraphicsPipelines`)

`Shadow_renderer::prewarm_pipelines(render_passes, mesh_spans, cull_mode)` in
`shadow_renderer.{hpp,cpp}` forces the format-hashed `Render_pipeline` (and
its underlying `VkPipeline` on Vulkan) to be constructed before the first
draw, for every render pass the runtime will use - one per active
`Shadow_render_node`'s shadow atlas. It warms one variant per {skinning state,
position encoding} and only the active graphics preset's shadow filter mode
and cull mode; switching either at runtime compiles the other once, on demand.

`Device::warmup_render_pipeline(const Render_pipeline_create_info&)` in
`device.{hpp,cpp}` constructs and immediately destructs a `Render_pipeline`;
on Vulkan the resulting binary is retained in the driver-level
`VkPipelineCache` (`Device_impl::m_pipeline_cache`), so subsequent
`vkCreateGraphicsPipelines` calls with the same shader modules and state tuple
skip IR optimization. `Forward_renderer::Warmup_target` carries the color /
depth / stencil formats, sample count and usage flags of one render pass the
runtime will use; when `Prewarm_parameters::warmup_targets` is non-empty,
`prewarm_standard_variants` calls `warmup_render_pipeline` once per
`(Render_pipeline_create_info, bucket-variant-key, matching view_count)` tuple
it visits.

### Orchestrator and editor wiring

`prewarm_all(App_context&, init_message)` in
`src/editor/renderers/prewarm.{hpp,cpp}` walks every `Scene_root` returned by
`App_scenes::get_scene_roots()`, snapshots its `Light_layer` counts (via
`compute_light_layer_partition`, using the preset's per-light-type limits so
the prewarmed light-count variants match what the runtime partitions), gathers
the content and controller mesh layers, pulls standard-variant pipelines from
the `Composer`, and calls `Forward_renderer::prewarm_standard_variants`. It
then calls `Shadow_renderer::prewarm_pipelines` for every `Shadow_render_node`
of that scene - or once with an empty render-pass list when there is none, so
the depth-only shader modules exist before the first frame even though shadow
nodes are typically built lazily after init. Finally it calls
`prewarm_variants` on `material_preview` and `brush_preview`.

Fullscreen composition passes (empty mesh layers) are skipped: they take the
`Forward_renderer::draw_primitives` path with their own fixed shader stages
and never consult the variant cache.

OpenXR builds prewarm both single-view and multiview view counts (sourced from
`Xr_session::get_view_count()`); single-view builds prewarm only
`view_count = 0`, which matches the runtime's `views.size() == 1` path.

It is called from `editor.cpp` between `run_startup_script()` and the
close + submit + `wait_idle` block. `init_message`, when non-empty, is invoked
once per `Scene_root` with the scene name, so `Init_status_display` can show
per-scene progress on the loading screen. One summary line is logged:
scene roots walked, forward pipelines warmed, shadow prewarm calls and nodes,
and the scene / preview / total milliseconds.

## The application-level encoder cache is not prewarmable

`Render_command_encoder_impl`'s `m_pipeline_map` is keyed on
`(pipeline_layout, vertex_module, fragment_module, full pipeline state,
sample_count, color_attachment_count, active_render_pass pointer,
vertex_input)`. It cannot be populated at init time, because the headset
render pass is constructed inside the multiview callback in
`Headset_view::render_headset()` and only exists after the first
`xrWaitFrame`. `Device::warmup_render_pipeline` populates the driver-level
cache only, so this cache misses on the first bind - which is cheap once the
driver cache holds the binary.

## Target

After a clean uninstall and reinstall (which wipes the on-device SPIR-V cache
and the migrated config), first frame should land in under five seconds on
Quest 3 with validation layers enabled - well inside the 30 s OS interstitial
budget.

## Future work

- [XR](plans/xr.md) - pipeline-count reporting, foveated / quad-view view
  counts, disabled composition passes, and the startup cost of the prewarm
  itself.
