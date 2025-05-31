#include "erhe_math/sphere.hpp"
#include "erhe_math/math_util.hpp"

#include <glm/gtx/norm.hpp>

// Adapted from https://github.com/juj/MathGeoLib

namespace erhe::math {

auto min(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) -> glm::vec3
{
    return glm::min(glm::min(a, b), c);
}

auto max(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) -> glm::vec3
{
    return glm::max(glm::max(a, b), c);
}

auto max(float a, float b, float c) -> float
{
    return std::max(std::max(a, b), c);
}

auto max(float a, float b, float c, float d) -> float
{
    return std::max(std::max(a, b), std::max(c, d));
}

// I did not verify this is was correctly converted from MathGeoLib to glm.
bool inverse_symmetric(glm::mat3& m)
{
    // Check symmetry: optional if you know it is symmetric
    // if (!(m[1][0] == m[0][1] && m[2][0] == m[0][2] && m[2][1] == m[1][2]))
    //     return false;

    const float a = m[0][0];
    const float b = m[1][0]; // == m[0][1]
    const float c = m[2][0]; // == m[0][2]
    const float d = m[1][1];
    const float e = m[2][1]; // == m[1][2]
    const float f = m[2][2];

    const float df_ee = d * f - e * e;
    const float ce_bf = c * e - b * f;
    const float be_dc = b * e - d * c;

    float det = a * df_ee + b * ce_bf + c * be_dc;
    if (glm::epsilonEqual(det, 0.0f, 1e-6f)) {
        return false;
    }

    det = 1.0f / det;

    glm::mat3 inv;

    inv[0][0] = det * df_ee;
    inv[1][0] = inv[0][1] = det * ce_bf;
    inv[2][0] = inv[0][2] = det * be_dc;
    inv[1][1] = det * (a * f - c * c);
    inv[2][1] = inv[1][2] = det * (c * b - a * e);
    inv[2][2] = det * (a * d - b * b);

    m = inv;
    return true;
}

auto Sphere::contains(const glm::vec3& point) const -> bool
{
    return glm::distance2(center, point) <= (radius * radius);
}

auto Sphere::contains(const glm::vec3& point, float epsilon) const -> bool
{
    return glm::distance2(center, point) <= (radius * radius + epsilon);
}

// The epsilon value used for enclosing sphere computations.
static const float s_epsilon = 1e-3f;

auto fit_sphere_through_points(const glm::vec3& ab, const glm::vec3& ac, float& s, float& t) -> bool
{
    const float BB = glm::dot(ab, ab);
    const float CC = glm::dot(ac, ac);
    const float BC = glm::dot(ab, ac);

    float denom = BB * CC - BC * BC;

    if (glm::epsilonEqual(denom, 0.f, 5e-3f)) {
        return false;
    }

    denom = 0.5f / denom; // == 1 / (2 * B^2 * C^2 - (BC)^2)

    s = (CC * BB - BC * CC) * denom;
    t = (CC * BB - BC * BB) * denom;

    return true;
}

auto fit_sphere_through_points(const glm::vec3& ab, const glm::vec3& ac, const glm::vec3& ad, float& s, float& t, float& u) -> bool
{
    const float BB = glm::dot(ab, ab);
    const float BC = glm::dot(ab, ac);
    const float BD = glm::dot(ab, ad);
    const float CC = glm::dot(ac, ac);
    const float CD = glm::dot(ac, ad);
    const float DD = glm::dot(ad, ad);

    glm::mat3 m;
    m[0][0] = BB; m[0][1] = BC; m[0][2] = BD;
    m[1][0] = BC; m[1][1] = CC; m[1][2] = CD;
    m[2][0] = BD; m[2][1] = CD; m[2][2] = DD;
    bool success = inverse_symmetric(m);
    if (!success) {
        return false;
    }
    glm::vec3 v = m * glm::vec3{BB * 0.5f, CC * 0.5f, DD * 0.5f};
    s = v.x;
    t = v.y;
    u = v.z;
    return true;
}

auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b) -> Sphere
{
    Sphere s{
        .center = (a + b) * 0.5f,
        .radius = glm::distance(b, s.center)
    };
    assert(std::isfinite(s.radius));
    assert(s.radius >= 0.f);

    // Allow floating point inconsistency and expand the radius by a small epsilon so that the containment tests
    // really contain the points (note that the points must be sufficiently near enough to the origin)
    s.radius += s_epsilon;

    assert(s.contains(a));
    assert(s.contains(b));
    return s;
}

auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) -> Sphere
{
    Sphere sphere;

    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;

    float s = 0.0f;
    float t = 0.0f;
    bool are_collinear = glm::length2(glm::cross(ab, ac)) < 1e-4f; // Manually test that we don't try to fit sphere to three collinear points.
    bool success = !are_collinear && fit_sphere_through_points(ab, ac, s, t);
    if (!success || std::abs(s) > 10000.f || std::abs(t) > 10000.f) { // If s and t are very far from the triangle, do a manual box fitting for numerical stability.
        glm::vec3 min_pt = min(a, b, c);
        glm::vec3 max_pt = max(a, b, c);
        sphere.center = (min_pt + max_pt) * 0.5f;
        sphere.radius = glm::distance(sphere.center, min_pt);
    } else if (s < 0.0f) {
        sphere.center = (a + c) * 0.5f;
        sphere.radius = glm::distance(a, c) * 0.5f;
        sphere.radius = std::max( // For numerical stability, expand the radius of the sphere so it certainly contains the third point.
            sphere.radius,
            glm::distance(b, sphere.center)
        );
    } else if (t < 0.f) {
        sphere.center = (a + b) * 0.5f;
        sphere.radius = glm::distance(a, b) * 0.5f;
        sphere.radius = std::max( // For numerical stability, expand the radius of the sphere so it certainly contains the third point.
            sphere.radius,
            glm::distance(c, sphere.center)
        );
    } else if (s + t > 1.f) {
        sphere.center = (b + c) * 0.5f;
        sphere.radius = glm::distance(b, c) * 0.5f;
        sphere.radius = std::max( // For numerical stability, expand the radius of the sphere so it certainly contains the third point.
            sphere.radius,
            glm::distance(a, sphere.center)
        );
    } else {
        const glm::vec3 center = s * ab + t * ac;
        sphere.center = a + center;
        // Mathematically, the following would be correct, but it suffers from floating point inaccuracies,
        // since it only tests distance against one point.
        // sphere.radius = glm::length(center);

        // For robustness, take the radius to be the distance to the farthest point (though the distance are all
        // equal).
        sphere.radius = std::sqrt(
            max(
                glm::distance2(sphere.center, a),
                glm::distance2(sphere.center, b),
                glm::distance2(sphere.center, c)
            )
        );
    }

    // Allow floating point inconsistency and expand the radius by a small epsilon so that the containment tests
    // really contain the points (note that the points must be sufficiently near enough to the origin)
    sphere.radius += 2.f * s_epsilon; // We test against one epsilon, so expand by two epsilons.

#ifdef MATH_ASSERT_CORRECTNESS
    if (!sphere.contains(a, s_epsilon) || !sphere.contains(b, s_epsilon) || !sphere.contains(c, s_epsilon))
    {
        LOGE("Pos: {}, r: {}", sphere.center, sphere.radius);
        LOGE("A: {}, dist: {}", a, glm::distance(a, sphere.center));
        LOGE("B: {}, dist: {}", b, glm::distance(b, sphere.center));
        LOGE("C: {}, dist: {}", c, glm::distance(c, sphere.center));
        ERGE_FATAL(false);
    }
#endif
    return sphere;
}

auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) -> Sphere
{
    Sphere sphere;

    float s,t,u;
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ad = d - a;
    bool success = fit_sphere_through_points(ab, ac, ad, s, t, u);
    if (!success || s < 0.0f || t < 0.0f || u < 0.0f || s + t + u > 1.0f) {
        sphere = optimal_enclosing_sphere(a, b, c);
        if (!sphere.contains(d)) {
            sphere = optimal_enclosing_sphere(a, b, d);
            if (!sphere.contains(c)) {
                sphere = optimal_enclosing_sphere(a, c, d);
                if (!sphere.contains(b)) {
                    sphere = optimal_enclosing_sphere(b, c, d);
                    sphere.radius = std::max( // For numerical stability, expand the radius of the sphere so it certainly contains the fourth point.
                        sphere.radius,
                        glm::distance(a, sphere.center) + 1e-3f
                    );
                    assert(sphere.contains(a));
                }
            }
        }
    } else { // The fitted sphere is inside the convex hull of the vertices (a,b,c,d), so it must be optimal.
        const glm::vec3 center = s*ab + t*ac + u*ad;

        sphere.center = a + center;
        // Mathematically, the following would be correct, but it suffers from floating point inaccuracies,
        // since it only tests distance against one point.
        //sphere.r = center.Length();

        // For robustness, take the radius to be the distance to the farthest point (though the distance are all
        // equal).
        sphere.radius = std::sqrt(
            max(
                glm::distance2(sphere.center, a),
                glm::distance2(sphere.center, b),
                glm::distance2(sphere.center, c),
                glm::distance2(sphere.center, d)
            )
        );
    }

    // Allow floating point inconsistency and expand the radius by a small epsilon so that the containment tests
    // really contain the points (note that the points must be sufficiently near enough to the origin)
    sphere.radius += 2.0f * s_epsilon; // We test against one epsilon, so expand using 2 epsilons.

#ifdef MATH_ASSERT_CORRECTNESS
    if (!sphere.Contains(a, sEpsilon) || !sphere.Contains(b, sEpsilon) || !sphere.Contains(c, sEpsilon) || !sphere.Contains(d, sEpsilon))
    {
        LOGE("Pos: %s, r: %f", sphere.pos.ToString().c_str(), sphere.r);
        LOGE("A: %s, dist: %f", a.ToString().c_str(), a.Distance(sphere.pos));
        LOGE("B: %s, dist: %f", b.ToString().c_str(), b.Distance(sphere.pos));
        LOGE("C: %s, dist: %f", c.ToString().c_str(), c.Distance(sphere.pos));
        LOGE("D: %s, dist: %f", d.ToString().c_str(), d.Distance(sphere.pos));
        mathassert(false);
    }
#endif

    return sphere;
}

auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& e, std::size_t& redundant_point) -> Sphere
{
    Sphere s = optimal_enclosing_sphere(b, c, d, e);
    if (s.contains(a, s_epsilon)) {
        redundant_point = 0;
        return s;
    }

    s = optimal_enclosing_sphere(a, c, d, e);
    if (s.contains(b, s_epsilon)) {
        redundant_point = 1;
        return s;
    }

    s = optimal_enclosing_sphere(a, b, d, e);
    if (s.contains(c, s_epsilon)) {
        redundant_point = 2;
        return s;
    }

    s = optimal_enclosing_sphere(a, b, c, e);
    if (s.contains(d, s_epsilon)) {
        redundant_point = 3;
        return s;
    }

    s = optimal_enclosing_sphere(a,b,c,d);
    assert(s.contains(e, s_epsilon));
    redundant_point = 4;
    return s;
}

auto optimal_enclosing_sphere(const std::vector<glm::vec3>& pts) -> Sphere
{
    const std::size_t num_points = pts.size();

    // If we have only a small number of points, can solve with a specialized function.
    switch (num_points) {
        case 0: return Sphere{};
        case 1: return Sphere{pts[0], 0.0f}; // Sphere around a single point will be degenerate with r = 0.
        case 2: return optimal_enclosing_sphere(pts[0], pts[1]);
        case 3: return optimal_enclosing_sphere(pts[0], pts[1], pts[2]);
        case 4: return optimal_enclosing_sphere(pts[0], pts[1], pts[2], pts[3]);
        default: break;
    }

    // The set of supporting points for the minimal sphere. Even though the minimal enclosing
    // sphere might have 2, 3 or 4 points in its support (sphere surface), always store here
    // indices to exactly four points.
    std::size_t sp[4] = { 0, 1, 2, 3 };

    // Due to numerical issues, it can happen that the minimal sphere for four points {a,b,c,d} does not
    // accommodate a fifth point e, but replacing any of the points a-d from the support with the point e
    // does not accommodate the all the five points either.
    // Therefore, keep a set of flags for each support point to avoid going in cycles, where the same
    // set of points are again and again added and removed from the support, causing an infinite loop.
    bool expendable[4] = { true, true, true, true };

    // The so-far constructed minimal sphere.
    Sphere s = optimal_enclosing_sphere(pts[sp[0]], pts[sp[1]], pts[sp[2]], pts[sp[3]]);
    float r_squared = s.radius * s.radius + s_epsilon;
    for(int i = 4; i < num_points; ++i) {
        if (i == sp[0] || i == sp[1] || i == sp[2] || i == sp[3]) {
            continue; // Take care not to add the same point twice to the support set.
        }

        // If the next point (pts[i]) does not fit inside the currently computed minimal sphere, compute
        // a new minimal sphere that also contains pts[i].
        if (glm::distance2(pts[i], s.center) > r_squared) {
            std::size_t redundant;
            s = optimal_enclosing_sphere(pts[sp[0]], pts[sp[1]], pts[sp[2]], pts[sp[3]], pts[i], redundant);
            r_squared = s.radius * s.radius + s_epsilon;
            // A sphere is uniquely defined by four points, so one of the five points passed in above is
            // now redundant, and can be removed from the support set.
            if (
                (redundant != 4) && 
                (
                    (sp[redundant] < i) || 
                    expendable[redundant]
                )
            ) {
                sp[redundant] = i; // Replace the old point with the new one.
                expendable[redundant] = false; // This new one cannot be evicted (until we proceed past it in the input list later)
                // Mark all points in the array before this index "expendable", meaning that they can be removed from the support set.
                if (sp[0] < i) expendable[0] = true;
                if (sp[1] < i) expendable[1] = true;
                if (sp[2] < i) expendable[2] = true;
                if (sp[3] < i) expendable[3] = true;

                // Have to start all over and make sure all old points also lie inside this new sphere,
                // since our guess for the minimal enclosing sphere changed.
                i = 0;
            }
        }
    }

    return s;
}

[[nodiscard]] auto Sphere::transformed_by(const glm::mat4& m) const -> Sphere
{
    const glm::vec4 transformed_center =           m * glm::vec4(center, 1.0f);
    const glm::vec3 scale_x            = glm::vec3(m * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    const glm::vec3 scale_y            = glm::vec3(m * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    const glm::vec3 scale_z            = glm::vec3(m * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));

    // Find the maximum scale factor to conservatively scale the radius
    const float max_scale = std::max(
        {
            glm::length(scale_x),
            glm::length(scale_y),
            glm::length(scale_z)
        }
    );

    return Sphere{
        .center = glm::vec3(transformed_center),
        .radius = radius * max_scale
    };
}

} // namespace erhe::math
