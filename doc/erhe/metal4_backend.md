# Metal 4 Graphics Backend

Stability: experimental

The Metal 4 backend implements `erhe::graphics` on Apple's Metal 4 API (the
`MTL4*` classes, macOS 26 and later) beside the Metal 3 backend
(`doc/erhe/metal_backend.md`). It is built subsystem by subsystem; the plan,
its order and its verification are in `doc/plans/metal_backend.md` section 7
"Metal 4".

## Present state

Commands are recorded into `MTL4::CommandBuffer`s and committed on an
`MTL4::CommandQueue`, resources are resident through a residency set,
encoders bind through argument tables and order their work with explicit
barriers, drawables are presented through the queue, and libraries and
pipeline states come from an `MTL4::Compiler` backed by a pipeline archive
that persists across runs (the sections below). Everything else
`doc/erhe/metal_backend.md`
describes (shader translation, the buffer index layout, the texture heap
layout, the swapchain, frame pacing and the present calibration, the
GPU-read draw count, GPU timers, deferred destruction) applies unchanged
except where a section here says otherwise. The images and test results
are those of the Metal 3 backend.

The device requires `MTL::GPUFamilyMetal4`, logs `Metal 4 backend, device:
<name>` at startup and reports `Device_info::api_info` as `Metal 4 (<name>)`;
the editor window title ends in `Metal 4`.

## Build

```bash
scripts/configure_xcode_metal4.sh
cmake --build build_xcode_metal4 --target editor --config Debug
METAL_BACKEND=metal4 scripts/run_graphics_tests_metal.sh
```

`ERHE_GRAPHICS_API=metal4` (Apple platforms only) uses the same CPM packages
as `metal` (metal-cpp `metal-cpp_27`, which carries the `MTL4*` headers, and
the SPIRV-Cross fork). The metal-cpp umbrella header `Metal/Metal.hpp` does
not include `MTL4AccelerationStructure.hpp`; the acceleration structure code
includes it itself.

## Defines

- `ERHE_GRAPHICS_API_METAL` names the Metal family. Both `metal` and `metal4`
  define it, so every site that only asks "is this Metal" (shader
  translation in `glsl_to_spirv.cpp` and `shader_stages_create_info.cpp`, the
  SDL window flag, the sky renderer, frame capture in `device.cpp`, the
  autorelease pools of `Scoped_transient_object_pool`) serves both backends.
- `ERHE_GRAPHICS_API_METAL4` is defined only by `metal4` and selects the Metal
  4 backend. Code that differs between the backends tests it first:

```cpp
#if defined(ERHE_GRAPHICS_API_METAL4)
# include "erhe_graphics/metal4/metal4_device.hpp"
#elif defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_device.hpp"
#endif
```

## Source layout

`src/erhe/graphics/erhe_graphics/metal4/` holds one `metal4_<name>` file per
`metal/metal_<name>` file, plus `metal4_submission.{hpp,cpp}` (residency set,
submission pools, barrier helpers). The pimpl classes keep their names
(`Device_impl`, `Command_buffer_impl`, ...) because a build tree compiles
exactly one backend. Backend-neutral Apple code stays shared in
`erhe_graphics/` (`clock_monitor_apple.mm`, `thermal_state_apple.mm`), as does
the generated `Metal_config` (`definitions/metal_config.py`).
`metal4_pixel_format_table.cpp` is generated with the Metal 3 table by
`scripts/metal_format_tables/generate.py`.

The GPU tests compare against the same goldens as every backend
(`src/erhe/graphics/test/golden/`); `results.json` names the backend
`metal4`.

## Submission

`Device_impl` owns one `MTL4::CommandQueue` (`newMTL4CommandQueue`).
`Command_buffer_impl::begin` takes a `Recording` from `Submission_pools`: an
`MTL4::CommandBuffer` + `MTL4::CommandAllocator` pair, begun with
`beginCommandBuffer(allocator)`, plus the lists of what the recording holds
until the GPU has completed it. The recording returns to the pool from its
commit feedback with its allocator reset and its lists cleared (capacity
kept), so allocators are never reset under in-flight commands, the pool
reaches a high-water mark (about one recording per command buffer in
flight) and steady-state commits allocate nothing of the backend's own. The
per-thread command buffer bags and frame retirement of
`doc/erhe/metal_backend.md` "Thread safety" are unchanged.

`Device_impl::submit_command_buffers` ends each recording
(`endCommandBuffer`) and commits it alone, under the device's submit mutex,
through `commit_locked`: the residency set is committed when it changed, the
commit gets a serial that stays in flight until its `MTL4::CommitFeedback`
handler (a Metal dispatch thread) has run, and the handler - a plain block over
the device and the recording, on an `MTL4::CommitOptions` created per commit
(an options object created ahead of time and reused delivered no feedback at
all, measured) - calls `Device_impl::on_commit_feedback`, which does
everything the Metal 3 completed handlers did:

