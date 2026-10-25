#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_device_sync_pool.hpp"
#include "erhe_graphics/vulkan/vulkan_emulated_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_verify/verify.hpp"

#include "vk_mem_alloc.h"

#include <cstdlib>
#include <cstring>

namespace erhe::graphics {

Command_buffer_impl::Command_buffer_impl(Device& device, erhe::utility::Debug_label debug_label)
    : m_device_impl{&device.get_impl()}
    , m_debug_label{std::move(debug_label)}
{
}

Command_buffer_impl::~Command_buffer_impl() noexcept
{
    // Implicit fence + semaphore are owned by Device_sync_pool; recycle
    // them back when the cb is destroyed (which happens when the
    // per-thread command pool is reset on the next frame-in-flight
    // cycle, or at Device_impl teardown). We only own these handles
    // when set_implicit_sync was called -- e.g. moved-from instances or
    // device-less stubs leave both fields null.
    if (m_device_impl != nullptr) {
        Device_sync_pool& sync_pool = m_device_impl->get_sync_pool();
        if (m_implicit_semaphore != VK_NULL_HANDLE) {
            sync_pool.recycle_semaphore(m_implicit_semaphore);
            m_implicit_semaphore = VK_NULL_HANDLE;
        }
        if (m_implicit_fence != VK_NULL_HANDLE) {
            sync_pool.recycle_fence(m_implicit_fence);
            m_implicit_fence = VK_NULL_HANDLE;
        }
    }
}

Command_buffer_impl::Command_buffer_impl(Command_buffer_impl&& other) noexcept
    : m_device_impl              {other.m_device_impl}
    , m_debug_label              {std::move(other.m_debug_label)}
    , m_frame_id                 {other.m_frame_id}
    , m_vk_command_buffer        {other.m_vk_command_buffer}
    , m_recording                {other.m_recording}
    , m_open_debug_label_count   {other.m_open_debug_label_count}
    , m_implicit_semaphore       {other.m_implicit_semaphore}
    , m_implicit_fence           {other.m_implicit_fence}
    , m_wait_for_cpu_producers   {std::move(other.m_wait_for_cpu_producers)}
    , m_signal_gpu_consumers     {std::move(other.m_signal_gpu_consumers)}
    , m_wait_for_own_gpu_signal  {other.m_wait_for_own_gpu_signal}
    , m_signal_own_cpu_completion{other.m_signal_own_cpu_completion}
    , m_swapchain_used           {other.m_swapchain_used}
    , m_active_render_pass_impl  {other.m_active_render_pass_impl}
    , m_descriptor_counters      {other.m_descriptor_counters}
{
    other.m_device_impl               = nullptr;
    other.m_vk_command_buffer         = VK_NULL_HANDLE;
    other.m_recording                 = false;
    other.m_open_debug_label_count    = 0;
    other.m_implicit_semaphore        = VK_NULL_HANDLE;
    other.m_implicit_fence            = VK_NULL_HANDLE;
    other.m_wait_for_own_gpu_signal   = false;
    other.m_signal_own_cpu_completion = false;
    other.m_swapchain_used            = nullptr;
    other.m_active_render_pass_impl   = nullptr;
}

auto Command_buffer_impl::operator=(Command_buffer_impl&& other) noexcept -> Command_buffer_impl&
{
    if (this == &other) {
        return *this;
    }

    // Recycle anything we currently hold before adopting other's state.
    if (m_device_impl != nullptr) {
        Device_sync_pool& sync_pool = m_device_impl->get_sync_pool();
        if (m_implicit_semaphore != VK_NULL_HANDLE) {
            sync_pool.recycle_semaphore(m_implicit_semaphore);
        }
        if (m_implicit_fence != VK_NULL_HANDLE) {
            sync_pool.recycle_fence(m_implicit_fence);
        }
    }

    m_device_impl             = other.m_device_impl;
    m_debug_label             = std::move(other.m_debug_label);
    m_frame_id                = other.m_frame_id;
    m_vk_command_buffer       = other.m_vk_command_buffer;
    m_recording               = other.m_recording;
    m_open_debug_label_count  = other.m_open_debug_label_count;
    m_implicit_semaphore      = other.m_implicit_semaphore;
    m_implicit_fence          = other.m_implicit_fence;
    m_wait_for_cpu_producers    = std::move(other.m_wait_for_cpu_producers);
    m_signal_gpu_consumers      = std::move(other.m_signal_gpu_consumers);
    m_wait_for_own_gpu_signal   = other.m_wait_for_own_gpu_signal;
    m_signal_own_cpu_completion = other.m_signal_own_cpu_completion;
    m_swapchain_used            = other.m_swapchain_used;
    m_active_render_pass_impl   = other.m_active_render_pass_impl;
    m_descriptor_counters       = other.m_descriptor_counters;

    other.m_device_impl            = nullptr;
    other.m_vk_command_buffer      = VK_NULL_HANDLE;
    other.m_recording              = false;
    other.m_open_debug_label_count = 0;
    other.m_implicit_semaphore     = VK_NULL_HANDLE;
    other.m_implicit_fence         = VK_NULL_HANDLE;
    other.m_swapchain_used         = nullptr;
    other.m_active_render_pass_impl = nullptr;
    return *this;
}

auto Command_buffer_impl::get_active_render_pass_impl() const noexcept -> Render_pass_impl*
{
    return m_active_render_pass_impl;
}

auto Command_buffer_impl::get_active_render_pass() const noexcept -> VkRenderPass
{
    return (m_active_render_pass_impl != nullptr) ? m_active_render_pass_impl->get_render_pass() : VK_NULL_HANDLE;
}

void Command_buffer_impl::set_active_render_pass_impl(Render_pass_impl* const render_pass_impl) noexcept
{
    m_active_render_pass_impl = render_pass_impl;
}

auto Command_buffer_impl::get_descriptor_counters() noexcept -> Descriptor_counters&
{
    return m_descriptor_counters;
}

auto Command_buffer_impl::get_debug_label() const noexcept -> erhe::utility::Debug_label
{
    return m_debug_label;
}

auto Command_buffer_impl::get_frame_id() const noexcept -> std::uint64_t
{
    return m_frame_id;
}

void Command_buffer_impl::set_frame_id(const std::uint64_t frame_id) noexcept
{
    m_frame_id = frame_id;
}

void Command_buffer_impl::begin()
{
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    // ONE_TIME_SUBMIT_BIT matches the lifetime contract: every cb is
    // freshly allocated by Device_impl::get_command_buffer from a
    // TRANSIENT pool, recorded once, submitted at most once, and
    // released back to the pool at the next reset cycle.
    const VkCommandBufferBeginInfo begin_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr
    };
    const VkResult result = vkBeginCommandBuffer(m_vk_command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        log_context->critical(
            "vkBeginCommandBuffer() failed with {} {}",
            static_cast<int32_t>(result), c_str(result)
        );
        std::abort();
    }
    // Reset the GPU timer query pool slice for the current frame in flight,
    // if pending. Must happen here (after vkBeginCommandBuffer, before any
    // render pass) because vkCmdResetQueryPool is forbidden inside a render
    // pass. Subsequent cbs in the same frame find the flag already cleared
    // and skip the reset. See Device_impl::maybe_reset_gpu_timer_slice.
    m_device_impl->maybe_reset_gpu_timer_slice(m_vk_command_buffer, m_frame_id);
    m_device_impl->record_frame_bracket_begin(m_vk_command_buffer, m_frame_id);
    m_recording = true;
    ERHE_VULKAN_TRACE(
        "Command_buffer_impl::begin() vk_cb=0x{:x} label='{}'",
        reinterpret_cast<std::uintptr_t>(m_vk_command_buffer),
        m_debug_label.string_view()
    );
}

