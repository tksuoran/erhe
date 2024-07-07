#include <spdlog/spdlog.h>

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry {

// Allocates new Corner / Corner_id
// - Point must be allocated.
// - Polygon must be allocated
auto Geometry::make_corner(const Point_id point_id, const Polygon_id polygon_id) -> Corner_id
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(point_id < m_next_point_id);
    ERHE_VERIFY(polygon_id < m_next_polygon_id);
    ++m_serial;
    const Corner_id corner_id = m_next_corner_id++;

    if (corner_id >= corners.size()) {
        corners.resize(static_cast<size_t>(corner_id) + s_grow);
    }

    ERHE_VERIFY(corner_id < corners.size());
    Corner& corner = corners[corner_id];
    corner.point_id   = point_id;
    corner.polygon_id = polygon_id;
    SPDLOG_LOGGER_TRACE(log, "\tmake_corner(point_id = {}, polygon_id = {}) = corner_id = {}", point_id, polygon_id, corner_id);
    return corner_id;
}

// Allocates new Point / Point_id
auto Geometry::make_point() -> Point_id
{
    ERHE_PROFILE_FUNCTION();

    ++m_serial;

    const Point_id point_id = m_next_point_id++;

    // TODO
    if (point_id >= points.size()) {
        points.resize(static_cast<size_t>(point_id) + s_grow);
    }

    ERHE_VERIFY(point_id < points.size());
    //points[point_id].first_point_corner_id = m_next_point_corner;
    points[point_id].corner_count = 0;
    SPDLOG_LOGGER_TRACE(log, "\tmake_point() point_id = {}", point_id);
    return point_id;
}

// Allocates new Polygon / Polygon_id
auto Geometry::make_polygon() -> Polygon_id
{
    ERHE_PROFILE_FUNCTION();

    ++m_serial;

    const Polygon_id polygon_id = m_next_polygon_id++;

    // TODO
    if (polygon_id >= polygons.size()) {
        polygons.resize(static_cast<size_t>(polygon_id) + s_grow);
    }

    ERHE_VERIFY(polygon_id < polygons.size());
    polygons[polygon_id].corner_count = 0;
    SPDLOG_LOGGER_TRACE(log, "\tmake_polygon() polygon_id = {}", polygon_id);
    return polygon_id;
}

// Allocates new Edge / Edge_id
// - Points must be already allocated
// - Points must be ordered
auto Geometry::make_edge(const Point_id a, const Point_id b) -> Edge_id
{
    ERHE_PROFILE_FUNCTION();

    ++m_serial;

    ERHE_VERIFY(a < b);
    //ERHE_VERIFY(a < b);
    ERHE_VERIFY(b < points.size());
    const Edge_id edge_id = m_next_edge_id++;

    if (edge_id >= edges.size()) {
        edges.resize(static_cast<size_t>(edge_id) + s_grow);
    }

    ERHE_VERIFY(edge_id < edges.size());
    Edge& edge = edges[edge_id];
    edge.a = a;
    edge.b = b;
    edge.first_edge_polygon_id = m_next_edge_polygon_id;
    edge.polygon_count = 0;
    SPDLOG_LOGGER_TRACE(log, "\tmake_edge(a = {}, b = {}) edge_id = {}", a, b, edge_id);
    return edge_id;
}

// Allocates new point corner.
// - Point must be already allocated.
// - Corner must be already allocated.
void Geometry::reserve_point_corner(const Point_id point_id, const Corner_id corner_id)
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(point_id < m_next_point_id);
    ERHE_VERIFY(corner_id < m_next_corner_id);

    ++m_serial;

    Point& point = points[point_id];
    ++m_next_point_corner_reserve;
    point.reserved_corner_count++;
}

