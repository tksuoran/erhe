#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/graphics_log.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include <fmt/format.h>
#include <gsl/assert>

namespace erhe::graphics
{

Sampler::Sampler(const Sampler_create_info& create_info)
    : min_filter    {create_info.min_filter    }
    , mag_filter    {create_info.mag_filter    }
    , wrap_mode     {create_info.wrap_mode     }
    , compare_mode  {create_info.compare_mode  }
    , compare_func  {create_info.compare_func  }
    , lod_bias      {create_info.lod_bias      }
    , max_lod       {create_info.max_lod       }
    , min_lod       {create_info.min_lod       }
    , max_anisotropy{create_info.max_anisotropy}
{
    Expects(m_handle.gl_name() != 0);
    apply();
    set_debug_label(create_info.debug_label);
    log_texture->trace("Created sampler '{}'", debug_label());
}

void Sampler::set_debug_label(const std::string& value)
{
    m_debug_label = fmt::format("(S:{}) {}", gl_name(), value);
    gl::object_label(
        gl::Object_identifier::sampler,
        gl_name(),
        static_cast<GLsizei>(m_debug_label.length()),
        m_debug_label.c_str()
    );
}

auto Sampler::debug_label() const -> const std::string&
{
    return m_debug_label;
}

auto Sampler::uses_mipmaps() const -> bool
{
    switch (min_filter) {
        case gl::Texture_min_filter::linear                : return false;
        case gl::Texture_min_filter::linear_mipmap_linear  : return true;
        case gl::Texture_min_filter::linear_mipmap_nearest : return true;
        case gl::Texture_min_filter::nearest               : return false;
        case gl::Texture_min_filter::nearest_mipmap_linear : return true;
        case gl::Texture_min_filter::nearest_mipmap_nearest: return true;
        default: return false;
    }
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
    //int get_min_filter_i{0};
    //int get_mag_filter_i{0};
    //gl::get_sampler_parameter_iv(name, gl::Sampler_parameter_i::texture_min_filter, &get_min_filter_i);
    //gl::get_sampler_parameter_iv(name, gl::Sampler_parameter_i::texture_mag_filter, &get_mag_filter_i);
    //gl::Texture_min_filter get_min_filter{static_cast<gl::Texture_min_filter>(get_min_filter_i)};
    //gl::Texture_mag_filter get_mag_filter{static_cast<gl::Texture_mag_filter>(get_mag_filter_i)};
    //log_texture->info("min filter = {} - {}", gl::c_str(min_filter), gl::c_str(get_min_filter));
    //log_texture->info("mag filter = {} - {}", gl::c_str(mag_filter), gl::c_str(get_mag_filter));
}

auto operator==(const Sampler& lhs, const Sampler& rhs) noexcept -> bool
{
    Expects(lhs.gl_name() != 0);
    Expects(rhs.gl_name() != 0);

    return lhs.gl_name() == rhs.gl_name();
}

auto operator!=(const Sampler& lhs, const Sampler& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

//class Sampler_hash
//{
//public:
//    [[nodiscard]] auto operator()(const Sampler& sampler) const noexcept -> size_t
//    {
//        Expects(sampler.gl_name() != 0);
//
//        return static_cast<size_t>(sampler.gl_name());
//    }
//};

} // namespace erhe::graphics
