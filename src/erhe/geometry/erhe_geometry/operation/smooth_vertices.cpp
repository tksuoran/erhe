#include "erhe_geometry/operation/smooth_vertices.hpp"
#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cstddef>
#include <set>
#include <vector>

namespace erhe::geometry::operation {

void smooth_vertices(
    const Geometry&               geometry,
    const std::set<GEO::index_t>& selected_vertices,
    const Smooth_vertices_options options,
    std::vector<GEO::vec3f>&      out_positions
)
{
    const GEO::Mesh&   mesh         = geometry.get_mesh();
    const GEO::index_t vertex_count = mesh.vertices.nb();

    // Slot of each selected vertex in out_positions, GEO::NO_INDEX for others.
    std::vector<GEO::index_t> slot(vertex_count, GEO::NO_INDEX);
    out_positions.clear();
    out_positions.reserve(selected_vertices.size());
    std::vector<GEO::index_t> vertices;
    vertices.reserve(selected_vertices.size());
    for (const GEO::index_t vertex : selected_vertices) {
        if (vertex >= vertex_count) {
            continue;
        }
        slot[vertex] = static_cast<GEO::index_t>(vertices.size());
        vertices.push_back(vertex);
        out_positions.push_back(get_pointf(mesh.vertices, vertex));
    }
    if (vertices.empty()) {
        return;
    }

    // Edge-connected neighbours of each selected vertex: the vertices before
    // and after it in every facet using it, each listed once.
    std::vector<std::vector<GEO::index_t>> neighbours(vertices.size());
    for (const GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_vertices(facet);
        for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
            const GEO::index_t vertex = mesh.facets.vertex(facet, local_corner);
            const GEO::index_t i      = slot[vertex];
            if (i == GEO::NO_INDEX) {
                continue;
            }
            const GEO::index_t previous = mesh.facets.vertex(facet, (local_corner + corner_count - 1) % corner_count);
            const GEO::index_t next     = mesh.facets.vertex(facet, (local_corner + 1) % corner_count);
            neighbours[i].push_back(previous);
            neighbours[i].push_back(next);
        }
    }
    for (std::size_t i = 0, end = vertices.size(); i < end; ++i) {
        std::vector<GEO::index_t>& list = neighbours[i];
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
        std::erase(list, vertices[i]);
    }

    // Each iteration reads the positions at its start.
    std::vector<GEO::vec3f> start_positions;
    for (int iteration = 0; iteration < options.repeat; ++iteration) {
        start_positions = out_positions;
        for (std::size_t i = 0, end = vertices.size(); i < end; ++i) {
            const std::vector<GEO::index_t>& list = neighbours[i];
            if (list.empty()) {
                continue;
            }
            GEO::vec3f sum{0.0f, 0.0f, 0.0f};
            for (const GEO::index_t neighbour : list) {
                const GEO::index_t neighbour_slot = slot[neighbour];
                sum += (neighbour_slot != GEO::NO_INDEX) ? start_positions[neighbour_slot] : get_pointf(mesh.vertices, neighbour);
            }
            const GEO::vec3f average = sum / static_cast<float>(list.size());
            const GEO::vec3f p       = start_positions[i];
            out_positions[i] = p + (options.factor * (average - p));
        }
    }
}

} // namespace erhe::geometry::operation
