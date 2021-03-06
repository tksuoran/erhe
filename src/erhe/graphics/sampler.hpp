#pragma once

#include "erhe/graphics/gl_objects.hpp"

#include <gsl/assert>

namespace erhe::graphics
{

class Sampler
{
public:
    Sampler();

    Sampler(gl::Texture_min_filter min_filter,
            gl::Texture_mag_filter mag_filter);

    Sampler(gl::Texture_min_filter min_filter,
            gl::Texture_mag_filter mag_filter,
            gl::Texture_wrap_mode  wrap_mode);

    ~Sampler() = default;

    auto gl_name() const
    -> unsigned int
    {
        return m_handle.gl_name();
    }

    gl::Texture_min_filter               min_filter    {gl::Texture_min_filter::nearest};
    gl::Texture_mag_filter               mag_filter    {gl::Texture_mag_filter::nearest};
    std::array<gl::Texture_wrap_mode, 3> wrap_mode     {gl::Texture_wrap_mode::clamp_to_edge,
                                                        gl::Texture_wrap_mode::clamp_to_edge,
                                                        gl::Texture_wrap_mode::clamp_to_edge};
    gl::Texture_compare_mode             compare_mode  {gl::Texture_compare_mode::none};
    gl::Texture_compare_func             compare_func  {gl::Texture_compare_func::lequal};
    float                                lod_bias      {-1000.0f};
    float                                max_lod       { 1000.0f};
    float                                min_lod       {-1000.0f};
    float                                max_anisotropy{1.0f};

protected:
    void apply();

private:
    Gl_sampler m_handle;
};

struct Sampler_hash
{
    auto operator()(const Sampler& sampler) const noexcept
    -> size_t
    {
        Expects(sampler.gl_name() != 0);

        return static_cast<size_t>(sampler.gl_name());
    }
};

auto operator==(const Sampler& lhs, const Sampler& rhs) noexcept
-> bool;

auto operator!=(const Sampler& lhs, const Sampler& rhs) noexcept
-> bool;

} // namespace erhe::graphics
