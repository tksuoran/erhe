#include "erhe_math/math_util.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_math/sphere.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/pca.hpp>

#include <quickhull/QuickHull.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stack>
#include <utility>

namespace erhe::math {

Bounding_volume_source::~Bounding_volume_source() = default;

using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::dmat4;
using glm::dvec2;
using glm::dvec3;
using glm::dvec4;

namespace {
    constexpr float epsilon         {1e-6f};
    constexpr float pi_minus_epsilon{glm::pi<float>() - epsilon};
}

auto create_frustum(
    const float       left,
    const float       right,
    const float       bottom,
    const float       top,
    const float       z_near,
    const float       z_far,
    const Depth_range depth_range
) -> mat4
{
    const float width          = right  - left;
    const float height         = top    - bottom;
    const float near_minus_far = z_near - z_far;
    const float far_minus_near = z_far  - z_near;
    if ((width == 0.0) || (height == 0.0f) || (near_minus_far == 0.0f) || (far_minus_near == 0.0f)) {
        return glm::mat4{1.0f}; // TODO Log warning
    }
    const float x =  (2.0f  * z_near) / width;
    const float y =  (2.0f  * z_near) / height;
    const float a =  (right + left   ) / width;
    const float b =  (top   + bottom ) / height;
    const float c = (depth_range == Depth_range::zero_to_one)
        ?   z_far                   / (z_near - z_far )   // zero to one
        : -(z_far + z_near        ) / (z_far  - z_near);  // negative one to one
    const float d = (depth_range == Depth_range::zero_to_one)
        ? -(z_far * z_near        ) / (z_far  - z_near)   // zero to one
        : -(2.0f  * z_far * z_near) / (z_far  - z_near);  // negative one to one

    return mat4{
        x, 0, 0, 0,
        0, y, 0, 0,
        a, b, c, -1.0f,
        0, 0, d, 0,
    };
}

auto create_frustum_infinite_far(
    const float       left,
    const float       right,
    const float       bottom,
    const float       top,
    const float       z_near,
    const Depth_range depth_range
) -> mat4
{
    const float width  = right  - left;
    const float height = top    - bottom;
    if ((width == 0.0) || (height == 0.0f)) {
        return glm::mat4{1.0f}; // TODO Log warning
    }

    assert(z_near >= -0.0f);
    const float x =  (2.0f  * z_near) / width;
    const float y =  (2.0f  * z_near) / height;
    const float a =  (right + left  ) / width;
    const float b =  (top   + bottom) / height;
    const float c = -1.0f; // same for both depth ranges
    const float d = (depth_range == Depth_range::zero_to_one)
        ? -z_near          // zero to one
        : -2.0f * z_near;  // negative one to one

    return mat4{
        x, 0, 0, 0,
        0, y, 0, 0,
        a, b, c, -1.0f,
        0, 0, d, 0
    };
}

auto create_frustum_simple(
    const float       width,
    const float       height,
    const float       z_near,
    const float       z_far,
    const Depth_range depth_range
) -> mat4
{
    return create_frustum(-width / 2.0f, width / 2.0f, -height / 2.0f, height / 2.0f, z_near, z_far, depth_range);
}

auto create_perspective(
    const float       fov_x,
    const float       fov_y,
    const float       z_near,
    const float       z_far,
    const Depth_range depth_range
) -> mat4
{
    const auto fov_y_clamped    = std::min(std::max(fov_y, epsilon), pi_minus_epsilon);
    const auto fov_x_clamped    = std::min(std::max(fov_x, epsilon), pi_minus_epsilon);
    const auto tan_x_half_angle = std::tan(fov_x_clamped * 0.5f);
    const auto tan_y_half_angle = std::tan(fov_y_clamped * 0.5f);
    const auto width            = 2.0f * z_near * tan_x_half_angle;
    const auto height           = 2.0f * z_near * tan_y_half_angle;
    return create_frustum_simple(width, height, z_near, z_far, depth_range);
}

auto create_perspective_xr(
    const float       fov_left,
    const float       fov_right,
    const float       fov_up,
    const float       fov_down,
    const float       z_near,
    const float       z_far,
    const Depth_range depth_range
) -> glm::mat4
{
    const auto fov_left_clamped  = std::min(std::max(fov_left,  -pi_minus_epsilon), pi_minus_epsilon);
    const auto fov_right_clamped = std::min(std::max(fov_right, -pi_minus_epsilon), pi_minus_epsilon);
    const auto fov_up_clamped    = std::min(std::max(fov_up,    -pi_minus_epsilon), pi_minus_epsilon);
    const auto fov_down_clamped  = std::min(std::max(fov_down,  -pi_minus_epsilon), pi_minus_epsilon);
    const auto tan_left          = std::tan(fov_left_clamped );
    const auto tan_right         = std::tan(fov_right_clamped);
    const auto tan_up            = std::tan(fov_up_clamped   );
    const auto tan_down          = std::tan(fov_down_clamped );
    const auto left              = z_near * tan_left;
    const auto right             = z_near * tan_right;
    const auto up                = z_near * tan_up;
    const auto down              = z_near * tan_down;
    return create_frustum(left, right, down, up, z_near, z_far, depth_range);
}

auto create_perspective_vertical(
    const float       fov_y,
    const float       aspect_ratio,
    const float       z_near,
    const float       z_far,
    const Depth_range depth_range
) -> mat4
{
    if (aspect_ratio == 0.0f) {
        return glm::mat4{1.0f}; // TODO log warning
    }
    const float fov_y_clamped    = std::min(std::max(fov_y, epsilon), pi_minus_epsilon);
    const auto  tan_half_angle   = std::tan(fov_y_clamped * 0.5f);
    const auto  height           = 2.0f * z_near * tan_half_angle;
    const auto  width            = height * aspect_ratio;
    return create_frustum_simple(width, height, z_near, z_far, depth_range);
}

auto create_perspective_horizontal(
    const float       fov_x,
    const float       aspect_ratio,
    const float       z_near,
    const float       z_far,
    const Depth_range depth_range
) -> mat4
{
    if (aspect_ratio == 0.0f) {
        return glm::mat4{1.0f}; // TODO log warning
    }
    const auto fov_x_clamped  = std::min(std::max(fov_x, epsilon), pi_minus_epsilon);
    const auto tan_half_angle = std::tan(fov_x_clamped * 0.5f);
    const auto width          = 2.0f * z_near * tan_half_angle;
    const auto height         = width / aspect_ratio;
    return create_frustum_simple(width, height, z_near, z_far, depth_range);
}

// http://and-what-happened.blogspot.com/p/just-formulas.html
// The projection produced by this formula has x, y and z extents of -1:+1.
// The perspective control value p is not restricted to integer values.
// The view plane is defined by z.
// Objects on the view plane will have a homogeneous w value of 1.0 after the transform.
auto create_projection(
    const float s,                // Stereo-scopic 3D eye separation
    const float p,                // Perspective (0 == parallel, 1 == perspective)
    const float n, const float f, // Near and z_far z clip depths
    const float w, const float h, // Width and height of viewport (at depth vz)
    const vec3  c,                // Center of viewport
    const vec3  e                 // Center of projection (eye position)
) -> mat4
{
    const float flip_handed = -1.0f;
    return mat4{
          2.0f / w,
          0.0f,
        ((2.0f * (e.x - c.x) + s) / (w * (c.z - e.z))) * flip_handed,
         (2.0f * ((c.x * e.z) - (e.x * c.z)) - s * c.z) / (w * (c.z - e.z)),

         0.0f,
         2.0f / h,
        (2.0f * (e.y - c.y) / (h * (c.z - e.z))) * flip_handed,
         2.0f * ((c.y * e.z) - (e.y * c.z)) / (h * (c.z - e.z)),

         0.0f,
         0.0f,
        ((2.0f * (c.z * (1.0f - p) - e.z) + p * (f + n)) / ((f - n) * (c.z - e.z))) * flip_handed,
        -((c.z * (1.0f - p) - e.z) * (f + n) + 2.0f * f * n * p) / ((f - n) * (c.z - e.z)),

        0.0f,
        0.0f,
        (p / (c.z - e.z)) * flip_handed,
        (c.z * (1.0f - p) - e.z) / (c.z - e.z)

    };
}

auto create_orthographic(
    const float       left,
    const float       right,
    const float       bottom,
    const float       top,
    const float       z_near,
    const float       z_far,
    const Depth_range depth_range
) -> mat4
{
    const float width          = right  - left;
    const float height         = top    - bottom;
    const float far_minus_near = z_far  - z_near;
    if ((width == 0.0) || (height == 0.0f) || (far_minus_near == 0.0f)) {
        return glm::mat4{1.0f}; // TODO Log warning
    }
    const float c_diag  = (depth_range == Depth_range::zero_to_one)
        ? -1.0f / far_minus_near                // zero to one
        : -2.0f / far_minus_near;               // negative one to one
    const float c_trans = (depth_range == Depth_range::zero_to_one)
        ? -z_near / far_minus_near              // zero to one
        : -(z_far + z_near) / far_minus_near;   // negative one to one
    return mat4{
        {
            2.0f / width,
            0.0f,
            0.0f,
            0.0f
        },
        {
            0.0f,
            2.0f / height,
            0.0f,
            0.0f
        },
        {
            0.0f,
            0.0f,
            c_diag,
            0.0f
        },
        {
            -(right + left) / width,
            -(top + bottom) / height,
            c_trans,
            1.0f
        }
    };
}

auto create_orthographic_centered(
    const float       width,
    const float       height,
    const float       z_near,
    const float       z_far,
    const Depth_range depth_range
) -> mat4
{
    // TODO Check bottom and top
    return create_orthographic(
        -width  / 2.0f,  width  / 2.0f,
         height / 2.0f, -height / 2.0f,
        z_near, z_far, depth_range
    );
}


// Creates world_from_view matrix
auto create_look_at(const vec3 eye, const vec3 center, const vec3 up0) -> mat4
{
#if 0
    // glm (and pratically everything since glu) creates view_from_world matrix.
    return glm::lookAt(eye, center, up0);
#else
    if (eye == center) {
        return mat4{1};
    }

    // TODO Verify that up0 has some reasonable length (that it is not (0,0,0)
    const vec3 back  = glm::normalize(eye - center);
    const vec3 up1   = (up0 == back) ? erhe::math::min_axis<float>(back) : up0;
    const vec3 right = glm::normalize(glm::cross(up1, back));
    const vec3 up    = glm::cross(back, right);

    return mat4{
        vec4{right, 0.0f},
        vec4{up,    0.0f},
        vec4{back,  0.0f},
        vec4{eye,   1.0f}
    };
#endif
}

void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b)
{
    r = 0.0f;
    g = 0.0f;
    b = 0.0f;

    // cppcheck-suppress variableScope
    float f;
    // cppcheck-suppress variableScope
    float p;
    // cppcheck-suppress variableScope
    float q;
    // cppcheck-suppress variableScope
    float t;
    // cppcheck-suppress variableScope
    int   i;

    if (s == 0.0f) {
        r = v;
        g = v;
        b = v;
    } else {
        if (h == 360.0f) {
            h = 0.0f;
        }

        h = h / 60.0f;
        i = static_cast<int>(h);
        f = h - static_cast<float>(i);
        p = v * (1.0f - s);
        q = v * (1.0f - (s * f));
        t = v * (1.0f - (s * (1.0f - f)));

        switch (i) {
            case 0: {
                r = v;
                g = t;
                b = p;
                break;
            }

            case 1: {
                r = q;
                g = v;
                b = p;
                break;
            }

            case 2: {
                r = p;
                g = v;
                b = t;
                break;
            }

            case 3: {
                r = p;
                g = q;
                b = v;
                break;
            }

            case 4: {
                r = t;
                g = p;
                b = v;
                break;
            }

            case 5: {
                r = v;
                g = p;
                b = q;
                break;
            }

            default: {
                r = 1.0f;
                g = 1.0f;
                b = 1.0f;
                break;
            }
        }
    }
}

