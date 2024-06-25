#include "renderers/light_mesh.hpp"

#include "erhe_geometry/shapes/cone.hpp"
#include "erhe_geometry/shapes/regular_polygon.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_primitive/buffer_info.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_math/math_util.hpp"

#include <glm/glm.hpp>

namespace editor
{

using mat4 = glm::mat4;
using vec3 = glm::vec3;
using vec4 = glm::vec4;

Light_mesh::Light_mesh()
{
#if 0
    erhe::graphics::Buffer_transfer_queue queue;

    Gl_buffer_sink buffer_sink{queue, {}, {}};

    erhe::primitive::Build_info build_info;
    auto& format_info = build_info.format;
    auto& buffer_info = build_info.buffer;
    format_info.features.fill_triangles   = true;
    format_info.features.edge_lines       = true;
    format_info.features.position         = true;
    format_info.vertex_attribute_mappings = &m_program_interface->shader_resources->attribute_mappings;

    buffer_info.buffer_sink = &buffer_sink;

    // Full screen quad
    {
        // -1 .. 1
        auto quad_geometry = erhe::geometry::shapes::make_quad(2.0f);
        quad_geometry.build_edges();
        m_quad_mesh = make_primitive(quad_geometry, build_info, Normal_style::none);
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
        m_cone_mesh = make_primitive(cone_geometry, build_info);
    }
#endif
}

auto Light_mesh::get_light_transform(const erhe::scene::Light& light) -> glm::mat4
{
    switch (light.type) {
        //using enum erhe::scene::Light_type;
        case erhe::scene::Light_type::directional: {
            return mat4{1.0f};
        }

        case erhe::scene::Light_type::point: {
            return mat4{1.0f};
        }

        case erhe::scene::Light_type::spot: {
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

            const float alpha   = light.outer_spot_angle;
            const float length  = light.range;
            const float apothem = length * std::tan(alpha * 0.5f);
            const float radius  = apothem / std::cos(glm::pi<float>() / static_cast<float>(m_light_cone_sides));

            return erhe::math::create_scale(radius, radius, length);
        }

        default: {
            return mat4{1.0f};
        }
    }
}

auto Light_mesh::point_in_light(
    const glm::vec3           point,
    const erhe::scene::Light& light
) -> bool
{
    if (light.type != erhe::scene::Light::Type::spot) {
        return true;
    }

    const auto* node = light.get_node();
    if (node == nullptr) {
        return false;
    }

    const float spot_angle         = light.outer_spot_angle * 0.5f;
    const float outer_angle        = spot_angle / std::cos(glm::pi<float>() / static_cast<float>(m_light_cone_sides));
    const float spot_cutoff        = std::cos(outer_angle);
    const float range              = light.range;
    const mat4  light_from_world   = node->node_from_world();
    const vec3  view_in_light_     = vec3{light_from_world * vec4{point, 1.0f}};
    const float distance           = -view_in_light_.z;
    const vec3  view_in_light      = glm::normalize(view_in_light_);
    const float cos_angle          = glm::dot(view_in_light, vec3{0.0f, 0.0f, -1.0f});
    const bool  outside_cone_angle = (cos_angle < spot_cutoff);
    const bool  outside_cone_range = (distance < 0.0f) || (distance > range);
    if (outside_cone_angle || outside_cone_range) {
        return false;
    } else {
        return true;
    }
}

auto Light_mesh::get_light_mesh(
    const erhe::scene::Light& light
) -> erhe::primitive::Renderable_mesh*
{
    switch (light.type) {
        //using enum erhe::scene::Light_type;
        case erhe::scene::Light_type::directional: {
            return &m_quad_mesh;
        }

        case erhe::scene::Light_type::point: {
            return nullptr;
        }

        case erhe::scene::Light_type::spot: {
            return &m_cone_mesh;
        }

        default: {
            return nullptr;
        }
    }
}

}
