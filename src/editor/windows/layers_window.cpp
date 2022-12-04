#include "windows/layers_window.hpp"
#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "tools/selection_tool.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"

#include "erhe/graphics/texture.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/profile.hpp"

#include <gsl/gsl>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using Light_type = erhe::scene::Light_type;

Layers_window::Layers_window()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
{
}

Layers_window::~Layers_window() noexcept
{
}

void Layers_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Layers_window::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Layers_window::post_initialize()
{
    m_editor_scenes  = get<Editor_scenes >();
    m_selection_tool = get<Selection_tool>();
    m_icon_set       = get<Icon_set      >();
}

void Layers_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

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

    const auto& scene_roots        = m_editor_scenes->get_scene_roots();
    const auto  mesh_icon          = m_icon_set->icons.mesh;
    const auto& icon_rasterization = get_scale_value() < 1.5f
        ? m_icon_set->get_small_rasterization()
        : m_icon_set->get_large_rasterization();

    std::shared_ptr<erhe::scene::Scene_item> item_clicked;
    for (const auto& scene_root : scene_roots)
    {
        if (ImGui::TreeNodeEx(scene_root->get_name().c_str(), parent_flags))
        {
            const auto& scene = scene_root->scene();
            for (const auto& layer : scene.mesh_layers)
            {
                if (ImGui::TreeNodeEx(layer->get_name().c_str(), parent_flags))
                {
                    const auto& meshes = layer->meshes;
                    for (const auto& mesh : meshes)
                    {
                        icon_rasterization.icon(mesh_icon);
                        ImGui::TreeNodeEx(
                            mesh->get_name().c_str(),
                            leaf_flags |
                            (
                                mesh->is_selected()
                                    ? ImGuiTreeNodeFlags_Selected
                                    : ImGuiTreeNodeFlags_None
                            )
                        );
                        if (ImGui::IsItemClicked())
                        {
                            item_clicked = mesh->shared_from_this();
                        }
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }

    if (item_clicked && m_selection_tool)
    {
        if (false) // TODO shift or maybe ctrl?
        {
            if (item_clicked->is_selected())
            {
                m_selection_tool->remove_from_selection(m_item_clicked);
            }
            else
            {
                m_selection_tool->add_to_selection(m_item_clicked);
            }
        }
        else
        {
            bool was_selected = item_clicked->is_selected();
            m_selection_tool->clear_selection();
            if (!was_selected)
            {
                m_selection_tool->add_to_selection(m_item_clicked);
            }
        }
    }
#endif
}

} // namespace editor
