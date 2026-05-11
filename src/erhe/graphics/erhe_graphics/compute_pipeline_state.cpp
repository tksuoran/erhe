#include "erhe_graphics/compute_pipeline_state.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
#   include "erhe_graphics/gl/gl_compute_pipeline.hpp"
#elif defined(ERHE_GRAPHICS_API_VULKAN)
#   include "erhe_graphics/vulkan/vulkan_compute_pipeline.hpp"
#elif defined(ERHE_GRAPHICS_API_METAL)
#   include "erhe_graphics/metal/metal_compute_pipeline.hpp"
#elif defined(ERHE_GRAPHICS_API_NONE)
#   include "erhe_graphics/null/null_compute_pipeline.hpp"
#endif

#include <algorithm>
#include <vector>

namespace erhe::graphics {

// --- Compute_pipeline ---

Compute_pipeline::Compute_pipeline(Device& device, const Compute_pipeline_data& data)
    : m_data{data}
    , m_impl{std::make_unique<Compute_pipeline_impl>(device, data)}
{
}

Compute_pipeline::~Compute_pipeline() noexcept = default;

Compute_pipeline::Compute_pipeline(Compute_pipeline&& other) noexcept = default;

auto Compute_pipeline::operator=(Compute_pipeline&& other) noexcept -> Compute_pipeline& = default;

auto Compute_pipeline::get_impl() -> Compute_pipeline_impl&
{
    return *m_impl;
}

auto Compute_pipeline::get_impl() const -> const Compute_pipeline_impl&
{
    return *m_impl;
}

auto Compute_pipeline::is_valid() const -> bool
{
    return (m_impl != nullptr) && m_impl->is_valid();
}

auto Compute_pipeline::get_data() const -> const Compute_pipeline_data&
{
    return m_data;
}

// TODO Move to graphics device?
ERHE_PROFILE_MUTEX(std::mutex, Compute_pipeline_state::s_mutex);
std::vector<Compute_pipeline_state*> Compute_pipeline_state::s_pipelines;

Compute_pipeline_state::Compute_pipeline_state()
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
}

Compute_pipeline_state::Compute_pipeline_state(Compute_pipeline_data&& create_info)
    : data{create_info}
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
}

Compute_pipeline_state::Compute_pipeline_state(const Compute_pipeline_state& other)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
    data = other.data;
}

auto Compute_pipeline_state::operator=(const Compute_pipeline_state& other) -> Compute_pipeline_state& = default;

Compute_pipeline_state::Compute_pipeline_state(Compute_pipeline_state&& old) noexcept
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
    data = old.data;
}

auto Compute_pipeline_state::operator=(Compute_pipeline_state&& old) noexcept -> Compute_pipeline_state&
{
    data = old.data;
    return *this;
}

Compute_pipeline_state::~Compute_pipeline_state() noexcept
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    const auto i = std::remove(s_pipelines.begin(), s_pipelines.end(), this);
    if (i != s_pipelines.end()) {
        s_pipelines.erase(i, s_pipelines.end());
    }
}

void Compute_pipeline_state::reset()
{
    data.name          = nullptr;
    data.shader_stages = nullptr;
}

auto Compute_pipeline_state::get_pipelines() -> std::vector<Compute_pipeline_state*>
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    return s_pipelines;
}

} // namespace erhe::graphics
