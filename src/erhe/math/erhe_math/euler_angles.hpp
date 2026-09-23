#pragma once

// Euler angles <-> quaternion, for all twelve axis orders, without a detour
// through a rotation matrix.
//
// Convention (the same as glm::eulerAngleABC / glm::extractEulerAngleABC):
// the angles (t1, t2, t3) of order ABC describe the rotation
//
//     R_A(t1) * R_B(t2) * R_C(t3)
//
// A rotation matrix can not tell q from -q, so glm's matrix extraction returns
// angles that reproduce the rotation but not necessarily the quaternion. The
// quaternion extraction here reproduces the quaternion itself, sign included:
// quatEulerAngleABC(extractEulerAngleABC(q)) == q (up to rounding), so q and
// -q yield different angles. Ranges of the extracted angles:
//
// - t2 is in [-pi/2, pi/2] for Tait-Bryan orders (A, B, C distinct) and in
//   [0, pi] for proper Euler orders (A == C).
// - t1 and t3 are in (-pi, pi], except that one of them is moved by 2 pi
//   into [-2 pi, 2 pi] when that is needed to reproduce the sign of q. The
//   one moved is the one of larger magnitude (the one nearer +-pi), so an
//   angle turning through +-pi keeps going instead of jumping by 2 pi; it
//   jumps by 4 pi (the same quaternion) when it reaches +-2 pi.
// - At gimbal lock (t2 at the end of its range, where only t1 +- t3 is
//   determined) t3 is 0 and t1 carries the whole rotation.

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace erhe::math {

namespace detail {

// Axis index 0 = X, 1 = Y, 2 = Z

template <typename T, glm::qualifier Q>
[[nodiscard]] auto vector_component(const glm::qua<T, Q>& q, const int axis) -> T
{
    return (axis == 0) ? q.x : (axis == 1) ? q.y : q.z;
}

template <typename T, glm::qualifier Q>
[[nodiscard]] auto axis_quaternion(const int axis, const T angle) -> glm::qua<T, Q>
{
    const T half = angle / T{2};
    const T s    = std::sin(half);
    return glm::qua<T, Q>::wxyz(
        std::cos(half),
        (axis == 0) ? s : T{0},
        (axis == 1) ? s : T{0},
        (axis == 2) ? s : T{0}
    );
}

// +1 when (axis_a, axis_b) is cyclic (X->Y, Y->Z, Z->X), -1 otherwise
[[nodiscard]] inline auto axis_pair_parity(const int axis_a, const int axis_b) -> int
{
    return (axis_b == ((axis_a + 1) % 3)) ? 1 : -1;
}

template <typename T>
[[nodiscard]] auto wrap_angle(T& angle) -> int
{
    const T pi     = glm::pi<T>();
    const T two_pi = glm::two_pi<T>();
    if (angle > pi) {
        angle -= two_pi;
        return 1;
    }
    if (angle <= -pi) {
        angle += two_pi;
        return 1;
    }
    return 0;
}

// t1 and t3 in (-2 pi, 2 pi] reproduce q exactly. Wrap both into (-pi, pi];
// each wrap flips the sign of the composed quaternion, so after an odd number
// of wraps move the larger-magnitude angle back out by 2 pi.
template <typename T>
void canonicalize_outer_angles(T& t1, T& t3)
{
    const T two_pi = glm::two_pi<T>();
    const int wrap_count = wrap_angle(t1) + wrap_angle(t3);
    if ((wrap_count % 2) == 0) {
        return;
    }
    T& moved = (std::abs(t3) > std::abs(t1)) ? t3 : t1;
    moved = (moved > T{0}) ? (moved - two_pi) : (moved + two_pi);
}

} // namespace detail

// Quaternion of R_axis_1(t1) * R_axis_2(t2) * R_axis_3(t3).
// Axis index 0 = X, 1 = Y, 2 = Z; axis_2 differs from axis_1 and axis_3.
template <typename T, glm::qualifier Q = glm::defaultp>
[[nodiscard]] auto euler_angles_to_quaternion(
    const int axis_1,
    const int axis_2,
    const int axis_3,
    const T   t1,
    const T   t2,
    const T   t3
) -> glm::qua<T, Q>
{
    return
        detail::axis_quaternion<T, Q>(axis_1, t1) *
        detail::axis_quaternion<T, Q>(axis_2, t2) *
        detail::axis_quaternion<T, Q>(axis_3, t3);
}

// Inverse of euler_angles_to_quaternion(), sign of q included. q does not
// need to be normalized. See the top of this file for the angle ranges.
template <typename T, glm::qualifier Q>
void quaternion_to_euler_angles(
    const glm::qua<T, Q>& q,
    const int             axis_1,
    const int             axis_2,
    const int             axis_3,
    T&                    t1,
    T&                    t2,
    T&                    t3
)
{
    const bool proper     = (axis_1 == axis_3);
    const int  axis_other = 3 - axis_1 - axis_2; // the axis named by neither axis_1 nor axis_2
    const T    parity     = static_cast<T>(detail::axis_pair_parity(axis_1, axis_2));
    const T    w          = q.w;
    const T    q_1        = detail::vector_component(q, axis_1);
    const T    q_2        = detail::vector_component(q, axis_2);
    const T    q_other    = parity * detail::vector_component(q, axis_other);

    // Both cases reduce to four numbers of the form
    //     a = r_ab * cos(alpha), b = r_ab * sin(alpha),
    //     c = r_cd * cos(delta), d = r_cd * sin(delta),
    // with r_ab = cos(half_middle), r_cd = sin(half_middle) (up to a common
    // positive scale), half_middle in [0, pi/2].
    //
    // Proper (R_i(t1) R_j(t2) R_i(t3), k = other axis, e = parity(i, j)):
    //     w   = cos(t2/2) cos((t1+t3)/2)    q_i = cos(t2/2) sin((t1+t3)/2)
    //     q_j = sin(t2/2) cos((t1-t3)/2)  e q_k = sin(t2/2) sin((t1-t3)/2)
    //   half_middle = t2/2, alpha = (t1+t3)/2, delta = (t1-t3)/2
    //
    // Tait-Bryan (R_i(t1) R_j(t2) R_k(t3), e = parity(i, j), u = e t3):
    //     w   - q_j   = sqrt2 cos(beta) cos((t1-u)/2)
    //     q_i - e q_k = sqrt2 cos(beta) sin((t1-u)/2)
    //     w   + q_j   = sqrt2 sin(beta) cos((t1+u)/2)
    //     q_i + e q_k = sqrt2 sin(beta) sin((t1+u)/2)
    //   half_middle = beta = t2/2 + pi/4, alpha = (t1-u)/2, delta = (t1+u)/2
    //
    // Taking half_middle from the magnitudes keeps r_ab and r_cd
    // non-negative, so the recovered alpha and delta reproduce the sign of q.
    T a;
    T b;
    T c;
    T d;
    if (proper) {
        a = w;
        b = q_1;
        c = q_2;
        d = q_other;
    } else {
        a = w - q_2;
        b = q_1 - q_other;
        c = w + q_2;
        d = q_1 + q_other;
    }

    const T r_ab        = std::hypot(a, b);
    const T r_cd        = std::hypot(c, d);
    const T half_middle = std::atan2(r_cd, r_ab);
    const T tolerance   = T{8} * std::numeric_limits<T>::epsilon() * (r_ab + r_cd);

    T alpha = std::atan2(b, a);
    T delta = std::atan2(d, c);
    // Gimbal lock: one of alpha / delta is undetermined. Setting it equal to
    // the determined one puts the whole rotation into t1 (t3 = 0).
    const bool lock_ab = (r_ab <= tolerance);
    const bool lock_cd = (r_cd <= tolerance);
    if (lock_cd) {
        delta = alpha;
    } else if (lock_ab) {
        alpha = delta;
    }

    T outer_1;
    T outer_3;
    if (proper) {
        t2      = T{2} * half_middle;
        outer_1 = alpha + delta;
        outer_3 = alpha - delta;
    } else {
        t2      = T{2} * half_middle - glm::half_pi<T>();
        outer_1 = delta + alpha;
        outer_3 = parity * (delta - alpha);
    }
    detail::canonicalize_outer_angles(outer_1, outer_3);
    t1 = outer_1;
    t3 = outer_3;
}

// Like quaternion_to_euler_angles(), but of all the angle triples that give
// q (sign included), returns the one nearest to the reference angles
// (r1, r2, r3), each angle then brought into (-2 pi, 2 pi] - the range over
// which an angle still tells q from -q. An editor showing angles passes the
// angles it shows as the reference: when they still give q they come back
// unchanged (up to rounding) instead of being folded into the canonical
// ranges, and when q moved the angles follow it continuously.
//
// The candidates are the two branches of the Euler decomposition,
//     (t1, t2, t3) and (t1 + pi, pi - t2, t3 + pi) (Tait-Bryan)
//                  or  (t1 + pi,     - t2, t3 + pi) (proper),
// and at gimbal lock the triple that keeps r3, each with any number of full
// turns added to its angles as long as the total number of turns is even
// (one full turn negates the quaternion).
template <typename T, glm::qualifier Q>
void quaternion_to_euler_angles_near(
    const glm::qua<T, Q>& q,
    const int             axis_1,
    const int             axis_2,
    const int             axis_3,
    const T               r1,
    const T               r2,
    const T               r3,
    T&                    t1,
    T&                    t2,
    T&                    t3
)
{
    const T pi     = glm::pi<T>();
    const T two_pi = glm::two_pi<T>();
    const bool proper = (axis_1 == axis_3);
    const T parity = static_cast<T>(detail::axis_pair_parity(axis_1, axis_2));

    T c1;
    T c2;
    T c3;
    quaternion_to_euler_angles(q, axis_1, axis_2, axis_3, c1, c2, c3);

    std::array<glm::vec<3, T, Q>, 3> candidates;
    std::size_t candidate_count = 0;
    candidates[candidate_count++] = glm::vec<3, T, Q>{c1, c2, c3};
    candidates[candidate_count++] = proper
        ? glm::vec<3, T, Q>{c1 + pi, -c2,     c3 + pi}
        : glm::vec<3, T, Q>{c1 + pi, pi - c2, c3 + pi};

    // At gimbal lock only t1 + sigma * t3 is determined; keep the reference's
    // t3 and solve t1 from the canonical triple (whose t3 is 0 there).
    const T lock_tolerance = T{64} * std::numeric_limits<T>::epsilon();
    const T lock_low       = proper ? T{0} : -glm::half_pi<T>();
    const T lock_high      = proper ? pi   :  glm::half_pi<T>();
    T sigma{0};
    if (std::abs(c2 - lock_low) <= lock_tolerance) {
        sigma = proper ? T{1} : -parity;
    } else if (std::abs(c2 - lock_high) <= lock_tolerance) {
        sigma = proper ? T{-1} : parity;
    }
    if (sigma != T{0}) {
        candidates[candidate_count++] = glm::vec<3, T, Q>{c1 + (sigma * c3) - (sigma * r3), c2, r3};
    }

    const glm::vec<3, T, Q> reference{r1, r2, r3};
    T best_distance = std::numeric_limits<T>::max();
    glm::vec<3, T, Q> best{c1, c2, c3};
    for (std::size_t i = 0; i < candidate_count; ++i) {
        glm::vec<3, T, Q> candidate = candidates[i];

        // A branch triple may give -q; one full turn on t1 makes it q.
        const glm::qua<T, Q> composed = euler_angles_to_quaternion<T, Q>(
            axis_1, axis_2, axis_3, candidate[0], candidate[1], candidate[2]
        );
        if (glm::dot(composed, q) < T{0}) {
            candidate[0] += two_pi;
        }

        // Nearest full-turn shift per angle, then restore an even total by
        // moving the angle for which a second-nearest shift costs least.
        std::array<T, 3> turns{};
        int turn_sum = 0;
        for (int a = 0; a < 3; ++a) {
            turns[a] = std::round((reference[a] - candidate[a]) / two_pi);
            turn_sum += static_cast<int>(turns[a]);
        }
        if ((turn_sum % 2) != 0) {
            int cheapest      = 0;
            T   cheapest_cost = std::numeric_limits<T>::max();
            T   cheapest_turn = T{0};
            for (int a = 0; a < 3; ++a) {
                const T exact       = (reference[a] - candidate[a]) / two_pi;
                const T alternative = (exact > turns[a]) ? (turns[a] + T{1}) : (turns[a] - T{1});
                const T nearest_d   = candidate[a] + (turns[a]    * two_pi) - reference[a];
                const T other_d     = candidate[a] + (alternative * two_pi) - reference[a];
                const T cost        = (other_d * other_d) - (nearest_d * nearest_d);
                if (cost < cheapest_cost) {
                    cheapest      = a;
                    cheapest_cost = cost;
                    cheapest_turn = alternative;
                }
            }
            turns[cheapest] = cheapest_turn;
        }
        T distance{0};
        for (int a = 0; a < 3; ++a) {
            candidate[a] += turns[a] * two_pi;
            const T d = candidate[a] - reference[a];
            distance += d * d;
        }
        if (distance < best_distance) {
            best_distance = distance;
            best          = candidate;
        }
    }

    // Into (-2 pi, 2 pi]: shifts of two full turns keep the quaternion.
    const T four_pi = T{2} * two_pi;
    for (int a = 0; a < 3; ++a) {
        while (best[a] > two_pi) {
            best[a] -= four_pi;
        }
        while (best[a] <= -two_pi) {
            best[a] += four_pi;
        }
    }
    t1 = best[0];
    t2 = best[1];
    t3 = best[2];
}

// Named per-order forms, mirroring glm::eulerAngleABC() (which returns a
// matrix) and glm::extractEulerAngleABC() (which takes a matrix).

#define ERHE_MATH_EULER_ORDER(ORDER, AXIS_1, AXIS_2, AXIS_3)                              \
    template <typename T, glm::qualifier Q = glm::defaultp>                               \
    [[nodiscard]] auto quatEulerAngle##ORDER(const T t1, const T t2, const T t3)          \
        -> glm::qua<T, Q>                                                                 \
    {                                                                                     \
        return euler_angles_to_quaternion<T, Q>(AXIS_1, AXIS_2, AXIS_3, t1, t2, t3);     \
    }                                                                                     \
    template <typename T, glm::qualifier Q>                                               \
    void extractEulerAngle##ORDER(const glm::qua<T, Q>& q, T& t1, T& t2, T& t3)           \
    {                                                                                     \
        quaternion_to_euler_angles(q, AXIS_1, AXIS_2, AXIS_3, t1, t2, t3);                \
    }

ERHE_MATH_EULER_ORDER(XYX, 0, 1, 0)
ERHE_MATH_EULER_ORDER(XZX, 0, 2, 0)
ERHE_MATH_EULER_ORDER(YXY, 1, 0, 1)
ERHE_MATH_EULER_ORDER(YZY, 1, 2, 1)
ERHE_MATH_EULER_ORDER(ZXZ, 2, 0, 2)
ERHE_MATH_EULER_ORDER(ZYZ, 2, 1, 2)
ERHE_MATH_EULER_ORDER(XYZ, 0, 1, 2)
ERHE_MATH_EULER_ORDER(XZY, 0, 2, 1)
ERHE_MATH_EULER_ORDER(YXZ, 1, 0, 2)
ERHE_MATH_EULER_ORDER(YZX, 1, 2, 0)
ERHE_MATH_EULER_ORDER(ZYX, 2, 1, 0)
ERHE_MATH_EULER_ORDER(ZXY, 2, 0, 1)

#undef ERHE_MATH_EULER_ORDER

} // namespace erhe::math
