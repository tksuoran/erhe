#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

namespace {

[[nodiscard]] auto prototype_test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto find_child(const std::shared_ptr<erhe::Hierarchy>& parent, const std::string& name) -> std::shared_ptr<erhe::Hierarchy>
{
    if (!parent) {
        return {};
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : parent->get_children()) {
        if (child && (child->get_name() == name)) {
            return child;
        }
    }
    return {};
}

[[nodiscard]] auto load(const char* file_name, std::shared_ptr<erhe::scene::Node>& out_root) -> erhe::usd::Usd_load_result
{
    out_root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments arguments{
        .path          = prototype_test_data_path(file_name),
        .root_node     = out_root,
        .mesh_layer_id = 0
    };
    return erhe::usd::load_usd(arguments);
}

} // namespace

// doc/usd-compatibility-plan.md X3: a `def` descendant of a `class` prim is a
// prototype - a prim of the tree, held abstract.
TEST(Class_prototypes, def_descendants_are_prims)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("class_prototypes.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    ASSERT_EQ(result.data.class_prototypes.size(), 1u);
    const erhe::usd::Usd_class_prototype& prototype = result.data.class_prototypes[0];
    EXPECT_EQ(prototype.stage_path, "/Prototypes/bolt");
    EXPECT_EQ(prototype.class_path, "/Prototypes");
    ASSERT_TRUE(prototype.item);
    EXPECT_EQ(prototype.item->get_name(), "bolt");

    // The class prim itself stays a class record, and the `class` it holds is
    // a nested class - only the `def` became a prim.
    ASSERT_EQ(result.data.classes.size(), 1u);
    EXPECT_EQ(result.data.classes[0].stage_path, "/Prototypes");
    ASSERT_EQ(result.data.classes[0].children.size(), 1u);
    EXPECT_EQ(result.data.classes[0].children[0].stage_path, "/Prototypes/Shiny");
}

TEST(Class_prototypes, prototype_subtree_carries_no_content)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("class_prototypes.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.class_prototypes.size(), 1u);

    const std::shared_ptr<erhe::Item_base>& prototype = result.data.class_prototypes[0].item;
    ASSERT_TRUE(prototype);
    EXPECT_EQ(prototype->get_flag_bits() & erhe::Item_flags::content, 0u);
    EXPECT_NE(prototype->get_flag_bits() & erhe::Item_flags::show_in_ui, 0u);

    const std::shared_ptr<erhe::Hierarchy> prototype_prim = std::dynamic_pointer_cast<erhe::Hierarchy>(prototype);
    ASSERT_TRUE(prototype_prim);
    const std::shared_ptr<erhe::Hierarchy> mesh = find_child(prototype_prim, "bolt_mesh");
    ASSERT_TRUE(mesh);
    EXPECT_EQ(mesh->get_flag_bits() & erhe::Item_flags::content, 0u);

    // Every prim of the file that is not a prototype is content.
    const std::shared_ptr<erhe::Hierarchy> world = find_child(root, "World");
    ASSERT_TRUE(world);
    EXPECT_NE(world->get_flag_bits() & erhe::Item_flags::content, 0u);
}

// The prototype is converted where the class prim's own holder is: the caller
// moves it under the Style item the class prim becomes.
TEST(Class_prototypes, prototype_is_held_by_the_class_prims_holder)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("class_prototypes.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_TRUE(find_child(root, "bolt"));
    EXPECT_FALSE(find_child(root, "Prototypes"));
}

TEST(Class_prototypes, referencing_prims_carry_their_arcs)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("class_prototypes.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.references.size(), 3u);
    for (const erhe::usd::Usd_prim_references& entry : result.data.references) {
        ASSERT_EQ(entry.references.size(), 1u);
        EXPECT_TRUE(entry.references[0].asset_path.empty());
        EXPECT_EQ(entry.references[0].prim_path, "/Prototypes/bolt");
    }
}

// doc/usd-compatibility-plan.md S1: any prim is a reference target - a `Scope`
// and a typeless `def` are prims of the tree the same way an `Xform` is.
TEST(Scope_reference_target, scope_and_typeless_targets_are_prims)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("scope_target.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const std::shared_ptr<erhe::Hierarchy> materials = find_child(root, "materials");
    ASSERT_TRUE(materials);
    EXPECT_FALSE(std::dynamic_pointer_cast<erhe::scene::Node>(materials));
    EXPECT_TRUE(find_child(materials, "panel"));
    EXPECT_TRUE(find_child(materials, "Red"));

    const std::shared_ptr<erhe::Hierarchy> geometry = find_child(root, "Geometry");
    ASSERT_TRUE(geometry);
    EXPECT_FALSE(std::dynamic_pointer_cast<erhe::scene::Node>(geometry));
    EXPECT_TRUE(find_child(geometry, "box"));
}

TEST(Scope_reference_target, arcs_name_the_scope_and_the_typeless_prim)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("references_scope_target.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.references.size(), 2u);
    EXPECT_EQ(result.data.references[0].stage_path, "/World/ScopeRef");
    ASSERT_EQ(result.data.references[0].references.size(), 1u);
    EXPECT_EQ(result.data.references[0].references[0].prim_path, "/materials");
    EXPECT_EQ(result.data.references[1].stage_path, "/World/TypelessRef");
    ASSERT_EQ(result.data.references[1].references.size(), 1u);
    EXPECT_EQ(result.data.references[1].references[0].prim_path, "/Geometry");
}

// A root-level `over` with `def` descendants: the prim holds its place, and
// the `Material` prim below it converts although no mesh of this file binds
// it, so a reference to the `over` root finds the material.
TEST(Over_root, over_root_and_its_material_are_prims)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("over_root.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const std::shared_ptr<erhe::Hierarchy> over_root = find_child(root, "ASSET_mtl");
    ASSERT_TRUE(over_root);
    const std::shared_ptr<erhe::Hierarchy> scope = find_child(over_root, "mtl");
    ASSERT_TRUE(scope);

    ASSERT_EQ(result.data.materials.size(), 1u);
    ASSERT_TRUE(result.data.materials[0]);
    EXPECT_EQ(result.data.materials[0]->get_name(), "PanelMaterial");
    const std::shared_ptr<erhe::Hierarchy> material = find_child(scope, "PanelMaterial");
    ASSERT_TRUE(material);
    EXPECT_EQ(material.get(), result.data.materials[0].get());
}

// A material no mesh of the file binds still becomes a prim of the tree
// (the "has no converted material" gap of the usd-wg survey).
TEST(Unbound_materials, unbound_material_prims_convert)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("scope_target.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.materials.size(), 1u);
    EXPECT_EQ(result.data.materials[0]->get_name(), "Red");
}
