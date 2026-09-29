# agfx vs. erhe::graphics

Comparison of [agfx](https://github.com/AmelieHeinrich/agfx) ("Amelie's graphics
library", a C99 RHI over D3D12, Metal 4 and Vulkan 1.4) against `erhe::graphics`
(see [erhe/graphics.md](../erhe/graphics.md), [erhe/vulkan_backend.md](../erhe/vulkan_backend.md),
[erhe/metal_backend.md](../erhe/metal_backend.md)). Reviewed from a local clone
of agfx `main` at commit `d8ad38b` ("ADD: Buffer barriers and ImGui impl fixes",
v2.0.0 era, 2026-09). The goal is to identify features and practices worth
adopting in `erhe::graphics`; section 6 is the sorted list.

## 1. What agfx is

agfx is a small render hardware interface aimed at indie games shipping on
Windows, macOS and Linux. Its design commitments:

- **C99 API with opaque handles** (`agfx.h`, 1.7k lines), a C++17 RAII wrapper
  (`agfx.hpp`, 1.4k) and an immediate-mode "ez" layer for D3D11/OpenGL
  refugees (`agfx_ez.hpp`, 1.7k). Bindings for Zig, Rust and Odin exist
  because the C ABI is the primary surface.
- **One backend per platform, chosen at build time:** D3D12 on Windows
  (`agfx_d3d12.cpp`, 3.1k lines), Metal 4 on macOS 26+ (`agfx_metal4.mm`,
  2.7k), Vulkan 1.4 on Linux (`agfx_vulkan.cpp`, 3.5k). There is no Vulkan
  backend on Windows, no OpenGL, no Android, no headless "null" backend.
- **Bindless first.** Every view (`agfxTextureView`, `agfxBufferView`,
  `agfxSampler`, acceleration structure) returns a 32-bit descriptor index.
  Shaders receive indices through a single 128-byte push-constant block and
  index `ResourceDescriptorHeap[]` / `SamplerDescriptorHeap[]` directly. There
  are no descriptor sets, root parameter tables or argument encoders in the
  API. On Vulkan this is one global `VkDescriptorSet` with a
  `VK_DESCRIPTOR_TYPE_MUTABLE_EXT` binding of 400k slots, a sampler binding of
  2k slots and an acceleration-structure binding of 8 slots
  (`agfx_vulkan.cpp:27-29`, `625-735`), all `UPDATE_AFTER_BIND`.
- **HLSL SM 6.6 as the single shader language.** DXC produces DXIL on Windows,
  DXIL translated by Apple's Metal Shader Converter on macOS, and SPIR-V on
  Linux. A 530-line HLSL include (`data/shaders/agfx.h`) wraps the descriptor
  heaps in `AGFXTexture2D<T>::Create(handle)`-style classes so shader code is
  identical across backends. Vertex input is vertex pulling from structured
  buffers; there is no input-assembler vertex layout in the API.
- **D3D12-shaped resource states.** Barriers are explicit
  `agfxCommandBufferTextureBarrier(texture, oldState, newState, mip, layer, agglomerate)`
  calls with a 16-value `agfxResourceState` enum; the library performs no
  hazard tracking. Vulkan maps each state to stage/access/layout and emits
  `vkCmdPipelineBarrier2`; Metal 4 has no per-resource barriers, so the
  `agglomerate` flag merges pending transitions into one stage barrier flushed
  at the next encoder.
- **Explicit everything else:** command queues (graphics/compute/transfer),
  timeline fences (`agfxFence` = timeline semaphore / `ID3D12Fence` /
  `MTLSharedEvent`), per-slot frames-in-flight pacing written by the
  application, no deferred destruction (destroying a resource still referenced
  by in-flight work is a documented use-after-free), explicit residency commit
  on Metal (`agfxDeviceMakeResourcesResident`).
- **Modern feature set:** inline ray tracing (ray query from compute; BLAS
  build, update, copy, compaction), mesh and task shaders, multi-draw indirect
  with a GPU-written count buffer (`agfxIndirectBundle`), placement heaps with
  aliasing barriers, timestamp query pools with resolve and readback, pipeline
  cache blob export with driver-version validation, HDR swap chains, a native
  handle escape hatch (`agfx_native.h`) for upscalers and profilers.
- **Testing is the standout.** `agfx_tests` registers 313 GPU tests (36
  buffer-golden, 257 image-golden, 20 validation), each written for the C, C++
  and ez API flavors where applicable, compared against `data/tests/golden/`
  with byte-exact memcmp for buffers and NVIDIA FLIP (threshold 0.05) for
  images, producing `results.json` for an HTML triptych report
  (`tools/test_report/index.html`). The same suite runs against every backend.
- **Agent tooling:** `.claude/skills/` holds twelve skills (synchronization,
  resources, bindless shaders, MDI, ray tracing, swap chain, porting guides
  from D3D11/D3D12/Metal/OpenGL/Vulkan) and a porting agent. The skills carry
  the design rationale and the cross-backend traps the header comments do
  not.

## 2. What erhe::graphics is

`erhe::graphics` (~72k lines including tests and image loaders) is a
Vulkan-style abstraction over Vulkan 1.3 (~23k lines), OpenGL 4.5+ DSA (~13k),
Metal 3 via metal-cpp and SPIRV-Cross (~9k) and a null backend for headless
runs, selected at build time by `ERHE_GRAPHICS_API`. It is written for one
application family (the erhe editor, `hextiles`, the OpenXR Quest build) and
optimizes for correctness under validation, zero steady-state allocation, XR
(multiview, fragment density maps, pre-rotation, external Vulkan device
creation for OpenXR) and headless verification (emulated swapchain, MCP
screenshot readback).

Binding is declared once in `Bind_group_layout` and materialized as a
code-generated GLSL preamble (`Shader_resource` computes std140/std430
layouts and emits the block declarations), so C++ and shader stay in sync
without reflection. Vulkan binds with push descriptors (set 0) plus a
descriptor-indexed `Texture_heap` (set 1, 4096 combined image samplers,
`PARTIALLY_BOUND | UPDATE_AFTER_BIND | VARIABLE_DESCRIPTOR_COUNT`).
Synchronization is mostly implicit: render-pass attachments carry
`usage_before` / `usage_after` which become `VkRenderPass` initial/final
layouts and inter-pass `VkMemoryBarrier2`s; textures track their current
layout; resources are destroyed through frame completion handlers; streaming
goes through `Ring_buffer` with per-frame reclamation.

## 3. Feature comparison

Legend: **Yes** = present in the public API and implemented on the primary
backend; **Partial** = present with a stated restriction; **No** = absent.

### 3.1 Platform and API surface

| Feature | agfx | erhe::graphics |
|---|---|---|
| Backends | D3D12 (Win), Metal 4 (macOS 26+), Vulkan 1.4 (Linux) | Vulkan 1.3 (Win, Linux, macOS via MoltenVK, Android), OpenGL 4.5, Metal 3, null (headless) |
| Backend selection | Compile time, one per OS | Compile time, `ERHE_GRAPHICS_API` |
| Headless / offscreen | Tests render to textures; no null backend | Yes: null backend, emulated Vulkan swapchain, Metal offscreen ring |
| Windows Vulkan | No | Yes |
| Android / Quest | No | Yes (pre-rotation, OpenXR external device creation) |
| Minimum GPU | SM 6.6 dynamic resources + enhanced barriers; Vulkan 1.4 + `VK_EXT_mutable_descriptor_type`; Apple M1 | Vulkan 1.3 with push descriptors or per-frame pool fallback; GL 4.5 DSA; Metal 3 |
| Language / ABI | C99 with C++17 wrapper; Zig/Rust/Odin bindings | C++20 classes with pimpl, no C ABI |
| Public core size | ~5k header lines + ~9.4k backend lines | ~72k lines total (backends ~48k) |
| Immediate-mode convenience layer | `agfx_ez.hpp` (pipeline cache by hash, dynamic constant ring, best-effort state tracking) | None; renderers build on the explicit API |
| ImGui backend | `agfx_imgui` (user textures via bindless handle) | `erhe::imgui` renderer on top of `erhe::graphics` |
| Native handle escape hatch | `agfx_native.h`: device, queue, command list, resources, heap, fence, swap chain, residency set | `Native_device_handles` (instance, physical device, device, queue family) for OpenXR; no per-resource accessors |
| Allocator / logger callbacks | Allocation, temp allocation and log function pointers on device creation | `Device_message_callback`; erhe logging |

### 3.2 Resources and binding

| Feature | agfx | erhe::graphics |
|---|---|---|
| Buffer memory types | GPU_ONLY, CPU_TO_GPU, GPU_TO_CPU | Vulkan memory property masks + VMA flags (`can_alias`, `dedicated_allocation`, mapped, ...) |
| Sub-allocation | None: one `vkAllocateMemory` / committed resource per object unless placed in an `agfxHeap` | VMA for every buffer and image; `VK_EXT_memory_budget` reporting |
| Placement heaps / aliasing | Yes: `agfxHeap`, allocation-info queries, `agfxCommandBufferAliasingBarrier` | Partial: VMA `can_alias` flag only; no heap or transient-resource API |
| Texture types | 1D, 2D, 2D array, 3D, cube | buffer, 1D, 2D, 2D array, 3D, cube, cube array |
| Texture views | Separate `agfxTextureView` (format, type, mip/layer range, writeable) | `Texture` with `view_source` / base level / base layer |
| Buffer views | Raw, structured, constant; each a bindless handle | Bound by (buffer, offset, length) into a layout slot; no bindless buffers |
| Formats | 4 unorm8, 3 unorm16, 6 float, D32F, BC1/3/4/5/6H/7, ASTC 4x4 and 8x8. No integer, packed, 10-bit or stencil formats | `erhe::dataformat`: unorm/snorm/uint/sint/float families, packed, BC1-7, ASTC 4x4, D16/D24/D32F, S8, D24S8, D32FS8 |
| Format capability query | None (fixed table) | `get_format_properties`, `probe_image_format_support`, `choose_depth_stencil_format` |
| Compressed texture loading | Demo-side (stb, cgltf) | KTX2 + Basis transcoding, DDS, PNG (wuffs, mango) in-library |
| Mipmap generation | Demo-side compute (`AgfxMipGen`) | `Blit_command_encoder::generate_mipmaps` |
| Sparse textures | No (unplanned) | GL `ARB_sparse_texture` only |
| Bindless textures | Yes, all views, 400k slots, one global set | Partial: `Texture_heap` for color combined image samplers, 4096 slots, per-pass set; depth via dedicated binding |
| Bindless buffers / storage images / AS | Yes | No (push descriptors per draw) |
| Push constants | 128 B, the only binding mechanism | Internal only (`ERHE_DRAW_ID` emulation); not exposed to users |
| Descriptor sets / layouts | None in API | `Bind_group_layout` (UBO, SSBO, combined image sampler, storage image, AS) |
| Shader resource declaration | HLSL include; host passes indices | C++ `Shader_resource` builders generate GLSL blocks and std140/std430 offsets |
| Vertex input | Vertex pulling only (no IA state) | `Vertex_input_state` from `Vertex_format`; quantized formats probed |
| Samplers | Filter, 4 address modes (incl. border), anisotropy, comparison, LOD | Min/mag/mipmap, 3 address modes, anisotropy, comparison, LOD; immutable samplers in layout |
| Residency | Explicit `agfxDeviceMakeResourcesResident` (Metal residency set) | Implicit |
| Resource lifetime | Immediate destroy; app must drain | Deferred to frame completion handler |

### 3.3 Commands, passes and pipelines

| Feature | agfx | erhe::graphics |
|---|---|---|
| Command buffers | `agfxCommandBuffer` per queue; reset/begin/end | `Command_buffer` per (frame, thread slot); 8 thread slots |
| Multithreaded recording | Not addressed (no locks in any backend) | Yes, thread slots; GL via shared worker contexts |
| Queues | Graphics, compute, transfer with GPU signal/wait | One graphics queue plus present |
| Fences | Timeline `agfxFence`, CPU wait and GPU signal/wait, user-managed values | Hidden per-command-buffer fences and semaphores; `wait_for_gpu` / `signal_gpu` dependencies; frame timeline semaphore |
| Render pass | Dynamic rendering (`vkCmdBeginRendering`); 8 color + depth; load/store ops; no MSAA | `VkRenderPass` via `vkCreateRenderPass2`; 4 color + depth + stencil; load/store/resolve actions; MSAA with resolve modes; multiview; FDM |
| Render target object | `agfxRenderTarget` wrapper per (texture, mip, layer), created per frame | Attachment fields in `Render_pass_descriptor` |
| Barriers | Explicit resource-state transitions, memory barriers, buffer barriers, aliasing barriers, UAV barriers | Implicit from `usage_before/after`; `transition_texture_layout`, `cmd_texture_barrier`, `memory_barrier` for the rest |
| Pipeline state | Fill, cull, front face, topology, depth test/write/clamp/compare, per-attachment blend (8), color formats | Full Vulkan-style state structs: rasterization (conservative, depth bias), depth/stencil with stencil ops, single blend state with constants and write mask, multisample, input assembly |
| Per-attachment blend | Yes | No (one `Color_blend_state`) |
| Dynamic state | Viewport, scissor | Viewport, depth range, scissor, depth bias |
| Wide lines / point size | No | No (renderer generates line geometry by compute) |
| Pipeline creation | Explicit object per pipeline; optional cache blob in/out | Created and hashed at draw time; `warmup_render_pipeline`; variants per render-pass format |
| Pipeline cache persistence | `agfx*PipelineGetCache` blob + `driverVersion` for validation | In-memory `VkPipelineCache` only |
| Draw calls | Draw, indexed, mesh; indirect bundles (draw, indexed, mesh, dispatch) with count buffer | Draw, indexed (instanced), `multi_draw_indexed_primitives_indirect` (no count buffer) |
| Indirect dispatch | Yes | No |
| Mesh / task shaders | Yes (capability gated) | No |
| Geometry / tessellation | No | Geometry when advertised; tessellation types exist but Vulkan feature off |
| Compute | Yes | Yes |
| Ray tracing | Inline ray query from compute; BLAS/TLAS build, update, copy, compaction, AABB geometry | Inline ray query; BLAS/TLAS build (triangles, float3 / snorm16x3); Metal AS |
| Copies | Buffer<->buffer, buffer<->texture, texture<->texture (region, mip, layer) inside a compute pass | `Blit_command_encoder` copies, `blit_framebuffer`, `fill_buffer`; `upload_to_buffer/texture` |
| Texture upload | Staging copy; `agfxTextureReplaceRegion` on UMA only | `Buffer_transfer_queue` with tickets and budgeted flush |
| Streaming | ez `DynamicRingBuffer` (constants only) | `Ring_buffer`, `Ring_buffer_client`, `Multi_copy_buffer` with frame reclamation |
| Readback | Mappable readback buffers | `Buffer::invalidate`, `Ring_buffer_usage::CPU_read`, `capture_last_frame` |

### 3.4 Presentation and timing

| Feature | agfx | erhe::graphics |
|---|---|---|
| Swap chain | Explicit acquire/present, image count, vsync flag, HDR flag, resize | Implicit present in `submit_command_buffers`; `prefer_high_dynamic_range`, `allow_tearing`, `force_disable_vsync` |
| Present modes | FIFO default; MAILBOX preferred when vsync off, else IMMEDIATE | Scored MAILBOX / FIFO_RELAXED / FIFO_LATEST_READY |
| Frame pacing | App-written slot fences | `VK_KHR_present_wait`, `VK_EXT_present_timing`, calibrated timestamps, `set_present_target_time` |
| GPU timestamps | `agfxQueryPool` write/resolve/readback on all three backends | `Gpu_timer` on Vulkan and GL; Metal and null return 0 |
| Debug labels | Pass names -> `vkCmdBeginDebugUtilsLabel` / PIX events; object names | `Scoped_debug_group`, `Scoped_queue_debug_group`, `set_debug_label` |
| Validation | `enableValidation` flag (Khronos layer, D3D12 debug layer) | Config `vulkan_validation_layers` with best-practices and sync validation; policy: validation-clean at all times |
| Capture integration | Native handles for PIX / RenderDoc | Bundled RenderDoc API, `start_frame_capture` / `end_frame_capture` |
| Device fault reporting | No | `VK_EXT_device_fault` |
| Shader debug info | Optional DXC debug symbols | NonSemantic.Shader.DebugInfo.100 on by default |

### 3.5 Shaders, tests and docs

| Feature | agfx | erhe::graphics |
|---|---|---|
| Shader language | HLSL SM 6.6 | GLSL 4.6 / Vulkan GLSL |
| Compiler | DXC (`agfx_shader` lib + `agfx_shader_cli`), Metal Shader Converter, spirv-reflect on Linux | glslang at runtime; SPIRV-Cross for MSL; driver GLSL on GL |
| Portability mechanism | One HLSL include hides `AGFX_VULKAN` / `AGFX_METAL` differences | Preamble defines (`ERHE_DRAW_ID`, `ERHE_TEXTURE_HEAP_*`, workarounds), extension directives |
| Workgroup size handling | Reflected by the compiler, passed to pipeline create info | Declared in GLSL |
| Shader cache | None (app stores pipeline blobs) | On-disk SPIR-V cache keyed by source hash + glslang version |
| Hot reload | No | `Shader_monitor`, `Reloadable_shader_stages` |
| GPU tests | 313 golden tests (memcmp buffers, FLIP images), C/C++/ez flavors, `--update-goldens`, HTML report, run on every backend | 38 GoogleTest GPU tests asserting readback values; deviceless std140 tests in CI; Vulkan/lavapipe 41 pass, GL 40 pass + 1 skip, Metal not run |
| Documentation | Doxygen comments in `agfx.h` carrying constraints; 12 Claude skills with rationale and traps | `doc/erhe/*.md` per backend (~1.5k lines), `gpu_coding_rules.md`, `graphics_test_coverage.md` |

## 4. agfx: strengths and weaknesses

### Strengths

- **The bindless model removes a whole layer.** No descriptor set layouts,
  pipeline layouts, root signatures or per-draw descriptor writes exist in the
  API or in the shaders; a draw is "push 128 bytes of indices". This makes the
  three backends converge on identical shader source and lets GPU-driven
  paths (culling writes handles into indirect bundles) work without host
  involvement.
- **The test suite is the best argument for the library.** Golden buffers and
  FLIP-compared images, one test per API flavor, per-test artifact directories
  and an HTML triptych viewer make a backend change reviewable by eye. The
  `--update-goldens` flow is the documented way to accept intentional changes.
- **Small and readable.** One translation unit per backend, no allocator
  layer, no state tracker, plain C structs. A reader can hold a backend in
  their head in an afternoon.
- **Complete modern feature coverage** for a library this size: MDI with count
  buffers on all backends (including the Metal indirect command buffer
  translation), inline RT with update and compaction, mesh shaders, timestamp
  queries with resolve, heaps and aliasing, HDR, pipeline cache blobs.
- **Escape hatches are first class.** `agfx_native.h` documents exactly which
  native objects are borrowed, what invalidates them and which invariants the
  caller must restore. The same header states the two rules every upscaler
  integration breaks.
- **Documentation carries constraints, not narration.** Header comments state
  alignment rules, state requirements before `agfxRenderPassBegin`, the
  drawID divergence on Vulkan and the residency rule; the skills give the
  minimal call sequences and the gotchas per topic.

### Weaknesses

- **No hazard tracking and no deferred destruction.** Every transition,
  UAV barrier and drain is the application's job; destroying a view frees a
  descriptor slot that the next allocation reuses immediately, which races
  in-flight work (`agfx_mipgen.h:24-30` documents the trap instead of fixing
  it). The ez layer's tracker is explicitly best-effort.
- **The `agglomerate` flag is a backend-divergent bool.** `false` is a
  silent no-op on Metal and ignored on D3D12/Vulkan (`agfx_metal4.mm:1134`),
  so a wrong value renders correctly on two backends and corrupts on the
  third. The skill calls it "the single most common cross-platform bug".
- **Vulkan multi-queue is not spec-clean.** The barrier code comments that
  resources are `VK_SHARING_MODE_CONCURRENT` so `VK_QUEUE_FAMILY_IGNORED` is
  valid on every barrier (`agfx_vulkan.cpp:1520-1530`), but every buffer,
  image and swapchain is created `VK_SHARING_MODE_EXCLUSIVE`
  (`agfx_vulkan.cpp:1696, 1963, 2381, 3141`). Cross-queue use with distinct
  families therefore relies on driver leniency; the queue-handoff tests do
  not exercise a family transfer.
- **One dedicated allocation per resource** on Vulkan and D3D12 unless the
  application manages heaps itself. Drivers cap `maxMemoryAllocationCount`
  (4096 on many), so a scene with thousands of textures needs the heap path
  and hand-computed offsets.
- **Narrow format and state coverage.** No integer, packed 10/11-bit, stencil
  or D24 formats; no MSAA; no stencil test; no depth bias; no blend constants
  or write masks; no line width; no multiview, VRS or conservative raster; no
  cube arrays; no geometry or tessellation stages.
- **Only three platforms, each with one backend**, and a very recent
  minimum spec (macOS 26, SM 6.6, `VK_EXT_mutable_descriptor_type`). No
  Windows Vulkan means no RenderDoc-on-Vulkan on Windows, no Android, no
  OpenGL fallback, no integrated GPUs older than 2016.
- **HLSL-only toolchain with proprietary pieces.** The macOS path depends on
  Apple's Metal Shader Converter dylib; Linux `dlopen`s a bundled
  `libdxcompiler.so`. There is no SPIR-V or MSL source path and no runtime
  shader cache.
- **Single-threaded by construction.** No backend contains a lock; the slot
  allocators and Metal barrier tracker are not safe to use from two threads.
- **Doc drift.** The README's "<10k LOC" predates the Vulkan backend (core is
  ~14k without tests), the Vulkan sharing-mode comment contradicts the code,
  and the `drawID` field is dead weight on Vulkan.

