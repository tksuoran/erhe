#include "erhe_graphics/metal/metal_sampler.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/device.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

namespace {

auto to_mtl_address_mode(const Sampler_address_mode mode) -> MTL::SamplerAddressMode
{
    switch (mode) {
        case Sampler_address_mode::repeat:          return MTL::SamplerAddressModeRepeat;
        case Sampler_address_mode::mirrored_repeat: return MTL::SamplerAddressModeMirrorRepeat;
        case Sampler_address_mode::clamp_to_edge:   return MTL::SamplerAddressModeClampToEdge;
        default:                                    return MTL::SamplerAddressModeClampToEdge;
    }
}

auto to_mtl_min_mag_filter(const Filter filter) -> MTL::SamplerMinMagFilter
{
    switch (filter) {
        case Filter::nearest: return MTL::SamplerMinMagFilterNearest;
        case Filter::linear:  return MTL::SamplerMinMagFilterLinear;
        default:              return MTL::SamplerMinMagFilterNearest;
    }
}

auto to_mtl_mip_filter(const Sampler_mipmap_mode mode) -> MTL::SamplerMipFilter
{
    switch (mode) {
        case Sampler_mipmap_mode::not_mipmapped: return MTL::SamplerMipFilterNotMipmapped;
        case Sampler_mipmap_mode::nearest:       return MTL::SamplerMipFilterNearest;
        case Sampler_mipmap_mode::linear:        return MTL::SamplerMipFilterLinear;
        default:                                 return MTL::SamplerMipFilterNotMipmapped;
    }
}

auto to_mtl_compare_function(const Compare_operation op) -> MTL::CompareFunction
{
    switch (op) {
        case Compare_operation::never:            return MTL::CompareFunctionNever;
        case Compare_operation::less:             return MTL::CompareFunctionLess;
        case Compare_operation::equal:            return MTL::CompareFunctionEqual;
        case Compare_operation::less_or_equal:    return MTL::CompareFunctionLessEqual;
        case Compare_operation::greater:          return MTL::CompareFunctionGreater;
        case Compare_operation::not_equal:        return MTL::CompareFunctionNotEqual;
        case Compare_operation::greater_or_equal: return MTL::CompareFunctionGreaterEqual;
        case Compare_operation::always:           return MTL::CompareFunctionAlways;
        default:                                  return MTL::CompareFunctionAlways;
    }
}

} // anonymous namespace

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
    , m_debug_label      {create_info.debug_label      }
{
    Device_impl& device_impl = device.get_impl();
    MTL::Device* mtl_device = device_impl.get_mtl_device();
    if (mtl_device == nullptr) {
        return;
    }

    MTL::SamplerDescriptor* desc = MTL::SamplerDescriptor::alloc()->init();
    desc->setMinFilter(to_mtl_min_mag_filter(m_min_filter));
    desc->setMagFilter(to_mtl_min_mag_filter(m_mag_filter));
    desc->setMipFilter(to_mtl_mip_filter(m_mipmap_mode));
    desc->setSAddressMode(to_mtl_address_mode(m_address_mode[0]));
    desc->setTAddressMode(to_mtl_address_mode(m_address_mode[1]));
    desc->setRAddressMode(to_mtl_address_mode(m_address_mode[2]));
    desc->setMaxAnisotropy(static_cast<NS::UInteger>(m_max_anisotropy));
    if (m_compare_enable) {
        desc->setCompareFunction(to_mtl_compare_function(m_compare_operation));
    }
    desc->setLodMinClamp(m_min_lod);
    desc->setLodMaxClamp(m_max_lod);
    desc->setSupportArgumentBuffers(true);

    if (!m_debug_label.empty()) {
        NS::String* label = NS::String::alloc()->init(m_debug_label.data(), NS::UTF8StringEncoding);
        desc->setLabel(label);
        label->release();
    }

    m_mtl_sampler = mtl_device->newSamplerState(desc);
    desc->release();
}

Sampler_impl::~Sampler_impl() noexcept
{
    if (m_mtl_sampler != nullptr) {
        m_mtl_sampler->release();
        m_mtl_sampler = nullptr;
    }
}

auto Sampler_impl::gl_name() const -> unsigned int { return 0; }
auto Sampler_impl::get_debug_label() const -> erhe::utility::Debug_label { return m_debug_label; }
auto Sampler_impl::uses_mipmaps() const -> bool { return m_mipmap_mode != Sampler_mipmap_mode::not_mipmapped; }
auto Sampler_impl::get_lod_bias() const -> float { return m_lod_bias; }
auto Sampler_impl::get_mtl_sampler() const -> MTL::SamplerState* { return m_mtl_sampler; }

auto operator==(const Sampler_impl& lhs, const Sampler_impl& rhs) noexcept -> bool { return &lhs == &rhs; }
auto operator!=(const Sampler_impl& lhs, const Sampler_impl& rhs) noexcept -> bool { return !(lhs == rhs); }

} // namespace erhe::graphics
