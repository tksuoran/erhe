#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_utility/align.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_PROFILE_LIBRARY_TRACY)
#   include <tracy/TracyC.h>
#endif

#include <fmt/format.h>

#include <sstream>
#include <vector>

namespace erhe::graphics {

Buffer_impl::Buffer_impl(Device& device, const Buffer_create_info& create_info) noexcept
    : m_device{device}
{
    const VkBufferCreateInfo buffer_create_info{
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .size                  = create_info.capacity_byte_count,
        .usage                 = to_vulkan_buffer_usage(create_info.usage),
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr
    };

    Device_impl&  device_impl = device.get_impl();
    VmaAllocator& allocator   = device.get_impl().get_allocator();
    VkResult      result      = VK_SUCCESS;

    const VmaAllocationCreateInfo allocation_create_info{
        .flags          = to_vulkan_memory_allocation_create_flags(create_info.memory_allocation_create_flag_bit_mask),
        .usage          = VMA_MEMORY_USAGE_UNKNOWN,
        .requiredFlags  = to_vulkan_memory_property_flags(create_info.required_memory_property_bit_mask),
        .preferredFlags = to_vulkan_memory_property_flags(create_info.preferred_memory_property_bit_mask),
        .memoryTypeBits = 0xffff'ffffu,
        .pool           = VK_NULL_HANDLE,
        .pUserData      = nullptr,
        .priority       = 0.0f
    };

	result = vmaCreateBuffer(allocator, &buffer_create_info, &allocation_create_info, &m_vk_buffer, &m_vma_allocation, nullptr);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vmaCreateBuffer() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    m_capacity_byte_count = create_info.capacity_byte_count;
    m_next_free_byte      = 0;

    if (!create_info.debug_label.empty()) {
        m_debug_label = create_info.debug_label;
        device_impl.set_debug_label(
            VK_OBJECT_TYPE_BUFFER,
            reinterpret_cast<uint64_t>(m_vk_buffer),
            create_info.debug_label.c_str()
        );
    }

    if (create_info.init_data != nullptr) {
        VmaAllocationInfo allocation_info{};
        vmaGetAllocationInfo(allocator, m_vma_allocation, &allocation_info);
        const VkMemoryType& memory_type = device_impl.get_memory_type(allocation_info.memoryType);
        const bool host_visible  = memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        const bool host_coherent = memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (host_visible == false) {
            // TODO Use staging buffer to upload data to not-host visible memory
            ERHE_FATAL(
                "TODO Staging buffer upload is not yet implemented. "
                "Buffer initial data upload requested but buffer memory is not host visible."
            );
            abort();
        }

        void* mapped_data = nullptr;
        result = vmaMapMemory(allocator, m_vma_allocation, &mapped_data);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vmaMapMemory() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }
        std::memcpy(mapped_data, create_info.init_data, create_info.capacity_byte_count);
        if (!host_coherent) {
            result = vmaFlushAllocation(allocator, m_vma_allocation, 0, create_info.capacity_byte_count);
            // const VkMappedMemoryRange memory_range{
            //     .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            //     .pNext  = nullptr,
            //     .memory = allocation_info.deviceMemory,
            //     .offset = allocation_info.offset,
            //     .size   = allocation_info.size
            // };
            // result = vkFlushMappedMemoryRanges(vulkan_device, 1, &memory_range);
            if (result != VK_SUCCESS) {
                log_swapchain->critical("vkFlushMappedMemoryRanges() failed with {} {}", static_cast<int32_t>(result), c_str(result));
                abort();
            }
        }

        vmaUnmapMemory(allocator, m_vma_allocation);
    }
}

Buffer_impl::Buffer_impl(Device& device)
    : m_device{device}
{
}

Buffer_impl::~Buffer_impl() noexcept
{
    // TODO
}

