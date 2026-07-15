#include "graph_editor/graph_editor_node.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace editor {

Graph_editor_node::Graph_editor_node(const char* label)
    : erhe::graph::Node{label}
{
}

auto Graph_editor_node::is_dirty() const -> bool
{
    return m_dirty;
}

void Graph_editor_node::mark_dirty()
{
    m_dirty = true;
}

void Graph_editor_node::clear_dirty()
{
    m_dirty = false;
}

void Graph_editor_node::imgui()
{
}

void Graph_editor_node::properties_imgui(App_context& app_context)
{
    // Content-scale-neutral: imgui() widgets size themselves with
    // content_scale(), which on the canvas carries zoom * ui-scale. Render
    // at 1 here and restore, so a properties-panel edit does not disturb
    // canvas-derived state (e.g. the preview display size).
    const float saved_content_scale = m_content_scale;
    m_content_scale = 1.0f;
    if (m_committed_parameters.empty()) {
        m_committed_parameters = dump_parameters(); // baseline for parameter undo
    }
    const bool was_dirty_before_widgets = is_dirty();
    imgui();
    if (!was_dirty_before_widgets && is_dirty()) {
        m_parameter_edit_in_progress = true; // a widget changed a parameter this frame
    }
    m_content_scale = saved_content_scale;
    commit_parameter_edit(app_context);
}

auto Graph_editor_node::get_ui_scale() const -> float
{
    return m_ui_scale;
}

void Graph_editor_node::set_ui_scale(const float scale)
{
    m_ui_scale = std::clamp(scale, 0.25f, 4.0f);
}

auto Graph_editor_node::get_factory_type_name() const -> const std::string&
{
    return m_type_name;
}

void Graph_editor_node::set_factory_type_name(const std::string& type_name)
{
    m_type_name = type_name;
}

void Graph_editor_node::write_parameters(nlohmann::json&) const
{
}

void Graph_editor_node::read_parameters(const nlohmann::json&)
{
}

auto Graph_editor_node::dump_parameters() const -> std::string
{
    nlohmann::json parameters = nlohmann::json::object();
    write_parameters(parameters);
    return parameters.dump();
}

auto Graph_editor_node::get_committed_parameters() const -> const std::string&
{
    return m_committed_parameters;
}

void Graph_editor_node::set_committed_parameters(const std::string& parameters)
{
    m_committed_parameters = parameters;
}

void Graph_editor_node::after_node_content(App_context&)
{
}

void Graph_editor_node::commit_parameter_edit(App_context& app_context)
{
    // Commit the parameter edit gesture once its widget deactivates (mouse
    // released on a drag / stepper, text input defocused). One undoable
    // operation per completed gesture. Runs from both the canvas and the
    // Node Properties window; the operation records the new committed state,
    // so a second call in the same frame is a no-op.
    if (m_parameter_edit_in_progress && !ImGui::IsAnyItemActive()) {
        m_parameter_edit_in_progress = false;
        std::string after_parameters = dump_parameters();
        if (after_parameters != m_committed_parameters) {
            std::string before_parameters = m_committed_parameters;
            commit_parameter_operation(app_context, std::move(before_parameters), std::move(after_parameters));
        }
    }
}

