#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "texture_graph/texture_graph_widgets.hpp"
#include "texture_graph/texture_graph_node.hpp" // texture_enum_stepper

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>

namespace editor {

using erhe::texgen::Gradient_stop;
using erhe::texgen::Gradient_interpolation;
using erhe::texgen::Curve_point;

namespace {

constexpr float pi = 3.14159265359f;

[[nodiscard]] auto clamp01(const float v) -> float
{
    return (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;
}

// Evaluates the gradient at x for the display ramp. Alloc-free: the bracketing
// stops are found by linear scan (the stored stops are not kept sorted), so the
// per-frame node draw performs no heap allocation. Cubic is displayed as
// smoothstep (the true cubic ramp shows on the node's rendered thumbnail).
[[nodiscard]] auto evaluate_gradient(
    const std::vector<Gradient_stop>& stops,
    const Gradient_interpolation      interpolation,
    const float                       x
) -> ImVec4
{
    if (stops.empty()) {
        return ImVec4{0.0f, 0.0f, 0.0f, 1.0f};
    }
    const Gradient_stop* left  = nullptr;
    const Gradient_stop* right = nullptr;
    for (const Gradient_stop& s : stops) {
        if ((s.position <= x) && ((left  == nullptr) || (s.position > left->position ))) { left  = &s; }
        if ((s.position >= x) && ((right == nullptr) || (s.position < right->position))) { right = &s; }
    }
    if (left  == nullptr) { return ImVec4{right->color[0], right->color[1], right->color[2], right->color[3]}; }
    if (right == nullptr) { return ImVec4{left->color[0],  left->color[1],  left->color[2],  left->color[3]};  }
    if ((left == right) || (right->position <= left->position)) {
        return ImVec4{left->color[0], left->color[1], left->color[2], left->color[3]};
    }
    float t = (x - left->position) / (right->position - left->position);
    if (interpolation == Gradient_interpolation::constant) {
        return ImVec4{left->color[0], left->color[1], left->color[2], left->color[3]};
    }
    if ((interpolation == Gradient_interpolation::smoothstep) || (interpolation == Gradient_interpolation::cubic)) {
        t = 0.5f - (0.5f * std::cos(pi * t));
    }
    return ImVec4{
        left->color[0] + ((right->color[0] - left->color[0]) * t),
        left->color[1] + ((right->color[1] - left->color[1]) * t),
        left->color[2] + ((right->color[2] - left->color[2]) * t),
        left->color[3] + ((right->color[3] - left->color[3]) * t)
    };
}

// Evaluates the Hermite curve at x (mirror of the emitted GLSL, for the display
// polyline).
[[nodiscard]] auto evaluate_curve(const std::vector<Curve_point>& points, const float x) -> float
{
    if (points.empty()) {
        return x;
    }
    if (points.size() == 1) {
        return points[0].y;
    }
    std::size_t segment = points.size() - 2;
    for (std::size_t i = 0; (i + 1) < points.size(); ++i) {
        if ((i == (points.size() - 2)) || (x <= points[i + 1].x)) {
            segment = i;
            break;
        }
    }
    const Curve_point& a = points[segment];
    const Curve_point& b = points[segment + 1];
    float d = b.x - a.x;
    if (d < 1.0e-8f) {
        return a.y;
    }
    const float t    = (x - a.x) / d;
    const float omt  = 1.0f - t;
    const float omt2 = omt * omt;
    const float omt3 = omt2 * omt;
    const float t2   = t * t;
    const float t3   = t2 * t;
    d /= 3.0f;
    const float y1  = a.y;
    const float yac = a.y + (d * a.right_slope);
    const float ybc = b.y - (d * b.left_slope);
    const float y2  = b.y;
    return (y1 * omt3) + (yac * omt2 * t * 3.0f) + (ybc * omt * t2 * 3.0f) + (y2 * t3);
}

// Recomputes each point's tangent slopes from its neighbors (Catmull-Rom finite
// difference) so an edited curve stays smooth without tangent-handle editing.
void recompute_curve_slopes(std::vector<Curve_point>& points)
{
    const std::size_t n = points.size();
    for (std::size_t i = 0; i < n; ++i) {
        float slope = 0.0f;
        if (n >= 2) {
            if (i == 0) {
                slope = (points[1].y - points[0].y) / std::max(1.0e-5f, points[1].x - points[0].x);
            } else if (i == (n - 1)) {
                slope = (points[i].y - points[i - 1].y) / std::max(1.0e-5f, points[i].x - points[i - 1].x);
            } else {
                slope = (points[i + 1].y - points[i - 1].y) / std::max(1.0e-5f, points[i + 1].x - points[i - 1].x);
            }
        }
        points[i].left_slope  = slope;
        points[i].right_slope = slope;
    }
}

} // anonymous namespace

auto texture_gradient_editor(
    const char*                  id,
    std::vector<Gradient_stop>&  stops,
    Gradient_interpolation&      interpolation
) -> bool
{
    bool changed = false;
    ImGui::PushID(id);

    ImDrawList*   draw_list = ImGui::GetWindowDrawList();
    ImGuiStorage* storage   = ImGui::GetStateStorage();
    const ImGuiID sel_key   = ImGui::GetID("##sel");
    const ImGuiID drag_key  = ImGui::GetID("##drag");
    int selected = storage->GetInt(sel_key, 0);

    const float  width    = 150.0f;
    const float  bar_h    = 18.0f;
    const float  handle_h = 8.0f;
    const ImVec2 bar_p0   = ImGui::GetCursorScreenPos();

    // Ramp.
    const int segments = 48;
    for (int i = 0; i < segments; ++i) {
        const float x0 = static_cast<float>(i)     / static_cast<float>(segments);
        const float x1 = static_cast<float>(i + 1) / static_cast<float>(segments);
        const ImVec4 c = evaluate_gradient(stops, interpolation, 0.5f * (x0 + x1));
        const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4{c.x, c.y, c.z, 1.0f});
        draw_list->AddRectFilled(
            ImVec2{bar_p0.x + (x0 * width), bar_p0.y},
            ImVec2{bar_p0.x + (x1 * width), bar_p0.y + bar_h},
            col
        );
    }
    draw_list->AddRect(bar_p0, ImVec2{bar_p0.x + width, bar_p0.y + bar_h}, IM_COL32(200, 200, 200, 255));

