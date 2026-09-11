// How a UsdUVTexture is sampled and how its texels are read: the wrap modes
// and the UsdTransform2d land on the erhe material slot, the inputs:scale of
// a color input becomes the factor the shader multiplies the texture with,
// and the normal slot carries the inputs:scale / inputs:bias decode
// (doc/usd_compatibility.md, Materials).

#include "erhe_graphics/enums.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <memory>
#include <string>

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

class Texture_inputs_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("texture_inputs.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
        material = material_at(root, "root/materials/Sampled");
        ASSERT_NE(material, nullptr);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
    erhe::primitive::Material*         material{nullptr};
};

TEST_F(Texture_inputs_import, wrap_modes_reach_the_slot_sampler)
{
    const erhe::primitive::Material_texture_samplers& slots = material->data.texture_samplers;
    EXPECT_EQ(slots.base_color.sampler.wrap_u, erhe::graphics::Sampler_address_mode::repeat);
    EXPECT_EQ(slots.base_color.sampler.wrap_v, erhe::graphics::Sampler_address_mode::mirrored_repeat);
    EXPECT_EQ(slots.emissive  .sampler.wrap_u, erhe::graphics::Sampler_address_mode::clamp_to_edge);
    EXPECT_EQ(slots.emissive  .sampler.wrap_v, erhe::graphics::Sampler_address_mode::clamp_to_edge);
}

TEST_F(Texture_inputs_import, transform2d_reaches_the_slot_transform)
{
    // The slot transform acts on the flipped texcoord the importer stores, so
    // the UsdTransform2d (rotation 90 degrees, scale (2, 3), translation
    // (0.25, 0.5)) is converted through `v' = 1 - v`: the rotation negates
    // and the offset becomes
    // (tx - sin(r) * sy, 1 - ty - cos(r) * sy) = (-2.75, 0.5).
    const erhe::primitive::Material_texture_sampler& slot = material->data.texture_samplers.base_color;
    EXPECT_NEAR(slot.rotation, glm::radians(-90.0f), 1e-5f);
    EXPECT_NEAR(slot.scale.x,  2.0f,  1e-5f);
    EXPECT_NEAR(slot.scale.y,  3.0f,  1e-5f);
    EXPECT_NEAR(slot.offset.x, -2.75f, 1e-5f);
    EXPECT_NEAR(slot.offset.y,  0.5f,  1e-5f);
    // A texture with no UsdTransform2d leaves the slot at the identity.
    const erhe::primitive::Material_texture_sampler& emissive = material->data.texture_samplers.emissive;
    EXPECT_NEAR(emissive.rotation, 0.0f, 1e-5f);
    EXPECT_NEAR(emissive.scale.x,  1.0f, 1e-5f);
    EXPECT_NEAR(emissive.scale.y,  1.0f, 1e-5f);
}

TEST_F(Texture_inputs_import, a_color_texture_scale_becomes_the_slot_factor)
{
    const glm::vec3 base_color = material->get_value(erhe::primitive::Material::base_color_property);
    EXPECT_NEAR(base_color.x, 0.5f,  1e-5f);
    EXPECT_NEAR(base_color.y, 0.25f, 1e-5f);
    EXPECT_NEAR(base_color.z, 0.75f, 1e-5f);
    // An emissive texture with no inputs:scale multiplies by one, which the
    // erhe emissive factor - zero by default - has to say explicitly.
    const glm::vec3 emissive = material->get_value(erhe::primitive::Material::emissive_property);
    EXPECT_NEAR(emissive.x, 1.0f, 1e-5f);
    EXPECT_NEAR(emissive.y, 1.0f, 1e-5f);
    EXPECT_NEAR(emissive.z, 1.0f, 1e-5f);
}

// An opacity input read through a second UsdUVTexture on the base color's
// file is the same image: the channel it names reaches the material, where
// an image of its own would have no erhe slot.
TEST_F(Texture_inputs_import, an_opacity_texture_on_the_base_color_file_names_its_channel)
{
    EXPECT_EQ(material->get_value(erhe::primitive::Material::opacity_channel_property), erhe::primitive::Texture_channel::r);
}

