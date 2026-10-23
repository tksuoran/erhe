# Metal backend: parity work

Status: in progress

Extends `doc/erhe/metal_backend.md`. The items below are where the Metal
backend still differs from Vulkan; each is verified on macOS with the
Xcode build (`doc/agents/macos.md`) and, where a GPU test covers it, with
`erhe_graphics_gpu_tests` on Metal.

## 5. Display-time pacing tier

Tiers S and M are built (`doc/erhe/metal_backend.md` "Frame pacing"). Tier S
with three drawables keeps the 120 Hz cadence but runs its sleep at the
clamp with about 5.6 ms of `nextDrawable` wait left: the queue depth needs display-time
sensing to remove. The native APIs of the macOS 27 SDK (headers of Xcode
27.1; all times are CFTimeInterval seconds of host time, the
`mach_absolute_time` / `CACurrentMediaTime` domain):

| Pacer input | Metal / CoreAnimation API | Availability |
|---|---|---|
| Refresh period, VRR range | `NSScreen` `minimumRefreshInterval` / `maximumRefreshInterval` / `displayUpdateGranularity` (0 = variable) / `lastDisplayUpdateTimestamp`; `CADisplayLink.duration` from `-[NSView displayLinkWithTarget:selector:]` (follows the window across screens) | macOS 12 / 14 |
| Target present time (FR3) | `MTLCommandBuffer presentDrawable:atTime:`, `MTLDrawable presentAtTime:`; `presentAfterMinimumDuration:` (relative to the previous frame's actual present) | macOS 10.11 / 10.15.4 |
| Presentation feedback | `MTLDrawable addPresentedHandler:` with `presentedTime` (0 = not presented) and `drawableID` | macOS 10.15.4 |
| Present wait (FR5) | none; emulated by a condition variable per frame id signalled from the presented handler | - |
| GPU <-> CPU clock | `MTLCommandBuffer GPUStartTime` / `GPUEndTime` are already host time; `MTLDevice sampleTimestamps:gpuTimestamp:` for counter timestamps | macOS 10.15 |
| OS-driven cadence | `CAMetalDisplayLink` (delegate receives the drawable, `targetTimestamp`, `targetPresentationTimestamp`; `preferredFrameLatency`, `preferredFrameRateRange`) | macOS 14 |

`MTL4CommandQueue waitForDrawable:` / `signalDrawable:` (macOS 26) are GPU
ordering, not timing. metal-cpp (`metal-cpp_27`) wraps everything above
except `CAMetalDisplayLink` and the AppKit display links, which need
Objective-C++ (`metal_surface.mm` is).

Tier M is tier W's algorithm on these inputs, keeping erhe's own loop in
control: `presentDrawable:atTime:` for targets, `wait_for_displayed_frame`
on the presented handler, `GPUStartTime` / `GPUEndTime` for the frame
bracket. It resolves for `auto` without a startup probe: the measurement
below showed targets honored, and the presented handler logs a warning when
more than a tenth of the first 240 targeted frames display a refresh before
their target. The C15 holdback is not used. Open: a `CADisplayLink` /
`NSScreen` resolver source. Driving frames from
a `CAMetalDisplayLink` delegate inverts erhe's M1 -> M2 -> R control flow and
bypasses the pacer; it is worth a measurement-only mode as the latency and
judder reference the pacer should match. `preferredFrameRateRange` is the
only way to request a cadence on a ProMotion / VRR panel (the VRR-aware tier
of `doc/plans/frame_pacing.md`).

To measure before building:

1. `std::chrono::steady_clock` (`Frame_time_recorder::now`) against
   `CACurrentMediaTime`: same clock, no offset?
2. Is `presentAtTime` honored windowed and fullscreen, at a T/2 target, and
   with `displaySyncEnabled` NO?
3. `presentedTime` windowed: display or compositor latch time; how often 0
   under load?
4. Presented-handler latency and thread.
5. Does a presenting command buffer complete at GPU end or when the
   compositor releases the drawable (the presented-frame budget assumes the
   latter)?
6. ProMotion: does the cadence follow `preferredFrameRateRange`; what does
   `CADisplayLink.duration` report under VRR; which screen's link applies to
   a window spanning two?

Answers, measured 2026-10-20 with a temporary probe in
`submit_command_buffers` (Debug windowed editor, default scene, M4 Pro, the
external 120 Hz display, about 550 frames per configuration):

1. Same clock: `Frame_time_recorder::now()` and `CACurrentMediaTime()`
   sampled back-to-back differ by 0.04 us (the bracket of the two `now()`
   calls is 0.08 us). Metal host times go into the frame records unconverted.
2. Targets are honored windowed, with `displaySyncEnabled` YES and NO. A
   target stepped by 2T from the previous one gives an achieved cadence of
   16.685 ms (p5 to p95 within 1 us). A 1.5T step alternates 8.34 / 16.69 ms:
   the drawable shows at the first refresh at or after the target, so a
   target between refreshes rounds up. The achieved time lies 0.3 ms before
   to 7.5 ms after the target. Fullscreen was not measured: the editor treats
   unfocused fullscreen as hidden and does not render, and an
   agent-launched editor has no focus (a user-run item, next to question 6).
3. `presentedTime` is on the display's refresh grid: consecutive deltas are
   8.344 ms with p5 to p95 within 1 us, while the window display mode
   reports 120.00 Hz (8.333 ms; the pacer's grid period tracker absorbs the
   0.13 % difference). One frame of 541 (the first) reported 0.
   `presentedTime - commit` is 22.3 ms (about 2.7 refresh periods of queue)
   under tier S.
4. The presented handler runs on a Metal-internal thread (not the main
   thread) 0.48 ms after `presentedTime` (p95 0.84 ms).
5. A presenting command buffer completes at GPU end: its completion handler
   runs 0.06 ms after `GPUEndTime` and 16.4 ms before `presentedTime`. The
   presented-frame throttle (`wait_for_presented_frame_slot`) therefore
   counts command buffers on the GPU, not drawables the compositor holds;
   `nextDrawable` is the backpressure.

### State (item 5 picked by the user, 2026-10-20)

Built: tier M (`doc/erhe/metal_backend.md` "Frame pacing") - presented
handler feedback and present wait, `presentDrawable:atTime:` targets,
`GPUStartTime` / `GPUEndTime` frame bracket, `cpu_slot_end` in `end_frame`
(Metal never stamped it, so the pacer had no CPU service times), and the
pacer's deadline offset estimator with the deadline-aware FR5 clamp
(`doc/frame_pacing/algorithm.md` section 3.3, claim C22 in both the C++
suite and `scripts/frame_pacing_sim.py`), enabled through
`Device::is_present_target_time_honored`.

Measured live (Debug windowed, default scene, 3 x 120 frames after 15 s):
before the estimator tier M ran N=1 with achieved == predicted to 1 us on
on-time frames, but 40 % of the frames displayed two refreshes (timing-only
misses: the compositor deadline) and release-to-display was 41.8 ms median
against tier S's 28.9 ms. With the estimator: 1 two-refresh frame of 348,
release-to-display 19.8 ms against tier S's 29.6 ms, learned offset 9.19 ms
(table in `doc/erhe/metal_backend.md` "Frame pacing") - when the pacer stays
at N=1. Runs that downshifted to N=2 at startup stayed there; the work of
[frame_pacing_metal.md](frame_pacing_metal.md) removed that lock, and
`auto` is tier M (OFF headless or under `force_disable_vsync`), gated in
Debug and Release, in every threading mode, fullscreen with Adaptive-Sync
([frame_pacing_adaptive_sync.md](frame_pacing_adaptive_sync.md)) and on
the ProMotion panel. An occluded window gets `presentedTime` 0 and no feedback
(logged "drawables were not displayed"); tier M then runs without feedback
(N=2 observed), which nobody sees.

Open (the remaining gates are in [frame_pacing_metal.md](frame_pacing_metal.md)):
a presentation-derived refresh source for `Display_refresh_resolver`
(`presentedTime` deltas or `CADisplayLink`): the window display mode says
8.333 ms, the display runs 8.344 ms; the pacer's grid period tracker
absorbs the 0.13 % today.

## 6. GPU-read draw count

Multi-draw indirect issues one indirect `drawIndexedPrimitives` per command
over a CPU-side draw count (`doc/erhe/metal_backend.md` "Multi-Draw
Indirect"); records are named by `first_instance`. What is missing is a
draw count the GPU writes (`Device_info::use_draw_indirect_count`, which
the GPU culling of `doc/erhe/draw_list_renderer.md` needs).

Metal 4 (macOS 26) adds no count draw: `MTL4RenderCommandEncoder` has a
single indirect `drawIndexedPrimitives` (GPU addresses) and no multi or
count variant, its compute encoder is still separate from render, and its
intra-pass barriers order work without allowing a dispatch inside a render
pass (`MTL4RenderEncoderOptionSuspending` forbids compute between the
parts). The usable primitive predates it:
`MTLRenderCommandEncoder executeCommandsInBuffer:indirectBuffer:indirectBufferOffset:`
(macOS 10.14) reads the executed range `{uint32 location, uint32 length}`
(`MTLIndirectCommandBufferExecutionRange`) from a GPU buffer. The indirect
command buffer (ICB) must hold concrete draws - an ICB command cannot be an
indirect draw - so a kernel writes them on the GPU with MSL
`render_command::draw_indexed_primitives` (index count, typed index pointer,
instance count, base vertex, base instance). SPIR-V and SPIRV-Cross cannot
express `command_buffer`, so this kernel is MSL source compiled at runtime
through the existing `newLibrary` path. macOS 26 adds ICB inheritance of
depth-stencil, depth bias, clip mode, cull mode, winding and fill mode
(`inheritDepthStencilState` ...), on top of `inheritPipelineState` /
`inheritBuffers`. Pipelines an ICB executes need
`supportIndirectCommandBuffers`; index buffers the commands reference need
`useResource:` (a residency set under MTL4).

Decision (user, 2026-10-23): option A. It is the first item of the work
queue, before raster occlusion culling
([occlusion_culling.md](occlusion_culling.md)), together with section 7
(Metal 4); which of the two is built first, or both together, is the
planner's choice (section 7 "Order").

Options considered:

- A (chosen): a preparation hook, now simpler. A Metal-only MSL kernel
  runs right after `cull_compact.comp` in the same compute pass, one thread
  per command slot: it encodes the job's surviving commands into an ICB and
  writes `{command_offset, count}` per job into a range buffer. The in-pass
  `multi_draw_indexed_primitives_indirect_count` becomes `useResource(index
  buffer)` + `executeCommandsInBuffer(icb, ranges, job * 8)`. Needs an
  `erhe::graphics` API to declare the ICB / encode step (a no-op on the
  other backends) and one kernel variant per index type.
- B: A without the range buffer: the kernel encodes all `max_draw_count`
  slots and `reset()`s those past the count; the pass executes the CPU
  maximum. Simpler, the GPU walks empty commands.
- End and reopen the render encoder around an in-pass compute encode, or
  encode `max_draw_count` draws on the CPU: unchanged, rejected (tile
  store/reload per call; CPU cost proportional to the maximum).
- A port to MTL4 does not by itself solve the count; it is a separate
  decision (explicit barriers everywhere, `MTL4ArgumentTable`,
  `MTL4CommandAllocator`). `MTLResidencySet` (macOS 15) could replace the
  texture heap's per-encode `useResource` loops without it, and
  `MTL4Compiler` / `MTL4Archive` relate to the open `MTLBinaryArchive` item.

Status (2026-10-23): option A is built. `erhe::graphics` has
`Indirect_count_draws` (doc/erhe/graphics.md "Indirect count draws"), Metal
reports `use_draw_indirect_count`, and the GPU culling's shadow passes draw
their generated commands through it on every backend
(doc/erhe/draw_list_renderer.md "GPU command generation"). Findings (details
in `doc/erhe/metal_backend.md` "Multi-Draw Indirect"): an ICB command costs
about 692 bytes; creation succeeds up to 4194304 commands and a
1048576-command range executes. `setVertexBytes` bindings are inherited into
ICB draws (one value for all commands), so `ERHE_DRAW_ID` is not per draw and
records stay named by `first_instance`. On macOS 26+ the new `inherit*`
states default to true. `supportIndirectCommandBuffers` cannot be set on
every pipeline (Metal refuses it for fragment shaders with directly bound
textures or samplers), so it is opt-in per `Render_pipeline`
(`Base_render_pipeline_create_info::indirect_count_draws`; the shadow
pipelines set it).

Verification (2026-10-23, M4 Pro): `Draw_indirect_test` count tests (one
job, count 0, two jobs with a 16-bit permuted second index buffer) pass on
Metal under Metal API validation and on KosmicKrisp (MoltenVK has no count
draw and skips them). The culling gates with `culling_classification` `gpu`
and the shadow map cache off: `get_cull_classification_check` 0 mask and 0
command mismatches on the default scene (4 jobs), the heavy bench scene (18
jobs, 6638 commands) and Bistro (784 jobs); `render_scene_image` md5
identical between `cpu` and `gpu` in one session on all three (Metal) and on
default and heavy (KosmicKrisp); `get_scene_lights` shadow entry and culled
counts equal between the modes.

Footprint (user decision 2026-10-23): each job reserves its whole chunk
entry count (rounded to 32) in the ICB, and the drawn counts are not known
before the GPU writes them, so one set per frame needed 131072 commands
(90.7 MB) per frame in flight on Bistro, 66560 (46 MB) with the ICB growing
in steps of 1024 commands. The GPU culling therefore prepares one set per
shadow pass (`Indirect_count_draws` takes any number of prepares per frame;
a set is drawn before the next prepare): the ICB is sized to the largest
single pass. High-water per frame in flight, re-measured with the same
gates (0 mismatches, `cpu` and `gpu` images identical): 1024 commands (0.7
MB) on the default scene, 2048 (1.4 MB) on heavy, 5120 (3.6 MB) on Bistro.

## 7. Metal 4

Decision (user, 2026-10-23): the backend supports Metal 4 (the `MTL4*`
API, macOS 26). Raster occlusion culling starts only once Metal has both
the GPU-read draw count (section 6) and Metal 4.

What changes (the MTL4 model; agfx's Metal 4 backend is a reference,
`doc/reference/agfx_comparison.md`):

- Command submission: `MTL4CommandQueue`, `MTL4CommandBuffer` recorded into
  an `MTL4CommandAllocator` (one per frame slot and recording thread, reset
  when the slot's frame has completed), commits with `MTL4CommitFeedback`
  in place of completed handlers (frame retirement, deferred destruction,
  the GPU timer resolve, the frame pacing GPU times).
- Binding: `MTL4ArgumentTable` (GPU addresses and resource IDs) replaces
  per-encoder `setBuffer` / `setVertexBytes`; push constants (index 15,
  `ERHE_DRAW_ID`) need a new home.
- Residency: `MTLResidencySet` replaces `useResource` (the texture heap's
  per-encode loops, the ICB's index buffers).
- Hazards: no automatic hazard tracking; explicit stage-to-stage barriers
  (`barrierAfterQueueStages` / encoder barriers) where the backend relies on
  Metal's tracking today, and the inter-encoder fence.
- Drawables: the queue waits on and signals the drawable
  (`waitForDrawable` / `signalDrawable`); the present path, presented
  handlers and tier M frame pacing (`doc/erhe/metal_backend.md` "Frame
  pacing") move with it.
- Timestamps: `MTL4CounterHeap` replaces the counter sample buffers of the
  GPU timers and the Tracy GPU zones (`doc/erhe/metal_backend.md` "GPU
  timers").
- Shaders and pipelines: `MTL4Compiler` / `MTL4Archive` (relates to the open
  `MTLBinaryArchive` item); `MTL4RenderPipelineDescriptor`.
- The GPU-read draw count carries over: `MTL4RenderCommandEncoder` has
  `executeCommandsInBuffer(icb, indirectRangeBuffer)` with the range buffer
  as a GPU address (pinned metal-cpp `metal-cpp_27` has the `MTL4*`
  headers).

Decisions (user, 2026-10-23):

- Order: section 6 first on the current API, verified by the existing
  tests; then Metal 4, which ports the count path with the rest
  (`executeCommandsInBuffer` exists on `MTL4RenderCommandEncoder`).
- Metal 4 is a separate backend beside Metal 3: its own
  `ERHE_GRAPHICS_API` value and source directory, gated by the same
  Metal ctest suite, image, race and pacing gates as it grows, with the
  Metal 3 tree staying green throughout. Whether Metal 3 is then removed is
  decided once Metal 4 passes those gates.
- Granularity: subsystem by subsystem in the new backend, one ctest-gated
  commit each. `MTL4::CommandQueue::commit` takes only `MTL4::CommandBuffer`s,
  which yield only `MTL4` encoders, which bind only through argument tables
  (no `setBytes`), so the first commit is the whole submission core at once.
  The order: (1) submission core - queue, command buffers and allocators,
  commit feedback, residency set, argument tables, encoders and barriers,
  drawables and frame pacing (landed, `doc/erhe/metal4_backend.md`); (2)
  counter heaps for the GPU timers and Tracy GPU zones (the timer pool moved
  to `MTL4CounterHeap` with (1), because MTL4 pass descriptors have no sample
  buffer attachments and the renderer tests expect nonzero timer results;
  what remains is the Tracy GPU zone check in a Tracy build); (3)
  `MTL4Compiler` / `MTL4Archive` for shaders and pipelines; (4) the
  carry-over check of the GPU-read draw count (section 6), which (1) already
  ported to the MTL4 encoders and residency. (Mixing Metal 3 encoders into an
  `MTL4CommandBuffer` is not possible; two queues synchronised by
  `MTLSharedEvent` would be, and is not taken.)

Verification: the Metal ctest suite, the Metal image and race gates
(`doc/editor/render_thread.md` "Metal"), the frame pacing live gates
(`doc/plans/frame_pacing_metal.md`), GPU timers and Tracy GPU zones, then
section 6's verification on MTL4.

State: the backend exists as `ERHE_GRAPHICS_API=metal4` (build tree
`build_xcode_metal4`, sources `src/erhe/graphics/erhe_graphics/metal4/`).
Step (1) landed: MTL4 submission, residency, argument tables, encoders with
conservative queue barriers, drawables through the queue, GPU timers on
counter heaps; shaders and pipelines are still the Metal 3 API. Its
document, with the per-subsystem migration status, is
`doc/erhe/metal4_backend.md`. Next: (2) the Tracy GPU zone check, then (3)
`MTL4Compiler`; the image, race and pacing gates as each step lands, and
narrowing the barriers.
