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
barriers, and drawables are presented through the queue (the sections
below). Shaders and pipelines still come from the Metal 3 API (MSL compiled
with `newLibrary`, `newRenderPipelineState` / `newComputePipelineState`), which
the MTL4 encoders accept. Everything else `doc/erhe/metal_backend.md`
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
  commands reference (MTL4 command buffers retain nothing: the per-call render
  and compute pipeline states and depth stencil states, texture views, all
  through `Command_buffer_impl::retain_until_completed`), returns its argument
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
until the presenting command buffer completed, and every later encoder's
queue barrier waits for the pre-warm kernel: with a pre-warm requested, the
next frame's GPU work starts only after the previous frame and its pre-warm.
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

MTL4 tracks no hazards. Every encoder starts with
`barrierAfterQueueStages(all stages, the encoder's stages,
VisibilityOptionDevice)`, a consumer barrier on all earlier work of the
queue - earlier encoders of its command buffer and every earlier commit.
This stands in for the Metal 3 inter-encoder fence and for Metal's hazard
tracking (compute to render, render to compute, uploads to consumers, the
GPU-read draw count kernel to `executeCommandsInBuffer`, a draw set's
commands to the next prepare that rewrites the ICB). Dispatches inside one
MTL4 compute encoder run concurrently (Metal 3 compute encoders were serial),
so every dispatch after the first starts with
`barrierAfterEncoderStages(dispatch | blit | acceleration structure)`, and
`Command_buffer::memory_barrier` issues the same barrier when a compute
encoder is open on the calling thread (between encoders the queue barrier
already covers it). The pre-warm kernel starts without a barrier (it touches
nothing else), but later encoders' barriers wait for it ("Submission").
The unconditional encoder-start barrier is temporary: commit B of
`doc/plans/resource_barriers.md` removes it once the application chains its
dependencies explicitly.

Explicit barriers (`Command_buffer::texture_barrier` / `buffer_barrier`,
`doc/erhe/graphics.md` "Resource barriers") are stage barriers here: MTL4 has
neither per-resource barriers nor layouts, so the resource, the range and
`discard` are not used. Each call maps its two usages to `MTL::Stages`
(`transfer_*` -> blit; `sampled` / `storage` / `input_attachment` /
`uniform` / `storage_*` -> the stages the `vertex_shader` / `fragment_shader`
/ `compute_shader` qualifiers name, vertex | fragment | dispatch without
one; `indirect` -> vertex | dispatch; attachments -> fragment, where tile
loads, depth tests, stores and resolves happen; `vertex` / `index` ->
vertex; acceleration structure accesses -> acceleration structure; `none`,
`present`, host accesses -> no stage, so no barrier) and merges them into one
of two pending barriers on `Command_buffer_impl`, chosen by the encoder kind
of the consumer stages (render: vertex / fragment / tile / object / mesh;
compute: dispatch / blit / acceleration structure). The pending barriers are
two stage-mask pairs (no allocation). Every encoder recorded into the
command buffer starts with `Command_buffer_impl::encode_start_barriers`,
which encodes the unconditional barrier and then, when the pending barrier
of the encoder's kind is set, one `barrierAfterQueueStages(union of after
stages, union of before stages, VisibilityOptionDevice)` and clears it. A
transfer recorded as a render encoder (`blit_framebuffer`'s draw, a depth /
stencil `clear_texture`, `Render_encoder_use::transfer`) also encodes the
pending compute barrier when it has blit-stage consumers, as a barrier
before the render stages, and leaves it pending for the compute encoders
after it. `end_recording` drops a pending barrier no encoder consumed and
logs the stage pair at debug level (`log_render_pass`).
`barrierAfterQueueStages` is the one primitive between encoders for both
scopes: it is a consumer barrier on all work before the current encoder,
earlier encoders of the same command buffer and earlier commits alike,
while `barrierAfterEncoderStages` orders only inside one pass (Apple's
`MTL4CommandEncoder.h`).

A barrier recorded while a compute encoder is open on the calling thread
(verified to record into the same command buffer) still enters the pending
union, and a compute consumer is also ordered inside the open encoder at
once: `barrierAfterEncoderStages(after, before)` against the encoder's
earlier dispatches, plus, for `Barrier_scope::earlier_submissions`,
`barrierAfterQueueStages(after, before)` for the earlier commit (Apple
documents the queue barrier for work before the current encoder only, so
the encoder barrier is not left to it).

`ERHE_METAL4_EXPLICIT_BARRIERS_ONLY=1` skips the unconditional barrier in
`encode_start_barriers` and the per-dispatch `encode_intra_encoder_barrier`,
so the `Resource_barrier_test` cases of `erhe_graphics_gpu_tests` verify the
explicit path on its own: each case passes with its barriers and fails
without them under the switch. It is a test switch that lives until the
unconditional barriers are removed; the editor is not correct with it set.

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
render passes at the vertex stage after the queue barrier and at the fragment
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

## Migration status

One row per subsystem. "Metal 3 code" means the subsystem is the unchanged copy
of the Metal 3 backend.

| Subsystem | State |
| :--- | :--- |
| Device, `MTL4CommandQueue`, `MTL4CommandBuffer`, `MTL4CommandAllocator`, `MTL4CommitFeedback` | MTL4 ("Submission"); the race gate (`ERHE_USE_TSAN` tree `build_xcode_metal4_tsan`, overlap and pipelining, `doc/agents/macos.md` "ThreadSanitizer") reports nothing |
| Argument tables (`MTL4ArgumentTable`), residency sets (`MTLResidencySet`) | MTL4 ("Residency", "Binding") |
| Render, compute and blit encoders, barriers | MTL4 ("Encoders and barriers"); barriers conservative |
| Swapchain, drawable, frame pacing | MTL4 ("Present"); tier M runs and calibrates; the frame pacing live gate (`doc/plans/frame_pacing_metal.md`, Dell 120 Hz, with the C26 pre-warm) is pending: it needs the user at the display |
| Counter heaps (GPU timers, Tracy GPU zones) | MTL4 `MTL4CounterHeap` ("GPU timers"); in a Tracy build the default scene shows the same GPU zones as Metal 3 (`Main`, `Post_processing`, `Overlay`) on the program timeline, with medians 0.73 / 0.32 / 0.15 ms, the same as Metal 3's (Release, windowed, unpaced) |
| `MTL4Compiler` (shaders, pipelines) | Metal 3 code (`newLibrary`, `newRenderPipelineState`) |
| GPU-read draw count (`executeCommandsInBuffer`) | MTL4 encoders and residency; verified by the `Draw_indirect_test` count tests |

## Future work

The ports and checks of `doc/plans/metal_backend.md` section 7: the
`MTL4Compiler` port, narrowing the barriers and the frame pacing live gate.
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
