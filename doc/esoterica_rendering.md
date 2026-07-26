# Esoterica vs. erhe rendering — comparison and parity plan

Sources: survey of `C:/git/tksuoran/Esoterica` (HEAD `cf36499`) and the current erhe
tree. Esoterica is Bobby Anguelov's engine: a Direct3D 12-only, bindless,
GPU-driven, clustered-forward renderer built around mesh shaders (SM 6.6
minimum). erhe is a multi-backend (Vulkan/OpenGL/Metal/null) engine with a
Vulkan-style device abstraction, a render graph, and a shader-variant-based
forward renderer.

The two engines make opposite foundational bets:

| Axis | Esoterica | erhe |
|---|---|---|
| Portability | None — D3D12 + SM 6.6 + mesh shaders required | Vulkan, OpenGL (down to 4.1), Metal, headless null; Android/Quest |
| Draw submission | Fully GPU-driven (compute culling → `ExecuteIndirect` mesh dispatch); zero per-instance CPU work | CPU builds indirect command lists per frame; `multi_draw_indexed_primitives_indirect` per pipeline |
| Binding model | Bindless-only (SM 6.6 `ResourceDescriptorHeap`); no vertex buffers or input layouts at all | Four texture-heap strategies per backend; classic vertex input; UBO/SSBO binding points |
| Shaders | HLSL, offline DXC compile, fixed permutation set (~4 per material shader), all PSOs at init | GLSL, runtime glslang→SPIR-V with disk cache, large variant space (24 bool + 20 int axes), on-demand compile + prewarm + hot reload |
| Pass structure | Hand-written vertically-integrated pass list, manual (assisted) barriers | Render graph DAG + render passes with declared usage/layout before/after |

Because of this, "parity" below means *visual/feature* parity, not architectural
convergence: erhe should gain Esoterica's image quality features (IBL, GTAO,
tiled lights, cascades-quality shadows at scale, post-AA, culling/LOD) on top of
erhe's own abstractions, not adopt D3D12-style bindless mesh shading wholesale.

---

## 1. Feature comparison

Legend: ✅ present, 🟡 partial/different approach, ❌ absent.

### Shading and lighting

| Feature | Esoterica | erhe | Notes |
|---|---|---|---|
| Shading path | ✅ Clustered forward (forward+) with depth prepass | 🟡 Plain forward, no depth prepass | erhe light counts are shader-variant constants, max 32 lights |
| Tiled/clustered light culling | ✅ Compute, bucket texture bitmask, scalarized iteration (`LightCulling.esf`) | ❌ | Biggest scalability gap for many lights |
| PBR material model | ✅ Cook-Torrance GGX/Smith/Schlick (`PBR.esh`) | ✅ GGX incl. anisotropic BRDF variants (`erhe_bxdf.glsl`, `erhe_ggx.glsl`) | erhe's material model is richer (anisotropy, blending modes) |
| Light types | ✅ Dir/point/spot, physical units (Kelvin, max intensity) | ✅ Dir/point/spot (`erhe::scene::Light`) | erhe lacks physically-based intensity/temperature parameterization |
| IBL / environment lighting | ✅ Split-sum: engine-rendered global env cubemap, DFG LUT, radiance + irradiance prefilter | ❌ Flat ambient term only | erhe has a BRDF-slice visualization but no IBL |
| Ambient occlusion | ✅ XeGTAO (prefilter/main/denoise, optional half-res + bilateral upsample, async compute) | ❌ Occlusion texture only | |
| Shadow maps — directional | ✅ 4-cascade CSM, 4096², optimized PCF, normal-offset bias, cascade blending | 🟡 Single tight-fit map per light (rotating-calipers frustum fit, texel snap, PCF 2/4/6, receiver-plane bias, distance-map technique) | Different philosophy; erhe quality is good near-field but has no cascade fallback for large ranges |
| Shadow maps — point/spot | 🟡 Fields exist, no pass | ✅ Spot in 2D array; point via cube-map array (radial distance) | erhe is *ahead* here |
| Transparency | ✅ Separate alpha-blend pass, alpha-test permutation, 1-layer ROV OIT (debug only) | ✅ Translucent composition passes, 7 blending modes, screen-door | Rough parity |
| Skybox / atmosphere | ❌ (env map only) | ✅ Hillaire atmosphere w/ compute LUTs + gradient sky | erhe ahead |

