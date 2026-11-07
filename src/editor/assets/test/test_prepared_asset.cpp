#include "assets/item_keys.hpp"
#include "assets/prepared_asset.hpp"

#include "erhe_asset_store/asset_store.hpp"
#include "erhe_asset_store/item_kind.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
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

auto make_payload(const std::size_t size) -> std::vector<std::byte>
{
    std::vector<std::byte> payload(size);
    for (std::size_t i = 0; i < size; ++i) {
        payload[i] = static_cast<std::byte>((i * 7u) & 0xffu);
    }
    return payload;
}

auto to_bytes(const std::string& text) -> std::vector<std::byte>
{
    std::vector<std::byte> bytes(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        bytes[i] = static_cast<std::byte>(text[i]);
    }
    return bytes;
}

constexpr const char* c_mesh_stamp_layout = "layout signature";
constexpr const char* c_bvh_backend        = "backend";
constexpr const char* c_bvh_library        = "bvh 1";

auto make_build_formats(const std::string& target) -> editor::Prepared_build_formats
{
    return editor::Prepared_build_formats{
        .target       = target,
        .mesh_stamp   = editor::make_local_mesh_stamp(c_mesh_stamp_layout),
        .bvh_stamp    = editor::make_raytrace_bvh_stamp(c_bvh_backend, c_bvh_library),
        .scene_stamps = {
            editor::make_scene_item_stamp(editor::Scene_item_textures::source),
            editor::make_scene_item_stamp(editor::Scene_item_textures::astc_4x4)
        },
        .texture_stamps = {
            editor::make_texture_stamp(erhe::graphics::Ktx2_block_target::astc_4x4),
            editor::make_source_texture_stamp()
        }
    };
}

enum class Records : unsigned int
{
    with_records = 0,
    without_records
};

// Records written over the stamp records of a written asset, and one left out, in the same transaction.
class Record_edits
{
public:
    std::vector<std::pair<std::string, std::string>> set;
    std::string                                      omit;
};

