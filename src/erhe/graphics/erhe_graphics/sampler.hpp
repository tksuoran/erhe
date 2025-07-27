#pragma once

#include "erhe_graphics/enums.hpp"

#include <array>
#include <memory>
#include <string>

namespace erhe::graphics {

class Device;

class Sampler_create_info
{
public:
    Filter              min_filter    {Filter::nearest};
    Filter              mag_filter    {Filter::nearest};
    Sampler_mipmap_mode mipmap_mode;

    std::array<Sampler_address_mode, 3> address_mode{
        Sampler_address_mode::clamp_to_edge,
        Sampler_address_mode::clamp_to_edge,
        Sampler_address_mode::clamp_to_edge
    };
    bool                compare_enable   {false};
    Compare_operation   compare_operation{Compare_operation::always};
    float               lod_bias         {    0.0f};
    float               max_lod          { 1000.0f};
    float               min_lod          {-1000.0f};
    float               max_anisotropy   {1.0f};
    std::string         debug_label;
};

class Sampler_impl;
class Sampler final
{
public:
    Sampler(Device& device, const Sampler_create_info& create_info);
    ~Sampler() noexcept;

    [[nodiscard]] auto get_debug_label() const -> const std::string&;
    [[nodiscard]] auto uses_mipmaps   () const -> bool;
    [[nodiscard]] auto get_lod_bias   () const -> float;
    [[nodiscard]] auto get_impl       () -> Sampler_impl&;
    [[nodiscard]] auto get_impl       () const -> const Sampler_impl&;

private:
    std::unique_ptr<Sampler_impl> m_impl;
};

[[nodiscard]] auto operator==(const Sampler& lhs, const Sampler& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Sampler& lhs, const Sampler& rhs) noexcept -> bool;

} // namespace erhe::graphics