## 5. erhe::graphics: strengths and weaknesses

### Strengths

- **Breadth and maturity of platforms:** four backends including headless,
  XR-specific features (multiview, fragment density maps, pre-rotation,
  external device creation), MoltenVK/KosmicKrisp handling, and a null
  backend that keeps the editor testable without a GPU.
- **Correctness discipline:** validation layers with best-practices and
  synchronization validation are a standing rule; device fault reporting;
  NonSemantic debug info by default; per-texture layout tracking; deferred
  destruction through frame completion; range-scoped upload barriers derived
  from declared usage.
- **Zero-allocation steady state:** `Ring_buffer` reclamation,
  `Multi_copy_buffer`, `Buffer_transfer_queue` and the pipeline hash cache
  exist precisely so frames do not allocate.
- **Rich render-pass model:** MSAA with min/max/average/sample-zero resolve,
  depth-stencil resolve, load/store-op-none, multiview view masks.
- **Frame pacing** on present-wait and present-timing extensions with
  calibrated timestamps, which agfx does not attempt.
- **Shader tooling:** one GLSL source for three backends, on-disk SPIR-V
  cache, hot reload, generated interface blocks with computed std140/std430
  layouts, driver workaround defines.
- **Substantial documentation** per backend, a test coverage document, and a
  coding-rules document.