    // Bar hit area - double-click adds a stop at the cursor.
    ImGui::InvisibleButton("##bar", ImVec2{width, bar_h});
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const float mx = clamp01((ImGui::GetIO().MousePos.x - bar_p0.x) / width);
        const ImVec4 c = evaluate_gradient(stops, interpolation, mx);
        stops.push_back(Gradient_stop{.position = mx, .color = {c.x, c.y, c.z, c.w}});
        selected = static_cast<int>(stops.size()) - 1;
        changed  = true;
    }

    // Stop tick marks on the bar.
    for (std::size_t i = 0; i < stops.size(); ++i) {
        const float hx = bar_p0.x + (clamp01(stops[i].position) * width);
        const bool  is_selected = (static_cast<int>(i) == selected);
        draw_list->AddLine(
            ImVec2{hx, bar_p0.y}, ImVec2{hx, bar_p0.y + bar_h},
            is_selected ? IM_COL32(255, 220, 0, 255) : IM_COL32(20, 20, 20, 255),
            is_selected ? 2.0f : 1.0f
        );
    }

    // Handle strip below the bar - one InvisibleButton, manual drag of the
    // nearest stop (kept in ImGuiStorage across frames so a drag stays on one
    // stop even as the mouse crosses others).
    const ImVec2 strip_p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##handles", ImVec2{width, handle_h + 2.0f});
    const bool strip_active = ImGui::IsItemActive();
    if (ImGui::IsItemActivated() && !stops.empty()) {
        const float mx = (ImGui::GetIO().MousePos.x - strip_p0.x) / width;
        int   nearest      = 0;
        float best_distance = 1.0e9f;
        for (std::size_t i = 0; i < stops.size(); ++i) {
            const float distance = std::fabs(stops[i].position - mx);
            if (distance < best_distance) {
                best_distance = distance;
                nearest       = static_cast<int>(i);
            }
        }
        selected = nearest;
        storage->SetInt(drag_key, nearest);
    }
    const int dragging = storage->GetInt(drag_key, -1);
    if (strip_active && (dragging >= 0) && (dragging < static_cast<int>(stops.size()))) {
        const float nx = clamp01((ImGui::GetIO().MousePos.x - strip_p0.x) / width);
        if (nx != stops[static_cast<std::size_t>(dragging)].position) {
            stops[static_cast<std::size_t>(dragging)].position = nx;
            changed = true;
        }
        selected = dragging;
    }
    if (!strip_active) {
        storage->SetInt(drag_key, -1);
    }
    for (std::size_t i = 0; i < stops.size(); ++i) {
        const float hx = strip_p0.x + (clamp01(stops[i].position) * width);
        const bool  is_selected = (static_cast<int>(i) == selected);
        const ImU32 handle_color = is_selected ? IM_COL32(255, 220, 0, 255) : IM_COL32(180, 180, 180, 255);
        draw_list->AddTriangleFilled(
            ImVec2{hx - 4.0f, strip_p0.y + handle_h},
            ImVec2{hx + 4.0f, strip_p0.y + handle_h},
            ImVec2{hx,        strip_p0.y},
            handle_color
        );
    }

