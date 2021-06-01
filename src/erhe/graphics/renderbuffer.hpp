#pragma once

#include "erhe/graphics/gl_objects.hpp"

#include <gsl/pointers>

namespace erhe::graphics
{

class Renderbuffer
{
public:
    Renderbuffer(gl::Internal_format internal_format,
                 unsigned int        width,
                 unsigned int        height);

    Renderbuffer(gl::Internal_format internal_format,
                 unsigned int        sample_count,
                 unsigned int        width,
                 unsigned int        height);

    ~Renderbuffer() = default;

    auto internal_format() const
    -> gl::Internal_format;

    auto sample_count() const
    -> unsigned int;

    auto width() const
    -> unsigned int;

    auto height() const
    -> unsigned int;

    auto gl_name() const
    -> unsigned int;

    void set_debug_label(const std::string& label)
    {
        gl::object_label(gl::Object_identifier::renderbuffer,
                         gl_name(), static_cast<GLsizei>(label.length()), label.c_str());
    }

private:
    Gl_renderbuffer     m_handle;
    gl::Internal_format m_internal_format;
    unsigned int        m_sample_count{0};
    unsigned int        m_width       {0};
    unsigned int        m_height      {0};
};

struct Renderbuffer_hash
{
    auto operator()(const Renderbuffer& renderbuffer) const noexcept
    -> size_t
    {
        Expects(renderbuffer.gl_name() != 0);

        return static_cast<size_t>(renderbuffer.gl_name());
    }
};

auto operator==(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept
-> bool;

auto operator!=(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept
-> bool;

} // namespace erhe::graphics
