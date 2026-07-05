#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/texture_graph_compose.hpp"
#include "texture_graph/texture_graph_operations.hpp"
#include "texture_graph/texture_graph_window.hpp"
#include "texture_graph/texture_renderer.hpp"
#include "app_context.hpp"
#include "operations/operation_stack.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/shader_code.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_texgen/node_descriptor.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

auto texture_index_stepper(const char* id, int& index, const int count) -> bool
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

auto texture_enum_stepper(const char* id, int& index, const char* const* names, const int count) -> bool
{
    const bool changed = texture_index_stepper(id, index, count);
    ImGui::SameLine();
    ImGui::TextUnformatted(((index >= 0) && (index < count)) ? names[index] : "?");
    return changed;
}

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
    m_dirty                = true;
    m_preview_needs_render = true;
}

void Texture_graph_node::clear_dirty()
{
    m_dirty = false;
}

auto Texture_graph_node::preview_needs_render() const -> bool
{
    return m_preview_needs_render;
}

void Texture_graph_node::clear_preview_needs_render()
{
    m_preview_needs_render = false;
}

auto Texture_graph_node::preview_output_index() const -> int
{
    return get_output_pins().empty() ? -1 : 0;
}

auto Texture_graph_node::get_preview_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_preview_texture;
}

auto Texture_graph_node::get_preview_texture_ref() -> std::shared_ptr<erhe::graphics::Texture>&
{
    return m_preview_texture;
}

auto Texture_graph_node::preview_display_size() const -> float
{
    return 96.0f;
}

auto Texture_graph_node::render_target_size() const -> int
{
    return 128;
}

auto Texture_graph_node::render_dag(
    App_context&                              context,
    Texture_renderer&                         renderer,
    const Texture_compose_dag&                dag,
    std::shared_ptr<erhe::graphics::Texture>& target,
    const int                                 size
) -> bool
{
    if (!dag.ok || (dag.sink == nullptr) || (context.current_command_buffer == nullptr)) {
        return false;
    }
    const erhe::texgen::Composer    composer{texture_graph_compose_options()};
    const erhe::texgen::Shader_code shader_code = composer.compose(*dag.sink, dag.sink_output_index);
    const std::string               fragment    = composer.assemble_fragment(shader_code);
    if (fragment.find("(error:") != std::string::npos) {
        return false; // composition failed (cycle / depth) - keep the previous texture
    }
    // Resolve each buffer cut point to its rendered texture. This only allocates
    // when the composition samples buffers (not steady state, and only on dirty
    // nodes), so it stays off the hot path.
    std::vector<Texture_sample_binding> sampler_bindings;
    sampler_bindings.reserve(dag.sampler_sources.size());
    for (const Texture_sampler_source& sampler_source : dag.sampler_sources) {
        Texture_sample_binding binding{};
        binding.binding = sampler_source.binding;
        binding.name    = std::string{"tex_"} + std::to_string(sampler_source.binding);
        binding.texture = sampler_source.buffer_node->get_preview_texture().get();
        sampler_bindings.push_back(binding);
    }
    return renderer.render_into(
        *context.current_command_buffer,
        target,
        size,
        fragment,
        shader_code.get_uniforms(),
        shader_code.get_samplers(),
        sampler_bindings
    );
}

void Texture_graph_node::render_products(App_context& context, Texture_renderer& renderer)
{
    const int output_index = preview_output_index();
    if ((descriptor() == nullptr) || (output_index < 0)) {
        return;
    }
    const Texture_compose_dag dag = build_texture_compose_dag(*this, static_cast<std::size_t>(output_index));
    static_cast<void>(render_dag(context, renderer, dag, m_preview_texture, render_target_size()));
}

auto Texture_graph_node::is_buffer() const -> bool
{
    return false;
}

void Texture_graph_node::evaluate(Texture_graph&)
{
    // Overridden in derived classes.
}

void Texture_graph_node::imgui()
{
}

auto Texture_graph_node::descriptor() const -> const erhe::texgen::Node_descriptor*
{
    return nullptr;
}

void Texture_graph_node::configure(erhe::texgen::Compose_node&) const
{
}

