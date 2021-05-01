#include "erhe/graphics/renderbuffer.hpp"

namespace erhe::graphics
{

Renderbuffer::Renderbuffer(gl::Internal_format internal_format,
                           unsigned int        width,
                           unsigned int        height)
    : m_internal_format{internal_format}
    , m_sample_count   {0}
    , m_width          {width}
    , m_height         {height}
{
    Expects(gl_name() != 0);
    Expects(m_width > 0);
    Expects(m_height > 0);

    gl::named_renderbuffer_storage_multisample(gl_name(), 0, internal_format, width, height);
}

Renderbuffer::Renderbuffer(gl::Internal_format internal_format,
                           unsigned int        sample_count,
                           unsigned int        width,
                           unsigned int        height)
    : m_internal_format{internal_format}
    , m_sample_count   {sample_count}
    , m_width          {width}
    , m_height         {height}
{
    Expects(gl_name() != 0);
    Expects(m_width > 0);
    Expects(m_height > 0);

    gl::named_renderbuffer_storage_multisample(gl_name(), sample_count, internal_format, width, height);
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
