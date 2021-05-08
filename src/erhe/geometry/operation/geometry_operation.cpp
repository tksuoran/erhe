#include "erhe/geometry/operation/geometry_operation.hpp"
#include "erhe/geometry/property_map_collection.inl"
#include "erhe/geometry/log.hpp"

#include <gsl/assert>
#include <set>

namespace erhe::geometry::operation
{

void Geometry_operation::post_processing()
{
    ZoneScoped;

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
    ZoneScoped;

    point_old_to_new.reserve(source.point_count());
    for (Point_id src_point_id = 0,
         point_end = source.point_count();
         src_point_id < point_end;
         ++src_point_id)
    {
        make_new_point_from_point(src_point_id);
    }
}

void Geometry_operation::make_polygon_centroids()
{
    ZoneScoped;

    old_polygon_centroid_to_new_points.reserve(source.polygon_count());
    for (Polygon_id src_polygon_id = 0,
         polygon_end = source.polygon_count();
         src_polygon_id < polygon_end;
         ++src_polygon_id)
    {
        Polygon& src_polygon = source.polygons[src_polygon_id];
        make_new_point_from_polygon_centroid(src_polygon_id);
    }
}

void Geometry_operation::reserve_edge_to_new_points()
{
    uint32_t point_count = source.point_count();
    m_old_edge_to_new_points.resize(static_cast<size_t>(point_count) * s_max_edge_point_slots);
    std::fill(begin(m_old_edge_to_new_points),
              end(m_old_edge_to_new_points),
              std::numeric_limits<uint32_t>::max());
}

auto Geometry_operation::find_or_make_point_from_edge(Point_id point_a, Point_id point_b, size_t count)
    -> Point_id
{
    Point_id a = std::min(point_a, point_b);
    Point_id b = std::max(point_a, point_b);

    for (uint32_t slot = a * s_max_edge_point_slots, end = slot + s_max_edge_point_slots; slot < end;)
    {
        Point_id& edge_b       = m_old_edge_to_new_points[slot++];
        Point_id& new_point_id = m_old_edge_to_new_points[slot++];
        if (edge_b == std::numeric_limits<uint32_t>::max())
        {
            edge_b       = b;
            new_point_id = destination.make_point();
            //log.trace("created edge {} - {} point {}\n", a, b, new_point_id);
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
    FATAL("s_max_edge_point_slots too low");
    return Point_id{0};
}

void Geometry_operation::make_edge_midpoints(const std::initializer_list<float> relative_positions)
{
    ZoneScoped;

    size_t split_count = relative_positions.size();
    //uint32_t point_count = source.point_count();
    reserve_edge_to_new_points();
    for (Polygon_id src_polygon_id = 0,
         polygon_end = source.polygon_count();
         src_polygon_id < polygon_end;
         ++src_polygon_id)
    {
        Polygon& src_polygon = source.polygons[src_polygon_id];
        for (uint32_t i = 0; i < src_polygon.corner_count; ++i)
        {
            Polygon_corner_id src_polygon_corner_id      = src_polygon.first_polygon_corner_id + i;
            Polygon_corner_id src_polygon_next_corner_id = src_polygon.first_polygon_corner_id + (i + 1) % src_polygon.corner_count;
            Corner_id         src_corner_id              = source.polygon_corners[src_polygon_corner_id];
            Corner_id         src_next_corner_id         = source.polygon_corners[src_polygon_next_corner_id];
            Corner&           src_corner                 = source.corners[src_corner_id];
            Corner&           src_next_corner            = source.corners[src_next_corner_id];
            bool              in_order                   = src_corner.point_id < src_next_corner.point_id;
            Point_id          point_a                    = std::min(src_corner.point_id, src_next_corner.point_id);
            Point_id          point_b                    = std::max(src_corner.point_id, src_next_corner.point_id);
            Corner_id         corner_a                   = in_order ? src_corner_id : src_next_corner_id;
            Corner_id         corner_b                   = in_order ? src_next_corner_id : src_corner_id;
            Point_id          new_point_id               = find_or_make_point_from_edge(src_corner.point_id, src_next_corner.point_id, split_count);
            for (auto t : relative_positions)
            {
                float weight_a = t;
                float weight_b = 1.0f - t;
                add_point_source(new_point_id, weight_a, point_a); // TODO only add these once per edge?
                add_point_source(new_point_id, weight_b, point_b); // TODO only add these once per edge?
                add_point_corner_source(new_point_id, weight_a, corner_a);
                add_point_corner_source(new_point_id, weight_b, corner_b);
                new_point_id++;
            }
        }
    }
}

auto Geometry_operation::get_edge_new_point(Point_id point_a, Point_id point_b) const
-> Point_id
{
    Point_id a = std::min(point_a, point_b);
    Point_id b = std::max(point_a, point_b);

    for (uint32_t slot = a * s_max_edge_point_slots, end = slot + s_max_edge_point_slots; slot < end;)
    {
        const Point_id& edge_b = m_old_edge_to_new_points[slot++];
        const Point_id& new_point_id = m_old_edge_to_new_points[slot++];
        if (edge_b == std::numeric_limits<uint32_t>::max())
        {
            break;
        }
        if (edge_b == b)
        {
            return new_point_id;
        }
    }
    FATAL("edge point not found");
    return Point_id{0};
}

auto Geometry_operation::make_new_point_from_point(float weight, Point_id old_point)
-> Point_id
{
    auto new_point = destination.make_point();
    //log_operation.trace("make_new_point_from_point (weight = {}, old_point = {}) new_point = {}\n",
    //                    weight, old_point, new_point);
    //erhe::log::Log::Indenter scope_indent;
    add_point_source(new_point, weight, old_point);
    size_t i = static_cast<size_t>(old_point);
    if (point_old_to_new.size() <= i)
    {
        point_old_to_new.resize(i + s_grow_size);
    }
    point_old_to_new[i] = new_point;
    return new_point;
}

auto Geometry_operation::make_new_point_from_point(Point_id old_point)
-> Point_id
{
    auto new_point = destination.make_point();
    //log_operation.trace("make_new_point_from_point (old_point = {}) new_point = {})\n",
    //                    old_point, new_point);
    //erhe::log::Log::Indenter scope_indent;
    add_point_source(new_point, 1.0f, old_point);
    size_t i = static_cast<size_t>(old_point);
    if (point_old_to_new.size() <= i)
    {
        point_old_to_new.resize(i + s_grow_size);
    }
    point_old_to_new[i] = new_point;
    return new_point;
}

auto Geometry_operation::make_new_point_from_polygon_centroid(Polygon_id old_polygon)
-> Point_id
{
    auto new_point = destination.make_point();
    //log_operation.trace("make_new_point_from_polygon_centroid (old_polygon = {}) new_point = {}\n",
    //                    old_polygon, new_point);
    //erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(old_polygon);
    if (old_polygon_centroid_to_new_points.size() <= i)
    {
        old_polygon_centroid_to_new_points.resize(i + s_grow_size);
    }
    old_polygon_centroid_to_new_points[i] = new_point;
    add_polygon_centroid(new_point, 1.0f, old_polygon);
    return new_point;
}

void Geometry_operation::add_polygon_centroid(Point_id   new_point_id,
                                              float      weight,
                                              Polygon_id old_polygon_id)
{
    Polygon& old_polygon = source.polygons[old_polygon_id];
    //log_operation.trace("add_polygon_centroid (new_point_id = {}, weight = {}, old_polygon_id = {})\n",
    //                    new_point_id, weight, old_polygon_id );
    //erhe::log::Log::Indenter scope_indent;
    for (Polygon_corner_id old_polygon_corner_id = old_polygon.first_polygon_corner_id,
         end = old_polygon.first_polygon_corner_id + old_polygon.corner_count;
         old_polygon_corner_id < end;
         ++old_polygon_corner_id)
    {
        Corner_id old_corner_id = source.polygon_corners[old_polygon_corner_id];
        Corner&   old_corner    = source.corners[old_corner_id];
        add_point_corner_source(new_point_id, weight, old_corner_id);
        add_point_source(new_point_id, weight, old_corner.point_id);
    }
}

void Geometry_operation::add_point_ring(Point_id new_point_id,
                                        float    weight,
                                        Point_id old_point_id)
{
    //log_operation.trace("add_point_ring (new_point_id = {}, weight = {}, old_point_id = {})\n",
    //                    new_point_id, weight, old_point_id);
    //erhe::log::Log::Indenter scope_indent;
    Point& old_point = source.points[old_point_id];
    for (Point_corner_id old_point_corner_id = old_point.first_point_corner_id,
        end = old_point.first_point_corner_id + old_point.corner_count;
        old_point_corner_id < end;
        ++old_point_corner_id)
    {
        Corner_id  ring_corner_id      = source.point_corners[old_point_corner_id];
        Corner&    ring_corner         = source.corners[ring_corner_id];
        Polygon_id ring_polygon_id     = ring_corner.polygon_id;
        Polygon&   ring_polygon        = source.polygons[ring_polygon_id];
        Corner_id  next_ring_corner_id = ring_polygon.next_corner(source, ring_corner_id);
        Corner&    next_ring_corner    = source.corners[next_ring_corner_id];
        Point_id   next_ring_point_id  = next_ring_corner.point_id;
        add_point_source(new_point_id, weight, next_ring_point_id);
    }
}

auto Geometry_operation::make_new_polygon_from_polygon(Polygon_id old_polygon_id)
-> Polygon_id
{
    auto new_polygon_id = destination.make_polygon();
    //log_operation.trace("make_new_polygon_from_polygon (old_polygon_id = {}) new_polygon_id = {}\n",
    //                    old_polygon_id, new_polygon_id);
    //erhe::log::Log::Indenter scope_indent;
    add_polygon_source(new_polygon_id, 1.0f, old_polygon_id);
    size_t i = static_cast<size_t>(old_polygon_id);
    if (polygon_old_to_new.size() <= i)
    {
        polygon_old_to_new.resize(i + s_grow_size);
    }
    polygon_old_to_new[i] = new_polygon_id;
    return new_polygon_id;
}

auto Geometry_operation::make_new_corner_from_polygon_centroid(Polygon_id new_polygon_id,
                                                               Polygon_id old_polygon_id)
-> Corner_id
{
    //log_operation.trace("make_new_corner_from_polygon_centroid (new_polygon_id = {}, old_polygon_id = {})\n",
    //                    new_polygon_id, old_polygon_id);
    //erhe::log::Log::Indenter scope_indent;
    auto new_point_id  = old_polygon_centroid_to_new_points[old_polygon_id];
    auto new_corner_id = destination.make_polygon_corner(new_polygon_id, new_point_id);
    //log_operation.trace("new_point_id = {}, new_corner_id = {}\n",
    //                    new_point_id, new_corner_id);
    distribute_corner_sources(new_corner_id, 1.0f, new_point_id);
    return new_corner_id;
}

auto Geometry_operation::make_new_corner_from_point(Polygon_id new_polygon_id,
                                                    Point_id   new_point_id)
-> Corner_id
{
    //log_operation.trace("make_new_corner_from_point (new_polygon_id = {}, new_point_id = {})\n",
    //                    new_polygon_id, new_point_id);
    //erhe::log::Log::Indenter scope_indent;
    auto new_corner_id = destination.make_polygon_corner(new_polygon_id, new_point_id);
    distribute_corner_sources(new_corner_id, 1.0f, new_point_id);
    return new_corner_id;
}

auto Geometry_operation::make_new_corner_from_corner(Polygon_id new_polygon_id,
                                                     Corner_id  old_corner_id)
-> Corner_id
{
    //log_operation.trace("make_new_corner_from_corner (new_polygon_id = {}, old_corner_id = {})\n",
    //                    new_polygon_id, old_corner_id);
    //erhe::log::Log::Indenter scope_indent;
    Corner old_corner  = source.corners[old_corner_id];
    auto old_point_id  = old_corner.point_id;
    auto new_point_id  = point_old_to_new[old_point_id];
    auto new_corner_id = destination.make_polygon_corner(new_polygon_id, new_point_id);
    //log_operation.trace("old_point_id = {}, new_point_id = {}, new_corner_id = {}\n",
    //                    old_point_id, new_point_id, new_corner_id);
    add_corner_source(new_corner_id, 1.0f, old_corner_id);
    return new_corner_id;
}

void Geometry_operation::add_polygon_corners(Polygon_id new_polygon_id,
                                             Polygon_id old_polygon_id)
{
    //log_operation.trace("add_polygon_corners (new_polygon_id = {}, old_polygon_id = {})\n",
    //                    new_polygon_id, old_polygon_id);
    //erhe::log::Log::Indenter scope_indent;
    Polygon& old_polygon = source.polygons[old_polygon_id];
    for (Polygon_corner_id old_polygon_corner_id = old_polygon.first_polygon_corner_id,
         end = old_polygon.first_polygon_corner_id + old_polygon.corner_count;
         old_polygon_corner_id < end;
         ++old_polygon_corner_id)
    {
        Corner_id old_corner_id = source.polygon_corners[old_polygon_corner_id];
        Corner&   old_corner    = source.corners[old_corner_id];
        Point_id  old_point_id  = old_corner.point_id;
        Point_id  new_point_id  = point_old_to_new[old_point_id];
        Corner_id new_corner_id = destination.make_polygon_corner(new_polygon_id, new_point_id);
        add_corner_source(new_corner_id, 1.0f, old_corner_id);
    }
}

void Geometry_operation::add_point_source(Point_id new_point_id,
                                          float    weight,
                                          Point_id old_point_id)
{
    //log_operation.trace("add_point_source        (new_point_id = {}, weight = {}, old_point_id = {})\n",
    //                    new_point_id, weight, old_point_id);
    //erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(new_point_id);
    if (new_point_sources.size() <= i)
    {
        new_point_sources.resize(i + s_grow_size);
    }
    new_point_sources[i].emplace_back(weight, old_point_id);
}

void Geometry_operation::add_point_corner_source(Point_id  new_point_id,
                                                 float     weight,
                                                 Corner_id old_corner_id)
{
    //log_operation.trace("add_point_corner_source (new_point_id = {}, weight = {}, old_corner_id = {})\n",
    //                    new_point_id, weight, old_corner_id);
    //erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(new_point_id);
    if (new_point_corner_sources.size() <= i)
    {
        new_point_corner_sources.resize(i + s_grow_size);
    }
    new_point_corner_sources[new_point_id].emplace_back(weight, old_corner_id);
}

void Geometry_operation::add_corner_source(Corner_id new_corner_id,
                                           float     weight,
                                           Corner_id old_corner_id)
{
    //log_operation.trace("add_corner_source (new_corner_id = {}, weight = {}, old_corner_id = {})\n",
    //                    new_corner_id, weight, old_corner_id);
    //erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(new_corner_id);
    if (new_corner_sources.size() <= i)
    {
        new_corner_sources.resize(i + s_grow_size);
    }
    new_corner_sources[i].emplace_back(weight, old_corner_id);
}

void Geometry_operation::distribute_corner_sources(Corner_id new_corner_id,
                                                   float     weight,
                                                   Point_id  new_point_id)
{
    //log_operation.trace("distribute_corner_sources (new_corner_id = {}, weight = {}, new_point_id = {})\n",
    //                    new_corner_id, weight, new_point_id);
    //erhe::log::Log::Indenter scope_indent;
    auto sources = new_point_corner_sources[new_point_id];
    for (auto& source : sources)
    {
        add_corner_source(new_corner_id, weight * source.first, source.second);
    }
}

void Geometry_operation::add_polygon_source(Polygon_id new_polygon_id,
                                            float      weight,
                                            Polygon_id old_polygon_id)
{
    //log_operation.trace("add_polygon_source (new_polygon_id = {}, weight = {}, old_polygon_id = {})\n",
    //                    new_polygon_id, weight, old_polygon_id);
    //erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(new_polygon_id);
    if (new_polygon_sources.size() <= i)
    {
        new_polygon_sources.resize(i + s_grow_size);
    }
    new_polygon_sources[i].emplace_back(weight, old_polygon_id);
}

void Geometry_operation::add_edge_source(Edge_id new_edge_id,
                                         float   weight,
                                         Edge_id old_edge_id)
{
    //log_operation.trace("add_edge_source (new_edge_id = {}, weight = {}, old_edge_id = {})\n",
    //                    new_edge_id, weight, old_edge_id);
    //erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(new_edge_id);
    if (new_edge_sources.size() <= i)
    {
        new_edge_sources.resize(i + s_grow_size);
    }
    new_edge_sources[i].emplace_back(weight, old_edge_id);
}

void Geometry_operation::build_destination_edges_with_sourcing()
{
    //log_operation.trace("build_destination_edges_with_sourcing()\n");
    //erhe::log::Log::Indenter scope_indent;

    destination.build_edges();

    for (Edge_id old_edge_id = 0,
         edge_end = source.edge_count();
         old_edge_id < edge_end;
         ++ old_edge_id)
    {
        Edge&    old_edge    = source.edges[old_edge_id];
        Point_id new_a       = point_old_to_new[old_edge.a];
        Point_id new_b       = point_old_to_new[old_edge.b];
        Point_id new_a_      = std::min(new_a, new_b);
        Point_id new_b_      = std::max(new_a, new_b);
        Edge_id  new_edge_id = destination.make_edge(new_a_, new_b_);
        add_edge_source(new_edge_id, 1.0f, old_edge_id);
        size_t i = static_cast<size_t>(old_edge_id);
        if (edge_old_to_new.size() <= i)
        {
            edge_old_to_new.resize(i + s_grow_size);
        }
        edge_old_to_new[i] = new_edge_id;
    }
}

void Geometry_operation::interpolate_all_property_maps()
{
    source.point_attributes()  .interpolate(destination.point_attributes(),   new_point_sources);
    source.polygon_attributes().interpolate(destination.polygon_attributes(), new_polygon_sources);
    source.corner_attributes() .interpolate(destination.corner_attributes(),  new_corner_sources);
    source.edge_attributes()   .interpolate(destination.edge_attributes(),    new_edge_sources);
}

} // namespace erhe::geometry::operation
