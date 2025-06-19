#include "windows/post_processing_window.hpp"

#include "editor_context.hpp"

#include "rendergraph/post_processing.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

#include <fmt/format.h>

namespace editor {

Post_processing_window::Post_processing_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, Editor_context& editor_context)
    : Imgui_window{imgui_renderer, imgui_windows, "Post Processing", "post_processing"}
    , m_context   {editor_context}
{
}

void Post_processing_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    const auto viewport_scene_view = m_context.scene_views->last_scene_view();
    if (!viewport_scene_view) {
        return;
    }
    Post_processing_node* node = viewport_scene_view->get_post_processing_node();
    if (node == nullptr) {
        return;
    }
    bool edited = false;
    if (ImGui::IsItemEdited()) edited = true;
    ImGui::SliderFloat("Size", &m_size, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::RadioButton("Nearest", !m_linear_filter)) { m_linear_filter = false; }
    if (ImGui::RadioButton("Linear", m_linear_filter)) { m_linear_filter = true; }

    auto& weights = node->weights;
    size_t level_count = node->level_widths.size();

    ImGui::DragInt("LowPass Count", &node->lowpass_count, 0.01f, 0, 8);

    ImGui::SliderFloat("I_max", &node->tonemap_luminance_max,  0.0f, 10.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) edited = true;
    ImGui::SliderFloat("alpha", &node->tonemap_alpha,  0.0f, 1.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) edited = true;

    static float c0 =  0.12f; //1.0 / 128.0f;
    static float c1 =  0.05f; //1.0 / 256.0f;
    static float c2 = -1.0f;  // -2.00f;
    static float c3 =  2.0f;  //  3.00f;
    ImGui::SliderFloat("c0", &c0,  0.0f, 1.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) edited = true;
    ImGui::SliderFloat("c1", &c1,  0.0f, 1.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) edited = true;
    ImGui::SliderFloat("c2", &c2, -4.0f, 4.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) edited = true;
    ImGui::SliderFloat("c3", &c3,  0.0f, 4.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) edited = true;
    if (edited) {
        weights[0] = c0;
        weights[1] = c0;
        float max_value = 0.0f;
        for (size_t i = 2, end = weights.size(); i < end; ++i) {
            float l = static_cast<float>(1 + level_count - i);
            float w = c3 * std::pow(l, c2);
            weights[i] = 1.0f - w;
            max_value = std::max(max_value, weights[i]);
        }
        float margin = max_value - 1.0f;
        for (size_t i = 2, end = weights.size(); i < end; ++i) {
            weights[i] = weights[i] - margin - c1;
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
    for (size_t source_level : node->downsample_source_levels) {
        size_t destination_level = source_level + 1;
        auto& texture = node->downsample_texture_views.at(destination_level);
        if (!texture || (texture->get_width() < 1) || (texture->get_height() < 1)) {
            continue;
        }

        int width  = static_cast<int>(m_size * static_cast<float>(node->level_widths.at(0)));
        int height = static_cast<int>(m_size * static_cast<float>(node->level_heights.at(0)));
        draw_image(texture, width, height, m_linear_filter);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(texture->get_debug_label().c_str());
            ImGui::EndTooltip();
        }
    }
    for (size_t source_level : node->upsample_source_levels) {
        size_t destination_level = source_level - 1;
        auto& texture = node->upsample_texture_views.at(destination_level);
        if (!texture || (texture->get_width() < 1) || (texture->get_height() < 1)) {
            continue;
        }
    
        int width  = static_cast<int>(m_size * static_cast<float>(node->level_widths.at(0)));
        int height = static_cast<int>(m_size * static_cast<float>(node->level_heights.at(0)));
        draw_image(texture, width, height, m_linear_filter);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(texture->get_debug_label().c_str());
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(source_level));
        ImGui::SliderFloat("##", &node->weights[source_level], 0.0f, 1.0f, "%.4f");
        if (ImGui::IsItemEdited()) edited = true;
        ImGui::PopID();
    }
    ImGui::PopStyleVar();

    if (edited) {
        node->update_parameters();
    }
#endif
}

} // namespace editor
