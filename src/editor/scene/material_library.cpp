#include "scene/material_library.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"

#include <fmt/format.h>

namespace editor {

void add_default_materials(Content_library& library)
{
    //const glm::vec2 roughness{0.68f, 0.34f};
    const glm::vec2 roughness{0.34f, 0.20f};

    auto& materials = *library.materials.get();

    materials.make<erhe::primitive::Material>("Default",   glm::vec3{0.500f, 0.500f, 0.500f}, roughness, 0.0f);
    materials.make<erhe::primitive::Material>("Titanium",  glm::vec3{0.542f, 0.497f, 0.449f}, roughness, 1.0f);
    materials.make<erhe::primitive::Material>("Chromium",  glm::vec3{0.549f, 0.556f, 0.554f}, roughness, 1.0f);
    materials.make<erhe::primitive::Material>("Iron",      glm::vec3{0.562f, 0.565f, 0.578f}, roughness, 1.0f);
    materials.make<erhe::primitive::Material>("Nickel",    glm::vec3{0.660f, 0.609f, 0.526f}, roughness, 1.0f);
    materials.make<erhe::primitive::Material>("Platinum",  glm::vec3{0.673f, 0.637f, 0.585f}, roughness, 1.0f);
    materials.make<erhe::primitive::Material>("Copper",    glm::vec3{0.955f, 0.638f, 0.538f}, roughness, 1.0f);
    materials.make<erhe::primitive::Material>("Palladium", glm::vec3{0.733f, 0.697f, 0.652f}, roughness, 1.0f);
    materials.make<erhe::primitive::Material>("Zinc",      glm::vec3{0.664f, 0.824f, 0.850f}, roughness, 1.0f);
    materials.make<erhe::primitive::Material>("Gold",      glm::vec3{1.022f, 0.782f, 0.344f}, roughness, 1.0f);
    materials.make<erhe::primitive::Material>("Aluminum",  glm::vec3{0.913f, 0.922f, 0.924f}, roughness, 1.0f);
    materials.make<erhe::primitive::Material>("Silver",    glm::vec3{0.972f, 0.960f, 0.915f}, roughness, 1.0f);

    materials.make<erhe::primitive::Material>("Cobalt",    glm::vec3{0.662f, 0.655f, 0.634f}, roughness, 1.0f);

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

    for (size_t i = 0, end = 10; i < end; ++i) {
        const float rel        = static_cast<float>(i + 1) / static_cast<float>(end + 1);
        const float hue        = rel * 360.0f;
        const float saturation = 0.8f;
        const float value      = 0.25f;
        float R, G, B;
        erhe::math::hsv_to_rgb(hue, saturation, value, R, G, B);
        //const std::string label = fmt::format("Hue {}", static_cast<int>(hue));
        materials.make<erhe::primitive::Material>(
            fmt::format("Hue {}", static_cast<int>(hue)),
            glm::vec3{R, G, B},
            glm::vec2{rel, rel}, // roughness
            0.95f // metalness
        );
    }
}

} // namespace editor
