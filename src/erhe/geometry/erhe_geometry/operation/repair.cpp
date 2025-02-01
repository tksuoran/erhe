#include "erhe_geometry/operation/repair.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/octree.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_repair.h>
#include <geogram/mesh/mesh_fill_holes.h>
#include <geogram/mesh/mesh_surface_intersection.h>

#include <numeric>

namespace erhe::geometry::operation {

class Repair : public Geometry_operation
{
public:
    Repair(const Geometry& source, Geometry& destination);

    void build();
};

Repair::Repair(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Repair::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    const double merge_vertices_epsilon = 0.001;
    const double fill_hole_max_area = 0.01;
    GEO::mesh_repair(
        destination_mesh,
        static_cast<GEO::MeshRepairMode>(GEO::MeshRepairMode::MESH_REPAIR_COLOCATE | GEO::MeshRepairMode::MESH_REPAIR_DUP_F),
        merge_vertices_epsilon
    );
    GEO::fill_holes(destination_mesh, fill_hole_max_area);
    GEO::MeshSurfaceIntersection intersection(destination_mesh);
    intersection.intersect();
    intersection.remove_internal_shells();
    intersection.simplify_coplanar_facets();
    GEO::mesh_repair(destination_mesh, GEO::MESH_REPAIR_DEFAULT, merge_vertices_epsilon);
    destination.get_attributes().bind();

    post_processing();
}

void repair(const Geometry& source, Geometry& destination)
{
    Repair operation{source, destination};
    operation.build();
}

class Weld : public Geometry_operation
{
public:
    Weld(const Geometry& source, Geometry& destination);

    void build_();
    void build();

private:
    void find_vertex_merge_candidates      ();
    void rotate_facets_to_min_vertex_first ();
    void sort_facets                       ();
    void scan_for_equal_and_opposite_facets();
    void mark_used_vertices                ();
    void count_used_vertices               ();

    auto format_facet_vertices(const GEO::index_t facet) const -> std::string;

