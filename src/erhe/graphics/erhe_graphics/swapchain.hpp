#pragma once

#include "erhe_dataformat/dataformat.hpp"

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

class Frame_begin_info
{
public:
    uint32_t resize_width  {0};
    uint32_t resize_height {0};
    bool     request_resize{false};
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

// Result of Device::wait_for_displayed_frame (frame pacing FR5 present-wait
// clamp, implementation plan step P2.2).
enum class Present_wait_result : unsigned int {
    displayed   = 0, // the frame is known to have reached the display
    timeout     = 1, // not displayed within the bounded timeout
    unsupported = 2  // no present-wait path: capability tier OFF, headless,
                     // GL backend, or the id predates the current swapchain
};

class Swapchain_impl;
class Swapchain
{
public:
    Swapchain(std::unique_ptr<Swapchain_impl>&& swapchain_impl);
    ~Swapchain() noexcept;

    [[nodiscard]] auto has_depth       () const -> bool;
    [[nodiscard]] auto has_stencil    () const -> bool;
    [[nodiscard]] auto get_color_format() const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_depth_format() const -> erhe::dataformat::Format;

    [[nodiscard]] auto get_impl() -> Swapchain_impl&;
    [[nodiscard]] auto get_impl() const -> const Swapchain_impl&;

private:
    std::unique_ptr<Swapchain_impl> m_impl;
};

} // namespace erhe::graphics
