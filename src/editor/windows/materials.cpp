#include "windows/materials.hpp"
#include "editor_imgui_windows.hpp"
#include "imgui_helpers.hpp"

#include "scene/scene_root.hpp"

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

auto Materials::selected_material() const -> std::shared_ptr<erhe::primitive::Material>
{
    return m_selected_material;
}

void Materials::connect()
{
    m_scene_root = require<Scene_root>();
    require<Editor_imgui_windows>();
}

void Materials::initialize_component()
{
    get<Editor_imgui_windows>()->register_imgui_window(this);

    if constexpr (true) // White default material
    {
        auto m = m_scene_root->make_material(
            "Default Material",
            glm::vec4{0.5f, 0.5f, 0.5f, 1.0f},
            0.02f,
            0.00f,
            0.97f
        );
        m->visible = true;
    }

    for (size_t i = 0, end = 10; i < end; ++i)
    {
        const float rel        = static_cast<float>(i) / static_cast<float>(end);
        const float hue        = rel * 360.0f;
        const float saturation = 0.9f;
        const float value      = 0.5f;
        float R, G, B;
        erhe::toolkit::hsv_to_rgb(hue, saturation, value, R, G, B);
        auto m = m_scene_root->make_material(
            fmt::format("Hue {}", static_cast<int>(hue)),
            glm::vec4{R, G, B, 1.0f},
            0.02f,
            0.00f,
            0.97f
        );
        m->visible = true;
    }

}

void Materials::imgui()
{
    const auto& materials = m_scene_root->materials();

    const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};
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
}

}
