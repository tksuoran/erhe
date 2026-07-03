#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "texture_graph/texture_graph_window.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/texture_graph_node_factory.hpp"
#include "texture_graph/texture_graph_operations.hpp"
#include "texture_graph/texture_renderer.hpp"
#include "texture_graph/nodes/texture_node_descriptors.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "operations/operation_stack.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_texgen/node_descriptor.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace editor {

namespace {

// Incremental evaluation: structural edits mark only the directly affected
// nodes dirty; Texture_graph::evaluate() propagates dirtiness downstream.
void mark_sink_node_dirty(erhe::graph::Pin* sink_pin)
{
    Texture_graph_node* sink_node = dynamic_cast<Texture_graph_node*>(sink_pin->get_owner_node());
    if (sink_node != nullptr) {
        sink_node->mark_dirty();
    }
}

} // namespace

Texture_graph_window::Texture_graph_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Texture Graph", "texture_graph"}
    , m_app_context            {app_context}
{
    m_node_editor = std::make_unique<ax::NodeEditor::EditorContext>(nullptr);

    // Match the arrowhead settings used for the input pin pivot (the pins are
    // drawn with PivotAlignment {-0.75, 0.5}, which leaves a gap for an
    // arrowhead); the default style has zero arrow size so the gap would
    // otherwise render empty.
    ax::NodeEditor::Style& style = m_node_editor->GetStyle();
    style.PinArrowSize  = 14.0f;
    style.PinArrowWidth = 14.0f;

    // Self-consistency check for the ported node descriptors: the editor has no
    // gtest target, so compose every MVP descriptor standalone once at startup
    // and log any that fail to assemble (see doc/texture-graph-plan.md Phase 3
    // Step 2 verification). Pin <-> descriptor consistency is already
    // guaranteed by construction (build_pins_from_descriptor).
    const std::vector<std::string> descriptor_failures = check_texture_node_descriptors();
    if (descriptor_failures.empty()) {
        log_graph_editor->info("Texture graph: all {} node descriptors compose cleanly", all_texture_node_descriptors().size());
    } else {
        for (const std::string& failure : descriptor_failures) {
            log_graph_editor->error("Texture graph: descriptor compose error - {}", failure);
        }
    }
}

Texture_graph_window::~Texture_graph_window() noexcept
{
}

auto Texture_graph_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void Texture_graph_window::update()
{
    // The graphics device does not exist during construction (the part ctor
    // must not touch App_context), so create the render helper on first use.
    if (!m_renderer && (m_app_context.graphics_device != nullptr)) {
        m_renderer = std::make_unique<Texture_renderer>(*m_app_context.graphics_device);
    }

    m_graph.evaluate_if_dirty();
    render_dirty_products();
}

void Texture_graph_window::render_dirty_products()
{
    if (!m_renderer) {
        return;
    }
    erhe::graphics::Command_buffer* command_buffer = m_app_context.current_command_buffer;
    if ((command_buffer == nullptr) || !command_buffer->is_recording()) {
        return; // window hidden / no swapchain image this frame
    }

    m_renderer->begin_frame();
    for (const std::shared_ptr<Texture_graph_node>& node : m_nodes) {
        if (node->preview_needs_render()) {
            node->render_products(m_app_context, *m_renderer);
            node->clear_preview_needs_render();
        }
    }
}

void Texture_graph_window::insert_node(const std::shared_ptr<Texture_graph_node>& node)
{
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    m_nodes.push_back(node);
    m_graph.register_node(node.get());
    node->mark_dirty();
}

void Texture_graph_window::erase_node(const std::shared_ptr<Texture_graph_node>& node)
{
    auto i = std::find(m_nodes.begin(), m_nodes.end(), node);
    if (i == m_nodes.end()) {
        return;
    }
    if (node->is_selected()) {
        m_app_context.selection->remove_from_selection(node);
    }
    // Downstream nodes lose an input; capture them before unregister_node()
    // disconnects the links.
    for (erhe::graph::Pin& pin : node->get_output_pins()) {
        for (erhe::graph::Link* link : pin.get_links()) {
            mark_sink_node_dirty(link->get_sink());
        }
    }
    m_graph.unregister_node(node.get());
    m_nodes.erase(i);
    node->on_removed_from_graph();
}