    // Interpolation stepper.
    static const char* const interpolation_names[] = {"Constant", "Linear", "Smoothstep", "Cubic"};
    int interpolation_index = static_cast<int>(interpolation);
    if (texture_enum_stepper("Interp", interpolation_index, interpolation_names, 4)) {
        interpolation = static_cast<Gradient_interpolation>(interpolation_index);
        changed = true;
    }

    // Selected stop color + delete.
    if ((selected >= 0) && (selected < static_cast<int>(stops.size()))) {
        Gradient_stop& stop = stops[static_cast<std::size_t>(selected)];
        const ImVec4 preview{stop.color[0], stop.color[1], stop.color[2], stop.color[3]};
        ImGui::ColorButton("##swatch", preview, ImGuiColorEditFlags_NoTooltip);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat4("##stop_color", stop.color.data(), 0.005f, 0.0f, 1.0f)) {
            changed = true;
        }
        if ((stops.size() > 1) && ImGui::SmallButton("Delete stop")) {
            stops.erase(stops.begin() + selected);
            selected = std::min(selected, static_cast<int>(stops.size()) - 1);
            changed  = true;
        }
    }

    storage->SetInt(sel_key, selected);
    ImGui::PopID();
    return changed;
}

auto texture_curve_editor(
    const char*               id,
    std::vector<Curve_point>& points
) -> bool
{
    bool changed = false;
    ImGui::PushID(id);

    ImDrawList*   draw_list = ImGui::GetWindowDrawList();
    ImGuiStorage* storage   = ImGui::GetStateStorage();
    const ImGuiID sel_key   = ImGui::GetID("##sel");
    const ImGuiID drag_key  = ImGui::GetID("##drag");
    int selected = storage->GetInt(sel_key, -1);

    const float  width  = 150.0f;
    const float  height = 90.0f;
    const ImVec2 p0     = ImGui::GetCursorScreenPos();

    // Box + quarter grid.
    draw_list->AddRectFilled(p0, ImVec2{p0.x + width, p0.y + height}, IM_COL32(24, 24, 24, 255));
    for (int i = 1; i < 4; ++i) {
        const float gx = p0.x + (width  * static_cast<float>(i) / 4.0f);
        const float gy = p0.y + (height * static_cast<float>(i) / 4.0f);
        draw_list->AddLine(ImVec2{gx, p0.y}, ImVec2{gx, p0.y + height}, IM_COL32(50, 50, 50, 255));
        draw_list->AddLine(ImVec2{p0.x, gy}, ImVec2{p0.x + width, gy}, IM_COL32(50, 50, 50, 255));
    }
    draw_list->AddRect(p0, ImVec2{p0.x + width, p0.y + height}, IM_COL32(200, 200, 200, 255));

    // Curve polyline (y up).
    const auto to_screen = [&](const float cx, const float cy) -> ImVec2 {
        return ImVec2{p0.x + (clamp01(cx) * width), p0.y + ((1.0f - clamp01(cy)) * height)};
    };
    const int polyline_samples = 48;
    ImVec2 previous{};
    for (int i = 0; i <= polyline_samples; ++i) {
        const float cx = static_cast<float>(i) / static_cast<float>(polyline_samples);
        const ImVec2 screen = to_screen(cx, evaluate_curve(points, cx));
        if (i > 0) {
            draw_list->AddLine(previous, screen, IM_COL32(120, 205, 255, 255), 1.5f);
        }
        previous = screen;
    }

    // Box hit area for add / remove (double-click) and drag start.
    ImGui::InvisibleButton("##box", ImVec2{width, height});
    const bool box_active = ImGui::IsItemActive();
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        // Nearest existing point in screen space.
        int   nearest       = -1;
        float best_distance = 1.0e9f;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const ImVec2 sp = to_screen(points[i].x, points[i].y);
            const float  distance = std::fabs(sp.x - mouse.x) + std::fabs(sp.y - mouse.y);
            if (distance < best_distance) {
                best_distance = distance;
                nearest       = static_cast<int>(i);
            }
        }
        const bool is_interior = (nearest > 0) && (nearest < (static_cast<int>(points.size()) - 1));
        if ((best_distance < 10.0f) && is_interior) {
            points.erase(points.begin() + nearest);
            recompute_curve_slopes(points);
            selected = -1;
            changed  = true;
        } else {
            const float mx = clamp01((mouse.x - p0.x) / width);
            const float my = clamp01(1.0f - ((mouse.y - p0.y) / height));
            std::size_t insert_at = points.size();
            for (std::size_t i = 0; i < points.size(); ++i) {
                if (mx < points[i].x) {
                    insert_at = i;
                    break;
                }
            }
            points.insert(points.begin() + insert_at, Curve_point{.x = mx, .y = my, .left_slope = 0.0f, .right_slope = 0.0f});
            recompute_curve_slopes(points);
            selected = static_cast<int>(insert_at);
            changed  = true;
        }
    }
    if (ImGui::IsItemActivated()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int   nearest       = -1;
        float best_distance = 1.0e9f;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const ImVec2 sp = to_screen(points[i].x, points[i].y);
            const float  distance = std::fabs(sp.x - mouse.x) + std::fabs(sp.y - mouse.y);
            if (distance < best_distance) {
                best_distance = distance;
                nearest       = static_cast<int>(i);
            }
        }
        selected = (best_distance < 16.0f) ? nearest : -1;
        storage->SetInt(drag_key, selected);
    }
    const int dragging = storage->GetInt(drag_key, -1);
    if (box_active && (dragging >= 0) && (dragging < static_cast<int>(points.size()))) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float  my    = clamp01(1.0f - ((mouse.y - p0.y) / height));
        Curve_point& point = points[static_cast<std::size_t>(dragging)];
        const int    last  = static_cast<int>(points.size()) - 1;
        if ((dragging == 0) || (dragging == last)) {
            point.x = (dragging == 0) ? 0.0f : 1.0f; // endpoints keep their x
        } else {
            const float lo = points[static_cast<std::size_t>(dragging - 1)].x + 0.001f;
            const float hi = points[static_cast<std::size_t>(dragging + 1)].x - 0.001f;
            point.x = std::min(std::max((mouse.x - p0.x) / width, lo), hi);
        }
        point.y = my;
        recompute_curve_slopes(points);
        selected = dragging;
        changed  = true;
    }
    if (!box_active) {
        storage->SetInt(drag_key, -1);
    }

    // Control points.
    for (std::size_t i = 0; i < points.size(); ++i) {
        const ImVec2 sp = to_screen(points[i].x, points[i].y);
        const bool   is_selected = (static_cast<int>(i) == selected);
        draw_list->AddCircleFilled(sp, is_selected ? 4.5f : 3.5f, is_selected ? IM_COL32(255, 220, 0, 255) : IM_COL32(230, 230, 230, 255));
    }

    storage->SetInt(sel_key, selected);
    ImGui::PopID();
    return changed;
}

} // namespace editor
