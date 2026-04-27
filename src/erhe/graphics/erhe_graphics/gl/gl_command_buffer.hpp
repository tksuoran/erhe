#pragma once

#include "erhe_utility/debug_label.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace erhe::dataformat {
    enum class Format : unsigned int;
}

namespace erhe::graphics {

class Buffer;
class Command_buffer;
class Device;
class Frame_begin_info;
class Frame_end_info;
class Frame_state;
class Swapchain_impl;
class Texture;
enum class Image_layout : unsigned int;
enum class Memory_barrier_mask : unsigned int;

// OpenGL backend stub. GL has no native command buffer object, so this
// will eventually capture a deferred command list (or stay a thin
// recording handle and execute against the implicit GL context). Filled
// in as we iterate.
class Command_buffer_impl final
{
public:
    Command_buffer_impl        (Device& device, erhe::utility::Debug_label debug_label);
    ~Command_buffer_impl       () noexcept;
    Command_buffer_impl        (const Command_buffer_impl&)            = delete;
    Command_buffer_impl& operator=(const Command_buffer_impl&)         = delete;
    Command_buffer_impl        (Command_buffer_impl&& other) noexcept;
    Command_buffer_impl& operator=(Command_buffer_impl&& other) noexcept;

    [[nodiscard]] auto get_debug_label() const noexcept -> erhe::utility::Debug_label;

    void begin();
    void end  ();

    [[nodiscard]] auto wait_for_swapchain(Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_swapchain   (const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool;
    void               end_swapchain     (const Frame_end_info& frame_end_info);

    void wait_for_fence    (Command_buffer& other);
    void wait_for_semaphore(Command_buffer& other);
    void signal_semaphore  (Command_buffer& other);
    void signal_fence      (Command_buffer& other);

    void upload_to_buffer         (const Buffer& buffer, std::size_t offset, const void* data, std::size_t length);
    void upload_to_texture        (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void clear_texture            (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout(const Texture& texture, Image_layout new_layout);
    void cmd_texture_barrier      (std::uint64_t usage_before, std::uint64_t usage_after);
    void memory_barrier           (Memory_barrier_mask barriers);

    // Swapchain that this cb engaged via begin_swapchain. Used by
    // Device_impl::submit_command_buffers to drive the implicit
    // present (swap_buffers on GL). Cleared after present.
    [[nodiscard]] auto take_swapchain_used() noexcept -> Swapchain_impl*;

private:
    Device*                    m_device{nullptr};
    erhe::utility::Debug_label m_debug_label;
    Swapchain_impl*            m_swapchain_used{nullptr};
};

} // namespace erhe::graphics
