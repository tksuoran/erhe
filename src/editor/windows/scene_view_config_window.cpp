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

void Scene_view_config_window::imgui(App_context& context, Scene_view& scene_view)
{
    std::shared_ptr<Scene_root> scene_root = scene_view.get_scene_root();

    if (!ImGui::BeginTable(
            "##viewport_view_settings",
            2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg,
            ImVec2{600.0f, 260.0}
        ))
    {
        // BeginTable can fail (e.g. clipped window, zero-size cells); calling
        // EndTable without a matching successful BeginTable triggers an
        // ImGui assertion. Bail out before any TableSetupColumn / TableNextRow.
        return;
    }
    ImGui::TableSetupColumn("Label",  ImGuiTableColumnFlags_WidthStretch, 1.0f, 1);
    ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch, 3.0f, 2);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Scene");
    ImGui::TableNextColumn();

    auto old_scene_root = scene_root;
    const bool combo_used = context.app_scenes->scene_combo("##Scene", scene_root, true);
    if (combo_used && scene_root != old_scene_root) {
        scene_view.set_scene_root(scene_root);
        Viewport_scene_view* viewport_scene_view = scene_view.as_viewport_scene_view();
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

    Viewport_scene_view* viewport_scene_view = scene_view.as_viewport_scene_view();
    if (viewport_scene_view != nullptr) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Camera");
        ImGui::TableNextColumn();
        if (scene_root) {
            std::shared_ptr<erhe::scene::Camera> camera = viewport_scene_view->get_camera();
            bool camera_combo_used = scene_root->camera_combo("##Camera", camera);
            if (camera_combo_used) {
                viewport_scene_view->set_camera(camera);
            }
        } else {
            ImGui::TextDisabled("(select a scene first)");
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Shadow Camera Override");
        ImGui::TableNextColumn();
        if (scene_root) {
            // When set, the shadow frustum fit (and the shadow fit debug
            // visualizations) target this camera instead of the viewport
            // camera, so the fit can be observed from outside its frustum.
            std::weak_ptr<erhe::scene::Camera> shadow_fit_override_camera = scene_view.get_shadow_fit_override_camera();
            const bool shadow_fit_camera_combo_used = scene_root->camera_combo("##ShadowFitTargetCamera", shadow_fit_override_camera, true);
            if (shadow_fit_camera_combo_used) {
                scene_view.set_shadow_fit_override_camera(shadow_fit_override_camera);
            }
        } else {
            ImGui::TextDisabled("(select a scene first)");
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Shader Debug");
        ImGui::TableNextColumn();

        erhe::scene_renderer::Shader_debug shader_debug = viewport_scene_view->get_shader_debug();
        int shader_debug_int = static_cast<int>(shader_debug);
        if (ImGui::Combo(
                "##ShaderDebug",
                &shader_debug_int,
                erhe::scene_renderer::c_shader_debug_strings,
                IM_ARRAYSIZE(erhe::scene_renderer::c_shader_debug_strings),
                IM_ARRAYSIZE(erhe::scene_renderer::c_shader_debug_strings)
            ))
        {
            viewport_scene_view->set_shader_debug(static_cast<erhe::scene_renderer::Shader_debug>(shader_debug_int));
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Renderer");
        ImGui::TableNextColumn();

        Renderer_choice renderer_choice = viewport_scene_view->get_renderer_choice();
        bool renderer_combo_used = ImGui::Combo(
            "##RendererChoice",
            reinterpret_cast<int*>(&renderer_choice),
            c_renderer_choice_strings,
            IM_ARRAYSIZE(c_renderer_choice_strings),
            IM_ARRAYSIZE(c_renderer_choice_strings)
        );
        if (renderer_combo_used) {
            viewport_scene_view->set_renderer_choice(renderer_choice);
        }
    }

    ImGui::EndTable();
}

void Scene_view_config_window::imgui()
{
    if (m_scene_view == nullptr) {
        return;
    }

    imgui(m_context, *m_scene_view);
}

}