TEST_F(Texture_inputs_import, the_normal_slot_carries_the_texel_decode)
{
    const glm::vec4 scale = material->get_value(erhe::primitive::Material::normal_texture_decode_scale_property);
    const glm::vec4 bias  = material->get_value(erhe::primitive::Material::normal_texture_decode_bias_property);
    EXPECT_NEAR(scale.x, -2.0f, 1e-5f);
    EXPECT_NEAR(scale.y,  2.0f, 1e-5f);
    EXPECT_NEAR(scale.z,  1.0f, 1e-5f);
    EXPECT_NEAR(scale.w,  2.0f, 1e-5f);
    EXPECT_NEAR(bias.x,   1.0f, 1e-5f);
    EXPECT_NEAR(bias.y,  -1.0f, 1e-5f);
    EXPECT_NEAR(bias.z,   0.0f, 1e-5f);
    EXPECT_NEAR(bias.w,  -1.0f, 1e-5f);
}

TEST_F(Texture_inputs_import, a_material_without_a_normal_texture_keeps_the_default_decode)
{
    std::shared_ptr<erhe::scene::Node> other_root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_result   other      = load(test_data_path("textured.usda"), other_root);
    ASSERT_TRUE(other.error.empty()) << other.error;
    erhe::primitive::Material* gridded = material_at(other_root, "root/materials/Gridded");
    ASSERT_NE(gridded, nullptr);
    const glm::vec4 scale = gridded->get_value(erhe::primitive::Material::normal_texture_decode_scale_property);
    const glm::vec4 bias  = gridded->get_value(erhe::primitive::Material::normal_texture_decode_bias_property);
    EXPECT_NEAR(scale.x,  2.0f, 1e-5f);
    EXPECT_NEAR(bias.x,  -1.0f, 1e-5f);
}

TEST_F(Texture_inputs_import, the_writer_writes_the_wrap_modes_and_the_normal_decode)
{
    // The wrap modes and the normal decode are the UsdUVTexture's own
    // inputs, written for every bound texture: USD's fallbacks are not
    // erhe's, so a value left unwritten would not read back.
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_texture_input_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    const std::filesystem::path path = directory / "texture_inputs.usda";

    std::vector<erhe::usd::Usd_save_texture> textures;
    textures.push_back(
        erhe::usd::Usd_save_texture{
            .material_index = 0,
            .slot           = erhe::usd::Usd_material_texture_slot::base_color,
            .path           = directory / "grid.png",
            .srgb           = true
        }
    );
    textures.push_back(
        erhe::usd::Usd_save_texture{
            .material_index = 0,
            .slot           = erhe::usd::Usd_material_texture_slot::normal,
            .path           = directory / "grid.png",
            .srgb           = false
        }
    );
    const erhe::usd::Usd_save_arguments save_arguments{
        .path      = path,
        .root_node = root,
        .materials = result.data.materials,
        .textures  = textures
    };
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;

    std::ifstream     stream{path, std::ios::binary};
    const std::string written{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    EXPECT_NE(written.find("inputs:wrapS = \"repeat\""), std::string::npos) << written;
    EXPECT_NE(written.find("inputs:wrapT = \"mirror\""), std::string::npos) << written;
    EXPECT_NE(written.find("inputs:bias = (1, -1, 0, -1)"), std::string::npos) << written;
    EXPECT_NE(written.find("inputs:scale = (-2, 2, 1, 2)"), std::string::npos) << written;
}

// A texture packed inside a `.usdz` has no file of its own: the reader hands
// the archive entry's bytes over and the caller decodes those.
TEST(Usdz_packed_texture, the_archive_entrys_bytes_reach_the_image)
{
    const std::shared_ptr<erhe::scene::Node> root   = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_result         result = load(test_data_path("packed.usdz"), root);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.images.size(), 1u);
    const erhe::usd::Usd_image& image = result.data.images.front();
    ASSERT_FALSE(image.bytes.empty());
    // The PNG signature: the bytes are the image file, not the archive.
    const std::vector<std::uint8_t> signature{0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
    ASSERT_GE(image.bytes.size(), signature.size());
    EXPECT_TRUE(std::equal(signature.begin(), signature.end(), image.bytes.begin()));
}

} // namespace
