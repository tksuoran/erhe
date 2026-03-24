#pragma once

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/ray.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace erhe::raytrace::test {

// Holds CPU buffers and geometry together so buffers outlive the geometry
class Test_geometry
{
public:
    std::unique_ptr<erhe::buffer::Cpu_buffer> vertex_buffer;
    std::unique_ptr<erhe::buffer::Cpu_buffer> index_buffer;
    std::unique_ptr<erhe::raytrace::IGeometry> geometry;
};

inline auto make_triangle_geometry(
    const std::string_view               name,
    const std::vector<glm::vec3>&        vertices,
    const std::vector<glm::uvec3>&       triangles
) -> Test_geometry
{
    const std::size_t vertex_byte_count   = vertices.size()  * 3 * sizeof(float);
    const std::size_t index_byte_count    = triangles.size() * 3 * sizeof(uint32_t);

    Test_geometry result;
    result.vertex_buffer = std::make_unique<erhe::buffer::Cpu_buffer>("test_vertices", vertex_byte_count);
    result.index_buffer  = std::make_unique<erhe::buffer::Cpu_buffer>("test_indices",  index_byte_count);

    // Write vertex data
    {
        std::span<std::byte> span = result.vertex_buffer->get_span();
        float* data = reinterpret_cast<float*>(span.data());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            data[i * 3 + 0] = vertices[i].x;
            data[i * 3 + 1] = vertices[i].y;
            data[i * 3 + 2] = vertices[i].z;
        }
    }

    // Write index data
    {
        std::span<std::byte> span = result.index_buffer->get_span();
        uint32_t* data = reinterpret_cast<uint32_t*>(span.data());
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            data[i * 3 + 0] = triangles[i].x;
            data[i * 3 + 1] = triangles[i].y;
            data[i * 3 + 2] = triangles[i].z;
        }
    }

    result.geometry = erhe::raytrace::IGeometry::create_unique(
        name,
        erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE
    );
    result.geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX,
        0,
        erhe::dataformat::Format::format_32_vec3_float,
        result.vertex_buffer.get(),
        0,
        3 * sizeof(float),
        vertices.size()
    );
    result.geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX,
        0,
        erhe::dataformat::Format::format_32_vec3_uint,
        result.index_buffer.get(),
        0,
        3 * sizeof(uint32_t),
        triangles.size()
    );
    result.geometry->commit();

    return result;
}

// Single triangle in XY plane: (0,0,0), (1,0,0), (0,1,0)
inline auto make_unit_triangle() -> Test_geometry
{
    return make_triangle_geometry(
        "unit_triangle",
        { {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f} },
        { {0, 1, 2} }
    );
}

// Two triangles forming a unit quad in XY plane
inline auto make_quad() -> Test_geometry
{
    return make_triangle_geometry(
        "quad",
        { {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f} },
        { {0, 1, 2}, {0, 2, 3} }
    );
}

// 12 triangles forming a unit cube centered at origin, side length 1
inline auto make_cube() -> Test_geometry
{
    const float h = 0.5f;
    return make_triangle_geometry(
        "cube",
        {
            // Front face (z = +h)
            {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h},
            // Back face (z = -h)
            { h, -h, -h}, {-h, -h, -h}, {-h,  h, -h}, { h,  h, -h},
            // Top face (y = +h)
            {-h,  h,  h}, { h,  h,  h}, { h,  h, -h}, {-h,  h, -h},
            // Bottom face (y = -h)
            {-h, -h, -h}, { h, -h, -h}, { h, -h,  h}, {-h, -h,  h},
            // Right face (x = +h)
            { h, -h,  h}, { h, -h, -h}, { h,  h, -h}, { h,  h,  h},
            // Left face (x = -h)
            {-h, -h, -h}, {-h, -h,  h}, {-h,  h,  h}, {-h,  h, -h},
        },
        {
            { 0,  1,  2}, { 0,  2,  3},   // front
            { 4,  5,  6}, { 4,  6,  7},   // back
            { 8,  9, 10}, { 8, 10, 11},   // top
            {12, 13, 14}, {12, 14, 15},   // bottom
            {16, 17, 18}, {16, 18, 19},   // right
            {20, 21, 22}, {20, 22, 23},   // left
        }
    );
}

inline auto make_ray(const glm::vec3& origin, const glm::vec3& direction, float t_far = 1000.0f) -> erhe::raytrace::Ray
{
    erhe::raytrace::Ray ray;
    ray.origin    = origin;
    ray.direction = direction;
    ray.t_near    = 0.0f;
    ray.t_far     = t_far;
    ray.mask      = 0xffffffffu;
    return ray;
}

} // namespace erhe::raytrace::test
