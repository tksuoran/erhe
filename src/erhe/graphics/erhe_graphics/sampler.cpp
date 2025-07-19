#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

Sampler::Sampler(Device& device, const Sampler_create_info& create_info)
    : m_handle           {device}
    , m_min_filter       {create_info.min_filter       }
    , m_mag_filter       {create_info.mag_filter       }
    , m_mipmap_mode      {create_info.mipmap_mode      }
    , m_address_mode     {create_info.address_mode     }
    , m_compare_enable   {create_info.compare_enable   }
    , m_compare_operation{create_info.compare_operation}
    , m_lod_bias         {create_info.lod_bias         }
    , m_max_lod          {create_info.max_lod          }
    , m_min_lod          {create_info.min_lod          }
    , m_max_anisotropy   {create_info.max_anisotropy   }
{
    ERHE_VERIFY(m_handle.gl_name() != 0);
    apply();
    set_debug_label(create_info.debug_label);
    log_texture->trace("Created sampler '{}'", get_debug_label());
}

void Sampler::set_debug_label(const std::string& value)
{
    m_debug_label = fmt::format("(S:{}) {}", gl_name(), value);
    gl::object_label(gl::Object_identifier::sampler, gl_name(), -1, m_debug_label.c_str());
}

auto Sampler::get_debug_label() const -> const std::string&
{
    return m_debug_label;
}

auto Sampler::uses_mipmaps() const -> bool
{
    return m_mipmap_mode != Sampler_mipmap_mode::not_mipmapped;
}

auto Sampler::to_gl(const Compare_operation compare_operation) -> int
{
    switch (compare_operation) {
        case Compare_operation::never:            return GL_NEVER;
        case Compare_operation::less:             return GL_LESS;
        case Compare_operation::equal:            return GL_EQUAL;
        case Compare_operation::less_or_equal:    return GL_LEQUAL;
        case Compare_operation::greater:          return GL_GREATER;
        case Compare_operation::not_equal:        return GL_NOTEQUAL;
        case Compare_operation::greater_or_equal: return GL_GEQUAL;
        case Compare_operation::always:           return GL_ALWAYS;
        default: {
            ERHE_FATAL("Bad compare_operation %u", static_cast<unsigned int>(compare_operation));
            return 0;
        }
    }
}

auto Sampler::to_gl(const Sampler_address_mode address_mode) -> int
{
    switch (address_mode) {
        case Sampler_address_mode::repeat:          return GL_REPEAT;
        case Sampler_address_mode::clamp_to_edge:   return GL_CLAMP_TO_EDGE;
        case Sampler_address_mode::mirrored_repeat: return GL_MIRRORED_REPEAT;
        default: {
            ERHE_FATAL("Bad address_mode %u", static_cast<unsigned int>(address_mode));
            return 0;
        }
    }
};

auto Sampler::to_gl(Filter filter, Sampler_mipmap_mode mipmap_mode) -> int
{
    switch (mipmap_mode) {
        case Sampler_mipmap_mode::not_mipmapped: {
            switch (filter) {
                case Filter::nearest: return GL_NEAREST;
                case Filter::linear:  return GL_LINEAR;
                default: ERHE_FATAL("Bad filter %u", static_cast<unsigned int>(filter)); return 0;
            }
            break;
        }
        case Sampler_mipmap_mode::nearest: {
            switch (filter) {
                case Filter::nearest: return GL_NEAREST_MIPMAP_NEAREST;
                case Filter::linear:  return GL_LINEAR_MIPMAP_NEAREST;
                default: ERHE_FATAL("Bad filter %u", static_cast<unsigned int>(filter)); return 0;
            }
            break;
        }
        case Sampler_mipmap_mode::linear: {
            switch (filter) {
                case Filter::nearest: return GL_NEAREST_MIPMAP_LINEAR;
                case Filter::linear:  return GL_LINEAR_MIPMAP_LINEAR;
                default: ERHE_FATAL("Bad filter %u", static_cast<unsigned int>(filter)); return 0;
            }
        }
        default: {
            ERHE_FATAL("Bad mipmap mode %u", static_cast<unsigned int>(mipmap_mode));
            return 0;
        }
    }
}

void Sampler::apply()
{
    ERHE_VERIFY(m_handle.gl_name() != 0);

    const auto name = m_handle.gl_name();

    const int gl_compare_mode = m_compare_enable ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE;
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_min_filter,     to_gl(m_min_filter, m_mipmap_mode));
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_mag_filter,     to_gl(m_mag_filter, Sampler_mipmap_mode::not_mipmapped));
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_compare_mode,   gl_compare_mode);
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_compare_func,   to_gl(m_compare_operation));
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_wrap_s,         to_gl(m_address_mode[0]));
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_wrap_t,         to_gl(m_address_mode[1]));
    gl::sampler_parameter_i(name, gl::Sampler_parameter_i::texture_wrap_r,         to_gl(m_address_mode[2]));
    gl::sampler_parameter_f(name, gl::Sampler_parameter_f::texture_lod_bias,       m_lod_bias);
    gl::sampler_parameter_f(name, gl::Sampler_parameter_f::texture_min_lod,        m_min_lod);
    gl::sampler_parameter_f(name, gl::Sampler_parameter_f::texture_max_lod,        m_max_lod);
    gl::sampler_parameter_f(name, gl::Sampler_parameter_f::texture_max_anisotropy, m_max_anisotropy);
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
    ERHE_VERIFY(lhs.gl_name() != 0);
    ERHE_VERIFY(rhs.gl_name() != 0);

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
//        ERHE_VERIFY(sampler.gl_name() != 0);
//
//        return static_cast<size_t>(sampler.gl_name());
//    }
//};

} // namespace erhe::graphics
