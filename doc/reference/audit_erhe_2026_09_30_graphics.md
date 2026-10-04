# erhe GPU foundation audit

Read-only architectural and API audit, 2026-09-30, at commit 7445e65e6 (main).
Scope: src/erhe/graphics (the neutral API and the Vulkan, OpenGL, Metal and
null backends), src/erhe/gl, src/erhe/dataformat, src/erhe/buffer,
src/erhe/circular_ring_buffer, src/erhe/codegen, the shader tree under
res/shaders, and the documents listed in the slice brief. Every claim below was
verified by reading the referenced lines; nothing was built or run. Counts come
from grep over the slice, excluding tests and vendored headers.
doc/erhe/layout.md is listed in the brief but describes erhe::scene layout
nodes (its "Key files / symbols" names src/erhe/scene/erhe_scene/layout.hpp).

Slice size (non-test .cpp/.hpp, wc -l): graphics 63.4k (neutral API 15.0k +
state/ 1.1k, vulkan 22.8k, gl 13.1k, metal 8.7k, null 2.8k), gl 1.8k plus
generated wrappers, dataformat 3.0k, buffer 0.5k, circular_ring_buffer 0.4k,
codegen 0.8k C++ plus 2.0k Python; res/shaders 42 files, 6.0k lines.

--------------------------------------------------------------------------------
## 1. Architecture

### 1.1 Layering: one library, four backends, compile-time selection

