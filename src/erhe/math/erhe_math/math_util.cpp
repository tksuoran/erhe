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
#include <stack>

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
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float z_near,
    const float z_far
) -> mat4
{
    const float width          = right  - left;
    const float height         = top    - bottom;
    const float near_minus_far = z_near - z_far;
    const float far_minus_near = z_far  - z_near;
    if ((width == 0.0) || (height == 0.0f) || (near_minus_far == 0.0f) || (far_minus_near == 0.0f)) {
        return glm::mat4{1.0f}; // TODO Log warning
    }
    const float x =  (2.0f  * z_near        ) / width;
    const float y =  (2.0f  * z_near        ) / height;
    const float a =  (right + left          ) / width;
    const float b =  (top   + bottom        ) / height;
  //const float c = -(z_far + z_near        ) / (z_far  - z_near); -- negative one to one
    const float c =   z_far                   / (z_near - z_far ); // zero to one
  //const float d = -(2.0f  * z_far * z_near) / (z_far  - z_near); -- negative one to one
    const float d = -(z_far * z_near        ) / (z_far  - z_near); // zero to one

    return mat4{
        x, 0, 0, 0,
        0, y, 0, 0,
        a, b, c, -1.0f,
        0, 0, d, 0,
    };
}

auto create_frustum_infinite_far(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float z_near
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
  //const float c = -1.0f;           -- negative one to one
    const float c = -1.0f;           // zero to one
  //const float d = -2.0f * z_near;  -- negative one to one
    const float d = -z_near;         // zero to one

    return mat4{
        x, 0, 0, 0,
        0, y, 0, 0,
        a, b, c, -1.0f,
        0, 0, d, 0
    };
}

auto create_frustum_simple(
    const float width,
    const float height,
    const float z_near,
    const float z_far
) -> mat4
{
    return create_frustum(-width / 2.0f, width / 2.0f, -height / 2.0f, height / 2.0f, z_near, z_far);
}

auto create_perspective(
    const float fov_x,
    const float fov_y,
    const float z_near,
    const float z_far
) -> mat4
{
    const auto fov_y_clamped    = std::min(std::max(fov_y, epsilon), pi_minus_epsilon);
    const auto fov_x_clamped    = std::min(std::max(fov_x, epsilon), pi_minus_epsilon);
    const auto tan_x_half_angle = std::tan(fov_x_clamped * 0.5f);
    const auto tan_y_half_angle = std::tan(fov_y_clamped * 0.5f);
    const auto width            = 2.0f * z_near * tan_x_half_angle;
    const auto height           = 2.0f * z_near * tan_y_half_angle;
    return create_frustum_simple(width, height, z_near, z_far);
}

auto create_perspective_xr(
    const float fov_left,
    const float fov_right,
    const float fov_up,
    const float fov_down,
    const float z_near,
    const float z_far
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
    return create_frustum(left, right, down, up, z_near, z_far);
}

auto create_perspective_vertical(
    const float fov_y,
    const float aspect_ratio,
    const float z_near,
    const float z_far
) -> mat4
{
    const float near_minus_far = z_near - z_far;
    const float far_minus_near = z_far  - z_near;
    if ((aspect_ratio == 0.0f) || (near_minus_far == 0.0f) || (far_minus_near == 0.0f)) {
        return glm::mat4{1.0f}; // TODO log warning
    }
    const float fov_y_clamped  = std::min(std::max(fov_y, epsilon), pi_minus_epsilon);
    return glm::perspective(fov_y_clamped, aspect_ratio, z_near, z_far);

    // See glm::perspectiveRH_ZO():

    // const float tan_half_angle = std::tan(fov_y_clamped * 0.5f);
    // const float x = 1.0f / (aspect_ratio * tan_half_angle);
    // const float y = 1.0f / (tan_half_angle);
    // const float c = z_far / (z_near - z_far);
    // const float d = -(z_far * z_near) / (z_far - z_near);
    // 
    // return mat4{
    //     x, 0, 0, 0,
    //     0, y, 0, 0,
    //     0, 0, c, -1.0f,
    //     0, 0, d, 0,
    // };
}

