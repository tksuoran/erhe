#include "assets/asset_paths.hpp"
#include "assets/editor_asset.hpp"

#include "erhe_asset_store/asset_store.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

using editor::Editor_asset;
using editor::Editor_asset_location;
using editor::Editor_asset_registry;

// A fresh directory per test, removed afterwards.
class Editor_asset_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = editor::normalize_asset_path(std::filesystem::temp_directory_path() / ("editor_asset_test_" + std::to_string(tick)));
        std::filesystem::create_directories(m_root / "scenes");
        std::filesystem::create_directories(m_root / "elsewhere");
        m_source = m_root / "scenes" / "host.glb";
        std::ofstream{m_source} << "glTF";
    }
    void TearDown() override
    {
        std::error_code error_code;
        std::filesystem::remove_all(m_root, error_code);
    }

    std::filesystem::path m_root;
    std::filesystem::path m_source;
};

TEST_F(Editor_asset_test, fallback_name_is_from_the_source_path)
{
    const std::filesystem::path cache{"cache/editor_assets"};
    const std::filesystem::path a = editor::get_fallback_editor_asset_path(m_source, cache);
    EXPECT_EQ(a, editor::get_fallback_editor_asset_path(m_source, cache));
    EXPECT_EQ(a.parent_path(), cache);
    EXPECT_EQ(a.extension(), std::filesystem::path{".erhe_editor"});
    EXPECT_EQ(a.filename().string().rfind("host-", 0), 0u);
    // The same stem in another directory names another fallback.
    EXPECT_NE(a, editor::get_fallback_editor_asset_path(m_root / "elsewhere" / "host.glb", cache));
}

TEST_F(Editor_asset_test, default_location_beside_the_source)
{
    Editor_asset_registry registry{m_root / "cache"};
    const std::shared_ptr<Editor_asset> asset = registry.get(m_source, {});
    ASSERT_TRUE(asset);
    EXPECT_EQ(asset->get_location(), Editor_asset_location::default_location);
    EXPECT_EQ(asset->get_asset_path(), m_root / "scenes" / "host.erhe_editor");
    EXPECT_EQ(registry.get(m_source, {}), asset);
    EXPECT_EQ(registry.find(m_source), asset);
    EXPECT_FALSE(registry.find(m_root / "elsewhere" / "other.glb"));
}

TEST_F(Editor_asset_test, recorded_location_comes_first)
{
    Editor_asset_registry registry{m_root / "cache"};
    const std::filesystem::path record = m_root / "elsewhere" / "kept.erhe_editor";
    const std::shared_ptr<Editor_asset> asset = registry.get(m_source, record);
    ASSERT_TRUE(asset);
    EXPECT_EQ(asset->get_location(), Editor_asset_location::record);
    EXPECT_EQ(asset->get_asset_path(), record);
    EXPECT_EQ(editor::make_editor_asset_record(&registry, m_source, m_source, {}), "../elsewhere/kept.erhe_editor");
}

TEST_F(Editor_asset_test, unwritable_location_falls_back_then_fails)
{
    // A record inside a directory that does not exist cannot be created.
    const std::filesystem::path unwritable = m_root / "missing" / "host.erhe_editor";
    {
        Editor_asset_registry registry{m_root / "cache"};
        const std::shared_ptr<Editor_asset> asset = registry.get(m_source, unwritable);
        ASSERT_TRUE(asset);
        EXPECT_EQ(asset->get_location(), Editor_asset_location::fallback);
        EXPECT_EQ(asset->get_asset_path(), editor::get_fallback_editor_asset_path(m_source, m_root / "cache"));
        // A fallback editor asset is never recorded (R2.6): the default name is.
        EXPECT_EQ(editor::make_editor_asset_record(&registry, m_source, m_source, unwritable), "host.erhe_editor");
    }
    // A cache directory below a regular file cannot be made either.
    Editor_asset_registry registry{m_source / "cache"};
    EXPECT_FALSE(registry.get(m_source, unwritable));
    const std::string message = registry.describe_unwritable(m_source, unwritable);
    EXPECT_NE(message.find("host.glb"), std::string::npos);
    EXPECT_NE(message.find("missing"), std::string::npos);
    EXPECT_NE(message.find("nor at the fallback"), std::string::npos);
}

TEST_F(Editor_asset_test, record_of_a_save)
{
    Editor_asset_registry registry{m_root / "cache"};
    // Not open: the scene's own record while it saves onto its source, else the default name.
    const std::filesystem::path record = m_root / "elsewhere" / "kept.erhe_editor";
    EXPECT_EQ(editor::make_editor_asset_record(&registry, m_source, m_source, record), "../elsewhere/kept.erhe_editor");
    EXPECT_EQ(editor::make_editor_asset_record(&registry, m_root / "elsewhere" / "copy.glb", m_source, record), "copy.erhe_editor");
    EXPECT_EQ(editor::make_editor_asset_record(nullptr, m_source, m_source, {}), "host.erhe_editor");
    // Open at its default location: that path.
    ASSERT_TRUE(registry.get(m_source, {}));
    EXPECT_EQ(editor::make_editor_asset_record(&registry, m_source, m_source, record), "host.erhe_editor");
}

} // namespace
