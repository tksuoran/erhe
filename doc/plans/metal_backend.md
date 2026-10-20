# Metal backend: parity work

Status: in progress

Extends `doc/erhe/metal_backend.md`. The items below are where the Metal
backend still differs from Vulkan; each is verified on macOS with the
Xcode build (`doc/agents/macos.md`) and, where a GPU test covers it, with
`erhe_graphics_gpu_tests` on Metal.

## 5. Display-time pacing tier

Tier S is built (`doc/erhe/metal_backend.md` "Frame pacing"). With three
drawables it keeps the 120 Hz cadence but runs its sleep at the clamp with
about 5.6 ms of `nextDrawable` wait left: the queue depth needs display-time
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

The proposed tier ("M"): tier W's algorithm on these inputs, keeping erhe's
own loop in control - `CADisplayLink` / `NSScreen` as a resolver source,
`presentDrawable:atTime:` for targets, `wait_for_displayed_frame` on the
presented handler, `GPUStartTime` / `GPUEndTime` for the frame bracket, the
C15 holdback as the fallback, auto-selected only after a startup probe shows
targets are honored (as the present-timing probe does). Driving frames from
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

Options, to decide before building:

- A (recommended): a preparation hook, now simpler. A Metal-only MSL kernel
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

Open: the ICB `maxCommandCount` limit on Apple silicon; whether
`setVertexBytes` push constants are inherited into ICB draws (with
`first_instance` record indexing they may not be needed); whether an
inherited pipeline requires every inheritable state inherited before macOS
26; the cost of `supportIndirectCommandBuffers` on all pipelines versus the
culled ones only. An offline MSL compile check needs
`xcodebuild -downloadComponent MetalToolchain`.

Verification: `Draw_indirect_test.draw_indexed_indirect_count` and
`_count_zero`, then the GPU culling gates of `doc/plans/culling.md` with
`culling_classification` `gpu` on Metal.
