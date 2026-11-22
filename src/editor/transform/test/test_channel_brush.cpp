#include "tools/channel_brush.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using editor::apply_dab;
using editor::channel_brush_falloff;
using editor::Channel_brush_blend;
using editor::Channel_brush_settings;
using editor::Channel_brush_stroke;

constexpr float c_tolerance = 1.0e-6f;

// Vertices on the x axis at 0, 0.1, ..., 1.0.
[[nodiscard]] auto make_line_positions() -> std::vector<glm::vec3>
{
    std::vector<glm::vec3> positions;
    for (int i = 0; i <= 10; ++i) {
        positions.emplace_back(0.1f * static_cast<float>(i), 0.0f, 0.0f);
    }
    return positions;
}

[[nodiscard]] auto make_settings(const Channel_brush_blend blend, const float target, const float strength) -> Channel_brush_settings
{
    Channel_brush_settings settings;
    settings.radius       = 0.55f;
    settings.strength     = strength;
    settings.target       = glm::vec4{target};
    settings.blend        = blend;
    settings.channel_mask = editor::c_channel_mask_r;
    return settings;
}

[[nodiscard]] auto bits_equal(const float a, const float b) -> bool
{
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

TEST(Channel_brush, falloff_end_points)
{
    EXPECT_NEAR(channel_brush_falloff(0.0f, 2.0f), 1.0f, c_tolerance);
    EXPECT_EQ  (channel_brush_falloff(2.0f, 2.0f), 0.0f);
    EXPECT_EQ  (channel_brush_falloff(3.0f, 2.0f), 0.0f);
    EXPECT_NEAR(channel_brush_falloff(1.0f, 2.0f), 0.5f, c_tolerance); // 3p^2 - 2p^3 at p = 0.5
    EXPECT_EQ  (channel_brush_falloff(0.0f, 0.0f), 0.0f);
    float previous = 1.0f;
    for (int i = 1; i <= 20; ++i) {
        const float value = channel_brush_falloff(0.1f * static_cast<float>(i), 2.0f);
        EXPECT_LE(value, previous);
        previous = value;
    }
}

TEST(Channel_brush, repeated_dabs_converge_to_target_never_past_it)
{
    const std::vector<glm::vec3> positions = make_line_positions();
    std::vector<glm::vec4>       values(positions.size(), glm::vec4{0.2f});
    Channel_brush_stroke         stroke;
    stroke.begin(values);
    std::vector<std::uint32_t>   changed;
    const Channel_brush_settings settings = make_settings(Channel_brush_blend::mix, 0.8f, 0.5f);

    apply_dab(settings, stroke, glm::vec3{0.0f}, positions, values, changed);
    const std::vector<glm::vec4> after_first = values;
    EXPECT_FALSE(changed.empty());
    EXPECT_NEAR(values[0].x, 0.2f + (0.6f * 0.5f), c_tolerance);
    for (int repeat = 0; repeat < 10; ++repeat) {
        apply_dab(settings, stroke, glm::vec3{0.0f}, positions, values, changed);
        EXPECT_TRUE(changed.empty());
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        EXPECT_EQ(values[i], after_first[i]);
        EXPECT_LE(values[i].x, 0.8f + c_tolerance);
    }

    // Full strength reaches the target at the center and stays there.
    std::vector<glm::vec4> full(positions.size(), glm::vec4{0.2f});
    stroke.begin(full);
    const Channel_brush_settings full_settings = make_settings(Channel_brush_blend::mix, 0.8f, 1.0f);
    for (int repeat = 0; repeat < 5; ++repeat) {
        apply_dab(full_settings, stroke, glm::vec3{0.0f}, positions, full, changed);
        EXPECT_NEAR(full[0].x, 0.8f, c_tolerance);
    }
}

TEST(Channel_brush, overlapping_dabs_take_the_alpha_maximum)
{
    const std::vector<glm::vec3> positions = make_line_positions();
    std::vector<glm::vec4>       values(positions.size(), glm::vec4{0.0f});
    Channel_brush_stroke         stroke;
    stroke.begin(values);
    std::vector<std::uint32_t>   changed;
    const Channel_brush_settings settings = make_settings(Channel_brush_blend::mix, 1.0f, 1.0f);
    const glm::vec3 centers[] = { glm::vec3{0.2f, 0.0f, 0.0f}, glm::vec3{0.5f, 0.0f, 0.0f}, glm::vec3{0.3f, 0.0f, 0.0f} };
    for (const glm::vec3& center : centers) {
        apply_dab(settings, stroke, center, positions, values, changed);
    }
    for (std::size_t i = 0; i < positions.size(); ++i) {
        float alpha = 0.0f;
        for (const glm::vec3& center : centers) {
            alpha = std::max(alpha, channel_brush_falloff(glm::distance(positions[i], center), settings.radius));
        }
        EXPECT_NEAR(values[i].x, alpha, c_tolerance) << "vertex " << i;
    }
}

TEST(Channel_brush, blend_modes_and_clamp)
{
    const std::vector<glm::vec3> positions{glm::vec3{0.0f}};
    std::vector<std::uint32_t>   changed;
    Channel_brush_stroke         stroke;
    class Case { public: Channel_brush_blend blend; float before; float target; float strength; float expected; };
    const Case cases[] = {
        { Channel_brush_blend::mix,      0.2f, 0.6f, 0.5f, 0.4f },
        { Channel_brush_blend::add,      0.2f, 0.6f, 0.5f, 0.5f },
        { Channel_brush_blend::subtract, 0.7f, 0.6f, 0.5f, 0.4f },
        { Channel_brush_blend::add,      0.8f, 1.0f, 1.0f, 1.0f }, // clamped to 1
        { Channel_brush_blend::subtract, 0.3f, 1.0f, 1.0f, 0.0f }, // clamped to 0
        { Channel_brush_blend::mix,      0.5f, 2.0f, 1.0f, 1.0f }  // target above 1 clamps
    };
    for (const Case& c : cases) {
        std::vector<glm::vec4> values{glm::vec4{c.before}};
        stroke.begin(values);
        apply_dab(make_settings(c.blend, c.target, c.strength), stroke, glm::vec3{0.0f}, positions, values, changed);
        EXPECT_NEAR(values[0].x, c.expected, c_tolerance) << "blend " << static_cast<int>(c.blend);
        EXPECT_GE(values[0].x, 0.0f);
        EXPECT_LE(values[0].x, 1.0f);
    }
}

TEST(Channel_brush, channel_mask_leaves_other_channels_bit_identical)
{
    const std::vector<glm::vec3> positions = make_line_positions();
    std::vector<glm::vec4>       values;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        values.emplace_back(0.1f + 0.01f * static_cast<float>(i), 0.3f / 7.0f, 0.123456789f, 0.987654321f);
    }
    const std::vector<glm::vec4> before = values;
    Channel_brush_stroke         stroke;
    stroke.begin(values);
    std::vector<std::uint32_t>   changed;
    Channel_brush_settings settings = make_settings(Channel_brush_blend::mix, 1.0f, 1.0f);
    settings.channel_mask = editor::c_channel_mask_g;
    apply_dab(settings, stroke, glm::vec3{0.2f, 0.0f, 0.0f}, positions, values, changed);
    EXPECT_FALSE(changed.empty());
    for (std::size_t i = 0; i < values.size(); ++i) {
        EXPECT_TRUE(bits_equal(values[i].x, before[i].x));
        EXPECT_TRUE(bits_equal(values[i].z, before[i].z));
        EXPECT_TRUE(bits_equal(values[i].w, before[i].w));
    }
    EXPECT_NEAR(values[2].y, 1.0f, c_tolerance);
}

TEST(Channel_brush, vertices_outside_the_sphere_are_untouched)
{
    const std::vector<glm::vec3> positions = make_line_positions();
    std::vector<glm::vec4>       values(positions.size(), glm::vec4{0.25f});
    Channel_brush_stroke         stroke;
    stroke.begin(values);
    std::vector<std::uint32_t>   changed;
    const Channel_brush_settings settings = make_settings(Channel_brush_blend::mix, 1.0f, 1.0f);
    apply_dab(settings, stroke, glm::vec3{0.0f}, positions, values, changed);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const bool inside = glm::distance(positions[i], glm::vec3{0.0f}) < settings.radius;
        if (inside) {
            EXPECT_NE(values[i].x, 0.25f) << "vertex " << i;
        } else {
            EXPECT_TRUE(bits_equal(values[i].x, 0.25f)) << "vertex " << i;
        }
        const bool reported = std::find(changed.begin(), changed.end(), static_cast<std::uint32_t>(i)) != changed.end();
        EXPECT_EQ(reported, inside) << "vertex " << i;
    }
}

} // anonymous namespace
