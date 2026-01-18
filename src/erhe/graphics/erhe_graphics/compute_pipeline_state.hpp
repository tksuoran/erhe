#pragma once

#include "erhe_profile/profile.hpp"

#include <mutex>
#include <vector>

namespace erhe::graphics {

class Shader_stages;

class Compute_pipeline_data
{
public:
    const char*    name         {nullptr};
    Shader_stages* shader_stages{nullptr};
};

class Compute_pipeline_state final
{
public:
    Compute_pipeline_state();
    Compute_pipeline_state(Compute_pipeline_data&& create_info);
    ~Compute_pipeline_state() noexcept;

    Compute_pipeline_state(const Compute_pipeline_state& other);
    auto operator=(const Compute_pipeline_state& other) -> Compute_pipeline_state&;
    Compute_pipeline_state(Compute_pipeline_state&& old) noexcept;
    auto operator=(Compute_pipeline_state&& old) noexcept -> Compute_pipeline_state&;

    void reset();

    Compute_pipeline_data data;

    static auto get_pipelines() -> std::vector<Compute_pipeline_state*>;

    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, s_mutex);
    static std::vector<Compute_pipeline_state*>       s_pipelines;
};

} // namespace erhe::graphics
