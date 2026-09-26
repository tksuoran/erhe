// `UsdGeomModelAPI` draw modes (doc/erhe/usd_compatibility.md, "Draw modes"): a
// prim that applies the schema becomes one `Usd_draw_mode` record of the
// authored attributes, a prim that authors a `model:` attribute without the
// schema becomes one too, the attributes a variant block authors are carried
// as `Draw_mode.<property>` opinions of that block rather than counted as
// opinions erhe has no place for, and the writer authors exactly the record's
// authored values back, as a fixed point.

#include "test_temporary_directory.hpp"

#include "erhe_scene/draw_mode_description.hpp"
#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = erhe_usd_test::process_temporary_directory() / "erhe_usd_draw_mode_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_text(const std::filesystem::path& path) -> std::string
{
    std::ifstream     stream{path, std::ios::binary};
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] auto find_draw_mode(
    const erhe::usd::Usd_data& data,
    const std::string&         stage_path
) -> const erhe::usd::Usd_draw_mode*
{
    for (const erhe::usd::Usd_draw_mode& record : data.draw_modes) {
        if (record.stage_path == stage_path) {
            return &record;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_variant_set(
    const erhe::usd::Usd_data& data,
    const std::string&         set_name
) -> const erhe::usd::Usd_variant_set*
{
    for (const erhe::usd::Usd_variant_set& set : data.variant_sets) {
        if (set.set_name == set_name) {
            return &set;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_variant(
    const erhe::usd::Usd_variant_set& set,
    const std::string&                variant_name
) -> const erhe::usd::Usd_variant*
{
    for (const erhe::usd::Usd_variant& variant : set.variants) {
        if (variant.name == variant_name) {
            return &variant;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_value(
    const erhe::usd::Usd_variant& variant,
    const std::string&            name
) -> const erhe::scene::Instance_override_value*
{
    for (const erhe::scene::Instance_override& entry : variant.overrides) {
        for (const erhe::scene::Instance_override_value& value : entry.values) {
            if (value.name == name) {
                return &value;
            }
        }
    }
    return nullptr;
}

// Every draw-mode record of a load, as the writer's argument list: the editor
// fills this from the draw-mode values it wrote, and the test from what it read.
[[nodiscard]] auto to_save_draw_modes(const erhe::usd::Usd_data& data) -> std::vector<erhe::usd::Usd_save_draw_mode>
{
    std::vector<erhe::usd::Usd_save_draw_mode> result;
    for (const erhe::usd::Usd_draw_mode& record : data.draw_modes) {
        if (record.prim) {
            result.push_back(erhe::usd::Usd_save_draw_mode{.item = record.prim, .description = record.description});
        }
    }
    return result;
}

// The variant sets of a load as the writer's argument list, which the editor
// fills from its own variant table: the fixture's blocks hold property
// opinions alone, so the name, the nesting, the selection and the overrides
// are all of it.
[[nodiscard]] auto to_save_variant_sets(const erhe::usd::Usd_data& data) -> std::vector<erhe::usd::Usd_save_variant_set>
{
    std::vector<erhe::usd::Usd_save_variant_set> result;
    for (const erhe::usd::Usd_variant_set& set : data.variant_sets) {
        erhe::usd::Usd_save_variant_set entry{};
        entry.item                   = set.prim;
        entry.set_name               = set.set_name;
        entry.enclosing_set_name     = set.enclosing_set_name;
        entry.enclosing_variant_name = set.enclosing_variant_name;
        entry.selected               = set.selected;
        for (const erhe::usd::Usd_variant& variant : set.variants) {
            erhe::usd::Usd_save_variant save_variant{};
            save_variant.name      = variant.name;
            save_variant.overrides = variant.overrides;
            entry.variants.push_back(std::move(save_variant));
        }
        result.push_back(std::move(entry));
    }
    return result;
}

class Draw_mode_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("draw_modes.usda"),
            .root_node     = source_root,
            .mesh_layer_id = 0
        };
        source = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("draw_modes.usda");
        const erhe::usd::Usd_save_arguments save_arguments{
            .path          = written_path,
            .root_node     = source_root,
            .materials     = source.data.materials,
            .variant_sets  = to_save_variant_sets(source.data),
            .draw_modes    = to_save_draw_modes(source.data)
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

        rewritten_path = temporary_path("draw_modes_again.usda");
        const erhe::usd::Usd_save_arguments rewrite_arguments{
            .path          = rewritten_path,
            .root_node     = reloaded_root,
            .materials     = reloaded.data.materials,
            .variant_sets  = to_save_variant_sets(reloaded.data),
            .draw_modes    = to_save_draw_modes(reloaded.data)
        };
        rewrite = erhe::usd::save_usda(rewrite_arguments);
        ASSERT_TRUE(rewrite.error.empty()) << rewrite.error;
    }

    std::shared_ptr<erhe::scene::Node> source_root;
    std::shared_ptr<erhe::scene::Node> reloaded_root;
    std::filesystem::path              written_path;
    std::filesystem::path              rewritten_path;
    erhe::usd::Usd_load_result         source;
    erhe::usd::Usd_save_result         save;
    erhe::usd::Usd_load_result         reloaded;
    erhe::usd::Usd_save_result         rewrite;
};

} // namespace

TEST_F(Draw_mode_round_trip, an_applied_schema_becomes_one_record)
{
    const erhe::usd::Usd_draw_mode* plain = find_draw_mode(source.data, "/World/Plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_TRUE(plain->description.draw_mode_authored);
    EXPECT_EQ(plain->description.draw_mode, erhe::scene::Draw_mode::default_);
    EXPECT_FALSE(plain->description.card_geometry_authored);
    EXPECT_FALSE(plain->description.extents_hint_authored);
    EXPECT_TRUE(plain->prim.operator bool());
}

TEST_F(Draw_mode_round_trip, every_authored_attribute_is_read)
{
    const erhe::usd::Usd_draw_mode* boxed = find_draw_mode(source.data, "/World/Boxed");
    ASSERT_NE(boxed, nullptr);
    const erhe::scene::Draw_mode_description& description = boxed->description;
    EXPECT_EQ(description.draw_mode, erhe::scene::Draw_mode::cards);
    EXPECT_EQ(description.card_geometry, erhe::scene::Draw_mode_card_geometry::box);
    EXPECT_EQ(description.card_visibility, erhe::scene::Draw_mode_card_visibility::simple);
    EXPECT_TRUE(description.apply_draw_mode);
    EXPECT_TRUE(description.apply_draw_mode_authored);
    EXPECT_TRUE(description.has_authored_value());
}

TEST_F(Draw_mode_round_trip, a_prim_without_the_schema_that_authors_a_draw_mode_is_read)
{
    const erhe::usd::Usd_draw_mode* sketchy = find_draw_mode(source.data, "/World/Sketchy");
    ASSERT_NE(sketchy, nullptr);
    EXPECT_EQ(sketchy->description.draw_mode, erhe::scene::Draw_mode::origin);
    EXPECT_TRUE(sketchy->description.draw_mode_authored);
}

TEST_F(Draw_mode_round_trip, a_color_and_an_extents_hint_are_read)
{
    const erhe::usd::Usd_draw_mode* cards = find_draw_mode(source.data, "/World/Cards");
    ASSERT_NE(cards, nullptr);
    const erhe::scene::Draw_mode_description& description = cards->description;
    ASSERT_TRUE(description.draw_mode_color_authored);
    EXPECT_FLOAT_EQ(description.draw_mode_color.x, 0.936f);
    EXPECT_FLOAT_EQ(description.draw_mode_color.y, 0.0f);
    ASSERT_TRUE(description.extents_hint_authored);
    EXPECT_FLOAT_EQ(description.extents_hint_min.x, -1.0f);
    EXPECT_FLOAT_EQ(description.extents_hint_max.y, 2.0f);
}

TEST_F(Draw_mode_round_trip, a_card_texture_path_is_resolved_against_the_stage_directory)
{
    const erhe::usd::Usd_draw_mode* boxed = find_draw_mode(source.data, "/World/Boxed");
    ASSERT_NE(boxed, nullptr);
    // The card textures sit in the selected variant block, so the composed
    // prim carries none of them: they are the block's own opinions.
    for (const std::string& path : boxed->description.card_textures) {
        EXPECT_TRUE(path.empty()) << path;
    }
}

TEST_F(Draw_mode_round_trip, a_variant_blocks_draw_mode_attributes_are_carried_opinions)
{
    const erhe::usd::Usd_variant_set* set = find_variant_set(source.data, "cardVariant");
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->unsupported_opinion_count, 0u);

    const erhe::usd::Usd_variant* textured = find_variant(*set, "Textured");
    ASSERT_NE(textured, nullptr);
    const erhe::scene::Instance_override_value* texture = find_value(*textured, "Draw_mode.card_texture_x_neg");
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->text, "./images/auto_rgba.png");
    ASSERT_NE(find_value(*textured, "Draw_mode.extents_hint_min"), nullptr);
    ASSERT_NE(find_value(*textured, "Draw_mode.extents_hint_max"), nullptr);

    const erhe::usd::Usd_variant* plainer = find_variant(*set, "Plainer");
    ASSERT_NE(plainer, nullptr);
    const erhe::scene::Instance_override_value* geometry = find_value(*plainer, "Draw_mode.card_geometry");
    ASSERT_NE(geometry, nullptr);
    EXPECT_EQ(geometry->text, "cross");
}

TEST_F(Draw_mode_round_trip, a_nested_variant_blocks_color_is_a_carried_opinion)
{
    const erhe::usd::Usd_variant_set* set = find_variant_set(source.data, "colorVariant");
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->unsupported_opinion_count, 0u);
    const erhe::usd::Usd_variant* dark = find_variant(*set, "Dark");
    ASSERT_NE(dark, nullptr);
    const erhe::scene::Instance_override_value* color = find_value(*dark, "Draw_mode.draw_mode_color");
    ASSERT_NE(color, nullptr);
    EXPECT_NE(color->text.find("0.025"), std::string::npos) << color->text;
}

TEST_F(Draw_mode_round_trip, the_writer_authors_the_schema_and_the_authored_values)
{
    const std::string text = read_text(written_path);
    EXPECT_NE(text.find("GeomModelAPI"), std::string::npos);
    EXPECT_NE(text.find("uniform token model:drawMode = \"cards\""), std::string::npos);
    EXPECT_NE(text.find("uniform token model:cardGeometry = \"box\""), std::string::npos);
    EXPECT_NE(text.find("uniform token model:cardVisibility = \"simple\""), std::string::npos);
    EXPECT_NE(text.find("uniform bool model:applyDrawMode = 1"), std::string::npos);
    EXPECT_NE(text.find("extentsHint"), std::string::npos);
    EXPECT_NE(text.find("model:cardTextureXNeg"), std::string::npos);
    // The prim that authors no draw mode of its own gets none.
    EXPECT_EQ(text.find("model:drawMode = \"inherited\""), std::string::npos);
}

TEST_F(Draw_mode_round_trip, the_records_come_back_and_a_second_save_is_a_fixed_point)
{
    const erhe::usd::Usd_draw_mode* boxed = find_draw_mode(reloaded.data, "/World/Boxed");
    ASSERT_NE(boxed, nullptr);
    EXPECT_EQ(boxed->description.draw_mode, erhe::scene::Draw_mode::cards);
    EXPECT_EQ(boxed->description.card_geometry, erhe::scene::Draw_mode_card_geometry::box);
    EXPECT_EQ(boxed->description.card_visibility, erhe::scene::Draw_mode_card_visibility::simple);
    EXPECT_TRUE(boxed->description.apply_draw_mode);

    const erhe::usd::Usd_draw_mode* cards = find_draw_mode(reloaded.data, "/World/Cards");
    ASSERT_NE(cards, nullptr);
    EXPECT_TRUE(cards->description.extents_hint_authored);
    EXPECT_FLOAT_EQ(cards->description.draw_mode_color.x, 0.936f);

    EXPECT_EQ(read_text(written_path), read_text(rewritten_path));
}
