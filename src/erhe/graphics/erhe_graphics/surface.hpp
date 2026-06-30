#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/enums.hpp"

#include <memory>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Surface_create_info
{
public:
    erhe::window::Context_window* context_window           {nullptr};
    bool                          prefer_low_bandwidth     {false};
    bool                          prefer_high_dynamic_range{false};
    bool                          allow_tearing            {false};
    bool                          allow_dropping_frames    {false};
};

class Surface_impl;
class Swapchain;

class Surface final
{
public:
    Surface(std::unique_ptr<Surface_impl>&& surface_impl);
    ~Surface() noexcept;

    [[nodiscard]] auto get_swapchain() -> Swapchain*;

    // Rotation the presentation engine expects the application to pre-apply to
    // its rendered content (Android pre-rotation). identity on non-Vulkan
    // backends and whenever no rotation is in effect.
    [[nodiscard]] auto get_surface_transform() const -> Surface_transform;

#if defined(ERHE_GRAPHICS_API_VULKAN)
    // Current backbuffer formats. The headless (surfaceless) emulated swapchain
    // has no public Swapchain, so pipeline format derivation
    // (Render_pipeline_create_info::set_format_from_render_pass) queries these
    // from the Surface. Vulkan-only feature.
    [[nodiscard]] auto get_color_format() -> erhe::dataformat::Format;
    [[nodiscard]] auto get_depth_format() -> erhe::dataformat::Format;
#endif

    [[nodiscard]] auto get_impl() -> Surface_impl&;
    [[nodiscard]] auto get_impl() const -> const Surface_impl&;

private:
    std::unique_ptr<Surface_impl> m_impl;
};

} // namespace erhe::graphics