### GPU pipeline and geometry

| Feature | Esoterica | erhe | Notes |
|---|---|---|---|
| GPU-driven culling | ✅ Instance → cluster (frustum + backface cone + per-triangle) → bucket resolve, all compute | ❌ No scene frustum culling at all (Item_filter + layers only; shadow-caster culling exists) | Largest architectural gap |
| Meshlets/clusters | ✅ 64-vert/64-tri clusters, compressed vertices, mesh-shader decompression | ❌ Classic vertex/index buffers | |
| LOD | ✅ Distance-based, GPU-selected, dither cross-fade, auto-generation at import | ❌ | |
| Instancing | ✅ GPU-resident instance data, `WorldUpdate` compute applies transform commands | 🟡 `gl_DrawID`→primitive SSBO; dedicated `Cube_renderer` instanced path | |
| Skinning | ✅ GPU, in mesh shader, 4 influences | ✅ GPU, joint matrix SSBO, vertex shader | Parity |
| Vertex compression | ✅ 32 B static / 64 B skinned, shared-exponent positions | ❌ Full-fat attributes (multi-stream flexible `Vertex_format`) | erhe trades size for flexibility |
| Occlusion culling | ❌ (no Hi-Z) | ❌ | Neither engine |

### Post-processing and AA

| Feature | Esoterica | erhe | Notes |
|---|---|---|---|
| Tonemapping | ✅ Tony McMapface 3D LUT | ✅ ACES + log closed-form (`erhe_tonemap.glsl`) | Parity (different curves) |
| Bloom | ❌ | ✅ Pyramidal 13-tap down / tent up | erhe ahead |
| Anti-aliasing | ✅ SMAA | 🟡 MSAA + alpha-to-coverage | MSAA is costly with HDR + heavy shading; no post-AA in erhe |
| DOF / motion blur | ❌ | ❌ | Neither |

### Infrastructure

| Feature | Esoterica | erhe | Notes |
|---|---|---|---|
| Render graph | ❌ Hand-written pass list | ✅ `Rendergraph` DAG | erhe ahead |
| Barrier/state tracking | 🟡 Manual, assisted (`DeviceResourceStates`) | ✅ Declarative usage/layout before/after on attachments | |
| Async compute | ✅ GTAO on compute queue, timeline-semaphore cross-queue sync | ❌ Single queue | |
| Multi-queue transfer | ✅ Dedicated transfer queue, staging suballocator | 🟡 Ring buffers + `Buffer_transfer_queue` on graphics queue | |
| Frames in flight | 2, deferred destruction countdown | Frames-in-flight ring buffers + `Device_sync_pool`, adaptive frame pacing (`Frame_pacer`) | erhe's frame pacing is far more advanced |
| Bindless textures | ✅ SM 6.6 heap, mandatory | ✅ `Texture_heap` (4 strategies) | Parity in effect |
| GPU buffer suballocation | ✅ Page/Handle allocators, TLSF staging, auto-resize, GPU append buffers | ✅ `Buffer_pool` + `Free_list_allocator` ring buffers | Parity; erhe lacks GPU append-buffer readback helper |
| Picking | ✅ GPU compute resolve + append-buffer readback | ✅ ID render + async ring readback, hybrid CPU raytrace | Parity (different mechanics) |
| Debug draw | ✅ CPU + **in-shader** debug draw (any shader can emit lines) | 🟡 Rich CPU debug renderer (3 wide-line paths, Jolt adapter, text); no in-shader debug draw | |
| Debug visualization modes | ✅ 11 viewport modes | ✅ 32 `Shader_debug` modes | Parity |
| ImGui | ✅ Multi-viewport, image cache | ✅ Multi-host incl. in-scene render targets, node editor | Parity+ |
| Shader hot reload | ✅ `CompileShaders.bat` live reload | ✅ `Shader_monitor` background poll | Parity |
| GPU profiling | ✅ Timestamp queries, DRED breadcrumbs, PIX/RenderDoc hooks | ✅ `Gpu_timer` per pass, RenderDoc, watchdog breadcrumbs | Parity |
| XR / multiview | ❌ | ✅ OpenXR, VK multiview, fragment density map | erhe ahead |
| Mesh shaders / VRS / RT | ✅ MS required; VRS in RHI; RT placeholder | ❌ (CPU raytrace for picking only) | |

