#include "windows/materials_window.hpp"
#include "editor_scenes.hpp"

#include "scene/material_library.hpp"
#include "scene/scene_root.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

Materials_window::Materials_window()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title}
{
}

[[nodiscard]] auto Materials_window::selected_material_library() const -> std::shared_ptr<Material_library>
{
    return m_selected_material_library;
}

auto Materials_window::selected_material() const -> std::shared_ptr<erhe::primitive::Material>
{
    return m_selected_material;
}

void Materials_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
    require<Editor_scenes>();
}

void Materials_window::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Materials_window::post_initialize()
{
    m_editor_scenes = get<Editor_scenes>();

    const auto& scene_roots = m_editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots)
    {
        const auto& material_library = scene_root->material_library();
        const auto& materials        = material_library->materials();

        if (materials.empty())
        {
            continue;
        }
        m_selected_material = materials.front();
        break;
    }
}

void Materials_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    const ImGuiTreeNodeFlags parent_flags{
        ImGuiTreeNodeFlags_DefaultOpen       |
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    const auto& scene_roots = m_editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots)
    {
        if (ImGui::TreeNodeEx(scene_root->name().c_str(), parent_flags))
        {
            const auto& material_library = scene_root->material_library();
            const auto& materials        = material_library->materials();

            const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};
            for (const auto& material : materials)
            {
                if (material->visible == false)
                {
                    continue;
                }
                bool selected = m_selected_material == material;
                const bool button_pressed = erhe::application::make_button(
                    material->name.c_str(),
                    selected
                        ? erhe::application::Item_mode::active
                        : erhe::application::Item_mode::normal,
                    button_size
                );
                if (button_pressed)
                {
                    m_selected_material_library = material_library;
                    m_selected_material         = material;
                }
            }
            ImGui::TreePop();
        }
    }
#endif
}

} // namespace editor
