#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"

#include "erhe_dataformat/vertex_format.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>

// bake_prepared_primitive_transform() (doc/editor/world_space_bake.md
// "Baking a primitive").

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;
using erhe::primitive::Prepared_primitive_export;
using erhe::primitive::Triangle_soup;

class Vertex
{
public:
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 tangent;
};

[[nodiscard]] auto make_export(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) -> Prepared_primitive_export
{
    Vertex_stream stream{0};
    stream.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position, 0);
    stream.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::normal,   0);
    stream.emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::tangent,  0);
    stream.finalize_stride();
    Prepared_primitive_export prepared{};
    prepared.core.vertex_format.streams.push_back(std::move(stream));
    prepared.core.vertex_data.resize(vertices.size() * sizeof(Vertex));
    std::memcpy(prepared.core.vertex_data.data(), vertices.data(), prepared.core.vertex_data.size());
    prepared.core.index_data = indices;
    return prepared;
}

[[nodiscard]] auto read_vertex(const Triangle_soup& soup, const std::size_t index) -> Vertex
{
    Vertex vertex{};
    std::memcpy(&vertex, soup.vertex_data.data() + (index * sizeof(Vertex)), sizeof(Vertex));
    return vertex;
}

[[nodiscard]] auto near(const glm::vec3& a, const glm::vec3& b) -> bool
{
    return glm::length(a - b) < 1.0e-5f;
}

} // anonymous namespace

TEST(Bake_transform, mirrored_scale_moves_positions_normals_tangents_and_flips_winding)
{
    const std::vector<Vertex> vertices{
        Vertex{.position = {0.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f}},
        Vertex{.position = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f}},
        Vertex{.position = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f}}
    };
    Prepared_primitive_export prepared = make_export(vertices, {0, 1, 2});
    const glm::mat4 transform = glm::scale(glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f}), glm::vec3{-2.0f, 3.0f, 1.0f});

    const erhe::primitive::Bake_transform_result result = erhe::primitive::bake_prepared_primitive_transform(prepared, transform);

    EXPECT_TRUE(result.flipped_winding);
    EXPECT_EQ(result.dropped_degenerate_triangle_count, 0u);
    ASSERT_EQ(prepared.core.index_data.size(), 3u);
    EXPECT_EQ(prepared.core.index_data[0], 0u);
    EXPECT_EQ(prepared.core.index_data[1], 2u);
    EXPECT_EQ(prepared.core.index_data[2], 1u);
    const Vertex v1 = read_vertex(prepared.core, 1);
    EXPECT_TRUE(near(v1.position, glm::vec3{8.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(near(v1.normal,   glm::vec3{0.0f, 0.0f, 1.0f}));
    EXPECT_TRUE(near(glm::vec3{v1.tangent}, glm::vec3{-1.0f, 0.0f, 0.0f}));
    EXPECT_FLOAT_EQ(v1.tangent.w, -1.0f);
    const Vertex v2 = read_vertex(prepared.core, 2);
    EXPECT_TRUE(near(v2.position, glm::vec3{10.0f, 3.0f, 0.0f}));
    EXPECT_TRUE(near(prepared.bounding_box.min, glm::vec3{8.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(near(prepared.bounding_box.max, glm::vec3{10.0f, 3.0f, 0.0f}));
    const std::vector<float> positions = erhe::primitive::get_float_positions(prepared.core);
    EXPECT_EQ(prepared.content_hash, erhe::primitive::compute_mesh_content_hash(prepared.core.index_data, positions));
}

TEST(Bake_transform, non_uniform_scale_keeps_normals_perpendicular)
{
    const glm::vec3 normal = glm::normalize(glm::vec3{1.0f, 1.0f, 0.0f});
    const glm::vec3 tangent = glm::normalize(glm::vec3{1.0f, -1.0f, 0.0f});
    const std::vector<Vertex> vertices{
        Vertex{.position = {0.0f, 0.0f, 0.0f}, .normal = normal, .tangent = glm::vec4{tangent, 1.0f}},
        Vertex{.position = {1.0f, -1.0f, 0.0f}, .normal = normal, .tangent = glm::vec4{tangent, 1.0f}},
        Vertex{.position = {0.0f, 0.0f, 1.0f}, .normal = normal, .tangent = glm::vec4{tangent, 1.0f}}
    };
    Prepared_primitive_export prepared = make_export(vertices, {0, 1, 2});
    const glm::mat4 transform = glm::scale(glm::mat4{1.0f}, glm::vec3{4.0f, 1.0f, 1.0f});

    static_cast<void>(erhe::primitive::bake_prepared_primitive_transform(prepared, transform));

    const Vertex v = read_vertex(prepared.core, 0);
    EXPECT_NEAR(glm::length(v.normal), 1.0f, 1.0e-5f);
    EXPECT_NEAR(glm::length(glm::vec3{v.tangent}), 1.0f, 1.0e-5f);
    EXPECT_NEAR(glm::dot(v.normal, glm::vec3{v.tangent}), 0.0f, 1.0e-5f);
    // The baked surface's own normal agrees with the transformed normal.
    const glm::vec3 p0 = read_vertex(prepared.core, 0).position;
    const glm::vec3 p1 = read_vertex(prepared.core, 1).position;
    const glm::vec3 p2 = read_vertex(prepared.core, 2).position;
    EXPECT_NEAR(std::abs(glm::dot(glm::normalize(glm::cross(p1 - p0, p2 - p0)), v.normal)), 1.0f, 1.0e-5f);
}

TEST(Bake_transform, triangles_that_collapse_to_one_position_are_dropped)
{
    // Two corners 1e-6 apart round to one float once translated by 1e5.
    const std::vector<Vertex> vertices{
        Vertex{.position = {0.0f,      0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f}},
        Vertex{.position = {1.0e-6f,   0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f}},
        Vertex{.position = {0.0f,      1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f}},
        Vertex{.position = {1.0f,      0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f}}
    };
    Prepared_primitive_export prepared = make_export(vertices, {0, 1, 2, 0, 3, 2});
    const glm::mat4 transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0e5f, 0.0f, 0.0f});

    const erhe::primitive::Bake_transform_result result = erhe::primitive::bake_prepared_primitive_transform(prepared, transform);

    EXPECT_FALSE(result.flipped_winding);
    EXPECT_EQ(result.dropped_degenerate_triangle_count, 1u);
    ASSERT_EQ(prepared.core.index_data.size(), 3u);
    EXPECT_EQ(prepared.core.index_data[1], 3u);
}
