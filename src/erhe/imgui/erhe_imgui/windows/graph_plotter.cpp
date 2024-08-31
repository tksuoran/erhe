#include "erhe_imgui/windows/graph_plotter.hpp"
#include "erhe_imgui/windows/graph.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <optional>

namespace erhe::imgui {

auto Graph_plotter::map(float in_value, float in_low, float in_high, float out_low, float out_high) -> float
{
    float in_range     = in_high  - in_low;
    float out_range    = out_high - out_low;
    float ratio        = out_range / in_range;
    float mapped_value = ((in_value - in_low) * ratio) + out_low;
    return mapped_value;
}

auto Graph_plotter::scale(float in_value, float in_low, float in_high, float out_low, float out_high) -> float
{
    float in_range     = in_high  - in_low;
    float out_range    = out_high - out_low;
    float ratio        = out_range / in_range;
    float mapped_value = in_value * ratio;
    return mapped_value;
}

auto Graph_plotter::map(ImVec2 in_value, ImRect in_box, ImRect out_box) -> ImVec2
{
    return ImVec2{
        map(in_value.x, in_box.Min.x, in_box.Max.x, out_box.Min.x, out_box.Max.x),
        map(in_value.y, in_box.Min.y, in_box.Max.y, out_box.Min.y, out_box.Max.y)
    };
}

auto Graph_plotter::scale(ImVec2 in_value, ImRect in_box, ImRect out_box) -> ImVec2
{
    return ImVec2{
        scale(in_value.x, in_box.Min.x, in_box.Max.x, out_box.Min.x, out_box.Max.x),
        scale(in_value.y, in_box.Min.y, in_box.Max.y, out_box.Min.y, out_box.Max.y)
    };
}

[[nodiscard]] auto Graph_plotter::sample_to_widget(float x, float y) const -> ImVec2
{
    return map(ImVec2{x, y}, m_view, m_bb); 
}

[[nodiscard]] auto Graph_plotter::widget_to_sample(float x, float y) const -> ImVec2
{
    return map(ImVec2{x, y}, m_bb, m_view); 
}

void Graph_plotter::add_line(ImDrawList* draw_list, float x0, float y0, float x1, float y1, ImU32 color, float thickness) const
{
    draw_list->AddLine(sample_to_widget(x0, y0), sample_to_widget(x1, y1), color, thickness);
}

void Graph_plotter::path_line_to(ImDrawList* draw_list, float x, float y) const
{
    draw_list->PathLineTo(sample_to_widget(x, y));
}

auto Graph_plotter::begin() -> bool
{
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    if (Window->SkipItems) {
        return false;
    }

    if (ImGui::IsWindowHovered()) {
        const auto& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ImVec2 delta_in_view = scale(ImVec2{io.MouseDelta.x, io.MouseDelta.y}, m_bb, m_view); 
            m_pan -= delta_in_view;
        }

        if (io.MouseWheel != 0.0f) {
            m_zoom_scale.x -= io.MouseWheel * 0.05f * m_zoom_scale.x;
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            float distance = io.MouseDelta.y;
            m_zoom_scale.x += 0.005f * distance * m_zoom_scale.x;
        }
    }

    ImGui::SliderFloat("Zoom Scale", &m_zoom_scale.x, 0.0001f, 10000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);

    if (m_zoom_scale.x < 0.0001f) {
        m_zoom_scale.x = 0.0001f;
    }
    if (m_zoom_scale.x > 10000.0f) {
        m_zoom_scale.x = 10000.0f;
    }
    m_zoom_scale.y = m_zoom_scale.x;

    ImVec2 canvas = ImGui::GetContentRegionAvail();
    m_bb = ImRect{Window->DC.CursorPos, Window->DC.CursorPos + canvas};
    const float width  = m_bb.GetWidth();
    const float height = m_bb.GetHeight();
    float aspect = (width != 0.0f && height != 0.0f) ? m_bb.GetWidth() / m_bb.GetHeight()  : 1.0f;
    if (!std::isfinite(aspect)) {
        aspect = 1.0f;
    }
    m_view = ImRect{-1.0f * aspect, 1.0f, 1.0f * aspect, -1.0}; 
    m_view.Min.x *= m_zoom_scale.x;
    m_view.Min.y *= m_zoom_scale.y;
    m_view.Max.x *= m_zoom_scale.x;
    m_view.Max.y *= m_zoom_scale.y;
    m_view.Translate(m_pan);

    ImGui::ItemSize(m_bb);
    if (!ImGui::ItemAdd(m_bb, 0)) {
        return false;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::RenderFrame(m_bb.Min, m_bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg, 1), true, style.FrameRounding);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(m_bb.Min, m_bb.Max, true);

