#include "erhe_math/math_util.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <gtest/gtest.h>

namespace {

// Project a point through a projection matrix and return NDC z
auto project_z(const glm::mat4& m, const glm::vec3& eye_space_point) -> float
{
    const glm::vec4 clip = m * glm::vec4{eye_space_point, 1.0f};
    return clip.z / clip.w;
}

constexpr float z_near = 0.1f;
constexpr float z_far  = 100.0f;
constexpr float tol    = 1e-5f;

// Eye-space points along the -Z axis (camera looks down -Z in RH)
const glm::vec3 at_near{0.0f, 0.0f, -z_near};
const glm::vec3 at_far {0.0f, 0.0f, -z_far};
const glm::vec3 at_mid {0.0f, 0.0f, -10.0f};

} // anonymous namespace

// ============================================================================
// create_frustum
// ============================================================================

TEST(CreateFrustum, ZeroToOne_NearMapsTo0)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_near), 0.0f, tol);
}

TEST(CreateFrustum, ZeroToOne_FarMapsTo1)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_far), 1.0f, tol);
}

TEST(CreateFrustum, ZeroToOne_MidIsBetween0And1)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    const float z = project_z(m, at_mid);
    EXPECT_GT(z, 0.0f);
    EXPECT_LT(z, 1.0f);
}

TEST(CreateFrustum, ZeroToOne_DepthIsMonotonic)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    const float z1 = project_z(m, {0.0f, 0.0f, -1.0f});
    const float z2 = project_z(m, {0.0f, 0.0f, -10.0f});
    const float z3 = project_z(m, {0.0f, 0.0f, -50.0f});
    EXPECT_LT(z1, z2);
    EXPECT_LT(z2, z3);
}

TEST(CreateFrustum, NegOneToOne_NearMapsToNeg1)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_near), -1.0f, tol);
}

TEST(CreateFrustum, NegOneToOne_FarMapsTo1)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_far), 1.0f, tol);
}

TEST(CreateFrustum, NegOneToOne_MidIsBetweenNeg1And1)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    const float z = project_z(m, at_mid);
    EXPECT_GT(z, -1.0f);
    EXPECT_LT(z, 1.0f);
}

TEST(CreateFrustum, NegOneToOne_DepthIsMonotonic)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    const float z1 = project_z(m, {0.0f, 0.0f, -1.0f});
    const float z2 = project_z(m, {0.0f, 0.0f, -10.0f});
    const float z3 = project_z(m, {0.0f, 0.0f, -50.0f});
    EXPECT_LT(z1, z2);
    EXPECT_LT(z2, z3);
}

// ============================================================================
// create_frustum with reverse depth (z_near and z_far swapped)
// ============================================================================

TEST(CreateFrustum, ZeroToOne_ReverseDepth_NearMapsTo1)
{
    // Reverse depth: pass z_far as near, z_near as far
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_far, z_near, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_near), 1.0f, tol);
}

TEST(CreateFrustum, ZeroToOne_ReverseDepth_FarMapsTo0)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_far, z_near, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_far), 0.0f, tol);
}

TEST(CreateFrustum, ZeroToOne_ReverseDepth_DepthIsMonotonicDecreasing)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_far, z_near, erhe::math::Depth_range::zero_to_one);
    const float z1 = project_z(m, {0.0f, 0.0f, -1.0f});
    const float z2 = project_z(m, {0.0f, 0.0f, -10.0f});
    const float z3 = project_z(m, {0.0f, 0.0f, -50.0f});
    EXPECT_GT(z1, z2);
    EXPECT_GT(z2, z3);
}

TEST(CreateFrustum, NegOneToOne_ReverseDepth_NearMapsTo1)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_far, z_near, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_near), 1.0f, tol);
}

TEST(CreateFrustum, NegOneToOne_ReverseDepth_FarMapsToNeg1)
{
    const auto m = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_far, z_near, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_far), -1.0f, tol);
}

// ============================================================================
// create_frustum_infinite_far
// ============================================================================

TEST(CreateFrustumInfiniteFar, ZeroToOne_NearMapsTo0)
{
    const auto m = erhe::math::create_frustum_infinite_far(-1.0f, 1.0f, -1.0f, 1.0f, z_near, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_near), 0.0f, tol);
}

TEST(CreateFrustumInfiniteFar, ZeroToOne_VeryFarApproaches1)
{
    const auto m = erhe::math::create_frustum_infinite_far(-1.0f, 1.0f, -1.0f, 1.0f, z_near, erhe::math::Depth_range::zero_to_one);
    const float z = project_z(m, {0.0f, 0.0f, -1e6f});
    EXPECT_GT(z, 0.999f);
    EXPECT_LE(z, 1.0f);
}

TEST(CreateFrustumInfiniteFar, ZeroToOne_DepthIsMonotonic)
{
    const auto m = erhe::math::create_frustum_infinite_far(-1.0f, 1.0f, -1.0f, 1.0f, z_near, erhe::math::Depth_range::zero_to_one);
    const float z1 = project_z(m, {0.0f, 0.0f, -1.0f});
    const float z2 = project_z(m, {0.0f, 0.0f, -10.0f});
    const float z3 = project_z(m, {0.0f, 0.0f, -100.0f});
    EXPECT_LT(z1, z2);
    EXPECT_LT(z2, z3);
}

