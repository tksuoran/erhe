#include "windows/scene_view_config_window.hpp"
#include "app_context.hpp"
#include "app_scenes.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "scene/viewport_scene_view.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_scene/scene.hpp"

#include <imgui/imgui.h>

namespace editor {

Scene_view_config_window::Scene_view_config_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Scene View Config", "scene_view_config"}
    , m_context                {app_context}
{
}

void Scene_view_config_window::set_scene_view(Scene_view* scene_view)
{
    m_scene_view = scene_view;
}

void Scene_view_config_window::imgui()
{
    if (m_scene_view == nullptr) {
        return;
    }

    std::shared_ptr<Scene_root> scene_root = m_scene_view->get_scene_root();
    if (!scene_root) {
        return;
    }

    ImGui::BeginTable(
        "##viewport_view_settings",
        2,
        ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg,
        ImVec2{600.0f, 150.0}
    );
    ImGui::TableSetupColumn("Label",  ImGuiTableColumnFlags_WidthStretch, 1.0f, 1);
    ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch, 3.0f, 2);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Scene");
    ImGui::TableNextColumn();

    //std::shared_ptr<Scene_root> old_scene_root = m_scene_view->get_scene_root();
    Scene_root* scene_root_raw = scene_root.get();
    const bool combo_used = m_context.app_scenes->scene_combo("##Scene", scene_root_raw, false);
    if (combo_used && scene_root.get() != scene_root_raw) {
        scene_root = (scene_root_raw != nullptr) 
            ? scene_root_raw->shared_from_this()
            : std::shared_ptr<Scene_root>{};
        m_scene_view->set_scene_root(scene_root);
        Viewport_scene_view* viewport_scene_view = m_scene_view->as_viewport_scene_view();
        if (viewport_scene_view != nullptr) {
            if (scene_root) {
                const auto& cameras = scene_root->get_hosted_scene()->get_cameras();
                viewport_scene_view->set_camera(
                    cameras.empty() 
                        ? std::shared_ptr<erhe::scene::Camera>{} 
                        : cameras.front()
                );
            } else {
                viewport_scene_view->set_camera(std::shared_ptr<erhe::scene::Camera>{} );
            }
        }
    }

    Viewport_scene_view* viewport_scene_view = m_scene_view->as_viewport_scene_view();
    if (viewport_scene_view != nullptr) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Camera");
        ImGui::TableNextColumn();
        std::shared_ptr<erhe::scene::Camera> camera = viewport_scene_view->get_camera();
        bool camera_combo_used = scene_root->camera_combo("##Camera", camera);
        if (camera_combo_used) {
            viewport_scene_view->set_camera(camera);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Shader");
        ImGui::TableNextColumn();

        Shader_stages_variant variant = viewport_scene_view->get_shader_stages_variant();
        bool variant_combo_used = ImGui::Combo(
            "##ShaderStagesVariant",
            reinterpret_cast<int*>(&variant),
            c_shader_stages_variant_strings,
            IM_ARRAYSIZE(c_shader_stages_variant_strings),
            IM_ARRAYSIZE(c_shader_stages_variant_strings)
        );
        if (variant_combo_used) {
            viewport_scene_view->set_shader_stages_variant(variant);
        }
    }

    ImGui::EndTable();
}

}
