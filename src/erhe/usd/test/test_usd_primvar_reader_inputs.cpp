// A UsdPreviewSurface input a UsdPrimvarReader feeds: the reader of
// `displayColor` / `displayOpacity` is the mesh's vertex colors, every other
// primvar is a warning and the input keeps its own value, and the rest of the
// material converts either way (doc/erhe/usd_compatibility.md, "Materials";
// doc/erhe/usd.md, "UsdPreviewSurface fallbacks and channel outputs").

#include "test_temporary_directory.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = erhe_usd_test::process_temporary_directory() / "erhe_usd_primvar_reader_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
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

// Whether a written layer holds a line containing the given text.
[[nodiscard]] auto file_has_line_with(const std::filesystem::path& path, const std::string& text) -> bool
{
    std::ifstream stream{path};
    std::string   line;
    while (std::getline(stream, line)) {
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

class Primvar_reader_inputs_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("primvar_reader_inputs.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
        vertex_color    = material_at(root, "root/materials/VertexColor");
        unknown_primvar = material_at(root, "root/materials/UnknownPrimvar");
        ASSERT_NE(vertex_color,    nullptr);
        ASSERT_NE(unknown_primvar, nullptr);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
    erhe::primitive::Material*         vertex_color   {nullptr};
    erhe::primitive::Material*         unknown_primvar{nullptr};
};

TEST_F(Primvar_reader_inputs_import, display_color_and_display_opacity_are_the_vertex_colors)
{
    EXPECT_EQ(vertex_color->get_base_color_source(), erhe::primitive::Material_input_source::vertex_color);
    EXPECT_EQ(vertex_color->get_opacity_source(),    erhe::primitive::Material_input_source::vertex_color);
}

// The whole material used to fail in the render-scene conversion over the
// connection, so the inputs beside it are what say the material converted.
TEST_F(Primvar_reader_inputs_import, the_rest_of_the_material_converts)
{
    EXPECT_NEAR(vertex_color->get_roughness().x, 0.125f, 1e-5f);
    EXPECT_NEAR(vertex_color->get_metallic(),    0.75f,  1e-5f);
}

TEST_F(Primvar_reader_inputs_import, a_vertex_color_input_writes_no_value_of_its_own)
{
    EXPECT_FALSE(vertex_color->has_own_value(erhe::primitive::Material::base_color_property.get()));
    EXPECT_FALSE(vertex_color->has_own_value(erhe::primitive::Material::opacity_property.get()));
}

TEST_F(Primvar_reader_inputs_import, an_unknown_primvar_leaves_the_input_at_its_own_value)
{
    EXPECT_EQ(unknown_primvar->get_base_color_source(), erhe::primitive::Material_input_source::value);
    EXPECT_NEAR(unknown_primvar->get_roughness().x, 0.375f, 1e-5f);
}

class Primvar_reader_inputs_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        source      = load(test_data_path("primvar_reader_inputs.usda"), source_root);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("primvar_reader_inputs.usda");
        const erhe::usd::Usd_save_arguments save_arguments{
            .path        = written_path,
            .root_node   = source_root,
            .materials   = source.data.materials,
            .dome_lights = source.data.dome_lights
        };
        save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        reloaded      = load(written_path, reloaded_root);
        ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    }

    std::shared_ptr<erhe::scene::Node> source_root;
    std::shared_ptr<erhe::scene::Node> reloaded_root;
    std::filesystem::path              written_path;
    erhe::usd::Usd_load_result         source;
    erhe::usd::Usd_save_result         save;
    erhe::usd::Usd_load_result         reloaded;
};

TEST_F(Primvar_reader_inputs_round_trip, the_readers_are_written_back)
{
    EXPECT_TRUE(file_has_line_with(written_path, "UsdPrimvarReader_float3"));
    EXPECT_TRUE(file_has_line_with(written_path, "UsdPrimvarReader_float\""));
    EXPECT_TRUE(file_has_line_with(written_path, "inputs:diffuseColor.connect"));
    EXPECT_TRUE(file_has_line_with(written_path, "inputs:opacity.connect"));
}

TEST_F(Primvar_reader_inputs_round_trip, the_sources_survive_the_round_trip)
{
    erhe::primitive::Material* vertex_color = material_at(reloaded_root, "root/materials/VertexColor");
    ASSERT_NE(vertex_color, nullptr);
    EXPECT_EQ(vertex_color->get_base_color_source(), erhe::primitive::Material_input_source::vertex_color);
    EXPECT_EQ(vertex_color->get_opacity_source(),    erhe::primitive::Material_input_source::vertex_color);
    EXPECT_NEAR(vertex_color->get_roughness().x, 0.125f, 1e-5f);
}

TEST_F(Primvar_reader_inputs_round_trip, an_unknown_primvar_is_not_written_back_as_a_reader)
{
    erhe::primitive::Material* unknown_primvar = material_at(reloaded_root, "root/materials/UnknownPrimvar");
    ASSERT_NE(unknown_primvar, nullptr);
    EXPECT_EQ(unknown_primvar->get_base_color_source(), erhe::primitive::Material_input_source::value);
}
