#include "renderers/program_interface.hpp"
#include "log.hpp"

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

Program_interface::~Program_interface() = default;


void Program_interface::connect()
{
    require<erhe::application::Window>(); // ensures we have graphics::Instance info

    // TODO Make erhe::graphics::Instance a component
}

Program_interface::Shader_resources::Shader_resources()
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

    material_block_offsets = {
        .roughness    = material_struct.add_vec2 ("roughness"   )->offset_in_parent(),
        .metallic     = material_struct.add_float("metallic"    )->offset_in_parent(),
        .transparency = material_struct.add_float("transparency")->offset_in_parent(),
        .base_color   = material_struct.add_vec4 ("base_color"  )->offset_in_parent(),
        .emissive     = material_struct.add_vec4 ("emissive"    )->offset_in_parent(),
        .base_texture = material_struct.add_uvec2("texture"     )->offset_in_parent(),
        .reserved     = material_struct.add_uvec2("reserved"    )->offset_in_parent()
    };
    material_block.add_struct("materials", &material_struct, 200);

    light_block_offsets = {
        .shadow_texture          = light_block.add_uvec2("shadow_texture"         )->offset_in_parent(),
        .reserved_1              = light_block.add_uvec2("reserved_1"             )->offset_in_parent(),
        .directional_light_count = light_block.add_uint ("directional_light_count")->offset_in_parent(),
        .spot_light_count        = light_block.add_uint ("spot_light_count"       )->offset_in_parent(),
        .point_light_count       = light_block.add_uint ("point_light_count"      )->offset_in_parent(),
        .reserved_0              = light_block.add_uint ("reserved_0"             )->offset_in_parent(),
        .ambient_light           = light_block.add_vec4 ("ambient_light"          )->offset_in_parent(),
        .reserved_2              = light_block.add_uvec4("reserved_2"             )->offset_in_parent(),
        .light = {
            .texture_from_world           = light_struct.add_mat4("texture_from_world"          )->offset_in_parent(),
            .position_and_inner_spot_cos  = light_struct.add_vec4("position_and_inner_spot_cos" )->offset_in_parent(),
            .direction_and_outer_spot_cos = light_struct.add_vec4("direction_and_outer_spot_cos")->offset_in_parent(),
            .radiance_and_range           = light_struct.add_vec4("radiance_and_range"          )->offset_in_parent(),
        },
        .light_struct            = light_block.add_struct("lights", &light_struct, c_max_light_count)->offset_in_parent()
    };

    camera_block_offsets = {
        .world_from_node      = camera_struct.add_mat4 ("world_from_node"     )->offset_in_parent(),
        .world_from_clip      = camera_struct.add_mat4 ("world_from_clip"     )->offset_in_parent(),
        .clip_from_world      = camera_struct.add_mat4 ("clip_from_world"     )->offset_in_parent(),
        .viewport             = camera_struct.add_vec4 ("viewport"            )->offset_in_parent(),
        .fov                  = camera_struct.add_vec4 ("fov"                 )->offset_in_parent(),
        .clip_depth_direction = camera_struct.add_float("clip_depth_direction")->offset_in_parent(),
        .view_depth_near      = camera_struct.add_float("view_depth_near"     )->offset_in_parent(),
        .view_depth_far       = camera_struct.add_float("view_depth_far"      )->offset_in_parent(),
        .exposure             = camera_struct.add_float("exposure"            )->offset_in_parent()
    };
    camera_block.add_struct("cameras", &camera_struct, 1);

    primitive_block_offsets = {
        .world_from_node = primitive_struct.add_mat4 ("world_from_node")->offset_in_parent(),
        .color           = primitive_struct.add_vec4 ("color"          )->offset_in_parent(),
        .material_index  = primitive_struct.add_uint ("material_index" )->offset_in_parent(),
        .size            = primitive_struct.add_float("size"           )->offset_in_parent(),
        .extra2          = primitive_struct.add_uint ("extra2"         )->offset_in_parent(),
        .extra3          = primitive_struct.add_uint ("extra3"         )->offset_in_parent()
    };
    primitive_block.add_struct("primitives", &primitive_struct, 1000);
}

void Program_interface::initialize_component()
{
    // No idea why cppcheck this interface_blocks is not used

    // cppcheck-suppress unreadVariable
    shader_resources = std::make_unique<Shader_resources>();
}

}
