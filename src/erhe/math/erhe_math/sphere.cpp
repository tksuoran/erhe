#include "erhe_math/sphere.hpp"
#include "erhe_math/math_util.hpp"

#include <glm/gtx/norm.hpp>
#include <glm/gtc/epsilon.hpp>

#include <algorithm>
#include <limits>

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

auto scaled_to_length(const glm::vec3& v, float new_length) -> glm::vec3
{
    float length = glm::length2(v);
    if (length < 1e-6f) {
        return glm::vec3{new_length, 0.0f, 0.0f};
    }

    length = std::sqrt(length);
    float scalar = new_length / length;
    return scalar * v;
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

// Every tolerance below is relative to the magnitude of the coordinates
// involved: float rounding in distance2() and radius * radius grows with that
// magnitude, and an absolute tolerance would be too loose for a small sphere
// and too tight for one far from the origin.
constexpr float float_epsilon = std::numeric_limits<float>::epsilon();

// Tolerance for a sphere/point containment test, in squared-distance units.
// The difference point - center carries an absolute error of about one ulp of
// the coordinate magnitude, which the squaring scales by twice the distance.
[[nodiscard]] auto containment_tolerance(const Sphere& sphere, const glm::vec3& point) -> float
{
    const float magnitude = glm::length(sphere.center) + glm::length(point);
    const float radius    = sphere.radius;
    return 8.0f * float_epsilon * ((radius * magnitude) + (radius * radius));
}

[[nodiscard]] auto contains_within_tolerance(const Sphere& sphere, const glm::vec3& point) -> bool
{
    return glm::distance2(sphere.center, point) <= ((sphere.radius * sphere.radius) + containment_tolerance(sphere, point));
}

// Grows the radius so that the exact test Sphere::contains(point) holds for the point,
// with margin for float rounding at the magnitude of the coordinates.
void grow_to_contain(Sphere& sphere, const glm::vec3& point)
{
    const float distance = glm::distance(sphere.center, point);
    const float padding  = 8.0f * float_epsilon * (distance + glm::length(sphere.center) + glm::length(point));
    sphere.radius = std::max(sphere.radius, distance + padding);
}

// Largest edge length of the vectors, used to normalize a fit so that its
// degeneracy thresholds are relative to the size of the input.
[[nodiscard]] auto largest_length(const glm::vec3& ab, const glm::vec3& ac) -> float
{
    return std::sqrt(std::max(glm::length2(ab), glm::length2(ac)));
}

[[nodiscard]] auto largest_length(const glm::vec3& ab, const glm::vec3& ac, const glm::vec3& ad) -> float
{
    return std::sqrt(std::max(std::max(glm::length2(ab), glm::length2(ac)), glm::length2(ad)));
}

auto fit_sphere_through_points(const glm::vec3& ab_in, const glm::vec3& ac_in, float& s, float& t) -> bool
{
    // Normalize so that the thresholds below are relative to the size of the triangle.
    const float scale = largest_length(ab_in, ac_in);
    if (scale == 0.0f) {
        return false;
    }
    const glm::vec3 ab = ab_in / scale;
    const glm::vec3 ac = ac_in / scale;
    const float BB = glm::dot(ab, ab);
    const float CC = glm::dot(ac, ac);
    const float BC = glm::dot(ab, ac);

    float denom = BB * CC - BC * BC; // == |ab|^2 |ac|^2 sin^2(angle)

    if (denom < 1e-6f) {
        return false;
    }

    denom = 0.5f / denom; // == 1 / (2 * B^2 * C^2 - (BC)^2)

    s = (CC * BB - BC * CC) * denom;
    t = (CC * BB - BC * BB) * denom;

    return true;
}

auto fit_sphere_through_points(const glm::vec3& ab_in, const glm::vec3& ac_in, const glm::vec3& ad_in, float& s, float& t, float& u) -> bool
{
    // Normalize so that the determinant test in inverse_symmetric() is relative
    // to the size of the tetrahedron. s, t and u are invariant under the scaling.
    const float scale = largest_length(ab_in, ac_in, ad_in);
    if (scale == 0.0f) {
        return false;
    }
    const glm::vec3 ab = ab_in / scale;
    const glm::vec3 ac = ac_in / scale;
    const glm::vec3 ad = ad_in / scale;
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
    // Expand the radius so that the containment tests really contain the points, at any
    // distance from the origin.
    grow_to_contain(s, a);
    grow_to_contain(s, b);
    return s;
}

// Keeps the candidate with the smaller radius.
void keep_smaller(Sphere& best, const Sphere& candidate)
{
    if (candidate.radius < best.radius) {
        best = candidate;
    }
}

// The minimal sphere of three points is either their circumscribed circle
// (when its center lies inside the triangle) or the minimal sphere of the
// two farthest points, which then contains the third. Every candidate is
// grown so that the exact containment tests hold, and the smallest one is
// the answer: a candidate that does not contain a point mathematically grows
// past the true minimal sphere, one that does equals it up to rounding.
auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) -> Sphere
{
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;

    Sphere best{.center = a, .radius = std::numeric_limits<float>::infinity()};

    float s = 0.0f;
    float t = 0.0f;
    const bool fitted = fit_sphere_through_points(ab, ac, s, t);
    if (fitted && (s >= 0.0f) && (t >= 0.0f) && ((s + t) <= 1.0f)) {
        // The circumscribed circle, with its center inside the triangle.
        Sphere circle{.center = a + (s * ab) + (t * ac), .radius = 0.0f};
        grow_to_contain(circle, a);
        grow_to_contain(circle, b);
        grow_to_contain(circle, c);
        keep_smaller(best, circle);
    }

    Sphere pair = optimal_enclosing_sphere(a, b);
    grow_to_contain(pair, c);
    keep_smaller(best, pair);

    pair = optimal_enclosing_sphere(a, c);
    grow_to_contain(pair, b);
    keep_smaller(best, pair);

    pair = optimal_enclosing_sphere(b, c);
    grow_to_contain(pair, a);
    keep_smaller(best, pair);

    return best;
}

