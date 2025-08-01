#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics { class Vertex_attribute; }

namespace erhe::scene_renderer {

using erhe::graphics::Vertex_attribute;

Program_interface::Program_interface(
    erhe::graphics::Device&          graphics_device,
    erhe::dataformat::Vertex_format& vertex_format
)
    : graphics_device{graphics_device}
    , fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    }
    , vertex_format      {vertex_format}
    , camera_interface   {graphics_device}
    , cube_interface     {graphics_device}
    , joint_interface    {graphics_device}
    , light_interface    {graphics_device}
    , material_interface {graphics_device}
    , primitive_interface{graphics_device}
{
}

auto Program_interface::make_prototype(
    const std::filesystem::path&                shader_path,
    erhe::graphics::Shader_stages_create_info&& in_create_info
) -> erhe::graphics::Shader_stages_prototype
{
    erhe::graphics::Shader_stages_create_info create_info = std::move(in_create_info);
    return make_prototype(shader_path, create_info);
}

auto Program_interface::make_prototype(
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

    create_info.fragment_outputs = &fragment_outputs,
    create_info.vertex_format    = &vertex_format;
    create_info.struct_types.push_back(&material_interface.material_struct);
    create_info.struct_types.push_back(&light_interface.light_struct);
    create_info.struct_types.push_back(&camera_interface.camera_struct);
    create_info.struct_types.push_back(&cube_interface.cube_instance_struct);
    create_info.struct_types.push_back(&cube_interface.cube_control_struct);
    create_info.struct_types.push_back(&primitive_interface.primitive_struct);
    create_info.struct_types.push_back(&joint_interface.joint_struct);
    // TODO: This will be (eventually) for compute shaders.
    // create_info.struct_types.push_back(&g_mesh_memory->get_vertex_data_in());
    // create_info.struct_types.push_back(&g_mesh_memory->get_vertex_data_out());
    create_info.add_interface_block(&material_interface.material_block);
    create_info.add_interface_block(&light_interface.light_block);
    create_info.add_interface_block(&light_interface.light_control_block);
    create_info.add_interface_block(&camera_interface.camera_block);
    create_info.add_interface_block(&cube_interface.cube_instance_block);
    create_info.add_interface_block(&cube_interface.cube_control_block);
    create_info.add_interface_block(&primitive_interface.primitive_block);
    create_info.add_interface_block(&joint_interface.joint_block);
    create_info.defines.emplace_back("ERHE_SHADOW_MAPS", "1");

    bool found = false;
    auto process_shader = [&create_info, &found](erhe::graphics::Shader_type shader_type, const std::filesystem::path& path) -> void
    {
        if (erhe::file::check_is_existing_non_empty_regular_file("Program_interface::make_prototype", path, true)) {
            create_info.shaders.emplace_back(shader_type, path);
            found = true;
        }
    };

    process_shader(erhe::graphics::Shader_type::compute_shader,  cs_path);
    process_shader(erhe::graphics::Shader_type::fragment_shader, fs_path);
    process_shader(erhe::graphics::Shader_type::geometry_shader, gs_path);
    process_shader(erhe::graphics::Shader_type::vertex_shader,   vs_path);

    if (!found) {
        log_program_interface->error("Could not load shader source file {} (.vert / .frag / .comp / .geom)", create_info.name);
        log_program_interface->error("current directory is {}", std::filesystem::current_path().string());
        log_program_interface->flush();
        ERHE_FATAL("Missing resource file");
    }
    return erhe::graphics::Shader_stages_prototype{graphics_device, create_info};
}

auto Program_interface::make_program(erhe::graphics::Shader_stages_prototype&& prototype) -> erhe::graphics::Shader_stages
{
    ERHE_PROFILE_FUNCTION();

    if (!prototype.is_valid()) {
        log_program_interface->error("current directory is {}", std::filesystem::current_path().string());
        log_program_interface->error("Compiling shader program {} failed", prototype.name());
        return erhe::graphics::Shader_stages{graphics_device, prototype.name()};
    }

    return erhe::graphics::Shader_stages{graphics_device, std::move(prototype)};
}

} // namespace erhe::scene_renderer
