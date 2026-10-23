#pragma once

// macOS + Vulkan: small Objective-C++ bridge that reaches through an SDL
// window into the underlying CAMetalLayer so we can pin drawableSize from
// C++ without going through MoltenVK. The implementation lives in
// vulkan_metal_layer_mac.mm and is compiled only when ERHE_OS_MACOS +
// ERHE_GRAPHICS_API_VULKAN are both defined.

#if defined(ERHE_OS_MACOS) && defined(ERHE_GRAPHICS_API_VULKAN)

namespace erhe::graphics {

// Explicitly write CAMetalLayer.drawableSize. Needed on the Vulkan/MoltenVK
// path because AppKit's live-resize layout callbacks can reset the layer's
// drawableSize to (bounds x contentsScale) which is transiently 0x0 (clamped
// to 1x1) during fast drag operations. That 1x1 drawable is then handed to
// MoltenVK's nextDrawable and Metal rejects the render pass. Pinning the
// layer's drawableSize to the current VkSwapchainKHR imageExtent right before
// every vkAcquireNextImageKHR closes the race. Returns true on success.
bool set_sdl_window_drawable_size(void* sdl_window, int width, int height);

// Runs the main run loop once without blocking (no-op off the main thread):
// a window ordered on screen during device init is composited only once a
// Core Animation commit runs, which the present calibration's presents need
// (vulkan_device_present_calibration.cpp).
void service_main_run_loop();

} // namespace erhe::graphics

#endif
