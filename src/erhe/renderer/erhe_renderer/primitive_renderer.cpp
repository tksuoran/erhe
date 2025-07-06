#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"

#include "erhe_scene/camera.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/sphere.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtx/norm.hpp>

namespace erhe::renderer {

// Note that this relies on bucket being stable, as in etl::vector<> when elements are never removed.
Primitive_renderer::Primitive_renderer(Debug_renderer& debug_renderer, Debug_renderer_bucket& bucket)
    : m_debug_renderer    {&debug_renderer}
    , m_bucket            {&bucket}
    , m_line_vertex_stride{m_debug_renderer->get_program_interface().line_vertex_struct->size_bytes()}
{
}

Primitive_renderer::Primitive_renderer(Primitive_renderer&& old)
    : m_debug_renderer              {std::exchange(old.m_debug_renderer, nullptr)}
    , m_bucket                      {std::exchange(old.m_bucket,         nullptr)}
    , m_line_vertex_stride          {m_debug_renderer->get_program_interface().line_vertex_struct->size_bytes()}
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

auto Primitive_renderer::operator=(Primitive_renderer&& old) -> Primitive_renderer&
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

void Primitive_renderer::make_lines(std::size_t line_count)
{
    std::size_t byte_count     = line_count * 2 * m_line_vertex_stride;
    m_last_allocate_word_count = byte_count / sizeof(float);
    m_last_allocate_gpu_data   = m_bucket->make_draw(byte_count, line_count);

    if (m_last_allocate_gpu_data.size_bytes() < byte_count) {
        m_last_allocate_gpu_float_data = nullptr;
        m_last_allocate_word_offset    = 0;
    } else {
        std::byte* start = m_last_allocate_gpu_data.data();
        m_last_allocate_gpu_float_data = reinterpret_cast<float*>(start);
        m_last_allocate_word_offset    = 0;
    }
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

void Primitive_renderer::add_plane(const glm::vec4& color, const glm::vec4& plane)
{
    glm::vec3 center = erhe::math::get_point_on_plane(plane);
    glm::vec3 normal{plane};
    glm::vec3 tangent;
    glm::vec3 bitangent;
    erhe::math::get_plane_basis(normal, tangent, bitangent);
    glm::vec4 zero{0.0f, 0.0f, 0.0f, 0.0f};

    make_lines(9);
    put(center,          m_half_line_thickness * 5.0f, color);
    put(center + normal, 0.0f, zero);

    put(center - tangent,        m_half_line_thickness, zero);
    put(center,           0.7f * m_half_line_thickness, color);
    put(center,           0.7f * m_half_line_thickness, color);
    put(center + tangent,        m_half_line_thickness, zero);

    put(center - bitangent,        m_half_line_thickness, zero);
    put(center,             0.7f * m_half_line_thickness, color);
    put(center,             0.7f * m_half_line_thickness, color);
    put(center + bitangent,        m_half_line_thickness, zero);

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
    const glm::vec4&                    edge_color,
    const glm::vec4&                    great_circle_color,
    const float                         edge_thickness,
    const float                         great_circle_thickness,
    const glm::vec3&                    local_center,
    const float                         local_radius,
    const erhe::scene::Transform* const camera_world_from_node,
    const int                           step_count
)
{
    erhe::math::Sphere sphere = erhe::math::transform(
        world_from_local.get_matrix(),
        erhe::math::Sphere{
            .center = local_center,
            .radius = local_radius
        }
    );
    const float     radius = sphere.radius;
    const glm::vec3 center = sphere.center;
    const glm::vec3 axis_x{radius, 0.0f, 0.0f};
    const glm::vec3 axis_y{0.0f, radius, 0.0f};
    const glm::vec3 axis_z{0.0f, 0.0f, radius};

    set_thickness(great_circle_thickness);
    for (int i = 0; i < step_count; ++i) {
        const float t0 = glm::two_pi<float>() * static_cast<float>(i    ) / static_cast<float>(step_count);
        const float t1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(step_count);
        add_lines(
            great_circle_color,
            {
                {
                    center + std::cos(t0) * axis_x + std::sin(t0) * axis_y,
                    center + std::cos(t1) * axis_x + std::sin(t1) * axis_y
                },
                {
                    center + std::cos(t0) * axis_y + std::sin(t0) * axis_z,
                    center + std::cos(t1) * axis_y + std::sin(t1) * axis_z
                },
                {
                    center + std::cos(t0) * axis_x + std::sin(t0) * axis_z,
                    center + std::cos(t1) * axis_x + std::sin(t1) * axis_z
                }
            }
        );
        //add_lines(
        //    0xffff0000,
        //    {
        //        {
        //            center + std::cos(t0) * axis_x + std::sin(t0) * axis_y,
        //            center + std::cos(t1) * axis_x + std::sin(t1) * axis_y
        //        }
        //    }
        //);
        //add_lines(
        //    0xff0000ff,
        //    {
        //        {
        //            center + std::cos(t0) * axis_y + std::sin(t0) * axis_z,
        //            center + std::cos(t1) * axis_y + std::sin(t1) * axis_z
        //        }
        //    }
        //);
        //add_lines(
        //    0xff00ff00,
        //    {
        //        {
        //            center + std::cos(t0) * axis_x + std::sin(t0) * axis_z,
        //            center + std::cos(t1) * axis_x + std::sin(t1) * axis_z
        //        }
        //    }
        //);
    }

    if (camera_world_from_node == nullptr) {
        return;
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

    const glm::vec3 camera_position                 = glm::vec3{camera_world_from_node->get_matrix() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    const glm::vec3 from_camera_to_sphere           = center - camera_position;
    const glm::vec3 from_sphere_to_camera           = camera_position - center;
    const glm::vec3 from_camera_to_sphere_direction = glm::normalize(from_camera_to_sphere);
    const glm::vec3 from_sphere_to_camera_direction = glm::normalize(from_sphere_to_camera);

    const float r2 = radius * radius;
    const float d2 = glm::length2(from_camera_to_sphere);
    const float d  = std::sqrt(d2);
    const float b2 = d2 - r2;
    const float b  = std::sqrt(b2);
    const float h  = radius * b / d;
    const float h2 = h * h;
    const float p  = std::sqrt(r2 - h2);

    const glm::vec3 P              = center + p * from_sphere_to_camera_direction;
    const glm::vec3 up0_direction  = glm::vec3{camera_world_from_node->get_matrix() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const glm::vec3 side_direction = erhe::math::safe_normalize_cross<float>(from_camera_to_sphere_direction, up0_direction);
    const glm::vec3 up_direction   = erhe::math::safe_normalize_cross<float>(side_direction, from_camera_to_sphere_direction);
    const glm::vec3 axis_a         = h * side_direction;
    const glm::vec3 axis_b         = h * up_direction;

    set_thickness(edge_thickness);
    for (int i = 0; i < step_count; ++i) {
        const float t0 = glm::two_pi<float>() * static_cast<float>(i    ) / static_cast<float>(step_count);
        const float t1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(step_count);
        add_lines(
            //0xffffffff,
            edge_color,
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
    const glm::vec3 top_center              = bottom_center + glm::vec3{0.0f, height, 0.0f};
    const glm::vec3 camera_position_in_node = glm::vec4{node_from_world * glm::vec4{camera_position_in_world, 1.0f}};

    set_thickness(major_thickness);

    class Cone_edge
    {
    public:
        Cone_edge(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& n, const glm::vec3& t, const glm::vec3& b, const float phi, const float n_dot_v)
        : p0     {p0}
        , p1     {p1}
        , n      {n}
        , t      {t}
        , b      {b}
        , phi    {phi}
        , n_dot_v{n_dot_v}
        {
        }

        glm::vec3  p0;
        glm::vec3  p1;
        glm::vec3  n;
        glm::vec3  t;
        glm::vec3  b;
        float      phi;
        float      n_dot_v;
    };

    std::vector<Cone_edge> cone_edges;
    for (int i = 0; i < side_count; ++i) {
        const float phi = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(side_count);
        const glm::vec3 sin_phi_z = std::cos(phi) * axis_x;
        const glm::vec3 cos_phi_x = std::sin(phi) * axis_z;

        const glm::vec3 p0       {bottom_center + bottom_radius * cos_phi_x + bottom_radius * sin_phi_z};
        const glm::vec3 p1       {top_center    + top_radius    * cos_phi_x + top_radius    * sin_phi_z};
        const glm::vec3 mid_point{0.5f * (p0 + p1)};

        const glm::vec3 B = glm::normalize(p1 - p0); // generatrix
        const glm::vec3 T{
            static_cast<float>(std::cos(phi + glm::half_pi<float>())),
            0.0f,
            static_cast<float>(std::sin(phi + glm::half_pi<float>()))
        };
        const glm::vec3 N       = erhe::math::safe_normalize_cross<float>(B, T);
        const glm::vec3 v       = glm::normalize(camera_position_in_node - mid_point);
        const float     n_dot_v = dot(N, v);

        cone_edges.emplace_back(p0, p1, N, T, B, phi, n_dot_v);
    }

    std::vector<Cone_edge> sign_flip_edges;

    const glm::vec3 bottom_v       = glm::normalize(camera_position_in_node -bottom_center);
    const float     bottom_n_dot_v = glm::dot(bottom_normal, bottom_v);
    const bool      bottom_visible = bottom_n_dot_v >= 0.0f;
    const glm::vec3 top_v          = glm::normalize(camera_position_in_node - top_center);
    const float     top_n_dot_v    = glm::dot(top_normal, top_v);
    const bool      top_visible    = top_n_dot_v >= 0.0f;

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

    for (size_t i = 0; i < cone_edges.size(); ++i) {
        const std::size_t next_i      = (i + 1) % cone_edges.size();
        const auto&       edge        = cone_edges[i];
        const auto&       next_edge   = cone_edges[next_i];
        const float       avg_n_dot_v = 0.5f * edge.n_dot_v + 0.5f * next_edge.n_dot_v;
        if (sign(edge.n_dot_v) != sign(next_edge.n_dot_v)) {
            if (std::abs(edge.n_dot_v) < std::abs(next_edge.n_dot_v)) {
                sign_flip_edges.push_back(edge);
            } else {
                sign_flip_edges.push_back(next_edge);
            }
        }
        if (bottom_radius > 0.0f) {
            add_lines(
                m,
                bottom_visible || (avg_n_dot_v > 0.0) ? major_color : minor_color,
                { { edge.p0, next_edge.p0 } }
            );
        }

        if (top_radius > 0.0f) {
            add_lines(
                m,
                top_visible || (avg_n_dot_v > 0.0) ? major_color : minor_color,
                { { edge.p1, next_edge.p1 } }
            );
        }
    }

    for (auto& edge : sign_flip_edges) {
        add_lines(m, major_color, { { edge.p0, edge.p1 } } );
    }
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
    const float                   major_radius,
    const float                   minor_radius,
    const glm::vec3&              camera_position_in_world,
    const int                     major_step_count,
    const int                     minor_step_count,
    const float                   epsilon,
    const int                     debug_major,
    const int                     debug_minor
)
{
    static_cast<void>(major_color);
    static_cast<void>(minor_color);
    static_cast<void>(debug_major);
    static_cast<void>(debug_minor);
    constexpr glm::vec3 axis_x{1.0f, 0.0f, 0.0f};
    constexpr glm::vec3 axis_y{0.0f, 1.0f, 0.0f};
    constexpr glm::vec3 axis_z{0.0f, 0.0f, 1.0f};
    const     glm::mat4 m                       = world_from_node.get_matrix();
    const     glm::mat4 node_from_world         = world_from_node.get_inverse_matrix();
    const     glm::vec3 camera_position_in_node = glm::vec4{node_from_world * glm::vec4{camera_position_in_world, 1.0f}};
    const     glm::vec2 tor                     = glm::vec2{major_radius, minor_radius};
    constexpr int  k = 8;
    set_thickness(major_thickness);
    for (int i = 0; i < major_step_count; ++i) {
        const float rel_major = static_cast<float>(i) / static_cast<float>(major_step_count);
        for (int j = 0; j < minor_step_count * k; ++j) {
            const float       rel_minor      = static_cast<float>(j    ) / static_cast<float>(minor_step_count * k);
            const float       rel_minor_next = static_cast<float>(j + 1) / static_cast<float>(minor_step_count * k);
            const Torus_point a       = torus_point(major_radius, minor_radius, rel_major, rel_minor);
            const Torus_point b       = torus_point(major_radius, minor_radius, rel_major, rel_minor_next);
            const Torus_point c       = torus_point(major_radius, minor_radius, rel_major, 0.5f * (rel_minor + rel_minor_next));
            const glm::dvec3  ray_dir = glm::normalize(glm::dvec3{camera_position_in_node} - glm::dvec3{c.p});
            const glm::dvec3  ray_org = glm::dvec3{c.p} + 1.5 * epsilon * ray_dir;
            const double      t       = ray_torus_intersection(glm::dvec3{ray_org}, glm::dvec3{ray_dir}, glm::dvec2{tor});
            const glm::dvec3  P0      = ray_org + t * ray_dir;
            const float       d       = static_cast<float>(glm::distance(P0, glm::dvec3{c.p}));
            const bool        visible = (t == -1.0f) || (t > 1e10) || (d < epsilon) || (d > glm::distance(c.p, camera_position_in_node));

            add_lines(
                m,
                visible ? major_color : minor_color,
                { { a.p, b.p } }
            );
#if 0
            if ((i == debug_major) && (j == debug_minor))
            {
                add_lines( // blue: normal
                    m,
                    vec4{0.0f, 0.0f, 1.0f, 0.5f},
                    { { c.p, c.p + 0.1f * c.n } }
                );
                add_lines( // cyan: point to camera
                    m,
                    vec4{0.0f, 1.0f, 1.0f, 0.5f},
                    { { camera_position_in_node, c.p } }
                );
                add_lines( // green: center to point
                    m,
                    vec4{0.0f, 1.0f, 0.0f, 1.0f},
                    { { vec3{0.0f, 0.0f, 0.0f}, c.p } }
                );
                if ((t > 0.0f) && (t < 1e10)) // && (d > epsilon))
                {
                    add_lines( // red: point to intersection
                        m,
                        vec4{1.0f, 0.0f, 0.0f, 1.0f},
                        { { ray_org, P0 } }
                    );
                }
            }
#endif
        }
    }

    for (int j = 0; j < minor_step_count; ++j) {
        const float rel_minor = static_cast<float>(j) / static_cast<float>(minor_step_count);
        for (int i = 0; i < major_step_count * k; ++i) {
            const float       rel_major      = static_cast<float>(i    ) / static_cast<float>(major_step_count * k);
            const float       rel_major_next = static_cast<float>(i + 1) / static_cast<float>(major_step_count * k);
            const Torus_point a = torus_point(major_radius, minor_radius, rel_major,      rel_minor);
            const Torus_point b = torus_point(major_radius, minor_radius, rel_major_next, rel_minor);
            const Torus_point c = torus_point(major_radius, minor_radius, 0.5f * (rel_major + rel_major_next), rel_minor);
            const glm::dvec3  ray_dir = glm::normalize(glm::dvec3{camera_position_in_node} - glm::dvec3{c.p});
            const glm::dvec3  ray_org = glm::dvec3{c.p} + 1.5 * epsilon * ray_dir;
            const double      t       = ray_torus_intersection(glm::dvec3{ray_org}, glm::dvec3{ray_dir}, glm::dvec2{tor});
            const glm::dvec3  P0      = ray_org + t * ray_dir;
            const float       d       = static_cast<float>(glm::distance(P0, glm::dvec3{c.p}));
            const bool        visible = (t == -1.0f) || (t > 1e10) || (d < epsilon) || (d > glm::distance(c.p, camera_position_in_node));

            add_lines(
                m,
                visible ? major_color : minor_color,
                { { a.p, b.p } }
            );
        }
    }
}
#pragma endregion add

} // namespace erhe::renderer