// The minimal sphere of four points is either their circumscribed sphere
// (when its center lies inside the tetrahedron) or the minimal sphere of
// three of them, which then contains the fourth; see the three point case
// for why the smallest grown candidate is the answer.
auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) -> Sphere
{
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ad = d - a;

    Sphere best{.center = a, .radius = std::numeric_limits<float>::infinity()};

    float s = 0.0f;
    float t = 0.0f;
    float u = 0.0f;
    const bool fitted = fit_sphere_through_points(ab, ac, ad, s, t, u);
    if (fitted && (s >= 0.0f) && (t >= 0.0f) && (u >= 0.0f) && ((s + t + u) <= 1.0f)) {
        Sphere circumsphere{.center = a + (s * ab) + (t * ac) + (u * ad), .radius = 0.0f};
        grow_to_contain(circumsphere, a);
        grow_to_contain(circumsphere, b);
        grow_to_contain(circumsphere, c);
        grow_to_contain(circumsphere, d);
        keep_smaller(best, circumsphere);
    }

    Sphere triple = optimal_enclosing_sphere(a, b, c);
    grow_to_contain(triple, d);
    keep_smaller(best, triple);

    triple = optimal_enclosing_sphere(a, b, d);
    grow_to_contain(triple, c);
    keep_smaller(best, triple);

    triple = optimal_enclosing_sphere(a, c, d);
    grow_to_contain(triple, b);
    keep_smaller(best, triple);

    triple = optimal_enclosing_sphere(b, c, d);
    grow_to_contain(triple, a);
    keep_smaller(best, triple);

    return best;
}

