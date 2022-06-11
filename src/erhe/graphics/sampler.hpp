#pragma once

#include "erhe/graphics/gl_objects.hpp"

#include <gsl/assert>

#include <array>

namespace erhe::graphics
{

class Sampler
{
public:
    Sampler();

    Sampler(
        const gl::Texture_min_filter min_filter,
        const gl::Texture_mag_filter mag_filter
    );

    Sampler(
        const gl::Texture_min_filter min_filter,
        const gl::Texture_mag_filter mag_filter,
        const gl::Texture_wrap_mode  wrap_mode
    );

    ~Sampler() noexcept = default;

    [[nodiscard]] auto gl_name() const -> unsigned int
    {
        return m_handle.gl_name();
    }

    void set_debug_label(const std::string& value);

    gl::Texture_min_filter               min_filter    {gl::Texture_min_filter::nearest};
    gl::Texture_mag_filter               mag_filter    {gl::Texture_mag_filter::nearest};

    std::array<gl::Texture_wrap_mode, 3> wrap_mode{
        gl::Texture_wrap_mode::clamp_to_border,
        gl::Texture_wrap_mode::clamp_to_border,
        gl::Texture_wrap_mode::clamp_to_border
    };
    gl::Texture_compare_mode             compare_mode  {gl::Texture_compare_mode::none};
    gl::Texture_compare_func             compare_func  {gl::Texture_compare_func::lequal};
    float                                lod_bias      {-1000.0f};
    float                                max_lod       { 1000.0f};
    float                                min_lod       {-1000.0f};
    float                                max_anisotropy{1.0f};

protected:
    void apply();

private:
    Gl_sampler  m_handle;
    std::string m_debug_label;
};

class Sampler_hash
{
public:
    [[nodiscard]]
    auto operator()(const Sampler& sampler) const noexcept -> size_t
    {
        Expects(sampler.gl_name() != 0);

        return static_cast<size_t>(sampler.gl_name());
    }
};

[[nodiscard]] auto operator==(
    const Sampler& lhs,
    const Sampler& rhs
) noexcept -> bool;

[[nodiscard]] auto operator!=(
    const Sampler& lhs,
    const Sampler& rhs
) noexcept -> bool;

} // namespace erhe::graphics
