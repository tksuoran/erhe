#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

namespace {

auto to_vk_filter(Filter filter) -> VkFilter
{
    switch (filter) {
        case Filter::nearest: return VK_FILTER_NEAREST;
        case Filter::linear:  return VK_FILTER_LINEAR;
        default:              return VK_FILTER_NEAREST;
    }
}

auto to_vk_sampler_mipmap_mode(Sampler_mipmap_mode mode) -> VkSamplerMipmapMode
{
    switch (mode) {
        case Sampler_mipmap_mode::nearest:       return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case Sampler_mipmap_mode::linear:        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        case Sampler_mipmap_mode::not_mipmapped: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        default:                                 return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
}

auto to_vk_sampler_address_mode(Sampler_address_mode mode) -> VkSamplerAddressMode
{
    switch (mode) {
        case Sampler_address_mode::repeat:          return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case Sampler_address_mode::mirrored_repeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case Sampler_address_mode::clamp_to_edge:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        default:                                    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

auto to_vk_compare_op(Compare_operation op) -> VkCompareOp
{
    switch (op) {
        case Compare_operation::never:            return VK_COMPARE_OP_NEVER;
        case Compare_operation::less:             return VK_COMPARE_OP_LESS;
        case Compare_operation::equal:            return VK_COMPARE_OP_EQUAL;
        case Compare_operation::less_or_equal:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case Compare_operation::greater:          return VK_COMPARE_OP_GREATER;
        case Compare_operation::not_equal:        return VK_COMPARE_OP_NOT_EQUAL;
        case Compare_operation::greater_or_equal: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case Compare_operation::always:           return VK_COMPARE_OP_ALWAYS;
        default:                                  return VK_COMPARE_OP_ALWAYS;
    }
}

} // anonymous namespace

Sampler_impl::Sampler_impl(Device& device, const Sampler_create_info& create_info)
    : m_device           {device}
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
    , m_debug_label      {create_info.debug_label      }
{
    VkDevice vulkan_device = device.get_impl().get_vulkan_device();

    const bool enable_anisotropy = m_max_anisotropy > 1.0f;

    const VkSamplerCreateInfo sampler_create_info{
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .magFilter               = to_vk_filter(m_mag_filter),
        .minFilter               = to_vk_filter(m_min_filter),
        .mipmapMode              = to_vk_sampler_mipmap_mode(m_mipmap_mode),
        .addressModeU            = to_vk_sampler_address_mode(m_address_mode[0]),
        .addressModeV            = to_vk_sampler_address_mode(m_address_mode[1]),
        .addressModeW            = to_vk_sampler_address_mode(m_address_mode[2]),
        .mipLodBias              = m_lod_bias,
        .anisotropyEnable        = enable_anisotropy ? VK_TRUE : VK_FALSE,
        .maxAnisotropy           = m_max_anisotropy,
        .compareEnable           = m_compare_enable ? VK_TRUE : VK_FALSE,
        .compareOp               = to_vk_compare_op(m_compare_operation),
        .minLod                  = m_min_lod,
        .maxLod                  = (m_mipmap_mode == Sampler_mipmap_mode::not_mipmapped) ? 0.0f : m_max_lod,
        .borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };

    VkResult result = vkCreateSampler(vulkan_device, &sampler_create_info, nullptr, &m_vulkan_sampler);
    if (result != VK_SUCCESS) {
        log_texture->error("vkCreateSampler() failed with {}", static_cast<int32_t>(result));
        m_vulkan_sampler = VK_NULL_HANDLE;
        return;
    }

    if (!m_debug_label.empty()) {
        device.get_impl().set_debug_label(
            VK_OBJECT_TYPE_SAMPLER,
            reinterpret_cast<uint64_t>(m_vulkan_sampler),
            m_debug_label.data()
        );
    }
}

Sampler_impl::~Sampler_impl() noexcept
{
    if (m_vulkan_sampler != VK_NULL_HANDLE) {
        VkDevice vulkan_device = m_device.get_impl().get_vulkan_device();
        vkDestroySampler(vulkan_device, m_vulkan_sampler, nullptr);
    }
}

auto Sampler_impl::get_vulkan_sampler() const -> VkSampler
{
    return m_vulkan_sampler;
}

auto Sampler_impl::get_debug_label() const -> erhe::utility::Debug_label
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
    return lhs.get_vulkan_sampler() == rhs.get_vulkan_sampler();
}

auto operator!=(const Sampler_impl& lhs, const Sampler_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
