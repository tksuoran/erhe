#include "erhe_graphics/renderbuffer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"
#include <fmt/format.h>

namespace erhe::graphics {

Renderbuffer::Renderbuffer(
    Device&                        device,
    const erhe::dataformat::Format pixelformat,
    const unsigned int             width,
    const unsigned int             height
)
    : m_device      {device}
    , m_handle      {device}
    , m_pixelformat {pixelformat}
    , m_sample_count{0}
    , m_width       {width}
    , m_height      {height}
{
    ERHE_VERIFY(gl_name() != 0);
    ERHE_VERIFY(m_width  > 0);
    ERHE_VERIFY(m_height > 0);

    std::optional<gl::Internal_format> gl_internal_format = gl_helpers::convert_to_gl(pixelformat);
    ERHE_VERIFY(gl_internal_format.has_value());
    device.named_renderbuffer_storage_multisample(gl_name(), 0, gl_internal_format.value(), width, height);
}

Renderbuffer::Renderbuffer(
    Device&                        device,
    const erhe::dataformat::Format pixelformat,
    const unsigned int             sample_count,
    const unsigned int             width,
    const unsigned int             height
)
    : m_device      {device}
    , m_handle      {device}
    , m_pixelformat {pixelformat}
    , m_sample_count{sample_count}
    , m_width       {width}
    , m_height      {height}
{
    ERHE_VERIFY(gl_name() != 0);
    ERHE_VERIFY(m_width  > 0);
    ERHE_VERIFY(m_height > 0);

    const std::optional<gl::Internal_format> gl_internal_format = gl_helpers::convert_to_gl(pixelformat);
    ERHE_VERIFY(gl_internal_format.has_value());
    const gl::Internal_format internal_format = gl_internal_format.value();

    if (sample_count > 0) {
        // int num_sample_counts = 0;
        // gl::get_internalformat_iv(
        //     gl::Texture_target::texture_2d_multisample,
        //     internal_format,
        //     gl::Internal_format_p_name::num_sample_counts,
        //     1,
        //     &num_sample_counts
        // );
        // if (num_sample_counts > 0)
        // {
        //     std::vector<int> samples;
        //     samples.resize(num_sample_counts);
        //     gl::get_internalformat_iv(
        //         gl::Texture_target::texture_2d_multisample,
        //         internal_format,
        //         gl::Internal_format_p_name::samples,
        //         num_sample_counts,
        //         reinterpret_cast<GLint*>(samples.data())
        //     );
        //     for (int i : samples)
        //     {
        //     }
        // }
        // else
        {
            m_sample_count = std::min(m_sample_count, static_cast<unsigned int>(device.limits.max_samples));
            // TODO Format_properties
            if (gl_helpers::has_color(internal_format) || gl_helpers::has_alpha(internal_format)) {
                m_sample_count = std::min(m_sample_count, static_cast<unsigned int>(device.limits.max_color_texture_samples));
            }
            if (gl_helpers::has_depth(internal_format) || gl_helpers::has_stencil(internal_format)) {
                m_sample_count = std::min(m_sample_count, static_cast<unsigned int>(device.limits.max_depth_texture_samples));
            }
            if (gl_helpers::is_integer(internal_format)) {
                m_sample_count = std::min(m_sample_count, static_cast<unsigned int>(device.limits.max_integer_samples));
            }
        }
    }

    gl::Texture_target texture_target = (m_sample_count > 0)
        ? gl::Texture_target::texture_2d_multisample
        : gl::Texture_target::texture_2d;

    int framebuffer_renderable_support{0};
    gl::get_internalformat_iv(
        texture_target,
        internal_format,
        gl::Internal_format_p_name::framebuffer_renderable,
        1,
        &framebuffer_renderable_support
    );

    constexpr int support_none   = 0x0000u;
    //constexpr int support_full   = 0x82B7u;
    constexpr int support_caveat = 0x82B8u;

    if (framebuffer_renderable_support == support_caveat) {
        log_texture->warn(
            "Format {} has framebuffer renderable support caveat",
            c_str(internal_format)
        );
    }
    if (framebuffer_renderable_support == support_none) {
        log_texture->warn(
            "Format {} has framebuffer renderable support none",
            c_str(internal_format)
        );
        ERHE_FATAL("format is not renderable");
    }

    device.named_renderbuffer_storage_multisample(
        gl_name(),
        m_sample_count,
        internal_format,
        m_width,
        m_height
    );
}

Renderbuffer::~Renderbuffer() noexcept
{
}

void Renderbuffer::set_debug_label(std::string_view label)
{
    m_debug_label = fmt::format("(R:{}) {}", gl_name(), label);
    gl::object_label(
        gl::Object_identifier::renderbuffer,
        gl_name(),
        static_cast<GLsizei>(m_debug_label.length()),
        m_debug_label.c_str()
    );
}

auto Renderbuffer::get_pixelformat() const -> erhe::dataformat::Format
{
    return m_pixelformat;
}

auto Renderbuffer::get_sample_count() const -> unsigned int
{
    return m_sample_count;
}

auto Renderbuffer::get_width() const -> unsigned int
{
    return m_width;
}

auto Renderbuffer::get_height() const -> unsigned int
{
    return m_height;
}

auto Renderbuffer::gl_name() const -> unsigned int
{
    return m_handle.gl_name();
}

auto Renderbuffer_hash::operator()(const Renderbuffer& renderbuffer) const noexcept -> size_t
{
    ERHE_VERIFY(renderbuffer.gl_name() != 0);

    return static_cast<size_t>(renderbuffer.gl_name());
}

auto operator==(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept -> bool
{
    ERHE_VERIFY(lhs.gl_name() != 0);
    ERHE_VERIFY(rhs.gl_name() != 0);

    return lhs.gl_name() == rhs.gl_name();
}

auto operator!=(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
