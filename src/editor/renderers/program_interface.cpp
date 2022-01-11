#include "renderers/program_interface.hpp"
#include "log.hpp"
#include "window.hpp"

#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor {

using erhe::graphics::Shader_stages;
using erhe::graphics::Vertex_attribute;

Program_interface::Program_interface()
    : erhe::components::Component{c_name}
{
}

Program_interface::~Program_interface() = default;


void Program_interface::connect()
{
    require<Window>(); // ensures we have graphics::Instance info

    // TODO Make erhe::graphics::Instance a component
}

Program_interface::Shader_resources::Shader_resources()
{
    fragment_outputs.add("out_color", gl::Fragment_shader_output_type::float_vec4, 0);

    attribute_mappings.add(gl::Attribute_type::float_vec4, "a_position_texcoord", {Vertex_attribute::Usage_type::position | Vertex_attribute::Usage_type::tex_coord, 0}, 0);
    attribute_mappings.add(gl::Attribute_type::float_vec3, "a_position",          {Vertex_attribute::Usage_type::position,  0}, 0);
    attribute_mappings.add(gl::Attribute_type::float_vec3, "a_normal",            {Vertex_attribute::Usage_type::normal,    0}, 1);
    attribute_mappings.add(gl::Attribute_type::float_vec3, "a_normal_flat",       {Vertex_attribute::Usage_type::normal,    1}, 2);
    attribute_mappings.add(gl::Attribute_type::float_vec3, "a_normal_smooth",     {Vertex_attribute::Usage_type::normal,    2}, 3);
    attribute_mappings.add(gl::Attribute_type::float_vec4, "a_tangent",           {Vertex_attribute::Usage_type::tangent,   0}, 4);
    attribute_mappings.add(gl::Attribute_type::float_vec4, "a_bitangent",         {Vertex_attribute::Usage_type::bitangent, 0}, 5);
    attribute_mappings.add(gl::Attribute_type::float_vec4, "a_color",             {Vertex_attribute::Usage_type::color,     0}, 6);
    attribute_mappings.add(gl::Attribute_type::float_vec2, "a_texcoord",          {Vertex_attribute::Usage_type::tex_coord, 0}, 7);
    attribute_mappings.add(gl::Attribute_type::float_vec4, "a_id",                {Vertex_attribute::Usage_type::id,        0}, 8);

    material_block_offsets = {
        .metallic     = material_struct.add_float("metallic"    )->offset_in_parent(),
        .roughness    = material_struct.add_float("roughness"   )->offset_in_parent(),
        .anisotropy   = material_struct.add_float("anisotropy"  )->offset_in_parent(),
        .transparency = material_struct.add_float("transparency")->offset_in_parent(),
        .base_color   = material_struct.add_vec4 ("base_color"  )->offset_in_parent(),
        .emissive     = material_struct.add_vec4 ("emissive"    )->offset_in_parent()
    };
    material_block.add_struct("materials", &material_struct, 200);

    light_block_offsets = {
        .directional_light_count = light_block.add_uint("directional_light_count"     )->offset_in_parent(),
        .spot_light_count        = light_block.add_uint("spot_light_count"            )->offset_in_parent(),
        .point_light_count       = light_block.add_uint("point_light_count"           )->offset_in_parent(),
        .reserved                = light_block.add_uint("reserved"                    )->offset_in_parent(),
        .ambient_light           = light_block.add_vec4("ambient_light"               )->offset_in_parent(),
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
