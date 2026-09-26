#include "test_temporary_directory.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include "spline-eval.hh"

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
    const std::filesystem::path directory = erhe_usd_test::process_temporary_directory() / "erhe_usd_attribute_spline_tests";
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

// The `inputs:intensity` spline of `attribute_splines.usda`, spelled the way
// the LightUSD evaluator reads it: the tangent slopes are value units per time
// code, which is what the reader turns into erhe's per-second tangents.
[[nodiscard]] auto make_reference_intensity_spline() -> lightusd::Spline<double>
{
    lightusd::Spline<double> spline;
    spline.curveType        = lightusd::SplineCurveType::Hermite;
    spline.preExtrapolation = lightusd::SplineExtrapolationMode::Held;
    spline.postExtrapolation = lightusd::SplineExtrapolationMode::Held;

    lightusd::SplineKnot<double> knot0;
    knot0.time                  = 0.0;
    knot0.value                 = 100.0;
    knot0.postTangentSlope      = 8.0;
    knot0.nextInterpolationMode = lightusd::SplineInterpolationMode::Curve;

    lightusd::SplineKnot<double> knot1;
    knot1.time                  = 12.0;
    knot1.value                 = 200.0;
    knot1.preTangentSlope       = 4.0;
    knot1.postTangentSlope      = 4.0;
    knot1.nextInterpolationMode = lightusd::SplineInterpolationMode::Curve;

    lightusd::SplineKnot<double> knot2;
    knot2.time                  = 24.0;
    knot2.value                 = 150.0;
    knot2.preTangentSlope       = -2.0;
    knot2.nextInterpolationMode = lightusd::SplineInterpolationMode::Curve;

    spline.knots.push_back(knot0);
    spline.knots.push_back(knot1);
    spline.knots.push_back(knot2);
    return spline;
}

// `attribute_splines.usda`: a sphere light whose `inputs:intensity` is a
// three-knot curve spline, and a UsdPreviewSurface whose `inputs:roughness` is
// a two-knot curve spline, whose `inputs:metallic` is a spline of linear
// segments alone, whose `inputs:opacity` is a spline of held segments alone,
// and whose `inputs:clearcoat` - an input erhe carries no channel for - has a
// spline of its own.
class Attribute_splines : public testing::Test
{
protected:
    void SetUp() override
    {
        source_path = test_data_path("attribute_splines.usda");
        load(source_path);
    }

