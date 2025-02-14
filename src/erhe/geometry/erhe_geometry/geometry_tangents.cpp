// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "mikktspace/mikktspace.hpp"

auto compute_mesh_tangents(GEO::Mesh& mesh, const bool make_facets_flat) -> bool
{
    // compute_polygon_normals
    // compute_polygon_centroids
    // compute_point_normals c_point_normals_smooth
    // generate_polygon_texture_coordinates

    class Mesh_context
    {
    public:
        Mesh_context(GEO::Mesh& mesh)
            : mesh      {mesh}
            , attributes{mesh}
        {
        }

        GEO::Mesh&      mesh;
        Mesh_attributes attributes;
        int             triangle_count     {0};
        bool            override_existing  {false};
        int             tangent_error_count{0};

        // Polygons are triangulated.
        class Triangle
        {
        public:
            GEO::index_t facet;
            GEO::index_t triangle_index;
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

        [[nodiscard]] auto get_facet(const int iFace) const -> GEO::index_t
        {
            assert(iFace < triangles.size());
            return get_triangle(iFace).facet;
        }

        [[nodiscard]] auto get_corner_triangulated(const int iFace, const int iVert) const -> GEO::index_t
        {
            assert(iVert > 0); // This only works for triangulated polygons, N > 3
            const auto&        triangle      = get_triangle(iFace);
            const GEO::index_t facet         = get_facet(iFace);
            const GEO::index_t corner_count  = mesh.facets.nb_corners(facet);
            assert(corner_count > 0);
            const uint32_t     corner_offset = (iVert - 1 + triangle.triangle_index) % corner_count;
            const GEO::index_t corner        = mesh.facets.corner(facet, corner_offset);
            return corner;
        }

        [[nodiscard]] auto get_corner_direct(const int iFace, const int iVert) const -> GEO::index_t
        {
            assert(iVert < 3); // This only works for triangles
            const GEO::index_t facet  = get_facet(iFace);
            const GEO::index_t corner = mesh.facets.corner(facet, iVert);
            return corner;
        }

        [[nodiscard]] auto get_corner(const int iFace, const int iVert) const -> GEO::index_t
        {
            const GEO::index_t facet  = get_facet(iFace);
            const GEO::index_t corner_count  = mesh.facets.nb_corners(facet);
            return (corner_count == 3)
                ? get_corner_direct(iFace, iVert)
                : get_corner_triangulated(iFace, iVert);
        }

        [[nodiscard]] auto get_vertex(const int iFace, const int iVert) const -> GEO::index_t
        {
            const GEO::index_t corner = get_corner(iFace, iVert);
            const GEO::index_t vertex = mesh.facet_corners.vertex(corner);
            return vertex;
        }

        [[nodiscard]] auto get_position(const int iFace, const int iVert) const -> GEO::vec3f
        {
            const GEO::index_t facet = get_facet(iFace);
            if (iVert == 0) {
                const GEO::vec3f position = attributes.facet_centroid.get(facet);
                return position;
            }
            const GEO::index_t vertex   = get_vertex(iFace, iVert);
            const GEO::vec3    position = mesh.vertices.point(vertex);
            return GEO::vec3f{position};
        }

        [[nodiscard]] auto get_normal(const int iFace, const int iVert) const -> GEO::vec3f
        {
            const GEO::index_t facet = get_facet(iFace);
            const std::optional<GEO::vec3f> facet_normal = attributes.facet_normal.try_get(facet);
            if (facet_normal.has_value()) {
                geo_assert(GEO::length2(facet_normal.value()) > 0.9f);
                return facet_normal.value();
            }

            const GEO::index_t corner = get_corner(iFace, iVert);
            const std::optional<GEO::vec3f> corner_normal = attributes.corner_normal.try_get(corner);
            if (corner_normal.has_value()) {
                geo_assert(GEO::length2(corner_normal.value()) > 0.9f);
                return corner_normal.value();
            }

            const GEO::index_t vertex = get_vertex(iFace, iVert);
            const std::optional<GEO::vec3f> vertex_normal = attributes.corner_normal.try_get(vertex);
            if (vertex_normal.has_value()) {
                geo_assert(GEO::length2(vertex_normal.value()) > 0.9f);
                return vertex_normal.value();
            }

            return GEO::vec3f{0.0f, 1.0f, 0.0f};
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

        [[nodiscard]] auto get_texcoord(const int iFace, const int iVert) const -> GEO::vec2f
        {
            if (iVert == 0) {
                // Calculate and return average texture coordinate from all polygon vertices
                GEO::vec2f texcoord_sum{0.0f, 0.0f};
                const GEO::index_t facet = get_facet(iFace);
                const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
                for (GEO::index_t corner : mesh.facets.corners(facet)) {
                    const std::optional<GEO::vec2f> corner_texcoord_0 = attributes.corner_texcoord_0.try_get(corner);
                    if (corner_texcoord_0.has_value()) {
                        texcoord_sum += corner_texcoord_0.value();
                    } else {
                        const GEO::index_t vertex = mesh.facet_corners.vertex(corner);
                        const std::optional<GEO::vec2f> vertex_texcoord_0 = attributes.vertex_texcoord_0.try_get(vertex);
                        if (vertex_texcoord_0.has_value()) {
                            texcoord_sum += vertex_texcoord_0.value();
                        } else {
                            return GEO::vec2f{0.0f, 0.0f}; // If we get here, facet is likely degenerate
                        }
                    }
                }
                const GEO::vec2f average_texcoord = texcoord_sum / static_cast<float>(corner_count);
                return average_texcoord;
            }

            const GEO::index_t corner = get_corner(iFace, iVert);
            const std::optional<GEO::vec2f> corner_texcoord_0 = attributes.corner_texcoord_0.try_get(corner);
            if (corner_texcoord_0.has_value()) {
                return corner_texcoord_0.value();
            }

            const GEO::index_t vertex = get_vertex(iFace, iVert);
            const std::optional<GEO::vec2f> vertex_texcoord_0 = attributes.vertex_texcoord_0.try_get(vertex);
            if (vertex_texcoord_0.has_value()) {
                return vertex_texcoord_0.value();
            }

            // degenerate polygon;
            return GEO::vec2f{0.0f, 0.0f};
        }

        void set_tangent(
            const int        iFace,
            const int        iVert,
            const GEO::vec3f tangent,
            const float      sign
        )
        {
            const GEO::index_t facet        = get_facet(iFace);
            const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
            if ((corner_count > 3) && (iVert == 0)) {
                return; // The center iVert is virtual, does not exist in Mesh
            }
            const GEO::index_t corner   = get_corner(iFace, iVert);
            const GEO::vec4f   tangent4 = GEO::vec4f{tangent, sign};
            attributes.facet_tangent .set(facet,  tangent4);
            attributes.corner_tangent.set(corner, tangent4);
        }

        void set_bitangent(
            const int        iFace,
            const int        iVert,
            const GEO::vec3f bitangent,
            const float      sign
        )
        {
            const GEO::index_t facet        = get_facet(iFace);
            const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
            if ((corner_count > 3) && (iVert == 0)) {
                return; // The center iVert is virtual, does not exist in Mesh
            }
            const GEO::index_t corner = get_corner(iFace, iVert);
            attributes.facet_bitangent .set(facet,  bitangent);
            attributes.corner_bitangent.set(corner, bitangent);
            static_cast<void>(sign);
        }

        std::vector<Triangle> triangles;
    };

    Mesh_context mesh_context{mesh};

    // MikkTSpace can only handle triangles or quads.
    // We triangulate all non-triangles by adding a virtual polygon centroid
    // and presenting N virtual triangles to MikkTSpace.
    int               virtual_face_index = 0;
    const std::size_t triangle_count     = count_mesh_facet_triangles(mesh);
    mesh_context.triangle_count = static_cast<int>(triangle_count);
    mesh_context.triangles.resize(triangle_count);
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        if (corner_count < 3) {
            continue;
        }
        for (GEO::index_t i = 0; i < corner_count - 2; ++i) {
            mesh_context.triangles[virtual_face_index++] = {facet, i};
        }
    }

    SMikkTSpaceInterface mikktspace{
        .m_getNumFaces = [](const SMikkTSpaceContext* pContext) {
            const auto* context   = static_cast<Mesh_context*>(pContext->m_pUserData);
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
            const Mesh_context* context = static_cast<Mesh_context*>(pContext->m_pUserData);
            const GEO::vec3f p = context->get_position(iFace, iVert);
            fvPosOut[0] = p[0];
            fvPosOut[1] = p[1];
            fvPosOut[2] = p[2];
        },

        .m_getNormal = [](
            const SMikkTSpaceContext* pContext,
            float                     fvNormOut[],
            int32_t                   iFace,
            int32_t                   iVert
        )
        {
            const Mesh_context* context = static_cast<Mesh_context*>(pContext->m_pUserData);
            const GEO::vec3f n = context->get_normal(iFace, iVert);
            fvNormOut[0] = n[0];
            fvNormOut[1] = n[1];
            fvNormOut[2] = n[2];
        },

        .m_getTexCoord = [](
            const SMikkTSpaceContext* pContext,
            float                     fvTexcOut[],
            int32_t                   iFace,
            int32_t                   iVert
        )
        {
            const Mesh_context* context = static_cast<Mesh_context*>(pContext->m_pUserData);
            const GEO::vec2f uv = context->get_texcoord(iFace, iVert);
            fvTexcOut[0] = uv[0];
            fvTexcOut[1] = uv[1];
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
            Mesh_context*      context      = static_cast<Mesh_context*>(pContext->m_pUserData);
            const GEO::index_t facet        = context->get_facet(iFace);
            const GEO::index_t corner_count = context->mesh.facets.nb_corners(facet);
            if ((corner_count > 3) && (iVert == 0)) {
                return;
            }

            const GEO::index_t corner = context->get_corner  (iFace, iVert); static_cast<void>(corner);
            const GEO::vec3f   P      = context->get_position(iFace, iVert); static_cast<void>(P);
            const GEO::vec3f   N      = context->get_normal(iFace, iVert);
            const GEO::vec3f   N_a    = context->get_normal(iFace, 0); static_cast<void>(N_a);
            const GEO::vec3f   N_b    = context->get_normal(iFace, 1); static_cast<void>(N_b);
            const GEO::vec3f   N_c    = context->get_normal(iFace, 2); static_cast<void>(N_c);
            const GEO::vec3f   T0     = GEO::vec3f{fvTangent  [0], fvTangent  [1], fvTangent  [2]};
            const GEO::vec3f   B0     = GEO::vec3f{fvBiTangent[0], fvBiTangent[1], fvBiTangent[2]};
            const float N_dot_T0 = GEO::dot(N, T0);
            const float N_dot_B0 = GEO::dot(N, B0);
            if (
                (std::abs(N_dot_T0) > 0.01f) ||
                (std::abs(N_dot_B0) > 0.01f)
            ) {
                ++context->tangent_error_count;
            }
            const GEO::vec3f T   = GEO::normalize(T0 - N_dot_T0 * N);
            const float      t_w = (GEO::dot(GEO::cross(N, T0), B0) < 0.0f) ? -1.0f : 1.0f;
            const GEO::vec3f B   = GEO::normalize(B0 - N_dot_B0 * N);
            const float      b_w = (GEO::dot(GEO::cross(B0, N), T0) < 0.0f) ? -1.0f : 1.0f;
            context->set_tangent  (iFace, iVert, T, t_w);
            context->set_bitangent(iFace, iVert, B, b_w);
        }
    };

    SMikkTSpaceContext context {
        .m_pInterface = &mikktspace,
        .m_pUserData  = &mesh_context
    };

    {
        const auto res = genTangSpaceDefault(&context);
        if (res == 0) {
            //// log_tangent_gen->trace("genTangSpaceDefault() returned 0");
            return false;
        }
    }

    // Post processing: Pick one tangent for polygon
    if (make_facets_flat) {
        for (GEO::index_t facet : mesh.facets) {
            const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
            if (corner_count < 3) {
                continue;
            }

            std::vector<std::optional<GEO::vec4f>> tangents;
            std::vector<std::optional<GEO::vec3f>> bitangents;

            std::optional<GEO::index_t> selected_tangent_corner_index;
            std::optional<GEO::index_t> selected_bitangent_corner_index;
            std::optional<GEO::index_t> selected_fallback_corner_index;
            std::optional<GEO::vec4f>   tangent_sum;
            std::optional<GEO::vec3f>   bitangent_sum;
            for (GEO::index_t local_facet_corner = 0; local_facet_corner < corner_count; ++local_facet_corner) {
                const GEO::index_t corner = mesh.facets.corner(facet, local_facet_corner);
                std::optional<GEO::vec4f> tangent;
                std::optional<GEO::vec3f> bitangent;
                std::optional<GEO::vec4f> corner_tangent = mesh_context.attributes.corner_tangent.try_get(corner);
                if (corner_tangent.has_value()) {
                    tangent = corner_tangent.value();
                    if (tangent_sum.has_value()) {
                        tangent_sum = tangent_sum.value() + tangent.value();
                    } else {
                        tangent_sum = tangent.value();
                    }
                }
                std::optional<GEO::vec3f> corner_bitangent = mesh_context.attributes.corner_bitangent.try_get(corner);
                if (corner_bitangent.has_value()) {
                    bitangent = corner_bitangent.value();
                    if (bitangent_sum.has_value()) {
                        bitangent_sum = bitangent_sum.value() + bitangent.value();
                    } else {
                        bitangent_sum = bitangent.value();
                    }
                }

                if (tangent.has_value()) {
                    const GEO::vec3f tangent3{tangent.value().x, tangent.value().y, tangent.value().z};
                    for (const auto& other : tangents) {
                        if (other.has_value()) {
                            const GEO::vec3f other3{other.value().x, other.value().y, other.value().z};
                            const float dot = GEO::dot(tangent3, other3);
                            if (dot > 0.99f) {
                                selected_tangent_corner_index = local_facet_corner;
                            }
                        }
                    }
                }
                if (bitangent.has_value()) {
                    for (const auto& other : bitangents) {
                        if (other.has_value()) {
                            const float dot = GEO::dot(bitangent.value(), other.value());
                            if (dot > 0.99f) {
                                selected_bitangent_corner_index = local_facet_corner;
                            }
                        }
                    }
                }
                if (tangent.has_value() && bitangent.has_value()) {
                    selected_fallback_corner_index = local_facet_corner;
                }
                tangents.  push_back(tangent);
                bitangents.push_back(bitangent);
            }

            std::optional<GEO::vec4f> tangent;
            std::optional<GEO::vec3f> bitangent;
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

            GEO::vec4f T = tangent.  has_value() ? tangent.  value() : GEO::vec4f{1.0f, 0.0f, 0.0f, 1.0f};
            GEO::vec3f B = bitangent.has_value() ? bitangent.value() : GEO::vec3f{0.0f, 0.0f, 1.0f};

            // Second pass - put tangent to all corners
            mesh_context.attributes.facet_tangent  .set(facet, T);
            mesh_context.attributes.facet_bitangent.set(facet, B);

            for (GEO::index_t corner : mesh.facets.corners(facet)) {
                mesh_context.attributes.corner_tangent  .set(corner, T);
                mesh_context.attributes.corner_bitangent.set(corner, B);
            }
        }
    }

    //// if (mesh_context.tangent_error_count != 0) {
    ////     log_tangent_gen->warn("tangent errors (T or B collinear with N) count = {}", mesh_context.tangent_error_count);
    //// }

    return true;
}
