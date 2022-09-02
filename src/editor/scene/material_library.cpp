#include "scene/material_library.hpp"

#include "erhe/toolkit/math_util.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>

namespace editor
{

Material_library::Material_library()
{
}

void Material_library::add_default_materials()
{
    const glm::vec2 roughness{0.68f, 0.34f};

    make_material("Default",   glm::vec3{0.500f, 0.500f, 0.500f}, roughness, 0.0f);
    make_material("Titanium",  glm::vec3{0.542f, 0.497f, 0.449f}, roughness, 1.0f);
    make_material("Chromium",  glm::vec3{0.549f, 0.556f, 0.554f}, roughness, 1.0f);
    make_material("Iron",      glm::vec3{0.562f, 0.565f, 0.578f}, roughness, 1.0f);
    make_material("Nickel",    glm::vec3{0.660f, 0.609f, 0.526f}, roughness, 1.0f);
    make_material("Platinum",  glm::vec3{0.673f, 0.637f, 0.585f}, roughness, 1.0f);
    make_material("Copper",    glm::vec3{0.955f, 0.638f, 0.538f}, roughness, 1.0f);
    make_material("Palladium", glm::vec3{0.733f, 0.697f, 0.652f}, roughness, 1.0f);
    make_material("Zinc",      glm::vec3{0.664f, 0.824f, 0.850f}, roughness, 1.0f);
    make_material("Gold",      glm::vec3{1.022f, 0.782f, 0.344f}, roughness, 1.0f);
    make_material("Aluminum",  glm::vec3{0.913f, 0.922f, 0.924f}, roughness, 1.0f);
    make_material("Silver",    glm::vec3{0.972f, 0.960f, 0.915f}, roughness, 1.0f);

    make_material("Cobalt",    glm::vec3{0.662f, 0.655f, 0.634f}, roughness, 1.0f);

    // water          0.020
    // plastic, glass 0.040 .. 0.045
    // crystal, gems  0.050 .. 0.080
    // diamondlike    0.100 .. 0.200

    // 0.2 - 0.45 forbidden zone

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

    for (size_t i = 0, end = 10; i < end; ++i)
    {
        const float rel        = static_cast<float>(i) / static_cast<float>(end);
        const float hue        = rel * 360.0f;
        const float saturation = 0.9f;
        const float value      = 0.5f;
        float R, G, B;
        erhe::toolkit::hsv_to_rgb(hue, saturation, value, R, G, B);
        //const std::string label = fmt::format("Hue {}", static_cast<int>(hue));
        make_material(
            fmt::format("Hue {}", static_cast<int>(hue)),
            glm::vec3{R, G, B},
            glm::vec2{0.02f, 0.02f},
            0.00f
        );
    }
}

[[nodiscard]] auto Material_library::materials() -> std::vector<std::shared_ptr<erhe::primitive::Material>>&
{
    return m_materials;
}

[[nodiscard]] auto Material_library::materials() const -> const std::vector<std::shared_ptr<erhe::primitive::Material>>&
{
    return m_materials;
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
auto Material_library::material_combo(
    const char*                                 label,
    std::shared_ptr<erhe::primitive::Material>& selected_material,
    const bool                                  empty_option
) const -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    int selection_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    const bool empty_entry = empty_option || (selected_material == nullptr);
    if (empty_entry)
    {
        names.push_back("(none)");
        materials.push_back({});
        ++index;
    }
    for (const auto& material : m_materials)
    {
        if (!material->visible)
        {
            continue;
        }
        names.push_back(material->name.c_str());
        materials.push_back(material);
        if (selected_material == material)
        {
            selection_index = index;
        }
        ++index;
    }

    // TODO Move to begin / end combo
    const bool selection_changed =
        ImGui::Combo(
            label,
            &selection_index,
            names.data(),
            static_cast<int>(names.size())
        ) &&
        (selected_material != materials.at(selection_index));
    if (selection_changed)
    {
        selected_material = materials.at(selection_index);
    }
    return selection_changed;
}
#endif

} // namespace editor
