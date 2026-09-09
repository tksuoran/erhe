// The prim's composed specifier (doc/usd-compatibility-plan.md X2): a prim no
// layer defines composes as `over`, USD's default traversal predicate reaches
// neither it nor anything below it, and erhe carries that as the item's
// `defined` property feeding the derived Item_flags::active bit. The prim is
// still a prim of the tree - a valid reference target - and the writer spells
// its specifier back from `defined`.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace {

[[nodiscard]] auto defined_test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto defined_temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_defined_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
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
        .path          = defined_test_data_path(file_name),
        .root_node     = out_root,
        .mesh_layer_id = 0
    };
    return erhe::usd::load_usd(arguments);
}

} // namespace

// The composed stage of `defined.usda`: `/World` and `/World/definedCube` are
// `over` opinions on top of a sublayer that defines them, so composition
// promotes both to `def`; `/World/undefinedCube` has no defining opinion in
// any layer and keeps `over`.
TEST(Defined_prims, an_undefined_prim_is_a_prim_of_the_tree)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("defined.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const std::shared_ptr<erhe::Hierarchy> world = find_child(root, "World");
    ASSERT_TRUE(world);
    EXPECT_TRUE(find_child(world, "definedCube"));
    EXPECT_TRUE(find_child(world, "undefinedCube"));
}

TEST(Defined_prims, a_defined_prim_has_no_local_value)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("defined.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const std::shared_ptr<erhe::Hierarchy> world = find_child(root, "World");
    ASSERT_TRUE(world);
    EXPECT_FALSE(world->read_local_value(erhe::Item_base::defined_property).has_value());
    EXPECT_TRUE (world->is_active());

    const std::shared_ptr<erhe::Hierarchy> defined_cube = find_child(world, "definedCube");
    ASSERT_TRUE(defined_cube);
    EXPECT_FALSE(defined_cube->read_local_value(erhe::Item_base::defined_property).has_value());
    EXPECT_TRUE (defined_cube->is_active());
}

TEST(Defined_prims, an_undefined_prim_is_not_active)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("defined.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const std::shared_ptr<erhe::Hierarchy> world = find_child(root, "World");
    ASSERT_TRUE(world);
    const std::shared_ptr<erhe::Hierarchy> undefined_cube = find_child(world, "undefinedCube");
    ASSERT_TRUE(undefined_cube);
    EXPECT_EQ(undefined_cube->read_local_value(erhe::Item_base::defined_property), std::optional<bool>{false});
    EXPECT_TRUE (undefined_cube->get_value(erhe::Item_base::active_property));
    EXPECT_FALSE(undefined_cube->is_active());
}

// A root-level `over` holding `def` descendants: the whole subtree is out of
// the default traversal, whatever a descendant says of itself, so the `def`
// child's own `defined` value is true and its derived bit is still clear.
TEST(Defined_prims, the_subtree_of_an_undefined_prim_is_not_active)
{
    std::shared_ptr<erhe::scene::Node> root;
    const erhe::usd::Usd_load_result   result = load("defined_over_root.usda", root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const std::shared_ptr<erhe::Hierarchy> over_root = find_child(root, "Root");
    ASSERT_TRUE(over_root);
    EXPECT_EQ(over_root->read_local_value(erhe::Item_base::defined_property), std::optional<bool>{false});
    EXPECT_FALSE(over_root->is_active());

    const std::shared_ptr<erhe::Hierarchy> child = find_child(over_root, "X");
    ASSERT_TRUE(child);
    EXPECT_FALSE(child->read_local_value(erhe::Item_base::defined_property).has_value());
    EXPECT_TRUE (child->get_value(erhe::Item_base::defined_property));
    EXPECT_FALSE(child->is_active());

    const std::shared_ptr<erhe::Hierarchy> world = find_child(root, "World");
    ASSERT_TRUE(world);
    EXPECT_TRUE(world->is_active());
}

// The writer spells the specifier from `defined`: an item written as `def`
// would come back defined, so the reload is what proves the `over`.
TEST(Defined_prims, the_specifier_survives_the_round_trip)
{
    std::shared_ptr<erhe::scene::Node>     source_root;
    const erhe::usd::Usd_load_result       source = load("defined.usda", source_root);
    ASSERT_TRUE(source.error.empty()) << source.error;

    const std::filesystem::path written_path = defined_temporary_path("defined.usda");
    const erhe::usd::Usd_save_arguments save_arguments{
        .path      = written_path,
        .root_node = source_root,
        .materials = source.data.materials
    };
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;

    const std::shared_ptr<erhe::scene::Node> reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_arguments reload_arguments{
        .path          = written_path,
        .root_node     = reloaded_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(reload_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;

    const std::shared_ptr<erhe::Hierarchy> world = find_child(reloaded_root, "World");
    ASSERT_TRUE(world);
    EXPECT_TRUE(world->is_active());

    const std::shared_ptr<erhe::Hierarchy> defined_cube = find_child(world, "definedCube");
    ASSERT_TRUE(defined_cube);
    EXPECT_FALSE(defined_cube->read_local_value(erhe::Item_base::defined_property).has_value());
    EXPECT_TRUE (defined_cube->is_active());

    const std::shared_ptr<erhe::Hierarchy> undefined_cube = find_child(world, "undefinedCube");
    ASSERT_TRUE(undefined_cube);
    EXPECT_EQ(undefined_cube->read_local_value(erhe::Item_base::defined_property), std::optional<bool>{false});
    EXPECT_FALSE(undefined_cube->is_active());
}
