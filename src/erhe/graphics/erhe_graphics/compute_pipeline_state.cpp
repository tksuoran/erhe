#include "erhe_graphics/compute_pipeline_state.hpp"

#include <algorithm>
#include <vector>

namespace erhe::graphics {

// TODO Move to graphics device?
ERHE_PROFILE_MUTEX(std::mutex, Compute_pipeline_state::s_mutex);
std::vector<Compute_pipeline_state*> Compute_pipeline_state::s_pipelines;

Compute_pipeline_state::Compute_pipeline_state()
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
}

Compute_pipeline_state::Compute_pipeline_state(Compute_pipeline_data&& create_info)
    : data{std::move(create_info)}
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

auto Compute_pipeline_state::operator=(const Compute_pipeline_state& other) -> Compute_pipeline_state&
{
    data = other.data;
    return *this;
}

Compute_pipeline_state::Compute_pipeline_state(Compute_pipeline_state&& old)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
    data = old.data;
}

auto Compute_pipeline_state::operator=(Compute_pipeline_state&& old) -> Compute_pipeline_state&
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
    data.name           = nullptr;
    data.shader_stages  = nullptr;
}

auto Compute_pipeline_state::get_pipelines() -> std::vector<Compute_pipeline_state*>
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    return s_pipelines;
}

} // namespace erhe::graphics
