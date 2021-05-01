#include "windows/material_properties.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/primitive/material.hpp"

#include "imgui.h"

namespace sample
{

Material_properties::Material_properties(const std::shared_ptr<Scene_manager>&  scene_manager,
                                         const std::shared_ptr<Selection_tool>& selection_tool)
    : m_scene_manager {scene_manager}
    , m_selection_tool{selection_tool}
{
}

void Material_properties::window(Pointer_context&)
{
    const auto& materials = m_scene_manager->materials();
    int last = static_cast<int>(materials.size()) - 1;
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
        auto material = materials.at(m_material_index);
        ImGui::Text("%s", material->name.c_str());
        ImGui::SliderFloat("Metallic",   &material->metallic,    0.0f, 1.0f);
        ImGui::SliderFloat("Anisotropy", &material->anisotropy, -1.0f, 1.0f);
        ImGui::SliderFloat("Roughness",  &material->roughness,   0.0f, 1.0f);
        ImGui::ColorEdit4 ("Base Color", &material->base_color.x, ImGuiColorEditFlags_Float);
    }
    ImGui::End();
}

}
