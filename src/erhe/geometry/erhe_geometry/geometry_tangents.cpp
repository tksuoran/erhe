// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_profile/profile.hpp"
#include "mikktspace/mikktspace.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry {

using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;

auto Geometry::has_polygon_tangents() const -> bool
{
    return m_serial_polygon_tangents == m_serial;
}

auto Geometry::has_polygon_bitangents() const -> bool
{
    return m_serial_polygon_bitangents == m_serial;
}

auto Geometry::has_corner_normals() const -> bool
{
    return m_serial_corner_normals == m_serial;
}

auto Geometry::has_corner_tangents() const -> bool
{
    return m_serial_corner_tangents == m_serial;
}

auto Geometry::has_corner_bitangents() const -> bool
{
    return m_serial_corner_bitangents == m_serial;
}

auto Geometry::compute_tangents(
    const bool corner_tangents,
    const bool corner_bitangents,
    const bool polygon_tangents,
    const bool polygon_bitangents,
    const bool make_polygons_flat,
    const bool override_existing
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (
        (!polygon_tangents   || has_polygon_tangents  ()) &&
        (!polygon_bitangents || has_polygon_bitangents()) &&
        (!corner_tangents    || has_corner_tangents   ()) &&
        (!corner_bitangents  || has_corner_bitangents ())
    ) {
        return true;
    }

    log_tangent_gen->trace("{} for {}", __func__, name);

    if (!compute_polygon_normals()) {
        return false;
    }
    if (!compute_polygon_centroids()) {
        return false;
    }
    if (!compute_point_normals(c_point_normals_smooth)) {
        return false;
    }
    //if (!generate_polygon_texture_coordinates(false)) {
    //    return false;
    //}

    class Geometry_context
    {
    public:
        Geometry* geometry           {nullptr};
        int       triangle_count     {0};
        bool      override_existing  {false};
        int       tangent_error_count{0};

        Property_map<Polygon_id, vec3>* polygon_normals     {nullptr};
        Property_map<Polygon_id, vec3>* polygon_centroids   {nullptr};
        Property_map<Polygon_id, vec4>* polygon_tangents    {nullptr};
        Property_map<Polygon_id, vec4>* polygon_bitangents  {nullptr};
        Property_map<Corner_id, vec3>*  corner_normals      {nullptr};
        Property_map<Corner_id, vec4>*  corner_tangents     {nullptr};
        Property_map<Corner_id, vec4>*  corner_bitangents   {nullptr};
        Property_map<Corner_id, vec2>*  corner_texcoords    {nullptr};
        Property_map<Point_id, vec3>*   point_locations     {nullptr};
        Property_map<Point_id, vec3>*   point_normals       {nullptr};
        Property_map<Point_id, vec3>*   point_normals_smooth{nullptr};
        Property_map<Point_id, vec2>*   point_texcoords     {nullptr};

        // Polygons are triangulated.
        class Triangle
        {
        public:
            Polygon_id polygon_id;
            uint32_t   triangle_index;
        };

        [[nodiscard]] auto get_triangle(const int iFace) -> Triangle&
        {
            assert(iFace < triangles.size());
            return triangles[iFace];
        }

        [[nodiscard]] auto get_triangle(const int iFace) const -> const Triangle&
        {
            assert(iFace < triangles.size());
            return triangles[iFace];
        }

        [[nodiscard]] auto get_polygon_id(const int iFace) const -> Polygon_id
        {
            assert(iFace < triangles.size());
            return get_triangle(iFace).polygon_id;
        }

        [[nodiscard]] auto get_polygon(const int iFace) -> Polygon&
        {
            const Polygon_id polygon_id = get_polygon_id(iFace);
            assert(polygon_id < geometry->polygons.size());
            return geometry->polygons[polygon_id];
        }

        [[nodiscard]] auto get_polygon(const int iFace) const -> const Polygon&
        {
            const Polygon_id polygon_id = get_polygon_id(iFace);
            assert(polygon_id < geometry->polygons.size());
            return geometry->polygons[polygon_id];
        }

        [[nodiscard]] auto get_corner_id_triangulated(const int iFace, const int iVert) const -> Point_id
        {
            assert(iVert > 0); // This only works for triangulated polygons, N > 3
            const auto& triangle = get_triangle(iFace);
            const auto& polygon  = get_polygon(iFace);
            assert(polygon.corner_count > 0);
            const uint32_t          corner_offset     = (iVert - 1 + triangle.triangle_index) % polygon.corner_count;
            const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + corner_offset;
            assert(polygon_corner_id < geometry->polygon_corners.size());
            const Corner_id         corner_id         = geometry->polygon_corners[polygon_corner_id];
            return corner_id;
        }

        [[nodiscard]] auto get_corner_id_direct(const int iFace, const int iVert) const -> Point_id
        {
            assert(iVert < 3); // This only works for triangles
            const auto&             polygon           = get_polygon(iFace);
            const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + iVert;
            assert(polygon_corner_id < geometry->polygon_corners.size());
            const Corner_id         corner_id         = geometry->polygon_corners[polygon_corner_id];
            return corner_id;
        }

        [[nodiscard]] auto get_corner_id(const int iFace, const int iVert) const -> Corner_id
        {
            const auto& polygon = get_polygon(iFace);
            return (polygon.corner_count == 3)
                ? get_corner_id_direct(iFace, iVert)
                : get_corner_id_triangulated(iFace, iVert);
        }

        [[nodiscard]] auto get_point_id(const int iFace, const int iVert) const -> Point_id
        {
            const Corner_id corner_id = get_corner_id(iFace, iVert);
            const Corner&   corner    = geometry->corners[corner_id];
            return corner.point_id;
        }

        [[nodiscard]] auto get_position(const int iFace, const int iVert) const -> vec3
        {
            const Polygon_id polygon_id = get_polygon_id(iFace);
            if (iVert == 0)
            {
                const vec3 position = polygon_centroids->get(polygon_id);
                return position;
            }
            const Point_id point_id = get_point_id(iFace, iVert);
            const vec3     position = point_locations->get(point_id);
            return position;
        }

        [[nodiscard]] auto get_normal(const int iFace, const int iVert) const -> vec3
        {
            static_cast<void>(iVert);
            const Polygon_id polygon_id = get_polygon_id(iFace);
            vec3 normal{0.0f, 1.0f, 0.0f};
            bool has_polygon_normal = polygon_normals->maybe_get(polygon_id, normal);
            if (!has_polygon_normal) {
                const Corner_id corner_id = get_corner_id(iFace, iVert);
                bool has_corner_normal = corner_normals->maybe_get(corner_id, normal);
                if (!has_corner_normal) {
                    const Point_id point_id = get_point_id(iFace, iVert);
                    bool has_point_normal = point_normals->maybe_get(point_id, normal);
                    static_cast<void>(has_point_normal);
                }
            }

            return normal;
#if 0
            if (iVert == 0) {
                const vec3 normal = polygon_normals->get(polygon_id);
                return normal;
            }
            const Corner_id corner_id = get_corner_id(iFace, iVert);
            const Point_id  point_id  = get_point_id (iFace, iVert);
            if ((corner_normals != nullptr) && corner_normals->has(corner_id)) {
                const vec3 normal = corner_normals->get(corner_id);
                return normal;
            } else if ((point_normals != nullptr) && point_normals->has(point_id)) {
                const vec3 normal = point_normals->get(point_id);
                return normal;
            } else if ((point_normals_smooth != nullptr) && point_normals_smooth->has(point_id)) {
                const vec3 normal = point_normals_smooth->get(point_id);
                return normal;
            } else if ((polygon_normals != nullptr) && polygon_normals->has(polygon_id)) {
                const vec3 normal = polygon_normals->get(polygon_id);
                return normal;
            }
            ERHE_FATAL("No normal source");
            // unreachable return vec3{0.0f, 1.0f, 0.0f};
#endif
        }

        [[nodiscard]] auto get_texcoord(const int iFace, const int iVert) -> vec2
        {
            if (iVert == 0) {
                // Calculate and return average texture coordinate from all polygon vertices
                const Polygon& polygon = get_polygon(iFace);
                vec2 texcoord{0.0f, 0.0f};
                for (
                    Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id;
                    polygon_corner_id < polygon.first_polygon_corner_id + polygon.corner_count;
                    ++polygon_corner_id
                ) {
                    assert(polygon_corner_id < geometry->polygon_corners.size());
                    const Corner_id corner_id = geometry->polygon_corners[polygon_corner_id];
                    assert(corner_id < geometry->corners.size());
                    const Corner&   corner    = geometry->corners[corner_id];
                    const Point_id  point_id  = corner.point_id;
                    assert(point_id < geometry->points.size());
                    if ((corner_texcoords != nullptr) && corner_texcoords->has(corner_id)) {
                        texcoord += corner_texcoords->get(corner_id);
                    } else if ((point_texcoords != nullptr) && point_texcoords->has(point_id)) {
                        texcoord += point_texcoords->get(point_id);
                    } else {
                        // If we get here, polygon is likely degenerate
                        return vec2{0.0f, 0.0f};
                    }
                }
                const vec2 average_texcoord = texcoord / static_cast<float>(polygon.corner_count);
                return average_texcoord;
            }
            const Corner_id corner_id = get_corner_id(iFace, iVert);
            const Point_id  point_id  = get_point_id (iFace, iVert);
            if ((corner_texcoords != nullptr) && corner_texcoords->has(corner_id)) {
                const vec2 texcoord = corner_texcoords->get(corner_id);
                return texcoord;
            } else if ((point_texcoords != nullptr) && point_texcoords->has(point_id)) {
                const vec2 texcoord = point_texcoords->get(point_id);
                return texcoord;
            }
            // degenerate polygon;
            return vec2{0.0f, 0.0f};
        }

        void set_tangent(
            const int   iFace,
            const int   iVert,
            const vec3  tangent,
            const float sign
        )
        {
            const Polygon& polygon = get_polygon(iFace);
            if ((polygon.corner_count > 3) && (iVert == 0)) {
                return;
            }
            const Polygon_id polygon_id = get_polygon_id(iFace);
            const Corner_id  corner_id  = get_corner_id(iFace, iVert);

            if (polygon_tangents && (override_existing || !polygon_tangents->has(polygon_id))) {
                SPDLOG_LOGGER_TRACE(log_tangent_gen, "put polygon_id {}, tangent = {}, sign = {}", polygon_id, tangent, sign);
                polygon_tangents->put(polygon_id, vec4{tangent, sign});
            }
            if (corner_tangents && (override_existing || !corner_tangents->has(corner_id))) {
                SPDLOG_LOGGER_TRACE(log_tangent_gen, "put polygon_id {}, corner_id = {} tangent = {}, sign = {}", polygon_id, corner_id, tangent, sign);
                corner_tangents->put(corner_id, vec4{tangent, sign});
            }
        }

        void set_bitangent(
            const int   iFace,
            const int   iVert,
            const vec3  bitangent,
            const float sign
        )
        {
            const Polygon& polygon = get_polygon(iFace);
            if ((polygon.corner_count > 3) && (iVert == 0)) {
                return;
            }
            const Polygon_id polygon_id = get_polygon_id(iFace);
            const Corner_id  corner_id  = get_corner_id(iFace, iVert);

            if (polygon_bitangents && (override_existing || !polygon_bitangents->has(polygon_id))) {
                SPDLOG_LOGGER_TRACE(log_tangent_gen, "put polygon_id {}, bitangent = {}, sign = {}", polygon_id, bitangent, sign);
                polygon_bitangents->put(polygon_id, vec4{bitangent, sign});
            }
            if (corner_bitangents && (override_existing || !corner_bitangents->has(corner_id))) {
                SPDLOG_LOGGER_TRACE(log_tangent_gen, "put polygon_id {}, corner_id = {} bitangent = {}, sign = {}", polygon_id, corner_id, bitangent, sign);
                corner_bitangents->put(corner_id, vec4{bitangent, sign});
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

    if (g.point_locations == nullptr) {
        log_tangent_gen->warn("{} geometry = {} - No point locations found. Skipping tangent generation.", __func__, name);
        return false;
    }

    // MikkTSpace can only handle triangles or quads.
    // We triangulate all non-triangles by adding a virtual polygon centroid
    // and presenting N virtual triangles to MikkTSpace.
    int               face_index     = 0;
    const std::size_t triangle_count = count_polygon_triangles();
    g.triangle_count = static_cast<int>(triangle_count);
    g.triangles.resize(triangle_count);
    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id) {
        assert(polygon_id < polygons.size());
        const Polygon& polygon = polygons[polygon_id];
        if (polygon.corner_count < 3) {
            continue;
        }
        for (uint32_t i = 0; i < polygon.corner_count - 2; ++i) {
            g.triangles[face_index++] = {polygon_id, i};
        }
    }

    SMikkTSpaceInterface mikktspace{
        .m_getNumFaces = [](const SMikkTSpaceContext* pContext) {
            const auto* context   = static_cast<Geometry_context*>(pContext->m_pUserData);
            const int   num_faces = context->triangle_count;
            return num_faces;
        },

        .m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int32_t) {
            const int num_vertices_of_face = 3;
            return num_vertices_of_face;
        },

        .m_getPosition = [](
            const SMikkTSpaceContext* pContext,
            float                     fvPosOut[],
            int32_t                   iFace,
            int32_t                   iVert
        )
        {
            const auto* context    = static_cast<Geometry_context*>(pContext->m_pUserData);
            vec3        g_location = context->get_position(iFace, iVert);
            fvPosOut[0] = g_location[0];
            fvPosOut[1] = g_location[1];
            fvPosOut[2] = g_location[2];
        },

        .m_getNormal = [](
            const SMikkTSpaceContext* pContext,
            float                     fvNormOut[],
            int32_t                   iFace,
            int32_t                   iVert
        )
        {
            const auto* context  = static_cast<Geometry_context*>(pContext->m_pUserData);
            vec3        g_normal = context->get_normal(iFace, iVert);
            fvNormOut[0] = g_normal[0];
            fvNormOut[1] = g_normal[1];
            fvNormOut[2] = g_normal[2];
        },

        .m_getTexCoord = [](
            const SMikkTSpaceContext* pContext,
            float                     fvTexcOut[],
            int32_t                   iFace,
            int32_t                   iVert
        )
        {
            auto* context    = static_cast<Geometry_context*>(pContext->m_pUserData);
            vec2  g_texcoord = context->get_texcoord(iFace, iVert);
            fvTexcOut[0] = g_texcoord[0];
            fvTexcOut[1] = g_texcoord[1];
        },

        .m_setTSpaceBasic = nullptr,

        .m_setTSpace = [](
            const SMikkTSpaceContext* pContext,
            const float               fvTangent[],
            const float               fvBiTangent[],
            const float               fMagS,
            const float               fMagT,
            const tbool               bIsOrientationPreserving,
            const int                 iFace,
            const int                 iVert
        )
        {
            static_cast<void>(fMagS);
            static_cast<void>(fMagT);
            static_cast<void>(bIsOrientationPreserving);
            auto*          context = static_cast<Geometry_context*>(pContext->m_pUserData);

            const Polygon& polygon = context->get_polygon(iFace);
            if ((polygon.corner_count > 3) && (iVert == 0)) {
                return;
            }

            const auto  polygon_id = context->get_polygon_id(iFace);        static_cast<void>(polygon_id);
            const auto  corner_id  = context->get_corner_id (iFace, iVert); static_cast<void>(corner_id);
            const auto  P          = context->get_position  (iFace, iVert); static_cast<void>(P);
            const auto  N        = context->get_normal(iFace, iVert);
            const auto  N_a      = context->get_normal(iFace, 0); static_cast<void>(N_a);
            const auto  N_b      = context->get_normal(iFace, 1); static_cast<void>(N_b);
            const auto  N_c      = context->get_normal(iFace, 2); static_cast<void>(N_c);
            const vec3  T0       = vec3{fvTangent  [0], fvTangent  [1], fvTangent  [2]};
            const vec3  B0       = vec3{fvBiTangent[0], fvBiTangent[1], fvBiTangent[2]};
            const float N_dot_T0 = glm::dot(N, T0);
            const float N_dot_B0 = glm::dot(N, B0);
            if (
                (std::abs(N_dot_T0) > 0.01f) ||
                (std::abs(N_dot_B0) > 0.01f)
            ) {
                ++context->tangent_error_count;
            }
            //ERHE_VERIFY(std::abs(N_dot_B0) < 0.01f);
            const vec3  T        = glm::normalize(T0 - N_dot_T0 * N);
            const float t_w      = (glm::dot(glm::cross(N, T0), B0) < 0.0f) ? -1.0f : 1.0f;
            const vec3  B        = glm::normalize(B0 - N_dot_B0 * N);
            const float b_w      = (glm::dot(glm::cross(B0, N), T0) < 0.0f) ? -1.0f : 1.0f;
            //// const float N_dot_T  = glm::dot(N, T);
            //// const float N_dot_B  = glm::dot(N, B);
            //ERHE_VERIFY(std::abs(N_dot_T) < 0.01f);
            //ERHE_VERIFY(std::abs(N_dot_B) < 0.01f);
            context->set_tangent  (iFace, iVert, T, t_w);
            context->set_bitangent(iFace, iVert, B, b_w);
        }
    };

    SMikkTSpaceContext context {
        .m_pInterface = &mikktspace,
        .m_pUserData  = &g
    };

    {
        ERHE_PROFILE_SCOPE("genTangSpaceDefault");

        const auto res = genTangSpaceDefault(&context);
        if (res == 0) {
            log_tangent_gen->trace("genTangSpaceDefault() returned 0");
            return false;
        }
    }

    // Post processing: Pick one tangent for polygon
    if (make_polygons_flat) {
        ERHE_PROFILE_SCOPE("make polygons flat");

        for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id) {
            Polygon& polygon = polygons[polygon_id];
            if (polygon.corner_count < 3) {
                continue;
            }

            std::vector<std::optional<vec4>> tangents;
            std::vector<std::optional<vec4>> bitangents;

            std::optional<uint32_t> selected_tangent_corner_index;
            std::optional<uint32_t> selected_bitangent_corner_index;
            std::optional<uint32_t> selected_fallback_corner_index;
            std::optional<vec4> tangent_sum;
            std::optional<vec4> bitangent_sum;
            for (uint32_t i = 0; i < polygon.corner_count; ++i) {
                const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                const Corner_id         corner_id         = polygon_corners[polygon_corner_id];
                std::optional<vec4> tangent;
                std::optional<vec4> bitangent;
                if (corner_tangents && g.corner_tangents->has(corner_id)) {
                    tangent = g.corner_tangents->get(corner_id);
                    if (tangent_sum.has_value()) {
                        tangent_sum = tangent_sum.value() + tangent.value();
                    } else {
                        tangent_sum = tangent.value();
                    }
                }
                if (corner_bitangents && g.corner_bitangents->has(corner_id)) {
                    bitangent = g.corner_bitangents->get(corner_id);
                    if (bitangent_sum.has_value()) {
                        bitangent_sum = bitangent_sum.value() + bitangent.value();
                    } else {
                        bitangent_sum = bitangent.value();
                    }
                }
                //SPDLOG_LOGGER_TRACE(log_tangent_gen, "polygon {} corner {} has tangent   {} value {}", polygon_id, i, tangent.  has_value(), tangent.  has_value() ? tangent.  value() : vec4{});
                //SPDLOG_LOGGER_TRACE(log_tangent_gen, "polygon {} corner {} has bitangent {} value {}", polygon_id, i, bitangent.has_value(), bitangent.has_value() ? bitangent.value() : vec4{});
                if ((override_existing || !selected_tangent_corner_index.has_value()) && tangent.has_value()) {
                    for (const auto& other : tangents) {
                        if (other.has_value()) {
                            const float dot = glm::dot(vec3{tangent.value()}, vec3{other.value()});
                            //SPDLOG_LOGGER_TRACE(log_tangent_gen, "tangent dot with other {} = {}", other.value(), dot);
                            if (dot > 0.99f) {
                                selected_tangent_corner_index = i;
                            }
                        }
                    }
                }
                if ((override_existing || !selected_bitangent_corner_index.has_value()) && bitangent.has_value()) {
                    for (const auto& other : bitangents) {
                        if (other.has_value()) {
                            const float dot = glm::dot(vec3{bitangent.value()}, vec3{other.value()});
                            SPDLOG_LOGGER_TRACE(log_tangent_gen, "bitangent dot with other {} = {}", other.value(), dot);
                            if (dot > 0.99f) {
                                selected_bitangent_corner_index = i;
                            }
                        }
                    }
                }
                if (tangent.has_value() && bitangent.has_value()) {
                    selected_fallback_corner_index = i;
                }
                tangents.  push_back(tangent);
                bitangents.push_back(bitangent);
            }

            std::optional<vec4> tangent;
            std::optional<vec4> bitangent;
            if (
                selected_tangent_corner_index.has_value() &&
                selected_bitangent_corner_index.has_value() &&
                selected_tangent_corner_index.value() == selected_bitangent_corner_index.value()
            ) {
                tangent   = tangents.  at(selected_tangent_corner_index.value());
                bitangent = bitangents.at(selected_tangent_corner_index.value());
            } else if (
                selected_tangent_corner_index.has_value() &&
                bitangents.at(selected_tangent_corner_index.value()).has_value()
            ) {
                tangent   = tangents.  at(selected_tangent_corner_index.value());
                bitangent = bitangents.at(selected_tangent_corner_index.value());
            } else if (
                selected_bitangent_corner_index.has_value() &&
                tangents.at(selected_bitangent_corner_index.value()).has_value()
            ) {
                tangent   = tangents.  at(selected_bitangent_corner_index.value());
                bitangent = bitangents.at(selected_bitangent_corner_index.value());
            } else if (selected_fallback_corner_index.has_value()) {
                tangent   = tangents.  at(selected_fallback_corner_index.value());
                bitangent = bitangents.at(selected_fallback_corner_index.value());
            }

            vec4 T = tangent.  has_value() ? tangent.  value() : vec4{1.0, 0.0, 0.0, 1.0};
            vec4 B = bitangent.has_value() ? bitangent.value() : vec4{0.0, 0.0, 1.0, 1.0};

            // Second pass - put tangent to all corners
            if (polygon_tangents) {
                g.polygon_tangents->put(polygon_id, T);
            }
            if (polygon_bitangents) {
                g.polygon_tangents->put(polygon_id, B);
            }
            if (corner_tangents) {
                for (uint32_t i = 0; i < polygon.corner_count; ++i) {
                    const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                    const Corner_id         corner_id         = polygon_corners[polygon_corner_id];
                    g.corner_tangents->put(corner_id, T);
                }
            }
            if (corner_bitangents) {
                for (uint32_t i = 0; i < polygon.corner_count; ++i) {
                    const Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id + i;
                    const Corner_id         corner_id         = polygon_corners[polygon_corner_id];
                    g.corner_bitangents->put(corner_id, B);
                }
            }
        }
    }
    if (polygon_tangents) {
        m_serial_polygon_tangents = m_serial;
    }
    if (polygon_bitangents) {
        m_serial_polygon_bitangents = m_serial;
    }
    if (corner_tangents) {
        m_serial_corner_tangents = m_serial;
    }
    if (corner_bitangents) {
        m_serial_corner_bitangents = m_serial;
    }

    if (g.tangent_error_count != 0) {
        log_tangent_gen->warn("{} geometry = {} - tangent errors (T or B collinear with N) count = {}.", __func__, name, g.tangent_error_count);
    }

    return true;
}

} // namespace erhe::geometry