- Every public type is a `final` class holding `std::unique_ptr<X_impl>`
  (Device: device.hpp:424-731, Buffer: buffer.hpp:29-83, Texture:
  texture.hpp:76-165, Render_pass: render_pass.hpp:98-148); the impl type is
  chosen by `#if defined(ERHE_GRAPHICS_API_*)` includes in each neutral .cpp
  (device.cpp:11-22, texture.cpp:5-16, render_pass.cpp:3-14). No virtual
  dispatch, no runtime backend choice (doc/plans/wasm_webgpu_port.md "Ground
  truth").
- The encoders avoid the heap: all three hold their impl in
  `erhe::utility::pimpl_ptr<Impl, 128, 16>` (render_command_encoder.hpp:86,
  compute_command_encoder.hpp:69, blit_command_encoder.hpp:60), `Texture_heap`
  in a 512-byte one (texture_heap.hpp:63); `make_render_command_encoder`
  returns by value without allocating (device.cpp:235-238).
- Below graphics: `erhe::dataformat` (dataformat.hpp:22-138),
  `erhe::circular_ring_buffer` (circular_ring_buffer_algorithm.hpp:11-84),
  `erhe::buffer` (free_list_allocator.hpp:19-53), `erhe::gl` (generated,
  src/erhe/gl/CMakeLists.txt:11-30), `erhe::codegen` (config structs,
  src/erhe/graphics/CMakeLists.txt:404-430). Above: 15 CMake targets link
  `erhe::graphics`; scene_renderer (49 files) is the heaviest includer.
- The infra report's split proposal is the right lever: every backend header
  includes device.hpp (vulkan_device.hpp:4), which pulls in shader_monitor.hpp
  (`<thread>`, `<mutex>`, shader_monitor.hpp:6-8), swapchain.hpp, surface.hpp,
  spirv_cache.hpp, the generated config, frame_time_recorder.hpp and glm via
  math_util.hpp (device.hpp:3-15); one change rebuilds everything.

Verdict: clean pimpl layering under one wide, heavy central header.

### 1.2 Backend abstraction quality

Vulkan-isms in the neutral API (documented as intentional, doc/erhe/graphics.md
"Notes"): `Image_layout` (enums.hpp:516-527), `Image_usage_flag_bit`
(:468-495), `Memory_allocation_create_flag_bit`, which is the VMA flag list
with `strategy_min_offset` and `can_alias` (:334-347),
`Memory_property_flag_bit` (:364-373), `Shader_stage_flags` ("Mirrors
VkShaderStageFlags. Only the Vulkan backend honors it", :232-248),
`Render_pass_descriptor::view_mask` and `fragment_density_map_texture`
("Ignored on non-Vulkan backends", render_pass.hpp:77-92), two raw `uint64_t`
memory masks in `Buffer_create_info` (buffer.hpp:19-22),
`Device::probe_image_format_support` ("Vulkan-specific", device.hpp:630-637).
`Command_buffer::transition_texture_layout` (command_buffer.hpp:104) has 105
external call sites, so layouts are a caller concern on every backend.

GL-isms: `Memory_barrier_mask` values are the literal GL bits
(enums.hpp:389-406), converted by a `static_cast` with "TODO Proper
conversion" (gl_device.cpp:1461-1464); `Glsl_type` uses GL sampler names
(enums.hpp:31-91); `final_source` takes an `std::optional<unsigned int> gl_name`
(shader_stages.hpp:53-59); `Device_info` carries `use_direct_state_access`,
`use_binary_shaders`, `use_multi_draw_indirect_arb` (device.hpp:194-207) and
`#ifdef`-only GL and Vulkan fields (device.hpp:142-185), so the public struct
differs per backend; `Surface::get_color_format` exists only under
`ERHE_GRAPHICS_API_VULKAN` (surface.hpp:38-45), `compile_glslang` only under
`ERHE_SPIRV` (shader_stages.hpp:109-111).

`#ifdef` density: 131 `ERHE_GRAPHICS_API_*` / `ERHE_SPIRV` lines in the
neutral sources, 4 per file being include selectors (26 files); behavioral
ones at device.cpp:41-45 (constructor signature), :81-99 (frame capture),
:134-138 (worker contexts), native_format.cpp (8), surface.cpp (6),
glsl_to_spirv.cpp (6), render_pipeline.cpp (5). Outside graphics: 79 lines in
21 files, editor.cpp 11 of them (editor.cpp:1325-1331, :168, :1133, :2545),
program_interface.cpp:269 (geometry shaders only on OpenGL),
sky_renderer.cpp:86/386/456, imgui_renderer.cpp:8/1223/1524. No file outside
graphics includes a backend header (grep); one consumer reaches through
`get_impl()` (imgui_renderer.cpp:887, `gl_name()` in a trace macro).

Per-backend parity gaps (verified):
- Metal: `Gpu_timer` reads 0 (doc/erhe/metal_backend.md file table);
  `blit_framebuffer` is `ERHE_FATAL("not implemented")`
  (metal_blit_command_encoder.cpp:70); present-wait `unsupported`
  (metal_device.cpp:504); swapchain resize and format sorting TODO
  (metal_device.cpp:521, :1052); multi-draw indirect is a CPU loop
  (doc/erhe/metal_backend.md "Multi-Draw Indirect"); ray-tracing position
  fetch always false (device.hpp:241-247); `sub_pixel_precision_bits` 0
  (device.hpp:348-352); no idle ring-buffer reclaim (metal_device.cpp:634-667
  drains handlers only; the reclaim exists only at vulkan_device.cpp:1631-1670,
  as doc/erhe/ring_buffer_memory.md "Idle reclaim" states).
- OpenGL: no ray query (device.hpp:212-220), depth/stencil resolve sample_zero
  only (enums.hpp:279-280), no frame capture (device.cpp:81-99), no idle ring
  reclaim (gl_device.cpp:1164-1189).
- Null: 76 functions returning empty / zero (null_device.cpp), the template
  the WebGPU plan forks.

Verdict: honest about being "Vulkan-style" and disciplined about includes, but
the neutral surface is the union of the backends' concepts (Vulkan layouts and
GL barrier bits at once).

### 1.3 Resource lifetime and ownership

- `Buffer`, `Bind_group_layout`, `Render_pipeline` are movable
  (buffer.hpp:37-38, bind_group_layout.hpp:129-130, render_pipeline.hpp:95-96);
  `Device`, `Render_pass`, encoders and `Gpu_timer` are pinned
  (device.hpp:433-436, render_pass.hpp:103-106, gpu_timer.hpp:46-49).
  `Texture` is move-constructible only (texture.hpp:83-84), being an
  `erhe::Item` (texture.hpp:77) that lives in `std::shared_ptr`.
- Descriptors hold raw pointers the caller keeps alive:
  `Render_pass_attachment_descriptor::texture` (render_pass.hpp:46),
  `Render_pipeline_data::shader_stages` (render_pipeline_state.hpp:25),
  `Bind_group_layout_binding::immutable_sampler` (bind_group_layout.hpp:83-87),
  `Gpu_timer::m_render_pass` (gpu_timer.hpp:83); only the last is protected by
  back-registration (render_pass.hpp:121-122, render_pass.cpp:163-171).
- Deferred GPU-side destruction is a backend convention, not an API: every
  Vulkan and Metal `~X_impl` captures handles into a lambda for
  `Device_impl::add_completion_handler` (29 sites in 23 files; pattern at
  vulkan_buffer.cpp:172-192, doc/erhe/vulkan_backend.md "Deferred resource
  destruction"). The neutral `Device::add_completion_handler` wraps each call
  in a second `std::function` (device.cpp:120-127).
- Public static registries `Render_pipeline_state::s_pipelines` and
  `Compute_pipeline_state::s_pipelines` (render_pipeline_state.hpp:52-55,
  compute_pipeline_state.hpp:59-63) serve one ImGui window
  (src/erhe/imgui/erhe_imgui/windows/pipelines.cpp:385).
- The Vulkan backend has a process singleton `Device_impl::s_device_impl`
  (vulkan_device.hpp:714, accessor :215; 10 backend readers, e.g.
  vulkan_render_command_encoder.cpp:133) to find the active render pass, which
  the neutral `Device` also tracks (device.hpp:730, render_pass.cpp:236-246).

### 1.4 Synchronization model

- Frames in flight: Vulkan 2 (vulkan_device.hpp:462), OpenGL 3 ("sizing hint
  only", gl_device.hpp:349-350), Metal 3 (metal_device.hpp:251), null 2
  (null_device.hpp:131); rendering report 1.3 confirmed.
- Device frame: `wait_frame` / `get_command_buffer(thread_slot)` /
  `submit_command_buffers` / `end_frame`, present implicit
  (device.hpp:439-467, :532-553; doc/erhe/graphics.md "Frame lifecycle").
  Legacy `begin_frame(Frame_begin_info)` / `end_frame(Frame_end_info)` shims
  remain (device.hpp:469-473), as do "legacy single-pool fields ... to be
  removed" (vulkan_device.hpp:122-131).
- Cross-cb dependencies name the other cb (`wait_for_cpu/gpu`,
  `signal_cpu/gpu`, command_buffer.hpp:63-93); the primitives are hidden. The
  header still opens with "Currently a stub" (command_buffer.hpp:27-30).
- Barriers: attachment `usage_before/usage_after` (render_pass.hpp:54-57)
  drive the Vulkan inter-pass and post-pass `VkMemoryBarrier2`
  (vulkan_render_pass.cpp:166-300, doc/erhe/vulkan_backend.md
  "Synchronization"); the inter-pass destination always includes
  fragment-shader reads "since any pass may sample" (vulkan_render_pass.cpp:202-206),
  one conservative global barrier per pass. Outside passes the caller places
  `Command_buffer::memory_barrier` (33 external sites) with GL-shaped bits.
- Layout tracking is one `mutable VkImageLayout` per texture
  (vulkan_texture.hpp:100), read and written by the render pass
  (vulkan_render_pass.cpp:648, :806, :1269-1293, :1665-1687). The blit encoder
  transitions copy destinations from `VK_IMAGE_LAYOUT_UNDEFINED`
  (vulkan_blit_command_encoder.cpp:153, :294, :512), which permits discard;
  doc/plans/graphics_tests_agfx_port.md "Open findings" and
  memory-bank/topics/graphics_tests.md [TRAPS] record it as needing
  per-subresource tracking. Confirmed open. `render_pass_before` /
  `render_pass_after` (render_pass.hpp:160-165) feed only a debug warning.

Verdict: correct and conservative; one queue, one layout per image, one
global barrier per pass. That is the ceiling on overlap, not a bug.

### 1.5 Memory management

- Buffers: VMA, a second allocator for device-address buffers
  (vulkan_device.hpp:255-265, :538-543), a 16-byte floor for
  acceleration-structure inputs (vulkan_buffer.cpp:70-80). Failure dumps VMA
  stats then `abort()` (vulkan_buffer.cpp:81-95) from a `noexcept` constructor
  (buffer.hpp:34).
- Ring buffers: `Ring_buffer` = one `Buffer` + `Circular_ring_buffer_algorithm`
  + CPU shadow without persistent mapping (ring_buffer.hpp:74-82); sync entries
  stamped with the frame, reclaimed in `frame_completed` (ring_buffer.cpp:229-268).
  The device allocator (vulkan_device.cpp:2101-2188) searches existing buffers
  then creates a spill buffer, so the rendering report's "no grow path" holds
  for `Ring_buffer::acquire` (ring_buffer.cpp:116-120 returns `{}`) but not for
  the device; Vulkan reclaims idle spills 16 frames later
  (vulkan_device.cpp:1637-1670). The allocator is copied into each backend
  (gl_device.cpp:1398, metal_device.cpp:828).
- Textures: estimated byte accounting in atomics (texture.cpp:66-102);
  doc/plans/texture_memory.md records an unexplained 688 MB per empty scene.
  Budget: VMA heap budgets, zeros elsewhere (device.hpp:398-412).

### 1.6 Error handling

- Counts: `ERHE_VERIFY` 662 (gl 341, vulkan 135, neutral 114, metal 70, null
  3), `ERHE_FATAL` 99, bare `abort()` 71 (vulkan_device_init.cpp 23,
  vulkan_device.cpp 11, vulkan_swapchain.cpp 10), `throw` 1
  (vulkan_bind_group_layout.cpp:132). No return codes beyond `bool` from the
  frame functions (device.hpp:464-467) and `Render_pipeline::is_valid`
  (render_pipeline.hpp:101).
- Data errors go through `Device::device_message` (device.hpp:671, 37 error
  sites, e.g. render_pass.cpp:96-161); the editor turns them fatal with
  validation on (device.hpp:511-514). Device loss reports the device fault
  before aborting (vulkan_device.hpp:376-385); buffer creation failure dumps
  VMA stats first (vulkan_buffer.cpp:81-95).

Verdict: consistent "abort on programmer error, message on data error"; the
bare `abort()`s lack only the ERHE_FATAL breadcrumb.

### 1.7 Threading model

- Vulkan/Metal: per-(frame, thread_slot) command pools, 8 slots
  (vulkan_device.hpp:463, :569-572; metal_device.hpp:211); worker recording is
  serialized by a public `Device_impl::m_recording_mutex` (vulkan_device.hpp:598-605).
- OpenGL: a share-context pool entered through `Scoped_worker_context`
  (scoped_worker_context.hpp:26-50), a thread_local active render pass
  (gl_device.hpp:99-102), per-context container adoption (render_pass.cpp:247-255);
  rules in doc/erhe/gl_worker_thread_contexts.md and
  doc/erhe/gl_worker_context_enforcement.md.
- Mutexes on the Vulkan pipeline maps (vulkan_device.hpp:648-651) and per
  `Base_render_pipeline` (render_pipeline.hpp:153-154); one atomic use, texture
  accounting (texture.cpp:66-70); `Circular_ring_buffer_algorithm` is
  single-threaded by contract (doc/erhe/circular_ring_buffer.md "Notes").

### 1.8 Shader pipeline and shader tree

- GLSL + C++-emitted interface (`Shader_resource`, shader_resource.hpp:18-33,
  :181-203; `Bind_group_layout` samplers, bind_group_layout.hpp:138-145) ->
  `final_source` (312 lines, shader_stages_create_info.cpp:219) -> glslang to
  SPIR-V with a disk cache (spirv_cache.hpp:11-25; forced on for Vulkan and
  Metal, optional for GL: CMakeLists.txt:460-465, gl_shader_stages.hpp:64-65)
  -> SPIRV-Cross to MSL on Metal (metal_shader_stages_prototype.cpp:23). Hot
  reload via `Shader_monitor` / `Reloadable_shader_stages` (shader_stages.hpp:154-168).
- The tree: 42 files, 19 `erhe_*.glsl` headers, standard.frag (1163 lines, 54
  preprocessor branches), standard.vert (462, 24), contract in
  erhe_standard_variant.glsl:1-40, axes in doc/erhe/shader_variants.md.
  Shaders branch on capability / `WORKAROUND_*` defines, never on the API
  (doc/erhe/shader_workarounds.md "Policy", device.hpp:320-333). `res/shaders`
  is a cwd-relative literal in each consumer (debug_renderer.cpp:141,
  text_renderer.cpp:30, id_renderer.cpp:520, ddgi_renderer.cpp:139-140).

### 1.9 The finished reports' observations, checked

- Rendering 1.3: "no grow path" is true of `Ring_buffer` but the device
  allocator spills and (Vulkan only) reclaims (1.5). The pending-vector swaps
  it flags are in scene_renderer; the library's own steady-state paths keep
  capacity (ring_buffer.cpp:259-264; descriptor sets recycled by completed
  frame, vulkan_texture_heap.cpp:265-297).
- Rendering 1.10 "Honored": encoders and `Texture_heap` allocate nothing per
  bind (1.1, 3.5).
- Infra 1: the `erhe_graphics` split is worthwhile (1.1); its "405 files" is
  now 526. `erhe_task` linking graphics is confirmed from this side: the
  guard queries are the backend-neutral functions at scoped_worker_context.hpp:17-25.
- Editor 1: makes no claim about erhe::graphics internals.

--------------------------------------------------------------------------------
## 2. API issues

### 2.1 Naming consistency

- Enum values break snake_case: `Vendor::Nvidia/Amd/Intel` (enums.hpp:12-17),
  `Ring_buffer_usage::None/CPU_write/GPU_access` (:408-414),
  `Load_action::Dont_care/Clear/Load`, `Store_action::Multisample_resolve`
  (render_pass.hpp:22-33), `Vertex_step::Step_per_vertex`
  (vertex_format.hpp:62-66), `Format_kind::format_kind_float` (dataformat.hpp:140-145).
- `Buffer_target` and `Buffer_usage` enumerate the same roles (enums.hpp:102-113,
  :303-326) with a `get_buffer_usage(Buffer_target)` bridge (:447);
  `Ring_buffer_client` takes a target, `Ring_buffer_create_info` a usage
  (ring_buffer_client.hpp:18, ring_buffer.hpp:16). `get_` is applied
  unevenly (`Shader_stages::name()`, shader_stages.hpp:144).

### 2.2 Boolean arguments (16 in public headers)

`get_depth_clear_value_pointer(bool)`, `get_depth_function(..., bool)`
(device.hpp:733-734); three `depth_test_enabled_*(bool reverse_depth)` presets
(depth_stencil_state.hpp:46-48); `with_face_culling_disabled_if(bool)`,
`with_winding_flip_if(bool)` (rasterization_state.hpp:48, :56);
`Base_render_pipeline::get_pipeline_for(..., bool front_face_flip, bool
disable_face_culling)` with 18 external call sites (render_pipeline.hpp:139-143);
`Shader_resource` sampler constructor `bool is_texture_heap`
(shader_resource.hpp:135) and `add_sampler` (:221), where the header's own
TODO says the flag may be redundant (:354-358); `set_readonly/set_writeonly(bool)`
(:207-208); `Shader_monitor::begin(bool)`, `set_enabled(bool)`, `set_run(bool)`
(shader_monitor.hpp:21, :26, :43); `Texture::set_two_component_normal(bool)`
(texture.hpp:157); `gl_helpers::set_error_checking(bool)` (gl_helpers.hpp:30).

### 2.3 Construction patterns

Create-info classes are used consistently. Irregularities: `Texture_create_info`
holds a `Device& device` member (texture.hpp:25) while the constructor takes
the device again (texture.hpp:87), making the create info non-assignable;
`Shader_resource` has seven constructors (shader_resource.hpp:81-155)
including a 9-parameter sampler one (:128-138) and a "legacy positional"
block one beside `Block_create_info` (:96-116); `Shader_stage` notes its
constructors "disable using designated initializers" (shader_stages.hpp:33-34);
`Buffer(Device&)` (buffer.hpp:32) builds an empty buffer.

### 2.4 Functions with more than six parameters

`Blit_command_encoder::copy_from_texture` (9, 9, 8) and `copy_from_buffer` (9)
on single lines (blit_command_encoder.hpp:33-38); `Command_buffer::upload_to_texture`
(9, command_buffer.hpp:102); the `Shader_resource` sampler constructor (9,
shader_resource.hpp:128-138); `get_pipeline_for` (7, render_pipeline.hpp:133-144);
`Ring_buffer::get_size_available_for_write` with three out-references
(ring_buffer.hpp:26-31); backend-internal `get_or_create_compatible_render_pass`
with 15 (vulkan_device.hpp:399-415). A `Texture_region` / `Buffer_region`
value type would remove all the blit ones.

### 2.5 Hidden coupling and ordering

- `set_bind_group_layout()` must precede `set_render_pipeline_state()`,
  enforced only in debug builds (vulkan_render_command_encoder.cpp:118-124).
- `Texture_heap::reset_heap` needs a `Command_buffer&` only for a debug group
  (texture_heap.hpp:46-52); `bind` reaches the pipeline layout via
  `encoder.get_impl()` (render_command_encoder.hpp:79-83).
- `Gpu_timer::write_begin_timestamp` / `on_render_pass_destroyed` are public
  "Internal: not intended to be called" (gpu_timer.hpp:61-69); `Buffer`
  befriends `Vertex_input_state` and `Texture` (buffer.hpp:78-79); the Vulkan
  render pass `const_cast`s the neutral texture to update its layout
  (vulkan_render_pass.cpp:1665-1687).

### 2.6 God classes and over-wide headers

- `Device` (device.hpp:424-731) covers frame lifecycle, submission, factories,
  format queries, shader caches, callbacks, frame pacing (:678-710) and OpenXR
  native handles (:82-105, :716); `Device_info` is 267 lines (:130-396).
  Vulkan `Device_impl`: 724 header lines, a 2300-line constructor
  (vulkan_device_init.cpp:206), GPU timers and calibrated timestamps inside
  (vulkan_device.hpp:292-344).
- texture.hpp includes erhe_item/item.hpp and typed.hpp (texture.hpp:5-6),
  which include the property system (item.hpp:4, typed.hpp:6): 2348 header
  lines paid by everything that names a `Texture`. enums.hpp (538 lines) is in
  every graphics header.

### 2.7 Duplicated concepts

- Two pipeline objects: `Render_pipeline_state` (mutable data plus global
  registry, render_pipeline_state.hpp:36-56) and `Render_pipeline` /
  `Base_render_pipeline` (compiled, format-keyed, render_pipeline.hpp:88-155);
  `set_render_pipeline_state` has 4 external callers beside
  `set_render_pipeline` with 19.
- `Memory_usage` (enums.hpp:328-332) beside required/preferred property masks
  (ring_buffer.cpp:15-46 maps between them); the active render pass tracked
  twice (1.3); three copies of the completion-handler drain
  (vulkan_device.cpp:1672-1682, gl_device.cpp:1176-1188, metal_device.cpp:636-647);
  GL `Gl_state_tracker` beside `Gl_binding_state` (gl_binding_state.hpp:19-50).

### 2.8 APIs easy to misuse

- `Ring_buffer_range`: `operator=(Ring_buffer_range&) = delete` takes a
  non-const reference (ring_buffer_range.hpp:23); close / release / cancel are
  caller-sequenced (:27-32); `Ring_buffer::acquire` verifies the usage with
  "TODO Cleanup this API" (ring_buffer.cpp:112).
- `Image_usage_flag_bit_mask::user_synchronized` points to
  `Device::cmd_texture_barrier()` (enums.hpp:485-489), which lives on
  `Command_buffer` (command_buffer.hpp:110) and has no external caller.
- `Bind_group_layout_binding::stage_flags` defaults to `none`, rejected at
  runtime by the one `throw` on Vulkan and ignored by GL and Metal
  (bind_group_layout.hpp:89-101, vulkan_bind_group_layout.cpp:132): a binding
  that works on GL throws on Vulkan.
- `Device::wait_idle` fires completion handlers, `submit_command_buffer_and_wait`
  does not (device.hpp:475-477, :555-562).

### 2.9 Const, move and virtuals

`Texture::clear() const`, `Shader_stages::reload() const` / `invalidate() const`
mutate the GPU object (texture.hpp:144, shader_stages.hpp:141-142).
`Command_encoder` is an abstract base with two pure virtuals
(command_encoder.hpp:12-19) so `Ring_buffer_client::bind` can take either
encoder (ring_buffer_client.cpp:32-71); the only other virtuals are
`Texture_reference` (texture.hpp:53-73) and `IBuffer` (ibuffer.hpp:13-25).

--------------------------------------------------------------------------------
## 3. Code health

### 3.1 Largest files and functions (non-test)

Files: vulkan_device.cpp 3077, vulkan_device_init.cpp 2507, vulkan_swapchain.cpp
2269, dataformat.cpp 2133, gl_device.cpp 2078, vulkan_helpers.cpp 1791,
vulkan_render_pass.cpp 1696, shader_resource.cpp 1320, vulkan_surface.cpp 1157,
metal_device.cpp 1139, erhe::gl gl_helpers.cpp 1117, gl_texture.cpp 1097.

Functions (brace-matched spans): Vulkan `Device_impl::Device_impl` 2300
(vulkan_device_init.cpp:206), Vulkan `Render_pass_impl::Render_pass_impl` 774
(vulkan_render_pass.cpp:374), GL `Device_impl::Device_impl` 648
(gl_device.cpp:116), Vulkan `set_render_pipeline_state` 375
(vulkan_render_command_encoder.cpp:87), Vulkan `Render_pipeline_impl` ctor 349
(vulkan_render_pipeline.cpp:18), `Shader_stages_create_info::final_source` 312
(shader_stages_create_info.cpp:219), `compile_spirv_to_mtl_function` 307
(metal_shader_stages_prototype.cpp:23).

### 3.2 Duplication across backends that could be shared

- The ring-buffer pool allocator and the completion-handler drain (2.7) are
  copy-equivalent across vulkan/gl/metal; only Vulkan reclaims
  (vulkan_device.cpp:1631-1670). One neutral `Ring_buffer_pool` would carry
  all three.
- `Shader_stages_prototype_impl` repeats constructors, `name()`,
  `create_info()`, `get_final_source()`, `get_dependency_paths()` per backend
  (vulkan_shader_stages_prototype.cpp:14-139, metal_shader_stages_prototype.cpp:333-495,
  gl_shader_stages_prototype.cpp:379-883, null_shader_stages_prototype.cpp:6-13)
  although the glslang front end is shared (`Glslang_shader_stages`,
  glsl_to_spirv.hpp:24-28; gl_shader_stages.hpp:65, vulkan_shader_stages.hpp:70,
  metal_shader_stages.hpp:57). Enum conversion tables: vulkan_helpers.cpp 42
  functions, gl_helpers.cpp 11, metal_helpers.cpp 13.

### 3.3 Dead code

- Twelve files under erhe_graphics/ are not in src/erhe/graphics/CMakeLists.txt:
  pipeline.cpp (a stale duplicate of render_pipeline_state.cpp assigning
  `data.name`, which `Render_pipeline_data` no longer has, pipeline.cpp:63-71
  vs render_pipeline_state.hpp:21-34), command_queue.cpp (empty namespace),
  scoped_buffer_mapping.cpp, png_loader_mango.{cpp,hpp},
  png_loader_mango_spng.{cpp,hpp}, png_loader_none.{cpp,hpp}; draw_indirect.hpp
  and vulkan_external_creators.hpp are header-only and live. `gl_renderbuffer.*`
  is commented out (src/erhe/graphics/CMakeLists.txt:156-157) and
  `class Renderbuffer;` stays forward declared (render_pass.hpp:17).
- Commented-out API: `set_render_pipeline_state(..., override_shader_stages)`
  (render_command_encoder.hpp:48), fifteen Metal-derived signatures
  (blit_command_encoder.hpp:41-57). `Multi_copy_buffer` and
  `Scoped_transient_object_pool` have one external user each.

### 3.4 Counters

- TODO/FIXME/HACK: graphics 62 (neutral 21, gl 19, vulkan 17, metal 5, null 0),
  dataformat 1, others 0. Notable: `ERHE_FATAL("TODO")` on a reachable GL path
  (gl_render_pass.cpp:600), "TODO Proper conversion" (gl_device.cpp:1463).
- `struct` (rule: class only): graphics 14 (3 in the `Enable_bit_mask_operators`
  trait, enums.hpp:417-441; 4 backend-internal, e.g. vulkan_device.hpp:686,
  metal_device.hpp:160; 7 in the dead spng loader); other slice libraries 8
  (codegen field_info.hpp:40-93 five, migration.hpp:36, vertex_format.hpp:156,
  gl enum_bit_mask_operators.hpp:9).
- `auto` locals (rule: explicit types; `auto f() -> T` excluded): neutral 104,
  vulkan 97, gl 79, metal 37, null 4; vulkan_helpers.cpp 45 alone.
- Non-ASCII (rule: ASCII only): device.hpp:257-258, gl_device.cpp:2062 and
  :2071, glsl_to_spirv.cpp:167, six lines in
  src/erhe/codegen/erhe_codegen/serialize_helpers.hpp (e.g. line 19), the
  codegen .py emitters.
- Logging: 616 erhe log calls across 17 categories (log_context 193,
  log_startup 114, log_swapchain 58, log_program 55); no printf / iostream
  outside a test.

### 3.5 Allocation in hot paths

- Clean: encoders and heap (1.1); the Vulkan pipeline bind hashes without
  allocating (vulkan_render_command_encoder.cpp:87-140); descriptor sets
  recycled by completed frame (vulkan_texture_heap.cpp:265-297);
  `submit_command_buffers` reserves once per submit (vulkan_device.cpp:1456);
  ring buffers keep capacity.
- Per frame by design: `Device::add_completion_handler` allocates two
  `std::function`s per call (device.cpp:120-127; 3 external sites); Metal
  `clear_texture` allocates the full clear size (metal_command_buffer.cpp:473).

### 3.6 Doc / code mismatches found

- doc/erhe/graphics.md "Frame lifecycle" names `Command_buffer::wait_for_fence`
  / `wait_for_semaphore`; the API is `wait_for_cpu` / `wait_for_gpu` /
  `signal_cpu` / `signal_gpu` (command_buffer.hpp:90-93).
- doc/erhe/graphics.md "Public API" shows `Scoped_render_pass(render_pass)`;
  the constructor requires a `Command_buffer&` (render_pass.hpp:160-165).
- doc/erhe/graphics.md "Texture heap" shows `reset_heap()`, `bind()`,
  `unbind()` without arguments; all three take an encoder or command buffer
  (texture_heap.hpp:52-60).
- doc/erhe/graphics.md "Bind group layout" snippet omits `stage_flags`,
  `immutable_sampler` and `image_format` (bind_group_layout.hpp:66, :87, :101);
  the first is mandatory on Vulkan (2.8).
- command_buffer.hpp:27-30 "Currently a stub" is stale; doc/erhe/metal_backend.md
  "File Structure" already corrects the same claim for Metal.

--------------------------------------------------------------------------------
## 4. Strengths (specific)

- Four backends behind one API with no backend header leaking out and no
  virtual dispatch (1.1, 1.2); the null backend is a skeleton for a fifth.
- Inline-storage encoders make "one encoder per pass" free
  (render_command_encoder.hpp:86).
- Barriers derived from declared attachment usage (render_pass.hpp:54-57,
  vulkan_render_pass.cpp:166-300); nested passes caught at the API
  (render_pass.cpp:236-246).
- Cross-cb sync by naming another cb, primitives hidden
  (command_buffer.hpp:63-93); deferred destruction applied uniformly at 29
  sites (doc/erhe/vulkan_backend.md "Deferred resource destruction").
- A ring-buffer pool that spills, warns at 8 buffers and reclaims idle spills
  (vulkan_device.cpp:2101-2188, :1631-1670), with a verification recipe
  (doc/erhe/ring_buffer_memory.md "Verification").
- `Shader_resource` generates GLSL interface text and std140/std430 sizes from
  C++, no reflection (shader_resource.hpp:18-33), unit tested without a device
  (test/CMakeLists.txt:15-18).
- Capability- and workaround-driven shader defines, never API-driven
  (doc/erhe/shader_workarounds.md "Policy"); every `Device_info` flag says why
  it exists (device.hpp:260-267, :233-239).
- `erhe::gl` is generated from the Khronos registry (doc/erhe/gl.md
  "Purpose"); `erhe::circular_ring_buffer` is pure arithmetic with 18 tests;
  logging is exhaustive and categorized (3.4).

--------------------------------------------------------------------------------
## 5. Test coverage

- Deviceless `erhe_graphics_tests`: one file, 7 tests, std140/std430 sizing
  only (test/CMakeLists.txt:15-18, test_shader_resource_size.cpp); runs in CI.
- `erhe_graphics_gpu_tests`: 53 files, 195 `TEST_F` cases; 176 pass on headless
  Vulkan, 175 + 1 skip on Metal, 165 + 11 skips on macOS Vulkan, 170 + 12
  skips + 6 driver failures on OpenGL (doc/erhe/graphics_test_coverage.md;
  memory-bank/topics/graphics_tests.md [STATE]). FLIP image and buffer
  goldens, one set for all backends. Not run in CI (`gpu` ctest label);
  doc/plans/graphics_tests.md proposes lavapipe. `erhe_gpu_test_support`
  (test/CMakeLists.txt:61-105) is reused by the renderer and scene_renderer
  GPU tests.
- Covered: device up, clears, raster / depth / blend / stencil state, MRT,
  indexed / instanced / indirect and GPU-written indirect draws, load/store
  actions, subresource targets, MSAA resolve, compute, buffer bindings and
  transfers, texture copies, sampling of every texture type, ray queries.
- Not covered: swapchain acquire / present, frame pacing (device.hpp:678-710),
  `capture_last_frame`, the ring-buffer pool (vulkan_device.cpp:2101-2188 has
  no test), `Texture_heap` except through renderer tests, `Shader_monitor` hot
  reload, `Gpu_timer` values, sparse textures, texture views (texture.hpp:42-44),
  multiview, fragment density maps, depth bias, device loss. `erhe::gl` and
  `erhe::buffer` have no tests; `erhe::codegen` has three assert-based
  executables (test_migration.cpp:5).
- Open engine findings the tests exposed: per-subresource layout tracking, the
  AMD GL comparison-sampler defect, `.pfm` preview clamping
  (doc/plans/graphics_tests_agfx_port.md "Open findings").

Verdict: broad, golden-backed GPU coverage; untested are the swapchain /
pacing / memory-pool paths that only the editor exercises.

--------------------------------------------------------------------------------
## 6. Future options (prioritized)

### 6.1 Split erhe_graphics into interface and backend targets

Cost: medium (include diet on device.hpp:3-15 first, then the CMake split).
Benefit: high; 15 consumers and four backends stop rebuilding on backend-only
changes, and the interface compiles against the null backend in CI. Pairs
with removing the twelve dead files (3.3).

### 6.2 Per-subresource layout tracking and a region value type

Cost: medium (vulkan_texture.hpp:100 becomes an array per level x layer;
vulkan_blit_command_encoder.cpp:153/294/512 read the tracked layout; a region
class replaces the 8-9 parameter blit signatures, blit_command_encoder.hpp:33-38).
Benefit: high; closes the only known correctness hole (1.4).

### 6.3 Shared ring-buffer pool with reclaim on every backend

Cost: low (lift vulkan_device.cpp:2101-2188 and :1631-1670 into a neutral
`Ring_buffer_pool`). Benefit: medium; removes three copies and gives GL /
Metal the bounded memory doc/erhe/ring_buffer_memory.md promises.

### 6.4 Shader compilation pipeline and SPIR-V cache

Cost: low for the SPIR-V cache plan (settings hash in the salt, atomic
rename); medium for a persisted `VkPipelineCache` (device.hpp:490-507) and
Metal `MTLBinaryArchive`. Benefit: medium; first-frame time on Quest is
dominated by pipeline compilation (device.hpp:495-498).

### 6.5 Descriptor model and bindless

Cost: high. Today: push descriptors per draw for buffers and dedicated
samplers, a variable-count set 1 for sampled images only (doc/erhe/graphics.md
"Texture heap"; vulkan_device.hpp:645-647). Bindless buffers / storage images
and push constants would let a GPU-driven renderer pass handles instead of
bindings (doc/reference/agfx_comparison.md "Weaknesses"). Benefit: high for
GPU-driven rendering only.

### 6.6 Render-pass and framebuffer abstraction

Cost: medium-high. Vulkan builds a `VkRenderPass` + framebuffer per
`Render_pass` and a compatible pass per pipeline format
(vulkan_device.hpp:399-415, vulkan_render_pass.cpp:374-1149) while
`dynamicRendering` is enabled and unused (doc/plans/vulkan_backend.md).
Dynamic rendering removes the 15-parameter factory and the 774-line
constructor. Benefit: medium; simplification more than speed.

### 6.7 Metal parity

Cost: low per item: GPU timers, `blit_framebuffer`, swapchain resize, idle
ring reclaim (6.3). Benefit: medium; Metal is the closest analogue to WebGPU.

### 6.8 GPU-driven rendering, mesh shaders, async compute

Cost: high. Prerequisites: 6.5, a GPU-side draw count (the indirect draw
takes a CPU `drawcount`, render_command_encoder.hpp:64-70), indirect dispatch,
a second queue (vulkan_device.hpp:552-555) and a mesh `Shader_type`
(enums.hpp:222-230). Benefit: high only once scene sizes exceed the draw-list
renderer's CPU budget.

### 6.9 WebGPU feasibility

Cost: high but mechanical for the backend (about 22 impl pairs forked from
null/, doc/plans/wasm_webgpu_port.md "Ground truth"); the neutral API already
has the WebGPU-shaped concepts (bind group layout, up-front pipelines,
encoders). The blockers are outside this slice (GLSL-to-WGSL, threading,
geogram, httplib per the plan). Benefit: strategic; native-Dawn-first is right.

### 6.10 API hygiene batch

Cost: low. Enums for the 16 boolean parameters (2.2), snake_case enum values
(2.1), drop `Texture_create_info::device` (2.3), remove the legacy frame
overloads and the "stub" comment (1.4), privatize `Gpu_timer`'s hooks (2.5),
return a struct from `get_size_available_for_write` (2.4), fix the non-ASCII
lines (3.4) and the four stale doc/erhe/graphics.md snippets (3.6). Benefit:
medium; the library sets the example for 15 consumers.
