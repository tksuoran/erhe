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

    Point_corner_id next_point_corner = 0;
    for (Point_id point_id = 0; point_id < m_next_point_id; ++point_id)
    {
        Point& point = points[point_id];
        point.first_point_corner_id = next_point_corner;
        point.corner_count = 0; // reset in case this has been done before
        next_point_corner += point.reserved_corner_count;
    }
    m_next_point_corner_reserve = next_point_corner;
    point_corners.resize(static_cast<size_t>(m_next_point_corner_reserve));
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

    log.info("{} for {}\n", __func__, name);

    auto* polygon_normals = polygon_attributes().find_or_create<vec3>(c_polygon_normals);
    auto* point_locations = point_attributes()  .find          <vec3>(c_point_locations);

    if (point_locations == nullptr)
    {
        log.warn("{} {}: Point locations are required, but not found.\n", __func__, name);
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

    log.info("{} for {}\n", __func__, name);

    auto* polygon_centroids = polygon_attributes().find_or_create<vec3>(c_polygon_centroids);
    auto* point_locations   = point_attributes()  .find          <vec3>(c_point_locations);

    if (point_locations == nullptr)
    {
        log.warn("{} {}: Point locations are required, but not found.\n", __func__, name);
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

    log.info("{} for {}\n", __func__, name);

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

    log.info("{} for {}\n", __func__, name);

    compute_polygon_normals();
    compute_polygon_centroids();
    auto* polygon_normals   = polygon_attributes().find          <vec3>(c_polygon_normals  );
    auto* polygon_centroids = polygon_attributes().find          <vec3>(c_polygon_centroids);
    auto* point_locations   = point_attributes  ().find          <vec3>(c_point_locations  );
    auto* corner_texcoords  = corner_attributes ().find_or_create<vec2>(c_corner_texcoords );

    if (point_locations == nullptr)
    {
        log_polygon_texcoords.warn("{} geometry = {} - No point locations found. Skipping tangent generation.\n", __func__, name);
        return false;
    }

    if (polygon_centroids == nullptr)
    {
        log_polygon_texcoords.warn("{} geometry = {} - No polygon centroids found. Skipping tangent generation.\n", __func__, name);
        return false;
    }

    if (polygon_normals == nullptr)
    {
        log_polygon_texcoords.warn("{} geometry = {} - No polygon normals found. Skipping tangent generation.\n", __func__, name);
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

    log.info("{} for {}\n", __func__, name);

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
        log_tangent_gen.warn("{} geometry = {} - No point locations found. Skipping tangent generation.\n", __func__, name);
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

void Geometry::merge(Geometry& other, glm::mat4 transform)
{
    // Append corners
    Corner_id combined_corner_count = corner_count() + other.corner_count();
    if (corners.size() < combined_corner_count)
    {
        corners.resize(combined_corner_count);
    }
    for (Corner_id corner_id = 0, end = other.corner_count(); corner_id < end; ++corner_id)
    {
        const Corner& src_corner = other.corners[corner_id];
        Corner&       dst_corner = corners[corner_id + m_next_corner_id];
        dst_corner.point_id   = src_corner.point_id   + m_next_point_id;
        dst_corner.polygon_id = src_corner.polygon_id + m_next_polygon_id;
    }

    // Append points
    Point_id combined_point_count = point_count() + other.point_count();
    if (points.size() < combined_point_count)
    {
        points.resize(combined_point_count);
    }
    for (Point_id point_id = 0, end = other.point_count(); point_id < end; ++point_id)
    {
        Point_id     dst_point_id = point_id + m_next_point_id;
        const Point& src_point    = other.points[point_id];;
        Point&       dst_point    = points[dst_point_id];
        dst_point.first_point_corner_id = src_point.first_point_corner_id + m_next_point_corner_reserve; 
        dst_point.corner_count          = src_point.corner_count;
        dst_point.reserved_corner_count = src_point.reserved_corner_count;
    }

    // Append point corners
    Point_corner_id combined_point_corner_count = point_corner_count() + other.point_corner_count();
    if (point_corners.size() < combined_point_corner_count)
    {
        point_corners.resize(combined_point_corner_count);
    }
    for (Point_corner_id point_corner_id = 0, end = other.point_corner_count(); point_corner_id < end; ++ point_corner_id)
    {
        Point_corner_id dst_point_corner_id = point_corner_id + m_next_point_corner_reserve;
        point_corners[dst_point_corner_id] = other.point_corners[point_corner_id] + m_next_corner_id;
    }

    // Append polygons
    Polygon_id combined_polygon_count = m_next_polygon_id + other.polygon_count();
    if (polygons.size() < combined_polygon_count)
    {
        polygons.resize(combined_polygon_count);
    }
    for (Polygon_id polygon_id = 0, end = other.polygon_count(); polygon_id < end; ++polygon_id)
    {
        Polygon_id     dst_polygon_id = polygon_id + m_next_polygon_id;
        const Polygon& src_polygon    = other.polygons[polygon_id];
        Polygon&       dst_polygon    = polygons[dst_polygon_id];
        dst_polygon.first_polygon_corner_id = src_polygon.first_polygon_corner_id + m_next_polygon_corner_id;
        dst_polygon.corner_count            = src_polygon.corner_count;
    }

    // Append polygon corners
    Polygon_corner_id combined_polygon_corner_count = polygon_corner_count() + other.polygon_corner_count();
    if (polygon_corners.size() < combined_polygon_corner_count)
    {
        polygon_corners.resize(combined_polygon_corner_count);
    }
    for (Polygon_corner_id polygon_corner_id = 0, end = other.polygon_corner_count(); polygon_corner_id < end; ++polygon_corner_id)
    {
        Polygon_corner_id dst_polygon_corner_id = polygon_corner_id + m_next_polygon_corner_id;
        polygon_corners[dst_polygon_corner_id]  = other.polygon_corners[polygon_corner_id] + m_next_corner_id;
    }

    // Merging only works with trimmed attribute maps
    other.corner_attributes ().trim(other.corner_count ());
    other.point_attributes  ().trim(other.point_count  ());
    other.polygon_attributes().trim(other.polygon_count());
    other.edge_attributes   ().trim(other.edge_count   ());
    m_corner_property_map_collection .trim(corner_count ());
    m_point_property_map_collection  .trim(point_count  ());
    m_polygon_property_map_collection.trim(polygon_count());
    m_edge_property_map_collection   .trim(edge_count   ());

    other.corner_attributes ().merge_to(m_corner_property_map_collection , transform);
    other.point_attributes  ().merge_to(m_point_property_map_collection  , transform);
    other.polygon_attributes().merge_to(m_polygon_property_map_collection, transform);
    other.edge_attributes   ().merge_to(m_edge_property_map_collection   , transform);

    m_next_corner_id            += other.corner_count();
    m_next_point_id             += other.point_count();
    m_next_point_corner_reserve += other.point_corner_count();
    m_next_polygon_id           += other.polygon_count();
    m_next_polygon_corner_id    += other.polygon_corner_count();
}

const char* c_str(glm::vec3::length_type axis)
{
    switch (axis)
    {
        case glm::vec3::length_type{0}: return "X";
        case glm::vec3::length_type{1}: return "Y";
        case glm::vec3::length_type{2}: return "Z";
        default: return "?";
    }
}

void Geometry::weld(Weld_settings weld_settings)
{
    static_cast<void>(weld_settings);

    struct Point_attribute_maps
    {
        Property_map<Point_id, glm::vec3>* locations {nullptr};
        Property_map<Point_id, glm::vec3>* normals   {nullptr};
        Property_map<Point_id, glm::vec3>* tangents  {nullptr};
        Property_map<Point_id, glm::vec3>* bitangents{nullptr};
        Property_map<Point_id, glm::vec2>* texcoords {nullptr};
    };
    Point_attribute_maps point_attribute_maps;
    point_attribute_maps.locations  = point_attributes().find<vec3>(c_point_locations);
    point_attribute_maps.normals    = point_attributes().find<vec3>(c_point_normals);
    point_attribute_maps.tangents   = point_attributes().find<vec3>(c_point_tangents);
    point_attribute_maps.bitangents = point_attributes().find<vec3>(c_point_bitangents);
    point_attribute_maps.texcoords  = point_attributes().find<vec2>(c_point_texcoords);

    struct Polygon_attribute_maps
    {
        Property_map<Polygon_id, glm::vec3>* centroids{nullptr};
        Property_map<Polygon_id, glm::vec3>* normals  {nullptr};
    };
    Polygon_attribute_maps polygon_attribute_maps;
    polygon_attribute_maps.centroids  = polygon_attributes().find<vec3>(c_polygon_centroids);
    polygon_attribute_maps.normals    = polygon_attributes().find<vec3>(c_polygon_normals);

    // Sort points
    std::vector<Point_id> new_to_old_point_id;
    new_to_old_point_id.resize(m_next_point_id);
    glm::vec3 min_corner(std::numeric_limits<float>::max(),    std::numeric_limits<float>::max(),    std::numeric_limits<float>::max());
    glm::vec3 max_corner(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    for (Point_id point_id = 0; point_id < m_next_point_id; ++point_id)
    {
        new_to_old_point_id[point_id] = point_id;
        if (!point_attribute_maps.locations->has(point_id))
        {
            continue;
        }
        vec3 position = point_attribute_maps.locations->get(point_id);
        min_corner = glm::min(min_corner, position);
        max_corner = glm::max(max_corner, position);
    }

    glm::vec3 bounding_box_size0 = max_corner - min_corner;
    auto axis0 = erhe::toolkit::max_axis_index(bounding_box_size0);
    glm::vec3 bounding_box_size1 = bounding_box_size0;
    bounding_box_size1[axis0] = 0.0f;
    auto axis1 = erhe::toolkit::max_axis_index(bounding_box_size1);
    glm::vec3 bounding_box_size2 = bounding_box_size1;
    bounding_box_size2[axis1] = 0.0f;
    auto axis2 = erhe::toolkit::max_axis_index(bounding_box_size2);

    log_weld.trace("Primary   axis = {} {}\n", axis0, c_str(axis0));
    log_weld.trace("Secondary axis = {} {}\n", axis1, c_str(axis1));
    log_weld.trace("Tertiary  axis = {} {}\n", axis2, c_str(axis2));
    std::sort(new_to_old_point_id.begin(),
              new_to_old_point_id.end(),
              [axis0, axis1, axis2, point_attribute_maps](const Point_id& lhs, const Point_id& rhs)
              {
                  if (!point_attribute_maps.locations->has(lhs) || !point_attribute_maps.locations->has(rhs))
                  {
                      return false;
                  }
                  glm::vec3 position_lhs = point_attribute_maps.locations->get(lhs);
                  glm::vec3 position_rhs = point_attribute_maps.locations->get(rhs);
                  if (position_lhs[axis0] != position_rhs[axis0])
                  {
                      bool is_less = position_lhs[axis0] < position_rhs[axis0];
                      log_weld.trace("{:2} vs {:2} {} [ {} ] {} {} [ {} ]\n",
                                     lhs, rhs, position_lhs, axis0, is_less ? "< " : ">=", position_rhs, axis0);
                      return is_less;
                  }
                  bool is_not_same = position_lhs[axis1] != position_rhs[axis1];
                  log_weld.trace("position_lhs[axis1] = {}, position_rhs[axis1] = {}, is not same = {}\n",
                                 position_lhs[axis1], position_rhs[axis1], is_not_same);
                  if (is_not_same)
                  {
                      bool is_less = position_lhs[axis1] < position_rhs[axis1];
                      log_weld.trace("{:2} vs {:2} {} [ {} ] {} {} [ {} ]\n",
                                     lhs, rhs, position_lhs, axis1, is_less ? "< " : ">=", position_rhs, axis1);
                      return is_less;
                  }
                  bool is_less = position_lhs[axis2] < position_rhs[axis2];
                  log_weld.trace("{:2} vs {:2} {} [ {} ] {} {} [ {} ]\n",
                                 lhs, rhs, position_lhs, axis2, is_less ? "< " : ">=", position_rhs, axis2);
                  return is_less;
              }
    );

    // Sort polygons
    std::vector<Polygon_id> new_to_old_polygon_id;
    new_to_old_polygon_id.resize(m_next_polygon_id);
    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        new_to_old_polygon_id[polygon_id] = polygon_id;
    }

    std::sort(new_to_old_polygon_id.begin(),
              new_to_old_polygon_id.end(),
              [axis0, axis1, axis2, polygon_attribute_maps](const Polygon_id& lhs, const Polygon_id& rhs)
              {
                  if (!polygon_attribute_maps.centroids->has(lhs) || !polygon_attribute_maps.centroids->has(rhs))
                  {
                      return false;
                  }
                  glm::vec3 position_lhs = polygon_attribute_maps.centroids->get(lhs);
                  glm::vec3 position_rhs = polygon_attribute_maps.centroids->get(rhs);
                  if (position_lhs[axis0] != position_rhs[axis0])
                  {
                      bool is_less = position_lhs[axis0] < position_rhs[axis0];
                      //log_weld.trace("{} vs {} {}[{}] {} {}[{}]\n",
                      //               lhs, rhs, position_lhs, axis0, is_less ? "<" : ">=", position_rhs, axis0);
                      return is_less;
                  }
                  if (position_lhs[axis1] != position_rhs[axis1])
                  {
                      bool is_less = position_lhs[axis1] < position_rhs[axis1];
                      //log_weld.trace("{} vs {} {}[{}] {} {}[{}]\n",
                      //               lhs, rhs, position_lhs, axis1, is_less ? "<" : ">=", position_rhs, axis1);
                      return is_less;
                  }
                  bool is_less = position_lhs[axis2] < position_rhs[axis2];
                  //log_weld.trace("{} vs {} {}[{}] {} {}[{}]\n",
                  //               lhs, rhs, position_lhs, axis2, is_less ? "<" : ">=", position_rhs, axis2);
                  return is_less;
              }
    );

    std::vector<Point_id> old_to_new_point_id;
    old_to_new_point_id.resize(m_next_point_id);
    {
        //log_weld.trace("Point remapping before merging:\n");
        log::Log::Indenter scoped_indent;
        for (Point_id i = 0; i < m_next_point_id; ++i)
        {
            Point_id old_point_id = new_to_old_point_id[i];
            Point_id new_point_id = i;
            old_to_new_point_id[old_point_id] = new_point_id;
            //log_weld.trace("{}: old {} -> new {}\n", i, old_point_id, old_to_new_point_id[old_point_id]);
        }
        for (Point_id i = 0; i < m_next_point_id; ++i)
        {
            //log_weld.trace("old_to_new[{}] = {}, new_to_old[{}] = {}", i, old_to_new_point_id[i], i, new_to_old_point_id[i]);
        }
    }

    std::vector<Polygon_id> old_to_new_polygon_id;
    old_to_new_polygon_id.resize(m_next_polygon_id);
    {
        log_weld.trace("Polygon remapping before merging / eliminating:\n");
        log::Log::Indenter scoped_indent;
        for (Polygon_id i = 0; i < m_next_polygon_id; ++i)
        {
            Polygon_id old_polygon_id = new_to_old_polygon_id[i];
            Polygon_id new_polygon_id = i;
            old_to_new_polygon_id[old_polygon_id] = new_polygon_id;
            log_weld.trace("{}: old {} -> new {}\n", i, old_polygon_id, old_to_new_polygon_id[old_polygon_id]);
        }
        for (Polygon_id i = 0; i < m_next_polygon_id; ++i)
        {
            log_weld.trace("old_to_new[{}] = {}, new_to_old[{}] = {}\n", i, old_to_new_polygon_id[i], i, new_to_old_polygon_id[i]);
        }
    }

    // Scan for mergable points
    struct Point_data
    {
        Point_data(Point_id id, const Point_attribute_maps& attribute_maps)
        {
            if ((attribute_maps.locations  != nullptr) && attribute_maps.locations ->has(id)) position  = attribute_maps.locations ->get(id);
            if ((attribute_maps.normals    != nullptr) && attribute_maps.normals   ->has(id)) normal    = attribute_maps.normals   ->get(id);
            if ((attribute_maps.tangents   != nullptr) && attribute_maps.tangents  ->has(id)) tangent   = attribute_maps.tangents  ->get(id); 
            if ((attribute_maps.bitangents != nullptr) && attribute_maps.bitangents->has(id)) bitangent = attribute_maps.bitangents->get(id);  
            if ((attribute_maps.texcoords  != nullptr) && attribute_maps.texcoords ->has(id)) texcoord  = attribute_maps.texcoords ->get(id); 
        }
        std::optional<glm::vec3> position;
        std::optional<glm::vec3> normal;
        std::optional<glm::vec3> tangent;
        std::optional<glm::vec3> bitangent;
        std::optional<glm::vec2> texcoord;
    };
    std::vector<Point_id> remove_new_point_ids;
    for (Point_id new_point_id = 0; new_point_id < m_next_point_id; ++new_point_id)
    {
        Point_id old_point_id = new_to_old_point_id[new_point_id];
        Point_data data(old_point_id, point_attribute_maps);
        log_weld.trace("new point {} old point{}", new_point_id, old_point_id);
        if (data.position.has_value())
        {
            log_weld.trace(" position {}", data.position.value());
        }
        if (data.normal.has_value())
        {
            log_weld.trace(" normal {}", data.normal.value());
        }
        log_weld.trace("\n");
    }

    for (Point_id span_start = 0; span_start < m_next_point_id; ++span_start)
    {
        Point_id old_reference_point_id = new_to_old_point_id[span_start];
        Point_data reference(old_reference_point_id, point_attribute_maps);
        //log_weld.trace("new {} old {} reference position {}\n", span_start, new_to_old_point_id[span_start], reference.position.value());
        for (Point_id new_point_id = span_start + 1; new_point_id < m_next_point_id; ++new_point_id)
        {
            Point_id old_point_id = new_to_old_point_id[new_point_id];
            Point_data current(old_point_id, point_attribute_maps);
            //log_weld.trace("new {} old {} current position {}\n", new_point_id, old_point_id, current.position.value());

            if (std::abs(reference.position.value()[axis0] != current.position.value()[axis0]))
            {
                float diff = std::abs(reference.position.value()[axis0] - current.position.value()[axis0]);
                if (diff > 0.001f)
                {
                    log_weld.trace("primary axis diff: span {} new point {} - {} vs. {}, diff {}\n",
                                   span_start, new_point_id, reference.position.value(), current.position.value(), diff);
                    break;
                }
            }
            else if (std::abs(reference.position.value()[axis1] != current.position.value()[axis1]))
            {
                float diff = std::abs(reference.position.value()[axis1] - current.position.value()[axis1]);
                if (diff > 0.001f)
                {
                    log_weld.trace("secondary axis diff: span {} new point {} - {} vs. {}, diff {}\n",
                                   span_start, new_point_id, reference.position.value(), current.position.value(), diff);
                    break;
                }
            }
            else
            {
                float diff = std::abs(reference.position.value()[axis2] - current.position.value()[axis2]);
                if (diff > 0.001f)
                {
                    log_weld.trace("tertiary axis diff: span {} new point {} - {} vs. {}, diff {}\n",
                                   span_start, new_point_id, reference.position.value(), current.position.value(), diff);
                    break;
                }
            }

            if (reference.position.has_value() && current.position.has_value())
            {
                float distance = glm::distance(reference.position.value(), current.position.value());
                if (distance > weld_settings.max_point_distance)
                {
                    log_weld.trace("position distance too large for span {} point {} - {} vs. {}, distance {}\n",
                                   span_start, new_point_id, reference.position.value(), current.position.value(), distance);
                    reference = current;
                    continue;
                }
            }
            if (reference.normal.has_value() && current.normal.has_value())
            {
                float dot_product = glm::dot(reference.normal.value(), current.normal.value());
                if (dot_product < weld_settings.min_normal_dot_product)
                {
                    log_weld.trace("normal dot product too small for span {} point {} - {} vs. {}, distance {}\n",
                                   span_start, new_point_id, reference.normal.value(), current.normal.value(), dot_product);
                    reference = current;
                    continue;
                }
            }
            if (reference.tangent.has_value() && current.tangent.has_value())
            {
                float dot_product = glm::dot(reference.tangent.value(), current.tangent.value());
                if (dot_product < weld_settings.min_normal_dot_product)
                {
                    //log_weld.trace("tangent dot product {} too large\n", dot_product);
                    reference = current;
                    continue;
                }
            }
            if (reference.bitangent.has_value() && current.bitangent.has_value())
            {
                float dot_product = glm::dot(reference.bitangent.value(), current.bitangent.value());
                if (dot_product < weld_settings.min_normal_dot_product)
                {
                    //log_weld.trace("bitangent dot product {} too large\n", dot_product);
                    reference = current;
                    continue;
                }
            }
            if (reference.texcoord.has_value() && current.texcoord.has_value())
            {
                float distance = glm::distance(reference.texcoord.value(), current.texcoord.value());
                if (distance > weld_settings.max_texcoord_distance)
                {
                    //log_weld.trace("texcoord distance {} too large\n", distance);
                    reference = current;
                    continue;
                }
            }
            remove_new_point_ids.push_back(new_point_id);
            new_to_old_point_id[new_point_id] = new_to_old_point_id[span_start];
            old_to_new_point_id[old_point_id] = span_start;
            log_weld.trace("merging new point {} old point {} to new point {} old point {}\n",
                           new_point_id, old_point_id, span_start, new_to_old_point_id[span_start]);
            //{
            //    erhe::log::Log::Indenter scoped_indent;
            //    if (reference.position.has_value() && current.position.has_value())
            //    {
            //        log_weld.trace("position {} vs {}\n", reference.position.value(), current.position.value());
            //    }
            //    if (reference.normal.has_value() && current.normal.has_value())
            //    {
            //        log_weld.trace("normal {} vs {}\n", reference.normal.value(), current.normal.value());
            //    }
            //}
            points[old_reference_point_id].corner_count          += points[old_point_id].corner_count;
            points[old_reference_point_id].reserved_corner_count += points[old_point_id].reserved_corner_count;
        }
    }
    log_weld.trace("Merged {} points\n", remove_new_point_ids.size());

    //log_weld.trace("Point remapping after merge, before removing duplicate points:\n");
    //{
    //    log::Log::Indenter scoped_indent;
    //    for (Point_id i = 0; i < m_next_point_id; ++i)
    //    {
    //        log_weld.trace("old_to_new[{}] = {}, new_to_old[{}] = {}", i, old_to_new_point_id[i], i, new_to_old_point_id[i]);
    //    }
    //}

    // Remove points
    auto old_point_count = m_next_point_id;
    for (size_t i = 0; i < remove_new_point_ids.size(); ++i)
    {
        // Move to end of points
        Point_id new_remove_point_id = remove_new_point_ids[i];
        Point_id old_remove_point_id = new_to_old_point_id[new_remove_point_id];
        Point_id new_keep_point_id   = --m_next_point_id;
        Point_id old_keep_point_id   = new_to_old_point_id[new_keep_point_id];
        //log_weld.trace("New point {} old point {} is being removed - swapping with new point {} old point {}\n",
        //               new_remove_point_id, old_remove_point_id, new_keep_point_id, old_keep_point_id);
        std::swap(new_to_old_point_id[new_remove_point_id], 
                  new_to_old_point_id[new_keep_point_id]);
        std::swap(new_remove_point_id, new_keep_point_id);
        old_to_new_point_id[old_keep_point_id] = new_keep_point_id;
    }

    //log_weld.trace("Point remapping after removing duplicate points:\n");
    //{
    //    log::Log::Indenter scoped_indent;
    //    for (Point_id i = 0; i < old_point_count; ++i)
    //    {
    //        log_weld.trace("old_to_new[{}] = ", i);
    //        Point_id new_point_id = old_to_new_point_id[i];
    //        if (new_point_id != std::numeric_limits<Point_id>::max())
    //        {
    //            log_weld.trace("{}", new_point_id);
    //        }
    //        else
    //        {
    //            log_weld.trace("<removed>");
    //        }
    //        if (i < m_next_point_id)
    //        {
    //            log_weld.trace(", new_to_old[{}] = {}", i, new_to_old_point_id[i]);
    //        }
    //        log_weld.trace("\n");
    //    }
    //}

    // Remap corner points
    for (Corner_id corner_id = 0, end = corner_count(); corner_id < end; ++corner_id)
    {
        Corner& corner = corners[corner_id];
        Point_id old_point_id = corner.point_id;
        Point_id new_point_id = old_to_new_point_id[old_point_id];
        corner.point_id = new_point_id;
        //log_weld.trace("corner {} using Point {} -> {}\n", corner_id, old_point_id, new_point_id);
    }

    // Remap points
    auto old_points = points;
    for (Point_id new_point_id = 0, end = point_count(); new_point_id < end; ++new_point_id)
    {
        Point_id old_point_id = new_to_old_point_id[new_point_id];
        log_weld.trace("Point new {} from old {}\n", new_point_id, old_point_id);
        points[new_point_id] = old_points[old_point_id];
    }

    point_attributes().remap_keys(new_to_old_point_id);
    point_attributes().trim(point_count());

    // Scan for polygon merge/eliminate
#if 1
    struct Polygon_data
    {
        Polygon_data(Polygon_id id, const Polygon_attribute_maps& attribute_maps)
        {
            VERIFY((attribute_maps.centroids != nullptr) && attribute_maps.centroids->has(id));
            VERIFY((attribute_maps.normals   != nullptr) && attribute_maps.normals  ->has(id));
            centroid = attribute_maps.centroids->get(id);
            normal   = attribute_maps.normals  ->get(id);
        }
        glm::vec3 centroid;
        glm::vec3 normal;
    };
    size_t can_merge_polygon_count = 0;
    std::set<Polygon_id> remove_new_polygon_ids;
    for (Polygon_id span_start = 0; span_start < m_next_polygon_id; ++span_start)
    {
        Polygon_id old_reference_polygon_id = new_to_old_polygon_id[span_start];
        Polygon_data reference(old_reference_polygon_id, polygon_attribute_maps);
        log_weld.trace("new {} old {} reference centroid {}\n", span_start, old_reference_polygon_id, reference.centroid);
        for (Polygon_id new_polygon_id = span_start + 1; new_polygon_id < m_next_polygon_id; ++new_polygon_id)
        {
            Polygon_id old_polygon_id = new_to_old_polygon_id[new_polygon_id];
            Polygon_data current(old_polygon_id, polygon_attribute_maps);

            if (std::abs(reference.centroid[axis0] != current.centroid[axis0]))
            {
                float diff = std::abs(reference.centroid[axis0] - current.centroid[axis0]);
                if (diff > 0.001f)
                {
                    log_weld.trace("primary axis diff: span {} new polygon id {} - {} vs. {}, diff {}\n", span_start, new_polygon_id, reference.centroid, current.centroid, diff);
                    break;
                }
            }
            else if (std::abs(reference.centroid[axis1] != current.centroid[axis1]))
            {
                float diff = std::abs(reference.centroid[axis1] - current.centroid[axis1]);
                if (diff > 0.001f)
                {
                    log_weld.trace("secondary axis diff: span {} new polygon id {} - {} vs. {}, diff {}\n", span_start, new_polygon_id, reference.centroid, current.centroid, diff);
                    break;
                }
            }
            else
            {
                float diff = std::abs(reference.centroid[axis2] - current.centroid[axis2]);
                if (diff > 0.001f)
                {
                    log_weld.trace("tertiary axis diff: span {} new polygon id {} - {} vs. {}, diff {}\n", span_start, new_polygon_id, reference.centroid, current.centroid, diff);
                    break;
                }
            }

            float distance = glm::distance(reference.centroid, current.centroid);
            if (distance > weld_settings.max_point_distance)
            {
                //log_weld.trace("position distance {} too large\n", distance);
                reference = current;
                continue;
            }
            float dot_product = glm::dot(reference.normal, current.normal);
            if (std::abs(dot_product) < weld_settings.min_normal_dot_product)
            {
                //log_weld.trace("normal dot product {} too large\n", dot_product);
                reference = current;
                continue;
            }

            // Merge case: identical polygons
            // TODO check polygons have matching corners
            if (dot_product >= weld_settings.min_normal_dot_product)
            {
                ++can_merge_polygon_count;
                log_weld.trace("merging polygons {} and {}\n", span_start, new_polygon_id);
                {
                    erhe::log::Log::Indenter scoped_indent;
                    log_weld.trace("centroid {} vs {}\n", reference.centroid, current.centroid);
                    log_weld.trace("normal {} vs {}\n", reference.normal, current.normal);
                }
                new_to_old_polygon_id[new_polygon_id] = new_to_old_polygon_id[span_start];

                remove_new_polygon_ids.insert(new_polygon_id);
                new_to_old_polygon_id[new_polygon_id] = new_to_old_point_id[span_start];
                old_to_new_polygon_id[old_polygon_id] = span_start;
                log_weld.trace("merging new polygon {} old polygon {} to new point {} old point {}\n",
                               new_polygon_id, old_polygon_id, span_start, new_to_old_point_id[span_start]);
            }

            // Elimination case: opposite polygons
            // TODO check polygons have matching corners
            if (dot_product <= -weld_settings.min_normal_dot_product)
            {
                log_weld.trace("elimination polygons {} and {}\n", span_start, new_polygon_id);
                {
                    erhe::log::Log::Indenter scoped_indent;
                    log_weld.trace("centroid {} vs {}\n", reference.centroid, current.centroid);
                    log_weld.trace("normal {} vs {}\n", reference.normal, current.normal);
                }
                //new_to_old_polygon_id[new_polygon_id] = new_to_old_polygon_id[span_start];

                remove_new_polygon_ids.insert(span_start);
                remove_new_polygon_ids.insert(new_polygon_id);
                //new_to_old_polygon_id[new_polygon_id] = new_to_old_point_id[span_start];
                //old_to_new_polygon_id[old_polygon_id] = span_start;
                //log_weld.trace("merging new polygon {} old polygon {} to new point {} old point {}\n",
                //               new_polygon_id, old_polygon_id, span_start, new_to_old_point_id[span_start]);
            }

        }
    }

    // Remove polygons
    auto old_polygon_count = m_next_polygon_id;
    for (Polygon_id new_remove_polygon_id : remove_new_polygon_ids)
    {
        Polygon_id old_remove_polygon_id = new_to_old_polygon_id[new_remove_polygon_id];
        Polygon_id new_keep_polygon_id   = --m_next_polygon_id;
        Polygon_id old_keep_polygon_id   = new_to_old_polygon_id[new_keep_polygon_id];
        log_weld.trace("New polygon {} old {} is being removed - swapping with new polygon {} old {}\n",
                       new_remove_polygon_id, old_remove_polygon_id, new_keep_polygon_id, old_keep_polygon_id);
        std::swap(new_to_old_polygon_id[new_remove_polygon_id], 
                  new_to_old_polygon_id[new_keep_polygon_id]);
        std::swap(new_remove_polygon_id, new_keep_polygon_id);
        old_to_new_polygon_id[old_keep_polygon_id] = new_keep_polygon_id;
    }

    // Remap corner polygons
    for (Corner_id corner_id = 0, end = corner_count(); corner_id < end; ++corner_id)
    {
        Corner& corner = corners[corner_id];
        Polygon_id old_polygon_id = corner.polygon_id;
        Polygon_id new_polygon_id = old_to_new_polygon_id[old_polygon_id];
        corner.polygon_id = new_polygon_id;
        log_weld.trace("corner {} using Polygon {} -> {}\n", corner_id, old_polygon_id, new_polygon_id);
    }

    // Remap polygons
    auto old_polygons = polygons;
    for (Polygon_id new_polygon_id = 0, end = polygon_count(); new_polygon_id < end; ++new_polygon_id)
    {
        Polygon_id old_polygon_id = new_to_old_polygon_id[new_polygon_id];
        log_weld.trace("Polygon new {} from old {}\n", new_polygon_id, old_polygon_id);
        polygons[new_polygon_id] = old_polygons[old_polygon_id];
    }

    polygon_attributes().remap_keys(new_to_old_polygon_id);
    polygon_attributes().trim(polygon_count());

    make_point_corners();

    // Remove unused corners
    std::vector<bool> corner_used;
    corner_used.resize(corner_count());
    for (Point_id point_id = 0, end = point_count(); point_id < end; ++point_id)
    {
        const Point& point = points[point_id];
        for (Point_corner_id point_corner_id = point.first_point_corner_id,
             end = point.first_point_corner_id + point.corner_count;
             point_corner_id < end;
             ++point_corner_id)
        {
            Corner_id corner_id = point_corners[point_corner_id];
            corner_used[corner_id] = true;
        }
    }
    for (Polygon_id polygon_id = 0, end = polygon_count(); polygon_id < end; ++polygon_id)
    {
        const Polygon& polygon = polygons[polygon_id];
        for (Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
             end = polygon.first_polygon_corner_id + polygon.corner_count;
             polygon_corner_id < end;
             ++polygon_corner_id)
        {
            Corner_id corner_id = polygon_corners[polygon_corner_id];
            corner_used[corner_id] = true;
        }
    }

    // Sort corners
    std::vector<Corner_id> new_to_old_corner_id;
    new_to_old_corner_id.resize(corner_count());
    log_weld.trace("Corner usage:\n");
    for (Corner_id corner_id = 0; corner_id < corner_count(); ++corner_id)
    {
        new_to_old_corner_id[corner_id] = corner_id;
        log_weld.trace("Corner {} {}\n", corner_id, corner_used[corner_id]);
    }

    std::sort(new_to_old_corner_id.begin(),
              new_to_old_corner_id.end(),
              [corner_used](const Corner_id& lhs, const Corner_id& rhs)
              {
                  if (corner_used[lhs] && !corner_used[rhs])
                  {
                      return true;
                  }
                  return false;
              }
    );

    log_weld.trace("Corner sort:\n");
    std::vector<Corner_id> old_to_new_corner_id;
    old_to_new_corner_id.resize(corner_count());
    Corner_id used_corner_count = 0;
    for (Corner_id i = 0; i < corner_count(); ++i)
    {
        Corner_id old_corner_id = new_to_old_corner_id[i];
        Corner_id new_corner_id = i;
        old_to_new_corner_id[old_corner_id] = new_corner_id;
        used_corner_count += corner_used[old_corner_id] ? 1 : 0;
        log_weld.trace("{}: old {} -> new {} - used = {}\n",
                       i, old_corner_id, old_to_new_corner_id[old_corner_id], corner_used[old_corner_id]);
    }
    m_next_corner_id = used_corner_count;

    // Remap corners
    auto old_corners = corners;
    for (Corner_id new_corner_id = 0, end = m_next_corner_id; new_corner_id < end; ++new_corner_id)
    {
        Corner_id old_corner_id = new_to_old_corner_id[new_corner_id];
        log_weld.trace("Corner new {} from old {}\n", new_corner_id, old_corner_id);
        corners[new_corner_id] = old_corners[old_corner_id];
    }

    corner_attributes().remap_keys(new_to_old_corner_id);
    corner_attributes().trim(corner_count());

    for (Point_id point_id = 0, end = point_count(); point_id < end; ++point_id)
    {
        const Point& point = points[point_id];
        for (Point_corner_id point_corner_id = point.first_point_corner_id,
             end = point.first_point_corner_id + point.corner_count;
             point_corner_id < end;
             ++point_corner_id)
        {
            Corner_id old_corner_id = point_corners[point_corner_id];
            point_corners[point_corner_id] = old_to_new_corner_id[old_corner_id];
        }
    }
    for (Polygon_id polygon_id = 0, end = polygon_count(); polygon_id < end; ++polygon_id)
    {
        const Polygon& polygon = polygons[polygon_id];
        for (Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
             end = polygon.first_polygon_corner_id + polygon.corner_count;
             polygon_corner_id < end;
             ++polygon_corner_id)
        {
            Corner_id old_corner_id = polygon_corners[polygon_corner_id];
            polygon_corners[polygon_corner_id] = old_to_new_corner_id[old_corner_id];
        }
    }

    // Sanity check
    for (Point_id point_id = 0, end = point_count(); point_id < end; ++point_id)
    {
        const Point& point = points[point_id];
        for (Point_corner_id point_corner_id = point.first_point_corner_id,
             end = point.first_point_corner_id + point.corner_count;
             point_corner_id < end;
             ++point_corner_id)
        {
            Corner_id corner_id = point_corners[point_corner_id];
            const Corner& corner = corners[corner_id];
            VERIFY(corner.point_id == point_id);
            VERIFY(corner.polygon_id < m_next_polygon_id);
        }
    }
    for (Polygon_id polygon_id = 0, end = polygon_count(); polygon_id < end; ++polygon_id)
    {
        const Polygon& polygon = polygons[polygon_id];
        for (Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
             end = polygon.first_polygon_corner_id + polygon.corner_count;
             polygon_corner_id < end;
             ++polygon_corner_id)
        {
            Corner_id corner_id = polygon_corners[polygon_corner_id];
            const Corner& corner = corners[corner_id];
            VERIFY(corner.polygon_id == polygon_id);
            VERIFY(corner.point_id < m_next_point_id);
        }
    }
    for (Point_id point_id = 0, end = point_count(); point_id < end; ++point_id)
    {
        const Point& point = points[point_id];
        for (Point_corner_id point_corner_id = point.first_point_corner_id,
             end = point.first_point_corner_id + point.corner_count;
             point_corner_id < end;
             ++point_corner_id)
        {
            Corner_id corner_id = point_corners[point_corner_id];
            const Corner& corner = corners[corner_id];
            VERIFY(corner.point_id == point_id);
            VERIFY(corner.polygon_id < m_next_polygon_id);
        }
    }
    for (Corner_id corner_id = 0, end = corner_count(); corner_id < end; ++corner_id)
    {
        const Corner&  corner  = corners [corner_id];
        const Point&   point   = points  [corner.point_id];
        const Polygon& polygon = polygons[corner.polygon_id];
        VERIFY(corner.point_id   < m_next_point_id);
        VERIFY(corner.polygon_id < m_next_polygon_id);
        bool corner_found = false;
        for (Point_corner_id point_corner_id = point.first_point_corner_id,
             end = point.first_point_corner_id + point.corner_count;
             point_corner_id < end;
             ++point_corner_id)
        {
            if (point_corners[point_corner_id] == corner_id)
            {
                corner_found = true;
                break;
            }
        }
        VERIFY(corner_found);
        corner_found = false;
        for (Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
             end = polygon.first_polygon_corner_id + polygon.corner_count;
             polygon_corner_id < end;
             ++polygon_corner_id)
        {
            if (polygon_corners[polygon_corner_id] == corner_id)
            {
                corner_found = true;
                break;
            }
        }
        VERIFY(corner_found);
        
    }
#endif

    log_weld.trace("merge done\n");
}


} // namespace erhe::geometry

