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
    : m_device                                {device}
    , m_capacity_byte_count                   {create_info.capacity_byte_count}
    , m_usage                                 {create_info.usage}
    , m_memory_allocation_create_flag_bit_mask{create_info.memory_allocation_create_flag_bit_mask}
    , m_required_memory_property_bit_mask     {create_info.required_memory_property_bit_mask}
    , m_preferred_memory_property_bit_mask    {create_info.preferred_memory_property_bit_mask}
    , m_debug_label                           {create_info.debug_label}
{
    constexpr const std::size_t sanity_threshold{2'000'000'000};
    ERHE_VERIFY(m_capacity_byte_count < sanity_threshold); // sanity check, can raise limit when needed
    log_buffer->debug(
        "Buffer_impl::Buffer_impl() capacity_byte_count = {}, flags = {}, usage = {}, required = {}, preferred = {}) debug_label = {}",
        m_capacity_byte_count,
        to_string_memory_allocation_create_flag_bit_mask(m_memory_allocation_create_flag_bit_mask),
        to_string(m_usage),
        to_string_memory_property_flag_bit_mask(create_info.required_memory_property_bit_mask),
        to_string_memory_property_flag_bit_mask(create_info.preferred_memory_property_bit_mask),
        m_debug_label
    );

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
    ERHE_VERIFY(m_vk_buffer != VK_NULL_HANDLE);
    ERHE_VERIFY(m_vma_allocation != VK_NULL_HANDLE);

    if (!create_info.debug_label.empty()) {
        m_debug_label = create_info.debug_label;
        device_impl.set_debug_label(
            VK_OBJECT_TYPE_BUFFER,
            reinterpret_cast<uint64_t>(m_vk_buffer),
            create_info.debug_label.c_str()
        );
    }

    const uint64_t requested_memory_property_flags =
        create_info.required_memory_property_bit_mask |
        create_info.preferred_memory_property_bit_mask;

    VmaAllocationInfo allocation_info{};
    vmaGetAllocationInfo(allocator, m_vma_allocation, &allocation_info);
    ERHE_VERIFY(allocation_info.size >= create_info.capacity_byte_count);
    m_vk_memory_type = device_impl.get_memory_type(allocation_info.memoryType);
    const bool host_visible  = m_vk_memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    const bool host_coherent = m_vk_memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    using namespace erhe::utility;
    m_persistently_mapped = host_visible && test_bit_set(
        requested_memory_property_flags,
        Memory_property_flag_bit_mask::host_persistent
    );

    if (m_persistently_mapped) {
        map_all_bytes();
    }

    if (create_info.init_data != nullptr) {
        if (host_visible == false) {
            // TODO Use staging buffer to upload data to not-host visible memory
            ERHE_FATAL(
                "TODO Staging buffer upload is not yet implemented. "
                "Buffer initial data upload requested but buffer memory is not host visible."
            );
            abort();
        }

        if (!m_persistently_mapped) {
            map_all_bytes();
        }
        std::memcpy(m_map.data(), create_info.init_data, create_info.capacity_byte_count);
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

        if (!m_persistently_mapped) {
            unmap();
        }
    }
}

Buffer_impl::Buffer_impl(Device& device)
    : m_device{device}
{
}

Buffer_impl::~Buffer_impl() noexcept
{
    Device_impl& device_impl = m_device.get_impl();

    const bool          persistently_mapped = m_persistently_mapped;
    const VmaAllocation vma_allocation      = m_vma_allocation;
    const VkBuffer      vk_buffer           = m_vk_buffer;

    device_impl.add_completion_handler(
        [&device_impl, persistently_mapped, vma_allocation, vk_buffer]() {
            VmaAllocator& allocator = device_impl.get_allocator();

            if (persistently_mapped) {
                vmaUnmapMemory(allocator, vma_allocation);
            }
            vmaDestroyBuffer(allocator, vk_buffer, vma_allocation);
        }
    );
}

Buffer_impl::Buffer_impl(Buffer_impl&& old) noexcept
    : m_device             {old.m_device}
    , m_vma_allocation     {std::exchange(old.m_vma_allocation, VK_NULL_HANDLE)}
    , m_vk_buffer          {std::exchange(old.m_vk_buffer,      VK_NULL_HANDLE)}
    , m_debug_label        {std::move(old.m_debug_label)}
    , m_map                {std::exchange(old.m_map, {})}
    , m_map_byte_offset    {std::exchange(old.m_map_byte_offset, 0)}
    , m_capacity_byte_count{std::exchange(old.m_capacity_byte_count, 0)}
{
}

auto Buffer_impl::operator=(Buffer_impl&& old) noexcept -> Buffer_impl&
{
    m_vma_allocation      = std::exchange(old.m_vma_allocation, VK_NULL_HANDLE);
    m_vk_buffer           = std::exchange(old.m_vk_buffer,      VK_NULL_HANDLE);
    m_debug_label         = std::move(old.m_debug_label);
    m_map                 = std::exchange(old.m_map, {});
    m_map_byte_offset     = std::exchange(old.m_map_byte_offset, 0);
    m_capacity_byte_count = std::exchange(old.m_capacity_byte_count, 0);
    return *this;
}

auto Buffer_impl::get_map() const -> std::span<std::byte>
{
    return m_map;
}

auto Buffer_impl::get_debug_label() const noexcept -> const std::string&
{
    return m_debug_label;
}

auto Buffer_impl::begin_write(const std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>
{
    ERHE_VERIFY(m_vk_buffer != VK_NULL_HANDLE);
    ERHE_VERIFY(m_vma_allocation != VK_NULL_HANDLE);

    if (!m_persistently_mapped) {
        ERHE_VERIFY(m_map.empty());
        map_all_bytes();

        if (byte_count == 0) {
            byte_count = m_capacity_byte_count - byte_offset;
        } else {
            byte_count = std::min(byte_count, m_capacity_byte_count - byte_offset);
        }

    }

    m_map = m_map_all.subspan(byte_offset, byte_count);
    m_map_byte_offset = byte_offset;
    ERHE_VERIFY(!m_map.empty());
    return m_map;
}

void Buffer_impl::end_write(const std::size_t byte_offset, const std::size_t byte_count) noexcept
{
    ERHE_VERIFY(!m_map.empty());
    ERHE_VERIFY(m_vk_buffer != VK_NULL_HANDLE);
    ERHE_VERIFY(m_vma_allocation != VK_NULL_HANDLE);

    if (byte_count > 0) {
        const bool host_coherent = m_vk_memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (!host_coherent) {
            flush_bytes(byte_offset, byte_count);
        }
    }
    if (!m_persistently_mapped) {
        unmap();
    }
}

auto Buffer_impl::map_all_bytes() noexcept -> std::span<std::byte>
{
    ERHE_VERIFY(m_vk_buffer != VK_NULL_HANDLE);
    ERHE_VERIFY(m_vma_allocation != VK_NULL_HANDLE);

    VmaAllocator& allocator = m_device.get_impl().get_allocator();
    VkResult      result    = VK_SUCCESS;

    void* map_pointer = nullptr;
    result = vmaMapMemory(allocator, m_vma_allocation, &map_pointer);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vmaMapMemory() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    m_map_all = std::span<std::byte>{
        static_cast<std::byte*>(map_pointer),
        m_capacity_byte_count
    };
    m_map = m_map_all;
    m_map_byte_offset = 0;
    return m_map;
}

auto Buffer_impl::map_bytes(const std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>
{
    ERHE_VERIFY(m_vk_buffer != VK_NULL_HANDLE);
    ERHE_VERIFY(m_vma_allocation != VK_NULL_HANDLE);

    if (!m_persistently_mapped) {
        ERHE_VERIFY(m_map.empty());
        map_all_bytes();

        if (byte_count == 0) {
            byte_count = m_capacity_byte_count - byte_offset;
        } else {
            byte_count = std::min(byte_count, m_capacity_byte_count - byte_offset);
        }
    }

    m_map = m_map_all.subspan(byte_offset, byte_count);
    m_map_byte_offset = byte_offset;
    ERHE_VERIFY(!m_map.empty());
    return m_map;
}

void Buffer_impl::unmap() noexcept
{
    ERHE_VERIFY(m_vk_buffer != VK_NULL_HANDLE);
    ERHE_VERIFY(m_vma_allocation != VK_NULL_HANDLE);

    ERHE_VERIFY(m_map.data() != nullptr);
    VmaAllocator& allocator = m_device.get_impl().get_allocator();
    vmaUnmapMemory(allocator, m_vma_allocation);
    m_map = {};
    m_map_all = {};
    m_map_byte_offset = 0;

}

void Buffer_impl::invalidate(const std::size_t byte_offset, const std::size_t byte_count) noexcept
{
    ERHE_VERIFY(m_vk_buffer != VK_NULL_HANDLE);
    ERHE_VERIFY(m_vma_allocation != VK_NULL_HANDLE);
    ERHE_VERIFY(m_map.data() != nullptr);

    VkDevice      vulkan_device = m_device.get_impl().get_vulkan_device();
    VmaAllocator& allocator     = m_device.get_impl().get_allocator();
    VkResult      result        = VK_SUCCESS;

    VmaAllocationInfo allocation_info{};
    vmaGetAllocationInfo(allocator, m_vma_allocation, &allocation_info);

    const VkMappedMemoryRange memory_range{
        .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext  = nullptr,
        .memory = allocation_info.deviceMemory,
        .offset = allocation_info.offset + byte_offset,
        .size   = byte_count
    };
    result = vkInvalidateMappedMemoryRanges(vulkan_device, 1, &memory_range);
    if (result != VK_SUCCESS) {
        log_context->critical("vkInvalidateMappedMemoryRanges() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
}

void Buffer_impl::flush_bytes(const std::size_t byte_offset, const std::size_t byte_count) noexcept
{
    ERHE_VERIFY(m_vk_buffer != VK_NULL_HANDLE);
    ERHE_VERIFY(m_vma_allocation != VK_NULL_HANDLE);
    ERHE_VERIFY(m_map.data() != nullptr);

    const bool host_coherent = m_vk_memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (host_coherent) {
        return;
    }

    VkDevice      vulkan_device = m_device.get_impl().get_vulkan_device();
    VmaAllocator& allocator     = m_device.get_impl().get_allocator();
    VkResult      result        = VK_SUCCESS;

    VmaAllocationInfo allocation_info{};
    vmaGetAllocationInfo(allocator, m_vma_allocation, &allocation_info);

    const VkMappedMemoryRange memory_range{
        .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext  = nullptr,
        .memory = allocation_info.deviceMemory,
        .offset = allocation_info.offset + byte_offset,
        .size   = byte_count
    };
    result = vkFlushMappedMemoryRanges(vulkan_device, 1, &memory_range);
    if (result != VK_SUCCESS) {
        log_context->critical("vkFlushMappedMemoryRanges() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
}

void Buffer_impl::dump() const noexcept
{
    ERHE_FATAL("not implemented");
}

void Buffer_impl::flush_and_unmap_bytes(const std::size_t byte_count) noexcept
{
    flush_bytes(0, byte_count);
    unmap();
}

auto Buffer_impl::get_capacity_byte_count() const noexcept -> std::size_t
{
    return m_capacity_byte_count;
}

auto Buffer_impl::get_vma_allocation() const -> VmaAllocation
{
    return m_vma_allocation;
}

auto Buffer_impl::get_vk_buffer() const -> VkBuffer
{
    return m_vk_buffer;
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
