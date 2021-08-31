#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/mikktspace.h"
#include "erhe/geometry/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include "Tracy.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry
{


auto Geometry::has_polygon_tangents() const -> bool
{
    return m_serial_polygon_tangents == m_serial;
}

auto Geometry::has_polygon_bitangents() const -> bool
{
    return m_serial_polygon_bitangents == m_serial;
}

auto Geometry::has_corner_tangents() const -> bool
{
    return m_serial_corner_tangents == m_serial;
}

auto Geometry::has_corner_bitangents() const -> bool
{
    return m_serial_corner_bitangents == m_serial;
}

auto Geometry::compute_tangents(const bool corner_tangents,
                                const bool corner_bitangents,
                                const bool polygon_tangents,
                                const bool polygon_bitangents,
                                const bool make_polygons_flat,
                                const bool override_existing) -> bool
{
    using namespace glm;

    ZoneScoped;

    if ((!polygon_tangents   || has_polygon_tangents  ()) &&
        (!polygon_bitangents || has_polygon_bitangents()) &&
        (!corner_tangents    || has_corner_tangents   ()) &&
        (!corner_bitangents  || has_corner_bitangents ()))
    {
        return true;
    }

    log_geometry.info("{} for {}\n", __func__, name);

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

    class Geometry_context
    {
    public:
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
        class Triangle
        {
        public:
            Polygon_id polygon_id;
            uint32_t   triangle_index;
        };

        auto get_triangle(const int iFace) -> Triangle&
        {
            return triangles[iFace];
        }

        auto get_triangle(const int iFace) const -> const Triangle&
        {
            return triangles[iFace];
        }

        auto get_polygon_id(const int iFace) const -> Polygon_id
        {
            return get_triangle(iFace).polygon_id;
        }

        auto get_polygon(const int iFace) -> Polygon&
        {
            return geometry->polygons[get_polygon_id(iFace)];
        }

        auto get_polygon(const int iFace) const -> const Polygon&
        {
            return geometry->polygons[get_polygon_id(iFace)];
        }

        auto get_corner_id_triangulated(const int iFace, const int iVert) const -> Point_id
        {
            VERIFY(iVert > 0); // This only works for triangulated polygons, N > 3
            const auto& triangle = get_triangle(iFace);
            const auto& polygon  = get_polygon(iFace);
            const uint32_t          corner_offset     = (iVert - 1 + triangle.triangle_index) % polygon.corner_count;
            const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + corner_offset;
            const Corner_id         corner_id         = geometry->polygon_corners[polygon_corner_id];
            return corner_id;
        }

        auto get_corner_id_direct(const int iFace, const int iVert) const -> Point_id
        {
            VERIFY(iVert < 3); // This only works for triangles
            const auto&             polygon           = get_polygon(iFace);
            const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + iVert;
            const Corner_id         corner_id         = geometry->polygon_corners[polygon_corner_id];
            return corner_id;
        }

        auto get_corner_id(const int iFace, const int iVert) const -> Corner_id
        {
            const auto& polygon = get_polygon(iFace);
            return (polygon.corner_count == 3) ? get_corner_id_direct(iFace, iVert)
                                               : get_corner_id_triangulated(iFace, iVert);
        }

        auto get_point_id(const int iFace, const int iVert) const -> Point_id
        {
            const Corner_id corner_id = get_corner_id(iFace, iVert);
            const Corner&   corner    = geometry->corners[corner_id];
            return corner.point_id;
        }

        auto get_position(const int iFace, const int iVert) const -> glm::vec3
        {
            const Polygon_id polygon_id = get_polygon_id(iFace);
            if (iVert == 0)
            {
                const glm::vec3 position = polygon_centroids->get(polygon_id);
                return position;
            }
            const Point_id  point_id = get_point_id(iFace, iVert);
            const glm::vec3 position = point_locations->get(point_id);
            return position;
        }

        auto get_normal(const int iFace, const int iVert) const -> glm::vec3
        {
            const Polygon_id polygon_id = get_polygon_id(iFace);
            if (iVert == 0)
            {
                const glm::vec3 normal = polygon_normals->get(polygon_id);
                return normal;
            }
            const Corner_id corner_id = get_corner_id(iFace, iVert);
            const Point_id  point_id  = get_point_id (iFace, iVert);
            if ((corner_normals != nullptr) && corner_normals->has(corner_id))
            {
                const glm::vec3 normal = corner_normals->get(corner_id);
                return normal;
            }
            else if ((point_normals != nullptr) && point_normals->has(point_id))
            {
                const glm::vec3 normal = point_normals->get(point_id);
                return normal;
            }
            else if ((point_normals_smooth != nullptr) && point_normals_smooth->has(point_id))
            {
                const glm::vec3 normal = point_normals_smooth->get(point_id);
                return normal;
            }
            else if ((polygon_normals != nullptr) && polygon_normals->has(polygon_id))
            {
                const glm::vec3 normal = polygon_normals->get(polygon_id);
                return normal;
            }
            else
            {
                FATAL("No normal source\n");
            }
            return glm::vec3(0.0f, 1.0f, 0.0f);
        }

        auto get_texcoord(const int iFace, const int iVert) -> glm::vec2
        {
            if (iVert == 0)
            {
                // Calculate and return average texture coordinate from all polygon vertices
                const Polygon& polygon    = get_polygon(iFace);
                glm::vec2      texcoord{0.0f, 0.0f};
                for (Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id;
                     polygon_corner_id < polygon.first_polygon_corner_id + polygon.corner_count;
                     ++polygon_corner_id)
                {
                    const Corner_id corner_id = geometry->polygon_corners[polygon_corner_id];
                    const Corner&   corner    = geometry->corners[corner_id];
                    const Point_id  point_id  = corner.point_id;
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
                const glm::vec2 average_texcoord = texcoord / static_cast<float>(polygon.corner_count);
                return average_texcoord;
            }
            const Corner_id corner_id = get_corner_id(iFace, iVert);
            const Point_id  point_id  = get_point_id (iFace, iVert);
            if ((corner_texcoords != nullptr) && corner_texcoords->has(corner_id))
            {
                const glm::vec2 texcoord = corner_texcoords->get(corner_id);
                return texcoord;
            }
            else if ((point_texcoords != nullptr) && point_texcoords->has(point_id))
            {
                const glm::vec2 texcoord = point_texcoords->get(point_id);
                return texcoord;
            }
            else
            {
                FATAL("No texture coordinate\n");
            }
            return glm::vec2(0.0f, 0.0f);
        }

        void set_tangent(const int iFace, const int iVert, const glm::vec3 tangent, const float sign)
        {
            const Polygon& polygon = get_polygon(iFace);
            if ((polygon.corner_count > 3) && (iVert == 0))
            {
                return;
            }
            const Polygon_id polygon_id = get_polygon_id(iFace);
            const Corner_id  corner_id  = get_corner_id(iFace, iVert);

            if (polygon_tangents && (override_existing || !polygon_tangents->has(polygon_id)))
            {
                polygon_tangents->put(polygon_id, glm::vec4(tangent, sign));
            }
            if (corner_tangents && (override_existing || !corner_tangents->has(corner_id)))
            {
                corner_tangents->put(corner_id, glm::vec4(tangent, sign));
            }
        }

        void set_bitangent(const int iFace, const int iVert, const glm::vec3 bitangent, const float sign)
        {
            const Polygon&   polygon = get_polygon(iFace);
            if ((polygon.corner_count > 3) && (iVert == 0))
            {
                return;
            }
            const Polygon_id polygon_id = get_polygon_id(iFace);
            const Corner_id  corner_id  = get_corner_id(iFace, iVert);

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
    int          face_index     = 0;
    const size_t triangle_count = count_polygon_triangles();
    g.triangle_count = static_cast<int>(triangle_count);
    g.triangles.resize(triangle_count);
    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id)
    {
        const Polygon& polygon = polygons[polygon_id];
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
        const auto* context   = reinterpret_cast<Geometry_context*>(pContext->m_pUserData);
        const int   num_faces = context->triangle_count;
        return num_faces;
    };

    mikktspace.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int32_t)
    {
        const int num_vertices_of_face = 3;
        return num_vertices_of_face;
    };

    mikktspace.m_getPosition = [](const SMikkTSpaceContext* pContext,
                                  float                     fvPosOut[],
                                  int32_t                   iFace,
                                  int32_t                   iVert)
    {
        const auto* context    = reinterpret_cast<Geometry_context*>(pContext->m_pUserData);
        glm::vec3   g_location = context->get_position(iFace, iVert);
        fvPosOut[0] = g_location[0];
        fvPosOut[1] = g_location[1];
        fvPosOut[2] = g_location[2];
    };

    mikktspace.m_getNormal = [](const SMikkTSpaceContext* pContext,
                                float                     fvNormOut[],
                                int32_t                   iFace,
                                int32_t                   iVert)
    {
        const auto* context  = reinterpret_cast<Geometry_context*>(pContext->m_pUserData);
        glm::vec3   g_normal = context->get_normal(iFace, iVert);
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
        auto*           context = reinterpret_cast<Geometry_context*>(pContext->m_pUserData);
        const auto      N     = context->get_normal(iFace, iVert);
        const glm::vec3 T     = glm::vec3(fvTangent  [0], fvTangent  [1], fvTangent  [2]);
        const glm::vec3 B     = glm::vec3(fvBiTangent[0], fvBiTangent[1], fvBiTangent[2]);
        const vec3      t_xyz = glm::normalize(T - N * glm::dot(N, T));
        const float     t_w   = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
        const vec3      b_xyz = glm::normalize(B - N * glm::dot(N, B));
        const float     b_w   = (glm::dot(glm::cross(B, N), T) < 0.0f) ? -1.0f : 1.0f;
        context->set_tangent  (iFace, iVert, t_xyz, t_w);
        context->set_bitangent(iFace, iVert, b_xyz, b_w);
    };

    SMikkTSpaceContext context = {};
    context.m_pInterface = &mikktspace;
    context.m_pUserData  = &g;

    {
        ZoneScopedN("genTangSpaceDefault")
        const auto res = genTangSpaceDefault(&context);
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
            for (uint32_t i = 0; i < polygon.corner_count; ++i)
            {
                const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                const Corner_id         corner_id         = polygon_corners[polygon_corner_id];
                if ((override_existing || !T.has_value()) && corner_tangents && g.corner_tangents->has(corner_id))
                {
                    const auto tangent = g.corner_tangents->get(corner_id);
                    for (auto other : tangents)
                    {
                        const float dot = glm::dot(glm::vec3(tangent), glm::vec3(other));
                        if (dot > 0.99f)
                        {
                            T = tangent;
                        }
                    }
                    tangents.emplace_back(tangent);
                }
                if ((override_existing || !B.has_value()) && corner_bitangents && g.corner_bitangents->has(corner_id))
                {
                    const auto bitangent = g.corner_bitangents->get(corner_id);
                    for (auto other : bitangents)
                    {
                        const float dot = glm::dot(glm::vec3(bitangent), glm::vec3(other));
                        if (dot > 0.99f)
                        {
                            B = bitangent;
                        }
                    }
                    bitangents.emplace_back(bitangent);
                }
            }

            glm::vec4 tangent{1.0, 0.0, 0.0, 1.0};
            if (T.has_value())
            {
                tangent = T.value();
            }

            glm::vec4 bitangent{0.0, 0.0, 1.0, 1.0};
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
                    const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                    const Corner_id         corner_id         = polygon_corners[polygon_corner_id];
                    g.corner_tangents->put(corner_id, tangent);
                }
            }
            if (corner_bitangents)
            {
                for (uint32_t i = 0; i < polygon.corner_count; ++i)
                {
                    const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                    const Corner_id         corner_id         = polygon_corners[polygon_corner_id];
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

}
