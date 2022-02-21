#pragma once

#include "windows/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <imgui.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace erhe::graphics
{
    class Gpu_timer;
};

namespace editor
{

class Editor_rendering;
class Id_renderer;
class Imgui_renderer;
class Line_renderer;
class Shadow_renderer;
class Text_renderer;

class Gpu_timer_plot
{
public:
    explicit Gpu_timer_plot(
        erhe::graphics::Gpu_timer* timer,
        const size_t               width = 256
    );

    void imgui    ();
    void clear    ();
    void sample   ();
    auto gpu_timer() const -> erhe::graphics::Gpu_timer*;

    size_t                     m_offset         {0};
    size_t                     m_value_count    {0};
    erhe::graphics::Gpu_timer* m_gpu_timer      {nullptr};
    float                      m_max_great      {1.0f};
    float                      m_max_ok         {2.5f};
    float                      m_scale_min      {0.0f};
    float                      m_scale_max      {2.0f}; // 2 ms
    float                      m_scale_max_limit{1.0f}; // 1 ms
    ImVec2                     m_frame_size     {256.0f, 64.0f};
    std::vector<float>         m_values;
};

class Performance_window
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Performance_window"};
    static constexpr std::string_view c_title{"Performance"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Performance_window();
    ~Performance_window() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

private:

    // Component dependencies
    std::shared_ptr<Editor_rendering> m_editor_rendering;
    std::shared_ptr<Shadow_renderer>  m_shadow_renderer;
    std::shared_ptr<Id_renderer>      m_id_renderer;
    std::shared_ptr<Imgui_renderer>   m_imgui_renderer;
    std::shared_ptr<Line_renderer>    m_line_renderer;
    std::shared_ptr<Text_renderer>    m_text_renderer;

    std::vector<Gpu_timer_plot> m_gpu_timer_plots;
    bool m_pause{false};

    int  m_taps   = 1;
    int  m_expand = 0;
    int  m_reduce = 0;
    bool m_linear = true;

};

} // namespace editor
