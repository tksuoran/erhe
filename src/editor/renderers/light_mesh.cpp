#include "renderers/light_mesh.hpp"
#include "renderers/programs.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/regular_polygon.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <glm/glm.hpp>

namespace sample
{

using namespace erhe::toolkit;
using namespace erhe::primitive;
using namespace erhe::scene;
using namespace glm;
using namespace std;

Light_mesh::Light_mesh()
    : Component("Light_mesh")
{
}

void Light_mesh::connect(shared_ptr<Programs> programs)
{
    m_programs = programs;

    initialization_depends_on(programs);
}

void Light_mesh::initialize_component()
{
    Primitive_builder::Format_info format_info;

    format_info.want_fill_triangles       = true;
    format_info.want_edge_lines           = true;
    format_info.want_position             = true;
    format_info.vertex_attribute_mappings = &m_programs->attribute_mappings;

    // Full screen quad
    {
        // -1 .. 1
        auto quad_geometry = erhe::geometry::shapes::make_quad(2.0f);
        quad_geometry.build_edges();
        Primitive_builder::Buffer_info buffer_info;
        m_quad_mesh = make_primitive(quad_geometry, format_info, buffer_info);
    }

    // Spot light cone
    {
        m_light_cone_sides = 11;

        auto cone_geometry = erhe::geometry::shapes::make_conical_frustum(
            0.0,                // min x
            1.0,                // max x
            0.0,                // bottom radius
            1.0,                // top radius
            false,              // use bottom
            true,               // use top (end)
            m_light_cone_sides, // slice count
            0                   // stack division
        );

        cone_geometry.transform(mat4_rotate_xz_cw);
        cone_geometry.build_edges();
        Primitive_builder::Buffer_info buffer_info;
        m_cone_mesh = make_primitive(cone_geometry, format_info, buffer_info);
    }
}

auto Light_mesh::get_light_transform(Light& light) -> glm::mat4
{
    switch (light.type)
    {
        case Light::Type::directional:
        {
            return mat4(1.0f);
        }

        case Light::Type::point:
        {
            return mat4(1.0f);
        }

        case Light::Type::spot:
        {
            //           Side:                     Bottom:              .
            //             .                    ______________          .
            //            /|\                  /       |    / \         .
            //           / | \                /   apothem  /   \        .
            //          /  +--+ alpha        /         | radius \       .
            //         /   |   \            /          | /       \      .
            //        /    |    \          /           |/         \     .
            //       /     |     \         \                      /     .
            //      /      |      \         \                    /      .
            //     /     length    \         \                  /       .
            //    /        |        \         \                /        .
            //   /         |         \         \______________/         .
            //  +----------+----------+                                 .

            float alpha   = light.outer_spot_angle;
            float length  = light.range;
            float apothem = length * std::tan(alpha * 0.5f);
            float radius  = apothem / std::cos(glm::pi<float>() / static_cast<float>(m_light_cone_sides));

            return create_scale(radius, radius, length);
        }

        default:
        {
            return mat4(1.0f);
        }
    }
}

auto Light_mesh::point_in_light(vec3 point, Light& light) -> bool
{
    if (light.type != Light::Type::spot)
    {
        return true;
    }

    float spot_angle         = light.outer_spot_angle * 0.5f;
    float outer_angle        = spot_angle / std::cos(glm::pi<float>() / static_cast<float>(m_light_cone_sides));
    float spot_cutoff        = std::cos(outer_angle);
    float range              = light.range;
    mat4  light_from_world   = light.node()->node_from_world();
    vec3  view_in_light      = vec3(light_from_world * vec4(point, 1.0f));
    float distance           = -view_in_light.z;
    view_in_light            = normalize(view_in_light);
    float cos_angle          = dot(view_in_light, vec3(0.0f, 0.0f, -1.0f));
    bool  outside_cone_angle = (cos_angle < spot_cutoff);
    bool  outside_cone_range = (distance < 0.0f) || (distance > range);
    if (outside_cone_angle || outside_cone_range)
    {
        return false;
    }
    else
    {
        return true;
    }
}

auto Light_mesh::get_light_mesh(Light& light) -> Primitive_geometry*
{
    switch (light.type)
    {
        case Light::Type::directional:
        {
            return &m_quad_mesh;
        }

        case Light::Type::point:
        {
            return nullptr;
        }

        case Light::Type::spot:
        {
            return &m_cone_mesh;
        }

        default:
        {
            return nullptr;
        }
    }
}

}
