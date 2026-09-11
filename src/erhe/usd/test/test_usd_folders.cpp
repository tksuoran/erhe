// A content-library folder is a `Scope` of the scene tree
// (doc/content-library-folders.md), and a USD file carries every `Scope` it
// is given: the writer writes one where it sits whatever it holds, and the
// reader makes an `erhe::Scope` item of one wherever it finds it, so a folder
// tree survives a save empty (doc/usd-compatibility-plan.md E4d).

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
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_folder_tests";
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

[[nodiscard]] auto scope_at(const std::shared_ptr<erhe::scene::Node>& root, const std::string& path) -> erhe::Scope*
{
    erhe::Hierarchy* prim = erhe::find_by_path(*root.get(), path);
    if (prim == nullptr) {
        return nullptr;
    }
    return erhe::is<erhe::Scope>(prim) ? static_cast<erhe::Scope*>(prim) : nullptr;
}

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

[[nodiscard]] auto occurrence_count(const std::string& haystack, const std::string& needle) -> std::size_t
{
    std::size_t count = 0;
    for (std::size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
        ++count;
    }
    return count;
}

// The folder tree of `data/folders.usda`: an empty `Scope` beside a nested
// one holding a material, and an empty sibling below that one.
class Folders_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        source      = load(test_data_path("folders.usda"), source_root);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("folders.usda");
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

TEST_F(Folders_round_trip, every_scope_of_the_file_is_a_scope_item)
{
    EXPECT_NE(scope_at(source_root, "World/Empty"),            nullptr);
    EXPECT_NE(scope_at(source_root, "World/Materials"),        nullptr);
    EXPECT_NE(scope_at(source_root, "World/Materials/Metals"), nullptr);
    EXPECT_NE(scope_at(source_root, "World/Materials/Unused"), nullptr);
    EXPECT_NE(material_at(source_root, "World/Materials/Metals/Copper"), nullptr);
}

TEST_F(Folders_round_trip, every_scope_is_written_where_it_sits)
{
    const std::string written = read_file(written_path);
    EXPECT_EQ(occurrence_count(written, "def Scope \"Empty\""),     1u) << written;
    EXPECT_EQ(occurrence_count(written, "def Scope \"Materials\""), 1u) << written;
    EXPECT_EQ(occurrence_count(written, "def Scope \"Metals\""),    1u) << written;
    EXPECT_EQ(occurrence_count(written, "def Scope \"Unused\""),    1u) << written;
}

TEST_F(Folders_round_trip, the_folder_tree_comes_back_in_place)
{
    EXPECT_NE(scope_at(reloaded_root, "World/Empty"),            nullptr);
    EXPECT_NE(scope_at(reloaded_root, "World/Materials"),        nullptr);
    EXPECT_NE(scope_at(reloaded_root, "World/Materials/Metals"), nullptr);
    EXPECT_NE(scope_at(reloaded_root, "World/Materials/Unused"), nullptr);
    EXPECT_NE(material_at(reloaded_root, "World/Materials/Metals/Copper"), nullptr);
    // The material stays in its folder rather than moving to the kind scope.
    EXPECT_EQ(material_at(reloaded_root, "World/Materials/Copper"), nullptr);
}

TEST_F(Folders_round_trip, second_save_is_byte_identical)
{
    const std::filesystem::path second_path = temporary_path("folders_second.usda");
    const erhe::usd::Usd_save_arguments save_arguments{
        .path      = second_path,
        .root_node = reloaded_root,
        .materials = reloaded.data.materials
    };
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;
    EXPECT_EQ(read_file(second_path), read_file(written_path));
}

// A folder the editor made carries `show_in_ui` and no `content` flag, the
// way the content library's kind scopes and the folders below them do: the
// writer carries it all the same, so a save keeps a folder that holds nothing
// the file carries.
class Editor_folders_export : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("save_root");

        std::shared_ptr<erhe::scene::Xform> world = std::make_shared<erhe::scene::Xform>("World");
        world->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        world->set_parent(root);

        std::shared_ptr<erhe::Scope> materials_scope = std::make_shared<erhe::Scope>("Materials");
        materials_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
        materials_scope->set_parent(world);

        std::shared_ptr<erhe::Scope> metals_scope = std::make_shared<erhe::Scope>("Metals");
        metals_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
        metals_scope->set_parent(materials_scope);

        material = std::make_shared<erhe::primitive::Material>(
            erhe::primitive::Material_create_info{.name = "Copper"}
        );
        material->set_parent(metals_scope);

        std::shared_ptr<erhe::Scope> unused_scope = std::make_shared<erhe::Scope>("Unused");
        unused_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
        unused_scope->set_parent(materials_scope);

        std::shared_ptr<erhe::Scope> brushes_scope = std::make_shared<erhe::Scope>("Brushes");
        brushes_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
        brushes_scope->set_parent(world);

        written_path = temporary_path("editor_folders.usda");
        const erhe::usd::Usd_save_arguments save_arguments{
            .path      = written_path,
            .root_node = root,
            .materials = std::vector<std::shared_ptr<erhe::primitive::Material>>{material}
        };
        const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        reloaded      = load(written_path, reloaded_root);
        ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    }

    std::shared_ptr<erhe::scene::Node>         root;
    std::shared_ptr<erhe::scene::Node>         reloaded_root;
    std::shared_ptr<erhe::primitive::Material> material;
    std::filesystem::path                      written_path;
    erhe::usd::Usd_load_result                 reloaded;
};

TEST_F(Editor_folders_export, a_folder_holding_nothing_carried_is_written)
{
    const std::string written = read_file(written_path);
    EXPECT_EQ(occurrence_count(written, "def Scope \"Unused\""),  1u) << written;
    EXPECT_EQ(occurrence_count(written, "def Scope \"Brushes\""), 1u) << written;
    EXPECT_EQ(occurrence_count(written, "def Scope \"Metals\""),  1u) << written;
}

TEST_F(Editor_folders_export, the_folders_come_back_as_scopes_in_place)
{
    EXPECT_NE(scope_at(reloaded_root, "World/Materials"),        nullptr);
    EXPECT_NE(scope_at(reloaded_root, "World/Materials/Metals"), nullptr);
    EXPECT_NE(scope_at(reloaded_root, "World/Materials/Unused"), nullptr);
    EXPECT_NE(scope_at(reloaded_root, "World/Brushes"),          nullptr);
    EXPECT_NE(material_at(reloaded_root, "World/Materials/Metals/Copper"), nullptr);
}

} // anonymous namespace