### Where erhe is already ahead

Multi-backend portability, render graph, frame pacing, point-light cube shadows,
shadow frustum fitting, bloom, procedural atmosphere, XR/multiview, text and
edge-line rendering, geometry processing (Geogram, Conway ops, subdivision, CSG),
glTF import/export, MSAA infrastructure, flexible vertex formats, texture graph.

### Where Esoterica is ahead (the parity targets)

1. IBL (split-sum environment lighting)
2. GTAO screen-space ambient occlusion
3. Depth prepass
4. Tiled/clustered light culling (many-light scalability)
5. Post-process AA (SMAA)
6. Frustum culling and LOD (any culling at all for the main view)
7. Cascaded shadow maps for large view ranges
8. GPU-driven submission (instance/cluster culling, GPU instance data)
9. Async compute
10. In-shader debug draw
11. Physical light units
12. Vertex/mesh compression, meshlets

---

## 2. API translation mapping

Esoterica RHI is free functions over opaque structs (`EE::Render::RHI::*`);
erhe is classes in `erhe::graphics`. Closest-equivalent mapping:

### Device and frame

| Esoterica (`Code/Base/Render/RHI.h`) | erhe (`src/erhe/graphics/erhe_graphics/`) | Notes |
|---|---|---|
| `RHI::Context` / `CreateContext(ContextParameters)` | `Device` ctor (`device.hpp`) with `Surface_create_info`, `Graphics_config` | |
| `ContextParameters::m_enableHostValidation/...` | `Graphics_config` (generated from `definitions/*.py`) | |
| `DeviceCapabilities` | `Device_info` + `Format_properties` | Both expose per-format capability queries |
| `Queue` (graphics/compute/transfer) + timeline semaphore | Single implicit queue inside `Device`; `Command_queue` type exists but unused | erhe has no multi-queue |
| `QueueSubmit(queue, cmdBuffers)` | `Device::submit_command_buffers(span)` | |
| `QueuePresent(queue, swapchain, image)` | Implicit present inside `submit_command_buffers` for cbs that ran `begin_swapchain` | erhe deliberately has no explicit present |
| `QueueHostWait(queue, value)` | `Device::wait_frame()` (pace) / `wait_idle()` | |
| `QueueDeviceWait(waiter, target, value)` | `Command_buffer::wait_for_semaphore/fence` | erhe: per-cb sync, not per-queue |
| `RHI::MaxPendingFrames` (2) | Frames-in-flight in `Device` + `Ring_buffer::frame_completed()` | |
| `RenderSystem::QueueResourceDelete(...)` | `Device::add_completion_handler(fn)` + RAII destruction | |
| `AcquireNextImage(ctx, swapchain)` | `Command_buffer::wait_for_swapchain` + `begin_swapchain` | |
| `Swapchain` / `SwapchainParameters` | `Surface` / `Swapchain` (`surface.hpp`, `swapchain.hpp`) | |
| `BeginFrameCapture/EndFrameCapture` | `Device::start_frame_capture()/end_frame_capture()` | Both RenderDoc |
| `GetDetailedMemoryStatistics` | (VMA stats; no public API) | Gap: erhe lacks a memory statistics query |

### Command recording

