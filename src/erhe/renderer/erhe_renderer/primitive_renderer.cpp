#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"

#include "erhe_scene/camera.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/sphere.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtx/norm.hpp>

namespace erhe::renderer {

// Note that this relies on bucket being stable, as in etl::vector<> when elements are never removed.
Primitive_renderer::Primitive_renderer(Debug_renderer& debug_renderer, Debug_renderer_bucket& bucket)
    : m_debug_renderer    {&debug_renderer}
    , m_bucket            {&bucket}
    , m_line_vertex_stride{m_debug_renderer->get_program_interface().line_vertex_struct->get_size_bytes()}
{
}

Primitive_renderer::Primitive_renderer(Primitive_renderer&& old) noexcept
    : m_debug_renderer              {std::exchange(old.m_debug_renderer, nullptr)}
    , m_bucket                      {std::exchange(old.m_bucket,         nullptr)}
    , m_line_vertex_stride          {m_debug_renderer->get_program_interface().line_vertex_struct->get_size_bytes()}
    , m_last_allocate_gpu_float_data{nullptr}
    , m_last_allocate_word_offset   {0}
    , m_last_allocate_word_count    {0}
    , m_line_color                  {old.m_line_color}
    , m_half_line_thickness         {old.m_half_line_thickness}
{
    ERHE_VERIFY(old.m_last_allocate_gpu_data.empty());
    ERHE_VERIFY(old.m_last_allocate_gpu_float_data == nullptr);
    ERHE_VERIFY(old.m_last_allocate_word_offset == 0);
    ERHE_VERIFY(old.m_last_allocate_word_count == 0);
}

auto Primitive_renderer::operator=(Primitive_renderer&& old) noexcept -> Primitive_renderer&
{
    m_debug_renderer = std::exchange(old.m_debug_renderer, nullptr);
    m_bucket         = std::exchange(old.m_bucket, nullptr);
    m_line_color     = old.m_line_color;
    m_half_line_thickness = old.m_half_line_thickness;
    ERHE_VERIFY(m_last_allocate_gpu_data.empty());
    ERHE_VERIFY(m_last_allocate_gpu_float_data == nullptr);
    ERHE_VERIFY(m_last_allocate_word_offset == 0);
    ERHE_VERIFY(m_last_allocate_word_count == 0);
    ERHE_VERIFY(old.m_last_allocate_gpu_data.empty());
    ERHE_VERIFY(old.m_last_allocate_gpu_float_data == nullptr);
    ERHE_VERIFY(old.m_last_allocate_word_offset == 0);
    ERHE_VERIFY(old.m_last_allocate_word_count == 0);
    return *this;
}

void Primitive_renderer::reserve_lines(std::size_t line_count)
{
    std::size_t byte_count     = line_count * 2 * m_line_vertex_stride;
    m_last_allocate_word_count = byte_count / sizeof(float);
    m_last_allocate_gpu_data   = m_bucket->make_draw(byte_count, 0);

    if (m_last_allocate_gpu_data.size_bytes() < byte_count) {
        m_last_allocate_gpu_float_data = nullptr;
        m_last_allocate_word_offset    = 0;
    } else {
        std::byte* start = m_last_allocate_gpu_data.data();
        m_last_allocate_gpu_float_data = reinterpret_cast<float*>(start);
        m_last_allocate_word_offset    = 0;
    }
}

void Primitive_renderer::make_primitives(std::size_t primitive_count, std::size_t vertices_per_primitive)
{
    std::size_t byte_count     = primitive_count * vertices_per_primitive * m_line_vertex_stride;
    m_last_allocate_word_count = byte_count / sizeof(float);
    m_last_allocate_gpu_data   = m_bucket->make_draw(byte_count, primitive_count);

    if (m_last_allocate_gpu_data.size_bytes() < byte_count) {
        m_last_allocate_gpu_float_data = nullptr;
        m_last_allocate_word_offset    = 0;
    } else {
        std::byte* start = m_last_allocate_gpu_data.data();
        m_last_allocate_gpu_float_data = reinterpret_cast<float*>(start);
        m_last_allocate_word_offset    = 0;
    }
}

void Primitive_renderer::make_lines(std::size_t line_count)
{
    make_primitives(line_count, 2);
}

void Primitive_renderer::set_line_color(const float r, const float g, const float b, const float a)
{
    m_line_color = glm::vec4{r, g, b, a};
}

void Primitive_renderer::set_line_color(const glm::vec3& color)
{
    m_line_color = glm::vec4{color, 1.0f};
}

void Primitive_renderer::set_line_color(const glm::vec4& color)
{
    m_line_color = color;
}

void Primitive_renderer::set_thickness(const float thickness)
{
    m_half_line_thickness = 0.5f * thickness;
}

#pragma region add
void Primitive_renderer::add_lines(const glm::mat4& transform, std::span<Line> lines)
{
    make_lines(lines.size());
    for (const Line& line : lines) {
        const glm::vec4 p0{transform * glm::vec4{line.p0, 1.0f}};
        const glm::vec4 p1{transform * glm::vec4{line.p1, 1.0f}};
        put(glm::vec3{p0} / p0.w, m_half_line_thickness, m_line_color);
        put(glm::vec3{p1} / p1.w, m_half_line_thickness, m_line_color);
    }
}

void Primitive_renderer::add_lines(const glm::mat4& transform, const std::vector<Line>& lines)
{
    make_lines(lines.size());
    for (const Line& line : lines) {
        const glm::vec4 p0{transform * glm::vec4{line.p0, 1.0f}};
        const glm::vec4 p1{transform * glm::vec4{line.p1, 1.0f}};
        put(glm::vec3{p0} / p0.w, m_half_line_thickness, m_line_color);
        put(glm::vec3{p1} / p1.w, m_half_line_thickness, m_line_color);
    }
}

void Primitive_renderer::add_lines(const glm::mat4& transform, const std::initializer_list<Line> lines)
{
    make_lines(lines.size());
    for (const Line& line : lines) {
        const glm::vec4 p0{transform * glm::vec4{line.p0, 1.0f}};
        const glm::vec4 p1{transform * glm::vec4{line.p1, 1.0f}};
        put(glm::vec3{p0} / p0.w, m_half_line_thickness, m_line_color);
        put(glm::vec3{p1} / p1.w, m_half_line_thickness, m_line_color);
    }
}

void Primitive_renderer::add_lines(const glm::mat4& transform, const std::initializer_list<Line4> lines)
{
    make_lines(lines.size());
    for (const Line4& line : lines) {
        const glm::vec4 p0{transform * glm::vec4{glm::vec3{line.p0}, 1.0f}};
        const glm::vec4 p1{transform * glm::vec4{glm::vec3{line.p1}, 1.0f}};
        put(glm::vec3{p0} / p0.w, line.p0.w, m_line_color);
        put(glm::vec3{p1} / p1.w, line.p1.w, m_line_color);
    }
}

void Primitive_renderer::add_line(const glm::vec4& color0, float width0, glm::vec3 p0, const glm::vec4& color1, float width1, glm::vec3 p1)
{
    make_lines(1);
    put(p0, 0.5f * width0, color0);
    put(p1, 0.5f * width1, color1);
}

void Primitive_renderer::add_lines(const std::vector<Line>& lines)
{
    make_lines(lines.size());
    for (const Line& line : lines) {
        put(line.p0, m_half_line_thickness, m_line_color);
        put(line.p1, m_half_line_thickness, m_line_color);
    }
}

void Primitive_renderer::add_lines(const std::initializer_list<Line> lines)
{
    make_lines(lines.size());
    for (const Line& line : lines) {
        put(line.p0, m_half_line_thickness, m_line_color);
        put(line.p1, m_half_line_thickness, m_line_color);
    }
}

void Primitive_renderer::add_triangle(
    const glm::mat4& transform,
    const glm::vec4& color,
    const glm::vec3& p0,
    const glm::vec3& p1,
    const glm::vec3& p2
)
{
    make_primitives(1, 3);
    const glm::vec4 q0{transform * glm::vec4{p0, 1.0f}};
    const glm::vec4 q1{transform * glm::vec4{p1, 1.0f}};
    const glm::vec4 q2{transform * glm::vec4{p2, 1.0f}};
    put(glm::vec3{q0} / q0.w, 0.0f, color);
    put(glm::vec3{q1} / q1.w, 0.0f, color);
    put(glm::vec3{q2} / q2.w, 0.0f, color);
}

void Primitive_renderer::add_triangles(
    const glm::mat4&                 transform,
    const glm::vec4&                 color,
    const std::span<const glm::vec3> positions,
    const std::span<const uint32_t>  indices
)
{
    const std::size_t triangle_count = indices.size() / 3;
    if (triangle_count == 0) {
        return;
    }
    make_primitives(triangle_count, 3);
    const std::size_t index_count = triangle_count * 3;
    for (std::size_t i = 0; i < index_count; ++i) {
        const glm::vec3& p = positions[indices[i]];
        const glm::vec4   q{transform * glm::vec4{p, 1.0f}};
        put(glm::vec3{q} / q.w, 0.0f, color);
    }
}

void Primitive_renderer::add_plane_indicator(const glm::vec4& color, const glm::vec3& center, const glm::vec3& normal)
{
    const glm::vec3 unit_normal = glm::normalize(normal);
    glm::vec3 tangent;
    glm::vec3 bitangent;
    erhe::math::get_plane_basis(unit_normal, tangent, bitangent);
    const glm::vec4 zero{0.0f, 0.0f, 0.0f, 0.0f};

    make_lines(5);
    put(center,               m_half_line_thickness * 5.0f, color);
    put(center + unit_normal, 0.0f, zero);

    put(center - tangent,        m_half_line_thickness, zero);
    put(center,           0.7f * m_half_line_thickness, color);
    put(center,           0.7f * m_half_line_thickness, color);
    put(center + tangent,        m_half_line_thickness, zero);

    put(center - bitangent,        m_half_line_thickness, zero);
    put(center,             0.7f * m_half_line_thickness, color);
    put(center,             0.7f * m_half_line_thickness, color);
    put(center + bitangent,        m_half_line_thickness, zero);
}

void Primitive_renderer::add_plane(const glm::vec4& color, const glm::vec4& plane)
{
    const glm::vec3 center = erhe::math::get_point_on_plane(plane);
    const glm::vec3 normal{plane};
    add_plane_indicator(color, center, normal);

    glm::vec3 tangent;
    glm::vec3 bitangent;
    erhe::math::get_plane_basis(glm::normalize(normal), tangent, bitangent);
    make_lines(4);
    put(center + tangent - bitangent, m_half_line_thickness, color);
    put(center + tangent + bitangent, m_half_line_thickness, color);

    put(center + tangent + bitangent, m_half_line_thickness, color);
    put(center - tangent + bitangent, m_half_line_thickness, color);

    put(center - tangent + bitangent, m_half_line_thickness, color);
    put(center - tangent - bitangent, m_half_line_thickness, color);

    put(center - tangent - bitangent, m_half_line_thickness, color);
    put(center + tangent - bitangent, m_half_line_thickness, color);
}

void Primitive_renderer::add_cube(
    const glm::mat4& transform,
    const glm::vec4& color,
    const glm::vec3& min_corner,
    const glm::vec3& max_corner,
    const bool       z_cross
)
{
    const auto a = min_corner;
    const auto b = max_corner;
    glm::vec3 p[8] = {
        glm::vec3{a.x, a.y, a.z},
        glm::vec3{b.x, a.y, a.z},
        glm::vec3{b.x, b.y, a.z},
        glm::vec3{a.x, b.y, a.z},
        glm::vec3{a.x, a.y, b.z},
        glm::vec3{b.x, a.y, b.z},
        glm::vec3{b.x, b.y, b.z},
        glm::vec3{a.x, b.y, b.z}
    };
    // 12 lines
    add_lines(
        transform,
        color,
        {
            // near plane
            { p[0], p[1] },
            { p[1], p[2] },
            { p[2], p[3] },
            { p[3], p[0] },

            // far plane
            { p[4], p[5] },
            { p[5], p[6] },
            { p[6], p[7] },
            { p[7], p[4] },

            // near to far
            { p[0], p[4] },
            { p[1], p[5] },
            { p[2], p[6] },
            { p[3], p[7] }
        }
    );
    if (z_cross)
    {
        // lines
        add_lines(
            transform,
            0.5f * color,
            {
                // near to far middle
                { 0.5f * p[0] + 0.5f * p[1], 0.5f * p[4] + 0.5f * p[5] },
                { 0.5f * p[1] + 0.5f * p[2], 0.5f * p[5] + 0.5f * p[6] },
                { 0.5f * p[2] + 0.5f * p[3], 0.5f * p[6] + 0.5f * p[7] },
                { 0.5f * p[3] + 0.5f * p[0], 0.5f * p[7] + 0.5f * p[4] },

                // near+far/2 plane
                { 0.5f * p[0] + 0.5f * p[4], 0.5f * p[1] + 0.5f * p[5] },
                { 0.5f * p[1] + 0.5f * p[5], 0.5f * p[2] + 0.5f * p[6] },
                { 0.5f * p[2] + 0.5f * p[6], 0.5f * p[3] + 0.5f * p[7] },
                { 0.5f * p[3] + 0.5f * p[7], 0.5f * p[0] + 0.5f * p[4] },
            }
        );
    }
}

void Primitive_renderer::add_bone(
    const glm::mat4& transform,
    const glm::vec4& color,
    const glm::vec3& start,
    const glm::vec3& end
)
{
    add_lines(transform, color, {{ start, end }} );
}

void Primitive_renderer::add_sphere(
    const erhe::scene::Transform&       world_from_local,
    const glm::vec4&                    major_color,
    const glm::vec4&                    minor_color,
    const float                         major_thickness,
    const float                         minor_thickness,
    const glm::vec3&                    local_center,
    const float                         local_radius,
    const erhe::scene::Transform* const camera_world_from_node,
    const int                           step_count
)
{
    const erhe::math::Sphere sphere = erhe::math::transform(
        world_from_local.get_matrix(),
        erhe::math::Sphere{
            .center = local_center,
            .radius = local_radius
        }
    );
    const float     radius = sphere.radius;
    const glm::vec3 center = sphere.center;

    constexpr glm::vec3 axis_x{1.0f, 0.0f, 0.0f};
    constexpr glm::vec3 axis_y{0.0f, 1.0f, 0.0f};
    constexpr glm::vec3 axis_z{0.0f, 0.0f, 1.0f};

    glm::vec3 camera_position{0.0f};
    bool      camera_outside = false;
    if (camera_world_from_node != nullptr) {
        camera_position = glm::vec3{camera_world_from_node->get_matrix() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
        camera_outside  = glm::length2(camera_position - center) > (radius * radius);
    }
    const glm::vec3 w = camera_position - center;

    // Great circles in the world XY, YZ and XZ planes. With a camera outside
    // the sphere, each circle is split at the exact horizon: the point
    // q(t) = center + radius (cos(t) u + sin(t) v) is visible iff
    // (q - center) . (camera - q) > 0, which reduces to
    // (u . w) cos(t) + (v . w) sin(t) > radius. The boundary points satisfy
    // the horizon condition, i.e. the visible arc endpoints lie exactly on
    // the silhouette circle drawn below.
    const auto add_great_circle = [&](const glm::vec3& u, const glm::vec3& v) {
        const auto add_arc = [&](const float t_first, const float t_span, const glm::vec4& color, const float thickness) {
            if (t_span <= 0.0f) {
                return;
            }
            set_thickness(thickness);
            const int segment_count = std::max(1, static_cast<int>(std::ceil(static_cast<float>(step_count) * t_span / glm::two_pi<float>())));
            for (int i = 0; i < segment_count; ++i) {
                const float t0 = t_first + (t_span * static_cast<float>(i    ) / static_cast<float>(segment_count));
                const float t1 = t_first + (t_span * static_cast<float>(i + 1) / static_cast<float>(segment_count));
                add_lines(
                    color,
                    {{
                        center + radius * ((std::cos(t0) * u) + (std::sin(t0) * v)),
                        center + radius * ((std::cos(t1) * u) + (std::sin(t1) * v))
                    }}
                );
            }
        };

        const float a      = glm::dot(u, w);
        const float b      = glm::dot(v, w);
        const float radial = std::sqrt((a * a) + (b * b));
        if (!camera_outside || (radial <= radius)) {
            // No camera, camera inside the sphere, or the whole circle is at
            // or behind the horizon
            add_arc(0.0f, glm::two_pi<float>(), minor_color, minor_thickness);
            return;
        }
        const float t_mid = std::atan2(b, a);
        const float t_cut = std::acos(glm::clamp(radius / radial, -1.0f, 1.0f));
        add_arc(t_mid - t_cut, 2.0f * t_cut, major_color, major_thickness);
        add_arc(t_mid + t_cut, glm::two_pi<float>() - (2.0f * t_cut), minor_color, minor_thickness);
    };
    add_great_circle(axis_x, axis_y);
    add_great_circle(axis_y, axis_z);
    add_great_circle(axis_x, axis_z);

    if (!camera_outside) {
        return; // no silhouette without a camera or from inside the sphere
    }

    //                             C = sphere center        .
    //                             r = sphere radius        .
    //         /|                  V = camera center        .
    //        / |  .               d = distance(C, V)       .
    //      r/  |     . b          d*d = r*r + b*b          .
    //      /   |h       .         d*d - r*r = b*b          .
    //     /    |           .      b = sqrt(d*d - r*r)      .
    //    /___p_|_____q________.   h = (r*b) / d            .
    //   C      P d             V  p*p + h*h = r*r          .
    //                             p = sqrt(r*r - h*h)      .

    const float r2 = radius * radius;
    const float d2 = glm::length2(w);
    const float d  = std::sqrt(d2);
    const float b  = std::sqrt(d2 - r2);
    const float h  = radius * b / d;
    const float p  = std::sqrt(r2 - (h * h));

    const glm::vec3 from_sphere_to_camera_direction = w / d;
    const glm::vec3 from_camera_to_sphere_direction = -from_sphere_to_camera_direction;

    const glm::vec3 P              = center + p * from_sphere_to_camera_direction;
    const glm::vec3 up0_direction  = glm::vec3{camera_world_from_node->get_matrix() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const glm::vec3 side_direction = erhe::math::safe_normalize_cross<float>(from_camera_to_sphere_direction, up0_direction);
    const glm::vec3 up_direction   = erhe::math::safe_normalize_cross<float>(side_direction, from_camera_to_sphere_direction);
    const glm::vec3 axis_a         = h * side_direction;
    const glm::vec3 axis_b         = h * up_direction;

    set_thickness(major_thickness);
    for (int i = 0; i < step_count; ++i) {
        const float t0 = glm::two_pi<float>() * static_cast<float>(i    ) / static_cast<float>(step_count);
        const float t1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(step_count);
        add_lines(
            major_color,
            {
                {
                    P + std::cos(t0) * axis_a + std::sin(t0) * axis_b,
                    P + std::cos(t1) * axis_a + std::sin(t1) * axis_b
                }
            }
        );
    }
}

auto sign(const float x) -> float
{
    return (x < 0.0f) ? -1.0f : (x == 0.0f) ? 0.0f : 1.0f;
}

auto sign(const double x) -> double
{
    return (x < 0.0) ? -1.0 : (x == 0.0) ? 0.0 : 1.0;
}

void Primitive_renderer::add_cone(
    const erhe::scene::Transform& world_from_node,
    const glm::vec4&              major_color,
    const glm::vec4&              minor_color,
    const float                   major_thickness,
    const float                   minor_thickness,
    const glm::vec3&              bottom_center,
    const float                   height,
    const float                   bottom_radius,
    const float                   top_radius,
    const glm::vec3&              camera_position_in_world,
    const int                     side_count
)
{
    constexpr glm::vec3 axis_x       {1.0f, 0.0f, 0.0f};
    constexpr glm::vec3 axis_y       {0.0f, 1.0f, 0.0f};
    constexpr glm::vec3 axis_z       {0.0f, 0.0f, 1.0f};
    constexpr glm::vec3 bottom_normal{0.0f, -1.0f, 0.0f};
    constexpr glm::vec3 top_normal   {0.0f,  1.0f, 0.0f};

    const glm::mat4 m                       = world_from_node.get_matrix();
    const glm::mat4 node_from_world         = world_from_node.get_inverse_matrix();
    const glm::vec3 top_center              = bottom_center + height * axis_y;
    const glm::vec3 camera_position_in_node = glm::vec3{node_from_world * glm::vec4{camera_position_in_world, 1.0f}};

    const glm::vec3 bottom_v       = glm::normalize(camera_position_in_node - bottom_center);
    const bool      bottom_visible = glm::dot(bottom_normal, bottom_v) >= 0.0f;
    const glm::vec3 top_v          = glm::normalize(camera_position_in_node - top_center);
    const bool      top_visible    = glm::dot(top_normal, top_v) >= 0.0f;

    set_thickness(minor_thickness);
    add_lines(
        m,
        minor_color,
        {
            { bottom_center - bottom_radius * axis_x, bottom_center + bottom_radius * axis_x },
            { bottom_center - bottom_radius * axis_z, bottom_center + bottom_radius * axis_z },
            { top_center    - top_radius    * axis_x, top_center    + top_radius    * axis_x },
            { top_center    - top_radius    * axis_z, top_center    + top_radius    * axis_z },
            { bottom_center,                          top_center                             },
            { bottom_center - bottom_radius * axis_x, top_center    - top_radius    * axis_x },
            { bottom_center + bottom_radius * axis_x, top_center    + top_radius    * axis_x },
            { bottom_center - bottom_radius * axis_z, top_center    - top_radius    * axis_z },
            { bottom_center + bottom_radius * axis_z, top_center    + top_radius    * axis_z }
        }
    );

    // The outward normal along the lateral generatrix at azimuth phi,
    // n(phi) = (height cos(phi), bottom_radius - top_radius, height sin(phi)) / slant,
    // is constant along the whole generatrix, and so is n(phi) . (camera - q)
    // for q on the generatrix. Whether the lateral surface faces the camera
    // is therefore an exact function of phi alone:
    //   facing(phi) = tangent_a cos(phi) + tangent_b sin(phi) - tangent_c > 0
    // and the two silhouette generatrices sit at the exact zero crossings
    // phi_mid -/+ phi_delta (the tangent planes through the camera).
    const glm::vec3 w            = camera_position_in_node - bottom_center;
    const float     tangent_a    = height * w.x;
    const float     tangent_b    = height * w.z;
    const float     tangent_c    = (bottom_radius * height) - ((bottom_radius - top_radius) * w.y);
    const float     tangent_norm = std::sqrt((tangent_a * tangent_a) + (tangent_b * tangent_b));
    const float     phi_mid      = std::atan2(tangent_b, tangent_a);

    bool  facing_all  = false;
    bool  facing_none = false;
    float phi_delta   = 0.0f; // facing arc is (phi_mid - phi_delta, phi_mid + phi_delta)
    if (tangent_norm > std::abs(tangent_c)) {
        phi_delta = std::acos(glm::clamp(tangent_c / tangent_norm, -1.0f, 1.0f));
    } else {
        facing_all  = (tangent_c < 0.0f);
        facing_none = !facing_all;
    }

    // Rim circles: visible parts (cap visible or lateral surface facing the
    // camera) in major style, hidden parts in minor style, split at the exact
    // silhouette azimuths so the arcs join the silhouette generatrices.
    const auto add_rim = [&](const glm::vec3& rim_center, const float rim_radius, const bool cap_visible) {
        if (rim_radius <= 0.0f) {
            return;
        }
        const auto add_rim_arc = [&](const float t_first, const float t_span, const glm::vec4& color, const float thickness) {
            if (t_span <= 0.0f) {
                return;
            }
            set_thickness(thickness);
            const int step_count = std::max(1, static_cast<int>(std::ceil(static_cast<float>(side_count) * t_span / glm::two_pi<float>())));
            for (int i = 0; i < step_count; ++i) {
                const float t0 = t_first + (t_span * static_cast<float>(i    ) / static_cast<float>(step_count));
                const float t1 = t_first + (t_span * static_cast<float>(i + 1) / static_cast<float>(step_count));
                add_lines(
                    m,
                    color,
                    {{
                        rim_center + rim_radius * ((std::cos(t0) * axis_x) + (std::sin(t0) * axis_z)),
                        rim_center + rim_radius * ((std::cos(t1) * axis_x) + (std::sin(t1) * axis_z))
                    }}
                );
            }
        };
        if (cap_visible || facing_all) {
            add_rim_arc(0.0f, glm::two_pi<float>(), major_color, major_thickness);
        } else if (facing_none) {
            add_rim_arc(0.0f, glm::two_pi<float>(), minor_color, minor_thickness);
        } else {
            add_rim_arc(phi_mid - phi_delta, 2.0f * phi_delta, major_color, major_thickness);
            add_rim_arc(phi_mid + phi_delta, glm::two_pi<float>() - (2.0f * phi_delta), minor_color, minor_thickness);
        }
    };
    add_rim(bottom_center, bottom_radius, bottom_visible);
    add_rim(top_center,    top_radius,    top_visible);

    // Silhouette generatrices at the exact tangency azimuths
    if (!facing_all && !facing_none) {
        set_thickness(major_thickness);
        for (const float phi : { phi_mid - phi_delta, phi_mid + phi_delta }) {
            const glm::vec3 radial = (std::cos(phi) * axis_x) + (std::sin(phi) * axis_z);
            add_lines(m, major_color, { { bottom_center + bottom_radius * radial, top_center + top_radius * radial } });
        }
    }
}

void Primitive_renderer::add_capsule(
    const erhe::scene::Transform& world_from_node,
    const glm::vec4&              major_color,
    const glm::vec4&              minor_color,
    const float                   major_thickness,
    const float                   minor_thickness,
    const glm::vec3&              bottom_center,
    const float                   length,
    const float                   bottom_radius,
    const float                   top_radius,
    const glm::vec3&              camera_position_in_world,
    const int                     side_count
)
{
    constexpr glm::vec3 axis_x{1.0f, 0.0f, 0.0f};
    constexpr glm::vec3 axis_y{0.0f, 1.0f, 0.0f};
    constexpr glm::vec3 axis_z{0.0f, 0.0f, 1.0f};

    const glm::mat4 m                       = world_from_node.get_matrix();
    const glm::mat4 node_from_world         = world_from_node.get_inverse_matrix();
    const glm::vec3 top_center              = bottom_center + length * axis_y;
    const glm::vec3 camera_position_in_node = glm::vec3{node_from_world * glm::vec4{camera_position_in_world, 1.0f}};

    // The tangent cone joining the cap spheres touches them at latitude angle
    // alpha with sin(alpha) = (bottom_radius - top_radius) / length; the caps
    // span latitudes [-pi/2, alpha] (bottom) and [alpha, pi/2] (top).
    const float radius_delta = bottom_radius - top_radius;
    const float sin_alpha    = (radius_delta == 0.0f) ? 0.0f : glm::clamp(radius_delta / length, -1.0f, 1.0f);
    const float cos_alpha    = std::sqrt(1.0f - (sin_alpha * sin_alpha));
    const float alpha        = std::asin(sin_alpha);

    const glm::vec3 bottom_ring_center = bottom_center + (bottom_radius * sin_alpha) * axis_y;
    const glm::vec3 top_ring_center    = top_center    + (top_radius    * sin_alpha) * axis_y;
    const float     bottom_ring_radius = bottom_radius * cos_alpha;
    const float     top_ring_radius    = top_radius    * cos_alpha;

    // Structural lines: junction (tangency) rings, cap profile arcs with the
    // connecting cone generatrices in the XY / ZY planes, and the axis
    set_thickness(minor_thickness);
    for (int i = 0; i < side_count; ++i) {
        const float phi0 = glm::two_pi<float>() * static_cast<float>(i    ) / static_cast<float>(side_count);
        const float phi1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(side_count);
        const glm::vec3 d0 = (std::cos(phi0) * axis_x) + (std::sin(phi0) * axis_z);
        const glm::vec3 d1 = (std::cos(phi1) * axis_x) + (std::sin(phi1) * axis_z);
        add_lines(
            m,
            minor_color,
            {
                { bottom_ring_center + bottom_ring_radius * d0, bottom_ring_center + bottom_ring_radius * d1 },
                { top_ring_center    + top_ring_radius    * d0, top_ring_center    + top_ring_radius    * d1 }
            }
        );
    }

    const int arc_step_count = std::max(2, side_count / 4);
    for (const glm::vec3& radial : { axis_x, -axis_x, axis_z, -axis_z }) {
        for (int i = 0; i < arc_step_count; ++i) {
            const float rel0 = static_cast<float>(i    ) / static_cast<float>(arc_step_count);
            const float rel1 = static_cast<float>(i + 1) / static_cast<float>(arc_step_count);
            const float bottom_theta0 = -glm::half_pi<float>() + ((alpha + glm::half_pi<float>()) * rel0);
            const float bottom_theta1 = -glm::half_pi<float>() + ((alpha + glm::half_pi<float>()) * rel1);
            const float top_theta0    = alpha + ((glm::half_pi<float>() - alpha) * rel0);
            const float top_theta1    = alpha + ((glm::half_pi<float>() - alpha) * rel1);
            add_lines(
                m,
                minor_color,
                {
                    {
                        bottom_center + bottom_radius * ((std::cos(bottom_theta0) * radial) + (std::sin(bottom_theta0) * axis_y)),
                        bottom_center + bottom_radius * ((std::cos(bottom_theta1) * radial) + (std::sin(bottom_theta1) * axis_y))
                    },
                    {
                        top_center + top_radius * ((std::cos(top_theta0) * radial) + (std::sin(top_theta0) * axis_y)),
                        top_center + top_radius * ((std::cos(top_theta1) * radial) + (std::sin(top_theta1) * axis_y))
                    }
                }
            );
        }
        add_lines(
            m,
            minor_color,
            { { bottom_ring_center + bottom_ring_radius * radial, top_ring_center + top_ring_radius * radial } }
        );
    }
    add_lines(m, minor_color, { { bottom_center - bottom_radius * axis_y, top_center + top_radius * axis_y } });

    // Silhouette: the capsule is convex and tangent continuous, so the view
    // silhouette is a single closed curve. A plane through the camera tangent
    // to the cone is tangent to both cap spheres as well (the cone normal n
    // satisfies n . (top_center - bottom_center) = length * sin(alpha) =
    // bottom_radius - top_radius), so the two cone silhouette generatrices
    // join the cap silhouette arcs exactly.
    set_thickness(major_thickness);

    // Cone silhouette generatrices: azimuths phi where the tangent plane with
    // normal n(phi) = (cos_alpha cos(phi), sin_alpha, cos_alpha sin(phi))
    // passes through the camera: n(phi) . (camera - bottom_center) = bottom_radius
    const glm::vec3 to_camera_from_bottom = camera_position_in_node - bottom_center;
    const float     tangent_a             = to_camera_from_bottom.x * cos_alpha;
    const float     tangent_b             = to_camera_from_bottom.z * cos_alpha;
    const float     tangent_c             = bottom_radius - (to_camera_from_bottom.y * sin_alpha);
    const float     tangent_norm          = std::sqrt((tangent_a * tangent_a) + (tangent_b * tangent_b));
    if (tangent_norm > std::abs(tangent_c)) {
        const float phi_mid   = std::atan2(tangent_b, tangent_a);
        const float phi_delta = std::acos(glm::clamp(tangent_c / tangent_norm, -1.0f, 1.0f));
        for (const float phi : { phi_mid - phi_delta, phi_mid + phi_delta }) {
            const glm::vec3 n =
                ((cos_alpha * std::cos(phi)) * axis_x) +
                (sin_alpha                   * axis_y) +
                ((cos_alpha * std::sin(phi)) * axis_z);
            add_lines(m, major_color, { { bottom_center + bottom_radius * n, top_center + top_radius * n } });
        }
    }

    // Cap silhouette arcs: the part of each sphere's view silhouette circle
    // whose latitude lies in the cap's range. The arc endpoints sit exactly
    // at the cap edge latitude alpha, i.e. on the cone generatrices above.
    const auto add_cap_silhouette_arc = [&](const glm::vec3& sphere_center, const float sphere_radius, const bool is_top) {
        const glm::vec3 to_camera = camera_position_in_node - sphere_center;
        const float     d2        = glm::dot(to_camera, to_camera);
        const float     r2        = sphere_radius * sphere_radius;
        if (d2 <= r2) {
            return; // camera inside the cap sphere - no silhouette
        }
        const float     d             = std::sqrt(d2);
        const float     h             = sphere_radius * std::sqrt(d2 - r2) / d; // silhouette circle radius
        const float     p             = std::sqrt(r2 - (h * h));                // sphere center to silhouette circle center
        const glm::vec3 dir           = to_camera / d;
        const glm::vec3 circle_center = sphere_center + p * dir;

        const glm::vec3 hint = (std::abs(dir.y) < 0.99f) ? axis_y : axis_x;
        const glm::vec3 e0   = erhe::math::safe_normalize_cross<float>(dir, hint);
        const glm::vec3 e1   = erhe::math::safe_normalize_cross<float>(e0, dir);

        // Cap test for circle point q(t) = circle_center + h (cos(t) e0 + sin(t) e1):
        // (q - sphere_center) . Y <= sphere_radius * sin(alpha) on the bottom cap
        // (>= on the top cap), i.e. wave_r * cos(t - t_mid) <=> cap_c
        const float wave_a = h * e0.y;
        const float wave_b = h * e1.y;
        const float wave_r = std::sqrt((wave_a * wave_a) + (wave_b * wave_b));
        const float cap_c  = (sphere_radius * sin_alpha) - (p * dir.y);

        float t_first;
        float t_span;
        if (wave_r < 1e-6f * sphere_radius) {
            // Silhouette circle is horizontal: fully on or fully off the cap
            const bool on_cap = is_top ? (cap_c <= 0.0f) : (cap_c >= 0.0f);
            if (!on_cap) {
                return;
            }
            t_first = 0.0f;
            t_span  = glm::two_pi<float>();
        } else {
            const float t_mid = std::atan2(wave_b, wave_a);
            const float t_cut = std::acos(glm::clamp(cap_c / wave_r, -1.0f, 1.0f));
            if (is_top) {
                t_first = t_mid - t_cut;
                t_span  = 2.0f * t_cut;
            } else {
                t_first = t_mid + t_cut;
                t_span  = glm::two_pi<float>() - (2.0f * t_cut);
            }
            if (t_span <= 0.0f) {
                return;
            }
        }

        const int step_count = std::max(1, static_cast<int>(std::ceil(static_cast<float>(side_count) * t_span / glm::two_pi<float>())));
        for (int i = 0; i < step_count; ++i) {
            const float t0 = t_first + (t_span * static_cast<float>(i    ) / static_cast<float>(step_count));
            const float t1 = t_first + (t_span * static_cast<float>(i + 1) / static_cast<float>(step_count));
            add_lines(
                m,
                major_color,
                {{
                    circle_center + h * ((std::cos(t0) * e0) + (std::sin(t0) * e1)),
                    circle_center + h * ((std::cos(t1) * e0) + (std::sin(t1) * e1))
                }}
            );
        }
    };
    add_cap_silhouette_arc(bottom_center, bottom_radius, false);
    add_cap_silhouette_arc(top_center,    top_radius,    true);
}

namespace {

struct Torus_point
{
    glm::vec3 p;
    glm::vec3 n;
};

[[nodiscard]] auto torus_point(const double R, const double r, const double rel_major, const double rel_minor) -> Torus_point
{
    const double    theta     = (glm::pi<double>() * 2.0 * rel_major);
    const double    phi       = (glm::pi<double>() * 2.0 * rel_minor);
    const double    sin_theta = std::sin(theta);
    const double    cos_theta = std::cos(theta);
    const double    sin_phi   = std::sin(phi);
    const double    cos_phi   = std::cos(phi);

    const double    vx = (R + r * cos_phi) * cos_theta;
    const double    vy = (R + r * cos_phi) * sin_theta;
    const double    vz =      r * sin_phi;

    const double    tx = -sin_theta;
    const double    ty =  cos_theta;
    const double    tz = 0.0f;
    const glm::vec3 T{tx, ty, tz};

    const double    bx = -sin_phi * cos_theta;
    const double    by = -sin_phi * sin_theta;
    const double    bz =  cos_phi;
    const glm::vec3 B{bx, by, bz};
    const glm::vec3 N = glm::normalize(glm::cross(T, B));

    return Torus_point{
        .p = glm::vec3{vx, vy, vz},
        .n = N
    };
}

// Adapted from https://www.shadertoy.com/view/4sBGDy
//
// The MIT License
// Copyright (C) 2014 Inigo Quilez
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// f(x) = (|x|^2 + R^2 - r^2)^2 - 4R^2|xy|^2 = 0
auto ray_torus_intersection(const glm::vec3 ro, const glm::vec3 rd, const glm::vec2 tor) -> float
{
    float po = 1.0;

    float Ra2 = tor.x * tor.x;
    float ra2 = tor.y * tor.y;

    float m = glm::dot(ro, ro);
    float n = glm::dot(ro, rd);

    // bounding sphere
    {
    	float h = n * n - m + (tor.x + tor.y) * (tor.x + tor.y);
    	if (h < 0.0f) {
            return -1.0f;
        }
    	//float t = -n-sqrt(h); // could use this to compute intersections from ro+t*rd
    }

	// find quartic equation
    float k  = (m - ra2 - Ra2) / 2.0f;
    float k3 = n;
    float k2 = n * n + Ra2 * rd.z * rd.z + k;
    float k1 = k * n + Ra2 * ro.z * rd.z;
    float k0 = k * k + Ra2 * ro.z * ro.z - Ra2 * ra2;

#if 1
    // prevent |c1| from being too close to zero
    if (std::abs(k3 * (k3 * k3 - k2) + k1) < 0.01) {
        po = -1.0f;
        float tmp = k1; k1 = k3; k3 = tmp;
        k0 = 1.0f / k0;
        k1 = k1 * k0;
        k2 = k2 * k0;
        k3 = k3 * k0;
    }
#endif

    float c2 = 2.0f * k2 - 3.0f * k3 * k3;
    float c1 = k3 * (k3 * k3 - k2) + k1;
    float c0 = k3 * (k3 * (-3.0f * k3 * k3 + 4.0f * k2) - 8.0f * k1) + 4.0f * k0;

    c2 /= 3.0f;
    c1 *= 2.0f;
    c0 /= 3.0f;

    float Q = c2 * c2 + c0;
    float R = 3.0f * c0 * c2 - c2 * c2 * c2 - c1 * c1;


    float h = R * R - Q * Q * Q;
    float z = 0.0f;
    if (h < 0.0f) {
    	// 4 intersections
        float sQ = std::sqrt(Q);
        z = 2.0f * sQ * std::cos(
            std::acos(R / (sQ * Q)) / 3.0f
        );
    } else {
        // 2 intersections
        float sQ = std::pow(
            std::sqrt(h) + std::abs(R),
            1.0f / 3.0f
        );
        z = sign(R) * std::abs(sQ + Q / sQ);
    }
    z = c2 - z;

    float d1 = z     - 3.0f * c2;
    float d2 = z * z - 3.0f * c0;
    if (std::abs(d1) < 1.0e-4) {
        if (d2 < 0.0f) {
            return -1.0f;
        }
        d2 = std::sqrt(d2);
    } else {
        if (d1 < 0.0f) {
            return -1.0f;
        }
        d1 = std::sqrt(d1 / 2.0f);
        d2 = c1 / d1;
    }

    //----------------------------------

    float result = 1e20f;

    h = d1 * d1 - z + d2;
    if (h > 0.0f) {
        h = std::sqrt(h);
        float t1 = -d1 - h - k3; t1 = (po < 0.0f) ? 2.0f / t1 : t1;
        float t2 = -d1 + h - k3; t2 = (po < 0.0f) ? 2.0f / t2 : t2;
        if (t1 > 0.0f) result = t1;
        if (t2 > 0.0f) result = std::min(result, t2);
    }

    h = d1 * d1 - z - d2;
    if (h > 0.0f) {
        h = std::sqrt(h);
        float t1 = d1 - h - k3; t1 = (po < 0.0f) ? 2.0f / t1 : t1;
        float t2 = d1 + h - k3; t2 = (po < 0.0f) ? 2.0f / t2 : t2;
        if (t1 > 0.0f) result = std::min(result, t1);
        if (t2 > 0.0f) result = std::min(result, t2);
    }

    return result;
}

auto ray_torus_intersection(const glm::dvec3 ro, const glm::dvec3 rd, const glm::dvec2 tor) -> double
{
    double po = 1.0;

    double Ra2 = tor.x * tor.x;
    double ra2 = tor.y * tor.y;

    double m = glm::dot(ro, ro);
    double n = glm::dot(ro, rd);

    // bounding sphere
    {
    	double h = n * n - m + (tor.x + tor.y) * (tor.x + tor.y);
    	if (h < 0.0) {
            return -1.0;
        }
    	//float t = -n-sqrt(h); // could use this to compute intersections from ro+t*rd
    }

	// find quartic equation
    double k  = (m - ra2 - Ra2) / 2.0;
    double k3 = n;
    double k2 = n * n + Ra2 * rd.z * rd.z + k;
    double k1 = k * n + Ra2 * ro.z * rd.z;
    double k0 = k * k + Ra2 * ro.z * ro.z - Ra2 * ra2;

#if 1
    // prevent |c1| from being too close to zero
    if (std::abs(k3 * (k3 * k3 - k2) + k1) < 0.001) {
        po = -1.0;
        double tmp = k1; k1 = k3; k3 = tmp;
        k0 = 1.0 / k0;
        k1 = k1 * k0;
        k2 = k2 * k0;
        k3 = k3 * k0;
    }
#endif

    double c2 = 2.0 * k2 - 3.0 * k3 * k3;
    double c1 = k3 * (k3 * k3 - k2) + k1;
    double c0 = k3 * (k3 * (-3.0 * k3 * k3 + 4.0 * k2) - 8.0 * k1) + 4.0 * k0;

    c2 /= 3.0;
    c1 *= 2.0;
    c0 /= 3.0;

    double Q = c2 * c2 + c0;
    double R = 3.0 * c0 * c2 - c2 * c2 * c2 - c1 * c1;

    double h = R * R - Q * Q * Q;
    double z = 0.0;
    if (h < 0.0) {
    	// 4 intersections
        double sQ = std::sqrt(Q);
        z = 2.0 * sQ * std::cos(
            std::acos(R / (sQ * Q)) / 3.0
        );
    } else {
        // 2 intersections
        double sQ = std::pow(
            std::sqrt(h) + std::abs(R),
            1.0 / 3.0
        );
        z = sign(R) * std::abs(sQ + Q / sQ);
    }
    z = c2 - z;

    double d1 = z     - 3.0 * c2;
    double d2 = z * z - 3.0 * c0;
    if (std::abs(d1) < 1.0e-6) {
        if (d2 < 0.0) {
            return -1.0;
        }
        d2 = std::sqrt(d2);
    } else {
        if (d1 < 0.0) {
            return -1.0;
        }
        d1 = std::sqrt(d1 / 2.0);
        d2 = c1 / d1;
    }

    //----------------------------------

    double result = 1e20;

    h = d1 * d1 - z + d2;
    if (h > 0.0) {
        h = std::sqrt(h);
        double t1 = -d1 - h - k3; t1 = (po < 0.0) ? 2.0 / t1 : t1;
        double t2 = -d1 + h - k3; t2 = (po < 0.0) ? 2.0 / t2 : t2;
        if (t1 > 0.0) result = t1;
        if (t2 > 0.0) result = std::min(result, t2);
    }

    h = d1 * d1 - z - d2;
    if (h > 0.0) {
        h = std::sqrt(h);
        double t1 = d1 - h - k3; t1 = (po < 0.0) ? 2.0 / t1 : t1;
        double t2 = d1 + h - k3; t2 = (po < 0.0) ? 2.0 / t2 : t2;
        if (t1 > 0.0) result = std::min(result, t1);
        if (t2 > 0.0) result = std::min(result, t2);
    }

    return result;
}

} // anonymous namespace

void Primitive_renderer::add_torus(
    const erhe::scene::Transform& world_from_node,
    const glm::vec4&              major_color,
    const glm::vec4&              minor_color,
    const float                   major_thickness,
    const float                   minor_thickness,
    const float                   major_radius,
    const float                   minor_radius,
    const glm::vec3&              camera_position_in_world,
    const int                     major_step_count,
    const int                     minor_step_count,
    const float                   epsilon
)
{
    constexpr glm::vec3 axis_z{0.0f, 0.0f, 1.0f};
    constexpr int       k = 8;

    const glm::mat4  m                       = world_from_node.get_matrix();
    const glm::mat4  node_from_world         = world_from_node.get_inverse_matrix();
    const glm::vec3  camera_position_in_node = glm::vec3{node_from_world * glm::vec4{camera_position_in_world, 1.0f}};
    const glm::dvec3 camera                  = glm::dvec3{camera_position_in_node};
    const glm::dvec2 tor                     = glm::dvec2{major_radius, minor_radius};

    // Occlusion test against the torus itself: the torus is not convex, so
    // facing the camera is necessary but not sufficient for visibility (the
    // near tube can hide the far side of the hole). The ray origin is offset
    // from the surface point toward the camera, and hits within epsilon of
    // the point are treated as self-hits.
    const auto is_unoccluded = [&](const glm::vec3& p) -> bool {
        const glm::dvec3 ray_dir = glm::normalize(camera - glm::dvec3{p});
        const glm::dvec3 ray_org = glm::dvec3{p} + 1.5 * static_cast<double>(epsilon) * ray_dir;
        const double     t       = ray_torus_intersection(ray_org, ray_dir, tor);
        if ((t == -1.0) || (t > 1e10)) {
            return true;
        }
        const glm::dvec3 hit = ray_org + t * ray_dir;
        const double     d   = glm::distance(hit, glm::dvec3{p});
        return (d < static_cast<double>(epsilon)) || (d > glm::distance(glm::dvec3{p}, camera));
    };

    // Structural wireframe: tube cross-section circles and rings around the
    // axis; visible parts major style, hidden parts minor style. Back-facing
    // points are rejected without running the quartic ray test.
    const auto add_wire_segment = [&](const Torus_point& a, const Torus_point& b, const Torus_point& mid) {
        const bool facing  = glm::dot(mid.n, camera_position_in_node - mid.p) > 0.0f;
        const bool visible = facing && is_unoccluded(mid.p);
        set_thickness(visible ? major_thickness : minor_thickness);
        add_lines(m, visible ? major_color : minor_color, { { a.p, b.p } });
    };

    for (int i = 0; i < major_step_count; ++i) {
        const float rel_major = static_cast<float>(i) / static_cast<float>(major_step_count);
        for (int j = 0; j < minor_step_count * k; ++j) {
            const float rel_minor      = static_cast<float>(j    ) / static_cast<float>(minor_step_count * k);
            const float rel_minor_next = static_cast<float>(j + 1) / static_cast<float>(minor_step_count * k);
            add_wire_segment(
                torus_point(major_radius, minor_radius, rel_major, rel_minor),
                torus_point(major_radius, minor_radius, rel_major, rel_minor_next),
                torus_point(major_radius, minor_radius, rel_major, 0.5f * (rel_minor + rel_minor_next))
            );
        }
    }
    for (int j = 0; j < minor_step_count; ++j) {
        const float rel_minor = static_cast<float>(j) / static_cast<float>(minor_step_count);
        for (int i = 0; i < major_step_count * k; ++i) {
            const float rel_major      = static_cast<float>(i    ) / static_cast<float>(major_step_count * k);
            const float rel_major_next = static_cast<float>(i + 1) / static_cast<float>(major_step_count * k);
            add_wire_segment(
                torus_point(major_radius, minor_radius, rel_major,      rel_minor),
                torus_point(major_radius, minor_radius, rel_major_next, rel_minor),
                torus_point(major_radius, minor_radius, 0.5f * (rel_major + rel_major_next), rel_minor)
            );
        }
    }

    // True silhouette contour. torus_point() puts the major circle in the XY
    // plane with the tube axis along Z, so on the cross-section circle at
    // major angle theta the surface normal is
    //   n(phi) = cos(phi) radial(theta) + sin(phi) Z
    // and the horizon condition n . (camera - q) = 0 reduces to
    //   (radial . w) cos(phi) + (Z . w) sin(phi) = minor_radius
    // with w = camera - ring_center(theta) - the same threshold form used by
    // add_sphere() / add_cone() / add_capsule() - giving up to two exact
    // silhouette points per cross section. The two solution branches are
    // traced into polylines around the major circle; where the branch pair
    // appears or disappears (grazing cross sections) the branch ends are
    // joined, exact up to one theta step. Self-occluded silhouette parts are
    // drawn in minor style via the same ray test.
    const int   silhouette_step_count = major_step_count * k;
    const float chord_threshold       = 2.0f * glm::two_pi<float>() * (major_radius + minor_radius) / static_cast<float>(silhouette_step_count);

    class Silhouette_sample
    {
    public:
        bool      exists{false};
        glm::vec3 q0    {0.0f};
        glm::vec3 q1    {0.0f};
        bool      vis0  {false};
        bool      vis1  {false};
    };

    const auto solve_at = [&](const float theta) -> Silhouette_sample {
        const glm::vec3 radial{std::cos(theta), std::sin(theta), 0.0f};
        const glm::vec3 ring_center = major_radius * radial;
        const glm::vec3 w           = camera_position_in_node - ring_center;
        const float     a           = glm::dot(radial, w);
        const float     b           = w.z;
        const float     reach       = std::sqrt((a * a) + (b * b));
        Silhouette_sample sample;
        sample.exists = reach > minor_radius;
        if (sample.exists) {
            const float phi_mid = std::atan2(b, a);
            const float phi_cut = std::acos(glm::clamp(minor_radius / reach, -1.0f, 1.0f));
            const auto point_at = [&](const float phi) {
                return ring_center + minor_radius * ((std::cos(phi) * radial) + (std::sin(phi) * axis_z));
            };
            sample.q0   = point_at(phi_mid - phi_cut);
            sample.q1   = point_at(phi_mid + phi_cut);
            sample.vis0 = is_unoccluded(sample.q0);
            sample.vis1 = is_unoccluded(sample.q1);
        }
        return sample;
    };

    const auto add_silhouette_segment = [&](const glm::vec3& p0, const glm::vec3& p1, const bool visible) {
        set_thickness(visible ? major_thickness : minor_thickness);
        add_lines(m, visible ? major_color : minor_color, { { p0, p1 } });
    };

    const auto emit_between = [&](const Silhouette_sample& s0, const Silhouette_sample& s1) {
        if (s0.exists && s1.exists) {
            add_silhouette_segment(s0.q0, s1.q0, s0.vis0 && s1.vis0);
            add_silhouette_segment(s0.q1, s1.q1, s0.vis1 && s1.vis1);
        } else if (s0.exists && !s1.exists) {
            add_silhouette_segment(s0.q0, s0.q1, s0.vis0 && s0.vis1); // branches fold together
        } else if (!s0.exists && s1.exists) {
            add_silhouette_segment(s1.q0, s1.q1, s1.vis0 && s1.vis1);
        }
    };

    Silhouette_sample prev = solve_at(0.0f);
    for (int i = 1; i <= silhouette_step_count; ++i) {
        const float theta0 = glm::two_pi<float>() * static_cast<float>(i - 1) / static_cast<float>(silhouette_step_count);
        const float theta1 = glm::two_pi<float>() * static_cast<float>(i    ) / static_cast<float>(silhouette_step_count);
        const Silhouette_sample cur = solve_at(theta1);

        // Near the folds (grazing cross sections, reach -> minor_radius) the
        // curve moves fast in phi for small steps in theta (phi_cut behaves
        // like a square root), so a uniform theta sampling leaves long chords
        // exactly where the contour turns around. Refine those intervals.
        const bool refine =
            (prev.exists != cur.exists) ||
            (
                prev.exists && cur.exists &&
                (
                    (glm::distance(prev.q0, cur.q0) > chord_threshold) ||
                    (glm::distance(prev.q1, cur.q1) > chord_threshold)
                )
            );
        if (refine) {
            constexpr int substep_count = 32;
            Silhouette_sample a = prev;
            for (int s = 1; s <= substep_count; ++s) {
                const Silhouette_sample b = (s == substep_count)
                    ? cur
                    : solve_at(theta0 + ((theta1 - theta0) * static_cast<float>(s) / static_cast<float>(substep_count)));
                emit_between(a, b);
                a = b;
            }
        } else {
            emit_between(prev, cur);
        }
        prev = cur;
    }
}
#pragma endregion add

} // namespace erhe::renderer