class Prepared_asset_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_directory = std::filesystem::temp_directory_path() / "editor_prepared_asset_tests" / test_info->name();
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory);
        m_mesh_key  = Key_builder{Item_kind::local_mesh}.add_uint_setting("index", 1).finish();
        m_bvh_key   = editor::make_raytrace_bvh_key(m_mesh_key, editor::make_raytrace_bvh_stamp(c_bvh_backend, c_bvh_library));
        m_payload   = make_payload(1000);
    }

    void TearDown() override
    {
        std::error_code error_code;
        std::filesystem::remove_all(m_directory, error_code);
    }

    // A prepared asset whose Scene item (glTF JSON) names the mesh item and its
    // BVH item and, when store_mesh_item is false, lacks the mesh item.
    void write_prepared_asset(const bool store_mesh_item)
    {
        write_prepared_asset(
            store_mesh_item, "pc",
            editor::make_local_mesh_stamp(c_mesh_stamp_layout),
            editor::make_scene_item_stamp(editor::Scene_item_textures::source)
        );
    }

    // bvh_stamp empty: no BVH item (an Export with raytrace_bvh off). Without_records: a Copy
    // of a build before the stamp records, or one killed between the Scene item and its records.
    void write_prepared_asset(
        const bool         store_mesh_item,
        const std::string& target,
        const std::string& mesh_stamp,
        const std::string& scene_stamp,
        const std::string&  bvh_stamp = editor::make_raytrace_bvh_stamp(c_bvh_backend, c_bvh_library),
        const Records       records   = Records::with_records,
        const Record_edits& edits     = {}
    )
    {
        std::string scene_json =
            "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"" + editor::make_item_uri(m_mesh_key) +
            "\",\"byteLength\":" + std::to_string(m_payload.size()) + "}";
        if (!bvh_stamp.empty()) {
            scene_json += ",{\"uri\":\"" + editor::make_item_uri(m_bvh_key) + "\",\"byteLength\":64}";
        }
        scene_json += "]}";
        m_scene_item = to_bytes(scene_json);
        m_scene_key  = editor::make_scene_item_key(target, scene_stamp, m_scene_item);

        std::filesystem::remove(prepared_path());
        std::unique_ptr<Asset_store> store = Asset_store::open(prepared_path(), Open_mode::prepared_create);
        ASSERT_TRUE(store);
        Write_transaction transaction{*store};
        ASSERT_TRUE(transaction.is_active());
        if (store_mesh_item) {
            ASSERT_TRUE(transaction.put(Item_data{.key = m_mesh_key, .kind = Item_kind::local_mesh, .stamp = mesh_stamp, .payload = m_payload}));
        }
        if (!bvh_stamp.empty()) {
            ASSERT_TRUE(transaction.put(Item_data{.key = m_bvh_key, .kind = Item_kind::raytrace_bvh, .stamp = bvh_stamp, .payload = make_payload(64)}));
        }
        ASSERT_TRUE(transaction.put(Item_data{.key = m_scene_key, .kind = Item_kind::scene, .target = target, .stamp = scene_stamp, .payload = m_scene_item}));
        ASSERT_TRUE(transaction.set_source_record(erhe::asset_store::Source_record{.relative_path = "scene.glb", .scene_id = "id"}));
        ASSERT_TRUE(transaction.set_prepared_scene(erhe::asset_store::Prepared_scene_record{.target = target, .scene_key = m_scene_key}));
        if (records == Records::with_records) {
            const std::vector<std::pair<std::string_view, std::string>> values{
                {editor::c_prepared_mesh_stamp_record,     mesh_stamp},
                {editor::c_prepared_bvh_stamp_record,      bvh_stamp.empty() ? std::string{editor::c_prepared_no_bvh_stamp} : bvh_stamp},
                {editor::c_prepared_scene_stamp_record,    scene_stamp},
                {editor::c_prepared_texture_stamps_record, std::string{}}
            };
            for (const auto& [name, value] : values) {
                if (name != edits.omit) {
                    ASSERT_TRUE(transaction.set_record(name, value));
                }
            }
            for (const auto& [name, value] : edits.set) {
                ASSERT_TRUE(transaction.set_record(name, value));
            }
        }
        ASSERT_TRUE(transaction.commit());
    }

    // The default asset with record edits (a tampered or foreign copy).
    void write_prepared_asset(const Record_edits& edits)
    {
        write_prepared_asset(
            true, "pc",
            editor::make_local_mesh_stamp(c_mesh_stamp_layout),
            editor::make_scene_item_stamp(editor::Scene_item_textures::source),
            editor::make_raytrace_bvh_stamp(c_bvh_backend, c_bvh_library),
            Records::with_records,
            edits
        );
    }

    [[nodiscard]] auto prepared_path() const -> std::filesystem::path { return m_directory / "scene.pc.erhe_prepared"; }
    [[nodiscard]] auto source_path  () const -> std::filesystem::path { return m_directory / "scene.glb"; }

    std::filesystem::path  m_directory;
    Key                    m_mesh_key;
    Key                    m_bvh_key;
    Key                    m_scene_key;
    std::vector<std::byte> m_payload;
    std::vector<std::byte> m_scene_item;
};

TEST(Prepared_asset_path, extension)
{
    EXPECT_TRUE (editor::is_prepared_asset_path("a/scene.pc.erhe_prepared"));
    EXPECT_FALSE(editor::is_prepared_asset_path("a/scene.glb"));
    EXPECT_FALSE(editor::is_prepared_asset_path("a/scene.erhe_editor"));
}

TEST_F(Prepared_asset_test, source_readable_opens_the_source)
{
    write_prepared_asset(true);
    {
        std::ofstream source{source_path(), std::ios::binary};
        source << "glTF";
    }
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    EXPECT_EQ(open.kind, editor::Prepared_open_kind::open_source);
    EXPECT_EQ(open.source_path, source_path().lexically_normal());
    EXPECT_FALSE(open.prepared_asset);
}

TEST_F(Prepared_asset_test, scene_name_follows_the_open)
{
    const auto scene_name = [this](const std::filesystem::path& path) -> std::string {
        return editor::get_prepared_open_scene_name(path, editor::resolve_prepared_open(path, make_build_formats("pc")));
    };
    write_prepared_asset(true);
    EXPECT_EQ(scene_name(prepared_path()), "scene.pc");
    {
        std::ofstream source{source_path(), std::ios::binary};
        source << "glTF";
    }
    EXPECT_EQ(scene_name(prepared_path()), "scene");
    EXPECT_EQ(scene_name(m_directory / "missing.pc.erhe_prepared"), "missing.pc");
}

