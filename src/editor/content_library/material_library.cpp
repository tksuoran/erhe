#include "content_library/content_library.hpp"
#include "content_library/style.hpp"

#include "erhe_physics/physics_material.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"

namespace editor {

auto make_brushed_metal_style() -> std::shared_ptr<Style>
{
    using erhe::primitive::Material;
    std::shared_ptr<Style> style = std::make_shared<Style>("Brushed metal");
    style->set_value(Material::roughness_property,                  glm::vec2{0.34f, 0.20f});
    style->set_value(Material::metallic_property,                   1.0f);
    style->set_value(Material::bxdf_model_property,                 erhe::primitive::Bxdf_model::anisotropic_brdf);
    style->set_value(Material::use_circular_brushed_metal_property, true);
    style->set_value(Material::use_aniso_control_property,          true);
    return style;
}

void add_default_materials(Content_library& library)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library.mutex};

    using erhe::primitive::Material;

    // The traits the metals share are one style (doc/property-system.md
    // D25): each material carries only its base color as a local value, so
    // an edited trait stays a local override when the style is swapped.
    const std::shared_ptr<Style> brushed_metal = make_brushed_metal_style();
    library.add(brushed_metal);

    auto make = [&library, &brushed_metal](const char* name, float r, float g, float b)
    {
        const std::shared_ptr<Material> material = library.make<Material>(std::string_view{name});
        material->set_style(brushed_metal);
        material->set_value(Material::base_color_property, glm::vec3{r, g, b});
    };
    make("Titanium",  0.542f, 0.497f, 0.449f);
    make("Chromium",  0.549f, 0.556f, 0.554f);
    make("Iron",      0.562f, 0.565f, 0.578f);
    make("Nickel",    0.660f, 0.609f, 0.526f);
    make("Platinum",  0.673f, 0.637f, 0.585f);
    make("Copper",    0.955f, 0.638f, 0.538f);
    make("Palladium", 0.733f, 0.697f, 0.652f);
    make("Zinc",      0.664f, 0.824f, 0.850f);
    make("Gold",      1.022f, 0.782f, 0.344f);
    make("Aluminum",  0.913f, 0.922f, 0.924f);
    make("Silver",    0.972f, 0.960f, 0.915f);
    make("Cobalt",    0.662f, 0.655f, 0.634f);

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

#if 0
    for (size_t i = 0, end = 10; i < end; ++i) {
        const float rel        = static_cast<float>(i + 1) / static_cast<float>(end + 1);
        const float hue        = rel * 360.0f;
        const float saturation = 0.8f;
        const float value      = 0.25f;
        float R, G, B;
        erhe::math::hsv_to_rgb(hue, saturation, value, R, G, B);
        //const std::string label = fmt::format("Hue {}", static_cast<int>(hue));
        library.make<erhe::primitive::Material>(
            fmt::format("Hue {}", static_cast<int>(hue)),
            glm::vec3{R, G, B},
            glm::vec2{rel, rel}, // roughness
            0.95f // metalness
        );
    }
#endif
}

void add_default_physics_materials(Content_library& library)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library.mutex};

    // Spec-default KHR_physics_rigid_bodies material: static / dynamic
    // friction 0.6, restitution 0.0, average combine modes. These match the
    // Physics_material property defaults; set explicitly so the values stay
    // correct even if the property defaults change.
    std::shared_ptr<erhe::physics::Physics_material> default_material =
        library.make<erhe::physics::Physics_material>("Default");
    default_material->set_static_friction    (0.6f);
    default_material->set_dynamic_friction   (0.6f);
    default_material->set_restitution        (0.0f);
    default_material->set_friction_combine   (erhe::physics::Combine_mode::e_average);
    default_material->set_restitution_combine(erhe::physics::Combine_mode::e_average);
}

}
