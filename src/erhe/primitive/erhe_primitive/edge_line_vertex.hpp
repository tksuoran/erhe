#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

namespace erhe::primitive {

// One edge-line endpoint as Content_wide_line_renderer's compute expansion
// reads it (the edge_line_vertex SSBO struct of
// compute_before_content_line.comp): vec4 position (w unused) and vec4 face
// normal, whose w packs the face's interior-tangent sign (the sign of w) and
// its edge-traversal winding (|w| 1: the face walks the edge from endpoint 0
// to endpoint 1, 2: the reverse). w 0 marks an edge without a face, which the
// shader treats as a degenerate frame.
static constexpr std::size_t edge_line_vertex_float_count = 8;
static constexpr std::size_t edge_line_vertex_byte_size   = edge_line_vertex_float_count * sizeof(float);

// One endpoint of edge-line joint side data: uvec4 joint indices, vec4
// joint weights.
static constexpr std::size_t edge_line_joint_byte_size = 4 * sizeof(uint32_t) + 4 * sizeof(float);

// A face adjacent to an edge, as the endpoint packing needs it.
class Edge_line_face
{
public:
    glm::vec3 normal  {0.0f, 1.0f, 0.0f}; // unit geometric normal
    glm::vec3 center  {0.0f};             // centroid
    bool      walks_a_to_b{false};        // the face's corner loop walks the edge endpoint a -> endpoint b
};

// Writes the two endpoints of edge (a, b), 2 * edge_line_vertex_float_count
// floats, to `out`. Endpoint a carries face A's normal, endpoint b face B's.
// `face_a` null: the edge has no face; both endpoints take their fallback
// normals and w 0. `face_b` null with `face_a` set: a boundary edge; face B
// reuses face A's plane on the opposite interior side, so the two half-quads
// form a full ribbon. The one packing rule for every edge-line build
// (Build_context::build_edge_lines(), build_lod_edge_lines()).
void write_edge_line_endpoints(
    const glm::vec3&      position_a,
    const glm::vec3&      position_b,
    const Edge_line_face* face_a,
    const Edge_line_face* face_b,
    const glm::vec3&      fallback_normal_a,
    const glm::vec3&      fallback_normal_b,
    float*                out
);

} // namespace erhe::primitive
