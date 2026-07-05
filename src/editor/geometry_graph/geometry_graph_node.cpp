#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/geometry_graph_operations.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "app_context.hpp"
#include "operations/operation_stack.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

namespace {

// Pin fill color per Geometry_pin_key payload type. Pins only accept links
// from pins with the same key, so matching colors show which connections
// are legal before a drag gets rejected.
[[nodiscard]] auto pin_key_color(const std::size_t key) -> ImU32
{
    switch (key) {
        case Geometry_pin_key::geometry:    return IM_COL32( 54, 192, 154, 255); // teal
        case Geometry_pin_key::float_value: return IM_COL32(160, 160, 160, 255); // grey
        case Geometry_pin_key::int_value:   return IM_COL32( 89, 140,  92, 255); // muted green
        case Geometry_pin_key::bool_value:  return IM_COL32(204, 166, 214, 255); // pink
        case Geometry_pin_key::vec3_value:  return IM_COL32( 99,  99, 199, 255); // indigo
        case Geometry_pin_key::vec4_value:  return IM_COL32(199, 199,  41, 255); // yellow
        case Geometry_pin_key::mat4_value:  return IM_COL32( 70, 120, 190, 255); // steel blue
        case Geometry_pin_key::material:    return IM_COL32(235, 122,  82, 255); // orange
        case Geometry_pin_key::points:      return IM_COL32(110, 205, 230, 255); // light cyan
        case Geometry_pin_key::instances:   return IM_COL32(130, 215, 120, 255); // spring green
        default:                            return IM_COL32( 68,  68,  68, 255);
    }
}

}

void process_for_graph(erhe::geometry::Geometry& geometry)
{
    geometry.process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges
        }
    );
}

