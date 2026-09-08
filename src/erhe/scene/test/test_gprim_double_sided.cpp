// `Gprim.double_sided` (USD `UsdGeomGprim.doubleSided`): an entry-store
// property of the geometry level, and one half of the rule
// erhe::scene::is_double_sided() spells for every render pass - a primitive is
// drawn from both sides when its material asks for it (glTF
// `material.doubleSided`) or when the prim itself does.

#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/gprim.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <memory>

using erhe::property::Value_source;
using erhe::scene::Gprim;
using erhe::scene::Mesh;
using erhe::scene::Xform;
using erhe::primitive::Material;

namespace {

[[nodiscard]] auto make_mesh(const std::shared_ptr<Material>& material) -> std::shared_ptr<Mesh>
{
    std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>("mesh");
    mesh->add_primitive(std::make_shared<erhe::primitive::Primitive>(erhe::primitive::Buffer_mesh{}), material);
    return mesh;
}

} // anonymous namespace

TEST(Gprim_double_sided, defaults_to_single_sided_and_becomes_local_when_written)
{
    std::shared_ptr<Mesh> mesh = make_mesh(nullptr);
    EXPECT_FALSE(mesh->get_double_sided());
    EXPECT_EQ(mesh->get_value_source(Gprim::double_sided_property.get()), Value_source::default_value);

    mesh->set_double_sided(true);
    EXPECT_TRUE(mesh->get_double_sided());
    EXPECT_EQ(mesh->get_value_source(Gprim::double_sided_property.get()), Value_source::local);

    mesh->clear_value(Gprim::double_sided_property);
    EXPECT_FALSE(mesh->get_double_sided());
    EXPECT_EQ(mesh->get_value_source(Gprim::double_sided_property.get()), Value_source::default_value);
}

TEST(Gprim_double_sided, inherits_from_the_holding_prim)
{
    std::shared_ptr<Xform> parent = std::make_shared<Xform>("parent");
    std::shared_ptr<Mesh>  mesh   = make_mesh(nullptr);
    erhe::scene::set_mesh_parent(mesh, parent);

    parent->set_value(Gprim::double_sided_property, true);
    EXPECT_TRUE(mesh->get_double_sided());
    EXPECT_EQ(mesh->get_value_source(Gprim::double_sided_property.get()), Value_source::inherited);

    mesh->set_double_sided(false); // a local value wins over the holder's
    EXPECT_FALSE(mesh->get_double_sided());
}

TEST(Gprim_double_sided, either_the_prim_or_the_material_makes_a_primitive_double_sided)
{
    std::shared_ptr<Material> material = std::make_shared<Material>("material");
    std::shared_ptr<Mesh>     mesh     = make_mesh(material);
    EXPECT_FALSE(erhe::scene::is_double_sided(*mesh.get(), mesh->get_primitives()[0]));

    material->set_double_sided(true);
    EXPECT_TRUE(erhe::scene::is_double_sided(*mesh.get(), mesh->get_primitives()[0]));

    material->set_double_sided(false);
    mesh->set_double_sided(true);
    EXPECT_TRUE(erhe::scene::is_double_sided(*mesh.get(), mesh->get_primitives()[0]));
}

TEST(Gprim_double_sided, a_primitive_without_a_material_follows_the_prim)
{
    std::shared_ptr<Mesh> mesh = make_mesh(nullptr);
    EXPECT_FALSE(erhe::scene::is_double_sided(*mesh.get(), mesh->get_primitives()[0]));

    mesh->set_double_sided(true);
    EXPECT_TRUE(erhe::scene::is_double_sided(*mesh.get(), mesh->get_primitives()[0]));
}