void rgb_to_hsv(const float r, const float g, const float b, float& h, float& s, float& v)
{
    h = 0;
    s = 1.0f;

    float max;
    float min;

    if (r > g) {
        max = r;
    } else {
        max = g;
    }

    if (b > max) {
        max = b;
    }

    if (r < g) {
        min = r;
    } else {
        min = g;
    }

    if (b < min) {
        min = b;
    }

    float diff = max - min;
    v    = max;

    if (max < 0.0001f) {
        s = 0;
    } else {
        s = diff / max;
    }

    if (s == 0) {
        h = 0.0f;
    } else {
        float r_dist = (max - r) / diff;
        float g_dist = (max - g) / diff;
        float b_dist = (max - b) / diff;
        if (r == max) {
            h = b_dist - g_dist;
        } else if (g == max) {
            h = 2.0f + r_dist - b_dist;
        } else if (b == max) { // TODO CLion claims condition is always true
            h = 4.0f + g_dist - r_dist;
        }
        h = h * 60.0f;
        if (h < 0) {
            h += 360.0f;
        }
    }
}

auto srgb_to_linear_rgb(const glm::vec3 srgb) -> glm::vec3
{
    return vec3{
        erhe::dataformat::srgb_to_linear(srgb.x),
        erhe::dataformat::srgb_to_linear(srgb.y),
        erhe::dataformat::srgb_to_linear(srgb.z)
    };
}

auto linear_rgb_to_srgb(const glm::vec3 linear_rgb) -> glm::vec3
{
    return vec3{
        erhe::dataformat::linear_rgb_to_srgb(linear_rgb.x),
        erhe::dataformat::linear_rgb_to_srgb(linear_rgb.y),
        erhe::dataformat::linear_rgb_to_srgb(linear_rgb.z)
    };
}

void cartesian_to_heading_elevation(const vec3 v, float& out_elevation, float& out_heading)
{
    out_elevation = -std::asin(v.y);
    out_heading   =  std::atan2(v.x, v.z);
}

void cartesian_to_spherical_iso(const vec3 v, float& out_theta, float& out_phi)
{
    out_theta = std::acos(glm::clamp(v.z, -1.0f, 1.0f));
    out_phi   = std::atan2(v.y, v.x);
}

auto spherical_to_cartesian_iso(const float theta, const float phi) -> vec3
{
    return vec3{
        std::sin(theta) * std::cos(phi),
        std::sin(theta) * std::sin(phi),
        std::cos(theta)
    };
}

