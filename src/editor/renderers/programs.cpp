#include "renderers/programs.hpp"

#include "log.hpp"
#include "erhe_tracy.hpp"

#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics_experimental/shader_monitor.hpp"

namespace sample {

using erhe::graphics::Configuration;
using erhe::graphics::Shader_stages;
using erhe::graphics::Vertex_attribute;
using erhe::log::Log;

void Programs::connect(std::shared_ptr<erhe::graphics::Shader_monitor> shader_monitor)
{
    m_shader_monitor = shader_monitor;

    initialization_depends_on(shader_monitor);
}


void Programs::initialize_component()
{
    ZoneScoped;

    log_programs.trace("{}()\n", __func__);
    Log::Indenter indenter;

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

    material_block_offsets.metallic     = material_struct.add_float("metallic"    ).offset_in_parent();
    material_block_offsets.roughness    = material_struct.add_float("roughness"   ).offset_in_parent();
    material_block_offsets.anisotropy   = material_struct.add_float("anisotropy"  ).offset_in_parent();
    material_block_offsets.transparency = material_struct.add_float("transparency").offset_in_parent();
    material_block_offsets.base_color   = material_struct.add_vec4 ("base_color"  ).offset_in_parent();
    material_block_offsets.emissive     = material_struct.add_vec4 ("emissive"    ).offset_in_parent();
    material_block.add_struct("materials", &material_struct, 200);

    light_block_offsets.directional_light_count            = light_block.add_uint  ("directional_light_count"     ).offset_in_parent();
    light_block_offsets.spot_light_count                   = light_block.add_uint  ("spot_light_count"            ).offset_in_parent();
    light_block_offsets.point_light_count                  = light_block.add_uint  ("point_light_count"           ).offset_in_parent();
    light_block_offsets.reserved                           = light_block.add_uint  ("reserved"                    ).offset_in_parent();
    light_block_offsets.ambient_light                      = light_block.add_vec4  ("ambient_light"               ).offset_in_parent();
    light_block_offsets.light.texture_from_world           = light_struct.add_mat4 ("texture_from_world"          ).offset_in_parent();
    light_block_offsets.light.position_and_inner_spot_cos  = light_struct.add_vec4 ("position_and_inner_spot_cos" ).offset_in_parent();
    light_block_offsets.light.direction_and_outer_spot_cos = light_struct.add_vec4 ("direction_and_outer_spot_cos").offset_in_parent();
    light_block_offsets.light.radiance_and_range           = light_struct.add_vec4 ("radiance_and_range"          ).offset_in_parent();
    light_block_offsets.light_struct             = light_block.add_struct("lights", &light_struct, 120).offset_in_parent();

    camera_block_offsets.world_from_node = camera_struct.add_mat4 ("world_from_node").offset_in_parent();
    camera_block_offsets.world_from_clip = camera_struct.add_mat4 ("world_from_clip").offset_in_parent();
    camera_block_offsets.clip_from_world = camera_struct.add_mat4 ("clip_from_world").offset_in_parent();
    camera_block_offsets.viewport        = camera_struct.add_vec4 ("viewport").offset_in_parent();
    camera_block_offsets.exposure        = camera_struct.add_float("exposure").offset_in_parent();
    camera_block.add_struct("cameras", &camera_struct, 1);

    primitive_block_offsets.world_from_node = primitive_struct.add_mat4 ("world_from_node").offset_in_parent();
    primitive_block_offsets.color           = primitive_struct.add_vec4 ("color"          ).offset_in_parent();
    primitive_block_offsets.material_index  = primitive_struct.add_uint ("material_index" ).offset_in_parent();
    primitive_block_offsets.size            = primitive_struct.add_float("size"           ).offset_in_parent();
    primitive_block_offsets.extra2          = primitive_struct.add_uint ("extra2"         ).offset_in_parent();
    primitive_block_offsets.extra3          = primitive_struct.add_uint ("extra3"         ).offset_in_parent();
    primitive_block.add_struct("primitives", &primitive_struct, 1000);

    nearest_sampler = std::make_unique<erhe::graphics::Sampler>(gl::Texture_min_filter::nearest,
                                                                gl::Texture_mag_filter::nearest);

    linear_sampler = std::make_unique<erhe::graphics::Sampler>(gl::Texture_min_filter::linear,
                                                               gl::Texture_mag_filter::linear);

    shadow_sampler_location = default_uniform_block.add_sampler("s_shadow",  gl::Uniform_type::sampler_2d_array).location();

    m_shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");

    basic      = make_program("basic");
    standard   = make_program("standard");
    edge_lines = make_program("edge_lines");
    wide_lines = make_program("wide_lines");
    points     = make_program("points");
    depth      = make_program("depth");
    id         = make_program("id");
    tool       = make_program("tool");
}

auto Programs::make_program(const std::string& name)
-> std::unique_ptr<erhe::graphics::Shader_stages>
{
    std::vector<std::string> no_defines;
    return make_program(name, no_defines);
}

auto Programs::make_program(const std::string& name,
                            const std::string& define)
-> std::unique_ptr<erhe::graphics::Shader_stages>
{
    std::vector<std::string> defines;
    defines.push_back(define);
    return make_program(name, defines);
}

auto Programs::make_program(const std::string&              name,
                            const std::vector<std::string>& defines)
-> std::unique_ptr<erhe::graphics::Shader_stages>
{
    ZoneScoped;

    log_programs.info("Programs::make_program({})\n", name);

    log_programs.info("current directory is {}\n", std::filesystem::current_path().string());

    std::filesystem::path vs_path = m_shader_path / std::filesystem::path(name + ".vert");
    std::filesystem::path gs_path = m_shader_path / std::filesystem::path(name + ".geom");
    std::filesystem::path fs_path = m_shader_path / std::filesystem::path(name + ".frag");

    bool vs_exists = std::filesystem::exists(vs_path);
    bool gs_exists = std::filesystem::exists(gs_path);
    bool fs_exists = std::filesystem::exists(fs_path);

    Shader_stages::Create_info create_info(name,
                                           &default_uniform_block,
                                           &attribute_mappings,
                                           &fragment_outputs);
    create_info.add_interface_block(&material_block);
    create_info.add_interface_block(&light_block);
    create_info.add_interface_block(&camera_block);
    create_info.add_interface_block(&primitive_block);
    create_info.struct_types.push_back(&material_struct);
    create_info.struct_types.push_back(&light_struct);
    create_info.struct_types.push_back(&camera_struct);
    create_info.struct_types.push_back(&primitive_struct);
    for (auto j : defines)
    {
        create_info.defines.emplace_back(j, "1");
    }
    if (vs_exists)
    {
        create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    }
    if (gs_exists)
    {
        create_info.shaders.emplace_back(gl::Shader_type::geometry_shader, gs_path);
    }
    if (fs_exists)
    {
        create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, fs_path);
    }

    Shader_stages::Prototype prototype(create_info);
    VERIFY(prototype.is_valid());
    auto p = std::make_unique<Shader_stages>(std::move(prototype));

    if (m_shader_monitor)
    {
        m_shader_monitor->add(create_info, p.get());
    }

    return p;
}

}
