// Texture coordinates across USD's `st` space and erhe's, the per-channel
// output a scalar UsdPreviewSurface input is connected through, and the
// schema fallback of an unauthored input (doc/usd_compatibility.md,
// Materials and Geometry attributes; src/erhe/usd/notes.md).

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

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
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_channel_tests";
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

// The `primvars:st` line of the first mesh in a written layer, so a round
// trip can be compared as the text the file holds.
[[nodiscard]] auto first_st_line(const std::filesystem::path& path) -> std::string
{
    std::ifstream stream{path};
    std::string   line;
    while (std::getline(stream, line)) {
        if (line.find("primvars:st = ") != std::string::npos) {
            return line;
        }
    }
    return {};
}

} // anonymous namespace

class Texture_channels_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("texture_channels.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
        channels  = material_at(root, "root/materials/Channels");
        fallbacks = material_at(root, "root/materials/Fallbacks");
        ASSERT_NE(channels,  nullptr);
        ASSERT_NE(fallbacks, nullptr);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
    erhe::primitive::Material*         channels {nullptr};
    erhe::primitive::Material*         fallbacks{nullptr};
};

TEST_F(Texture_channels_import, a_named_output_becomes_the_slot_channel)
{
    EXPECT_EQ(channels->get_roughness_channel(), erhe::primitive::Texture_channel::r);
    EXPECT_EQ(channels->get_metallic_channel(),  erhe::primitive::Texture_channel::a);
    EXPECT_EQ(channels->get_occlusion_channel(), erhe::primitive::Texture_channel::g);
}

TEST_F(Texture_channels_import, a_gltf_packed_material_keeps_the_defaults)
{
    // The Fallbacks material connects nothing, so every channel stays at the
    // erhe default, which is glTF's packing.
    EXPECT_EQ(fallbacks->get_roughness_channel(), erhe::primitive::Texture_channel::g);
    EXPECT_EQ(fallbacks->get_metallic_channel(),  erhe::primitive::Texture_channel::b);
    EXPECT_EQ(fallbacks->get_occlusion_channel(), erhe::primitive::Texture_channel::r);
    EXPECT_EQ(fallbacks->get_opacity_channel(),   erhe::primitive::Texture_channel::a);
}

TEST_F(Texture_channels_import, an_unauthored_diffuse_color_is_the_schema_fallback)
{
    // I2: an unauthored UsdPreviewSurface input is the schema fallback, and
    // diffuseColor's is the 0.18 grey - not erhe's white default - so the
    // importer writes it as a local value.
    const glm::vec3 base_color = fallbacks->get_value(erhe::primitive::Material::base_color_property);
    EXPECT_NEAR(base_color.x, 0.18f, 1e-5f);
    EXPECT_NEAR(base_color.y, 0.18f, 1e-5f);
    EXPECT_NEAR(base_color.z, 0.18f, 1e-5f);
    EXPECT_TRUE(fallbacks->has_local_value(erhe::primitive::Material::base_color_property.get()));
    // An input whose USD fallback is the erhe default writes nothing.
    EXPECT_FALSE(fallbacks->has_local_value(erhe::primitive::Material::metallic_property.get()));
}

TEST_F(Texture_channels_import, st_is_read_into_erhe_texcoord_space)
{
    // USD's `st` origin is the image's bottom-left corner, erhe's the
    // top-left one: the authored v of 0 and 0.25 read back as 1 and 0.75.
    ASSERT_FALSE(result.data.meshes.empty());
    const std::shared_ptr<erhe::scene::Mesh>& mesh = result.data.meshes.front();
    ASSERT_FALSE(mesh->get_primitives().empty());
    const std::shared_ptr<erhe::geometry::Geometry> geometry = mesh->get_primitives().front().primitive->render_shape->get_geometry_const();
    ASSERT_TRUE(geometry.operator bool());
    const GEO::Mesh& geo_mesh = geometry->get_mesh();
    ASSERT_GT(geo_mesh.facets.nb(), 0u);
    std::vector<float> v_values;
    for (GEO::index_t corner = geo_mesh.facets.corners_begin(0); corner < geo_mesh.facets.corners_end(0); ++corner) {
        const std::optional<GEO::vec2f> uv = geometry->get_attributes().corner_texcoord_0.try_get(corner);
        ASSERT_TRUE(uv.has_value());
        v_values.push_back(uv.value().y);
    }
    ASSERT_EQ(v_values.size(), 4u);
    EXPECT_NEAR(v_values[0], 1.0f,  1e-5f);
    EXPECT_NEAR(v_values[1], 1.0f,  1e-5f);
    EXPECT_NEAR(v_values[2], 0.75f, 1e-5f);
    EXPECT_NEAR(v_values[3], 0.75f, 1e-5f);
}