void calculate_bounding_convex_hull(const std::span<const glm::vec3> points, Convex_hull& out_hull)
{
    ERHE_PROFILE_FUNCTION();

    out_hull.clear();
    if (points.empty()) {
        return;
    }

    // The quickhull CCW flag is inverted relative to "CCW viewed from outside
    // the hull": the library's native half-edge winding already is CCW viewed
    // from outside (face planes with outward normals derive from it), and
    // CCW=true REVERSES it to clockwise (ConvexHull() emits v[0], v[2], v[1]).
    // Pass false so the result follows the erhe Convex_hull convention
    // (triangles CCW viewed from outside, cross(p1 - p0, p2 - p0) pointing
    // outward), which point_in_convex_hull() and
    // clip_convex_hull_points_to_frustum() rely on.
    constexpr bool reversed_winding     = false;
    constexpr bool use_original_indices = false;

    // Persistent instance: quickhull keeps its internal mesh and pool buffers
    // across getConvexHull() calls, so reusing one instance avoids
    // re-allocating them for every hull. thread_local keeps the function
    // callable from any thread without locking.
    static thread_local quickhull::QuickHull<float> quick_hull{};

    // glm::vec3 is three tightly packed floats, so the span's data feeds
    // quickhull's float-pointer entry (a non-owning view) without a copy.
    static_assert(sizeof(glm::vec3) == 3 * sizeof(float));
    quickhull::ConvexHull<float> convex_hull = quick_hull.getConvexHull(
        &points[0].x,
        points.size(),
        reversed_winding,
        use_original_indices,
        epsilon
    );

    const std::vector<size_t>& index_buffer = convex_hull.getIndexBuffer();
    out_hull.triangle_indices.reserve(index_buffer.size() / 3);
    for (size_t i = 0, end = index_buffer.size(); i < end; i += 3) {
        out_hull.triangle_indices.push_back(
            {
                index_buffer[i + 0],
                index_buffer[i + 1],
                index_buffer[i + 2]
            }
        );
    }
    const quickhull::VertexDataSource<float>& vertex_data_source = convex_hull.getVertexBuffer();
    out_hull.points.reserve(vertex_data_source.size());
    for (quickhull::Vector3<float> p : vertex_data_source) {
        out_hull.points.push_back(
            glm::vec3{p.x, p.y, p.z}
        );
    }
}

auto calculate_bounding_convex_hull(const std::span<const glm::vec3> points) -> Convex_hull
{
    Convex_hull result{};
    calculate_bounding_convex_hull(points, result);
    return result;
}

auto calculate_bounding_convex_hull(const Bounding_volume_source& source) -> Convex_hull
{
    ERHE_PROFILE_FUNCTION();

    std::vector<glm::vec3> point_cloud;
    for (size_t i = 0, i_end = source.get_element_count(); i < i_end; ++i) {
        for (size_t j = 0, j_end = source.get_element_point_count(i); j < j_end; ++j) {
            const std::optional<glm::vec3> point = source.get_point(i, j);
            if (point.has_value()) {
                point_cloud.push_back(point.value());
            }
        }
    }
    return calculate_bounding_convex_hull(std::span<const glm::vec3>{point_cloud});
}

[[nodiscard]] inline auto cross(const glm::vec2& a, const glm::vec2& b) -> float {
    return a.x * b.y - a.y * b.x;
}

void calculate_bounding_convex_hull(const std::span<const glm::vec2> point_cloud, std::vector<glm::vec2>& out_hull)
{
    ERHE_PROFILE_FUNCTION();

    // Andrew's monotone chain. Returns the hull vertices counter-clockwise with no
    // repeated wrap vertex. Robust to collinear and duplicate inputs: collinear
    // points are dropped (strict turns only), so the result is always strictly
    // convex and never contains a point interior to or on an edge of its own hull
    // (which a less robust hull can do, producing a degenerate non-convex polygon).
    //
    // The sorted input copy persists across calls (capacity kept) so steady-state
    // calls do not allocate; it is dead before this function returns, so the
    // reuse is safe on any (non-reentrant) call path.
    static thread_local std::vector<glm::vec2> points;
    points.assign(point_cloud.begin(), point_cloud.end());
    std::sort(
        points.begin(), points.end(),
        [](const glm::vec2& a, const glm::vec2& b) -> bool {
            return (a.x < b.x) || ((a.x == b.x) && (a.y < b.y));
        }
    );
    points.erase(
        std::unique(
            points.begin(), points.end(),
            [](const glm::vec2& a, const glm::vec2& b) -> bool {
                return (a.x == b.x) && (a.y == b.y);
            }
        ),
        points.end()
    );

    const std::size_t n = points.size();
    if (n < 3) {
        out_hull.assign(points.begin(), points.end()); // a single point or a segment is its own hull
        return;
    }

    // orientation(o, a, b) > 0 for a counter-clockwise (left) turn at o.
    const auto orientation = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) -> float {
        return cross(a - o, b - o);
    };

    out_hull.clear();
    out_hull.resize(2 * n);
    std::size_t k = 0;

    // Lower hull, left to right.
    for (std::size_t i = 0; i < n; ++i) {
        while ((k >= 2) && (orientation(out_hull[k - 2], out_hull[k - 1], points[i]) <= 0.0f)) {
            --k;
        }
        out_hull[k++] = points[i];
    }

    // Upper hull, right to left. The lower hull already placed the rightmost point;
    // require at least 'lower_size' vertices so the lower hull is never unwound.
    const std::size_t lower_size = k + 1;
    for (std::size_t i = n - 1; i >= 1; --i) {
        const glm::vec2& p = points[i - 1];
        while ((k >= lower_size) && (orientation(out_hull[k - 2], out_hull[k - 1], p) <= 0.0f)) {
            --k;
        }
        out_hull[k++] = p;
    }

    out_hull.resize(k - 1); // last point coincides with the first; drop it
}

auto calculate_bounding_convex_hull(const std::vector<glm::vec2>& point_cloud) -> std::vector<glm::vec2>
{
    std::vector<glm::vec2> result;
    calculate_bounding_convex_hull(std::span<const glm::vec2>{point_cloud}, result);
    return result;
}

void calculate_bounding_volume(
    const Bounding_volume_source& source,
    Aabb&                         bounding_box,
    Sphere&                       bounding_sphere
)
{
    ERHE_PROFILE_FUNCTION();

    bounding_box.min = vec3{std::numeric_limits<float>::max()};
    bounding_box.max = vec3{std::numeric_limits<float>::lowest()};
    bounding_sphere.center = vec3{0.0f};
    bounding_sphere.radius = 0.0f;

    glm::vec3 x_min{std::numeric_limits<float>::max()};
    glm::vec3 y_min{std::numeric_limits<float>::max()};
    glm::vec3 z_min{std::numeric_limits<float>::max()};
    glm::vec3 x_max{std::numeric_limits<float>::lowest()};
    glm::vec3 y_max{std::numeric_limits<float>::lowest()};
    glm::vec3 z_max{std::numeric_limits<float>::lowest()};

    if (source.get_element_count() == 0) {
        bounding_box.min = vec3{0.0f};
        bounding_box.max = vec3{0.0f};
        return;
    }

    for (size_t i = 0, i_end = source.get_element_count(); i < i_end; ++i) {
        for (size_t j = 0, j_end = source.get_element_point_count(i); j < j_end; ++j) {
            const auto point = source.get_point(i, j);
            if (point.has_value()) {
                const vec3 position = point.value();
                bounding_box.min = glm::min(bounding_box.min, position);
                bounding_box.max = glm::max(bounding_box.max, position);

                if (position.x < x_min.x) x_min = position;
                if (position.x > x_max.x) x_max = position;
                if (position.y < y_min.y) y_min = position;
                if (position.y > y_max.y) y_max = position;
                if (position.z < z_min.z) z_min = position;
                if (position.z > z_max.z) z_max = position;
            }
        }
    }

    std::vector<glm::vec3> points;
    for (size_t i = 0, i_end = source.get_element_count(); i < i_end; ++i) {
        for (size_t j = 0, j_end = source.get_element_point_count(i); j < j_end; ++j) {
            const auto point = source.get_point(i, j);
            if (point.has_value()) {
                const auto p = point.value();
                points.push_back(p);
            }
        }
    }
    const Sphere sphere = optimal_enclosing_sphere(points);
    bounding_sphere.radius = sphere.radius;
    bounding_sphere.center = sphere.center;
}

