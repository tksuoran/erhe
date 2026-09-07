// `class` prims and `inherits` arcs (doc/usd-compatibility-plan.md X3). The
// reader records every class prim of the root layer with its opinions and its
// chain, and every prim's `inherits` targets in list-op order; the writer puts
// a style item back as a `class` prim and a prim with a style back as an
// `inherits` arc. erhe::usd creates no Style item - that is the editor's half
// of the step - so the export side builds a stand-in prim carrying the class
// token a style has.

#include "erhe_item/item.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_property/owner_type.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_style_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_lines(const std::filesystem::path& path) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    std::ifstream            stream{path};
    std::string              line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

[[nodiscard]] auto has_line_with(const std::vector<std::string>& lines, const std::string& needle) -> bool
{
    for (const std::string& line : lines) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto find_class(
    const std::vector<erhe::usd::Usd_class_prim>& classes,
    const std::string&                            stage_path
) -> const erhe::usd::Usd_class_prim*
{
    for (const erhe::usd::Usd_class_prim& class_prim : classes) {
        if (class_prim.stage_path == stage_path) {
            return &class_prim;
        }
        const erhe::usd::Usd_class_prim* found = find_class(class_prim.children, stage_path);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_value(const erhe::usd::Usd_class_prim& class_prim, const std::string& name) -> std::string
{
    for (const erhe::scene::Instance_override_value& value : class_prim.values) {
        if (value.name == name) {
            return value.text;
        }
    }
    return {};
}

[[nodiscard]] auto find_inherits(
    const erhe::usd::Usd_data& data,
    const std::string&         stage_path
) -> const erhe::usd::Usd_prim_inherits*
{
    for (const erhe::usd::Usd_prim_inherits& entry : data.prim_inherits) {
        if (entry.stage_path == stage_path) {
            return &entry;
        }
    }
    return nullptr;
}

// A stand-in for editor::Style: a prim whose class token is the one the
// exporter recognizes a style by. erhe::usd cannot name the editor type, and
// the token is exactly what it goes on (usd_export.cpp).
class Test_style : public erhe::Item<erhe::Item_base, erhe::Typed, Test_style>
{
public:
    explicit Test_style(const std::string_view name) : Item{name}
    {
        enable_flag_bits(erhe::Item_flags::show_in_ui);
    }
    explicit Test_style(const Test_style& other) = default;

    static constexpr std::string_view static_type_name{"Style"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Typed::get_static_type() | erhe::Item_type::style;
    }
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Style"; }
    [[nodiscard]] auto get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type> override
    {
        return erhe::property::root_owner_type;
    }
};

class Styles_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("styles.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        loaded = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(loaded.error.empty()) << loaded.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         loaded;
};

TEST_F(Styles_import, class_prims_are_recorded_with_their_values)
{
    const erhe::usd::Usd_class_prim* metal = find_class(loaded.data.classes, "/World/Styles/Metal");
    ASSERT_NE(metal, nullptr);
    EXPECT_EQ(metal->name, "Metal");
    EXPECT_TRUE(metal->inherits.empty());
    EXPECT_EQ(find_value(*metal, "Material.roughness"), "0.2 0.2"); // Material.roughness is anisotropic
    EXPECT_EQ(find_value(*metal, "Mesh.shadow_cast"), "0"); // the USDA literal of a bool
}

TEST_F(Styles_import, a_class_inheriting_a_class_records_its_chain)
{
    const erhe::usd::Usd_class_prim* shiny = find_class(loaded.data.classes, "/World/Styles/Shiny");
    ASSERT_NE(shiny, nullptr);
    ASSERT_EQ(shiny->inherits.size(), 1u);
    EXPECT_EQ(shiny->inherits.front(), "/World/Styles/Metal");
    EXPECT_EQ(find_value(*shiny, "Material.metallic"), "1");
}

TEST_F(Styles_import, class_prims_are_not_scene_content)
{
    for (const std::shared_ptr<erhe::scene::Node>& node : loaded.data.nodes) {
        ASSERT_TRUE(node);
        EXPECT_NE(node->get_name(), "Metal");
        EXPECT_NE(node->get_name(), "Shiny");
    }
    for (const std::shared_ptr<erhe::Typed>& prim : loaded.data.prims) {
        ASSERT_TRUE(prim);
        EXPECT_NE(prim->get_name(), "Metal");
        EXPECT_NE(prim->get_name(), "Shiny");
    }
}

TEST_F(Styles_import, a_mesh_prim_records_its_inherits_arc)
{
    const erhe::usd::Usd_prim_inherits* entry = find_inherits(loaded.data, "/World/Holder/quad");
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(entry->item);
    EXPECT_EQ(entry->item->get_name(), "quad");
    ASSERT_EQ(entry->inherits.size(), 1u);
    EXPECT_EQ(entry->inherits.front(), "/World/Styles/Metal");
}

TEST_F(Styles_import, a_material_prim_records_its_inherits_arc)
{
    const erhe::usd::Usd_prim_inherits* entry = find_inherits(loaded.data, "/World/Chrome");
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(entry->item);
    EXPECT_EQ(entry->item->get_name(), "Chrome");
    ASSERT_EQ(entry->inherits.size(), 1u);
    EXPECT_EQ(entry->inherits.front(), "/World/Styles/Shiny");
}

TEST_F(Styles_import, a_target_that_names_no_prim_is_recorded_as_authored)
{
    const erhe::usd::Usd_prim_inherits* entry = find_inherits(loaded.data, "/World/Dangling");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->inherits.size(), 1u);
    EXPECT_EQ(entry->inherits.front(), "/World/Styles/Missing");
    EXPECT_EQ(find_class(loaded.data.classes, "/World/Styles/Missing"), nullptr);
}

class Styles_export : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("save_root");

        std::shared_ptr<erhe::scene::Xform> world = std::make_shared<erhe::scene::Xform>("World");
        world->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        world->set_parent(root);

        std::shared_ptr<erhe::Scope> styles_scope = std::make_shared<erhe::Scope>("Styles");
        styles_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
        styles_scope->set_parent(world);

        metal = std::make_shared<Test_style>("Metal");
        metal->set_parent(styles_scope);

        shiny = std::make_shared<Test_style>("Shiny");
        shiny->set_parent(styles_scope);
        EXPECT_TRUE(shiny->set_style(metal));

        std::shared_ptr<erhe::scene::Xform> holder = std::make_shared<erhe::scene::Xform>("Holder");
        holder->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        holder->set_parent(world);
        EXPECT_TRUE(holder->set_style(shiny));

        written_path = temporary_path("styles.usda");
        const erhe::usd::Usd_save_arguments save_arguments{
            .path      = written_path,
            .root_node = root
        };
        save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;
        lines = read_lines(written_path);
    }

    std::shared_ptr<erhe::scene::Node> root;
    std::shared_ptr<Test_style>        metal;
    std::shared_ptr<Test_style>        shiny;
    std::filesystem::path              written_path;
    erhe::usd::Usd_save_result         save;
    std::vector<std::string>           lines;
};

