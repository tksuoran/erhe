#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/mikktspace.h"
#include "erhe/toolkit/math_util.hpp"

#include "erhe/geometry/mikktspace.h"

#include "Tracy.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <stdexcept>

namespace erhe::geometry
{

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::mat4;

// Allocates new Corner / Corner_id
// - Point must be allocated.
// - Polygon must be allocated
auto Geometry::make_corner(Point_id point_id, Polygon_id polygon_id) -> Corner_id
{
    ZoneScoped;

    Expects(point_id < m_next_point_id);
    Expects(polygon_id < m_next_polygon_id);
    ++m_serial;
    Corner_id corner_id = m_next_corner_id++;

    if (corner_id >= corners.size()) {
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

    Point_id point_id = m_next_point_id++;

    // TODO
    if (point_id >= points.size()) {
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

    Polygon_id polygon_id = m_next_polygon_id++;

    // TODO
    if (polygon_id >= polygons.size()) {
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
auto Geometry::make_edge(Point_id a, Point_id b) -> Edge_id
{
    ZoneScoped;

    ++m_serial;

    VERIFY(a < b);
    //Expects(a < b);
    Expects(b < points.size());
    Edge_id edge_id = m_next_edge_id++;

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
void Geometry::reserve_point_corner(Point_id point_id, Corner_id corner_id)
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

    point_corners.resize(static_cast<size_t>(m_next_point_corner_reserve));

    Point_corner_id next_point_corner = 0;
    for (Point_id point_id = 0; point_id < m_next_point_id; ++point_id)
    {
        Point& point = points[point_id];
        point.first_point_corner_id = next_point_corner;
        next_point_corner += point.reserved_corner_count;
    }
    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        Polygon& polygon = polygons[polygon_id];
        for (Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
             polygon_corner_end = polygon.first_polygon_corner_id + polygon.corner_count;
             polygon_corner_id < polygon_corner_end;
             ++polygon_corner_id)
        {
            Corner_id       corner_id       = polygon_corners[polygon_corner_id];
            Corner&         corner          = corners[corner_id];
            Point_id        point_id        = corner.point_id;
            Point&          point           = points[point_id];
            Point_corner_id point_corner_id = point.first_point_corner_id + point.corner_count++;
            VERIFY(point_corner_id < point_corners.size());
            point_corners[point_corner_id] = corner_id;
        }
    }

    // Sort point corners.
    // Point corners have been added above in some non-specific order.
    for (Point_id point_id = 0; point_id < m_next_point_id; ++point_id)
    {
        Point& point = points[point_id];
        struct Point_corner_context
        {
            Point_corner_id point_corner_id;
            Corner_id corner_id;
            Point_id point_ids[3];
            bool used{false};
        };

        // Collect point corner context
        std::vector<Point_corner_context> point_corner_contexts;
        for (Point_corner_id point_corner_id = point.first_point_corner_id,
             point_corner_end = point.first_point_corner_id + point.corner_count;
             point_corner_id < point_corner_end;
             ++point_corner_id)
        {
            Corner_id  prev_corner_id   = 0;
            Corner_id  middle_corner_id = point_corners[point_corner_id];
            Corner_id  next_corner_id   = 0;
            Corner&    middle_corner    = corners[middle_corner_id];
            Polygon_id polygon_id       = middle_corner.polygon_id;
            Polygon&   polygon          = polygons[polygon_id];
            bool       found            = false;
            for (uint32_t i = 0; i < polygon.corner_count; ++i)
            {
                Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                Corner_id corner_id = polygon_corners[polygon_corner_id];
                if (corner_id == middle_corner_id)
                {
                    Polygon_corner_id prev_polygon_corner_id = polygon.first_polygon_corner_id + (polygon.corner_count + i - 1) % polygon.corner_count;
                    Polygon_corner_id next_polygon_corner_id = polygon.first_polygon_corner_id + (i + 1) % polygon.corner_count;
                    prev_corner_id = polygon_corners[prev_polygon_corner_id];
                    next_corner_id = polygon_corners[next_polygon_corner_id];
                    found          = true;
                    break;
                }
            }
            if (!found)
            {
                continue;
            }
            Corner& prev_corner = corners[prev_corner_id];
            Corner& next_corner = corners[next_corner_id];

            Point_corner_context point_corner_context;
            point_corner_context.point_corner_id = point_corner_id;
            point_corner_context.corner_id       = middle_corner_id;
            point_corner_context.point_ids[0]    = prev_corner  .point_id;
            point_corner_context.point_ids[1]    = middle_corner.point_id;
            point_corner_context.point_ids[2]    = next_corner  .point_id;

            point_corner_contexts.push_back(point_corner_context);
        }

        for (uint32_t i = 0, end = static_cast<uint32_t>(point_corner_contexts.size()); i < end; ++i)
        {
            uint32_t next_i = (i + 1) % end;
            Point_corner_context& head = point_corner_contexts[i];
            Point_corner_context& next = point_corner_contexts[next_i];
            bool found{false};
            for (uint32_t j = 0; j < end; ++j)
            {
                Point_corner_context& node = point_corner_contexts[j];
                if (node.used)
                {
                    continue;
                }
                if (node.point_ids[2] == head.point_ids[0])
                {
                    found = true;
                    node.used = true;
                    if (j != next_i)
                    {
                        std::swap(next, node);
                    }
                    break;
                }
            }
            if (!found) {
                log.trace("Could not sort point corners\n");
            }
            Point_corner_id point_corner_id = point.first_point_corner_id + i;
            point_corners[point_corner_id] = head.corner_id;
        }
        
    }
}

// Allocates new edge polygon.
// - Edge must be already allocated.
// - Polygon must be already allocated.
auto Geometry::make_edge_polygon(Edge_id edge_id, Polygon_id polygon_id) -> Edge_polygon_id
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
    Edge_polygon_id edge_polygon_id = edge.first_edge_polygon_id + edge.polygon_count;
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
auto Geometry::make_polygon_corner_(Polygon_id polygon_id, Corner_id corner_id) -> Polygon_corner_id
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
    Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + polygon.corner_count;
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
auto Geometry::make_polygon_corner(Polygon_id polygon_id, Point_id point_id) -> Corner_id
{
    ZoneScoped;

    Expects(polygon_id < m_next_polygon_id);
    Expects(point_id < m_next_point_id);

    ++m_serial;

    Corner_id corner_id = make_corner(point_id, polygon_id);
    make_polygon_corner_(polygon_id, corner_id);
    reserve_point_corner(point_id, corner_id);
    //log.trace("polygon {} adding point {} as corner {}\n", polygon_id, point_id, corner_id);
    return corner_id;
}

auto Geometry::count_polygon_triangles() const
-> size_t
{
    ZoneScoped;

    size_t triangle_count{0};
    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        if (polygons[polygon_id].corner_count < 2)
        {
            continue;
        }
        triangle_count += polygons[polygon_id].corner_count - 2;
    }

    return triangle_count;
}

void Geometry::info(Mesh_info& info) const
{
    info.polygon_count  = polygon_count();
    info.corner_count   = corner_count();
    info.triangle_count = count_polygon_triangles();
    info.edge_count     = edge_count();

    // Additional vertices needed for centroids
    // 3 indices per triangle after triangulation, 2 indices per edge, 1 per corner, 1 index per centroid
    info.vertex_count_corners        = info.corner_count;
    info.vertex_count_centroids      = info.polygon_count;
    info.index_count_fill_triangles  = 3 * info.triangle_count;
    info.index_count_edge_lines      = 2 * info.edge_count;
    info.index_count_corner_points   = info.corner_count;
    info.index_count_centroid_points = info.polygon_count;
    info.index_count_centroid_points = info.polygon_count;
}

auto Geometry::Mesh_info::operator+=(const Geometry::Mesh_info& o)
-> Geometry::Mesh_info&
{
    polygon_count               += o.polygon_count;
    corner_count                += o.corner_count;
    triangle_count              += o.triangle_count;
    edge_count                  += o.edge_count;
    vertex_count_corners        += o.vertex_count_corners;
    vertex_count_centroids      += o.vertex_count_centroids;
    index_count_fill_triangles  += o.index_count_fill_triangles;
    index_count_edge_lines      += o.index_count_edge_lines;
    index_count_corner_points   += o.index_count_corner_points;
    index_count_centroid_points += o.index_count_centroid_points;
    return *this;
}

void Geometry::reserve_points(size_t point_count)
{
    ZoneScoped;

    if (point_count > points.size())
    {
        points.reserve(point_count);
    }
}

void Geometry::reserve_polygons(size_t polygon_count)
{
    ZoneScoped;

    if (polygon_count > polygons.size())
    {
        polygons.reserve(polygon_count);
    }
}

// Requires point locations
auto Geometry::compute_polygon_normals() -> bool
{
    ZoneScoped;

    if (m_serial_polygon_normals == m_serial)
    {
        return true;
    }

    if (m_next_polygon_id == 0)
    {
        return true;
    }

    log.info("{} for {}\n", __func__, m_name);

    auto* polygon_normals = polygon_attributes().find_or_create<vec3>(c_polygon_normals);
    auto* point_locations = point_attributes()  .find          <vec3>(c_point_locations);

    if (point_locations == nullptr)
    {
        log.warn("{} {}: Point locations are required, but not found.\n", __func__, m_name);
        return false;
    }

    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        Polygon& polygon = polygons[polygon_id];
        polygon.compute_normal(polygon_id, *this, *polygon_normals, *point_locations);
    }

    m_serial_polygon_normals = m_serial;

    return true;
}

auto Geometry::compute_polygon_centroids() -> bool
{
    ZoneScoped;

    if (m_serial_polygon_centroids == m_serial)
    {
        return true;
    }

    if (m_next_polygon_id == 0)
    {
        return true;
    }

    log.info("{} for {}\n", __func__, m_name);

    auto* polygon_centroids = polygon_attributes().find_or_create<vec3>(c_polygon_centroids);
    auto* point_locations   = point_attributes()  .find          <vec3>(c_point_locations);

    if (point_locations == nullptr)
    {
        log.warn("{} {}: Point locations are required, but not found.\n", __func__, m_name);
        return false;
    }

    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        Polygon& polygon = polygons[polygon_id];
        polygon.compute_centroid(polygon_id, *this, *polygon_centroids, *point_locations);
    }

    m_serial_polygon_centroids = m_serial;

    return true;
}

void Geometry::build_edges()
{
    ZoneScoped;

    if (m_serial_edges == m_serial)
    {
        return;
    }
    if (m_next_polygon_id == 0)
    {
        return;
    }

    edges.clear();
    m_next_edge_id = 0;

    log_build_edges.info("build_edges() : {} polygons\n", m_next_polygon_id);
    erhe::log::Log::Indenter scope_indent;

    size_t polygon_index{0};
    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        Polygon& polygon = polygons[polygon_id];
        erhe::log::Log::Indenter scope_indent;

        for (uint32_t polygon_corner_index = 0; polygon_corner_index < polygon.corner_count; ++polygon_corner_index)
        {
            Polygon_corner_id previous_polygon_corner_id    = polygon.first_polygon_corner_id + (polygon.corner_count + polygon_corner_index - 1) % polygon.corner_count;
            Polygon_corner_id polygon_corner_id             = polygon.first_polygon_corner_id + polygon_corner_index;
            Corner_id         previous_corner_id_in_polygon = polygon_corners[previous_polygon_corner_id];
            Corner_id         corner_id_in_polygon          = polygon_corners[polygon_corner_id];
            Corner&           previous_corner_in_polygon    = corners[previous_corner_id_in_polygon];
            Corner&           corner_in_polygon             = corners[corner_id_in_polygon];
            Point_id          a                             = previous_corner_in_polygon.point_id;
            Point_id          b                             = corner_in_polygon.point_id;
            if (a == b)
            {
                log_build_edges.warn("Bad edge {} - {}\n", a, b);
                continue;
            }
            VERIFY(a != b);
            if (a < b)
            {
                Edge_id edge_id = make_edge(a, b);
                Point& pa = points[a];
                make_edge_polygon(edge_id, polygon_id);
                VERIFY(pa.corner_count > 0);
                for (Point_corner_id point_corner_id = pa.first_point_corner_id,
                     point_corner_end = pa.first_point_corner_id + pa.corner_count;
                     point_corner_id < point_corner_end;
                     ++point_corner_id)
                {
                    Corner_id  corner_id_in_point  = point_corners[point_corner_id];
                    Corner&    corner_in_point     = corners[corner_id_in_point];
                    Polygon_id polygon_id_in_point = corner_in_point.polygon_id;
                    Polygon&   polygon_in_point    = polygons[polygon_id_in_point];
                    Corner_id  prev_corner_id      = polygon_in_point.prev_corner(*this, corner_id_in_point);
                    Corner&    prev_corner         = corners[prev_corner_id];
                    Point_id   prev_point_id       = prev_corner.point_id;
                    if (prev_point_id == b)
                    {
                        make_edge_polygon(edge_id, polygon_id_in_point);
                        ++polygon_index;
                    }

                }
            }
        }
    }