// Save and reload: what the writer has to put back the way it found it.
class Texture_channels_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        source      = load(test_data_path("texture_channels.usda"), source_root);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("texture_channels.usda");
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

TEST_F(Texture_channels_round_trip, st_is_written_back_byte_for_byte)
{
    EXPECT_EQ(first_st_line(written_path), first_st_line(test_data_path("texture_channels.usda")));
}

TEST_F(Texture_channels_round_trip, the_channels_are_written_back)
{
    erhe::primitive::Material* channels = material_at(reloaded_root, "root/materials/Channels");
    ASSERT_NE(channels, nullptr);
    EXPECT_EQ(channels->get_roughness_channel(), erhe::primitive::Texture_channel::r);
    EXPECT_EQ(channels->get_metallic_channel(),  erhe::primitive::Texture_channel::a);
    EXPECT_EQ(channels->get_occlusion_channel(), erhe::primitive::Texture_channel::g);
}

TEST_F(Texture_channels_round_trip, the_diffuse_color_fallback_reproduces_itself)
{
    erhe::primitive::Material* fallbacks = material_at(reloaded_root, "root/materials/Fallbacks");
    ASSERT_NE(fallbacks, nullptr);
    const glm::vec3 base_color = fallbacks->get_value(erhe::primitive::Material::base_color_property);
    EXPECT_NEAR(base_color.x, 0.18f, 1e-5f);
    EXPECT_NEAR(base_color.y, 0.18f, 1e-5f);
    EXPECT_NEAR(base_color.z, 0.18f, 1e-5f);
}

TEST(Uv_transform_conversion, the_flip_is_its_own_inverse)
{
    const glm::vec2 uv{0.25f, 0.75f};
    EXPECT_EQ(erhe::usd::flip_texcoord_v(uv), glm::vec2(0.25f, 0.25f));
    EXPECT_EQ(erhe::usd::flip_texcoord_v(erhe::usd::flip_texcoord_v(uv)), uv);
}

TEST(Uv_transform_conversion, the_identity_maps_onto_the_identity)
{
    const erhe::usd::Erhe_uv_transform erhe_transform = erhe::usd::to_erhe_uv_transform(erhe::usd::Usd_uv_transform_2d{});
    EXPECT_NEAR(erhe_transform.rotation, 0.0f, 1e-5f);
    EXPECT_EQ(erhe_transform.scale,  glm::vec2(1.0f, 1.0f));
    EXPECT_EQ(erhe_transform.offset, glm::vec2(0.0f, 0.0f));
}

TEST(Uv_transform_conversion, a_transform2d_round_trips)
{
    const erhe::usd::Usd_uv_transform_2d usd{
        .rotation_degrees = 90.0f,
        .scale            = glm::vec2{2.0f, 3.0f},
        .translation      = glm::vec2{0.25f, 0.5f}
    };
    const erhe::usd::Erhe_uv_transform erhe_transform = erhe::usd::to_erhe_uv_transform(usd);
    // (tx - sin(r) * sy, 1 - ty - cos(r) * sy) with r = 90 degrees.
    EXPECT_NEAR(erhe_transform.rotation,  glm::radians(-90.0f), 1e-5f);
    EXPECT_NEAR(erhe_transform.scale.x,   2.0f,  1e-5f);
    EXPECT_NEAR(erhe_transform.scale.y,   3.0f,  1e-5f);
    EXPECT_NEAR(erhe_transform.offset.x, -2.75f, 1e-5f);
    EXPECT_NEAR(erhe_transform.offset.y,  0.5f,  1e-5f);

    const erhe::usd::Usd_uv_transform_2d back = erhe::usd::to_usd_uv_transform_2d(erhe_transform);
    EXPECT_NEAR(back.rotation_degrees, usd.rotation_degrees, 1e-3f);
    EXPECT_NEAR(back.scale.x,          usd.scale.x,          1e-5f);
    EXPECT_NEAR(back.scale.y,          usd.scale.y,          1e-5f);
    EXPECT_NEAR(back.translation.x,    usd.translation.x,    1e-5f);
    EXPECT_NEAR(back.translation.y,    usd.translation.y,    1e-5f);
}

