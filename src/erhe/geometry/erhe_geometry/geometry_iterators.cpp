#include "erhe_geometry/geometry.hpp"

namespace erhe::geometry
{

void Geometry::for_each_corner(
    std::function<void(Corner_context&)> callback
)
{
    for (
        Corner_id corner_id = 0, end = get_corner_count();
        corner_id < end;
        ++corner_id
    ) {
        Corner& corner = corners[corner_id];
        Corner_context context{
            .corner_id = corner_id,
            .corner    = corner
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Geometry::for_each_corner_const(
    std::function<void(Corner_context_const&)> callback
) const
{
    for (
        Corner_id corner_id = 0, end = get_corner_count();
        corner_id < end;
        ++corner_id
    ) {
        const Corner& corner = corners[corner_id];
        Corner_context_const context{
            .corner_id = corner_id,
            .corner    = corner
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Geometry::for_each_point(
    std::function<void(Point_context&)> callback
)
{
    for (
        Point_id point_id = 0, end = get_point_count();
        point_id < end;
        ++point_id
    ) {
        Point& point = points[point_id];
        Point_context context{
            .point_id = point_id,
            .point    = point
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Geometry::for_each_point_const(
    std::function<void(Point_context_const&)> callback
) const
{
    for (
        Point_id point_id = 0, end = get_point_count();
        point_id < end;
        ++point_id
    ) {
        const Point& point = points[point_id];
        Point_context_const context{
            .point_id = point_id,
            .point    = point
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Geometry::for_each_polygon(
    std::function<void(Polygon_context&)> callback
)
{
    for (
        Polygon_id polygon_id = 0, end = get_polygon_count();
        polygon_id < end;
        ++polygon_id
    ) {
        Polygon& polygon = polygons[polygon_id];
        Polygon_context context{
            .polygon_id = polygon_id,
            .polygon    = polygon
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Geometry::for_each_polygon_const(
    std::function<void(Polygon_context_const&)> callback
) const
{
    for (
        Polygon_id polygon_id = 0, end = get_polygon_count();
        polygon_id < end;
        ++polygon_id
    ) {
        const Polygon& polygon = polygons[polygon_id];
        Polygon_context_const context{
            .polygon_id = polygon_id,
            .polygon    = polygon
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Geometry::for_each_edge(
    std::function<void(Edge_context&)> callback
)
{
    for (
        Edge_id edge_id = 0, end = get_edge_count();
        edge_id < end;
        ++edge_id
    ) {
        Edge& edge = edges[edge_id];
        Edge_context context{
            .edge_id = edge_id,
            .edge    = edge
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Geometry::for_each_edge_const(
    std::function<void(Edge_context_const&)> callback
) const
{
    for (
        Edge_id edge_id = 0, end = get_edge_count();
        edge_id < end;
        ++edge_id
    ) {
        const Edge& edge = edges[edge_id];
        Edge_context_const context{
            .edge_id = edge_id,
            .edge    = edge
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Point::for_each_corner(
    Geometry&                                  geometry,
    std::function<void(Point_corner_context&)> callback
)
{
    for (
        Point_corner_id point_corner_id = first_point_corner_id,
        end = first_point_corner_id + corner_count;
        point_corner_id < end;
        ++point_corner_id
    ) {
        Corner_id& corner_id = geometry.point_corners[point_corner_id];
        Point_corner_context context{
            .geometry        = geometry,
            .point_corner_id = point_corner_id,
            .corner_id       = corner_id,
            .corner          = geometry.corners[corner_id]
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Point::for_each_corner_const(
    const Geometry&                                  geometry,
    std::function<void(Point_corner_context_const&)> callback
) const
{
    for (
        Point_corner_id point_corner_id = first_point_corner_id,
        end = first_point_corner_id + corner_count;
        point_corner_id < end;
        ++point_corner_id
    ) {
        const Corner_id& corner_id = geometry.point_corners[point_corner_id];
        Point_corner_context_const context{
            .geometry        = geometry,
            .point_corner_id = point_corner_id,
            .corner_id       = corner_id,
            .corner          = geometry.corners[corner_id]
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Point::for_each_corner_neighborhood(
    Geometry&                                               geometry,
    std::function<void(Point_corner_neighborhood_context&)> callback
)
{
    for (uint32_t i = 0; i < corner_count; ++i) {
        const Point_corner_id prev_point_corner_id = first_point_corner_id + (corner_count + i - 1) % corner_count;
        const Point_corner_id point_corner_id      = first_point_corner_id + i;
        const Point_corner_id next_point_corner_id = first_point_corner_id + (i + 1) % corner_count;
        const Corner_id       prev_corner_id       = geometry.point_corners[prev_point_corner_id];
        const Corner_id       corner_id            = geometry.point_corners[point_corner_id];
        const Corner_id       next_corner_id       = geometry.point_corners[next_point_corner_id];
        Point_corner_neighborhood_context context{
            .geometry             = geometry,
            .prev_point_corner_id = prev_point_corner_id,
            .point_corner_id      = point_corner_id,
            .next_point_corner_id = next_point_corner_id,
            .prev_corner_id       = geometry.point_corners[prev_point_corner_id],
            .corner_id            = geometry.point_corners[point_corner_id],
            .next_corner_id       = geometry.point_corners[next_point_corner_id],
            .prev_corner          = geometry.corners[prev_corner_id],
            .corner               = geometry.corners[corner_id],
            .next_corner          = geometry.corners[next_corner_id]
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Point::for_each_corner_neighborhood_const(
    const Geometry&                                               geometry,
    std::function<void(Point_corner_neighborhood_context_const&)> callback
) const
{
    for (uint32_t i = 0; i < corner_count; ++i) {
        const Point_corner_id prev_point_corner_id = first_point_corner_id + (corner_count + i - 1) % corner_count;
        const Point_corner_id point_corner_id      = first_point_corner_id + i;
        const Point_corner_id next_point_corner_id = first_point_corner_id + (i + 1) % corner_count;
        const Corner_id       prev_corner_id       = geometry.point_corners[prev_point_corner_id];
        const Corner_id       corner_id            = geometry.point_corners[point_corner_id];
        const Corner_id       next_corner_id       = geometry.point_corners[next_point_corner_id];
        Point_corner_neighborhood_context_const context{
            .geometry             = geometry,
            .prev_point_corner_id = prev_point_corner_id,
            .point_corner_id      = point_corner_id,
            .next_point_corner_id = next_point_corner_id,
            .prev_corner_id       = prev_corner_id,
            .corner_id            = corner_id,
            .next_corner_id       = next_corner_id,
            .prev_corner          = geometry.corners[prev_corner_id],
            .corner               = geometry.corners[corner_id],
            .next_corner          = geometry.corners[next_corner_id]
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Polygon::for_each_corner(
    Geometry&                                    geometry,
    std::function<void(Polygon_corner_context&)> callback
)
{
    for (
        Polygon_corner_id polygon_corner_id = first_polygon_corner_id,
        end = first_polygon_corner_id + corner_count;
        polygon_corner_id < end;
        ++polygon_corner_id
    ) {
        const Corner_id corner_id = geometry.polygon_corners[polygon_corner_id];
        Polygon_corner_context context{
            .geometry          = geometry,
            .polygon_corner_id = polygon_corner_id,
            .corner_id         = corner_id,
            .corner            = geometry.corners[corner_id]
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Polygon::for_each_corner_const(
    const Geometry&                                    geometry,
    std::function<void(Polygon_corner_context_const&)> callback
) const
{
    for (
        Polygon_corner_id polygon_corner_id = first_polygon_corner_id,
        end = first_polygon_corner_id + corner_count;
        polygon_corner_id < end;
        ++polygon_corner_id
    ) {
        const Corner_id corner_id = geometry.polygon_corners[polygon_corner_id];
        Polygon_corner_context_const context{
            .geometry          = geometry,
            .polygon_corner_id = polygon_corner_id,
            .corner_id         = corner_id,
            .corner            = geometry.corners[corner_id]
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Polygon::for_each_corner_neighborhood(
    Geometry&                                                 geometry,
    std::function<void(Polygon_corner_neighborhood_context&)> callback
)
{
    for (uint32_t i = 0; i < corner_count; ++i) {
        const Polygon_corner_id prev_polygon_corner_id = first_polygon_corner_id + (corner_count + i - 1) % corner_count;
        const Polygon_corner_id polygon_corner_id      = first_polygon_corner_id + i;
        const Polygon_corner_id next_polygon_corner_id = first_polygon_corner_id + (i + 1) % corner_count;
        const Corner_id         prev_corner_id         = geometry.polygon_corners[prev_polygon_corner_id];
        const Corner_id         corner_id              = geometry.polygon_corners[polygon_corner_id];
        const Corner_id         next_corner_id         = geometry.polygon_corners[next_polygon_corner_id];
        Polygon_corner_neighborhood_context context{
            .geometry               = geometry,
            .prev_polygon_corner_id = prev_polygon_corner_id,
            .polygon_corner_id      = polygon_corner_id,
            .next_polygon_corner_id = next_polygon_corner_id,
            .prev_corner_id         = prev_corner_id,
            .corner_id              = corner_id,
            .next_corner_id         = next_corner_id,
            .prev_corner            = geometry.corners[prev_corner_id],
            .corner                 = geometry.corners[corner_id],
            .next_corner            = geometry.corners[next_corner_id]
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Polygon::for_each_corner_neighborhood_const(
    const Geometry&                                                 geometry,
    std::function<void(Polygon_corner_neighborhood_context_const&)> callback
) const
{
    for (uint32_t i = 0; i < corner_count; ++i) {
        const Polygon_corner_id prev_polygon_corner_id = first_polygon_corner_id + (corner_count + i - 1) % corner_count;
        const Polygon_corner_id polygon_corner_id      = first_polygon_corner_id + i;
        const Polygon_corner_id next_polygon_corner_id = first_polygon_corner_id + (i + 1) % corner_count;
        const Corner_id         prev_corner_id         = geometry.polygon_corners[prev_polygon_corner_id];
        const Corner_id         corner_id              = geometry.polygon_corners[polygon_corner_id];
        const Corner_id         next_corner_id         = geometry.polygon_corners[next_polygon_corner_id];
        Polygon_corner_neighborhood_context_const context{
            .geometry               = geometry,
            .prev_polygon_corner_id = prev_polygon_corner_id,
            .polygon_corner_id      = polygon_corner_id,
            .next_polygon_corner_id = next_polygon_corner_id,
            .prev_corner_id         = prev_corner_id,
            .corner_id              = corner_id,
            .next_corner_id         = next_corner_id,
            .prev_corner            = geometry.corners[prev_corner_id],
            .corner                 = geometry.corners[corner_id],
            .next_corner            = geometry.corners[next_corner_id]
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Edge::for_each_polygon(
    Geometry&                                  geometry,
    std::function<void(Edge_polygon_context&)> callback)
{
    for (
        Edge_polygon_id edge_polygon_id = first_edge_polygon_id,
        end = first_edge_polygon_id + polygon_count;
        edge_polygon_id < end;
        ++edge_polygon_id
    ) {
        const Polygon_id polygon_id = geometry.edge_polygons[edge_polygon_id];
        Edge_polygon_context context{
            .geometry        = geometry,
            .edge_polygon_id = edge_polygon_id,
            .polygon_id      = polygon_id,
            .polygon         = geometry.polygons[polygon_id]
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

void Edge::for_each_polygon_const(
    const Geometry&                                  geometry,
    std::function<void(Edge_polygon_context_const&)> callback
) const
{
    for (
        Edge_polygon_id edge_polygon_id = first_edge_polygon_id,
        end = first_edge_polygon_id + polygon_count;
        edge_polygon_id < end;
        ++edge_polygon_id
    ) {
        const Polygon_id polygon_id = geometry.edge_polygons.at(edge_polygon_id);
        Edge_polygon_context_const context{
            .geometry        = geometry,
            .edge_polygon_id = edge_polygon_id,
            .polygon_id      = polygon_id,
            .polygon         = geometry.polygons.at(polygon_id)
        };
        callback(context);
        if (context.break_) {
            return;
        }
    }
}

}
