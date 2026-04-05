#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics { class Vertex_attribute; }

namespace erhe::scene_renderer {

using erhe::graphics::Vertex_attribute;

Program_interface::Program_interface(
    erhe::graphics::Device&          graphics_device,
    erhe::dataformat::Vertex_format& vertex_format,
    Program_interface_config&        config
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
    , config             {config}
    , camera_interface   {graphics_device, config.max_camera_count}
    , cube_interface     {graphics_device}
    , joint_interface    {graphics_device, config.max_joint_count}
    , light_interface    {graphics_device, config.max_light_count}
    , material_interface {graphics_device, config.max_material_count}
    , primitive_interface{graphics_device, config.max_primitive_count}
{
    // Write clamped values back to config so callers see actual UBO limits
    config.max_camera_count    = static_cast<int>(camera_interface.max_camera_count);
    config.max_joint_count     = static_cast<int>(joint_interface.max_joint_count);
    config.max_light_count     = static_cast<int>(light_interface.max_light_count);
    config.max_material_count  = static_cast<int>(material_interface.max_material_count);
    config.max_primitive_count = static_cast<int>(primitive_interface.max_primitive_count);

    // Create bind group layout describing the descriptor types for all interface blocks
    auto to_binding_type = [](const erhe::graphics::Shader_resource& block) -> erhe::graphics::Binding_type {
        return (block.get_type() == erhe::graphics::Shader_resource::Type::shader_storage_block)
            ? erhe::graphics::Binding_type::storage_buffer
            : erhe::graphics::Binding_type::uniform_buffer;
    };
    erhe::graphics::Bind_group_layout_create_info layout_create_info{
        .bindings = {
            {material_buffer_binding_point,      to_binding_type(material_interface.material_block)},
            {light_buffer_binding_point,         to_binding_type(light_interface.light_block)},
            {light_control_buffer_binding_point, to_binding_type(light_interface.light_control_block)},
            {primitive_buffer_binding_point,     to_binding_type(primitive_interface.primitive_block)},
            {camera_buffer_binding_point,        to_binding_type(camera_interface.camera_block)},
            {cube_instance_buffer_binding_point, to_binding_type(cube_interface.cube_instance_block)},
            {cube_control_buffer_binding_point,  to_binding_type(cube_interface.cube_control_block)},
            {joint_buffer_binding_point,         to_binding_type(joint_interface.joint_block)},
        },
        .debug_label = "Scene renderer"
    };
    // Add shadow sampler bindings (used by non-bindless paths: Vulkan descriptor indexing, OpenGL sampler array)
    if (graphics_device.get_info().uses_default_uniform_block()) {
        layout_create_info.bindings.push_back({c_texture_heap_slot_shadow_compare,    erhe::graphics::Binding_type::combined_image_sampler});
        layout_create_info.bindings.push_back({c_texture_heap_slot_shadow_no_compare, erhe::graphics::Binding_type::combined_image_sampler});
    }
    bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(graphics_device, layout_create_info);
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

    create_info.fragment_outputs = &fragment_outputs;
    if (!create_info.no_vertex_input) {
        create_info.vertex_format = &vertex_format;
    }
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
    create_info.bind_group_layout = bind_group_layout.get();
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
#if !defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
    process_shader(erhe::graphics::Shader_type::geometry_shader, gs_path);
#endif
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