    // Draw grid
    float grid_spacing = 0.1f;
    {
        float grid_interval = grid_spacing;
        float view_size = std::max(m_view.GetWidth(), m_view.GetHeight());
        while (view_size / grid_interval > 100.0f) {
            grid_interval *= 10.0f;
        }
        while (view_size / grid_interval < 10.0f) {
            grid_interval *= 0.1f;
        }
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_Separator);

        for (float x = grid_interval; x <= m_view.Max.x; x += grid_interval) {
            if (x < m_view.Min.x) continue;
            add_line(draw_list, x, m_view.Min.y, x, m_view.Max.y, color);
        }
        for (float x = -grid_interval; x >= m_view.Min.x; x -= grid_interval) {
            if (x > m_view.Max.x) continue;
            add_line(draw_list, x, m_view.Min.y, x, m_view.Max.y, color);
        }
        for (float y = grid_interval; y <= m_view.Min.y; y += grid_interval) {
            if (y < m_view.Max.y) continue;
            add_line(draw_list, m_view.Min.x, y, m_view.Max.x, y, color);
        }
        for (float y = -grid_interval; y >= m_view.Max.y; y -= grid_interval) {
            if (y > m_view.Min.y) continue;
            add_line(draw_list, m_view.Min.x, y, m_view.Max.x, y, color);
        }
        add_line(draw_list, 0.0f, m_view.Min.y, 0.0f, m_view.Max.y, ImGui::GetColorU32(ImGuiCol_SeparatorActive));
        add_line(draw_list, m_view.Min.x, 0.0f, m_view.Max.x, 0.0f, ImGui::GetColorU32(ImGuiCol_SeparatorActive));
    }
    return true;
}

void Graph_plotter::plot(const Graph<ImVec2>& graph)
{
    if (!graph.plot) {
        return;
    }
    const auto& io = ImGui::GetIO();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    std::optional<std::size_t> hover_sample;
    const auto& samples = graph.samples;
    if (ImGui::IsMouseHoveringRect(m_bb.Min, m_bb.Max)) {
        ImVec2 hover_pos{io.MousePos.x, io.MousePos.y};
        float min_dx = std::numeric_limits<float>::max();
        for (size_t i = 0, end = samples.size(); i < end; ++i) {
            ImVec2 sample = samples.at(i);
            ImVec2 p = sample_to_widget(sample.x, sample.y * graph.y_scale);
            float dx = p.x - hover_pos.x;
            //float dy = p.y - hover_pos.y;
            //float distance_squared = dx * dx + dy * dy;
            if (std::abs(dx) < min_dx) {
                min_dx = std::abs(dx);
                hover_sample = i;
            }
        }
    }
    if (graph.draw_path) {
        draw_list->PathClear();
        for (size_t i = 0, end = samples.size(); i < end; ++i) {
            ImVec2 sample = samples.at(i);
            path_line_to(draw_list, sample.x, sample.y * graph.y_scale);
        }
        draw_list->PathStroke(graph.path_color, ImDrawFlags_None, graph.path_thickness);
    }
    if (graph.draw_keys) {
        for (size_t i = 0, end = samples.size(); i < end; ++i) {
            ImVec2 sample = samples.at(i);
            ImVec2 p = sample_to_widget(sample.x, sample.y * graph.y_scale);
            draw_list->AddRect(
                p - ImVec2(4.0f, 4.0f),
                p + ImVec2(4.0f, 4.0f),
                hover_sample.has_value() && (hover_sample.value() == i) ? graph.hover_color : graph.key_color,
                0.0f,
                ImDrawFlags_None,
                2.0f
            );
        }
    }
    if (hover_sample.has_value()) {
        if (ImGui::BeginTooltip()) {
            ImVec2 sample = samples.at(hover_sample.value());
            std::string tooltip = fmt::format(
                "Sample {}: {} = {}{}, {} = {}{}",
                hover_sample.value(),
                graph.x_label, sample.x, graph.x_unit,
                graph.y_label, sample.y, graph.y_unit
            );
            ImGui::TextUnformatted(tooltip.c_str());
            ImGui::EndTooltip();
        }
    }
}

