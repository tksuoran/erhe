#include "texture_graph/texture_graph_node.hpp"
#include "app_context.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

namespace {

// Pin fill color per Texture_pin_key value type. Pins only accept links from
// pins with the same key, so matching colors show which connections are legal
// before a drag gets rejected.
[[nodiscard]] auto pin_key_color(const std::size_t key) -> ImU32
{
    switch (key) {
        case Texture_pin_key::grayscale: return IM_COL32(200, 200, 200, 255); // light grey
        case Texture_pin_key::rgb:       return IM_COL32( 90, 160, 235, 255); // blue
        case Texture_pin_key::rgba:      return IM_COL32(120, 205, 150, 255); // green
        default:                         return IM_COL32( 68,  68,  68, 255);
    }
}

} // namespace

Texture_graph_node::Texture_graph_node(const char* label)
    : erhe::graph::Node{label}
{
}

auto Texture_graph_node::input_from_links(const std::size_t i) const -> Texture_payload
{
    const erhe::graph::Pin& input_pin = get_input_pins().at(i);
    Texture_payload result{};
    for (erhe::graph::Link* link : input_pin.get_links()) {
        erhe::graph::Pin*   source_pin  = link->get_source();
        const std::size_t   slot        = source_pin->get_slot();
        erhe::graph::Node*  source_node = source_pin->get_owner_node();
        Texture_graph_node* source_texture_graph_node = dynamic_cast<Texture_graph_node*>(source_node);
        if (source_texture_graph_node != nullptr) {
            // MVP inputs take a single link; the last connected source wins.
            result = source_texture_graph_node->get_output(slot);
        }
    }
    return result;
}

void Texture_graph_node::pull_inputs()
{
    for (std::size_t i = 0, end = m_input_payloads.size(); i < end; ++i) {
        m_input_payloads[i] = input_from_links(i);
    }
}

auto Texture_graph_node::get_input(const std::size_t i) const -> const Texture_payload&
{
    return m_input_payloads.at(i);
}

auto Texture_graph_node::get_output(const std::size_t i) const -> const Texture_payload&
{
    return m_output_payloads.at(i);
}

void Texture_graph_node::set_input(const std::size_t i, const Texture_payload& payload)
{
    m_input_payloads.at(i) = payload;
}

void Texture_graph_node::set_output(const std::size_t i, const Texture_payload& payload)
{
    m_output_payloads.at(i) = payload;
}

void Texture_graph_node::make_input_pin(const std::size_t key, const std::string_view name)
{
    m_input_payloads.emplace_back();
    base_make_input_pin(key, name);
}

void Texture_graph_node::make_output_pin(const std::size_t key, const std::string_view name)
{
    m_output_payloads.emplace_back();
    base_make_output_pin(key, name);
}

auto Texture_graph_node::is_dirty() const -> bool
{
    return m_dirty;
}

void Texture_graph_node::mark_dirty()
{
    m_dirty = true;
}

void Texture_graph_node::clear_dirty()
{
    m_dirty = false;
}

void Texture_graph_node::evaluate(Texture_graph&)
{
    // Overridden in derived classes.
}

void Texture_graph_node::imgui()
{
}

void Texture_graph_node::on_removed_from_graph()
{
}

auto Texture_graph_node::get_factory_type_name() const -> const std::string&
{
    return m_type_name;
}

void Texture_graph_node::set_factory_type_name(const std::string& type_name)
{
    m_type_name = type_name;
}

void Texture_graph_node::write_parameters(nlohmann::json&) const
{
}

void Texture_graph_node::read_parameters(const nlohmann::json&)
{
}

auto Texture_graph_node::dump_parameters() const -> std::string
{
    nlohmann::json parameters = nlohmann::json::object();
    write_parameters(parameters);
    return parameters.dump();
}

auto Texture_graph_node::get_committed_parameters() const -> const std::string&
{
    return m_committed_parameters;
}

void Texture_graph_node::set_committed_parameters(const std::string& parameters)
{
    m_committed_parameters = parameters;
}

