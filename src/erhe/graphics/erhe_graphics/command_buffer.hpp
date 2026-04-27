#pragma once

#include "erhe_utility/debug_label.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace erhe::dataformat {
    enum class Format : unsigned int;
}

namespace erhe::graphics {

class Buffer;
class Command_buffer_impl;
class Device;
class Frame_begin_info;
class Frame_end_info;
class Frame_state;
class Texture;
enum class Image_layout : unsigned int;
enum class Memory_barrier_mask : unsigned int;

// Public erhe::graphics handle for a backend command buffer. Currently a
// stub: the actual recording API will land as we iterate. Intent is for
// callers (notably erhe::xr) to hold an explicit Command_buffer instead
// of relying on the implicit single-cb-per-frame model so that
// submission timing is decoupled from Device::end_frame().
class Command_buffer final
{
public:
    explicit Command_buffer(Device& device, erhe::utility::Debug_label debug_label = {});
    ~Command_buffer        () noexcept;
    Command_buffer         (const Command_buffer&)            = delete;
    Command_buffer& operator=(const Command_buffer&)          = delete;
    Command_buffer         (Command_buffer&& other) noexcept;
    Command_buffer& operator=(Command_buffer&& other) noexcept;

    [[nodiscard]] auto get_debug_label() const noexcept -> erhe::utility::Debug_label;

    void begin();
    void end  ();

    // True between begin() and end(). Used by callers that may have already
    // ended + submitted the cb deeper in the call stack (e.g. OpenXR's
    // submit-before-xrEndFrame requirement) to avoid double end / double
    // submit.
    [[nodiscard]] auto is_recording() const noexcept -> bool;

    // Swapchain-frame lifecycle (moved off Device). wait_for_swapchain
    // pumps the per-frame-in-flight pacing on the backing Swapchain
    // (pool reset, fence recycle on Vulkan; acquire of next image and
    // setup of acquire/present semaphores) and fills Frame_state with
    // predicted display timing. begin_swapchain / end_swapchain bracket
    // recording that targets the swapchain image so the backend can
    // track which submit branch this Command_buffer feeds.
    [[nodiscard]] auto wait_for_swapchain(Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_swapchain   (const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool;
    void               end_swapchain     (const Frame_end_info& frame_end_info);

    // Cross-Command_buffer synchronization. Each Command_buffer carries
    // an implicit fence + binary semaphore allocated by Device_impl when
    // the cb is handed out by get_command_buffer(). The four methods
    // below register dependency edges that the backend collects at
    // submit time:
    //
    //   wait_for_semaphore(other)  -- this cb's submit waits on other's
    //                                 implicit semaphore (GPU->GPU).
    //   signal_semaphore  (other)  -- this cb's submit signals other's
    //                                 implicit semaphore (GPU->GPU).
    //   wait_for_fence    (other)  -- CPU-side vkWaitForFences on other's
    //                                 implicit fence before this cb's
    //                                 submit is enqueued.
    //   signal_fence      (other)  -- this cb's vkQueueSubmit2 will use
    //                                 other's implicit fence as its
    //                                 signal fence (only one signal_fence
    //                                 per cb).
    //
    // The fences/semaphores themselves are deliberately not exposed
    // through the public API; callers only ever name another
    // Command_buffer as the wait/signal target.
    void wait_for_fence    (Command_buffer& other);
    void wait_for_semaphore(Command_buffer& other);
    void signal_semaphore  (Command_buffer& other);
    void signal_fence      (Command_buffer& other);

    // GPU-work primitives. These record commands into the cb's
    // backend command buffer. Each one used to live on Device and
    // route through the now-removed immediate-commands path; with
    // the explicit cb model, the caller owns the cb and must
    // submit it via Device::submit_command_buffers (or batch it
    // with other cbs that the caller submits).
    void upload_to_buffer         (const Buffer& buffer, std::size_t offset, const void* data, std::size_t length);
    void upload_to_texture        (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void clear_texture            (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout(const Texture& texture, Image_layout new_layout);

    // Option B explicit barrier (between render passes). Inserts a
    // pipeline barrier from usage_before to usage_after into the cb.
    // Must be called outside of a render pass (between
    // Scoped_render_pass destructor and the next constructor).
    void cmd_texture_barrier      (std::uint64_t usage_before, std::uint64_t usage_after);

    // glMemoryBarrier-shaped global memory barrier. Records a
    // vkCmdPipelineBarrier2 (Vulkan) or glMemoryBarrier (GL) into the
    // cb. Must be called outside of a render pass.
    void memory_barrier           (Memory_barrier_mask barriers);

    [[nodiscard]] auto get_impl()       -> Command_buffer_impl&;
    [[nodiscard]] auto get_impl() const -> const Command_buffer_impl&;

private:
    std::unique_ptr<Command_buffer_impl> m_impl;
    bool                                 m_recording{false};
};

} // namespace erhe::graphics
