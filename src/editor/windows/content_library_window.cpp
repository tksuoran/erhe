#include "windows/content_library_window.hpp"

#include "editor_context.hpp"
#include "editor_scenes.hpp"
#include "scene/scene_root.hpp"
#include "tools/brushes/brush.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

namespace editor
{

Content_library_window::Content_library_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_scenes&               editor_scenes

)
    : Imgui_window{imgui_renderer, imgui_windows, "Content Library", "content_library"}
    , m_context   {editor_context}
{
    const auto& scene_roots = editor_scenes.get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        const auto& content_library = scene_root->content_library();
        if (!content_library) {
            continue;
        }
        if (!content_library->is_shown_in_ui) {
            continue;
        }

        const auto& materials = content_library->materials.entries();
        if (materials.empty()) {
            continue;
        }

        m_materials.set_selected_entry(materials.front());
        break;
    }
}

auto Content_library_window::selected_animation() const -> std::shared_ptr<erhe::scene::Animation>
{
    return m_animations.get_selected_entry();
}

auto Content_library_window::selected_brush() const -> std::shared_ptr<Brush>
{
    return m_brushes.get_selected_entry();
}

auto Content_library_window::selected_material() const -> std::shared_ptr<erhe::primitive::Material>
{
    return m_materials.get_selected_entry();
}

auto Content_library_window::selected_skin() const -> std::shared_ptr<erhe::scene::Skin>
{
    return m_skins.get_selected_entry();
}

void Content_library_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};

    constexpr ImGuiTreeNodeFlags parent_flags{
        ImGuiTreeNodeFlags_DefaultOpen       |
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    const auto& scene_roots = m_context.editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        const auto& content_library = scene_root->content_library();
        if (!content_library) {
            continue;
        }
        if (!content_library->is_shown_in_ui) {
            continue;
        }

        if (ImGui::TreeNodeEx(scene_root->get_name().c_str(), parent_flags)) {
            m_animations.imgui(m_context, content_library->animations);
            m_brushes   .imgui(m_context, content_library->brushes);
            m_cameras   .imgui(m_context, content_library->cameras);
            m_lights    .imgui(m_context, content_library->lights);
            m_materials .imgui(m_context, content_library->materials);
            m_meshes    .imgui(m_context, content_library->meshes);
            m_skins     .imgui(m_context, content_library->skins);
            //m_textures  .imgui(content_library->textures);
            ImGui::TreePop();
        }
    }
#endif
}

} // namespace editor