    m_serial_edges = m_serial;
}

void Geometry::debug_trace()
{
    ZoneScoped;

    for (Point_id point_id = 0; point_id < m_next_point_id; ++point_id)
    {
        log.trace("point {:3} = ", point_id);
        Point& point = points[point_id];
        for (Point_corner_id point_corner_id = point.first_point_corner_id;
             point_corner_id < point.first_point_corner_id + point.corner_count;
             ++point_corner_id)
        {
            Corner_id corner_id = point_corners[point_corner_id];
            Corner&   corner    = corners[corner_id];
            if (point_corner_id > point.first_point_corner_id)
            {
                log.trace(", ");
            }
            log.trace("corner {:3} polygon {:3}", corner_id, corner.polygon_id);
        }
        log.trace("\n");
    }

    for (Polygon_id polygon_id = 0, end = m_next_polygon_id;
         polygon_id < end;
         ++polygon_id)
    {
        const Polygon& polygon = polygons[polygon_id];
        log.trace("polygon {:3} = ", polygon_id);
        for (Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
             end = polygon.first_polygon_corner_id + polygon.corner_count;
             polygon_corner_id < end;
             ++polygon_corner_id)
        {
            Corner_id     corner_id = polygon_corners[polygon_corner_id];
            const Corner& corner    = corners[corner_id];
            Point_id      point_id  = corner.point_id;
            if (polygon_corner_id > polygon.first_polygon_corner_id)
            {
                log.trace(", ");
            }
            log.trace("corner {:3} point {:3}", corner_id, point_id);
        }
        log.trace("\n");
    }

    for (Edge_id edge_id = 0; edge_id < m_next_edge_id; ++edge_id)
    {
        const Edge& edge = edges[edge_id];
        log.trace("edge {:3} = {:3} .. {:3} :", edge_id, edge.a, edge.b);
        for (Edge_polygon_id edge_polygon_id = edge.first_edge_polygon_id,
             end = edge.first_edge_polygon_id + edge.polygon_count;
             edge_polygon_id < end;
             ++edge_polygon_id)
        {
            Polygon_id polygon_id = edge_polygons[edge_polygon_id];
            log.trace("{:3} ", polygon_id);
        }
        log.trace("\n");
    }
}

