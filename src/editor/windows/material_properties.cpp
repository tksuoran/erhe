#include "windows/material_properties.hpp"
#include "tools.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "erhe/primitive/material.hpp"

#include "imgui.h"

namespace editor
{

Material_properties::Material_properties()
    : erhe::components::Component{c_name}
{
}

Material_properties::~Material_properties() = default;

void Material_properties::connect()
{
    m_scene_root     = get<Scene_root>();
    m_selection_tool = get<Selection_tool>();
}

void Material_properties::initialize_component()
{
    get<Editor_tools>()->register_window(this);
}

void Material_properties::window(Pointer_context&)
{
    const auto& materials = m_scene_root->materials();
    const int   last      = static_cast<int>(materials.size()) - 1;
    ImGui::Begin("Material");
    if (ImGui::Button("Prev"))
    {
        m_material_index = m_material_index > 0 ? m_material_index - 1 : last;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next"))
    {
        m_material_index = m_material_index < last ? m_material_index + 1 : 0;
    }
    if (m_material_index >= 0 && m_material_index <= last)
    {
        const auto material = materials.at(m_material_index);
        ImGui::Text("%s", material->name.c_str());
        ImGui::SliderFloat("Metallic",   &material->metallic,    0.0f, 1.0f);
        ImGui::SliderFloat("Anisotropy", &material->anisotropy, -1.0f, 1.0f);
        ImGui::SliderFloat("Roughness",  &material->roughness,   0.0f, 1.0f);
        ImGui::ColorEdit4 ("Base Color", &material->base_color.x, ImGuiColorEditFlags_Float);
    }
    ImGui::End();
}

}
