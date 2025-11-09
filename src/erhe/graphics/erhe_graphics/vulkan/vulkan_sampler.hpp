#pragma once

#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/enums.hpp"

#include <array>
#include <string>

namespace erhe::graphics {

class Device_impl;

class Sampler_impl
{
public:
    Sampler_impl(Device& device, const Sampler_create_info& create_info);
    ~Sampler_impl() noexcept = default;

    //[[nodiscard]] auto get_vulkan_sampler() const -> VkSampler
    //{
    //    return m_vulkan_sampler;
    //}

    [[nodiscard]] auto get_debug_label() const -> const std::string&;
    [[nodiscard]] auto uses_mipmaps   () const -> bool;

    auto get_lod_bias() const -> float;

private:
    //VkSampler         m_vulkan_sampler;
    Filter              m_min_filter    {Filter::nearest};
    Filter              m_mag_filter    {Filter::nearest};
    Sampler_mipmap_mode m_mipmap_mode   {Sampler_mipmap_mode::not_mipmapped};

    std::array<Sampler_address_mode, 3> m_address_mode{
        Sampler_address_mode::clamp_to_edge,
        Sampler_address_mode::clamp_to_edge,
        Sampler_address_mode::clamp_to_edge
    };
    bool                m_compare_enable   {false};
    Compare_operation   m_compare_operation{Compare_operation::always};
    float               m_lod_bias      {    0.0f};
    float               m_max_lod       { 1000.0f};
    float               m_min_lod       {-1000.0f};
    float               m_max_anisotropy{1.0f};
    std::string         m_debug_label;
};

[[nodiscard]] auto operator==(const Sampler_impl& lhs, const Sampler_impl& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Sampler_impl& lhs, const Sampler_impl& rhs) noexcept -> bool;

} // namespace erhe::graphics
