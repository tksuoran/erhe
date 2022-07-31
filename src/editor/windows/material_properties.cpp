#include "windows/material_properties.hpp"
#include "scene/material_library.hpp"
#include "windows/materials_window.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/primitive/material.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

Material_properties::Material_properties()
    : erhe::components::Component    {c_label}
    , erhe::application::Imgui_window{c_title, c_label}
{
}

Material_properties::~Material_properties() noexcept
{
}

void Material_properties::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Material_properties::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Material_properties::post_initialize()
{
    m_materials_window = get<Materials_window>();
}

void Material_properties::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const auto selected_material = m_materials_window->selected_material();
    if (selected_material)
    {
        ImGui::InputText("Name", &selected_material->name);
        ImGui::Text("Index = %zu", selected_material->index);
        ImGui::SliderFloat("Metallic",    &selected_material->metallic,    0.0f, 1.0f);
        ImGui::SliderFloat("Roughness X", &selected_material->roughness.x, 0.0f, 1.0f);
        ImGui::SliderFloat("Roughness Y", &selected_material->roughness.y, 0.0f, 1.0f);
        ImGui::ColorEdit4 ("Base Color",  &selected_material->base_color.x, ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4 ("Emissive",    &selected_material->emissive.x  , ImGuiColorEditFlags_Float);
    }
#endif
}

} // namespace editor
