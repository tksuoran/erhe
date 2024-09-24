#include "erhe_renderer/buffer_writer.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_verify/verify.hpp"

#include <sstream>

namespace erhe::renderer {

Buffer_writer::Buffer_writer(erhe::graphics::Instance& graphic_instance, erhe::graphics::Buffer& buffer)
    : m_graphics_instance{graphic_instance}
    , m_buffer{buffer}
{
}

auto Buffer_writer::begin(gl::Buffer_target target, std::size_t byte_count, Buffer_writer_flags flags) -> std::span<std::byte>
{
    write_offset = m_graphics_instance.align_buffer_offset(target, write_offset);

    if (byte_count == 0) {
        byte_count = m_buffer.capacity_byte_count() - write_offset;
    } else {
        std::size_t available_capacity = m_buffer.capacity_byte_count() - write_offset;
        if (byte_count > available_capacity) {
            if (erhe::bit::test_all_rhs_bits_set(flags, Buffer_writer_flag_masks::allow_wrap)) {
                reset();
                available_capacity = m_buffer.capacity_byte_count() - write_offset;
                ERHE_VERIFY(available_capacity >= byte_count);
                ++wrap_count;
            } else if (erhe::bit::test_all_rhs_bits_set(flags, Buffer_writer_flag_masks::allow_clamp)) {
               byte_count = std::min(byte_count, m_buffer.capacity_byte_count() - write_offset);
            } else {
                ERHE_FATAL("Buffer_writer::begin() out of memory");
            }
        }
    }

    if (!m_graphics_instance.info.use_persistent_buffers) {
        // Only requested range will be temporarily mapped
        map_offset   = write_offset;
        write_offset = 0;
        write_end    = byte_count;
        m_buffer.begin_write(map_offset, byte_count);
        range.first_byte_offset = map_offset;
        m_map = m_buffer.map();
        return m_map;
    } else {
        // The whole buffer is always mapped - return subspan for requested range
        write_end               = write_offset + byte_count;
        range.first_byte_offset = write_offset;
        write_offset = 0;
        m_map = m_buffer.map().subspan(range.first_byte_offset, byte_count);
        return m_map;
    }

}

auto Buffer_writer::subspan(const std::size_t byte_count) -> std::span<std::byte>
{
    ERHE_VERIFY(m_map.size() >= write_offset + byte_count);
    auto result = m_map.subspan(write_offset, byte_count);
    write_offset += byte_count;
    return result;
}

void Buffer_writer::dump()
{
    auto span = m_buffer.map();
    uint8_t* data = reinterpret_cast<uint8_t*>(span.data());
    const std::size_t byte_count = span.size();
    const std::size_t word_count{byte_count / sizeof(uint32_t)};

    std::stringstream ss;
    for (std::size_t i = 0; i < word_count; ++i) {
        if (i % 16u == 0) {
            ss << fmt::format("{:04x}: ", static_cast<unsigned int>(i));
        }

        ss << fmt::format("{:02x} ", data[i]);

        if (i % 16u == 15u) {
            log_buffer_writer->info(ss.str());
            ss = std::stringstream();
        }
    }
    if (!ss.str().empty()) {
        log_buffer_writer->info(ss.str());
    }
}

void Buffer_writer::end()
{
    if (!m_graphics_instance.info.use_persistent_buffers) {
        range.byte_count = write_offset;
        m_buffer.end_write(map_offset, write_offset);
        write_offset += map_offset;
        map_offset = 0;
        write_end = 0;
    } else {
        range.byte_count = write_offset;
        write_offset += range.first_byte_offset;
    }
    m_map = {};
}

void Buffer_writer::reset()
{
    range.first_byte_offset = 0;
    range.byte_count        = 0;
    map_offset              = 0;
    write_offset            = 0;
    wrap_count              = 0;
    m_map                   = {};
}

} // namespace erhe::renderer
