#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/geometry_graph_node_factory.hpp"
#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/geometry_graph_operations.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "geometry_graph/nodes/geometry_output_node.hpp"
#include "geometry_graph/nodes/geometry_source_nodes.hpp"
#include "geometry_graph/nodes/conway_node.hpp"
#include "geometry_graph/nodes/group_nodes.hpp"

#include "app_context.hpp"
#include "brushes/brush.hpp"
#include "app_scenes.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_log.hpp"
#include "items.hpp"
#include "content_library/content_library.hpp"
#include "graph_editor/graph_clipboard.hpp"
#include "operations/compound_operation.hpp"
#include "operations/operation_stack.hpp"
#include "preview/brush_preview.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "windows/inventory_slot_payload.hpp"
#include "windows/item_reference.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>
#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <cctype>

namespace editor {

namespace {

// Incremental evaluation: structural edits mark only the directly affected
// nodes dirty; Geometry_graph::evaluate() propagates dirtiness downstream.
void mark_sink_node_dirty(erhe::graph::Pin* sink_pin)
{
    Geometry_graph_node* sink_node = dynamic_cast<Geometry_graph_node*>(sink_pin->get_owner_node());
    if (sink_node != nullptr) {
        sink_node->mark_dirty();
    }
}

}

Geometry_graph_window::Geometry_graph_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context,
    std::string_view             title,
    std::string_view             ini_label
)
    : Graph_editor_window_base{imgui_renderer, imgui_windows, title, ini_label}
    , m_app_context           {app_context}
{
    // The window edits an explicit target Graph_mesh (issue #252); when the
    // target is unset it shows an empty state (see resolve_target).
    m_node_editor = std::make_unique<ax::NodeEditor::EditorContext>(nullptr);

    // Input pins are drawn with PivotAlignment {-0.75, 0.5}, which leaves a
    // gap between the link end and the pin for an arrowhead. The default
    // style has PinArrowSize/Width = 0 (no arrowhead), so match the Graph
    // window's arrow settings or the gap renders empty.
    ax::NodeEditor::Style& style = m_node_editor->GetStyle();
    style.PinArrowSize  = 14.0f;
    style.PinArrowWidth = 14.0f;

    // Node edges / corners get sizing cursors and left-drag resizing; the
    // resulting size is adopted into the node's requested extent each frame
    // (apply_node_resize, after the canvas End()).
    m_node_editor->EnableNodeResize(true);
}

Geometry_graph_window::~Geometry_graph_window() noexcept
{
}

auto Geometry_graph_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void Geometry_graph_window::resolve_target()
{
    // Issue #252: the window edits an explicit target, not the global
    // selection. Lock the weak_ptr; a deleted asset resolves to null.
    std::shared_ptr<Graph_mesh> target = m_target.lock();
    // Self-healing against the scene-close bug class (see CLAUDE.md
    // "Scene-hosted references in editor parts"): weak_ptr expiry can
    // never signal a scene close, because this window's own m_graph_mesh
    // shared_ptr keeps the asset alive. Drop a target whose hosting scene
    // is no longer registered so the window cannot keep showing (and
    // editing) closed-scene content. A host-less asset (library orphan,
    // e.g. removed from the library while an undo entry keeps it alive)
    // stays editable, as before.
    if (target && (m_app_context.app_scenes != nullptr)) {
        erhe::Item_host* const item_host = target->get_item_host();
        if ((item_host != nullptr) && !m_app_context.app_scenes->is_host_registered(item_host)) {
            m_target.reset();
            target.reset();
        }
    }
    if (m_graph_mesh != target) {
        m_graph_mesh = target;
        // Restart the spawn grid for the newly edited graph so new nodes do
        // not continue from another graph's high-water mark.
        m_spawn_count = 0;
    }
}

void Geometry_graph_window::set_target(const std::shared_ptr<Graph_mesh>& graph_mesh)
{
    m_target = graph_mesh;
    resolve_target();
}

auto Geometry_graph_window::get_target() -> std::shared_ptr<Graph_mesh>
{
    resolve_target();
    return m_graph_mesh;
}

auto Geometry_graph_window::get_current_graph_mesh() -> const std::shared_ptr<Graph_mesh>&
{
    // Keep m_graph_mesh consistent with the live target regardless of call
    // ordering (an MCP mutation may arrive before the frame's update).
    resolve_target();
    return m_graph_mesh;
}

auto Geometry_graph_window::graph() -> Geometry_graph&
{
    return m_graph_mesh->graph();
}

auto Geometry_graph_window::mutable_nodes() -> std::vector<std::shared_ptr<Geometry_graph_node>>&
{
    return m_graph_mesh->nodes();
}

auto Geometry_graph_window::get_nodes() -> const std::vector<std::shared_ptr<Geometry_graph_node>>&
{
    // The MCP handlers resolve nodes through this before mutating, so it
    // must agree with the graph the mutation will target (resolve first).
    resolve_target();
    if (!m_graph_mesh) {
        static const std::vector<std::shared_ptr<Geometry_graph_node>> s_empty{};
        return s_empty; // empty state - no target
    }
    return m_graph_mesh->nodes();
}

void Geometry_graph_window::insert_node(Graph_mesh& graph_mesh, const std::shared_ptr<Geometry_graph_node>& node)
{
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    // The output node of a graph publishes to the owning asset (consumed
    // by Geometry_graph_mesh attachments); graphs only live in the content
    // library, so every node has an owning asset.
    node->set_owning_graph_mesh(std::dynamic_pointer_cast<Graph_mesh>(graph_mesh.shared_from_this()));
    graph_mesh.nodes().push_back(node);
    graph_mesh.graph().register_node(node.get());
    // A node entering the graph must re-evaluate even when it was clean
    // on removal (undo restore): the output node has to recreate its
    // scene mesh, released in on_removed_from_graph().
    node->mark_dirty();
}

