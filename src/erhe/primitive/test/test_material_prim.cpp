// A Material is a typed prim (doc/usd-compatibility-plan.md U4): it carries
// the USD `UsdShadeMaterial` typeName token and it can be parented in a
// prim tree, though nothing places it there yet.

#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_primitive/material.hpp"

#include <gtest/gtest.h>

#include <memory>

using erhe::primitive::Material;

TEST(Material_prim, carries_the_material_token)
{
    const std::shared_ptr<Material> material = std::make_shared<Material>("Copper");

    EXPECT_TRUE(erhe::is<erhe::Typed>(material));
    EXPECT_TRUE(erhe::is<Material>(material));
    EXPECT_EQ(material->get_prim_type_name(), "Material");
    EXPECT_EQ(material->get_class_type_name(), "Material");

    // The class fixes the token, so it is not authorable.
    material->set_prim_type_name("Xform");
    EXPECT_EQ(material->get_prim_type_name(), "Material");
}

TEST(Material_prim, composes_a_path_under_a_scope)
{
    const std::shared_ptr<erhe::Scope> root  = std::make_shared<erhe::Scope>("root");
    const std::shared_ptr<erhe::Scope> scope = std::make_shared<erhe::Scope>("Materials");
    const std::shared_ptr<Material>    copper = std::make_shared<Material>("Copper");
    scope->set_parent(root);
    copper->set_parent(scope);

    EXPECT_EQ(copper->get_path(), "Materials/Copper");
    EXPECT_EQ(erhe::find_by_path(*root, "Materials/Copper"), copper.get());
    EXPECT_EQ(copper->get_reference_path(), "Materials/Copper");
}

TEST(Material_prim, outside_a_tree_it_is_named_by_its_name)
{
    const std::shared_ptr<Material> material = std::make_shared<Material>("Copper");

    EXPECT_EQ(material->get_path(), "");
    EXPECT_EQ(material->get_reference_path(), "Copper");
}
