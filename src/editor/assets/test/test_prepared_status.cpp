#include "assets/prepared_status.hpp"
#include "assets/editor_asset.hpp"

#include "erhe_asset_store/asset_store.hpp"
#include "erhe_asset_store/item_kind.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using erhe::asset_store::Asset_store;
using erhe::asset_store::Item_data;
using erhe::asset_store::Item_kind;
using erhe::asset_store::Key;
using erhe::asset_store::Key_builder;
using erhe::asset_store::Open_mode;
using erhe::asset_store::Write_transaction;
using editor::Prepared_drop;
using editor::Prepared_kind_status;

[[nodiscard]] auto make_key(const Item_kind kind, const std::uint64_t index) -> Key
{
    return Key_builder{kind}.add_uint_setting("index", index).finish();
}

class Prepared_status_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_directory = std::filesystem::temp_directory_path() / "editor_prepared_status_tests" / test_info->name();
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory);
        m_store = Asset_store::open(m_directory / "a.erhe_editor", Open_mode::editor_read_write);
        ASSERT_TRUE(m_store);
        // Mesh items 0..3 (no target) of 1000 + i * 100 bytes, Scene items 0..1 of target pc.
        for (std::uint64_t i = 0; i < 4; ++i) {
            put(make_key(Item_kind::local_mesh, i), Item_kind::local_mesh, "", 1000 + (i * 100));
        }
        put(make_key(Item_kind::scene, 0), Item_kind::scene, "pc", 50);
        put(make_key(Item_kind::scene, 1), Item_kind::scene, "pc", 60);
    }

    void TearDown() override
    {
        m_store.reset();
        std::error_code error_code;
        std::filesystem::remove_all(m_directory, error_code);
    }

    void put(const Key& key, const Item_kind kind, const char* const target, const std::size_t size)
    {
        const std::vector<std::byte> payload(size, std::byte{0x5a});
        ASSERT_TRUE(m_store->put(Item_data{.key = key, .kind = kind, .target = target, .meta = "{}", .payload = payload}));
    }

    // The scene asks for mesh items 0, 1 and 9 (absent) and Scene item 1.
    [[nodiscard]] auto make_asked() const -> std::map<Item_kind, std::set<Key>>
    {
        return {
            {Item_kind::local_mesh, {make_key(Item_kind::local_mesh, 0), make_key(Item_kind::local_mesh, 1), make_key(Item_kind::local_mesh, 9)}},
            {Item_kind::scene,      {make_key(Item_kind::scene, 1)}}
        };
    }

    [[nodiscard]] static auto flatten(const std::map<Item_kind, std::set<Key>>& asked) -> std::set<Key>
    {
        std::set<Key> keys;
        for (const auto& [kind, kind_keys] : asked) {
            keys.insert(kind_keys.begin(), kind_keys.end());
        }
        return keys;
    }

    std::filesystem::path        m_directory;
    std::unique_ptr<Asset_store> m_store;
};

TEST_F(Prepared_status_test, measure_counts_present_missing_and_unreferenced)
{
    std::map<Item_kind, Prepared_kind_status> kinds;
    ASSERT_TRUE(editor::measure_store_items(*m_store, make_asked(), kinds));
    const Prepared_kind_status& meshes = kinds[Item_kind::local_mesh];
    EXPECT_EQ(meshes.present, 2u);
    EXPECT_EQ(meshes.missing, 1u);
    EXPECT_EQ(meshes.stored_count, 4u);
    EXPECT_EQ(meshes.stored_bytes, 1000u + 1100u + 1200u + 1300u);
    EXPECT_EQ(meshes.unreferenced_count, 2u);
    EXPECT_EQ(meshes.unreferenced_bytes, 1200u + 1300u);
    const Prepared_kind_status& scenes = kinds[Item_kind::scene];
    EXPECT_EQ(scenes.present, 1u);
    EXPECT_EQ(scenes.missing, 0u);
    EXPECT_EQ(scenes.stored_count, 2u);
    EXPECT_EQ(scenes.unreferenced_count, 1u);
    EXPECT_EQ(scenes.unreferenced_bytes, 50u);
}