auto Geometry::compute_point_normal(Point_id point_id)
-> vec3
{
    ZoneScoped;

    Expects(point_id < points.size());

    auto* polygon_normals = polygon_attributes().find_or_create<vec3>(c_polygon_normals);

    vec3 normal_sum(0.0f, 0.0f, 0.0f);

    Point& point = points[point_id];
    for (Point_corner_id point_corner_id = point.first_point_corner_id,
        end = point.first_point_corner_id + point.corner_count;
        point_corner_id < end;
        ++point_corner_id)
    {
        Corner_id corner_id = point_corners[point_corner_id];
        Corner&   corner    = corners[corner_id];
        if (polygon_normals->has(corner.polygon_id))
        {
            normal_sum += polygon_normals->get(corner.polygon_id);
        }
        else
        {
            // TODO on demand calculate polygon normal?
        }
    }
    return normalize(normal_sum);
}

auto Geometry::compute_point_normals(const Property_map_descriptor& descriptor) -> bool
{
    ZoneScoped;

    if (m_serial_point_normals == m_serial)
    {
        return true;
    }

    auto* point_normals   = point_attributes().find_or_create<vec3>(descriptor);
    auto* polygon_normals = polygon_attributes().find<vec3>(c_polygon_normals);
    if (polygon_normals == nullptr)
    {
        bool polygon_normals_ok = compute_polygon_normals();
        if (!polygon_normals_ok)
        {
            return false;
        }
        polygon_normals = polygon_attributes().find<vec3>(c_polygon_normals);
    }

    point_normals->clear();
    for (Point_id point_id = 0; point_id < m_next_point_id; ++point_id)
    {
        vec3 normal_sum(0.0f, 0.0f, 0.0f);
        Point& point = points[point_id];
        for (Point_corner_id point_corner_id = point.first_point_corner_id,
            end = point.first_point_corner_id + point.corner_count;
            point_corner_id < end;
            ++point_corner_id)
        {
            Corner_id corner_id = point_corners[point_corner_id];
            Corner&   corner    = corners[corner_id];
            if (polygon_normals->has(corner.polygon_id))
            {
                normal_sum += polygon_normals->get(corner.polygon_id);
            }
            // TODO else
        }
        point_normals->put(point_id, normalize(normal_sum));
    }

    m_serial_point_normals = m_serial;
    return true;
}