### Weaknesses

- **Binding is heavier per draw than it needs to be.** Push descriptors
  rewrite every UBO/SSBO/sampler binding per draw call; the texture heap
  covers only color textures. There is no bindless buffer, storage image or
  acceleration structure path, and push constants are not user-facing, so a
  GPU-driven renderer cannot hand a shader "handles in 128 bytes".
- **No GPU-driven draw count, no indirect dispatch, no mesh shaders.**
  `multi_draw_indexed_primitives_indirect` takes a CPU-side draw count;
  culling on the GPU cannot shrink the submitted count.
- **One queue.** Uploads, acceleration structure builds and probe updates
  (DDGI, radiance cascades) share the graphics queue; there is no async
  compute or transfer path.
- **Pipeline cache is not persisted**, so every launch recompiles every
  pipeline variant; `warmup_render_pipeline` only moves the cost.
- **No golden-image regression testing.** The 38 GPU tests assert a handful
  of pixel or buffer values; a shading or barrier regression that keeps those
  values intact passes. Metal GPU tests have never been run.
- **Single blend state** for all attachments; per-attachment blend and write
  masks are unavailable.
- **Size and coupling.** Three full backends plus a null backend at ~48k lines
  is a large surface to keep consistent; `command_queue.hpp` is a stale stub
  that conflicts with live names; the texture-heap size in
  `vulkan_backend.md` (256) disagrees with the code (4096);
  `src/erhe/graphics/Readme.md` and `claude_review.md` describe an OpenGL-era
  library.
