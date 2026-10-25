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
class Buffer_impl;
class Command_buffer;
class Device;
class Device_impl;
class Frame_begin_info;
class Frame_end_info;
class Frame_state;
class Render_pass_impl;
class Swapchain_impl;
class Texture;
enum class Image_layout : unsigned int;
class Texture_subresource_range;
enum class Barrier_scope : unsigned int;
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
    // Populated from a Command_buffer_impl whose signal_cpu_completion()
    // was called; Vulkan's vkQueueSubmit2 only takes one fence per submit
    // so collect_synchronization() asserts no other cb already set it.
    VkFence                                fence{VK_NULL_HANDLE};
};

// Vulkan backend stub. Owns (eventually) a VkCommandPool +
// VkCommandBuffer pair allocated from the Device's graphics queue
// family. begin()/end() will wrap vkBeginCommandBuffer /
// vkEndCommandBuffer; submit happens via Device::submit_command_buffers.
// Descriptor operation counts of one command buffer, for the
// ERHE_LOG_VULKAN descriptor trace ("[CB_SUMMARY]", logged when the command
// buffer is submitted). Kept per command buffer, so the threads that record
// into their own command buffers count without sharing a counter.
class Descriptor_counters final
{
public:
    uint32_t push_buf_count {0};
    uint32_t push_img_count {0};
    uint32_t alloc_set_count{0};
    uint32_t heap_bind_count{0};
    uint32_t draw_count     {0};
};

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
    // The device frame this cb was obtained for; stamped by
    // Device_impl::get_command_buffer.
    [[nodiscard]] auto get_frame_id   () const noexcept -> std::uint64_t;
    void               set_frame_id   (std::uint64_t frame_id) noexcept;

    void begin();
    void end  ();

    [[nodiscard]] auto wait_for_swapchain(Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_swapchain   (const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool;
    void               end_swapchain     (const Frame_end_info& frame_end_info);

    void signal_gpu_completion_to    (Command_buffer& consumer);
    void wait_for_gpu_completion_signal();
    void signal_cpu_completion       ();
    void wait_for_cpu_completion_of  (Command_buffer& producer);

    // GPU-work primitives that record into m_vk_command_buffer.
    // upload_* allocate a staging buffer, vkCmdCopyBuffer[ToImage]
    // into the destination, and queue the staging buffer for
    // destruction once the GPU has consumed the cb (via the
    // device's per-frame completion handlers). The cb must be in
    // recording state (caller has called begin()); the caller is
    // responsible for ending and submitting the cb.
    void upload_to_buffer         (const Buffer& buffer, std::size_t offset, const void* data, std::size_t length);
    [[nodiscard]] auto try_upload_to_buffer(const Buffer& buffer, std::size_t offset, const void* data, std::size_t length, std::size_t staging_capacity_byte_count) -> bool;
    void upload_to_texture        (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void clear_texture            (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout(const Texture& texture, Image_layout new_layout);
    void texture_barrier          (const Texture& texture, const Texture_subresource_range& range, std::uint64_t usage_before, std::uint64_t usage_after, Barrier_scope scope);
    void buffer_barrier           (const Buffer& buffer, std::uint64_t usage_before, std::uint64_t usage_after, Barrier_scope scope);
    void cmd_texture_barrier      (std::uint64_t usage_before, std::uint64_t usage_after);
    void memory_barrier           (Memory_barrier_mask barriers);

    // Vulkan-specific: hand out the underlying VkCommandBuffer for
    // backend code that needs to record vkCmd* into it directly. Other
    // backends do not expose anything analogous.
    [[nodiscard]] auto get_vulkan_command_buffer() const noexcept -> VkCommandBuffer;

    // Debug-utils label region bookkeeping for Scoped_debug_group. The
    // begin/end vkCmd* calls are routed through the impl so it can keep
    // an open-region count, which end() verifies is zero: a cb-level
    // label region must be contained in the cb's recording lifetime
    // (VUID-vkCmdEndDebugUtilsLabelEXT-commandBuffer-recording). A
    // region spanning an end() + submit must be a queue-level label
    // (Scoped_queue_debug_group) instead. begin_debug_label returns
    // false (recording nothing) when the cb is not in the recording
    // state.
    [[nodiscard]] auto begin_debug_label(const VkDebugUtilsLabelEXT& label_info) -> bool;
    void               end_debug_label  ();

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
    // fence registered via wait_for_cpu_completion_of(producer) before the cb's submit
    // is enqueued. Called by Device_impl::submit_command_buffers prior
    // to building VkSubmitInfo2.
    // The Vulkan render pass started on this command buffer and not yet
    // ended (Render_pass_impl start_render_pass / end_render_pass);
    // nullptr outside a pass. Render_command_encoder_impl reads it for the
    // pipeline's render pass, sample count and color attachment count.
    [[nodiscard]] auto get_active_render_pass_impl() const noexcept -> Render_pass_impl*;
    [[nodiscard]] auto get_active_render_pass     () const noexcept -> VkRenderPass;
    void               set_active_render_pass_impl(Render_pass_impl* render_pass_impl) noexcept;

    [[nodiscard]] auto get_descriptor_counters() noexcept -> Descriptor_counters&;

    void pre_submit_wait();

    // Append this cb's wait/signal/command-buffer submit-info entries to
    // the supplied aggregate. Adds:
    //  - this cb's VkCommandBufferSubmitInfo,
    //  - this cb's own implicit_semaphore as a wait when
    //    wait_for_gpu_completion_signal() was called,
    //  - consumer's implicit_semaphore for each signal_gpu_completion_to(consumer),
    //  - the swapchain acquire-semaphore (wait) and present-semaphore
    //    (signal) when this cb engaged a swapchain via begin_swapchain,
    //  - this cb's own implicit_fence as submit_info.fence when
    //    signal_cpu_completion() was called (asserting no other cb already
    //    set it; vkQueueSubmit2 takes only one fence per submit).
    void collect_synchronization(Vulkan_submit_info& submit_info);

private:
    void record_buffer_upload_copy(VkBuffer source_buffer, std::size_t source_offset, Buffer_impl& impl, std::size_t offset, std::size_t length);

    // Held as Device_impl* (not Device*) so that a Command_buffer_impl
    // destroyed during Device_impl::~Device_impl can still reach its
    // Device_impl. unique_ptr::reset() zeroes its raw pointer before
    // running the deleter, so going through Device::get_impl() (which
    // dereferences Device::m_impl) at that point would dereference null.
    Device_impl*               m_device_impl{nullptr};
    erhe::utility::Debug_label m_debug_label;
    std::uint64_t              m_frame_id{0};
    VkCommandBuffer            m_vk_command_buffer{VK_NULL_HANDLE};
    bool                       m_recording{false};
    int                        m_open_debug_label_count{0};

    VkSemaphore                m_implicit_semaphore{VK_NULL_HANDLE};
    VkFence                    m_implicit_fence    {VK_NULL_HANDLE};

    // Producers whose implicit fence this cb's submit blocks on
    // (wait_for_cpu_completion_of).
    std::vector<Command_buffer*> m_wait_for_cpu_producers;
    // Consumers whose implicit semaphore this cb's submit signals
    // (signal_gpu_completion_to).
    std::vector<Command_buffer*> m_signal_gpu_consumers;
    // This cb's submit waits on its own implicit semaphore
    // (wait_for_gpu_completion_signal).
    bool                         m_wait_for_own_gpu_signal{false};
    // This cb's submit signals its own implicit fence
    // (signal_cpu_completion).
    bool                         m_signal_own_cpu_completion{false};

    // Set by begin_swapchain when the per-frame swapchain lookup
    // succeeds. Cleared after collect_synchronization spliced the
    // swapchain semaphores into a submit. Used so collect_synchronization
    // can find the active per-slot acquire/present semaphores without
    // having Device_impl thread them through.
    Swapchain_impl*            m_swapchain_used{nullptr};
    Render_pass_impl*          m_active_render_pass_impl{nullptr};
    Descriptor_counters        m_descriptor_counters{};
};

} // namespace erhe::graphics
