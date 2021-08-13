#include "erhe/graphics/renderbuffer.hpp"

namespace erhe::graphics
{

Renderbuffer::Renderbuffer(const gl::Internal_format internal_format,
                           const unsigned int        width,
                           const unsigned int        height)
    : m_internal_format{internal_format}
    , m_sample_count   {0}
    , m_width          {width}
    , m_height         {height}
{
    Expects(gl_name() != 0);
    Expects(m_width  > 0);
    Expects(m_height > 0);

    gl::named_renderbuffer_storage_multisample(gl_name(), 0, internal_format, width, height);
}

Renderbuffer::Renderbuffer(const gl::Internal_format internal_format,
                           const unsigned int        sample_count,
                           const unsigned int        width,
                           const unsigned int        height)
    : m_internal_format{internal_format}
    , m_sample_count   {sample_count}
    , m_width          {width}
    , m_height         {height}
{
    Expects(gl_name() != 0);
    Expects(m_width  > 0);
    Expects(m_height > 0);

    gl::named_renderbuffer_storage_multisample(gl_name(), sample_count, internal_format, width, height);
}

Renderbuffer::~Renderbuffer() = default;

void Renderbuffer::set_debug_label(std::string_view label)
{
    gl::object_label(gl::Object_identifier::renderbuffer,
                     gl_name(), static_cast<GLsizei>(label.length()), label.data());
}

auto Renderbuffer::internal_format() const
-> gl::Internal_format
{
    return m_internal_format;
}

auto Renderbuffer::sample_count() const
-> unsigned int
{
    return m_sample_count;
}

auto Renderbuffer::width() const
-> unsigned int
{
    return m_width;
}

auto Renderbuffer::height() const
-> unsigned int
{
    return m_height;
}

auto Renderbuffer::gl_name() const
-> unsigned int
{
    return m_handle.gl_name();
}

auto Renderbuffer_hash::operator()(const Renderbuffer& renderbuffer) const noexcept
-> size_t
{
    Expects(renderbuffer.gl_name() != 0);

    return static_cast<size_t>(renderbuffer.gl_name());
}

auto operator==(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept
-> bool
{
    Expects(lhs.gl_name() != 0);
    Expects(rhs.gl_name() != 0);

    return lhs.gl_name() == rhs.gl_name();
}

auto operator!=(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept
-> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
