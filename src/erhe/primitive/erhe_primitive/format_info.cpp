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
                .shader_type = erhe::graphics::Glsl_type::float_vec3,
                .data_type   = erhe::dataformat::Format::format_32_vec3_float
            }
        );
    }

    if (attributes.normal) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage         = { Vertex_attribute::Usage_type::normal },
                .shader_type   = erhe::graphics::Glsl_type::float_vec3,
                .data_type     = attribute_types.normal,
                .default_value = glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}
            }
        );
    }

    if (attributes.normal_flat) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage         = { Vertex_attribute::Usage_type::normal, 1 },
                .shader_type   = erhe::graphics::Glsl_type::float_vec3,
                .data_type     = attribute_types.normal_flat,
                .default_value = glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}
            }
        );
    }

    if (attributes.normal_smooth) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage         = { Vertex_attribute::Usage_type::normal, 2 },
                .shader_type   = erhe::graphics::Glsl_type::float_vec3,
                .data_type     = attribute_types.normal_smooth,
                .default_value = glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}
            }
        );
    }

    if (attributes.tangent) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage         = { Vertex_attribute::Usage_type::tangent },
                .shader_type   = erhe::graphics::Glsl_type::float_vec4, // TODO Is this really always vec4? Maybe check
                .data_type     = attribute_types.tangent,
                .default_value = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}
            }
        );
    }

    if (attributes.bitangent) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage         = { Vertex_attribute::Usage_type::bitangent },
                .shader_type   = erhe::graphics::Glsl_type::float_vec4, // TODO vec4, really? Shouldn't this be vec3
                .data_type     = attribute_types.bitangent,
                .default_value = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f}
            }
        );
    }

    if (attributes.color) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage         = { Vertex_attribute::Usage_type::color },
                .shader_type   = erhe::graphics::Glsl_type::float_vec4,
                .data_type     = attribute_types.color,
                .default_value = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}
            }
        );
    }

    if (attributes.id) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage       = { Vertex_attribute::Usage_type::id },
                .shader_type = erhe::graphics::Glsl_type::float_vec3,
                .data_type   = attribute_types.id_vec3
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
        ////                 .type      = gl::Vertex_attrib_type::unsigned_int,
        ////                 .dimension = 1
        ////             }
        ////         }
        ////     );
        //// }
    }

    if (attributes.texcoord) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage       = { Vertex_attribute::Usage_type::tex_coord },
                .shader_type = erhe::graphics::Glsl_type::float_vec2,
                .data_type   = attribute_types.texcoord,
            }
        );
    }

    if (attributes.joint_indices) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage       = { Vertex_attribute::Usage_type::joint_indices },
                .shader_type = erhe::graphics::Glsl_type::unsigned_int_vec4,
                .data_type   = attribute_types.joint_indices
            }
        );
    }

    if (attributes.joint_weights) {
        vertex_format.add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage       = { Vertex_attribute::Usage_type::joint_weights },
                .shader_type = erhe::graphics::Glsl_type::float_vec4,
                .data_type   = attribute_types.joint_weights
            }
        );
    }
    return vertex_format;
}

}