- **No native handle accessors per object**, so integrating an upscaler, a
  GPU profiler such as Tracy's Vulkan zones, or a third-party compute library
  needs backend-internal access.
- **Metal GPU timers return zero**, leaving the Metal backend without GPU
  timing.

## 6. Features to adopt from agfx, sorted

Sorted by expected value to erhe divided by implementation effort. Each item
names the agfx precedent and the erhe seam it would extend.

1. **Persistent pipeline cache with driver-version validation.** Store
   `vkGetPipelineCacheData` at shutdown next to the SPIR-V cache, keyed by
   `pipelineCacheUUID` and driver version as agfx does with `driverVersion`
   (`agfx.h:152-161`); load it in `vulkan_device_init.cpp` where the cache is
   created with `initialDataSize = 0`. Small change, direct startup win for
   the editor's many pipeline variants.
2. **Golden-image GPU tests with a perceptual comparison and an HTML report.**
   Extend `erhe_graphics_gpu_tests` so image tests write PNG/PFM artifacts,
   compare against `test/golden/` with FLIP (or a documented tolerance) and
   emit a JSON summary an HTML viewer renders as output/golden/diff triptychs,
   with an `--update-goldens` mode. agfx precedent: `test_framework.h`,
   `test_compare.cpp`, `tools/test_report/index.html`. This is the change
   that would have caught several shadow and GI regressions earlier.
