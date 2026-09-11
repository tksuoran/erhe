// The OpenPBR network of a USD material, which is the terminal erhe reads
// wherever a Material prim offers one: it carries the anisotropic roughness
// and the transmission UsdPreviewSurface has no input for
// (doc/usd_compatibility.md, Materials; src/erhe/usd/notes.md).

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
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
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_open_pbr_tests";
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

// The lines of the prim block starting at the first line holding `needle`,
// down to the line whose indentation returns to that of the opening line.
[[nodiscard]] auto block_lines(const std::vector<std::string>& lines, const std::string& needle) -> std::vector<std::string>
{
    std::vector<std::string> block;
    std::size_t              index = 0;
    while ((index < lines.size()) && (lines[index].find(needle) == std::string::npos)) {
        ++index;
    }
    if (index >= lines.size()) {
        return block;
    }
    const std::size_t indent = lines[index].find_first_not_of(' ');
    block.push_back(lines[index]);
    for (++index; index < lines.size(); ++index) {
        block.push_back(lines[index]);
        const std::size_t line_indent = lines[index].find_first_not_of(' ');
        if ((line_indent == indent) && (lines[index].find('}') != std::string::npos)) {
            break;
        }
    }
    return block;
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

[[nodiscard]] auto material_index_of(const erhe::usd::Usd_load_result& result, const std::string& name) -> std::size_t
{
    for (std::size_t index = 0, end = result.data.materials.size(); index < end; ++index) {
        if (result.data.materials[index]->get_name() == name) {
            return index;
        }
    }
    return result.data.materials.size();
}

class Open_pbr_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("open_pbr.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Open_pbr_import, an_open_pbr_network_carries_the_anisotropic_roughness)
{
    erhe::primitive::Material* material = material_at(root, "root/materials/Anisotropic");
    ASSERT_NE(material, nullptr);
    // MaterialX's roughness_anisotropy parameterization on
    // specular_roughness 0.4 and specular_roughness_anisotropy 0.75:
    // alpha = 0.16, aspect = 0.5, and the two alphas are 0.32 and 0.08.
    const glm::vec2 roughness = material->get_value(erhe::primitive::Material::roughness_property);
    EXPECT_NEAR(roughness.x, std::sqrt(0.32f), 1.0e-5f);
    EXPECT_NEAR(roughness.y, std::sqrt(0.08f), 1.0e-5f);
    // The two roughness components only reach the shading through a BXDF
    // model that reads both.
    EXPECT_EQ(
        material->get_value(erhe::primitive::Material::bxdf_model_property),
        erhe::primitive::Bxdf_model::anisotropic_brdf
    );
}

TEST_F(Open_pbr_import, an_open_pbr_network_carries_transmission_and_emission)
{
    erhe::primitive::Material* material = material_at(root, "root/materials/Anisotropic");
    ASSERT_NE(material, nullptr);
    EXPECT_NEAR(material->get_value(erhe::primitive::Material::transmission_property), 0.6f, 1.0e-5f);
    // OpenPBR's emission is a luminance times a color; erhe's emissive is the
    // linear color the shader adds.
    const glm::vec3 emissive = material->get_value(erhe::primitive::Material::emissive_property);
    EXPECT_NEAR(emissive.x, 2.0f,  1.0e-5f);
    EXPECT_NEAR(emissive.y, 1.0f,  1.0e-5f);
    EXPECT_NEAR(emissive.z, 0.5f,  1.0e-5f);
}

TEST_F(Open_pbr_import, an_open_pbr_network_carries_the_base_layer_and_the_opacity)
{
    erhe::primitive::Material* material = material_at(root, "root/materials/Anisotropic");
    ASSERT_NE(material, nullptr);
    const glm::vec3 base_color = material->get_value(erhe::primitive::Material::base_color_property);
    EXPECT_NEAR(base_color.x, 0.2f, 1.0e-5f);
    EXPECT_NEAR(base_color.y, 0.4f, 1.0e-5f);
    EXPECT_NEAR(base_color.z, 0.8f, 1.0e-5f);
    EXPECT_NEAR(material->get_value(erhe::primitive::Material::metallic_property), 1.0f, 1.0e-5f);
    EXPECT_NEAR(material->get_value(erhe::primitive::Material::ior_property),      1.4f, 1.0e-5f);
    EXPECT_NEAR(material->get_value(erhe::primitive::Material::opacity_property),  0.5f, 1.0e-5f);
    // OpenPBR has no opacity threshold, so an opacity below one is blended.
    EXPECT_EQ(
        material->get_value(erhe::primitive::Material::blending_mode_property),
        erhe::primitive::Material_blending_mode::alpha_blend
    );
}

TEST_F(Open_pbr_import, a_material_offering_both_networks_is_read_as_open_pbr)
{
    erhe::primitive::Material* material = material_at(root, "root/materials/Both");
    ASSERT_NE(material, nullptr);
    // The file authors roughness 0.2 on its UsdPreviewSurface and
    // specular_roughness 0.7 on its OpenPBR shader.
    const glm::vec2 roughness = material->get_value(erhe::primitive::Material::roughness_property);
    EXPECT_NEAR(roughness.x, 0.7f, 1.0e-5f);
    EXPECT_NEAR(roughness.y, 0.7f, 1.0e-5f);
    const glm::vec3 base_color = material->get_value(erhe::primitive::Material::base_color_property);
    EXPECT_NEAR(base_color.x, 0.1f, 1.0e-5f);
    EXPECT_NEAR(base_color.y, 0.2f, 1.0e-5f);
    EXPECT_NEAR(base_color.z, 0.3f, 1.0e-5f);
}

TEST_F(Open_pbr_import, a_texture_on_base_color_binds_the_slot)
{
    erhe::primitive::Material* material = material_at(root, "root/materials/Textured");
    ASSERT_NE(material, nullptr);
    const std::size_t material_index = material_index_of(result, "Textured");
    ASSERT_LT(material_index, result.data.materials.size());
    bool slot_is_bound = false;
    for (const erhe::usd::Usd_material_texture_binding& binding : result.data.material_texture_bindings) {
        if ((binding.material_index == material_index) && (binding.slot == erhe::usd::Usd_material_texture_slot::base_color)) {
            slot_is_bound = true;
        }
    }
    EXPECT_TRUE(slot_is_bound);
    // A connected input takes its value from the texture, so the erhe factor
    // is the UsdUVTexture's inputs:scale.
    const glm::vec3 base_color = material->get_value(erhe::primitive::Material::base_color_property);
    EXPECT_NEAR(base_color.x, 0.5f,  1.0e-5f);
    EXPECT_NEAR(base_color.y, 0.25f, 1.0e-5f);
    EXPECT_NEAR(base_color.z, 0.75f, 1.0e-5f);
}

TEST_F(Open_pbr_import, a_texture_graph_bound_through_the_preview_surface_survives_the_open_pbr_preference)
{
    // An erhe texture graph binds a slot through a UsdPreviewSurface input
    // whichever terminal supplies the values, so a material read as OpenPBR
    // still records the binding its preview surface carries.
    const std::size_t material_index = material_index_of(result, "Both");
    ASSERT_LT(material_index, result.data.materials.size());
    bool slot_is_bound = false;
    for (const erhe::usd::Usd_material_graph_binding& binding : result.data.material_graph_bindings) {
        if ((binding.material_index == material_index) && (binding.slot == erhe::usd::Usd_material_texture_slot::emissive)) {
            slot_is_bound = true;
            EXPECT_EQ(binding.graph_path, "/root/Graph_Textures/Rust");
        }
    }
    EXPECT_TRUE(slot_is_bound);
}

// The two parameterizations of an anisotropic surface convert both ways, so
// a written network reads back as the pair erhe holds.
TEST(Open_pbr_roughness_conversion, the_anisotropy_inverse_round_trips)
{
    const std::vector<glm::vec2> pairs{
        glm::vec2{0.5656854f, 0.2828427f},
        glm::vec2{0.4f,       0.4f      },
        glm::vec2{0.6f,       0.3f      },
        glm::vec2{0.25f,      0.2f      }
    };
    for (const glm::vec2& pair : pairs) {
        const erhe::usd::Open_pbr_roughness open_pbr = erhe::usd::from_anisotropic_roughness(pair);
        const glm::vec2                     back     = erhe::usd::to_anisotropic_roughness(open_pbr.roughness, open_pbr.anisotropy);
        EXPECT_NEAR(back.x, pair.x, 1.0e-5f) << pair.x << ", " << pair.y;
        EXPECT_NEAR(back.y, pair.y, 1.0e-5f) << pair.x << ", " << pair.y;
    }
    // The parameterization runs one way only: an anisotropy is not negative,
    // so it always says X is the rougher direction. A pair whose Y component
    // is the larger one is not expressible and carries the X roughness alone
    // (the exact pair rides as the material's own erhe:Material:roughness).
    const erhe::usd::Open_pbr_roughness rougher_y = erhe::usd::from_anisotropic_roughness(glm::vec2{0.2f, 0.6f});
    EXPECT_NEAR(rougher_y.roughness,  0.2f, 1.0e-5f);
    EXPECT_NEAR(rougher_y.anisotropy, 0.0f, 1.0e-5f);
    // The node clamps its anisotropy at 0.98, so a ratio below sqrt(1 - 0.98)
    // saturates there and the network is as anisotropic as it can say.
    const erhe::usd::Open_pbr_roughness extreme = erhe::usd::from_anisotropic_roughness(glm::vec2{0.9f, 0.1f});
    EXPECT_NEAR(extreme.anisotropy, 0.98f, 1.0e-5f);
}

class Open_pbr_export : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("save_root");
        source = load(test_data_path("open_pbr.usda"), root);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("open_pbr.usda");
        save_arguments.path      = written_path;
        save_arguments.root_node = root;
        save_arguments.materials = source.data.materials;
        save_arguments.textures  = std::vector<erhe::usd::Usd_save_texture>{
            erhe::usd::Usd_save_texture{
                .material_index = material_index_of(source, "Textured"),
                .slot           = erhe::usd::Usd_material_texture_slot::base_color,
                .path           = test_data_path("images/auto_rgba.png"),
                .srgb           = true
            }
        };
        save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;
        lines = read_lines(written_path);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         source;
    std::filesystem::path              written_path;
    erhe::usd::Usd_save_arguments      save_arguments;
    erhe::usd::Usd_save_result         save;
    std::vector<std::string>           lines;
};

TEST_F(Open_pbr_export, an_anisotropic_and_transmissive_material_writes_both_terminals)
{
    const std::vector<std::string> block = block_lines(lines, "def Material \"Anisotropic\"");
    ASSERT_FALSE(block.empty());
    // The preview surface stays on outputs:surface and the OpenPBR network is
    // the second terminal.
    EXPECT_TRUE(has_line_with(block, "token outputs:surface.connect = </root/materials/Anisotropic/surface.outputs:surface>"));
    EXPECT_TRUE(has_line_with(block, "token outputs:mtlx:surface.connect = </root/materials/Anisotropic/open_pbr.outputs:surface>"));
    EXPECT_TRUE(has_line_with(block, "uniform token info:id = \"ND_open_pbr_surface_surfaceshader\""));
    EXPECT_TRUE(has_line_with(block, "def Shader \"open_pbr\""));
    // The values the preview surface has no input for, and the base layer.
    EXPECT_TRUE(has_line_with(block, "float inputs:transmission_weight = 0.6"));
    EXPECT_TRUE(has_line_with(block, "float inputs:specular_roughness = 0.4"));
    EXPECT_TRUE(has_line_with(block, "float inputs:specular_roughness_anisotropy = 0.75"));
    EXPECT_TRUE(has_line_with(block, "float inputs:base_metalness = 1"));
    EXPECT_TRUE(has_line_with(block, "float inputs:specular_ior = 1.4"));
    EXPECT_TRUE(has_line_with(block, "float inputs:geometry_opacity = 0.5"));
    EXPECT_TRUE(has_line_with(block, "color3f inputs:base_color = (0.2, 0.4, 0.8)"));
    EXPECT_TRUE(has_line_with(block, "color3f inputs:emission_color = (2, 1, 0.5)"));
    EXPECT_TRUE(has_line_with(block, "float inputs:emission_luminance = 1"));
    // The pair erhe holds, which neither terminal carries as it is.
    EXPECT_TRUE(has_line_with(block, "custom float2 erhe:Material:roughness"));
}

TEST_F(Open_pbr_export, a_material_the_preview_surface_carries_writes_no_open_pbr_network)
{
    // `Both` is isotropic and not transmissive: one terminal, one shader.
    const std::vector<std::string> block = block_lines(lines, "def Material \"Both\"");
    ASSERT_FALSE(block.empty());
    EXPECT_FALSE(has_line_with(block, "outputs:mtlx:surface"));
    EXPECT_FALSE(has_line_with(block, "ND_open_pbr_surface_surfaceshader"));
}

TEST_F(Open_pbr_export, the_open_pbr_inputs_read_the_preview_surface_textures)
{
    // `Textured` is anisotropic and reads an image on its base color, so its
    // OpenPBR input connects to the material's one UsdUVTexture prim - the
    // preview surface's own - rather than to a second network.
    const std::vector<std::string> block = block_lines(lines, "def Material \"Textured\"");
    ASSERT_FALSE(block.empty());
    EXPECT_TRUE(
        has_line_with(block, "color3f inputs:base_color.connect = </root/materials/Textured/base_color_texture.outputs:rgb>")
    );
    std::size_t texture_prims = 0;
    for (const std::string& line : block) {
        if (line.find("def Shader \"base_color_texture\"") != std::string::npos) {
            ++texture_prims;
        }
    }
    EXPECT_EQ(texture_prims, 1u);
}

TEST_F(Open_pbr_export, the_written_network_reads_back_and_a_second_save_is_identical)
{
    std::shared_ptr<erhe::scene::Node> reload_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_result   reloaded    = load(written_path, reload_root);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;

    erhe::primitive::Material* material = material_at(reload_root, "root/materials/Anisotropic");
    ASSERT_NE(material, nullptr);
    const glm::vec2 roughness = material->get_value(erhe::primitive::Material::roughness_property);
    EXPECT_NEAR(roughness.x, std::sqrt(0.32f), 1.0e-5f);
    EXPECT_NEAR(roughness.y, std::sqrt(0.08f), 1.0e-5f);
    EXPECT_NEAR(material->get_value(erhe::primitive::Material::transmission_property), 0.6f, 1.0e-5f);
    EXPECT_NEAR(material->get_value(erhe::primitive::Material::ior_property),          1.4f, 1.0e-5f);
    const glm::vec3 emissive = material->get_value(erhe::primitive::Material::emissive_property);
    EXPECT_NEAR(emissive.x, 2.0f, 1.0e-5f);
    EXPECT_NEAR(emissive.z, 0.5f, 1.0e-5f);

    const std::filesystem::path      second_path = temporary_path("open_pbr_2.usda");
    erhe::usd::Usd_save_arguments    second_arguments = save_arguments;
    second_arguments.path      = second_path;
    second_arguments.root_node = reload_root;
    second_arguments.materials = reloaded.data.materials;
    const erhe::usd::Usd_save_result second = erhe::usd::save_usda(second_arguments);
    ASSERT_TRUE(second.error.empty()) << second.error;
    EXPECT_EQ(read_lines(second_path), lines);
}

} // namespace
