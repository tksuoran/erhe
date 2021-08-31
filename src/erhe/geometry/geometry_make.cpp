#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/log.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry
{

// Allocates new Corner / Corner_id
// - Point must be allocated.
// - Polygon must be allocated
auto Geometry::make_corner(const Point_id point_id, const Polygon_id polygon_id) -> Corner_id
{
    ZoneScoped;

    Expects(point_id < m_next_point_id);
    Expects(polygon_id < m_next_polygon_id);
    ++m_serial;
    const Corner_id corner_id = m_next_corner_id++;

    if (corner_id >= corners.size())
    {
        corners.resize(static_cast<size_t>(corner_id) + s_grow);
    }

    VERIFY(corner_id < corners.size());
    Corner& corner = corners[corner_id];
    corner.point_id   = point_id;
    corner.polygon_id = polygon_id;
    //log.trace("\tmake_corner(point_id = {}, polygon_id = {}) = corner_id = {}\n", point_id, polygon_id, corner_id);
    return corner_id;
}

// Allocates new Point / Point_id
auto Geometry::make_point() -> Point_id
{
    ZoneScoped;

    ++m_serial;

    const Point_id point_id = m_next_point_id++;

    // TODO
    if (point_id >= points.size())
    {
        points.resize(static_cast<size_t>(point_id) + s_grow);
    }

    VERIFY(point_id < points.size());
    //points[point_id].first_point_corner_id = m_next_point_corner;
    points[point_id].corner_count = 0;
    //log.trace("\tmake_point() point_id = {}\n", point_id);
    return point_id;
}

// Allocates new Polygon / Polygon_id
auto Geometry::make_polygon() -> Polygon_id
{
    ZoneScoped;

    ++m_serial;

    const Polygon_id polygon_id = m_next_polygon_id++;

    // TODO
    if (polygon_id >= polygons.size())
    {
        polygons.resize(static_cast<size_t>(polygon_id) + s_grow);
    }

    VERIFY(polygon_id < polygons.size());
    polygons[polygon_id].corner_count = 0;
    //log.trace("\tmake_polygon() polygon_id = {}\n", polygon_id);
    return polygon_id;
}

// Allocates new Edge / Edge_id
// - Points must be already allocated
// - Points must be ordered
auto Geometry::make_edge(const Point_id a, const Point_id b) -> Edge_id
{
    ZoneScoped;

    ++m_serial;

    VERIFY(a < b);
    //Expects(a < b);
    Expects(b < points.size());
    const Edge_id edge_id = m_next_edge_id++;

    if (edge_id >= edges.size())
    {
        edges.resize(static_cast<size_t>(edge_id) + s_grow);
    }

    VERIFY(edge_id < edges.size());
    Edge& edge = edges[edge_id];
    edge.a = a;
    edge.b = b;
    edge.first_edge_polygon_id = m_next_edge_polygon_id;
    edge.polygon_count = 0;
    //log.trace("\tmake_edge(a = {}, b = {}) edge_id = {}\n", a, b, edge_id);
    return edge_id;
}

// Allocates new point corner.
// - Point must be already allocated.
// - Corner must be already allocated.
void Geometry::reserve_point_corner(const Point_id point_id, const Corner_id corner_id)
{
    ZoneScoped;

    Expects(point_id < m_next_point_id);
    Expects(corner_id < m_next_corner_id);

    ++m_serial;

    Point& point = points[point_id];
    ++m_next_point_corner_reserve;
    point.reserved_corner_count++;
}

void Geometry::make_point_corners()
{
    ZoneScoped;

    ++m_serial;

    Point_corner_id next_point_corner = 0;
    for_each_point([&](auto& i)
    {
        i.point.first_point_corner_id = next_point_corner;
        i.point.corner_count = 0; // reset in case this has been done before
        next_point_corner += i.point.reserved_corner_count;
    });
    m_next_point_corner_reserve = next_point_corner;
    point_corners.resize(static_cast<size_t>(m_next_point_corner_reserve));
    for_each_polygon([&](auto& i)
    {
        i.polygon.for_each_corner(*this, [&](auto& j)
        {
            VERIFY(j.corner_id != std::numeric_limits<Corner_id>::max());
            VERIFY(j.corner_id < m_next_corner_id);
            const Point_id  point_id        = j.corner.point_id;
            VERIFY(point_id != std::numeric_limits<Point_id>::max());
            VERIFY(point_id < m_next_point_id);
            Point&          point           = j.geometry.points[point_id];
            Point_corner_id point_corner_id = point.first_point_corner_id + point.corner_count++;
            VERIFY(point_corner_id < point_corners.size());
            point_corners[point_corner_id] = j.corner_id;
        });
    });

    // Point corners have been added above in some non-specific order.
    sort_point_corners();
}

void Geometry::sort_point_corners()
{
    class Point_corner_info
    {
    public:
        Point_corner_id point_corner_id{0};
        Corner_id       corner_id{0};
        Point_id        point_ids[3];
        bool            used{false};
    };

    for_each_point([&](auto& i)
    {
        std::vector<Point_corner_info> point_corner_infos;

        i.point.for_each_corner(*this, [&](auto& j)
        {
            Corner_id        prev_corner_id   = 0;
            const Corner_id  middle_corner_id = j.corner_id; //point_corners[j.point_corner_id];
            Corner_id        next_corner_id   = 0;
            const Corner&    middle_corner    = j.corner; // corners[middle_corner_id];
            const Polygon_id polygon_id       = middle_corner.polygon_id;
            const Polygon&   polygon          = polygons[polygon_id];
            bool             found            = false;
            polygon.for_each_corner_neighborhood_const(*this, [&](auto& k)
            {
                if (k.corner_id == middle_corner_id)
                {
                    prev_corner_id = k.prev_corner_id;
                    next_corner_id = k.next_corner_id;
                    found          = true;
                    return k.break_iteration();
                }
            });
            if (!found)
            {
                return;
            }
            const Corner& prev_corner = corners[prev_corner_id];
            const Corner& next_corner = corners[next_corner_id];

            Point_corner_info point_corner_info;
            point_corner_info.point_corner_id = j.point_corner_id;
            point_corner_info.corner_id       = middle_corner_id;
            point_corner_info.point_ids[0]    = prev_corner  .point_id;
            point_corner_info.point_ids[1]    = middle_corner.point_id;
            point_corner_info.point_ids[2]    = next_corner  .point_id;

            point_corner_infos.push_back(point_corner_info);
        });

        for (uint32_t j = 0, end = static_cast<uint32_t>(point_corner_infos.size()); j < end; ++j)
        {
            const uint32_t     next_j = (j + 1) % end;
            Point_corner_info& head   = point_corner_infos[j];
            Point_corner_info& next   = point_corner_infos[next_j];
            bool found{false};
            for (uint32_t k = 0; k < end; ++k)
            {
                Point_corner_info& node = point_corner_infos[k];
                if (node.used)
                {
                    continue;
                }
                if (node.point_ids[2] == head.point_ids[0])
                {
                    found = true;
                    node.used = true;
                    if (k != next_j)
                    {
                        std::swap(next, node);
                    }
                    break;
                }
            }
            if (!found)
            {
                log_geometry.trace("Could not sort point corners\n");
            }
            const Point_corner_id point_corner_id = i.point.first_point_corner_id + j;
            point_corners[point_corner_id] = head.corner_id;
        }
    });
}

// Allocates new edge polygon.
// - Edge must be already allocated.
// - Polygon must be already allocated.
auto Geometry::make_edge_polygon(const Edge_id edge_id, const Polygon_id polygon_id) -> Edge_polygon_id
{
    ZoneScoped;

    Expects(edge_id < m_next_edge_id);
    Expects(polygon_id < m_next_polygon_id);

    ++m_serial;

    Edge& edge = edges[edge_id];
    if (edge.polygon_count == 0)
    {
        edge.first_edge_polygon_id = m_next_edge_polygon_id;
        m_edge_polygon_edge = edge_id;
    }
    else
    {
        VERIFY(m_edge_polygon_edge == edge_id);
    }
    ++m_next_edge_polygon_id;
    const Edge_polygon_id edge_polygon_id = edge.first_edge_polygon_id + edge.polygon_count;
    ++edge.polygon_count;
    //log.trace("\tmake_edge_polygon(edge = {}, polygon = {}) first_edge_polygon_id = {}, edge.polygon_count = {}\n",
    //          edge_id, polygon_id, edge.first_edge_polygon_id, edge.polygon_count);

    if (edge_polygon_id >= edge_polygons.size())
    {
        edge_polygons.resize(edge_polygon_id + s_grow);
    }

    edge_polygons[edge_polygon_id] = polygon_id;
    return edge_polygon_id;
}

// Allocates new polygon corner.
// - Polygon must be already allocated.
// - Corner must be already allocated.
auto Geometry::make_polygon_corner_(const Polygon_id polygon_id, const Corner_id corner_id) -> Polygon_corner_id
{
    ZoneScoped;

    Expects(polygon_id < m_next_polygon_id);
    Expects(corner_id < m_next_corner_id);

    ++m_serial;

    Polygon& polygon = polygons[polygon_id];
    if (polygon.corner_count == 0)
    {
        polygon.first_polygon_corner_id = m_next_polygon_corner_id;
        m_polygon_corner_polygon = polygon_id;
    }
    else
    {
        VERIFY(m_polygon_corner_polygon == polygon_id);
    }
    ++m_next_polygon_corner_id;
    const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + polygon.corner_count;
    ++polygon.corner_count;

    //log.trace("\tmake_polygon_corner_(polygon_id = {}, corner_id = {}) first_polygon_corner_id = {}, polygon.corner_count = {}\n",
    //          polygon_id, corner_id, polygon.first_polygon_corner_id, polygon.corner_count);

    if (polygon_corner_id >= polygon_corners.size())
    {
        polygon_corners.resize(polygon_corner_id + s_grow);
    }

    polygon_corners[polygon_corner_id] = corner_id;
    return polygon_corner_id;
}

// Allocates new polygon corner.
// - Polygon must be already allocated.
// - Point must be already allocated.
auto Geometry::make_polygon_corner(const Polygon_id polygon_id, const Point_id point_id) -> Corner_id
{
    ZoneScoped;

    Expects(polygon_id < m_next_polygon_id);
    Expects(point_id < m_next_point_id);

    ++m_serial;

    const Corner_id corner_id = make_corner(point_id, polygon_id);
    make_polygon_corner_(polygon_id, corner_id);
    reserve_point_corner(point_id, corner_id);
    //log.trace("polygon {} adding point {} as corner {}\n", polygon_id, point_id, corner_id);
    return corner_id;
}

auto Geometry::make_point(const float x, const float y, const float z)
-> Point_id
{
    ZoneScoped;

    const Point_id point_id        = make_point();
    auto* const    point_positions = point_attributes().find_or_create<glm::vec3>(c_point_locations);

    point_positions->put(point_id, glm::vec3(x, y, z));

    return point_id;
}

auto Geometry::make_point(const float x, const float y, const float z, const float s, const float t)
-> Point_id
{
    ZoneScoped;

    const Point_id point_id        = make_point();
    auto* const    point_positions = point_attributes().find_or_create<glm::vec3>(c_point_locations);
    auto* const    point_texcoords = point_attributes().find_or_create<glm::vec2>(c_point_texcoords);

    point_positions->put(point_id, glm::vec3(x, y, z));
    point_texcoords->put(point_id, glm::vec2(s, t));

    return point_id;
}

auto Geometry::make_point(const double x, const double y, const double z)
-> Point_id
{
    return make_point(float(x), float(y), float(z));
}

auto Geometry::make_point(const double x, const double y, const double z, const double s, const double t)
-> Point_id
{
    return make_point(float(x), float(y), float(z), float(s), float(t));
}

auto Geometry::make_polygon(const std::initializer_list<Point_id> point_list)
-> Polygon_id
{
    ZoneScoped;

    const Polygon_id polygon_id = make_polygon();
    for (Point_id point_id : point_list)
    {
        make_polygon_corner(polygon_id, point_id);
    }
    return polygon_id;
}


}
