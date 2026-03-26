#include "erhe_geometry/plane_intersection.hpp"

#include <cmath>

namespace erhe::geometry {

Planef::Planef(const GEO::vec3f& p1, const GEO::vec3f& p2, const GEO::vec3f& p3)
{
    GEO::vec3f n = GEO::normalize(cross(p2 - p1, p3 - p1));
    a = n.x;
    b = n.y;
    c = n.z;
    d = -(a * p1.x + b * p1.y + c * p1.z);
}

Planef::Planef(const GEO::vec3f& point, const GEO::vec3f& normal)
{
    a = normal.x;
    b = normal.y;
    c = normal.z;
    d = -(a * point.x + b * point.y + c * point.z);
}

Planef::Planef(float a_in, float b_in, float c_in, float d_in)
    : a{a_in}
    , b{b_in}
    , c{c_in}
    , d{d_in}
{
}

Planef::Planef() = default;

auto Planef::normal() const -> GEO::vec3f
{
    return GEO::vec3f(a, b, c);
}

auto intersect_three_planes_cramer(
    const Planef& a,
    const Planef& b,
    const Planef& c
) -> std::optional<GEO::vec3f>
{
    const GEO::vec3f a_n = a.normal();
    const GEO::vec3f b_n = b.normal();
    const GEO::vec3f c_n = c.normal();
    const GEO::vec3f bxc = GEO::cross(b_n, c_n);
    const GEO::vec3f cxa = GEO::cross(c_n, a_n);
    const GEO::vec3f axb = GEO::cross(a_n, b_n);
    const float      det = GEO::dot(axb, c_n); // scalar triple product
    if (std::abs(det) < 1e-06f) {
        return std::nullopt;
    }
    return (-a.d * bxc - b.d * cxa - c.d * axb) / det;
}

auto intersect_three_planes_least_squares(
    const Planef& plane_a,
    const Planef& plane_b,
    const Planef& plane_c,
    const GEO::vec3f& reference_point
) -> GEO::vec3f
{
    // Each plane equation  n_i . x + d_i = 0  constrains x to a plane.
    // With unit normals, the residual  n_i . x + d_i  is the signed distance.
    //
    // We minimize  sum_i (n_i . x + d_i)^2  =  || A x - b ||^2
    //   where  A = [n_a; n_b; n_c]  (rows = normals)
    //          b = [-d_a; -d_b; -d_c]
    //
    // Normal equations:  M x = c
    //   M = A^T A = sum n_i (x) n_i   (symmetric positive semi-definite)
    //   c = A^T b = sum (-d_i) n_i
    //
    // When M is rank-deficient (planes don't have a unique intersection),
    // there are infinitely many least-squares minimizers. We pick the one
    // closest to reference_point via Tikhonov regularization:
    //
    //   (M + eps I) x = c + eps * P_0
    //
    // This always has a unique solution (M + eps I is positive definite).
    // For well-conditioned systems: x -> M^{-1} c  (exact intersection).
    // For degenerate systems: x -> P_0 in unconstrained directions.
    //
    // Solved via Cholesky decomposition of the 3x3 SPD matrix.

    const GEO::vec3f n_a = plane_a.normal();
    const GEO::vec3f n_b = plane_b.normal();
    const GEO::vec3f n_c = plane_c.normal();

    constexpr float epsilon = 1e-6f;

    // M + eps I = sum n_i (x) n_i + eps I
    const float m00 = (n_a.x * n_a.x) + (n_b.x * n_b.x) + (n_c.x * n_c.x) + epsilon;
    const float m01 = (n_a.x * n_a.y) + (n_b.x * n_b.y) + (n_c.x * n_c.y);
    const float m02 = (n_a.x * n_a.z) + (n_b.x * n_b.z) + (n_c.x * n_c.z);
    const float m11 = (n_a.y * n_a.y) + (n_b.y * n_b.y) + (n_c.y * n_c.y) + epsilon;
    const float m12 = (n_a.y * n_a.z) + (n_b.y * n_b.z) + (n_c.y * n_c.z);
    const float m22 = (n_a.z * n_a.z) + (n_b.z * n_b.z) + (n_c.z * n_c.z) + epsilon;

    // rhs = c + eps * P_0
    const float r0 = (-plane_a.d * n_a.x) + (-plane_b.d * n_b.x) + (-plane_c.d * n_c.x) + (epsilon * reference_point.x);
    const float r1 = (-plane_a.d * n_a.y) + (-plane_b.d * n_b.y) + (-plane_c.d * n_c.y) + (epsilon * reference_point.y);
    const float r2 = (-plane_a.d * n_a.z) + (-plane_b.d * n_b.z) + (-plane_c.d * n_c.z) + (epsilon * reference_point.z);

    // Cholesky decomposition: M + eps I = L * L^T
    const float l00 = std::sqrt(m00);
    const float l10 = m01 / l00;
    const float l20 = m02 / l00;
    const float l11 = std::sqrt(m11 - (l10 * l10));
    const float l21 = (m12 - (l20 * l10)) / l11;
    const float l22 = std::sqrt(m22 - (l20 * l20) - (l21 * l21));

    // Forward substitution: L y = rhs
    const float y0 = r0 / l00;
    const float y1 = (r1 - (l10 * y0)) / l11;
    const float y2 = (r2 - (l20 * y0) - (l21 * y1)) / l22;

    // Back substitution: L^T x = y
    const float x2 = y2 / l22;
    const float x1 = (y1 - (l21 * x2)) / l11;
    const float x0 = (y0 - (l10 * x1) - (l20 * x2)) / l00;

    return GEO::vec3f{x0, x1, x2};
}

} // namespace erhe::geometry
