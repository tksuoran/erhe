#include "test_temporary_directory.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

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
    const std::filesystem::path directory = erhe_usd_test::process_temporary_directory() / "erhe_usd_attribute_sample_tests";
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
        if (!line.empty() && (line.back() == '\r')) {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

[[nodiscard]] auto file_holds(const std::filesystem::path& path, const std::string& text) -> bool
{
    for (const std::string& line : read_lines(path)) {
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto find_channel(
    const erhe::scene::Animation&              animation,
    const erhe::property::Dependency_property* property,
    const std::string&                         target_name
) -> const erhe::scene::Animation_channel*
{
    for (const erhe::scene::Animation_channel& channel : animation.channels) {
        if ((channel.property == property) && channel.target && (channel.target->get_name() == target_name)) {
            return &channel;
        }
    }
    return nullptr;
}

// `attribute_samples.usda`: a mesh whose `visibility` is time-sampled, a
// sphere light whose `inputs:intensity` and `inputs:color` are, and a
// UsdPreviewSurface whose `inputs:diffuseColor` and `inputs:roughness` are.
class Attribute_samples : public testing::Test
{
protected:
    void SetUp() override
    {
        source_path = test_data_path("attribute_samples.usda");
        root        = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = source_path,
            .root_node     = root,
            .mesh_layer_id = 0
        };
        loaded = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(loaded.error.empty()) << loaded.error;
        ASSERT_EQ(loaded.data.animations.size(), 1u);
        ASSERT_TRUE(loaded.data.animations.front());
    }

    [[nodiscard]] auto save(const char* file_name) -> std::filesystem::path
    {
        const std::filesystem::path path = temporary_path(file_name);
        erhe::usd::Usd_save_arguments save_arguments{
            .path      = path,
            .root_node = root
        };
        save_arguments.time_codes_per_second = loaded.data.time_codes.time_codes_per_second;
        save_arguments.animations            = loaded.data.animations;
        const erhe::usd::Usd_save_result save_result = erhe::usd::save_usda(save_arguments);
        EXPECT_TRUE(save_result.error.empty()) << save_result.error;
        return path;
    }

    [[nodiscard]] auto animation() -> erhe::scene::Animation&
    {
        return *loaded.data.animations.front();
    }

    [[nodiscard]] auto find_light(const std::string& name) -> std::shared_ptr<erhe::scene::Light>
    {
        for (const std::shared_ptr<erhe::scene::Light>& light : loaded.data.lights) {
            if (light && (light->get_name() == name)) {
                return light;
            }
        }
        return {};
    }

    [[nodiscard]] auto find_material(const std::string& name) -> std::shared_ptr<erhe::primitive::Material>
    {
        for (const std::shared_ptr<erhe::primitive::Material>& material : loaded.data.materials) {
            if (material && (material->get_name() == name)) {
                return material;
            }
        }
        return {};
    }

    [[nodiscard]] auto find_node(const std::string& name) -> std::shared_ptr<erhe::scene::Node>
    {
        for (const std::shared_ptr<erhe::scene::Node>& node : loaded.data.nodes) {
            if (node && (node->get_name() == name)) {
                return node;
            }
        }
        return {};
    }

    [[nodiscard]] auto find_sampler(
        const erhe::property::Dependency_property* property,
        const std::string&                         target_name
    ) -> erhe::scene::Animation_sampler*
    {
        const erhe::scene::Animation_channel* channel = find_channel(animation(), property, target_name);
        if ((channel == nullptr) || (channel->sampler_index >= animation().samplers.size())) {
            return nullptr;
        }
        return &animation().samplers[channel->sampler_index];
    }

    std::filesystem::path              source_path;
    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         loaded;
};

TEST_F(Attribute_samples, one_channel_per_sampled_attribute)
{
    EXPECT_EQ(animation().channels.size(), 5u);
    EXPECT_NE(find_channel(animation(), erhe::Item_base::visible_property.get_ptr(),              "panel"), nullptr);
    EXPECT_NE(find_channel(animation(), erhe::scene::Light::intensity_property.get_ptr(),         "key"),   nullptr);
    EXPECT_NE(find_channel(animation(), erhe::scene::Light::color_property.get_ptr(),             "key"),   nullptr);
    EXPECT_NE(find_channel(animation(), erhe::primitive::Material::base_color_property.get_ptr(), "surf"),  nullptr);
    EXPECT_NE(find_channel(animation(), erhe::primitive::Material::roughness_property.get_ptr(),  "surf"),  nullptr);
}

// The keys are in seconds - time code / timeCodesPerSecond - the way the
// sampled xformOp channels are.
TEST_F(Attribute_samples, keys_are_in_seconds_and_carry_the_sampled_values)
{
    const erhe::scene::Animation_sampler* intensity = find_sampler(erhe::scene::Light::intensity_property.get_ptr(), "key");
    ASSERT_NE(intensity, nullptr);
    ASSERT_EQ(intensity->timestamps.size(), 2u);
    ASSERT_EQ(intensity->data.size(),       2u);
    EXPECT_FLOAT_EQ(intensity->timestamps[0], 0.0f);
    EXPECT_FLOAT_EQ(intensity->timestamps[1], 1.0f);
    EXPECT_FLOAT_EQ(intensity->data[0], 100.0f);
    EXPECT_FLOAT_EQ(intensity->data[1], 300.0f);

    const erhe::scene::Animation_sampler* color = find_sampler(erhe::scene::Light::color_property.get_ptr(), "key");
    ASSERT_NE(color, nullptr);
    ASSERT_EQ(color->data.size(), 6u);
    EXPECT_FLOAT_EQ(color->data[3], 0.0f);
    EXPECT_FLOAT_EQ(color->data[4], 1.0f);
    EXPECT_FLOAT_EQ(color->data[5], 0.0f);

    // erhe's roughness is a vec2; a UsdPreviewSurface scalar fills both.
    const erhe::scene::Animation_sampler* roughness = find_sampler(erhe::primitive::Material::roughness_property.get_ptr(), "surf");
    ASSERT_NE(roughness, nullptr);
    ASSERT_EQ(roughness->data.size(), 4u);
    EXPECT_FLOAT_EQ(roughness->data[0], 0.25f);
    EXPECT_FLOAT_EQ(roughness->data[1], 0.25f);
    EXPECT_FLOAT_EQ(roughness->data[2], 0.75f);
    EXPECT_FLOAT_EQ(roughness->data[3], 0.75f);

    // `visibility` is a token; the channel drives the boolean `visible`.
    const erhe::scene::Animation_sampler* visible = find_sampler(erhe::Item_base::visible_property.get_ptr(), "panel");
    ASSERT_NE(visible, nullptr);
    ASSERT_EQ(visible->data.size(), 3u);
    EXPECT_FLOAT_EQ(visible->data[0], 1.0f);
    EXPECT_FLOAT_EQ(visible->data[1], 0.0f);
    EXPECT_FLOAT_EQ(visible->data[2], 1.0f);
    EXPECT_FLOAT_EQ(visible->timestamps[1], 0.5f);
}

// The static value an item holds is the pose at the stage's start time code,
// which is the reference frame of the clip.
TEST_F(Attribute_samples, static_values_are_the_pose_at_the_start_time_code)
{
    const std::shared_ptr<erhe::scene::Light> light = find_light("key");
    ASSERT_TRUE(light);
    EXPECT_FLOAT_EQ(light->get_intensity(), 100.0f);
    EXPECT_EQ(light->get_color(), glm::vec3(1.0f, 1.0f, 1.0f));

    const std::shared_ptr<erhe::primitive::Material> material = find_material("surf");
    ASSERT_TRUE(material);
    EXPECT_EQ(material->get_base_color(), glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(material->get_roughness().x, 0.25f);

    const std::shared_ptr<erhe::scene::Node> panel = find_node("panel");
    ASSERT_TRUE(panel);
    EXPECT_TRUE(panel->get_value(erhe::Item_base::visible_property));
}

// Applying the clip writes the animated layer; clear_applied() puts every
// target back on the value it authored.
TEST_F(Attribute_samples, applying_the_clip_poses_the_targets)
{
    const std::shared_ptr<erhe::scene::Light>       light    = find_light("key");
    const std::shared_ptr<erhe::primitive::Material> material = find_material("surf");
    const std::shared_ptr<erhe::scene::Node>        panel    = find_node("panel");
    ASSERT_TRUE(light);
    ASSERT_TRUE(material);
    ASSERT_TRUE(panel);

    animation().apply(0.5f);
    EXPECT_NEAR(light->get_intensity(), 200.0f, 1e-3f);
    EXPECT_NEAR(light->get_color().r,     0.5f, 1e-5f);
    EXPECT_NEAR(light->get_color().g,     1.0f, 1e-5f);
    EXPECT_NEAR(material->get_base_color().r, 0.5f, 1e-5f);
    EXPECT_NEAR(material->get_base_color().b, 0.5f, 1e-5f);
    EXPECT_NEAR(material->get_roughness().x,  0.5f, 1e-5f);
    // A boolean has nothing between two keys, so it holds the previous key.
    EXPECT_FALSE(panel->get_value(erhe::Item_base::visible_property));

    animation().clear_applied();
    EXPECT_FLOAT_EQ(light->get_intensity(), 100.0f);
    EXPECT_EQ(material->get_base_color(), glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(panel->get_value(erhe::Item_base::visible_property));
}

TEST_F(Attribute_samples, a_save_writes_the_samples_and_the_time_code_metadata)
{
    const std::filesystem::path path = save("attribute_samples_saved.usda");
    EXPECT_TRUE(file_holds(path, "timeCodesPerSecond = 24"));
    EXPECT_TRUE(file_holds(path, "startTimeCode = 0"));
    EXPECT_TRUE(file_holds(path, "endTimeCode = 24"));
    EXPECT_TRUE(file_holds(path, "float inputs:intensity.timeSamples = {"));
    EXPECT_TRUE(file_holds(path, "color3f inputs:color.timeSamples = {"));
    EXPECT_TRUE(file_holds(path, "color3f inputs:diffuseColor.timeSamples = {"));
    EXPECT_TRUE(file_holds(path, "float inputs:roughness.timeSamples = {"));
    EXPECT_TRUE(file_holds(path, "token visibility.timeSamples = {"));
}

// The fixture is written in the writer's own spelling, so a save of what was
// loaded reproduces the file it was loaded from and a second save reproduces
// the first.
TEST_F(Attribute_samples, a_second_save_is_byte_identical)
{
    const std::filesystem::path first  = save("attribute_samples_first.usda");
    const std::filesystem::path second = save("attribute_samples_second.usda");
    EXPECT_EQ(read_lines(first), read_lines(second));
    EXPECT_EQ(read_lines(first), read_lines(source_path));
}

// An edited key travels back into the attribute's time samples: the samples
// are always derived from the clip's keys, no authored record standing in.
TEST_F(Attribute_samples, an_edited_key_is_written_back)
{
    erhe::scene::Animation_sampler* intensity = find_sampler(erhe::scene::Light::intensity_property.get_ptr(), "key");
    ASSERT_NE(intensity, nullptr);
    ASSERT_EQ(intensity->data.size(), 2u);
    intensity->data[1] = 900.0f;

    const std::filesystem::path path = save("attribute_samples_edited.usda");
    EXPECT_TRUE(file_holds(path, "24: 900"));
}

} // namespace
