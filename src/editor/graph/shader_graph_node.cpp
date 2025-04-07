#include "graph/shader_graph_node.hpp"
#include "editor_context.hpp"
#include "editor_log.hpp"
#include "tools/selection_tool.hpp"

//#include "windows/IconsMaterialDesignIcons.h"
#define ICON_MDI_COG                                      "\xf3\xb0\x92\x93" // U+F0493

#include "erhe_defer/defer.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"

#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_imgui/imgui_renderer.hpp"

namespace editor {

auto get_flow_direction_name(int direction) -> const char*
{
    switch (direction) {
        case Flow_direction::left_to_right: return "Left to Right";
        case Flow_direction::right_to_left: return "Right to Left";
        case Flow_direction::top_to_bottom: return "Top to Bottom";
        case Flow_direction::bottom_to_top: return "Bottom to Top";
        default: return "?";
    }
}

Shader_graph_node::Shader_graph_node(const char* label)
    : erhe::graph::Node{label}
{
}

// For given slot / pin, accumulates payload from all connected links
auto Shader_graph_node::accumulate_input_from_links(const std::size_t i) -> Payload
{
    erhe::graph::Pin& input_pin = get_input_pins().at(i);

    if (input_pin.is_sink()) { // && pull
        Payload sum{};
        for (erhe::graph::Link* link : input_pin.get_links()) {
            erhe::graph::Pin*  source_pin               = link->get_source();
            std::size_t        slot                     = source_pin->get_slot();
            erhe::graph::Node* source_node              = source_pin->get_owner_node();
            Shader_graph_node* source_shader_graph_node = dynamic_cast<Shader_graph_node*>(source_node);
            sum += source_shader_graph_node->get_output(slot);
        }
        return sum;
    }
    return {};
}

auto Shader_graph_node::get_input(std::size_t i) const -> Payload
{
    return m_input_payloads.at(i);
}

void Shader_graph_node::set_input(std::size_t i, Payload value)
{
    m_input_payloads.at(i) = value;
}

auto Shader_graph_node::get_output(const std::size_t i) const -> Payload
{
    return m_output_payloads.at(i);
}

void Shader_graph_node::set_output(const std::size_t i, Payload payload)
{
    m_output_payloads.at(i) = payload;
}

void Shader_graph_node::make_input_pin(std::size_t key, std::string_view name)
{
    m_input_payloads.emplace_back(std::move(Payload{}));
    base_make_input_pin(key, name);
}

void Shader_graph_node::make_output_pin(std::size_t key, std::string_view name)
{
    m_output_payloads.emplace_back(std::move(Payload{}));
    base_make_output_pin(key, name);
}

void Shader_graph_node::evaluate(Shader_graph&)
{
    // Overridden in derived classes
}

void Shader_graph_node::imgui()
{
}

void Shader_graph_node::node_editor(Editor_context& editor_context, ax::NodeEditor::EditorContext& node_editor)
{
    log_graph_editor->info("Shader_graph_node {} {}", get_name(), get_id());

    ImGui::PushID(static_cast<int>(get_id()));
    ERHE_DEFER( ImGui::PopID(); );

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0f, 0.0f});
    ERHE_DEFER( ImGui::PopStyleVar(1); );

    ax::NodeEditor::NodeId node_id{get_id()};
    node_editor.BeginNode(node_id);

    Node_context context {
        .context         = editor_context,
        .node_editor     = node_editor,
        .pin_width       = 32.0f,
        .pin_label_width =  70.0f,
        .center_width    = 220.0f,
        .icon_font       = editor_context.imgui_renderer->icon_font()
    };
    context.side_width      = context.pin_width + context.pin_label_width;
    context.pin_table_size  = ImVec2{context.side_width, 0.0f};
    context.node_table_size = ImVec2{context.center_width + 2.0f * context.side_width, 0.0f},

    ImGui::BeginTable("##NodeTable", 3, ImGuiTableFlags_None, context.node_table_size);
    ImGui::TableSetupColumn("lhs",    ImGuiTableColumnFlags_WidthFixed, context.side_width);
    ImGui::TableSetupColumn("center", ImGuiTableColumnFlags_WidthFixed, context.center_width);
    ImGui::TableSetupColumn("rhs",    ImGuiTableColumnFlags_WidthFixed, context.side_width);

    // "Header" row
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(m_name.c_str());
        //ImGui::TableSetColumnIndex(2);
        //ImGui::PushFont(context.icon_font);
        //const float cell_width   = ImGui::GetContentRegionAvail().x;
        //const float button_width = ImGui::CalcTextSize(ICON_MDI_COG).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        //ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cell_width - button_width);
        //if (ImGui::Button(ICON_MDI_COG)) {
        //    m_show_config = !m_show_config;
        //}
        ImGui::PopFont();
    }

    ImGui::TableNextRow();

    //if (m_show_config) {
    //    ImGui::TableSetColumnIndex(1);
    //    ImGui::TextUnformatted("Config");
    //    if (ImGui::IsItemHovered()) {
    //        node_editor.Suspend();
    //        ImGui::SetTooltip("Config tooltip");
    //        node_editor.Resume();
    //    }
    //
    //    if (ImGui::BeginCombo("Flow Direction", get_flow_direction_name(m_flow_direction))) {
    //        for (int i = 0; i < Flow_direction::count; ++i) {
    //            bool selected = (m_flow_direction == i);
    //            ImGui::Selectable(get_flow_direction_name(i), &selected, ImGuiSelectableFlags_None);
    //            if (selected) {
    //                m_flow_direction = i;
    //            }
    //        }
    //        ImGui::EndCombo();
    //    }
    //} else
    {
        // Input pins
        ImGui::TableSetColumnIndex(0);
        show_input_pins(context);

        // Content
        ImGui::TableSetColumnIndex(1);
        imgui();

        // Output pins - and settings icon
        ImGui::TableSetColumnIndex(2);
        show_output_pins(context);
    }

    ImGui::EndTable();

    node_editor.EndNode();
    const bool item_selection   = is_selected();
    const bool editor_selection = node_editor.IsNodeSelected(get_id());
    if (item_selection != editor_selection) {
        if (editor_selection) {
            editor_context.selection->add_to_selection(shared_from_this());
        } else {
            editor_context.selection->remove_from_selection(shared_from_this());
        }
    }
}

