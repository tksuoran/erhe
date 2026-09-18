// A Material with a style (D25 in doc/property_system.md): the style
// supplies shared traits, a local value wins, get_values bakes the
// effective values, operator== sees the style.

#include "erhe_primitive/material.hpp"
#include "erhe_property/property_style.hpp"

#include <gtest/gtest.h>

#include <memory>

using erhe::primitive::Material;
using namespace erhe::property;

namespace {

auto make_style() -> std::shared_ptr<const Property_style>
{
    Property_set values;
    values.set(Material::roughness_property.get(), Property_value{glm::vec2{0.34f, 0.20f}});
    values.set(Material::metallic_property.get(),  Property_value{1.0f});
    values.set(Material::bxdf_model_property.get(), Property_value{Enum_value{static_cast<int32_t>(erhe::primitive::Bxdf_model::anisotropic_brdf)}});
    return std::make_shared<const Property_style>("Brushed metal", std::move(values));
}

} // namespace

TEST(Material_style, style_supplies_traits_and_local_wins)
{
    auto material = std::make_shared<Material>(std::string_view{"Gold"});
    const std::shared_ptr<const Property_style> style = make_style();
    ASSERT_TRUE(material->set_style(style));
    material->set_value(Material::base_color_property, glm::vec3{1.0f, 0.8f, 0.3f});

    EXPECT_EQ(material->get_metallic(), 1.0f);
    EXPECT_EQ(material->get_roughness(), (glm::vec2{0.34f, 0.20f}));
    EXPECT_EQ(material->get_bxdf_model(), erhe::primitive::Bxdf_model::anisotropic_brdf);
    EXPECT_EQ(material->get_value_source(Material::metallic_property.get()), Value_source::style);
    EXPECT_EQ(material->get_value_source(Material::base_color_property.get()), Value_source::local);
    EXPECT_FALSE(material->has_local_value(Material::metallic_property.get()));

    material->set_metallic(0.5f); // local override
    EXPECT_EQ(material->get_metallic(), 0.5f);
    EXPECT_EQ(material->get_value_source(Material::metallic_property.get()), Value_source::local);
    material->clear_value(Material::metallic_property);
    EXPECT_EQ(material->get_metallic(), 1.0f); // back to the style

    // get_values bakes the effective values; set_values would write locals.
    const erhe::primitive::Material_values values = material->get_values();
    EXPECT_EQ(values.metallic, 1.0f);
    EXPECT_EQ(values.roughness, (glm::vec2{0.34f, 0.20f}));

    ASSERT_TRUE(material->set_style(nullptr));
    EXPECT_NE(material->get_metallic(), 1.0f); // the metadata default
    EXPECT_EQ(material->get_value_source(Material::metallic_property.get()), Value_source::default_value);
}

TEST(Material_style, equality_and_copy_see_the_style)
{
    auto a = std::make_shared<Material>(std::string_view{"M"});
    auto b = std::make_shared<Material>(std::string_view{"M"});
    const std::shared_ptr<const Property_style> style = make_style();
    ASSERT_TRUE(a->set_style(style));
    EXPECT_FALSE(*a == *b);
    ASSERT_TRUE(b->set_style(style));
    EXPECT_TRUE(*a == *b);

    auto copy = std::make_shared<Material>(*a);
    EXPECT_EQ(copy->get_style(), style);
    EXPECT_EQ(copy->get_metallic(), 1.0f);
}