void Command_buffer_impl::end()
{
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    // Cb-level debug label regions must close before the cb ends: the
    // end label records into the same cb as the begin label, and
    // recording into an ended cb violates
    // VUID-vkCmdEndDebugUtilsLabelEXT-commandBuffer-recording. A region
    // that needs to span this end() (e.g. a rendergraph node that ends
    // + submits the frame cb mid-execution) cannot be a cb label at
    // all -- it must use Scoped_queue_debug_group, selected by
    // Rendergraph::execute() via Rendergraph_node::submits_command_buffer().
    ERHE_VERIFY(m_open_debug_label_count == 0);

    const VkResult result = vkEndCommandBuffer(m_vk_command_buffer);
    if (result != VK_SUCCESS) {
        log_context->critical(
            "vkEndCommandBuffer() failed with {} {}",
            static_cast<int32_t>(result), c_str(result)
        );
        std::abort();
    }
    m_recording = false;
    ERHE_VULKAN_TRACE(
        "Command_buffer_impl::end() vk_cb=0x{:x} label='{}'",
        reinterpret_cast<std::uintptr_t>(m_vk_command_buffer),
        m_debug_label.string_view()
    );
}

auto Command_buffer_impl::begin_debug_label(const VkDebugUtilsLabelEXT& label_info) -> bool
{
    if (!m_recording || (m_vk_command_buffer == VK_NULL_HANDLE)) {
        return false;
    }
    vkCmdBeginDebugUtilsLabelEXT(m_vk_command_buffer, &label_info);
    ++m_open_debug_label_count;
    return true;
}

void Command_buffer_impl::end_debug_label()
{
    // end() verifies no regions are open, so a scope whose cb was ended
    // mid-lifetime aborts there, at the cause -- by the time this
    // destructor-driven call runs, the cb is still recording.
    ERHE_VERIFY(m_recording);
    ERHE_VERIFY(m_open_debug_label_count > 0);
    vkCmdEndDebugUtilsLabelEXT(m_vk_command_buffer);
    --m_open_debug_label_count;
}

auto Command_buffer_impl::get_vulkan_command_buffer() const noexcept -> VkCommandBuffer
{
    return m_vk_command_buffer;
}

void Command_buffer_impl::set_vulkan_command_buffer(VkCommandBuffer command_buffer) noexcept
{
    m_vk_command_buffer = command_buffer;
}

void Command_buffer_impl::set_implicit_sync(VkSemaphore semaphore, VkFence fence) noexcept
{
    m_implicit_semaphore = semaphore;
    m_implicit_fence     = fence;
}

auto Command_buffer_impl::get_implicit_semaphore() const noexcept -> VkSemaphore
{
    return m_implicit_semaphore;
}

