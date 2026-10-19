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

`Device_impl::resize_swapchain_to_window()` (`metal_device.cpp`) is empty.
Windowed: set the `CAMetalLayer` `drawableSize` from the window's pixel size
and recreate the app-managed depth-stencil texture at that size. Headless:
recreate the emulated backbuffer ring at the new
`Context_window::get_width/get_height`. Verify by resizing the window (and,
headless, by changing the window size through the MCP server) and reading
back a frame at the new size.

## 5. Present wait and format sorting

- `Device_impl::wait_for_displayed_frame` returns
  `Present_wait_result::unsupported` and `get_frame_pacing_tier` returns
  `off`. Implement present wait with `MTL::Drawable::addPresentedHandler`
  (presented time per frame id), which lets the frame pacing tiers of
  `doc/frame_pacing/` run on Metal.
- `Device_impl::sort_depth_stencil_formats` (`metal_device.cpp`) leaves the
  list unsorted. Apply the Vulkan backend's ordering (depth+stencil before
  depth-only, then higher precision; `vulkan_device.cpp`), filtered by the
  capabilities in `metal_pixel_format_table.cpp`.

## 6. Multi-draw indirect on the GPU

Multi-draw indirect loops on the CPU over `drawIndexedPrimitives`, reading
the draw commands back from the indirect buffer (`doc/erhe/metal_backend.md`
"Multi-Draw Indirect"). Replace the loop with an indirect command buffer
(`MTL::IndirectCommandBuffer`) encoded by a compute pass from the same draw
commands, so the draw count and the draw parameters never round-trip
through the CPU.

## 7. Headless frame pacing

`Device_impl::acquire_frame_slot` does not wait for frame `frame_id - N` to
complete on the GPU (`doc/erhe/metal_backend.md` "Thread safety"). Windowed,
`begin_swapchain` bounds the presented frames in flight; headless, nothing
bounds how far the CPU runs ahead of a GPU-bound frame, and the ring buffer
pool spills instead. Wait in `acquire_frame_slot` until every command buffer
committed for frame `frame_id - N` has completed (the per-frame counts the
completion handlers already maintain, with a condition variable), and
verify with a GPU-bound headless scene that the frame count in flight stays
at N and the pool stops growing.
