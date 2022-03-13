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

    ~Renderbuffer() noexcept;

    [[nodiscard]] auto internal_format() const -> gl::Internal_format;
    [[nodiscard]] auto sample_count   () const -> unsigned int;
    [[nodiscard]] auto width          () const -> unsigned int;
    [[nodiscard]] auto height         () const -> unsigned int;
    [[nodiscard]] auto gl_name        () const -> unsigned int;

    void set_debug_label(const std::string& label);

private:
    Gl_renderbuffer     m_handle;
    std::string         m_debug_label;
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

[[nodiscard]] auto operator==(
    const Renderbuffer& lhs,
    const Renderbuffer& rhs
) noexcept -> bool;

[[nodiscard]] auto operator!=(
    const Renderbuffer& lhs,
    const Renderbuffer& rhs
) noexcept -> bool;

} // namespace erhe::graphics
