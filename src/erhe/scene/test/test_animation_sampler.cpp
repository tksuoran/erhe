// Animation_sampler::evaluate() against the glTF 2.0 interpolation rules.
//
// These cover the three sampler interpolation modes and the malformed-input
// paths. The reference behavior is the glTF specification (section
// "Animations", appendix C "Spline Interpolation") as implemented by the
// Khronos glTF-Sample-Renderer.

#include "erhe_scene/animation.hpp"
#include "erhe_scene/node.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace {

constexpr float tol = 1e-4f;

[[nodiscard]] auto make_channel(const erhe::scene::Animation_path path, const std::size_t value_offset)
    -> erhe::scene::Animation_channel
{
    erhe::scene::Animation_channel channel{};
    channel.path           = path;
    channel.sampler_index  = 0;
    channel.target         = {};
    channel.start_position = 0;
    channel.value_offset   = value_offset;
    return channel;
}

} // anonymous namespace

// ============================================================================
// STEP
// ============================================================================

// STEP holds the value of the preceding keyframe for the whole span; it must
// not be interpolated toward the next one.
TEST(animation_sampler, step_holds_the_previous_keyframe)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::STEP};
    sampler.set(
        std::vector<float>{0.0f, 1.0f, 2.0f},
        std::vector<float>{
             0.0f, 0.0f, 0.0f,
            10.0f, 0.0f, 0.0f,
            20.0f, 0.0f, 0.0f
        }
    );
    erhe::scene::Animation_channel channel = make_channel(erhe::scene::Animation_path::TRANSLATION, 0);

    EXPECT_NEAR(sampler.evaluate(channel, 0.00f).x,  0.0f, tol);
    EXPECT_NEAR(sampler.evaluate(channel, 0.25f).x,  0.0f, tol);
    EXPECT_NEAR(sampler.evaluate(channel, 0.99f).x,  0.0f, tol);
    EXPECT_NEAR(sampler.evaluate(channel, 1.00f).x, 10.0f, tol);
    EXPECT_NEAR(sampler.evaluate(channel, 1.75f).x, 10.0f, tol);
    EXPECT_NEAR(sampler.evaluate(channel, 2.00f).x, 20.0f, tol);
}

// ============================================================================
// LINEAR
// ============================================================================

TEST(animation_sampler, linear_interpolates_translation)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
    sampler.set(
        std::vector<float>{0.0f, 2.0f},
        std::vector<float>{
            0.0f, 0.0f, 0.0f,
            8.0f, 4.0f, 0.0f
        }
    );
    erhe::scene::Animation_channel channel = make_channel(erhe::scene::Animation_path::TRANSLATION, 0);

    const glm::vec4 mid = sampler.evaluate(channel, 1.0f);
    EXPECT_NEAR(mid.x, 4.0f, tol);
    EXPECT_NEAR(mid.y, 2.0f, tol);
}

// Times before the first and after the last keyframe clamp to the endpoints.
TEST(animation_sampler, linear_clamps_outside_the_keyframe_range)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
    sampler.set(
        std::vector<float>{1.0f, 2.0f},
        std::vector<float>{
            3.0f, 0.0f, 0.0f,
            7.0f, 0.0f, 0.0f
        }
    );
    erhe::scene::Animation_channel channel = make_channel(erhe::scene::Animation_path::TRANSLATION, 0);

    EXPECT_NEAR(sampler.evaluate(channel, -5.0f).x, 3.0f, tol);
    EXPECT_NEAR(sampler.evaluate(channel,  9.0f).x, 7.0f, tol);
}

// Rotation output accessors may be normalized byte / short, which round-trips
// to a slightly non-unit quaternion. The result must still be a rotation.
TEST(animation_sampler, linear_rotation_result_is_normalized)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
    // Two rotations about Y, 90 degrees apart, deliberately off unit length.
    const glm::quat q0 = glm::angleAxis(0.0f,                  glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat q1 = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
    sampler.set(
        std::vector<float>{0.0f, 1.0f},
        std::vector<float>{
            0.97f * q0.x, 0.97f * q0.y, 0.97f * q0.z, 0.97f * q0.w,
            1.04f * q1.x, 1.04f * q1.y, 1.04f * q1.z, 1.04f * q1.w
        }
    );
    erhe::scene::Animation_channel channel = make_channel(erhe::scene::Animation_path::ROTATION, 0);

    const glm::vec4 value = sampler.evaluate(channel, 0.5f);
    EXPECT_NEAR(glm::length(value), 1.0f, tol);

    // Halfway between 0 and 90 degrees about Y is 45 degrees about Y.
    const glm::quat expected = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat actual{value.w, value.x, value.y, value.z};
    EXPECT_NEAR(std::abs(glm::dot(actual, expected)), 1.0f, tol);
}

// ============================================================================
// CUBICSPLINE
// ============================================================================

// glTF appendix C scales both tangent terms by the keyframe delta t_d. The
// stored tangents are per-second derivatives, so the factor is only invisible
// when keyframes happen to be one second apart - this span is two seconds.
TEST(animation_sampler, cubicspline_scales_tangents_by_the_keyframe_delta)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::CUBICSPLINE};
    // Per keyframe: in-tangent, value, out-tangent (VEC3 each).
    sampler.set(
        std::vector<float>{0.0f, 2.0f},
        std::vector<float>{
            0.0f, 0.0f, 0.0f,   0.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f, // key 0, out tangent 1/s
            0.0f, 0.0f, 0.0f,   0.0f, 0.0f, 0.0f,   0.0f, 0.0f, 0.0f  // key 1, in tangent 0
        }
    );
    erhe::scene::Animation_channel channel = make_channel(
        erhe::scene::Animation_path::TRANSLATION,
        erhe::scene::get_component_count(erhe::scene::Animation_path::TRANSLATION)
    );

    // t = 0.5 (one second into a two second span):
    //   s1    = t_d * (t^3 - 2t^2 + t) = 2 * (0.125 - 0.5 + 0.5) = 0.25
    //   value = s1 * out_tangent       = 0.25
    // Without the t_d factor this would come out as 0.125.
    EXPECT_NEAR(sampler.evaluate(channel, 1.0f).x, 0.25f, tol);
}