TEST_F(Styles_export, a_style_item_is_written_as_a_class_prim)
{
    EXPECT_TRUE(has_line_with(lines, "class \"Metal\""));
    EXPECT_TRUE(has_line_with(lines, "class \"Shiny\""));
}

TEST_F(Styles_export, a_style_with_a_style_is_written_with_an_inherits_arc)
{
    EXPECT_TRUE(has_line_with(lines, "inherits = </World/Styles/Metal>"));
}

TEST_F(Styles_export, a_prim_with_a_style_is_written_with_an_inherits_arc)
{
    EXPECT_TRUE(has_line_with(lines, "inherits = </World/Styles/Shiny>"));
}

TEST_F(Styles_export, no_style_reference_leaks_out_as_a_custom_attribute)
{
    EXPECT_FALSE(has_line_with(lines, "erhe:Item_base:style"));
}

TEST_F(Styles_export, a_second_save_is_textually_identical)
{
    const std::filesystem::path second_path = temporary_path("styles_2.usda");
    const erhe::usd::Usd_save_arguments save_arguments{
        .path      = second_path,
        .root_node = root
    };
    const erhe::usd::Usd_save_result second = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(second.error.empty()) << second.error;
    EXPECT_EQ(read_lines(second_path), lines);
}

// The class prim a style is written as reads back as the class prim it was,
// with its values and its chain.
TEST_F(Styles_export, the_written_class_prims_read_back)
{
    metal->set_value(erhe::Item_base::visible_property, false);
    const std::filesystem::path round_trip_path = temporary_path("styles_round_trip.usda");
    const erhe::usd::Usd_save_arguments save_arguments{
        .path      = round_trip_path,
        .root_node = root
    };
    const erhe::usd::Usd_save_result written = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(written.error.empty()) << written.error;

    std::shared_ptr<erhe::scene::Node>  reload_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_arguments load_arguments{
        .path          = round_trip_path,
        .root_node     = reload_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;

    const erhe::usd::Usd_class_prim* reloaded_metal = find_class(reloaded.data.classes, "/World/Styles/Metal");
    ASSERT_NE(reloaded_metal, nullptr);
    EXPECT_EQ(find_value(*reloaded_metal, "visible"), "false");

    const erhe::usd::Usd_class_prim* reloaded_shiny = find_class(reloaded.data.classes, "/World/Styles/Shiny");
    ASSERT_NE(reloaded_shiny, nullptr);
    ASSERT_EQ(reloaded_shiny->inherits.size(), 1u);
    EXPECT_EQ(reloaded_shiny->inherits.front(), "/World/Styles/Metal");

    const erhe::usd::Usd_prim_inherits* holder = find_inherits(reloaded.data, "/World/Holder");
    ASSERT_NE(holder, nullptr);
    ASSERT_EQ(holder->inherits.size(), 1u);
    EXPECT_EQ(holder->inherits.front(), "/World/Styles/Shiny");
}

} // anonymous namespace
