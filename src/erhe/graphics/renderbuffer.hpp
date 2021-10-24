#pragma once

#include "erhe/graphics/gl_objects.hpp"

#include <gsl/pointers>

namespace erhe::graphics
{

class Renderbuffer
{
public:
    Renderbuffer(
        const gl::Internal_format internal_format,
        const unsigned int        width,
        const unsigned int        height
    );

    Renderbuffer(
        const gl::Internal_format internal_format,
        const unsigned int        sample_count,
        const unsigned int        width,
        const unsigned int        height
    );

    ~Renderbuffer();

    auto internal_format() const -> gl::Internal_format;
    auto sample_count   () const -> unsigned int;
    auto width          () const -> unsigned int;
    auto height         () const -> unsigned int;
    auto gl_name        () const -> unsigned int;
    void set_debug_label(std::string_view label);

private:
    Gl_renderbuffer     m_handle;
    gl::Internal_format m_internal_format;
    unsigned int        m_sample_count{0};
    unsigned int        m_width       {0};
    unsigned int        m_height      {0};
};

class Renderbuffer_hash
{
public:
    auto operator()(const Renderbuffer& renderbuffer) const noexcept -> size_t;
};

auto operator==(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept -> bool;

auto operator!=(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept -> bool;

} // namespace erhe::graphics