TEST_F(Prepared_asset_test, source_missing_is_a_prepared_run)
{
    write_prepared_asset(true);
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    ASSERT_EQ(open.kind, editor::Prepared_open_kind::prepared_run);
    ASSERT_TRUE(open.prepared_asset);
    EXPECT_EQ(open.prepared_asset->get_target(), "pc");
    EXPECT_EQ(open.scene_key, m_scene_key);
    EXPECT_EQ(open.scene_item_size, m_scene_item.size());
    // The parse reads the Scene item through its reader.
    const std::shared_ptr<const erhe::gltf::Gltf_buffer_reader> reader = editor::make_scene_item_reader(open);
    ASSERT_TRUE(reader);
    std::vector<std::byte> bytes(m_scene_item.size());
    EXPECT_TRUE(reader->read_at(0, bytes));
    EXPECT_EQ(bytes, m_scene_item);
}

TEST_F(Prepared_asset_test, resolver_reads_item_ranges)
{
    write_prepared_asset(true);
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    ASSERT_EQ(open.kind, editor::Prepared_open_kind::prepared_run);
    const erhe::gltf::Gltf_buffer_resolver resolver = editor::make_prepared_buffer_resolver(open.prepared_asset);

    const std::shared_ptr<const erhe::gltf::Gltf_buffer_reader> reader = resolver(editor::make_item_uri(m_mesh_key));
    ASSERT_TRUE(reader);
    std::vector<std::byte> whole(m_payload.size());
    ASSERT_TRUE(reader->read_at(0, whole));
    EXPECT_EQ(whole, m_payload);
    std::array<std::byte, 100> range{};
    ASSERT_TRUE(reader->read_at(450, range));
    for (std::size_t i = 0; i < range.size(); ++i) {
        EXPECT_EQ(range[i], m_payload[450 + i]);
    }
    std::array<std::byte, 16> past_end{};
    EXPECT_FALSE(reader->read_at(m_payload.size() - 8, past_end));

    const Key unknown_key = Key_builder{Item_kind::local_mesh}.add_uint_setting("index", 2).finish();
    EXPECT_FALSE(resolver(editor::make_item_uri(unknown_key)));
    EXPECT_FALSE(resolver("scene.bin"));
    EXPECT_FALSE(resolver("erhe-item:not-a-key"));
}

TEST_F(Prepared_asset_test, reader_keeps_the_store_open)
{
    write_prepared_asset(true);
    std::shared_ptr<const erhe::gltf::Gltf_buffer_reader> reader;
    {
        editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
        ASSERT_EQ(open.kind, editor::Prepared_open_kind::prepared_run);
        reader = editor::make_prepared_buffer_resolver(open.prepared_asset)(editor::make_item_uri(m_mesh_key));
    }
    ASSERT_TRUE(reader);
    std::vector<std::byte> whole(m_payload.size());
    ASSERT_TRUE(reader->read_at(0, whole));
    EXPECT_EQ(whole, m_payload);
}

// The open reads the records, not the items: an item missing after Copy (a changed asset) fails at its read.
TEST_F(Prepared_asset_test, missing_mesh_item_resolves_to_no_reader)
{
    write_prepared_asset(false);
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    ASSERT_EQ(open.kind, editor::Prepared_open_kind::prepared_run);
    const erhe::gltf::Gltf_buffer_resolver resolver = editor::make_prepared_buffer_resolver(open.prepared_asset);
    EXPECT_FALSE(resolver(editor::make_item_uri(m_mesh_key)));
    EXPECT_TRUE(resolver(editor::make_item_uri(m_bvh_key)));
}

TEST_F(Prepared_asset_test, missing_file_fails)
{
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    EXPECT_EQ(open.kind, editor::Prepared_open_kind::failed);
}

