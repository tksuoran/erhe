// The `active` prim metadatum (doc/usd-compatibility-plan.md X2): an
// authored `active = false` lands on the item's `active` property as a
// local value, takes the item and its subtree out through the derived
// Item_flags::active bit, and is written back out as prim metadata.

#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_active_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
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

class Active_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("active.usda"),
            .root_node     = source_root,
            .mesh_layer_id = 0
        };
        source = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("active.usda");
        const erhe::usd::Usd_save_arguments save_arguments{
            .path      = written_path,
            .root_node = source_root,
            .materials = source.data.materials
        };
        save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        const erhe::usd::Usd_load_arguments reload_arguments{
            .path          = written_path,
            .root_node     = reloaded_root,
            .mesh_layer_id = 0
        };
        reloaded = erhe::usd::load_usd(reload_arguments);
        ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    }

    std::shared_ptr<erhe::scene::Node> source_root;
    std::shared_ptr<erhe::scene::Node> reloaded_root;
    std::filesystem::path              written_path;
    erhe::usd::Usd_load_result         source;
    erhe::usd::Usd_save_result         save;
    erhe::usd::Usd_load_result         reloaded;
};

} // namespace

TEST_F(Active_round_trip, an_inactive_prim_still_becomes_an_item)
{
    // Prim::IsActive() says inactive prims are pruned from traversal; the
    // Tydra render-scene conversion the importer walks does not prune them,
    // which is what lets the opinion survive a round trip.
    EXPECT_TRUE(find_node(source.data, "off"  ).operator bool());
    EXPECT_TRUE(find_node(source.data, "below").operator bool());
    EXPECT_TRUE(find_node(source.data, "on"   ).operator bool());
}

TEST_F(Active_round_trip, authored_active_is_a_local_value)
{
    const std::shared_ptr<erhe::scene::Node> off = find_node(source.data, "off");
    ASSERT_TRUE(off.operator bool());
    EXPECT_EQ(off->read_local_value(erhe::Item_base::active_property), std::optional<bool>{false});
    EXPECT_FALSE(off->is_active());
}

TEST_F(Active_round_trip, a_prim_without_the_metadatum_has_no_local_value)
{
    const std::shared_ptr<erhe::scene::Node> on = find_node(source.data, "on");
    ASSERT_TRUE(on.operator bool());
    EXPECT_FALSE(on->read_local_value(erhe::Item_base::active_property).has_value());
    EXPECT_TRUE(on->is_active());
}

TEST_F(Active_round_trip, the_subtree_of_an_inactive_prim_is_inactive)
{
    const std::shared_ptr<erhe::scene::Node> below = find_node(source.data, "below");
    ASSERT_TRUE(below.operator bool());
    EXPECT_FALSE(below->read_local_value(erhe::Item_base::active_property).has_value());
    EXPECT_FALSE(below->is_active());
}

TEST_F(Active_round_trip, active_survives_the_round_trip)
{
    const std::shared_ptr<erhe::scene::Node> off = find_node(reloaded.data, "off");
    ASSERT_TRUE(off.operator bool());
    EXPECT_EQ(off->read_local_value(erhe::Item_base::active_property), std::optional<bool>{false});
    EXPECT_FALSE(off->is_active());

    const std::shared_ptr<erhe::scene::Node> below = find_node(reloaded.data, "below");
    ASSERT_TRUE(below.operator bool());
    EXPECT_FALSE(below->is_active());

    const std::shared_ptr<erhe::scene::Node> on = find_node(reloaded.data, "on");
    ASSERT_TRUE(on.operator bool());
    EXPECT_FALSE(on->read_local_value(erhe::Item_base::active_property).has_value());
    EXPECT_TRUE(on->is_active());
}
