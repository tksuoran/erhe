#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/geometry_graph_node_factory.hpp"
#include "geometry_graph/geometry_graph_operations.hpp"
#include "geometry_graph/nodes/geometry_output_node.hpp"
#include "geometry_graph/nodes/group_nodes.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "operations/operation_stack.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>
#include <taskflow/taskflow.hpp>

#include <algorithm>

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
    m_node_editor = std::make_unique<ax::NodeEditor::EditorContext>(nullptr);
}

Geometry_graph_window::~Geometry_graph_window() noexcept
{
}

auto Geometry_graph_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void Geometry_graph_window::insert_node(const std::shared_ptr<Geometry_graph_node>& node)
{
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    m_nodes.push_back(node);
    m_graph.register_node(node.get());
    // A node entering the graph must re-evaluate even when it was clean
    // on removal (undo restore): the output node has to recreate its
    // scene mesh, released in on_removed_from_graph().
    node->mark_dirty();
}

void Geometry_graph_window::erase_node(const std::shared_ptr<Geometry_graph_node>& node)
{
    auto i = std::find(m_nodes.begin(), m_nodes.end(), node);
    if (i == m_nodes.end()) {
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
    m_graph.unregister_node(node.get());
    m_nodes.erase(i);
    node->on_removed_from_graph();
}

auto Geometry_graph_window::connect_pins(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool
{
    erhe::graph::Link* link = m_graph.connect(source_pin, sink_pin);
    if (link == nullptr) {
        return false;
    }
    mark_sink_node_dirty(sink_pin);
    return true;
}

auto Geometry_graph_window::disconnect_pins(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool
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
    m_app_context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_node_insert_remove_operation>(
            *this, node, Geometry_graph_node_insert_remove_operation::Mode::remove
        )
    );
}

auto Geometry_graph_window::connect(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool
{
    if ((source_pin == nullptr) || (sink_pin == nullptr) || (source_pin->get_key() != sink_pin->get_key())) {
        return false;
    }
    // Validate before creating the operation: a refused link must not
    // leave a no-op entry on the undo stack.
    if (m_graph.would_create_cycle(source_pin, sink_pin)) {
        log_graph_editor->warn(
            "Geometry graph: connecting '{}' to '{}' would create a cycle - refusing",
            source_pin->get_owner_node()->get_name(),
            sink_pin  ->get_owner_node()->get_name()
        );
        return false;
    }
    m_app_context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_link_insert_remove_operation>(
            *this, source_pin, sink_pin, Geometry_graph_link_insert_remove_operation::Mode::insert
        )
    );
    return true;
}

void Geometry_graph_window::disconnect(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin)
{
    m_app_context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_link_insert_remove_operation>(
            *this, source_pin, sink_pin, Geometry_graph_link_insert_remove_operation::Mode::remove
        )
    );
}

void Geometry_graph_window::set_node_parameters(const std::shared_ptr<Geometry_graph_node>& node, const nlohmann::json& parameters)
{
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
    const std::shared_ptr<Geometry_graph_node> node = make_node(type_name);
    if (!node) {
        return nullptr;
    }
    m_app_context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_node_insert_remove_operation>(
            *this, node, Geometry_graph_node_insert_remove_operation::Mode::insert
        )
    );
    set_node_position(*node.get(), next_node_spawn_position());
    return node.get();
}

auto Geometry_graph_window::get_graph() -> Geometry_graph&
{
    return m_graph;
}

auto Geometry_graph_window::get_nodes() const -> const std::vector<std::shared_ptr<Geometry_graph_node>>&
{
    return m_nodes;
}

void Geometry_graph_window::file_toolbar()
{
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("##graph_path", &m_graph_path);
    ImGui::SameLine(); if (ImGui::Button("Save"))  { save_graph(std::filesystem::path{m_graph_path}); }
    ImGui::SameLine(); if (ImGui::Button("Load"))  { load_graph(std::filesystem::path{m_graph_path}); }
    ImGui::SameLine(); if (ImGui::Button("Clear")) { clear_graph(); }
}