void Texture_graph_node::node_editor(App_context& app_context, ax::NodeEditor::EditorContext& node_editor)
{
    ImGui::PushID(static_cast<int>(get_id()));
    ERHE_DEFER( ImGui::PopID(); );

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0f, 0.0f});
    ERHE_DEFER( ImGui::PopStyleVar(1); );

    ax::NodeEditor::NodeId node_id{get_id()};
    node_editor.BeginNode(node_id);

    const float  pin_label_width = 70.0f;
    const float  center_width    = 150.0f;
    const ImVec2 node_table_size{center_width + (2.0f * pin_label_width), 0.0f};

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const ImVec2 node_position = node_editor.GetNodePosition(node_id);
    const ImVec2 node_size     = node_editor.GetNodeSize(node_id);
    const float  left_edge     = node_position.x;
    const float  right_edge    = node_position.x + node_size.x;

    ImGui::BeginTable("##NodeTable", 3, ImGuiTableFlags_None, node_table_size);
    ImGui::TableSetupColumn("lhs",    ImGuiTableColumnFlags_WidthFixed, pin_label_width);
    ImGui::TableSetupColumn("center", ImGuiTableColumnFlags_WidthFixed, center_width);
    ImGui::TableSetupColumn("rhs",    ImGuiTableColumnFlags_WidthFixed, pin_label_width);

    // "Header" row
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(m_name.c_str());
    }

    ImGui::TableNextRow();

    // Input pins on the left edge
    ImGui::TableSetColumnIndex(0);
    node_editor.PushStyleVar(ax::NodeEditor::StyleVar_SourceDirection, ImVec2{ 1.0f, 0.0});
    node_editor.PushStyleVar(ax::NodeEditor::StyleVar_TargetDirection, ImVec2{-1.0f, 0.0});
    node_editor.PushStyleVar(ax::NodeEditor::StyleVar_PivotAlignment,  ImVec2{-0.75f, 0.5f});
    show_pins(node_editor, *draw_list, get_input_pins(), left_edge, false);
    node_editor.PopStyleVar(3);

    // Content
    ImGui::TableSetColumnIndex(1);
    imgui();

    // Output pins on the right edge
    ImGui::TableSetColumnIndex(2);
    node_editor.PushStyleVar(ax::NodeEditor::StyleVar_SourceDirection, ImVec2{1.0f, 0.0});
    node_editor.PushStyleVar(ax::NodeEditor::StyleVar_TargetDirection, ImVec2{1.0f, 0.0});
    node_editor.PushStyleVar(ax::NodeEditor::StyleVar_PinArrowSize,  0.0f);
    node_editor.PushStyleVar(ax::NodeEditor::StyleVar_PinArrowWidth, 0.0f);
    show_pins(node_editor, *draw_list, get_output_pins(), right_edge, true);
    node_editor.PopStyleVar(4);

    ImGui::EndTable();

    node_editor.EndNode();

    // Selection integration (parameter undo is added in a later step).
    const bool item_selection   = is_selected();
    const bool editor_selection = node_editor.IsNodeSelected(get_id());
    if (item_selection != editor_selection) {
        if (editor_selection) {
            app_context.selection->add_to_selection(shared_from_this());
        } else {
            app_context.selection->remove_from_selection(shared_from_this());
        }
    }
}

void Texture_graph_node::show_pins(
    ax::NodeEditor::EditorContext&                                  node_editor,
    ImDrawList&                                                     draw_list,
    const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& pins,
    const float                                                     edge_x,
    const bool                                                      right_edge
)
{
    const float half_extent = 10.0f;
    for (const erhe::graph::Pin& pin : pins) {
        if (right_edge) {
            const float column_width = ImGui::GetColumnWidth();
            const float text_width   = ImGui::CalcTextSize(pin.get_name().data()).x;
            const float padding      = column_width - text_width;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding);
        }
        ImGui::TextUnformatted(pin.get_name().data());

        const ImVec2 cell_min      = ImGui::GetItemRectMin();
        const ImVec2 cell_max      = ImGui::GetItemRectMax();
        const float  cell_center_y = 0.5f * (cell_min.y + cell_max.y);
        const ImVec2 pin_center{edge_x, cell_center_y};
        const ImVec2 min{pin_center.x - half_extent, pin_center.y - half_extent};
        const ImVec2 max{pin_center.x + half_extent, pin_center.y + half_extent};

        node_editor.BeginPin(ax::NodeEditor::PinId{&pin}, pin.is_source() ? ax::NodeEditor::PinKind::Output : ax::NodeEditor::PinKind::Input);
        node_editor.PinRect(min, max);
        node_editor.EndPin();

        draw_list.AddRectFilled(min, max, pin_key_color(pin.get_key()), 4.0f, ImDrawFlags_RoundCornersAll);
        draw_list.AddRect      (min, max, 0xffcccccc, 4.0f, ImDrawFlags_RoundCornersAll, 2.0f);
    }
}

} // namespace editor