// The endpoints of a cubic span are the keyframe values regardless of tangents.
TEST(animation_sampler, cubicspline_passes_through_the_keyframe_values)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::CUBICSPLINE};
    sampler.set(
        std::vector<float>{0.0f, 1.0f},
        std::vector<float>{
            -3.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   5.0f, 0.0f, 0.0f,
             7.0f, 0.0f, 0.0f,   9.0f, 0.0f, 0.0f,  -2.0f, 0.0f, 0.0f
        }
    );
    erhe::scene::Animation_channel channel = make_channel(
        erhe::scene::Animation_path::TRANSLATION,
        erhe::scene::get_component_count(erhe::scene::Animation_path::TRANSLATION)
    );

    EXPECT_NEAR(sampler.evaluate(channel, 0.0f   ).x, 1.0f, tol);
    EXPECT_NEAR(sampler.evaluate(channel, 0.9999f).x, 9.0f, 1e-2f);
    EXPECT_NEAR(sampler.evaluate(channel, 1.0f   ).x, 9.0f, tol);
}

// A component-wise cubic spline of quaternions is not a unit quaternion; glTF
// requires the result to be normalized.
TEST(animation_sampler, cubicspline_rotation_result_is_normalized)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::CUBICSPLINE};
    const glm::quat q0 = glm::angleAxis(0.0f,                  glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat q1 = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
    sampler.set(
        std::vector<float>{0.0f, 1.0f},
        std::vector<float>{
            0.0f, 0.0f, 0.0f, 0.0f,   q0.x, q0.y, q0.z, q0.w,   0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,   q1.x, q1.y, q1.z, q1.w,   0.0f, 0.0f, 0.0f, 0.0f
        }
    );
    erhe::scene::Animation_channel channel = make_channel(
        erhe::scene::Animation_path::ROTATION,
        erhe::scene::get_component_count(erhe::scene::Animation_path::ROTATION)
    );

    EXPECT_NEAR(glm::length(sampler.evaluate(channel, 0.5f)), 1.0f, tol);
}

// ============================================================================
// Malformed input
// ============================================================================

// Animation data comes from files. An empty sampler must return the identity
// value for the channel rather than reading out of bounds.
TEST(animation_sampler, empty_sampler_returns_identity)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
    sampler.set(std::vector<float>{}, std::vector<float>{});

    erhe::scene::Animation_channel translation = make_channel(erhe::scene::Animation_path::TRANSLATION, 0);
    erhe::scene::Animation_channel rotation    = make_channel(erhe::scene::Animation_path::ROTATION,    0);
    erhe::scene::Animation_channel scale       = make_channel(erhe::scene::Animation_path::SCALE,       0);

    EXPECT_EQ(sampler.evaluate(translation, 0.5f), glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(sampler.evaluate(rotation,    0.5f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(sampler.evaluate(scale,       0.5f), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
}

// glTF requires strictly increasing timestamps. A file that violates it must
// hold a keyframe value, not divide by a zero-length span.
TEST(animation_sampler, duplicate_timestamps_hold_instead_of_aborting)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
    sampler.set(
        std::vector<float>{0.0f, 1.0f, 1.0f, 2.0f},
        std::vector<float>{
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            2.0f, 0.0f, 0.0f,
            3.0f, 0.0f, 0.0f
        }
    );
    erhe::scene::Animation_channel channel = make_channel(erhe::scene::Animation_path::TRANSLATION, 0);

    // Lands on the duplicated pair: must return one of the two keyframe values
    // and must not crash.
    const float value = sampler.evaluate(channel, 1.0f).x;
    EXPECT_TRUE((value == 1.0f) || (value == 2.0f));
}

// A stale start_position (keyframes removed from the sampler since the last
// evaluate) must be clamped, not used as an index.
TEST(animation_sampler, stale_start_position_is_clamped)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
    sampler.set(
        std::vector<float>{0.0f, 1.0f},
        std::vector<float>{
            0.0f, 0.0f, 0.0f,
            4.0f, 0.0f, 0.0f
        }
    );
    erhe::scene::Animation_channel channel = make_channel(erhe::scene::Animation_path::TRANSLATION, 0);
    channel.start_position = 17;

    EXPECT_NEAR(sampler.evaluate(channel, 0.5f).x, 2.0f, tol);
}

// An animation whose samplers carry no keyframes has no time range; the
// accessors must not read from an empty vector.
TEST(animation_sampler, empty_sampler_time_range_is_safe)
{
    erhe::scene::Animation animation{"empty"};
    animation.samplers.emplace_back(erhe::scene::Animation_interpolation_mode::LINEAR);
    animation.channels.push_back(make_channel(erhe::scene::Animation_path::TRANSLATION, 0));

    EXPECT_NO_FATAL_FAILURE({
        static_cast<void>(animation.get_first_time());
        static_cast<void>(animation.get_last_time());
    });
}
