#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_sublayer_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::string
{
    std::ifstream     stream{path, std::ios::binary};
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] auto find_node(const erhe::usd::Usd_data& data, const std::string& name) -> std::shared_ptr<erhe::scene::Node>
{
    for (const std::shared_ptr<erhe::scene::Node>& node : data.nodes) {
        if (node && (node->get_name() == name)) {
            return node;
        }
    }
    return {};
}

[[nodiscard]] auto local_translation(const std::shared_ptr<erhe::scene::Node>& node) -> glm::vec3
{
    const glm::mat4 parent_from_node = node->parent_from_node();
    return glm::vec3{parent_from_node[3]};
}

// `sublayers.usda` lists two sublayers that disagree, and overrides one of
// their prims with an `over` of its own (doc/usd-compatibility-plan.md S1).
class Sublayers_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("sublayers.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Sublayers_import, every_sublayer_prim_is_in_the_composed_tree)
{
    // `World` and `Shared` and `RootOver` come from the stronger sublayer,
    // `WeakOnly` only from the weaker one, and the root layer authors no prim
    // of its own beyond the `over`.
    for (const char* name : {"World", "Shared", "RootOver", "WeakOnly"}) {
        EXPECT_TRUE(find_node(result.data, name).operator bool()) << name;
    }
}

TEST_F(Sublayers_import, the_stronger_sublayer_wins_over_the_weaker)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, "Shared");
    ASSERT_TRUE(node.operator bool());
    EXPECT_FLOAT_EQ(local_translation(node).x, 1.0f);
}

TEST_F(Sublayers_import, the_root_layers_over_wins_over_every_sublayer)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, "RootOver");
    ASSERT_TRUE(node.operator bool());
    EXPECT_FLOAT_EQ(local_translation(node).x, 9.0f);
}

TEST_F(Sublayers_import, a_prim_only_the_weaker_sublayer_authors_is_added_whole)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, "WeakOnly");
    ASSERT_TRUE(node.operator bool());
    EXPECT_FLOAT_EQ(local_translation(node).x, 3.0f);
    // Its child mesh came with it.
    EXPECT_FALSE(result.data.meshes.empty());
}

TEST_F(Sublayers_import, the_sublayers_are_reported)
{
    ASSERT_EQ(result.data.sublayers.size(), 2u);
    EXPECT_EQ(result.data.sublayers[0], "./sublayer_strong.usda");
    EXPECT_EQ(result.data.sublayers[1], "./sublayer_weak.usda");
}

TEST_F(Sublayers_import, stage_metadata_the_root_leaves_unauthored_comes_from_the_strongest_sublayer)
{
    EXPECT_DOUBLE_EQ(result.data.meters_per_unit, 0.01);
    EXPECT_EQ(result.data.up_axis, "Y");
}

TEST(Sublayers_stage_metadata, the_root_layers_own_opinion_wins)
{
    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments      load_arguments{
        .path          = test_data_path("sublayers_meters.usda"),
        .root_node     = root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result result = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_DOUBLE_EQ(result.data.meters_per_unit, 1.0);
}

// A sublayer is reached the way a `references` asset path is, parent-relative
// segments included - the shape a stack whose shared layers sit beside the
// tree uses.
TEST(Sublayers_asset_paths, a_parent_relative_sublayer_resolves)
{
    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments      load_arguments{
        .path          = test_data_path("sublayer_stack/parent_relative.usda"),
        .root_node     = root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result result = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_TRUE(find_node(result.data, "Shared").operator bool());
    EXPECT_DOUBLE_EQ(result.data.meters_per_unit, 0.01);
}

// A USD-backed scene is edited as the one composed stage it became, so a save
// writes the composed content into one layer and authors no `subLayers`
// (src/erhe/usd/notes.md).
TEST_F(Sublayers_import, a_save_writes_one_layer_and_is_a_fixed_point)
{
    const std::filesystem::path         first_path = temporary_path("sublayers.usda");
    const erhe::usd::Usd_save_arguments save_arguments{
        .path      = first_path,
        .root_node = root
    };
    const erhe::usd::Usd_save_result save_result = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save_result.error.empty()) << save_result.error;

    const std::string written = read_file(first_path);
    EXPECT_EQ(written.find("subLayers"), std::string::npos);
    EXPECT_NE(written.find("\"WeakOnly\""), std::string::npos);
    EXPECT_NE(written.find("\"Shared\""), std::string::npos);

    const std::shared_ptr<erhe::scene::Node> second_root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments      reload_arguments{
        .path          = first_path,
        .root_node     = second_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(reload_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    EXPECT_TRUE(reloaded.data.sublayers.empty());

    const std::filesystem::path         second_path = temporary_path("sublayers_again.usda");
    const erhe::usd::Usd_save_arguments second_save_arguments{
        .path      = second_path,
        .root_node = second_root
    };
    const erhe::usd::Usd_save_result second_save_result = erhe::usd::save_usda(second_save_arguments);
    ASSERT_TRUE(second_save_result.error.empty()) << second_save_result.error;

    EXPECT_EQ(read_file(second_path), written);
}

} // anonymous namespace