3. **GPU-written draw count and indirect dispatch.** Add
   `draw_indexed_indirect_count` (`vkCmdDrawIndexedIndirectCount`, GL
   `ARB_indirect_parameters`, Metal CPU fallback) and `dispatch_indirect` to
   the encoders, plus `Buffer_usage::indirect` count buffers. agfx precedent:
   `agfxIndirectBundle` with `maxCountCount` and `countIndex`
   (`agfx.h:752-884`). Enables GPU frustum and occlusion culling in
   `Draw_list_renderer`.
4. **User-facing push constants.** Expose a 128-byte push-constant range in
   `Bind_group_layout` and `set_push_constants` on both encoders (Vulkan
   native, GL uniform block, Metal buffer 15 which already exists for
   `ERHE_DRAW_ID`). Cheap, and a prerequisite for item 5.
5. **Generalize the texture heap into a resource heap.** Add bindless
   storage buffers, storage images and depth textures alongside the existing
   combined image samplers, using `VK_EXT_mutable_descriptor_type` where
   available and separate descriptor arrays otherwise, with 32-bit handles
   returned at allocation. agfx precedent: the single global set at
   `agfx_vulkan.cpp:625-735` and the HLSL wrappers in `data/shaders/agfx.h`;
   erhe would mirror the wrappers as GLSL macros in the preamble. Medium
   effort; it removes per-draw push-descriptor traffic for material and
   instance data.
