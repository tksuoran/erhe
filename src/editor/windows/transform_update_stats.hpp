#pragma once

#include "erhe_imgui/windows/performance_window.hpp"
#include "erhe_scene/scene.hpp"

namespace editor {

class App_scenes;
class Tools;

// Aggregates Scene::Transform_update_stats across all registered scenes plus
// the tool scene, once per frame, right after the per-frame transform update
// sites have run (Editor::tick()). Feeds three plots registered in the
// Performance window (dirty node count, visited node count, CPU cost) and the
// MCP get_transform_update_stats query.
class Transform_update_stats_tracker
{
public:
    explicit Transform_update_stats_tracker(erhe::imgui::Performance_window& performance_window);
    ~Transform_update_stats_tracker() noexcept;

    // Called once per frame by Editor::tick() after the transform update
    // sites; drains every scene's accumulator into the frame aggregate.
    void sample_frame(App_scenes& app_scenes, Tools& tools);

    [[nodiscard]] auto get_frame_stats() const -> const erhe::scene::Scene::Transform_update_stats&;

    // Running aggregate since launch or the last reset_aggregate(), for
    // averaging over a measurement window (MCP get_transform_update_stats).
    [[nodiscard]] auto get_aggregate_stats      () const -> const erhe::scene::Scene::Transform_update_stats&;
    [[nodiscard]] auto get_aggregate_frame_count() const -> std::size_t;
    [[nodiscard]] auto get_peak_total_ms        () const -> double;
    void reset_aggregate();

private:
    class Stat_plot : public erhe::imgui::Plot
    {
    public:
        enum class Source : unsigned int {
            dirty_count = 0,
            visited_count,
            total_ms
        };

        Stat_plot(Transform_update_stats_tracker& tracker, Source source, const char* label);

        void sample() override;
        auto label() const -> const char* override;

    private:
        Transform_update_stats_tracker& m_tracker;
        Source                          m_source;
        const char*                     m_label;
    };

    erhe::imgui::Performance_window&           m_performance_window;
    erhe::scene::Scene::Transform_update_stats m_frame_stats;
    erhe::scene::Scene::Transform_update_stats m_aggregate_stats;
    std::size_t                                m_aggregate_frame_count{0};
    double                                     m_peak_total_ms        {0.0};
    Stat_plot                                  m_dirty_plot;
    Stat_plot                                  m_visited_plot;
    Stat_plot                                  m_time_plot;
};

}
