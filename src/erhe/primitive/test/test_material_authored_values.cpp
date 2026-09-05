// A Material's local values are its AUTHORED values (doc/property-system.md
// D32): a Material_values snapshot at the defaults leaves every property at
// Value_source::default_value, and the generic elision pass takes back what
// a field-by-field importer fill made local.

#include "erhe_primitive/material.hpp"
#include "erhe_property/dependency_object.hpp"

#include <gtest/gtest.h>

#include <memory>

using erhe::primitive::Material;
using erhe::primitive::Material_create_info;
using erhe::primitive::Material_values;
using namespace erhe::property;

TEST(Material_authored_values, a_create_info_at_the_defaults_authors_nothing)
{
    const Material material{Material_create_info{.name = "Default"}};

    EXPECT_EQ(material.get_value_source(Material::base_color_property.get()),   Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::opacity_property.get()),      Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::roughness_property.get()),    Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::metallic_property.get()),     Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::reflectance_property.get()),  Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::emissive_property.get()),     Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::ior_property.get()),          Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::transmission_property.get()), Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::bxdf_model_property.get()),   Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::blending_mode_property.get()),Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::double_sided_property.get()), Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::alpha_cutoff_property.get()), Value_source::default_value);

    // The effective values are the ones the snapshot asked for.
    EXPECT_EQ(material.get_values(), Material_values{});
}

TEST(Material_authored_values, a_create_info_authors_the_fields_that_differ)
{
    Material_values values{};
    values.metallic = 1.0f;
    values.ior      = 1.5f; // the default
    const Material material{Material_create_info{.name = "Metal", .values = values}};

    EXPECT_EQ(material.get_value_source(Material::metallic_property.get()), Value_source::local);
    EXPECT_EQ(material.get_value_source(Material::ior_property.get()),      Value_source::default_value);
    EXPECT_EQ(material.get_metallic(), 1.0f);
    EXPECT_EQ(material.get_ior(),      1.5f);
}

// What an importer that writes every field one by one leaves behind, and
// what the generic pass makes of it.
TEST(Material_authored_values, the_elision_pass_takes_back_default_valued_locals)
{
    Material material{std::string_view{"Imported"}};
    material.set_base_color(glm::vec3{1.0f, 1.0f, 1.0f}); // the default
    material.set_metallic(0.25f);                         // authored
    ASSERT_EQ(material.get_value_source(Material::base_color_property.get()), Value_source::local);

    clear_default_valued_local_properties(material);

    EXPECT_EQ(material.get_value_source(Material::base_color_property.get()), Value_source::default_value);
    EXPECT_EQ(material.get_value_source(Material::metallic_property.get()),   Value_source::local);
    EXPECT_EQ(material.get_metallic(), 0.25f);
}
