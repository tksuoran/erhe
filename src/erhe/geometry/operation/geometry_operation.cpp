#include "erhe/geometry/operation/geometry_operation.hpp"
#include "erhe/geometry/property_map_collection.inl"
#include "erhe/geometry/log.hpp"

#include <gsl/assert>
#include <set>

namespace erhe::geometry::operation
{

auto Geometry_operation::make_new_point_from_point(float weight, Point_id old_point)
-> Point_id
{
    auto new_point = m_destination.make_point();
    log_operation.trace("make_new_point_from_point (weight = {}, old_point = {}) new_point = {}\n",
                        weight, old_point, new_point);
    erhe::log::Log::Indenter scope_indent;
    add_point_source(new_point, weight, old_point);
    size_t i = static_cast<size_t>(old_point);
    if (m_point_old_to_new.size() <= i)
    {
        m_point_old_to_new.resize(i + s_grow_size);
    }
    m_point_old_to_new[i] = new_point;
    return new_point;
}

auto Geometry_operation::make_new_point_from_point(Point_id old_point)
-> Point_id
{
    auto new_point = m_destination.make_point();
    log_operation.trace("make_new_point_from_point (old_point = {}) new_point = {})\n",
                        old_point, new_point);
    erhe::log::Log::Indenter scope_indent;
    add_point_source(new_point, 1.0f, old_point);
    size_t i = static_cast<size_t>(old_point);
    if (m_point_old_to_new.size() <= i)
    {
        m_point_old_to_new.resize(i + s_grow_size);
    }
    m_point_old_to_new[i] = new_point;
    return new_point;
}

auto Geometry_operation::make_new_point_from_polygon_centroid(Polygon_id old_polygon)
-> Point_id
{
    auto new_point = m_destination.make_point();
    log_operation.trace("make_new_point_from_polygon_centroid (old_polygon = {}) new_point = {}\n",
                        old_polygon, new_point);
    erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(old_polygon);
    if (m_old_polygon_centroid_to_new_points.size() <= i)
    {
        m_old_polygon_centroid_to_new_points.resize(i + s_grow_size);
    }
    m_old_polygon_centroid_to_new_points[i] = new_point;
    add_polygon_centroid(new_point, 1.0f, old_polygon);
    return new_point;
}

void Geometry_operation::add_polygon_centroid(Point_id   new_point_id,
                                              float      weight,
                                              Polygon_id old_polygon_id)
{
    Polygon& old_polygon = m_source.polygons[old_polygon_id];
    log_operation.trace("add_polygon_centroid (new_point_id = {}, weight = {}, old_polygon_id = {})\n",
                        new_point_id, weight, old_polygon_id );
    erhe::log::Log::Indenter scope_indent;
    for (Polygon_corner_id old_polygon_corner_id = old_polygon.first_polygon_corner_id,
         end = old_polygon.first_polygon_corner_id + old_polygon.corner_count;
         old_polygon_corner_id < end;
         ++old_polygon_corner_id)
    {
        Corner_id old_corner_id = m_source.polygon_corners[old_polygon_corner_id];
        Corner&   old_corner    = m_source.corners[old_corner_id];
        add_point_corner_source(new_point_id, weight, old_corner_id);
        add_point_source(new_point_id, weight, old_corner.point_id);
    }
}

void Geometry_operation::add_point_ring(Point_id new_point_id,
                                        float    weight,
                                        Point_id old_point_id)
{
    log_operation.trace("add_point_ring (new_point_id = {}, weight = {}, old_point_id = {})\n",
                        new_point_id, weight, old_point_id);
    erhe::log::Log::Indenter scope_indent;
    Point& old_point = m_source.points[old_point_id];
    for (Point_corner_id old_point_corner_id = old_point.first_point_corner_id,
        end = old_point.first_point_corner_id + old_point.corner_count;
        old_point_corner_id < end;
        ++old_point_corner_id)
    {
        Corner_id  ring_corner_id      = m_source.point_corners[old_point_corner_id];
        Corner&    ring_corner         = m_source.corners[ring_corner_id];
        Polygon_id ring_polygon_id     = ring_corner.polygon_id;
        Polygon&   ring_polygon        = m_source.polygons[ring_polygon_id];
        Corner_id  next_ring_corner_id = ring_polygon.next_corner(m_source, ring_corner_id);
        Corner&    next_ring_corner    = m_source.corners[next_ring_corner_id];
        Point_id   next_ring_point_id  = next_ring_corner.point_id;
        add_point_source(new_point_id, weight, next_ring_point_id);
    }
}

auto Geometry_operation::make_new_polygon_from_polygon(Polygon_id old_polygon_id)
-> Polygon_id
{
    auto new_polygon_id = m_destination.make_polygon();
    log_operation.trace("make_new_polygon_from_polygon (old_polygon_id = {}) new_polygon_id = {}\n",
                        old_polygon_id, new_polygon_id);
    erhe::log::Log::Indenter scope_indent;
    add_polygon_source(new_polygon_id, 1.0f, old_polygon_id);
    size_t i = static_cast<size_t>(old_polygon_id);
    if (m_polygon_old_to_new.size() <= i)
    {
        m_polygon_old_to_new.resize(i + s_grow_size);
    }
    m_polygon_old_to_new[i] = new_polygon_id;
    return new_polygon_id;
}

auto Geometry_operation::make_new_corner_from_polygon_centroid(Polygon_id new_polygon_id,
                                                               Polygon_id old_polygon_id)
-> Corner_id
{
    log_operation.trace("make_new_corner_from_polygon_centroid (new_polygon_id = {}, old_polygon_id = {})\n",
                        new_polygon_id, old_polygon_id);
    erhe::log::Log::Indenter scope_indent;
    auto new_point_id  = m_old_polygon_centroid_to_new_points[old_polygon_id];
    auto new_corner_id = m_destination.make_polygon_corner(new_polygon_id, new_point_id);
    log_operation.trace("new_point_id = {}, new_corner_id = {}\n",
                        new_point_id, new_corner_id);
    distribute_corner_sources(new_corner_id, 1.0f, new_point_id);
    return new_corner_id;
}

auto Geometry_operation::make_new_corner_from_point(Polygon_id new_polygon_id,
                                                    Point_id   new_point_id)
-> Corner_id
{
    log_operation.trace("make_new_corner_from_point (new_polygon_id = {}, new_point_id = {})\n",
                        new_polygon_id, new_point_id);
    erhe::log::Log::Indenter scope_indent;
    auto new_corner_id = m_destination.make_polygon_corner(new_polygon_id, new_point_id);
    distribute_corner_sources(new_corner_id, 1.0f, new_point_id);
    return new_corner_id;
}

auto Geometry_operation::make_new_corner_from_corner(Polygon_id new_polygon_id,
                                                     Corner_id  old_corner_id)
-> Corner_id
{
    log_operation.trace("make_new_corner_from_corner (new_polygon_id = {}, old_corner_id = {})\n",
                        new_polygon_id, old_corner_id);
    erhe::log::Log::Indenter scope_indent;
    Corner old_corner  = m_source.corners[old_corner_id];
    auto old_point_id  = old_corner.point_id;
    auto new_point_id  = m_point_old_to_new[old_point_id];
    auto new_corner_id = m_destination.make_polygon_corner(new_polygon_id, new_point_id);
    log_operation.trace("old_point_id = {}, new_point_id = {}, new_corner_id = {}\n",
                        old_point_id, new_point_id, new_corner_id);
    add_corner_source(new_corner_id, 1.0f, old_corner_id);
    return new_corner_id;
}

void Geometry_operation::add_polygon_corners(Polygon_id new_polygon_id,
                                             Polygon_id old_polygon_id)
{
    log_operation.trace("add_polygon_corners (new_polygon_id = {}, old_polygon_id = {})\n",
                        new_polygon_id, old_polygon_id);
    erhe::log::Log::Indenter scope_indent;
    Polygon& old_polygon = m_source.polygons[old_polygon_id];
    for (Polygon_corner_id old_polygon_corner_id = old_polygon.first_polygon_corner_id,
         end = old_polygon.first_polygon_corner_id + old_polygon.corner_count;
         old_polygon_corner_id < end;
         ++old_polygon_corner_id)
    {
        Corner_id old_corner_id = m_source.polygon_corners[old_polygon_corner_id];
        Corner&   old_corner    = m_source.corners[old_corner_id];
        Point_id  old_point_id  = old_corner.point_id;
        Point_id  new_point_id  = m_point_old_to_new[old_point_id];
        Corner_id new_corner_id = m_destination.make_polygon_corner(new_polygon_id, new_point_id);
        add_corner_source(new_corner_id, 1.0f, old_corner_id);
    }
}

void Geometry_operation::add_point_source(Point_id new_point_id,
                                          float    weight,
                                          Point_id old_point_id)
{
    log_operation.trace("add_point_source        (new_point_id = {}, weight = {}, old_point_id = {})\n",
                        new_point_id, weight, old_point_id);
    erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(new_point_id);
    if (m_new_point_sources.size() <= i)
    {
        m_new_point_sources.resize(i + s_grow_size);
    }
    m_new_point_sources[i].emplace_back(weight, old_point_id);
}

void Geometry_operation::add_point_corner_source(Point_id  new_point_id,
                                                 float     weight,
                                                 Corner_id old_corner_id)
{
    log_operation.trace("add_point_corner_source (new_point_id = {}, weight = {}, old_corner_id = {})\n",
                        new_point_id, weight, old_corner_id);
    erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(new_point_id);
    if (m_new_point_corner_sources.size() <= i)
    {
        m_new_point_corner_sources.resize(i + s_grow_size);
    }
    m_new_point_corner_sources[new_point_id].emplace_back(weight, old_corner_id);
}

void Geometry_operation::add_corner_source(Corner_id new_corner_id,
                                           float     weight,
                                           Corner_id old_corner_id)
{
    log_operation.trace("add_corner_source (new_corner_id = {}, weight = {}, old_corner_id = {})\n",
                        new_corner_id, weight, old_corner_id);
    erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(new_corner_id);
    if (m_new_corner_sources.size() <= i)
    {
        m_new_corner_sources.resize(i + s_grow_size);
    }
    m_new_corner_sources[i].emplace_back(weight, old_corner_id);
}

void Geometry_operation::distribute_corner_sources(Corner_id new_corner_id,
                                                   float     weight,
                                                   Point_id  new_point_id)
{
    log_operation.trace("distribute_corner_sources (new_corner_id = {}, weight = {}, new_point_id = {})\n",
                        new_corner_id, weight, new_point_id);
    erhe::log::Log::Indenter scope_indent;
    auto sources = m_new_point_corner_sources[new_point_id];
    for (auto& source : sources)
    {
        add_corner_source(new_corner_id, weight * source.first, source.second);
    }
}

void Geometry_operation::add_polygon_source(Polygon_id new_polygon_id,
                                            float      weight,
                                            Polygon_id old_polygon_id)
{
    log_operation.trace("add_polygon_source (new_polygon_id = {}, weight = {}, old_polygon_id = {})\n",
                        new_polygon_id, weight, old_polygon_id);
    erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(new_polygon_id);
    if (m_new_polygon_sources.size() <= i)
    {
        m_new_polygon_sources.resize(i + s_grow_size);
    }
    m_new_polygon_sources[i].emplace_back(weight, old_polygon_id);
}

void Geometry_operation::add_edge_source(Edge_id new_edge_id,
                                         float   weight,
                                         Edge_id old_edge_id)
{
    log_operation.trace("add_edge_source (new_edge_id = {}, weight = {}, old_edge_id = {})\n",
                        new_edge_id, weight, old_edge_id);
    erhe::log::Log::Indenter scope_indent;
    size_t i = static_cast<size_t>(new_edge_id);
    if (m_new_edge_sources.size() <= i)
    {
        m_new_edge_sources.resize(i + s_grow_size);
    }
    m_new_edge_sources[i].emplace_back(weight, old_edge_id);
}

void Geometry_operation::build_destination_edges_with_sourcing()
{
    log_operation.trace("build_destination_edges_with_sourcing()\n");
    erhe::log::Log::Indenter scope_indent;

    m_destination.build_edges();

    for (Edge_id old_edge_id = 0,
         edge_end = m_source.edge_count();
         old_edge_id < edge_end;
         ++ old_edge_id)
    {
        Edge&    old_edge    = m_source.edges[old_edge_id];
        Point_id new_a       = m_point_old_to_new[old_edge.a];
        Point_id new_b       = m_point_old_to_new[old_edge.b];
        Point_id new_a_      = std::min(new_a, new_b);
        Point_id new_b_      = std::max(new_a, new_b);
        Edge_id  new_edge_id = m_destination.make_edge(new_a_, new_b_);
        add_edge_source(new_edge_id, 1.0f, old_edge_id);
        size_t i = static_cast<size_t>(old_edge_id);
        if (m_edge_old_to_new.size() <= i)
        {
            m_edge_old_to_new.resize(i + s_grow_size);
        }
        m_edge_old_to_new[i] = new_edge_id;
    }
}

void Geometry_operation::interpolate_all_property_maps()
{
    log_operation.trace("interpolate_all_property_maps()\n");
    erhe::log::Log::Indenter scope_indent;

    m_source.point_attributes()  .interpolate(m_destination.point_attributes(),   m_new_point_sources);
    m_source.polygon_attributes().interpolate(m_destination.polygon_attributes(), m_new_polygon_sources);
    m_source.corner_attributes() .interpolate(m_destination.corner_attributes(),  m_new_corner_sources);
    m_source.edge_attributes()   .interpolate(m_destination.edge_attributes(),    m_new_edge_sources);
}

} // namespace erhe::geometry::operation
