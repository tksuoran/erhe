#pragma once

#include "erhe_utility/debug_label.hpp"

#include "volk.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

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

// Backend-private aggregate that Device_impl::submit_command_buffers
// fills by walking each Command_buffer_impl in submit order. Holds
// every wait/signal already converted into Vulkan submit-info form so
// the final vkQueueSubmit2 only needs to splice the vectors into a
// VkSubmitInfo2.
class Vulkan_submit_info final
{
public:
    std::vector<VkCommandBufferSubmitInfo> command_buffers;
    std::vector<VkSemaphoreSubmitInfo>     wait_semaphores;
    std::vector<VkSemaphoreSubmitInfo>     signal_semaphores;
    // Swapchains whose present_semaphore is signalled by this submit.
    // After the submit succeeds Device_impl::submit_command_buffers
    // calls Swapchain_impl::present() on each entry to drive
    // vkQueuePresentKHR. Populated by Command_buffer_impl::collect_synchronization
    // when a cb engaged a swapchain via begin_swapchain.
    std::vector<Swapchain_impl*>           swapchains_to_present;
    // Single fence the submit signals on completion, if any.
    // Populated from a Command_buffer_impl whose signal_fence(...) was
    // called; Vulkan's vkQueueSubmit2 only takes one fence per submit
    // so collect_synchronization() asserts no other cb already set it.
    VkFence                                fence{VK_NULL_HANDLE};
};

// Vulkan backend stub. Owns (eventually) a VkCommandPool +
// VkCommandBuffer pair allocated from the Device's graphics queue
// family. begin()/end() will wrap vkBeginCommandBuffer /
// vkEndCommandBuffer; submit happens via Device::submit_command_buffers.
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

    // GPU-work primitives that record into m_vk_command_buffer.
    // upload_* allocate a staging buffer, vkCmdCopyBuffer[ToImage]
    // into the destination, and queue the staging buffer for
    // destruction once the GPU has consumed the cb (via the
    // device's per-frame completion handlers). The cb must be in
    // recording state (caller has called begin()); the caller is
    // responsible for ending and submitting the cb.
    void upload_to_buffer         (const Buffer& buffer, std::size_t offset, const void* data, std::size_t length);
    void upload_to_texture        (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void clear_texture            (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout(const Texture& texture, Image_layout new_layout);
    void cmd_texture_barrier      (std::uint64_t usage_before, std::uint64_t usage_after);
    void memory_barrier           (Memory_barrier_mask barriers);

    // Vulkan-specific: hand out the underlying VkCommandBuffer for
    // backend code that needs to record vkCmd* into it directly. Other
    // backends do not expose anything analogous.
    [[nodiscard]] auto get_vulkan_command_buffer() const noexcept -> VkCommandBuffer;

    // Vulkan-specific: bind a freshly-allocated VkCommandBuffer to this
    // Command_buffer_impl. Called by Device_impl::get_command_buffer
    // after vkAllocateCommandBuffers succeeds. The cb's lifetime is
    // owned by the per-(frame_in_flight, thread_slot) pool the
    // allocation came from -- the pool gets vkResetCommandPool'd when
    // the frame-in-flight slot's fence completes, freeing all cbs that
    // were allocated from it.
    void set_vulkan_command_buffer(VkCommandBuffer command_buffer) noexcept;

    // Bind the implicit fence + binary semaphore that this cb owns for
    // the duration of its pool cycle. Called by Device_impl from
    // get_command_buffer right after the cb is wrapped. Both handles
    // come from Device_sync_pool and are recycled back in this impl's
    // destructor when the per-thread pool is reset.
    void set_implicit_sync(VkSemaphore semaphore, VkFence fence) noexcept;

    [[nodiscard]] auto get_implicit_semaphore() const noexcept -> VkSemaphore;
    [[nodiscard]] auto get_implicit_fence    () const noexcept -> VkFence;

    // CPU-side wait phase of submit. Performs vkWaitForFences on every
    // fence registered via wait_for_fence(other) before the cb's submit
    // is enqueued. Called by Device_impl::submit_command_buffers prior
    // to building VkSubmitInfo2.
    void pre_submit_wait();

    // Append this cb's wait/signal/command-buffer submit-info entries to
    // the supplied aggregate. Adds:
    //  - this cb's VkCommandBufferSubmitInfo,
    //  - other's implicit_semaphore for each wait_for_semaphore(other),
    //  - other's implicit_semaphore for each signal_semaphore(other),
    //  - the swapchain acquire-semaphore (wait) and present-semaphore
    //    (signal) when this cb engaged a swapchain via begin_swapchain,
    //  - other's implicit_fence as submit_info.fence when
    //    signal_fence(other) was called (asserting no other cb already
    //    set it; vkQueueSubmit2 takes only one fence per submit).
    void collect_synchronization(Vulkan_submit_info& submit_info);

private:
    Device*                    m_device{nullptr};
    erhe::utility::Debug_label m_debug_label;
    VkCommandBuffer            m_vk_command_buffer{VK_NULL_HANDLE};

    VkSemaphore                m_implicit_semaphore{VK_NULL_HANDLE};
    VkFence                    m_implicit_fence    {VK_NULL_HANDLE};

    std::vector<Command_buffer*> m_wait_for_fence_list;
    std::vector<Command_buffer*> m_wait_for_semaphore_list;
    std::vector<Command_buffer*> m_signal_semaphore_list;
    std::vector<Command_buffer*> m_signal_fence_list;

    // Set by begin_swapchain when the per-frame swapchain lookup
    // succeeds. Cleared after collect_synchronization spliced the
    // swapchain semaphores into a submit. Used so collect_synchronization
    // can find the active per-slot acquire/present semaphores without
    // having Device_impl thread them through.
    Swapchain_impl*            m_swapchain_used{nullptr};
};

} // namespace erhe::graphics