void Geometry_graph_window::erase_node(Graph_mesh& graph_mesh, const std::shared_ptr<Geometry_graph_node>& node)
{
    std::vector<std::shared_ptr<Geometry_graph_node>>& nodes = graph_mesh.nodes();
    auto i = std::find(nodes.begin(), nodes.end(), node);
    if (i == nodes.end()) {
        return;
    }
    // Issue #252: canvas nodes are no longer pushed into the global
    // selection, so there is nothing to remove from it here.
    // Downstream nodes lose an input; capture them before
    // unregister_node() disconnects the links.
    for (erhe::graph::Pin& pin : node->get_output_pins()) {
        for (erhe::graph::Link* link : pin.get_links()) {
            mark_sink_node_dirty(link->get_sink());
        }
    }
    graph_mesh.graph().unregister_node(node.get());
    nodes.erase(i);
    node->on_removed_from_graph();
    // A designated (display / ghost) node leaving the graph re-bakes so the
    // scene falls back to the wired input; the designation id is kept so an
    // undo of this erase (same node object, same id) self-heals it.
    graph_mesh.graph().handle_designated_node_removed(node->get_id());
}

auto Geometry_graph_window::connect_pins(Graph_mesh& graph_mesh, erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool
{
    erhe::graph::Link* link = graph_mesh.graph().connect(source_pin, sink_pin);
    if (link == nullptr) {
        return false;
    }
    mark_sink_node_dirty(sink_pin);
    return true;
}

auto Geometry_graph_window::disconnect_pins(Graph_mesh& graph_mesh, erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool
{
    std::vector<std::unique_ptr<erhe::graph::Link>>& links = graph_mesh.graph().get_links();
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
    graph_mesh.graph().disconnect(i->get());
    mark_sink_node_dirty(sink_pin);
    return true;
}

auto Geometry_graph_window::get_node_position(const Graph_editor_node& node) -> ImVec2
{
    return m_node_editor->GetNodePosition(ax::NodeEditor::NodeId{node.get_id()});
}

void Geometry_graph_window::set_node_position(const Graph_editor_node& node, const ImVec2& position)
{
    m_node_editor->SetNodePosition(ax::NodeEditor::NodeId{node.get_id()}, position);
}

void Geometry_graph_window::select_canvas_nodes(const std::vector<std::size_t>& node_ids)
{
    m_node_editor->ClearSelection();
    for (const std::size_t node_id : node_ids) {
        m_node_editor->SelectNode(ax::NodeEditor::NodeId{node_id}, true);
    }
}

auto Geometry_graph_window::get_node_size(const Graph_editor_node& node) -> ImVec2
{
    return m_node_editor->GetNodeSize(ax::NodeEditor::NodeId{node.get_id()});
}

void Geometry_graph_window::collect_selected_nodes(std::vector<std::shared_ptr<Graph_editor_node>>& out)
{
    for (const std::shared_ptr<Geometry_graph_node>& node : get_nodes()) {
        if (m_node_editor->IsNodeSelected(ax::NodeEditor::NodeId{node->get_id()})) {
            out.push_back(node);
        }
    }
}

auto Geometry_graph_window::find_graph_editor_node(const std::size_t node_id) -> std::shared_ptr<Graph_editor_node>
{
    for (const std::shared_ptr<Geometry_graph_node>& node : get_nodes()) {
        if (node->get_id() == node_id) {
            return node;
        }
    }
    return {};
}

void Geometry_graph_window::remove_node(const std::shared_ptr<Geometry_graph_node>& node)
{
    // By value: get_current_graph_mesh() returns a reference to m_graph_mesh,
    // which a nested refresh would reassign under a reference binding.
    const std::shared_ptr<Graph_mesh> graph_mesh = get_current_graph_mesh();
    if (!graph_mesh) {
        return; // empty state - no asset selected
    }
    m_app_context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_node_insert_remove_operation>(
            *this, graph_mesh, node, Geometry_graph_node_insert_remove_operation::Mode::remove
        )
    );
}

auto Geometry_graph_window::connect(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool
{
    if ((source_pin == nullptr) || (sink_pin == nullptr) || (source_pin->get_key() != sink_pin->get_key())) {
        return false;
    }
    // By value: get_current_graph_mesh() returns a reference to m_graph_mesh,
    // which a nested refresh would reassign under a reference binding.
    const std::shared_ptr<Graph_mesh> graph_mesh = get_current_graph_mesh();
    if (!graph_mesh) {
        return false; // empty state - no asset selected
    }
    // Validate before creating the operation: a refused link must not
    // leave a no-op entry on the undo stack. (The cycle test is unaffected
    // by the sink pin's existing input links, so it can run before the
    // replace handling below.)
    if (graph_mesh->graph().would_create_cycle(source_pin, sink_pin)) {
        log_graph_editor->warn(
            "Geometry graph: connecting '{}' to '{}' would create a cycle - refusing",
            source_pin->get_owner_node()->get_name(),
            sink_pin  ->get_owner_node()->get_name()
        );
        return false;
    }
    // Connecting to a single-link input pin that already has a link replaces
    // it: the old link(s) are disconnected in the same undo step (Compound),
    // so one undo restores the previous wiring. Multi-input sockets (Join,
    // Instance points, Realize instances - Pin::allows_multiple_links) keep
    // every link and accumulate payloads instead.
    const std::vector<erhe::graph::Link*>& existing_links = sink_pin->get_links();
    if (!sink_pin->allows_multiple_links() && !existing_links.empty()) {
        Compound_operation::Parameters parameters;
        for (erhe::graph::Link* link : existing_links) {
            parameters.operations.push_back(
                std::make_shared<Geometry_graph_link_insert_remove_operation>(
                    *this, graph_mesh, link->get_source(), link->get_sink(), Geometry_graph_link_insert_remove_operation::Mode::remove
                )
            );
        }
        parameters.operations.push_back(
            std::make_shared<Geometry_graph_link_insert_remove_operation>(
                *this, graph_mesh, source_pin, sink_pin, Geometry_graph_link_insert_remove_operation::Mode::insert
            )
        );
        m_app_context.operation_stack->execute_now(std::make_shared<Compound_operation>(std::move(parameters)));
        return true;
    }
    m_app_context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_link_insert_remove_operation>(
            *this, graph_mesh, source_pin, sink_pin, Geometry_graph_link_insert_remove_operation::Mode::insert
        )
    );
    return true;
}

void Geometry_graph_window::disconnect(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin)
{
    const std::shared_ptr<Graph_mesh> graph_mesh = get_current_graph_mesh();
    if (!graph_mesh) {
        return; // empty state - no asset selected
    }
    m_app_context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_link_insert_remove_operation>(
            *this, graph_mesh, source_pin, sink_pin, Geometry_graph_link_insert_remove_operation::Mode::remove
        )
    );
}

