#include "erhe_physics/box3d/box3d_mesh_shape.hpp"
#include "erhe_physics/box3d/glm_conversions.hpp"
#include "erhe_physics/physics_log.hpp"

#include <fmt/format.h>

#include <vector>

namespace erhe::physics {

Box3d_mesh_shape::Box3d_mesh_shape(
    const float*    points,
    const int       point_count,
    const int       point_stride_bytes,
    const uint32_t* indices,
    const int       triangle_count
)
    : m_point_count   {point_count}
    , m_triangle_count{triangle_count}
{
    if ((points == nullptr) || (indices == nullptr) || (point_count < 3) || (triangle_count < 1)) {
        log_physics->error(
            "box3d mesh shape: degenerate input ({} points, {} triangles)",
            point_count, triangle_count
        );
        return;
    }

    std::vector<b3Vec3>  vertices;
    std::vector<int32_t> mesh_indices;
    vertices.reserve(static_cast<std::size_t>(point_count));
    mesh_indices.reserve(static_cast<std::size_t>(triangle_count) * 3u);

    const char* base = reinterpret_cast<const char*>(points);
    for (int i = 0; i < point_count; ++i) {
        const float* point = reinterpret_cast<const float*>(
            base + (static_cast<std::size_t>(i) * static_cast<std::size_t>(point_stride_bytes))
        );
        vertices.push_back(b3Vec3{point[0], point[1], point[2]});
    }
    for (int i = 0; i < (triangle_count * 3); ++i) {
        mesh_indices.push_back(static_cast<int32_t>(indices[i]));
    }

    b3MeshDef mesh_def{};
    mesh_def.vertices        = vertices.data();
    mesh_def.indices         = mesh_indices.data();
    mesh_def.materialIndices = nullptr;
    mesh_def.vertexCount     = point_count;
    mesh_def.triangleCount   = triangle_count;
    mesh_def.weldVertices    = false;
    mesh_def.weldTolerance   = 0.0f;
    mesh_def.useMedianSplit  = false;
    mesh_def.identifyEdges   = true;

    // Degenerate triangles are reported rather than silently dropped, so a bad
    // source mesh shows up in the log instead of as missing collision.
    int degenerate_triangle_indices[16]{};
    m_mesh = b3CreateMesh(&mesh_def, &degenerate_triangle_indices[0], 16);
    if (m_mesh == nullptr) {
        log_physics->error(
            "box3d mesh shape: b3CreateMesh failed for {} points, {} triangles",
            point_count, triangle_count
        );
    }
}

Box3d_mesh_shape::~Box3d_mesh_shape() noexcept
{
    if (m_mesh != nullptr) {
        b3DestroyMesh(m_mesh);
        m_mesh = nullptr;
    }
}

void Box3d_mesh_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    if (m_mesh == nullptr) {
        log_physics->error("box3d body '{}': mesh shape has no valid mesh and was not attached", context.debug_label);
        return;
    }
    context.contains_mesh = true;

    // b3CreateMeshShape takes a scale but no transform, so a mesh nested under a
    // rotated or translated compound child cannot be placed. erhe creates mesh
    // shapes at the root of a body's shape tree, so this is a latent case only.
    const bool has_rotation    = (std::abs(local_transform.q.s) < (1.0f - 1e-5f));
    const bool has_translation =
        (local_transform.p.x != 0.0f) || (local_transform.p.y != 0.0f) || (local_transform.p.z != 0.0f);
    if (has_rotation || has_translation) {
        log_physics->warn(
            "box3d body '{}': mesh shape placement (translation {}, {}, {} / rotation) is ignored; "
            "Box3D mesh shapes accept a scale but no transform",
            context.debug_label, local_transform.p.x, local_transform.p.y, local_transform.p.z
        );
    }

    const b3ShapeId shape_id = b3CreateMeshShape(context.body, context.shape_def, m_mesh, to_box3d(scale));
    context.shape_ids->push_back(shape_id);
}

auto Box3d_mesh_shape::is_convex() const -> bool
{
    return false;
}

auto Box3d_mesh_shape::contains_mesh() const -> bool
{
    return true;
}

auto Box3d_mesh_shape::describe() const -> std::string
{
    return fmt::format("Box3d_mesh_shape(points = {}, triangles = {})", m_point_count, m_triangle_count);
}

auto ICollision_shape::create_mesh_shape(
    const float*    points,
    const int       point_count,
    const int       point_stride_bytes,
    const uint32_t* indices,
    const int       triangle_count
) -> ICollision_shape*
{
    return new Box3d_mesh_shape(points, point_count, point_stride_bytes, indices, triangle_count);
}

auto ICollision_shape::create_mesh_shape_shared(
    const float*    points,
    const int       point_count,
    const int       point_stride_bytes,
    const uint32_t* indices,
    const int       triangle_count
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_mesh_shape>(points, point_count, point_stride_bytes, indices, triangle_count);
}

} // namespace erhe::physics
