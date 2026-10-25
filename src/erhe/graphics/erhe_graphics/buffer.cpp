#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_buffer.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL4)
# include "erhe_graphics/metal4/metal4_buffer.hpp"
#elif defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_buffer.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_buffer.hpp"
#endif

#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <mutex>
#include <utility>

namespace erhe::graphics {

namespace {

// Process-wide buffer memory accounting; buffers are created and destroyed
// from worker threads as well as the main thread.
std::mutex               s_buffer_statistics_mutex;
Buffer_memory_statistics s_buffer_statistics{};

[[nodiscard]] auto get_category_bytes(Buffer_memory_statistics& statistics, const Buffer_usage usage) -> std::size_t&
{
    using erhe::utility::test_bit_set;
    if (test_bit_set(usage, Buffer_usage::acceleration_structure_storage)) {
        return statistics.acceleration_structure_bytes;
    }
    if (test_bit_set(usage, Buffer_usage::acceleration_structure_build_input)) {
        return statistics.acceleration_structure_build_input_bytes;
    }
    if (test_bit_set(usage, Buffer_usage::vertex) || test_bit_set(usage, Buffer_usage::index)) {
        return statistics.vertex_index_bytes;
    }
    return statistics.other_bytes;
}

void count_buffer(const std::size_t byte_count, const Buffer_usage usage)
{
    const std::lock_guard<std::mutex> lock{s_buffer_statistics_mutex};
    s_buffer_statistics.buffer_count += 1;
    s_buffer_statistics.byte_count   += byte_count;
    get_category_bytes(s_buffer_statistics, usage) += byte_count;
}

void uncount_buffer(const std::size_t byte_count, const Buffer_usage usage)
{
    const std::lock_guard<std::mutex> lock{s_buffer_statistics_mutex};
    s_buffer_statistics.buffer_count -= 1;
    s_buffer_statistics.byte_count   -= byte_count;
    get_category_bytes(s_buffer_statistics, usage) -= byte_count;
}

}

auto Buffer::get_memory_statistics() -> Buffer_memory_statistics
{
    const std::lock_guard<std::mutex> lock{s_buffer_statistics_mutex};
    return s_buffer_statistics;
}

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
    count_buffer(m_counted_byte_count, m_counted_usage);
}
Buffer::~Buffer() noexcept
{
    if (m_impl) {
        uncount_buffer(m_counted_byte_count, m_counted_usage);
    }
}
Buffer::Buffer(Device& device, const Buffer_create_info& create_info) noexcept
    : m_impl              {std::make_unique<Buffer_impl>(device, create_info)}
    , m_counted_byte_count{create_info.capacity_byte_count}
    , m_counted_usage     {create_info.usage}
{
    count_buffer(m_counted_byte_count, m_counted_usage);
}
Buffer::Buffer(Buffer&& other) noexcept
    : m_impl              {std::move(other.m_impl)}
    , m_counted_byte_count{std::exchange(other.m_counted_byte_count, std::size_t{0})}
    , m_counted_usage     {std::exchange(other.m_counted_usage, Buffer_usage::none)}
{
}
auto Buffer::operator=(Buffer&& other) noexcept -> Buffer&
{
    if (this != &other) {
        if (m_impl) {
            uncount_buffer(m_counted_byte_count, m_counted_usage);
        }
        m_impl               = std::move(other.m_impl);
        m_counted_byte_count = std::exchange(other.m_counted_byte_count, std::size_t{0});
        m_counted_usage      = std::exchange(other.m_counted_usage, Buffer_usage::none);
    }
    return *this;
}
auto Buffer::get_debug_label() const noexcept -> erhe::utility::Debug_label
{
    return m_impl->get_debug_label();
}
auto Buffer::get_capacity_byte_count() const noexcept -> std::size_t
{
    return m_impl->get_capacity_byte_count();
}
auto Buffer::get_device_address() const noexcept -> uint64_t
{
    return m_impl->get_device_address();
}
auto Buffer::get_map() const -> std::span<std::byte>
{
    return m_impl->get_map();
}
void Buffer::unmap() noexcept
{
    m_impl->unmap();
}
void Buffer::invalidate(std::size_t byte_offset, std::size_t byte_count) noexcept
{
    m_impl->invalidate(byte_offset, byte_count);
}
void Buffer::flush_bytes(std::size_t byte_offset, std::size_t byte_count) noexcept
{
    m_impl->flush_bytes(byte_offset, byte_count);
}
void Buffer::flush_and_unmap_bytes(std::size_t byte_count) noexcept
{
    m_impl->flush_and_unmap_bytes(byte_count);
}
void Buffer::upload_sub_data(std::size_t byte_offset, std::size_t byte_count, const void* data) noexcept
{
    m_impl->upload_sub_data(byte_offset, byte_count, data);
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
auto Buffer::map_all_bytes() noexcept -> std::span<std::byte>
{
    return m_impl->map_all_bytes();
}
auto Buffer::map_bytes(const std::size_t byte_offset, const std::size_t byte_count) noexcept -> std::span<std::byte>
{
    return m_impl->map_bytes(byte_offset, byte_count);
}
auto Buffer::get_impl() -> Buffer_impl&
{
    return *m_impl.get();
}
auto Buffer::get_impl() const -> const Buffer_impl&
{
    return *m_impl.get();
}

} // namespace erhe::graphics
