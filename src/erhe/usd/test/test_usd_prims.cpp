// The prim class hierarchy across the USD round trip
// (doc/usd-compatibility-plan.md U1): the importer creates the class the
// `typeName` names and the exporter writes the `typeName` the class names.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_prim_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

// The prim of `name` anywhere below `root`, whatever its class.
[[nodiscard]] auto find_prim(const std::shared_ptr<erhe::Hierarchy>& root, const std::string& name) -> std::shared_ptr<erhe::Hierarchy>
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : root->get_children()) {
        if (child->get_name() == name) {
            return child;
        }
        const std::shared_ptr<erhe::Hierarchy> found = find_prim(child, name);
        if (found) {
            return found;
        }
    }
    return {};
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::string
{
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] auto load(const std::filesystem::path& path, const std::shared_ptr<erhe::scene::Node>& root) -> erhe::usd::Usd_load_result
{
    const erhe::usd::Usd_load_arguments arguments{
        .path          = path,
        .root_node     = root,
        .mesh_layer_id = 0
    };
    return erhe::usd::load_usd(arguments);
}

class Prim_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("prims.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Prim_import, xform_prim_is_an_xform)
{
    const std::shared_ptr<erhe::Hierarchy> empty = find_prim(root, "Empty");
    ASSERT_TRUE(empty.operator bool());
    EXPECT_TRUE(erhe::is<erhe::scene::Xform>(empty.get()));
    EXPECT_EQ(empty->get_name(), "Empty");
}

TEST_F(Prim_import, scope_prim_is_a_scope)
{
    const std::shared_ptr<erhe::Hierarchy> group = find_prim(root, "Group");
    ASSERT_TRUE(group.operator bool());
    EXPECT_TRUE(erhe::is<erhe::Scope>(group.get()));
    EXPECT_FALSE(erhe::is<erhe::scene::Node>(group.get()));
    const erhe::Scope* scope = static_cast<const erhe::Scope*>(group.get());
    EXPECT_EQ(scope->get_prim_type_name(), "Scope");
    ASSERT_EQ(group->get_children().size(), 1u);
    EXPECT_EQ(group->get_children().front()->get_name(), "cube");
}

TEST_F(Prim_import, prim_without_an_erhe_class_is_typed)
{
    const std::shared_ptr<erhe::Hierarchy> box = find_prim(root, "Box");
    ASSERT_TRUE(box.operator bool());
    ASSERT_TRUE(erhe::is<erhe::Typed>(box.get()));
    EXPECT_FALSE(erhe::is<erhe::Scope>(box.get()));
    EXPECT_FALSE(erhe::is<erhe::scene::Node>(box.get()));
    EXPECT_EQ(static_cast<const erhe::Typed*>(box.get())->get_prim_type_name(), "Cube");
}

TEST_F(Prim_import, typeless_def_is_typed_without_a_token)
{
    const std::shared_ptr<erhe::Hierarchy> untyped = find_prim(root, "Untyped");
    ASSERT_TRUE(untyped.operator bool());
    ASSERT_TRUE(erhe::is<erhe::Typed>(untyped.get()));
    EXPECT_FALSE(erhe::is<erhe::scene::Node>(untyped.get()));
    EXPECT_EQ(static_cast<const erhe::Typed*>(untyped.get())->get_prim_type_name(), "");
    ASSERT_EQ(untyped->get_children().size(), 1u);
    EXPECT_TRUE(erhe::is<erhe::scene::Xform>(untyped->get_children().front().get()));
}

TEST_F(Prim_import, prims_are_listed)
{
    std::size_t scope_count = 0;
    std::size_t typed_count = 0;
    for (const std::shared_ptr<erhe::Typed>& prim : result.data.prims) {
        ASSERT_TRUE(prim.operator bool());
        if (erhe::is<erhe::Scope>(prim.get())) {
            ++scope_count;
        } else {
            ++typed_count;
        }
    }
    EXPECT_EQ(scope_count, 1u);
    EXPECT_EQ(typed_count, 2u); // the Cube prim and the typeless def
}

// A prim outside Xformable carries no transform, so the mesh under the scope
// composes with the transform of the Xform above it
// (doc/usd-compatibility-plan.md C5).
TEST_F(Prim_import, transform_composes_through_a_scope)
{
    const std::shared_ptr<erhe::Hierarchy> cube = find_prim(root, "cube");
    ASSERT_TRUE(cube.operator bool());
    ASSERT_TRUE(erhe::is<erhe::scene::Node>(cube.get()));
    const erhe::scene::Node* node = static_cast<const erhe::scene::Node*>(cube.get());
    const glm::vec3 world_position = glm::vec3{node->world_from_node() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    EXPECT_NEAR(world_position.x, 0.0f, 1e-5f);
    EXPECT_NEAR(world_position.y, 2.0f, 1e-5f);
    EXPECT_NEAR(world_position.z, 0.0f, 1e-5f);
}

class Prim_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        source      = load(test_data_path("prims.usda"), source_root);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("prims.usda");
        const erhe::usd::Usd_save_arguments save_arguments{
            .path      = written_path,
            .root_node = source_root,
            .materials = source.data.materials
        };
        const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        reloaded      = load(written_path, reloaded_root);
        ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    }

    std::shared_ptr<erhe::scene::Node> source_root;
    std::shared_ptr<erhe::scene::Node> reloaded_root;
    std::filesystem::path              written_path;
    erhe::usd::Usd_load_result         source;
    erhe::usd::Usd_load_result         reloaded;
};

