#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "windows/rendergraph_window.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <vector>
#include <string>

namespace editor {

Rendergraph_window::Rendergraph_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, Editor_context& editor_context)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Render Graph", "rendergraph"}
    , m_context                {editor_context}
{
}

Rendergraph_window::~Rendergraph_window() noexcept
{
}

auto Rendergraph_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

namespace {

auto get_connection_color(const int key) -> ImVec4
{
    switch (key) {
        case erhe::rendergraph::Rendergraph_node_key::window:              return ImVec4{0.4f, 0.5f, 0.8f, 1.0f};
        case erhe::rendergraph::Rendergraph_node_key::viewport:            return ImVec4{0.8f, 1.0f, 0.2f, 1.0f};
        case erhe::rendergraph::Rendergraph_node_key::shadow_maps:         return ImVec4{0.6f, 0.6f, 0.6f, 1.0f};
        case erhe::rendergraph::Rendergraph_node_key::depth_visualization: return ImVec4{0.1f, 0.8f, 0.8f, 1.0f};
        default: return ImVec4{1.0f, 0.0f, 1.0f, 1.0f};
    }
}

}

void Rendergraph_window::imgui()
{
#if 0
    const ImGuiTreeNodeFlags parent_flags{
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };
    //const ImGuiTreeNodeFlags leaf_flags{
    //    ImGuiTreeNodeFlags_SpanFullWidth    |
    //    ImGuiTreeNodeFlags_NoTreePushOnOpen |
    //    ImGuiTreeNodeFlags_Leaf
    //};

    if (ImGui::TreeNodeEx("Render Graph", parent_flags)) {
        const auto& render_graph_nodes = m_render_graph->get_nodes();
        for (const auto& node : render_graph_nodes) {
            if (ImGui::TreeNodeEx(node->get_name().c_str(), parent_flags | ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto& inputs = node->get_inputs();
                for (const auto& input : inputs) {
                    const auto& producer_node = input.producer_node.lock();
                    if (producer_node) {
                        ImGui::Text(
                            "Input '%s' Producer %s (%s)",
                            input.key.c_str(),
                            producer_node->get_name().c_str(),
                            c_str(input.resource_routing)
                        );
                    }
                }

                const auto& outputs = node->get_outputs();
                for (const auto& output : outputs) {
                    const auto& consumer_node = output.consumer_node.lock();
                    if (consumer_node) {
                        ImGui::Text(
                            "Output '%s' Consumer %s (%s)",
                            output.key.c_str(),
                            consumer_node->get_name().c_str(),
                            c_str(output.resource_routing)
                        );
                    }
                }

                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }

    const auto& scene_roots = m_editor_scenes->get_scene_roots();
    if (ImGui::TreeNodeEx("Scenes", parent_flags)) {
        for (const auto& scene_root : scene_roots) {
            ImGui::Text("%s", scene_root->get_name().c_str());
            //if (ImGui::TreeNodeEx(scene_root->name().c_str(), parent_flags)) {
            //    ImGui::TreePop();
            //}
        }
        ImGui::TreePop();
    }
#endif
    if (!m_node_editor) {
        ax::NodeEditor::Config config;
        m_node_editor = std::make_unique<ax::NodeEditor::EditorContext>(nullptr);
    }

    auto& rendergraph = *m_context.rendergraph;

    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderFloat("Image Size", &m_image_size, 4.0f, 1000.0f);
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderFloat("Curve Strength", &m_curve_strength, 0.0f, 100.0f);
    ImGui::SetNextItemWidth(400.0f);
    const bool x_gap_changed = ImGui::SliderFloat("X Gap", &rendergraph.x_gap, 0.0f, 200.0f);
    ImGui::SetNextItemWidth(400.0f);
    const bool y_gap_changed = ImGui::SliderFloat("Y Gap", &rendergraph.y_gap, 0.0f, 200.0f);
    if (ImGui::Button("Automatic Layout") || x_gap_changed || y_gap_changed) {
        rendergraph.automatic_layout(m_image_size);
    }

    m_node_editor->Begin("Rendergraph", ImVec2{0.0f, 0.0f});

    const float zoom = 1.0f; //m_node_editor->GetCurrentZoom();
    const auto& render_graph_nodes = rendergraph.get_nodes();

    for (auto* node : render_graph_nodes) {
        if (node->is_enabled()) {
            ImGui::Text("Execute render graph node '%s'", node->get_name().c_str());
        } else {
            ImGui::Text("Disabled render graph node '%s'", node->get_name().c_str());
        }
    }

    //// m_imnodes_context->PushStyleVar(ImNodesStyleVar_CurveStrength, m_curve_strength);
    for (auto* node : render_graph_nodes) {
        log_graph_editor->info("Node {}", node->get_id());

        // Start rendering node
        const auto   glm_position = node->get_position();
        const ImVec2 start_position{glm_position.x, glm_position.y};
        const bool   start_selected = node->get_selected();
        ImVec2       position = start_position;
        bool         selected = start_selected;
        const std::string shortLabel = node->get_name().substr(0, 12);
        const std::string fullLabel = fmt::format("{}: {} ", node->get_depth(), node->get_name());
        const auto& inputs  = node->get_inputs();
        const auto& outputs = node->get_outputs();

        const ax::NodeEditor::NodeId node_id{node->get_id()};
        ImGui::PushID(static_cast<int>(node->get_id()));
        ERHE_DEFER( ImGui::PopID(); );

        m_node_editor->BeginNode(node_id);

        ImVec2 pin_table_size{200.0f, 0.0f};

        // Input pins
        ImGui::BeginTable("##InputPin", 2, ImGuiTableFlags_None, pin_table_size);
        ImGui::TableSetupColumn("InputPin",   ImGuiTableColumnFlags_WidthFixed, 20.0f);
        ImGui::TableSetupColumn("InputLabel", ImGuiTableColumnFlags_None);
        for (const auto& input : inputs) {
            log_graph_editor->info("  Input {} {}", input.id.get_id(), input.label);
            const ax::NodeEditor::PinId pin_id{input.id.get_id()};
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            m_node_editor->BeginPin(pin_id, ax::NodeEditor::PinKind::Input);
            ImGui::Bullet();
            m_node_editor->EndPin();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(input.label.c_str());
        }
        ImGui::EndTable();

        ImGui::SameLine();

        // Content
        ImGui::SameLine();
        for (const auto& output : outputs) {
            const auto& texture = node->get_producer_output_texture(output.resource_routing, output.key);
            if (
                texture &&
                (texture->target() == gl::Texture_target::texture_2d) &&
                (texture->width () >= 1) &&
                (texture->height() >= 1) &&
                (gl_helpers::has_color(texture->internal_format()))
            ) {
                const float aspect = static_cast<float>(texture->width()) / static_cast<float>(texture->height());
                m_imgui_renderer.image(
                    texture,
                    static_cast<int>(zoom * aspect * m_image_size),
                    static_cast<int>(zoom * m_image_size),
                    glm::vec2{0.0f, 1.0f},
                    glm::vec2{1.0f, 0.0f},
                    glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
                    glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                    false
                );
                //std::string text = fmt::format(
                //    "O: {} Size: {} x {} Format: {}",
                //    texture->debug_label(),
                //    texture->width(), texture->height(),
                //    gl::c_str(texture->internal_format())
                //);
                //ImGui::TextUnformatted(text.c_str());
                //if (ImGui::IsItemHovered()) {
                //    ImGui::Text("%s @ depth %d", output.label.c_str(), node->get_depth());
                //    std::string size = fmt::format("Size: {} x {}", texture->width(), texture->height(), gl::c_str(texture->internal_format()));
                //    std::string format = fmt::format("Format: {}", gl::c_str(texture->internal_format()));
                //    ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_None);
                //    ImGui::TextUnformatted(size.c_str());
                //    ImGui::TextUnformatted(format.c_str());
                //    ImGui::EndTooltip();
                //}
            }
        }

        ImGui::SameLine();

        // Output pins
        ImGui::BeginTable("##Outputs", 2, ImGuiTableFlags_None, pin_table_size);
        ImGui::TableSetupColumn("OutputLabel", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("OutputPin",   ImGuiTableColumnFlags_WidthFixed, 20.0f);
        for (const auto& output : outputs) {
            log_graph_editor->info("  Output {} {}", output.id.get_id(), output.label);
            const ax::NodeEditor::PinId pin_id{output.id.get_id()};
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(output.label.c_str());

            ImGui::TableSetColumnIndex(1);
            m_node_editor->BeginPin(pin_id, ax::NodeEditor::PinKind::Output);
            ImGui::Bullet();
            m_node_editor->EndPin();
        }
        ImGui::EndTable(); // Outputs

        m_node_editor->EndNode();

        if ((position.x != start_position.x) || (position.y != start_position.y)) {
            node->set_position(glm::vec2{position.x, position.y});
        }
        if (selected != start_selected) {
            node->set_selected(selected);
        }
    }

    // Links
    for (auto* node : render_graph_nodes) {
        // Render output connections of this node
        const auto& outputs = node->get_outputs();
        for (const auto& output : outputs) {
            const ax::NodeEditor::PinId pin_id{output.id.get_id()};
            for (auto* consumer : output.consumer_nodes) {
                if (consumer == nullptr) {
                    continue;
                }
                const erhe::rendergraph::Rendergraph_consumer_connector* consumer_input = consumer->get_input(output.resource_routing, output.key);

                const ax::NodeEditor::LinkId link_id{consumer_input};
                const ax::NodeEditor::PinId input_pin_id{consumer_input->id.get_id()};
                const ax::NodeEditor::PinId output_pin_id{output.id.get_id()};

                log_graph_editor->info("  Link {} to {}", output.id.get_id(), consumer_input->id.get_id());

                const bool connection_ok = m_node_editor->Link(link_id, output_pin_id, input_pin_id);
                if (!connection_ok) {
                    log_scene->info("Connection delete");
                }
            }
        }
    }

    if (ImGui::IsMouseReleased(1) && ImGui::IsWindowHovered() && !ImGui::IsMouseDragging(1)) {
        ImGui::FocusWindow(ImGui::GetCurrentWindow());
        ImGui::OpenPopup("NodesContextMenu");
    }

    if (ImGui::BeginPopup("NodesContextMenu")) {
        //// for (const auto& desc : available_nodes) {
        ////     if (ImGui::MenuItem(desc.first.c_str())) {
        ////         nodes.push_back(desc.second());
        ////         ImNodes::AutoPositionNode(nodes.back());
        ////     }
        //// }

        ImGui::Separator();
        if (ImGui::MenuItem("Reset Zoom TODO")) {
            // m_imnodes_context->GetCanvas().m_Zoom = 1;
        }

        if (ImGui::IsAnyMouseDown() && !ImGui::IsWindowHovered()) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    m_node_editor->End();
}

} // namespace editor
