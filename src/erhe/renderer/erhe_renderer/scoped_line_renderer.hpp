#pragma once

#include "erhe_renderer/buffer_writer.hpp"
//#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/instance.hpp"

namespace erhe::graphics {
    class Buffer;
}
namespace erhe::scene {
    class Camera;
    class Transform;
}

namespace erhe::renderer {

class Line_renderer;
class Line_renderer_bucket;

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

class Scoped_line_renderer
{
public:
    Scoped_line_renderer(Line_renderer& line_renderer, Line_renderer_bucket& bucket, bool indirect = false);
    ~Scoped_line_renderer();

#pragma region Draw API
    void set_line_color(float r, float g, float b, float a);
    void set_line_color(const glm::vec3& color);
    void set_line_color(const glm::vec4& color);
    void set_thickness (float thickness);

    void add_lines(const std::initializer_list<Line> lines);
    void add_lines(const glm::mat4& transform, const std::initializer_list<Line> lines);
    void add_lines(const glm::mat4& transform, const std::initializer_list<Line4> lines);

    void add_line(const glm::vec4& color0, float width0, glm::vec3 p0, const glm::vec4& color1, float width1, glm::vec3 p2);

    void add_lines(const glm::mat4 transform, const glm::vec4& color, const std::initializer_list<Line> lines)
    {
        set_line_color(color);
        add_lines(transform, lines);
    }

    void add_lines(const glm::vec4& color, const std::initializer_list<Line> lines)
    {
        set_line_color(color);
        add_lines(lines);
    }

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

    void add_sphere(
        const erhe::scene::Transform& transform,
        const glm::vec4&              edge_color,
        const glm::vec4&              great_circle_color,
        float                         edge_thickness,
        float                         great_circle_thickness,
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

    void add_torus(
        const erhe::scene::Transform& world_from_node,
        const glm::vec4&              major_color,
        const glm::vec4&              minor_color,
        float                         major_thickness,
        float                         major_radius,
        float                         minor_radius,
        const glm::vec3&              camera_position_in_world,
        int                           major_step_count,
        int                           minor_step_count,
        float                         epsilon,
        int                           debug_major,
        int                           debug_minor
    );
#pragma endregion Draw API

private:
    void allocate(std::size_t line_count);
    inline void put(
        const glm::vec3& point,
        float            thickness,
        const glm::vec4& color
    )
    {
        ERHE_VERIFY(m_word_offset + 8 <= m_word_count);
        m_gpu_float_data[m_word_offset++] = point.x;
        m_gpu_float_data[m_word_offset++] = point.y;
        m_gpu_float_data[m_word_offset++] = point.z;
        m_gpu_float_data[m_word_offset++] = thickness;
        m_gpu_float_data[m_word_offset++] = color.r;
        m_gpu_float_data[m_word_offset++] = color.g;
        m_gpu_float_data[m_word_offset++] = color.b;
        m_gpu_float_data[m_word_offset++] = color.a;
        ERHE_VERIFY(m_word_offset <= m_word_count);
    }

    Line_renderer&         m_line_renderer;
    Line_renderer_bucket&  m_bucket;
    bool                   m_indirect;
    std::size_t            m_first_line{0};
    //std::size_t            m_line_count{0};
    std::size_t            m_line_vertex_stride;

    std::span<std::byte>   m_gpu_data;
    std::byte*             m_start         {nullptr};
    std::size_t            m_byte_count    {0};
    std::size_t            m_word_count    {0};
    float*                 m_gpu_float_data{nullptr};
    std::size_t            m_word_offset   {0}; // relative to m_gpu_data

    // Current state
    std::vector<std::byte> m_indirect_buffer;
    glm::vec4              m_line_color    {1.0f, 1.0f, 1.0f, 1.0f};
    float                  m_line_thickness{1.0f};
};

} // namespace erhe::renderer