void Geometry::make_point_corners()
{
    ERHE_PROFILE_FUNCTION();

    ++m_serial;

    Point_corner_id next_point_corner = 0;
    for_each_point([&](auto& i) {
        i.point.first_point_corner_id = next_point_corner;
        i.point.corner_count = 0; // reset in case this has been done before
        next_point_corner += i.point.reserved_corner_count;
    });
    m_next_point_corner_reserve = next_point_corner;
    point_corners.resize(static_cast<size_t>(m_next_point_corner_reserve));
    for_each_polygon([&](auto& i) {
        i.polygon.for_each_corner(*this, [&](auto& j) {
            //ERHE_VERIFY(j.corner_id != std::numeric_limits<Corner_id>::max());
            //ERHE_VERIFY(j.corner_id < m_next_corner_id);
            const Point_id  point_id        = j.corner.point_id;
            //ERHE_VERIFY(point_id != std::numeric_limits<Point_id>::max());
            //ERHE_VERIFY(point_id < m_next_point_id);
            Point&          point           = j.geometry.points[point_id];
            Point_corner_id point_corner_id = point.first_point_corner_id + point.corner_count++;
            //ERHE_VERIFY(point_corner_id < point_corners.size());
            point_corners[point_corner_id] = j.corner_id;
        });
    });

    // Point corners have been added above in some non-specific order.
    sort_point_corners();
}

void Geometry::sort_point_corners()
{
    ERHE_PROFILE_FUNCTION();

    class Point_corner_info
    {
    public:
        Point_corner_id point_corner_id{0};
        Corner_id       corner_id      {0};
        Point_id        prev_point_id  {0};
        Point_id        next_point_id  {0};
        bool            used           {false};
    };

    std::vector<Point_corner_info> point_corner_infos;
    point_corner_infos.reserve(20);

    bool failures{false};

    for_each_point([&](auto& i) {
        if (i.point.corner_count < 3) {
            return;
        }
        point_corner_infos.clear();
        i.point.for_each_corner(*this, [&](auto& j) {
            const Corner_id          middle_corner_id = j.corner_id; //point_corners[j.point_corner_id];
            std::optional<Corner_id> next_corner_id   = 0;
            const Corner&            middle_corner = j.corner; // corners[middle_corner_id];
            const Polygon_id         polygon_id    = middle_corner.polygon_id;
            const Polygon&           polygon       = polygons[polygon_id];
            polygon.for_each_corner_neighborhood_const(*this, [&](auto& k) {
                if (k.corner_id == middle_corner_id) {
                    const Corner& prev_corner = corners[k.prev_corner_id];
                    const Corner& next_corner = corners[k.next_corner_id];

                    point_corner_infos.push_back(
                        {
                            .point_corner_id = j.point_corner_id,
                            .corner_id       = middle_corner_id,
                            .prev_point_id   = prev_corner.point_id,
                            .next_point_id   = next_corner.point_id,
                            .used            = false
                        }
                    );
                    k.break_iteration();
                }
            });
        });

#if 0
        log_geometry->info("sort: point_id = {}, corner_count = {}", i.point_id, i.point.corner_count);
#endif
        for (uint32_t j = 0, end = static_cast<uint32_t>(point_corner_infos.size()); j < end; ++j) {
            const uint32_t     next_j = (j + 1) % end;
            Point_corner_info& head   = point_corner_infos[j];
            Point_corner_info& next   = point_corner_infos[next_j];
            bool found{false};
#if 0
            log_geometry->info(
                "    j = {}, next_j = {}, head.point_corner_id = {}, head.prev_point_id = {}, head.next_point_id = {}",
                j,
                next_j,
                head.point_corner_id,
                head.prev_point_id,
                head.next_point_id
            );
#endif
            for (uint32_t k = 0; k < end; ++k) {
                Point_corner_info& node = point_corner_infos[k];
#if 0
                log_geometry->info(
                    "    k = {}, node.point_corner_id = {}, node.next_point_id = {}, prev_point_id = {}, used = {}",
                    k,
                    node.point_corner_id,
                    node.next_point_id,
                    node.prev_point_id,
                    node.used
                );
#endif
                if (node.used) {
                    continue;
                }
                if (node.next_point_id == head.prev_point_id) {
                    found = true;
                    node.used = true;
                    if (k != next_j) {
                        std::swap(next, node);
                    }
                    break;
                }
            }
            if (!found) {
                //// log_geometry->warn(
                ////     "Could not sort point corners for point_id = {} head.prev_point_id = {}",
                ////     i.point_id,
                ////     head.prev_point_id
                //// );
                failures = true;
            }
            const Point_corner_id point_corner_id = i.point.first_point_corner_id + j;
            point_corners[point_corner_id] = head.corner_id;
        }
    });

    ///// TODO
    // if (failures) {
    //     log_geometry->error("Could not sort point corners");
    //     debug_trace();
    //     log_geometry->error("Could not sort point corners");
    // }
}

