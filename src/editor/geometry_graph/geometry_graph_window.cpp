#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/geometry_graph_operations.hpp"
#include "geometry_graph/nodes/boolean_node.hpp"
#include "geometry_graph/nodes/conway_node.hpp"
#include "geometry_graph/nodes/geometry_output_node.hpp"
#include "geometry_graph/nodes/geometry_unary_operation_node.hpp"
#include "geometry_graph/nodes/join_geometry_node.hpp"
#include "geometry_graph/nodes/math_node.hpp"
#include "geometry_graph/nodes/mesh_box_node.hpp"
#include "geometry_graph/nodes/mesh_cone_node.hpp"
#include "geometry_graph/nodes/mesh_disc_node.hpp"
#include "geometry_graph/nodes/mesh_sphere_node.hpp"
#include "geometry_graph/nodes/mesh_torus_node.hpp"
#include "geometry_graph/nodes/subdivide_node.hpp"
#include "geometry_graph/nodes/transform_node.hpp"
#include "geometry_graph/nodes/value_nodes.hpp"

#include "erhe_geometry/operation/normalize.hpp"
#include "erhe_geometry/operation/repair.hpp"
#include "erhe_geometry/operation/reverse.hpp"
#include "erhe_geometry/operation/triangulate.hpp"

#include "app_context.hpp"
#include "operations/operation_stack.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

#include <algorithm>

namespace editor {

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
    m_graph.mark_dirty();
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
    m_graph.unregister_node(node.get());
    m_nodes.erase(i);
    node->on_removed_from_graph();
    m_graph.mark_dirty();
}

auto Geometry_graph_window::connect_pins(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool
{
    erhe::graph::Link* link = m_graph.connect(source_pin, sink_pin);
    if (link == nullptr) {
        return false;
    }
    m_graph.mark_dirty();
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
    m_graph.mark_dirty();
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

auto Geometry_graph_window::make_node(const std::string& type_name) -> std::shared_ptr<Geometry_graph_node>
{
    if (type_name == "box")         { return std::make_shared<Mesh_box_node   >(); }
    if (type_name == "sphere")      { return std::make_shared<Mesh_sphere_node>(); }
    if (type_name == "torus")       { return std::make_shared<Mesh_torus_node >(); }
    if (type_name == "cone")        { return std::make_shared<Mesh_cone_node  >(); }
    if (type_name == "disc")        { return std::make_shared<Mesh_disc_node  >(); }
    if (type_name == "subdivide")   { return std::make_shared<Subdivide_node  >(); }
    if (type_name == "conway")      { return std::make_shared<Conway_node     >(); }
    if (type_name == "transform")   { return std::make_shared<Transform_node  >(); }
    if (type_name == "triangulate") { return std::make_shared<Geometry_unary_operation_node>("Triangulate", &erhe::geometry::operation::triangulate); }
    if (type_name == "normalize")   { return std::make_shared<Geometry_unary_operation_node>("Normalize",   &erhe::geometry::operation::normalize); }
    if (type_name == "reverse")     { return std::make_shared<Geometry_unary_operation_node>("Reverse",     &erhe::geometry::operation::reverse); }
    if (type_name == "repair")      { return std::make_shared<Geometry_unary_operation_node>("Repair",      &erhe::geometry::operation::repair); }
    if (type_name == "join")        { return std::make_shared<Join_geometry_node>(); }
    if (type_name == "boolean")     { return std::make_shared<Boolean_node      >(); }
    if (type_name == "float")       { return std::make_shared<Float_value_node  >(); }
    if (type_name == "integer")     { return std::make_shared<Integer_value_node>(); }
    if (type_name == "vector")      { return std::make_shared<Vector_value_node >(); }
    if (type_name == "math")        { return std::make_shared<Math_node         >(); }
    if (type_name == "output")      { return std::make_shared<Geometry_output_node>(m_app_context); }
    return {};
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

                       if (ImGui::Button("Join"))    { add_node_of_type("join"); }
    ImGui::SameLine(); if (ImGui::Button("Boolean")) { add_node_of_type("boolean"); }
    ImGui::SameLine(); if (ImGui::Button("Float"))   { add_node_of_type("float"); }
    ImGui::SameLine(); if (ImGui::Button("Integer")) { add_node_of_type("integer"); }
    ImGui::SameLine(); if (ImGui::Button("Vector"))  { add_node_of_type("vector"); }
    ImGui::SameLine(); if (ImGui::Button("Math"))    { add_node_of_type("math"); }
    ImGui::SameLine(); if (ImGui::Button("Output"))  { add_node_of_type("output"); }
}

void Geometry_graph_window::imgui()
{
    node_toolbar();

    m_graph.evaluate_if_dirty();

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
                    (source_pin->get_key() == sink_pin->get_key())
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