Buffer_impl::Buffer_impl(Buffer_impl&& old) noexcept
    : m_device             {old.m_device}
    , m_vma_allocation     {std::exchange(old.m_vma_allocation, VK_NULL_HANDLE)}
    , m_vk_buffer          {std::exchange(old.m_vk_buffer,      VK_NULL_HANDLE)}
    , m_debug_label        {std::move(old.m_debug_label)}
    , m_capacity_byte_count{std::exchange(old.m_capacity_byte_count, 0)}
    , m_next_free_byte     {std::exchange(old.m_next_free_byte,      0)}
{
}

auto Buffer_impl::operator=(Buffer_impl&& old) noexcept -> Buffer_impl&
{
    m_vma_allocation      = std::exchange(old.m_vma_allocation, VK_NULL_HANDLE);
    m_vk_buffer           = std::exchange(old.m_vk_buffer,      VK_NULL_HANDLE);
    m_debug_label         = std::move(old.m_debug_label);
    m_capacity_byte_count = std::exchange(old.m_capacity_byte_count, 0);
    m_next_free_byte      = std::exchange(old.m_next_free_byte,      0);
    return *this;
}

auto Buffer_impl::get_map() const -> std::span<std::byte>
{
    return {};
}

auto Buffer_impl::get_debug_label() const noexcept -> const std::string&
{
    return m_debug_label;
}

void Buffer_impl::clear() noexcept
{
    m_next_free_byte = 0;
}

auto Buffer_impl::allocate_bytes(const std::size_t byte_count, const std::size_t alignment) noexcept -> std::optional<std::size_t>
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_allocate_mutex};
    const std::size_t offset         = erhe::utility::align_offset_non_power_of_two(m_next_free_byte, alignment);
    const std::size_t next_free_byte = offset + byte_count;
    if (next_free_byte > m_capacity_byte_count) {
        const std::size_t available_byte_count = (offset < m_capacity_byte_count) ? m_capacity_byte_count - offset : 0;
        log_buffer->error(
            "erhe::graphics::Buffer_impl::allocate_bytes(): Out of memory requesting {} bytes, currently allocated {}, total size {}, free {} bytes",
            byte_count,
            m_next_free_byte,
            m_capacity_byte_count,
            available_byte_count
        );
        return {};
    }

    m_next_free_byte = next_free_byte;
    ERHE_VERIFY(m_next_free_byte <= m_capacity_byte_count);
    log_buffer->trace("buffer allocated {} bytes at offset {}", byte_count, offset);
    return offset;
}

auto Buffer_impl::begin_write(const std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>
{
    static_cast<void>(byte_offset);
    static_cast<void>(byte_count);
    return {};
}

void Buffer_impl::end_write(const std::size_t byte_offset, const std::size_t byte_count) noexcept
{
    static_cast<void>(byte_offset);
    static_cast<void>(byte_count);
}

auto Buffer_impl::map_all_bytes(Buffer_map_flags flags) noexcept -> std::span<std::byte>
{
    static_cast<void>(flags);
    return {};
}

auto Buffer_impl::map_bytes(const std::size_t byte_offset, const std::size_t byte_count, Buffer_map_flags flags) noexcept -> std::span<std::byte>
{
    static_cast<void>(byte_offset);
    static_cast<void>(byte_count);
    static_cast<void>(flags);
    return {};
}

void Buffer_impl::unmap() noexcept
{
}

void Buffer_impl::flush_bytes(const std::size_t byte_offset, const std::size_t byte_count) noexcept
{
    static_cast<void>(byte_offset);
    static_cast<void>(byte_count);
}

void Buffer_impl::dump() const noexcept
{
}

void Buffer_impl::flush_and_unmap_bytes(const std::size_t byte_count) noexcept
{
    static_cast<void>(byte_count);
}

auto Buffer_impl::get_used_byte_count() const -> std::size_t
{
    return 0;
}

auto Buffer_impl::get_available_byte_count(std::size_t alignment) const noexcept -> std::size_t
{
    static_cast<void>(alignment);
    return 0;
}

auto Buffer_impl::get_capacity_byte_count() const noexcept -> std::size_t
{
    return 0;
}

auto operator==(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool
{
    return
        (lhs.m_vma_allocation == rhs.m_vma_allocation) &&
        (lhs.m_vk_buffer      == rhs.m_vk_buffer);
}

auto operator!=(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
