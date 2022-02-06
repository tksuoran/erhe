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

    m_scene_root->make_material("Iron",     glm::vec3{0.560f, 0.570f, 0.580f});
    m_scene_root->make_material("Silver",   glm::vec3{0.972f, 0.960f, 0.915f});
    m_scene_root->make_material("Aluminum", glm::vec3{0.913f, 0.921f, 0.925f});
    m_scene_root->make_material("Gold",     glm::vec3{1.000f, 0.766f, 0.336f});
    m_scene_root->make_material("Copper",   glm::vec3{0.955f, 0.637f, 0.538f});
    m_scene_root->make_material("Chromium", glm::vec3{0.550f, 0.556f, 0.554f});
    m_scene_root->make_material("Nickel",   glm::vec3{0.660f, 0.609f, 0.526f});
    m_scene_root->make_material("Titanium", glm::vec3{0.542f, 0.497f, 0.449f});
    m_scene_root->make_material("Cobalt",   glm::vec3{0.662f, 0.655f, 0.634f});
    m_scene_root->make_material("Platinum", glm::vec3{0.672f, 0.637f, 0.585f});

    // Iron      = c4c7c7 (198, 198, 200)
    // Brass     = d6b97b (214, 185, 123)
    // Copper    = fad0c0 (250, 208, 192)
    // Gold      = ffe29b (255, 226, 155)
    // Aluminium = f5f6f6 (245, 246, 246)
    // Chrome    = c4c5c5 (196, 197, 197)
    // Silver    = fcfaf5 (252, 250, 245)
    // Cobalt    = d3d2cf (211, 210, 207)
    // Titanium  = c1bab1 (195, 186, 177)
    // Platinum  = d5d0c8 (213, 208, 200)
    // Nickel    = d3cbbe (211, 203, 190)
    // Zinc      = d5eaed (213, 234, 237)
    // Mercury   = e5e4e4 (229, 228, 228)
    // Palladium = ded9d3 (222, 217, 211)

    //for (size_t i = 0, end = 10; i < end; ++i)
    //{
    //    const float rel        = static_cast<float>(i) / static_cast<float>(end);
    //    const float hue        = rel * 360.0f;
    //    const float saturation = 0.9f;
    //    const float value      = 0.5f;
    //    float R, G, B;
    //    erhe::toolkit::hsv_to_rgb(hue, saturation, value, R, G, B);
    //    auto m = m_scene_root->make_material(
    //        fmt::format("Hue {}", static_cast<int>(hue)),
    //        glm::vec4{R, G, B, 1.0f},
    //        0.02f,
    //        0.00f,
    //        0.97f
    //    );
    //    m->visible = true;
    //}

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
