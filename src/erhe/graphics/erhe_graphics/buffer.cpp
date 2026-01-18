#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_buffer.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#endif

#include "erhe_utility/align.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

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
        case Buffer_target::uniform_texel: return Buffer_usage::uniform_texel;
        case Buffer_target::storage_texel: return Buffer_usage::storage_texel;
        case Buffer_target::transfer_src : return Buffer_usage::transfer_src;
        case Buffer_target::transfer_dst : return Buffer_usage::transfer_dst;
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
auto Buffer::get_debug_label() const noexcept -> const std::string&
{
    return m_impl->get_debug_label();
}
auto Buffer::get_capacity_byte_count() const noexcept -> std::size_t
{
    return m_impl->get_capacity_byte_count();
}
auto Buffer::get_map() const -> std::span<std::byte>
{
    return m_impl->get_map();
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

Buffer_allocator::Buffer_allocator(Buffer* buffer)
    : m_buffer{buffer}
{
}

Buffer_allocator::~Buffer_allocator() noexcept = default;

Buffer_allocator::Buffer_allocator(Buffer_allocator&& old) noexcept
    : m_buffer        {old.m_buffer}
    , m_next_free_byte{old.m_next_free_byte}
{
}

auto Buffer_allocator::operator=(Buffer_allocator&& old) noexcept -> Buffer_allocator&
{
    m_buffer         = old.m_buffer;
    m_next_free_byte = old.m_next_free_byte;
    return *this;
}

void Buffer_allocator::clear() noexcept
{
    m_next_free_byte = 0;
}

auto Buffer_allocator::allocate_bytes(const std::size_t byte_count, const std::size_t alignment) noexcept -> std::optional<std::size_t>
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_allocate_mutex};
    const std::size_t offset         = erhe::utility::align_offset_non_power_of_two(m_next_free_byte, alignment);
    const std::size_t next_free_byte = offset + byte_count;
    const std::size_t capacity_byte_count = m_buffer->get_capacity_byte_count();
    if (next_free_byte > capacity_byte_count) {
        const std::size_t available_byte_count = (offset < capacity_byte_count) ? capacity_byte_count - offset : 0;
        log_buffer->error(
            "erhe::graphics::Buffer_impl::allocate_bytes(): Out of memory requesting {} bytes, currently allocated {}, total size {}, free {} bytes",
            byte_count,
            m_next_free_byte,
            capacity_byte_count,
            available_byte_count
        );
        return {};
    }

    m_next_free_byte = next_free_byte;
    ERHE_VERIFY(m_next_free_byte <= capacity_byte_count);
    log_buffer->trace("buffer {}: allocated {} bytes at offset {}", m_buffer->get_debug_label(), byte_count, offset);
    return offset;
}

auto Buffer_allocator::get_used_byte_count() const -> std::size_t
{
    return m_next_free_byte;
}
auto Buffer_allocator::get_available_byte_count(std::size_t alignment) const noexcept -> std::size_t
{
    const std::size_t capacity_byte_count = m_buffer->get_capacity_byte_count();
    const std::size_t aligned_offset = erhe::utility::align_offset_non_power_of_two(m_next_free_byte, alignment);
    if (aligned_offset >= capacity_byte_count) {
        return 0;
    }
    return capacity_byte_count - aligned_offset;
}

auto Buffer_allocator::get_buffer() -> Buffer*
{
    return m_buffer;
}

auto Buffer_allocator::get_buffer() const -> const Buffer*
{
    return m_buffer;
}

} // namespace erhe::graphics
