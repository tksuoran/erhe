#include "windows/layers_window.hpp"

#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <gsl/gsl>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using Light_type = erhe::scene::Light_type;

Layers_window* g_layers_window{nullptr};

Layers_window::Layers_window()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
{
}

Layers_window::~Layers_window() noexcept
{
    ERHE_VERIFY(g_layers_window == this);
    g_layers_window = nullptr;
}

void Layers_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Layers_window::initialize_component()
{
    ERHE_VERIFY(g_layers_window == nullptr);
    erhe::application::g_imgui_windows->register_imgui_window(this, "layers");
    g_layers_window = this;
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

    const auto& scene_roots        = g_editor_scenes->get_scene_roots();
    const auto  mesh_icon          = g_icon_set->icons.mesh;
    const auto& icon_rasterization = get_scale_value() < 1.5f
        ? g_icon_set->get_small_rasterization()
        : g_icon_set->get_large_rasterization();

    std::shared_ptr<erhe::scene::Item> item_clicked;
    for (const auto& scene_root : scene_roots)
    {
        if (ImGui::TreeNodeEx(scene_root->get_name().c_str(), parent_flags))
        {
            const auto& scene       = scene_root->scene();
            const auto& mesh_layers = scene.get_mesh_layers();
            for (const auto& layer : mesh_layers)
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

    if (item_clicked && (g_selection_tool != nullptr))
    {
        if (false) // TODO shift or maybe ctrl?
        {
            if (item_clicked->is_selected())
            {
                g_selection_tool->remove_from_selection(item_clicked);
            }
            else
            {
                g_selection_tool->add_to_selection(item_clicked);
            }
        }
        else
        {
            bool was_selected = item_clicked->is_selected();
            g_selection_tool->clear_selection();
            if (!was_selected)
            {
                g_selection_tool->add_to_selection(item_clicked);
            }
        }
    }
#endif
}

} // namespace editor
