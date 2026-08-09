#include "windows/transform_update_stats.hpp"

#include "app_scenes.hpp"
#include "scene/scene_root.hpp"
#include "tools/tools.hpp"

#include <algorithm>

namespace editor {

Transform_update_stats_tracker::Stat_plot::Stat_plot(
    Transform_update_stats_tracker& tracker,
    const Source                    source,
    const char*                     label
)
    : m_tracker{tracker}
    , m_source {source}
    , m_label  {label}
{
    m_values.resize(256);
    if (source == Source::total_ms) {
        m_max_great       = 1.0f;
        m_max_ok          = 4.0f;
        m_scale_max_limit = 4.0f;
    } else {
        m_unit            = "nodes";
        m_integer_values  = true;
        m_max_great       = 250.0f;
        m_max_ok          = 1000.0f;
        m_scale_max_limit = 100.0f;
    }
}

void Transform_update_stats_tracker::Stat_plot::sample()
{
    const erhe::scene::Scene::Transform_update_stats& stats = m_tracker.get_frame_stats();
    float value{0.0f};
    switch (m_source) {
        case Source::dirty_count:   value = static_cast<float>(stats.dirty_count);   break;
        case Source::visited_count: value = static_cast<float>(stats.visited_count); break;
        case Source::total_ms:      value = static_cast<float>(stats.total_ms());    break;
        default:                                                                     break;
    }
    m_values[m_offset % m_values.size()] = value;
    m_value_count = std::min(m_value_count + 1, m_values.size());
    m_offset++;
}

auto Transform_update_stats_tracker::Stat_plot::label() const -> const char*
{
    return m_label;
}

Transform_update_stats_tracker::Transform_update_stats_tracker(erhe::imgui::Performance_window& performance_window)
    : m_performance_window{performance_window}
    , m_dirty_plot  {*this, Stat_plot::Source::dirty_count,   "Transform dirty nodes"}
    , m_visited_plot{*this, Stat_plot::Source::visited_count, "Transform visited nodes"}
    , m_time_plot   {*this, Stat_plot::Source::total_ms,      "Transform update CPU"}
{
    m_performance_window.register_plot(&m_dirty_plot);
    m_performance_window.register_plot(&m_visited_plot);
    m_performance_window.register_plot(&m_time_plot);
}

Transform_update_stats_tracker::~Transform_update_stats_tracker() noexcept
{
    m_performance_window.unregister_plot(&m_dirty_plot);
    m_performance_window.unregister_plot(&m_visited_plot);
    m_performance_window.unregister_plot(&m_time_plot);
}

void Transform_update_stats_tracker::sample_frame(App_scenes& app_scenes, Tools& tools)
{
    m_frame_stats.reset();
    for (const std::shared_ptr<Scene_root>& scene_root : app_scenes.get_scene_roots()) {
        m_frame_stats.add(scene_root->get_scene().sample_transform_update_stats());
    }
    const std::shared_ptr<Scene_root> tool_scene_root = tools.get_tool_scene_root();
    if (tool_scene_root) {
        erhe::scene::Scene* const tool_scene = tool_scene_root->get_hosted_scene();
        if (tool_scene != nullptr) {
            m_frame_stats.add(tool_scene->sample_transform_update_stats());
        }
    }
    m_aggregate_stats.add(m_frame_stats);
    m_aggregate_frame_count += 1;
    m_peak_total_ms = std::max(m_peak_total_ms, m_frame_stats.total_ms());
}

auto Transform_update_stats_tracker::get_frame_stats() const -> const erhe::scene::Scene::Transform_update_stats&
{
    return m_frame_stats;
}

auto Transform_update_stats_tracker::get_aggregate_stats() const -> const erhe::scene::Scene::Transform_update_stats&
{
    return m_aggregate_stats;
}

auto Transform_update_stats_tracker::get_aggregate_frame_count() const -> std::size_t
{
    return m_aggregate_frame_count;
}

auto Transform_update_stats_tracker::get_peak_total_ms() const -> double
{
    return m_peak_total_ms;
}

void Transform_update_stats_tracker::reset_aggregate()
{
    m_aggregate_stats.reset();
    m_aggregate_frame_count = 0;
    m_peak_total_ms         = 0.0;
}

}