6. **Metal GPU timestamps.** Implement `Gpu_timer` on Metal with counter
   sample buffers so the Metal backend stops reporting zero. agfx precedent:
   `agfxQueryPool` on Metal 4. Small, self-contained.
7. **Per-attachment blend state.** Replace the single `Color_blend_state`
   with an array indexed by attachment (Vulkan and Metal support it natively;
   GL via `glBlendFuncSeparatei`). agfx precedent: the `[8]` arrays in
   `agfxRenderPipelineCreateInfo`. Small API change; unblocks MRT passes that
   need additive and replace targets in one pass.
8. **Per-object native handle accessors.** A `native.hpp` that returns the
   backend object behind a `Buffer`, `Texture`, `Command_buffer` and
   `Device` under an `ERHE_EXPOSE_VULKAN`-style guard, with the same borrowed-
   ownership and "outside a pass only" rules agfx documents in
   `agfx_native.h`. Needed for Tracy GPU zones and any upscaler experiment.
9. **Async transfer and compute queues.** Create dedicated transfer and
   compute queues when the device has distinct families, route
   `Buffer_transfer_queue` flushes and acceleration structure builds to them,
   and hand off with the existing frame timeline semaphore. agfx precedent:
   `agfxCommandQueueType` with `agfxCommandQueueSignal/Wait`. Adopt the
   spec-clean form (`VK_SHARING_MODE_CONCURRENT` or explicit family
   transfers), which agfx's comment describes but its code does not do.
   Medium-high effort; the payoff is upload and GI probe work off the
   graphics queue.
