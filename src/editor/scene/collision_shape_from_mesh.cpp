#include "scene/collision_shape_from_mesh.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <geogram/mesh/mesh.h>

#include <vector>

namespace editor {

auto build_shape_from_node_mesh(const erhe::scene::Node* node, const bool convex_hull) -> std::shared_ptr<erhe::physics::ICollision_shape>
{
    if (node == nullptr) {
        return {};
    }
    const auto mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node);
    if (!mesh) {
        return {};
    }
    for (const auto& prim : mesh->get_primitives()) {
        if (!prim.primitive || !prim.primitive->render_shape) {
            continue;
        }
        const auto& geom = prim.primitive->render_shape->get_geometry_const();
        if (!geom || (geom->get_mesh().vertices.nb() == 0)) {
            continue;
        }
        const GEO::Mesh& geo_mesh = geom->get_mesh();

        if (convex_hull) {
            GEO::Mesh convex_hull_mesh{};
            if (!erhe::geometry::make_convex_hull(geo_mesh, convex_hull_mesh)) {
                return {};
            }
            const GEO::index_t vertex_count = convex_hull_mesh.vertices.nb();
            std::vector<float> coordinates(3 * vertex_count);
            for (GEO::index_t v = 0; v < vertex_count; ++v) {
                const float* ptr = convex_hull_mesh.vertices.single_precision_point_ptr(v);
                coordinates[(3 * v) + 0] = ptr[0];
                coordinates[(3 * v) + 1] = ptr[1];
                coordinates[(3 * v) + 2] = ptr[2];
            }
            return erhe::physics::ICollision_shape::create_convex_hull_shape_shared(
                coordinates.data(),
                static_cast<int>(vertex_count),
                static_cast<int>(3 * sizeof(float))
            );
        }

        // Triangle mesh: all vertices plus fan-triangulated facets.
        const GEO::index_t vertex_count = geo_mesh.vertices.nb();
        std::vector<float> coordinates(3 * vertex_count);
        for (GEO::index_t v = 0; v < vertex_count; ++v) {
            const float* ptr = geo_mesh.vertices.single_precision_point_ptr(v);
            coordinates[(3 * v) + 0] = ptr[0];
            coordinates[(3 * v) + 1] = ptr[1];
            coordinates[(3 * v) + 2] = ptr[2];
        }
        std::vector<uint32_t> indices;
        for (GEO::index_t facet = 0, facet_count = geo_mesh.facets.nb(); facet < facet_count; ++facet) {
            const GEO::index_t corners_begin = geo_mesh.facets.corners_begin(facet);
            const GEO::index_t corners_end   = geo_mesh.facets.corners_end(facet);
            if ((corners_end - corners_begin) < 3) {
                continue;
            }
            const GEO::index_t first_vertex = geo_mesh.facet_corners.vertex(corners_begin);
            for (GEO::index_t corner = corners_begin + 1; (corner + 1) < corners_end; ++corner) {
                indices.push_back(first_vertex);
                indices.push_back(geo_mesh.facet_corners.vertex(corner));
                indices.push_back(geo_mesh.facet_corners.vertex(corner + 1));
            }
        }
        if (indices.empty()) {
            return {};
        }
        return erhe::physics::ICollision_shape::create_mesh_shape_shared(
            coordinates.data(),
            static_cast<int>(vertex_count),
            static_cast<int>(3 * sizeof(float)),
            indices.data(),
            static_cast<int>(indices.size() / 3)
        );
    }
    return {};
}

}
