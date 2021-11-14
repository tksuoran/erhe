#include "windows/layers_window.hpp"
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

Layers_window::Layers_window()
    : erhe::components::Component{c_name}
{
}

Layers_window::~Layers_window() = default;

void Layers_window::connect()
{
    m_scene_root     = get<Scene_root>();
    m_selection_tool = get<Selection_tool>();
    m_icon_set       = get<Icon_set>();
    Expects(m_scene_root     != nullptr);
    Expects(m_selection_tool != nullptr);
    Expects(m_icon_set       != nullptr);
}

void Layers_window::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Layers_window::imgui(Pointer_context&)
{
    const ImGuiTreeNodeFlags parent_flags{
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };
    const ImGuiTreeNodeFlags leaf_flags{
        ImGuiTreeNodeFlags_SpanFullWidth    |
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_Leaf
    };

    const auto& scene = m_scene_root->scene();
    ImGui::Begin("Mesh layers");
    for (const auto& layer : scene.mesh_layers)
    {
        if (ImGui::TreeNodeEx(layer->name.c_str(), parent_flags))
        {
            const auto& meshes = layer->meshes;
            for (const auto& mesh : meshes)
            {
                m_icon_set->icon(*mesh.get());
                ImGui::TreeNodeEx(
                    mesh->name().c_str(),
                    leaf_flags | (mesh->is_selected()
                        ? ImGuiTreeNodeFlags_Selected
                        : ImGuiTreeNodeFlags_None)
                );
                if (ImGui::IsItemClicked())
                {
                    m_node_clicked = mesh->shared_from_this();
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::End();

    ImGui::Begin("Light layers");
    for (const auto& layer : scene.light_layers)
    {
        if (ImGui::TreeNodeEx(layer->name.c_str(), parent_flags))
        {
            const auto& lights = layer->lights;
            for (const auto& light : lights)
            {
                m_icon_set->icon(*light.get());
                ImGui::TreeNodeEx(
                    light->name().c_str(),
                    leaf_flags | (light->is_selected()
                        ? ImGuiTreeNodeFlags_Selected
                        : ImGuiTreeNodeFlags_None)
                );
                if (ImGui::IsItemClicked())
                {
                    m_node_clicked = light->shared_from_this();
                }
            }
            ImGui::TreePop();
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