// The minimal sphere of five points is the minimal sphere of four of them
// that contains the fifth. The first candidate found to contain its fifth
// point within rounding tolerance is it: the sphere contains all five, and
// the minimal sphere of a subset is never larger than that of the set. When
// rounding leaves every candidate short, the smallest candidate grown to
// reach its fifth point is the answer up to rounding, by the argument of the
// three point case.
auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& e, std::size_t& redundant_point) -> Sphere
{
    const glm::vec3* points[5] = {&a, &b, &c, &d, &e};
    Sphere best{.center = a, .radius = std::numeric_limits<float>::infinity()};
    std::size_t best_redundant = 4;
    for (std::size_t redundant = 0; redundant < 5; ++redundant) {
        const glm::vec3* support[4];
        std::size_t count = 0;
        for (std::size_t i = 0; i < 5; ++i) {
            if (i != redundant) {
                support[count++] = points[i];
            }
        }
        Sphere candidate = optimal_enclosing_sphere(*support[0], *support[1], *support[2], *support[3]);
        if (contains_within_tolerance(candidate, *points[redundant])) {
            grow_to_contain(candidate, *points[redundant]);
            redundant_point = redundant;
            return candidate;
        }
        grow_to_contain(candidate, *points[redundant]);
        if (candidate.radius < best.radius) {
            best           = candidate;
            best_redundant = redundant;
        }
    }
    redundant_point = best_redundant;
    return best;
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
    for (std::size_t i = 4; i < num_points; ++i) {
        if (i == sp[0] || i == sp[1] || i == sp[2] || i == sp[3]) {
            continue; // Take care not to add the same point twice to the support set.
        }

        // If the next point (pts[i]) does not fit inside the currently computed minimal sphere, compute
        // a new minimal sphere that also contains pts[i].
        if (!contains_within_tolerance(s, pts[i])) {
            std::size_t redundant;
            s = optimal_enclosing_sphere(pts[sp[0]], pts[sp[1]], pts[sp[2]], pts[sp[3]], pts[i], redundant);
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
                // since our guess for the minimal enclosing sphere changed. The loop increment
                // brings i to 0, so the first point is checked again too.
                i = std::numeric_limits<std::size_t>::max();
            }
        }
    }

    // When the redundant support point could not be evicted above (the cycle guard),
    // the sphere still changed without a restart, so points checked before it may lie
    // outside. Enclose every point left outside by growing the sphere toward it; the
    // result is an enclosing sphere for every input, minimal up to those cases.
    for (const glm::vec3& point : pts) {
        if (s.contains(point)) {
            continue;
        }
        const glm::vec3 to_point   = point - s.center;
        const float     distance   = glm::length(to_point);
        const float     new_radius = 0.5f * (s.radius + distance);
        s.center += to_point * ((new_radius - s.radius) / distance);
        s.radius  = new_radius;
        grow_to_contain(s, point);
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

void Sphere::enclose(const glm::vec3& point, float epsilon)
{
	glm::vec3 d = point - center;
	float dist2 = glm::length2(d);
	if (dist2 + epsilon > radius * radius) {
#ifdef MATH_ASSERT_CORRECTNESS
		Sphere copy = *this;
#endif
		float dist = std::sqrt(dist2);
		float halfDist = (dist - radius) * 0.5f;
		// Nudge this Sphere towards the target point. Add half the missing distance to radius,
		// and the other half to position. This gives a tighter enclosure, instead of if
		// the whole missing distance were just added to radius.
		center += d * halfDist / dist;
		radius += halfDist + 1e-4f; // Use a fixed epsilon deliberately, the param is a squared epsilon, so different order of magnitude.
#ifdef MATH_ASSERT_CORRECTNESS
		mathassert(this->Contains(copy, epsilon));
#endif
	}

	// assume(this->Contains(point));
}

void Sphere::enclose(const Sphere& sphere)
{
	// To enclose another sphere into this sphere, we only need to enclose two points:
	// 1) Enclose the farthest point on the other sphere into this sphere.
	// 2) Enclose the opposite point of the farthest point into this sphere.
	glm::vec3 to_farthest_point = scaled_to_length(sphere.center - center, sphere.radius);
	enclose(sphere.center + to_farthest_point);
	enclose(sphere.center - to_farthest_point);

	// assume(this->Contains(sphere));
}

} // namespace erhe::math