[[nodiscard]] auto transform(const glm::mat4& m, const Sphere& sphere) -> Sphere
{
#if 0
    // Initial simple version
    const float r     =  sphere.radius; // inscribed sphere radius
    const float R     = (2.0f * r) / std::sqrt(3.0f);
    const float phi   = (1.0f + std::sqrt(5.0f)) / 2.0f; // golden ratio
    const float denom = std::sqrt(phi * sqrt(5.0f));
    const float k     = R / denom;
    const float k_phi = k * phi;

    Bounding_volume_combiner combiner;
    combiner.add_point(m, sphere.center + vec3{ 0.0f,   k,      k_phi});
    combiner.add_point(m, sphere.center + vec3{ 0.0f,   k,     -k_phi});
    combiner.add_point(m, sphere.center + vec3{ 0.0f,  -k,      k_phi});
    combiner.add_point(m, sphere.center + vec3{ 0.0f,  -k,     -k_phi});
    combiner.add_point(m, sphere.center + vec3{ k,      k_phi,  0.0f });
    combiner.add_point(m, sphere.center + vec3{ k,     -k_phi,  0.0f });
    combiner.add_point(m, sphere.center + vec3{-k,      k_phi,  0.0f });
    combiner.add_point(m, sphere.center + vec3{-k,     -k_phi,  0.0f });
    combiner.add_point(m, sphere.center + vec3{ k_phi,  0.0f,   k    });
    combiner.add_point(m, sphere.center + vec3{-k_phi,  0.0f,   k    });
    combiner.add_point(m, sphere.center + vec3{ k_phi,  0.0f,  -k    });
    combiner.add_point(m, sphere.center + vec3{-k_phi,  0.0f,  -k    });

    Aabb   transformed_icosahedron_bounding_box;
    Sphere transformed_icosahedron_bounding_sphere;
    calculate_bounding_volume(combiner, transformed_icosahedron_bounding_box, transformed_icosahedron_bounding_sphere);
#endif

    using mat3 = glm::mat3;

    // The maximum radius of transformed sphere is r * sqrt(rho(M^TM)) where sqrt(rho(M^TM)) is the spectral radius of M,
    // M is mat3 submatrix of m.
    const float r = sphere.radius; // inscribed sphere radius
    const mat3 m3{m};
    const mat3 covariant = glm::transpose(m3) * m3;
    vec3 eigenvalues;
    mat3 eigenvectors;
    const unsigned int result_count = glm::findEigenvaluesSymReal<3, float>(covariant, eigenvalues, eigenvectors);
    float max_eigenvalue = 0.0f;
    for (unsigned int i = 0; i < result_count; ++i) {
        max_eigenvalue = std::max(max_eigenvalue, eigenvalues[i]);
    }
    const float R_ = r * std::sqrt(max_eigenvalue);

    Sphere transformed_bounding_sphere
    {
        .center = vec3{m * vec4{sphere.center, 1.0f}},
        .radius = R_
    };

    return transformed_bounding_sphere;
}

[[nodiscard]] auto compose(
    glm::vec3 scale,
    glm::quat rotation,
    glm::vec3 translation,
    glm::vec3 skew,
    glm::vec4 perspective
) -> glm::mat4
{
    glm::mat4 p = glm::mat4{1.0f};
    p[0][3] = perspective.x;
    p[1][3] = perspective.y;
    p[2][3] = perspective.z;
    p[3][3] = perspective.w;

    glm::mat4 m = glm::translate(p, translation);
    m *= glm::mat4_cast(rotation);

    glm::mat4 skew_x{1.0f};
    skew_x[2][1] = skew.x;
    m *= skew_x;

    glm::mat4 skew_y{1.0f};
    skew_y[2][0] = skew.y;
    m *= skew_y;

    glm::mat4 skew_z{1.0f};
    skew_z[1][0] = skew.z;
    m *= skew_z;

    return glm::scale(m, scale);
}

[[nodiscard]] auto compose(
    glm::vec3 scale,
    glm::quat rotation,
    glm::vec3 translation,
    glm::vec3 skew
) -> glm::mat4
{
    glm::mat4 m = create_translation<float>(translation);
    m *= glm::mat4_cast(rotation);

    glm::mat4 skew_x{1.0f};
    skew_x[2][1] = skew.x;
    m *= skew_x;

    glm::mat4 skew_y{1.0f};
    skew_y[2][0] = skew.y;
    m *= skew_y;

    glm::mat4 skew_z{1.0f};
    skew_z[1][0] = skew.z;
    m *= skew_z;

    return glm::scale(m, scale);
}

[[nodiscard]] auto compose_inverse(
    glm::vec3 scale,
    glm::quat rotation,
    glm::vec3 translation,
    glm::vec3 skew
) -> glm::mat4
{
    // TODO compute directly
    return glm::inverse(
        compose(scale, rotation, translation, skew)
    );
}

Point_vector_bounding_volume_source::Point_vector_bounding_volume_source()
{
}

Point_vector_bounding_volume_source::Point_vector_bounding_volume_source(std::size_t capacity)
{
    m_points.reserve(capacity);
}

void Point_vector_bounding_volume_source::add(const float x, const float y, const float z)
{
    m_points.emplace_back(x, y, z);
}

void Point_vector_bounding_volume_source::add(glm::vec3 point)
{
    m_points.push_back(point);
}

auto Point_vector_bounding_volume_source::get_element_count() const -> std::size_t
{
    return m_points.size();
}

auto Point_vector_bounding_volume_source::get_element_point_count(std::size_t) const -> std::size_t
{
    return 1;
}

auto Point_vector_bounding_volume_source::get_point(std::size_t element_index, std::size_t) const -> std::optional<glm::vec3>
{
    return m_points[element_index];
}

auto torus_volume(const float major_radius, const float minor_radius) -> float
{
    return
        glm::pi<float>() *
        minor_radius *
        minor_radius *
        glm::two_pi<float>() *
        major_radius;
}

auto extract_frustum_planes(const glm::mat4& clip_from_world, float clip_z_near, float clip_z_far) -> std::array<glm::vec4, 6>
{
    const glm::mat4& m = clip_from_world;
    const glm::vec4 row0{m[0][0], m[1][0], m[2][0], m[3][0]};
    const glm::vec4 row1{m[0][1], m[1][1], m[2][1], m[3][1]};
    const glm::vec4 row2{m[0][2], m[1][2], m[2][2], m[3][2]};
    const glm::vec4 row3{m[0][3], m[1][3], m[2][3], m[3][3]};

    auto normalize = [](const glm::vec4 plane) -> glm::vec4 {
        const float length = glm::length(glm::vec3(plane));
        return (length != 0.0f)
            ? plane / length
            : vec4{0.0f, 0.0f, 0.0f, 0.0f};
    };

    return {
        normalize(              row3 + row0), // Left
        normalize(              row3 - row0), // Right
        normalize(              row3 + row1), // Bottom
        normalize(              row3 - row1), // Top
        normalize(clip_z_near * row3 + row2), // Near, or far when using reverse Z
        normalize(clip_z_far  * row3 - row2)  // Far, or near when using reverse Z
    };
}

