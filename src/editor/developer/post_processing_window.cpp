#include "developer/post_processing_window.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"

#include "rendergraph/post_processing.hpp"
#include "scene/viewport_scene_views.hpp"

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/resource_routing.hpp"

#include <imgui/imgui.h>

#include <fmt/format.h>

namespace editor {

Post_processing_window::Post_processing_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "Post Processing", "post_processing", true}
    , m_context   {app_context}
{
}

void Post_processing_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    // log_frame->trace("Post_processing_window::imgui()");

    bool edited = false;
    const std::vector<std::shared_ptr<Post_processing_node>>& nodes = m_context.scene_views->get_post_processing_nodes();
    if (!nodes.empty()) {
        if (m_post_processing_node.expired()) {
            m_post_processing_node = nodes.front();
            m_selection = 0;
        }
        int last_index = static_cast<int>(nodes.size() - 1);
        edited = ImGui::SliderInt("Viewport", &m_selection, 0, last_index);
        if (edited && (m_selection >= 0) && (m_selection < nodes.size())) {
            m_post_processing_node = nodes.at(m_selection);
        }
    }

    std::shared_ptr<Post_processing_node> node = m_post_processing_node.lock();
    if (!node) {
        return;
    }

    ImGui::SliderFloat("Size", &m_size, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::RadioButton("Nearest", !m_linear_filter)) { m_linear_filter = false; }
    if (ImGui::RadioButton("Linear", m_linear_filter)) { m_linear_filter = true; }

    // The pyramid and its input are built and replaced by the render phase
    // (Post_processing_node::update_size), which may run while this window
    // draws: the window reads the layout the render phase published last (a
    // one-frame-late copy) and draws the textures through references the
    // render thread resolves.
    node->get_pyramid_info(m_pyramid_info);
    update_texture_references(node);

    // The live parameters (main thread); the node copies them into the
    // frame being prepared (Post_processing_node::prepare_rendergraph_node),
    // so an edit here reaches the next frame's render phase.
    Post_processing_parameters& parameters = node->parameters;
    size_t level_count = m_pyramid_info.level_widths.size();

    if (level_count == 0) {
        // update_size() has not populated the pyramid yet -- e.g. first frame
        // before the rendergraph has executed, or the input viewport texture
        // is not yet connected. Sliders that touch per-level weights would
        // index out of range; thumbnails would have nothing to display.
        ImGui::TextUnformatted("Post processing node is not yet sized.");
        return;
    }

    ImGui::DragInt("LowPass Count", &parameters.lowpass_count, 0.01f, 0, 8);

    ImGui::SliderFloat("I_max", &parameters.tonemap_luminance_max,  0.0f, 10.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    ImGui::SliderFloat("alpha", &parameters.tonemap_alpha,  0.0f, 1.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);

    ImGui::SliderFloat("c0", &parameters.weight_c0,  0.0f, 1.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) edited = true;
    ImGui::SliderFloat("c1", &parameters.weight_c1,  0.0f, 1.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) edited = true;
    ImGui::SliderFloat("c2", &parameters.weight_c2, -4.0f, 4.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) edited = true;
    ImGui::SliderFloat("c3", &parameters.weight_c3,  0.0f, 4.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) edited = true;
    // The per-level weights follow the curve when it is edited, and when the
    // pyramid the render phase published has a different level count (a
    // resize rebuilt it), so the per-level sliders below edit this pyramid.
    if (edited || (parameters.weights.size() != level_count)) {
        compute_post_processing_weights(parameters, level_count, parameters.weights);
    }

    // Diagnostic header: surface state the user cannot otherwise see, so a
    // black viewport can be bisected visually against the thumbnails below.
    ImGui::Text(
        "level0: %d x %d   levels: %zu   downsample_textures: %zu   upsample_textures: %zu",
        m_pyramid_info.level0_width, m_pyramid_info.level0_height, level_count,
        m_pyramid_info.downsample_count,
        m_pyramid_info.upsample_count
    );
    ImGui::Text(
        "input: '%.*s'  %dx%d",
        static_cast<int>(m_pyramid_info.input_label.size()),
        m_pyramid_info.input_label.data(),
        m_pyramid_info.level0_width,
        m_pyramid_info.level0_height
    );

    const int display_width  = static_cast<int>(m_size * static_cast<float>(m_pyramid_info.level_widths [0]));
    const int display_height = static_cast<int>(m_size * static_cast<float>(m_pyramid_info.level_heights[0]));
    const erhe::graphics::Filter filter = m_linear_filter
        ? erhe::graphics::Filter::linear
        : erhe::graphics::Filter::nearest;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});

    // Input thumbnail: what the forward renderer handed post-processing.
    m_context.imgui_renderer->image(
        erhe::imgui::Draw_texture_parameters{
            .texture_reference = m_input_reference,
            .width             = display_width,
            .height            = display_height,
            .uv0               = m_context.imgui_renderer->get_rtt_uv0(),
            .uv1               = m_context.imgui_renderer->get_rtt_uv1(),
            .filter            = filter,
            .debug_label       = erhe::utility::Debug_label{"post_processing input"}
        }
    );
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text(
            "input '%.*s' %dx%d",
            static_cast<int>(m_pyramid_info.input_label.size()),
            m_pyramid_info.input_label.data(),
            m_pyramid_info.level0_width,
            m_pyramid_info.level0_height
        );
        ImGui::EndTooltip();
    }

    // Downsample row: one thumbnail per downsample_textures[k] (pyramid level k+1).
    for (size_t k = 0; k < m_downsample_references.size(); ++k) {
        m_context.imgui_renderer->image(
            erhe::imgui::Draw_texture_parameters{
                .texture_reference = m_downsample_references[k],
                .width             = display_width,
                .height            = display_height,
                .uv0               = m_context.imgui_renderer->get_rtt_uv0(),
                .uv1               = m_context.imgui_renderer->get_rtt_uv1(),
                .filter            = filter,
                .debug_label       = erhe::utility::Debug_label{"post_processing downsample"}
            }
        );
        if (ImGui::IsItemHovered() && (k + 1 < level_count)) {
            ImGui::BeginTooltip();
            ImGui::Text(
                "downsample[%zu] (pyramid level %zu) %dx%d",
                k, k + 1, m_pyramid_info.level_widths[k + 1], m_pyramid_info.level_heights[k + 1]
            );
            ImGui::EndTooltip();
        }
    }

    // Upsample row: one thumbnail per upsample_textures[k] (pyramid level k).
    // The final (smallest index printed last, largest size) is pyramid level 0
    // == the post-processed image that the viewport consumer samples.
    for (size_t k = m_upsample_references.size(); k > 0; --k) {
        const size_t index = k - 1;
        m_context.imgui_renderer->image(
            erhe::imgui::Draw_texture_parameters{
                .texture_reference = m_upsample_references[index],
                .width             = display_width,
                .height            = display_height,
                .uv0               = m_context.imgui_renderer->get_rtt_uv0(),
                .uv1               = m_context.imgui_renderer->get_rtt_uv1(),
                .filter            = filter,
                .debug_label       = erhe::utility::Debug_label{"post_processing upsample"}
            }
        );
        if (ImGui::IsItemHovered() && (index < level_count)) {
            ImGui::BeginTooltip();
            ImGui::Text(
                "upsample[%zu] (pyramid level %zu) %dx%d",
                index, index, m_pyramid_info.level_widths[index], m_pyramid_info.level_heights[index]
            );
            ImGui::EndTooltip();
        }
        // Per-level weight slider maps to upsample_source_levels entry that
        // writes this level. upsample_source_levels is ordered deepest->shallowest;
        // index maps to source_level = index + 1 (destination_level = index).
        const size_t source_level = index + 1;
        if (source_level < parameters.weights.size()) {
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(source_level));
            ImGui::SliderFloat("##Post_processing_window::imgui()", &parameters.weights[source_level], 0.0f, 1.0f, "%.4f");
            ImGui::PopID();
        }
    }
    ImGui::PopStyleVar();

    // The edited parameters reach the GPU in the next frame: its prepare
    // phase copies them (Post_processing_node::prepare_rendergraph_node) and
    // its render phase uploads the copy (update_parameters).
}

void Post_processing_window::update_texture_references(const std::shared_ptr<Post_processing_node>& node)
{
    // Rebuilt only when the node or the published level counts change.
    if (
        (m_references_node.lock() == node) &&
        m_input_reference &&
        (m_downsample_references.size() == m_pyramid_info.downsample_count) &&
        (m_upsample_references.size()   == m_pyramid_info.upsample_count)
    ) {
        return;
    }
    m_references_node = node;
    m_input_reference = std::make_shared<Post_processing_node_texture_reference>(node, Post_processing_texture_role::input, 0);
    m_downsample_references.clear();
    for (size_t level = 0; level < m_pyramid_info.downsample_count; ++level) {
        m_downsample_references.push_back(std::make_shared<Post_processing_node_texture_reference>(node, Post_processing_texture_role::downsample, level));
    }
    m_upsample_references.clear();
    for (size_t level = 0; level < m_pyramid_info.upsample_count; ++level) {
        m_upsample_references.push_back(std::make_shared<Post_processing_node_texture_reference>(node, Post_processing_texture_role::upsample, level));
    }
}

}
