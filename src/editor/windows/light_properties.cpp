#include "windows/light_properties.hpp"
#include "tools.hpp"
#include "scene/scene_root.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/scene.hpp"

#include "imgui.h"

namespace editor
{

Light_properties::Light_properties()
    : erhe::components::Component(c_name)
{
}

Light_properties::~Light_properties() = default;

void Light_properties::connect()
{
    m_scene_root = get<Scene_root>();
}

void Light_properties::initialize_component()
{
    get<Editor_tools>()->register_window(this);
}

auto Light_properties::animation() const -> bool
{
    return m_animation;
}

void Light_properties::window(Pointer_context&)
{
    ImGui::Begin("Lights");
    ImGui::Checkbox("Animation", &m_animation);
    ImGui::Separator();
    const auto& layers = m_scene_root->all_layers();
    int last_layer = static_cast<int>(layers.size()) - 1;
    if (ImGui::Button("Prev Layer"))
    {
        m_layer_index = m_layer_index > 0 ? m_layer_index - 1 : last_layer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Layer"))
    {
        m_layer_index = m_layer_index < last_layer ? m_layer_index + 1 : 0;
    }
    if (m_layer_index >= 0 && m_layer_index <= last_layer)
    {
        const auto& layer  = layers.at(m_layer_index);
        const auto& lights = layer->lights;
        int last_light = static_cast<int>(lights.size()) - 1;
        if (ImGui::Button("Prev Light"))
        {
            m_light_index = m_light_index > 0 ? m_light_index - 1 : last_light;
        }
        ImGui::SameLine();
        if (ImGui::Button("Next Light"))
        {
            m_light_index = m_light_index < last_light ? m_light_index + 1 : 0;
        }
        if (m_light_index >= 0 && m_light_index <= last_light)
        {
            auto light = lights.at(m_light_index);
            const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;
            ImGui::Text("%s", light->name().c_str());

            make_combo("Type", light->type, erhe::scene::Light::c_type_strings, IM_ARRAYSIZE(erhe::scene::Light::c_type_strings));
            if (light->type == erhe::scene::Light::Type::spot)
            {
                ImGui::SliderFloat ("Inner Spot", &light->inner_spot_angle, 0.0f, glm::pi<float>());
                ImGui::SliderFloat ("Outer Spot", &light->outer_spot_angle, 0.0f, glm::pi<float>());
            }
            ImGui::SliderFloat("Intensity",  &light->intensity, 0.01f, 2000.0f, "%.3f", logarithmic);
            ImGui::ColorEdit3 ("Color",      &light->color.x,   ImGuiColorEditFlags_Float);
        }
        ImGui::Separator();
        ImGui::ColorEdit3("Ambient Light", &layer->ambient_light.x, ImGuiColorEditFlags_Float);
    }


    ImGui::End();
}

} // namespace editor
