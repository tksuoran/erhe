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

## 6. GPU-read draw count

Multi-draw indirect issues one indirect `drawIndexedPrimitives` per command
over a CPU-side draw count (`doc/erhe/metal_backend.md` "Multi-Draw
Indirect"); records are named by `first_instance`. What is missing is a
draw count the GPU writes (`Device_info::use_draw_indirect_count`, which
the GPU culling of `doc/erhe/draw_list_renderer.md` needs): encode the
commands into an `MTL::IndirectCommandBuffer` from a compute kernel that
reads the count (a hand-written MSL kernel; the GLSL path cannot express an
indirect command buffer) and execute it with
`executeCommandsInBuffer` over an indirect range, then enable
`Draw_indirect_test.draw_indexed_indirect_count` and `_count_zero`.