// Allocates new edge polygon.
// - Edge must be already allocated.
// - Polygon must be already allocated.
auto Geometry::make_edge_polygon(const Edge_id edge_id, const Polygon_id polygon_id) -> Edge_polygon_id
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(edge_id < m_next_edge_id);
    ERHE_VERIFY(polygon_id < m_next_polygon_id);

    ++m_serial;

    Edge& edge = edges[edge_id];
    if (edge.polygon_count == 0) {
        edge.first_edge_polygon_id = m_next_edge_polygon_id;
        m_edge_polygon_edge = edge_id;
    } else {
        ERHE_VERIFY(m_edge_polygon_edge == edge_id);
    }
    ++m_next_edge_polygon_id;
    const Edge_polygon_id edge_polygon_id = edge.first_edge_polygon_id + edge.polygon_count;
    ++edge.polygon_count;
    SPDLOG_LOGGER_TRACE(
        log,
        "\tmake_edge_polygon(edge = {}, polygon = {}) first_edge_polygon_id = {}, edge.polygon_count = {}",
        edge_id,
        polygon_id,
        edge.first_edge_polygon_id,
        edge.polygon_count
    );

    if (edge_polygon_id >= edge_polygons.size()) {
        edge_polygons.resize(edge_polygon_id + s_grow);
    }

    edge_polygons[edge_polygon_id] = polygon_id;
    return edge_polygon_id;
}

// Allocates new polygon corner.
// - Polygon must be already allocated.
// - Corner must be already allocated.
auto Geometry::make_polygon_corner_(
    const Polygon_id polygon_id,
    const Corner_id  corner_id
) -> Polygon_corner_id
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(polygon_id < m_next_polygon_id);
    ERHE_VERIFY(corner_id < m_next_corner_id);

    ++m_serial;

    Polygon& polygon = polygons[polygon_id];
    if (polygon.corner_count == 0) {
        polygon.first_polygon_corner_id = m_next_polygon_corner_id;
        m_polygon_corner_polygon = polygon_id;
    } else {
        ERHE_VERIFY(m_polygon_corner_polygon == polygon_id);
    }
    ++m_next_polygon_corner_id;
    const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + polygon.corner_count;
    ++polygon.corner_count;

    SPDLOG_LOGGER_TRACE(
        log,
        "\tmake_polygon_corner_(polygon_id = {}, corner_id = {}) first_polygon_corner_id = {}, polygon.corner_count = {}",
        polygon_id,
        corner_id,
        polygon.first_polygon_corner_id,
        polygon.corner_count
    );

    if (polygon_corner_id >= polygon_corners.size()) {
        polygon_corners.resize(polygon_corner_id + s_grow);
    }

    polygon_corners[polygon_corner_id] = corner_id;
    return polygon_corner_id;
}

