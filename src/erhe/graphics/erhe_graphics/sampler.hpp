#pragma once

#include "erhe_graphics/gl_objects.hpp"

#include <array>
#include <string>

namespace erhe::graphics {

class Device;

class Sampler_create_info
{
public:
    gl::Texture_min_filter               min_filter    {gl::Texture_min_filter::nearest_mipmap_nearest};
    gl::Texture_mag_filter               mag_filter    {gl::Texture_mag_filter::nearest};

    std::array<gl::Texture_wrap_mode, 3> wrap_mode{
        gl::Texture_wrap_mode::clamp_to_edge,
        gl::Texture_wrap_mode::clamp_to_edge,
        gl::Texture_wrap_mode::clamp_to_edge
    };
    gl::Texture_compare_mode             compare_mode  {gl::Texture_compare_mode::none};
    gl::Texture_compare_func             compare_func  {gl::Texture_compare_func::lequal};
    float                                lod_bias      {    0.0f};
    float                                max_lod       { 1000.0f};
    float                                min_lod       {-1000.0f};
    float                                max_anisotropy{1.0f};
    std::string                          debug_label;
};

class Sampler
{
public:
    Sampler(Device& device, const Sampler_create_info& create_info);
    ~Sampler() noexcept = default;

    [[nodiscard]] auto gl_name() const -> unsigned int
    {
        return m_handle.gl_name();
    }

    void set_debug_label(const std::string& value); // TODO Remove

    [[nodiscard]] auto get_debug_label() const -> const std::string&;
    [[nodiscard]] auto uses_mipmaps   () const -> bool;

    auto get_lod_bias() const -> float { return m_lod_bias; }

protected:
    void apply();

private:
    Gl_sampler               m_handle;
    gl::Texture_min_filter   m_min_filter    {gl::Texture_min_filter::nearest_mipmap_nearest};
    gl::Texture_mag_filter   m_mag_filter    {gl::Texture_mag_filter::nearest};

    std::array<gl::Texture_wrap_mode, 3> m_wrap_mode{
        gl::Texture_wrap_mode::clamp_to_border,
        gl::Texture_wrap_mode::clamp_to_border,
        gl::Texture_wrap_mode::clamp_to_border
    };
    gl::Texture_compare_mode m_compare_mode  {gl::Texture_compare_mode::none};
    gl::Texture_compare_func m_compare_func  {gl::Texture_compare_func::lequal};
    float                    m_lod_bias      {    0.0f};
    float                    m_max_lod       { 1000.0f};
    float                    m_min_lod       {-1000.0f};
    float                    m_max_anisotropy{1.0f};
    std::string              m_debug_label;
};

[[nodiscard]] auto operator==(const Sampler& lhs, const Sampler& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Sampler& lhs, const Sampler& rhs) noexcept -> bool;

} // namespace erhe::graphics