auto Geometry::make_point(float x, float y, float z)
-> Point_id
{
    ZoneScoped;

    Point_id point_id        = make_point();
    auto*    point_positions = point_attributes().find_or_create<vec3>(c_point_locations);

    point_positions->put(point_id, vec3(x, y, z));

    return point_id;
}

auto Geometry::make_point(float x, float y, float z, float s, float t)
-> Point_id
{
    ZoneScoped;

    Point_id point_id        = make_point();
    auto*    point_positions = point_attributes().find_or_create<vec3>(c_point_locations);
    auto*    point_texcoords = point_attributes().find_or_create<vec2>(c_point_texcoords);

    point_positions->put(point_id, vec3(x, y, z));
    point_texcoords->put(point_id, vec2(s, t));

    return point_id;
}

auto Geometry::make_point(double x, double y, double z)
-> Point_id
{
    return make_point(float(x), float(y), float(z));
}

auto Geometry::make_point(double x, double y, double z, double s, double t)
-> Point_id
{
    return make_point(float(x), float(y), float(z), float(s), float(t));
}

auto Geometry::make_polygon(const std::initializer_list<Point_id> point_list)
-> Polygon_id
{
    ZoneScoped;

    Polygon_id polygon_id = make_polygon();
    for (Point_id point_id : point_list)
    {
        make_polygon_corner(polygon_id, point_id);
    }
    return polygon_id;
}

void Geometry::transform(const mat4& m)
{
    ZoneScoped;

    mat4 it = glm::transpose(glm::inverse(m));

    // Check.. Did I forget something?
    auto* polygon_centroids = polygon_attributes().find<vec3>(c_polygon_centroids);
    auto* polygon_normals   = polygon_attributes().find<vec3>(c_polygon_normals  );
    auto* point_locations   = point_attributes()  .find<vec3>(c_point_locations  );
    auto* point_normals     = point_attributes()  .find<vec3>(c_point_normals    );
    auto* corner_normals    = corner_attributes() .find<vec3>(c_corner_normals   );
    auto* corner_tangents   = corner_attributes() .find<vec4>(c_corner_tangents  );

    for (Point_id point_id = 0; point_id < m_next_point_id; ++point_id)
    {
        if ((point_locations != nullptr) && point_locations->has(point_id))
        {
            point_locations->put(point_id, vec3(m * vec4(point_locations->get(point_id), 1.0f)));
        }

        if ((point_normals != nullptr) && point_normals->has(point_id))
        {
            point_normals->put(point_id, vec3(it * vec4(point_normals->get(point_id), 0.0f)));
        }
    }

    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        if ((polygon_centroids != nullptr) && polygon_centroids->has(polygon_id))
        {
            polygon_centroids->put(polygon_id, vec3(m * vec4(polygon_centroids->get(polygon_id), 1.0f)));
        }

        if ((polygon_normals != nullptr) && polygon_normals->has(polygon_id))
        {
            polygon_normals->put(polygon_id, vec3(it * vec4(polygon_normals->get(polygon_id), 0.0f)));
        }
    }

    for (Corner_id corner_id = 0, end = m_next_corner_id; corner_id < end; ++corner_id)
    {
        if ((corner_normals != nullptr) && corner_normals->has(corner_id))
        {
            corner_normals->put(corner_id, vec3(it * vec4(corner_normals->get(corner_id), 0.0f)));
        }

        if ((corner_tangents != nullptr) && corner_tangents->has(corner_id))
        {
            vec4  t0_sign = corner_tangents->get(corner_id);
            vec3  t0      = vec3(t0_sign);
            float sign    = t0_sign.w;
            corner_tangents->put(corner_id, vec4(vec3(it * vec4(t0, 0.0f)), sign));
        }
    }
}

void Geometry::reverse_polygons()
{
    ZoneScoped;

    ++m_serial;

    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        polygons[polygon_id].reverse(*this);
    }
}

void Geometry::Mesh_info::trace(erhe::log::Log::Category& log) const
{
    log.trace("{} vertex corners vertices\n", vertex_count_corners);
    log.trace("{} centroid vertices\n",       vertex_count_centroids);
    log.trace("{} fill triangles indices\n",  index_count_fill_triangles);
    log.trace("{} edge lines indices\n",      index_count_edge_lines);
    log.trace("{} corner points indices\n",   index_count_corner_points);
    log.trace("{} centroid points indices\n", index_count_centroid_points);
}

