#include "erhe/geometry/operation/geometry_operation.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include <gsl/assert>
#include <set>

namespace erhe::geometry::operation
{

void Geometry_operation::post_processing()
{
    ERHE_PROFILE_FUNCTION

    destination.make_point_corners();
    destination.build_edges();
    interpolate_all_property_maps();
    destination.compute_point_normals(c_point_normals_smooth);
    destination.compute_polygon_centroids();
    destination.generate_polygon_texture_coordinates();
    destination.compute_tangents();
}

void Geometry_operation::make_points_from_points()
{
    ERHE_PROFILE_FUNCTION

    point_old_to_new.reserve(source.get_point_count());
    source.for_each_point_const([&](auto& i)
    {
        make_new_point_from_point(i.point_id);
    });
}

void Geometry_operation::make_polygon_centroids()
{
    ERHE_PROFILE_FUNCTION

    old_polygon_centroid_to_new_points.reserve(source.get_polygon_count());
    source.for_each_polygon_const([&](auto& i)
    {
        make_new_point_from_polygon_centroid(i.polygon_id);
    });
}

void Geometry_operation::reserve_edge_to_new_points()
{
    const uint32_t point_count = source.get_point_count();
    m_old_edge_to_new_points.resize(static_cast<size_t>(point_count) * s_max_edge_point_slots);
    std::fill(
        begin(m_old_edge_to_new_points),
        end(m_old_edge_to_new_points),
        std::numeric_limits<uint32_t>::max()
    );
}

auto Geometry_operation::find_or_make_point_from_edge(
    const Point_id point_a,
    const Point_id point_b,
    const size_t   count
) -> Point_id
{
    const Point_id a = std::min(point_a, point_b);
    const Point_id b = std::max(point_a, point_b);

    for (
        uint32_t slot = a * s_max_edge_point_slots,
        end = slot + s_max_edge_point_slots;
        slot < end;
    )
    {
        Point_id& edge_b       = m_old_edge_to_new_points[slot++];
        Point_id& new_point_id = m_old_edge_to_new_points[slot++];
        if (edge_b == std::numeric_limits<uint32_t>::max())
        {
            edge_b       = b;
            new_point_id = destination.make_point();
            // log_geometry.trace("created edge {} - {} point {}\n", a, b, new_point_id);
            for (size_t i = 1; i < count; ++i)
            {
                destination.make_point();
            }
            return new_point_id;
        }
        if (edge_b == b)
        {
            return new_point_id;
        }
    }
    ERHE_FATAL("s_max_edge_point_slots too low\n");
}

void Geometry_operation::make_edge_midpoints(const std::initializer_list<float> relative_positions)
{
    ERHE_PROFILE_FUNCTION

    const size_t split_count = relative_positions.size();
    reserve_edge_to_new_points();

    source.for_each_polygon_const([&](auto& i)
    {
        i.polygon.for_each_corner_neighborhood_const(source, [&](auto& j)
        {
            const bool      in_order = j.corner.point_id < j.next_corner.point_id;
            const Point_id  point_a  = j.corner.point_id;
            const Point_id  point_b  = j.next_corner.point_id;
            const Corner_id corner_a = j.corner_id;
            const Corner_id corner_b = j.next_corner_id;

            Point_id new_point_id = find_or_make_point_from_edge(
                j.corner.point_id,
                j.next_corner.point_id,
                split_count
            );
            //log_subdivide.trace(
            //    "Polygon {} edge {}-{} midpoint {} count {}\n",
            //    i.polygon_id,
            //    point_a,
            //    point_b,
            //    new_point_id,
            //    split_count
            //);
            if (!in_order)
            {
                new_point_id = static_cast<Point_id>(new_point_id + split_count - 1);
            }
            for (auto t : relative_positions)
            {
                const float weight_a = t;
                const float weight_b = 1.0f - t;
                //log_subdivide.trace(
                //    "   new point {} source point a {} weight {}\n"
                //    "   new point {} source point b {} weight {}\n",
                //    new_point_id, point_a, weight_a,
                //    new_point_id, point_b, weight_b
                //);
                add_point_source       (new_point_id, weight_a, point_a); // TODO only add these once per edge?
                add_point_source       (new_point_id, weight_b, point_b); // TODO only add these once per edge?
                add_point_corner_source(new_point_id, weight_a, corner_a);
                add_point_corner_source(new_point_id, weight_b, corner_b);
                if (in_order)
                {
                    ++new_point_id;
                }
                else
                {
                    --new_point_id;
                }
            }
        });
    });
}

auto Geometry_operation::get_edge_new_point(
    const Point_id point_a,
    const Point_id point_b,
    const Point_id split_position,
    const Point_id split_count
) const -> Point_id
{
    const bool     in_order = point_a < point_b;
    const Point_id a        = std::min(point_a, point_b);
    const Point_id b        = std::max(point_a, point_b);

    for (uint32_t slot = a * s_max_edge_point_slots, end = slot + s_max_edge_point_slots; slot < end;)
    {
        const Point_id edge_b       = m_old_edge_to_new_points[slot++];
        const Point_id new_point_id = m_old_edge_to_new_points[slot++];
        if (edge_b == std::numeric_limits<uint32_t>::max())
        {
            break;
        }
        if (edge_b == b)
        {
            const auto edge_point = in_order
                ? new_point_id + split_count - 1 - split_position
                : new_point_id + split_position;
            //log_subdivide.trace(
            //    "Edge {} {} midpoint {}/{} = {}\n",
            //    point_a,
            //    point_b,
            //    split_position,
            //    split_count,
            //    edge_point
            //);
            return edge_point;
        }
    }

    log_catmull_clark->error("edge point {}-{} not found", point_a, point_b);
    // log_catmull_clark.error("point {} edge end points: ", a);
    // for (
    //     uint32_t slot = a * s_max_edge_point_slots,
    //     end = slot + s_max_edge_point_slots;
    //     slot < end;
    // )
    // {
    //     const Point_id edge_b       = m_old_edge_to_new_points[slot++];
    //     const Point_id new_point_id = m_old_edge_to_new_points[slot++];
    //     if (edge_b == std::numeric_limits<uint32_t>::max())
    //     {
    //         continue;
    //     }
    //     log_catmull_clark.error(" {:3}", edge_b);
    // }
    // log_catmull_clark.error("\n");

    return Point_id{0};
}

auto Geometry_operation::make_new_point_from_point(
    const float    point_weight,
    const Point_id old_point
) -> Point_id
{
    const auto new_point = destination.make_point();
    // log_operation.trace(
    //     "make_new_point_from_point (weight = {}, old_point = {}) new_point = {}\n",
    //     weight, old_point, new_point
    // );
    // const erhe::log::Indenter scope_indent;
    add_point_source(new_point, point_weight, old_point);
    const size_t i = static_cast<size_t>(old_point);
    if (point_old_to_new.size() <= i)
    {
        point_old_to_new.resize(i + s_grow_size);
    }
    point_old_to_new[i] = new_point;
    return new_point;
}

auto Geometry_operation::make_new_point_from_point(
    const Point_id old_point
) -> Point_id
{
    const auto new_point = destination.make_point();
    // log_operation.trace(
    //     "make_new_point_from_point (old_point = {}) new_point = {})\n",
    //     old_point, new_point
    // );
    // const erhe::log::Indenter scope_indent;
    add_point_source(new_point, 1.0f, old_point);
    const size_t i = static_cast<size_t>(old_point);
    if (point_old_to_new.size() <= i)
    {
        point_old_to_new.resize(i + s_grow_size);
    }
    point_old_to_new[i] = new_point;
    return new_point;
}

auto Geometry_operation::make_new_point_from_polygon_centroid(
    const Polygon_id old_polygon
) -> Point_id
{
    const auto new_point = destination.make_point();
    // log_operation.trace(
    //     "make_new_point_from_polygon_centroid (old_polygon = {}) new_point = {}\n",
    //     old_polygon, new_point
    // );
    // const erhe::log::Indenter scope_indent;
    const size_t i = static_cast<size_t>(old_polygon);
    if (old_polygon_centroid_to_new_points.size() <= i)
    {
        old_polygon_centroid_to_new_points.resize(i + s_grow_size);
    }
    old_polygon_centroid_to_new_points[i] = new_point;
    add_polygon_centroid(new_point, 1.0f, old_polygon);
    return new_point;
}

void Geometry_operation::add_polygon_centroid(
    const Point_id   new_point_id,
    const float      polygon_weight,
    const Polygon_id old_polygon_id
)
{
    const Polygon& old_polygon = source.polygons[old_polygon_id];
    // log_operation.trace(
    //     "add_polygon_centroid (new_point_id = {}, weight = {}, old_polygon_id = {})\n",
    //     new_point_id, weight, old_polygon_id
    // );
    // const erhe::log::Indenter scope_indent;
    old_polygon.for_each_corner_const(source, [&](auto& i)
    {
        add_point_corner_source(new_point_id, polygon_weight, i.corner_id);
        add_point_source(new_point_id, polygon_weight, i.corner.point_id);
    });
}

void Geometry_operation::add_point_ring(
    const Point_id new_point_id,
    const float    point_weight,
    const Point_id old_point_id
)
{
    // log_operation.trace(
    //     "add_point_ring (new_point_id = {}, weight = {}, old_point_id = {})\n",
    //     new_point_id, weight, old_point_id
    // );
    // const erhe::log::Indenter scope_indent;
    const Point& old_point = source.points[old_point_id];
    old_point.for_each_corner_const(source, [&](auto& i)
    {
        const Polygon_id ring_polygon_id     = i.corner.polygon_id;
        const Polygon&   ring_polygon        = source.polygons[ring_polygon_id];
        const Corner_id  next_ring_corner_id = ring_polygon.next_corner(source, i.corner_id);
        const Corner&    next_ring_corner    = source.corners[next_ring_corner_id];
        const Point_id   next_ring_point_id  = next_ring_corner.point_id;
        add_point_source(new_point_id, point_weight, next_ring_point_id);
    });
}

auto Geometry_operation::make_new_polygon_from_polygon(
    const Polygon_id old_polygon_id
) -> Polygon_id
{
    const auto new_polygon_id = destination.make_polygon();
    // log_operation.trace(
    //     "make_new_polygon_from_polygon (old_polygon_id = {}) new_polygon_id = {}\n",
    //     old_polygon_id, new_polygon_id
    // );
    // const erhe::log::Indenter scope_indent;
    add_polygon_source(new_polygon_id, 1.0f, old_polygon_id);
    const size_t i = static_cast<size_t>(old_polygon_id);
    if (polygon_old_to_new.size() <= i)
    {
        polygon_old_to_new.resize(i + s_grow_size);
    }
    polygon_old_to_new[i] = new_polygon_id;
    return new_polygon_id;
}

auto Geometry_operation::make_new_corner_from_polygon_centroid(
    const Polygon_id new_polygon_id,
    const Polygon_id old_polygon_id
) -> Corner_id
{
    // log_operation.trace(
    //     "make_new_corner_from_polygon_centroid (new_polygon_id = {}, old_polygon_id = {})\n",
    //     new_polygon_id, old_polygon_id
    // );
    // const erhe::log::Indenter scope_indent;
    const auto new_point_id  = old_polygon_centroid_to_new_points[old_polygon_id];
    const auto new_corner_id = destination.make_polygon_corner(new_polygon_id, new_point_id);
    // log_operation.trace(
    //     "new_point_id = {}, new_corner_id = {}\n",
    //     new_point_id, new_corner_id
    // );
    distribute_corner_sources(new_corner_id, 1.0f, new_point_id);
    return new_corner_id;
}

auto Geometry_operation::make_new_corner_from_point(
    const Polygon_id new_polygon_id,
    const Point_id   new_point_id
) -> Corner_id
{
    // log_operation.trace(
    //     "make_new_corner_from_point (new_polygon_id = {}, new_point_id = {})\n",
    //     new_polygon_id, new_point_id
    // );
    // const erhe::log::Indenter scope_indent;
    const auto new_corner_id = destination.make_polygon_corner(new_polygon_id, new_point_id);
    distribute_corner_sources(new_corner_id, 1.0f, new_point_id);
    return new_corner_id;
}

auto Geometry_operation::make_new_corner_from_corner(
    const Polygon_id new_polygon_id,
    const Corner_id  old_corner_id
) -> Corner_id
{
    // const log_operation.trace(
    //     "make_new_corner_from_corner (new_polygon_id = {}, old_corner_id = {})\n",
    //     new_polygon_id, old_corner_id
    // );
    // const erhe::log::Indenter scope_indent;
    const Corner old_corner    = source.corners[old_corner_id];
    const auto   old_point_id  = old_corner.point_id;
    const auto   new_point_id  = point_old_to_new[old_point_id];
    const auto   new_corner_id = destination.make_polygon_corner(new_polygon_id, new_point_id);
    // log_operation.trace(
    //     "old_point_id = {}, new_point_id = {}, new_corner_id = {}\n",
    //     old_point_id, new_point_id, new_corner_id
    // );
    add_corner_source(new_corner_id, 1.0f, old_corner_id);
    return new_corner_id;
}

void Geometry_operation::add_polygon_corners(
    const Polygon_id new_polygon_id,
    const Polygon_id old_polygon_id
)
{
    // log_operation.trace(
    //     "add_polygon_corners (new_polygon_id = {}, old_polygon_id = {})\n",
    //     new_polygon_id, old_polygon_id
    // );
    // const erhe::log::Indenter scope_indent;
    const Polygon& old_polygon = source.polygons[old_polygon_id];
    old_polygon.for_each_corner_const(source, [&](auto& i)
    {
        const Point_id  old_point_id  = i.corner.point_id;
        const Point_id  new_point_id  = point_old_to_new[old_point_id];
        const Corner_id new_corner_id = destination.make_polygon_corner(new_polygon_id, new_point_id);
        add_corner_source(new_corner_id, 1.0f, i.corner_id);
    });
}

void Geometry_operation::add_point_source(
    const Point_id new_point_id,
    const float    point_weight,
    const Point_id old_point_id
)
{
    // log_operation.trace(
    //     "add_point_source (new_point_id = {}, weight = {}, old_point_id = {})\n",
    //     new_point_id, weight, old_point_id
    // );
    // const erhe::log::Indenter scope_indent;
    const size_t i = static_cast<size_t>(new_point_id);
    if (new_point_sources.size() <= i)
    {
        new_point_sources.resize(i + s_grow_size);
    }
    new_point_sources[i].emplace_back(point_weight, old_point_id);
}

void Geometry_operation::add_point_corner_source(
    const Point_id  new_point_id,
    const float     corner_weight,
    const Corner_id old_corner_id
)
{
    // log_operation.trace(
    //     "add_point_corner_source (new_point_id = {}, weight = {}, old_corner_id = {})\n",
    //     new_point_id, weight, old_corner_id
    // );
    // const erhe::log::Indenter scope_indent;
    const size_t i = static_cast<size_t>(new_point_id);
    if (new_point_corner_sources.size() <= i)
    {
        new_point_corner_sources.resize(i + s_grow_size);
    }
    new_point_corner_sources[new_point_id].emplace_back(corner_weight, old_corner_id);
}

void Geometry_operation::add_corner_source(
    const Corner_id new_corner_id,
    const float     corner_weight,
    const Corner_id old_corner_id
)
{
    // log_operation.trace(
    //     "add_corner_source (new_corner_id = {}, weight = {}, old_corner_id = {})\n",
    //     new_corner_id, weight, old_corner_id
    // );
    // const erhe::log::Indenter scope_indent;
    const size_t i = static_cast<size_t>(new_corner_id);
    if (new_corner_sources.size() <= i)
    {
        new_corner_sources.resize(i + s_grow_size);
    }
    new_corner_sources[i].emplace_back(corner_weight, old_corner_id);
}

void Geometry_operation::distribute_corner_sources(
    const Corner_id new_corner_id,
    const float     point_weight,
    const Point_id  new_point_id
)
{
    // log_operation.trace(
    //     "distribute_corner_sources (new_corner_id = {}, weight = {}, new_point_id = {})\n",
    //     new_corner_id, weight, new_point_id
    // );
    // const erhe::log::Indenter scope_indent;
    const auto point_corner_sources = new_point_corner_sources[new_point_id];
    for (const auto& point_corner_source : point_corner_sources)
    {
        const float     corner_weight = point_weight * point_corner_source.first;
        const Corner_id corner_id     = point_corner_source.second;
        add_corner_source(new_corner_id, corner_weight, corner_id);
    }
}

void Geometry_operation::add_polygon_source(
    const Polygon_id new_polygon_id,
    const float      polygon_weight,
    const Polygon_id old_polygon_id
)
{
    // log_operation.trace(
    //     "add_polygon_source (new_polygon_id = {}, weight = {}, old_polygon_id = {})\n",
    //     new_polygon_id, weight, old_polygon_id
    // );
    // const erhe::log::Indenter scope_indent;
    const size_t i = static_cast<size_t>(new_polygon_id);
    if (new_polygon_sources.size() <= i)
    {
        new_polygon_sources.resize(i + s_grow_size);
    }
    new_polygon_sources[i].emplace_back(polygon_weight, old_polygon_id);
}

void Geometry_operation::add_edge_source(
    const Edge_id new_edge_id,
    const float   edge_weight,
    const Edge_id old_edge_id
)
{
    // log_operation.trace(
    //     "add_edge_source (new_edge_id = {}, weight = {}, old_edge_id = {})\n",
    //     new_edge_id, weight, old_edge_id
    // );
    // const erhe::log::Indenter scope_indent;
    const size_t i = static_cast<size_t>(new_edge_id);
    if (new_edge_sources.size() <= i)
    {
        new_edge_sources.resize(i + s_grow_size);
    }
    new_edge_sources[i].emplace_back(edge_weight, old_edge_id);
}

void Geometry_operation::build_destination_edges_with_sourcing()
{
    // log_operation.trace("build_destination_edges_with_sourcing()\n");
    // const erhe::log::Indenter scope_indent;

    destination.build_edges();

    source.for_each_edge_const([&](auto& i)
    {
        const Point_id new_a       = point_old_to_new[i.edge.a];
        const Point_id new_b       = point_old_to_new[i.edge.b];
        const Point_id new_a_      = std::min(new_a, new_b);
        const Point_id new_b_      = std::max(new_a, new_b);
        const Edge_id  new_edge_id = destination.make_edge(new_a_, new_b_);
        add_edge_source(new_edge_id, 1.0f, i.edge_id);
        const size_t index = static_cast<size_t>(i.edge_id);
        if (edge_old_to_new.size() <= index)
        {
            edge_old_to_new.resize(index + s_grow_size);
        }
        edge_old_to_new[index] = new_edge_id;
    });
}

void Geometry_operation::interpolate_all_property_maps()
{
    new_point_sources  .resize(destination.get_point_count());
    new_polygon_sources.resize(destination.get_polygon_count());
    new_corner_sources .resize(destination.get_corner_count());
    new_edge_sources   .resize(destination.get_edge_count());
    source.point_attributes()  .interpolate(destination.point_attributes(),   new_point_sources);
    source.polygon_attributes().interpolate(destination.polygon_attributes(), new_polygon_sources);
    source.corner_attributes() .interpolate(destination.corner_attributes(),  new_corner_sources);
    source.edge_attributes()   .interpolate(destination.edge_attributes(),    new_edge_sources);
}

} // namespace erhe::geometry::operation
