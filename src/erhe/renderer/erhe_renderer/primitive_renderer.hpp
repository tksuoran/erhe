#pragma once

#include "erhe_renderer/debug_renderer.hpp"

#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace erhe::graphics {
    class Buffer;
}
namespace erhe::scene {
    class Camera;
    class Transform;
}

namespace erhe::renderer {

class Debug_renderer;
class Debug_renderer_bucket;

class Line
{
public:
    glm::vec3 p0;
    glm::vec3 p1;
};

class Line4
{
public:
    glm::vec4 p0;
    glm::vec4 p1;
};

class Primitive_renderer
{
public:
    Primitive_renderer(Debug_renderer& debug_renderer, Debug_renderer_bucket& bucket);
    Primitive_renderer(Primitive_renderer& other) = delete;
    auto operator=    (Primitive_renderer& other) -> Primitive_renderer& = delete;
    Primitive_renderer(Primitive_renderer&& old) noexcept;
    auto operator=    (Primitive_renderer&& old) noexcept -> Primitive_renderer&;

#pragma region Draw API
    void set_line_color(float r, float g, float b, float a);
    void set_line_color(const glm::vec3& color);
    void set_line_color(const glm::vec4& color);
    void set_thickness (float thickness);

    void add_lines(const std::vector<Line>& lines);
    void add_lines(const std::initializer_list<Line> lines);

    void add_lines(const glm::mat4& transform, const std::vector<Line>& lines);
    void add_lines(const glm::mat4& transform, const std::initializer_list<Line> lines);
    void add_lines(const glm::mat4& transform, const std::initializer_list<Line4> lines);
    void add_lines(const glm::mat4& transform, std::span<Line> lines);

    void add_line(const glm::vec4& color0, float width0, glm::vec3 p0, const glm::vec4& color1, float width1, glm::vec3 p2);

    void add_lines(const glm::mat4 transform, const glm::vec4& color, const std::initializer_list<Line> lines)
    {
        set_line_color(color);
        add_lines(transform, lines);
    }
    void add_lines(const glm::mat4 transform, const glm::vec4& color, const std::vector<Line>& lines)
    {
        set_line_color(color);
        add_lines(transform, lines);
    }
    void add_lines(const glm::mat4 transform, const glm::vec4& color, std::span<Line> lines)
    {
        set_line_color(color);
        add_lines(transform, lines);
    }

    void add_lines(const glm::vec4& color, const std::initializer_list<Line> lines)
    {
        set_line_color(color);
        add_lines(lines);
    }

    // Filled triangles. These use the same per-vertex layout as lines
    // (position + color; the line-width slot is unused) and are rendered by
    // the triangle-primitive bucket (Debug_renderer_shader_key tier=simple,
    // primitive_type=triangle). A color alpha < 1 gives a translucent fill
    // through the premultiplied-over visible blend state. positions is an
    // index buffer of triangle list indices into positions.
    void add_triangle (const glm::mat4& transform, const glm::vec4& color, const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2);
    void add_triangles(const glm::mat4& transform, const glm::vec4& color, std::span<const glm::vec3> positions, std::span<const uint32_t> indices);

    void add_plane(const glm::vec4& color, const glm::vec4& plane);

    // Draws the orientation indicator add_plane() uses - a center marker, a
    // stub along the normal, and a tangent/bitangent cross - at an explicit
    // point (e.g. the center of a bounded face) rather than at the plane's
    // closest point to the world origin. The bounding quad is not drawn.
    void add_plane_indicator(const glm::vec4& color, const glm::vec3& center, const glm::vec3& normal);

    void add_cube(
        const glm::mat4& transform,
        const glm::vec4& color,
        const glm::vec3& min_corner,
        const glm::vec3& max_corner,
        bool             z_cross = false
    );

    void add_bone(
        const glm::mat4& transform,
        const glm::vec4& color,
        const glm::vec3& start,
        const glm::vec3& end
    );

    // Sphere with great circles in the world XY, YZ and XZ planes and, when
    // a camera transform is given, the exact view silhouette circle. The
    // silhouette and the visible great circle arcs use major style, hidden
    // arcs minor style; the visible/hidden split is exact, so visible arc
    // endpoints lie on the silhouette circle. Without a camera (or with the
    // camera inside the sphere) the great circles are drawn fully in minor
    // style and the silhouette is omitted.
    void add_sphere(
        const erhe::scene::Transform& transform,
        const glm::vec4&              major_color,
        const glm::vec4&              minor_color,
        float                         major_thickness,
        float                         minor_thickness,
        const glm::vec3&              local_center,
        float                         local_radius,
        const erhe::scene::Transform* camera_world_from_node = nullptr,
        int                           step_count = 40
    );

