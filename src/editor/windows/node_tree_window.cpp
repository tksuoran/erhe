#include "windows/node_tree_window.hpp"
#include "editor_imgui_windows.hpp"
#include "graphics/icon_set.hpp"
#include "log.hpp"

#include "tools/selection_tool.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "erhe/graphics/texture.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"

#include <gsl/gsl>
#include <imgui.h>

namespace editor
{

using Light_type = erhe::scene::Light_type;

Node_tree_window::Node_tree_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Node_tree_window::~Node_tree_window() = default;

void Node_tree_window::connect()
{
    m_scene_root     = get<Scene_root>();
    m_selection_tool = get<Selection_tool>();
    m_icon_set       = get<Icon_set>();
    require<Editor_imgui_windows>();
    Expects(m_scene_root     != nullptr);
    Expects(m_selection_tool != nullptr);
    Expects(m_icon_set       != nullptr);
}

void Node_tree_window::initialize_component()
{
    get<Editor_imgui_windows>()->register_imgui_window(this);
}

void Node_tree_window::imgui_tree_node(erhe::scene::Node* node)
{
    using erhe::scene::is_empty;
    using erhe::scene::is_mesh;
    using erhe::scene::is_light;
    using erhe::scene::is_camera;
    using erhe::scene::as_mesh;
    using erhe::scene::as_light;
    using erhe::scene::as_camera;

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
    //else if (is_physics(node))
    //{
    //    //log_tools.info("P {}\n", node->name());
    //    m_icon_set->icon(*node);
    //}
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

    const std::string label = fmt::format("{}", node->name());
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

void Node_tree_window::on_begin()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2{0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{3.0f, 3.0f});
}

void Node_tree_window::on_end()
{
    ImGui::PopStyleVar(2);
}

void Node_tree_window::imgui()
{
    const auto& scene = m_scene_root->scene();
    for (const auto& node : scene.nodes)
    {
        if (node->parent() == nullptr)
        {
            imgui_tree_node(node.get());
        }
    }

    const ImGuiIO& io = ImGui::GetIO();
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
            const bool was_selected = m_node_clicked->is_selected();
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
