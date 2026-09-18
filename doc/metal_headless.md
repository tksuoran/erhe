# Metal headless configuration

The Metal backend runs without a window (`ERHE_WINDOW_LIBRARY=none`) the
same way the Vulkan backend does: an emulated swapchain of offscreen
textures stands in for the `CAMetalLayer` drawables, the full render
pipeline and the in-editor MCP server run, and `capture_screenshot` reads
the last composited frame back synchronously. The Vulkan sibling is
`src/erhe/graphics/erhe_graphics/vulkan/vulkan_emulated_swapchain.{hpp,cpp}`;
this document states what the Metal side does.

## Requirements

- R1. `scripts/configure_xcode_metal_headless.sh` configures
  `build_xcode_metal_headless/` with `ERHE_GRAPHICS_API=metal` and
  `ERHE_WINDOW_LIBRARY=none`, otherwise identical to
  `scripts/configure_xcode_metal.sh`. The `editor` target builds from it
  with `cmake --build build_xcode_metal_headless --target editor --config Debug`.
- R2. The editor launched from that build (working directory = repo root,
  `ERHE_AI_DRIVER=1`) reaches `Main loop: completed frame 12` in
  `logs/log.txt`, renders every frame through the swapchain render pass,
  and answers on the MCP port.
- R3. `Device::capture_last_frame` in the headless Metal build returns the
  most recently composited frame synchronously (no arm step), as tightly
  packed opaque RGBA8 with `format_8_vec4_srgb`, so the MCP
  `capture_screenshot` tool produces a PNG with the scene in it on the
  first call. `Device::request_frame_capture` returns true and is a no-op
  there, matching the contract in `device.hpp`.
- R4. The offscreen backbuffer takes its size from the null window
  (`Context_window::get_width/get_height`), and its color format is the
  one the windowed layer uses (`BGRA8Unorm_sRGB`), so pipeline format
  derivation, which on Metal reads `Swapchain::get_color_format`, is
  unchanged.
- R5. The windowed Metal build (`build_xcode_metal`) builds and behaves as
  before: real drawables, `presentDrawable`, armed capture.
- R6. `metal_surface.mm` compiles in both window configurations: the SDL
  view and layer creation exist only under `ERHE_WINDOW_LIBRARY_SDL`; with
  `ERHE_WINDOW_LIBRARY_NONE` no SDL header is included.

## Design

- D1. The headless decision is made where the surface is created:
  `Surface_impl` creates a `CAMetalLayer` only under
  `ERHE_WINDOW_LIBRARY_SDL`, from the context window's SDL window, so with
  `ERHE_WINDOW_LIBRARY_NONE` the surface has no layer.
  `Surface_impl::is_headless()` is exactly "there is no layer"; it also
  exposes the backbuffer size (`get_backbuffer_width/height`) from the
  context window. The Vulkan backend keys the same decision on
  `Context_window::has_vulkan_surface()`.
- D2. The emulated backbuffer is a ring of three `MTL::Texture` color
  targets (`BGRA8Unorm_sRGB`, usage render target + shader read, private
  storage) created once from the null window size. It lives inside
  `Swapchain_impl` behind `is_headless()`, so there is one Metal
  `Swapchain` in both configurations and the public swapchain interface
  (formats, `wait_frame`, `begin_frame`, `end_frame`) is the same one.
  `Render_pass_impl` attaches `Swapchain_impl::get_current_color_texture()`
  as color attachment 0 - the drawable's texture when windowed, the current
  ring texture when headless - and its existing "no backbuffer, bail out"
  branch tests that texture instead of the drawable. Depth stays
  app-managed as in the windowed path.
- D3. `begin_frame` advances the ring index and returns true; no drawable
  is acquired, so `submit_command_buffers` records no `presentDrawable`
  and `wait_for_displayed_frame` keeps returning `unsupported`. Frame
  pacing is the device's own completion tracking, as in the Vulkan
  emulated swapchain. Ring reuse is safe because the device submits on one
  queue in order and the swapchain render pass clears (discards) its color
  attachment.
- D4. `Render_pass_impl::end_render_pass` calls
  `Swapchain_impl::mark_render_pass_recorded()`, which records the current
  ring index as the composited frame (a no-op when windowed).
  `capture_last_frame` in headless mode blits that texture into the
  persistent shared capture buffer on a fresh command buffer, waits for it,
  and converts to RGBA8 with the same pixel-format handling the windowed
  path uses. It returns false until one frame has been composited. All ring
  textures are created readable, so no `framebufferOnly` question arises,
  and `request_frame_capture` has nothing to arm: it reports success and
  does nothing.
- D5. No per-frame heap allocation: the ring is created at construction and
  the capture buffer on first capture, and both are reused.

## Verification

1. `scripts/configure_xcode_metal_headless.sh`, then build `editor`.
2. Launch from the repo root with `ERHE_AI_DRIVER=1`; grep `logs/log.txt`
   for `Main loop: completed frame 12` and `MCP server: listening on`.
3. `py -3 scripts/mcp_call.py capture_screenshot` (or `python3` on macOS)
   succeeds on the first call; the PNG at `logs/mcp_screenshot.png` is
   not a uniform color (a scene is visible).
4. `logs/log.txt` holds no `error` lines from `erhe.graphics` and no
   Metal validation message.
5. `cmake --build build_xcode_metal --target editor --config Debug` still
   builds; a windowed launch still presents and `capture_screenshot`
   still works there (needs a live display).
6. `scripts/run_graphics_tests_metal.sh` passes as before.
