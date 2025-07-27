#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/gl/gl_buffer.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

auto get_buffer_usage(Buffer_target target) -> Buffer_usage
{
    switch (target) {
        case Buffer_target::index        : return Buffer_usage::index;
        case Buffer_target::vertex       : return Buffer_usage::vertex;
        case Buffer_target::uniform      : return Buffer_usage::uniform;
        case Buffer_target::storage      : return Buffer_usage::storage;
        case Buffer_target::draw_indirect: return Buffer_usage::indirect;
        case Buffer_target::texture      : return Buffer_usage::texture;
        case Buffer_target::pixel        : return Buffer_usage::pixel;
        case Buffer_target::transfer     : return Buffer_usage::transfer;
        default: ERHE_FATAL("get_buffer_usage(): Bad Buffer_target %u", static_cast<unsigned int>(target)); return Buffer_usage::none;
    }
}

Buffer::Buffer(Device& device)
    : m_impl{std::make_unique<Buffer_impl>(device)}
{
}
Buffer::~Buffer() noexcept
{
}
Buffer::Buffer(Device& device, const Buffer_create_info& create_info) noexcept
    : m_impl{std::make_unique<Buffer_impl>(device, create_info)}
{
}
Buffer::Buffer(Buffer&& other) noexcept
    : m_impl{std::move(other.m_impl)}
{
}
auto Buffer::operator=(Buffer&& other) noexcept -> Buffer&
{
    m_impl = std::move(other.m_impl);
    return *this;
}
auto Buffer::get_capacity_byte_count() const noexcept -> std::size_t
{
    return m_impl->get_capacity_byte_count();
}
auto Buffer::allocate_bytes(std::size_t byte_count, std::size_t alignment) noexcept -> std::optional<std::size_t>
{
    return m_impl->allocate_bytes(byte_count, alignment);
}
auto Buffer::get_debug_label() const noexcept -> const std::string&
{
    return m_impl->get_debug_label();
}
auto Buffer::get_used_byte_count() const -> std::size_t
{
    return m_impl->get_used_byte_count();
}
auto Buffer::get_available_byte_count(std::size_t alignment) const noexcept -> std::size_t
{
    return m_impl->get_available_byte_count(alignment);
}
auto Buffer::get_map() const -> std::span<std::byte>
{
    return m_impl->get_map();
}
void Buffer::clear() noexcept
{
    m_impl->clear();
}
void Buffer::unmap() noexcept
{
    m_impl->unmap();
}
void Buffer::flush_bytes(std::size_t byte_offset, std::size_t byte_count) noexcept
{
    m_impl->flush_bytes(byte_offset, byte_count);
}
void Buffer::flush_and_unmap_bytes(std::size_t byte_count) noexcept
{
    m_impl->flush_and_unmap_bytes(byte_count);
}
void Buffer::dump() const noexcept
{
    m_impl->dump();
}
auto Buffer::begin_write(std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>
{
    return m_impl->begin_write(byte_offset, byte_count);
}
void Buffer::end_write(std::size_t byte_offset, std::size_t byte_count) noexcept
{
    m_impl->end_write(byte_offset, byte_count);
}
auto Buffer::map_all_bytes(Buffer_map_flags flags) noexcept -> std::span<std::byte>
{
    return m_impl->map_all_bytes(flags);
}
auto Buffer::map_bytes(const std::size_t byte_offset, const std::size_t byte_count, Buffer_map_flags flags) noexcept -> std::span<std::byte>
{
    return m_impl->map_bytes(byte_offset, byte_count, flags);
}
auto Buffer::get_impl() -> Buffer_impl&
{
    return *m_impl.get();
}
auto Buffer::get_impl() const -> const Buffer_impl&
{
    return *m_impl.get();
}


////

} // namespace erhe::graphics