| Esoterica | erhe | Notes |
|---|---|---|
| `CommandPool` / `CommandBuffer` / `BeginCommandBuffer` | `Device::get_command_buffer(thread_slot)` → `Command_buffer::begin()/end()` | erhe pools are per (frame, thread slot), managed internally |
| `CmdSetRenderTargets(cb, rts, depth, LoadAction, slices, mips)` | `Render_pass_descriptor` attachments (`texture_level/layer`, `Load_action`, `Store_action`, clear values) + `Scoped_render_pass` | erhe declares at pass creation, not per command |
| `LoadActionType` / `StoreActionType` | `Load_action` / `Store_action` (incl. `Multisample_resolve`) | erhe adds MSAA resolve store actions |
| `CmdSetViewport` / `CmdSetScissor` | `Render_command_encoder::set_viewport_rect`, `set_viewport_depth_range`, `set_scissor_rect` | |
| `CmdSetPipeline(cb, pipeline)` | `set_render_pipeline` / `set_compute_pipeline` (via encoders) | |
| `CmdSetRootConstants` / `CmdSetRootParameter` | No push constants — `Ring_buffer_client::bind(encoder, range)` per-draw UBO/SSBO ranges | Root constants ≈ small ring-buffer UBO ranges |
| `CmdSetIndexBuffer` | `Render_command_encoder::set_index_buffer` | |
| (no vertex buffers — bindless) | `set_vertex_buffer(buffer, offset, index)` + `Vertex_input_state` | Fundamental model difference |
| `CmdSetStencilReference` | Stencil reference in `Depth_stencil_state` (static in pipeline) | |
| `CmdDraw*` / `CmdDrawIndexed*Instanced` | `draw_primitives` / `draw_indexed_primitives` (+ instance_count) | |
| `CmdExecuteIndirect(sig, maxCount, argBuf, counterBuf)` | `multi_draw_indexed_primitives_indirect(type, index_type, offset, drawcount, stride)` | erhe: fixed draw-indexed signature, CPU drawcount, no counter buffer, no compute/state-change indirect |
| `CmdDispatchCompute` | `Compute_command_encoder::dispatch_compute(x,y,z)` | |
| `CmdDispatchMesh` | — | No mesh shaders in erhe |
| `CmdBarrier(...)` (global/buffer/texture, enhanced-barrier model) | `usage_before/after` + `layout_before/after` on attachments; `Memory_barrier_mask`; backends derive transitions | erhe: declarative; no arbitrary mid-pass buffer barrier API exposed |
| `CmdClearTexture` / `CmdClearBuffer` | `Load_action::Clear`; `Blit_command_encoder::fill_buffer` | |
| `CmdCopyBuffer` / `CmdCopyTexture` (both directions) | `Blit_command_encoder::copy_from_buffer/copy_from_texture` overloads | |
| `CmdBegin/EndDebugMarker`, `EE_RHI_COMMAND_BUFFER_PROFILE_SCOPE` | `Scoped_debug_group`, `Scoped_gpu_zone` | |
| `QueryPool`, `CmdBegin/End/ResolveQuery` | `Gpu_timer` (+ `Render_pass::register_gpu_timer`) | erhe wraps timestamps only |
| `CmdWriteDebugMarker` (breadcrumbs) | Watchdog breadcrumbs (editor-level) | |
| `CmdSetShadingRate` | — | No VRS in erhe |

### Resources

