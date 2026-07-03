#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "texture_graph/texture_graph_window.hpp"
#include "texture_graph/texture_graph_node.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

#include <algorithm>

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
    m_graph.evaluate_if_dirty();
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
    // A later step wraps this in an undoable operation.
    erase_node(node);
}

auto Texture_graph_window::connect(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool
{
    if ((source_pin == nullptr) || (sink_pin == nullptr) || (source_pin->get_key() != sink_pin->get_key())) {
        return false;
    }
    if (m_graph.would_create_cycle(source_pin, sink_pin)) {
        log_graph_editor->warn(
            "Texture graph: connecting '{}' to '{}' would create a cycle - refusing",
            source_pin->get_owner_node()->get_name(),
            sink_pin  ->get_owner_node()->get_name()
        );
        return false;
    }
    // A later step wraps this in an undoable operation.
    return connect_pins(source_pin, sink_pin);
}

void Texture_graph_window::disconnect(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin)
{
    // A later step wraps this in an undoable operation.
    disconnect_pins(source_pin, sink_pin);
}

auto Texture_graph_window::make_node(const std::string& /*type_name*/) -> std::shared_ptr<Texture_graph_node>
{
    // The node factory arrives with the MVP node set in a later step; until
    // then there are no node types to create.
    return {};
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
    insert_node(node); // a later step wraps this in an undoable operation
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

void Texture_graph_window::node_toolbar()
{
    // The MVP node buttons (uniform, perlin, voronoi, blend, colorize,
    // output, ...) are added with the node factory in a later step.
    ImGui::TextUnformatted("No node types yet");
}

void Texture_graph_window::imgui()
{
    node_toolbar();

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