void Geometry::generate_texture_coordinates_spherical()
{
    ZoneScoped;

    log.info("{} for {}\n", __func__, m_name);

    compute_polygon_normals();
    compute_point_normals(c_point_normals);
    auto* polygon_normals      = polygon_attributes().find          <vec3>(c_polygon_normals     );
    auto* corner_normals       = corner_attributes ().find          <vec3>(c_corner_normals      );
    auto* point_normals        = point_attributes  ().find          <vec3>(c_point_normals       );
    auto* point_normals_smooth = point_attributes  ().find          <vec3>(c_point_normals_smooth);
    auto* corner_texcoords     = corner_attributes ().find_or_create<vec2>(c_corner_texcoords    );
    auto* point_locations      = point_attributes  ().find          <vec3>(c_point_locations     );

    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        Polygon& polygon = polygons[polygon_id];
        for (Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
             end = polygon.first_polygon_corner_id + polygon.corner_count;
             polygon_corner_id < end;
             ++polygon_corner_id)
        {
            Corner_id corner_id = polygon_corners[polygon_corner_id];
            Corner&   corner    = corners[corner_id];
            Point_id  point_id  = corner.point_id;
            glm::vec3 normal{0.0f, 1.0f, 0.0f};
            if ((point_locations != nullptr) && point_locations->has(point_id))
            {
                normal = glm::normalize(point_locations->get(point_id));
            }
            else if ((corner_normals != nullptr) && corner_normals->has(corner_id))
            {
                normal = corner_normals->get(corner_id);
            }
            else if ((point_normals != nullptr) && point_normals->has(point_id))
            {
                normal = point_normals->get(point_id);
            }
            else if ((point_normals_smooth != nullptr) && point_normals_smooth->has(point_id))
            {
                normal = point_normals_smooth->get(point_id);
            }
            else if ((polygon_normals != nullptr) && polygon_normals->has(polygon_id))
            {
                normal = polygon_normals->get(polygon_id);
            }
            else
            {
                FATAL("No normal sources\n");
            }
            float u;
            float v;
            //if (std::abs(normal.x) >= std::abs(normal.y) && std::abs(normal.x) >= normal.z)
            {
                u = (-normal.z / std::abs(normal.x) + 1.0f) / 2.0f;
                v = (-normal.y / std::abs(normal.x) + 1.0f) / 2.0f;
            }
            //else if (std::abs(normal.y) >= std::abs(normal.x) && std::abs(normal.y) >= normal.z)
            //{
            //    u = (-normal.z / std::abs(normal.y) + 1.0f) / 2.0f;
            //    v = (-normal.x / std::abs(normal.y) + 1.0f) / 2.0f;
            //}
            //else
            //{
            //    u = (-normal.x / std::abs(normal.z) + 1.0f) / 2.0f;
            //    v = (-normal.y / std::abs(normal.z) + 1.0f) / 2.0f;
            //}
            corner_texcoords->put(corner_id, glm::vec2(u, v));
        }
    }
}

auto Geometry::generate_polygon_texture_coordinates(bool overwrite_existing_texture_coordinates) -> bool
{
    ZoneScoped;

    if (m_serial_polygon_texture_coordinates == m_serial)
    {
        return true;
    }

    log.info("{} for {}\n", __func__, m_name);

    compute_polygon_normals();
    compute_polygon_centroids();
    auto* polygon_normals   = polygon_attributes().find          <vec3>(c_polygon_normals  );
    auto* polygon_centroids = polygon_attributes().find          <vec3>(c_polygon_centroids);
    auto* point_locations   = point_attributes  ().find          <vec3>(c_point_locations  );
    auto* corner_texcoords  = corner_attributes ().find_or_create<vec2>(c_corner_texcoords );

    if (point_locations == nullptr)
    {
        log_polygon_texcoords.warn("{} geometry = {} - No point locations found. Skipping tangent generation.\n", __func__, m_name);
        return false;
    }

    if (polygon_centroids == nullptr)
    {
        log_polygon_texcoords.warn("{} geometry = {} - No polygon centroids found. Skipping tangent generation.\n", __func__, m_name);
        return false;
    }

    if (polygon_normals == nullptr)
    {
        log_polygon_texcoords.warn("{} geometry = {} - No polygon normals found. Skipping tangent generation.\n", __func__, m_name);
        return false;
    }

    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        polygons[polygon_id].compute_planar_texture_coordinates(polygon_id,
                                                                *this,
                                                                *corner_texcoords,
                                                                *polygon_centroids,
                                                                *polygon_normals,
                                                                *point_locations,
                                                                overwrite_existing_texture_coordinates);
    }

    m_serial_polygon_texture_coordinates = m_serial;
    return true;
}

