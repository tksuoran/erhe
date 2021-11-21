#include "windows/material_properties.hpp"
#include "tools.hpp"
#include "scene/scene_root.hpp"

#include "erhe/primitive/material.hpp"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace editor
{

Material_properties::Material_properties()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Material_properties::~Material_properties() = default;

void Material_properties::connect()
{
    m_scene_root = get<Scene_root>();
}

void Material_properties::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Material_properties::materials()
{
    const auto& materials = m_scene_root->materials();

    ImGui::Begin("MaterialsList");

    for (const auto& material : materials)
    {
        bool selected = m_selected_material == material.get();
        ImGui::Selectable(material->name.c_str(), &selected);
        if (selected)
        {
            m_selected_material = material.get();
        }
    }

    ImGui::End();
}

void Material_properties::imgui(Pointer_context&)
{
    materials();

    ImGui::Begin("Material");
    {
        if (m_selected_material != nullptr)
        {
            ImGui::InputText("Name", &m_selected_material->name);
            ImGui::SliderFloat("Metallic",   &m_selected_material->metallic,    0.0f, 1.0f);
            ImGui::SliderFloat("Anisotropy", &m_selected_material->anisotropy, -1.0f, 1.0f);
            ImGui::SliderFloat("Roughness",  &m_selected_material->roughness,   0.0f, 1.0f);
            ImGui::ColorEdit4 ("Base Color", &m_selected_material->base_color.x, ImGuiColorEditFlags_Float);
        }
    }
    ImGui::End();
}

}