void Geometry_graph_window::set_node_parameters(const std::shared_ptr<Geometry_graph_node>& node, const nlohmann::json& parameters)
{
    if (!node) {
        return;
    }
    std::string before_parameters = node->dump_parameters();
    node->read_parameters(parameters); // marks the node dirty
    std::string after_parameters = node->dump_parameters();
    m_app_context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_parameter_operation>(
            *this, node, std::move(before_parameters), std::move(after_parameters)
        )
    );
}

auto Geometry_graph_window::make_node(const std::string& type_name) -> std::shared_ptr<Geometry_graph_node>
{
    return make_geometry_graph_node(m_app_context, type_name);
}

auto Geometry_graph_window::next_node_spawn_position() -> ImVec2
{
    constexpr float step_x  = 320.0f; // node width is ~290 (70 + 150 + 70 pin/center columns)
    constexpr float step_y  = 200.0f;
    constexpr int   columns = 4;
    const int index  = m_spawn_count++;
    const int column = index % columns;
    const int row    = index / columns;
    return ImVec2{static_cast<float>(column) * step_x, static_cast<float>(row) * step_y};
}

auto Geometry_graph_window::add_node_of_type(const std::string& type_name, const ImVec2* position) -> Geometry_graph_node*
{
    const std::shared_ptr<Graph_mesh> graph_mesh = get_current_graph_mesh();
    if (!graph_mesh) {
        return nullptr; // empty state - no asset selected
    }
    const std::shared_ptr<Geometry_graph_node> node = make_node(type_name);
    if (!node) {
        return nullptr;
    }
    m_app_context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_node_insert_remove_operation>(
            *this, graph_mesh, node, Geometry_graph_node_insert_remove_operation::Mode::insert
        )
    );
    set_node_position(*node.get(), (position != nullptr) ? *position : next_node_spawn_position());
    return node.get();
}

void Geometry_graph_window::add_node_from_palette(const std::string& type_name, const ImVec2* spawn_position)
{
    add_node_of_type(type_name, spawn_position);
}

auto Geometry_graph_window::clipboard_kind() const -> const char*
{
    return "geometry_graph";
}

auto Geometry_graph_window::get_current_graph() -> erhe::graph::Graph*
{
    const std::shared_ptr<Graph_mesh>& graph_mesh = get_current_graph_mesh();
    return graph_mesh ? &graph_mesh->graph() : nullptr;
}

auto Geometry_graph_window::paste_nodes(const nlohmann::json& clipboard, const ImVec2& position) -> std::vector<std::size_t>
{
    // By value: get_current_graph_mesh() returns a reference to m_graph_mesh,
    // which a nested refresh would reassign under a reference binding.
    const std::shared_ptr<Graph_mesh> graph_mesh = get_current_graph_mesh();
    if (!graph_mesh) {
        return {}; // empty state - no asset selected
    }
    return paste_graph_nodes<Geometry_graph_op_traits>(*this, m_app_context, graph_mesh, clipboard, position, &make_geometry_graph_node);
}

void Geometry_graph_window::remove_nodes(const std::vector<std::shared_ptr<Graph_editor_node>>& nodes)
{
    const std::shared_ptr<Graph_mesh> graph_mesh = get_current_graph_mesh();
    if (!graph_mesh) {
        return; // empty state - no asset selected
    }
    remove_graph_nodes<Geometry_graph_op_traits>(*this, m_app_context, graph_mesh, nodes);
}

void Geometry_graph_window::build_palette()
{
    if (!m_palette_categories.empty()) {
        return; // built once - the node set is fixed at compile time
    }
    // Mirrors the texture graph palette structure (filter + collapsing
    // headers + selectables); the categories follow the old toolbar rows.
    m_palette_categories = {
        Palette_category{
            .name    = "Primitives",
            .entries = {
                Palette_entry{.type_name = "box",    .label = "Box"},
                Palette_entry{.type_name = "sphere", .label = "Sphere"},
                Palette_entry{.type_name = "torus",  .label = "Torus"},
                Palette_entry{.type_name = "cone",   .label = "Cone"},
                Palette_entry{.type_name = "disc",   .label = "Disc"}
            }
        },
        Palette_category{
            .name    = "Sources",
            .entries = {
                Palette_entry{.type_name = "brush",      .label = "Brush"},
                Palette_entry{.type_name = "scene_mesh", .label = "Scene Mesh"}
            }
        },
        Palette_category{
            .name    = "Operations",
            .entries = {
                Palette_entry{.type_name = "subdivide",   .label = "Subdivide"},
                Palette_entry{.type_name = "transform",   .label = "Transform"},
                Palette_entry{.type_name = "triangulate", .label = "Triangulate"},
                Palette_entry{.type_name = "normalize",   .label = "Normalize"},
                Palette_entry{.type_name = "reverse",     .label = "Reverse"},
                Palette_entry{.type_name = "repair",      .label = "Repair"}
            }
        },
        // The "Conway" category is inserted below, built from the operator
        // table (each operator is its own node type).
        Palette_category{
            .name    = "Points",
            .entries = {
                Palette_entry{.type_name = "distribute", .label = "Distribute Points"},
                Palette_entry{.type_name = "instance",   .label = "Instance on Points"},
                Palette_entry{.type_name = "realize",    .label = "Realize Instances"}
            }
        },
        Palette_category{
            .name    = "Combine",
            .entries = {
                Palette_entry{.type_name = "join",    .label = "Join"},
                Palette_entry{.type_name = "boolean", .label = "Boolean"}
            }
        },
        Palette_category{
            .name    = "Values",
            .entries = {
                Palette_entry{.type_name = "float",   .label = "Float"},
                Palette_entry{.type_name = "integer", .label = "Integer"},
                Palette_entry{.type_name = "vector",  .label = "Vector"},
                Palette_entry{.type_name = "math",    .label = "Math"}
            }
        },
        Palette_category{
            .name    = "Utility",
            .entries = {
                Palette_entry{.type_name = "passthrough", .label = "Passthrough"}
            }
        },
        Palette_category{
            .name    = "Groups",
            .entries = {
                Palette_entry{.type_name = "group_input",  .label = "Group Input"},
                Palette_entry{.type_name = "group_output", .label = "Group Output"},
                Palette_entry{.type_name = "group",        .label = "Group"}
            }
        },
        Palette_category{
            .name    = "Output",
            .entries = {
                Palette_entry{.type_name = "output", .label = "Output"}
            }
        }
    };

    // "Conway" group right after "Operations", one entry per operator, built
    // from the operator table so the palette cannot drift from the factory.
    Palette_category conway_category{.name = "Conway"};
    for (const Conway_node::Operation_info& info : Conway_node::c_operation_infos) {
        conway_category.entries.push_back(Palette_entry{.type_name = info.type_name, .label = info.label});
    }
    const auto operations_it = std::find_if(
        m_palette_categories.begin(), m_palette_categories.end(),
        [](const Palette_category& category) { return category.name == "Operations"; }
    );
    m_palette_categories.insert(
        (operations_it != m_palette_categories.end()) ? operations_it + 1 : m_palette_categories.end(),
        std::move(conway_category)
    );
}

