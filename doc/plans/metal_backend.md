# Metal backend: parity work

Status: proposed

Extends `doc/erhe/metal_backend.md`. The items below are where the Metal
backend still differs from Vulkan; each is verified on macOS with the
Xcode build (`doc/agents/macos.md`) and, where a GPU test covers it, with
`erhe_graphics_gpu_tests` on Metal.

## 1. Compile and run the backend-shared changes

`Ring_buffer_pool` (`doc/erhe/ring_buffer_memory.md`) replaced the Metal
backend's own ring-buffer allocator, and `Scoped_debug_group` takes its label
as a `std::string_view`. Both Metal edits were written on a machine without
the Metal toolchain: build the editor and `erhe_graphics_gpu_tests` on
macOS, start the editor, and confirm that the pool spills and reclaims
(`doc/erhe/ring_buffer_memory.md` describes the log lines and the memory
bound).

## 2. GPU timers

`Gpu_timer_impl` (`metal_gpu_timer.cpp`) records nothing and reads 0, so the
DDGI / radiance cascades timings and every other `Gpu_timer` consumer show 0
on Metal. Implement it with `MTL::CounterSampleBuffer` timestamp sampling at
the stage boundaries the device supports
(`MTL::Device::supportsCounterSampling`), resolved after the command buffer
completes, and convert GPU ticks to nanoseconds with
`MTL::Device::sampleTimestamps` calibration.

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

## 7. GPU address fault in the viewport pass

A freshly started windowed editor driven by a short MCP test sequence
(`ctest -R "key_press_ctrl_a_selects_the_focused_tree|imgui_click_double|capture_screenshot_annotates_imgui_items"`
from `build_xcode_metal`) faults on the GPU in about two runs of three:
the command buffer completes with `kIOGPUCommandBufferCallbackErrorPageFault`
(logged as "Metal command buffer '...' of frame N failed"), and after a
second fault Metal ignores every later submission of the process
(`kIOGPUCommandBufferCallbackErrorSubmissionsIgnored`), so the editor stops
rendering - screenshots and `render_scene_image` then time out or come
back empty. Creating the command buffer with
`MTL::CommandBufferErrorOptionEncoderExecutionStatus` names the faulting
encoder: "Viewport window renderpass", in the first test's frames (around
frame 140). The fault does not occur under `MTL_DEBUG_LAYER=1
MTL_SHADER_VALIDATION=1`, and a full editor-suite run usually avoids it,
which points at a GPU resource released while a later frame still binds it
rather than an out-of-range index in a shader. Find the stale binding with
a Metal GPU capture of a faulting frame.