TEST(Prepared_format_check, stamp_forms)
{
    const std::string mesh_stamp = editor::make_local_mesh_stamp("signature");
    EXPECT_TRUE(mesh_stamp.starts_with("layout="));
    EXPECT_EQ(mesh_stamp.size(), std::string{"layout="}.size() + 32);
    EXPECT_EQ(mesh_stamp.find("bvh"), std::string::npos);
    EXPECT_EQ(editor::make_raytrace_bvh_stamp(c_bvh_backend, "bvh 2"), "bvh=backend/bvh 2");
}

TEST(Prepared_format_check, scene_key_follows_target_and_stamp)
{
    const std::vector<std::byte> bytes = to_bytes("scene");
    const std::string source = editor::make_scene_item_stamp(editor::Scene_item_textures::source);
    const std::string astc   = editor::make_scene_item_stamp(editor::Scene_item_textures::astc_4x4);
    EXPECT_EQ(source, "glb;textures=source");
    EXPECT_EQ(editor::make_scene_item_key("pc", source, bytes), editor::make_scene_item_key("pc", source, bytes));
    EXPECT_NE(editor::make_scene_item_key("pc", source, bytes), editor::make_scene_item_key("quest", source, bytes));
    EXPECT_NE(editor::make_scene_item_key("quest", source, bytes), editor::make_scene_item_key("quest", astc, bytes));
}

auto make_records(const editor::Prepared_build_formats& build) -> editor::Prepared_stamp_records
{
    return editor::Prepared_stamp_records{
        .mesh_stamp     = build.mesh_stamp,
        .bvh_stamp      = build.bvh_stamp,
        .scene_stamp    = build.scene_stamps.front(),
        .texture_stamps = {}
    };
}

// R11.4, R11.6: Texture items of a block format the build neither samples nor decodes fail the open.
TEST(Prepared_format_check, texture_stamp_of_an_unread_block_format_fails_the_open)
{
    const editor::Prepared_build_formats build = make_build_formats("quest");
    editor::Prepared_stamp_records records = make_records(build);
    records.texture_stamps = {
        editor::make_texture_stamp(erhe::graphics::Ktx2_block_target::astc_4x4),
        editor::make_texture_stamp(erhe::graphics::Ktx2_block_target::bc)
    };
    const editor::Prepared_format_check check = editor::check_prepared_formats("a", "pc", records, build);
    ASSERT_EQ(check.mismatches.size(), 1u);
    EXPECT_EQ(check.mismatches.front().kind, "texture");
    EXPECT_EQ(check.mismatches.front().stored_stamp, "ktx2;blocks=bc");
    EXPECT_EQ(check.mismatches.front().effect, editor::Prepared_stamp_effect::open_fails);
    EXPECT_NE(check.error.find("the texture items"), std::string::npos);
    EXPECT_NE(check.error.find("of target pc"), std::string::npos);

    // A build that samples BC reads both.
    editor::Prepared_build_formats bc_build = build;
    bc_build.texture_stamps.push_back(editor::make_texture_stamp(erhe::graphics::Ktx2_block_target::bc));
    EXPECT_TRUE(editor::check_prepared_formats("a", "pc", records, bc_build).error.empty());
}

TEST(Prepared_format_check, one_line_per_differing_record)
{
    const editor::Prepared_build_formats build = make_build_formats("pc");
    editor::Prepared_stamp_records records = make_records(build);
    records.mesh_stamp = editor::make_local_mesh_stamp("other layout");
    records.bvh_stamp  = editor::make_raytrace_bvh_stamp(c_bvh_backend, "bvh 0");
    const editor::Prepared_format_check check = editor::check_prepared_formats("a", "pc", records, build);
    ASSERT_EQ(check.mismatches.size(), 2u);
    EXPECT_EQ(check.mismatches[0].kind, "local_mesh");
    EXPECT_EQ(check.mismatches[0].effect, editor::Prepared_stamp_effect::open_fails);
    EXPECT_EQ(check.mismatches[0].stored_stamp, records.mesh_stamp);
    EXPECT_EQ(check.mismatches[0].expected_stamp, build.mesh_stamp);
    EXPECT_EQ(check.mismatches[1].kind, "raytrace_bvh");
    EXPECT_EQ(check.mismatches[1].effect, editor::Prepared_stamp_effect::rebuilt_in_memory);
    EXPECT_EQ(check.mismatches[1].stored_stamp,   "bvh=backend/bvh 0");
    EXPECT_EQ(check.mismatches[1].expected_stamp, "bvh=backend/bvh 1");
    EXPECT_EQ(check.notices.size(), 2u);
    EXPECT_NE(check.error.find("the local_mesh and world_mesh items"), std::string::npos);
    EXPECT_NE(check.error.find(records.mesh_stamp), std::string::npos);
    EXPECT_NE(check.error.find(build.mesh_stamp), std::string::npos);
    EXPECT_EQ(check.error.find("bvh=backend/bvh 0"), std::string::npos);
    EXPECT_FALSE(check.exported_without_bvh);
    EXPECT_EQ(editor::get_stored_bvh_use(check), erhe::gltf::Stored_bvh_use::ignore);
}

