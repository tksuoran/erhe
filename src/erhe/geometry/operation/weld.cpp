#include "erhe/geometry/operation/weld.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/geometry_log.hpp"
#include "erhe/geometry/operation/octree.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <numeric>

namespace erhe::geometry::operation
{

using vec3 = glm::vec3;

void Weld::find_point_merge_candidates()
{
    const auto* point_locations = source.point_attributes().find<vec3>(c_point_locations);

    std::vector<glm::vec3> points;
    points.resize(source.get_point_count());
    source.for_each_point_const(
        [&](const auto& i) {
            points[i.point_id] = point_locations->get(i.point_id);
        }
    );

    unibn::Octree<glm::vec3> octree;
    unibn::OctreeParams params;
    octree.initialize(points);

    source.for_each_point_const(
        [&](const auto& i) {
            if (m_point_id_merge_candidates[i.point_id] != i.point_id) {
                //// log_merge->info("Point {} is already marked to be merged", i.point_id);
                return; // continue already marked
            }

            const glm::vec3 query_location = points[i.point_id];

            std::vector<uint32_t> nearby_point_indices;
            octree.radiusNeighbors<unibn::L2Distance<glm::vec3>>(
                query_location,
                m_max_distance,
                nearby_point_indices
            );

            for (const auto merge_point_id : nearby_point_indices) {
                m_point_id_merge_candidates[merge_point_id] = i.point_id;
            }
        }
    );

    //// std::stringstream ss;
    //// for (const auto id : m_point_id_merge_candidates) {
    ////     ss << " " << id;
    //// }
    //// log_merge->info("Point merge candidates:{}", ss.str());
}

// Rotate polygon corners so that the corner with smallest point
// (after considering point merges) is the first corner.
void Weld::rotate_polygons_to_least_point_first()
{
    source.for_each_polygon([&](auto &i){
        Polygon&      polygon         = i.polygon;
        Corner_id     first_corner_id = source.polygon_corners[polygon.first_polygon_corner_id];
        const Corner& first_corner    = source.corners[first_corner_id];
        Point_id      min_point_id0   = first_corner.point_id;
        Point_id      min_point_id    = m_point_id_merge_candidates[min_point_id0];
        uint32_t      min_point_slot  = 0;
        std::vector<Corner_id> copy_of_polygon_corners;

        // Find corner with smallest Point_id
        for (uint32_t j = 0; j < polygon.corner_count; ++j) {
            Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + j;
            const Corner_id   corner_id         = source.polygon_corners[polygon_corner_id];
            const Corner&     corner            = source.corners[corner_id];
            const Point_id    point_id0         = corner.point_id;
            const Point_id    point_id          = m_point_id_merge_candidates[point_id0];
            copy_of_polygon_corners.push_back(corner_id);
            if (point_id < min_point_id) {
                min_point_id = point_id;
                min_point_slot = j;
            }
        }

        // Rotate corners of polygon
        for (uint32_t j = 0; j < polygon.corner_count; ++j) {
            Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + j;
            const Corner_id   corner_id         = copy_of_polygon_corners.at((j + min_point_slot) % polygon.corner_count);
            source.polygon_corners[polygon_corner_id] = corner_id;
        }
    });
}

// Polygon sorting is based on (merged) point ids.
//
// - Polygons are sorted based on the first point id of the first corner
// - rotate_polygons_to_least_point_first() must be done before sort_polygons()
//
// This sorting makes it possible to use the sliding window
// algorithm to find potentially equal and opposite polygons.
void Weld::sort_polygons()
{
    std::sort(
        m_polygon_id_sorted.begin(),
        m_polygon_id_sorted.end(),
        [this](const Polygon_id& lhs, const Polygon_id& rhs) {
            Polygon&      lhs_polygon         = source.polygons       [lhs];
            Corner_id     lhs_first_corner_id = source.polygon_corners[lhs_polygon.first_polygon_corner_id];
            const Corner& lhs_first_corner    = source.corners        [lhs_first_corner_id];
            Point_id      lhs_point_id0       = lhs_first_corner.point_id;
            Point_id      lhs_point_id        = m_point_id_merge_candidates[lhs_point_id0];

            Polygon&      rhs_polygon         = source.polygons       [rhs];
            Corner_id     rhs_first_corner_id = source.polygon_corners[rhs_polygon.first_polygon_corner_id];
            const Corner& rhs_first_corner    = source.corners        [rhs_first_corner_id];
            Point_id      rhs_point_id0       = rhs_first_corner.point_id;
            Point_id      rhs_point_id        = m_point_id_merge_candidates[rhs_point_id0];

            return lhs_point_id < rhs_point_id;
        }
    );

    //// log_merge->info("Sorted polygons:");
    //// for (const auto id : m_polygon_id_sorted) {
    ////     log_merge->info(
    ////         "   {}: {}",
    ////         id,
    ////         format_polygon_points(source.polygons.at(id))
    ////     );
    //// }
}

auto Weld::format_polygon_points(const Polygon& polygon) const -> std::string
{
    std::stringstream ss;
    for (uint32_t i = 0; i < polygon.corner_count; ++i) {
        const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
        const Corner_id         corner_id         = source.polygon_corners[polygon_corner_id];
        const Corner&           corner            = source.corners[corner_id];
        const Point_id          point_id0         = corner.point_id;
        const Point_id          point_id          = m_point_id_merge_candidates[point_id0];
        ss << fmt::format("{:2} ", point_id);
    }
    return ss.str();
}

// Uses the sliding window algorithm to find
// polygons which are equal or opposite.
//
// The test is purely based on topology, by comparing
// (merged) Point_ids.
//
// The sliding window scan:
//
// - Scanning is done with a "sliding window"
// - The window start position is called left
// - The window end position is called right
// - Steps:
//   1. Left is initialized to first element (after sorting)
//   2. Right is initialized to left + 1
//   3. Right moves forward as long as point locations are within merge threshold
//   4. When right point is different from left, left is advanced by one and goto step 2
void Weld::scan_for_equal_and_opposite_polygons()
{
    for (
        std::size_t left_sort_index = 0, end = m_polygon_id_sorted.size();
        left_sort_index < end;
        ++left_sort_index
    ) {
        const Polygon_id left_polygon_id = m_polygon_id_sorted[left_sort_index];
        if (m_polygon_id_remove[left_polygon_id]) {
            continue; // already marked
        }
        const Polygon& left_polygon = source.polygons[left_polygon_id];

        if (left_polygon.corner_count == 0) {
            m_polygon_id_remove[left_polygon_id] = true;
            continue;
        }

        for (
            std::size_t right_sort_index = left_sort_index + 1;
            right_sort_index < end;
            ++right_sort_index
        ) {
            const Polygon_id right_polygon_id = m_polygon_id_sorted[right_sort_index];
            if (m_polygon_id_remove[right_polygon_id]) {
                continue; // already marked
            }
            const Polygon& right_polygon = source.polygons[right_polygon_id];

            if (left_polygon.corner_count != right_polygon.corner_count) {
                continue;
            }

            // Polygons can't be equal nor opposite if they don't share the same point with least id
            const Corner_id left_first_corner_id  = source.polygon_corners[left_polygon.first_polygon_corner_id];
            const Corner&   left_first_corner     = source.corners        [left_first_corner_id];
            const Point_id  left_first_point_id0  = left_first_corner.point_id;
            const Point_id  left_first_point_id   = m_point_id_merge_candidates[left_first_point_id0];
            const Corner_id right_first_corner_id = source.polygon_corners[right_polygon.first_polygon_corner_id];
            const Corner&   right_first_corner    = source.corners        [right_first_corner_id];
            const Point_id  right_first_point_id0 = right_first_corner.point_id;
            const Point_id  right_first_point_id  = m_point_id_merge_candidates[right_first_point_id0];
            if (left_first_point_id != right_first_point_id) {
                break; // Sliding window: Advance left
            }

            const uint32_t corner_count = left_polygon.corner_count;

            bool polygons_are_equal    = (corner_count >= 1);
            bool polygons_are_opposite = (corner_count >= 3);

            for (uint32_t corner_index = 0; corner_index < corner_count; ++corner_index) {
                const Corner_id left_corner_id  = source.polygon_corners[left_polygon.first_polygon_corner_id + corner_index];
                const Corner&   left_corner     = source.corners        [left_corner_id];
                const Point_id  left_point_id0  = left_corner.point_id;
                const Point_id  left_point_id   = m_point_id_merge_candidates[left_point_id0];

                const Corner_id right_corner_id = source.polygon_corners[right_polygon.first_polygon_corner_id + corner_index];
                const Corner&   right_corner    = source.corners        [right_corner_id];
                const Point_id  right_point_id0 = right_corner.point_id;
                const Point_id  right_point_id  = m_point_id_merge_candidates[right_point_id0];

                if (left_point_id != right_point_id) {
                    polygons_are_equal = false;
                }
                const uint32_t  reverse_corner_index    = (corner_count - corner_index) % corner_count;
                const Corner_id right_reverse_corner_id = source.polygon_corners[right_polygon.first_polygon_corner_id + reverse_corner_index];
                const Corner&   right_reverse_corner    = source.corners        [right_reverse_corner_id];
                const Point_id  right_reverse_point_id0 = right_reverse_corner.point_id;
                const Point_id  right_reverse_point_id  = m_point_id_merge_candidates[right_reverse_point_id0];

                if (left_point_id != right_reverse_point_id) {
                    polygons_are_opposite = false;
                }
            }

            ERHE_VERIFY(!polygons_are_equal || !polygons_are_opposite); // Should not be able to be both

            if (polygons_are_equal) {
                m_polygon_id_remove[right_polygon_id] = true;
            }

            if (polygons_are_opposite) {
                m_polygon_id_remove[left_polygon_id ] = true;
                m_polygon_id_remove[right_polygon_id] = true;
            }
        }
    }

    //// std::stringstream ss;
    //// for (const auto id : m_polygon_id_sorted) {
    ////     if (m_polygon_id_remove[id]) {
    ////         ss << " " << id;
    ////     }
    //// }
    //// log_merge->info("Polygons to remove:{}", ss.str());
}

// Collect points that are used be polygons that are not removed.
void Weld::mark_used_points()
{
    for (
        std::size_t sort_index = 0, end = m_polygon_id_sorted.size();
        sort_index < end;
        ++sort_index
    ) {
        const Polygon_id polygon_id = m_polygon_id_sorted[sort_index];
        if (m_polygon_id_remove[polygon_id]) {
            continue;
        }
        const Polygon& polygon = source.polygons[polygon_id];
        for (uint32_t corner_index = 0; corner_index < polygon.corner_count; ++corner_index)
        {
            const Corner_id corner_id = source.polygon_corners[polygon.first_polygon_corner_id + corner_index];
            const Corner&   corner    = source.corners        [corner_id];
            const Point_id  point_id0 = corner.point_id;
            const Point_id  point_id  = m_point_id_merge_candidates[point_id0];
            m_point_id_used[point_id] = true;
        }
    }

    //// std::stringstream ss;
    //// for (std::size_t i = 0; i < m_point_id_used.size(); ++i) {
    ////     if (m_point_id_used[i]){
    ////         ss << " " << i;
    ////     }
    //// }
    //// log_merge->info("Used points:{}", ss.str());
}

void Weld::count_used_points()
{
    m_used_point_count = 0;
    for (const bool point_used : m_point_id_used) {
        if (point_used) {
            ++m_used_point_count;
        }
    }
    //// log_merge->info("Used point count = {}", m_used_point_count);
}

Weld::Weld(
    Geometry& source,
    Geometry& destination
)
    : Geometry_operation{source, destination}
{
    ERHE_PROFILE_FUNCTION();

    const uint32_t point_count   = source.get_point_count();
    const uint32_t polygon_count = source.get_polygon_count();
    m_point_id_merge_candidates.resize(point_count);
    m_point_id_used            .resize(point_count);
    m_polygon_id_sorted        .resize(polygon_count);
    m_polygon_id_remove        .resize(polygon_count);
    std::iota(m_point_id_merge_candidates.begin(), m_point_id_merge_candidates.end(), Point_id{0});
    std::iota(m_polygon_id_sorted.begin(), m_polygon_id_sorted.end(), Polygon_id{0});

    m_max_distance = 0.005f; // 5mm

    find_point_merge_candidates         ();
    rotate_polygons_to_least_point_first();
    sort_polygons                       ();
    scan_for_equal_and_opposite_polygons();
    mark_used_points                    ();
    count_used_points                   ();

    // Copy used points
    point_old_to_new.reserve(m_used_point_count);
    source.for_each_point_const([&](auto& i) {
        if (m_point_id_used[i.point_id]) {
            make_new_point_from_point(i.point_id);
        }
    });

    // Copy used polygons
    source.for_each_polygon_const([&](auto& i) {
        if (m_polygon_id_remove[i.polygon_id]) {
            return;
        }

        const Polygon_id new_polygon_id = make_new_polygon_from_polygon(i.polygon_id);

        i.polygon.for_each_corner_const(source, [&](auto& j) {
            const Point_id  old_point_id0 = j.corner.point_id;
            const Point_id  old_point_id  = m_point_id_merge_candidates[old_point_id0];
            const Point_id  new_point_id  = point_old_to_new[old_point_id];
            const Corner_id new_corner_id = destination.make_polygon_corner(new_polygon_id, new_point_id);
            add_corner_source(new_corner_id, 1.0f, j.corner_id);
        });
    });

    post_processing();
}

auto weld(Geometry& source) -> Geometry
{
    return Geometry(
        fmt::format("weld({})", source.name),
        [&source](auto& result) {
            Weld operation{source, result};
        }
    );
}


} // namespace erhe::geometry::operation
