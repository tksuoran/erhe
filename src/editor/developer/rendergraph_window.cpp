#include "developer/rendergraph_window.hpp"

#include "app_context.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_graph/graph.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>

#include <string>

namespace editor {

Rendergraph_window::Rendergraph_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context)
    : Graph_editor_window_base{imgui_renderer, imgui_windows, "Render Graph", "rendergraph", true}
    , m_context               {app_context}
{
}

Rendergraph_window::~Rendergraph_window() noexcept
{
}

auto Rendergraph_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void Rendergraph_window::controls_imgui()
{
    // View-only window: no node palette.
}

void Rendergraph_window::collect_selected_nodes(std::vector<std::shared_ptr<Graph_editor_node>>&)
{
    // Rendergraph nodes are not Graph_editor_nodes.
}

auto Rendergraph_window::find_graph_editor_node(std::size_t) -> std::shared_ptr<Graph_editor_node>
{
    return {};
}

auto Rendergraph_window::get_node_position(const Graph_editor_node&) -> ImVec2
{
    return ImVec2{0.0f, 0.0f};
}

void Rendergraph_window::set_node_position(const Graph_editor_node&, const ImVec2&)
{
}

auto Rendergraph_window::get_node_size(const Graph_editor_node&) -> ImVec2
{
    return ImVec2{0.0f, 0.0f};
}

auto Rendergraph_window::get_node_editor() -> ax::NodeEditor::EditorContext*
{
    return m_node_editor.get();
}

auto Rendergraph_window::clipboard_kind() const -> const char*
{
    return "rendergraph";
}

auto Rendergraph_window::get_current_graph() -> erhe::graph::Graph*
{
    return &m_context.rendergraph->get_graph();
}

auto Rendergraph_window::paste_nodes(const nlohmann::json&, const ImVec2&) -> std::vector<std::size_t>
{
    return {};
}

void Rendergraph_window::remove_nodes(const std::vector<std::shared_ptr<Graph_editor_node>>&)
{
    // The rendergraph is owned by the renderer, not editable from here.
}

void Rendergraph_window::build_palette()
{
    // View-only window: nothing spawnable.
}

void Rendergraph_window::add_node_from_palette(const std::string&, const ImVec2*)
{
}

void Rendergraph_window::imgui()
{
    if (!m_node_editor) {
        m_node_editor = std::make_unique<ax::NodeEditor::EditorContext>(nullptr);
    }

    erhe::rendergraph::Rendergraph& rendergraph = *m_context.rendergraph;

    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderFloat("Image Size", &m_image_size, 4.0f, 1000.0f);

    m_node_editor->Begin("Rendergraph", ImVec2{0.0f, 0.0f});

    const float zoom = 1.0f;
    const std::vector<erhe::rendergraph::Rendergraph_node*>& render_graph_nodes = rendergraph.get_nodes();

    for (erhe::rendergraph::Rendergraph_node* node : render_graph_nodes) {
        const std::string label = fmt::format("{}: {}{}", node->get_depth(), node->get_name(), node->is_enabled() ? "" : " (disabled)");
        const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& inputs  = node->get_input_pins();
        const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& outputs = node->get_output_pins();

        const ax::NodeEditor::NodeId node_id{node->get_id()};
        ImGui::PushID(static_cast<int>(node->get_id()));
        ERHE_DEFER( ImGui::PopID(); );

        m_node_editor->BeginNode(node_id);

        ImGui::TextUnformatted(label.c_str());

        ImVec2 pin_table_size{200.0f, 0.0f};

        // Input pins
        ImGui::BeginTable("##InputPin", 2, ImGuiTableFlags_None, pin_table_size);
        ImGui::TableSetupColumn("InputPin",   ImGuiTableColumnFlags_WidthFixed, 20.0f);
        ImGui::TableSetupColumn("InputLabel", ImGuiTableColumnFlags_None);
        for (const erhe::graph::Pin& input : inputs) {
            const ax::NodeEditor::PinId pin_id{&input};
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            m_node_editor->BeginPin(pin_id, ax::NodeEditor::PinKind::Input);
            ImGui::Bullet();
            m_node_editor->EndPin();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(input.get_name().data());
        }
        ImGui::EndTable();

        ImGui::SameLine();

        // Content: thumbnails of the node's producer output textures
        for (const erhe::graph::Pin& output : outputs) {
            const int key = static_cast<int>(output.get_key());
            const std::shared_ptr<erhe::graphics::Texture>& texture = node->get_producer_output_texture(key);
            if (
                texture &&
                (texture->get_texture_type() == erhe::graphics::Texture_type::texture_2d) &&
                (texture->get_width () >= 1) &&
                (texture->get_height() >= 1) &&
                (erhe::dataformat::has_color(texture->get_pixelformat()))
            ) {
                const float aspect = static_cast<float>(texture->get_width()) / static_cast<float>(texture->get_height());
                m_imgui_renderer.image(
                    erhe::imgui::Draw_texture_parameters{
                        .texture_reference = std::static_pointer_cast<erhe::graphics::Texture_reference>(texture),
                        .width             = static_cast<int>(zoom * aspect * m_image_size),
                        .height            = static_cast<int>(zoom * m_image_size),
                        .debug_label       = erhe::utility::Debug_label{"Rendergraph_window::imgui()"}
                    }
                );
            }
        }

        ImGui::SameLine();

        // Output pins
        ImGui::BeginTable("##Outputs", 2, ImGuiTableFlags_None, pin_table_size);
        ImGui::TableSetupColumn("OutputLabel", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("OutputPin",   ImGuiTableColumnFlags_WidthFixed, 20.0f);
        for (const erhe::graph::Pin& output : outputs) {
            const ax::NodeEditor::PinId pin_id{&output};
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(output.get_name().data());

            ImGui::TableSetColumnIndex(1);
            m_node_editor->BeginPin(pin_id, ax::NodeEditor::PinKind::Output);
            ImGui::Bullet();
            m_node_editor->EndPin();
        }
        ImGui::EndTable(); // Outputs

        m_node_editor->EndNode();
    }

    // Links, identified by pointer like in the other graph editor windows
    // (Graph_editor_window_base::collect_selected_links relies on this).
    for (const std::unique_ptr<erhe::graph::Link>& link : rendergraph.get_graph().get_links()) {
        m_node_editor->Link(
            ax::NodeEditor::LinkId{link.get()},
            ax::NodeEditor::PinId{link->get_source()},
            ax::NodeEditor::PinId{link->get_sink()}
        );
    }

    m_node_editor->End();
}

}