auto Geometry_graph_window::is_evaluation_run_done() -> bool
{
    // pre: m_evaluation_run != nullptr
    std::lock_guard<std::mutex> lock{m_evaluation_run->mutex};
    return m_evaluation_run->done;
}

auto Geometry_graph_window::next_graph_needing_evaluation() -> std::shared_ptr<Graph_mesh>
{
    // The currently edited graph may be an orphan (asset removed from its
    // library by undo/delete while still selected); it is not reachable
    // through the content libraries below, but edits to it must still
    // evaluate or the get_geometry_graph barrier returns stale payloads.
    if (m_graph_mesh && m_graph_mesh->graph().is_evaluation_needed()) {
        return m_graph_mesh;
    }
    if (m_app_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_app_context.app_scenes->get_scene_roots()) {
            const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
            if (!content_library || !content_library->graph_meshes) {
                continue;
            }
            for (const std::shared_ptr<Graph_mesh>& graph_mesh : content_library->graph_meshes->get_all<Graph_mesh>()) {
                if (graph_mesh->graph().is_evaluation_needed()) {
                    return graph_mesh;
                }
            }
        }
    }
    return {};
}

void Geometry_graph_window::launch_evaluation(const std::shared_ptr<Graph_mesh>& graph_mesh)
{
    // Snapshot the live graph into a shadow clone the worker owns:
    // factory-built nodes carrying the same parameters, cached payloads
    // and dirty flags, plus the same links. Only the shadow is touched
    // off the main thread, so the live graph stays free for the UI and
    // for structural edits while the run is in flight.
    const std::vector<std::shared_ptr<Geometry_graph_node>>& live_nodes = graph_mesh->nodes();
    Geometry_graph&                                          live_graph = graph_mesh->graph();

    std::shared_ptr<Evaluation_run> run = std::make_shared<Evaluation_run>();
    run->target = graph_mesh;
    run->shadow_nodes.reserve(live_nodes.size());
    run->live_node_ids.reserve(live_nodes.size());
    for (const std::shared_ptr<Geometry_graph_node>& node : live_nodes) {
        const std::shared_ptr<Geometry_graph_node> shadow = make_geometry_graph_node(m_app_context, node->get_factory_type_name());
        if (!shadow) {
            // Should not happen (every insertable type has a factory
            // entry); keep shadow_nodes parallel to live_nodes so the
            // link recreation below cannot mis-wire, and skip this node.
            log_graph_editor->warn("Geometry graph evaluation: no factory for node type '{}'", node->get_factory_type_name());
            run->shadow_nodes.push_back({});
            run->live_node_ids.push_back(node->get_id());
            continue;
        }
        node->prepare_for_evaluation();
        nlohmann::json parameters = nlohmann::json::object();
        node->write_parameters(parameters);
        shadow->read_parameters(parameters);
        shadow->set_log_source_id(node->get_id());
        shadow->capture_evaluation_state(*node);
        const std::size_t input_count  = node->get_input_pins().size();
        const std::size_t output_count = node->get_output_pins().size();
        for (std::size_t slot = 0; slot < input_count; ++slot) {
            shadow->set_input(slot, node->get_input(slot));
        }
        for (std::size_t slot = 0; slot < output_count; ++slot) {
            shadow->set_output(slot, node->get_output(slot));
        }
        // read_parameters() marked the shadow dirty; the snapshot decides.
        if (node->is_dirty()) {
            shadow->mark_dirty();
        } else {
            shadow->clear_dirty();
        }
        run->shadow_graph.register_node(shadow.get());
        run->shadow_nodes.push_back(shadow);
        run->live_node_ids.push_back(node->get_id());
    }
    for (const std::unique_ptr<erhe::graph::Link>& link : live_graph.get_links()) {
        std::size_t source_index = live_nodes.size();
        std::size_t sink_index   = live_nodes.size();
        for (std::size_t i = 0, end = live_nodes.size(); i < end; ++i) {
            if (live_nodes[i].get() == link->get_source()->get_owner_node()) { source_index = i; }
            if (live_nodes[i].get() == link->get_sink  ()->get_owner_node()) { sink_index   = i; }
        }
        if ((source_index >= run->shadow_nodes.size()) || (sink_index >= run->shadow_nodes.size())) {
            continue;
        }
        Geometry_graph_node* shadow_source = run->shadow_nodes[source_index].get();
        Geometry_graph_node* shadow_sink   = run->shadow_nodes[sink_index].get();
        if ((shadow_source == nullptr) || (shadow_sink == nullptr)) {
            continue;
        }
        run->shadow_graph.connect(
            &shadow_source->get_output_pins().at(link->get_source()->get_slot()),
            &shadow_sink  ->get_input_pins ().at(link->get_sink  ()->get_slot())
        );
    }
    // Display / ghost designation follows the snapshot: shadow nodes carry
    // set_log_source_id(live id), so the live ids resolve unchanged in the
    // shadow graph (raw copy - the snapshot's per-node dirty flags are
    // authoritative and must not be disturbed).
    run->shadow_graph.copy_display_designation_from(live_graph);
    // Per-node previews: the worker builds a preview primitive for every
    // node it evaluates (mesh_memory's buffer writes are worker-safe here -
    // the output node already builds its renderable mesh on this worker).
    if ((m_app_context.editor_settings != nullptr) && m_app_context.editor_settings->graph_node_previews.enabled) {
        run->shadow_graph.set_preview_mesh_memory(m_app_context.mesh_memory);
    }
    // A fresh Geometry_graph is born forced-full (so a graph's first
    // evaluation runs every node); the snapshot's per-node dirty flags
    // already carry the live state, so discard the shadow's birth flag
    // and forward only an explicit live forced-full request.
    static_cast<void>(run->shadow_graph.consume_forced_full());
    if (live_graph.consume_forced_full()) {
        run->shadow_graph.mark_dirty();
    }
    // The snapshot now owns the dirty state; edits made while the run is
    // in flight re-mark nodes dirty and trigger the next run.
    for (const std::shared_ptr<Geometry_graph_node>& node : live_nodes) {
        node->clear_dirty();
    }

    m_evaluation_run = run;
    App_context& context = m_app_context;
    ++context.pending_async_ops;
    context.executor->silent_async(
        [run, &context]() {
            ++context.running_async_ops;
            // Geometry operations call into Geogram, whose assertion
            // mechanism throws by default; an exception escaping the task
            // would terminate the process. Catch at the task boundary:
            // the affected nodes simply keep stale payloads (their edits
            // re-mark them dirty).
            try {
                run->shadow_graph.evaluate_if_dirty();
            } catch (const std::exception& e) {
                log_graph_editor->error("Geometry graph evaluation failed: {}", e.what());
            } catch (...) {
                log_graph_editor->error("Geometry graph evaluation failed: unknown exception");
            }
            --context.running_async_ops;
            --context.pending_async_ops;
            {
                std::lock_guard<std::mutex> lock{run->mutex};
                run->done = true;
            }
            run->condition.notify_all();
        }
    );
}

