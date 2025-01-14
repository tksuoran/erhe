#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>

namespace erhe::geometry {

auto Geometry::extract_geogram_mesh() const -> GEO::Mesh*
{
    ERHE_PROFILE_FUNCTION();

    GEO::Mesh* mesh = new GEO::Mesh(3, false); // false = use double precision

    Property_map<Point_id, glm::vec3>*  erhe_point_locations      = point_attributes().find<glm::vec3 >(c_point_locations     );
    Property_map<Point_id, glm::vec3>*  erhe_point_normals        = point_attributes().find<glm::vec3 >(c_point_normals       );
    Property_map<Point_id, glm::vec3>*  erhe_point_normals_smooth = point_attributes().find<glm::vec3 >(c_point_normals_smooth);
    Property_map<Point_id, glm::vec4>*  erhe_point_tangents       = point_attributes().find<glm::vec4 >(c_point_tangents      );
    Property_map<Point_id, glm::vec4>*  erhe_point_bitangents     = point_attributes().find<glm::vec4 >(c_point_bitangents    );
    Property_map<Point_id, glm::vec2>*  erhe_point_texcoords      = point_attributes().find<glm::vec2 >(c_point_texcoords     );
    Property_map<Point_id, glm::vec4>*  erhe_point_colors         = point_attributes().find<glm::vec4 >(c_point_colors        );
    Property_map<Point_id, glm::vec2>*  erhe_point_aniso_control  = point_attributes().find<glm::vec2 >(c_point_aniso_control );
    Property_map<Point_id, glm::uvec4>* erhe_point_joint_indices  = point_attributes().find<glm::uvec4>(c_point_joint_indices );
    Property_map<Point_id, glm::vec4>*  erhe_point_joint_weights  = point_attributes().find<glm::vec4 >(c_point_joint_weights );
    GEO::MeshVertices&         mesh_vertices     = mesh->vertices;
    GEO::AttributesManager&    vertex_attributes = mesh_vertices.attributes();
    GEO::Attribute<GEO::vec3f> mesh_vertex_normal                 {vertex_attributes, "NORMAL"};
    GEO::Attribute<bool>       mesh_vertex_normal_present         {vertex_attributes, "NORMAL_present"};
    GEO::Attribute<GEO::vec3f> mesh_vertex_normal_smooth          {vertex_attributes, "NORMAL_SMOOTH"};
    GEO::Attribute<bool>       mesh_vertex_normal_smooth_present  {vertex_attributes, "NORMAL_SMOOTH_present"};
    GEO::Attribute<GEO::vec4f> mesh_vertex_tangent                {vertex_attributes, "TANGENT"};
    GEO::Attribute<bool>       mesh_vertex_tangent_present        {vertex_attributes, "TANGENT_present"};
    GEO::Attribute<GEO::vec4f> mesh_vertex_bitangent              {vertex_attributes, "BITANGENT"};
    GEO::Attribute<bool>       mesh_vertex_bitangent_present      {vertex_attributes, "BITANGENT_present"};
    GEO::Attribute<GEO::vec2f> mesh_vertex_texcoord_0             {vertex_attributes, "TEXCOORD_0"};
    GEO::Attribute<bool>       mesh_vertex_texcoord_0_present     {vertex_attributes, "TEXCOORD_0_present"};
    GEO::Attribute<GEO::vec4f> mesh_vertex_color_0                {vertex_attributes, "COLOR_0"};
    GEO::Attribute<bool>       mesh_vertex_color_0_present        {vertex_attributes, "COLOR_0_present"};
    GEO::Attribute<GEO::vec2f> mesh_vertex_aniso_control          {vertex_attributes, "ANISO_CONTROL"};
    GEO::Attribute<bool>       mesh_vertex_aniso_control_present  {vertex_attributes, "ANISO_CONTROL_present"};
    GEO::Attribute<GEO::vec4i> mesh_vertex_joint_indices_0        {vertex_attributes, "JOINT_INDICES_0"};
    GEO::Attribute<bool>       mesh_vertex_joint_indices_0_present{vertex_attributes, "JOINT_INDICES_0_present"};
    GEO::Attribute<GEO::vec4f> mesh_vertex_joint_weights_0        {vertex_attributes, "JOINT_WEIGHTS_0"};
    GEO::Attribute<bool>       mesh_vertex_joint_weights_0_present{vertex_attributes, "JOINT_WEIGHTS_0_present"};

    for_each_point_const(
        [&](Point_context_const& i) {
            GEO::index_t vertex_index = mesh_vertices.create_vertex();
            ERHE_VERIFY(vertex_index == i.point_id);

            glm::vec3 pos;
            ERHE_VERIFY(erhe_point_locations->maybe_get(i.point_id, pos));

            double* p_ptr = mesh_vertices.point_ptr(vertex_index);
            //float* p_ptr = mesh_vertices.single_precision_point_ptr(vertex_index);
            p_ptr[0] = pos.x;
            p_ptr[1] = pos.y;
            p_ptr[2] = pos.z;

            glm::vec3 n;
            if ((erhe_point_normals != nullptr) && (mesh_vertex_normal_present[vertex_index] = erhe_point_normals->maybe_get(i.point_id, n))) {
                mesh_vertex_normal[vertex_index] = GEO::vec3f{n.x, n.y, n.z};
            }

            glm::vec3 n_smooth;
            if ((erhe_point_normals_smooth != nullptr) && (mesh_vertex_normal_smooth_present[vertex_index] = erhe_point_normals_smooth->maybe_get(i.point_id, n_smooth))) {
                mesh_vertex_normal_smooth[vertex_index] = GEO::vec3f{n_smooth.x, n_smooth.y, n_smooth.z};
            }

            glm::vec4 t;
            if ((erhe_point_tangents != nullptr) && (mesh_vertex_tangent_present[vertex_index] = erhe_point_tangents->maybe_get(i.point_id, t))) {
                mesh_vertex_tangent[vertex_index] = GEO::vec4f{t.x, t.y, t.z, t.w};
            }

            glm::vec4 b;
            if ((erhe_point_bitangents != nullptr) && (mesh_vertex_bitangent_present[vertex_index] = erhe_point_bitangents->maybe_get(i.point_id, b))) {
                mesh_vertex_bitangent[vertex_index] = GEO::vec4f{b.x, b.y, b.z, b.w};
            }

            glm::vec2 tc0;
            if ((erhe_point_texcoords != nullptr) && (mesh_vertex_texcoord_0_present[vertex_index] = erhe_point_texcoords->maybe_get(i.point_id, tc0))) {
                mesh_vertex_texcoord_0[vertex_index] = GEO::vec2f{tc0.x, tc0.y};
            }

            glm::vec4 c0;
            if ((erhe_point_colors != nullptr) && (mesh_vertex_color_0_present[vertex_index] = erhe_point_colors->maybe_get(i.point_id, c0))) {
                mesh_vertex_color_0[vertex_index] = GEO::vec4f{c0.x, c0.y, c0.z, c0.w};
            }

            glm::vec2 a;
            if ((erhe_point_aniso_control != nullptr) && (mesh_vertex_aniso_control_present[vertex_index] = erhe_point_aniso_control->maybe_get(i.point_id, a))) {
                mesh_vertex_aniso_control[vertex_index] = GEO::vec2f{a.x, a.y};
            }

            glm::uvec4 joint_indices;
            if ((erhe_point_joint_indices != nullptr) && (mesh_vertex_joint_indices_0_present[vertex_index] = erhe_point_joint_indices->maybe_get(i.point_id, joint_indices))) {
                mesh_vertex_joint_indices_0[vertex_index] = GEO::vec4i{static_cast<GEO::Numeric::int32>(joint_indices.x), static_cast<GEO::Numeric::int32>(joint_indices.y)};
            }

            glm::vec4 joint_weights;
            if ((erhe_point_joint_weights != nullptr) && (mesh_vertex_joint_weights_0_present[vertex_index] = erhe_point_joint_weights->maybe_get(i.point_id, joint_weights))) {
                mesh_vertex_joint_weights_0[vertex_index] = GEO::vec4f{joint_weights.x, joint_weights.y, joint_weights.z, joint_weights.w};
            }
        }
    );

    Property_map<Polygon_id, glm::vec3>* erhe_polygon_normals       = polygon_attributes().find<glm::vec3 >(c_polygon_normals      );
    Property_map<Polygon_id, glm::vec4>* erhe_polygon_colors        = polygon_attributes().find<glm::vec4 >(c_polygon_colors       );
    Property_map<Polygon_id, glm::vec2>* erhe_polygon_aniso_control = polygon_attributes().find<glm::vec2 >(c_polygon_aniso_control);
    Property_map<Corner_id, glm::vec3>*  erhe_corner_normals        = corner_attributes ().find<glm::vec3 >(c_corner_normals       );
    Property_map<Corner_id, glm::vec4>*  erhe_corner_tangents       = corner_attributes ().find<glm::vec4 >(c_corner_tangents      );
    Property_map<Corner_id, glm::vec4>*  erhe_corner_bitangents     = corner_attributes ().find<glm::vec4 >(c_corner_bitangents    );
    Property_map<Corner_id, glm::vec2>*  erhe_corner_texcoords      = corner_attributes ().find<glm::vec2 >(c_corner_texcoords     );
    Property_map<Corner_id, glm::vec4>*  erhe_corner_colors         = corner_attributes ().find<glm::vec4 >(c_corner_colors        );
    Property_map<Corner_id, glm::vec2>*  erhe_corner_aniso_control  = corner_attributes ().find<glm::vec2 >(c_corner_aniso_control );
    GEO::MeshFacets&            mesh_facets        = mesh->facets;
    GEO::MeshFacetCornersStore& mesh_facet_corners = mesh->facet_corners;
    GEO::Attribute<GEO::vec3f> mesh_facet_normal                      {mesh_facets.attributes(),        "NORMAL"};
    GEO::Attribute<bool>       mesh_facet_normal_present              {mesh_facets.attributes(),        "NORMAL_present"};
    GEO::Attribute<GEO::vec4f> mesh_facet_color_0                     {mesh_facets.attributes(),        "COLOR_0"};
    GEO::Attribute<bool>       mesh_facet_color_0_present             {mesh_facets.attributes(),        "COLOR_0_present"};
    GEO::Attribute<GEO::vec2f> mesh_facet_aniso_control               {mesh_facets.attributes(),        "ANISO_CONTROL"};
    GEO::Attribute<bool>       mesh_facet_aniso_control_present       {mesh_facets.attributes(),        "ANISO_CONTROL_present"};
    GEO::Attribute<GEO::vec3f> mesh_facet_corner_normal               {mesh_facet_corners.attributes(), "NORMAL"};
    GEO::Attribute<bool>       mesh_facet_corner_normal_present       {mesh_facet_corners.attributes(), "NORMAL_present"};
    GEO::Attribute<GEO::vec4f> mesh_facet_corner_tangent              {mesh_facet_corners.attributes(), "TANGENT"};
    GEO::Attribute<bool>       mesh_facet_corner_tangent_present      {mesh_facet_corners.attributes(), "TANGENT_present"};
    GEO::Attribute<GEO::vec4f> mesh_facet_corner_bitangent            {mesh_facet_corners.attributes(), "BITANGENT"};
    GEO::Attribute<bool>       mesh_facet_corner_bitangent_present    {mesh_facet_corners.attributes(), "BITANGENT_present"};
    GEO::Attribute<GEO::vec2f> mesh_facet_corner_texcoord_0           {mesh_facet_corners.attributes(), "TEXCOORD_0"};
    GEO::Attribute<bool>       mesh_facet_corner_texcoord_0_present   {mesh_facet_corners.attributes(), "TEXCOORD_0_present"};
    GEO::Attribute<GEO::vec4f> mesh_facet_corner_color_0              {mesh_facet_corners.attributes(), "COLOR_0"};
    GEO::Attribute<bool>       mesh_facet_corner_color_0_present      {mesh_facet_corners.attributes(), "COLOR_0_present"};
    GEO::Attribute<GEO::vec2f> mesh_facet_corner_aniso_control        {mesh_facet_corners.attributes(), "ANISO_CONTROL"};
    GEO::Attribute<bool>       mesh_facet_corner_aniso_control_present{mesh_facet_corners.attributes(), "ANISO_CONTROL_present"};

    for_each_polygon_const(
        [&](Polygon_context_const& i) -> void
        {
            GEO::vector<GEO::index_t> vertices;
            i.polygon.for_each_corner_const(
                *this,
                [&](Polygon::Polygon_corner_context_const& j) -> void
                {
                    GEO::index_t vertex_id = static_cast<GEO::index_t>(j.corner.point_id);
                    vertices.push_back(vertex_id);
                }
            );
            GEO::index_t facet_index = mesh_facets.create_polygon(vertices);
            ERHE_VERIFY(facet_index == i.polygon_id);

            i.polygon.for_each_corner_const(
                *this,
                [&](Polygon::Polygon_corner_context_const& j) -> void
                {
                    GEO::index_t local_corner = static_cast<GEO::index_t>(j.polygon_corner_id - i.polygon.first_polygon_corner_id);
                    GEO::index_t facet_corner = mesh_facets.corner(facet_index, local_corner);

                    glm::vec3 n;
                    if ((erhe_corner_normals != nullptr) && (mesh_facet_corner_normal_present[facet_corner] = erhe_corner_normals->maybe_get(j.corner_id, n))) {
                        mesh_facet_corner_normal[facet_corner] = GEO::vec3f{n.x, n.y, n.z};
                    }

                    glm::vec4 t;
                    if ((erhe_corner_tangents != nullptr) && (mesh_facet_corner_tangent_present[facet_corner] = erhe_corner_tangents->maybe_get(j.corner_id, t))) {
                        mesh_facet_corner_tangent[facet_corner] = GEO::vec4f{t.x, t.y, t.z, t.w};
                    }

                    glm::vec4 b;
                    if ((erhe_corner_bitangents != nullptr) && (mesh_facet_corner_bitangent_present[facet_corner] = erhe_corner_bitangents->maybe_get(j.corner_id, b))) {
                        mesh_facet_corner_bitangent[facet_corner] = GEO::vec4f{b.x, b.y, b.z, b.w};
                    }

                    glm::vec2 tc0;
                    if ((erhe_corner_texcoords != nullptr) && (mesh_facet_corner_texcoord_0_present[facet_corner] = erhe_corner_texcoords->maybe_get(j.corner_id, tc0))) {
                        mesh_facet_corner_texcoord_0[facet_corner] = GEO::vec2f{tc0.x, tc0.y};
                    }

                    glm::vec4 c0;
                    if ((erhe_corner_colors != nullptr) && (mesh_facet_corner_color_0_present[facet_corner] = erhe_corner_colors->maybe_get(j.corner_id, c0))) {
                        mesh_facet_corner_color_0[facet_corner] = GEO::vec4f{c0.x, c0.y, c0.z, c0.w};
                    }

                    glm::vec2 a;
                    if ((erhe_corner_aniso_control != nullptr) && (mesh_facet_corner_aniso_control_present[facet_corner] = erhe_corner_aniso_control->maybe_get(j.corner_id, a))) {
                        mesh_facet_corner_aniso_control[facet_corner] = GEO::vec2f{a.x, a.y};
                    }
                }
            );

            glm::vec3 n;
            if ((erhe_polygon_normals != nullptr) && (mesh_facet_normal_present[facet_index] = erhe_polygon_normals->maybe_get(i.polygon_id, n))) {
                mesh_facet_normal[facet_index] = GEO::vec3f{n.x, n.y, n.z};
            }

            glm::vec4 c0;
            if ((erhe_polygon_colors != nullptr) && (mesh_facet_color_0_present[facet_index] = erhe_polygon_colors->maybe_get(i.polygon_id, c0))) {
                mesh_facet_color_0[facet_index] = GEO::vec4f{c0.x, c0.y, c0.z, c0.w};
            }

            glm::vec2 a;
            if ((erhe_polygon_aniso_control != nullptr) && (mesh_facet_aniso_control_present[facet_index] = erhe_polygon_aniso_control->maybe_get(i.polygon_id, a))) {
                mesh_facet_aniso_control[facet_index] = GEO::vec2f{a.x, a.y};
            }
        }
    );

    return mesh;
}

} // namespace erhe::geometry

