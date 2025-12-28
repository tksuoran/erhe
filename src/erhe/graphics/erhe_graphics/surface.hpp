#pragma once

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

    [[nodiscard]] auto get_impl() -> Surface_impl&;
    [[nodiscard]] auto get_impl() const -> const Surface_impl&;

private:
    std::unique_ptr<Surface_impl> m_impl;
};

} // namespace erhe::graphics

