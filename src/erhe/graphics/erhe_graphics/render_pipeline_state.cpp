#include "erhe_graphics/render_pipeline_state.hpp"

#include <algorithm>
#include <vector>

namespace erhe::graphics {

// TODO Move to graphics device?
ERHE_PROFILE_MUTEX(std::mutex, Render_pipeline_state::s_mutex);
std::vector<Render_pipeline_state*>         Render_pipeline_state::s_pipelines;

Render_pipeline_state::Render_pipeline_state()
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
}

Render_pipeline_state::Render_pipeline_state(Pipeline_data&& create_info)
    : data{std::move(create_info)}
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
}

Render_pipeline_state::Render_pipeline_state(const Render_pipeline_state& other)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
    data = other.data;
}

auto Render_pipeline_state::operator=(const Render_pipeline_state& other) -> Render_pipeline_state&
{
    data = other.data;
    return *this;
}

Render_pipeline_state::Render_pipeline_state(Render_pipeline_state&& old)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
    data = old.data;
}

auto Render_pipeline_state::operator=(Render_pipeline_state&& old) -> Render_pipeline_state&
{
    data = old.data;
    return *this;
}

Render_pipeline_state::~Render_pipeline_state() noexcept
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    const auto i = std::remove(s_pipelines.begin(), s_pipelines.end(), this);
    if (i != s_pipelines.end()) {
        s_pipelines.erase(i, s_pipelines.end());
    }
}

void Render_pipeline_state::reset()
{
    data.name           = nullptr;
    data.shader_stages  = nullptr;
    data.vertex_input   = nullptr;
    data.input_assembly = Input_assembly_state{};
    data.multisample    = Multisample_state   {};
    data.rasterization  = Rasterization_state {};
    data.depth_stencil  = Depth_stencil_state {};
    data.color_blend    = Color_blend_state   {};
}

auto Render_pipeline_state::get_pipelines() -> std::vector<Render_pipeline_state*>
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    return s_pipelines;
}

} // namespace erhe::graphics
