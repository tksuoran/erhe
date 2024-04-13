#include "erhe_scene_renderer/program_interface.hpp"

#include "erhe_gl/command_info.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics
{
    class Vertex_attribute;
}

namespace erhe::scene_renderer
{

using erhe::graphics::Vertex_attribute;

Program_interface::Program_interface(
    erhe::graphics::Instance& graphics_instance
)
    : fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = gl::Fragment_shader_output_type::float_vec4,
            .location = 0
        }
    }
    , attribute_mappings{
        graphics_instance,
        {
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 0,
                .shader_type     = gl::Attribute_type::float_vec4,
                .name            = "a_position_texcoord",
                .src_usage       = {
                    .type = Vertex_attribute::Usage_type::position | Vertex_attribute::Usage_type::tex_coord
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 0,
                .shader_type     = gl::Attribute_type::float_vec3,
                .name            = "a_position",
                .src_usage       = {
                    .type = Vertex_attribute::Usage_type::position
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 1,
                .shader_type     = gl::Attribute_type::float_vec3,
                .name            = "a_normal",
                .src_usage       = {
                    .type = Vertex_attribute::Usage_type::normal
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 3,
                .shader_type     = gl::Attribute_type::float_vec3,
                .name            = "a_normal_smooth",
                .src_usage       = {
                    .type  = Vertex_attribute::Usage_type::normal,
                    .index = 1
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 2,
                .shader_type     = gl::Attribute_type::float_vec3,
                .name            = "a_normal_flat",
                .src_usage       = {
                    .type  = Vertex_attribute::Usage_type::normal,
                    .index = 2
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 4,
                .shader_type     = gl::Attribute_type::float_vec4,
                .name            = "a_tangent",
                .src_usage       = {
                    .type  = Vertex_attribute::Usage_type::tangent,
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 5,
                .shader_type     = gl::Attribute_type::float_vec4,
                .name            = "a_bitangent",
                .src_usage       = {
                    .type  = Vertex_attribute::Usage_type::bitangent,
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 6,
                .shader_type     = gl::Attribute_type::float_vec4,
                .name            = "a_color",
                .src_usage       = {
                    .type  = Vertex_attribute::Usage_type::color,
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 7,
                .shader_type     = gl::Attribute_type::float_vec2,
                .name            = "a_aniso_control",
                .src_usage       = {
                    .type  = Vertex_attribute::Usage_type::aniso_control,
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 8,
                .shader_type     = gl::Attribute_type::float_vec2,
                .name            = "a_texcoord",
                .src_usage       = {
                    .type  = Vertex_attribute::Usage_type::tex_coord,
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 9,
                .shader_type     = gl::Attribute_type::float_vec4,
                .name            = "a_id",
                .src_usage       = {
                    .type  = Vertex_attribute::Usage_type::id,
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 10,
                .shader_type     = gl::Attribute_type::unsigned_int_vec4,
                .name            = "a_joints",
                .src_usage       =
                {
                    .type  = Vertex_attribute::Usage_type::joint_indices,
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 11,
                .shader_type     = gl::Attribute_type::float_vec4,
                .name            = "a_weights",
                .src_usage       = {
                    .type  = Vertex_attribute::Usage_type::joint_weights,
                }
            }
        }
    }
    , camera_interface   {graphics_instance}
    , joint_interface    {graphics_instance}
    , light_interface    {graphics_instance}
    , material_interface {graphics_instance}
    , primitive_interface{graphics_instance}
{
}

auto Program_interface::make_prototype(
    erhe::graphics::Instance&                   graphics_instance,
    const std::filesystem::path&                shader_path,
    erhe::graphics::Shader_stages_create_info&& in_create_info
) -> erhe::graphics::Shader_stages_prototype
{
    erhe::graphics::Shader_stages_create_info create_info = std::move(in_create_info);
    return make_prototype(graphics_instance, shader_path, create_info);
}

auto Program_interface::make_prototype(
    erhe::graphics::Instance&                  graphics_instance,
    const std::filesystem::path&               shader_path,
    erhe::graphics::Shader_stages_create_info& create_info
) -> erhe::graphics::Shader_stages_prototype
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(log_programs, "Programs::make_program({})", create_info.name);
    SPDLOG_LOGGER_TRACE(log_programs, "current directory is {}", std::filesystem::current_path().string());

    const std::filesystem::path cs_path = shader_path / std::filesystem::path(create_info.name + ".comp");
    const std::filesystem::path fs_path = shader_path / std::filesystem::path(create_info.name + ".frag");
    const std::filesystem::path gs_path = shader_path / std::filesystem::path(create_info.name + ".geom");
    const std::filesystem::path vs_path = shader_path / std::filesystem::path(create_info.name + ".vert");

    create_info.vertex_attribute_mappings = &attribute_mappings,
    create_info.fragment_outputs          = &fragment_outputs,
    create_info.struct_types.push_back(&material_interface.material_struct);
    create_info.struct_types.push_back(&light_interface.light_struct);
    create_info.struct_types.push_back(&camera_interface.camera_struct);
    create_info.struct_types.push_back(&primitive_interface.primitive_struct);
    create_info.struct_types.push_back(&joint_interface.joint_struct);
    // TODO: This will be (eventually) for compute shaders.
    // create_info.struct_types.push_back(&g_mesh_memory->get_vertex_data_in());
    // create_info.struct_types.push_back(&g_mesh_memory->get_vertex_data_out());
    create_info.add_interface_block(&material_interface.material_block);
    create_info.add_interface_block(&light_interface.light_block);
    create_info.add_interface_block(&light_interface.light_control_block);
    create_info.add_interface_block(&camera_interface.camera_block);
    create_info.add_interface_block(&primitive_interface.primitive_block);
    create_info.add_interface_block(&joint_interface.joint_block);

    if (graphics_instance.info.gl_version < 430) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_storage_buffer_object));
        create_info.extensions.push_back({igl::ShaderStage::vertex_shader,   "GL_ARB_shader_storage_buffer_object"});
        create_info.extensions.push_back({igl::ShaderStage::geometry_shader, "GL_ARB_shader_storage_buffer_object"});
        create_info.extensions.push_back({igl::ShaderStage::fragment_shader, "GL_ARB_shader_storage_buffer_object"});
    }
    if (graphics_instance.info.gl_version < 460) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_draw_parameters));
        create_info.extensions.push_back({igl::ShaderStage::vertex_shader,   "GL_ARB_shader_draw_parameters"});
        create_info.extensions.push_back({igl::ShaderStage::geometry_shader, "GL_ARB_shader_draw_parameters"});
        create_info.defines.push_back({"gl_DrawID", "gl_DrawIDARB"});
    }

    create_info.defines.emplace_back("ERHE_SHADOW_MAPS", "1");

    if (graphics_instance.info.use_bindless_texture) {
        create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
        create_info.extensions.push_back({igl::ShaderStage::fragment_shader, "GL_ARB_bindless_texture"});
    }

    if (erhe::file::check_is_existing_non_empty_regular_file("Program_interface::make_prototype", cs_path, true)) {
        create_info.shaders.emplace_back(igl::ShaderStage::compute_shader,  cs_path);
    }
    if (erhe::file::check_is_existing_non_empty_regular_file("Program_interface::make_prototype", fs_path, true)) {
        create_info.shaders.emplace_back(igl::ShaderStage::fragment_shader, fs_path);
    }
    if (erhe::file::check_is_existing_non_empty_regular_file("Program_interface::make_prototype", gs_path, true)) {
        create_info.shaders.emplace_back(igl::ShaderStage::geometry_shader, gs_path);
    }
    if (erhe::file::check_is_existing_non_empty_regular_file("Program_interface::make_prototype", vs_path, true)) {
        create_info.shaders.emplace_back(igl::ShaderStage::vertex_shader,   vs_path);
    }

    return erhe::graphics::Shader_stages_prototype{graphics_instance, create_info};
}

auto Program_interface::make_program(
    erhe::graphics::Shader_stages_prototype&& prototype
) -> erhe::graphics::Shader_stages
{
    ERHE_PROFILE_FUNCTION();

    if (!prototype.is_valid()) {
        log_program_interface->error("current directory is {}", std::filesystem::current_path().string());
        log_program_interface->error("Compiling shader program {} failed", prototype.name());
        return erhe::graphics::Shader_stages{prototype.name()};
    }

    return erhe::graphics::Shader_stages{std::move(prototype)};
}

} // namespace erhe::scene_renderer