[[nodiscard]] auto extract_frustum_corners(const glm::mat4& world_from_clip, float clip_z_near, float clip_z_far) -> std::array<glm::vec3, 8>
{
    auto transform = [&world_from_clip](float x, float y, float z) -> glm::vec3 {
        const glm::vec4 h = world_from_clip * glm::vec4{x, y, z, 1.0f};
        return h.w != 0.0f ? h / h.w : glm::vec3{0.0f, 0.0f, 0.0f};
    };
    return {
        transform(-1.0f, -1.0f, clip_z_near), // 0    2------3
        transform( 1.0f, -1.0f, clip_z_near), // 1   /|     /|
        transform(-1.0f,  1.0f, clip_z_near), // 2  6-+----7 |      <-- the plane further away from camera is the near plane when using reverse Z
        transform( 1.0f,  1.0f, clip_z_near), // 3  | |    | |
        transform(-1.0f, -1.0f, clip_z_far ), // 4  | |    | |    <---- the plane closer to camera is the far plane when using reverse Z
        transform( 1.0f, -1.0f, clip_z_far ), // 5  | 0----|-1
        transform(-1.0f,  1.0f, clip_z_far ), // 6  |/     |/
        transform( 1.0f,  1.0f, clip_z_far )  // 7  4------5
    };
}

auto get_frustum_plane_corner(size_t plane, size_t local_corner_index) -> size_t
{
    switch (plane) {
        case plane_left  : return std::array<size_t, 4>{ 0, 4, 6, 2 }[local_corner_index]; // left    cw
        case plane_right : return std::array<size_t, 4>{ 1, 5, 7, 3 }[local_corner_index]; // right   ccw
        case plane_bottom: return std::array<size_t, 4>{ 0, 4, 5, 1 }[local_corner_index]; // bottom  cw
        case plane_top   : return std::array<size_t, 4>{ 2, 3, 7, 6 }[local_corner_index]; // top     ccw
        case plane_near  : return std::array<size_t, 4>{ 4, 6, 7, 5 }[local_corner_index]; // near    cw
        case plane_far   : return std::array<size_t, 4>{ 0, 1, 3, 2 }[local_corner_index]; // far     ccw
        default: ERHE_FATAL("get_frustum_plane_corner(): invalid plane = %zu (valid range is 0..5)", plane);
    }
}

auto get_point_on_plane(const glm::vec4& plane) -> glm::vec3
{
    glm::vec3 normal = glm::vec3(plane);
    float d = plane.w;

    // Avoid division by zero
    float denom = glm::dot(normal, normal);
    if (denom == 0.0f) {
        return glm::vec3{0.0f}; // degenerate plane
    }

    // Return point: -d * n / ||n||^2
    return -d * normal / denom;
}

void get_plane_basis(const glm::vec3& normal, glm::vec3& tangent, glm::vec3& bitangent)
{
    glm::vec3 tangent_ = erhe::math::min_axis<float>(normal);

    bitangent = glm::normalize(glm::cross(normal, tangent_));
    tangent   = glm::normalize(glm::cross(bitangent, normal));
}

// https://iquilezles.org/articles/frustumcorrect/
// false if fully outside, true if inside or intersects
auto aabb_in_frustum(
    const std::array<glm::vec4, 6>& planes,
    const std::array<glm::vec3, 8>& corners,
    const Aabb&                     aabb
) -> bool
{
    // Check box outside/inside of frustum: reject when the box is entirely on
    // the negative side of one plane. Center + extents (p-vertex) form: the
    // most-positive corner's plane distance is dist(center) + dot(abs(n), extent),
    // identical to testing all 8 corners but 2 dot products per plane instead of 8.
    const glm::vec3 center = aabb.center();
    const glm::vec3 extent = 0.5f * aabb.diagonal();
    for (int i = 0; i < 6; i++) {
        const glm::vec3 normal{planes[i]};
        const float center_distance  = glm::dot(normal, center) + planes[i].w;
        const float projected_extent = glm::dot(glm::abs(normal), extent);
        if ((center_distance + projected_extent) < 0.0f) {
            return false;
        }
    }

    // Check frustum outside/inside box
    int out;
    out = 0; for (const glm::vec3& p : corners) out += ((p.x > aabb.max.x) ? 1 : 0); if (out == 8) return false;
    out = 0; for (const glm::vec3& p : corners) out += ((p.x < aabb.min.x) ? 1 : 0); if (out == 8) return false;
    out = 0; for (const glm::vec3& p : corners) out += ((p.y > aabb.max.y) ? 1 : 0); if (out == 8) return false;
    out = 0; for (const glm::vec3& p : corners) out += ((p.y < aabb.min.y) ? 1 : 0); if (out == 8) return false;
    out = 0; for (const glm::vec3& p : corners) out += ((p.z > aabb.max.z) ? 1 : 0); if (out == 8) return false;
    out = 0; for (const glm::vec3& p : corners) out += ((p.z < aabb.min.z) ? 1 : 0); if (out == 8) return false;
    return true;
}

// https://bruop.github.io/improved_frustum_culling/
// https://iquilezles.org/articles/frustumcorrect/

auto aabb_in_convex_volume(const std::span<const glm::vec4> planes, const Aabb& aabb) -> bool
{
    // Conservative half-space rejection against an arbitrary inward-facing
    // plane set (same convention as extract_frustum_planes(): a point p is
    // inside a plane when dot(vec4{p, 1}, plane) >= 0). The AABB is rejected
    // only when it lies entirely outside one single plane, which proves no
    // intersection - tested in center + extents (p-vertex) form: the
    // most-positive corner's plane distance is dist(center) + dot(abs(n), extent).
    // Unlike aabb_in_frustum() there is no corner set, so the reverse
    // box-vs-corners pass is skipped: this may keep an AABB that is outside
    // the volume near a silhouette edge (false positive), but never rejects
    // one that intersects it (no false negative).
    const glm::vec3 center = aabb.center();
    const glm::vec3 extent = 0.5f * aabb.diagonal();
    for (const glm::vec4& plane : planes) {
        const glm::vec3 normal{plane};
        const float center_distance  = glm::dot(normal, center) + plane.w;
        const float projected_extent = glm::dot(glm::abs(normal), extent);
        if ((center_distance + projected_extent) < 0.0f) {
            return false;
        }
    }
    return true;
}

auto plane_point_distance(const glm::vec4& plane, const glm::vec3& point) -> float
{
    return glm::dot(glm::vec4(point, 1.0f), plane);
}

[[nodiscard]] auto sphere_in_frustum(
    const std::array<glm::vec4, 6>& planes,
    const Sphere&                   sphere
) -> bool
{
   const float dist01 = std::min(plane_point_distance(planes[0], sphere.center), plane_point_distance(planes[1], sphere.center));
   const float dist23 = std::min(plane_point_distance(planes[2], sphere.center), plane_point_distance(planes[3], sphere.center));
   const float dist45 = std::min(plane_point_distance(planes[4], sphere.center), plane_point_distance(planes[5], sphere.center));
   return std::min(std::min(dist01, dist23), dist45) + sphere.radius > 0.0f;
}

