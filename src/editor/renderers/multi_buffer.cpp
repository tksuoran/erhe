// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "renderers/multi_buffer.hpp"
#include "renderers/programs.hpp"
#include "renderers/program_interface.hpp"
#include "log.hpp"

#include "erhe/toolkit/profile.hpp"

namespace editor
{

auto Multi_buffer::writer() -> erhe::application::Buffer_writer&
{
    return m_writer;
}

void Multi_buffer::allocate(
    const gl::Buffer_target target,
    const unsigned int      binding_point,
    const std::size_t       size,
    const std::string&      name
)
{
    m_binding_point = binding_point;

    log_render->info("{}: binding = {} size = {}", name, binding_point, size);

    static constexpr gl::Buffer_storage_mask storage_mask{
        gl::Buffer_storage_mask::map_coherent_bit   |
        gl::Buffer_storage_mask::map_persistent_bit |
        gl::Buffer_storage_mask::map_write_bit
    };

    static constexpr gl::Map_buffer_access_mask access_mask{
        gl::Map_buffer_access_mask::map_coherent_bit   |
        gl::Map_buffer_access_mask::map_persistent_bit |
        gl::Map_buffer_access_mask::map_write_bit
    };

    for (std::size_t slot = 0; slot < s_frame_resources_count; ++slot)
    {
        m_buffers.emplace_back(
            target,
            size,
            storage_mask,
            access_mask,
            fmt::format("{} {}", name, slot)
        );
    }
}

[[nodiscard]] auto Multi_buffer::current_buffer() -> erhe::graphics::Buffer&
{
    return m_buffers.at(m_current_slot);
}

void Multi_buffer::next_frame()
{
    m_current_slot = (m_current_slot + 1) % s_frame_resources_count;

    m_writer.reset();

    SPDLOG_LOGGER_TRACE(
        log_render,
        "{} next_frame() - current slot is now {}",
        m_name,
        m_current_slot
    );
}

void Multi_buffer::bind()
{
    ERHE_PROFILE_FUNCTION

    if (m_writer.range.byte_count == 0)
    {
        return;
    }

    const auto& buffer = current_buffer();

    SPDLOG_LOGGER_TRACE(
        log_draw,
        "binding {} {} buffer offset = {} byte count = {}",
        m_name,
        m_binding_point,
        m_writer.range.first_byte_offset,
        m_writer.range.byte_count
    );

    ERHE_VERIFY(
        (buffer.target() != gl::Buffer_target::uniform_buffer) ||
        (m_writer.range.byte_count <= static_cast<std::size_t>(erhe::graphics::Instance::limits.max_uniform_block_size))
    );
    ERHE_VERIFY(
        m_writer.range.first_byte_offset + m_writer.range.byte_count <= buffer.capacity_byte_count()
    );

    if (buffer.target() == gl::Buffer_target::draw_indirect_buffer)
    {
        gl::bind_buffer(buffer.target(), static_cast<GLuint>(buffer.gl_name()));
    }
    else
    {
        gl::bind_buffer_range(
            buffer.target(),
            static_cast<GLuint>    (m_binding_point),
            static_cast<GLuint>    (buffer.gl_name()),
            static_cast<GLintptr>  (m_writer.range.first_byte_offset),
            static_cast<GLsizeiptr>(m_writer.range.byte_count)
        );
    }
}

}