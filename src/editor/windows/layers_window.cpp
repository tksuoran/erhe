#include "windows/layers_window.hpp"

#include "editor_context.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using Light_type = erhe::scene::Light_type;

Layers_window::Layers_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&               editor_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Layers", "layers"}
    , m_context                {editor_context}
{
}

void Layers_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

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

    const auto& scene_roots        = m_context.editor_scenes->get_scene_roots();
    const auto  mesh_icon          = m_context.icon_set->icons.mesh;
    const auto& icon_rasterization = get_scale_value() < 1.5f
        ? m_context.icon_set->get_small_rasterization()
        : m_context.icon_set->get_large_rasterization();

    std::shared_ptr<erhe::scene::Item> item_clicked;
    for (const auto& scene_root : scene_roots) {
        if (ImGui::TreeNodeEx(scene_root->get_name().c_str(), parent_flags)) {
            const auto& scene       = scene_root->scene();
            const auto& mesh_layers = scene.get_mesh_layers();
            for (const auto& layer : mesh_layers) {
                if (ImGui::TreeNodeEx(layer->get_name().c_str(), parent_flags)) {
                    const auto& meshes = layer->meshes;
                    for (const auto& mesh : meshes) {
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
                        if (ImGui::IsItemClicked()) {
                            item_clicked = mesh->shared_from_this();
                        }
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }

    if (item_clicked) {
        if (false) { // TODO shift or maybe ctrl?
            if (item_clicked->is_selected()) {
                m_context.selection->remove_from_selection(item_clicked);
            } else {
                m_context.selection->add_to_selection(item_clicked);
            }
        } else {
            bool was_selected = item_clicked->is_selected();
            m_context.selection->clear_selection();
            if (!was_selected) {
                m_context.selection->add_to_selection(item_clicked);
            }
        }
    }
#endif
}

} // namespace editor
