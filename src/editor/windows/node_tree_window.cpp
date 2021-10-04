#include "windows/node_tree_window.hpp"
#include "log.hpp"
#include "tools/selection_tool.hpp"
#include "tools.hpp"
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

ImVec4 imvec_from_glm(glm::vec4 v)
{
    return ImVec4{v.x, v.y, v.z, v.w};
}

void Node_tree_window::icon(ImVec2 uv0, glm::vec4 tint_color)
{
    const float size      = ImGui::GetTextLineHeight();
    const auto  icon_size = ImVec2(size, size);

    ImGui::Image(reinterpret_cast<ImTextureID>(m_icon_set->texture.get()),
                 icon_size,
                 uv0,
                 m_icon_set->uv1(uv0),
                 imvec_from_glm(tint_color));
    ImGui::SameLine();
}

auto Node_tree_window::get_icon(const Light_type type) const -> const ImVec2
{
    switch (type)
    {
        case Light_type::spot:        return m_icon_set->icons.spot_light;
        case Light_type::directional: return m_icon_set->icons.directional_light;
        case Light_type::point:       return m_icon_set->icons.point_light;
        default: return {};
    }
}

void Node_tree_window::node_imgui(const std::shared_ptr<erhe::scene::Node>& node)
{    
    const auto node_selection_bit = node->is_selected() ? ImGuiTreeNodeFlags_Selected
                                                        : ImGuiTreeNodeFlags_None;
    const ImGuiTreeNodeFlags node_flags{ImGuiTreeNodeFlags_OpenOnArrow       |
                                        ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                        ImGuiTreeNodeFlags_SpanFullWidth     |
                                        node_selection_bit};
    icon(m_icon_set->icons.node);
    std::shared_ptr<erhe::scene::INode_attachment> item_clicked{nullptr};
    if (ImGui::TreeNodeEx(node->label().c_str(), node_flags))
    {
        if (ImGui::IsItemClicked())
        {
            item_clicked = node;
        }
        for (const auto& attachment : node->attachments)
        {
            const auto attachment_selection_bit = attachment->is_selected() ? ImGuiTreeNodeFlags_Selected
                                                                            : ImGuiTreeNodeFlags_None;

            const ImGuiTreeNodeFlags leaf_flags{ImGuiTreeNodeFlags_SpanFullWidth    |
                                                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                                ImGuiTreeNodeFlags_Leaf             |
                                                attachment_selection_bit};
            auto mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(attachment);
            if (mesh)
            {
                icon(m_icon_set->icons.mesh);
                ImGui::TreeNodeEx(mesh->name().c_str(), leaf_flags);
                if (ImGui::IsItemClicked())
                {
                    item_clicked = attachment;
                }
            }
            auto camera = std::dynamic_pointer_cast<erhe::scene::Camera>(attachment);
            if (camera)
            {
                icon(m_icon_set->icons.camera);
                ImGui::TreeNodeEx(camera->name().c_str(), leaf_flags);
                if (ImGui::IsItemClicked())
                {
                    item_clicked = attachment;
                }
            }
            auto light = std::dynamic_pointer_cast<erhe::scene::Light>(attachment);
            if (light)
            {
                icon(get_icon(light->type), glm::vec4{light->color, 1.0f});
                ImGui::TreeNodeEx(light->name().c_str(), leaf_flags);
                if (ImGui::IsItemClicked())
                {
                    item_clicked = attachment;
                }
            }
            auto child_node = std::dynamic_pointer_cast<erhe::scene::Node>(attachment);
            if (child_node)
            {
                node_imgui(child_node);
            }
        }
        ImGui::TreePop();
    }

    ImGuiIO& io = ImGui::GetIO();
    if (item_clicked)
    {
        if (io.KeyShift) // ctrl?
        {
            if (item_clicked->is_selected())
            {
                m_selection_tool->remove_from_selection(item_clicked);
            }
            else
            {
                m_selection_tool->add_to_selection(item_clicked);
            }
        }
        else
        {
            bool was_selected = item_clicked->is_selected();
            m_selection_tool->clear_selection();
            if (!was_selected)
            {
                m_selection_tool->add_to_selection(item_clicked);
            }
        }
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
