// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_renderer/multi_buffer.hpp"
#include "erhe_renderer/renderer_log.hpp"

#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::renderer
{

Multi_buffer::Multi_buffer(
    erhe::graphics::Instance& graphics_instance,
    const std::string_view    name
)
    : m_instance{graphics_instance}
    , m_writer  {graphics_instance}
    , m_name    {name}
{
}

auto Multi_buffer::writer() -> Buffer_writer&
{
    return m_writer;
}

namespace {

static constexpr gl::Buffer_storage_mask storage_mask_persistent{
    gl::Buffer_storage_mask::map_coherent_bit   |
    gl::Buffer_storage_mask::map_persistent_bit |
    gl::Buffer_storage_mask::map_write_bit
};
static constexpr gl::Buffer_storage_mask storage_mask_not_persistent{
    gl::Buffer_storage_mask::map_write_bit
};
inline auto storage_mask(igl::IDevice& device) -> gl::Buffer_storage_mask
{
    return instance.info.use_persistent_buffers
        ? storage_mask_persistent
        : storage_mask_not_persistent;
}

static constexpr gl::Map_buffer_access_mask access_mask_persistent{
    gl::Map_buffer_access_mask::map_coherent_bit   |
    gl::Map_buffer_access_mask::map_persistent_bit |
    gl::Map_buffer_access_mask::map_write_bit
};
static constexpr gl::Map_buffer_access_mask access_mask_not_persistent{
    gl::Map_buffer_access_mask::map_write_bit
};
inline auto access_mask(igl::IDevice& device) -> gl::Map_buffer_access_mask
{
    return instance.info.use_persistent_buffers
        ? access_mask_persistent
        : access_mask_not_persistent;
}

}

void Multi_buffer::allocate(
    const gl::Buffer_target target,
    const unsigned int      binding_point,
    const std::size_t       size
)
{
    ERHE_VERIFY(gl_helpers::is_indexed(target));
    m_binding_point = binding_point;

    log_multi_buffer->trace("{}: binding point = {} size = {}", m_name, binding_point, size);

    for (std::size_t slot = 0; slot < s_frame_resources_count; ++slot) {
        m_buffers.emplace_back(
            m_instance,
            target,
            size,
            storage_mask(m_instance),
            access_mask(m_instance),
            fmt::format("{} {}", m_name, slot)
        );
    }
}

void Multi_buffer::allocate(
    const gl::Buffer_target target,
    const std::size_t       size
)
{
    ERHE_VERIFY(!gl_helpers::is_indexed(target));
    m_binding_point = 0;

    log_multi_buffer->trace("{}: size = {}", m_name, size);

    for (std::size_t slot = 0; slot < s_frame_resources_count; ++slot) {
        m_buffers.emplace_back(
            m_instance,
            target,
            size,
            storage_mask(m_instance),
            access_mask(m_instance),
            fmt::format("{} {}", m_name, slot)
        );
    }
}

[[nodiscard]] auto Multi_buffer::buffers() -> std::vector<erhe::graphics::Buffer>&
{
    return m_buffers;
}

[[nodiscard]] auto Multi_buffer::buffers() const -> const std::vector<erhe::graphics::Buffer>&
{
    return m_buffers;
}

[[nodiscard]] auto Multi_buffer::current_buffer() -> erhe::graphics::Buffer&
{
    return m_buffers.at(m_current_slot);
}

[[nodiscard]] auto Multi_buffer::name() const -> const std::string&
{
    return m_name;
}

void Multi_buffer::next_frame()
{
    m_current_slot = (m_current_slot + 1) % s_frame_resources_count;

    m_writer.reset();

    SPDLOG_LOGGER_TRACE(
        log_multi_buffer,
        "{} next_frame() - current slot is now {}",
        m_name,
        m_current_slot
    );
}

void Multi_buffer::bind(const erhe::renderer::Buffer_range& range)
{
    ERHE_PROFILE_FUNCTION();

    if (range.byte_count == 0) {
        return;
    }

    const auto& buffer = current_buffer();

    SPDLOG_LOGGER_TRACE(
        log_multi_buffer,
        "binding {} {} {} buffer offset = {} byte count = {}",
        m_name,
        gl::c_str(buffer.target()),
        m_binding_point.has_value() ? "uses binding point" : "non-indexed binding",
        m_binding_point.has_value() ? m_binding_point.value() : 0,
        range.first_byte_offset,
        range.byte_count
    );

    ERHE_VERIFY(
        (buffer.target() != gl::Buffer_target::uniform_buffer) ||
        (range.byte_count <= static_cast<std::size_t>(m_instance.limits.max_uniform_block_size))
    );
    ERHE_VERIFY(
        range.first_byte_offset + range.byte_count <= buffer.capacity_byte_count()
    );

    if (gl_helpers::is_indexed(buffer.target())) {
        gl::bind_buffer_range(
            buffer.target(),
            static_cast<GLuint>    (m_binding_point),
            static_cast<GLuint>    (buffer.gl_name()),
            static_cast<GLintptr>  (range.first_byte_offset),
            static_cast<GLsizeiptr>(range.byte_count)
        );
    } else {
        gl::bind_buffer(buffer.target(), static_cast<GLuint>(buffer.gl_name()));
    }
}

void Multi_buffer::reset()
{
    m_buffers.clear();
    m_current_slot = 0;
    m_writer.reset();
}

} // namespace erhe::renderer
