#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/nodes/mesh_box_node.hpp"
#include "geometry_graph/nodes/mesh_cone_node.hpp"
#include "geometry_graph/nodes/mesh_disc_node.hpp"
#include "geometry_graph/nodes/mesh_sphere_node.hpp"
#include "geometry_graph/nodes/mesh_torus_node.hpp"

#include "app_context.hpp"
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

void Geometry_graph_window::add_node(const std::shared_ptr<Geometry_graph_node>& node)
{
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    m_nodes.push_back(node);
    m_graph.register_node(node.get());
    m_graph.mark_dirty();
}

void Geometry_graph_window::node_toolbar()
{
                       if (ImGui::Button("Box"))    { add_node(std::make_shared<Mesh_box_node   >()); }
    ImGui::SameLine(); if (ImGui::Button("Sphere")) { add_node(std::make_shared<Mesh_sphere_node>()); }
    ImGui::SameLine(); if (ImGui::Button("Torus"))  { add_node(std::make_shared<Mesh_torus_node >()); }
    ImGui::SameLine(); if (ImGui::Button("Cone"))   { add_node(std::make_shared<Mesh_cone_node  >()); }
    ImGui::SameLine(); if (ImGui::Button("Disc"))   { add_node(std::make_shared<Mesh_disc_node  >()); }
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
                        erhe::graph::Link* link = m_graph.connect(source_pin, sink_pin);
                        if (link != nullptr) {
                            m_node_editor->Link(
                                ax::NodeEditor::LinkId{link},
                                ax::NodeEditor::PinId{source_pin},
                                ax::NodeEditor::PinId{sink_pin}
                            );
                            m_graph.mark_dirty();
                        }
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
                    const std::shared_ptr<Geometry_graph_node>& geometry_graph_node = *i;
                    if (geometry_graph_node->is_selected()) {
                        m_app_context.selection->remove_from_selection(geometry_graph_node);
                    }
                    m_graph.unregister_node(i->get());
                    m_nodes.erase(i);
                    m_graph.mark_dirty();
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
                    m_graph.disconnect(i->get());
                    m_graph.mark_dirty();
                }
            }
        }
    }
    m_node_editor->EndDelete();
}

}