10. **Acceleration structure update and compaction.** Add refit
    (`allow_update`, `update(cb)`) and compaction (compacted-size query, copy)
    to `Acceleration_structure`. agfx precedent: `agfxComputePassUpdate/
    Compact/WriteCompactedSizeToBuffer` (`agfx.h:975-1000`). Medium; matters
    for skinned meshes and memory on Quest.
11. **Placement heaps for transient render-graph resources.** Let the
    rendergraph allocate short-lived attachments from a per-frame heap with
    aliasing barriers derived from node ordering, using VMA's
    `VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT` which erhe already maps. agfx
    precedent: `agfxHeap` and `agfxCommandBufferAliasingBarrier`. Medium;
    value is memory, mostly on the Quest.
12. **Mesh and task shaders.** `VK_EXT_mesh_shader` stages in
    `Shader_stages_create_info`, `draw_mesh_tasks` and its indirect form on
    the encoder, capability in `Device_info`. agfx precedent: task/mesh
    modules and group sizes in the pipeline create info. High effort across
    three backends (no GL path); the first consumer would be the wide-line
    and solid-wireframe renderers that currently expand geometry in compute.
13. **Offline shader compile and validation CLI.** A `scripts/` Python tool
    or small executable that runs glslang over every shader variant the
    editor can request and fails CI on errors, mirroring
    `agfx_shader_cli`. Low effort; catches shader breakage without a GPU.
14. **Topic skills with call sequences and traps.** agfx's per-topic skills
    (synchronization, resources, bindless, MDI) are the form erhe's backend
    documents lack: minimal correct call sequence, then the traps with
    evidence. Fold that structure into `doc/erhe/vulkan_backend.md` and
    `gpu_coding_rules.md` sections rather than adding new files.