auto Texture_graph_window::connect_pins(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool
{
    erhe::graph::Link* link = m_graph.connect(source_pin, sink_pin);
    if (link == nullptr) {
        return false;
    }
    mark_sink_node_dirty(sink_pin);
    return true;
}

auto Texture_graph_window::disconnect_pins(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool
{
    std::vector<std::unique_ptr<erhe::graph::Link>>& links = m_graph.get_links();
    auto i = std::find_if(
        links.begin(),
        links.end(),
        [source_pin, sink_pin](const std::unique_ptr<erhe::graph::Link>& entry) {
            return (entry->get_source() == source_pin) && (entry->get_sink() == sink_pin);
        }
    );
    if (i == links.end()) {
        return false;
    }
    m_graph.disconnect(i->get());
    mark_sink_node_dirty(sink_pin);
    return true;
}

auto Texture_graph_window::get_node_position(const Texture_graph_node& node) -> ImVec2
{
    return m_node_editor->GetNodePosition(ax::NodeEditor::NodeId{node.get_id()});
}

void Texture_graph_window::set_node_position(const Texture_graph_node& node, const ImVec2& position)
{
    m_node_editor->SetNodePosition(ax::NodeEditor::NodeId{node.get_id()}, position);
}

void Texture_graph_window::remove_node(const std::shared_ptr<Texture_graph_node>& node)
{
    m_app_context.operation_stack->execute_now(
        std::make_shared<Texture_graph_node_insert_remove_operation>(
            *this, node, Texture_graph_node_insert_remove_operation::Mode::remove
        )
    );
}

auto Texture_graph_window::connect(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool
{
    if ((source_pin == nullptr) || (sink_pin == nullptr) || (source_pin->get_key() != sink_pin->get_key())) {
        return false;
    }
    // Validate before creating the operation: a refused link must not leave a
    // no-op entry on the undo stack.
    if (m_graph.would_create_cycle(source_pin, sink_pin)) {
        log_graph_editor->warn(
            "Texture graph: connecting '{}' to '{}' would create a cycle - refusing",
            source_pin->get_owner_node()->get_name(),
            sink_pin  ->get_owner_node()->get_name()
        );
        return false;
    }
    m_app_context.operation_stack->execute_now(
        std::make_shared<Texture_graph_link_insert_remove_operation>(
            *this, source_pin, sink_pin, Texture_graph_link_insert_remove_operation::Mode::insert
        )
    );
    return true;
}

void Texture_graph_window::disconnect(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin)
{
    m_app_context.operation_stack->execute_now(
        std::make_shared<Texture_graph_link_insert_remove_operation>(
            *this, source_pin, sink_pin, Texture_graph_link_insert_remove_operation::Mode::remove
        )
    );
}

void Texture_graph_window::set_node_parameters(const std::shared_ptr<Texture_graph_node>& node, const nlohmann::json& parameters)
{
    std::string before_parameters = node->dump_parameters();
    node->read_parameters(parameters); // marks the node dirty
    std::string after_parameters = node->dump_parameters();
    m_app_context.operation_stack->execute_now(
        std::make_shared<Texture_graph_parameter_operation>(
            *this, node, std::move(before_parameters), std::move(after_parameters)
        )
    );
}

auto Texture_graph_window::make_node(const std::string& type_name) -> std::shared_ptr<Texture_graph_node>
{
    return make_texture_graph_node(m_app_context, type_name);
}

auto Texture_graph_window::next_node_spawn_position() -> ImVec2
{
    constexpr float step_x  = 320.0f; // node width is ~290 (70 + 150 + 70 pin/center columns)
    constexpr float step_y  = 200.0f;
    constexpr int   columns = 4;
    const int index  = m_spawn_count++;
    const int column = index % columns;
    const int row    = index / columns;
    return ImVec2{static_cast<float>(column) * step_x, static_cast<float>(row) * step_y};
}

auto Texture_graph_window::add_node_of_type(const std::string& type_name) -> Texture_graph_node*
{
    const std::shared_ptr<Texture_graph_node> node = make_node(type_name);
    if (!node) {
        return nullptr;
    }
    m_app_context.operation_stack->execute_now(
        std::make_shared<Texture_graph_node_insert_remove_operation>(
            *this, node, Texture_graph_node_insert_remove_operation::Mode::insert
        )
    );
    set_node_position(*node.get(), next_node_spawn_position());
    return node.get();
}

auto Texture_graph_window::get_graph() -> Texture_graph&
{
    return m_graph;
}

auto Texture_graph_window::get_nodes() const -> const std::vector<std::shared_ptr<Texture_graph_node>>&
{
    return m_nodes;
}

auto Texture_graph_window::get_renderer() -> Texture_renderer*
{
    // Mirror update()'s lazy creation so an MCP export issued before the first
    // frame's update() still has a renderer.
    if (!m_renderer && (m_app_context.graphics_device != nullptr)) {
        m_renderer = std::make_unique<Texture_renderer>(*m_app_context.graphics_device);
    }
    return m_renderer.get();
}

void Texture_graph_window::file_toolbar()
{
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("##graph_path", &m_graph_path);
    ImGui::SameLine(); if (ImGui::Button("Save"))  { save_graph(std::filesystem::path{m_graph_path}); }
    ImGui::SameLine(); if (ImGui::Button("Load"))  { load_graph(std::filesystem::path{m_graph_path}); }
    ImGui::SameLine(); if (ImGui::Button("Clear")) { clear_graph(); }
}

namespace {

[[nodiscard]] auto to_lower_ascii(std::string text) -> std::string
{
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

} // namespace

void Texture_graph_window::build_palette()
{
    if (!m_palette_categories.empty()) {
        return; // built once - the descriptor registry is immutable
    }
    // Fixed display order; any descriptor whose category is not in this list is
    // appended under its own header (or "Other" when the category is empty), so
    // a newly registered node type always appears without touching this code.
    const char* const category_order[] = { "Generators", "Patterns", "Filters", "Channels", "Utility" };

    const auto find_or_add_category = [this](const std::string& name) -> Palette_category& {
        for (Palette_category& category : m_palette_categories) {
            if (category.name == name) {
                return category;
            }
        }
        m_palette_categories.push_back(Palette_category{.name = name, .entries = {}});
        return m_palette_categories.back();
    };

    // Seed the fixed categories in order (kept only if non-empty, dropped below).
    for (const char* name : category_order) {
        find_or_add_category(name);
    }
    for (const erhe::texgen::Node_descriptor* descriptor : all_texture_node_descriptors()) {
        const std::string category_name = descriptor->category.empty() ? std::string{"Other"} : descriptor->category;
        Palette_category& category = find_or_add_category(category_name);
        const std::string label = descriptor->label.empty() ? descriptor->name : descriptor->label;
        category.entries.push_back(Palette_entry{.type_name = descriptor->name, .label = label});
    }
    // The output sink node has no descriptor; list it in its own category last.
    find_or_add_category("Output").entries.push_back(Palette_entry{.type_name = "output", .label = "Output"});

    // Drop any seeded-but-empty categories.
    m_palette_categories.erase(
        std::remove_if(
            m_palette_categories.begin(),
            m_palette_categories.end(),
            [](const Palette_category& category) { return category.entries.empty(); }
        ),
        m_palette_categories.end()
    );
}

void Texture_graph_window::node_palette()
{
    build_palette();

    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##palette_filter", "Filter nodes...", &m_palette_filter);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        m_palette_filter.clear();
    }

    const std::string filter    = to_lower_ascii(m_palette_filter);
    const bool        filtering = !filter.empty();

    const auto entry_matches = [&filter](const Palette_entry& entry) -> bool {
        return (to_lower_ascii(entry.label).find(filter) != std::string::npos) ||
               (to_lower_ascii(entry.type_name).find(filter) != std::string::npos);
    };

    for (const Palette_category& category : m_palette_categories) {
        bool any_match = !filtering;
        if (filtering) {
            for (const Palette_entry& entry : category.entries) {
                if (entry_matches(entry)) {
                    any_match = true;
                    break;
                }
            }
            if (!any_match) {
                continue; // hide categories with no matching entry while filtering
            }
        }
        const ImGuiTreeNodeFlags flags = filtering ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        if (ImGui::CollapsingHeader(category.name.c_str(), flags)) {
            for (const Palette_entry& entry : category.entries) {
                if (filtering && !entry_matches(entry)) {
                    continue;
                }
                ImGui::PushID(entry.type_name.c_str());
                if (ImGui::Selectable(entry.label.c_str())) {
                    add_node_of_type(entry.type_name);
                }
                ImGui::PopID();
            }
        }
    }
}

void Texture_graph_window::imgui()
{
    file_toolbar();
    node_palette();

    m_node_editor->Begin("Texture Graph", ImVec2{0.0f, 0.0f});

    for (erhe::graph::Node* node : m_graph.get_nodes()) {
        Texture_graph_node* texture_graph_node = dynamic_cast<Texture_graph_node*>(node);
        if (texture_graph_node != nullptr) {
            texture_graph_node->node_editor(m_app_context, *m_node_editor.get());
        }
    }

    for (const std::unique_ptr<erhe::graph::Link>& link : m_graph.get_links()) {
        m_node_editor->Link(
            ax::NodeEditor::LinkId{link.get()},
            ax::NodeEditor::PinId{link->get_source()},
            ax::NodeEditor::PinId{link->get_sink()}
        );
    }

    handle_link_create();
    handle_deletions();

    m_node_editor->End();
}

void Texture_graph_window::handle_link_create()
{
    if (m_node_editor->BeginCreate()) {
        ax::NodeEditor::PinId lhs_pin_handle;
        ax::NodeEditor::PinId rhs_pin_handle;
        if (m_node_editor->QueryNewLink(&lhs_pin_handle, &rhs_pin_handle)) {
            bool acceptable = false;
            if (rhs_pin_handle && lhs_pin_handle) {
                erhe::graph::Pin*  lhs_pin     = lhs_pin_handle.AsPointer<erhe::graph::Pin>();
                erhe::graph::Pin*  rhs_pin     = rhs_pin_handle.AsPointer<erhe::graph::Pin>();
                erhe::graph::Pin*  source_pin  = lhs_pin->is_source() ? lhs_pin : rhs_pin->is_source() ? rhs_pin : nullptr;
                erhe::graph::Pin*  sink_pin    = lhs_pin->is_sink  () ? lhs_pin : rhs_pin->is_sink  () ? rhs_pin : nullptr;
                erhe::graph::Node* source_node = (source_pin != nullptr) ? source_pin->get_owner_node() : nullptr;
                erhe::graph::Node* sink_node   = (sink_pin   != nullptr) ? sink_pin  ->get_owner_node() : nullptr;
                if (
                    (source_pin != nullptr) &&
                    (sink_pin != nullptr) &&
                    (source_pin != sink_pin) &&
                    (source_node != sink_node) &&
                    (source_pin->get_key() == sink_pin->get_key()) &&
                    !m_graph.would_create_cycle(source_pin, sink_pin)
                ) {
                    acceptable = true;
                    if (m_node_editor->AcceptNewItem()) { // mouse released?
                        connect(source_pin, sink_pin);
                    }
                }
            }
            if (!acceptable) {
                m_node_editor->RejectNewItem();
            }
        }
    }
    m_node_editor->EndCreate();
}

void Texture_graph_window::handle_deletions()
{
    if (m_node_editor->BeginDelete()) {
        ax::NodeEditor::NodeId node_handle = 0;
        while (m_node_editor->QueryDeletedNode(&node_handle)) {
            if (m_node_editor->AcceptDeletedItem()) {
                auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node_handle](const std::shared_ptr<Texture_graph_node>& entry){
                    ax::NodeEditor::NodeId entry_node_id = ax::NodeEditor::NodeId{entry->get_id()};
                    return entry_node_id == node_handle;
                });
                if (i != m_nodes.end()) {
                    const std::shared_ptr<Texture_graph_node> texture_graph_node = *i; // copy - remove_node() erases from m_nodes
                    remove_node(texture_graph_node);
                }
            }
        }

        ax::NodeEditor::LinkId link_handle;
        while (m_node_editor->QueryDeletedLink(&link_handle)) {
            if (m_node_editor->AcceptDeletedItem()) {
                std::vector<std::unique_ptr<erhe::graph::Link>>& links = m_graph.get_links();
                auto i = std::find_if(links.begin(), links.end(), [link_handle](const std::unique_ptr<erhe::graph::Link>& entry){
                    ax::NodeEditor::LinkId entry_link_id = ax::NodeEditor::LinkId{entry.get()};
                    return entry_link_id == link_handle;
                });
                if (i != links.end()) {
                    disconnect((*i)->get_source(), (*i)->get_sink());
                }
            }
        }
    }
    m_node_editor->EndDelete();
}

} // namespace editor
