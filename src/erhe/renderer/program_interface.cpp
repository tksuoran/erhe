#include "erhe/renderer/program_interface.hpp"
#include "erhe/renderer/renderer_log.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/shader_monitor.hpp"
#include "erhe/application/window.hpp"
#include "erhe/gl/command_info.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

namespace erhe::graphics
{
class Vertex_attribute;
}

namespace erhe::renderer
{

using erhe::graphics::Vertex_attribute;

Program_interface* g_program_interface{nullptr};

Program_interface::Program_interface()
    : erhe::components::Component{c_type_name}
{
}

Program_interface::~Program_interface() noexcept
{
}

void Program_interface::deinitialize_component()
{
    ERHE_VERIFY(g_program_interface == this);
    shader_resources.reset();
    g_program_interface = nullptr;
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
    std::size_t max_primitive_count,
    std::size_t max_joint_count
)
    : fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = gl::Fragment_shader_output_type::float_vec4,
            .location = 0
        }
    }
    , attribute_mappings{
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 0,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_position_texcoord",
            .src_usage       =
            {
                .type = Vertex_attribute::Usage_type::position | Vertex_attribute::Usage_type::tex_coord
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 0,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_position",
            .src_usage       =
            {
                .type = Vertex_attribute::Usage_type::position
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 1,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_normal",
            .src_usage       =
            {
                .type = Vertex_attribute::Usage_type::normal
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 2,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_normal_flat",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::normal,
                .index = 1
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 3,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_normal_smooth",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::normal,
                .index = 2
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 4,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_tangent",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::tangent,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 5,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_bitangent",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::bitangent,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 6,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_color",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::color,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 7,
            .shader_type     = gl::Attribute_type::float_vec2,
            .name            = "a_texcoord",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::tex_coord,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 8,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_id",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::id,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 9,
            .shader_type     = gl::Attribute_type::unsigned_int_vec4,
            .name            = "a_joints",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::joint_indices,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 10,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_weights",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::joint_weights,
            }
        }
    }
    , camera_interface   {max_camera_count   }
    , light_interface    {max_light_count    }
    , material_interface {max_material_count }
    , primitive_interface{max_primitive_count}
    , joint_interface    {max_joint_count    }
{
}

auto Program_interface::Shader_resources::make_prototype(
    const std::filesystem::path&               shader_path,
    erhe::graphics::Shader_stages::Create_info create_info
) -> std::unique_ptr<erhe::graphics::Shader_stages::Prototype>
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

    if (erhe::graphics::Instance::info.gl_version < 430) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_storage_buffer_object));
        create_info.extensions.push_back({gl::Shader_type::vertex_shader,   "GL_ARB_shader_storage_buffer_object"});
        create_info.extensions.push_back({gl::Shader_type::geometry_shader, "GL_ARB_shader_storage_buffer_object"});
        create_info.extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_shader_storage_buffer_object"});
    }
    if (erhe::graphics::Instance::info.gl_version < 460) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_draw_parameters));
        create_info.extensions.push_back({gl::Shader_type::vertex_shader,   "GL_ARB_shader_draw_parameters"});
        create_info.extensions.push_back({gl::Shader_type::geometry_shader, "GL_ARB_shader_draw_parameters"});
        create_info.defines.push_back({"gl_DrawID", "gl_DrawIDARB"});
    }

    create_info.defines.emplace_back("ERHE_SHADOW_MAPS", "1");

    if (erhe::graphics::Instance::info.use_bindless_texture) {
        create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
        create_info.extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
    }

    const auto check_file = [](const std::filesystem::path& path) {
        std::error_code error_code;
        const bool exists = std::filesystem::exists(path, error_code);
        if (error_code || !exists) {
            return false;
        }
        const bool is_empty = std::filesystem::is_empty(path, error_code);
        if (error_code || is_empty) {
            return false;
        }
        const bool is_regular_file = std::filesystem::is_regular_file(path, error_code);
        if (error_code || !is_regular_file) {
            return false;
        }
        return true;
    };

    if (check_file(cs_path)) {
        create_info.shaders.emplace_back(gl::Shader_type::compute_shader,  cs_path);
    }
    if (check_file(fs_path)) {
        create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, fs_path);
    }
    if (check_file(gs_path)) {
        create_info.shaders.emplace_back(gl::Shader_type::geometry_shader, gs_path);
    }
    if (check_file(vs_path)) {
        create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    }

    return std::make_unique<erhe::graphics::Shader_stages::Prototype>(create_info);
}

auto Program_interface::Shader_resources::make_program(
    erhe::graphics::Shader_stages::Prototype& prototype
) -> std::unique_ptr<erhe::graphics::Shader_stages>
{
    ERHE_PROFILE_FUNCTION();

    if (!prototype.is_valid()) {
        log_program_interface->error("current directory is {}", std::filesystem::current_path().string());
        log_program_interface->error("Compiling shader program {} failed", prototype.name());
        return {};
    }

    auto p = std::make_unique<erhe::graphics::Shader_stages>(std::move(prototype));

    if (erhe::application::g_shader_monitor != nullptr) {
        erhe::application::g_shader_monitor->add(prototype.create_info(), p.get());
    }

    return p;
}

void Program_interface::initialize_component()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(g_program_interface == nullptr);

    auto ini = erhe::application::get_ini("erhe.ini", "renderer");
    ini->get("max_material_count",  config.max_material_count );
    ini->get("max_light_count",     config.max_light_count    );
    ini->get("max_camera_count",    config.max_camera_count   );
    ini->get("max_primitive_count", config.max_primitive_count);
    ini->get("max_draw_count",      config.max_draw_count     );
    ini->get("max_joint_count",     config.max_joint_count    );

    shader_resources = std::make_unique<Shader_resources>(
        config.max_material_count,
        config.max_light_count,
        config.max_camera_count,
        config.max_primitive_count,
        config.max_joint_count
    );

    g_program_interface = this;
}

} // namespace erhe::renderer