void Geometry_graph_window::node_toolbar()
{
                       if (ImGui::Button("Box"))    { add_node_of_type("box"); }
    ImGui::SameLine(); if (ImGui::Button("Sphere")) { add_node_of_type("sphere"); }
    ImGui::SameLine(); if (ImGui::Button("Torus"))  { add_node_of_type("torus"); }
    ImGui::SameLine(); if (ImGui::Button("Cone"))   { add_node_of_type("cone"); }
    ImGui::SameLine(); if (ImGui::Button("Disc"))   { add_node_of_type("disc"); }

                       if (ImGui::Button("Subdivide"))   { add_node_of_type("subdivide"); }
    ImGui::SameLine(); if (ImGui::Button("Conway"))      { add_node_of_type("conway"); }
    ImGui::SameLine(); if (ImGui::Button("Transform"))   { add_node_of_type("transform"); }
    ImGui::SameLine(); if (ImGui::Button("Triangulate")) { add_node_of_type("triangulate"); }
    ImGui::SameLine(); if (ImGui::Button("Normalize"))   { add_node_of_type("normalize"); }
    ImGui::SameLine(); if (ImGui::Button("Reverse"))     { add_node_of_type("reverse"); }
    ImGui::SameLine(); if (ImGui::Button("Repair"))      { add_node_of_type("repair"); }

                       if (ImGui::Button("Distribute")) { add_node_of_type("distribute"); }
    ImGui::SameLine(); if (ImGui::Button("Instance"))   { add_node_of_type("instance"); }
    ImGui::SameLine(); if (ImGui::Button("Realize"))    { add_node_of_type("realize"); }

                       if (ImGui::Button("Join"))    { add_node_of_type("join"); }
    ImGui::SameLine(); if (ImGui::Button("Boolean")) { add_node_of_type("boolean"); }
    ImGui::SameLine(); if (ImGui::Button("Float"))   { add_node_of_type("float"); }
    ImGui::SameLine(); if (ImGui::Button("Integer")) { add_node_of_type("integer"); }
    ImGui::SameLine(); if (ImGui::Button("Vector"))  { add_node_of_type("vector"); }
    ImGui::SameLine(); if (ImGui::Button("Math"))    { add_node_of_type("math"); }
    ImGui::SameLine(); if (ImGui::Button("Output"))  { add_node_of_type("output"); }
    ImGui::SameLine(); if (ImGui::Button("Group In"))  { add_node_of_type("group_input"); }
    ImGui::SameLine(); if (ImGui::Button("Group Out")) { add_node_of_type("group_output"); }
    ImGui::SameLine(); if (ImGui::Button("Group"))     { add_node_of_type("group"); }
}

auto Geometry_graph_window::is_evaluation_run_done() -> bool
{
    // pre: m_evaluation_run != nullptr
    std::lock_guard<std::mutex> lock{m_evaluation_run->mutex};
    return m_evaluation_run->done;
}