// Allocates new polygon corner.
// - Polygon must be already allocated.
// - Point must be already allocated.
auto Geometry::make_polygon_corner(
    const Polygon_id polygon_id,
    const Point_id   point_id
) -> Corner_id
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(polygon_id < m_next_polygon_id);
    ERHE_VERIFY(point_id < m_next_point_id);

    ++m_serial;

    const Corner_id corner_id = make_corner(point_id, polygon_id);
    make_polygon_corner_(polygon_id, corner_id);
    reserve_point_corner(point_id, corner_id);
    SPDLOG_LOGGER_TRACE(log, "polygon {} adding point {} as corner {}", polygon_id, point_id, corner_id);
    return corner_id;
}

auto Geometry::make_point(const float x, const float y, const float z) -> Point_id
{
    ERHE_PROFILE_FUNCTION();

    const Point_id point_id        = make_point();
    auto* const    point_positions = point_attributes().find_or_create<glm::vec3>(c_point_locations);

    point_positions->put(point_id, glm::vec3{x, y, z});

    return point_id;
}

auto Geometry::make_point(const float x, const float y, const float z, const float s, const float t) -> Point_id
{
    ERHE_PROFILE_FUNCTION();

    const Point_id point_id        = make_point();
    auto* const    point_positions = point_attributes().find_or_create<glm::vec3>(c_point_locations);
    auto* const    point_texcoords = point_attributes().find_or_create<glm::vec2>(c_point_texcoords);

    point_positions->put(point_id, glm::vec3{x, y, z});
    point_texcoords->put(point_id, glm::vec2{s, t});

    return point_id;
}

auto Geometry::make_point(const double x, const double y, const double z) -> Point_id
{
    return make_point(float(x), float(y), float(z));
}

auto Geometry::make_point(const double x, const double y, const double z, const double s, const double t) -> Point_id
{
    return make_point(float(x), float(y), float(z), float(s), float(t));
}

auto Geometry::make_polygon(const std::initializer_list<Point_id> point_list) -> Polygon_id
{
    ERHE_PROFILE_FUNCTION();

    const Polygon_id polygon_id = make_polygon();
    for (Point_id point_id : point_list) {
        make_polygon_corner(polygon_id, point_id);
    }
    return polygon_id;
}

auto Geometry::make_polygon_reverse(const std::initializer_list<Point_id> point_list) -> Polygon_id
{
    ERHE_PROFILE_FUNCTION();

    const Polygon_id polygon_id = make_polygon();
    for (
        auto i = rbegin(point_list), end = rend(point_list);
        i != end;
        ++i
    ) {
        const Point_id point_id = *i;
        make_polygon_corner(polygon_id, point_id);
    }
    return polygon_id;
}

void Geometry::compute_polygon_corner_texcoords(Property_map<Corner_id, glm::vec2>* corner_texcoords)
{
    // These are required for fallback
    compute_polygon_normals();
    compute_polygon_centroids();
    const auto* const polygon_normals   = polygon_attributes().find          <glm::vec3>(c_polygon_normals  );
    const auto* const polygon_centroids = polygon_attributes().find          <glm::vec3>(c_polygon_centroids);
    const auto* const point_locations   = point_attributes  ().find          <glm::vec3>(c_point_locations  );
    //      auto* const corner_texcoords  = corner_attributes ().find_or_create<glm::vec2>(c_corner_texcoords );

    for_each_polygon([&](auto& i) {
        std::array<glm::vec2, 4> texcoords = {
            glm::vec2{0, 0},
            glm::vec2{1, 0},
            glm::vec2{1, 1},
            glm::vec2{0, 1}
        };
        std::size_t slot = 0;
        if (i.polygon.corner_count <= 4) {
            i.polygon.for_each_corner_const(
                *this,
                [&](const erhe::geometry::Polygon::Polygon_corner_context_const& j) {
                    corner_texcoords->put(j.corner_id, texcoords[slot++]);
                }
            );
        } else {
            // Fallback when there are more than 4 corners
            i.polygon.compute_planar_texture_coordinates(
                i.polygon_id,
                *this,
                *corner_texcoords,
                *polygon_centroids,
                *polygon_normals,
                *point_locations,
                true
            );
        }
    });
}

} // namespace erhe::geometry