void Graph_editor_node::node_editor(App_context& app_context, ax::NodeEditor::EditorContext& node_editor)
{
    ImGui::PushID(static_cast<int>(get_id()));
    ERHE_DEFER( ImGui::PopID(); );

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0f, 0.0f});
    ERHE_DEFER( ImGui::PopStyleVar(1); );

    // Issue #251: node content is authored in screen space at the zoomed size,
    // so canvas-unit pixel metrics must be multiplied by the view scale. The
    // per-node ui scale (Node Properties "Size") folds in here so the whole
    // node - widths, pins, previews - scales with it.
    m_content_scale = node_editor.GetCurrentZoom() * m_ui_scale;

    ax::NodeEditor::NodeId node_id{get_id()};
    m_is_hovered = (node_editor.GetHoveredNode() == node_id);
    node_editor.BeginNode(node_id);

    // The canvas pushed the font at (base * zoom); scale that pushed base by
    // the node's ui scale so text tracks the scaled widths.
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * m_ui_scale);

    const float  pin_label_width = 70.0f  * m_content_scale;
    const float  center_width    = 150.0f * m_content_scale;
    const ImVec2 node_table_size{center_width + (2.0f * pin_label_width), 0.0f};

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // The node's frame is drawn from its canvas-space bounds mapped to screen;
    // pins snap to that frame, so compute the edges in screen space too. The
    // pin cell centers (below) are already screen space (ImGui item rects), so
    // this keeps pin X in sync with pin Y and with the node border.
    const ImVec2 node_position   = node_editor.GetNodePosition(node_id);
    const ImVec2 node_size       = node_editor.GetNodeSize(node_id);
    const float  left_edge       = node_editor.CanvasToScreen(node_position).x;
    const float  right_edge      = node_editor.CanvasToScreen(ImVec2{node_position.x + node_size.x, node_position.y}).x;

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
    if (m_committed_parameters.empty()) {
        m_committed_parameters = dump_parameters(); // baseline for parameter undo
    }
    const bool was_dirty_before_widgets = is_dirty();
    imgui();
    if (!was_dirty_before_widgets && is_dirty()) {
        m_parameter_edit_in_progress = true; // a widget changed a parameter this frame
    }
    after_node_content(app_context);

    // Output pins on the right edge
    ImGui::TableSetColumnIndex(2);
    node_editor.PushStyleVar(ax::NodeEditor::StyleVar_SourceDirection, ImVec2{1.0f, 0.0});
    node_editor.PushStyleVar(ax::NodeEditor::StyleVar_TargetDirection, ImVec2{1.0f, 0.0});
    node_editor.PushStyleVar(ax::NodeEditor::StyleVar_PinArrowSize,  0.0f);
    node_editor.PushStyleVar(ax::NodeEditor::StyleVar_PinArrowWidth, 0.0f);
    show_pins(node_editor, *draw_list, get_output_pins(), right_edge, true);
    node_editor.PopStyleVar(4);

    ImGui::EndTable();

    ImGui::PopFont();

    node_editor.EndNode();

    commit_parameter_edit(app_context);

    // Issue #252: node selection lives purely in the ax::NodeEditor canvas.
    // The node is deliberately NOT synced into the global selection - doing so
    // put the containing graph asset and the node in the selection together, so
    // a single Delete deleted both the node (canvas handle_deletions) and the
    // whole graph asset (Selection::delete_selection). Canvas Delete is now the
    // only path that removes a node here.
}

void Graph_editor_node::show_pins(
    ax::NodeEditor::EditorContext&                                  node_editor,
    ImDrawList&                                                     draw_list,
    const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& pins,
    const float                                                     edge_x,
    const bool                                                      right_edge
)
{
    // Issue #251: pins are drawn and hit-tested in screen space at the zoomed
    // size. edge_x is the node border in screen coordinates, cell_center_y comes
    // from the (screen-space) ImGui item rect, and the half extent scales with
    // the view. The visible square is drawn directly to the screen-space draw
    // list; PinRect stores the node editor's pin bounds in CANVAS units (the
    // editor maps them back to screen and hit-tests in canvas space), so the
    // screen rect is mapped back with ScreenToCanvas.
    const float half_extent = 10.0f * m_content_scale;

    // Pin labels use a slightly smaller font than the node body so they read as
    // secondary text and leave more clearance from the socket squares. The node
    // content pushed its own font at (base * zoom), so scale that pushed base.
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
    ERHE_DEFER( ImGui::PopFont(); );

    // Straddle the node border so 1/4 of the socket sits inside the node and
    // 3/4 outside (the square is 2 * half_extent wide, so the center shifts
    // half_extent * 0.5 toward the outside). The socket then intrudes only
    // half_extent * 0.5 (= 5 * scale) past the border - clear of the pin label,
    // which starts one NodePadding (8 * scale) inside - so the two no longer
    // overlap. "Outside" is left for the input edge, right for the output edge.
    const float pin_offset = 0.5f * half_extent;
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
        const float  pin_center_x  = right_edge ? (edge_x + pin_offset) : (edge_x - pin_offset);
        const ImVec2 pin_center{pin_center_x, cell_center_y};
        const ImVec2 min{pin_center.x - half_extent, pin_center.y - half_extent};
        const ImVec2 max{pin_center.x + half_extent, pin_center.y + half_extent};

        node_editor.BeginPin(ax::NodeEditor::PinId{&pin}, pin.is_source() ? ax::NodeEditor::PinKind::Output : ax::NodeEditor::PinKind::Input);
        node_editor.PinRect(node_editor.ScreenToCanvas(min), node_editor.ScreenToCanvas(max));
        node_editor.EndPin();

        draw_list.AddRectFilled(min, max, pin_key_color(pin.get_key()), 4.0f * m_content_scale, ImDrawFlags_RoundCornersAll);
        draw_list.AddRect      (min, max, 0xffcccccc, 4.0f * m_content_scale, ImDrawFlags_RoundCornersAll, 2.0f * m_content_scale);
    }
}

} // namespace editor