void Geometry_graph_window::finish_evaluation()
{
    // pre: m_evaluation_run && done. Copy the shadow results back to the
    // target graph's live nodes (matched by id; nodes removed while the
    // run was in flight are skipped) and apply the output nodes'
    // evaluated scene products. Main thread.
    const std::shared_ptr<Evaluation_run> run = std::move(m_evaluation_run);
    const std::vector<std::shared_ptr<Geometry_graph_node>>& live_nodes = run->target->nodes();
    for (std::size_t i = 0, end = run->shadow_nodes.size(); i < end; ++i) {
        const std::size_t live_id = run->live_node_ids[i];
        Geometry_graph_node* live_node = nullptr;
        for (const std::shared_ptr<Geometry_graph_node>& node : live_nodes) {
            if (node->get_id() == live_id) {
                live_node = node.get();
                break;
            }
        }
        Geometry_graph_node* shadow_node = run->shadow_nodes[i].get();
        if ((live_node == nullptr) || (shadow_node == nullptr)) {
            continue;
        }
        const std::size_t input_count  = std::min(live_node->get_input_pins ().size(), shadow_node->get_input_pins ().size());
        const std::size_t output_count = std::min(live_node->get_output_pins().size(), shadow_node->get_output_pins().size());
        for (std::size_t slot = 0; slot < input_count; ++slot) {
            live_node->set_input(slot, shadow_node->get_input(slot));
        }
        for (std::size_t slot = 0; slot < output_count; ++slot) {
            live_node->set_output(slot, shadow_node->get_output(slot));
        }
        live_node->take_preview(*shadow_node);
        Geometry_output_node* live_output   = dynamic_cast<Geometry_output_node*>(live_node);
        Geometry_output_node* shadow_output = dynamic_cast<Geometry_output_node*>(shadow_node);
        if ((live_output != nullptr) && (shadow_output != nullptr)) {
            live_output->take_evaluated(*shadow_output);
            live_output->apply_evaluated_to_scene();
        }
        Group_node* live_group   = dynamic_cast<Group_node*>(live_node);
        Group_node* shadow_group = dynamic_cast<Group_node*>(shadow_node);
        if ((live_group != nullptr) && (shadow_group != nullptr)) {
            live_group->adopt_subgraph_outputs(*shadow_group);
        }
    }
    // An asset-owned output node just published fresh baked products to
    // the target asset; push them to every bound attachment. Evaluation
    // finishes are rare, so the scene sweep costs nothing per frame.
    apply_baked_products_to_attachments(run->target);
}

void Geometry_graph_window::apply_baked_products_to_attachments(const std::shared_ptr<Graph_mesh>& graph_mesh)
{
    if (m_app_context.app_scenes == nullptr) {
        return;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : m_app_context.app_scenes->get_scene_roots()) {
        for (const std::shared_ptr<erhe::scene::Node>& node : scene_root->get_scene().get_flat_nodes()) {
            const std::shared_ptr<Geometry_graph_mesh> attachment = erhe::scene::get_attachment<Geometry_graph_mesh>(node.get());
            if (attachment && (attachment->get_graph_mesh() == graph_mesh)) {
                attachment->apply_baked_products();
            }
        }
    }
}

void Geometry_graph_window::update_evaluation()
{
    // Once-per-frame refresh (the template's Texture_graph_window::update()
    // parity): keeps m_graph_mesh tracking the selection even when the
    // window is closed, so MCP reads never see a stale graph.
    resolve_target();

    process_attachment_push_requests();

    if (m_evaluation_run) {
        if (is_evaluation_run_done()) {
            finish_evaluation();
        }
    }
    if (!m_evaluation_run) {
        const std::shared_ptr<Graph_mesh> next = next_graph_needing_evaluation();
        if (next) {
            launch_evaluation(next);
        }
    }

    update_node_previews();
}

void Geometry_graph_window::set_node_previews_enabled(const bool enabled)
{
    if (m_app_context.editor_settings == nullptr) {
        return;
    }
    Graph_node_previews_config& previews = m_app_context.editor_settings->graph_node_previews;
    if (previews.enabled == enabled) {
        return;
    }
    previews.enabled = enabled;
    if (!enabled) {
        return;
    }
    // Preview primitives are only built when a node evaluates; force a full
    // run of every graph so clean nodes get previews too (the setting is
    // editor-global, so every Graph_mesh in every scene participates).
    const auto mark_graph = [](const std::shared_ptr<Graph_mesh>& graph_mesh) {
        if (graph_mesh) {
            graph_mesh->graph().mark_dirty();
        }
    };
    mark_graph(m_graph_mesh);
    if (m_app_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_app_context.app_scenes->get_scene_roots()) {
            const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
            if (!content_library || !content_library->graph_meshes) {
                continue;
            }
            for (const std::shared_ptr<Graph_mesh>& graph_mesh : content_library->graph_meshes->get_all<Graph_mesh>()) {
                if (graph_mesh != m_graph_mesh) {
                    mark_graph(graph_mesh);
                }
            }
        }
    }
}

