#include "windows/node_tree_window.hpp"
#include "log.hpp"
#include "tools/selection_tool.hpp"
#include "tools.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "graphics/icon_set.hpp"
#include "erhe/graphics/texture.hpp"

#include "erhe/scene/scene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"

#include "imgui.h"

#include <gsl/gsl>

namespace editor
{

using Light_type = erhe::scene::Light_type;

Node_tree_window::Node_tree_window()
    : erhe::components::Component{c_name}
{
}

Node_tree_window::~Node_tree_window() = default;

void Node_tree_window::connect()
{
    m_scene_root     = get<Scene_root>();
    m_selection_tool = get<Selection_tool>();
    m_icon_set       = get<Icon_set>();
    Expects(m_scene_root     != nullptr);
    Expects(m_selection_tool != nullptr);
    Expects(m_icon_set       != nullptr);
}

void Node_tree_window::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Node_tree_window::imgui_tree_node(erhe::scene::Node* node)
{
    //erhe::log::Indenter log_indent;

    using namespace erhe::scene;

    if (is_empty(node))
    {
        //log_tools.info("E {} ({})\n", node->name());
        m_icon_set->icon(*node);
    }
    else if (is_mesh(node))
    {
        //log_tools.info("M {} ({})\n", node->name(), mesh->m_id.get_id());
        m_icon_set->icon(*as_mesh(node));
    }
    else if (is_light(node)) // note light is before camera - light is also camera
    {
        m_icon_set->icon(*as_light(node));
    }
    else if (is_camera(node))
    {
        //log_tools.info("C {}\n", node->name());
        m_icon_set->icon(*as_camera(node));
    }
    else if (is_physics(node))
    {
        //log_tools.info("P {}\n", node->name());
        m_icon_set->icon(*node);
    }
    else
    {
        //log_tools.info("? {} ({})\n", node->name());
        m_icon_set->icon(*node);
    }

    const auto child_count = node->child_count();
    const bool is_leaf = (child_count == 0);

    const ImGuiTreeNodeFlags parent_flags{ImGuiTreeNodeFlags_OpenOnArrow      | ImGuiTreeNodeFlags_OpenOnDoubleClick};
    const ImGuiTreeNodeFlags leaf_flags  {ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Leaf             };

    const ImGuiTreeNodeFlags node_flags{0
        | ImGuiTreeNodeFlags_SpanFullWidth
        | (is_leaf
            ? leaf_flags
            : parent_flags)
        | (node->is_selected()
            ? ImGuiTreeNodeFlags_Selected
            : ImGuiTreeNodeFlags_None)};

    std::string label = fmt::format("{} ({})", node->name(), node->depth());
    const auto node_open = ImGui::TreeNodeEx(label.c_str(), node_flags);
    if (ImGui::IsItemClicked())
    {
        m_node_clicked = node->shared_from_this();
    }

    if (node_open)
    {
        for (const auto& child_node : node->children())
        {
            imgui_tree_node(child_node.get());
        }

        if (!is_leaf)
        {
            ImGui::TreePop();
        }
    }
}

void Node_tree_window::imgui(Pointer_context&)
{
    const auto& scene = m_scene_root->scene();
    ImGui::Begin("Node Tree");
    for (const auto& node : scene.nodes)
    {
        if (node->parent() == nullptr)
        {
            imgui_tree_node(node.get());
        }
    }
    ImGui::End();

    ImGuiIO& io = ImGui::GetIO();
    if (m_node_clicked)
    {
        if (io.KeyShift) // ctrl?
        {
            if (m_node_clicked->is_selected())
            {
                m_selection_tool->remove_from_selection(m_node_clicked);
            }
            else
            {
                m_selection_tool->add_to_selection(m_node_clicked);
            }
        }
        else
        {
            bool was_selected = m_node_clicked->is_selected();
            m_selection_tool->clear_selection();
            if (!was_selected)
            {
                m_selection_tool->add_to_selection(m_node_clicked);
            }
        }
    }
    m_node_clicked.reset();
}

}
