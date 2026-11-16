#include "erhe_math/math_util.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

namespace {

class Lcg
{
public:
    auto next() -> float
    {
        m_state = (m_state * 6364136223846793005ull) + 1442695040888963407ull;
        return static_cast<float>(m_state >> 40) / static_cast<float>(1u << 24); // [0, 1)
    }
    auto signed_next() -> float
    {
        return (2.0f * next()) - 1.0f;
    }
    auto unit_vector() -> glm::vec3
    {
        for (;;) {
            const glm::vec3 v{signed_next(), signed_next(), signed_next()};
            const float     length = glm::length(v);
            if ((length > 0.1f) && (length <= 1.0f)) {
                return v / length;
            }
        }
    }

private:
    std::uint64_t m_state{0x2545f4914f6cdd1dull};
};

// A ring pose as the Rotate tool sees one: a pointer point on the ring plane, its direction from the center normalized.
class Ring_pose
{
public:
    glm::vec3 normal;
    glm::vec3 reference; // unit, in the plane
    glm::vec3 side;      // unit, in the plane, normal x reference
};

auto make_pose(Lcg& random) -> Ring_pose
{
    const glm::vec3 normal = random.unit_vector();
    glm::vec3 in_plane = glm::cross(normal, random.unit_vector());
    while (glm::length(in_plane) < 0.1f) {
        in_plane = glm::cross(normal, random.unit_vector());
    }
    const glm::vec3 center    = 100.0f * glm::vec3{random.signed_next(), random.signed_next(), random.signed_next()};
    const glm::vec3 point     = center + ((0.01f + (50.0f * random.next())) * glm::normalize(in_plane));
    const glm::vec3 reference = glm::normalize(point - center);
    return Ring_pose{.normal = normal, .reference = reference, .side = glm::normalize(glm::cross(normal, reference))};
}

TEST(Angle_of_rotation, zero_motion_is_exactly_zero_over_many_poses)
{
    Lcg random;
    for (int i = 0; i < 100000; ++i) {
        const Ring_pose pose = make_pose(random);
        const glm::vec3 q    = pose.reference; // the press pixel again: the same direction bit for bit
        const float     angle = erhe::math::angle_of_rotation<float>(q, pose.normal, pose.reference);
        ASSERT_EQ(angle, 0.0f) << "pose " << i;
        // Unnormalized inputs of one direction too.
        ASSERT_EQ(erhe::math::angle_of_rotation<float>(3.0f * q, pose.normal, 3.0f * pose.reference), 0.0f) << "pose " << i;
    }
}

TEST(Angle_of_rotation, signed_angle_about_the_axis)
{
    Lcg random;
    for (int i = 0; i < 10000; ++i) {
        const Ring_pose pose  = make_pose(random);
        const float     theta = 3.1f * random.signed_next();
        const glm::vec3 q     = glm::angleAxis(theta, pose.normal) * pose.reference;
        const float     angle = erhe::math::angle_of_rotation<float>(q, pose.normal, pose.reference);
        ASSERT_TRUE(std::isfinite(angle));
        EXPECT_NEAR(angle, theta, 2.0e-4f) << "pose " << i;
    }
}

TEST(Angle_of_rotation, tiny_and_opposite_directions_stay_finite)
{
    const glm::vec3 normal{0.0f, 1.0f, 0.0f};
    const glm::vec3 reference{1.0f, 0.0f, 0.0f};
    const float tiny = erhe::math::angle_of_rotation<float>(glm::angleAxis(1.0e-6f, normal) * reference, normal, reference);
    EXPECT_TRUE(std::isfinite(tiny));
    EXPECT_NEAR(tiny, 1.0e-6f, 1.0e-7f);
    const float opposite = erhe::math::angle_of_rotation<float>(-reference, normal, reference);
    EXPECT_TRUE(std::isfinite(opposite));
    EXPECT_NEAR(std::abs(opposite), glm::pi<float>(), 1.0e-6f);
    // A quarter turn about +Y takes +X to -Z (right-handed).
    EXPECT_NEAR(erhe::math::angle_of_rotation<float>(glm::vec3{0.0f, 0.0f, -1.0f}, normal, reference), 0.5f * glm::pi<float>(), 1.0e-6f);
}

} // anonymous namespace