TEST(CreateFrustumInfiniteFar, NegOneToOne_NearMapsToNeg1)
{
    const auto m = erhe::math::create_frustum_infinite_far(-1.0f, 1.0f, -1.0f, 1.0f, z_near, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_near), -1.0f, tol);
}

TEST(CreateFrustumInfiniteFar, NegOneToOne_VeryFarApproaches1)
{
    const auto m = erhe::math::create_frustum_infinite_far(-1.0f, 1.0f, -1.0f, 1.0f, z_near, erhe::math::Depth_range::negative_one_to_one);
    const float z = project_z(m, {0.0f, 0.0f, -1e6f});
    EXPECT_GT(z, 0.999f);
    EXPECT_LE(z, 1.0f);
}

// ============================================================================
// create_perspective_vertical
// ============================================================================

TEST(CreatePerspectiveVertical, ZeroToOne_NearMapsTo0)
{
    const float fov_y = glm::radians(90.0f);
    const auto m = erhe::math::create_perspective_vertical(fov_y, 16.0f / 9.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_near), 0.0f, tol);
}

TEST(CreatePerspectiveVertical, ZeroToOne_FarMapsTo1)
{
    const float fov_y = glm::radians(90.0f);
    const auto m = erhe::math::create_perspective_vertical(fov_y, 16.0f / 9.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_far), 1.0f, tol);
}

TEST(CreatePerspectiveVertical, NegOneToOne_NearMapsToNeg1)
{
    const float fov_y = glm::radians(90.0f);
    const auto m = erhe::math::create_perspective_vertical(fov_y, 16.0f / 9.0f, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_near), -1.0f, tol);
}

TEST(CreatePerspectiveVertical, NegOneToOne_FarMapsTo1)
{
    const float fov_y = glm::radians(90.0f);
    const auto m = erhe::math::create_perspective_vertical(fov_y, 16.0f / 9.0f, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_far), 1.0f, tol);
}

TEST(CreatePerspectiveVertical, ZeroToOne_ReverseDepth)
{
    const float fov_y = glm::radians(90.0f);
    const auto m = erhe::math::create_perspective_vertical(fov_y, 16.0f / 9.0f, z_far, z_near, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_near), 1.0f, tol);
    EXPECT_NEAR(project_z(m, at_far),  0.0f, tol);
}

// ============================================================================
// create_perspective_horizontal
// ============================================================================

TEST(CreatePerspectiveHorizontal, ZeroToOne_NearAndFar)
{
    const float fov_x = glm::radians(90.0f);
    const auto m = erhe::math::create_perspective_horizontal(fov_x, 16.0f / 9.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_near), 0.0f, tol);
    EXPECT_NEAR(project_z(m, at_far),  1.0f, tol);
}

TEST(CreatePerspectiveHorizontal, NegOneToOne_NearAndFar)
{
    const float fov_x = glm::radians(90.0f);
    const auto m = erhe::math::create_perspective_horizontal(fov_x, 16.0f / 9.0f, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_near), -1.0f, tol);
    EXPECT_NEAR(project_z(m, at_far),   1.0f, tol);
}

// ============================================================================
// create_perspective (fov_x, fov_y)
// ============================================================================

TEST(CreatePerspective, ZeroToOne_NearAndFar)
{
    const float fov = glm::radians(90.0f);
    const auto m = erhe::math::create_perspective(fov, fov, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_near), 0.0f, tol);
    EXPECT_NEAR(project_z(m, at_far),  1.0f, tol);
}

TEST(CreatePerspective, NegOneToOne_NearAndFar)
{
    const float fov = glm::radians(90.0f);
    const auto m = erhe::math::create_perspective(fov, fov, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_near), -1.0f, tol);
    EXPECT_NEAR(project_z(m, at_far),   1.0f, tol);
}

// ============================================================================
// create_orthographic
// ============================================================================

TEST(CreateOrthographic, ZeroToOne_NearMapsTo0)
{
    const auto m = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_near), 0.0f, tol);
}

TEST(CreateOrthographic, ZeroToOne_FarMapsTo1)
{
    const auto m = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_far), 1.0f, tol);
}

TEST(CreateOrthographic, ZeroToOne_MidIsBetween0And1)
{
    const auto m = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    const float z = project_z(m, at_mid);
    EXPECT_GT(z, 0.0f);
    EXPECT_LT(z, 1.0f);
}

TEST(CreateOrthographic, ZeroToOne_DepthIsMonotonic)
{
    const auto m = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    const float z1 = project_z(m, {0.0f, 0.0f, -1.0f});
    const float z2 = project_z(m, {0.0f, 0.0f, -10.0f});
    const float z3 = project_z(m, {0.0f, 0.0f, -50.0f});
    EXPECT_LT(z1, z2);
    EXPECT_LT(z2, z3);
}

