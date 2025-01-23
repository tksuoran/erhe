#include "erhe_graphics/pipeline.hpp"

#include <algorithm>
#include <vector>

namespace erhe::graphics {

// TODO Move to graphics instance?
ERHE_PROFILE_MUTEX(std::mutex, Pipeline::s_mutex);
std::vector<Pipeline*>         Pipeline::s_pipelines;

Pipeline::Pipeline()
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
}

Pipeline::Pipeline(Pipeline_data&& create_info)
    : data{std::move(create_info)}
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
}

Pipeline::Pipeline(const Pipeline& other)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
    data = other.data;
}

auto Pipeline::operator=(const Pipeline& other) -> Pipeline&
{
    data = other.data;
    return *this;
}

Pipeline::Pipeline(Pipeline&& old)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_pipelines.push_back(this);
    data = old.data;
}

auto Pipeline::operator=(Pipeline&& old) -> Pipeline&
{
    data = old.data;
    return *this;
}

Pipeline::~Pipeline() noexcept
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    const auto i = std::remove(s_pipelines.begin(), s_pipelines.end(), this);
    if (i != s_pipelines.end()) {
        s_pipelines.erase(i, s_pipelines.end());
    }
}

void Pipeline::reset()
{
    data.name           = nullptr;
    data.shader_stages  = nullptr;
    data.vertex_input   = nullptr;
    data.input_assembly = Input_assembly_state{};
    data.rasterization  = Rasterization_state {};
    data.depth_stencil  = Depth_stencil_state {};
    data.color_blend    = Color_blend_state   {};
}

auto Pipeline::get_pipelines() -> std::vector<Pipeline*>
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    return s_pipelines;
}

} // namespace erhe::graphics
