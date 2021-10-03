#include "windows/node_tree_window.hpp"
#include "tools.hpp"
#include "scene/scene_root.hpp"

#include "graphics/icon_set.hpp"
//#include "graphics/textures.hpp"
#include "erhe/graphics/texture.hpp"

#include "erhe/scene/scene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"

#include "imgui.h"

#include <gsl/gsl>

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
    m_icon_set   = get<Icon_set>();
    //m_textures   = get<Textures>();
    Expects(m_scene_root != nullptr);
    Expects(m_icon_set   != nullptr);
    //Expects(m_textures != nullptr);
}

void Node_tree_window::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Node_tree_window::icon(ImVec2 uv0)
{
    const float size      = ImGui::GetTextLineHeight();
    const auto  icon_size = ImVec2(size, size);

    ImGui::Image(reinterpret_cast<ImTextureID>(m_icon_set->texture.get()),
                 icon_size,
                 uv0,
                 m_icon_set->uv1(uv0));
    ImGui::SameLine();
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
                icon(m_icon_set->icons.mesh);
                ImGui::Text("(M) %s", mesh->name().c_str());
            }
            auto camera = std::dynamic_pointer_cast<erhe::scene::Camera>(attachment);
            if (camera)
            {
                icon(m_icon_set->icons.camera);
                ImGui::Text("(C) %s", camera->name().c_str());
            }
            auto light = std::dynamic_pointer_cast<erhe::scene::Light>(attachment);
            if (light)
            {
                icon(m_icon_set->icons.light);
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