auto calculate_min_area_obb_2d(const std::vector<glm::vec2>& hull_points, const int debug_edge) -> Min_area_obb_2d
{
    Min_area_obb_2d result{};
    const std::size_t count = hull_points.size();
    if (count < 2) {
        return result;
    }
    float min_aabb_area = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec2& A         = hull_points[i];
        const glm::vec2& B         = hull_points[(i + 1) % count];
        const glm::vec2  AB        = B - A;
        const float      ab_length = glm::length(AB);
        if (ab_length < epsilon) {
            continue; // skip degenerate (duplicate point) edges
        }
        const glm::vec2  x_axis = AB / ab_length;
        const glm::vec2  y_axis = glm::vec2{-x_axis.y, x_axis.x};
        const glm::mat2  original_from_edge{x_axis, y_axis};
        const glm::mat2  edge_from_original = glm::transpose(original_from_edge);

        glm::vec2 edge_aabb_min_corner{std::numeric_limits<float>::max()};
        glm::vec2 edge_aabb_max_corner{std::numeric_limits<float>::lowest()};
        for (const glm::vec2& p_in_original : hull_points) {
            const glm::vec2 p_in_edge = edge_from_original * p_in_original;
            edge_aabb_min_corner = glm::min(edge_aabb_min_corner, p_in_edge);
            edge_aabb_max_corner = glm::max(edge_aabb_max_corner, p_in_edge);
        }
        const glm::vec2 edge_aabb_size = edge_aabb_max_corner - edge_aabb_min_corner;
        const float     aabb_area     = edge_aabb_size.x * edge_aabb_size.y;
        const bool      is_debug_edge = (debug_edge >= 0) && (static_cast<std::size_t>(debug_edge) == i);
        if ((aabb_area < min_aabb_area) || is_debug_edge) {
            min_aabb_area             = aabb_area;
            result.edge_from_original = edge_from_original;
            result.aabb_min           = edge_aabb_min_corner;
            result.aabb_max           = edge_aabb_max_corner;
            result.edge_a             = A;
            result.edge_b             = B;
            result.area               = aabb_area;
            if (is_debug_edge) {
                return result;
            }
        }
    }
    return result;
}

auto point_in_convex_hull(const Convex_hull& hull, const glm::vec3& point, const float tolerance) -> bool
{
    if (hull.triangle_indices.empty()) {
        return false;
    }
    // Hull triangles are CCW seen from outside, so cross(p1 - p0, p2 - p0)
    // points outward; the point is inside when it is not in front of any
    // triangle plane.
    for (const std::array<std::size_t, 3>& triangle : hull.triangle_indices) {
        const glm::vec3& p0 = hull.points[triangle[0]];
        const glm::vec3& p1 = hull.points[triangle[1]];
        const glm::vec3& p2 = hull.points[triangle[2]];
        const glm::vec3  outward_normal = glm::cross(p1 - p0, p2 - p0); // not normalized
        if (glm::dot(outward_normal, point - p0) > (tolerance * glm::length(outward_normal))) {
            return false;
        }
    }
    return true;
}

// Welds nearly-coincident points in place (lexicographic sort + adjacent
// merge). The per-triangle Sutherland-Hodgman clips emit each shared hull
// vertex once per incident triangle (bit-identical copies) and each
// shared-edge crossing once per side (equal within a few ulps); welding
// removes that redundancy before the points are hulled again. The tolerance
// is relative to the point set extent, far below any geometrically
// significant distance.
static void weld_points(std::vector<glm::vec3>& points)
{
    if (points.size() < 2) {
        return;
    }
    glm::vec3 min_point = points.front();
    glm::vec3 max_point = points.front();
    for (const glm::vec3& p : points) {
        min_point = glm::min(min_point, p);
        max_point = glm::max(max_point, p);
    }
    const glm::vec3 extent    = max_point - min_point;
    const float     tolerance = 1e-5f * std::max(std::max(extent.x, extent.y), std::max(extent.z, 1.0f));
    std::sort(
        points.begin(), points.end(),
        [](const glm::vec3& a, const glm::vec3& b) -> bool {
            if (a.x != b.x) {
                return a.x < b.x;
            }
            if (a.y != b.y) {
                return a.y < b.y;
            }
            return a.z < b.z;
        }
    );
    points.erase(
        std::unique(
            points.begin(), points.end(),
            [tolerance](const glm::vec3& a, const glm::vec3& b) -> bool {
                return
                    (std::abs(a.x - b.x) <= tolerance) &&
                    (std::abs(a.y - b.y) <= tolerance) &&
                    (std::abs(a.z - b.z) <= tolerance);
            }
        ),
        points.end()
    );
}

void clip_convex_hull_points_by_planes(const Convex_hull& hull, const std::span<const glm::vec4> planes, std::vector<glm::vec3>& out_points)
{
    ERHE_PROFILE_FUNCTION();

    // Sutherland-Hodgman ping-pong polygons, reused across calls (capacity
    // kept) so steady-state calls do not allocate. They are dead before this
    // function returns, so the reuse is safe even when a caller (e.g.
    // clip_convex_hull_points_to_frustum) invokes this function repeatedly.
    static thread_local std::vector<glm::vec3> polygon;
    static thread_local std::vector<glm::vec3> clipped;
    out_points.clear();
    for (const std::array<std::size_t, 3>& triangle : hull.triangle_indices) {
        polygon.clear();
        polygon.push_back(hull.points[triangle[0]]);
        polygon.push_back(hull.points[triangle[1]]);
        polygon.push_back(hull.points[triangle[2]]);
        // Sutherland-Hodgman: clip the triangle polygon against each plane in turn
        for (const glm::vec4& plane : planes) {
            clipped.clear();
            for (std::size_t i = 0, count = polygon.size(); i < count; ++i) {
                const glm::vec3& a          = polygon[i];
                const glm::vec3& b          = polygon[(i + 1) % count];
                const float      distance_a = plane_point_distance(plane, a);
                const float      distance_b = plane_point_distance(plane, b);
                if (distance_a >= 0.0f) {
                    clipped.push_back(a);
                }
                if ((distance_a >= 0.0f) != (distance_b >= 0.0f)) {
                    const float t = distance_a / (distance_a - distance_b);
                    clipped.push_back(a + t * (b - a));
                }
            }
            polygon.swap(clipped);
            if (polygon.empty()) {
                break;
            }
        }
        out_points.insert(out_points.end(), polygon.begin(), polygon.end());
    }
    weld_points(out_points);
}

auto clip_convex_hull_points_by_planes(const Convex_hull& hull, const std::span<const glm::vec4> planes) -> std::vector<glm::vec3>
{
    std::vector<glm::vec3> result;
    clip_convex_hull_points_by_planes(hull, planes, result);
    return result;
}

