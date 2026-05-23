# Init-time GPU prewarm

> **Status: API surface in place, bodies currently stubbed.** The
> entry points described below exist (`prewarm_all`,
> `Forward_renderer::prewarm_standard_variants`,
> `Shadow_renderer::prewarm_pipelines`,
> `Device::warmup_render_pipeline`) but most of the bodies are stubbed
> out pending the wider variant / mesh-memory rework. The design intent
> below is still the target; the work to finish wiring it is tracked as
> item F in `doc/work.md`.

## Why

Quest's launch interstitial gives roughly 30 seconds for the first
`xrEndFrame` call to land. The editor's first frame on a cold install
otherwise triggers a burst of glslang -> SPIR-V ->
`vkCreateShaderModule` compiles for every standard-shader variant the
forward path asks for, plus `vkCreateGraphicsPipelines` calls for each
new render-pass / format combination. Cumulatively that can exceed the
OS budget, at which point the process is `SIGKILL`'d with no abort, no
validation message, and no log line beyond the OS's `Process
org.libsdl.app.quest has died` notice.

The fix is to move that work into the existing init phase, where the
trailing `m_graphics_device->wait_idle()` at the end of init absorbs
any GPU work the prewarm enqueues. The cost shows up at init (target
under a second) instead of at first frame.

## Pieces

### Shader-module cache (glslang -> SPIR-V -> `vkCreateShaderModule`)

`Forward_renderer::prewarm_standard_variants(const Prewarm_parameters&)`
in `forward_renderer.{hpp,cpp}`. Walks the same buckets
`Forward_renderer::render` would build for each `(pipeline, mesh_span)`
pair and, for every requested view count, resolves the bucket's
`Shader_key` through `Shader_variant_cache::get(...)`. Mirrors the
runtime gate that picks pipelines with a non-null `vertex_format`.

The `extra_materials` span handles content-library materials whose
meshes are not yet attached; each material is keyed against the
fallback `Vertex_format` (from `Mesh_memory`) with both
`mesh_has_skin=false` and `mesh_has_skin=true` so a later runtime
assignment to a static or skinned mesh hits the cache. When the
fallback format carries no joint attributes, the skinned variant
collapses to the same key and the second `get` is a cache hit.

`Scene_preview::prewarm_variants(...)` in `scene_preview.{hpp,cpp}`
drives `prewarm_standard_variants` against the preview's own
scene_root + content_library. Single-view only (preview render
targets are 2D offscreen textures). `Material_preview` and
`Brush_preview` inherit it.

### VkPipeline cache (`vkCreateGraphicsPipelines`)

`Shadow_renderer::prewarm_pipelines(render_passes)` in
`shadow_renderer.{hpp,cpp}`. Forces the format-hashed `Render_pipeline`
(and its underlying `VkPipeline` on Vulkan) to be constructed before
the first draw for every render pass the runtime will use; that's one
per active `Shadow_render_node`'s shadow atlas.

`Device::warmup_render_pipeline(const Render_pipeline_create_info&)`
in `device.{hpp,cpp}`. Constructs and immediately destructs a
`Render_pipeline`; on Vulkan the resulting binary is retained in the
driver-level `VkPipelineCache` (`Device_impl::m_pipeline_cache`) so
subsequent `vkCreateGraphicsPipelines` calls with the same shader
modules + state tuple skip IR optimization.
`Forward_renderer::Warmup_target` carries the color / depth / stencil
formats, sample count, and usage flags of one render pass the runtime
will use; when `Prewarm_parameters::warmup_targets` is non-empty,
`prewarm_standard_variants` calls `warmup_render_pipeline` once per
`(Render_pipeline_create_info, bucket-variant-key, matching view_count)`
tuple it visits.

### Orchestrator + editor wiring

`prewarm_all(App_context&, init_message)` in
`src/editor/renderers/prewarm.{hpp,cpp}`. Walks every `Scene_root`
returned by `App_scenes::get_scene_roots()`, snapshots its
`Light_layer` counts (via `compute_light_layer_partition`), gathers
content + controller mesh layers, pulls
standard-variant pipelines from the `Composer`, and calls
`Forward_renderer::prewarm_standard_variants`. Then walks every
`Shadow_render_node` returned by `App_rendering::get_all_shadow_nodes()`
and calls `Shadow_renderer::prewarm_pipelines`. Finally calls
`prewarm_variants` on `material_preview` and `brush_preview`. OpenXR
builds prewarm both single-view and multiview view counts (sourced
from `Xr_session::get_view_count()`); single-view builds prewarm only
view_count = 0.

Wired into `editor.cpp` between `run_startup_script()` and the
`close+submit+wait_idle` block. `init_message`, when non-empty, is
invoked once per `Scene_root` with the scene name so
`Init_status_display` can show per-scene progress on the loading
screen. The intended log line is one summary line:

```
prewarm: forward+content-library compiled N new variants (T total),
         warmed K pipeline(s) in M ms;
         shadow prewarm walked S node(s) in P ms;
         previews compiled X new variants (Y total) in Z ms
```

## Quest target

After clean uninstall + reinstall (wipes the on-device SPIR-V cache
and migrated config), the goal is to reach first frame in under five
seconds on Quest 3 with validation layers enabled -- well inside the
30 s OS interstitial budget.

## Known follow-ups

- Pipeline-count reporting in the shadow log line. The current
  message would report the *node count*, not the actual number of
  `vkCreateGraphicsPipelines` calls (which is
  `sum_over_nodes(render_passes.size())`).
- Foveated / quad-view view counts. The orchestrator sources view
  counts from `Xr_session::get_view_count()`; if a Quest config ever
  adds quad-view (4) or foveated rendering paths, the prewarm
  needs to cover them.
- Disabled composition passes are still prewarmed today (one extra
  shader compile per unique `(key, view_count)`). If a future build
  adds many disabled passes wired to feature flags, this may become
  noticeable; revisit and gate.
- Application-level encoder cache (the `m_pipeline_map` keyed on
  `(pipeline_layout, vertex_module, fragment_module, full pipeline
  state, sample_count, color_attachment_count, active_render_pass
  pointer, vertex_input)` inside `Render_command_encoder_impl`)
  cannot be populated at init time because the headset render pass
  is constructed inside the multiview callback in
  `Headset_view::render_headset()` and only exists after the first
  `xrWaitFrame`. `Device::warmup_render_pipeline` populates only the
  driver-level cache; the application-level cache will miss on the
  first bind but the cost is small once the driver cache has the
  binary.
