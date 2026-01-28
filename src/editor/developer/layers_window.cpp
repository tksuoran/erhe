#include "developer/layers_window.hpp"

#include "app_context.hpp"
#include "app_rendering.hpp"
#include "brushes/brush_tool.hpp"
#include "app_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/hotbar.hpp"
#include "tools/hud.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

namespace editor {

using Light_type = erhe::scene::Light_type;

Layers_window::Layers_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Layers", "layers", true}
    , m_context                {app_context}
{
}

void Layers_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    if (ImGui::TreeNodeEx("Brush tool")) {
        m_context.brush_tool->tool_properties(*this);
        ImGui::TreePop();
    }
    m_context.imgui_windows->debug_imgui();

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

    const auto& scene_roots = m_context.app_scenes->get_scene_roots();
    const char* mesh_icon   = m_context.icon_set->icons.mesh;

    std::shared_ptr<erhe::Item_base> item_clicked;
    for (const auto& scene_root : scene_roots) {
        if (ImGui::TreeNodeEx(scene_root->get_name().c_str(), parent_flags)) {
            const auto& scene       = scene_root->get_scene();
            const auto& mesh_layers = scene.get_mesh_layers();
            for (const auto& layer : mesh_layers) {
                if (ImGui::TreeNodeEx(layer->get_name().c_str(), parent_flags)) {
                    const auto& meshes = layer->meshes;
                    for (const auto& mesh : meshes) {
                        m_context.icon_set->draw_icon(mesh_icon, glm::vec4{0.6f, 0.6f, 0.6f, 1.0f});
                        ImGui::TreeNodeEx(
                            mesh->get_name().c_str(),
                            leaf_flags |
                            (
                                mesh->is_selected() ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None
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

    //m_context.app_scenes->imgui();

    m_context.scene_views->debug_imgui();

    if (ImGui::TreeNodeEx("Hotbar")) {
        auto& hotbar = *m_context.hotbar;
        auto& color_inactive = hotbar.get_color(0);
        auto& color_hover    = hotbar.get_color(1);
        auto& color_active   = hotbar.get_color(2);
        ImGui::ColorEdit4("Inactive", &color_inactive.x, ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4("Hover",    &color_hover.x,    ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4("Active",   &color_active.x,   ImGuiColorEditFlags_Float);

        auto position = hotbar.get_position();
        if (ImGui::DragFloat3("Position", &position.x, 0.01f, -1.0f, 1.0f)) {
            hotbar.set_position(position);
        }
        ImGui::TreePop();

        bool locked = hotbar.get_locked();
        if (ImGui::Checkbox("Locked", &locked)) {
            hotbar.set_locked(locked);
        }
    }

    //m_context.app_rendering->imgui();

    if (ImGui::TreeNodeEx("Hud")) {
        auto& hud = *m_context.hud;
        hud.imgui();
        ImGui::TreePop();
    }
}

}
