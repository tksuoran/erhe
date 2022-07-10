#include "renderers/program_interface.hpp"
#include "log.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/window.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor {

using erhe::graphics::Shader_stages;
using erhe::graphics::Vertex_attribute;

Program_interface::Program_interface()
    : erhe::components::Component{c_label}
{
}

Program_interface::~Program_interface() noexcept
{
}

void Program_interface::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::application::Window>(); // ensures we have graphics::Instance info

    // TODO Make erhe::graphics::Instance a component
}

Program_interface::Shader_resources::Shader_resources(
    std::size_t max_material_count,
    std::size_t max_light_count,
    std::size_t max_camera_count,
    std::size_t max_primitive_count
)
    : camera_interface   {max_camera_count   }
    , light_interface    {max_light_count    }
    , material_interface {max_material_count }
    , primitive_interface{max_primitive_count}

{
    fragment_outputs.add("out_color", gl::Fragment_shader_output_type::float_vec4, 0);

    attribute_mappings.add(
        {
            .layout_location = 0,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_position_texcoord",
            .src_usage       =
            {
                .type = Vertex_attribute::Usage_type::position | Vertex_attribute::Usage_type::tex_coord
            }
        }
    );
    attribute_mappings.add(
        {
            .layout_location = 0,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_position",
            .src_usage       =
            {
                .type = Vertex_attribute::Usage_type::position
            }
        }
    );
    attribute_mappings.add(
        {
            .layout_location = 1,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_normal",
            .src_usage       =
            {
                .type = Vertex_attribute::Usage_type::normal
            }
        }
    );
    attribute_mappings.add(
        {
            .layout_location = 2,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_normal_flat",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::normal,
                .index = 1
            }
        }
    );
    attribute_mappings.add(
        {
            .layout_location = 3,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_normal_smooth",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::normal,
                .index = 2
            }
        }
    );
    attribute_mappings.add(
        {
            .layout_location = 4,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_tangent",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::tangent,
            }
        }
    );
    attribute_mappings.add(
        {
            .layout_location = 5,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_bitangent",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::bitangent,
            }
        }
    );
    attribute_mappings.add(
        {
            .layout_location = 6,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_color",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::color,
            }
        }
    );
    attribute_mappings.add(
        {
            .layout_location = 7,
            .shader_type     = gl::Attribute_type::float_vec2,
            .name            = "a_texcoord",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::tex_coord,
            }
        }
    );
    attribute_mappings.add(
        {
            .layout_location = 8,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_id",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::id,
            }
        }
    );
}

void Program_interface::initialize_component()
{
    const auto& config = get<erhe::application::Configuration>();

    shader_resources = std::make_unique<Shader_resources>(
        config->renderer.max_material_count,
        config->renderer.max_light_count,
        config->renderer.max_camera_count,
        config->renderer.max_primitive_count
    );
}

} // namespace editor