// The status measures against a listing of the store (Editor_asset::get_item_catalog()):
// a listing taken before a write does not see it until it is listed again.
TEST_F(Prepared_status_test, catalog_measures_like_the_store_and_follows_invalidation)
{
    const std::shared_ptr<const editor::Item_catalog> catalog = editor::list_item_catalog(*m_store);
    ASSERT_TRUE(catalog);
    EXPECT_EQ(catalog->items.size(), 6u);
    std::map<Item_kind, Prepared_kind_status> from_store;
    std::map<Item_kind, Prepared_kind_status> from_catalog;
    ASSERT_TRUE(editor::measure_store_items(*m_store, make_asked(), from_store));
    editor::measure_catalog_items(*catalog, make_asked(), from_catalog);
    for (const Item_kind kind : {Item_kind::local_mesh, Item_kind::scene}) {
        EXPECT_EQ(from_store[kind].present,            from_catalog[kind].present);
        EXPECT_EQ(from_store[kind].missing,            from_catalog[kind].missing);
        EXPECT_EQ(from_store[kind].stored_count,       from_catalog[kind].stored_count);
        EXPECT_EQ(from_store[kind].stored_bytes,       from_catalog[kind].stored_bytes);
        EXPECT_EQ(from_store[kind].unreferenced_count, from_catalog[kind].unreferenced_count);
        EXPECT_EQ(from_store[kind].unreferenced_bytes, from_catalog[kind].unreferenced_bytes);
    }

    m_store.reset();
    editor::Editor_asset asset{
        m_directory / "a.gltf",
        Asset_store::open(m_directory / "a.erhe_editor", Open_mode::editor_read_write),
        editor::Editor_asset_location::default_location
    };
    const std::shared_ptr<const editor::Item_catalog> first = asset.get_item_catalog();
    ASSERT_TRUE(first);
    EXPECT_EQ(asset.get_item_catalog(), first); // listed once
    const std::vector<std::byte> payload(10, std::byte{0x5a});
    ASSERT_TRUE(asset.get_store().put(Item_data{.key = make_key(Item_kind::local_mesh, 9), .kind = Item_kind::local_mesh, .meta = "{}", .payload = payload}));
    EXPECT_FALSE(asset.get_item_catalog()->items.contains(make_key(Item_kind::local_mesh, 9)));
    asset.invalidate_item_catalog();
    EXPECT_TRUE(asset.get_item_catalog()->items.contains(make_key(Item_kind::local_mesh, 9)));
}

TEST_F(Prepared_status_test, select_drop_keys_per_drop)
{
    const std::set<Key> asked = flatten(make_asked());
    std::vector<Key> keys;
    ASSERT_TRUE(editor::select_drop_keys(*m_store, Prepared_drop::unreferenced_items, asked, "pc", keys));
    EXPECT_EQ(std::set<Key>(keys.begin(), keys.end()), (std::set<Key>{make_key(Item_kind::local_mesh, 2), make_key(Item_kind::local_mesh, 3), make_key(Item_kind::scene, 0)}));
    ASSERT_TRUE(editor::select_drop_keys(*m_store, Prepared_drop::target_items, asked, "pc", keys));
    EXPECT_EQ(std::set<Key>(keys.begin(), keys.end()), (std::set<Key>{make_key(Item_kind::scene, 0), make_key(Item_kind::scene, 1)}));
    ASSERT_TRUE(editor::select_drop_keys(*m_store, Prepared_drop::target_items, asked, "quest", keys));
    EXPECT_TRUE(keys.empty());
    ASSERT_TRUE(editor::select_drop_keys(*m_store, Prepared_drop::all_items, asked, "pc", keys));
    EXPECT_EQ(keys.size(), 6u);
    ASSERT_TRUE(editor::select_drop_keys(*m_store, Prepared_drop::prepared_asset, asked, "pc", keys));
    EXPECT_TRUE(keys.empty());
}