void clip_convex_hull_points_to_frustum(
    const Convex_hull&              hull,
    const std::array<glm::vec4, 6>& frustum_planes,
    const std::array<glm::vec3, 8>& frustum_corners,
    std::vector<glm::vec3>&         out_points
)
{
    ERHE_PROFILE_FUNCTION();

    // Vertices of (hull intersected with frustum). Both bodies are convex, so every
    // vertex of the intersection lies on the surface of at least one of them.
    // Clipping the hull surface to the frustum planes yields the hull vertices
    // inside the frustum plus the hull edges crossing frustum faces; clipping the
    // frustum surface to the hull's face planes yields the frustum corners inside
    // the hull plus the frustum edges crossing hull faces. The union is the
    // complete vertex set - clipping in one direction only (even with frustum
    // corners inside the hull added) misses the frustum-edge-through-hull-face
    // vertices, which drops near receivers when the frustum apex is inside the hull.
    clip_convex_hull_points_by_planes(hull, frustum_planes, out_points);

    // Inward-facing face planes of the hull (CCW triangles -> outward normal),
    // deduplicated: coplanar triangles of one hull face yield the same plane,
    // and clipping the frustum against duplicates is wasted work (a box-like
    // hull has 12 triangles but 6 faces). Merging near-coplanar planes can
    // only enlarge the clipped body by the tolerance, which is the safe
    // (conservative) direction for a cull volume.
    //
    // hull_planes / frustum_hull / frustum_clipped persist across calls
    // (capacity kept) so steady-state calls do not allocate. They belong to
    // this function only - the nested clip_convex_hull_points_by_planes call
    // uses its own scratch, so hull_planes stays intact across it.
    static thread_local std::vector<glm::vec4> hull_planes;
    hull_planes.clear();
    hull_planes.reserve(hull.triangle_indices.size());
    for (const std::array<std::size_t, 3>& triangle : hull.triangle_indices) {
        const glm::vec3& p0 = hull.points[triangle[0]];
        const glm::vec3& p1 = hull.points[triangle[1]];
        const glm::vec3& p2 = hull.points[triangle[2]];
        glm::vec3   normal = glm::cross(p1 - p0, p2 - p0);
        const float length = glm::length(normal);
        if (length < epsilon) {
            continue; // degenerate triangle
        }
        const glm::vec3 inward = -(normal / length);
        const float     w      = -glm::dot(inward, p0);
        bool duplicate = false;
        for (const glm::vec4& existing : hull_planes) {
            if (
                (glm::dot(glm::vec3{existing}, inward) > 0.99999f) &&
                (std::abs(existing.w - w) <= (1e-4f * std::max(1.0f, std::abs(w))))
            ) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            hull_planes.push_back(glm::vec4{inward, w});
        }
    }
    if (!hull_planes.empty()) {
        // The frustum's hull topology is known: with the extract_frustum_corners()
        // numbering (bit 0 = x, bit 1 = y, bit 2 = near/far) its 6 quad faces
        // split into 12 fixed triangles, so no hull computation is needed.
        // This also avoids QuickHull epsilon-merging the tiny near rectangle
        // when the camera near plane is small (which could collapse the near
        // face and silently drop the clip output around the frustum apex).
        // Triangle winding here is unspecified; clip_convex_hull_points_by_planes
        // does not depend on it (this local hull is not used for plane derivation).
        static constexpr std::array<std::array<std::size_t, 3>, 12> frustum_triangles{{
            {0, 1, 3}, {0, 3, 2}, // near
            {4, 7, 5}, {4, 6, 7}, // far
            {0, 2, 6}, {0, 6, 4}, // left
            {1, 5, 7}, {1, 7, 3}, // right
            {0, 4, 5}, {0, 5, 1}, // bottom
            {2, 3, 7}, {2, 7, 6}  // top
        }};
        static thread_local Convex_hull frustum_hull;
        frustum_hull.points.assign(frustum_corners.begin(), frustum_corners.end());
        frustum_hull.triangle_indices.assign(frustum_triangles.begin(), frustum_triangles.end());
        static thread_local std::vector<glm::vec3> frustum_clipped;
        clip_convex_hull_points_by_planes(frustum_hull, hull_planes, frustum_clipped);
        out_points.insert(out_points.end(), frustum_clipped.begin(), frustum_clipped.end());
        // The two passes overlap where hull vertices lie on frustum faces and
        // vice versa; weld the union before it is hulled again.
        weld_points(out_points);
    }
}

auto clip_convex_hull_points_to_frustum(
    const Convex_hull&              hull,
    const std::array<glm::vec4, 6>& frustum_planes,
    const std::array<glm::vec3, 8>& frustum_corners
) -> std::vector<glm::vec3>
{
    std::vector<glm::vec3> result;
    clip_convex_hull_points_to_frustum(hull, frustum_planes, frustum_corners, result);
    return result;
}

auto build_shadow_caster_volume_planes(const std::array<glm::vec4, 6>& main_frustum_planes, const glm::vec3& light_direction) -> Shadow_volume_planes
{
    Shadow_volume_planes result{};
    float min_alignment = std::numeric_limits<float>::max();
    float max_alignment = std::numeric_limits<float>::lowest();
    for (const glm::vec4& plane : main_frustum_planes) {
        const float alignment = glm::dot(glm::vec3{plane}, light_direction);
        if (alignment < min_alignment) {
            min_alignment = alignment;
            result.light_facing_plane = plane;
        }
        if (alignment > max_alignment) {
            max_alignment = alignment;
            result.far_cap_plane = plane;
        }
        if (alignment >= 0.0f) {
            // Keep only planes which do not face away from the light; dropping
            // light-facing planes leaves the volume open toward the light
            // (conservative: never clips a potential caster).
            result.planes[result.plane_count] = plane;
            ++result.plane_count;
        }
    }
    return result;
}

auto build_shadow_caster_silhouette(
    const std::array<glm::vec4, 6>& main_frustum_planes,
    const std::array<glm::vec3, 8>& main_frustum_corners,
    const glm::vec3&                light_direction
) -> Shadow_caster_silhouette
{
    // Kept/dropped per face, matching build_shadow_caster_volume_planes().
    // Plane order from extract_frustum_planes(): left,right,bottom,top,near,far.
    std::array<bool, 6> kept{};
    for (std::size_t face = 0; face < 6; ++face) {
        kept[face] = (glm::dot(glm::vec3{main_frustum_planes[face]}, light_direction) >= 0.0f);
    }

    // Interior reference point so the swept side planes can be oriented inward.
    glm::vec3 centroid{0.0f};
    for (const glm::vec3& corner : main_frustum_corners) {
        centroid += corner;
    }
    centroid *= (1.0f / 8.0f);

    // The 12 frustum edges as (face_a, face_b, corner_a, corner_b), using the
    // extract_frustum_corners() numbering (bit 0 = x, bit 1 = y, bit 2 = z:
    // 0:(-,-,n) 1:(+,-,n) 2:(-,+,n) 3:(+,+,n) 4:(-,-,f) 5:(+,-,f) 6:(-,+,f)
    // 7:(+,+,f)). An edge whose two faces differ in kept/dropped is a
    // silhouette edge.
    static constexpr std::array<std::array<std::size_t, 4>, 12> edges{{
        {0, 2, 0, 4}, // left  & bottom
        {0, 3, 2, 6}, // left  & top
        {0, 4, 0, 2}, // left  & near
        {0, 5, 4, 6}, // left  & far
        {1, 2, 1, 5}, // right & bottom
        {1, 3, 3, 7}, // right & top
        {1, 4, 1, 3}, // right & near
        {1, 5, 5, 7}, // right & far
        {2, 4, 0, 1}, // bottom & near
        {2, 5, 4, 5}, // bottom & far
        {3, 4, 2, 3}, // top    & near
        {3, 5, 6, 7}  // top    & far
    }};

    Shadow_caster_silhouette result{};
    for (const std::array<std::size_t, 4>& edge : edges) {
        if (kept[edge[0]] == kept[edge[1]]) {
            continue; // both kept or both dropped: not a silhouette edge
        }
        const glm::vec3& a = main_frustum_corners[edge[2]];
        const glm::vec3& b = main_frustum_corners[edge[3]];
        glm::vec3 normal = glm::cross(b - a, light_direction);
        const float length = glm::length(normal);
        if (length < epsilon) {
            continue; // edge parallel to the light: swept plane is degenerate
        }
        normal /= length;
        if (glm::dot(normal, centroid - a) < 0.0f) {
            normal = -normal; // orient inward (frustum interior on the >= 0 side)
        }
        result.planes[result.count] = glm::vec4{normal, -glm::dot(normal, a)};
        result.edges [result.count] = std::array<glm::vec3, 2>{a, b};
        ++result.count;
    }
    return result;
}