// The placement a UsdPreviewSurface texture ends up with, checked against the
// reference renderer's own formula. usdview (Storm) computes the sampled
// coordinate as `translation + R(rotation) * (scale * st)` with R the
// counter-clockwise rotation (pxr/imaging/hdSt/codeGen.cpp, the
// UsdTransform2d code generator). erhe reads `st` through the V flip and lets
// Material_buffer build `M = R(rotation) * S(scale)`, which the fragment
// shader applies as `M * texcoord + offset` (res/shaders/erhe_texture.glsl).
// The two agree exactly when to_erhe_uv_transform() converts through the flip,
// which is what these cases pin: every one of them is a material of the USD
// working group's TextureTransformTest asset.
namespace {

[[nodiscard]] auto usdview_placement(const erhe::usd::Usd_uv_transform_2d& usd, const glm::vec2 st) -> glm::vec2
{
    const float     rotation = glm::radians(usd.rotation_degrees);
    const float     c        = std::cos(rotation);
    const float     s        = std::sin(rotation);
    const glm::vec2 scaled{usd.scale.x * st.x, usd.scale.y * st.y};
    return usd.translation + glm::vec2{(c * scaled.x) - (s * scaled.y), (s * scaled.x) + (c * scaled.y)};
}

// What Material_buffer packs and erhe_texture.glsl applies, on an erhe
// texcoord.
[[nodiscard]] auto erhe_placement(const erhe::usd::Erhe_uv_transform& erhe_transform, const glm::vec2 uv) -> glm::vec2
{
    const float     c = std::cos(erhe_transform.rotation);
    const float     s = std::sin(erhe_transform.rotation);
    const glm::mat2 rotation{c, s, -s, c};
    const glm::mat2 scale{erhe_transform.scale.x, 0.0f, 0.0f, erhe_transform.scale.y};
    return ((rotation * scale) * uv) + erhe_transform.offset;
}

class Transform_2d_case
{
public:
    const char*                    name;
    erhe::usd::Usd_uv_transform_2d usd;
};

} // anonymous namespace

TEST(Uv_transform_placement, every_texture_transform_test_material_lands_where_usdview_puts_it)
{
    const Transform_2d_case cases[] = {
        {"Offset_U",  {.rotation_degrees =  0.0f,       .scale = {1.0f, 1.0f}, .translation = { 0.5f,        0.0f       }}},
        {"Offset_V",  {.rotation_degrees =  0.0f,       .scale = {1.0f, 1.0f}, .translation = { 0.0f,       -0.5f       }}},
        {"Offset_UV", {.rotation_degrees =  0.0f,       .scale = {1.0f, 1.0f}, .translation = { 0.5f,       -0.5f       }}},
        {"Rotation",  {.rotation_degrees = 22.5f,       .scale = {1.0f, 1.0f}, .translation = { 0.38268343f, 0.076120496f}}},
        {"Scale",     {.rotation_degrees =  0.0f,       .scale = {1.5f, 1.5f}, .translation = { 0.0f,       -0.5f       }}},
        {"All",       {.rotation_degrees = 17.188734f,  .scale = {1.5f, 1.5f}, .translation = { 0.24328034f, -0.33300462f}}}
    };
    const glm::vec2 st_points[] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {0.5f, 0.5f}, {0.25f, 0.75f}
    };
    for (const Transform_2d_case& transform_case : cases) {
        const erhe::usd::Erhe_uv_transform erhe_transform = erhe::usd::to_erhe_uv_transform(transform_case.usd);
        for (const glm::vec2 st : st_points) {
            const glm::vec2 expected = erhe::usd::flip_texcoord_v(usdview_placement(transform_case.usd, st));
            const glm::vec2 actual   = erhe_placement(erhe_transform, erhe::usd::flip_texcoord_v(st));
            EXPECT_NEAR(actual.x, expected.x, 1e-5f) << transform_case.name << " at st (" << st.x << ", " << st.y << ")";
            EXPECT_NEAR(actual.y, expected.y, 1e-5f) << transform_case.name << " at st (" << st.x << ", " << st.y << ")";
        }
    }
}
