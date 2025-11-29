#pragma once

#include <memory>

namespace erhe::graphics {

class Device;
class Surface;

class Swapchain_create_info
{
public:
    Surface& surface;
};

class Swapchain_impl;
class Swapchain
{
public:
    Swapchain           (const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) noexcept;
    Swapchain& operator=(Swapchain&&) = delete;
    ~Swapchain() noexcept;

    Swapchain(Device& device, const Swapchain_create_info& create_info);

    [[nodiscard]] auto get_impl() -> Swapchain_impl&;
    [[nodiscard]] auto get_impl() const -> const Swapchain_impl&;

private:
    std::unique_ptr<Swapchain_impl> m_impl;
};

} // namespace erhe::graphics