Not recommended now: a D3D12 backend (no erhe target platform needs it), an
immediate-mode "ez" layer (the editor's renderers already own that role), a
C ABI (no bindings consumer), switching the shader language to HLSL (the
GLSL preamble generation and SPIR-V cache are load-bearing).

## 7. Feasibility of agfx as an erhe::graphics backend

The question is whether `erhe::graphics` could sit on agfx, either replacing
its own backends or as a fifth `ERHE_GRAPHICS_API` value that wraps
`agfx.h` behind the existing pimpl classes. The wrapping shape itself is
natural: every erhe public class already delegates to a `*_impl`, and agfx's
objects (`agfxBuffer`, `agfxTexture`, `agfxCommandBuffer`, `agfxRenderPass`)
map one-to-one onto `Buffer_impl`, `Texture_impl`, `Command_buffer_impl` and
`Render_pass_impl`. Everything below the object mapping is where the
obstacles are.

### Blocking obstacles

1. **Platform coverage.** agfx builds Vulkan only on Linux, D3D12 on Windows
   and Metal 4 on macOS 26+. erhe's primary development platform is Windows
   Vulkan (RenderDoc, validation, `build_vs2026_vulkan_headless`); its
   shipping XR target is the Quest 3, whose driver reports Vulkan 1.1 to 1.3,
   while agfx requires Vulkan 1.4 with `VK_EXT_mutable_descriptor_type`
   (`agfx_vulkan.cpp:439-469`). Adding a Win32 surface to agfx's Vulkan
   backend is small; the 1.4 and mutable-descriptor requirements exclude the
   Quest and older desktop drivers outright. There is no OpenGL backend and
   no null backend, so `ERHE_GRAPHICS_API=none` builds and the Quest build
   would keep erhe's own backends regardless, which means agfx would be an
   additional backend, not a replacement.
2. **Shader binding model.** agfx has exactly one binding mechanism: 128
   bytes of push constants holding 32-bit descriptor indices, resolved through
   a single global descriptor set. erhe's shaders bind uniform and storage
   blocks by offset into ring buffers through `Bind_group_layout`, and the
   GLSL block declarations are generated by `Shader_resource`. Running on agfx
   means every shader reads its blocks as `buffer Block { } blocks[]` indexed
   by handle and every texture through a handle, so all erhe shaders
   (dozens of GLSL files plus the generated preamble) and every renderer's
   binding code change. On Vulkan the SPIR-V that glslang emits can target the
   agfx set layout (aliased typed arrays on the mutable binding are legal), so
   GLSL could stay; on Metal agfx expects DXIL converted by Apple's Metal
   Shader Converter with agfx's argument-table layout, so the Metal path
   requires HLSL. Keeping the GLSL preamble, the SPIR-V cache and hot reload
   therefore only works for the Vulkan and D3D12 (via a DXIL translation that
   does not exist for GLSL) backends; in practice the shader language changes.
3. **Missing render features erhe uses today.** From grep of `src/editor` and
   `src/erhe/scene_renderer`: MSAA render targets with resolve (viewport
   resources, DDGI), stencil (`app_rendering.cpp`, `sky_renderer.cpp`, debug
   renderer), D16 and D24S8 depth formats (shadow maps select 16- or 32-bit
   depth by preset), integer formats (`format_16_vec2_uint`,
   `format_32_vec4_uint`, `format_8_vec4_uint`, `format_16_vec4_sint` for ID
   and data render targets), multiview `view_mask` (`headset_view.cpp`),
   fragment density maps, depth bias, blend constants and write masks,
   conservative rasterization, cube arrays and texture buffers. agfx has
   none of these (`agfx.h:449-512`, `1597-1674`). Each is either an agfx
   feature request or an erhe renderer rewrite.
4. **Synchronization model inversion.** erhe's rendergraph declares
   `usage_before` / `usage_after` per attachment and the backend derives
   layouts and barriers; textures track their layout; destruction is deferred
   to frame completion. agfx tracks nothing and destroys immediately. An agfx
   backend would need a resource-state tracker of its own (the ez layer's is
   explicitly best-effort and does not see bindless reads), a deferred
   destruction queue keyed on `agfxFence` values, and correct `agglomerate`
   values on every barrier for Metal. This is a reimplementation of the parts
   of `vulkan_render_pass.cpp` and `vulkan_texture.cpp` that carry the most
   validation-derived knowledge.

### Significant obstacles

5. **Memory allocation.** agfx makes one dedicated allocation per resource
   unless the caller places it in an `agfxHeap` at a hand-computed offset.
   erhe scenes with thousands of textures and per-frame buffers would exceed
   `maxMemoryAllocationCount` (4096 on many drivers), so erhe would have to
   write a sub-allocator over `agfxHeap`, duplicating what VMA does now, and
   lose `VK_EXT_memory_budget` reporting.
6. **Threading.** No agfx backend contains a lock; the descriptor slot
   allocators and the Metal barrier tracker are single-threaded. erhe records
   from worker thread slots (`mcp_server_mesh_components.cpp:107`, the
   lightmap baker). erhe would serialize all agfx calls behind one mutex or
   patch agfx.
7. **Presentation and frame pacing.** agfx owns the swap chain and exposes no
   `VkSwapchainKHR` accessor in `agfx_native.h`, so erhe's present-wait,
   present-timing and calibrated-timestamp pacing, the emulated swapchain for
   headless capture, `swapchain_maintenance1` present-mode switching and
   Android pre-rotation are all unreachable. Headless rendering itself is
   possible (agfx's tests render to textures without a swap chain).
8. **Streaming and readback.** `agfxBufferMap` on `CPU_TO_GPU` and
   `GPU_TO_CPU` memory covers `Ring_buffer` and readback, but there is no
   flush or invalidate range API, so non-coherent memory correctness depends
   on agfx always choosing coherent types (it does on the current backends;
   unspecified in the API).
9. **Maturity and ownership.** agfx is at v2.0.0 with one maintainer, the
   Vulkan backend is two months old and, per its README, mostly AI-written;
   this review found a comment/code contradiction on sharing mode
   (section 4) and a documented descriptor-slot reuse race. Breaking API
   changes are normal at this stage. erhe would fork it, as it forks SDL,
   ImGui and geogram, and carry the fixes.

### What agfx would buy

A D3D12 backend with PIX integration, Metal 4 on Apple Silicon (dropping
Metal 3 and macOS versions before 26), mesh shaders, acceleration-structure
update and compaction, a GPU count buffer for indirect draws, and the golden
test suite as a conformance check for erhe's own use of the API. Every one of
these except D3D12 is adoptable into erhe's existing backends at lower cost
(section 6), and D3D12 is not needed by any erhe target platform.

### Verdict

Using agfx as the backend is not feasible as a replacement (items 1, 3, 4)
and not worthwhile as an additional Windows-only backend: the shader binding
rewrite (item 2) is the same work whether one backend or three sits under it,
and the missing features (item 3) would have to be added to agfx first. The
productive relationship is the one in section 6: adopt agfx's bindless
resource heap, count-buffer indirect draws, persistent pipeline cache,
golden-image testing and native escape hatch inside erhe's own backends, and
use agfx's test suite as a checklist for `erhe_graphics_gpu_tests` coverage.

## 8. Housekeeping found during the review

Items in erhe that the comparison surfaced and that are cheap to fix
independently of section 6: remove the stale `command_queue.hpp` stub;
correct the texture-heap descriptor count in `vulkan_backend.md`; replace
`src/erhe/graphics/Readme.md` and delete `claude_review.md` in favor of
`doc/erhe/graphics.md`; decide whether `textureCompressionASTC_LDR` should be
enabled on Vulkan given the KTX2 loader can transcode to ASTC.
