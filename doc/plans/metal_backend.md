# Metal backend: parity work

Status: proposed

Extends `doc/erhe/metal_backend.md`. The items below are where the Metal
backend still differs from Vulkan; each is verified on macOS with the
Xcode build (`doc/agents/macos.md`) and, where a GPU test covers it, with
`erhe_graphics_gpu_tests` on Metal.

## 3. `blit_framebuffer`

`Blit_command_encoder_impl::blit_framebuffer` (`metal_blit_command_encoder.cpp`)
is `ERHE_FATAL("not implemented")`. Its only editor caller is the OpenXR
desktop mirror, which Metal does not run, so nothing reaches it today.
Implement it together with the Vulkan one (`doc/plans/xr.md` "Desktop
mirror"): a copy on the blit encoder for the unscaled case, a full-screen
triangle render pass sampling the source for the scaled one, and a GPU test
blitting between two render passes of different sizes.

## 4. Swapchain resize

Windowed resize relies on SDL's Metal view keeping the layer's
`drawableSize` current (`doc/erhe/metal_backend.md` "Swapchain"); it has
not been exercised with a live resize. Interactive check (an agent cannot
resize the window: there is no MCP tool for it, and scripting System Events
needs the Accessibility permission): with the windowed Metal editor,
drag-resize the window larger and smaller, also across displays of
different scale, and confirm the viewports and ImGui fill the window
without stretching or clipping and that `capture_screenshot` returns the
new size.

## 5. Present wait

`Device_impl::wait_for_displayed_frame` returns
`Present_wait_result::unsupported` and `get_frame_pacing_tier` returns
`off`. Present wait alone would have no caller: it is the FR5 clamp of
tier W (`doc/frame_pacing/capability_tiers.md`), which also needs a target
present time, presentation feedback and a GPU to reference clock
calibration. The Metal mapping, to decide which tier to build:

- Tier S needs only FIFO backpressure: `CAMetalLayer.displaySyncEnabled`
  on and a small `maximumDrawableCount`; the blocking it senses is the
  `nextDrawable` / presented-frame wait in `begin_swapchain`. Smallest step.
- Tier W: present wait from `MTL::Drawable::addPresentedHandler` (presented
  time per frame id, condition variable per id), target present time from
  `presentDrawable:atTime:` (CoreVideo host time), feedback from
  `MTLDrawable.presentedTime`, and calibration between the `CACurrentMediaTime`
  host clock and the `Frame_time_recorder` reference clock. The display
  refresh comes from `CAMetalDisplayLink` / `NSScreen`.

Depth-stencil format sorting is done (`sort_depth_stencil_formats`).

## 6. GPU-read draw count

Multi-draw indirect issues one indirect `drawIndexedPrimitives` per command
over a CPU-side draw count (`doc/erhe/metal_backend.md` "Multi-Draw
Indirect"); records are named by `first_instance`. What is missing is a
draw count the GPU writes (`Device_info::use_draw_indirect_count`, which
the GPU culling of `doc/erhe/draw_list_renderer.md` needs).

Metal has no indirect-count draw. The GPU-side form is an
`MTL::IndirectCommandBuffer` (inherited pipeline state and buffers) whose
draws a compute kernel encodes from the commands and the count (a
hand-written MSL kernel: the GLSL path cannot express an indirect command
buffer), executed with `executeCommandsInBuffer` over the encoded range.
The kernel must run in a compute encoder before the render encoder that
draws, but `Render_command_encoder::multi_draw_indexed_primitives_indirect_count`
is called inside the render pass. The options, to decide before building:

- A preparation hook: the count draws of a pass are declared before the pass
  starts (command buffer, offset, count buffer, maximum), the backend encodes
  their indirect command buffers in one compute encoder ahead of the pass,
  and the in-pass call executes the prepared one. Fits the GPU culling,
  whose commands are produced by `cull_compact.comp` before the shadow
  passes; needs an `erhe::graphics` API addition the other backends treat as
  a no-op.
- End and reopen the render encoder around an in-pass compute encode: no
  API change, but every call stores and reloads the attachments (a tile
  flush on Apple GPUs).
- Encode `max_draw_count` indirect draws on the CPU after a compute pass
  zeroes the commands past the count: the same ordering problem, plus CPU
  encode cost proportional to the maximum.

Verification: `Draw_indirect_test.draw_indexed_indirect_count` and
`_count_zero`, then the GPU culling gates of `doc/plans/culling.md` with
`culling_classification` `gpu` on Metal.
