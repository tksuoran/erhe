#include "erhe_geometry/operation/flip_facets.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace erhe::geometry::operation {

namespace {

// Reverses the facets marked in the flip mask (indexed by source facet) on
// the scratch and emits the result. Nothing is created or deleted, so the
// source's vertex, edge and facet indices carry over unchanged.
class Flip_facets : public Edit_mesh_operation
{
public:
    Flip_facets(const Geometry& source, Geometry& destination)
        : Edit_mesh_operation{source, destination}
    {
        m_flip.assign(m_edit_mesh.get_facet_slot_count(), 0);
    }

    [[nodiscard]] auto get_flip() -> std::vector<std::uint8_t>&
    {
        return m_flip;
    }

    [[nodiscard]] auto get_edit_mesh() const -> const Edit_mesh&
    {
        return m_edit_mesh;
    }

    void build()
    {
        const GEO::index_t facet_slot_count = m_edit_mesh.get_facet_slot_count();
        m_flip.resize(facet_slot_count, 0);
        for (GEO::index_t facet = 0; facet < facet_slot_count; ++facet) {
            if ((m_flip[facet] != 0) && m_edit_mesh.is_facet_alive(facet)) {
                m_edit_mesh.reverse_facet(facet);
            }
        }
        emit();
        fix_normals();
    }

private:
    // The emission interpolates normals from the unflipped source: a flipped
    // facet's corner normals and the vertex normal of a vertex whose facets
    // are all flipped turn around, and the smooth vertex normals follow the
    // new winding.
    void fix_normals()
    {
        Mesh_attributes&   attributes   = destination.get_attributes();
        const GEO::index_t facet_count  = destination_mesh.facets.nb();
        const GEO::index_t vertex_count = destination_mesh.vertices.nb();

        // 0: no facet seen, 1: every facet seen so far flipped, 2: an unflipped facet seen.
        std::vector<std::uint8_t> vertex_state(vertex_count, 0);
        for (GEO::index_t facet = 0; facet < facet_count; ++facet) {
            const bool flipped = (facet < m_flip.size()) && (m_flip[facet] != 0);
            for (GEO::index_t corner : destination_mesh.facets.corners(facet)) {
                const GEO::index_t vertex = destination_mesh.facet_corners.vertex(corner);
                if (flipped) {
                    if (vertex_state[vertex] == 0) {
                        vertex_state[vertex] = 1;
                    }
                    const std::optional<GEO::vec3f> normal = attributes.corner_normal.try_get(corner);
                    if (normal.has_value()) {
                        attributes.corner_normal.set(corner, -normal.value());
                    }
                } else {
                    vertex_state[vertex] = 2;
                }
            }
        }
        for (GEO::index_t vertex = 0; vertex < vertex_count; ++vertex) {
            if (vertex_state[vertex] != 1) {
                continue;
            }
            const std::optional<GEO::vec3f> normal = attributes.vertex_normal.try_get(vertex);
            if (normal.has_value()) {
                attributes.vertex_normal.set(vertex, -normal.value());
            }
        }
        compute_mesh_vertex_normal_smooth(destination_mesh, attributes);
    }

