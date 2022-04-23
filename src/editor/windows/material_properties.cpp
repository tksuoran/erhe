#include "windows/material_properties.hpp"
#include "windows/materials.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/primitive/material.hpp"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace editor
{

Material_properties::Material_properties()
    : erhe::components::Component    {c_name}
    , erhe::application::Imgui_window{c_title}
{
}

Material_properties::~Material_properties() = default;

void Material_properties::connect()
{
    m_materials = get<Materials>();
    require<erhe::application::Imgui_windows>();
}

void Material_properties::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Material_properties::imgui()
{
    if (m_materials == nullptr)
    {
        return;
    }

    {
        const auto selected_material = m_materials->selected_material();
        if (selected_material)
        {
            ImGui::InputText("Name", &selected_material->name);
            ImGui::SliderFloat("Metallic",    &selected_material->metallic,    0.0f, 1.0f);
            ImGui::SliderFloat("Roughness X", &selected_material->roughness.x, 0.0f, 1.0f);
            ImGui::SliderFloat("Roughness Y", &selected_material->roughness.y, 0.0f, 1.0f);
            ImGui::ColorEdit4 ("Base Color",  &selected_material->base_color.x, ImGuiColorEditFlags_Float);
        }
    }
}

} // namespace editor
