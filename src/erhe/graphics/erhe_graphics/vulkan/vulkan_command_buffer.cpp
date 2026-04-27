#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_device_sync_pool.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/graphics_log.hpp"
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
    : m_device     {&device}
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
    if (m_device != nullptr) {
        Device_sync_pool& sync_pool = m_device->get_impl().get_sync_pool();
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
    : m_device                   {other.m_device}
    , m_debug_label              {std::move(other.m_debug_label)}
    , m_vk_command_buffer        {other.m_vk_command_buffer}
    , m_implicit_semaphore       {other.m_implicit_semaphore}
    , m_implicit_fence           {other.m_implicit_fence}
    , m_wait_for_fence_list      {std::move(other.m_wait_for_fence_list)}
    , m_wait_for_semaphore_list  {std::move(other.m_wait_for_semaphore_list)}
    , m_signal_semaphore_list    {std::move(other.m_signal_semaphore_list)}
    , m_signal_fence_list        {std::move(other.m_signal_fence_list)}
    , m_swapchain_used           {other.m_swapchain_used}
{
    other.m_device              = nullptr;
    other.m_vk_command_buffer   = VK_NULL_HANDLE;
    other.m_implicit_semaphore  = VK_NULL_HANDLE;
    other.m_implicit_fence      = VK_NULL_HANDLE;
    other.m_swapchain_used      = nullptr;
}

auto Command_buffer_impl::operator=(Command_buffer_impl&& other) noexcept -> Command_buffer_impl&
{
    if (this == &other) {
        return *this;
    }

    // Recycle anything we currently hold before adopting other's state.
    if (m_device != nullptr) {
        Device_sync_pool& sync_pool = m_device->get_impl().get_sync_pool();
        if (m_implicit_semaphore != VK_NULL_HANDLE) {
            sync_pool.recycle_semaphore(m_implicit_semaphore);
        }
        if (m_implicit_fence != VK_NULL_HANDLE) {
            sync_pool.recycle_fence(m_implicit_fence);
        }
    }

    m_device                  = other.m_device;
    m_debug_label             = std::move(other.m_debug_label);
    m_vk_command_buffer       = other.m_vk_command_buffer;
    m_implicit_semaphore      = other.m_implicit_semaphore;
    m_implicit_fence          = other.m_implicit_fence;
    m_wait_for_fence_list     = std::move(other.m_wait_for_fence_list);
    m_wait_for_semaphore_list = std::move(other.m_wait_for_semaphore_list);
    m_signal_semaphore_list   = std::move(other.m_signal_semaphore_list);
    m_signal_fence_list       = std::move(other.m_signal_fence_list);
    m_swapchain_used          = other.m_swapchain_used;

    other.m_device              = nullptr;
    other.m_vk_command_buffer   = VK_NULL_HANDLE;
    other.m_implicit_semaphore  = VK_NULL_HANDLE;
    other.m_implicit_fence      = VK_NULL_HANDLE;
    other.m_swapchain_used      = nullptr;
    return *this;
}

auto Command_buffer_impl::get_debug_label() const noexcept -> erhe::utility::Debug_label
{
    return m_debug_label;
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
    log_swapchain->trace(
        "Command_buffer_impl::begin() vk_cb=0x{:x} label='{}'",
        reinterpret_cast<std::uintptr_t>(m_vk_command_buffer),
        m_debug_label.string_view()
    );
}

void Command_buffer_impl::end()
{
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    const VkResult result = vkEndCommandBuffer(m_vk_command_buffer);
    if (result != VK_SUCCESS) {
        log_context->critical(
            "vkEndCommandBuffer() failed with {} {}",
            static_cast<int32_t>(result), c_str(result)
        );
        std::abort();
    }
    log_swapchain->trace(
        "Command_buffer_impl::end() vk_cb=0x{:x} label='{}'",
        reinterpret_cast<std::uintptr_t>(m_vk_command_buffer),
        m_debug_label.string_view()
    );
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

void Command_buffer_impl::wait_for_fence(Command_buffer& other)
{
    m_wait_for_fence_list.push_back(&other);
}

void Command_buffer_impl::wait_for_semaphore(Command_buffer& other)
{
    m_wait_for_semaphore_list.push_back(&other);
}

void Command_buffer_impl::signal_semaphore(Command_buffer& other)
{
    m_signal_semaphore_list.push_back(&other);
}

void Command_buffer_impl::signal_fence(Command_buffer& other)
{
    m_signal_fence_list.push_back(&other);
}

void Command_buffer_impl::pre_submit_wait()
{
    if (m_wait_for_fence_list.empty()) {
        return;
    }
    ERHE_VERIFY(m_device != nullptr);
    const VkDevice vulkan_device = m_device->get_impl().get_vulkan_device();
    for (Command_buffer* other : m_wait_for_fence_list) {
        ERHE_VERIFY(other != nullptr);
        const VkFence fence = other->get_impl().get_implicit_fence();
        if (fence == VK_NULL_HANDLE) {
            continue;
        }
        const VkResult result = vkWaitForFences(vulkan_device, 1, &fence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            log_context->critical(
                "vkWaitForFences() in pre_submit_wait failed with {} {}",
                static_cast<int32_t>(result), c_str(result)
            );
            std::abort();
        }
    }
    m_wait_for_fence_list.clear();
}

void Command_buffer_impl::upload_to_buffer(const Buffer& buffer, const std::size_t offset, const void* data, const std::size_t length)
{
    ERHE_VERIFY(m_device != nullptr);
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

    Device_impl& device_impl = m_device->get_impl();
    VmaAllocator allocator   = device_impl.get_allocator();

    const VkBufferCreateInfo staging_buffer_create_info{
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext       = nullptr,
        .flags       = 0,
        .size        = length,
        .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VmaAllocationCreateInfo staging_alloc_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
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

    const VkBufferCopy copy_region{
        .srcOffset = 0,
        .dstOffset = offset,
        .size      = length
    };
    vkCmdCopyBuffer(m_vk_command_buffer, staging_buffer, impl.get_vk_buffer(), 1, &copy_region);

    // Chain the transfer write to the precise consumer stages this buffer
    // was created for, so downstream readers don't need their own
    // pre-barrier.
    const Vk_stage_access_2     dst_scope = buffer_usage_to_vk_stage_access(impl.get_usage());
    const VkPipelineStageFlags2 dst_stage_mask =
        (dst_scope.stage != 0) ? dst_scope.stage : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    const VkAccessFlags2 dst_access_mask =
        (dst_scope.access != 0)
            ? dst_scope.access
            : (VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
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

    // Defer staging buffer destruction until the GPU has consumed this
    // frame. add_completion_handler queues against the current
    // m_frame_index; the timeline semaphore signaled by end_frame is
    // what releases the handler.
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
    ERHE_VERIFY(m_device != nullptr);
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    if ((data == nullptr) || (width <= 0) || (height <= 0)) {
        return;
    }

    const std::size_t pixel_size = erhe::dataformat::get_format_size_bytes(pixelformat);
    const std::size_t src_stride = (row_stride > 0)
        ? static_cast<std::size_t>(row_stride)
        : (static_cast<std::size_t>(width) * pixel_size);
    const std::size_t data_size  = src_stride * static_cast<std::size_t>(height);

    Device_impl& device_impl = m_device->get_impl();
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
            .mipLevel       = static_cast<uint32_t>(level),
            .baseArrayLayer = 0,
            .layerCount     = 1
        },
        .imageOffset = {x, y, 0},
        .imageExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            1
        }
    };

    tex_impl.transition_layout(m_vk_command_buffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(m_vk_command_buffer, staging_buffer, destination_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    tex_impl.transition_layout(m_vk_command_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    device_impl.add_completion_handler(
        [allocator, staging_buffer, staging_allocation](Device_impl&) {
            vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);
        }
    );
}

void Command_buffer_impl::clear_texture(const Texture& texture, std::array<double, 4> value)
{
    ERHE_VERIFY(m_device != nullptr);
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

    tex_impl.transition_layout(m_vk_command_buffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (is_depth || is_stencil) {
        const VkClearDepthStencilValue clear_depth_stencil{
            .depth   = static_cast<float>(value[0]),
            .stencil = static_cast<uint32_t>(value[1])
        };
        VkImageAspectFlags aspect_mask = 0;
        if (is_depth)   aspect_mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (is_stencil) aspect_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        const VkImageSubresourceRange range{aspect_mask, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
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
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
        vkCmdClearColorImage(m_vk_command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range);
    }
    tex_impl.transition_layout(m_vk_command_buffer, final_layout);
}

void Command_buffer_impl::transition_texture_layout(const Texture& texture, Image_layout new_layout)
{
    ERHE_VERIFY(m_device != nullptr);
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

void Command_buffer_impl::cmd_texture_barrier(std::uint64_t usage_before, std::uint64_t usage_after)
{
    ERHE_VERIFY(m_vk_command_buffer != VK_NULL_HANDLE);

    // Strip the user_synchronized modifier -- it only controls whether
    // the automatic end-of-renderpass barrier fires; the actual
    // stage/access mapping should ignore it.
    const std::uint64_t src_usage = usage_before & ~Image_usage_flag_bit_mask::user_synchronized;
    const std::uint64_t dst_usage = usage_after  & ~Image_usage_flag_bit_mask::user_synchronized;

    const Vk_stage_access src_sa = usage_to_vk_stage_access(src_usage, false);
    const Vk_stage_access dst_sa = usage_to_vk_stage_access(dst_usage, false);

    if ((src_sa.stage == 0) || (dst_sa.stage == 0)) {
        return;
    }

    const VkMemoryBarrier2 memory_barrier{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext         = nullptr,
        .srcStageMask  = static_cast<VkPipelineStageFlags2>(src_sa.stage),
        .srcAccessMask = static_cast<VkAccessFlags2>(src_sa.access),
        .dstStageMask  = static_cast<VkPipelineStageFlags2>(dst_sa.stage),
        .dstAccessMask = static_cast<VkAccessFlags2>(dst_sa.access),
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

    for (Command_buffer* other : m_wait_for_semaphore_list) {
        ERHE_VERIFY(other != nullptr);
        const VkSemaphore semaphore = other->get_impl().get_implicit_semaphore();
        ERHE_VERIFY(semaphore != VK_NULL_HANDLE);
        submit_info.wait_semaphores.push_back(VkSemaphoreSubmitInfo{
            .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext       = nullptr,
            .semaphore   = semaphore,
            .value       = 0,
            .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0,
        });
    }
    m_wait_for_semaphore_list.clear();

    for (Command_buffer* other : m_signal_semaphore_list) {
        ERHE_VERIFY(other != nullptr);
        const VkSemaphore semaphore = other->get_impl().get_implicit_semaphore();
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
    m_signal_semaphore_list.clear();

    if (!m_signal_fence_list.empty()) {
        // VkSubmitInfo2 carries a single VkFence. Multiple cbs in the
        // same submit cannot each pin a different one.
        ERHE_VERIFY(m_signal_fence_list.size() == 1);
        ERHE_VERIFY(submit_info.fence == VK_NULL_HANDLE);
        const VkFence fence = m_signal_fence_list.front()->get_impl().get_implicit_fence();
        ERHE_VERIFY(fence != VK_NULL_HANDLE);
        submit_info.fence = fence;
        m_signal_fence_list.clear();
    }

    if (m_swapchain_used != nullptr) {
        // Wait on the per-slot acquire-semaphore so the cb only writes
        // the swapchain image after vkAcquireNextImageKHR has returned
        // and the previous frame's present is finished. Signal the
        // per-slot present-semaphore so vkQueuePresentKHR (called via
        // Swapchain_impl::present from submit_command_buffers right
        // after the submit succeeds) can wait on cb completion.
        const VkSemaphore acquire_semaphore = m_swapchain_used->get_active_acquire_semaphore();
        const VkSemaphore present_semaphore = m_swapchain_used->get_active_present_semaphore();
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
    ERHE_VERIFY(m_device != nullptr);
    Surface* surface = m_device->get_surface();
    if (surface != nullptr) {
        Swapchain* swapchain = surface->get_swapchain();
        if (swapchain != nullptr) {
            const bool ok = swapchain->get_impl().wait_frame(out_frame_state);
            log_swapchain->trace(
                "Command_buffer_impl::wait_for_swapchain() ok={}",
                ok
            );
            if (ok) {
                return true;
            }
        }
    }
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = false;
    log_swapchain->warn("Command_buffer_impl::wait_for_swapchain() returning false (no surface or swapchain wait_frame failed)");
    return false;
}

auto Command_buffer_impl::begin_swapchain(const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool
{
    ERHE_VERIFY(m_device != nullptr);
    Surface* surface = m_device->get_surface();
    if (surface == nullptr) {
        out_frame_state.should_render = false;
        log_swapchain->warn("Command_buffer_impl::begin_swapchain() no surface");
        return false;
    }
    Swapchain* swapchain = surface->get_swapchain();
    if (swapchain == nullptr) {
        out_frame_state.should_render = false;
        log_swapchain->warn("Command_buffer_impl::begin_swapchain() no swapchain");
        return false;
    }

    const bool ok = swapchain->get_impl().begin_frame(frame_begin_info);
    out_frame_state.should_render = ok;
    log_swapchain->trace(
        "Command_buffer_impl::begin_swapchain() ok={} acquire_sem=0x{:x} present_sem=0x{:x}",
        ok,
        reinterpret_cast<std::uintptr_t>(swapchain->get_impl().get_active_acquire_semaphore()),
        reinterpret_cast<std::uintptr_t>(swapchain->get_impl().get_active_present_semaphore())
    );
    if (ok) {
        // Remember that this cb feeds the swapchain so
        // collect_synchronization later splices the per-slot
        // acquire (wait) and present (signal) semaphores into the
        // submit. Cleared after collect_synchronization runs.
        m_swapchain_used = &swapchain->get_impl();
        // Friend access: tell the still-existing legacy end_frame submit
        // branch that this frame engaged the swapchain, so it picks the
        // Swapchain_impl::end_frame path (acquire-wait + present-signal +
        // vkQueuePresentKHR). Goes away with the legacy single-cb path.
        m_device->get_impl().m_had_swapchain_frame = true;
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
