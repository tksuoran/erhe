#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/log.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"

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
using glm::mat3;
using glm::mat4;

Geometry::Geometry() = default;

Geometry::Geometry(
    std::string_view               name,
    std::function<void(Geometry&)> generator
)
    : name{name}
{
    if (generator)
    {
        generator(*this);
    }
}

Geometry::Geometry(std::string_view name)
    : name{name}
{
}

Geometry::~Geometry()
{
    // TODO Debug how to enable named return value optimization
    // log_geometry.warn("Geometry {} destructor\n", name);
}

Geometry::Geometry(Geometry&& other) noexcept
    : name                                {std::move(other.name            )}
    , corners                             {std::move(other.corners         )}
    , points                              {std::move(other.points          )}
    , polygons                            {std::move(other.polygons        )}
    , edges                               {std::move(other.edges           )}
    , point_corners                       {std::move(other.point_corners   )}
    , polygon_corners                     {std::move(other.polygon_corners )}
    , edge_polygons                       {std::move(other.edge_polygons   )}
    , m_next_corner_id                    {other.m_next_corner_id           }
    , m_next_point_id                     {other.m_next_point_id            }
    , m_next_polygon_id                   {other.m_next_polygon_id          }
    , m_next_edge_id                      {other.m_next_edge_id             }
    , m_next_point_corner_reserve         {other.m_next_point_corner_reserve}
    , m_next_polygon_corner_id            {other.m_next_polygon_corner_id   }
    , m_next_edge_polygon_id              {other.m_next_edge_polygon_id     }
    , m_polygon_corner_polygon            {other.m_polygon_corner_polygon   }
    , m_edge_polygon_edge                 {other.m_edge_polygon_edge        }
    , m_point_property_map_collection     {std::move(other.m_point_property_map_collection)}
    , m_corner_property_map_collection    {std::move(other.m_corner_property_map_collection)}
    , m_polygon_property_map_collection   {std::move(other.m_polygon_property_map_collection)}
    , m_edge_property_map_collection      {std::move(other.m_edge_property_map_collection)}
    , m_serial                            {other.m_serial}
    , m_serial_edges                      {other.m_serial_edges                      }
    , m_serial_polygon_normals            {other.m_serial_polygon_normals            }
    , m_serial_polygon_centroids          {other.m_serial_polygon_centroids          }
    , m_serial_polygon_tangents           {other.m_serial_polygon_tangents           }
    , m_serial_polygon_bitangents         {other.m_serial_polygon_bitangents         }
    , m_serial_polygon_texture_coordinates{other.m_serial_polygon_texture_coordinates}
    , m_serial_point_normals              {other.m_serial_point_normals              }
    , m_serial_point_tangents             {other.m_serial_point_tangents             }
    , m_serial_point_bitangents           {other.m_serial_point_bitangents           }
    , m_serial_point_texture_coordinates  {other.m_serial_point_texture_coordinates  }
    , m_serial_smooth_point_normals       {other.m_serial_smooth_point_normals       }
    , m_serial_corner_normals             {other.m_serial_corner_normals             }
    , m_serial_corner_tangents            {other.m_serial_corner_tangents            }
    , m_serial_corner_bitangents          {other.m_serial_corner_bitangents          }
    , m_serial_corner_texture_coordinates {other.m_serial_corner_texture_coordinates }
{
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

auto Mesh_info::operator+=(const Mesh_info& o)
-> Mesh_info&
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

void Geometry::reserve_points(const size_t point_count)
{
    ZoneScoped;

    if (point_count > points.size())
    {
        points.reserve(point_count);
    }
}

void Geometry::reserve_polygons(const size_t polygon_count)
{
    ZoneScoped;

    if (polygon_count > polygons.size())
    {
        polygons.reserve(polygon_count);
    }
}

auto Geometry::has_polygon_normals() const -> bool
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
    return false;
}