void Graph_plotter::plot(const Graph<std::array<ImVec2, 2>>& graph)
{
    if (!graph.plot) {
        return;
    }
    const auto& io = ImGui::GetIO();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    class Hover_entry
    {
    public:
        std::size_t sample;
        std::size_t index;
    };
    std::optional<Hover_entry> hover_entry;

    const auto& samples = graph.samples;
    if (ImGui::IsMouseHoveringRect(m_bb.Min, m_bb.Max)) {
        ImVec2 hover_pos{io.MousePos.x, io.MousePos.y};
        float min_dx = std::numeric_limits<float>::max();
        bool segment_found = false;
        for (size_t i = 0, end = samples.size(); i < end; ++i) {
            const auto& sample = samples.at(i);
            ImVec2 p[2] = {
                sample_to_widget(sample[0].x, sample[0].y * graph.y_scale),
                sample_to_widget(sample[1].x, sample[1].y * graph.y_scale)
            };
            float dx0 = std::abs(p[0].x - hover_pos.x);
            float dx1 = std::abs(p[1].x - hover_pos.x);
            float dx  = std::min(dx0, dx1);
            if ((hover_pos.x >= p[0].x) && (hover_pos.y <= p[1].x)) {
                segment_found = true;
                float dy0                = p[0].y - hover_pos.y;
                float dy1                = p[1].y - hover_pos.y;
                float distance_squared_0 = dx0 * dx0 + dy0 * dy0;
                float distance_squared_1 = dx1 * dx1 + dy1 * dy1;
                hover_entry = Hover_entry{
                    i,
                    (distance_squared_0 < distance_squared_1) ? std::size_t{0} : std::size_t{1}
                };
            } else if (!segment_found && (dx < min_dx)) {
                min_dx = dx;
                float dy0                = p[0].y - hover_pos.y;
                float dy1                = p[1].y - hover_pos.y;
                float distance_squared_0 = dx0 * dx0 + dy0 * dy0;
                float distance_squared_1 = dx1 * dx1 + dy1 * dy1;
                hover_entry = Hover_entry{
                    i,
                    (distance_squared_0 < distance_squared_1) ? std::size_t{0} : std::size_t{1}
                };
            }
        }
    }
    if (graph.draw_path) {
        for (size_t i = 0, end = samples.size(); i < end; ++i) {
            auto sample = samples.at(i);
            ImVec2 p[2] = {
                sample_to_widget(sample[0].x, sample[0].y * graph.y_scale),
                sample_to_widget(sample[1].x, sample[1].y * graph.y_scale)
            };
            draw_list->AddLine(p[0], p[1], graph.path_color, graph.path_thickness);
        }
    }
    if (graph.draw_keys) {
        for (size_t i = 0, end = samples.size(); i < end; ++i) {
            const auto& sample = samples.at(i);
            for (size_t j = 0; j < 2; ++j) {
                if (hover_entry.has_value() && (hover_entry.value().sample == i) && (hover_entry.value().index == j)) {
                    continue;
                }
                ImVec2 p = sample_to_widget(sample[j].x, sample[j].y * graph.y_scale);
                draw_list->AddRect(
                    p - ImVec2(4.0f, 4.0f),
                    p + ImVec2(4.0f, 4.0f),
                    graph.key_color,
                    0.0f,
                    ImDrawFlags_None,
                    2.0f
                );
            }
        }
    }
    if (hover_entry.has_value()) {
        size_t i = hover_entry.value().sample;
        size_t j = hover_entry.value().index;
        const auto& sample = samples.at(i);
        ImVec2 p = sample_to_widget(sample[j].x, sample[j].y * graph.y_scale);
        draw_list->AddRect(
            p - ImVec2(4.0f, 4.0f),
            p + ImVec2(4.0f, 4.0f),
            graph.hover_color,
            0.0f,
            ImDrawFlags_None,
            2.0f
        );
        if (ImGui::BeginTooltip()) {
            std::string tooltip = fmt::format(
                "Sample {}.{}: {} = {}{}, {} = {}{}",
                hover_entry.value().sample,
                j,
                graph.x_label, sample[j].x, graph.x_unit,
                graph.y_label, sample[j].y, graph.y_unit
            );
            ImGui::TextUnformatted(tooltip.c_str());
            ImGui::EndTooltip();
        }
    }
}

void Graph_plotter::sample_text(float x, float y, const char* text, ImU32 col)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = sample_to_widget(x, y);

    draw_list->AddRect(
        p - ImVec2(4.0f, 4.0f),
        p + ImVec2(4.0f, 4.0f),
        col,
        0.0f,
        ImDrawFlags_None,
        2.0f
    );

    ImVec2 q = ImVec2{p.x + 8.0f, p.y};
    draw_list->AddText(q, col, text);
}

void Graph_plotter::sample_x_line(float x, ImU32 col, float thickness)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    add_line(draw_list, x, m_view.Min.y, x, m_view.Max.y, col, thickness);
}

void Graph_plotter::sample_y_line(float y, ImU32 col, float thickness)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    add_line(draw_list, m_view.Min.x, y, m_view.Max.x, y, col, thickness);
}

void Graph_plotter::end()
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PopClipRect();
}

} // namespace erhe::imgui
