#include "erhe_primitive/edge_line_vertex.hpp"

namespace erhe::primitive {

namespace {

// Interior-tangent sign of a face: the sign that makes
// sign * cross(normal, edge_direction) point from the edge midpoint toward
// the face's centroid. The compute shader needs each face's true interior
// side to place its half-quad: at a silhouette edge both interiors project to
// the same screen side, which only a per-face sign (not a heuristic) gets
// right.
[[nodiscard]] auto get_interior_sign(const Edge_line_face& face, const glm::vec3& edge_direction, const glm::vec3& edge_midpoint) -> float
{
    const glm::vec3 tangent     = glm::cross(face.normal, edge_direction);
    const glm::vec3 to_interior = face.center - edge_midpoint;
    return (glm::dot(tangent, to_interior) >= 0.0f) ? 1.0f : -1.0f;
}

} // anonymous namespace

void write_edge_line_endpoints(
    const glm::vec3&            position_a,
    const glm::vec3&            position_b,
    const Edge_line_face* const face_a,
    const Edge_line_face* const face_b,
    const glm::vec3&            fallback_normal_a,
    const glm::vec3&            fallback_normal_b,
    float* const                out
)
{
    glm::vec3 normal_a = fallback_normal_a;
    glm::vec3 normal_b = fallback_normal_b;
    float     sign_a   = 0.0f;
    float     sign_b   = 0.0f;
    float     tdir_a   = 0.0f; // edge-traversal winding (+/-1), 0 without a face
    float     tdir_b   = 0.0f;
    if (face_a != nullptr) {
        const glm::vec3 edge_direction = position_b - position_a;
        const glm::vec3 edge_midpoint  = 0.5f * (position_a + position_b);
        normal_a = face_a->normal;
        sign_a   = get_interior_sign(*face_a, edge_direction, edge_midpoint);
        tdir_a   = face_a->walks_a_to_b ? 1.0f : -1.0f;
        if (face_b != nullptr) {
            normal_b = face_b->normal;
            sign_b   = get_interior_sign(*face_b, edge_direction, edge_midpoint);
            tdir_b   = face_b->walks_a_to_b ? 1.0f : -1.0f;
        } else {
            normal_b = normal_a;
            sign_b   = -sign_a;
            tdir_b   = -tdir_a;
        }
    }

    // Both per-face signs packed into normal.w: its SIGN carries the
    // interior-tangent sign, its MAGNITUDE the edge-traversal winding
    // (tdir > 0 -> 1, tdir < 0 -> 2). Exact in fp32. The tent wide-line path
    // decodes both; the simple-quad and geometry-shader backends read only
    // normal.xyz.
    const float packed_w_a = sign_a * ((tdir_a < 0.0f) ? 2.0f : 1.0f);
    const float packed_w_b = sign_b * ((tdir_b < 0.0f) ? 2.0f : 1.0f);
    const float data[2 * edge_line_vertex_float_count] = {
        position_a.x, position_a.y, position_a.z, 0.0f, normal_a.x, normal_a.y, normal_a.z, packed_w_a,
        position_b.x, position_b.y, position_b.z, 0.0f, normal_b.x, normal_b.y, normal_b.z, packed_w_b
    };
    for (std::size_t i = 0; i < 2 * edge_line_vertex_float_count; ++i) {
        out[i] = data[i];
    }
}

} // namespace erhe::primitive
