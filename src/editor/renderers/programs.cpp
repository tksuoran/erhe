#include "renderers/programs.hpp"
#include "log.hpp"

#include "renderers/program_interface.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/graphics/shader_monitor.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor {

using erhe::graphics::Shader_stages;

Programs::Programs()
    : erhe::components::Component{c_label}
{
}

Programs::~Programs() noexcept
{
}

void Programs::declare_required_components()
{
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Configuration>();

    m_program_interface = require<Program_interface>();
    m_shader_monitor    = require<erhe::application::Shader_monitor>();
}

void Programs::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    //const erhe::log::Indenter indenter;

    {
        const erhe::application::Scoped_gl_context gl_context{
            Component::get<erhe::application::Gl_context_provider>()
        };

        nearest_sampler = std::make_unique<erhe::graphics::Sampler>(
            gl::Texture_min_filter::nearest,
            gl::Texture_mag_filter::nearest
        );

        linear_sampler = std::make_unique<erhe::graphics::Sampler>(
            gl::Texture_min_filter::linear,
            gl::Texture_mag_filter::linear
        );

        linear_mipmap_linear_sampler = std::make_unique<erhe::graphics::Sampler>(
            gl::Texture_min_filter::linear_mipmap_linear,
            gl::Texture_mag_filter::linear
        );
    }

    log_programs->info("samplers created");

    if (!erhe::graphics::Instance::info.use_bindless_texture)
    {
        shadow_map_default_uniform_block = std::make_unique<erhe::graphics::Shader_resource>();
        textured_default_uniform_block   = std::make_unique<erhe::graphics::Shader_resource>();

        shadow_map_default_uniform_block->add_sampler(
            "s_shadow",
            gl::Uniform_type::sampler_2d_array,
            shadow_texture_unit
        );
        textured_default_uniform_block->add_sampler(
            "s_texture",
            gl::Uniform_type::sampler_2d,
            base_texture_unit
        );
    }
    const auto* shadow_default_uniform_block = erhe::graphics::Instance::info.use_bindless_texture
        ? nullptr
        : shadow_map_default_uniform_block.get();
    const auto* base_texture_default_uniform_block = erhe::graphics::Instance::info.use_bindless_texture
        ? nullptr
        : textured_default_uniform_block.get();

    m_shader_path = fs::path("res") / fs::path("shaders");

    // TODO Make parallel task queue work
    m_queue = std::make_unique<Serial_task_queue>();


    // Not available on Dell laptop.
    //standard      = make_program("standard", {}, {{gl::Shader_type::fragment_shader, "GL_NV_fragment_shader_barycentric"}});

    using ci = erhe::graphics::Shader_stages::Create_info;

    queue(standard            , ci{ .name = "standard", .default_uniform_block = shadow_default_uniform_block, .dump_interface = true } );
    queue(brush               , ci{ .name = "brush"   , .default_uniform_block = shadow_default_uniform_block } );
    queue(textured            , ci{ .name = "textured", .default_uniform_block = base_texture_default_uniform_block } );
    queue(edge_lines          , ci{ .name = "edge_lines"      } );
    queue(wide_lines          , ci{ .name = "wide_lines"      } );
    queue(points              , ci{ .name = "points"          } );
    queue(depth               , ci{ .name = "depth"           } );
    queue(id                  , ci{ .name = "id"              } );
    queue(tool                , ci{ .name = "tool"            } );
    queue(visualize_depth     , ci{ .name = "visualize_depth", .default_uniform_block = shadow_default_uniform_block } );
    queue(visualize_normal    , ci{ .name = "standard", .defines = { std::pair<std::string, std::string>{"ERHE_VISUALIZE_NORMAL",    "1"}}, .default_uniform_block = shadow_default_uniform_block } );
    queue(visualize_tangent   , ci{ .name = "standard", .defines = { std::pair<std::string, std::string>{"ERHE_VISUALIZE_TANGENT",   "1"}}, .default_uniform_block = shadow_default_uniform_block } );
    queue(visualize_bitangent , ci{ .name = "standard", .defines = { std::pair<std::string, std::string>{"ERHE_VISUALIZE_BITANGENT", "1"}}, .default_uniform_block = shadow_default_uniform_block } );

    SPDLOG_LOGGER_TRACE(log_programs, "all programs queued, waiting for them to complete");
    m_queue->wait();
    m_queue.reset();
    SPDLOG_LOGGER_TRACE(log_programs, "queue finished");
}

void Programs::queue(
    std::unique_ptr<erhe::graphics::Shader_stages>& program,
    erhe::graphics::Shader_stages::Create_info      create_info
)
{
    m_queue->enqueue(
        [this, &program, &create_info]
        ()
        {
            SPDLOG_LOGGER_TRACE(log_programs, "before compiling {}", create_info.name);
            {
                const erhe::application::Scoped_gl_context gl_context{
                    Component::get<erhe::application::Gl_context_provider>()
                };

                SPDLOG_LOGGER_TRACE(log_programs, "compiling {}", create_info.name);
                program = make_program(create_info);
            }
            SPDLOG_LOGGER_TRACE(log_programs, "after compiling {}", create_info.name);
        }
    );
}

auto Programs::make_program(
    erhe::graphics::Shader_stages::Create_info create_info
) -> std::unique_ptr<erhe::graphics::Shader_stages>
{
    ERHE_PROFILE_FUNCTION

    SPDLOG_LOGGER_TRACE(log_programs, "Programs::make_program({})", create_info.name);
    SPDLOG_LOGGER_TRACE(log_programs, "current directory is {}", fs::current_path().string());

    const fs::path vs_path = m_shader_path / fs::path(create_info.name + ".vert");
    const fs::path gs_path = m_shader_path / fs::path(create_info.name + ".geom");
    const fs::path fs_path = m_shader_path / fs::path(create_info.name + ".frag");

    const bool vs_exists = fs::exists(vs_path);
    const bool gs_exists = fs::exists(gs_path);
    const bool fs_exists = fs::exists(fs_path);

    const auto& shader_resources = *m_program_interface->shader_resources.get();

    create_info.vertex_attribute_mappings = &shader_resources.attribute_mappings,
    create_info.fragment_outputs          = &shader_resources.fragment_outputs,
    create_info.add_interface_block(&shader_resources.material_block);
    create_info.add_interface_block(&shader_resources.light_block);
    create_info.add_interface_block(&shader_resources.camera_block);
    create_info.add_interface_block(&shader_resources.primitive_block);
    create_info.struct_types.push_back(&shader_resources.material_struct);
    create_info.struct_types.push_back(&shader_resources.light_struct);
    create_info.struct_types.push_back(&shader_resources.camera_struct);
    create_info.struct_types.push_back(&shader_resources.primitive_struct);

    const auto& config = Component::get<erhe::application::Configuration>();
    if (config->shadow_renderer.enabled)
    {
        create_info.defines.emplace_back("ERHE_SHADOW_MAPS", "1");
    }
    if (config->graphics.simpler_shaders)
    {
        create_info.defines.emplace_back("ERHE_SIMPLER_SHADERS", "1");
    }

    if (erhe::graphics::Instance::info.use_bindless_texture)
    {
        create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
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

    Shader_stages::Prototype prototype{create_info};
    if (!prototype.is_valid())
    {
        log_programs->error("current directory is {}", fs::current_path().string());
        log_programs->error("Compiling shader program {} failed", create_info.name);
        return {};
    }

    auto p = std::make_unique<Shader_stages>(std::move(prototype));

    if (m_shader_monitor)
    {
        m_shader_monitor->add(create_info, p.get());
    }

    return p;
}

}
