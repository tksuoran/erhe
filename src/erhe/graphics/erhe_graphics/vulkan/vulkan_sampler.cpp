#include "erhe_graphics/gl/gl_sampler.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

Sampler_impl::Sampler_impl(Device& device, const Sampler_create_info& create_info)
    : m_min_filter       {create_info.min_filter       }
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
}

auto Sampler_impl::get_debug_label() const -> const std::string&
{
    return m_debug_label;
}

auto Sampler_impl::get_lod_bias() const -> float
{
    return m_lod_bias;
}

auto Sampler_impl::uses_mipmaps() const -> bool
{
    return m_mipmap_mode != Sampler_mipmap_mode::not_mipmapped;
}

auto operator==(const Sampler_impl& lhs, const Sampler_impl& rhs) noexcept -> bool
{
    return false;
}

auto operator!=(const Sampler_impl& lhs, const Sampler_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

//class Sampler_hash
//{
//public:
//    [[nodiscard]] auto operator()(const Sampler_impl& sampler) const noexcept -> size_t
//    {
//        ERHE_VERIFY(sampler.gl_name() != 0);
//
//        return static_cast<size_t>(sampler.gl_name());
//    }
//};

} // namespace erhe::graphics
