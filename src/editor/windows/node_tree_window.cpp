#include "windows/node_tree_window.hpp"
#include "tools.hpp"
#include "scene/scene_root.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"

#include "imgui.h"

namespace editor
{

Node_tree_window::Node_tree_window()
    : erhe::components::Component{c_name}
{
}

Node_tree_window::~Node_tree_window() = default;

void Node_tree_window::connect()
{
    m_scene_root = get<Scene_root>();
}

void Node_tree_window::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Node_tree_window::node_imgui(const std::shared_ptr<erhe::scene::Node>& node)
{
    if (ImGui::TreeNode(node->label().c_str()))
    {
        for (const auto& attachment : node->attachments)
        {
            auto mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(attachment);
            if (mesh)
            {
                //ImGui::Text("(M) %s##%s[%u]", mesh->name().c_str(), node->label().c_str(), child_counter);
                ImGui::Text("(M) %s", mesh->name().c_str());
            }
            auto camera = std::dynamic_pointer_cast<erhe::scene::Camera>(attachment);
            if (camera)
            {
                ImGui::Text("(C) %s", camera->name().c_str());
            }
            auto light = std::dynamic_pointer_cast<erhe::scene::Light>(attachment);
            if (light)
            {
                ImGui::Text("(L) %s", light->name().c_str());
            }
            auto child_node = std::dynamic_pointer_cast<erhe::scene::Node>(attachment);
            if (child_node)
            {
                node_imgui(child_node);
            }
        }
        ImGui::TreePop();
    }
}

void Node_tree_window::imgui(Pointer_context&)
{
    const auto& scene = m_scene_root->scene();
    const auto& nodes = scene.nodes;
    ImGui::Begin("Node Tree");
    for (const auto& node : nodes)
    {
        if (node->parent == nullptr)
        {
            node_imgui(node);
        }
    }
    ImGui::End();
}

}
