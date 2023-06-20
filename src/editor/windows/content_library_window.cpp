#include "windows/content_library_window.hpp"

#include "editor_scenes.hpp"
#include "scene/material_library.hpp"
#include "scene/scene_root.hpp"
#include "tools/brushes/brush.hpp"

#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/imgui/imgui_helpers.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

Content_library_window* g_content_library_window{nullptr};

Content_library_window::Content_library_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_scenes&               editor_scenes
)
    : Imgui_window   {imgui_renderer, imgui_windows, "Content Library", "content_library"}
    , m_editor_scenes{editor_scenes}
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

auto Content_library_window::selected_brush() const -> std::shared_ptr<Brush>
{
    return m_brushes.get_selected_entry();
}

auto Content_library_window::selected_material() const -> std::shared_ptr<erhe::primitive::Material>
{
    return m_materials.get_selected_entry();
}

void Content_library_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};

    constexpr ImGuiTreeNodeFlags parent_flags{
        ImGuiTreeNodeFlags_DefaultOpen       |
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    ERHE_PROFILE_FUNCTION();

    const auto& scene_roots = m_editor_scenes.get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        const auto& content_library = scene_root->content_library();
        if (!content_library) {
            continue;
        }
        if (!content_library->is_shown_in_ui) {
            continue;
        }

        if (ImGui::TreeNodeEx(scene_root->get_name().c_str(), parent_flags)) {
            m_brushes  .imgui(content_library->brushes);
            m_cameras  .imgui(content_library->cameras);
            m_lights   .imgui(content_library->lights);
            m_meshes   .imgui(content_library->meshes);
            m_materials.imgui(content_library->materials);
            ImGui::TreePop();
        }
    }
#endif
}

} // namespace editor
