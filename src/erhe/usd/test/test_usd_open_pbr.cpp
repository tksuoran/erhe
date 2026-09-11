// The OpenPBR network of a USD material, which is the terminal erhe reads
// wherever a Material prim offers one: it carries the anisotropic roughness
// and the transmission UsdPreviewSurface has no input for
// (doc/usd_compatibility.md, Materials; src/erhe/usd/notes.md).

#include "erhe_graphics/enums.hpp"
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
#include <memory>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
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
    // The UsdUVTexture's wrap modes reach the slot the texture was bound to.
    const erhe::primitive::Material_texture_samplers& slots = material->data.texture_samplers;
    EXPECT_EQ(slots.base_color.sampler.wrap_u, erhe::graphics::Sampler_address_mode::repeat);
    EXPECT_EQ(slots.base_color.sampler.wrap_v, erhe::graphics::Sampler_address_mode::mirrored_repeat);
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
        if ((binding.material_index == material_index) && (binding.slot == erhe::usd::Usd_material_texture_slot::base_color)) {
            slot_is_bound = true;
            EXPECT_EQ(binding.graph_path, "/root/Graph_Textures/Rust");
        }
    }
    EXPECT_TRUE(slot_is_bound);
}

} // namespace