void build_shadow_caster_cull_planes_from_hull(const Convex_hull& hull, const glm::vec3& light_direction, std::vector<glm::vec4>& out_planes, std::vector<glm::vec3>* out_far_plane_hull)
{
    ERHE_PROFILE_FUNCTION();

    out_planes.clear();
    if (hull.points.size() < 3) {
        return;
    }
    const glm::vec3 light = glm::normalize(light_direction);

    // Orthonormal basis on the plane perpendicular to the light (the same helper
    // convex_polyhedron_from_planes uses to order face vertices).
    glm::vec3 tangent;
    glm::vec3 bitangent;
    get_plane_basis(light, tangent, bitangent);

    // Project every hull point onto that plane (its silhouette as seen along the
    // light) and track the point furthest from the light (smallest projection
    // along the light direction) for the flat far cap.
    // projected / silhouette persist across calls (capacity kept) so
    // steady-state calls do not allocate; both are dead before this function
    // returns, and the nested 2D hull call uses its own scratch.
    static thread_local std::vector<glm::vec2> projected;
    projected.clear();
    projected.reserve(hull.points.size());
    float min_s = std::numeric_limits<float>::max();
    for (const glm::vec3& p : hull.points) {
        projected.push_back(glm::vec2{glm::dot(p, tangent), glm::dot(p, bitangent)});
        min_s = std::min(min_s, glm::dot(p, light));
    }

    // Far cap: perpendicular to the light through the furthest (away-from-light)
    // point, inward normal pointing toward the light. Closes the volume on the far
    // side; the volume stays open toward the light (the side planes are parallel to
    // it). Inside is dot(light, p) >= min_s.
    out_planes.push_back(glm::vec4{light, -min_s});

    // 2D silhouette: the convex hull of the projected points, returned CCW with no
    // repeated wrap vertex, so consecutive pairs (including last -> first) are the
    // silhouette edges. A hull of fewer than 3 points means the body is edge-on to
    // the light (no lateral extent); signal the caller to use a coarser body.
    static thread_local std::vector<glm::vec2> silhouette;
    calculate_bounding_convex_hull(std::span<const glm::vec2>{projected}, silhouette);
    if (silhouette.size() < 3) {
        out_planes.clear();
        return;
    }

    // Optional world-space silhouette polygon: the 2D hull lifted onto the far
    // plane (s = min_s along the light), in order, for visualization.
    if (out_far_plane_hull != nullptr) {
        out_far_plane_hull->clear();
        out_far_plane_hull->reserve(silhouette.size());
        for (const glm::vec2& q : silhouette) {
            out_far_plane_hull->push_back((q.x * tangent) + (q.y * bitangent) + (min_s * light));
        }
    }

    // 2D centroid lifted back onto the plane, to orient each swept side plane inward.
    glm::vec2 centroid2{0.0f};
    for (const glm::vec2& q : silhouette) {
        centroid2 += q;
    }
    centroid2 *= (1.0f / static_cast<float>(silhouette.size()));
    const glm::vec3 centroid_on_plane = (centroid2.x * tangent) + (centroid2.y * bitangent);

    // Each silhouette edge swept along the light direction is an inward-facing side
    // plane. The on-plane lift uses s = 0; since the plane is parallel to the light,
    // the reference point's light coordinate does not affect the plane.
    const std::size_t silhouette_count = silhouette.size();
    for (std::size_t i = 0; i < silhouette_count; ++i) {
        const glm::vec2& a2 = silhouette[i];
        const glm::vec2& b2 = silhouette[(i + 1) % silhouette_count];
        const glm::vec3  a  = (a2.x * tangent) + (a2.y * bitangent);
        const glm::vec3  b  = (b2.x * tangent) + (b2.y * bitangent);
        glm::vec3   normal = glm::cross(b - a, light);
        const float length = glm::length(normal);
        if (length < epsilon) {
            continue; // edge parallel to the light: swept plane is degenerate
        }
        normal /= length;
        if (glm::dot(normal, centroid_on_plane - a) < 0.0f) {
            normal = -normal; // orient inward (interior on the >= 0 side)
        }
        out_planes.push_back(glm::vec4{normal, -glm::dot(normal, a)});
    }
}

auto convex_polyhedron_from_planes(const std::span<const glm::vec4> planes, const float tolerance) -> Convex_polyhedron
{
    Convex_polyhedron result{};
    const std::size_t plane_count = planes.size();
    if (plane_count < 3) {
        return result;
    }

    // Vertices: every plane-plane-plane intersection that lies inside all the
    // half-spaces (dot(vec4{p,1}, plane) >= 0 for all planes).
    for (std::size_t i = 0; i < plane_count; ++i) {
        const glm::vec3 ni{planes[i]};
        for (std::size_t j = i + 1; j < plane_count; ++j) {
            const glm::vec3 nj{planes[j]};
            for (std::size_t k = j + 1; k < plane_count; ++k) {
                const glm::vec3 nk{planes[k]};
                const float det = glm::dot(ni, glm::cross(nj, nk));
                if (std::abs(det) < 1e-6f) {
                    continue; // planes are not linearly independent
                }
                const glm::vec3 point =
                    (-planes[i].w * glm::cross(nj, nk)
                     - planes[j].w * glm::cross(nk, ni)
                     - planes[k].w * glm::cross(ni, nj)) / det;

                bool inside = true;
                for (std::size_t m = 0; m < plane_count; ++m) {
                    if (glm::dot(glm::vec4{point, 1.0f}, planes[m]) < -tolerance) {
                        inside = false;
                        break;
                    }
                }
                if (!inside) {
                    continue;
                }
                bool duplicate = false;
                for (const glm::vec3& existing : result.vertices) {
                    if (glm::distance(existing, point) < tolerance) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    result.vertices.push_back(point);
                }
            }
        }
    }

    // Per plane: the vertices lying on it, ordered CCW about the plane normal,
    // and the boundary edges between consecutive face vertices.
    result.face_vertices.resize(plane_count);
    std::set<std::pair<std::size_t, std::size_t>> unique_edges;
    for (std::size_t m = 0; m < plane_count; ++m) {
        const glm::vec3 normal{planes[m]};
        std::vector<std::size_t> on_plane;
        for (std::size_t vi = 0; vi < result.vertices.size(); ++vi) {
            if (std::abs(glm::dot(glm::vec4{result.vertices[vi], 1.0f}, planes[m])) < tolerance) {
                on_plane.push_back(vi);
            }
        }
        if (on_plane.size() < 3) {
            continue; // plane has no bounded face in this intersection
        }
        glm::vec3 centroid{0.0f};
        for (const std::size_t vi : on_plane) {
            centroid += result.vertices[vi];
        }
        centroid /= static_cast<float>(on_plane.size());
        glm::vec3 tangent;
        glm::vec3 bitangent;
        get_plane_basis(normal, tangent, bitangent);
        std::sort(
            on_plane.begin(), on_plane.end(),
            [&result, &centroid, &tangent, &bitangent](const std::size_t a, const std::size_t b) {
                const glm::vec3 da = result.vertices[a] - centroid;
                const glm::vec3 db = result.vertices[b] - centroid;
                return std::atan2(glm::dot(da, bitangent), glm::dot(da, tangent))
                     < std::atan2(glm::dot(db, bitangent), glm::dot(db, tangent));
            }
        );
        result.face_vertices[m] = on_plane;
        for (std::size_t e = 0; e < on_plane.size(); ++e) {
            std::size_t a = on_plane[e];
            std::size_t b = on_plane[(e + 1) % on_plane.size()];
            if (a > b) {
                std::swap(a, b);
            }
            unique_edges.insert(std::pair<std::size_t, std::size_t>{a, b});
        }
    }
    result.edges.reserve(unique_edges.size());
    for (const std::pair<std::size_t, std::size_t>& edge : unique_edges) {
        result.edges.push_back(std::array<std::size_t, 2>{edge.first, edge.second});
    }
    return result;
}

} // namespace erhe::math