TEST(Prepared_format_check, no_bvh_stamp_is_noticed)
{
    const editor::Prepared_build_formats build = make_build_formats("pc");
    editor::Prepared_stamp_records records = make_records(build);
    records.bvh_stamp = std::string{editor::c_prepared_no_bvh_stamp};
    const editor::Prepared_format_check check = editor::check_prepared_formats("a", "pc", records, build);
    EXPECT_TRUE(check.mismatches.empty());
    EXPECT_TRUE(check.error.empty());
    EXPECT_TRUE(check.exported_without_bvh);
    ASSERT_EQ(check.notices.size(), 1u);
    EXPECT_NE(check.notices.front().find("raytrace_bvh off"), std::string::npos);
    EXPECT_NE(check.notices.front().find("built in memory"), std::string::npos);
    EXPECT_EQ(editor::get_stored_bvh_use(check), erhe::gltf::Stored_bvh_use::use);
}

TEST(Prepared_format_check, matching_stamps_pass)
{
    const editor::Prepared_build_formats build = make_build_formats("quest");
    editor::Prepared_stamp_records records = make_records(build);
    records.scene_stamp    = build.scene_stamps.back();
    records.texture_stamps = build.texture_stamps;
    const editor::Prepared_format_check check = editor::check_prepared_formats("a", "quest", records, build);
    EXPECT_TRUE(check.mismatches.empty());
    EXPECT_TRUE(check.notices.empty());
    EXPECT_TRUE(check.error.empty());
    EXPECT_EQ(editor::get_stored_bvh_use(check), erhe::gltf::Stored_bvh_use::use);
}

TEST_F(Prepared_asset_test, mesh_layout_mismatch_fails_the_open)
{
    const std::string stored = editor::make_local_mesh_stamp("other layout");
    write_prepared_asset(true, "pc", stored, editor::make_scene_item_stamp(editor::Scene_item_textures::source));
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    EXPECT_EQ(open.kind, editor::Prepared_open_kind::failed);
    EXPECT_FALSE(open.prepared_asset);
    EXPECT_NE(open.error.find("the local_mesh and world_mesh items"), std::string::npos);
    EXPECT_NE(open.error.find(stored), std::string::npos);
    EXPECT_NE(open.error.find(make_build_formats("pc").mesh_stamp), std::string::npos);
}

TEST_F(Prepared_asset_test, bvh_mismatch_runs_with_a_notice)
{
    write_prepared_asset(
        true, "pc",
        editor::make_local_mesh_stamp(c_mesh_stamp_layout),
        editor::make_scene_item_stamp(editor::Scene_item_textures::source),
        editor::make_raytrace_bvh_stamp(c_bvh_backend, "bvh 0")
    );
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    ASSERT_EQ(open.kind, editor::Prepared_open_kind::prepared_run);
    ASSERT_EQ(open.format_check.mismatches.size(), 1u);
    EXPECT_EQ(open.format_check.mismatches.front().kind, "raytrace_bvh");
    EXPECT_EQ(open.format_check.mismatches.front().stored_stamp, "bvh=backend/bvh 0");
    EXPECT_EQ(open.format_check.mismatches.front().effect, editor::Prepared_stamp_effect::rebuilt_in_memory);
    EXPECT_TRUE(open.format_check.error.empty());
    EXPECT_EQ(editor::get_stored_bvh_use(open.format_check), erhe::gltf::Stored_bvh_use::ignore);
}