auto Geometry::compute_tangents(bool corner_tangents,
                                bool corner_bitangents,
                                bool polygon_tangents,
                                bool polygon_bitangents,
                                bool make_polygons_flat,
                                bool override_existing) -> bool
{
    ZoneScoped;

    if ((!polygon_tangents   || (m_serial_polygon_tangents   == m_serial)) &&
        (!polygon_bitangents || (m_serial_polygon_bitangents == m_serial)) &&
        (!corner_tangents    || (m_serial_corner_tangents    == m_serial)) &&
        (!corner_bitangents  || (m_serial_corner_bitangents  == m_serial)))
    {
        return false;
    }

    log.info("{} for {}\n", __func__, m_name);

    if (!compute_polygon_normals())
    {
        return false;
    }
    if (!compute_polygon_centroids())
    {
        return false;
    }
    if (!compute_point_normals(c_point_normals_smooth))
    {
        return false;
    }
    if (!generate_polygon_texture_coordinates(false))
    {
        return false;
    }

    struct Geometry_context
    {
        Geometry* geometry{nullptr};
        int       triangle_count{0};
        bool      override_existing{false};

        Property_map<Polygon_id, glm::vec3>* polygon_normals     {nullptr};
        Property_map<Polygon_id, glm::vec3>* polygon_centroids   {nullptr};
        Property_map<Polygon_id, glm::vec4>* polygon_tangents    {nullptr};
        Property_map<Polygon_id, glm::vec4>* polygon_bitangents  {nullptr};
        Property_map<Corner_id, glm::vec3>*  corner_normals      {nullptr};
        Property_map<Corner_id, glm::vec4>*  corner_tangents     {nullptr};
        Property_map<Corner_id, glm::vec4>*  corner_bitangents   {nullptr};
        Property_map<Corner_id, glm::vec2>*  corner_texcoords    {nullptr};
        Property_map<Point_id, glm::vec3>*   point_locations     {nullptr};
        Property_map<Point_id, glm::vec3>*   point_normals       {nullptr};
        Property_map<Point_id, glm::vec3>*   point_normals_smooth{nullptr};
        Property_map<Point_id, glm::vec2>*   point_texcoords     {nullptr};

        // Polygons are triangulated.
        struct Triangle
        {
            Polygon_id polygon_id;
            uint32_t   triangle_index;
        };

        Triangle& get_triangle(int iFace)
        {
            return triangles[iFace];
        }

        Polygon_id get_polygon_id(int iFace)
        {
            return get_triangle(iFace).polygon_id;
        }

        Polygon& get_polygon(int iFace)
        {
            return geometry->polygons[get_polygon_id(iFace)];
        }

        Point_id get_corner_id_triangulated(int iFace, int iVert)
        {
            VERIFY(iVert > 0); // This only works for triangulated polygons, N > 3
            auto& triangle = get_triangle(iFace);
            auto& polygon  = get_polygon(iFace);
            uint32_t          corner_offset     = (iVert - 1 + triangle.triangle_index) % polygon.corner_count;
            Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + corner_offset;
            Corner_id         corner_id         = geometry->polygon_corners[polygon_corner_id];
            return corner_id;
        }

        Point_id get_corner_id_direct(int iFace, int iVert)
        {
            VERIFY(iVert < 3); // This only works for triangles
            auto&             polygon           = get_polygon(iFace);
            Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + iVert;
            Corner_id         corner_id         = geometry->polygon_corners[polygon_corner_id];
            return corner_id;
        }

        Corner_id get_corner_id(int iFace, int iVert)
        {
            auto& polygon = get_polygon(iFace);
            return (polygon.corner_count == 3) ? get_corner_id_direct(iFace, iVert)
                                               : get_corner_id_triangulated(iFace, iVert);
        }

        Point_id get_point_id(int iFace, int iVert)
        {
            Corner_id corner_id = get_corner_id(iFace, iVert);
            Corner&   corner    = geometry->corners[corner_id];
            return corner.point_id;
        }

        glm::vec3 get_position(int iFace, int iVert)
        {
            Polygon_id polygon_id = get_polygon_id(iFace);
            if (iVert == 0)
            {
                glm::vec3 position = polygon_centroids->get(polygon_id);
                return position;
            }
            Point_id point_id = get_point_id(iFace, iVert);
            glm::vec3 position = point_locations->get(point_id);
            return position;
        }

        glm::vec3 get_normal(int iFace, int iVert)
        {
            Polygon_id polygon_id = get_polygon_id(iFace);
            if (iVert == 0)
            {
                glm::vec3 normal = polygon_normals->get(polygon_id);
                return normal;
            }
            Corner_id corner_id = get_corner_id(iFace, iVert);
            Point_id  point_id  = get_point_id (iFace, iVert);
            if ((corner_normals != nullptr) && corner_normals->has(corner_id))
            {
                glm::vec3 normal = corner_normals->get(corner_id);
                return normal;
            }
            else if ((point_normals != nullptr) && point_normals->has(point_id))
            {
                glm::vec3 normal = point_normals->get(point_id);
                return normal;
            }
            else if ((point_normals_smooth != nullptr) && point_normals_smooth->has(point_id))
            {
                glm::vec3 normal = point_normals_smooth->get(point_id);
                return normal;
            }
            else if ((polygon_normals != nullptr) && polygon_normals->has(polygon_id))
            {
                glm::vec3 normal = polygon_normals->get(polygon_id);
                return normal;
            }
            else
            {
                FATAL("No normal source\n");
            }
            return glm::vec3(0.0f, 1.0f, 0.0f);
        }

        glm::vec2 get_texcoord(int iFace, int iVert)
        {
            if (iVert == 0)
            {
                // Calculate and return average texture coordinate from all polygon vertices
                Polygon&   polygon    = get_polygon(iFace);
                glm::vec2  texcoord{0.0f, 0.0f};
                for (Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id;
                     polygon_corner_id < polygon.first_polygon_corner_id + polygon.corner_count;
                     ++polygon_corner_id)
                {
                    Corner_id corner_id = geometry->polygon_corners[polygon_corner_id];
                    Corner&   corner    = geometry->corners[corner_id];
                    Point_id  point_id  = corner.point_id;
                    if ((corner_texcoords != nullptr) && corner_texcoords->has(corner_id))
                    {
                        texcoord += corner_texcoords->get(corner_id);
                    }
                    else if ((point_texcoords != nullptr) && point_texcoords->has(point_id))
                    {
                        texcoord += point_texcoords->get(point_id);
                    }
                    else
                    {
                        FATAL("No texcoord\n");
                    }
                }
                glm::vec2 average_texcoord = texcoord / static_cast<float>(polygon.corner_count);
                return average_texcoord;
            }
            Corner_id corner_id = get_corner_id(iFace, iVert);
            Point_id  point_id  = get_point_id (iFace, iVert);
            if ((corner_texcoords != nullptr) && corner_texcoords->has(corner_id))
            {
                glm::vec2 texcoord = corner_texcoords->get(corner_id);
                return texcoord;
            }
            else if ((point_texcoords != nullptr) && point_texcoords->has(point_id))
            {
                glm::vec2 texcoord = point_texcoords->get(point_id);
                return texcoord;
            }
            else
            {
                FATAL("No texture coordinate\n");
            }
            return glm::vec2(0.0f, 0.0f);
        }

        void set_tangent(int iFace, int iVert, glm::vec3 tangent, float sign)
        {
            Polygon&   polygon = get_polygon(iFace);
            if ((polygon.corner_count > 3) && (iVert == 0))
            {
                return;
            }
            Polygon_id polygon_id = get_polygon_id(iFace);
            Corner_id  corner_id  = get_corner_id(iFace, iVert);

            if (polygon_tangents && (override_existing || !polygon_tangents->has(polygon_id)))
            {
                polygon_tangents->put(polygon_id, glm::vec4(tangent, sign));
            }
            if (corner_tangents && (override_existing || !corner_tangents->has(corner_id)))
            {
                corner_tangents->put(corner_id, glm::vec4(tangent, sign));
            }
        }

        void set_bitangent(int iFace, int iVert, glm::vec3 bitangent, float sign)
        {
            Polygon&   polygon = get_polygon(iFace);
            if ((polygon.corner_count > 3) && (iVert == 0))
            {
                return;
            }
            Polygon_id polygon_id = get_polygon_id(iFace);
            Corner_id  corner_id  = get_corner_id(iFace, iVert);

            if (polygon_bitangents && (override_existing || !polygon_bitangents->has(polygon_id)))
            {
                polygon_bitangents->put(polygon_id, glm::vec4(bitangent, sign));
            }
            if (corner_bitangents && (override_existing || !corner_bitangents->has(corner_id)))
            {
                corner_bitangents->put(corner_id, glm::vec4(bitangent, sign));
            }
        }

        std::vector<Triangle> triangles;
    };

    Geometry_context g;
    g.geometry             = this;
    g.override_existing    = override_existing;
    g.polygon_normals      = polygon_attributes().find<vec3>(c_polygon_normals     );
    g.polygon_centroids    = polygon_attributes().find<vec3>(c_polygon_centroids   );
    g.corner_normals       = corner_attributes ().find<vec3>(c_corner_normals      );
    g.corner_texcoords     = corner_attributes ().find<vec2>(c_corner_texcoords    );
    g.point_locations      = point_attributes  ().find<vec3>(c_point_locations     );
    g.point_normals        = point_attributes  ().find<vec3>(c_point_normals       );
    g.point_normals_smooth = point_attributes  ().find<vec3>(c_point_normals_smooth);
    g.point_texcoords      = point_attributes  ().find<vec2>(c_point_texcoords     );
    g.polygon_tangents     = polygon_tangents   ? polygon_attributes().find_or_create<vec4>(c_polygon_tangents  ) : nullptr;
    g.polygon_bitangents   = polygon_bitangents ? polygon_attributes().find_or_create<vec4>(c_polygon_bitangents) : nullptr;
    g.corner_tangents      = corner_tangents    ? corner_attributes ().find_or_create<vec4>(c_corner_tangents   ) : nullptr;
    g.corner_bitangents    = corner_bitangents  ? corner_attributes ().find_or_create<vec4>(c_corner_bitangents ) : nullptr;

    if (g.point_locations == nullptr)
    {
        log_tangent_gen.warn("{} geometry = {} - No point locations found. Skipping tangent generation.\n", __func__, m_name);
        return false;
    }

    // MikkTSpace can only handle triangles or quads.
    // We triangulate all non-triangles by adding a virtual polygon centroid
    // and presenting N virtual triangles to MikkTSpace.
    int    face_index     = 0;
    size_t triangle_count = count_polygon_triangles();
    g.triangle_count = static_cast<int>(triangle_count);
    g.triangles.resize(triangle_count);
    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        Polygon& polygon = polygons[polygon_id];
        if (polygon.corner_count < 3)
        {
            continue;
        }
        for (uint32_t i = 0; i < polygon.corner_count - 2; ++i)
        {
            g.triangles[face_index++] = {polygon_id, i};
        }
    }

    SMikkTSpaceInterface mikktspace = {};
    mikktspace.m_getNumFaces = [](const SMikkTSpaceContext* pContext)
    {
        auto* context   = reinterpret_cast<Geometry_context*>(pContext->m_pUserData);
        int   num_faces = context->triangle_count;
        return num_faces;
    };

    mikktspace.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int32_t)
    {
        int num_vertices_of_face = 3;
        return num_vertices_of_face;
    };

    mikktspace.m_getPosition = [](const SMikkTSpaceContext* pContext,
                                  float                     fvPosOut[],
                                  int32_t                   iFace,
                                  int32_t                   iVert)
    {
        auto*     context = reinterpret_cast<Geometry_context*>(pContext->m_pUserData);
        glm::vec3 g_location = context->get_position(iFace, iVert);
        fvPosOut[0] = g_location[0];
        fvPosOut[1] = g_location[1];
        fvPosOut[2] = g_location[2];
    };

    mikktspace.m_getNormal = [](const SMikkTSpaceContext* pContext,
                                float                     fvNormOut[],
                                int32_t                   iFace,
                                int32_t                   iVert)
    {
        auto*     context  = reinterpret_cast<Geometry_context*>(pContext->m_pUserData);
        glm::vec3 g_normal = context->get_normal(iFace, iVert);
        fvNormOut[0] = g_normal[0];
        fvNormOut[1] = g_normal[1];
        fvNormOut[2] = g_normal[2];
    };

    mikktspace.m_getTexCoord = [](const SMikkTSpaceContext* pContext,
                                  float                     fvTexcOut[],
                                  int32_t                   iFace,
                                  int32_t                   iVert)
    {
        auto*     context    = reinterpret_cast<Geometry_context*>(pContext->m_pUserData);
        glm::vec2 g_texcoord = context->get_texcoord(iFace, iVert);
        fvTexcOut[0] = g_texcoord[0];
        fvTexcOut[1] = g_texcoord[1];
    };

	mikktspace.m_setTSpace = [](const SMikkTSpaceContext* pContext,
                                const float               fvTangent[],
                                const float               fvBiTangent[],
                                const float               fMagS,
                                const float               fMagT,
                                const tbool               bIsOrientationPreserving,
                                const int                 iFace,
                                const int                 iVert)
    {
        static_cast<void>(fMagS);
        static_cast<void>(fMagT);
        static_cast<void>(bIsOrientationPreserving);
        auto*     context = reinterpret_cast<Geometry_context*>(pContext->m_pUserData);
        auto      N     = context->get_normal(iFace, iVert);
        glm::vec3 T     = glm::vec3(fvTangent  [0], fvTangent  [1], fvTangent  [2]);
        glm::vec3 B     = glm::vec3(fvBiTangent[0], fvBiTangent[1], fvBiTangent[2]);
        vec3      t_xyz = glm::normalize(T - N * glm::dot(N, T));
        float     t_w   = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
        vec3      b_xyz = glm::normalize(B - N * glm::dot(N, B));
        float     b_w   = (glm::dot(glm::cross(B, N), T) < 0.0f) ? -1.0f : 1.0f;
        context->set_tangent  (iFace, iVert, t_xyz, t_w);
        context->set_bitangent(iFace, iVert, b_xyz, b_w);
    };

    SMikkTSpaceContext context = {};
    context.m_pInterface = &mikktspace;
    context.m_pUserData  = &g;

    {
        ZoneScopedN("genTangSpaceDefault")
        auto res = genTangSpaceDefault(&context);
        if (res == 0)
        {
            log_tangent_gen.trace("genTangSpaceDefault() returned 0\n");
            return false;
        }
    }

    // Post processing: Pick one tangent for polygon
    if (make_polygons_flat)
    {
        ZoneScopedN("make polygons flat")

        for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
        {
            Polygon& polygon = polygons[polygon_id];
            if (polygon.corner_count < 3)
            {
                continue;
            }
        
            std::optional<glm::vec4> T;
            std::optional<glm::vec4> B;
            std::vector<glm::vec4> tangents;
            std::vector<glm::vec4> bitangents;
            glm::vec4 tangent{1.0, 0.0, 0.0, 1.0};
            glm::vec4 bitangent{0.0, 0.0, 1.0, 1.0};
            for (uint32_t i = 0; i < polygon.corner_count; ++i)
            {
                Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                Corner_id         corner_id         = polygon_corners[polygon_corner_id];
                if ((override_existing || !T.has_value()) && corner_tangents && g.corner_tangents->has(corner_id))
                {
                    tangent = g.corner_tangents->get(corner_id);
                    for (auto other : tangents)
                    {
                        float dot = glm::dot(glm::vec3(tangent), glm::vec3(other));
                        if (dot > 0.99f)
                        {
                            T = tangent;
                        }
                    }
                    tangents.emplace_back(tangent);
                }
                if ((override_existing || !B.has_value()) && corner_bitangents && g.corner_bitangents->has(corner_id))
                {
                    bitangent = g.corner_bitangents->get(corner_id);
                    for (auto other : bitangents)
                    {
                        float dot = glm::dot(glm::vec3(bitangent), glm::vec3(other));
                        if (dot > 0.99f)
                        {
                            B = bitangent;
                        }
                    }
                    bitangents.emplace_back(bitangent);
                }
            }
            if (T.has_value())
            {
                tangent = T.value();
            }
            if (B.has_value())
            {
                bitangent = B.value();
            }

            // Second pass - put tangent to all corners
            if (polygon_tangents)
            {
                g.polygon_tangents->put(polygon_id, tangent);
            }
            if (polygon_bitangents)
            {
                g.polygon_tangents->put(polygon_id, bitangent);
            }
            if (corner_tangents)
            {
                for (uint32_t i = 0; i < polygon.corner_count; ++i)
                {
                    Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                    Corner_id         corner_id         = polygon_corners[polygon_corner_id];
                    g.corner_tangents->put(corner_id, tangent);
                }
            }
            if (corner_bitangents)
            {
                for (uint32_t i = 0; i < polygon.corner_count; ++i)
                {
                    Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                    Corner_id         corner_id         = polygon_corners[polygon_corner_id];
                    g.corner_bitangents->put(corner_id, bitangent);
                }
            }
        }
    }
    if (polygon_tangents)
    {
        m_serial_polygon_tangents = m_serial;
    }
    if (polygon_bitangents)
    {
        m_serial_polygon_bitangents = m_serial;
    }
    if (corner_tangents)
    {
        m_serial_corner_tangents = m_serial;
    }
    if (corner_bitangents)
    {
        m_serial_corner_bitangents = m_serial;
    }
    return true;
}

} // namespace erhe::geometry