    void load(const std::filesystem::path& path)
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = path,
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

// A curve spline becomes a cubic sampler whose keys are [in tangent, value,
// out tangent] in seconds and per-second tangents, read at a value offset of
// one value the way a glTF cubic channel is.
TEST_F(Attribute_splines, a_curve_spline_becomes_a_cubic_sampler)
{
    const erhe::scene::Animation_channel* channel =
        find_channel(animation(), erhe::scene::Light::intensity_property.get_ptr(), "key");
    ASSERT_NE(channel, nullptr);
    EXPECT_EQ(channel->value_offset, 1u);

    const erhe::scene::Animation_sampler* intensity =
        find_sampler(erhe::scene::Light::intensity_property.get_ptr(), "key");
    ASSERT_NE(intensity, nullptr);
    EXPECT_EQ(intensity->interpolation_mode, erhe::scene::Animation_interpolation_mode::CUBICSPLINE);
    ASSERT_EQ(intensity->timestamps.size(), 3u);
    ASSERT_EQ(intensity->data.size(),       9u);

    EXPECT_FLOAT_EQ(intensity->timestamps[0], 0.0f);
    EXPECT_FLOAT_EQ(intensity->timestamps[1], 0.5f);
    EXPECT_FLOAT_EQ(intensity->timestamps[2], 1.0f);

    // [in tangent, value, out tangent] per key; a slope of 8 per time code at
    // 24 time codes per second is 192 per second.
    EXPECT_FLOAT_EQ(intensity->data[0],   0.0f);
    EXPECT_FLOAT_EQ(intensity->data[1], 100.0f);
    EXPECT_FLOAT_EQ(intensity->data[2], 192.0f);
    EXPECT_FLOAT_EQ(intensity->data[3],  96.0f);
    EXPECT_FLOAT_EQ(intensity->data[4], 200.0f);
    EXPECT_FLOAT_EQ(intensity->data[5],  96.0f);
    EXPECT_FLOAT_EQ(intensity->data[6], -48.0f);
    EXPECT_FLOAT_EQ(intensity->data[7], 150.0f);
    EXPECT_FLOAT_EQ(intensity->data[8],   0.0f);
}

// The erhe sampler and the USD evaluator say the same thing inside the
// segments, which is what the per-second tangent conversion has to get right.
TEST_F(Attribute_splines, the_cubic_sampler_agrees_with_the_usd_evaluator)
{
    erhe::scene::Animation_sampler* intensity =
        find_sampler(erhe::scene::Light::intensity_property.get_ptr(), "key");
    ASSERT_NE(intensity, nullptr);
    const erhe::scene::Animation_channel* found =
        find_channel(animation(), erhe::scene::Light::intensity_property.get_ptr(), "key");
    ASSERT_NE(found, nullptr);

    const lightusd::Spline<double> reference = make_reference_intensity_spline();
    constexpr double               time_codes_per_second = 24.0;
    const std::vector<double>      time_codes{3.0, 12.0, 19.5};
    for (const double time_code : time_codes) {
        double expected{0.0};
        ASSERT_TRUE(lightusd::EvaluateSpline(reference, time_code, &expected));
        erhe::scene::Animation_channel channel = *found;
        const glm::vec4 value = intensity->evaluate(channel, static_cast<float>(time_code / time_codes_per_second));
        EXPECT_NEAR(static_cast<double>(value.x), expected, 1e-4 * std::max(1.0, std::abs(expected)))
            << "at time code " << time_code;
    }
}

// A spline of linear segments alone is exactly a linear sampler and one of
// held segments alone exactly a step sampler, both of which key the plain
// value.
TEST_F(Attribute_splines, linear_and_held_splines_become_plain_samplers)
{
    const erhe::scene::Animation_sampler* metallic =
        find_sampler(erhe::primitive::Material::metallic_property.get_ptr(), "surf");
    ASSERT_NE(metallic, nullptr);
    EXPECT_EQ(metallic->interpolation_mode, erhe::scene::Animation_interpolation_mode::LINEAR);
    ASSERT_EQ(metallic->data.size(), 2u);
    EXPECT_FLOAT_EQ(metallic->data[0], 0.0f);
    EXPECT_FLOAT_EQ(metallic->data[1], 1.0f);

    const erhe::scene::Animation_sampler* opacity =
        find_sampler(erhe::primitive::Material::opacity_property.get_ptr(), "surf");
    ASSERT_NE(opacity, nullptr);
    EXPECT_EQ(opacity->interpolation_mode, erhe::scene::Animation_interpolation_mode::STEP);
    ASSERT_EQ(opacity->data.size(), 2u);
    EXPECT_FLOAT_EQ(opacity->data[0], 1.0f);
    EXPECT_FLOAT_EQ(opacity->data[1], 0.5f);

    // erhe's roughness is a vec2, so a scalar knot fills both components and
    // both tangents.
    const erhe::scene::Animation_sampler* roughness =
        find_sampler(erhe::primitive::Material::roughness_property.get_ptr(), "surf");
    ASSERT_NE(roughness, nullptr);
    EXPECT_EQ(roughness->interpolation_mode, erhe::scene::Animation_interpolation_mode::CUBICSPLINE);
    ASSERT_EQ(roughness->data.size(), 12u);
    EXPECT_FLOAT_EQ(roughness->data[2], 0.25f);
    EXPECT_FLOAT_EQ(roughness->data[3], 0.25f);
    EXPECT_FLOAT_EQ(roughness->data[4], 0.375f);
    EXPECT_FLOAT_EQ(roughness->data[5], 0.375f);
}

// The spline of an attribute erhe carries no channel for drives nothing, so
// the file's four carried attributes are the whole animation.
TEST_F(Attribute_splines, a_spline_erhe_does_not_carry_contributes_no_channel)
{
    EXPECT_EQ(animation().channels.size(), 4u);
    EXPECT_NE(find_channel(animation(), erhe::scene::Light::intensity_property.get_ptr(),        "key"),  nullptr);
    EXPECT_NE(find_channel(animation(), erhe::primitive::Material::roughness_property.get_ptr(), "surf"), nullptr);
    EXPECT_NE(find_channel(animation(), erhe::primitive::Material::metallic_property.get_ptr(),  "surf"), nullptr);
    EXPECT_NE(find_channel(animation(), erhe::primitive::Material::opacity_property.get_ptr(),   "surf"), nullptr);
}

// A save writes the cubic channels back as `.spline` blocks and the plain ones
// as time samples, and the knots extend the layer's time code range.
TEST_F(Attribute_splines, a_save_writes_the_spline_and_the_time_code_metadata)
{
    const std::filesystem::path path = save("attribute_splines_saved.usda");
    EXPECT_TRUE(file_holds(path, "timeCodesPerSecond = 24"));
    EXPECT_TRUE(file_holds(path, "startTimeCode = 0"));
    EXPECT_TRUE(file_holds(path, "endTimeCode = 24"));
    EXPECT_TRUE(file_holds(path, "float inputs:intensity.spline = {"));
    EXPECT_TRUE(file_holds(path, "float inputs:roughness.spline = {"));
    EXPECT_TRUE(file_holds(path, "hermite,"));
    EXPECT_TRUE(file_holds(path, "0: 100; post curve (8)"));
    // A linear or held spline reloads as a plain sampler, which a save writes
    // as time samples: the same values, spelled the way erhe keys them.
    EXPECT_TRUE(file_holds(path, "float inputs:metallic.timeSamples = {"));
    EXPECT_TRUE(file_holds(path, "float inputs:opacity.timeSamples = {"));
    EXPECT_FALSE(file_holds(path, "inputs:clearcoat.spline"));
}

// The save is a fixed point: reloading it gives the same clip, and saving that
// reproduces the file byte for byte.
TEST_F(Attribute_splines, a_second_save_is_byte_identical)
{
    const std::filesystem::path first = save("attribute_splines_first.usda");
    load(first);
    const erhe::scene::Animation_sampler* intensity =
        find_sampler(erhe::scene::Light::intensity_property.get_ptr(), "key");
    ASSERT_NE(intensity, nullptr);
    EXPECT_EQ(intensity->interpolation_mode, erhe::scene::Animation_interpolation_mode::CUBICSPLINE);
    const std::filesystem::path second = save("attribute_splines_second.usda");
    EXPECT_EQ(read_lines(first), read_lines(second));
}

// An edited key travels back into the spline: the knots are always derived
// from the clip's keys, no authored record standing in.
TEST_F(Attribute_splines, an_edited_key_is_written_back)
{
    erhe::scene::Animation_sampler* intensity =
        find_sampler(erhe::scene::Light::intensity_property.get_ptr(), "key");
    ASSERT_NE(intensity, nullptr);
    ASSERT_EQ(intensity->data.size(), 9u);
    intensity->data[4] = 900.0f;

    const std::filesystem::path path = save("attribute_splines_edited.usda");
    EXPECT_TRUE(file_holds(path, "12: 900"));
}

} // namespace