void Geometry_graph_window::launch_evaluation()
{
    // Snapshot the live graph into a shadow clone the worker owns:
    // factory-built nodes carrying the same parameters, cached payloads
    // and dirty flags, plus the same links. Only the shadow is touched
    // off the main thread, so the live graph stays free for the UI and
    // for structural edits while the run is in flight.
    std::shared_ptr<Evaluation_run> run = std::make_shared<Evaluation_run>();
    run->shadow_nodes.reserve(m_nodes.size());
    run->live_node_ids.reserve(m_nodes.size());
    for (const std::shared_ptr<Geometry_graph_node>& node : m_nodes) {
        const std::shared_ptr<Geometry_graph_node> shadow = make_geometry_graph_node(m_app_context, node->get_factory_type_name());
        if (!shadow) {
            log_graph_editor->warn("Geometry graph evaluation: no factory for node type '{}'", node->get_factory_type_name());
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
    for (const std::unique_ptr<erhe::graph::Link>& link : m_graph.get_links()) {
        std::size_t source_index = m_nodes.size();
        std::size_t sink_index   = m_nodes.size();
        for (std::size_t i = 0, end = m_nodes.size(); i < end; ++i) {
            if (m_nodes[i].get() == link->get_source()->get_owner_node()) { source_index = i; }
            if (m_nodes[i].get() == link->get_sink  ()->get_owner_node()) { sink_index   = i; }
        }
        if ((source_index >= run->shadow_nodes.size()) || (sink_index >= run->shadow_nodes.size())) {
            continue;
        }
        Geometry_graph_node* shadow_source = run->shadow_nodes[source_index].get();
        Geometry_graph_node* shadow_sink   = run->shadow_nodes[sink_index].get();
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
    if (m_graph.consume_forced_full()) {
        run->shadow_graph.mark_dirty();
    }
    // The snapshot now owns the dirty state; edits made while the run is
    // in flight re-mark nodes dirty and trigger the next run.
    for (const std::shared_ptr<Geometry_graph_node>& node : m_nodes) {
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
    // live nodes (matched by id; nodes removed while the run was in
    // flight are skipped) and apply the output nodes' evaluated scene
    // products. Main thread.
    const std::shared_ptr<Evaluation_run> run = std::move(m_evaluation_run);
    for (std::size_t i = 0, end = run->shadow_nodes.size(); i < end; ++i) {
        const std::size_t live_id = run->live_node_ids[i];
        Geometry_graph_node* live_node = nullptr;
        for (const std::shared_ptr<Geometry_graph_node>& node : m_nodes) {
            if (node->get_id() == live_id) {
                live_node = node.get();
                break;
            }
        }
        if (live_node == nullptr) {
            continue;
        }
        Geometry_graph_node* shadow_node = run->shadow_nodes[i].get();
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
}

void Geometry_graph_window::update_evaluation()
{
    if (m_evaluation_run) {
        if (!is_evaluation_run_done()) {
            return;
        }
        finish_evaluation();
    }
    if (m_graph.is_evaluation_needed()) {
        launch_evaluation();
    }
}

void Geometry_graph_window::wait_for_idle_evaluation()
{
    // Multi-round: finishing a run can leave the graph dirty again
    // (edits made while it ran), which needs another run.
    for (;;) {
        if (m_evaluation_run) {
            {
                std::unique_lock<std::mutex> lock{m_evaluation_run->mutex};
                m_evaluation_run->condition.wait(lock, [this]() { return m_evaluation_run->done; });
            }
            finish_evaluation();
        }
        if (!m_graph.is_evaluation_needed()) {
            return;
        }
        launch_evaluation();
    }
}

void Geometry_graph_window::imgui()
{
    file_toolbar();
    node_toolbar();

    if (m_evaluation_run) {
        ImGui::TextUnformatted("Evaluating graph in background...");
    }

    m_node_editor->Begin("Geometry Graph", ImVec2{0.0f, 0.0f});

    for (erhe::graph::Node* node : m_graph.get_nodes()) {
        Geometry_graph_node* geometry_graph_node = dynamic_cast<Geometry_graph_node*>(node);
        if (geometry_graph_node != nullptr) {
            geometry_graph_node->node_editor(m_app_context, *m_node_editor.get());
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

void Geometry_graph_window::handle_deletions()
{
    if (m_node_editor->BeginDelete()) {
        ax::NodeEditor::NodeId node_handle = 0;
        while (m_node_editor->QueryDeletedNode(&node_handle)){
            if (m_node_editor->AcceptDeletedItem()) {
                auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node_handle](const std::shared_ptr<Geometry_graph_node>& entry){
                    ax::NodeEditor::NodeId entry_node_id = ax::NodeEditor::NodeId{entry->get_id()};
                    return entry_node_id == node_handle;
                });
                if (i != m_nodes.end()) {
                    const std::shared_ptr<Geometry_graph_node> geometry_graph_node = *i; // copy - remove_node() erases from m_nodes
                    remove_node(geometry_graph_node);
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

}