void Shader_graph_node::show_input_pins(Node_context& context)
{
    ImGui::BeginTable("##InputPins", 2, ImGuiTableFlags_None, context.pin_table_size);
    ImGui::TableSetupColumn("InputPin",   ImGuiTableColumnFlags_WidthFixed, context.pin_width);
    ImGui::TableSetupColumn("InputLabel", ImGuiTableColumnFlags_WidthFixed, context.pin_label_width);
    for (const erhe::graph::Pin& pin : get_input_pins()) {
        log_graph_editor->info("  Input Pin {} {}", pin.get_name(), pin.get_id());
        ImGui::TableNextRow();
            
        ImGui::TableSetColumnIndex(0);
        context.node_editor.BeginPin(ax::NodeEditor::PinId{&pin}, ax::NodeEditor::PinKind::Input);
        ImGui::Bullet();
        context.node_editor.EndPin();

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(pin.get_name().data());
    }
    ImGui::EndTable();
}

void Shader_graph_node::show_output_pins(Node_context& context)
{
    ImGui::BeginTable("##OutputPins", 2, ImGuiTableFlags_None, context.pin_table_size);
    ImGui::TableSetupColumn("OutputLabel", ImGuiTableColumnFlags_WidthFixed, context.pin_label_width);
    ImGui::TableSetupColumn("OutputPin",   ImGuiTableColumnFlags_WidthFixed, context.pin_width);
    for (const erhe::graph::Pin& pin : get_output_pins()) {
        log_graph_editor->info("  Output Pin {} {}", pin.get_name(), pin.get_id());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const char* label = pin.get_name().data();
        float column_width = ImGui::GetColumnWidth();
        float text_width   = ImGui::CalcTextSize(label).x;
        float padding      = column_width - text_width;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding);
        ImGui::TextUnformatted(label);

        ImGui::TableSetColumnIndex(1);
        context.node_editor.BeginPin(ax::NodeEditor::PinId{&pin}, ax::NodeEditor::PinKind::Output);
        ImGui::Bullet();
        context.node_editor.EndPin();
    }
    ImGui::EndTable(); // Outputs
}

} // namespace editor