auto Command_buffer_impl::get_implicit_fence() const noexcept -> VkFence
{
    return m_implicit_fence;
}

void Command_buffer_impl::signal_gpu_completion_to(Command_buffer& consumer)
{
    m_signal_gpu_consumers.push_back(&consumer);
}

void Command_buffer_impl::wait_for_gpu_completion_signal()
{
    m_wait_for_own_gpu_signal = true;
}

void Command_buffer_impl::signal_cpu_completion()
{
    m_signal_own_cpu_completion = true;
}

void Command_buffer_impl::wait_for_cpu_completion_of(Command_buffer& producer)
{
    m_wait_for_cpu_producers.push_back(&producer);
}

void Command_buffer_impl::pre_submit_wait()
{
    if (m_wait_for_cpu_producers.empty()) {
        return;
    }
    ERHE_VERIFY(m_device_impl != nullptr);
    const VkDevice vulkan_device = m_device_impl->get_vulkan_device();
    for (Command_buffer* producer : m_wait_for_cpu_producers) {
        ERHE_VERIFY(producer != nullptr);
        const VkFence fence = producer->get_impl().get_implicit_fence();
        if (fence == VK_NULL_HANDLE) {
            continue;
        }
        const double fence_wait_begin = erhe::frame_pacing::Frame_time_recorder::now();
        const VkResult result = vkWaitForFences(vulkan_device, 1, &fence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            log_context->critical(
                "vkWaitForFences() in pre_submit_wait failed with {} {}",
                static_cast<int32_t>(result), c_str(result)
            );
            m_device_impl->report_device_fault("pre_submit_wait vkWaitForFences");
            std::abort();
        }
        m_device_impl->get_frame_time_recorder().add(
            static_cast<std::int64_t>(m_frame_id),
            &erhe::frame_pacing::Frame_time_record::fence_wait_duration,
            erhe::frame_pacing::Frame_time_recorder::now() - fence_wait_begin
        );
    }
    m_wait_for_cpu_producers.clear();
}

// The copy and post-copy barrier shared by upload_to_buffer and
// try_upload_to_buffer: source_buffer holds the staged bytes at
// source_offset.
void Command_buffer_impl::record_buffer_upload_copy(
    const VkBuffer     source_buffer,
    const std::size_t  source_offset,
    Buffer_impl&       impl,
    const std::size_t  offset,
    const std::size_t  length
)
{
    const VkBufferCopy copy_region{
        .srcOffset = source_offset,
        .dstOffset = offset,
        .size      = length
    };
    vkCmdCopyBuffer(m_vk_command_buffer, source_buffer, impl.get_vk_buffer(), 1, &copy_region);

    // Chain the transfer write to the precise consumer stages this buffer
    // was created for, so downstream readers don't need their own
    // pre-barrier. The next copy into the buffer is a consumer too: every
    // VkBuffer is created with TRANSFER_DST (Buffer_impl), whatever its
    // Buffer_usage says, and a later upload_to_buffer into the same range -
    // in this command buffer or in one submitted after it - must not race
    // this write (write-after-write).
    const Vk_stage_access_2     dst_scope = buffer_usage_to_vk_stage_access(impl.get_usage());
    const VkPipelineStageFlags2 dst_stage_mask =
        ((dst_scope.stage != 0) ? dst_scope.stage : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) | VK_PIPELINE_STAGE_2_COPY_BIT;
    const VkAccessFlags2 dst_access_mask =
        ((dst_scope.access != 0) ? dst_scope.access : VK_ACCESS_2_MEMORY_READ_BIT) | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    const VkBufferMemoryBarrier2 buffer_barrier{
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask        = dst_stage_mask,
        .dstAccessMask       = dst_access_mask,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = impl.get_vk_buffer(),
        .offset              = static_cast<VkDeviceSize>(offset),
        .size                = static_cast<VkDeviceSize>(length)
    };
    const VkDependencyInfo post_copy_dep_info{
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 0,
        .pMemoryBarriers          = nullptr,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers    = &buffer_barrier,
        .imageMemoryBarrierCount  = 0,
        .pImageMemoryBarriers     = nullptr,
    };
    vkCmdPipelineBarrier2(m_vk_command_buffer, &post_copy_dep_info);
}

auto Command_buffer_impl::try_upload_to_buffer(
    const Buffer&     buffer,
    const std::size_t offset,
    const void*       data,
    const std::size_t length,
    const std::size_t staging_capacity_byte_count
) -> bool
{
    ERHE_VERIFY(m_device_impl != nullptr);
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    Buffer_impl& impl = const_cast<Buffer_impl&>(buffer.get_impl());

    // Host-visible destination: a memcpy through the map, no staging.
    if (impl.is_host_visible()) {
        std::span<std::byte> map = impl.map_bytes(offset, length);
        if (!map.empty()) {
            std::memcpy(map.data(), data, length);
            impl.end_write(offset, length);
            return true;
        }
    }

    // Staged through the device's capped upload staging ring; no room means
    // nothing is recorded and the caller retries next frame.
    Ring_buffer_range range = m_device_impl->allocate_upload_staging(Buffer_target::transfer_src, length, staging_capacity_byte_count);
    if (range.get_buffer() == nullptr) {
        return false;
    }
    ERHE_VERIFY(range.get_span().size() >= length);
    std::memcpy(range.get_span().data(), data, length);
    range.bytes_written(length);
    range.close();
    record_buffer_upload_copy(
        range.get_buffer()->get_buffer()->get_impl().get_vk_buffer(),
        range.get_byte_start_offset_in_buffer(),
        impl,
        offset,
        length
    );
    range.release();
    return true;
}