    void add_cone(
        const erhe::scene::Transform& transform,
        const glm::vec4&              major_color,
        const glm::vec4&              minor_color,
        float                         major_thickness,
        float                         minor_thickness,
        const glm::vec3&              center,
        float                         height,
        float                         bottom_radius,
        float                         top_radius,
        const glm::vec3&              camera_position_in_world,
        int                           side_count
    );

    // Tapered capsule on the local Y axis: cap spheres of bottom_radius at
    // bottom_center and top_radius at bottom_center + length * Y, joined by
    // their common tangent cone (the convex hull of the two spheres; equal
    // radii give the classic capsule). Draws the single closed view
    // silhouette in major style and structural lines (junction rings, cap
    // profile arcs, side generatrices, axis) in minor style.
    void add_capsule(
        const erhe::scene::Transform& world_from_node,
        const glm::vec4&              major_color,
        const glm::vec4&              minor_color,
        float                         major_thickness,
        float                         minor_thickness,
        const glm::vec3&              bottom_center,
        float                         length,
        float                         bottom_radius,
        float                         top_radius,
        const glm::vec3&              camera_position_in_world,
        int                           side_count
    );

    // Torus with the major circle in the local XY plane (tube axis along Z).
    // Draws the wireframe circles and the exact silhouette contour (up to
    // two horizon points per tube cross section, traced around the major
    // circle). Visible lines use major style; back-facing or self-occluded
    // lines use minor style. Occlusion is ray tested against the torus;
    // epsilon is the self-hit tolerance for those rays.
    void add_torus(
        const erhe::scene::Transform& world_from_node,
        const glm::vec4&              major_color,
        const glm::vec4&              minor_color,
        float                         major_thickness,
        float                         minor_thickness,
        float                         major_radius,
        float                         minor_radius,
        const glm::vec3&              camera_position_in_world,
        int                           major_step_count,
        int                           minor_step_count,
        float                         epsilon
    );
#pragma endregion Draw API

private:
    void reserve_lines  (std::size_t line_count);
    void make_lines     (std::size_t line_count);
    // Allocate space for primitive_count primitives of vertices_per_primitive
    // vertices each (2 = line, 3 = triangle, 1 = point) and reset the put()
    // write cursor to the start of the allocation.
    void make_primitives(std::size_t primitive_count, std::size_t vertices_per_primitive);

    inline void put(
        const glm::vec3& point_world,
        float            thickness,
        const glm::vec4& color
    )
    {
        if (m_last_allocate_gpu_float_data == nullptr) {
            return;
        }
        ERHE_VERIFY(m_last_allocate_word_offset + 8 <= m_last_allocate_word_count);
        m_last_allocate_gpu_float_data[m_last_allocate_word_offset++] = point_world.x;
        m_last_allocate_gpu_float_data[m_last_allocate_word_offset++] = point_world.y;
        m_last_allocate_gpu_float_data[m_last_allocate_word_offset++] = point_world.z;
        m_last_allocate_gpu_float_data[m_last_allocate_word_offset++] = thickness;
        m_last_allocate_gpu_float_data[m_last_allocate_word_offset++] = color.r;
        m_last_allocate_gpu_float_data[m_last_allocate_word_offset++] = color.g;
        m_last_allocate_gpu_float_data[m_last_allocate_word_offset++] = color.b;
        m_last_allocate_gpu_float_data[m_last_allocate_word_offset++] = color.a;
        ERHE_VERIFY(m_last_allocate_word_offset <= m_last_allocate_word_count);
    }

    Debug_renderer*        m_debug_renderer{nullptr};
    Debug_renderer_bucket* m_bucket{nullptr};

    // offset (in lines), relative to vertex buffer range in Line_renderer
    // initialized in Primitive_renderer constructor
    std::size_t            m_line_vertex_stride;

    std::span<std::byte>   m_last_allocate_gpu_data;
    float*                 m_last_allocate_gpu_float_data{nullptr};
    std::size_t            m_last_allocate_word_offset   {0};
    std::size_t            m_last_allocate_word_count    {0};

    // Current state
    glm::vec4              m_line_color           {1.0f, 1.0f, 1.0f, 1.0f};
    float                  m_half_line_thickness  {0.5f};
};

} // namespace erhe::renderer
