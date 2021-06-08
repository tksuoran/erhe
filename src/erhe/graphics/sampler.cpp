#include "erhe/graphics/sampler.hpp"

namespace erhe::graphics
{

Sampler::Sampler() = default;

Sampler::Sampler(gl::Texture_min_filter min_filter,
                 gl::Texture_mag_filter mag_filter)
    : min_filter{min_filter}
    , mag_filter{mag_filter}
{
    Expects(m_handle.gl_name() != 0);

    apply();
}

Sampler::Sampler(gl::Texture_min_filter min_filter,
                 gl::Texture_mag_filter mag_filter,
                 gl::Texture_wrap_mode  wrap_mode)
    : min_filter{min_filter}
    , mag_filter{mag_filter}
    , wrap_mode {wrap_mode, wrap_mode, wrap_mode}
{
    Expects(m_handle.gl_name() != 0);

    apply();
}

void Sampler::apply()
{
    Expects(m_handle.gl_name() != 0);

    const auto name = m_handle.gl_name();
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_min_filter,     static_cast<int>(min_filter));
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_mag_filter,     static_cast<int>(mag_filter));
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_compare_mode,   static_cast<int>(compare_mode));
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_compare_func,   static_cast<int>(compare_func));
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_wrap_s,         static_cast<int>(wrap_mode[0]));
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_wrap_t,         static_cast<int>(wrap_mode[1]));
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_wrap_r,         static_cast<int>(wrap_mode[2]));
    gl::sampler_parameter_f(name, gl::Sampler_parameter_f::texture_lod_bias,       lod_bias);
    gl::sampler_parameter_f(name, gl::Sampler_parameter_f::texture_min_lod,        min_lod);
    gl::sampler_parameter_f(name, gl::Sampler_parameter_f::texture_max_lod,        max_lod);
    gl::sampler_parameter_f(name, gl::Sampler_parameter_f::texture_max_anisotropy, max_anisotropy);
}

auto operator==(const Sampler& lhs, const Sampler& rhs) noexcept
-> bool
{
    Expects(lhs.gl_name() != 0);
    Expects(rhs.gl_name() != 0);

    return lhs.gl_name() == rhs.gl_name();
}

auto operator!=(const Sampler& lhs, const Sampler& rhs) noexcept
-> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