    std::vector<std::uint8_t> m_flip;
};

void remap_selection(const Geometry_operation& operation, Component_remap* remap)
{
    if ((remap == nullptr) || (remap->source == nullptr) || (remap->destination == nullptr)) {
        return;
    }
    operation.remap_component_selection(*remap->source, *remap->destination);
}

auto get_centre(const Edit_mesh& edit_mesh, const GEO::index_t facet) -> GEO::vec3f
{
    const std::span<const Edit_corner> corners = edit_mesh.get_facet_corners(facet);
    GEO::vec3f sum{0.0f, 0.0f, 0.0f};
    for (const Edit_corner& corner : corners) {
        sum += edit_mesh.get_position(corner.vertex);
    }
    return corners.empty() ? sum : (sum / static_cast<float>(corners.size()));
}

// The Newell normal, unnormalized: its length is twice the facet area.
auto get_area_normal(const Edit_mesh& edit_mesh, const GEO::index_t facet) -> GEO::vec3f
{
    const std::span<const Edit_corner> corners = edit_mesh.get_facet_corners(facet);
    const std::size_t n = corners.size();
    GEO::vec3f normal{0.0f, 0.0f, 0.0f};
    for (std::size_t i = 0; i < n; ++i) {
        const GEO::vec3f a = edit_mesh.get_position(corners[i].vertex);
        const GEO::vec3f b = edit_mesh.get_position(corners[(i + 1) % n].vertex);
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    return normal;
}

// True when the facet, in its source winding, runs from vertex_a to vertex_b.
auto runs_from_to(const Edit_mesh& edit_mesh, const GEO::index_t facet, const GEO::index_t vertex_a, const GEO::index_t vertex_b) -> bool
{
    const std::span<const Edit_corner> corners = edit_mesh.get_facet_corners(facet);
    const std::size_t n = corners.size();
    for (std::size_t i = 0; i < n; ++i) {
        if ((corners[i].vertex == vertex_a) && (corners[(i + 1) % n].vertex == vertex_b)) {
            return true;
        }
    }
    return false;
}

// The edge between the two vertices when it has exactly two facets,
// GEO::NO_INDEX otherwise.
auto get_manifold_edge(const Edit_mesh& edit_mesh, const GEO::index_t vertex_a, const GEO::index_t vertex_b) -> GEO::index_t
{
    const GEO::index_t edge = edit_mesh.find_edge(vertex_a, vertex_b);
    if ((edge == GEO::NO_INDEX) || (edit_mesh.get_edge_facet_count(edge) != 2)) {
        return GEO::NO_INDEX;
    }
    return edge;
}

// Fills flip (indexed by facet) with the facets recalculate_facet_normals()
// reverses.
void compute_recalculate_flips(
    const Edit_mesh&              edit_mesh,
    const std::set<GEO::index_t>& selected_facets,
    const Normal_side             side,
    std::vector<std::uint8_t>&    flip
)
{
    const GEO::index_t facet_slot_count = edit_mesh.get_facet_slot_count();
    std::vector<std::uint8_t> in_region(facet_slot_count, 0);
    if (selected_facets.empty()) {
        for (GEO::index_t facet = 0; facet < facet_slot_count; ++facet) {
            in_region[facet] = edit_mesh.is_facet_alive(facet) ? 1 : 0;
        }
    } else {
        for (const GEO::index_t facet : selected_facets) {
            if ((facet < facet_slot_count) && edit_mesh.is_facet_alive(facet)) {
                in_region[facet] = 1;
            }
        }
    }

    // visited: 0 unvisited, 1 in the current component and not yet oriented,
    // 2 oriented.
    flip.assign(facet_slot_count, 0);
    std::vector<std::uint8_t> visited(facet_slot_count, 0);
    std::vector<GEO::index_t> component;
    std::vector<GEO::index_t> stack;
    for (GEO::index_t seed = 0; seed < facet_slot_count; ++seed) {
        if ((in_region[seed] == 0) || (visited[seed] != 0)) {
            continue;
        }

        // The component of the region containing seed, connected through
        // manifold edges whose two facets are both in the region.
        component.clear();
        stack.clear();
        stack.push_back(seed);
        visited[seed] = 1;
        while (!stack.empty()) {
            const GEO::index_t facet = stack.back();
            stack.pop_back();
            component.push_back(facet);
            const std::span<const Edit_corner> corners = edit_mesh.get_facet_corners(facet);
            const std::size_t n = corners.size();
            for (std::size_t i = 0; i < n; ++i) {
                const GEO::index_t edge = get_manifold_edge(edit_mesh, corners[i].vertex, corners[(i + 1) % n].vertex);
                if (edge == GEO::NO_INDEX) {
                    continue;
                }
                for (const GEO::index_t other : edit_mesh.get_edge(edge).facets) {
                    if ((other != facet) && (in_region[other] != 0) && (visited[other] == 0)) {
                        visited[other] = 1;
                        stack.push_back(other);
                    }
                }
            }
        }

        // The region centre, weighted by facet area (the plain mean of the
        // facet centres when every facet is degenerate).
        GEO::vec3f weighted_sum{0.0f, 0.0f, 0.0f};
        GEO::vec3f plain_sum   {0.0f, 0.0f, 0.0f};
        float      area_sum    {0.0f};
        for (const GEO::index_t facet : component) {
            const GEO::vec3f centre = get_centre(edit_mesh, facet);
            const float      area   = 0.5f * GEO::length(get_area_normal(edit_mesh, facet));
            weighted_sum += area * centre;
            plain_sum    += centre;
            area_sum     += area;
        }
        const GEO::vec3f region_centre = (area_sum > 0.0f)
            ? (weighted_sum / area_sum)
            : (plain_sum / static_cast<float>(component.size()));

        // The extreme facet: the largest distance of its centre from the
        // region centre along its own normal line. It is surely on the
        // outside, so its normal must point away from the region centre.
        GEO::index_t best_facet = component.front();
        float        best_distance{-1.0f};
        float        best_signed_distance{0.0f};
        for (const GEO::index_t facet : component) {
            const GEO::vec3f area_normal = get_area_normal(edit_mesh, facet);
            const float      length      = GEO::length(area_normal);
            if (length <= 0.0f) {
                continue;
            }
            const float signed_distance = GEO::dot(get_centre(edit_mesh, facet) - region_centre, area_normal / length);
            if (std::abs(signed_distance) > best_distance) {
                best_distance        = std::abs(signed_distance);
                best_signed_distance = signed_distance;
                best_facet           = facet;
            }
        }

        // Orient the extreme facet, then propagate: a neighbour that runs the
        // shared edge in the same direction as the oriented facet's final
        // winding is flipped.
        flip[best_facet]    = (best_signed_distance < 0.0f) ? 1 : 0;
        visited[best_facet] = 2;
        stack.clear();
        stack.push_back(best_facet);
        while (!stack.empty()) {
            const GEO::index_t facet = stack.back();
            stack.pop_back();
            const std::span<const Edit_corner> corners = edit_mesh.get_facet_corners(facet);
            const std::size_t n = corners.size();
            for (std::size_t i = 0; i < n; ++i) {
                const GEO::index_t a    = corners[i].vertex;
                const GEO::index_t b    = corners[(i + 1) % n].vertex;
                const GEO::index_t edge = get_manifold_edge(edit_mesh, a, b);
                if (edge == GEO::NO_INDEX) {
                    continue;
                }
                const GEO::index_t from = (flip[facet] != 0) ? b : a;
                const GEO::index_t to   = (flip[facet] != 0) ? a : b;
                for (const GEO::index_t other : edit_mesh.get_edge(edge).facets) {
                    if ((other == facet) || (in_region[other] == 0) || (visited[other] != 1)) {
                        continue;
                    }
                    flip[other]    = runs_from_to(edit_mesh, other, from, to) ? 1 : 0;
                    visited[other] = 2;
                    stack.push_back(other);
                }
            }
        }

        if (side == Normal_side::inside) {
            for (const GEO::index_t facet : component) {
                flip[facet] = (flip[facet] != 0) ? 0 : 1;
            }
        }
    }
}

} // anonymous namespace

void flip_facets(
    const Geometry&               source,
    Geometry&                     destination,
    const std::set<GEO::index_t>& selected_facets,
    Component_remap*              remap
)
{
    Flip_facets operation{source, destination};
    std::vector<std::uint8_t>& flip = operation.get_flip();
    for (const GEO::index_t facet : selected_facets) {
        if (facet < flip.size()) {
            flip[facet] = 1;
        }
    }
    operation.build();
    remap_selection(operation, remap);
}

void recalculate_facet_normals(
    const Geometry&               source,
    Geometry&                     destination,
    const std::set<GEO::index_t>& selected_facets,
    const Normal_side             side,
    Component_remap*              remap
)
{
    Flip_facets operation{source, destination};
    compute_recalculate_flips(operation.get_edit_mesh(), selected_facets, side, operation.get_flip());
    operation.build();
    remap_selection(operation, remap);
}

} // namespace erhe::geometry::operation
