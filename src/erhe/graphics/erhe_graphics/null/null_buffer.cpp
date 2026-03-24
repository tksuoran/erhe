#include "erhe_graphics/null/null_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <utility>

namespace erhe::graphics {

Buffer_impl::Buffer_impl(Device& device)
    : m_device{device}
{
}

Buffer_impl::Buffer_impl(Device& device, const Buffer_create_info& create_info) noexcept
    : m_device             {device}
    , m_storage            (create_info.capacity_byte_count)
    , m_capacity_byte_count{create_info.capacity_byte_count}
    , m_usage              {create_info.usage}
    , m_debug_label        {create_info.debug_label}
{
    if (create_info.init_data != nullptr) {
        const std::byte* source = static_cast<const std::byte*>(create_info.init_data);
        std::copy(source, source + m_capacity_byte_count, m_storage.begin());
    }
}

Buffer_impl::~Buffer_impl() noexcept = default;

Buffer_impl::Buffer_impl(Buffer_impl&& old) noexcept
    : m_device             {old.m_device}
    , m_storage            {std::move(old.m_storage)}
    , m_map                {}
    , m_capacity_byte_count{old.m_capacity_byte_count}
    , m_usage              {old.m_usage}
    , m_debug_label        {old.m_debug_label}
{
}

auto Buffer_impl::operator=(Buffer_impl&& old) noexcept -> Buffer_impl&
{
    ERHE_VERIFY(&m_device == &old.m_device);
    m_storage            = std::move(old.m_storage);
    m_map                = std::span<std::byte>{};
    m_capacity_byte_count = old.m_capacity_byte_count;
    m_usage              = old.m_usage;
    m_debug_label        = old.m_debug_label;
    return *this;
}

auto Buffer_impl::get_capacity_byte_count() const noexcept -> std::size_t
{
    return m_capacity_byte_count;
}

auto Buffer_impl::get_debug_label() const noexcept -> erhe::utility::Debug_label
{
    return m_debug_label;
}

auto Buffer_impl::get_map() const -> std::span<std::byte>
{
    return m_map;
}

auto Buffer_impl::gl_name() const noexcept -> unsigned int
{
    return 0;
}

void Buffer_impl::unmap() noexcept
{
    m_map = std::span<std::byte>{};
}

void Buffer_impl::invalidate(std::size_t byte_offset, std::size_t byte_count) noexcept
{
    static_cast<void>(byte_offset);
    static_cast<void>(byte_count);
}

void Buffer_impl::flush_bytes(std::size_t byte_offset, std::size_t byte_count) noexcept
{
    static_cast<void>(byte_offset);
    static_cast<void>(byte_count);
}

void Buffer_impl::flush_and_unmap_bytes(std::size_t byte_count) noexcept
{
    static_cast<void>(byte_count);
    m_map = std::span<std::byte>{};
}

void Buffer_impl::dump() const noexcept
{
}

auto Buffer_impl::begin_write(std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>
{
    if (byte_count == 0) {
        byte_count = m_capacity_byte_count - byte_offset;
    } else {
        byte_count = std::min(byte_count, m_capacity_byte_count - byte_offset);
    }
    m_map = std::span<std::byte>(m_storage.data() + byte_offset, byte_count);
    return m_map;
}

void Buffer_impl::end_write(std::size_t byte_offset, std::size_t byte_count) noexcept
{
    static_cast<void>(byte_offset);
    static_cast<void>(byte_count);
    m_map = std::span<std::byte>{};
}

auto Buffer_impl::map_all_bytes() noexcept -> std::span<std::byte>
{
    m_map = std::span<std::byte>(m_storage.data(), m_capacity_byte_count);
    return m_map;
}

auto Buffer_impl::map_bytes(std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>
{
    ERHE_VERIFY(byte_offset + byte_count <= m_capacity_byte_count);
    m_map = std::span<std::byte>(m_storage.data() + byte_offset, byte_count);
    return m_map;
}

auto operator==(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool
{
    return &lhs == &rhs;
}

auto operator!=(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
