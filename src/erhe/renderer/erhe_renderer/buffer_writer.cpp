#include "erhe_renderer/buffer_writer.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_verify/verify.hpp"

#include <sstream>

namespace erhe::renderer
{

Buffer_writer::Buffer_writer(igl::IDevice& device)
    : m_instance{instance}
{
}

void Buffer_writer::shader_storage_align()
{
    while (write_offset % m_instance.implementation_defined.shader_storage_buffer_offset_alignment) {
        write_offset++;
    }
}

void Buffer_writer::uniform_align()
{
    while (write_offset % m_instance.implementation_defined.uniform_buffer_offset_alignment) {
        write_offset++;
    }
}

auto Buffer_writer::begin(
    erhe::graphics::Buffer* const buffer,
    std::size_t                   byte_count
) -> gsl::span<std::byte>
{
    ERHE_VERIFY(m_buffer == nullptr);
    m_buffer = buffer;
    const gl::Buffer_target buffer_target = m_buffer->target();

    switch (buffer_target) {
        //using enum gl::Buffer_target;
        case gl::Buffer_target::shader_storage_buffer: {
            shader_storage_align();
            break;
        }

        case gl::Buffer_target::uniform_buffer: {
            uniform_align();
            break;
        }
        default: {
            // TODO
            break;
        }
    }

    if (byte_count == 0) {
        byte_count = buffer->capacity_byte_count() - write_offset;
    } else {
        byte_count = std::min(byte_count, buffer->capacity_byte_count() - write_offset);
    }

    if (!m_instance.info.use_persistent_buffers) {
        // Only requested range will be temporarily mapped
        map_offset   = write_offset;
        write_offset = 0;
        write_end    = byte_count;
        m_buffer->begin_write(map_offset, byte_count);
        range.first_byte_offset = map_offset + write_offset;
        m_map = buffer->map();
        return m_map;
    } else {
        // The whole buffer is always mapped - return subspan for requested range
        write_end               = write_offset + byte_count;
        range.first_byte_offset = write_offset;
        write_offset = 0;
        m_map = buffer->map().subspan(range.first_byte_offset, byte_count);
        return m_map;
    }

}

auto Buffer_writer::subspan(const std::size_t byte_count) -> gsl::span<std::byte>
{
    ERHE_VERIFY(m_map.size() >= write_offset + byte_count);
    auto result = m_map.subspan(write_offset, byte_count);
    write_offset += byte_count;
    return result;
}

void Buffer_writer::dump()
{
    auto span = m_buffer->map();
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
    ERHE_VERIFY(m_buffer != nullptr);
    if (!m_instance.info.use_persistent_buffers) {
        range.byte_count = write_offset;
        m_buffer->end_write(map_offset, write_offset);
        write_offset += map_offset;
        map_offset = 0;
        write_end = 0;
    } else {
        range.byte_count = write_offset;
        write_offset += range.first_byte_offset;
    }
    m_buffer = nullptr;
    m_map = {};

}

void Buffer_writer::reset()
{
    range.first_byte_offset = 0;
    range.byte_count        = 0;
    map_offset              = 0;
    write_offset            = 0;
    m_map                   = {};
}

} // namespace erhe::renderer
