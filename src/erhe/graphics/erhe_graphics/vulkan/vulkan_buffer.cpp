#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
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

void Buffer_impl::allocate_storage(const void* init_data)
{
    static_cast<void>(init_data);
}

Buffer_impl::Buffer_impl(Device& device, const Buffer_create_info& create_info) noexcept
    : m_device             {device}
    , m_capacity_byte_count{create_info.capacity_byte_count}
    , m_usage              {create_info.usage     }
    , m_direction          {create_info.direction }
    , m_cache_mode         {create_info.cache_mode}
    , m_mapping            {create_info.mapping   }
    , m_coherency          {create_info.coherency }
    , m_debug_label        {create_info.debug_label}
{
    constexpr const std::size_t sanity_threshold{2'000'000'000};
    ERHE_VERIFY(m_capacity_byte_count < sanity_threshold); // sanity check, can raise limit when needed
    log_buffer->debug(
        "Buffer_impl::Buffer_impl() capacity_byte_count = {}, usage = {}, direction = {}, cache_mode = {}, mapping = {}, coherency = {}) name = {} debug_label = {}",
        m_capacity_byte_count,
        to_string(create_info.usage),
        c_str(create_info.direction),
        c_str(create_info.cache_mode),
        c_str(create_info.mapping),
        c_str(create_info.coherency),
        gl_name(),
        m_debug_label
    );
}

Buffer_impl::Buffer_impl(Device& device)
    : m_device{device}
{
}

Buffer_impl::~Buffer_impl() noexcept
{
    if (m_allocated) {
        ERHE_PROFILE_MEM_FREE_NS(this, s_pool_name);
    }
}

Buffer_impl::Buffer_impl(Buffer_impl&& other) noexcept
{
}

auto Buffer_impl::operator=(Buffer_impl&& other) noexcept -> Buffer_impl&
{
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
}

auto Buffer_impl::allocate_bytes(const std::size_t byte_count, const std::size_t alignment) noexcept -> std::optional<std::size_t>
{
    static_cast<void>(byte_count);
    static_cast<void>(alignment);
    return 0;
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
    return false;
}

auto operator!=(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
