#include "erhe_graphics/state/multisample_state.hpp"
#include "erhe_gl/wrapper_functions.hpp"

#include <utility>

// #define DISABLE_CACHE 1

namespace erhe::graphics {

auto Multisample_state_hash::operator()(const Multisample_state& multisample_state) noexcept -> std::size_t
{
    return
        (
            (multisample_state.sample_shading_enable    ? 1u : 0u) |
            (multisample_state.alpha_to_coverage_enable ? 2u : 0u) |
            (multisample_state.alpha_to_one_enable      ? 4u : 0u)
        ) ^
        (
            std::hash<float>{}(multisample_state.min_sample_shading)
        );
}

void Multisample_state_tracker::reset()
{
    gl::disable(gl::Enable_cap::sample_shading);
    gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    gl::disable(gl::Enable_cap::sample_alpha_to_one);
    gl::min_sample_shading(1.0f);
}

void Multisample_state_tracker::execute(const Multisample_state& state)
{
#if DISABLE_CACHE
    if (state.sample_shading_enable) {
        gl::enable(gl::Enable_cap::sample_shading);
    } else {
        gl::disable(gl::Enable_cap::sample_shading);
    }
    if (state.alpha_to_coverage_enable) {
        gl::enable(gl::Enable_cap::sample_alpha_to_coverage);
    } else {
        gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    }
    if (state.alpha_to_one_enable) {
        gl::enable(gl::Enable_cap::sample_alpha_to_one);
    } else {
        gl::disable(gl::Enable_cap::sample_alpha_to_one);
    }
    gl::min_sample_shading(state.min_sample_shading);
#else
    if (state.sample_shading_enable) {
        if (!m_cache.sample_shading_enable) {
            gl::enable(gl::Enable_cap::sample_shading);
            m_cache.sample_shading_enable = true;
        }
        if (m_cache.min_sample_shading != state.min_sample_shading) {
            gl::min_sample_shading(state.min_sample_shading);
            m_cache.min_sample_shading = state.min_sample_shading;
        }
    } else {
        if (m_cache.sample_shading_enable) {
            gl::disable(gl::Enable_cap::sample_shading);
            m_cache.sample_shading_enable = false;
        }
    }

    if (state.alpha_to_coverage_enable) {
        if (!m_cache.alpha_to_coverage_enable) {
            gl::enable(gl::Enable_cap::sample_alpha_to_coverage);
            m_cache.alpha_to_coverage_enable = true;
        }
    } else {
        if (m_cache.alpha_to_coverage_enable) {
            gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
            m_cache.alpha_to_coverage_enable = false;
        }
    }

    if (state.alpha_to_one_enable) {
        if (!m_cache.alpha_to_one_enable) {
            gl::enable(gl::Enable_cap::sample_alpha_to_one);
            m_cache.alpha_to_one_enable = true;
        }
    } else {
        if (m_cache.alpha_to_one_enable) {
            gl::disable(gl::Enable_cap::sample_alpha_to_one);
            m_cache.alpha_to_one_enable = false;
        }
    }
#endif
}

auto operator==(const Multisample_state& lhs, const Multisample_state& rhs) noexcept -> bool
{
    return
        (lhs.sample_shading_enable    == rhs.sample_shading_enable   ) &&
        (lhs.alpha_to_coverage_enable == rhs.alpha_to_coverage_enable) &&
        (lhs.alpha_to_one_enable      == rhs.alpha_to_one_enable     ) &&
        (lhs.min_sample_shading       == rhs.min_sample_shading);
}

auto operator!=(const Multisample_state& lhs, const Multisample_state& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