- logs `feedback->error()` ("Metal command buffer '<label>' of frame <n>
  failed");
- for the presenting command buffer, writes `gpu_frame_begin` /
  `gpu_frame_end` from `GPUStartTime` / `GPUEndTime` (through the clock
  monitor in tier M) and returns the presented-frame budget;
- resolves the command buffer's GPU timer samples;
- returns what the recording held: runs its callbacks (staging buffer
  releases, the windowed capture's completion), releases the objects its
  commands reference (MTL4 command buffers retain nothing: the texture views
  created per call, through `Command_buffer_impl::retain_until_completed`;
  pipeline and depth stencil states live in `Pipeline_state_cache`, "Shaders
  and pipelines"), returns its argument
  tables, inline constant chunks and the recording;
- counts the completion against the frame the commit was charged to.

A commit is charged to the frame its command buffer was begun in, or, when
that frame has retired already (a recording committed after its frame
retired), to the oldest frame not retired yet. Frames retire in order and
only once every commit charged to them has run its feedback - `wait_idle`
included, which waits for the feedback of every commit made before it - so
no deferred release frees what an in-flight commit uses (verified: a
completion always finds its frame's entry, and a retiring frame has
`completed == committed`).

A presenting command buffer commits as two MTL4 command buffers: the work
before the drawable's first use when the drawable is taken, the rest at
submit ("Present"). Both take a recording from the pool and both are charged
to the command buffer's frame (the late-commit rule above applies to each).
The first part is committed with the CPU waits and queue waits registered on
the command buffer up to that point, so a producer the command buffer waits
for (`wait_for_cpu_completion_of`, `wait_for_gpu_completion_signal`) must be
submitted before the consumer reaches its swapchain pass - not in the same
`submit_command_buffers` batch after it. `commit_drawable_prefix` VERIFYs
that every such producer has queued its signal, instead of waiting forever.
Waits registered after the first part apply to the rest.

`wait_idle` waits until no commit up to the latest serial is in flight
(`wait_for_commit`), which also guarantees that their feedback handlers have
finished; `submit_command_buffer_and_wait` waits for the serial of its own
commit only (and the commits before it), not for later commits of other
threads, such as a presenting commit held back by `queue->wait(drawable)`.
A command buffer destroyed without a commit returns its recording at once.
The device teardown waits for the GPU first, before the surface, the
swapchain textures and the pipelines are released.

Cross command buffer sync uses the same `MTL::Event` / `MTL::SharedEvent`
pair per command buffer as Metal 3, on the queue: `wait_for_gpu_completion_signal`
becomes `queue->wait(event, value)` before the command buffer's commit,
`signal_gpu_completion_to` and `signal_cpu_completion` become
`queue->signalEvent` after the producer's commit. A queue wait holds back
every later commit of the queue, not only its own command buffer, so the
producer must be committed before the consumer: a consumer committed first
(an XR producer / consumer pair submitted in the wrong order, or from two
threads racing) waits on the GPU forever, and with it everything committed
after it.

Backend-internal work (the headless swapchain readback, the GPU pre-warm, the
present calibration probes) records into a recording from
`Device_impl::begin_internal_recording` and commits through the same
`commit_locked`, so it is ordered, resident and covered by `wait_idle`; its
feedback is a plain function pointer with arguments in the recording (no
`std::function` per frame). The pre-warm is ordered after the presenting
commit by `queue->signalEvent(event)` right after that commit and
`queue->wait(event)` before its own. Unlike Metal 3, where the next frame did
not depend on the pre-warm, that queue wait holds back every later commit
until the presenting command buffer completed: with a pre-warm requested,
the next frame's GPU work starts only after the previous frame.
The pacer's C26 tuning (`doc/plans/frame_pacing_metal.md`) was measured on
Metal 3 and is not live-gated on this backend.

## Residency

`Residency` owns one `MTL::ResidencySet`, attached to the queue with
`addResidencySet`. Every buffer, texture, indirect command buffer and
acceleration structure the backend allocates is added at creation and removed
in its deferred release (`Buffer_impl`, `Texture_impl`, the emulated
swapchain textures, the capture buffer, staging buffers, the indirect count
draw buffers and ICBs, acceleration structures and their scratch, instance and
transform buffers, the pre-warm buffer, the pools' own chunks). Texture views
are not added: they share their parent's allocation. Additions and removals
only mark the set changed; it is committed at most once per queue commit, in
`commit_locked`, and a removed allocation is kept referenced until that
commit. The `CAMetalLayer`'s own residency set (its drawables) is attached to
the queue by `Surface_impl`.

This replaces every `useResource` of the Metal 3 backend: the texture heap's
per-bind loops, the index buffers and ICB of the GPU-read draw count, the
bottom level structures an instance structure references.

## Binding

Each encoder gets its own `MTL4::ArgumentTable` (31 buffers, 31 textures, 16
samplers) from `Submission_pools`, held by the command buffer until the GPU has
completed it. A render pass binds its table to the vertex and fragment stages
together (the buffer index layout is the same for both); a compute encoder
binds its own. MTL4 encoders snapshot a table's bindings at each draw and
dispatch, so a table is rewritten freely between draws of its encoder and never
shared between encoders.

- Buffers: `setAddress(gpuAddress + offset, index)` at the indices of
  `doc/erhe/metal_backend.md` "Explicit Metal Buffer Index Layout"
  (SPIRV-Cross output unchanged); vertex buffers at index 16 and up;
  acceleration structures with `setResource(gpuResourceID, 13)`.
- Textures and samplers: `setTexture(gpuResourceID, index)` and
  `setSamplerState(gpuResourceID, index)` (samplers are created with
  `supportArgumentBuffers`).
- Index buffers are draw arguments (`drawIndexedPrimitives` takes the index
  buffer's GPU address and remaining length; indirect draws take the indirect
  buffer's address).
- Push constant (index 15, `ERHE_DRAW_ID`): an immutable draw id table of
  16-byte entries holding 0, 1, 2, ... (`Submission_pools::get_draw_id_table`,
  4096 entries, grown to the next power of two when a multi-draw needs more,
  earlier tables kept until the pools go). Draw i of a multi-draw points
  index 15 at entry i; every pipeline bind, every encoder start and
  `executeCommandsInBuffer` point it at entry 0 of the first table
  (`get_draw_id_zero`, fixed for the pools' lifetime, read without a lock).
  No per-draw data is written.
- Inline constants (what `setBytes` carried: the `blit_framebuffer` source
  offset): `Command_buffer_impl::allocate_inline_bytes` copies them into
  shared, resident 64 KiB chunks the command buffer takes from the pools
  (256-byte aligned) and returns with its recording. The GPU-read draw count
  kernel's parameters go into its table buffer, the pre-warm's iteration
  count into a slot of its own buffer.
- The texture heap's argument buffer is still written with
  `MTL::ArgumentEncoder` (resource ids into a ring buffer range, layout
  unchanged); its address goes into the table at
  `metal_arg_buffer_index` (14).

## Encoders and barriers

Render passes use `MTL4::RenderCommandEncoder` from an
`MTL4::RenderPassDescriptor`; the attachment setup (load / store / clear
actions, MSAA color, depth and stencil resolve, levels and slices) is the
Metal 3 code, since the descriptor carries the same
`MTL::RenderPass*AttachmentDescriptor`s. Compute and every blit (buffer and
texture copies, fills, mipmap generation, the capture copies) use
`MTL4::ComputeCommandEncoder`, one encoder per blit call as the Metal 3 blit
encoders were. Acceleration structures build on a compute encoder from
`MTL4::PrimitiveAccelerationStructureDescriptor` /
`MTL4::InstanceAccelerationStructureDescriptor` (buffers as GPU address
ranges); instance structures use indirect instance descriptors, which name
their bottom level structure by `gpuResourceID`.

MTL4 tracks no hazards. The backend orders work from what the application
declares, at the level of explicitness the Vulkan backend needs: every
declaration is a stage barrier (MTL4 has neither per-resource barriers nor
layouts, so a resource, a range and `discard` are not used). MTL4 has two
queue barriers (`MTL4CommandEncoder.h`; metal-cpp carries no doc comments):

- the consumer barrier, `barrierAfterQueueStages(after, before)`: the work
  of the CURRENT encoder in the `before` stages waits for the `after` stages
  of all work before the current encoder (earlier encoders of its command
  buffer and earlier commits). It does not order later encoders.
- the producer barrier, `barrierAfterStages(after, before)`: the work of
  every LATER encoder, of this command buffer and of later commits, in the
  `before` stages waits for the `after` stages of the work encoded so far
  in the current encoder and in the prior encoders. Either side may name
  stages the current encoder does not run.

`barrierAfterEncoderStages` orders only inside one encoder.

One table maps each declaration to `MTL::Stages` (`metal4_submission.hpp`:
`get_image_usage_stages`, `get_buffer_usage_stages`,
`get_image_layout_stages`, `get_memory_barrier_stages`; the stage bits are
`constexpr` there):

| Declaration | Barrier (producer stages -> consumer stages) |
|---|---|
| Render pass begin, each attachment's `usage_before` | consumer barrier of the pass: the stages of the `usage_before`s (fragment when none names a GPU access) -> fragment, the stage of attachment loads, tests, stores and resolves and of the pass's sampling (Vulkan's inter-pass barrier) |
| Render pass end, each attachment's `usage_after` (not `user_synchronized`) | producer barrier before `endEncoding`: fragment -> the stages of the `usage_after`s (Vulkan's post-pass barrier) |
| `texture_barrier` / `buffer_barrier` / `cmd_texture_barrier` | the stages of `usage_before` -> the stages of `usage_after` |
| `memory_barrier(mask)` | every stage -> the consumer stages of the mask bits, as Vulkan's (`texture_fetch`, `shader_image_access`, `shader_storage`, `uniform`, `atomic_counter` -> vertex / fragment / dispatch; `command` -> vertex / dispatch; `vertex_attrib_array`, `element_array` -> vertex; `pixel_buffer`, `texture_update`, `buffer_update` -> blit; `framebuffer` -> fragment; `client_mapped_buffer` -> none: the CPU reads after the commit completed) |
| `transition_texture_layout(layout)` | every stage -> the stages of the layout's accesses |
| Backend transfers on a texture (`upload_to_texture`, `clear_texture`, the blit encoder's texture copies, `generate_mipmaps`, `blit_framebuffer`) | consumer barrier of the transfer's encoder: every stage -> the transfer's stage; producer barrier before its `endEncoding`: the transfer's stage -> every stage (Vulkan's transitions to and from the transfer layout); buffer-only blits (`fill_buffer`, buffer copies) none, as on Vulkan |

The usage stages: `transfer_*` -> blit; `sampled` / `storage` /
`input_attachment` / `uniform` / `storage_*` -> the stages the
`vertex_shader` / `fragment_shader` / `compute_shader` qualifiers name,
vertex | fragment | dispatch without one (an unqualified `sampled` thus
reaches both render and compute consumers); `indirect` -> vertex |
dispatch; attachments -> fragment; `vertex` / `index` -> vertex;
acceleration structure accesses -> acceleration structure; `none`,
`present`, host accesses -> no stage, so no barrier.

The application's declarations go through
`Command_buffer_impl::add_stage_barrier`. Between encoders a declaration
is pending (one stage-mask pair on `Command_buffer_impl`, the union of the
pending ones; no allocation). Every encoder recorded into the command
buffer starts with `Command_buffer_impl::encode_start_barriers`, which
encodes, when barriers are pending, the consumer barrier for their
consumers in this encoder (merged with the encoder's own pair: a render
pass's begin barrier, a transfer's wait for earlier accesses) and the
producer barrier for their consumers in every later encoder, and clears
them; with nothing pending only the encoder's own pair, and a compute
encoder without one starts with no barrier. A transfer recorded as a
render encoder (`blit_framebuffer`'s draw, a depth / stencil
`clear_texture`, `Render_encoder_use::transfer`) treats pending blit-stage
consumers as its render stages. `end_recording` encodes the barriers still
pending when the command buffer ends as a producer barrier in one empty
compute encoder: its consumers are in later commits.

A declaration made while a compute encoder is open on the calling thread
(verified to record into the same command buffer) is encoded there at once:
the encoder's later dispatches wait - `barrierAfterEncoderStages(producer
stages the encoder runs, consumer)` for its earlier dispatches, plus the
consumer barrier when the producer stages include stages a compute encoder
does not run (an earlier encoder) or the scope is `earlier_submissions` (an
earlier commit) - and a producer barrier orders every later encoder.

The backend's own recordings order what they write for the consumers they
know: the GPU-read draw count kernel
(`Compute_command_encoder_impl::encode_indirect_count_draws`) encodes a
consumer barrier from the render stages (the draws of an earlier prepare
that read the slot's indirect command buffer) before it and a producer
barrier dispatch -> vertex | fragment after it, assuming
`executeCommandsInBuffer` reads the indirect command buffer and its range
at the vertex stage; its inputs, the command and count buffers, are ordered
by the application's `memory_barrier(command | shader_storage |
pixel_buffer)` after the compaction kernel (`draw_list_scene_gpu_cull.cpp`),
which is recorded between encoders and so is encoded at the start of the
kernel's encoder. An acceleration structure build ends with the producer
barrier acceleration structure -> acceleration structure | dispatch
(Vulkan's barrier after `vkCmdBuildAccelerationStructuresKHR`). The
swapchain capture copy (`Swapchain_impl::record_capture`) starts with the
consumer barrier fragment -> blit (the swapchain pass's `usage_after`,
`present`, names no GPU consumer); the headless capture's own recording
(`Swapchain_impl::read_back_emulated`) encodes it on its encoder directly.
The present calibration probes and the pre-warm kernel start without a
barrier: a probe clears only its drawable, which the queue's drawable wait
orders, and the pre-warm touches nothing else.

Dispatches inside one MTL4 compute encoder run concurrently (Metal 3
compute encoders were serial), so every dispatch after the first starts with
`barrierAfterEncoderStages(dispatch | blit | acceleration structure)`
(`encode_intra_encoder_barrier`). This is more than Vulkan needs - the
application states no barrier between dispatches that do not depend on each
other - and stays until the dependent dispatches carry their barriers
(`doc/plans/resource_barriers.md`).

The `Resource_barrier_test` cases of `erhe_graphics_gpu_tests` check each
mapping with a slow producer and a fast consumer, including a consumer that
is the second encoder of its kind after the barrier (compute, and the
vertex stage of a render pass). On this hardware (M4 Pro) the compute cases
fail without their barrier; the second-encoder cases also passed with
consumer barriers alone, and the vertex-stage case passes even without a
barrier, so neither proves the producer barrier: that rests on the header's
semantics.

## Present

`Swapchain_impl::begin_frame` does not take a drawable: windowed, it marks
the frame's drawable pending, and the drawable is taken at its first use.
Every encoder that uses the swapchain's current texture (the swapchain render
pass, a `blit_framebuffer` from or to it) first calls
`Command_buffer_impl::acquire_drawable_for_use`, with no encoder open. At the
first use of a frame, when the command buffer engaged that swapchain
(`begin_swapchain`) and has recorded work, the recording so far is ended and
committed at once as its own MTL4 command buffer, labelled `<label> (before
drawable)` (`Device_impl::commit_drawable_prefix`), recording continues in a
second `Recording` from the pool, and then `nextDrawable` takes the drawable.
The command buffer's GPU timer samples stay with its last recording, which
resolves them. Headless there is no pending drawable and the frame is one
commit.

`submit_command_buffers` calls `queue->wait(drawable)` before the commit of
the rest - the only commit that waits for the drawable - adds the drawable's
presented handler (`Present_feedback`) and counts the presented-frame budget,
commits, then calls `queue->signalDrawable(drawable)` and presents with the
drawable's own `presentAtTime(target - request offset)` (tier M with a
target) or `present()`. `present_request_time` / `present_return_time`
bracket the commit and the present. The present calibration probes present
the same way; their result is cached under the present path `metal4`
(`doc/erhe/graphics.md` "Present calibration cache").

The split exists because MTL4 has no drawable wait inside a command buffer:
`queue->wait(drawable)` holds back the whole commit after it and every later
commit of the queue, and a drawable that `nextDrawable` returns is not yet
free for the GPU. A frame committed whole behind the drawable wait, with the
drawable taken at frame start, takes 2.9 - 3.2 ms against Metal 3's
1.7 - 2.0 ms (Release, windowed default scene, unpaced, pipelined,
`scripts/render_thread_bench.py --scene default --configs pipelined --tracy
--frames 1200`): the GPU idles 0.7 - 0.8 ms per frame and the compositor
displays 1143 of 1204 frames (Metal 3: 701 of 1204, Metal System Trace), so
drawables return only at its swaps. Committed in two parts, the CPU records
the work before the drawable instead of waiting and the GPU runs it while the
CPU waits for the drawable: the median frame is 1.46 - 1.54 ms against
Metal 3's 1.54 - 1.64 ms, and the GPU zones match Metal 3 ("Migration
status").

Frame pacing inputs (`doc/erhe/metal_backend.md` "Frame pacing"): the
presented-frame budget wait in `begin_swapchain` is the record's
`fence_wait_duration` (a wait for earlier frames' GPU completion), the
`nextDrawable` wait at the drawable's first use is its acquire
(`acquire_begin` / `acquire_end`), and the achieved present time and present
wait come from the presented handler. The GPU bracket (`gpu_frame_begin` /
`gpu_frame_end`) spans both commits, from the GPU start of the commit before
the drawable to the GPU end of the presenting commit, and the gap between the
two commits (the CPU recording the rest of the frame, `nextDrawable`, the
queue's drawable wait) is the record's `gpu_idle_time`, so
`gpu_service_time()` counts only GPU work and the pacer does not read its own
presentation wait as GPU load (`doc/frame_pacing/inputs.md`). The two commit
feedbacks run in either order on Metal's dispatch threads; each stores its
GPU start and end in a per-frame slot (`Device_impl::merge_present_gpu_bracket`,
more slots than frames in flight) and the second writes the idle time, then
the bracket (through the clock monitor in tier M). A frame whose drawable
never comes leaves a half that the next frame in its slot overwrites. In the
editor's phase log the drawable wait is in the render phase's swapchain
render pass (the `execute_rendergraph_node` zone of `window_imgui_host.cpp`
in Tracy), not in the `swapchain` term (`begin_swapchain`).

Windowed screenshot capture copies the drawable into the capture buffer on the
frame's command buffer, as Metal 3 does, with a completed callback on that
command buffer; `read_back_capture` waits for the callback once the frame has
ended (its command buffers are then committed).

## GPU timers

The timer machinery of `doc/erhe/metal_backend.md` "GPU timers" is unchanged
except for where timestamps go: MTL4 pass descriptors have no sample buffer
attachments, so the pool's buffers are `MTL4::CounterHeap`s of type timestamp
(4096 entries each, chunks invalidated when taken) and the encoders write the
boundary samples with `writeTimestamp(TimestampGranularityRelaxed, ...)` -
render passes at the vertex stage after the start barrier and at the fragment
stage before `endEncoding`, compute encoders at their start and end. Relaxed:
Apple documents that precise timestamps may split encoders, which would
change the cost being measured; relaxed ones may land later or earlier than
the boundary, so pass times read with them are not directly comparable to
the Metal 3 backend's sample buffer attachments. The samples are resolved on
the CPU (`resolveCounterRange`) in the commit feedback. Counter heap
timestamps count ticks of `MTL::Device::queryTimestampFrequency` (24 MHz on
Apple silicon, the `mach_absolute_time` clock), not the nanoseconds of
`sampleTimestamps` and the commit feedback's `GPUStartTime`; the pool
converts them with `1e9 / queryTimestampFrequency`, which puts timer results
and Tracy zone times in the nanosecond time base of the Tracy context's
`sampleTimestamps` calibration pair.

## Shaders and pipelines

`Pipeline_compiler` (`metal4_pipeline_compiler.hpp`) owns the device's one
`MTL4::Compiler`, created from an `MTL4::CompilerDescriptor` in the
`Device_impl` constructor right after the queue, before any library or
pipeline. Every library and pipeline state of the backend is created through
it, from any thread (`MTL4::Compiler` is thread safe; the statistics are
under a mutex). `~Device_impl` writes the archive and destroys the compiler
after its first `wait_idle`; pipeline states do not depend on the compiler,
so the ones released after it (the `blit_framebuffer` pipelines, the
completion handlers drained at the end) are unaffected.

- Libraries: the GLSL -> SPIR-V -> MSL path is unchanged
  (`doc/erhe/metal_backend.md` "Shader Compilation Pipeline"). The MSL goes
  into an `MTL4::LibraryDescriptor` with default `MTL::CompileOptions`, the
  options of the Metal 3 backend's `newLibrary`, so the `precise` helpers
  keep their source pragmas ("Precise arithmetic" there), and
  `compiler->newLibrary` compiles it. Each stage keeps an
  `MTL4::LibraryFunctionDescriptor` naming `main0` (it retains its library)
  where Metal 3 keeps an `MTL::Function`.
- Render pipelines: `MTL4::RenderPipelineDescriptor` and
  `newRenderPipelineState(descriptor, compilerTaskOptions)`, from
  `Render_pipeline_impl`, from `Render_command_encoder::set_render_pipeline_state`
  through `Pipeline_state_cache` (below) and for `blit_framebuffer`.
- Compute pipelines: `MTL4::ComputePipelineDescriptor` with the function
  descriptor and a label, from `Compute_pipeline_impl`,
  `set_compute_pipeline_state` through `Pipeline_state_cache` and the
  raw-MSL kernels (the GPU-read draw
  count encode kernel, the pre-warm kernel;
  `Pipeline_compiler::new_compute_pipeline_from_source`). The threadgroup
  size stays a dispatch argument, as on Metal 3. The present calibration
  probes only clear their drawables and have no pipeline.

`set_render_pipeline_state` and `set_compute_pipeline_state` take a
pipeline state description per bind, and the debug renderer's wide lines
and the content wide lines bind one every frame. `Pipeline_state_cache`
(`metal4_pipeline_state_cache.hpp`, owned by `Device_impl`, one mutex for
the main and the render thread), the counterpart of the Vulkan backend's
pipeline map, creates each distinct one once and keeps it for the device's
lifetime: render pipelines keyed on the stage function descriptors (which
the entry retains), the pass's color formats and sample count,
alpha-to-coverage / alpha-to-one, the blend state without its constant and
the vertex input layout; depth stencil states keyed on the
`Depth_stencil_state` and whether the pass has depth / stencil; compute
pipelines on the function descriptor. `Device::clear_render_pipeline_cache`
(after a shader reload) releases the entries frame-deferred. Measured on the
heavy scene with three meshes selected and physics on, 3 minutes, Release:
without the cache 215879 pipeline creations (about ten per frame, each a
compiler call and a serializer capture) and resident memory growing from
316 to 336 MB; with it 75 creations in the whole run and 322 to 328 MB. The
archive is 3.28 MB in both runs: the serializer does not duplicate an
identical pipeline.

Where each field of the Metal 3 descriptors went:

| Metal 3 | MTL4 |
| :--- | :--- |
| `vertexFunction`, `fragmentFunction`, `computeFunction` (`MTL::Function`) | `vertexFunctionDescriptor`, `fragmentFunctionDescriptor`, `computeFunctionDescriptor` (`MTL4::LibraryFunctionDescriptor`: library + name) |
| `supportIndirectCommandBuffers` (bool) | `supportIndirectCommandBuffers` (`MTL4::IndirectCommandBufferSupportState`), set from `Indirect_count_draw_support` as before |
| color attachment pixel format, blend factors and operations, write mask | the same on `MTL4::RenderPipelineColorAttachmentDescriptor`; `blendingEnabled` becomes `blendingState` (`MTL4::BlendState`) |
| `sampleCount` | `rasterSampleCount` |
| `alphaToCoverageEnabled`, `alphaToOneEnabled` | `alphaToCoverageState`, `alphaToOneState` |
| `vertexDescriptor` | `vertexDescriptor` (the same `MTL::VertexDescriptor`) |
| `depthAttachmentPixelFormat`, `stencilAttachmentPixelFormat` | none: an MTL4 render pipeline is not tied to depth / stencil formats; the depth stencil state (`MTL::DepthStencilDescriptor`, unchanged) still follows the pass's formats |
| `label` (set only for `blit_framebuffer`) | `label` on `MTL4::PipelineDescriptor`, the shader stages name for every pipeline |
| `binaryArchives` (not used) | none on the descriptor: the lookup archive is in `MTL4::CompilerTaskOptions` |

Neither backend sets `inputPrimitiveTopology`, rasterization or vertex
amplification state, shader validation or reflection options
(`MTL4::PipelineOptions`), which stay at their defaults. The Bistro viewport
pass costs the same as before the port (GPU timers, Release, headless
pacing off: 3.47 - 3.53 ms against 3.45 - 3.54 ms in the loops Medium
preset), and the image gates match.

### Pipeline archive

The compiler descriptor carries an `MTL4::PipelineDataSetSerializer`
configured to capture binaries, which keeps the GPU binaries of every
pipeline the compiler creates. Every creation passes `MTL4::CompilerTaskOptions`
whose `lookupArchives` holds the archive loaded at device creation, so a
pipeline the archive holds is not compiled again; such a pipeline is still
captured (one created by `MTL4::Archive::newRenderPipelineState` directly is
not, measured). `~Device_impl` serializes the capture with
`serializeAsArchiveAndFlushToURL`. The archive therefore holds the pipelines
of the last run: pipelines only an earlier run used are dropped.

The file is `<cwd>/pipeline_cache/metal4_<FNV-1a 64 of the GPU name>_<OS
build>.mtl4archive` (`kern.osversion`, which names the Metal compiler that
produced the binaries), beside the Vulkan backend's `vk_*.bin`
(`doc/erhe/vulkan_backend.md` "Pipeline cache persistence") and gitignored
with it. A `.manifest` file beside it holds the archive's size and FNV-1a
64 content hash (`metal4_pipeline_archive_file.hpp`), and the archive is
opened only when the manifest matches: `MTL::Device::newArchive` returns an
error for a truncated archive but crashes (SIGSEGV inside Metal) on a file
that is not an archive at all, measured with 5 bytes and with 4 KiB of
random data. An absent, truncated, modified or foreign file is ignored with
the warning `Metal 4 pipeline archive <path> ignored (<reason>); it is
rewritten at exit`, every pipeline compiles, and the file is replaced at
exit. The archive and manifest are installed from temporary files, the old
manifest removed first, so an interrupted write leaves an archive without a
matching manifest; the next write removes the temporary files a crashed
write left (`<name>.<pid>.tmp` of a process that no longer runs). Editors
running at the same time (the `mcp_server_tests` fixtures) each write the
whole file; whichever pair is installed last wins, and a mixed pair fails
the manifest check and is rebuilt. The file functions take no Metal objects and are tested
in `erhe_graphics_tests` (`test_metal4_pipeline_archive_file.cpp`).

Pipelines are keyed by content: with one fragment shader edited (a constant
in `text.frag`), a warm start of the default scene reported 59 created, 58
from archive, and the scene images stayed identical.

At the end of device frame 300 (or at device destruction when the run is
shorter) the device logs one summary line; each pipeline is created once
(`Render_pipeline`, `Compute_pipeline`, `Pipeline_state_cache`), so the
count is of distinct pipelines:

```
Metal 4 pipelines at startup: <n> created, <k> from archive, <t> ms pipeline compile; <m> libraries, <u> ms library compile
```

"from archive" counts the creations the loaded archive held, found by
asking the archive itself before the compile; only creations before the
summary pay that probe (about 0.06 ms each in a standalone check). The write logs `Metal 4
pipeline archive <path>: wrote <bytes> bytes (<n> pipeline creations this
run) in <t> ms`.

Measured (M4 Pro, macOS 27, default scene, editor started from the
repo root, time from launch to `Main loop: completed frame 12`):

| Run | Release | Debug |
| :--- | :--- | :--- |
| Cold (no archive) | 59 created, 0 from archive, 168 - 173 ms pipeline compile; frame 12 at 1.67 - 1.81 s | 59 created, 0 from archive, 174 - 177 ms; frame 12 at 6.46 - 6.59 s |
| Warm | 59 created, 59 from archive, 6.9 - 7.2 ms; frame 12 at 1.53 - 1.59 s | 59 created, 59 from archive, 7.6 - 7.7 ms; frame 12 at 6.35 - 6.44 s |

The archive is 2.9 MB; checking it at start takes under 25 ms (Debug),
writing it 90 - 140 ms. The 282 libraries compile in about 8 ms in every run
because the system's Metal shader cache serves the MSL compile; it does not
serve MTL4 pipeline compiles (a standalone check: unique pipelines take
about 8 ms each on every run without the archive, 0.06 ms with it).

Pipelines are created synchronously, at `Render_pipeline` construction or
when a `Base_render_pipeline` first creates the variant a draw needs.
`MTL4::CompilerTask` (the completion-handler overloads of the compiler)
would let creation run in the background, but a draw that finds its pipeline
pending has nowhere to go: `Render_pipeline` has no pending state on any
backend, so it is a follow-up ("Future work"), not part of this port.

## Migration status

One row per subsystem; every subsystem is on the MTL4 API.

| Subsystem | State |
| :--- | :--- |
| Device, `MTL4CommandQueue`, `MTL4CommandBuffer`, `MTL4CommandAllocator`, `MTL4CommitFeedback` | MTL4 ("Submission"); the race gate (`ERHE_USE_TSAN` tree `build_xcode_metal4_tsan`, overlap and pipelining, `doc/agents/macos.md` "ThreadSanitizer") reports nothing |
| Argument tables (`MTL4ArgumentTable`), residency sets (`MTLResidencySet`) | MTL4 ("Residency", "Binding") |
| Render, compute and blit encoders, barriers | MTL4 ("Encoders and barriers"); barriers from the application's declarations, the per-dispatch barrier conservative |
| Swapchain, drawable, frame pacing | MTL4 ("Present"); tier M runs and calibrates; the frame pacing live gate (`doc/plans/frame_pacing_metal.md`, Dell 120 Hz, with the C26 pre-warm) is pending: it needs the user at the display |
| Counter heaps (GPU timers, Tracy GPU zones) | MTL4 `MTL4CounterHeap` ("GPU timers"); in a Tracy build the default scene shows the same GPU zones as Metal 3 (`Main`, `Post_processing`, `Overlay`) on the program timeline, with medians 0.73 / 0.32 / 0.15 ms, the same as Metal 3's (Release, windowed, unpaced) |
| `MTL4Compiler` (shaders, pipelines), `MTL4Archive` | MTL4 ("Shaders and pipelines"); the image gates, the Bistro image and the GPU culling check match the Metal 3 compiled build; the archive serves every pipeline of a warm start |
| GPU-read draw count (`executeCommandsInBuffer`) | MTL4 encoders and residency; verified by the `Draw_indirect_test` count tests |

## Future work

The checks of `doc/plans/metal_backend.md` section 7: narrowing the barriers
and the frame pacing live gate. Pipeline creation through `MTL4::CompilerTask`
(asynchronous, "Shaders and pipelines") needs a pending state for
`Render_pipeline` that draws skip.
In the unpaced Release bench (`scripts/render_thread_bench.py`, default
scene, pipelined, windowed, `displaySyncEnabled` off) the median frame is at
Metal 3's, but the mean is not (2.0 - 2.4 ms against 1.6 - 1.8 ms): about
one `nextDrawable` wait in twelve takes 6 - 8 ms, about one 120 Hz refresh,
with no compositor swap during the wait (111 of 1329 waits, 0.82 s of a
2.4 s run, Metal System Trace; Metal 3: 11 of 1211). The tail disappears
without `queue->wait(drawable)`, so it belongs to the MTL4 drawable wait;
how to avoid it is open. In the same captures the Tracy GPU zone counts of
the Metal 4 build are below the frame count in some runs (Main 566 - 792 of
980 - 1130 frames) and equal to it in others; Metal 3's always equal it.
