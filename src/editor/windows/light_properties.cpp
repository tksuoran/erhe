#include "windows/light_properties.hpp"
#include "graphics/icon_set.hpp"
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
    m_scene_root     = get<Scene_root>();
    m_selection_tool = get<Selection_tool>();
    m_icon_set       = get<Icon_set>();
}

void Light_properties::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

auto Light_properties::animation() const -> bool
{
    return m_animation;
}

void Light_properties::imgui(Pointer_context&)
{
    ImGui::Begin("Lights");
    const auto& light_layer = m_scene_root->light_layer();
    for (auto item : m_selection_tool->selection())
    {
        const auto light = as_light(item);
        if (!light)
        {
            continue;
        }
        m_icon_set->icon(*light);
        const bool light_open = ImGui::TreeNodeEx(
            light->name().c_str(),
            ImGuiTreeNodeFlags_SpanFullWidth
        );
        if (light_open)
        {
            const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;
            ImGui::Text("%s", light->name().c_str());

            make_combo("Type", light->type, erhe::scene::Light::c_type_strings, IM_ARRAYSIZE(erhe::scene::Light::c_type_strings));
            if (light->type == erhe::scene::Light::Type::spot)
            {
                ImGui::SliderFloat("Inner Spot", &light->inner_spot_angle, 0.0f, glm::pi<float>());
                ImGui::SliderFloat("Outer Spot", &light->outer_spot_angle, 0.0f, glm::pi<float>());
            }
            ImGui::SliderFloat("Intensity",  &light->intensity, 0.01f, 2000.0f, "%.3f", logarithmic);
            ImGui::ColorEdit3 ("Color",      &light->color.x,   ImGuiColorEditFlags_Float);
            ImGui::TreePop();
        }
    }
    ImGui::Separator();
    ImGui::Checkbox("Animation", &m_animation);
    ImGui::ColorEdit3("Ambient Light", &light_layer->ambient_light.x, ImGuiColorEditFlags_Float);

    ImGui::End();
}

} // namespace editor
