#include "erhe_math/euler_angles.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <functional>
#include <random>
#include <string>

namespace {

class Euler_order
{
public:
    const char* name;
    int         axis_1;
    int         axis_2;
    int         axis_3;
    std::function<glm::mat4 (float, float, float)>                    glm_matrix;
    std::function<glm::quat (float, float, float)>                    quat_float;
    std::function<void (const glm::quat&, float&, float&, float&)>    extract_float;
    std::function<glm::dquat (double, double, double)>                quat_double;
    std::function<void (const glm::dquat&, double&, double&, double&)> extract_double;

    [[nodiscard]] auto is_proper() const -> bool { return axis_1 == axis_3; }
};

#define EULER_ORDER(ORDER, A1, A2, A3)                                                                      \
    Euler_order{                                                                                            \
        #ORDER, A1, A2, A3,                                                                                 \
        [](float a, float b, float c) { return glm::eulerAngle##ORDER(a, b, c); },                          \
        [](float a, float b, float c) { return erhe::math::quatEulerAngle##ORDER(a, b, c); },               \
        [](const glm::quat& q, float& a, float& b, float& c) { erhe::math::extractEulerAngle##ORDER(q, a, b, c); }, \
        [](double a, double b, double c) { return erhe::math::quatEulerAngle##ORDER(a, b, c); },            \
        [](const glm::dquat& q, double& a, double& b, double& c) { erhe::math::extractEulerAngle##ORDER(q, a, b, c); } \
    }

auto all_orders() -> const std::array<Euler_order, 12>&
{
    static const std::array<Euler_order, 12> orders{
        EULER_ORDER(XYX, 0, 1, 0),
        EULER_ORDER(XZX, 0, 2, 0),
        EULER_ORDER(YXY, 1, 0, 1),
        EULER_ORDER(YZY, 1, 2, 1),
        EULER_ORDER(ZXZ, 2, 0, 2),
        EULER_ORDER(ZYZ, 2, 1, 2),
        EULER_ORDER(XYZ, 0, 1, 2),
        EULER_ORDER(XZY, 0, 2, 1),
        EULER_ORDER(YXZ, 1, 0, 2),
        EULER_ORDER(YZX, 1, 2, 0),
        EULER_ORDER(ZYX, 2, 1, 0),
        EULER_ORDER(ZXY, 2, 0, 1)
    };
    return orders;
}

#undef EULER_ORDER

constexpr float  pi_f     = glm::pi<float>();
constexpr double pi_d     = glm::pi<double>();
constexpr float  tol_f    = 2e-5f;
constexpr double tol_d    = 1e-12;

auto max_abs_diff(const glm::mat3& a, const glm::mat3& b) -> float
{
    float d = 0.0f;
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            d = std::max(d, std::abs(a[c][r] - b[c][r]));
        }
    }
    return d;
}

template <typename Q>
auto max_abs_diff(const Q& a, const Q& b) -> typename Q::value_type
{
    return std::max(
        std::max(std::abs(a.w - b.w), std::abs(a.x - b.x)),
        std::max(std::abs(a.y - b.y), std::abs(a.z - b.z))
    );
}

// Uniformly distributed unit quaternion, both hemispheres (w < 0 included)
template <typename T>
auto random_unit_quaternion(std::mt19937& rng) -> glm::qua<T>
{
    std::normal_distribution<T> n{T{0}, T{1}};
    for (;;) {
        const glm::qua<T> q = glm::qua<T>::wxyz(n(rng), n(rng), n(rng), n(rng));
        const T length = glm::length(q);
        if (length > T{1e-3}) {
            return q / length;
        }
    }
}

// The ranges documented in erhe_math/euler_angles.hpp
void expect_ranges(const Euler_order& order, const double t1, const double t2, const double t3, const double tolerance)
{
    const double two_pi = 2.0 * pi_d;
    if (order.is_proper()) {
        EXPECT_GE(t2, 0.0  - tolerance) << order.name;
        EXPECT_LE(t2, pi_d + tolerance) << order.name;
    } else {
        EXPECT_GE(t2, -0.5 * pi_d - tolerance) << order.name;
        EXPECT_LE(t2,  0.5 * pi_d + tolerance) << order.name;
    }
    EXPECT_LE(std::abs(t1), two_pi + tolerance) << order.name;
    EXPECT_LE(std::abs(t3), two_pi + tolerance) << order.name;
    // At most one of the outer angles leaves (-pi, pi]
    const bool t1_outside = std::abs(t1) > pi_d + tolerance;
    const bool t3_outside = std::abs(t3) > pi_d + tolerance;
    EXPECT_FALSE(t1_outside && t3_outside) << order.name << " t1 = " << t1 << " t3 = " << t3;
}

} // anonymous namespace

// ============================================================================
// Euler angles -> quaternion matches glm's Euler angles -> matrix
// ============================================================================

TEST(EulerAngles, QuaternionMatchesGlmMatrix)
{
    std::mt19937 rng{1234u};
    std::uniform_real_distribution<float> angle{-2.0f * pi_f, 2.0f * pi_f};
    for (const Euler_order& order : all_orders()) {
        for (int i = 0; i < 200; ++i) {
            const float t1 = angle(rng);
            const float t2 = angle(rng);
            const float t3 = angle(rng);
            const glm::mat3 expected{order.glm_matrix(t1, t2, t3)};
            const glm::mat3 actual  {glm::mat3_cast(order.quat_float(t1, t2, t3))};
            EXPECT_LT(max_abs_diff(expected, actual), tol_f) << order.name << " " << t1 << " " << t2 << " " << t3;
        }
    }
}

// ============================================================================
// Quaternion -> Euler angles -> quaternion reproduces q including its sign
// ============================================================================

TEST(EulerAngles, QuaternionRoundtripKeepsSignFloat)
{
    std::mt19937 rng{42u};
    for (const Euler_order& order : all_orders()) {
        for (int i = 0; i < 1000; ++i) {
            const glm::quat q = random_unit_quaternion<float>(rng);
            for (const glm::quat& signed_q : {q, -q}) {
                float t1{};
                float t2{};
                float t3{};
                order.extract_float(signed_q, t1, t2, t3);
                const glm::quat back = order.quat_float(t1, t2, t3);
                EXPECT_LT(max_abs_diff(back, signed_q), tol_f) << order.name << " w = " << signed_q.w;
                expect_ranges(order, t1, t2, t3, 1e-5);
            }
        }
    }
}

TEST(EulerAngles, QuaternionRoundtripKeepsSignDouble)
{
    std::mt19937 rng{4242u};
    for (const Euler_order& order : all_orders()) {
        for (int i = 0; i < 1000; ++i) {
            const glm::dquat q = random_unit_quaternion<double>(rng);
            for (const glm::dquat& signed_q : {q, -q}) {
                double t1{};
                double t2{};
                double t3{};
                order.extract_double(signed_q, t1, t2, t3);
                const glm::dquat back = order.quat_double(t1, t2, t3);
                EXPECT_LT(max_abs_diff(back, signed_q), tol_d) << order.name << " w = " << signed_q.w;
                expect_ranges(order, t1, t2, t3, 1e-12);
            }
        }
    }
}

TEST(EulerAngles, OppositeQuaternionsGiveDifferentAngles)
{
    std::mt19937 rng{7u};
    for (const Euler_order& order : all_orders()) {
        for (int i = 0; i < 100; ++i) {
            const glm::dquat q = random_unit_quaternion<double>(rng);
            double a1{}, a2{}, a3{};
            double b1{}, b2{}, b3{};
            order.extract_double( q, a1, a2, a3);
            order.extract_double(-q, b1, b2, b3);
            // Same rotation, so the middle angle agrees; the outer angles
            // differ by an odd number of 2 pi turns in total.
            EXPECT_NEAR(a2, b2, 1e-9) << order.name;
            const double turns = ((a1 - b1) + (a3 - b3)) / (2.0 * pi_d);
            const double odd   = std::abs(std::fmod(std::round(turns), 2.0));
            EXPECT_NEAR(turns, std::round(turns), 1e-6) << order.name;
            EXPECT_EQ(odd, 1.0) << order.name << " turns = " << turns;
        }
    }
}

// ============================================================================
// Agreement with glm's matrix extraction (as rotations)
// ============================================================================

TEST(EulerAngles, SameRotationAsGlmMatrixExtraction)
{
    std::mt19937 rng{99u};
    for (const Euler_order& order : all_orders()) {
        for (int i = 0; i < 200; ++i) {
            const glm::quat q = random_unit_quaternion<float>(rng);
            float t1{}, t2{}, t3{};
            order.extract_float(q, t1, t2, t3);
            const glm::mat3 from_angles{order.glm_matrix(t1, t2, t3)};
            EXPECT_LT(max_abs_diff(from_angles, glm::mat3_cast(q)), tol_f) << order.name;
        }
    }
}

// ============================================================================
// Euler angles -> quaternion -> Euler angles
// ============================================================================

TEST(EulerAngles, AngleRoundtripInCanonicalRange)
{
    std::mt19937 rng{5u};
    std::uniform_real_distribution<double> outer{-0.999 * pi_d, 0.999 * pi_d};
    std::uniform_real_distribution<double> middle_proper{0.01, pi_d - 0.01};
    std::uniform_real_distribution<double> middle_tait_bryan{-0.5 * pi_d + 0.01, 0.5 * pi_d - 0.01};
    for (const Euler_order& order : all_orders()) {
        for (int i = 0; i < 500; ++i) {
            const double t1 = outer(rng);
            const double t2 = order.is_proper() ? middle_proper(rng) : middle_tait_bryan(rng);
            const double t3 = outer(rng);
            double e1{}, e2{}, e3{};
            order.extract_double(order.quat_double(t1, t2, t3), e1, e2, e3);
            EXPECT_NEAR(e1, t1, 1e-9) << order.name;
            EXPECT_NEAR(e2, t2, 1e-9) << order.name;
            EXPECT_NEAR(e3, t3, 1e-9) << order.name;
        }
    }
}

TEST(EulerAngles, AngleRoundtripBeyondPiKeepsOuterAngle)
{
    // One outer angle in (pi, 2 pi) selects the other quaternion hemisphere;
    // it must come back unchanged instead of being wrapped by 2 pi.
    for (const Euler_order& order : all_orders()) {
        const double t2 = order.is_proper() ? 0.8 : 0.3;
        for (const double big : {1.3 * pi_d, -1.7 * pi_d}) {
            double e1{}, e2{}, e3{};
            order.extract_double(order.quat_double(big, t2, 0.2), e1, e2, e3);
            EXPECT_NEAR(e1, big, 1e-9) << order.name;
            EXPECT_NEAR(e2, t2,  1e-9) << order.name;
            EXPECT_NEAR(e3, 0.2, 1e-9) << order.name;

            order.extract_double(order.quat_double(-0.2, t2, big), e1, e2, e3);
            EXPECT_NEAR(e1, -0.2, 1e-9) << order.name;
            EXPECT_NEAR(e2, t2,   1e-9) << order.name;
            EXPECT_NEAR(e3, big,  1e-9) << order.name;
        }
    }
}

// ============================================================================
// Gimbal lock
// ============================================================================

TEST(EulerAngles, GimbalLockPutsRotationIntoFirstAngle)
{
    for (const Euler_order& order : all_orders()) {
        const std::array<double, 2> locked_middles = order.is_proper()
            ? std::array<double, 2>{0.0, pi_d}
            : std::array<double, 2>{-0.5 * pi_d, 0.5 * pi_d};
        for (const double t2 : locked_middles) {
            for (const double t1 : {0.0, 0.4, -2.5, 1.5 * pi_d}) {
                for (const double t3 : {0.0, 0.7, -1.1}) {
                    const glm::dquat q = order.quat_double(t1, t2, t3);
                    for (const glm::dquat& signed_q : {q, -q}) {
                        double e1{}, e2{}, e3{};
                        order.extract_double(signed_q, e1, e2, e3);
                        EXPECT_EQ(e3, 0.0) << order.name << " t2 = " << t2;
                        EXPECT_NEAR(e2, t2, 1e-7) << order.name;
                        EXPECT_LT(max_abs_diff(order.quat_double(e1, e2, e3), signed_q), 1e-12) << order.name;
                    }
                }
            }
        }
    }
}

// ============================================================================
// Continuity: turning about the first axis covers the full 4 pi double cover
// ============================================================================

TEST(EulerAngles, TurningThroughPiDoesNotWrap)
{
    for (const Euler_order& order : all_orders()) {
        const double t2 = order.is_proper() ? 0.6 : 0.4;
        double previous_t1 = 0.0;
        int    jumps       = 0;
        const int steps = 720;
        for (int i = 0; i <= steps; ++i) {
            const double t1 = -1.9 * pi_d + (3.8 * pi_d * i) / steps; // stays inside (-2 pi, 2 pi)
            double e1{}, e2{}, e3{};
            order.extract_double(order.quat_double(t1, t2, 0.1), e1, e2, e3);
            EXPECT_NEAR(e1, t1, 1e-9) << order.name << " step " << i;
            if ((i > 0) && (std::abs(e1 - previous_t1) > 1.0)) {
                ++jumps;
            }
            previous_t1 = e1;
        }
        EXPECT_EQ(jumps, 0) << order.name;
    }
}