void Geometry_graph_window::update_node_previews()
{
    // Renders pending per-node preview thumbnails (armed by
    // finish_evaluation via take_preview) into per-node textures, a few
    // per frame. Reuses Brush_preview's offscreen scene; needs a recording
    // command buffer (present during the normal frame update).
    if ((m_app_context.current_command_buffer == nullptr) || (m_app_context.brush_preview == nullptr)) {
        return;
    }
    // Previews are an editor-global setting now (graph_node_previews).
    if ((m_app_context.editor_settings == nullptr) || !m_app_context.editor_settings->graph_node_previews.enabled) {
        return;
    }
    // Previews are cached textures; a change to the edge-line settings must
    // arm a re-render on every existing preview (nodes without a preview
    // primitive skip the flag). First frame just records the snapshot.
    const Preview_edge_lines_config* edge_lines = (m_app_context.editor_settings != nullptr)
        ? &m_app_context.editor_settings->graph_node_preview_edge_lines
        : nullptr;
    if (edge_lines != nullptr) {
        const bool changed =
            (edge_lines->enabled != m_preview_edge_lines_snapshot.enabled) ||
            (edge_lines->width   != m_preview_edge_lines_snapshot.width  ) ||
            (edge_lines->color   != m_preview_edge_lines_snapshot.color  );
        if (changed) {
            if (m_preview_edge_lines_snapshot_valid) {
                const auto arm_graph = [](const std::shared_ptr<Graph_mesh>& graph_mesh) {
                    if (!graph_mesh) {
                        return;
                    }
                    for (const std::shared_ptr<Geometry_graph_node>& node : graph_mesh->nodes()) {
                        node->mark_preview_needs_render();
                    }
                };
                arm_graph(m_graph_mesh);
                if (m_app_context.app_scenes != nullptr) {
                    for (const std::shared_ptr<Scene_root>& scene_root : m_app_context.app_scenes->get_scene_roots()) {
                        const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
                        if (!content_library || !content_library->graph_meshes) {
                            continue;
                        }
                        for (const std::shared_ptr<Graph_mesh>& graph_mesh : content_library->graph_meshes->get_all<Graph_mesh>()) {
                            if (graph_mesh != m_graph_mesh) {
                                arm_graph(graph_mesh);
                            }
                        }
                    }
                }
            }
            m_preview_edge_lines_snapshot = *edge_lines;
        }
        m_preview_edge_lines_snapshot_valid = true;
    }
    int budget = 2;
    const auto render_graph_previews = [this, &budget, edge_lines](const std::shared_ptr<Graph_mesh>& graph_mesh) {
        if (!graph_mesh) {
            return;
        }
        for (const std::shared_ptr<Geometry_graph_node>& node : graph_mesh->nodes()) {
            if (budget <= 0) {
                return;
            }
            if (!node->get_preview_primitive()) {
                continue;
            }
            // The render-target resolution follows the preview's on-canvas
            // display size (zoom-scaled, quantized) so a zoomed-in node is
            // rendered sharp instead of upscaled; a size change forces a
            // re-render even when the geometry is unchanged.
            const int                                size_pixels = node->get_preview_desired_texture_size();
            std::shared_ptr<erhe::graphics::Texture> texture     = node->get_preview_texture();
            const bool wrong_size = !texture || (texture->get_width() != size_pixels);
            if (!node->preview_needs_render() && !wrong_size) {
                continue;
            }
            if (wrong_size) {
                node->set_preview_texture(
                    std::make_shared<erhe::graphics::Texture>(
                        *m_app_context.graphics_device,
                        erhe::graphics::Texture_create_info{
                            .device      = *m_app_context.graphics_device,
                            .usage_mask  =
                                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                                erhe::graphics::Image_usage_flag_bit_mask::sampled |
                                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                            .type        = erhe::graphics::Texture_type::texture_2d,
                            .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
                            .use_mipmaps = true,
                            .width       = size_pixels,
                            .height      = size_pixels,
                            .debug_label = "Geometry graph node preview"
                        }
                    )
                );
            }
            // Headlight shading: N.V-dimmed neutral look (reads curvature
            // better than the brush preview material). Orientation is the
            // node's persistent arcball / hover-spin orientation.
            m_app_context.brush_preview->render_preview(node->get_preview_texture(), 0, node->get_preview_primitive(), {}, node->get_preview_orientation(), true, edge_lines);
            node->clear_preview_needs_render();
            --budget;
        }
    };
    // The currently edited graph first (it may be a library orphan), then
    // every scene's Graph_mesh assets (previews may be enabled on graphs
    // edited by other window instances).
    render_graph_previews(m_graph_mesh);
    if (m_app_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_app_context.app_scenes->get_scene_roots()) {
            const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
            if (!content_library || !content_library->graph_meshes) {
                continue;
            }
            for (const std::shared_ptr<Graph_mesh>& graph_mesh : content_library->graph_meshes->get_all<Graph_mesh>()) {
                if (graph_mesh != m_graph_mesh) {
                    render_graph_previews(graph_mesh);
                }
            }
        }
    }
}

void Geometry_graph_window::process_attachment_push_requests()
{
    // Out-of-band pushes (no evaluation involved): a bound node re-entered
    // a scene after missing a push, or an output node left an asset graph
    // publishing an empty bake. Steady-state cost is one bool per graph.
    // The currently edited graph is checked directly because it may be a
    // library orphan (asset removed by undo/delete while still selected).
    if (m_graph_mesh && m_graph_mesh->consume_attachment_push_request()) {
        apply_baked_products_to_attachments(m_graph_mesh);
    }
    if (m_app_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_app_context.app_scenes->get_scene_roots()) {
            const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
            if (!content_library || !content_library->graph_meshes) {
                continue;
            }
            for (const std::shared_ptr<Graph_mesh>& graph_mesh : content_library->graph_meshes->get_all<Graph_mesh>()) {
                if (graph_mesh->consume_attachment_push_request()) {
                    apply_baked_products_to_attachments(graph_mesh);
                }
            }
        }
    }
}

void Geometry_graph_window::wait_for_idle_evaluation()
{
    // Multi-round: finishing a run can leave a graph dirty again (edits
    // made while it ran), and other graphs may be waiting their turn
    // behind the single in-flight run.
    for (;;) {
        if (m_evaluation_run) {
            {
                std::unique_lock<std::mutex> lock{m_evaluation_run->mutex};
                m_evaluation_run->condition.wait(lock, [this]() { return m_evaluation_run->done; });
            }
            finish_evaluation();
        }
        const std::shared_ptr<Graph_mesh> next = next_graph_needing_evaluation();
        if (!next) {
            return;
        }
        launch_evaluation(next);
    }
}

