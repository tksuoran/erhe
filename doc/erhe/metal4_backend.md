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
Conservative: the barriers serialize all encoders; narrowing them to the
stages and resources that conflict is future work.

## Present

For the command buffer that engaged the swapchain, `submit_command_buffers`
calls `queue->wait(drawable)` before the commit, adds the drawable's presented
handler (`Present_feedback`) and counts the presented-frame budget, commits,
then calls `queue->signalDrawable(drawable)` and presents with the drawable's
own `presentAtTime(target - request offset)` (tier M with a target) or
`present()`. `present_request_time` / `present_return_time` bracket the commit
and the present. The present calibration probes present the same way. Every
frame pacing input of `doc/erhe/metal_backend.md` "Frame pacing" is kept: the
achieved present time and present wait from the presented handler, the GPU
bracket from the commit feedback, the acquire times around `nextDrawable`.

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
| Counter heaps (GPU timers, Tracy GPU zones) | MTL4 `MTL4CounterHeap` ("GPU timers"); in a Tracy build the default scene shows the same GPU zones as Metal 3 (`Main`, `Post_processing`, `Overlay`) on the program timeline, with medians 0.87 / 0.37 / 0.16 ms against Metal 3's 0.73 / 0.32 / 0.15 ms (Release, windowed, unpaced) |
| `MTL4Compiler` (shaders, pipelines) | Metal 3 code (`newLibrary`, `newRenderPipelineState`) |
| GPU-read draw count (`executeCommandsInBuffer`) | MTL4 encoders and residency; verified by the `Draw_indirect_test` count tests |

## Future work

The ports and checks of `doc/plans/metal_backend.md` section 7: the
`MTL4Compiler` port, narrowing the barriers and the frame pacing live gate.
In the unpaced Release bench (`scripts/render_thread_bench.py`, default
scene, pipelined, windowed) the render phase takes 2.9 ms per frame against
1.65 ms on Metal 3, most of it in the `swapchain` term (2.2 ms against
1.2 ms); where that time goes is not measured yet.
