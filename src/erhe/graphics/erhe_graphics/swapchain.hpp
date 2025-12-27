#pragma once

#include <memory>

namespace erhe::graphics {

class Device;
class Surface;


class Frame_state
{
public:
    uint64_t predicted_display_time  {0};
    uint64_t predicted_display_period{0};
    bool     should_render           {false};
};

class Frame_end_info
{
public:
    uint64_t requested_display_time{0};
};

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

    void wait_frame (Frame_state& out_frame_state);
    void begin_frame();
    void end_frame  (const Frame_end_info& frame_end_info);

    [[nodiscard]] auto get_impl() -> Swapchain_impl&;
    [[nodiscard]] auto get_impl() const -> const Swapchain_impl&;

private:
    std::unique_ptr<Swapchain_impl> m_impl;
};

} // namespace erhe::graphics