void Geometry_graph_window::target_selector_imgui()
{
    // Rebuild the picker candidate list: every Graph_mesh asset in every
    // scene's content library. Scratch member reused across frames.
    m_target_candidates.clear();
    if (m_app_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_app_context.app_scenes->get_scene_roots()) {
            const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
            if (!content_library || !content_library->graph_meshes) {
                continue;
            }
            for (const std::shared_ptr<Graph_mesh>& graph_mesh : content_library->graph_meshes->get_all<Graph_mesh>()) {
                m_target_candidates.push_back(graph_mesh);
            }
        }
    }
    ImGui::TextUnformatted("Target");
    ImGui::SameLine();
    Item_reference_options options;
    options.candidates                  = m_target_candidates;
    options.accept_content_library_node = true;
    options.none_text                   = "(no target)";
    options.show_select_button          = false; // keep target decoupled from the global selection
    if (item_reference_imgui<Graph_mesh>(m_app_context, "geometry_graph_target", m_target, Graph_mesh::get_static_type(), options)) {
        // Target changed via the selector; resolve now so the canvas reflects it this frame.
        resolve_target();
    }
}

void Geometry_graph_window::controls_imgui()
{
    resolve_target();
    if (!m_graph_mesh) {
        ImGui::TextUnformatted("No target Graph Mesh.");
        ImGui::TextUnformatted("Pick one in the Geometry Graph window's Target selector, or 'Open Editor' on a Graph Mesh in the Content Library.");
        return;
    }
    ImGui::Text("Editing: %s", m_graph_mesh->get_name().c_str());
    // Per-node mesh preview thumbnails on the canvas; editor-global and
    // persistent (Editor_settings_config::graph_node_previews), on by
    // default. Auto-rotate spins a hovered node's preview (paused while
    // drag-rotating it).
    if (m_app_context.editor_settings != nullptr) {
        Graph_node_previews_config& previews = m_app_context.editor_settings->graph_node_previews;
        bool node_previews = previews.enabled;
        if (ImGui::Checkbox("Show node previews", &node_previews)) {
            set_node_previews_enabled(node_previews);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-rotate", &previews.auto_rotate);
    }
    ImGui::Separator();
    node_palette();

    if (m_evaluation_run) {
        ImGui::TextUnformatted("Evaluating graph in background...");
    }
}

void Geometry_graph_window::imgui()
{
    // One-shot focus request (set by set_node_editor_zoom): bring this window
    // to the front of its dock tab so headless zoom captures show the graph.
    if (m_focus_requested) {
        ImGui::SetWindowFocus();
        m_focus_requested = false;
    }

    // Issue #252: the target-item selector at the top of the window. Drawn
    // before resolve_target() so a pick / clear takes effect this frame.
    target_selector_imgui();

    // The canvas draws the explicit target graph (issue #252).
    resolve_target();
    if (!m_graph_mesh) {
        ImGui::TextUnformatted("No target Graph Mesh. Pick one above, or 'Open Editor' on a Graph Mesh in the Content Library.");
        return;
    }

    // Issue #251 (bug a): remember where the canvas starts so the zoom-level
    // overlay can be drawn over its top-left corner after End(). The size is
    // captured too, for the drag-and-drop target rect below.
    const ImVec2 canvas_screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_avail_size = ImGui::GetContentRegionAvail();

    m_node_editor->Begin("Geometry Graph", ImVec2{0.0f, 0.0f});

    for (erhe::graph::Node* node : graph().get_nodes()) {
        Geometry_graph_node* geometry_graph_node = dynamic_cast<Geometry_graph_node*>(node);
        if (geometry_graph_node != nullptr) {
            geometry_graph_node->node_editor(m_app_context, *m_node_editor.get());
        }
    }

    for (const std::unique_ptr<erhe::graph::Link>& link : graph().get_links()) {
        m_node_editor->Link(
            ax::NodeEditor::LinkId{link.get()},
            ax::NodeEditor::PinId{link->get_source()},
            ax::NodeEditor::PinId{link->get_sink()}
        );
    }

    // Selection deferred by last frame's paste / duplicate (the pasted nodes
    // exist on the canvas only after the node draw loop above).
    apply_pending_canvas_selection(*m_node_editor.get());

    handle_link_create();
    handle_deletions();

    // Ctrl+X / Ctrl+C / Ctrl+V / Ctrl+D on the focused canvas (shared with
    // the texture graph via Graph_editor_window_base).
    handle_clipboard_shortcuts(*m_node_editor.get());

    // Right-click menus: Cut / Copy / Paste / Duplicate / Delete on a node,
    // Paste + "Add node" on the canvas background (shared with the texture
    // graph via Graph_editor_window_base).
    node_context_menu(*m_node_editor.get());
    node_background_context_menu(*m_node_editor.get());

    m_node_editor->End();

    // Interactive node resizing (edge / corner drags): adopt the dragged
    // size into the node's requested extent.
    apply_node_resize(*m_node_editor.get());

    // Dropping a content-library brush on the canvas creates a Brush source
    // node at the drop position.
    canvas_drag_and_drop_target(
        canvas_screen_pos,
        ImVec2{canvas_screen_pos.x + canvas_avail_size.x, canvas_screen_pos.y + canvas_avail_size.y}
    );

    // Issue #251 (bug a): show the current canvas zoom in the corner so
    // rendering issues that appear only at certain zoom levels are diagnosable
    // at a glance (and correlate with the erhe.imgui.node_editor log traces).
    const float zoom = m_node_editor->GetCurrentZoom();
    char zoom_text[32];
    snprintf(zoom_text, sizeof(zoom_text), "Zoom: %.2f", zoom);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2{canvas_screen_pos.x + 8.0f, canvas_screen_pos.y + 8.0f},
        IM_COL32(220, 220, 220, 220),
        zoom_text
    );
}