| Esoterica | erhe | Notes |
|---|---|---|
| `Buffer` / `CreateBuffer(BufferParameters)` | `Buffer` / `Buffer_create_info` (`buffer.hpp`) | Both VMA-style memory flags |
| `MapBuffer` / `m_pMappedAddress_WriteCombined` | `map_bytes/map_all_bytes/begin_write/end_write`, persistent mapping gated on `Device_info::use_persistent_buffers` | |
| `GetBufferHandle(buffer, DescriptorTypeFlags)` | SSBO/UBO binding points via `Bind_group_layout` + `set_buffer` | erhe buffers are bound, not handle-addressed |
| `BufferSubAllocate` (TLSF) | `Free_list_allocator` / `Buffer_allocation` (`src/erhe/buffer/`) | |
| `DeviceResizeBuffer` | `Buffer_pool` lazy block growth | |
| `DeviceAppendBuffer<T>` (GPU append + readback) | — (nearest: `Ring_buffer` CPU_read `Pending_read` path) | Gap: no atomic-counter append + readback helper |
| `PageAllocator` / `HandleAllocator` | `Buffer_pool` / `Format_pools` | |
| `Texture` / `CreateTexture(TextureParameters)` | `Texture` / `Texture_create_info` | erhe adds texture views, wrapped external textures, sparse |
| `GetTextureHandle(tex, flags, mip)` (bindless SRV/UAV) | `Texture_heap::allocate(texture, sampler)` → `uint64_t` handle; storage image via `set_storage_image` | Per-mip UAV handles have no erhe equivalent (storage image binds a level) |
| `Sampler` / `GetSamplerStateHandle` | `Sampler` / `Sampler_create_info`; heap handles combine texture+sampler | Esoterica: separate sampler heap; erhe: combined image-sampler |
| Common samplers (6, static in root sig) | Editor-level sampler objects; `Imgui_renderer` sampler pool | |
| `Shader` (≤2 stage bytecode blobs) | `Shader_stages` (+ `Shader_stages_prototype`) | Esoterica: offline DXIL; erhe: runtime GLSL→SPIR-V |
| `ShaderReflection` (from bytecode) | None needed — `Shader_resource`/`Bind_group_layout` *generate* the interface | Inverted direction: erhe synthesizes declarations, Esoterica reflects them |
| `RootSignature` (reflected + static samplers) | `Bind_group_layout` | Same role: binding contract |
| `Pipeline` (Graphics/Mesh/Compute/RT params) | `Render_pipeline` (via `Render_pipeline_create_info`), `Compute_pipeline` | |
| `PipelineCache` + `GetPipelineCacheData` | `spirv_cache/` on disk + driver `VkPipelineCache` via `warmup_render_pipeline` | |
| All PSOs created at init (hard rule) | On-demand `Base_render_pipeline::get_pipeline_for(...)` + prewarm (`doc/prewarm.md`) | Opposite policies |
| `CommandSignature` | — (fixed indirect layout) | Needed only for indirect-with-root-constant patterns |
| `BlendState`/`DepthStencilState`/`RasterizerState` | `Color_blend_state`/`Depth_stencil_state`/`Rasterization_state`/`Multisample_state`/`Input_assembly_state` (`state/`) | |
| `DataFormat` (~150, DXBC/ASTC) | `erhe::dataformat::Format` (~70) | Gap: erhe formats lack BC/ASTC compressed-texture entries in the pipeline (loads uncompressed) |
| `SetDebugName` (mandatory) | `erhe::utility::Debug_label` on every resource | |
| `AccelerationStructure` / `CmdDispatchRays` (placeholder) | `erhe::raytrace` (CPU: embree/bvh/tinybvh) | Different purposes |

### Engine layer

| Esoterica | erhe | Notes |
|---|---|---|
| `RenderSystem` (device owner, shader registry, staging, async creates) | `Device` + editor `App_rendering` + `Programs` + `Mesh_memory` | |
| `ForwardShadingRenderer` | `erhe::scene_renderer::Forward_renderer` + editor `Composer`/`Composition_pass` | |
| `RenderWorldSystem` / `DeviceRenderWorld` | `erhe::scene::Scene` + `Mesh_layer`/`Light_layer` + per-frame buffer fills (`Primitive_buffer` etc.) | Esoterica keeps the world GPU-resident; erhe re-uploads per frame from ring buffers |
| `RenderViewport` (all per-view targets) | `Viewport_scene_view` + `Render_target` + rendergraph nodes | |
| `RenderPass_*` classes | `Rendergraph_node` subclasses + `Composition_pass` | |
| `CascadedShadowPass` | `Shadow_render_node` / `Shadow_renderer` | |
| `RenderPass_PostProcess` (tonemap) | `Post_processing_node` (bloom + tonemap) | |
| `ImguiRenderer` | `erhe::imgui::Imgui_renderer` + hosts | |
| `DebugDrawingSystem` + `RenderPass_DebugDraw` | `erhe::renderer::Debug_renderer` + `Text_renderer` | |
| `ViewportPicking` / `InstancePickingResolve.esf` | `editor::Id_renderer` | |
| `MaterialShaderParametersInstance` (persistent GPU material params, 32 B blocks) | `Material_buffer` re-uploaded per frame | Esoterica: persistent; erhe: per-frame ring upload |
| `SkinningProxy::WriteTransforms` | `Joint_buffer` fill | |
| Shader types Material/Surface/Compute + `.esf/.esh` + reflector | `Shader_stages_create_info` + variant `Shader_key` + `#include` GLSL | |
| `MeshBuilder::BuildAndAppendClusters` (import-time clustering) | `Primitive_builder` (`erhe_primitive`) | |
| `EE::Render::Mesh/StaticMesh/SkeletalMesh` | `erhe::scene::Mesh` + `erhe::primitive::Primitive`/`Buffer_mesh` | |
| `EE::Render::Material` (shader + param storage) | `erhe::primitive::Material` | |
| `TextureResource` + compilers (BC/ASTC, channel packing) | `Texture` + wuffs/mango loaders + `Image_transfer` | Gap: no offline texture compiler / block compression in erhe |

