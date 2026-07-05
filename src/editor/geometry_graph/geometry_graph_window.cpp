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
#include "geometry_graph/nodes/group_nodes.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "editor_log.hpp"
#include "items.hpp"
#include "content_library/content_library.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <imgui/imgui.h>
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
    App_context&                 app_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Geometry Graph", "geometry_graph"}
    , m_app_context            {app_context}
{
    // The window edits the selected content-library Graph_mesh; when none
    // is selected it shows an empty state (see refresh_current_graph_mesh).
    m_node_editor = std::make_unique<ax::NodeEditor::EditorContext>(nullptr);

    // Input pins are drawn with PivotAlignment {-0.75, 0.5}, which leaves a
    // gap between the link end and the pin for an arrowhead. The default
    // style has PinArrowSize/Width = 0 (no arrowhead), so match the Graph
    // window's arrow settings or the gap renders empty.
    ax::NodeEditor::Style& style = m_node_editor->GetStyle();
    style.PinArrowSize  = 14.0f;
    style.PinArrowWidth = 14.0f;
}

Geometry_graph_window::~Geometry_graph_window() noexcept
{
}

auto Geometry_graph_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void Geometry_graph_window::refresh_current_graph_mesh()
{
    std::shared_ptr<Graph_mesh> selected;
    if (m_app_context.selection != nullptr) {
        // Content-library assets are selected wrapped in a Content_library_node;
        // get<>() unwraps that (the pattern Properties uses for the selected
        // Material), unlike Selection::get_last_selected<>().
        selected = get<Graph_mesh>(m_app_context.selection->get_selected_items());
    }
    if (m_graph_mesh != selected) {
        m_graph_mesh = selected;
        // Restart the spawn grid for the newly edited graph so new nodes do
        // not continue from another graph's high-water mark.
        m_spawn_count = 0;
    }
}

auto Geometry_graph_window::get_current_graph_mesh() -> const std::shared_ptr<Graph_mesh>&
{
    // Keep m_graph_mesh consistent with the live selection regardless of call
    // ordering (an MCP mutation may arrive before the frame's update).
    refresh_current_graph_mesh();
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
    // must agree with the graph the mutation will target (refresh first).
    refresh_current_graph_mesh();
    if (!m_graph_mesh) {
        static const std::vector<std::shared_ptr<Geometry_graph_node>> s_empty{};
        return s_empty; // empty state - no asset selected
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
    if (node->is_selected()) {
        m_app_context.selection->remove_from_selection(node);
    }
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

auto Geometry_graph_window::get_node_position(const Geometry_graph_node& node) -> ImVec2
{
    return m_node_editor->GetNodePosition(ax::NodeEditor::NodeId{node.get_id()});
}

void Geometry_graph_window::set_node_position(const Geometry_graph_node& node, const ImVec2& position)
{
    m_node_editor->SetNodePosition(ax::NodeEditor::NodeId{node.get_id()}, position);
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
    // leave a no-op entry on the undo stack.
    if (graph_mesh->graph().would_create_cycle(source_pin, sink_pin)) {
        log_graph_editor->warn(
            "Geometry graph: connecting '{}' to '{}' would create a cycle - refusing",
            source_pin->get_owner_node()->get_name(),
            sink_pin  ->get_owner_node()->get_name()
        );
        return false;
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

auto Geometry_graph_window::add_node_of_type(const std::string& type_name) -> Geometry_graph_node*
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
    set_node_position(*node.get(), next_node_spawn_position());
    return node.get();
}

namespace {

[[nodiscard]] auto to_lower_ascii(std::string text) -> std::string
{
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

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
            .name    = "Operations",
            .entries = {
                Palette_entry{.type_name = "subdivide",   .label = "Subdivide"},
                Palette_entry{.type_name = "conway",      .label = "Conway"},
                Palette_entry{.type_name = "transform",   .label = "Transform"},
                Palette_entry{.type_name = "triangulate", .label = "Triangulate"},
                Palette_entry{.type_name = "normalize",   .label = "Normalize"},
                Palette_entry{.type_name = "reverse",     .label = "Reverse"},
                Palette_entry{.type_name = "repair",      .label = "Repair"}
            }
        },
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
}

void Geometry_graph_window::node_palette()
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
        nlohmann::json parameters = nlohmann::json::object();
        node->write_parameters(parameters);
        shadow->read_parameters(parameters);
        shadow->set_log_source_id(node->get_id());
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
    refresh_current_graph_mesh();

    process_attachment_push_requests();

    if (m_evaluation_run) {
        if (!is_evaluation_run_done()) {
            return;
        }
        finish_evaluation();
    }
    const std::shared_ptr<Graph_mesh> next = next_graph_needing_evaluation();
    if (next) {
        launch_evaluation(next);
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

void Geometry_graph_window::controls_imgui()
{
    refresh_current_graph_mesh();
    if (!m_graph_mesh) {
        ImGui::TextUnformatted("No Graph Mesh selected.");
        ImGui::TextUnformatted("Create one in the Content Library (right-click the Graph Meshes folder) and select it.");
        return;
    }
    ImGui::Text("Editing: %s", m_graph_mesh->get_name().c_str());
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

    // The canvas draws the currently edited graph (selection-driven).
    refresh_current_graph_mesh();
    if (!m_graph_mesh) {
        ImGui::TextUnformatted("No Graph Mesh selected.");
        ImGui::TextUnformatted("Create one in the Content Library (right-click the Graph Meshes folder) and select it.");
        return;
    }

    // Issue #251 (bug a): remember where the canvas starts so the zoom-level
    // overlay can be drawn over its top-left corner after End().
    const ImVec2 canvas_screen_pos = ImGui::GetCursorScreenPos();

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

    handle_link_create();
    handle_deletions();

    // Issue #251 pilot: a canvas-background context menu ("Add node"). This
    // proves a popup opens on the canvas background now that the faked
    // coordinate space is gone. Suspend()/Resume() bracket the popup out of the
    // node-editor's channel splitter; ShowBackgroundContextMenu() reports a
    // right-click on the background (using the editor's own gesture, so it does
    // not conflict with right-drag navigation).
    m_node_editor->Suspend();
    if (m_node_editor->ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("geometry_graph_background_menu");
    }
    if (ImGui::BeginPopup("geometry_graph_background_menu")) {
        build_palette();
        for (const Palette_category& category : m_palette_categories) {
            if (ImGui::BeginMenu(category.name.c_str())) {
                for (const Palette_entry& entry : category.entries) {
                    if (ImGui::MenuItem(entry.label.c_str())) {
                        add_node_of_type(entry.type_name);
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }
    m_node_editor->Resume();

    m_node_editor->End();

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

Geometry_graph_palette_window::Geometry_graph_palette_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Geometry_graph_window&       graph_window
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Geometry Graph Palette", "geometry_graph_palette"}
    , m_graph_window           {graph_window}
{
}

void Geometry_graph_palette_window::imgui()
{
    m_graph_window.controls_imgui();
}

}