    float                     m_max_distance;
    uint32_t                  m_used_vertex_count;
    std::vector<GEO::index_t> m_vertex_merge_candidates;
    std::vector<bool>         m_vertex_used;
    std::vector<GEO::index_t> m_facets_sorted;
    std::vector<GEO::index_t> m_facet_remove;
};

void Weld::find_vertex_merge_candidates()
{
    std::vector<GEO::vec3f> points;
    points.resize(source_mesh.vertices.nb());
    for (GEO::index_t src_vertex : source_mesh.vertices) {
        points[src_vertex] = GEO::vec3f{source_mesh.vertices.point(src_vertex)};
    }

    unibn::Octree<GEO::vec3f> octree;
    unibn::OctreeParams params;
    octree.initialize(points);

    for (GEO::index_t src_vertex : source_mesh.vertices) {
        if (m_vertex_merge_candidates[src_vertex] != src_vertex) {
            continue; // continue already marked
        }

        const GEO::vec3f query_location = points[src_vertex];

        std::vector<GEO::index_t> nearby_vertex_indices;
        octree.radiusNeighbors<unibn::L2Distance<GEO::vec3f>>(
            query_location,
            m_max_distance,
            nearby_vertex_indices
        );

        for (const auto merge_vertex : nearby_vertex_indices) {
            m_vertex_merge_candidates[merge_vertex] = src_vertex;
        }
    }
}

// Rotate facets so that the corner with smallest vertex
// (after considering vertex merges) is the first corner.
void Weld::rotate_facets_to_min_vertex_first()
{
    GEO::Mesh& mutable_source_mesh = const_cast<GEO::Mesh&>(source_mesh);
    for (GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t        first_corner_id          = source_mesh.facets.corner(src_facet, 0);
        const GEO::index_t        min_vertex0              = source_mesh.facet_corners.vertex(first_corner_id);
        GEO::index_t              min_vertex               = m_vertex_merge_candidates[min_vertex0];
        GEO::index_t              min_vertex_local_corner = 0;
        std::vector<GEO::index_t> facet_vertices;

        // Find corner with smallest vertex
        const GEO::index_t corner_count = source_mesh.facets.nb_corners(src_facet);
        for (GEO::index_t local_facet_corner = 0; local_facet_corner < corner_count; ++local_facet_corner) {
            const GEO::index_t corner  = source_mesh.facets.corner(src_facet, local_facet_corner);
            const GEO::index_t vertex0 = source_mesh.facet_corners.vertex(corner);
            const GEO::index_t vertex  = m_vertex_merge_candidates[vertex0];
            facet_vertices.push_back(vertex);
            if (vertex < min_vertex) {
                min_vertex = vertex;
                min_vertex_local_corner = local_facet_corner;
            }
        }

        // Rotate corners of polygon
        for (GEO::index_t local_facet_corner = 0; local_facet_corner < corner_count; ++local_facet_corner) {
            const GEO::index_t rotated_vertex = facet_vertices.at((local_facet_corner + min_vertex_local_corner) % corner_count);
            mutable_source_mesh.facets.set_vertex(src_facet, local_facet_corner, rotated_vertex);
        }
    }
}

// Facet sorting is based on (merged) vertex (indices).
//
// - Facets are sorted based on the first vertex (index) of the first corner
// - rotate_facets_to_min_vertex_first() must be done before sort_facets()
//
// This sorting makes it possible to use the sliding window
// algorithm to find potentially equal and opposite facets.
void Weld::sort_facets()
{
    std::sort(
        m_facets_sorted.begin(),
        m_facets_sorted.end(),
        [this](const GEO::index_t lhs_facet, const GEO::index_t rhs_facet) {
            const GEO::index_t lhs_first_corner = source_mesh.facets.corner(lhs_facet, 0);
            const GEO::index_t lhs_vertex0      = source_mesh.facet_corners.vertex(lhs_first_corner);
            const GEO::index_t lhs_vertex       = m_vertex_merge_candidates[lhs_vertex0];

            const GEO::index_t rhs_first_corner = source_mesh.facets.corner(rhs_facet, 0);
            const GEO::index_t rhs_vertex0      = source_mesh.facet_corners.vertex(rhs_first_corner);
            const GEO::index_t rhs_vertex       = m_vertex_merge_candidates[rhs_vertex0];

            return lhs_vertex < rhs_vertex;
        }
    );
}

auto Weld::format_facet_vertices(const GEO::index_t facet) const -> std::string
{
    std::stringstream ss;
    for (GEO::index_t corner : source_mesh.facets.corners(facet)) {
        const GEO::index_t vertex0 = source_mesh.facet_corners.vertex(corner);
        const GEO::index_t vertex  = m_vertex_merge_candidates[vertex0];
        ss << fmt::format("{:2} ", vertex);
    }
    return ss.str();
}

// Uses the sliding window algorithm to find facets which are equal or opposite.
//
// The test is based on topology, by comparing (merged) vertex (indices).
//
// The sliding window scan:
//
// - Scanning is done with a "sliding window"
// - The window start position is called left
// - The window end position is called right
// - Steps:
//   1. Left is initialized to first element (after sorting)
//   2. Right is initialized to left + 1
//   3. Right moves forward as long as vertex positions are within merge threshold
//   4. When right vertex position is different from left, left is advanced by one and goto step 2
void Weld::scan_for_equal_and_opposite_facets()
{
    for (size_t left_sort_index = 0, end = m_facets_sorted.size(); left_sort_index < end;++left_sort_index) {
        const GEO::index_t left_facet = m_facets_sorted[left_sort_index];
        if (m_facet_remove[left_facet]) {
            continue; // already marked
        }

        const GEO::index_t left_corner_count = source_mesh.facets.nb_corners(left_facet);
        if (left_corner_count == 0) {
            m_facet_remove[left_corner_count] = true;
            continue;
        }

        for (size_t right_sort_index = left_sort_index + 1; right_sort_index < end; ++right_sort_index) {
            const GEO::index_t right_facet = m_facets_sorted[right_sort_index];
            if (m_facet_remove[right_facet]) {
                continue; // already marked
            }

            const GEO::index_t right_corner_count = source_mesh.facets.nb_corners(right_facet);
            if (left_corner_count != right_corner_count) {
                continue;
            }

            // Facets can't be equal nor opposite if they don't share the same min vertex
            const GEO::index_t left_first_corner   = source_mesh.facets.corner(left_facet, 0);
            const GEO::index_t left_first_vertex0  = source_mesh.facet_corners.vertex(left_first_corner);
            const GEO::index_t left_first_vertex   = m_vertex_merge_candidates[left_first_vertex0];
            const GEO::index_t right_first_corner  = source_mesh.facets.corner(right_facet, 0);
            const GEO::index_t right_first_vertex0 = source_mesh.facet_corners.vertex(right_first_corner);
            const GEO::index_t right_first_vertex  = m_vertex_merge_candidates[right_first_vertex0];
            if (left_first_vertex != right_first_vertex) {
                break; // Sliding window: Advance left
            }

            const GEO::index_t corner_count = left_corner_count;

            bool facets_are_equal    = (corner_count >= 1);
            bool facets_are_opposite = (corner_count >= 3);

            for (GEO::index_t local_facet_corner = 0; local_facet_corner < corner_count; ++local_facet_corner) {
                const GEO::index_t left_corner   = source_mesh.facets.corner(left_facet, local_facet_corner);
                const GEO::index_t left_vertex0  = source_mesh.facet_corners.vertex(left_corner);
                const GEO::index_t left_vertex   = m_vertex_merge_candidates[left_vertex0];

                const GEO::index_t right_corner  = source_mesh.facets.corner(right_facet, local_facet_corner);
                const GEO::index_t right_vertex0 = source_mesh.facet_corners.vertex(right_corner);
                const GEO::index_t right_vertex  = m_vertex_merge_candidates[right_vertex0];

                if (left_vertex != right_vertex) {
                    facets_are_equal = false;
                }
                const GEO::index_t reverse_local_facet_corner = (corner_count - local_facet_corner) % corner_count;
                const GEO::index_t right_reverse_corner       = source_mesh.facets.corner(right_facet, reverse_local_facet_corner);
                const GEO::index_t right_reverse_vertex0      = source_mesh.facet_corners.vertex(right_reverse_corner);
                const GEO::index_t right_reverse_vertex       = m_vertex_merge_candidates[right_reverse_vertex0];

                if (left_vertex != right_reverse_vertex) {
                    facets_are_opposite = false;
                }
            }

            ERHE_VERIFY(!facets_are_equal || !facets_are_opposite); // Should not be able to be both

            if (facets_are_equal) {
                m_facet_remove[right_facet] = true;
            }

            if (facets_are_opposite) {
                m_facet_remove[left_facet] = true;
                m_facet_remove[right_facet] = true;
            }
        }
    }
}

// Collect points that are used be polygons that are not removed.
void Weld::mark_used_vertices()
{
    for (size_t sort_index = 0, end = m_facets_sorted.size(); sort_index < end; ++sort_index) {
        const GEO::index_t facet = m_facets_sorted[sort_index];
        if (m_facet_remove[facet]) {
            continue;
        }
        for (GEO::index_t corner : source_mesh.facets.corners(facet)) {
            const GEO::index_t vertex0 = source_mesh.facet_corners.vertex(corner);
            const GEO::index_t vertex  = m_vertex_merge_candidates[vertex0];
            m_vertex_used[vertex] = true;
        }
    }
}

void Weld::count_used_vertices()
{
    m_used_vertex_count = 0;
    for (const bool point_used : m_vertex_used) {
        if (point_used) {
            ++m_used_vertex_count;
        }
    }
}

Weld::Weld(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Weld::build_()
{
    GEO::mat4 i{};
    i.load_identity();
    destination.copy_with_transform(source, i);

    GEO::MeshSurfaceIntersection intersection(destination_mesh);
    intersection.set_delaunay(true);
    intersection.set_detect_intersecting_neighbors(true);
    intersection.set_radial_sort(true);
    intersection.intersect();
    intersection.remove_internal_shells();
    intersection.simplify_coplanar_facets(0.01); // optional

    post_processing();
}

void Weld::build()
{
    const GEO::index_t vertex_count = source_mesh.vertices.nb();
    const GEO::index_t facet_count  = source_mesh.facets.nb();
    m_vertex_merge_candidates.resize(vertex_count);
    m_vertex_used            .resize(vertex_count);
    m_facets_sorted          .resize(facet_count);
    m_facet_remove           .resize(facet_count);
    std::iota(m_vertex_merge_candidates.begin(), m_vertex_merge_candidates.end(), GEO::index_t{0});
    std::iota(m_facets_sorted.begin(), m_facets_sorted.end(), GEO::index_t{0});

    m_max_distance = 0.005f; // 5mm

    find_vertex_merge_candidates      ();
    rotate_facets_to_min_vertex_first ();
    sort_facets                       ();
    scan_for_equal_and_opposite_facets();
    mark_used_vertices                ();
    count_used_vertices               ();

    // Copy used vertices
    m_vertex_src_to_dst.reserve(m_used_vertex_count);
    for (GEO::index_t src_vertex : source_mesh.vertices) {
        if (m_vertex_used[src_vertex]) {
            make_new_dst_vertex_from_src_vertex(src_vertex);
        }
    }

    // Copy used facets
    for (GEO::index_t src_facet : source_mesh.facets) {
        if (m_facet_remove[src_facet]) {
            continue;
        }

        const GEO::index_t corner_count = source_mesh.facets.nb_corners(src_facet);
        const GEO::index_t dst_facet = make_new_dst_facet_from_src_facet(src_facet, corner_count);

        for (GEO::index_t local_facet_corner = 0; local_facet_corner < corner_count; ++local_facet_corner) {
            const GEO::index_t src_corner  = source_mesh.facets.corner(src_facet, local_facet_corner);
            const GEO::index_t old_vertex0 = source_mesh.facet_corners.vertex(src_corner);
            const GEO::index_t old_vertex  = m_vertex_merge_candidates[old_vertex0];
            const GEO::index_t new_vertex  = m_vertex_src_to_dst[old_vertex];
            const GEO::index_t new_corner  = destination_mesh.facets.corner(dst_facet, local_facet_corner);
            destination_mesh.facets.set_vertex(dst_facet, local_facet_corner, new_vertex);
            add_corner_source(new_corner, 1.0f, src_corner);
        }
    }

    post_processing();
}

void weld(const Geometry& source, Geometry& destination)
{
    Weld operation{source, destination};
    operation.build();
}


} // namespace erhe::geometry::operation
