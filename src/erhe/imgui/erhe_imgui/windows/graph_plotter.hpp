#pragma once

#include "erhe_imgui/windows/graph.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <array>

namespace erhe::imgui {

class Graph_plotter
{
public:
    auto begin() -> bool;
    void plot (const Graph<ImVec2>& graph);
    void plot (const Graph<std::array<ImVec2, 2>>& graph);
    void end  ();

    void sample_text(float x, float y, const char* text, ImU32 col);
    void sample_x_line(float x, ImU32 col, float thickness = 1.0f);
    void sample_y_line(float y, ImU32 col, float thickness = 1.0f);

private:
    [[nodiscard]] static auto map  (float in_value, float in_low, float in_high, float out_low, float out_high) -> float;
    [[nodiscard]] static auto scale(float in_value, float in_low, float in_high, float out_low, float out_high) -> float;
    [[nodiscard]] static auto map  (ImVec2 input_value, ImRect input_box, ImRect output_box) -> ImVec2;
    [[nodiscard]] static auto scale(ImVec2 input_value, ImRect input_box, ImRect output_box) -> ImVec2;
    [[nodiscard]] auto sample_to_widget(float x, float y) const -> ImVec2;
    [[nodiscard]] auto widget_to_sample(float x, float y) const -> ImVec2;
    void add_line(ImDrawList* draw_list, float x0, float y0, float x1, float y1, ImU32 color, float thickness = 1.0f) const;
    void path_line_to(ImDrawList* draw_list, float x, float y) const;

    ImRect m_view; // In sample sapce
    ImRect m_bb;   // In ImGui pixel coordinate space
    ImVec2 m_zoom_scale{1.0f, 1.0f};
    ImVec2 m_pan       {0.0f, 0.0f};
};

} // namespace erhe::imgui
