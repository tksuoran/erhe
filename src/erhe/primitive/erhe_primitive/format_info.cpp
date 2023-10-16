#include "erhe_primitive/format_info.hpp"

namespace erhe::primitive
{

auto prepare_vertex_format(
    const Attributes&      attributes,
    const Attribute_types& attribute_types
) -> erhe::graphics::Vertex_format
{
    using Vertex_attribute = erhe::graphics::Vertex_attribute;
    erhe::graphics::Vertex_format vertex_format;

    if (attributes.position) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage       = { Vertex_attribute::Usage_type::position },
                .shader_type = gl::Attribute_type::float_vec3,
                .data_type   = {
                    .type      = attribute_types.position,
                    .dimension = 3
                }
            }
        );
    }

    if (attributes.normal) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::normal
                },
                .shader_type   = gl::Attribute_type::float_vec3,
                .data_type = {
                    .type      = attribute_types.normal,
                    .dimension = 3
                }
            }
        );
    }

    if (attributes.normal_flat) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::normal,
                    .index     = 1
                },
                .shader_type   = gl::Attribute_type::float_vec3,
                .data_type = {
                    .type      = attribute_types.normal_flat,
                    .dimension = 3
                }
            }
        );
    }

    if (attributes.normal_smooth) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::normal,
                    .index     = 2
                },
                .shader_type   = gl::Attribute_type::float_vec3,
                .data_type = {
                    .type      = attribute_types.normal_smooth,
                    .dimension = 3
                }
            }
        );
    }

    if (attributes.tangent) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::tangent
                },
                .shader_type   = gl::Attribute_type::float_vec4,
                .data_type = {
                    .type      = attribute_types.tangent,
                    .dimension = 4
                }
            }
        );
    }

    if (attributes.bitangent) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::bitangent
                },
                .shader_type   = gl::Attribute_type::float_vec4,
                .data_type = {
                    .type      = attribute_types.bitangent,
                    .dimension = 4
                }
            }
        );
    }

    if (attributes.color) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::color
                },
                .shader_type   = gl::Attribute_type::float_vec4,
                .data_type = {
                    .type      = attribute_types.color,
                    .dimension = 4
                }
            }
        );
    }

    if (attributes.id) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::id
                },
                .shader_type   = gl::Attribute_type::float_vec3,
                .data_type = {
                    .type      = attribute_types.id_vec3,
                    .dimension = 3
                }
            }
        );

        //// TODO
        //// if (erhe::graphics::g_instance->info.use_integer_polygon_ids) {
        ////     vf->add_attribute(
        ////         erhe::graphics::Vertex_attribute{
        ////             .usage = {
        ////                 .type      = Vertex_attribute::Usage_type::id
        ////             },
        ////             .shader_type   = gl::Attribute_type::unsigned_int,
        ////             .data_type = {
        ////                 .type      = igl::VertexAttributeFormat::unsigned_int,
        ////                 .dimension = 1
        ////             }
        ////         }
        ////     );
        //// }
    }

    if (attributes.texcoord) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::tex_coord
                },
                .shader_type   = gl::Attribute_type::float_vec2,
                .data_type = {
                    .type      = attribute_types.texcoord,
                    .dimension = 2
                }
            }
        );
    }

    if (attributes.joint_indices) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::joint_indices
                },
                .shader_type   = gl::Attribute_type::unsigned_int_vec4,
                .data_type = {
                    .type      = attribute_types.joint_indices,
                    .dimension = 4
                }
            }
        );
    }

    if (attributes.joint_weights) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::joint_weights
                },
                .shader_type   = gl::Attribute_type::float_vec4,
                .data_type = {
                    .type      = attribute_types.joint_weights,
                    .dimension = 4
                }
            }
        );
    }
    return vertex_format;
}

}