TEST_F(Prepared_status_test, drop_unreferenced_leaves_asked_items_and_shrinks)
{
    // Bigger unreferenced payloads so the VACUUM frees whole pages.
    put(make_key(Item_kind::local_mesh, 5), Item_kind::local_mesh, "", 256 * 1024);
    const std::uint64_t size_before = m_store->get_database_size().value_or(0);
    const std::map<Item_kind, std::set<Key>> asked = make_asked();
    std::vector<Key> keys;
    ASSERT_TRUE(editor::select_drop_keys(*m_store, Prepared_drop::unreferenced_items, flatten(asked), "pc", keys));
    ASSERT_TRUE(editor::drop_store_items(*m_store, keys));
    std::map<Item_kind, Prepared_kind_status> kinds;
    ASSERT_TRUE(editor::measure_store_items(*m_store, asked, kinds));
    EXPECT_EQ(kinds[Item_kind::local_mesh].unreferenced_count, 0u);
    EXPECT_EQ(kinds[Item_kind::scene].unreferenced_count, 0u);
    EXPECT_EQ(kinds[Item_kind::local_mesh].present, 2u);
    EXPECT_EQ(kinds[Item_kind::scene].present, 1u);
    EXPECT_LT(m_store->get_database_size().value_or(0), size_before);
}

TEST_F(Prepared_status_test, compare_prepared_items_needs_exactly_the_asked_items)
{
    const Key mesh_key  = make_key(Item_kind::local_mesh, 0);
    const Key scene_key = make_key(Item_kind::scene, 1);
    std::unique_ptr<Asset_store> prepared = Asset_store::open(m_directory / "a.pc.erhe_prepared", Open_mode::prepared_create);
    ASSERT_TRUE(prepared);
    std::string mismatch;
    EXPECT_FALSE(editor::compare_prepared_items(*prepared, {mesh_key, scene_key}, scene_key, "pc", mismatch));
    EXPECT_FALSE(mismatch.empty());
    {
        Write_transaction transaction{*prepared};
        ASSERT_TRUE(transaction.copy_item_from(*m_store, mesh_key));
        ASSERT_TRUE(transaction.copy_item_from(*m_store, scene_key));
        ASSERT_TRUE(transaction.set_prepared_scene(erhe::asset_store::Prepared_scene_record{.target = "pc", .scene_key = scene_key}));
        ASSERT_TRUE(transaction.commit());
    }
    EXPECT_TRUE(editor::compare_prepared_items(*prepared, {mesh_key, scene_key}, scene_key, "pc", mismatch)) << mismatch;
    EXPECT_FALSE(editor::compare_prepared_items(*prepared, {mesh_key, scene_key}, scene_key, "quest", mismatch));
    EXPECT_FALSE(editor::compare_prepared_items(*prepared, {mesh_key, scene_key, make_key(Item_kind::local_mesh, 1)}, scene_key, "pc", mismatch));
    EXPECT_FALSE(editor::compare_prepared_items(*prepared, {scene_key}, scene_key, "pc", mismatch));
    EXPECT_FALSE(editor::compare_prepared_items(*prepared, {mesh_key, make_key(Item_kind::scene, 0)}, make_key(Item_kind::scene, 0), "pc", mismatch));
}

TEST(Prepared_drop_names, round_trip)
{
    for (const Prepared_drop drop : {Prepared_drop::none, Prepared_drop::unreferenced_items, Prepared_drop::target_items, Prepared_drop::all_items, Prepared_drop::prepared_asset}) {
        Prepared_drop parsed{Prepared_drop::none};
        ASSERT_TRUE(editor::prepared_drop_from_string(editor::c_str(drop), parsed));
        EXPECT_EQ(parsed, drop);
    }
    Prepared_drop parsed{Prepared_drop::none};
    EXPECT_FALSE(editor::prepared_drop_from_string("everything", parsed));
}

}