TEST_F(Prepared_asset_test, asset_without_bvh_items_runs_with_a_notice)
{
    write_prepared_asset(
        true, "pc",
        editor::make_local_mesh_stamp(c_mesh_stamp_layout),
        editor::make_scene_item_stamp(editor::Scene_item_textures::source),
        std::string{}
    );
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    ASSERT_EQ(open.kind, editor::Prepared_open_kind::prepared_run);
    EXPECT_TRUE(open.format_check.mismatches.empty());
    EXPECT_TRUE(open.format_check.exported_without_bvh);
    EXPECT_EQ(editor::get_stored_bvh_use(open.format_check), erhe::gltf::Stored_bvh_use::use);
}

TEST_F(Prepared_asset_test, unread_scene_stamp_fails_naming_the_target)
{
    write_prepared_asset(
        true, "pc",
        editor::make_local_mesh_stamp(c_mesh_stamp_layout),
        editor::make_scene_item_stamp(editor::Scene_item_textures::bc)
    );
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("quest"));
    EXPECT_EQ(open.kind, editor::Prepared_open_kind::failed);
    EXPECT_NE(open.error.find("of target pc"), std::string::npos);
    EXPECT_NE(open.error.find("glb;textures=bc"), std::string::npos);
}

TEST_F(Prepared_asset_test, other_target_runs_with_a_notice)
{
    write_prepared_asset(
        true, "quest",
        editor::make_local_mesh_stamp(c_mesh_stamp_layout),
        editor::make_scene_item_stamp(editor::Scene_item_textures::astc_4x4)
    );
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    ASSERT_EQ(open.kind, editor::Prepared_open_kind::prepared_run);
    EXPECT_EQ(open.prepared_asset->get_target(), "quest");
    EXPECT_TRUE(open.format_check.mismatches.empty());
    ASSERT_EQ(open.format_check.notices.size(), 1u);
    EXPECT_NE(open.format_check.notices.front().find("target quest"), std::string::npos);
}

TEST_F(Prepared_asset_test, failed_stamp_check_keeps_nothing_open)
{
    const std::string stored = editor::make_local_mesh_stamp("other layout");
    write_prepared_asset(true, "pc", stored, editor::make_scene_item_stamp(editor::Scene_item_textures::source));
    const editor::Prepared_open failed = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    EXPECT_EQ(failed.kind, editor::Prepared_open_kind::failed);
    EXPECT_NE(failed.error.find(stored), std::string::npos);
    EXPECT_FALSE(failed.prepared_asset);
    EXPECT_FALSE(editor::make_scene_item_reader(failed));
}


// The records decide, not the items: a mesh layout record that differs refuses although the items match.
TEST_F(Prepared_asset_test, mesh_stamp_record_mismatch_refuses)
{
    const std::string recorded = editor::make_local_mesh_stamp("other layout");
    write_prepared_asset(Record_edits{.set = {{std::string{editor::c_prepared_mesh_stamp_record}, recorded}}, .omit = {}});
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    EXPECT_EQ(open.kind, editor::Prepared_open_kind::failed);
    EXPECT_FALSE(open.prepared_asset);
    EXPECT_NE(open.error.find(recorded), std::string::npos);
    EXPECT_NE(open.error.find(make_build_formats("pc").mesh_stamp), std::string::npos);
    EXPECT_NE(open.error.find("recorded at Export"), std::string::npos);
}

TEST_F(Prepared_asset_test, bvh_stamp_record_mismatch_marks_rebuild)
{
    write_prepared_asset(Record_edits{.set = {{std::string{editor::c_prepared_bvh_stamp_record}, editor::make_raytrace_bvh_stamp(c_bvh_backend, "bvh 0")}}, .omit = {}});
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    ASSERT_EQ(open.kind, editor::Prepared_open_kind::prepared_run);
    ASSERT_EQ(open.format_check.mismatches.size(), 1u);
    EXPECT_EQ(open.format_check.mismatches.front().effect, editor::Prepared_stamp_effect::rebuilt_in_memory);
    EXPECT_EQ(editor::get_stored_bvh_use(open.format_check), erhe::gltf::Stored_bvh_use::ignore);
}