// Requires point locations
auto Geometry::compute_polygon_normals() -> bool
{
    ZoneScoped;

    if (has_polygon_normals())
    {
        return true;
    }

    log_geometry.info("{} for {}\n", __func__, name);

    auto*       polygon_normals = polygon_attributes().find_or_create<vec3>(c_polygon_normals);
    const auto* point_locations = point_attributes()  .find          <vec3>(c_point_locations);

    if (point_locations == nullptr)
    {
        log_geometry.warn("{} {}: Point locations are required, but not found.\n", __func__, name);
        return false;
    }

    for_each_polygon([&](auto& i)
    {
        i.polygon.compute_normal(i.polygon_id, *this, *polygon_normals, *point_locations);
    });

    m_serial_polygon_normals = m_serial;

    return true;
}

auto Geometry::has_polygon_centroids() const -> bool
{
    if (m_serial_polygon_centroids == m_serial)
    {
        return true;
    }

    if (m_next_polygon_id == 0)
    {
        return true;
    }

    return false;
}

auto Geometry::compute_polygon_centroids() -> bool
{
    ZoneScoped;

    if (has_polygon_centroids())
    {
        return true;
    }

    log_geometry.info("{} for {}\n", __func__, name);

    auto*       polygon_centroids = polygon_attributes().find_or_create<vec3>(c_polygon_centroids);
    const auto* point_locations   = point_attributes()  .find          <vec3>(c_point_locations);

    if (point_locations == nullptr)
    {
        log_geometry.warn("{} {}: Point locations are required, but not found.\n", __func__, name);
        return false;
    }

    for_each_polygon([&](auto& i)
    {
        i.polygon.compute_centroid(i.polygon_id, *this, *polygon_centroids, *point_locations);
    });

    m_serial_polygon_centroids = m_serial;

    return true;
}

auto Geometry::has_edges() const -> bool
{
    if (m_serial_edges == m_serial)
    {
        return true;
    }
    if (m_next_polygon_id == 0)
    {
        return true;
    }

    return false;
}

void Geometry::build_edges()
{
    ZoneScoped;

    if (has_edges())
    {
        return;
    }

    edges.clear();
    m_next_edge_id = 0;

    log_build_edges.trace("build_edges() : {} polygons\n", m_next_polygon_id);
    erhe::log::Indenter scope_indent;

    size_t polygon_index{0};
    for_each_polygon([&](auto& i)
    {
        erhe::log::Indenter scope_indent;

        i.polygon.for_each_corner_neighborhood(*this, [&](auto& j)
        {
            const Point_id a = j.prev_corner.point_id;
            const Point_id b = j.corner.point_id;
            if (a == b)
            {
                log_build_edges.warn("Bad edge {} - {}\n", a, b);
                return;
            }
            if (a < b)
            {
                const Edge_id edge_id = make_edge(a, b);
                const Point&  pa      = points[a];
                make_edge_polygon(edge_id, i.polygon_id);
                VERIFY(pa.corner_count > 0);
                pa.for_each_corner_const(*this, [&](auto& k)
                {
                     const Polygon_id polygon_id_in_point = k.corner.polygon_id;
                     const Polygon&   polygon_in_point    = polygons[polygon_id_in_point];
                     const Corner_id  prev_corner_id      = polygon_in_point.prev_corner(*this, k.corner_id);
                     const Corner&    prev_corner         = corners[prev_corner_id];
                     const Point_id   prev_point_id       = prev_corner.point_id;
                     if (prev_point_id == b)
                     {
                         make_edge_polygon(edge_id, polygon_id_in_point);
                         ++polygon_index;
                     }
                });
            }
        });
    });

    m_serial_edges = m_serial;
}

