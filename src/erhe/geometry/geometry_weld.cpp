// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/geometry_log.hpp"
#include "erhe/geometry/remapper.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/glm.hpp>


namespace erhe::geometry
{

using glm::vec2;
using glm::vec3;
using glm::vec4;

const char* c_str(const vec3::length_type axis)
{
    switch (axis)
    {
        case vec3::length_type{0}: return "X";
        case vec3::length_type{1}: return "Y";
        case vec3::length_type{2}: return "Z";
        default: return "?";
    }
}

void Geometry::trace_points(spdlog::logger& logger) const
{
#if SPDLOG_ACTIVE_LEVEL > SPDLOG_LEVEL_TRACE
    static_cast<void>(logger);
#else
    auto* point_locations = point_attributes().find<vec3>(c_point_locations);
    if (point_locations == nullptr)
    {
        return;
    }
    for (Point_id point_id = 0; point_id < m_next_point_id; ++point_id)
    {
        vec3 position;
        if (point_locations->maybe_get(point_id, position))
        {
            logger.trace("    {:2}: {}", point_id, position);
        }
    }
#endif
}

void Geometry::compute_bounding_box(vec3& min_corner, vec3& max_corner) const
{
    min_corner = vec3{std::numeric_limits<float>::max(),    std::numeric_limits<float>::max(),    std::numeric_limits<float>::max()};
    max_corner = vec3{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    auto* point_locations = point_attributes().find<vec3>(c_point_locations);
    if (point_locations == nullptr)
    {
        return;
    }
    for (Point_id point_id = 0; point_id < m_next_point_id; ++point_id)
    {
        vec3 position;
        if (point_locations->maybe_get(point_id, position))
        {
            min_corner = glm::min(min_corner, position);
            max_corner = glm::max(max_corner, position);
        }
    }
}

class Point_attribute_maps
{
public:
    Property_map<Point_id, vec3>* locations {nullptr};
    Property_map<Point_id, vec3>* normals   {nullptr};
    Property_map<Point_id, vec3>* tangents  {nullptr};
    Property_map<Point_id, vec3>* bitangents{nullptr};
    Property_map<Point_id, vec2>* texcoords {nullptr};
};

class Point_data
{
public:
    Point_data(
        const Point_id              old_id,
        const Point_attribute_maps& attribute_maps
    )
    {
        if (
            (attribute_maps.locations != nullptr) &&
            attribute_maps.locations->has(old_id)
        )
        {
            position = attribute_maps.locations->get(old_id);
        }
        //// if ((attribute_maps.normals    != nullptr) && attribute_maps.normals   ->has(old_id)) normal    = attribute_maps.normals   ->get(old_id);
        //// if ((attribute_maps.tangents   != nullptr) && attribute_maps.tangents  ->has(old_id)) tangent   = attribute_maps.tangents  ->get(old_id);
        //// if ((attribute_maps.bitangents != nullptr) && attribute_maps.bitangents->has(old_id)) bitangent = attribute_maps.bitangents->get(old_id);
        //// if ((attribute_maps.texcoords  != nullptr) && attribute_maps.texcoords ->has(old_id)) texcoord  = attribute_maps.texcoords ->get(old_id);
    }
    std::optional<vec3> position;
    //// std::optional<vec3> normal;
    //// std::optional<vec3> tangent;
    //// std::optional<vec3> bitangent;
    //// std::optional<vec2> texcoord;
};

auto get_sorting_axises(const vec3& min_corner, const vec3& max_corner) -> std::array<glm::length_t, 3>
{
    const vec3 bounding_box_size0 = max_corner - min_corner;
    const auto axis0 = erhe::toolkit::max_axis_index(bounding_box_size0);
    vec3       bounding_box_size1 = bounding_box_size0;
    bounding_box_size1[axis0] = 0.0f;
    const auto axis1 = erhe::toolkit::max_axis_index(bounding_box_size1);
    vec3       bounding_box_size2 = bounding_box_size1;
    bounding_box_size2[axis1] = 0.0f;
    const auto axis2 = erhe::toolkit::max_axis_index(bounding_box_size2);
    SPDLOG_LOGGER_TRACE(log_weld, "Primary   axis = {} {}", axis0, c_str(axis0));
    SPDLOG_LOGGER_TRACE(log_weld, "Secondary axis = {} {}", axis1, c_str(axis1));
    SPDLOG_LOGGER_TRACE(log_weld, "Tertiary  axis = {} {}", axis2, c_str(axis2));

    return std::array<glm::length_t, 3>{axis0, axis1, axis2};
}

void sort_points_by_location(
    Geometry&                           geometry,
    Remapper<Point_id>&                 point_remapper,
    const Point_attribute_maps&         point_attribute_maps
)
{
    log_weld->trace("\n\nSTEP 1\n\n");

    geometry.trace_points(*log_weld.get());
    vec3 min_corner;
    vec3 max_corner;
    geometry.compute_bounding_box(min_corner, max_corner);

    const auto sorting_axises = get_sorting_axises(min_corner, max_corner);

    //// geometry.debug_trace();
    //// geometry.sanity_check();

    std::sort(
        point_remapper.old_from_new.begin(),
        point_remapper.old_from_new.end(),
        [sorting_axises, point_attribute_maps]
        (const Point_id& lhs, const Point_id& rhs)
        {
            if (
                !point_attribute_maps.locations->has(lhs) ||
                !point_attribute_maps.locations->has(rhs)
            )
            {
                return false;
            }
            const vec3 position_lhs = point_attribute_maps.locations->get(lhs);
            const vec3 position_rhs = point_attribute_maps.locations->get(rhs);
            if (position_lhs[sorting_axises[0]] != position_rhs[sorting_axises[0]])
            {
                return position_lhs[sorting_axises[0]] < position_rhs[sorting_axises[0]];
            }
            if (position_lhs[sorting_axises[1]] != position_rhs[sorting_axises[1]])
            {
                return position_lhs[sorting_axises[1]] < position_rhs[sorting_axises[1]];
            }
            return position_lhs[sorting_axises[2]] < position_rhs[sorting_axises[2]];
        }
    );

    log_weld->trace("Points after sort:");
    for (Point_id new_point_id = 0; new_point_id < geometry.m_next_point_id; ++new_point_id)
    {
        Point_id old_point_id = point_remapper.old_from_new[new_point_id];
        if (!point_attribute_maps.locations->has(old_point_id))
        {
            continue;
        }
        vec3 position = point_attribute_maps.locations->get(old_point_id);
        log_weld->trace("    new {:2} old {:2}: {}", new_point_id, old_point_id, position);
    }
    point_remapper.create_new_from_old_mapping();

    log_weld->trace("Point remapping after sort, before merging:");
    point_remapper.dump();
}

void merge_sorted_points(
    Geometry&                      geometry,
    Remapper<Point_id>&            point_remapper,
    const Point_attribute_maps&    point_attribute_maps,
    const Geometry::Weld_settings& weld_settings
)
{
    log_weld->trace("\n\nSTEP 2\n\n");

    //// geometry.debug_trace();

    //  - Scanning is done with a sliding window
    //  - The window start position is called primary
    //  - The window end position is called secondary
    //  - Primary (window start) is initialized to first point (after sorting)
    //  - Secondary (window end) is initialized to window start
    //  - Secondary (window end) moves forward as long as point locations are within merge threshold
    //  - When secondary (window end) point is different from primary (window start),
    //    primary (window start) is advanced by one
    for (Point_id primary_new_id = 0; primary_new_id < point_remapper.new_size; ++primary_new_id)
    {
        const Point_id primary_old_id = point_remapper.old_id(primary_new_id);

        if (point_remapper.merge.find_secondary(primary_new_id) != nullptr)
        {
            log_weld->trace(
                "Span / Primary new point {} old point {} has already been removed/merged",
                primary_new_id, primary_old_id
            );
            continue;
        }

        const Point_data primary_attributes{primary_old_id, point_attribute_maps};
        if (!primary_attributes.position.has_value())
        {
            continue;
        }
        log_weld->trace(
            "Span / Primay new point {:2} old point {:2} position {}",
            primary_new_id,
            point_remapper.old_from_new[primary_new_id], primary_attributes.position.value()
        );

        for (
            Point_id secondary_new_id = primary_new_id + 1;
            secondary_new_id < point_remapper.new_size;
            ++secondary_new_id
        )
        {
            const Point_id secondary_old_id = point_remapper.old_id(secondary_new_id);
            if (point_remapper.merge.find_secondary(secondary_new_id) != nullptr)
            {
                continue;
            }
            const Point_data secondary_attributes{secondary_old_id, point_attribute_maps};
            if (!secondary_attributes.position.has_value())
            {
                continue;
            }
            const float distance = glm::distance(primary_attributes.position.value(), secondary_attributes.position.value());
            if (distance > weld_settings.max_point_distance)
            {
                continue;
            }

            log_weld->trace(
                "merging drop new point {:2} drop old point {:2} to keep new point {:2} keep old point {:2}",
                secondary_new_id, secondary_old_id, primary_new_id, primary_old_id
            );

            log_weld->trace(
                "position keep {} - drop{}",
                primary_attributes  .position.value(),
                secondary_attributes.position.value()
            );

            point_remapper.merge.insert(primary_new_id, secondary_new_id);
        }
    }

    log_weld->trace("Merged {} points", point_remapper.merge.size());
    log_weld->trace("Point remapping after merge, before removing duplicate points:");
    //// geometry.debug_trace();
    point_remapper.dump();

    point_remapper.reorder_to_drop_merge_duplicates_and_elimitated();
    point_remapper.update_secondary_new_from_old();
    point_remapper.trim();
    geometry.m_next_point_id = point_remapper.new_size;

    log_weld->trace("Points after trim:");
    //// geometry.debug_trace();
    point_remapper.dump();

    // Remap points
    log_weld->trace("Remap points");
    geometry.reallocate_point_corners(point_remapper);

    // Remap corner points
    // For each corner, replace old point ids with new point ids
    log_weld->trace("Remap corner points");
    for (Corner_id corner_id = 0, end = geometry.get_corner_count(); corner_id < end; ++corner_id)
    {
        Corner& corner = geometry.corners[corner_id];
        const Point_id old_point_id = corner.point_id;
        ERHE_VERIFY(old_point_id != std::numeric_limits<Point_id>::max());
        const Point_id new_point_id = point_remapper.new_id(old_point_id);
        corner.point_id = new_point_id;
        log_weld->trace("Corner {:2} point {:2} -> {:2}", corner_id, old_point_id, new_point_id);
    }

    geometry.point_attributes().remap_keys(point_remapper.old_from_new);
    geometry.point_attributes().trim(geometry.get_point_count());
}

void sort_polygons(Geometry& geometry, Remapper<Polygon_id>& polygon_remapper)
{
    log_weld->trace("\n\nSTEP 4\n\n");
    log_weld->trace("Sort polygons based on first corner (least) point id");

    //// geometry.debug_trace();

    std::sort(
        polygon_remapper.old_from_new.begin(),
        polygon_remapper.old_from_new.end(),
        [&geometry](
            const Polygon_id& lhs,
            const Polygon_id& rhs
        )
        {
            Polygon&      lhs_polygon         = geometry.polygons       [lhs];
            Corner_id     lhs_first_corner_id = geometry.polygon_corners[lhs_polygon.first_polygon_corner_id];
            const Corner& lhs_first_corner    = geometry.corners        [lhs_first_corner_id];
            Point_id      lhs_point_id        = lhs_first_corner.point_id;

            Polygon&      rhs_polygon         = geometry.polygons       [rhs];
            Corner_id     rhs_first_corner_id = geometry.polygon_corners[rhs_polygon.first_polygon_corner_id];
            const Corner& rhs_first_corner    = geometry.corners        [rhs_first_corner_id];
            Point_id      rhs_point_id        = rhs_first_corner.point_id;

            return lhs_point_id < rhs_point_id;
        }
    );

    polygon_remapper.create_new_from_old_mapping();

    log_weld->trace("Polygon remapping before merging / eliminating:");
    polygon_remapper.dump();
}

void Geometry::reallocate_point_corners(Remapper<Point_id>& point_remapper)
{
    auto old_points        = points;        // copy intended
    auto old_point_corners = point_corners; // copy intended

    uint32_t next_point_corner = 0;
    for (
        Point_id new_point_id = 0, new_point_end = get_point_count();
        new_point_id < new_point_end;
        ++new_point_id
    )
    {
        const Point_id old_point_id = point_remapper.old_id(new_point_id);
        const Point&   old_point    = old_points[old_point_id];
              Point&   new_point    = points    [new_point_id];
        points[new_point_id].first_point_corner_id = next_point_corner;
        points[new_point_id].corner_count          = 0;

        std::stringstream ss;
        for (uint32_t i = 0, i_end = old_point.corner_count; i < i_end; ++i)
        {
            const Point_corner_id old_point_corner_id = old_point.first_point_corner_id + i;
            const Point_corner_id new_point_corner_id = new_point.first_point_corner_id + new_point.corner_count;
            ERHE_VERIFY(new_point_corner_id == next_point_corner);
            const Corner_id old_corner_id = old_point_corners[old_point_corner_id];
            const Corner&   corner        = corners[old_corner_id];
            if (corner.polygon_id >= m_next_polygon_id)
            {
                ss << fmt::format(" !{:2}", old_corner_id);
                continue;
            }
            point_corners[new_point_corner_id] = old_corner_id;
            ++new_point.corner_count;
            ++new_point.reserved_corner_count;
            ++next_point_corner;
            ss << fmt::format("  {:2}", old_corner_id);
        }
        SPDLOG_LOGGER_TRACE(
            log_weld,
            "Point new {:2} old {:2} corners:{}",
            new_point_id,
            old_point_id,
            ss.str()
        );

        // For each new point that is mapped to `new_point_id`,
        // create new point corners that point to the original corners.

        // Corners which point at removed polygons are dropped here
        point_remapper.for_each_primary_new(
            new_point_id,
            [this, new_point_id, &old_points, &old_point_corners, &next_point_corner]
            (
                const Point_id primary_new_id,
                const Point_id primary_old_id,
                const Point_id /*secondary_new_id*/,
                const Point_id secondary_old_id
            )
            {
                ERHE_VERIFY(new_point_id == primary_new_id);
                      Point& primary_new   = points    [primary_new_id  ];
                const Point& secondary_old = old_points[secondary_old_id];
                std::stringstream ss;
                for (uint32_t i = 0, end = secondary_old.corner_count; i < end; ++i)
                {
                    const Point_corner_id old_point_corner_id = secondary_old.first_point_corner_id + i;
                    const Point_corner_id new_point_corner_id = primary_new.first_point_corner_id + primary_new.corner_count;
                    const Corner_id       corner_id           = old_point_corners[old_point_corner_id];
                    const Corner&         corner              = corners[corner_id];
                    if (corner.polygon_id >= m_next_polygon_id)
                    {
                        ss << fmt::format(" !{:2}", corner_id);
                        continue;
                    }

                    point_corners[new_point_corner_id] = corner_id;
                    ++primary_new.corner_count;
                    ++next_point_corner;
                    ss << fmt::format("  {:2}", corner_id);
                }
                log_weld->trace(
                    "Point new {:2} from old {:2} corners:{}",
                    new_point_id,
                    primary_old_id,
                    ss.str()
                );
            }
        );
    }
    m_next_point_corner_reserve = next_point_corner;
    point_corners.resize(m_next_point_corner_reserve);
}

// Does *not* update corners
void Geometry::reallocate_point_corners()
{
    //// const auto begin_corner_count = m_next_corner_id;
    ////
    //// Remapper<Corner_id> corner_remapper{m_next_corner_id};

    auto old_points        = points;        // copy intended
    auto old_point_corners = point_corners; // copy intended

    uint32_t next_point_corner = 0;
    for (Point_id point_id = 0, point_end = get_point_count();
        point_id < point_end;
        ++point_id
    )
    {
        Point& old_point = old_points[point_id];
        Point& new_point = points    [point_id];
        points[point_id].first_point_corner_id = next_point_corner;
        points[point_id].corner_count          = 0;

        std::stringstream ss;
        for (uint32_t i = 0, i_end = old_point.corner_count; i < i_end; ++i)
        {
            const Point_corner_id old_point_corner_id = old_point.first_point_corner_id + i;
            const Point_corner_id new_point_corner_id = new_point.first_point_corner_id + new_point.corner_count;
            ERHE_VERIFY(new_point_corner_id == next_point_corner);
            const Corner_id corner_id = old_point_corners[old_point_corner_id];
            const Corner&   corner    = corners[corner_id];
            if (corner.polygon_id >= m_next_polygon_id)
            {
                //// corner_remapper.eliminate.push_back(corner_id);
                // Drop corner from point, because it is pointing to removed polygon
                ss << fmt::format(" !{:2}", corner_id);
                continue;
            }
            point_corners[new_point_corner_id] = corner_id;
            ++new_point.corner_count;
            ++new_point.reserved_corner_count;
            ++next_point_corner;
            ss << fmt::format("  {:2}", corner_id);
        }
        log_weld->trace(
            "Point {:2} corners:{}",
            point_id,
            ss.str()
        );
    }
    m_next_point_corner_reserve = next_point_corner;
    point_corners.resize(m_next_point_corner_reserve);

    //// corner_remapper.reorder_to_drop_merge_duplicates_and_elimitated();
    //// corner_remapper.update_secondary_new_from_old();
    //// corner_remapper.trim();
    //// m_next_corner_id = corner_remapper.new_size;
    ////
    //// const auto end_corner_count = m_next_corner_id;
    ////
    //// log_weld->trace("Corners before = {}, after = {}", begin_corner_count, end_corner_count);
}

void Geometry::rotate_polygons_to_least_point_first()
{
    log_weld->trace("Rotating polygon corners so that first corner points to least Point_id");

    for (
        Polygon_id polygon_id = 0;
        polygon_id < m_next_polygon_id;
        ++polygon_id
    )
    {
        Polygon&      polygon         = polygons[polygon_id];
        Corner_id     first_corner_id = polygon_corners[polygon.first_polygon_corner_id];
        const Corner& first_corner    = corners[first_corner_id];
        Point_id      min_point_id    = first_corner.point_id;
        uint32_t      min_point_slot  = 0;
        std::vector<Corner_id> copy_of_polygon_corners;

        // Find corner with smallest Point_id
        {
            std::stringstream ss;
            for (uint32_t i = 0; i < polygon.corner_count; ++i)
            {
                Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                const Corner_id   corner_id         = polygon_corners[polygon_corner_id];
                const Corner&     corner            = corners[corner_id];
                //ss << fmt::format(" c: {} p: {}", corner_id, corner.point_id);
                ss << fmt::format(" {}", corner.point_id);
                copy_of_polygon_corners.push_back(corner_id);
                if (corner.point_id < min_point_id)
                {
                    min_point_id = corner.point_id;
                    min_point_slot = i;
                }
            }

            log_weld->trace("Polygon {} original points:{}", polygon_id, ss.str());
        }

        // Rotate corners of polygon
        {
            std::stringstream ss;
            for (uint32_t i = 0; i < polygon.corner_count; ++i)
            {
                Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                const Corner_id   corner_id         = copy_of_polygon_corners.at((i + min_point_slot) % polygon.corner_count);
                const Corner&     corner            = corners[corner_id];
                //ss << fmt::format(" c: {} p : {}", corner_id, corner.point_id);
                ss << fmt::format(" {}", corner.point_id);
                polygon_corners[polygon_corner_id] = corner_id;
            }
            log_weld->trace("Polygon {} rotated points:{}", polygon_id, ss.str());
        }
    }
}

void scan_for_equal_and_opposite_polygons(
    Geometry&             geometry,
    Remapper<Polygon_id>& polygon_remapper,
    Remapper<Corner_id>&  corner_remapper
)
{
    log_weld->trace("\n\nSTEP 5\n\n");
    //// geometry.debug_trace();
    for (
        Polygon_id primary_new_id = 0;
        primary_new_id < geometry.m_next_polygon_id;
        ++primary_new_id
    )
    {
        const Polygon_id primary_old_id = polygon_remapper.old_id(primary_new_id);
        if (polygon_remapper.merge.find_secondary(primary_new_id) != nullptr)
        {
            continue;
        }
        Polygon& primary_polygon = geometry.polygons[primary_old_id];
        std::stringstream ssp1;
        for (uint32_t i = 0; i < primary_polygon.corner_count; ++i)
        {
            Polygon_corner_id polygon_corner_id = primary_polygon.first_polygon_corner_id + i;
            const Corner_id   corner_id         = geometry.polygon_corners[polygon_corner_id];
            const Corner&     corner            = geometry.corners[corner_id];
            const Point_id    point_id          = corner.point_id;
            ssp1 << fmt::format(" {}", point_id);
        }

        log_weld->trace(
            "primary   polygon new {:2} old {:2} points:{}",
            primary_new_id,
            primary_old_id,
            ssp1.str()
        );

        Corner_id      primary_first_corner_id = geometry.polygon_corners[primary_polygon.first_polygon_corner_id];
        const Corner&  primary_first_corner    = geometry.corners        [primary_first_corner_id];
        const Point_id primary_first_point_id  = primary_first_corner.point_id;

        for (
            Polygon_id secondary_new_id = primary_new_id + 1;
            secondary_new_id < geometry.m_next_polygon_id;
            ++secondary_new_id
        )
        {
            if (polygon_remapper.merge.find_secondary(secondary_new_id) != nullptr)
            {
                continue;
            }
            const Polygon_id secondary_old_id = polygon_remapper.old_id(secondary_new_id);
            Polygon& secondary_polygon = geometry.polygons[secondary_old_id];

            std::stringstream ssp2;
            for (uint32_t i = 0; i < secondary_polygon.corner_count; ++i)
            {
                Polygon_corner_id polygon_corner_id = secondary_polygon.first_polygon_corner_id + i;
                const Corner_id   corner_id         = geometry.polygon_corners[polygon_corner_id];
                const Corner&     corner            = geometry.corners[corner_id];
                const Point_id    point_id          = corner.point_id;
                ssp2 << fmt::format(" {}", point_id);
            }

            log_weld->trace(
                "secondary polygon new {:2} old {:2} points:{}",
                secondary_new_id,
                secondary_old_id,
                ssp2.str()
            );

            const Corner_id secondary_first_corner_id = geometry.polygon_corners[secondary_polygon.first_polygon_corner_id];
            const Corner&   secondary_first_corner    = geometry.corners        [secondary_first_corner_id];
            const Point_id  secondary_first_point_id  = secondary_first_corner.point_id;

            if (primary_first_point_id != secondary_first_point_id)
            {
                // log_weld->trace("end of span");
                break;
            }

            if (primary_polygon.corner_count != secondary_polygon.corner_count)
            {
                // not equal and not opposite
                // log_weld->trace("corner count mismatch");
                continue;
            }

            const uint32_t corner_count = primary_polygon.corner_count;

            bool polygons_are_equal    = (corner_count >= 1);
            bool polygons_are_opposite = (corner_count >= 3);

            for (uint32_t i = 0; i < corner_count; ++i)
            {
                const Corner_id primary_corner_id = geometry.polygon_corners[primary_polygon.first_polygon_corner_id + i];
                const Corner&   primary_corner    = geometry.corners        [primary_corner_id];
                const Point_id  primary_point_id  = primary_corner.point_id;

                {
                    const Corner_id secondary_corner_id = geometry.polygon_corners[secondary_polygon.first_polygon_corner_id + i];
                    const Corner&   secondary_corner    = geometry.corners        [secondary_corner_id];
                    const Point_id  secondary_point_id  = secondary_corner.point_id;

                    if (primary_point_id != secondary_point_id)
                    {
                        polygons_are_equal = false;
                    }
                }
                {
                    const uint32_t  j                   = (corner_count - i) % corner_count;
                    const Corner_id secondary_corner_id = geometry.polygon_corners[secondary_polygon.first_polygon_corner_id + j];
                    const Corner&   secondary_corner    = geometry.corners        [secondary_corner_id];
                    const Point_id  secondary_point_id  = secondary_corner.point_id;

                    if (primary_point_id != secondary_point_id)
                    {
                        polygons_are_opposite = false;
                    }
                }
            }

            ERHE_VERIFY(!polygons_are_equal || !polygons_are_opposite); // Should not be able to be both

            // Merge case: identical polygons
            if (polygons_are_equal)
            {
                SPDLOG_LOGGER_TRACE(
                    log_weld,
                    "merging new polygon {:2} old polygon {:2} to new polygon {:2} old polygon {:2}",
                    secondary_new_id,
                    secondary_old_id,
                    primary_new_id,
                    primary_old_id
                );
                polygon_remapper.merge.insert(primary_new_id, secondary_new_id);
            }

            // Elimination case: opposite polygons
            if (polygons_are_opposite)
            {
                polygon_remapper.eliminate.push_back(primary_new_id);
                polygon_remapper.eliminate.push_back(secondary_new_id);
                std::stringstream ssc1;
                std::stringstream ssc2;
                for (uint32_t i = 0; i < corner_count; ++i)
                {
                    const Polygon_corner_id primary_polygon_corner_id   = primary_polygon.first_polygon_corner_id + i;
                    const Polygon_corner_id secondary_polygon_corner_id = secondary_polygon.first_polygon_corner_id + i;
                    const Corner_id         primary_corner_id           = geometry.polygon_corners[primary_polygon_corner_id];
                    const Corner_id         secondary_corner_id         = geometry.polygon_corners[secondary_polygon_corner_id];
                    corner_remapper.eliminate.push_back(primary_corner_id);
                    corner_remapper.eliminate.push_back(secondary_corner_id);
                    ssc1 << fmt::format(" {}", primary_corner_id);
                    ssc2 << fmt::format(" {}", secondary_corner_id);
                }
                log_weld->trace(
                    "elim. primary polygon new {:2} old {:2} corners:{}",
                    primary_new_id,
                    primary_old_id,
                    ssc1.str()
                );
                log_weld->trace(
                    "elim. secondary polygon new {:2} old {:2} corners:{}",
                    secondary_new_id,
                    secondary_old_id,
                    ssc2.str()
                );
            }
        }
    }
}

void drop_dead_polygons(
    Geometry&             geometry,
    Remapper<Polygon_id>& polygon_remapper
)
{
    log_weld->trace("\n\nSTEP 6\n\n");

    //// geometry.debug_trace();

    auto old_polygons = geometry.polygons; // intentional copy

    log_weld->trace("Merged {} polygons", polygon_remapper.merge.size());
    log_weld->trace("Polygon remapping before trim prepare:");
    polygon_remapper.dump();
    polygon_remapper.reorder_to_drop_merge_duplicates_and_elimitated();
    polygon_remapper.update_secondary_new_from_old();

    polygon_remapper.trim(
        [&geometry](const Point_id new_id, const Point_id old_id)
        {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            const Polygon& polygon = geometry.polygons[old_id];
            std::stringstream ss;
            for (
                Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
                end = polygon.first_polygon_corner_id + polygon.corner_count;
                polygon_corner_id < end;
                ++polygon_corner_id)
            {
                const Corner_id corner_id = geometry.polygon_corners[polygon_corner_id];
                ss << fmt::format(" {:2}", corner_id);
            }
            log_weld->trace("Dropping polygon new {:2} old {:2} corners:{}", new_id, old_id, ss.str());
#else
            static_cast<void>(new_id);
            static_cast<void>(old_id);
#endif
        }
    );
    geometry.m_next_polygon_id = polygon_remapper.new_size;

    log_weld->trace("Polygon remapping after trim prepare:");
    polygon_remapper.dump();

    // Remap polygons
    auto old_polygon_corners = geometry.polygon_corners; // intentional copy
    uint32_t next_polygon_corner = 0;
    for (
        Polygon_id new_polygon_id = 0, new_polygon_id_end = geometry.get_polygon_count();
        new_polygon_id < new_polygon_id_end;
        ++new_polygon_id
    )
    {
        const Polygon_id old_polygon_id = polygon_remapper.old_id(new_polygon_id);

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        std::stringstream ss;
        ss << fmt::format("Polygon new {:2} from old {:2} corners:", new_polygon_id, old_polygon_id);
#endif
        Polygon& old_polygon = old_polygons[old_polygon_id];
        Polygon& new_polygon = geometry.polygons[new_polygon_id];
        geometry.polygons[new_polygon_id].first_polygon_corner_id = next_polygon_corner;
        geometry.polygons[new_polygon_id].corner_count            = 0;
        for (uint32_t i = 0, end = old_polygon.corner_count; i < end; ++i)
        {
            const Polygon_corner_id old_polygon_corner_id = old_polygon.first_polygon_corner_id + i;
            const Polygon_corner_id new_polygon_corner_id = new_polygon.first_polygon_corner_id + new_polygon.corner_count;
            const Corner_id         corner_id             = old_polygon_corners[old_polygon_corner_id];
            geometry.polygon_corners[new_polygon_corner_id] = corner_id;
            ++new_polygon.corner_count;
            ++next_polygon_corner;
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            ss << fmt::format(" {}", corner_id);
#endif
        }
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        ss << " - ";
#endif
        polygon_remapper.for_each_primary_new(
            new_polygon_id,
            [
                &geometry,
                new_polygon_id,
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
                &ss,
#endif
                &old_polygons,
                &old_polygon_corners,
                &next_polygon_corner
            ]
            (
                Polygon_id primary_new_id,
                Polygon_id /*primary_old_id*/,
                Polygon_id /*secondary_new_id*/,
                Polygon_id secondary_old_id
            )
            {
                ERHE_VERIFY(new_polygon_id == primary_new_id);
                Polygon& primary_new   = geometry.polygons    [primary_new_id];
                Polygon& secondary_old = old_polygons[secondary_old_id];
                ERHE_VERIFY(secondary_old.corner_count > 0);
                for (uint32_t i = 0, end = secondary_old.corner_count; i < end; ++i)
                {
                    const Polygon_corner_id old_polygon_corner_id = secondary_old.first_polygon_corner_id + i;
                    const Polygon_corner_id new_polygon_corner_id = primary_new.first_polygon_corner_id + primary_new.corner_count;
                    ERHE_VERIFY(new_polygon_corner_id == next_polygon_corner);
                    const Corner_id corner_id = old_polygon_corners[old_polygon_corner_id];
                    geometry.polygon_corners[new_polygon_corner_id] = corner_id;
                    ++primary_new.corner_count;
                    ++next_polygon_corner;
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
                    ss << fmt::format(" {}", corner_id);
#endif
                }
            });
        SPDLOG_LOGGER_TRACE(log_weld, "{}", ss.str());
    }

    // Remap corners
    for (Corner_id corner_id = 0, end = geometry.m_next_corner_id; corner_id < end; ++corner_id)
    {
        Corner& corner = geometry.corners[corner_id];
        const Polygon_id old_polygon_id = corner.polygon_id;
        const Polygon_id new_polygon_id = polygon_remapper.new_id(old_polygon_id);
        log_weld->trace(
            "Corner {} point = {} polygon old {} new {}",
            corner_id,
            corner.point_id,
            old_polygon_id,
            new_polygon_id
        );
        corner.polygon_id = new_polygon_id;
    }

    geometry.m_next_polygon_corner_id = next_polygon_corner;
    geometry.polygon_corners.resize(geometry.m_next_polygon_corner_id);

    geometry.polygon_attributes().remap_keys(polygon_remapper.old_from_new);
    geometry.polygon_attributes().trim(geometry.get_polygon_count());
}

void drop_dead_corners_and_remap_corners(
    Geometry&            geometry,
    Remapper<Corner_id>& corner_remapper
)
{
    log_weld->trace("\n\nSTEP 8\n\n");
    //// geometry.debug_trace();

    auto old_corners = geometry.corners; // intentional copy

    corner_remapper.create_new_from_old_mapping();
    log_weld->trace("drop_dead_corners before prep:");
    corner_remapper.dump();
    corner_remapper.reorder_to_drop_merge_duplicates_and_elimitated();
    corner_remapper.update_secondary_new_from_old();
    corner_remapper.trim();
    log_weld->trace("drop_dead_corners after prep:");
    corner_remapper.dump();

    // Remap corners
    // For each corner, replace old point ids with new point ids
    log_weld->trace("Remap corner points");
    for (Corner_id new_corner_id = 0, end = corner_remapper.new_size; new_corner_id < end; ++new_corner_id)
    {
        Corner& new_corner = geometry.corners[new_corner_id];
        const Corner_id old_corner_id = corner_remapper.old_id(new_corner_id);
        const Corner&   old_corner    = old_corners[old_corner_id];
        log_weld->trace(
            "Corner new {} old {} point = {} polygon = {}",
            new_corner_id,
            old_corner_id,
            old_corner.point_id,
            old_corner.polygon_id
        );
        new_corner = old_corner;
    }

    log_weld->trace("old corner count = {}, new corner count = {}", geometry.m_next_corner_id, corner_remapper.new_size);
    geometry.m_next_corner_id = corner_remapper.new_size;
}

void Geometry::remap_corners(Remapper<Corner_id>& corner_remapper)
{
    log_weld->trace("Corner renaming:");

    // Rewrire corners
    auto old_corners = corners; // copy intended
    for (
        Corner_id new_corner_id = 0, end = m_next_corner_id;
        new_corner_id < end;
        ++new_corner_id
    )
    {
        const Corner_id old_corner_id = corner_remapper.old_id(new_corner_id);
        log_weld->trace("Corner new {:2} from old {:2}", new_corner_id, old_corner_id);
        corners[new_corner_id] = old_corners[old_corner_id];
    }

    corner_attributes().remap_keys(corner_remapper.old_from_new);
    corner_attributes().trim(get_corner_count());

    // Replace corners in all points
    for (
        Point_id point_id = 0, point_id_end = get_point_count();
        point_id < point_id_end;
        ++point_id
    )
    {
        const Point& point = points[point_id];
        std::stringstream ss;
        for (
            Point_corner_id point_corner_id = point.first_point_corner_id,
            end = point.first_point_corner_id + point.corner_count;
            point_corner_id < end;
            ++point_corner_id
        )
        {
            const Corner_id old_corner_id = point_corners[point_corner_id];
            const Corner_id new_corner_id = corner_remapper.new_id(old_corner_id);
            ss << fmt::format(" {:2} -> {:2}", old_corner_id, new_corner_id);
            point_corners[point_corner_id] = new_corner_id;
        }
        log_weld->trace("Point {:2} corners:{}", point_id, ss.str());
    }

    // Replace corners in all polygons
    for (
        Polygon_id polygon_id = 0, polygon_id_end = get_polygon_count();
        polygon_id < polygon_id_end;
        ++polygon_id
    )
    {
        const Polygon& polygon = polygons[polygon_id];
        std::stringstream ss;
        for (
            Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
            end = polygon.first_polygon_corner_id + polygon.corner_count;
            polygon_corner_id < end;
            ++polygon_corner_id
        )
        {
            const Corner_id old_corner_id = polygon_corners[polygon_corner_id];
            const Corner_id new_corner_id = corner_remapper.new_id(old_corner_id);
            ss << fmt::format(" {:2} -> {:2}", old_corner_id, new_corner_id);
            polygon_corners[polygon_corner_id] = new_corner_id;
        }
        log_weld->trace("Polygon {:2} corners:{}", polygon_id, ss.str());
    }

    log_weld->trace("after corner renaming:");
    //// debug_trace();
}

void Geometry::weld(const Weld_settings& weld_settings)
{
    static_cast<void>(weld_settings);
    ERHE_PROFILE_FUNCTION

    const Point_attribute_maps point_attribute_maps
    {
        .locations  = point_attributes().find<vec3>(c_point_locations),
        .normals    = point_attributes().find<vec3>(c_point_normals),
        .tangents   = point_attributes().find<vec3>(c_point_tangents),
        .bitangents = point_attributes().find<vec3>(c_point_bitangents),
        .texcoords  = point_attributes().find<vec2>(c_point_texcoords)
    };

    SPDLOG_LOGGER_TRACE(log_weld, "Points before sort:");

    Remapper<Point_id>   point_remapper  {m_next_point_id  };
    Remapper<Polygon_id> polygon_remapper{m_next_polygon_id};
    Remapper<Corner_id>  corner_remapper {m_next_corner_id };

    sort_points_by_location(*this, point_remapper, point_attribute_maps);                // STEP 1 updates point remapper
    merge_sorted_points    (*this, point_remapper, point_attribute_maps, weld_settings); // STEP 2 updates point remapper, corner point ids are updated
    rotate_polygons_to_least_point_first();                                              // STEP 3
    sort_polygons                       (*this, polygon_remapper);                       // STEP 4 updates polygon remapper
    scan_for_equal_and_opposite_polygons(*this, polygon_remapper, corner_remapper);      // STEP 5 updates polygon and corner remappers
    drop_dead_polygons                  (*this, polygon_remapper);                       // STEP 6 updates polygons, polygon corners
    reallocate_point_corners();                                                          // STEP 7
    drop_dead_corners_and_remap_corners (*this, corner_remapper);                        // STEP 8

    log_weld->trace("\n\nSTEP 9\n\n");
    //// debug_trace();
    remap_corners                       (corner_remapper);                               // STEP 9

    log_weld->trace("\n\nSTEP 10\n\n");
    sort_point_corners();
    build_edges();
    compute_point_normals(erhe::geometry::c_point_normals_smooth);

    log_weld->info("merge done");
}

}