TEST_F(Prepared_asset_test, unread_texture_stamp_record_refuses)
{
    write_prepared_asset(Record_edits{.set = {{std::string{editor::c_prepared_texture_stamps_record}, "source | ktx2;blocks=etc2"}}, .omit = {}});
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    EXPECT_EQ(open.kind, editor::Prepared_open_kind::failed);
    EXPECT_NE(open.error.find("ktx2;blocks=etc2"), std::string::npos);
    EXPECT_EQ(open.error.find("'source'"), std::string::npos);
}

// An asset made before the records (or a Copy that never committed them) is refused, naming the record.
TEST_F(Prepared_asset_test, missing_record_refuses_naming_it)
{
    write_prepared_asset(
        true, "pc",
        editor::make_local_mesh_stamp(c_mesh_stamp_layout),
        editor::make_scene_item_stamp(editor::Scene_item_textures::source),
        editor::make_raytrace_bvh_stamp(c_bvh_backend, c_bvh_library),
        Records::without_records
    );
    const editor::Prepared_open open = editor::resolve_prepared_open(prepared_path(), make_build_formats("pc"));
    EXPECT_EQ(open.kind, editor::Prepared_open_kind::failed);
    EXPECT_FALSE(open.prepared_asset);
    EXPECT_NE(open.error.find(std::string{editor::c_prepared_mesh_stamp_record}), std::string::npos);
    EXPECT_NE(open.error.find("export it again"), std::string::npos);

    for (const std::string_view name : {
        editor::c_prepared_bvh_stamp_record, editor::c_prepared_scene_stamp_record, editor::c_prepared_texture_stamps_record
    }) {
        write_prepared_asset(Record_edits{.set = {}, .omit = std::string{name}});
        const editor::Prepared_build_formats build = make_build_formats("pc");
        const editor::Prepared_open missing = editor::resolve_prepared_open(prepared_path(), build);
        EXPECT_EQ(missing.kind, editor::Prepared_open_kind::failed) << name;
        EXPECT_NE(missing.error.find(std::string{name}), std::string::npos) << missing.error;
    }
}


// Copy's write (R9.3): a new container renamed over the target; the target is untouched on a failure.
class Prepared_write_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_directory = std::filesystem::temp_directory_path() / "editor_prepared_write_tests" / test_info->name();
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory);
        m_editor = Asset_store::open(m_directory / "scene.erhe_editor", Open_mode::editor_read_write);
        ASSERT_TRUE(m_editor);
        m_mesh_key  = Key_builder{Item_kind::local_mesh}.add_uint_setting("index", 1).finish();
        m_scene_key = Key_builder{Item_kind::scene}.add_uint_setting("index", 2).finish();
        ASSERT_TRUE(m_editor->put(Item_data{.key = m_mesh_key,  .kind = Item_kind::local_mesh, .stamp = "layout=a", .payload = make_payload(3000)}));
        ASSERT_TRUE(m_editor->put(Item_data{.key = m_scene_key, .kind = Item_kind::scene, .target = "pc", .stamp = "glb;textures=source", .payload = make_payload(500)}));
    }

    void TearDown() override
    {
        m_editor.reset();
        std::error_code error_code;
        std::filesystem::remove_all(m_directory, error_code);
    }

    [[nodiscard]] auto target() const -> std::filesystem::path { return m_directory / "scene.pc.erhe_prepared"; }

    [[nodiscard]] auto make_request() const -> editor::Prepared_write_request
    {
        return editor::Prepared_write_request{
            .prepared_asset_path = target(),
            .items               = {
                {.key = m_mesh_key,  .kind = Item_kind::local_mesh, .store = m_editor.get()},
                {.key = m_scene_key, .kind = Item_kind::scene,      .store = m_editor.get()}
            },
            .source              = erhe::asset_store::Source_record{.relative_path = "scene.glb", .scene_id = "id"},
            .scene               = erhe::asset_store::Prepared_scene_record{.target = "pc", .scene_key = m_scene_key},
            .stamps              = editor::Prepared_stamp_records{.mesh_stamp = "layout=a", .bvh_stamp = "none", .scene_stamp = "glb;textures=source", .texture_stamps = {}},
            .advance             = {}
        };
    }

    void write_old_target() const
    {
        std::ofstream file{target(), std::ios::binary};
        file << "the previous prepared asset";
    }

    [[nodiscard]] auto read_target() const -> std::string
    {
        std::ifstream file{target(), std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

    [[nodiscard]] auto temporaries() const -> std::size_t
    {
        std::size_t count = 0;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{m_directory}) {
            count += (entry.path().filename().string().find(".tmp-") != std::string::npos) ? 1u : 0u;
        }
        return count;
    }

    std::filesystem::path        m_directory;
    std::unique_ptr<Asset_store> m_editor;
    Key                          m_mesh_key;
    Key                          m_scene_key;
};

