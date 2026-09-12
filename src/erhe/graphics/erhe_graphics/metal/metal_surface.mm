#include "erhe_graphics/metal/metal_surface.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_swapchain.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_window/window.hpp"

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#import <QuartzCore/CAMetalLayer.h>

#if defined(ERHE_WINDOW_LIBRARY_SDL)
#   include <SDL3/SDL.h>
#   include <SDL3/SDL_metal.h>
#endif

namespace erhe::graphics {

Surface_impl::Surface_impl(Device_impl& device_impl, const Surface_create_info& create_info)
    : m_device_impl        {device_impl}
    , m_surface_create_info{create_info}
{
#if defined(ERHE_WINDOW_LIBRARY_SDL)
    if (create_info.context_window != nullptr) {
        SDL_Window* sdl_window = static_cast<SDL_Window*>(create_info.context_window->get_sdl_window());
        if (sdl_window != nullptr) {
            m_sdl_metal_view = SDL_Metal_CreateView(sdl_window);
            if (m_sdl_metal_view != nullptr) {
                m_metal_layer = static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(m_sdl_metal_view));
                if (m_metal_layer != nullptr) {
                    m_metal_layer->setDevice(device_impl.get_mtl_device());
                    m_metal_layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
                    // Readable drawables, so the swapchain can blit a
                    // frame into a buffer for Device::capture_last_frame
                    // (Swapchain_impl::record_capture). Framebuffer-only
                    // drawables reject every read.
                    m_metal_layer->setFramebufferOnly(false);
                    if (device_impl.get_graphics_config().force_disable_vsync) {
                        ((CAMetalLayer*)m_metal_layer).displaySyncEnabled = NO;
                        log_startup->info("Disabled vsync (force_disable_vsync)");
                    }
                }
            }
        }
    }
#endif

    if (m_metal_layer == nullptr) {
        log_startup->info(
            "Metal surface is headless: rendering into an emulated swapchain of offscreen textures"
        );
    }

    m_swapchain = std::make_unique<Swapchain>(
        std::make_unique<Swapchain_impl>(
            m_device_impl,
            *this
        )
    );
}

Surface_impl::~Surface_impl() noexcept
{
    m_swapchain.reset();
#if defined(ERHE_WINDOW_LIBRARY_SDL)
    if (m_sdl_metal_view != nullptr) {
        SDL_Metal_DestroyView(m_sdl_metal_view);
        m_sdl_metal_view = nullptr;
    }
#endif
}

auto Surface_impl::get_swapchain() -> Swapchain*
{
    return m_swapchain.get();
}

auto Surface_impl::get_metal_layer() const -> CA::MetalLayer*
{
    return m_metal_layer;
}

auto Surface_impl::is_headless() const -> bool
{
    return m_metal_layer == nullptr;
}

auto Surface_impl::get_backbuffer_width() const -> int
{
    return (m_surface_create_info.context_window != nullptr)
        ? m_surface_create_info.context_window->get_width()
        : 0;
}

auto Surface_impl::get_backbuffer_height() const -> int
{
    return (m_surface_create_info.context_window != nullptr)
        ? m_surface_create_info.context_window->get_height()
        : 0;
}

} // namespace erhe::graphics