---

## 3. Incremental parity plan

Ordered so each step is independently shippable, earlier steps unlock later
ones, and everything stays within erhe's existing architecture (render graph,
GLSL variants, multi-backend). Per-step: what, where it hooks in, and why this
order.

### Phase 1 — Image quality quick wins

**1.1 Depth prepass** (small)
- Add a `VARIANT_DEPTH_ONLY` prepass `Composition_pass` for opaque content in
  `App_rendering`, and switch the opaque fill passes to `Compare_operation`
  equal with depth writes off. The variant already exists (used by shadows).
- Prerequisite for GTAO and tiled light culling (both consume depth before
  shading); also cuts overdraw cost of the heavy fragment shader.

**1.2 Physical light units** (small)
- Add intensity-in-lumens/lux and color-temperature (blackbody Kelvin → RGB) to
  `erhe::scene::Light` and `Light_buffer`, mirroring Esoterica's
  `m_temperature`/`m_maxIntensity`/`m_tint` and its `AttenuationNoCusp`
  radius falloff. Purely additive; improves content compatibility and makes IBL
  calibration (1.4) meaningful.

**1.3 GTAO** (medium)
- Port XeGTAO (as Esoterica did: prefilter depth → main → denoise, optional
  half-res + bilateral upsample) to GLSL compute. erhe already has
  `Compute_command_encoder`, storage images (used by sky LUTs), and the render
  graph to slot a `Gtao_rendergraph_node` between the depth prepass and the
  forward pass. Output an AO texture sampled in `standard.frag`
  (`ao * material occlusion`), plus a `Shader_debug` mode.
- Fallback: skip on devices without compute (GL 4.1) — flat AO=1, same pattern
  as the sky LUT and wide-line compute paths.

**1.4 IBL — split-sum environment lighting** (medium-large)
- Steps, all with existing machinery (cube-map array render passes exist for
  point shadows; compute exists for LUTs):
  a. DFG BRDF LUT compute (one-time).
  b. Capture the scene (or just the sky, first increment) into a cubemap;
     the `Sky_renderer` output makes an excellent first light source.
  c. Radiance prefilter (per-mip roughness) + irradiance convolution compute.
  d. Replace the flat `ambient_light` term in `standard.frag` with
     diffuse-irradiance + specular-prefiltered + DFG.
- Rendergraph node `Environment_map_node` producing `radiance`/`irradiance`
  outputs consumed by `Viewport_scene_view`. Recapture on demand (sun moved,
  explicit invalidation), not per frame.
- This is the single largest visual-quality jump on the list.

### Phase 2 — Scalability

**2.1 CPU frustum culling** (small)
- `Buffer_mesh` already stores `bounding_box`/`bounding_sphere`; cull in
  `bucket_primitives()`/`Draw_indirect_buffer` against the camera frustum
  before emitting indirect commands. The shadow fit already does plane/AABB
  tests (`light_frustum_fit.cpp`) — reuse those helpers.
- Cheap, immediate, and later becomes the reference implementation the GPU
  path (3.1) is validated against.