TEST_F(Prim_round_trip, every_class_comes_back)
{
    const std::shared_ptr<erhe::Hierarchy> empty = find_prim(reloaded_root, "Empty");
    ASSERT_TRUE(empty.operator bool());
    EXPECT_TRUE(erhe::is<erhe::scene::Xform>(empty.get()));

    const std::shared_ptr<erhe::Hierarchy> group = find_prim(reloaded_root, "Group");
    ASSERT_TRUE(group.operator bool());
    EXPECT_TRUE(erhe::is<erhe::Scope>(group.get()));
    ASSERT_EQ(group->get_children().size(), 1u);
    EXPECT_TRUE(erhe::is<erhe::scene::Node>(group->get_children().front().get()));

    const std::shared_ptr<erhe::Hierarchy> box = find_prim(reloaded_root, "Box");
    ASSERT_TRUE(box.operator bool());
    ASSERT_TRUE(erhe::is<erhe::Typed>(box.get()));
    EXPECT_EQ(static_cast<const erhe::Typed*>(box.get())->get_prim_type_name(), "Cube");

    const std::shared_ptr<erhe::Hierarchy> untyped = find_prim(reloaded_root, "Untyped");
    ASSERT_TRUE(untyped.operator bool());
    ASSERT_TRUE(erhe::is<erhe::Typed>(untyped.get()));
    EXPECT_EQ(static_cast<const erhe::Typed*>(untyped.get())->get_prim_type_name(), "");
    ASSERT_EQ(untyped->get_children().size(), 1u);
    EXPECT_EQ(untyped->get_children().front()->get_name(), "Child");
}

TEST_F(Prim_round_trip, transform_still_composes_through_the_scope)
{
    const std::shared_ptr<erhe::Hierarchy> cube = find_prim(reloaded_root, "cube");
    ASSERT_TRUE(cube.operator bool());
    ASSERT_TRUE(erhe::is<erhe::scene::Node>(cube.get()));
    const erhe::scene::Node* node = static_cast<const erhe::scene::Node*>(cube.get());
    const glm::vec3 world_position = glm::vec3{node->world_from_node() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    EXPECT_NEAR(world_position.y, 2.0f, 1e-5f);
}

// Writing what was just read back must reach a fixed point: the second file
// is the first one, byte for byte.
TEST_F(Prim_round_trip, second_save_is_byte_identical)
{
    const std::filesystem::path second_path = temporary_path("prims_second.usda");
    const erhe::usd::Usd_save_arguments save_arguments{
        .path      = second_path,
        .root_node = reloaded_root,
        .materials = reloaded.data.materials
    };
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;
    EXPECT_EQ(read_file(second_path), read_file(written_path));
}

} // anonymous namespace