void Geometry::debug_trace() const
{
    ZoneScoped;

    for_each_corner_const([](auto& i)
    {
        log_geometry.info("corner {:2} = point {:2} polygon {:2}\n", i.corner_id, i.corner.point_id, i.corner.polygon_id);
    });

    for_each_point_const([&](auto& i)
    {
        log_geometry.info("point {:2} corners  = ", i.point_id);
        i.point.for_each_corner_const(*this, [&](auto& j)
        {
            if (j.corner_id > i.point.first_point_corner_id)
            {
                log_geometry.info(", ");
            }
            log_geometry.info("{:2}", j.corner_id);
        });
        log_geometry.info("\n");
        log_geometry.info("point {:2} polygons = ", i.point_id);
        i.point.for_each_corner_const(*this, [&](auto& j)
        {
            if (j.corner_id > i.point.first_point_corner_id)
            {
                log_geometry.info(", ");
            }
            log_geometry.info("{:2}", j.corner.polygon_id);
        });
        log_geometry.info("\n");
    });

    for_each_polygon_const([&](auto& i)
    {
        log_geometry.info("polygon {:2} corners = ", i.polygon_id);
        i.polygon.for_each_corner_const(*this, [&](auto& j)
        {
            if (j.polygon_corner_id > i.polygon.first_polygon_corner_id)
            {
                log_geometry.info(", ");
            }
            log_geometry.info("{:2}", j.corner_id);
        });
        log_geometry.info("\n");
        log_geometry.info("polygon {:2} points  = ", i.polygon_id);
        i.polygon.for_each_corner_const(*this, [&](auto& j)
        {
            Point_id point_id = j.corner.point_id;
            if (j.polygon_corner_id > i.polygon.first_polygon_corner_id)
            {
                log_geometry.info(", ");
            }
            log_geometry.info("{:2}", point_id);
        });
        log_geometry.info("\n");
    });

    for_each_edge_const([&](auto& i)
    {
        log_geometry.info("edge {:2} = {:2} .. {:2} :", i.edge_id, i.edge.a, i.edge.b);
        i.edge.for_each_polygon_const(*this, [&](auto& j)
        {
            log_geometry.info("{:2} ", j.polygon_id);
        });
        log_geometry.info("\n");
    });
}

auto Geometry::compute_point_normal(const Point_id point_id)
-> vec3
{
    ZoneScoped;

    Expects(point_id < points.size());

    compute_polygon_normals();
    const auto* polygon_normals = polygon_attributes().find<vec3>(c_polygon_normals);

    vec3 normal_sum(0.0f, 0.0f, 0.0f);

    points[point_id].for_each_corner(*this, [&](auto& i)
    {
        if (polygon_normals->has(i.corner.polygon_id))
        {
            normal_sum += polygon_normals->get(i.corner.polygon_id);
        }
        else
        {
            // TODO on demand calculate polygon normal?
        }
    });
    return normalize(normal_sum);
}

auto Geometry::has_point_normals() const -> bool
{
    return m_serial_point_normals == m_serial;
}

auto Geometry::compute_point_normals(const Property_map_descriptor& descriptor) -> bool
{
    ZoneScoped;

    if (has_point_normals())
    {
        return true;
    }

    auto*       point_normals   = point_attributes().find_or_create<vec3>(descriptor);
    const auto* polygon_normals = polygon_attributes().find<vec3>(c_polygon_normals);
    if (polygon_normals == nullptr)
    {
        const bool polygon_normals_ok = compute_polygon_normals();
        if (!polygon_normals_ok)
        {
            return false;
        }
        polygon_normals = polygon_attributes().find<vec3>(c_polygon_normals);
    }

    point_normals->clear();

    for_each_point([&](auto& i)
    {
        vec3 normal_sum(0.0f, 0.0f, 0.0f);
        i.point.for_each_corner(*this, [&](auto& j)
        {
            if (polygon_normals->has(j.corner.polygon_id))
            {
                normal_sum += polygon_normals->get(j.corner.polygon_id);
            }
            // TODO else
        });
        point_normals->put(i.point_id, normalize(normal_sum));
    });

    m_serial_point_normals = m_serial;
    return true;
}

