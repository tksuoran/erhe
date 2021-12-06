#include "windows/materials.hpp"
#include "tools.hpp"
#include "scene/scene_root.hpp"

#include "erhe/imgui/imgui_helpers.hpp"
#include "erhe/primitive/material.hpp"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace editor
{

Materials::Materials()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Materials::~Materials() = default;

void Materials::connect()
{
    m_scene_root = get<Scene_root>();
}

auto Materials::selected_material() const -> std::shared_ptr<erhe::primitive::Material>
{
    return m_selected_material;
}

void Materials::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);

    if constexpr (true) // White default material
    {
        auto m = m_scene_root->make_material(
            "Default Material",
            glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            0.50f,
            0.00f,
            0.50f
        );
        m->visible = true;
    }

    for (size_t i = 0, end = 10; i < end; ++i)
    {
        const float rel        = static_cast<float>(i) / static_cast<float>(end);
        const float hue        = rel * 360.0f;
        const float saturation = 0.9f;
        const float value      = 1.0f;
        float R, G, B;
        erhe::toolkit::hsv_to_rgb(hue, saturation, value, R, G, B);
        auto m = m_scene_root->make_material(
            fmt::format("Hue {}", static_cast<int>(hue)),
            glm::vec4{R, G, B, 1.0f},
            0.444f,
            0.95f,
            0.70f
        );
        m->visible = true;
    }

}

void Materials::imgui()
{
    const auto& materials = m_scene_root->materials();
    using namespace erhe::imgui;

    ImGui::Begin(c_title.data());
    const auto button_size = ImVec2{ImGui::GetContentRegionAvailWidth(), 0.0f};
    for (const auto& material : materials)
    {
        if (material->visible == false)
        {
            continue;
        }
        bool selected = m_selected_material == material;
        const bool button_pressed = make_button(
            material->name.c_str(),
            selected ? Item_mode::active : Item_mode::normal,
            button_size
        );
        if (button_pressed)
        {
            m_selected_material = material;
        }
    }
    ImGui::End();
}

}
