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
    Swapchain(std::unique_ptr<Swapchain_impl>&& swapchain_impl);
    ~Swapchain() noexcept;

    void start_of_frame();
    void present       ();

    [[nodiscard]] auto get_impl() -> Swapchain_impl&;
    [[nodiscard]] auto get_impl() const -> const Swapchain_impl&;

private:
    std::unique_ptr<Swapchain_impl> m_impl;
};

} // namespace erhe::graphics
