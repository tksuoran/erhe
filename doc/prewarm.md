# Init-time GPU prewarm

## Why

Quest's launch interstitial gives roughly 30 seconds for the first
`xrEndFrame` call to land. The editor's first frame on a cold install
used to trigger a burst of glslang -> SPIR-V -> `vkCreateShaderModule`
compiles for every standard-shader variant the bucketed forward path
asks for at runtime, plus `vkCreateGraphicsPipelines` calls for each
new render-pass-format combo. The cumulative cost exceeded the budget
and the OS killed the process with `SIGKILL` (signal 9) before the
editor ever submitted a frame, with no abort, no validation message,
and no log line beyond the OS's `Process org.libsdl.app.quest has
died` notice.

The fix is to move that work into the existing init phase, where the
trailing `m_graphics_device->wait_idle()` at the end of init absorbs
any GPU work the prewarm enqueues. The cost shows up at init (around
640 ms total on a fresh install) instead of at first frame.

## Work done

### Phase 1: shader-variant cache (glslang -> SPIR-V -> `vkCreateShaderModule`)

- `Forward_renderer::prewarm_standard_variants(const Prewarm_parameters&)`
  in `src/erhe/scene_renderer/erhe_scene_renderer/forward_renderer.{hpp,cpp}`.
  Walks the same buckets `Forward_renderer::render` would build for
  each `(pipeline, mesh_span)` pair and calls
  `Standard_shader_variants::get_or_compile(key, view_count)` for every
  requested view count. Mirrors the runtime gate exactly:
  `pipeline.uses_standard_variants && pipeline.vertex_format != nullptr`.
  An `extra_materials` span handles content-library materials whose
  meshes are not yet attached; each material is keyed against
  `fallback_vertex_format` with both `mesh_has_skin=false` and
  `mesh_has_skin=true` so a future runtime application of any
  content-lib material to a skinned or static mesh hits the cache.

- `Scene_preview::prewarm_variants(...)` in
  `src/editor/preview/scene_preview.{hpp,cpp}`. Drives
  `prewarm_standard_variants` against the preview's own scene_root +
  content_library. Single-view only (preview render targets are 2D
  offscreen textures). Material_preview and Brush_preview inherit it.

- `compute_standard_variant_light_counts(const erhe::scene::Light_layer&)`
  in `src/erhe/scene_renderer/erhe_scene_renderer/standard_shader_variant.{hpp,cpp}`.
  Single-source-of-truth tally for the six light-count fields the
  variant cache keys on; used by both prewarm sites. The runtime path
  in `Light_projections::apply` keeps its inline tally because it
  reuses the per-type-and-shadow scratch arrays for UBO slot
  assignment, but a comment now flags the constraint that the inline
  tally and the helper must stay bit-identical.

### Phase 2: VkPipeline cache (`vkCreateGraphicsPipelines`)

- `Shadow_renderer::prewarm_pipelines(vertex_input_state, render_passes,
  reverse_depth)` in `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.{hpp,cpp}`.
  Calls the existing `get_pipeline()` to register the matching
  `Lazy_render_pipeline` entry, then invokes
  `Lazy_render_pipeline::get_pipeline_for()` on each supplied
  render-pass descriptor so its format-hashed VkPipeline is
  constructed before the first frame.

- `Shadow_render_node::get_render_passes()` and `get_reverse_depth()`
  accessors in `src/editor/rendergraph/shadow_render_node.{hpp,cpp}`.
  `get_reverse_depth()` returns `m_scene_view.get_reverse_depth()` to
  match the runtime call site in `execute_rendergraph_node` exactly;
  the cached `m_reverse_depth` (last set by `reconfigure`) could
  diverge from the scene_view's live setting and would cause
  prewarm-cache misses.

- `Device::warmup_render_pipeline(const Render_pipeline_create_info&)`
  in `src/erhe/graphics/erhe_graphics/device.{hpp,cpp}`. Constructs a
  discardable `Render_pipeline`; on Vulkan the resulting binary is
  retained in the driver-level VkPipelineCache
  (`Device_impl::m_pipeline_cache`) so subsequent
  `vkCreateGraphicsPipelines` calls with the same shader modules and
  state tuple skip IR optimization. Hooked into the variant walk via
  `Forward_renderer::Warmup_target` (see `forward_renderer.hpp`); each
  target carries the color/depth/stencil formats, sample count, and
  usage flags of one render pass the runtime will use. When
  `Prewarm_parameters::warmup_targets` is non-empty,
  `prewarm_standard_variants` calls `warmup_render_pipeline` once per
  (Lazy_render_pipeline, bucket-variant-key, matching view_count)
  tuple it visits, then returns the warmup count for the prewarm log
  line.

- Editor wiring (`src/editor/renderers/prewarm.cpp`). Sources the
  multiview Quest target from
  `headset_view->get_headset()->get_xr_session()`:
  color/depth-stencil formats picked by
  `Xr_session::enumerate_swapchain_formats()`, sample_count 1 (per
  D6's multiview-MSAA workaround), and usage flags mirroring
  `headset_view.cpp`'s per-frame render pass descriptor. Single-view
  (desktop mirror, main viewport offscreen) warmup is intentionally
  not wired because (a) those render passes are constructed lazily on
  first rendergraph execute and do not exist at this insertion point,
  and (b) the desktop path does not face the 30 s OS interstitial
  budget that motivates the Quest warmup.

### Orchestrator + editor wiring

- `prewarm_all(App_context&)` in `src/editor/renderers/prewarm.{hpp,cpp}`.
  Walks every `Scene_root` returned by `App_scenes::get_scene_roots()`,
  snapshots its `Light_layer` counts, gathers content + controller
  mesh layers, pulls standard-variant pipelines from the Composer,
  and calls `Forward_renderer::prewarm_standard_variants`. Then walks
  every `Shadow_render_node` returned by
  `App_rendering::get_all_shadow_nodes()` and calls
  `Shadow_renderer::prewarm_pipelines`. Finally calls
  `prewarm_variants` on `material_preview` and `brush_preview`.
  OpenXR builds prewarm both `view_count = 0` (single-view mirror)
  and `view_count = 2` (multiview headset); single-view builds prewarm
  only 0.

- `App_rendering::composition_passes()` accessor in
  `src/editor/app_rendering.{hpp,cpp}`. Returns the Composer's
  passes vector by const-reference for init-time use; comment
  documents that the accessor does NOT take the Composer's mutex and
  must not be called from the render thread.

- Wired into `src/editor/editor.cpp` between `run_startup_script()`
  and the `close+submit+wait_idle` block. Logs a single summary line:

  ```
  prewarm: forward+content-library compiled N new variants (T total) in M ms;
           shadow prewarm walked K node(s) in P ms;
           previews compiled X new variants (Y total) in Z ms
  ```

### Quest verification

After clean uninstall + reinstall (wipes the on-device SPIR-V cache
and migrated config), the editor reaches first frame in around 3.2
seconds on Quest 3 with validation layers enabled, well inside the
30 s OS interstitial budget. Prior to the prewarm, the same setup
hung past 25 seconds and was SIGKILL'd. Logged numbers from a
representative run: 12 forward+content variants in 565 ms, 1 shadow
node in 34 ms, 1 preview variant in 43 ms.

## Remaining work

### Phase 2 application-level cache (still not addressed)

The runtime forward path takes one of two routes per pipeline:

1. **Lazy_render_pipeline path** (single-view non-variant, used when
   `parameters.render_pass != nullptr` and no override): goes through
   `Lazy_render_pipeline::get_pipeline_for(desc)`, which constructs a
   `Render_pipeline` and caches it in `m_variants` keyed by format
   hash. Step 2 covers this for the shadow renderer.

2. **Override path** (multiview AND/OR per-bucket variant): goes
   through `Render_command_encoder_impl::set_render_pipeline_state`,
   which keys an *application-level* `m_pipeline_map` cache on
   `(pipeline_layout, vertex_module, fragment_module, full pipeline
   state, sample_count, color_attachment_count, active_render_pass
   pointer, vertex_input)`. Populating this cache requires an active
   render pass identical to the one the runtime will use. On Quest
   the headset render pass is constructed inside the multiview
   callback in `Headset_view::render_headset()` from
   `Render_views_frame::shared_color_texture` /
   `shared_depth_stencil_texture`, which only exist after the first
   `xrWaitFrame`. So we still cannot populate this cache at init
   time.

`Device::warmup_render_pipeline` is now wired (see Phase 2 above) and
populates the *driver-level* VkPipelineCache
(`Device_impl::m_pipeline_cache`); the runtime
`vkCreateGraphicsPipelines` calls reuse it for IR-optimization work
even when the application-level cache misses on the first bind. The
remaining gap is the application-level cache, which would require a
dummy render pass + encoder at init (the heavyweight option from the
original plan). Quest currently launches inside the 30 s budget so
this is "nice to have", not a blocker; measure first.

### Other follow-ups

- Pipeline count in the shadow log line. The current message reports
  the *node count*, not the actual number of `vkCreateGraphicsPipelines`
  calls (which is `sum_over_nodes(render_passes.size())`). Tighten the
  log so the diagnostic shows pipeline counts.
- `Standard_variant_light_counts` divergence detector. Optional debug
  assertion that compares the runtime tally in
  `Light_projections::apply` against
  `compute_standard_variant_light_counts` once per frame; would catch
  any future drift between the two sites loudly.
- Foveated / quad-view view counts. The orchestrator hardcodes
  `view_counts = {0, 2}` for OpenXR. The forward renderer reads
  `parameters.multiview_views.size()` directly and could in principle
  take any value; if Quest ever adds quad-view (4) or foveated
  rendering paths, the prewarm would miss them. Easy fix once the
  values become reachable: query `Headset_view` for the active view
  count list.
- Disabled composition passes are intentionally still prewarmed today
  (one extra glslang compile per unique `(key, view_count)`). If a
  future build adds many disabled passes wired to feature flags, this
  may become noticeable; revisit and gate.