void Texture_graph_node::build_pins_from_descriptor(const erhe::texgen::Node_descriptor& descriptor)
{
    for (const erhe::texgen::Input_descriptor& input : descriptor.inputs) {
        make_input_pin(value_type_to_pin_key(input.type), input.name);
    }
    for (const erhe::texgen::Output_descriptor& output : descriptor.outputs) {
        make_output_pin(value_type_to_pin_key(output.type), erhe::texgen::value_type_name(output.type));
    }
}

void Texture_graph_node::on_removed_from_graph()
{
}

void Texture_graph_node::set_owning_graph_texture(const std::weak_ptr<Graph_texture>& graph_texture)
{
    m_owning_graph_texture = graph_texture;
}

auto Texture_graph_node::get_owning_graph_texture() const -> std::shared_ptr<Graph_texture>
{
    return m_owning_graph_texture.lock();
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

void Texture_graph_node::draw_preview(App_context& app_context)
{
    if (!m_preview_texture || (app_context.imgui_renderer == nullptr)) {
        return;
    }
    // Issue #251: the preview thumbnail is display geometry (an ImGui image
    // widget) laid out in the zoomed node content, so its on-screen size scales
    // with the view. The render-target resolution (render_target_size) is a GPU
    // texture size and deliberately does NOT scale.
    const float size = preview_display_size() * m_content_scale;
    app_context.imgui_renderer->image(
        erhe::imgui::Draw_texture_parameters{
            .texture_reference = m_preview_texture,
            .width             = static_cast<int>(size),
            .height            = static_cast<int>(size),
            .uv0               = app_context.imgui_renderer->get_rtt_uv0(),
            .uv1               = app_context.imgui_renderer->get_rtt_uv1(),
            .filter            = erhe::graphics::Filter::linear,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label       = "Texture_graph_node preview"
        }
    );
}

void Texture_graph_node::node_editor(App_context& app_context, ax::NodeEditor::EditorContext& node_editor)
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
    // pins snap to that frame, so compute the edges in screen space too (pin
    // cell centers below are already screen-space ImGui item rects). This keeps
    // pin X in sync with pin Y and with the node border.
    const ImVec2 node_position = node_editor.GetNodePosition(node_id);
    const ImVec2 node_size     = node_editor.GetNodeSize(node_id);
    const float  left_edge     = node_editor.CanvasToScreen(node_position).x;
    const float  right_edge    = node_editor.CanvasToScreen(ImVec2{node_position.x + node_size.x, node_position.y}).x;

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
    draw_preview(app_context);

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

    // Commit the parameter edit gesture once its widget deactivates (mouse
    // released on a drag / stepper, text input defocused). One undoable
    // operation per completed gesture.
    if (m_parameter_edit_in_progress && !ImGui::IsAnyItemActive()) {
        m_parameter_edit_in_progress = false;
        std::string after_parameters = dump_parameters();
        if (after_parameters != m_committed_parameters) {
            std::string before_parameters = m_committed_parameters;
            app_context.operation_stack->execute_now(
                std::make_shared<Texture_graph_parameter_operation>(
                    *app_context.texture_graph_window,
                    std::dynamic_pointer_cast<Texture_graph_node>(node_from_this()),
                    std::move(before_parameters),
                    std::move(after_parameters)
                )
            );
        }
    }

    // Issue #252: node selection lives purely in the ax::NodeEditor canvas.
    // The node is deliberately NOT synced into the global selection - doing
    // so put the containing Graph_texture asset and the node in the selection
    // together, so a single Delete deleted both the node (canvas
    // handle_deletions) and the whole graph asset (Selection::delete_selection).
    // Canvas Delete is now the only path that removes a node here.
}

void Texture_graph_node::show_pins(
    ax::NodeEditor::EditorContext&                                  node_editor,
    ImDrawList&                                                     draw_list,
    const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& pins,
    const float                                                     edge_x,
    const bool                                                      right_edge
)
{
    // Issue #251: pins draw and hit-test in screen space at the zoomed size.
    // edge_x is the node border in screen coordinates, cell_center_y is a
    // screen-space ImGui item rect, and the half extent scales with the view.
    // The visible square draws to the screen-space draw list; PinRect stores
    // the editor's pin bounds in CANVAS units (mapped back with ScreenToCanvas)
    // which the editor draws via its own screen mapping and hit-tests in canvas.
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

} // namespace editor