auto imgui_index_stepper(const char* id, int& index, const int count) -> bool
{
    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::ArrowButton("##prev", ImGuiDir_Left) && (count > 0)) {
        index = ((index + count) - 1) % count;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Right) && (count > 0)) {
        index = (index + 1) % count;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

auto imgui_enum_stepper(const char* id, int& index, const char* const* names, const int count) -> bool
{
    const bool changed = imgui_index_stepper(id, index, count);
    ImGui::SameLine();
    ImGui::TextUnformatted(((index >= 0) && (index < count)) ? names[index] : "?");
    return changed;
}

void write_vec3(nlohmann::json& out, const char* key, const glm::vec3& value)
{
    out[key] = { value.x, value.y, value.z };
}

void write_ivec3(nlohmann::json& out, const char* key, const glm::ivec3& value)
{
    out[key] = { value.x, value.y, value.z };
}

auto read_vec3(const nlohmann::json& in, const char* key, const glm::vec3& fallback) -> glm::vec3
{
    if (in.contains(key) && in[key].is_array() && (in[key].size() == 3)) {
        return glm::vec3{in[key][0].get<float>(), in[key][1].get<float>(), in[key][2].get<float>()};
    }
    return fallback;
}

auto read_ivec3(const nlohmann::json& in, const char* key, const glm::ivec3& fallback) -> glm::ivec3
{
    if (in.contains(key) && in[key].is_array() && (in[key].size() == 3)) {
        return glm::ivec3{in[key][0].get<int>(), in[key][1].get<int>(), in[key][2].get<int>()};
    }
    return fallback;
}

Geometry_graph_node::Geometry_graph_node(const char* label)
    : erhe::graph::Node{label}
{
}

auto Geometry_graph_node::accumulate_input_from_links(const std::size_t i) const -> Geometry_payload
{
    const erhe::graph::Pin& input_pin = get_input_pins().at(i);
    Geometry_payload accumulated{};
    for (erhe::graph::Link* link : input_pin.get_links()) {
        erhe::graph::Pin*    source_pin  = link->get_source();
        const std::size_t    slot        = source_pin->get_slot();
        erhe::graph::Node*   source_node = source_pin->get_owner_node();
        Geometry_graph_node* source_geometry_graph_node = dynamic_cast<Geometry_graph_node*>(source_node);
        if (source_geometry_graph_node != nullptr) {
            accumulated += source_geometry_graph_node->get_output(slot);
        }
    }
    return accumulated;
}

void Geometry_graph_node::pull_inputs()
{
    for (std::size_t i = 0, end = m_input_payloads.size(); i < end; ++i) {
        m_input_payloads[i] = accumulate_input_from_links(i);
    }
}

auto Geometry_graph_node::get_input(const std::size_t i) const -> const Geometry_payload&
{
    return m_input_payloads.at(i);
}

auto Geometry_graph_node::get_output(const std::size_t i) const -> const Geometry_payload&
{
    return m_output_payloads.at(i);
}

void Geometry_graph_node::set_input(const std::size_t i, const Geometry_payload& payload)
{
    m_input_payloads.at(i) = payload;
}

void Geometry_graph_node::set_output(const std::size_t i, const Geometry_payload& payload)
{
    m_output_payloads.at(i) = payload;
}

void Geometry_graph_node::make_input_pin(const std::size_t key, const std::string_view name)
{
    m_input_payloads.emplace_back();
    base_make_input_pin(key, name);
}

void Geometry_graph_node::make_output_pin(const std::size_t key, const std::string_view name)
{
    m_output_payloads.emplace_back();
    base_make_output_pin(key, name);
}

auto Geometry_graph_node::is_dirty() const -> bool
{
    return m_dirty;
}

void Geometry_graph_node::mark_dirty()
{
    m_dirty = true;
}

void Geometry_graph_node::clear_dirty()
{
    m_dirty = false;
}

void Geometry_graph_node::evaluate(Geometry_graph&)
{
    // Overridden in derived classes
}

void Geometry_graph_node::imgui()
{
}

void Geometry_graph_node::on_removed_from_graph()
{
}

auto Geometry_graph_node::get_factory_type_name() const -> const std::string&
{
    return m_type_name;
}

void Geometry_graph_node::set_factory_type_name(const std::string& type_name)
{
    m_type_name = type_name;
}

void Geometry_graph_node::write_parameters(nlohmann::json&) const
{
}

void Geometry_graph_node::read_parameters(const nlohmann::json&)
{
}

auto Geometry_graph_node::dump_parameters() const -> std::string
{
    nlohmann::json parameters = nlohmann::json::object();
    write_parameters(parameters);
    return parameters.dump();
}

auto Geometry_graph_node::get_committed_parameters() const -> const std::string&
{
    return m_committed_parameters;
}

auto Geometry_graph_node::get_log_id() const -> std::size_t
{
    return (m_log_source_id != 0) ? m_log_source_id : get_id();
}

void Geometry_graph_node::set_log_source_id(const std::size_t id)
{
    m_log_source_id = id;
}

auto Geometry_graph_node::get_owning_graph_mesh() const -> std::shared_ptr<Graph_mesh>
{
    return m_owning_graph_mesh.lock();
}

void Geometry_graph_node::set_owning_graph_mesh(const std::shared_ptr<Graph_mesh>& graph_mesh)
{
    m_owning_graph_mesh = graph_mesh;
}

void Geometry_graph_node::set_committed_parameters(const std::string& parameters)
{
    m_committed_parameters = parameters;
}

void Geometry_graph_node::node_editor(App_context& app_context, ax::NodeEditor::EditorContext& node_editor)
{
    ImGui::PushID(static_cast<int>(get_id()));
    ERHE_DEFER( ImGui::PopID(); );

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0f, 0.0f});
    ERHE_DEFER( ImGui::PopStyleVar(1); );

    // Issue #251: node content is authored in screen space at the zoomed size,
    // so canvas-unit pixel metrics must be multiplied by the view scale.
    m_content_scale = node_editor.GetCurrentZoom();

    ax::NodeEditor::NodeId node_id{get_id()};
    node_editor.BeginNode(node_id);

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

    // Commit the parameter edit gesture once its widget deactivates
    // (mouse released on a drag / stepper, text input defocused). One
    // undoable operation per completed gesture.
    if (m_parameter_edit_in_progress && !ImGui::IsAnyItemActive()) {
        m_parameter_edit_in_progress = false;
        std::string after_parameters = dump_parameters();
        if (after_parameters != m_committed_parameters) {
            std::string before_parameters = m_committed_parameters;
            app_context.operation_stack->execute_now(
                std::make_shared<Geometry_graph_parameter_operation>(
                    *app_context.geometry_graph_window,
                    std::dynamic_pointer_cast<Geometry_graph_node>(node_from_this()),
                    std::move(before_parameters),
                    std::move(after_parameters)
                )
            );
        }
    }

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

void Geometry_graph_node::show_pins(
    ax::NodeEditor::EditorContext&                                   node_editor,
    ImDrawList&                                                      draw_list,
    const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& pins,
    const float                                                      edge_x,
    const bool                                                       right_edge
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
        node_editor.PinRect(node_editor.ScreenToCanvas(min), node_editor.ScreenToCanvas(max));
        node_editor.EndPin();

        draw_list.AddRectFilled(min, max, pin_key_color(pin.get_key()), 4.0f * m_content_scale, ImDrawFlags_RoundCornersAll);
        draw_list.AddRect      (min, max, 0xffcccccc, 4.0f * m_content_scale, ImDrawFlags_RoundCornersAll, 2.0f * m_content_scale);
    }
}

}