void Command_buffer_impl::upload_to_buffer(const Buffer& buffer, const std::size_t offset, const void* data, const std::size_t length)
{
    ERHE_VERIFY(m_device_impl != nullptr);
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    Buffer_impl& impl = const_cast<Buffer_impl&>(buffer.get_impl());

    // Host-visible buffers: just memcpy via the persistent map. No
    // command recording needed and no staging buffer involved.
    if (impl.is_host_visible()) {
        std::span<std::byte> map = impl.map_bytes(offset, length);
        if (!map.empty()) {
            std::memcpy(map.data(), data, length);
            impl.end_write(offset, length);
            return;
        }
    }

    Device_impl& device_impl = *m_device_impl;
    VmaAllocator allocator   = device_impl.get_allocator();

    const VkBufferCreateInfo staging_buffer_create_info{
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .size                  = length,
        .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr
    };
    VmaAllocationCreateInfo staging_alloc_info{
        .flags          = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage          = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags  = 0,
        .preferredFlags = 0,
        .memoryTypeBits = 0,
        .pool           = VK_NULL_HANDLE,
        .pUserData      = nullptr,
        .priority       = 0.0f
    };
    VkBuffer          staging_buffer{VK_NULL_HANDLE};
    VmaAllocation     staging_allocation{VK_NULL_HANDLE};
    VmaAllocationInfo staging_allocation_info{};
    const VkResult result = vmaCreateBuffer(allocator, &staging_buffer_create_info, &staging_alloc_info, &staging_buffer, &staging_allocation, &staging_allocation_info);
    if (result != VK_SUCCESS) {
        log_buffer->error("upload_to_buffer: staging buffer creation failed with {}", static_cast<int32_t>(result));
        return;
    }
    vmaSetAllocationName(allocator, staging_allocation, "upload_to_buffer staging");
    std::memcpy(staging_allocation_info.pMappedData, data, length);
    vmaFlushAllocation(allocator, staging_allocation, 0, length);

    record_buffer_upload_copy(staging_buffer, 0, impl, offset, length);

    // Defer staging buffer destruction until the GPU has consumed this
    // frame. add_completion_handler queues against the calling role's
    // frame (Device_impl::get_frame_index(), the frame of this cb); the
    // timeline semaphore signaled by that frame's end_frame is what
    // releases the handler.
    device_impl.add_completion_handler(
        [allocator, staging_buffer, staging_allocation](Device_impl&) {
            vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);
        }
    );
}