void Geometry_graph_window::canvas_drag_and_drop_target(const ImVec2& rect_min, const ImVec2& rect_max)
{
    // Custom target over the whole canvas rect (the ax::NodeEditor canvas is
    // not a single ImGui item, so BeginDragDropTarget() cannot latch onto it).
    // Accepts a content-library brush (the same "Content_library_node" payload
    // the item tree sets and the 3D viewport accepts) and creates a Brush
    // source node at the drop position, bound to the dropped brush.
    const ImGuiID drag_target_id = ImGui::GetID(static_cast<const void*>(this));
    const ImRect  rect{rect_min, rect_max};
    if (!ImGui::BeginDragDropTargetCustom(rect, drag_target_id)) {
        return;
    }
    ERHE_DEFER( ImGui::EndDragDropTarget(); );

    // Node types dragged from this editor's palette spawn at the drop
    // position (with their own ghost preview while in flight).
    accept_palette_drop(*m_node_editor.get(), rect_min, rect_max);

    // Peek at the payloads while the drag is still in flight so a ghost of
    // the prospective node can be drawn at the cursor; IsDelivery() below
    // tells the actual drop apart. The default whole-canvas highlight is
    // replaced by that ghost. A brush arrives either as a content-library
    // node (item tree drag) or inside an inventory / hotbar brush slot
    // (Inventory window drag); a scene mesh arrives as a hierarchy drag of
    // the mesh attachment ("Mesh") or of a node carrying one ("Node").
    std::shared_ptr<Brush>             brush{};
    std::shared_ptr<erhe::scene::Mesh> mesh{};
    bool                               delivery = false;
    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
        "Content_library_node",
        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect
    );
    const ImGuiPayload* slot_payload = (payload == nullptr)
        ? ImGui::AcceptDragDropPayload(
            c_inventory_slot_payload_type,
            ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect
        )
        : nullptr;
    const ImGuiPayload* item_payload = ((payload == nullptr) && (slot_payload == nullptr))
        ? ImGui::AcceptDragDropPayload(
            erhe::scene::Node::static_type_name.data(),
            ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect
        )
        : nullptr;
    if ((payload == nullptr) && (slot_payload == nullptr) && (item_payload == nullptr)) {
        item_payload = ImGui::AcceptDragDropPayload(
            erhe::scene::Mesh::static_type_name.data(),
            ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect
        );
    }
    if (payload != nullptr) {
        erhe::Item_base* const      item_base    = *(static_cast<erhe::Item_base**>(payload->Data));
        const Content_library_node* library_node = dynamic_cast<const Content_library_node*>(item_base);
        if (library_node == nullptr) {
            return;
        }
        brush = std::dynamic_pointer_cast<Brush>(library_node->item);
        delivery = payload->IsDelivery();
    } else if (slot_payload != nullptr) {
        const Slot_drag_payload& slot = *static_cast<const Slot_drag_payload*>(slot_payload->Data);
        if (slot.brush != nullptr) {
            brush = std::dynamic_pointer_cast<Brush>(slot.brush->shared_from_this());
        }
        delivery = slot_payload->IsDelivery();
    } else if (item_payload != nullptr) {
        // Hierarchy drag: resolve the mesh from the dragged item (a Mesh
        // directly, or a Node's mesh attachment).
        erhe::Item_base* const item_base = *(static_cast<erhe::Item_base**>(item_payload->Data));
        mesh = erhe::scene::get_mesh(item_base->shared_from_this());
        delivery = item_payload->IsDelivery();
    } else {
        return; // no spawnable payload dragged over the canvas
    }
    if (!brush && !mesh) {
        return; // only brushes and scene meshes make geometry source nodes
    }

    if (!delivery) {
        // Still dragging: preview where the node would land, as a ghost of
        // the typical source node footprint (pin columns + center column +
        // NodePadding wide; header, picker, preview image and stats tall for
        // a brush, no preview image for a scene mesh - the real node is
        // content-sized on spawn).
        const char*  label     = brush ? brush->get_name().c_str() : mesh->get_name().c_str();
        const ImVec2 footprint = brush ? ImVec2{306.0f, 250.0f} : ImVec2{306.0f, 150.0f};
        draw_canvas_drop_ghost(*m_node_editor.get(), rect_min, rect_max, label, footprint);
        return;
    }

    // The drop happens outside the canvas Begin/End (screen space), so
    // convert through the editor's stored view transform.
    const ImVec2 spawn_position = m_node_editor->ScreenToCanvas(ImGui::GetMousePos());
    if (brush) {
        Geometry_graph_node* node = add_node_of_type("brush", &spawn_position);
        Brush_geometry_node* brush_node = dynamic_cast<Brush_geometry_node*>(node);
        if (brush_node != nullptr) {
            // Undo of the drop removes the node object itself (the insert
            // operation keeps it alive), so the brush binding survives redo.
            brush_node->set_brush(brush);
        }
    } else {
        Geometry_graph_node*      node      = add_node_of_type("scene_mesh", &spawn_position);
        Scene_mesh_geometry_node* mesh_node = dynamic_cast<Scene_mesh_geometry_node*>(node);
        if (mesh_node != nullptr) {
            // Same undo note as the brush binding above.
            mesh_node->set_mesh(mesh);
        }
    }
}

void Geometry_graph_window::set_node_editor_zoom(float zoom)
{
    show_window();            // ensure the window is visible so it renders for a capture
    m_focus_requested = true; // and bring it to the front of its dock tab
    if (m_node_editor) {
        m_node_editor->SetZoom(zoom);
    }
}

void Geometry_graph_window::handle_link_create()
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
                    !graph().would_create_cycle(source_pin, sink_pin)
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

void Geometry_graph_window::handle_deletions()
{
    if (m_node_editor->BeginDelete()) {
        ax::NodeEditor::NodeId node_handle = 0;
        while (m_node_editor->QueryDeletedNode(&node_handle)){
            if (m_node_editor->AcceptDeletedItem()) {
                std::vector<std::shared_ptr<Geometry_graph_node>>& nodes = mutable_nodes();
                auto i = std::find_if(nodes.begin(), nodes.end(), [node_handle](const std::shared_ptr<Geometry_graph_node>& entry){
                    ax::NodeEditor::NodeId entry_node_id = ax::NodeEditor::NodeId{entry->get_id()};
                    return entry_node_id == node_handle;
                });
                if (i != nodes.end()) {
                    const std::shared_ptr<Geometry_graph_node> geometry_graph_node = *i; // copy - remove_node() erases from the node vector
                    remove_node(geometry_graph_node);
                }
            }
        }

        ax::NodeEditor::LinkId link_handle;
        while (m_node_editor->QueryDeletedLink(&link_handle)) {
            if (m_node_editor->AcceptDeletedItem()) {
                std::vector<std::unique_ptr<erhe::graph::Link>>& links = graph().get_links();
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

}
