// Materials are prims of the scene tree, placed where the stage puts them
// (doc/usd-compatibility-plan.md U4): the reader parents a `Material` prim
// under the prim that holds it and the writer writes it back there, so a
// stage whose materials live in `/World/Looks` round-trips without gaining a
// `/Materials` scope. Two materials of one name in two scopes prove that a
// `material:binding` is resolved by path rather than by name.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_material_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
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

// The material prim at an erhe item path (M1), or null when the path names
// no prim or names one of another class.
[[nodiscard]] auto material_at(
    const std::shared_ptr<erhe::scene::Node>& root,
    const std::string&                        path
) -> erhe::primitive::Material*
{
    erhe::Hierarchy* prim = erhe::find_by_path(*root.get(), path);
    if (prim == nullptr) {
        return nullptr;
    }
    return erhe::is<erhe::primitive::Material>(prim) ? static_cast<erhe::primitive::Material*>(prim) : nullptr;
}

// The material the first primitive of the mesh prim at `path` binds.
[[nodiscard]] auto bound_material_of(
    const std::shared_ptr<erhe::scene::Node>& root,
    const std::string&                        path
) -> erhe::primitive::Material*
{
    erhe::Hierarchy* prim = erhe::find_by_path(*root.get(), path);
    if ((prim == nullptr) || !erhe::is<erhe::scene::Mesh>(prim)) {
        return nullptr;
    }
    const erhe::scene::Mesh* mesh = static_cast<const erhe::scene::Mesh*>(prim);
    if (mesh->get_primitives().empty()) {
        return nullptr;
    }
    return mesh->get_primitives().front().material.get();
}

class Looks_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("looks.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Looks_import, material_prims_land_where_the_stage_puts_them)
{
    EXPECT_TRUE(erhe::is<erhe::Scope>(erhe::find_by_path(*root.get(), "World/Looks")));
    EXPECT_TRUE(erhe::is<erhe::Scope>(erhe::find_by_path(*root.get(), "World/Other")));
    EXPECT_NE(material_at(root, "World/Looks/Gold"),   nullptr);
    EXPECT_NE(material_at(root, "World/Looks/Shared"), nullptr);
    EXPECT_NE(material_at(root, "World/Other/Shared"), nullptr);
    // No `Materials` scope is invented: the file said where its materials go.
    EXPECT_EQ(erhe::find_by_path(*root.get(), "Materials"), nullptr);
}

TEST_F(Looks_import, binding_is_resolved_by_path_not_by_name)
{
    erhe::primitive::Material* looks_shared = material_at(root, "World/Looks/Shared");
    erhe::primitive::Material* other_shared = material_at(root, "World/Other/Shared");
    ASSERT_NE(looks_shared, nullptr);
    ASSERT_NE(other_shared, nullptr);
    EXPECT_NE(looks_shared, other_shared);

    EXPECT_EQ(bound_material_of(root, "World/Group/gold_panel"),  material_at(root, "World/Looks/Gold"));
    EXPECT_EQ(bound_material_of(root, "World/Group/looks_panel"), looks_shared);
    EXPECT_EQ(bound_material_of(root, "World/Group/other_panel"), other_shared);

    using erhe::primitive::Material;
    EXPECT_NEAR(looks_shared->get_value(Material::base_color_property).y, 0.8f, 1e-5f);
    EXPECT_NEAR(other_shared->get_value(Material::base_color_property).x, 0.9f, 1e-5f);
}

class Looks_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        source      = load(test_data_path("looks.usda"), source_root);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("looks.usda");
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

TEST_F(Looks_round_trip, material_prims_are_written_where_they_sit)
{
    const std::string written = read_file(written_path);
    EXPECT_NE(written.find("def Scope \"Looks\""), std::string::npos) << written;
    EXPECT_NE(written.find("def Material \"Gold\""), std::string::npos) << written;
    EXPECT_EQ(written.find("def Scope \"Materials\""), std::string::npos) << written;

    EXPECT_NE(material_at(reloaded_root, "World/Looks/Gold"),   nullptr);
    EXPECT_NE(material_at(reloaded_root, "World/Looks/Shared"), nullptr);
    EXPECT_NE(material_at(reloaded_root, "World/Other/Shared"), nullptr);
}

TEST_F(Looks_round_trip, bindings_survive_the_round_trip)
{
    erhe::primitive::Material* looks_shared = material_at(reloaded_root, "World/Looks/Shared");
    erhe::primitive::Material* other_shared = material_at(reloaded_root, "World/Other/Shared");
    ASSERT_NE(looks_shared, nullptr);
    ASSERT_NE(other_shared, nullptr);
    EXPECT_EQ(bound_material_of(reloaded_root, "World/Group/gold_panel"),  material_at(reloaded_root, "World/Looks/Gold"));
    EXPECT_EQ(bound_material_of(reloaded_root, "World/Group/looks_panel"), looks_shared);
    EXPECT_EQ(bound_material_of(reloaded_root, "World/Group/other_panel"), other_shared);
}

// Writing what was just read back must reach a fixed point: the second file
// is the first one, byte for byte.
TEST_F(Looks_round_trip, second_save_is_byte_identical)
{
    const std::filesystem::path second_path = temporary_path("looks_second.usda");
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