TEST(CreateOrthographic, NegOneToOne_NearMapsToNeg1)
{
    const auto m = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_near), -1.0f, tol);
}

TEST(CreateOrthographic, NegOneToOne_FarMapsTo1)
{
    const auto m = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_far), 1.0f, tol);
}

TEST(CreateOrthographic, NegOneToOne_MidIsBetweenNeg1And1)
{
    const auto m = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    const float z = project_z(m, at_mid);
    EXPECT_GT(z, -1.0f);
    EXPECT_LT(z, 1.0f);
}

TEST(CreateOrthographic, NegOneToOne_DepthIsMonotonic)
{
    const auto m = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::negative_one_to_one);
    const float z1 = project_z(m, {0.0f, 0.0f, -1.0f});
    const float z2 = project_z(m, {0.0f, 0.0f, -10.0f});
    const float z3 = project_z(m, {0.0f, 0.0f, -50.0f});
    EXPECT_LT(z1, z2);
    EXPECT_LT(z2, z3);
}

// ============================================================================
// create_orthographic with reverse depth
// ============================================================================

TEST(CreateOrthographic, ZeroToOne_ReverseDepth)
{
    const auto m = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_far, z_near, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m, at_near), 1.0f, tol);
    EXPECT_NEAR(project_z(m, at_far),  0.0f, tol);
}

TEST(CreateOrthographic, NegOneToOne_ReverseDepth)
{
    const auto m = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_far, z_near, erhe::math::Depth_range::negative_one_to_one);
    EXPECT_NEAR(project_z(m, at_near),  1.0f, tol);
    EXPECT_NEAR(project_z(m, at_far),  -1.0f, tol);
}

// ============================================================================
// Consistency: perspective functions produce same depth as create_frustum
// ============================================================================

TEST(PerspectiveConsistency, VerticalMatchesFrustum)
{
    const float fov_y = glm::radians(90.0f);
    const float aspect = 16.0f / 9.0f;
    for (auto depth_range : {erhe::math::Depth_range::zero_to_one, erhe::math::Depth_range::negative_one_to_one}) {
        const auto m_vert = erhe::math::create_perspective_vertical(fov_y, aspect, z_near, z_far, depth_range);
        const float z_near_ndc = project_z(m_vert, at_near);
        const float z_far_ndc  = project_z(m_vert, at_far);
        const float z_mid_ndc  = project_z(m_vert, at_mid);

        const float tan_half = std::tan(fov_y * 0.5f);
        const float half_h = z_near * tan_half;
        const float half_w = half_h * aspect;
        const auto m_frust = erhe::math::create_frustum(-half_w, half_w, -half_h, half_h, z_near, z_far, depth_range);
        EXPECT_NEAR(project_z(m_frust, at_near), z_near_ndc, tol);
        EXPECT_NEAR(project_z(m_frust, at_far),  z_far_ndc,  tol);
        EXPECT_NEAR(project_z(m_frust, at_mid),  z_mid_ndc,  tol);
    }
}

TEST(PerspectiveConsistency, HorizontalMatchesFrustum)
{
    const float fov_x = glm::radians(90.0f);
    const float aspect = 16.0f / 9.0f;
    for (auto depth_range : {erhe::math::Depth_range::zero_to_one, erhe::math::Depth_range::negative_one_to_one}) {
        const auto m_horiz = erhe::math::create_perspective_horizontal(fov_x, aspect, z_near, z_far, depth_range);
        const float z_near_ndc = project_z(m_horiz, at_near);
        const float z_far_ndc  = project_z(m_horiz, at_far);

        const float tan_half = std::tan(fov_x * 0.5f);
        const float half_w = z_near * tan_half;
        const float half_h = half_w / aspect;
        const auto m_frust = erhe::math::create_frustum(-half_w, half_w, -half_h, half_h, z_near, z_far, depth_range);
        EXPECT_NEAR(project_z(m_frust, at_near), z_near_ndc, tol);
        EXPECT_NEAR(project_z(m_frust, at_far),  z_far_ndc,  tol);
    }
}

// ============================================================================
// Default parameter backward compatibility
// ============================================================================

TEST(DefaultParameter, FrustumDefaultIsZeroToOne)
{
    const auto m_default  = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far);
    const auto m_explicit = erhe::math::create_frustum(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m_default, at_near), project_z(m_explicit, at_near), tol);
    EXPECT_NEAR(project_z(m_default, at_far),  project_z(m_explicit, at_far),  tol);
    EXPECT_NEAR(project_z(m_default, at_mid),  project_z(m_explicit, at_mid),  tol);
}

TEST(DefaultParameter, OrthographicDefaultIsZeroToOne)
{
    const auto m_default  = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far);
    const auto m_explicit = erhe::math::create_orthographic(-1.0f, 1.0f, -1.0f, 1.0f, z_near, z_far, erhe::math::Depth_range::zero_to_one);
    EXPECT_NEAR(project_z(m_default, at_near), project_z(m_explicit, at_near), tol);
    EXPECT_NEAR(project_z(m_default, at_far),  project_z(m_explicit, at_far),  tol);
}
