#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/log.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#if defined(ERHE_USE_GEOMETRIC_TOOLS)
#   include <Mathematics/PolyhedralMassProperties.h>
#endif

#include <cmath>
#include <stdexcept>
#include <sstream>

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

Geometry::~Geometry() noexcept
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

auto Geometry::count_polygon_triangles() const -> size_t
{
    ERHE_PROFILE_FUNCTION

    size_t triangle_count{0};
    for (
        Polygon_id polygon_id = 0;
        polygon_id < m_next_polygon_id;
        ++polygon_id
    )
    {
        if (polygons[polygon_id].corner_count < 2)
        {
            continue;
        }
        triangle_count += polygons[polygon_id].corner_count - 2;
    }

    return triangle_count;
}

auto Geometry::get_mesh_info() const -> Mesh_info
{
    const auto polygon_count  = get_polygon_count();
    const auto corner_count   = get_corner_count();
    const auto triangle_count = count_polygon_triangles();
    const auto edge_count     = get_edge_count();
    return Mesh_info{
        .polygon_count  = polygon_count,
        .corner_count   = corner_count,
        .triangle_count = count_polygon_triangles(),
        .edge_count     = edge_count,

        // Additional vertices needed for centroids
        // 3 indices per triangle after triangulation, 2 indices per edge, 1 per corner, 1 index per centroid
        .vertex_count_corners        = corner_count,
        .vertex_count_centroids      = polygon_count,
        .index_count_fill_triangles  = 3 * triangle_count,
        .index_count_edge_lines      = 2 * edge_count,
        .index_count_corner_points   = corner_count,
        .index_count_centroid_points = polygon_count
    };
}

void Geometry::reserve_points(const size_t point_count)
{
    ERHE_PROFILE_FUNCTION

    if (point_count > points.size())
    {
        points.reserve(point_count);
    }
}

void Geometry::reserve_polygons(const size_t polygon_count)
{
    ERHE_PROFILE_FUNCTION

    if (polygon_count > polygons.size())
    {
        polygons.reserve(polygon_count);
    }
}

