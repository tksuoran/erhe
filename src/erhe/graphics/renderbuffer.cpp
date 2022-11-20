#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/graphics_log.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/gl_helpers.hpp"
#include "erhe/gl/wrapper_functions.hpp"

namespace erhe::graphics
{

Renderbuffer::Renderbuffer(
    const gl::Internal_format internal_format,
    const unsigned int        width,
    const unsigned int        height
)
    : m_internal_format{internal_format}
    , m_sample_count   {0}
    , m_width          {width}
    , m_height         {height}
{
    Expects(gl_name() != 0);
    Expects(m_width  > 0);
    Expects(m_height > 0);

    gl::named_renderbuffer_storage_multisample(
        gl_name(),
        0,
        internal_format,
        width,
        height
    );
}

Renderbuffer::Renderbuffer(
    const gl::Internal_format internal_format,
    const unsigned int        sample_count,
    const unsigned int        width,
    const unsigned int        height
)
    : m_internal_format{internal_format}
    , m_sample_count   {sample_count}
    , m_width          {width}
    , m_height         {height}
{
    Expects(gl_name() != 0);
    Expects(m_width  > 0);
    Expects(m_height > 0);

    if (sample_count > 0)
    {
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
            m_sample_count = std::min(m_sample_count, static_cast<unsigned int>(Instance::limits.max_samples));
            if (gl_helpers::has_color(m_internal_format) || gl_helpers::has_alpha(m_internal_format))
            {
                m_sample_count = std::min(m_sample_count, static_cast<unsigned int>(Instance::limits.max_color_texture_samples));
            }
            if (gl_helpers::has_depth(m_internal_format) || gl_helpers::has_stencil(m_internal_format))
            {
                m_sample_count = std::min(m_sample_count, static_cast<unsigned int>(Instance::limits.max_depth_texture_samples));
            }
            if (gl_helpers::is_integer(m_internal_format))
            {
                m_sample_count = std::min(m_sample_count, static_cast<unsigned int>(Instance::limits.max_integer_samples));
            }
        }
    }

    gl::Texture_target texture_target = m_sample_count > 0 ?
        gl::Texture_target::texture_2d_multisample :
        gl::Texture_target::texture_2d;

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

    if (framebuffer_renderable_support == support_caveat)
    {
        log_texture->warn(
            "Format {} has framebuffer renderable support caveat",
            c_str(internal_format)
        );
    }
    if (framebuffer_renderable_support == support_none)
    {
        log_texture->warn(
            "Format {} has framebuffer renderable support none",
            c_str(internal_format)
        );
        m_internal_format = gl::Internal_format::rgba8; // TODO what should be done?
    }

    gl::named_renderbuffer_storage_multisample(
        gl_name(),
        m_sample_count,
        m_internal_format,
        m_width,
        m_height
    );
}

Renderbuffer::~Renderbuffer() noexcept
{
}

void Renderbuffer::set_debug_label(const std::string& label)
{
    m_debug_label = "(R) " + label;
    gl::object_label(
        gl::Object_identifier::renderbuffer,
        gl_name(),
        static_cast<GLsizei>(label.length()),
        m_debug_label.c_str()
    );
}

auto Renderbuffer::internal_format() const -> gl::Internal_format
{
    return m_internal_format;
}

auto Renderbuffer::sample_count() const -> unsigned int
{
    return m_sample_count;
}

auto Renderbuffer::width() const -> unsigned int
{
    return m_width;
}

auto Renderbuffer::height() const -> unsigned int
{
    return m_height;
}

auto Renderbuffer::gl_name() const -> unsigned int
{
    return m_handle.gl_name();
}

auto Renderbuffer_hash::operator()(
    const Renderbuffer& renderbuffer
) const noexcept -> size_t
{
    Expects(renderbuffer.gl_name() != 0);

    return static_cast<size_t>(renderbuffer.gl_name());
}

auto operator==(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept -> bool
{
    Expects(lhs.gl_name() != 0);
    Expects(rhs.gl_name() != 0);

    return lhs.gl_name() == rhs.gl_name();
}

auto operator!=(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
