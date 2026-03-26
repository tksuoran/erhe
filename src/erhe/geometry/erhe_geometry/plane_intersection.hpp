#pragma once

#include <geogram/basic/geometry.h>

#include <optional>

namespace erhe::geometry {

class Planef
{
public:
    Planef(const GEO::vec3f& p1, const GEO::vec3f& p2, const GEO::vec3f& p3);
    Planef(const GEO::vec3f& point, const GEO::vec3f& normal);
    Planef(float a, float b, float c, float d);
    Planef();

    [[nodiscard]] auto normal() const -> GEO::vec3f;

    float a{0.0f};
    float b{0.0f};
    float c{0.0f};
    float d{0.0f};
};

// Original Cramer's rule method.
// Returns the exact intersection point of three planes, or nullopt
// if the planes are nearly coplanar (det < threshold).
[[nodiscard]] auto intersect_three_planes_cramer(
    const Planef& plane_a,
    const Planef& plane_b,
    const Planef& plane_c
) -> std::optional<GEO::vec3f>;

// Least-squares method with Tikhonov regularization.
// Returns the point that minimizes the sum of squared distances to all
// three planes. When the system is degenerate (planes don't have a unique
// intersection), returns the minimizer closest to reference_point.
// Always returns a finite result.
[[nodiscard]] auto intersect_three_planes_least_squares(
    const Planef& plane_a,
    const Planef& plane_b,
    const Planef& plane_c,
    const GEO::vec3f& reference_point
) -> GEO::vec3f;

} // namespace erhe::geometry