auto Geometry::has_polygon_normals() const -> bool
{
    ERHE_PROFILE_FUNCTION

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
    ERHE_PROFILE_FUNCTION

    if (has_polygon_normals())
    {
        return true;
    }

    log_geometry->info("{} for {}", __func__, name);

    auto*       const polygon_normals = polygon_attributes().find_or_create<vec3>(c_polygon_normals);
    const auto* const point_locations = point_attributes()  .find          <vec3>(c_point_locations);

    if (point_locations == nullptr)
    {
        log_geometry->warn(
            "{} {}: Point locations are required, but not found.",
            __func__,
            name
        );
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
    ERHE_PROFILE_FUNCTION

    if (has_polygon_centroids())
    {
        return true;
    }

    log_geometry->info("{} for {}", __func__, name);

    auto*       const polygon_centroids = polygon_attributes().find_or_create<vec3>(c_polygon_centroids);
    const auto* const point_locations   = point_attributes()  .find          <vec3>(c_point_locations);

    if (point_locations == nullptr)
    {
        log_geometry->warn("{} {}: Point locations are required, but not found.", __func__, name);
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

void Geometry::build_edges(bool is_manifold)
{
    ERHE_PROFILE_FUNCTION

    if (has_edges())
    {
        return;
    }

    edges.clear();
    m_next_edge_id = 0;

    log_build_edges->info("{} build_edges() : {} polygons", name, m_next_polygon_id);

    //const erhe::log::Indenter scope_indent;
    size_t polygon_index{0};

    size_t polygon_edge_count = 0;
    // First pass - shared edges
    {
        ERHE_PROFILE_SCOPE("first pass");

        for_each_polygon([&](auto& i)
        {
            //const erhe::log::Indenter scope_indent;

            i.polygon.for_each_corner_neighborhood(*this, [&](auto& j)
            {
                const Point_id a = j.prev_corner.point_id;
                const Point_id b = j.corner.point_id;
                ++polygon_edge_count;
                if (a == b)
                {
                    log_build_edges->warn("Bad edge {} - {}", a, b);
                    return;
                }
                if (a < b) // This does not work for non-shared edges going wrong direction
                {
                    const Edge_id edge_id = make_edge(a, b);
                    const Point&  pa      = points[a];
                    make_edge_polygon(edge_id, i.polygon_id);
                    ERHE_VERIFY(pa.corner_count > 0);
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
    }

    // Second pass - non-shared edges wrong direction or non-manifold wrong direction
    if (!is_manifold || (get_edge_count() != polygon_edge_count / 2))
    {
        ERHE_PROFILE_SCOPE("second pass");

        for_each_polygon([&](auto& i)
        {
            //const erhe::log::Indenter scope_indent;

            i.polygon.for_each_corner_neighborhood(*this, [&](auto& j)
            {
                const Point_id a_ = j.prev_corner.point_id;
                const Point_id b_ = j.corner.point_id;
                if (a_ == b_)
                {
                    return;
                }

                auto edge = find_edge(a_, b_);
                if (!edge)
                {
                    // ERHE_VERIFY(b < a); This does not hold for non-manifold objects
                    {
                        const Point_id a = std::max(a_, b_);
                        const Point_id b = std::min(a_, b_);
                        const Edge_id edge_id = make_edge(b, a); // Swapped a, b because b < a
                        const Point&  pb      = points[b];
                        make_edge_polygon(edge_id, i.polygon_id);
                        ERHE_VERIFY(pb.corner_count > 0);
                        pb.for_each_corner_const(*this, [&](auto& k)
                        {
                             const Polygon_id polygon_id_in_point = k.corner.polygon_id;
                             const Polygon&   polygon_in_point    = polygons[polygon_id_in_point];
                             const Corner_id  prev_corner_id      = polygon_in_point.prev_corner(*this, k.corner_id);
                             const Corner&    prev_corner         = corners[prev_corner_id];
                             const Point_id   prev_point_id       = prev_corner.point_id;
                             if (prev_point_id == a)
                             {
                                 make_edge_polygon(edge_id, polygon_id_in_point);
                                 ++polygon_index;
                             }
                        });
                    }
                }
            });
        });
    }

    m_serial_edges = m_serial;
}

void Geometry::debug_trace() const
{
    ERHE_PROFILE_FUNCTION

    for_each_corner_const([](auto& i)
    {
        log_geometry->info("corner {:2} = point {:2} polygon {:2}", i.corner_id, i.corner.point_id, i.corner.polygon_id);
    });

    for_each_point_const([&](auto& i)
    {
        {
            std::stringstream ss;
            ss << fmt::format("point {:2} corners  = ", i.point_id);
            i.point.for_each_corner_const(*this, [&](auto& j)
            {
                if (j.corner_id > i.point.first_point_corner_id)
                {
                    ss << ", ";
                }
                ss << fmt::format("{:2}", j.corner_id);
            });
            log_geometry->info("{}", ss.str());
        }

        {
            std::stringstream ss;
            ss << fmt::format("point {:2} polygons = ", i.point_id);
            i.point.for_each_corner_const(*this, [&](auto& j)
            {
                if (j.corner_id > i.point.first_point_corner_id)
                {
                    ss << ", ";
                }
                ss << fmt::format("{:2}", j.corner.polygon_id);
            });
           log_geometry->info("{}", ss.str());
        }
    });

    for_each_polygon_const([&](auto& i)
    {
        {
            std::stringstream ss;
            ss << fmt::format("polygon {:2} corners = ", i.polygon_id);
            i.polygon.for_each_corner_const(*this, [&](auto& j)
            {
                if (j.polygon_corner_id > i.polygon.first_polygon_corner_id)
                {
                    ss << ", ";
                }
                ss << fmt::format("{:2}", j.corner_id);
            });
            log_geometry->info("{}", ss.str());
        }
        {
            std::stringstream ss;
            ss << fmt::format("polygon {:2} points  = ", i.polygon_id);
            i.polygon.for_each_corner_const(*this, [&](auto& j)
            {
                Point_id point_id = j.corner.point_id;
                if (j.polygon_corner_id > i.polygon.first_polygon_corner_id)
                {
                    ss << ", ";
                }
                ss << fmt::format("{:2}", point_id);
            });
            log_geometry->info("{}", ss.str());
        }
    });

    for_each_edge_const([&](auto& i)
    {
        std::stringstream ss;
        ss << fmt::format("edge {:2} = {:2} .. {:2} :", i.edge_id, i.edge.a, i.edge.b);
        i.edge.for_each_polygon_const(*this, [&](auto& j)
        {
            ss << fmt::format("{:2} ", j.polygon_id);
        });
        log_geometry->info("{}", ss.str());
    });
}

auto Geometry::compute_point_normal(const Point_id point_id) -> vec3
{
    ERHE_PROFILE_FUNCTION

    Expects(point_id < points.size());

    compute_polygon_normals();
    const auto* const polygon_normals = polygon_attributes().find<vec3>(c_polygon_normals);

    vec3 normal_sum{0.0f};

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
    ERHE_PROFILE_FUNCTION

    if (has_point_normals())
    {
        return true;
    }

    log_geometry->info("{} for {}", __func__, name);

    auto* const point_normals   = point_attributes().find_or_create<vec3>(descriptor);
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
        vec3 normal_sum{0.0f};
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

auto Geometry::transform(const mat4& m) -> Geometry&
{
    ERHE_PROFILE_FUNCTION

    if (m == mat4{1.0f})
    {
        return *this;
    }

    //const mat4 it = glm::transpose(glm::inverse(m));

    polygon_attributes().transform(m);
    point_attributes  ().transform(m);
    corner_attributes ().transform(m);
    edge_attributes   ().transform(m);

    const auto det = glm::determinant(m);
    if (det < 0.0f)
    {
        reverse_polygons();
    }

    return *this;
}

void Geometry::reverse_polygons()
{
    ERHE_PROFILE_FUNCTION

    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        polygons[polygon_id].reverse(*this);
    }
}

void Geometry::flip_reversed_polygons()
{
    ERHE_PROFILE_FUNCTION

    ++m_serial;

    compute_polygon_normals();
    compute_polygon_centroids();

    const auto* const polygon_normals   = polygon_attributes().find_or_create<vec3>(c_polygon_normals);
    const auto* const polygon_centroids = polygon_attributes().find_or_create<vec3>(c_polygon_centroids);

    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        const auto normal   = polygon_normals->get(polygon_id);
        const auto centroid = glm::normalize(polygon_centroids->get(polygon_id));
        if (glm::dot(normal, centroid) < 0.0f)
        {
            polygons[polygon_id].reverse(*this);
        }
    }
}

void Mesh_info::trace(const std::shared_ptr<spdlog::logger>& log) const
{
    log->trace("{} vertex corners vertices", vertex_count_corners);
    log->trace("{} centroid vertices",       vertex_count_centroids);
    log->trace("{} fill triangles indices",  index_count_fill_triangles);
    log->trace("{} edge lines indices",      index_count_edge_lines);
    log->trace("{} corner points indices",   index_count_corner_points);
    log->trace("{} centroid points indices", index_count_centroid_points);
}

void Geometry::generate_texture_coordinates_spherical()
{
    ERHE_PROFILE_FUNCTION

    log_geometry->info("{} for {}", __func__, name);

    compute_polygon_normals();
    compute_point_normals(c_point_normals);
    const auto* const polygon_normals      = polygon_attributes().find          <vec3>(c_polygon_normals     );
    const auto* const corner_normals       = corner_attributes ().find          <vec3>(c_corner_normals      );
    const auto* const point_normals        = point_attributes  ().find          <vec3>(c_point_normals       );
    const auto* const point_normals_smooth = point_attributes  ().find          <vec3>(c_point_normals_smooth);
          auto* const corner_texcoords     = corner_attributes ().find_or_create<vec2>(c_corner_texcoords    );
    const auto* const point_locations      = point_attributes  ().find          <vec3>(c_point_locations     );

    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        Polygon& polygon = polygons[polygon_id];
        for (
            Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
            end = polygon.first_polygon_corner_id + polygon.corner_count;
            polygon_corner_id < end;
            ++polygon_corner_id
        )
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
                ERHE_FATAL("No normal sources\n");
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
            corner_texcoords->put(corner_id, glm::vec2{u, v});
        }
    }
}

auto Geometry::has_polygon_texture_coordinates() const -> bool
{
    return m_serial_polygon_texture_coordinates == m_serial;
}

auto Geometry::generate_polygon_texture_coordinates(const bool overwrite_existing_texture_coordinates) -> bool
{
    ERHE_PROFILE_FUNCTION

    if (has_polygon_texture_coordinates())
    {
        return true;
    }

    log_geometry->info("{} for {}", __func__, name);

    compute_polygon_normals();
    compute_polygon_centroids();
    const auto* const polygon_normals   = polygon_attributes().find          <vec3>(c_polygon_normals  );
    const auto* const polygon_centroids = polygon_attributes().find          <vec3>(c_polygon_centroids);
    const auto* const point_locations   = point_attributes  ().find          <vec3>(c_point_locations  );
          auto* const corner_texcoords  = corner_attributes ().find_or_create<vec2>(c_corner_texcoords );

    if (point_locations == nullptr)
    {
        log_polygon_texcoords->warn(
            "{} geometry = {} - No point locations found. Skipping generation.",
            __func__,
            name
        );
        return false;
    }

    if (polygon_centroids == nullptr)
    {
        log_polygon_texcoords->warn(
            "{} geometry = {} - No polygon centroids found. Skipping generation.",
            __func__,
            name
        );
        return false;
    }

    if (polygon_normals == nullptr)
    {
        log_polygon_texcoords->warn(
            "{} geometry = {} - No polygon normals found. Skipping generation.",
            __func__,
            name
        );
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
                log_geometry->error(
                    "Sanity check failure: Point {} uses corner {} but corner {} point is {}",
                    i.point_id, j.corner_id, j.corner_id, j.corner.point_id
                );
                ++error_count;
            }
            if (j.corner.polygon_id >= m_next_polygon_id)
            {
                log_geometry->error(
                    "Sanity check failure: Point {} uses corner {} which points to invalid polygon {}",
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
                log_geometry->error(
                    "Sanity check failure: Polygon {} uses corner {} but corner {} polygon is {}",
                    i.polygon_id, j.corner_id, j.corner_id, j.corner.polygon_id
                );
                ++error_count;
            }
            if (j.corner.point_id >= m_next_point_id)
            {
                log_geometry->error(
                    "Sanity check failure: Polygon {} uses corner {} which points to invalid point {}",
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
                    log_geometry->error(
                        "Sanity check failure: Polygon {} uses corner {} which uses point {} which does not point back to the corner",
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
            log_geometry->error(
                "Sanity check failure: Corner {} points to invalid point {}",
                i.corner_id,
                i.corner.point_id
            );
            ++error_count;
        }
        else
        {
            //log_weld.trace("Corner {} point {} polygon {}\n", corner_id, corner.point_id, corner.polygon_id);
            //const erhe::log::Indenter scope_indent;

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
                log_geometry->error(
                    "Sanity check failure: Corner {} not found referenced by any point",
                    i.corner_id
                );
                ++error_count;
            }
        }
        if (i.corner.polygon_id >= m_next_polygon_id)
        {
            log_geometry->error(
                "Sanity check failure: Corner {} points to invalid polygon {}",
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
                log_geometry->error(
                    "Sanity check failure: Corner {} not found referenced by any polygon",
                    i.corner_id
                );
                ++error_count;
            }
        }
        if (corner_point_found != corner_polygon_found)
        {
            log_geometry->error(
                "Corner {} found in point mismatch found in polygon",
                i.corner_id
            );
            ++error_count;
        }
    });
    if (error_count > 0)
    {
        log_geometry->error(
            "Sanity check failure: Detected {} errors",
            error_count
         );
    }
}

auto Geometry::get_mass_properties() -> Mass_properties
{
    ERHE_PROFILE_FUNCTION

    if (!compute_polygon_centroids())
    {
        return Mass_properties{
            0.0f,
            glm::vec3{0.0f},
            glm::mat3{1.0f}
        };
    }

#if !defined(ERHE_USE_GEOMETRIC_TOOLS)
    const auto* const point_locations   = point_attributes  ().find<vec3>(c_point_locations);
    const auto* const polygon_centroids = polygon_attributes().find<vec3>(c_polygon_centroids);

    float sum{0.0f};
    for_each_polygon_const([&](auto& i)
    {
        if (i.polygon.corner_count < 3)
        {
            return;
        }
        const vec3 p0 = polygon_centroids->get(i.polygon_id);
        i.polygon.for_each_corner_neighborhood_const(*this, [&](auto& j)
        {
            const vec3 p1 = point_locations->get(j.corner.point_id);
            const vec3 p2 = point_locations->get(j.next_corner.point_id);
            const mat3 m{p0, p1, p2};

            //float a = dot(cross(p0, p1), p2);
            const float b = glm::determinant(m);
            sum += std::fabs(b);
        });
    });

    const float mass = sum / 6.0f;
    return Mass_properties{
        .volume            = mass,
        .center_of_gravity = glm::vec3{0.0f},
        .inertial          = glm::mat3{1.0f}
    };
#else
    // Copy vertex data in gt format
    std::vector<gte::Vector3<float>> vertex_data;
    vertex_data.reserve(point_count());
    for_each_point_const([&](auto& i)
    {
        glm::vec3 position = point_locations->get(i.point_id);
        vertex_data.push_back(
            gte::Vector3<float>{position.x, position.y, position.z}
        );
    });

    const int triangle_count = static_cast<int>(count_polygon_triangles());

    std::vector<int> triangles;
    triangles.reserve(triangle_count * 3);
    for_each_polygon([&](auto& i)
    {
        const Corner_id first_corner_id = i.polygon.first_polygon_corner_id;
        const Point_id  first_point_id  = corners[first_corner_id].point_id;
        i.polygon.for_each_corner_neighborhood(*this, [&](auto& j)
        {
            const Corner_id corner_id      = j.corner_id;
            const Corner_id next_corner_id = j.next_corner_id;
            const Point_id  point_id       = corners[corner_id].point_id;
            const Point_id  next_point_id  = corners[next_corner_id].point_id;
            if (first_point_id == point_id)
            {
                return;
            }
            if (first_point_id == next_point_id)
            {
                return;
            }
            triangles.push_back(first_point_id);
            triangles.push_back(point_id);
            triangles.push_back(next_point_id);
        });
    });

    ERHE_VERIFY(triangles.size() == triangle_count * 3);

    //auto vertex_data = reinterpret_cast<gte::Vector3<float> const*>(point_locations->values.data());
    float mass{0.0f};
    gte::Vector3<float> center_of_mass{0.0f, 0.0f, 0.0f};
    gte::Matrix3x3<float> inertia{};
    gte::ComputeMassProperties<float>(
        vertex_data.data(),
        triangle_count,
        triangles.data(),
        true, // inertia relative to center of mass
        mass,
        center_of_mass,
        inertia
    );

    return Mass_properties{
        std::abs(mass),
        glm::vec3{
            center_of_mass[0],
            center_of_mass[1],
            center_of_mass[2]
        },
        glm::mat3{
            inertia(0, 0), inertia(1, 0), inertia(2, 0),
            inertia(0, 1), inertia(1, 1), inertia(2, 1),
            inertia(0, 2), inertia(1, 2), inertia(2, 2)
        }
    };
#endif
}

} // namespace erhe::geometry

