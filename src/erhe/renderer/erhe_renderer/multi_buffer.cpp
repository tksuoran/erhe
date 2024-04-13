// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_renderer/multi_buffer.hpp"
#include "erhe_renderer/renderer_log.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include "igl/Device.h"
#include "igl/Buffer.h"

namespace erhe::renderer
{

Multi_buffer::Multi_buffer(
    igl::IDevice&          device,
    const std::string_view name
)
    : m_device{device}
    , m_writer{device}
    , m_name  {name}
{
}

auto Multi_buffer::writer() -> Buffer_writer&
{
    return m_writer;
}


void Multi_buffer::allocate(
    igl::BufferDesc::BufferType buffer_type,
    const unsigned int          binding_point,
    const std::size_t           size
)
{
    // TODO ERHE_VERIFY(gl_helpers::is_indexed(target));
    m_binding_point = binding_point;

    log_multi_buffer->trace("{}: binding point = {} size = {}", m_name, binding_point, size);

    for (std::size_t slot = 0; slot < s_frame_resources_count; ++slot) {
        igl::Result result{};
        const std::shared_ptr<igl::IBuffer> buffer = m_device.createBuffer(
            igl::BufferDesc{
                buffer_type,
                nullptr,
                size,
                igl::ResourceStorage::Shared,
                0, // BufferAPIHint
                fmt::format("{} {}", m_name, slot)
            },
            &result
        );
        assert(result.isOk());
        assert(buffer);
        m_buffers.push_back(buffer);
    }
}

void Multi_buffer::allocate(
    igl::BufferDesc::BufferType buffer_type,
    const std::size_t           size
)
{
    // TODO ERHE_VERIFY(!gl_helpers::is_indexed(target));
    m_binding_point = 0;

    log_multi_buffer->trace("{}: size = {}", m_name, size);

    for (std::size_t slot = 0; slot < s_frame_resources_count; ++slot) {
        igl::Result result{};
        const std::shared_ptr<igl::IBuffer> buffer = m_device.createBuffer(
            igl::BufferDesc{
                buffer_type,
                nullptr,
                size,
                igl::ResourceStorage::Shared,
                0, // BufferAPIHint
                fmt::format("{} {}", m_name, slot)
            },
            &result
        );
        assert(result.isOk());
        assert(buffer);
        m_buffers.push_back(buffer);
    }
}

[[nodiscard]] auto Multi_buffer::buffers() -> std::vector<std::shared_ptr<igl::IBuffer>>&
{
    return m_buffers;
}

[[nodiscard]] auto Multi_buffer::buffers() const -> const std::vector<std::shared_ptr<igl::IBuffer>>&
{
    return m_buffers;
}

[[nodiscard]] auto Multi_buffer::current_buffer() -> const std::shared_ptr<igl::IBuffer>&
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

#if 0
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

    // TODO ERHE_VERIFY(
    //     (buffer.target() != gl::Buffer_target::uniform_buffer) ||
    //     (range.byte_count <= static_cast<std::size_t>(m_instance.limits.max_uniform_block_size))
    // );
    ERHE_VERIFY(
        range.first_byte_offset + range.byte_count <= buffer->getSizeInBytes()
    );

    // static_cast<igl::BufferDesc::BufferType>(igl::BufferDesc::BufferTypeBits::Indirect),
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
#endif

void Multi_buffer::reset()
{
    m_buffers.clear();
    m_current_slot = 0;
    m_writer.reset();
}

} // namespace erhe::renderer