void Geometry::transform(const mat4& m)
{
    ZoneScoped;

    const mat4 it = glm::transpose(glm::inverse(m));

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
            const vec4  t0_sign = corner_tangents->get(corner_id);
            const vec3  t0      = vec3(t0_sign);
            const float sign    = t0_sign.w;
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

void Mesh_info::trace(erhe::log::Category& log) const
{
    log_geometry.trace("{} vertex corners vertices\n", vertex_count_corners);
    log_geometry.trace("{} centroid vertices\n",       vertex_count_centroids);
    log_geometry.trace("{} fill triangles indices\n",  index_count_fill_triangles);
    log_geometry.trace("{} edge lines indices\n",      index_count_edge_lines);
    log_geometry.trace("{} corner points indices\n",   index_count_corner_points);
    log_geometry.trace("{} centroid points indices\n", index_count_centroid_points);
}

void Geometry::generate_texture_coordinates_spherical()
{
    ZoneScoped;

    log_geometry.info("{} for {}\n", __func__, name);

    compute_polygon_normals();
    compute_point_normals(c_point_normals);
    const auto* polygon_normals      = polygon_attributes().find          <vec3>(c_polygon_normals     );
    const auto* corner_normals       = corner_attributes ().find          <vec3>(c_corner_normals      );
    const auto* point_normals        = point_attributes  ().find          <vec3>(c_point_normals       );
    const auto* point_normals_smooth = point_attributes  ().find          <vec3>(c_point_normals_smooth);
          auto* corner_texcoords     = corner_attributes ().find_or_create<vec2>(c_corner_texcoords    );
    const auto* point_locations      = point_attributes  ().find          <vec3>(c_point_locations     );

    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        Polygon& polygon = polygons[polygon_id];
        for (Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
             end = polygon.first_polygon_corner_id + polygon.corner_count;
             polygon_corner_id < end;
             ++polygon_corner_id)
        {
            const Corner_id corner_id = polygon_corners[polygon_corner_id];
            const Corner&   corner    = corners[corner_id];
            const Point_id  point_id  = corner.point_id;

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

auto Geometry::has_polygon_texture_coordinates() const -> bool
{
    return m_serial_polygon_texture_coordinates == m_serial;
}

auto Geometry::generate_polygon_texture_coordinates(const bool overwrite_existing_texture_coordinates) -> bool
{
    ZoneScoped;

    if (has_polygon_texture_coordinates())
    {
        return true;
    }

    log_geometry.info("{} for {}\n", __func__, name);

    compute_polygon_normals();
    compute_polygon_centroids();
    const auto* polygon_normals   = polygon_attributes().find          <vec3>(c_polygon_normals  );
    const auto* polygon_centroids = polygon_attributes().find          <vec3>(c_polygon_centroids);
    const auto* point_locations   = point_attributes  ().find          <vec3>(c_point_locations  );
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
        polygons[polygon_id].compute_planar_texture_coordinates(
            polygon_id,
            *this,
            *corner_texcoords,
            *polygon_centroids,
            *polygon_normals,
            *point_locations,
            overwrite_existing_texture_coordinates
        );
    }

    m_serial_polygon_texture_coordinates = m_serial;
    return true;
}

void Geometry::sanity_check() const
{
    size_t error_count = 0;

    for_each_point_const([&](auto& i)
    {
        i.point.for_each_corner_const(*this, [&](auto& j)
        {
            if (j.corner.point_id != i.point_id)
            {
                log_geometry.error("Sanity check failure: Point {} uses corner {} but corner {} point is {}\n",
                          i.point_id, j.corner_id, j.corner_id, j.corner.point_id);
                ++error_count;
            }
            if (j.corner.polygon_id >= m_next_polygon_id)
            {
                log_geometry.error(
                    "Sanity check failure: Point {} uses corner {} which points to invalid polygon {}\n",
                    i.point_id, j.corner_id, j.corner.polygon_id
                );
                ++error_count;
            }
        });
    });

    for_each_polygon_const([&](auto& i)
    {
        i.polygon.for_each_corner_const(*this, [&](auto& j)
        {
            if (j.corner.polygon_id != i.polygon_id)
            {
                log_geometry.error(
                    "Sanity check failure: Polygon {} uses corner {} but corner {} polygon is {}\n",
                    i.polygon_id, j.corner_id, j.corner_id, j.corner.polygon_id
                );
                ++error_count;
            }
            if (j.corner.point_id >= m_next_point_id)
            {
                log_geometry.error(
                    "Sanity check failure: Polygon {} uses corner {} which points to invalid point {}\n",
                    i.polygon_id, j.corner_id, j.corner_id, j.corner.point_id
                );
                ++error_count;
            }
            else
            {
                bool corner_found = false;
                const Point& point = points[j.corner.point_id];
                point.for_each_corner_const(*this, [&](auto& k)
                {
                    if (k.corner_id == j.corner_id)
                    {
                        corner_found = true;
                        return k.break_iteration();
                    }
                });
                if (!corner_found)
                {
                    log_geometry.error(
                        "Sanity check failure: Polygon {} uses corner {} which uses point {} which does not point back to the corner\n",
                        i.polygon_id, j.corner_id, j.corner.point_id
                    );
                    ++error_count;
                }
            }
        });
    });

    for_each_corner_const([&](auto& i)
    {
        bool corner_point_found  {false};
        bool corner_polygon_found{false};
        if (i.corner.point_id >= m_next_point_id)
        {
            log_geometry.error("Sanity check failure: Corner {} points to invalid point {}\n",
                      i.corner_id, i.corner.point_id);
            ++error_count;
        }
        else
        {
            bool corner_found = false;
            //log_weld.trace("Corner {} point {} polygon {}\n", corner_id, corner.point_id, corner.polygon_id);
            erhe::log::Indenter scope_indent;
            const Point& point = points[i.corner.point_id];
            point.for_each_corner_const(*this, [&](auto& j)
            {
                if (j.corner_id == i.corner_id)
                {
                    corner_point_found = true;
                    return j.break_iteration();
                }
            });
            if (!corner_point_found)
            {
                log_geometry.error(
                    "Sanity check failure: Corner {} not found referenced by any point\n",
                    i.corner_id
                );
                ++error_count;
            }
        }
        if (i.corner.polygon_id >= m_next_polygon_id)
        {
            log_geometry.error(
                "Sanity check failure: Corner {} points to invalid polygon {}\n",
                i.corner_id, i.corner.polygon_id
            );
            ++error_count;
            return;
        }
        else
        {
            const Polygon& polygon = polygons[i.corner.polygon_id];
            polygon.for_each_corner_const(*this, [&](auto& j)
            {
                //log_weld.trace("Polygon corner {}\n", polygon_corners[polygon_corner_id]);
                if (j.corner_id == i.corner_id)
                {
                    corner_polygon_found = true;
                    return j.break_iteration();
                }
            });
            if (!corner_polygon_found)
            {
                log_geometry.error(
                    "Sanity check failure: Corner {} not found referenced by any polygon\n",
                    i.corner_id
                );
                ++error_count;
            }
        }
        if (corner_point_found != corner_polygon_found)
        {
            log_geometry.error(
                "Corner {} found in point mismatch found in polygon\n",
                i.corner_id
            );
            ++error_count;
        }
    });
    if (error_count > 0)
    {
         log_geometry.error(
             "Sanity check failure: Detected {} errors\n",
             error_count
         );
    }
}

auto Geometry::volume() -> float
{
    if (!compute_polygon_centroids())
    {
        return 0.0f;
    }

    const auto* point_locations   = point_attributes  ().find<vec3>(c_point_locations);
    const auto* polygon_centroids = polygon_attributes().find<vec3>(c_polygon_centroids);

    float sum{0.0f};
    for_each_polygon([&](auto& i)
    {
        if (i.polygon.corner_count < 3)
        {
            return;
        }
        const vec3 p0 = polygon_centroids->get(i.polygon_id);
        i.polygon.for_each_corner_neighborhood(*this, [&](auto& j)
        {
            const vec3 p1 = point_locations->get(j.corner.point_id);
            const vec3 p2 = point_locations->get(j.next_corner.point_id);
            const mat3 m{p0, p1, p2};

            //float a = dot(cross(p0, p1), p2);
            const float b = glm::determinant(m);
            sum += std::fabs(b);
        });
    });

    return sum / 6.0f;
}

} // namespace erhe::geometry