TEST_F(Prepared_write_test, temporary_path_is_beside_the_target)
{
    const std::filesystem::path temporary = editor::make_prepared_temporary_path(target());
    EXPECT_EQ(temporary.parent_path(), target().parent_path());
    EXPECT_TRUE(temporary.filename().string().starts_with("scene.pc.erhe_prepared.tmp-"));
    EXPECT_EQ(temporary.filename().string().size(), std::string{"scene.pc.erhe_prepared.tmp-"}.size() + 16);
    EXPECT_NE(temporary, editor::make_prepared_temporary_path(target()));
}

TEST_F(Prepared_write_test, replaces_the_target_with_a_new_container)
{
    write_old_target();
    const editor::Prepared_write_result result = editor::write_prepared_asset(make_request());
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.written.at(Item_kind::local_mesh), 1u);
    EXPECT_EQ(result.written.at(Item_kind::scene), 1u);
    EXPECT_EQ(result.item_count, 2u);
    EXPECT_EQ(temporaries(), 0u);

    const editor::Prepared_open open = editor::resolve_prepared_open(target(), make_build_formats("pc"));
    EXPECT_EQ(open.kind, editor::Prepared_open_kind::failed); // the records name stamps of no real build
    std::unique_ptr<Asset_store> store = Asset_store::open(target(), Open_mode::prepared_read_only);
    ASSERT_TRUE(store);
    EXPECT_TRUE(store->contains(m_mesh_key));
    EXPECT_TRUE(store->contains(m_scene_key));
    EXPECT_EQ(store->get_prepared_scene()->scene_key, m_scene_key);
    EXPECT_EQ(store->get_record(editor::c_prepared_bvh_stamp_record), std::optional<std::string>{"none"});
    EXPECT_EQ(store->get_record(editor::c_prepared_texture_stamps_record), std::optional<std::string>{""});
}

TEST_F(Prepared_write_test, missing_item_fails_naming_it_and_leaves_the_target)
{
    write_old_target();
    editor::Prepared_write_request request = make_request();
    const Key missing = Key_builder{Item_kind::raytrace_bvh}.add_uint_setting("index", 3).finish();
    request.items.push_back({.key = missing, .kind = Item_kind::raytrace_bvh, .store = m_editor.get()});
    const editor::Prepared_write_result result = editor::write_prepared_asset(request);
    EXPECT_NE(result.error.find(missing.to_hex()), std::string::npos) << result.error;
    EXPECT_NE(result.error.find("(raytrace_bvh)"), std::string::npos) << result.error;
    EXPECT_TRUE(result.written.empty());
    EXPECT_EQ(read_target(), "the previous prepared asset");
    EXPECT_EQ(temporaries(), 0u);
}

TEST_F(Prepared_write_test, stale_temporaries_are_removed)
{
    for (const char* const name : {"scene.pc.erhe_prepared.tmp-0123456789abcdef", "scene.pc.erhe_prepared.tmp-0123456789abcdef-journal", "other.pc.erhe_prepared.tmp-1"}) {
        std::ofstream file{m_directory / name, std::ios::binary};
        file << "left by a killed Export";
    }
    const editor::Prepared_write_result result = editor::write_prepared_asset(make_request());
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(temporaries(), 1u);
    EXPECT_TRUE(std::filesystem::exists(m_directory / "other.pc.erhe_prepared.tmp-1"));
}

}