auto create_perspective_horizontal(
    const float fov_x,
    const float aspect_ratio,
    const float z_near,
    const float z_far
) -> mat4
{
    if (aspect_ratio == 0.0f) {
        return glm::mat4{1.0f}; // TODO log warning
    }
    const auto fov_x_clamped  = std::min(std::max(fov_x, epsilon), pi_minus_epsilon);
    const auto tan_half_angle = std::tan(fov_x_clamped * 0.5f);
    const auto width          = 2.0f * z_near * tan_half_angle;
    const auto height         = width / aspect_ratio;
    return create_frustum_simple(width, height, z_near, z_far);
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
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float z_near,
    const float z_far
) -> mat4
{
    const float width          = right  - left;
    const float height         = top    - bottom;
    const float far_minus_near = z_far  - z_near;
    if ((width == 0.0) || (height == 0.0f) || (far_minus_near == 0.0f)) {
        return glm::mat4{1.0f}; // TODO Log warning
    }
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
            -1.0f / far_minus_near,
            0.0f
        },
        {
            -(right + left) / width,
            -(top + bottom) / height,
            -z_near / far_minus_near,
            1.0f
        }
    };
}

auto create_orthographic_centered(
    const float width,
    const float height,
    const float z_near,
    const float z_far
) -> mat4
{
    // TODO Check bottom and top
    return create_orthographic(
        -width  / 2.0f,  width  / 2.0f,
         height / 2.0f, -height / 2.0f,
        z_near, z_far
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

auto calculate_bounding_convex_hull(const Bounding_volume_source& source) -> Convex_hull
{
    std::vector<quickhull::Vector3<float>> point_cloud;
    for (size_t i = 0, i_end = source.get_element_count(); i < i_end; ++i) {
        for (size_t j = 0, j_end = source.get_element_point_count(i); j < j_end; ++j) {
            const auto point = source.get_point(i, j);
            if (point.has_value()) {
                const auto p = point.value();
                point_cloud.push_back(quickhull::Vector3<float>{p.x, p.y, p.z});
            }
        }
    }

    constexpr bool  ccw                  = true;
    constexpr bool  use_original_indices = false;
    //constexpr float epsilon              = 0.0001f;

    quickhull::QuickHull<float> quick_hull{};
    quickhull::ConvexHull<float> convex_hull = quick_hull.getConvexHull(
        point_cloud,
        ccw,
        use_original_indices,
        epsilon
    );

    Convex_hull result{};
    const std::vector<size_t>& index_buffer = convex_hull.getIndexBuffer();
    result.triangle_indices.reserve(index_buffer.size() / 3);
    for (size_t i = 0, end = index_buffer.size(); i < end; i += 3) {
        result.triangle_indices.push_back(
            {
                index_buffer[i + 0],
                index_buffer[i + 1],
                index_buffer[i + 2]
            }
        );
    }
    const quickhull::VertexDataSource<float>& vertex_data_source = convex_hull.getVertexBuffer();
    result.points.reserve(vertex_data_source.size());
    for (quickhull::Vector3<float> p : vertex_data_source) {
        result.points.push_back(
            glm::vec3{p.x, p.y, p.z}
        );
    }
    return result;
}

[[nodiscard]] inline auto cross(const glm::vec2& a, const glm::vec2& b) -> float {
    return a.x * b.y - a.y * b.x;
}

[[nodiscard]] inline auto same_sign(float a, float b, float c) -> bool {
    return (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0);
}

auto calculate_bounding_convex_hull(const std::vector<glm::vec2>& point_cloud) -> std::vector<glm::vec2>
{
    class Convex_hull_task
    {
    public:
        void execute(std::stack<Convex_hull_task>& tasks, std::vector<glm::vec2>& convex_hull)
        {
            // From the given set of points in Sk, find farthest point, say C, from segment PQ
            glm::vec2       C;
            float           max_area = 0.0f;
            const glm::vec2 AB       = B - A;
            for (const glm::vec2 P : S) {
                const float area = std::abs(cross(AB, B - P));
                if (area > max_area) {
                    max_area = area;
                    C = P;
                }
            }

            // Add point C to convex hull - sorted later using angle
            convex_hull.push_back(C);

            // Three points P, Q, and C partition the remaining points of Sk into 3 subsets: S0, S1, and S2
            //     where S0 are points inside triangle PCQ, S1 are points on the right side of the oriented
            //     line from P to C, and S2 are points on the right side of the oriented line from C to Q.
            const glm::vec2 AC = C - A;
            const glm::vec2 BC = C - B;
            const glm::vec2 CB = B - C;
            const glm::vec2 CA = A - C;
            Convex_hull_task S1{ .A = A, .B = C, .S = {} };
            Convex_hull_task S2{ .A = C, .B = B, .S = {} };
            for (const glm::vec2 P : S) {
                const glm::vec2 AP = P - A;
                const glm::vec2 BP = P - B;
                const glm::vec2 CP = P - C;
                const float cross1 = cross(AB, AP);
                const float cross2 = cross(BC, BP);
                const float cross3 = cross(CA, CP);
                if (same_sign(cross1, cross2, cross3)) continue; // S0 - point inside triangle ACB

                if (cross(AC, AP) < 0.0f) {
                    S1.S.push_back(P);
                } else if (cross(CB, CP) < 0.0f) {
                    S2.S.push_back(P);
                }
            }
            if (!S1.S.empty()) {
                tasks.push(std::move(S1));
            }
            if (!S2.S.empty()) {
                tasks.push(std::move(S2));
            }
        }
        glm::vec2              A; // aka P
        glm::vec2              B; // aka Q
        std::vector<glm::vec2> S;
    };

    // Find left and right most points, say A & B, and add A & B to convex hull
    glm::vec2 A{std::numeric_limits<float>::max()};
    glm::vec2 B{std::numeric_limits<float>::lowest()};
    for (const glm::vec2 P : point_cloud) {
        if ((P.x < A.x) || ((P.x == A.x) && (P.y < A.y))) {
            A = P;
        }

        if ((P.x > B.x) || ((P.x == B.x) && (P.y > B.y))) {
            B = P;
        }
    }

    std::vector<glm::vec2> convex_hull;
    convex_hull.push_back(A);
    convex_hull.push_back(B);

    // Segment AB divides the remaining (n − 2) points into 2 groups S1 and S2
    //     where S1 are points in S that are on the right side of the oriented line from A to B,
    //     and S2 are points in S that are on the right side of the oriented line from B to A
    const glm::vec2 B_minus_A = B - A;
    Convex_hull_task above{.A = A, .B = B, .S = {}};
    Convex_hull_task below{.A = A, .B = B, .S = {}};
    for (glm::vec2 P : point_cloud) {
        if ((P == A) || (P == B)) {
            continue;
        }
        const float area = cross(B_minus_A, P - A);
        if (area > 0.0f) {
            above.S.push_back(P);
        } else if (area < 0.0f) {
            below.S.push_back(P);
        }
    }

    std::stack<Convex_hull_task> tasks;
    tasks.push(std::move(above));
    tasks.push(std::move(below));

    while (!tasks.empty()) {
        Convex_hull_task task = tasks.top();
        tasks.pop();
        task.execute(tasks, convex_hull);
    }

    // Sort based on angle
    glm::vec2 centroid{0.0f, 0.0f};
    for (glm::vec2 P : convex_hull) {
        centroid += P;
    }
    centroid /= static_cast<float>(convex_hull.size());
    std::sort(
        convex_hull.begin(),
        convex_hull.end(),
        [centroid](const glm::vec2& lhs, const glm::vec2& rhs) -> bool {
            const float angle_lhs = std::atan2(lhs.y - centroid.y, lhs.x - centroid.x);
            const float angle_rhs = std::atan2(rhs.y - centroid.y, rhs.x - centroid.x);
            return angle_lhs < angle_rhs;
        }
    );
    return convex_hull;
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
    // Check box outside/inside of frustum
    for (int i = 0; i < 6; i++) {
        int out = 0;
        out += ((glm::dot(planes[i], glm::vec4(aabb.min.x, aabb.min.y, aabb.min.z, 1.0f)) < 0.0f) ? 1 : 0);
        out += ((glm::dot(planes[i], glm::vec4(aabb.max.x, aabb.min.y, aabb.min.z, 1.0f)) < 0.0f) ? 1 : 0);
        out += ((glm::dot(planes[i], glm::vec4(aabb.min.x, aabb.max.y, aabb.min.z, 1.0f)) < 0.0f) ? 1 : 0);
        out += ((glm::dot(planes[i], glm::vec4(aabb.max.x, aabb.max.y, aabb.min.z, 1.0f)) < 0.0f) ? 1 : 0);
        out += ((glm::dot(planes[i], glm::vec4(aabb.min.x, aabb.min.y, aabb.max.z, 1.0f)) < 0.0f) ? 1 : 0);
        out += ((glm::dot(planes[i], glm::vec4(aabb.max.x, aabb.min.y, aabb.max.z, 1.0f)) < 0.0f) ? 1 : 0);
        out += ((glm::dot(planes[i], glm::vec4(aabb.min.x, aabb.max.y, aabb.max.z, 1.0f)) < 0.0f) ? 1 : 0);
        out += ((glm::dot(planes[i], glm::vec4(aabb.max.x, aabb.max.y, aabb.max.z, 1.0f)) < 0.0f) ? 1 : 0);
        if (out == 8) {
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


} // namespace erhe::math