void Command_buffer_impl::upload_to_texture(
    const Texture&           texture,
    int                      level,
    int                      x,
    int                      y,
    int                      width,
    int                      height,
    erhe::dataformat::Format pixelformat,
    const void*              data,
    int                      row_stride
)
{
    ERHE_VERIFY(m_device_impl != nullptr);
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);
    // Block-compressed uploads go through Blit_command_encoder::copy_from_buffer
    ERHE_VERIFY(!erhe::dataformat::is_block_compressed(pixelformat));

    if ((data == nullptr) || (width <= 0) || (height <= 0)) {
        return;
    }

    const std::size_t pixel_size = erhe::dataformat::get_format_size_bytes(pixelformat);
    const std::size_t src_stride = (row_stride > 0)
        ? static_cast<std::size_t>(row_stride)
        : (static_cast<std::size_t>(width) * pixel_size);
    const std::size_t data_size  = src_stride * static_cast<std::size_t>(height);

    Device_impl& device_impl = *m_device_impl;
    VmaAllocator allocator   = device_impl.get_allocator();

    VmaAllocationCreateInfo staging_alloc_info{};
    staging_alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    staging_alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    const VkBufferCreateInfo staging_buffer_info{
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .size                  = data_size,
        .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr
    };

    VkBuffer          staging_buffer{VK_NULL_HANDLE};
    VmaAllocation     staging_allocation{VK_NULL_HANDLE};
    VmaAllocationInfo staging_alloc_result{};

    const VkResult result = vmaCreateBuffer(allocator, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_allocation, &staging_alloc_result);
    if (result != VK_SUCCESS) {
        log_texture->error("upload_to_texture: staging buffer creation failed with {}", static_cast<int32_t>(result));
        return;
    }
    vmaSetAllocationName(allocator, staging_allocation, "upload_to_texture staging");

    std::memcpy(staging_alloc_result.pMappedData, data, data_size);
    vmaFlushAllocation(allocator, staging_allocation, 0, data_size);

    const Texture_impl& tex_impl         = texture.get_impl();
    VkImage             destination_image = tex_impl.get_vk_image();

    const VkBufferImageCopy region{
        .bufferOffset      = 0,
        .bufferRowLength   = (row_stride > 0) ? static_cast<uint32_t>(row_stride / pixel_size) : 0,
        .bufferImageHeight = 0,
        .imageSubresource  = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = static_cast<uint32_t>(level + tex_impl.get_view_base_level()),
            .baseArrayLayer = static_cast<uint32_t>(tex_impl.get_view_base_array_layer()),
            .layerCount     = 1
        },
        .imageOffset = {x, y, 0},
        .imageExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            1
        }
    };

    const Image_subresource_range range{.base_level = static_cast<uint32_t>(level), .level_count = 1, .base_layer = 0, .layer_count = 1};
    tex_impl.transition_layout(m_vk_command_buffer, range, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(m_vk_command_buffer, staging_buffer, destination_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    tex_impl.transition_layout(m_vk_command_buffer, range, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    device_impl.add_completion_handler(
        [allocator, staging_buffer, staging_allocation](Device_impl&) {
            vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);
        }
    );
}

void Command_buffer_impl::clear_texture(const Texture& texture, std::array<double, 4> value)
{
    ERHE_VERIFY(m_device_impl != nullptr);
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    const Texture_impl& tex_impl = texture.get_impl();
    VkImage             image    = tex_impl.get_vk_image();
    if (image == VK_NULL_HANDLE) {
        return;
    }

    const erhe::dataformat::Format pixelformat = tex_impl.get_pixelformat();
    const bool is_depth   = erhe::dataformat::get_depth_size_bits(pixelformat) > 0;
    const bool is_stencil = erhe::dataformat::get_stencil_size_bits(pixelformat) > 0;

    const VkImageLayout final_layout = (is_depth || is_stencil)
        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // The subresources this texture covers: all of the image for a texture,
    // the view's own levels and layers for a view. transition_layout() moves
    // exactly these to TRANSFER_DST, so the clear must not reach past them.
    const Image_subresource_range all        = tex_impl.get_all_subresources();
    const uint32_t                base_level = all.base_level + static_cast<uint32_t>(tex_impl.get_view_base_level());
    const uint32_t                base_layer = all.base_layer + static_cast<uint32_t>(tex_impl.get_view_base_array_layer());

    tex_impl.transition_layout(m_vk_command_buffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (is_depth || is_stencil) {
        const VkClearDepthStencilValue clear_depth_stencil{
            .depth   = static_cast<float>(value[0]),
            .stencil = static_cast<uint32_t>(value[1])
        };
        VkImageAspectFlags aspect_mask = 0;
        if (is_depth)   aspect_mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (is_stencil) aspect_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        const VkImageSubresourceRange range{aspect_mask, base_level, all.level_count, base_layer, all.layer_count};
        vkCmdClearDepthStencilImage(m_vk_command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_depth_stencil, 1, &range);
    } else {
        const VkClearColorValue clear_color{
            .float32 = {
                static_cast<float>(value[0]),
                static_cast<float>(value[1]),
                static_cast<float>(value[2]),
                static_cast<float>(value[3])
            }
        };
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, base_level, all.level_count, base_layer, all.layer_count};
        vkCmdClearColorImage(m_vk_command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range);
    }
    tex_impl.transition_layout(m_vk_command_buffer, final_layout);
}

void Command_buffer_impl::transition_texture_layout(const Texture& texture, Image_layout new_layout)
{
    ERHE_VERIFY(m_device_impl != nullptr);
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    const Texture_impl& tex_impl = texture.get_impl();
    VkImage             image    = tex_impl.get_vk_image();
    if (image == VK_NULL_HANDLE) {
        return;
    }

    tex_impl.transition_layout(m_vk_command_buffer, to_vk_image_layout(new_layout));
}

void Command_buffer_impl::memory_barrier(Memory_barrier_mask barriers)
{
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    // Translate the OpenGL-shaped Memory_barrier_mask bits to Vulkan
    // dst stage/access. Source side is conservative: cover any prior
    // memory write at any pipeline stage. Caller emits this between
    // render passes (vkCmdPipelineBarrier2 inside a render pass would
    // need a matching subpass self-dependency).
    const unsigned int mask = static_cast<unsigned int>(barriers);

    VkPipelineStageFlags2 dst_stage  = 0;
    VkAccessFlags2        dst_access = 0;

    constexpr unsigned int vertex_attrib_array_barrier_bit  = 0x00000001u;
    constexpr unsigned int element_array_barrier_bit        = 0x00000002u;
    constexpr unsigned int uniform_barrier_bit              = 0x00000004u;
    constexpr unsigned int texture_fetch_barrier_bit        = 0x00000008u;
    constexpr unsigned int shader_image_access_barrier_bit  = 0x00000020u;
    constexpr unsigned int command_barrier_bit              = 0x00000040u;
    constexpr unsigned int pixel_buffer_barrier_bit         = 0x00000080u;
    constexpr unsigned int texture_update_barrier_bit       = 0x00000100u;
    constexpr unsigned int buffer_update_barrier_bit        = 0x00000200u;
    constexpr unsigned int framebuffer_barrier_bit          = 0x00000400u;
    constexpr unsigned int atomic_counter_barrier_bit       = 0x00001000u;
    constexpr unsigned int shader_storage_barrier_bit       = 0x00002000u;
    constexpr unsigned int client_mapped_buffer_barrier_bit = 0x00004000u;

    constexpr VkPipelineStageFlags2 any_shader_stage =
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    constexpr VkAccessFlags2        shader_storage_rw =
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

    if ((mask & vertex_attrib_array_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        dst_access |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if ((mask & element_array_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        dst_access |= VK_ACCESS_2_INDEX_READ_BIT;
    }
    if ((mask & uniform_barrier_bit) != 0) {
        dst_stage  |= any_shader_stage;
        dst_access |= VK_ACCESS_2_UNIFORM_READ_BIT;
    }
    if ((mask & texture_fetch_barrier_bit) != 0) {
        dst_stage  |= any_shader_stage;
        dst_access |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }
    if ((mask & shader_image_access_barrier_bit) != 0) {
        dst_stage  |= any_shader_stage;
        dst_access |= shader_storage_rw;
    }
    if ((mask & command_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        dst_access |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    }
    if ((mask & pixel_buffer_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        dst_access |= VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if ((mask & texture_update_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        dst_access |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if ((mask & buffer_update_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        dst_access |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if ((mask & framebuffer_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                   |  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                   |  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        dst_access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
                   |  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                   |  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                   |  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    if ((mask & atomic_counter_barrier_bit) != 0) {
        dst_stage  |= any_shader_stage;
        dst_access |= shader_storage_rw;
    }
    if ((mask & shader_storage_barrier_bit) != 0) {
        dst_stage  |= any_shader_stage;
        dst_access |= shader_storage_rw;
    }
    if ((mask & client_mapped_buffer_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_HOST_BIT;
        dst_access |= VK_ACCESS_2_HOST_READ_BIT;
    }

    if (dst_stage == 0) {
        return;
    }

    const VkMemoryBarrier2 memory_barrier_info{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext         = nullptr,
        .srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask  = dst_stage,
        .dstAccessMask = dst_access,
    };
    const VkDependencyInfo dependency_info{
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 1,
        .pMemoryBarriers          = &memory_barrier_info,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers    = nullptr,
        .imageMemoryBarrierCount  = 0,
        .pImageMemoryBarriers     = nullptr,
    };
    vkCmdPipelineBarrier2(m_vk_command_buffer, &dependency_info);

    ERHE_VULKAN_SYNC_TRACE(
        "[MEM_BARRIER] mask=0x{:x} src_stage={} src_access={} dst_stage={} dst_access={}",
        mask,
        pipeline_stage_flags_str(memory_barrier_info.srcStageMask),
        access_flags_str(memory_barrier_info.srcAccessMask),
        pipeline_stage_flags_str(memory_barrier_info.dstStageMask),
        access_flags_str(memory_barrier_info.dstAccessMask)
    );
}

void Command_buffer_impl::texture_barrier(
    const Texture&                   texture,
    const Texture_subresource_range& range,
    const std::uint64_t              usage_before,
    const std::uint64_t              usage_after,
    const Barrier_scope              scope
)
{
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    // The first synchronization scope of a pipeline barrier holds every
    // command submitted to the queue before it (vkspec "Execution and Memory
    // Dependencies"), so earlier_submissions records the same barrier as
    // command_buffer: this backend submits on one queue.
    static_cast<void>(scope);

    const Texture_impl& tex_impl = texture.get_impl();
    if (tex_impl.get_vk_image() == VK_NULL_HANDLE) {
        return;
    }
    const Image_subresource_range all = tex_impl.get_all_subresources();
    ERHE_VERIFY(range.base_level < all.level_count);
    ERHE_VERIFY(range.base_layer < all.layer_count);
    const uint32_t level_count = (range.level_count == Texture_subresource_range::remaining) ? (all.level_count - range.base_level) : range.level_count;
    const uint32_t layer_count = (range.layer_count == Texture_subresource_range::remaining) ? (all.layer_count - range.base_layer) : range.layer_count;
    ERHE_VERIFY((range.base_level + level_count) <= all.level_count);
    ERHE_VERIFY((range.base_layer + layer_count) <= all.layer_count);

    const erhe::dataformat::Format pixelformat = texture.get_pixelformat();
    const Image_aspect_kind aspect_kind =
        ((erhe::dataformat::get_depth_size_bits(pixelformat) > 0) || (erhe::dataformat::get_stencil_size_bits(pixelformat) > 0))
            ? Image_aspect_kind::depth_stencil
            : Image_aspect_kind::color;
    const Vk_stage_access_2 src        = image_usage_to_vk_barrier_scope(usage_before, Barrier_side::source);
    const Vk_stage_access_2 dst        = image_usage_to_vk_barrier_scope(usage_after,  Barrier_side::destination);
    const VkImageLayout     old_layout = image_usage_to_vk_barrier_layout(usage_before, aspect_kind);
    const VkImageLayout     new_layout = image_usage_to_vk_barrier_layout(usage_after,  aspect_kind);
    tex_impl.record_usage_barrier(
        m_vk_command_buffer,
        Image_subresource_range{
            .base_level  = range.base_level,
            .level_count = level_count,
            .base_layer  = range.base_layer,
            .layer_count = layer_count
        },
        src.stage,
        src.access,
        dst.stage,
        dst.access,
        old_layout,
        new_layout
    );
}

void Command_buffer_impl::buffer_barrier(const Buffer& buffer, const std::uint64_t usage_before, const std::uint64_t usage_after, const Barrier_scope scope)
{
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);
    static_cast<void>(scope); // see texture_barrier

    const VkBuffer vk_buffer = buffer.get_impl().get_vk_buffer();
    if (vk_buffer == VK_NULL_HANDLE) {
        return;
    }
    const Vk_stage_access_2 src = buffer_usage_to_vk_barrier_scope(usage_before, Barrier_side::source);
    const Vk_stage_access_2 dst = buffer_usage_to_vk_barrier_scope(usage_after,  Barrier_side::destination);
    if ((src.stage == 0) || (dst.stage == 0)) {
        return; // usage_before none: nothing earlier to order against
    }
    const VkBufferMemoryBarrier2 buffer_barrier_info{
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = src.stage,
        .srcAccessMask       = src.access,
        .dstStageMask        = dst.stage,
        .dstAccessMask       = dst.access,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = vk_buffer,
        .offset              = 0,
        .size                = VK_WHOLE_SIZE
    };
    const VkDependencyInfo dependency_info{
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 0,
        .pMemoryBarriers          = nullptr,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers    = &buffer_barrier_info,
        .imageMemoryBarrierCount  = 0,
        .pImageMemoryBarriers     = nullptr,
    };
    vkCmdPipelineBarrier2(m_vk_command_buffer, &dependency_info);

    ERHE_VULKAN_SYNC_TRACE(
        "[BUF_BARRIER] buffer=0x{:x} src_stage={} src_access={} dst_stage={} dst_access={}",
        reinterpret_cast<std::uintptr_t>(vk_buffer),
        pipeline_stage_flags_str(src.stage),
        access_flags_str(src.access),
        pipeline_stage_flags_str(dst.stage),
        access_flags_str(dst.access)
    );
}

void Command_buffer_impl::cmd_texture_barrier(std::uint64_t usage_before, std::uint64_t usage_after)
{
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    // The texture_barrier mapping (stage qualifiers honoured); it ignores
    // the user_synchronized modifier, which only controls whether the
    // automatic end-of-renderpass barrier fires.
    const Vk_stage_access_2 src_sa = image_usage_to_vk_barrier_scope(usage_before, Barrier_side::source);
    const Vk_stage_access_2 dst_sa = image_usage_to_vk_barrier_scope(usage_after,  Barrier_side::destination);

    if ((src_sa.stage == 0) || (dst_sa.stage == 0)) {
        return;
    }

    const VkMemoryBarrier2 memory_barrier{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext         = nullptr,
        .srcStageMask  = src_sa.stage,
        .srcAccessMask = src_sa.access,
        .dstStageMask  = dst_sa.stage,
        .dstAccessMask = dst_sa.access,
    };
    const VkDependencyInfo dependency_info{
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 1,
        .pMemoryBarriers          = &memory_barrier,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers    = nullptr,
        .imageMemoryBarrierCount  = 0,
        .pImageMemoryBarriers     = nullptr,
    };
    vkCmdPipelineBarrier2(m_vk_command_buffer, &dependency_info);
}

void Command_buffer_impl::collect_synchronization(Vulkan_submit_info& submit_info)
{
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    submit_info.command_buffers.push_back(VkCommandBufferSubmitInfo{
        .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .pNext         = nullptr,
        .commandBuffer = m_vk_command_buffer,
        .deviceMask    = 0,
    });

    if (m_wait_for_own_gpu_signal) {
        // A producer registered signal_gpu_completion_to(this); wait on
        // this cb's own implicit semaphore.
        ERHE_VERIFY(m_implicit_semaphore != VK_NULL_HANDLE);
        submit_info.wait_semaphores.push_back(VkSemaphoreSubmitInfo{
            .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext       = nullptr,
            .semaphore   = m_implicit_semaphore,
            .value       = 0,
            .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0,
        });
        m_wait_for_own_gpu_signal = false;
    }

    for (Command_buffer* consumer : m_signal_gpu_consumers) {
        ERHE_VERIFY(consumer != nullptr);
        const VkSemaphore semaphore = consumer->get_impl().get_implicit_semaphore();
        ERHE_VERIFY(semaphore != VK_NULL_HANDLE);
        submit_info.signal_semaphores.push_back(VkSemaphoreSubmitInfo{
            .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext       = nullptr,
            .semaphore   = semaphore,
            .value       = 0,
            .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0,
        });
    }
    m_signal_gpu_consumers.clear();

    if (m_signal_own_cpu_completion) {
        // VkSubmitInfo2 carries a single VkFence. Multiple cbs in the
        // same submit cannot each pin a different one.
        ERHE_VERIFY(submit_info.fence == VK_NULL_HANDLE);
        ERHE_VERIFY(m_implicit_fence != VK_NULL_HANDLE);
        submit_info.fence = m_implicit_fence;
        m_signal_own_cpu_completion = false;
    }

    if (m_swapchain_used != nullptr) {
        // Wait on the per-slot acquire-semaphore so the cb only writes
        // the swapchain image after vkAcquireNextImageKHR has returned
        // and the previous frame's present is finished. Signal the
        // per-slot present-semaphore so vkQueuePresentKHR (called via
        // Swapchain_impl::present from submit_command_buffers right
        // after the submit succeeds) can wait on cb completion.
        const VkSemaphore acquire_semaphore = m_swapchain_used->get_active_acquire_semaphore(m_frame_id);
        const VkSemaphore present_semaphore = m_swapchain_used->get_active_present_semaphore(m_frame_id);
        if (acquire_semaphore != VK_NULL_HANDLE) {
            submit_info.wait_semaphores.push_back(VkSemaphoreSubmitInfo{
                .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .pNext       = nullptr,
                .semaphore   = acquire_semaphore,
                .value       = 0,
                .stageMask   = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .deviceIndex = 0,
            });
        }
        if (present_semaphore != VK_NULL_HANDLE) {
            submit_info.signal_semaphores.push_back(VkSemaphoreSubmitInfo{
                .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .pNext       = nullptr,
                .semaphore   = present_semaphore,
                .value       = 0,
                .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .deviceIndex = 0,
            });
        }
        // Implicit present: Device_impl::submit_command_buffers will
        // call Swapchain_impl::present() on this entry after the
        // submit returns, so the user never has to call present()
        // explicitly.
        submit_info.swapchains_to_present.push_back(m_swapchain_used);
        m_swapchain_used = nullptr;
    }
}

auto Command_buffer_impl::wait_for_swapchain(Frame_state& out_frame_state) -> bool
{
    // Moved off Device_impl. Forwards to the backing Swapchain_impl;
    // the Vulkan swapchain owns its own per-frame-in-flight pacing
    // (vkAcquireNextImageKHR + acquire/present semaphore setup) which
    // also drives ensure_device_frame_slot for per-thread pool resets.
    // The swapchain indexes its per-slot state by this command buffer's
    // frame id.
    ERHE_VERIFY(m_device_impl != nullptr);
    Surface* surface = m_device_impl->get_surface();
    if (surface != nullptr) {
        if (surface->get_impl().is_headless()) {
            // Headless: drive the emulated swapchain instead of a real one.
            Emulated_swapchain_impl* emulated_swapchain = surface->get_impl().get_emulated_swapchain();
            if ((emulated_swapchain != nullptr) && emulated_swapchain->wait_frame(m_frame_id, out_frame_state)) {
                return true;
            }
        } else {
            Swapchain* swapchain = surface->get_swapchain();
            if (swapchain != nullptr) {
                const bool ok = swapchain->get_impl().wait_frame(m_frame_id, out_frame_state);
                ERHE_VULKAN_TRACE(
                    "Command_buffer_impl::wait_for_swapchain() ok={}",
                    ok
                );
                if (ok) {
                    return true;
                }
            }
        }
    }
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = false;
    // Demoted to debug: the editor calls this every frame and a
    // not-yet-ready surface (Android background / pre-foreground
    // launch race) is a normal state, not a warning condition.
    ERHE_VULKAN_TRACE("Command_buffer_impl::wait_for_swapchain() returning false (no surface or swapchain wait_frame failed)");
    return false;
}

auto Command_buffer_impl::begin_swapchain(const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool
{
    ERHE_VERIFY(m_device_impl != nullptr);
    Surface* surface = m_device_impl->get_surface();
    if (surface == nullptr) {
        out_frame_state.should_render = false;
        log_swapchain->warn("Command_buffer_impl::begin_swapchain() no surface");
        return false;
    }

    if (surface->get_impl().is_headless()) {
        Emulated_swapchain_impl* emulated_swapchain = surface->get_impl().get_emulated_swapchain();
        if (emulated_swapchain == nullptr) {
            out_frame_state.should_render = false;
            return false;
        }
        const bool ok = emulated_swapchain->begin_frame(frame_begin_info);
        out_frame_state.should_render = ok;
        // Intentionally do NOT set m_swapchain_used:
        // the emulated frame submits as an ordinary no-present device frame
        // (no acquire/present semaphores, no vkQueuePresentKHR).
        return ok;
    }

    Swapchain* swapchain = surface->get_swapchain();
    if (swapchain == nullptr) {
        out_frame_state.should_render = false;
        log_swapchain->warn("Command_buffer_impl::begin_swapchain() no swapchain");
        return false;
    }

    const bool ok = swapchain->get_impl().begin_frame(m_frame_id, frame_begin_info);
    out_frame_state.should_render = ok;
    ERHE_VULKAN_TRACE(
        "Command_buffer_impl::begin_swapchain() ok={} acquire_sem=0x{:x} present_sem=0x{:x}",
        ok,
        reinterpret_cast<std::uintptr_t>(swapchain->get_impl().get_active_acquire_semaphore(m_frame_id)),
        reinterpret_cast<std::uintptr_t>(swapchain->get_impl().get_active_present_semaphore(m_frame_id))
    );
    if (ok) {
        // Remember that this cb feeds the swapchain so
        // collect_synchronization later splices the per-slot
        // acquire (wait) and present (signal) semaphores into the
        // submit. Cleared after collect_synchronization runs.
        m_swapchain_used = &swapchain->get_impl();
    }
    return ok;
}

void Command_buffer_impl::end_swapchain(const Frame_end_info& /*frame_end_info*/)
{
    // No-op. begin_swapchain latches the swapchain on the cb;
    // submit + present run when the cb is handed to
    // Device::submit_command_buffers (implicit-present path), so
    // there is nothing to do here. The method exists for symmetry
    // with begin_swapchain and as a future hook for swapchain-
    // specific end-of-recording bookkeeping.
}

} // namespace erhe::graphics
