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

namespace {

// The leaf BxDF (doc/plans/leaf_material.md): a thin double-sided surface.
auto make_leaf_style() -> std::shared_ptr<Style>
{
    using erhe::primitive::Material;
    std::shared_ptr<Style> style = std::make_shared<Style>("Leaf");
    style->set_value(Material::bxdf_model_property,   erhe::primitive::Bxdf_model::leaf);
    style->set_value(Material::double_sided_property, true);
    return style;
}

} // anonymous namespace

void add_default_materials(Content_library& library)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library.mutex};

    using erhe::primitive::Material;

    // The traits the metals share are one style (doc/erhe/property_system.md
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

    // Vegetation: front and back face albedo (linear), roughness and
    // reflectance, and the diffuse transmission. Reflectance 0.23 - 0.65 and
    // roughness 0.28 - 0.71 are the measured leaf ranges of the plan's
    // "Model" section; the back face is lighter and rougher than the front.
    const std::shared_ptr<Style> leaf = make_leaf_style();
    library.add(leaf);
    class Leaf_preset
    {
    public:
        const char* name;
        glm::vec3   front_color;
        float       front_roughness;
        float       front_reflectance;
        glm::vec3   back_color;
        float       back_roughness;
        float       back_reflectance;
        float       transmission;
        glm::vec3   transmission_color;
    };
    const Leaf_preset leaf_presets[] = {
        {"Leaf, glossy", {0.05f, 0.16f, 0.03f}, 0.30f, 0.65f, {0.12f, 0.22f, 0.08f}, 0.70f, 0.35f, 0.40f, {0.35f, 0.60f, 0.10f}},
        {"Leaf, matte",  {0.06f, 0.14f, 0.04f}, 0.70f, 0.30f, {0.12f, 0.20f, 0.09f}, 0.80f, 0.30f, 0.45f, {0.35f, 0.55f, 0.12f}},
        {"Grass blade",  {0.08f, 0.20f, 0.04f}, 0.50f, 0.40f, {0.10f, 0.22f, 0.06f}, 0.60f, 0.40f, 0.35f, {0.40f, 0.65f, 0.12f}},
        {"Petal",        {0.70f, 0.15f, 0.25f}, 0.60f, 0.35f, {0.75f, 0.30f, 0.40f}, 0.70f, 0.35f, 0.50f, {0.90f, 0.40f, 0.50f}}
    };
    for (const Leaf_preset& preset : leaf_presets) {
        const std::shared_ptr<Material> material = library.make<Material>(std::string_view{preset.name});
        material->set_style(leaf);
        material->set_value(Material::base_color_property,                 preset.front_color);
        material->set_value(Material::roughness_property,                  glm::vec2{preset.front_roughness, preset.front_roughness});
        material->set_value(Material::reflectance_property,                preset.front_reflectance);
        material->set_value(Material::back_base_color_property,            preset.back_color);
        material->set_value(Material::back_roughness_property,             preset.back_roughness);
        material->set_value(Material::back_reflectance_property,           preset.back_reflectance);
        material->set_value(Material::diffuse_transmission_property,       preset.transmission);
        material->set_value(Material::diffuse_transmission_color_property, preset.transmission_color);
    }

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