**2.2 Tiled light culling → forward+** (medium-large)
- Compute pass over the prepass depth: per-tile light index lists in an SSBO
  (Esoterica uses a bucket-bitmask texture; an index-list SSBO is simpler and
  fits erhe's binding model). `standard.frag` iterates the tile's list instead
  of a fixed variant-constant light count.
- This collapses the six `LIGHT_COUNT_*` variant axes (a large chunk of the
  variant explosion) and removes the 32-light ceiling. Keep the variant path as
  the non-compute fallback.

**2.3 Cascaded shadow maps** (medium)
- Extend `Shadow_renderer` to N cascades per directional light: split the view
  range, run the existing tight-fit per cascade (the fitting code is already
  per-light and modular), store in the existing depth `texture_2d_array`
  (one layer per cascade), add cascade selection + blend in
  `erhe_light.glsl`/`sample_light_visibility()`. Esoterica reference:
  `RenderPass_CascadedShadow` + `ShadowMapSampling.esh` (4-tap optimized PCF,
  normal-offset bias, smoothstep cascade blend).
- erhe's tight fit becomes the per-cascade fit — combining both engines'
  strengths.

**2.4 SMAA** (medium)
- Port the standard SMAA 3-pass chain (edge detect → blend weights →
  neighborhood blend) as a rendergraph node between `Viewport_scene_view` and
  `Post_processing_node`; ship the area/search LUTs as assets like Esoterica's
  `Data/Render/SMAA`. Offer it as an alternative to MSAA (big win for HDR +
  heavy shading + mobile/Quest where MSAA resolve bandwidth hurts).

### Phase 3 — GPU-driven submission

Each step keeps the CPU path as fallback for non-compute backends.

**3.1 GPU frustum culling of draws** (medium)
- Move 2.1 to compute: upload all candidate draw commands + bounds, cull in a
  compute pass writing compacted commands and a count. Requires
  `multi_draw_indirect_count` (GL 4.6 `GL_ARB_indirect_parameters` /
  `VK_KHR_draw_indirect_count`) — add `Device_info::use_draw_indirect_count`
  and a `Render_command_encoder::multi_draw_indexed_primitives_indirect_count`
  taking a count buffer. This is the one genuine RHI addition the plan needs.

**3.2 Persistent GPU scene data** (medium-large)
- Stop re-uploading `Primitive_buffer`/`Material_buffer` every frame: keep
  persistent GPU-resident primitive and material arrays (erhe already has
  `Free_list_allocator` for stable slots) updated by dirty-tracking uploads —
  Esoterica's `MaterialShaderParametersInstance`/`WorldUpdate` model, minus the
  command-buffer compute (CPU dirty writes into a staging ring are fine at
  erhe's scene sizes). Frees CPU frame time and is a prerequisite for making
  3.1 profitable at scale.

**3.3 LOD** (medium)
- Per-`Mesh_primitive` LOD chain (author-provided or meshoptimizer-simplified
  at import in `erhe_primitive`/glTF load), distance selection inside the 3.1
  culling pass (Esoterica: `InstanceCulling.esf` `ReadLODDistance`), optional
  screen-door cross-fade using the existing `screen_door` blending mode.

**3.4 GPU append buffer + readback helper** (small)
- Generalize the `Id_renderer` ring-readback into a reusable
  `Gpu_append_buffer` (atomic counter SSBO + N-frame readback ring), matching
  Esoterica's `DeviceAppendBuffer`. Unlocks 3.5 and GPU-side stats.

**3.5 In-shader debug draw** (small, high leverage)
- A `debug_draw_line/point/box()` GLSL include writing to the append buffer
  from any shader; drain into `Debug_renderer` on readback. One of Esoterica's
  best developer-experience features (`Docs/Rendering/AppendBuffer.md`) and
  nearly free once 3.4 exists.

### Phase 4 — Beyond (optional, matching Esoterica's frontier)

- **4.1 Async compute**: second Vulkan queue for GTAO/LUT work
  (`Command_queue` exists; needs queue selection + cross-queue timeline waits
  in `Command_buffer`). Esoterica pattern: split at depth-downsample, join
  before shading.
- **4.2 Compressed textures**: BC/ASTC formats in `erhe::dataformat`, KTX2
  loading, offline transcode — closes the texture-pipeline gap.
- **4.3 Vertex compression**: optional quantized `Vertex_format` streams
  (Esoterica: shared-exponent uint16 positions, snorm16 normals) — erhe's
  multi-stream `Vertex_format` supports this without structural change.
- **4.4 Meshlets/mesh shading**: only worth it after 3.x; would ride
  `VK_EXT_mesh_shader` with the 3.1 compute path as the fallback, cluster
  build at import in `Primitive_builder`.

### Sequencing rationale

Phase 1 maximizes visual return per line of code using machinery erhe already
has (compute, cube maps, render graph). Phase 2 removes the two hardest scaling
walls (light count, draw count) and the shadow-range limitation. Phase 3 is the
architectural investment toward Esoterica's GPU-driven model, staged so every
step ships with a fallback. Dependencies: 1.1 → 1.3, 2.2; 1.4 builds on the sky;
2.1 → 3.1 → 3.3; 3.4 → 3.5. Independent and can be done anytime: 1.2, 2.3, 2.4,
3.2, and all of phase 4 except 4.4.
